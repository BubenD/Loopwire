#include "render/Render3D.h"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Edraw_Desktop {

V3 Camera3D::eye() const {
    const double y = yawDeg * M_PI / 180.0, p = pitchDeg * M_PI / 180.0;
    return target + V3{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)} * distance;
}

void Camera3D::axes(V3& right, V3& up, V3& forward) const {
    forward = (target - eye()).normalized();
    const V3 worldUp{0, 0, 1};
    right = cross3(forward, worldUp).normalized();
    if (right.length() < 0.5) right = V3{1, 0, 0};
    up = cross3(right, forward).normalized();
}

void Camera3D::fit(const V3& lo, const V3& hi, double aspect) {
    target = (lo + hi) * 0.5;
    const double radius = std::max(1.0, (hi - lo).length() * 0.5);
    distance = radius / std::sin(fovDeg * M_PI / 360.0) * 1.08;
    // CLOSER, until the box's corners - not the sphere round it - just fill the picture: a long harness seen from an
    // angle fills it, rather than sitting small in the middle of a sphere's worth of room.
    const double tanH = std::tan(fovDeg * M_PI / 360.0);
    aspect = std::max(0.2, aspect);
    for (int pass = 0; pass < 6; ++pass) {
        V3 right, up, forward;
        axes(right, up, forward);
        const V3 from = eye();
        double worst = 0.0;
        for (int i = 0; i < 8; ++i) {
            const V3 c{(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z};
            const V3 d = c - from;
            const double z = std::max(1e-6, dot3(d, forward));
            worst = std::max({worst, std::abs(dot3(d, right) / z) / (tanH * aspect), std::abs(dot3(d, up) / z) / tanH});
        }
        if (worst <= 1e-9) break;
        const double next = distance * worst / 0.9;
        // Never inside the box: from its middle at least its radius off.
        distance = std::max(radius * 1.05, next);
        if (std::abs(worst - 0.9) < 0.01) break;
    }
}

void Camera3D::rayAt(double px, double py, int w, int h, V3& origin, V3& dir) const {
    V3 right, up, forward;
    axes(right, up, forward);
    const double focal = 0.5 * std::max(1, h) / std::tan(fovDeg * M_PI / 360.0);
    origin = eye();
    dir = (forward + right * ((px - w * 0.5) / focal) - up * ((py - h * 0.5) / focal)).normalized();
}

void renderScene3D(QImage& image, const std::vector<DrawItem3D>& items, const Camera3D& cam, bool wireframe,
                   const QColor& background, const Section3D& section) {
    image.fill(background);
    const int w = image.width(), h = image.height();
    if (w <= 0 || h <= 0) return;
    V3 right, up, forward;
    cam.axes(right, up, forward);
    const V3 eye = cam.eye();
    const double focal = 0.5 * h / std::tan(cam.fovDeg * M_PI / 360.0);
    const double nearZ = cam.nearPlane();
    // THE LIGHT: from over the viewer's shoulder, up and to the left, with some all round so nothing is black.
    const V3 light = (forward * -1.0 + up * 0.6 + right * -0.35).normalized();
    struct P {
        double x, y, z;
        bool ok;
    };
    auto project = [&](const V3& v) {
        const V3 d = v - eye;
        const double z = dot3(d, forward);
        if (z < nearZ) return P{0, 0, z, false};
        return P{w * 0.5 + focal * dot3(d, right) / z, h * 0.5 - focal * dot3(d, up) / z, z, true};
    };
    if (wireframe) {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing, true);
        for (const DrawItem3D& it : items) {
            if (!it.mesh) continue;
            QColor pen = it.color.lighter(130);
            pen.setAlphaF(static_cast<float>(std::clamp(it.alpha, 0.0, 1.0)));
            p.setPen(QPen(pen, 1.0));
            std::vector<P> pts;
            pts.reserve(it.mesh->v.size());
            for (const V3& v : it.mesh->v) pts.push_back(project(v));
            for (const auto& t : it.mesh->f) {
                if (it.sectioned && section.on) {
                    const V3 mid = (it.mesh->v[static_cast<size_t>(t[0])] + it.mesh->v[static_cast<size_t>(t[1])] +
                                    it.mesh->v[static_cast<size_t>(t[2])]) / 3.0;
                    if (section.cuts(mid)) continue;
                }
                for (int k = 0; k < 3; ++k) {
                    const P& a = pts[static_cast<size_t>(t[static_cast<size_t>(k)])];
                    const P& b = pts[static_cast<size_t>(t[static_cast<size_t>((k + 1) % 3)])];
                    if (a.ok && b.ok) p.drawLine(QPointF(a.x, a.y), QPointF(b.x, b.y));
                }
            }
        }
        return;
    }
    std::vector<float> depth(static_cast<size_t>(w) * static_cast<size_t>(h), std::numeric_limits<float>::max());
    auto* bits = reinterpret_cast<QRgb*>(image.bits());
    const int stride = image.bytesPerLine() / 4;
    // SOLID FIRST, THEN SEE-THROUGH over it: a see-through item blends with what is behind it, and hides nothing.
    std::vector<const DrawItem3D*> order;
    for (const DrawItem3D& it : items)
        if (it.mesh && it.alpha >= 0.999) order.push_back(&it);
    for (const DrawItem3D& it : items)
        if (it.mesh && it.alpha < 0.999 && it.alpha > 0.001) order.push_back(&it);
    for (const DrawItem3D* item : order) {
        const DrawItem3D& it = *item;
        const bool blend = it.alpha < 0.999;
        const double keep = 1.0 - it.alpha;
        const bool cut = it.sectioned && section.on;
        std::vector<P> pts;
        pts.reserve(it.mesh->v.size());
        for (const V3& v : it.mesh->v) pts.push_back(project(v));
        const double cr = it.color.redF(), cg = it.color.greenF(), cb = it.color.blueF();
        for (const auto& t : it.mesh->f) {
            const P& a = pts[static_cast<size_t>(t[0])];
            const P& b = pts[static_cast<size_t>(t[1])];
            const P& c = pts[static_cast<size_t>(t[2])];
            if (!a.ok || !b.ok || !c.ok) continue;
            // LIT BY HOW SQUARELY IT FACES THE LIGHT, either side of it (a model's faces are not always wound alike).
            const V3& va = it.mesh->v[static_cast<size_t>(t[0])];
            const V3 n = cross3(it.mesh->v[static_cast<size_t>(t[1])] - va, it.mesh->v[static_cast<size_t>(t[2])] - va).normalized();
            const double lit = 0.28 + 0.72 * std::abs(dot3(n, light));
            const QRgb colour = qRgb(std::clamp(static_cast<int>(cr * lit * 255.0), 0, 255),
                                     std::clamp(static_cast<int>(cg * lit * 255.0), 0, 255),
                                     std::clamp(static_cast<int>(cb * lit * 255.0), 0, 255));
            const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (std::abs(area) < 1e-9) continue;
            const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
            const int x1 = std::min(w - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
            const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
            const int y1 = std::min(h - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
            if (x0 > x1 || y0 > y1) continue;
            // Depth across it by 1/z, which is what stays straight across a perspective picture.
            const double ia = 1.0 / a.z, ib = 1.0 / b.z, ic = 1.0 / c.z;
            for (int y = y0; y <= y1; ++y) {
                const double py = y + 0.5;
                for (int x = x0; x <= x1; ++x) {
                    const double px = x + 0.5;
                    double wa = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
                    double wb = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
                    double wc = 1.0 - wa - wb;
                    if (wa < -1e-6 || wb < -1e-6 || wc < -1e-6) continue;
                    const float z = static_cast<float>(1.0 / (wa * ia + wb * ib + wc * ic));
                    float& d = depth[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
                    if (z >= d) continue;
                    // CUT AWAY beyond the section: the point this pixel shows, back from its depth along its ray.
                    if (cut) {
                        const V3 at = eye + (forward + right * ((px - w * 0.5) / focal) - up * ((py - h * 0.5) / focal)) * z;
                        if (section.cuts(at)) continue;
                    }
                    if (blend) {
                        QRgb& under = bits[y * stride + x];
                        under = qRgb(static_cast<int>(qRed(colour) * it.alpha + qRed(under) * keep),
                                     static_cast<int>(qGreen(colour) * it.alpha + qGreen(under) * keep),
                                     static_cast<int>(qBlue(colour) * it.alpha + qBlue(under) * keep));
                        continue;
                    }
                    d = z;
                    bits[y * stride + x] = colour;
                }
            }
        }
    }
}

} // namespace Edraw_Desktop
