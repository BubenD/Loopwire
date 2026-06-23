#include "geometry/Geometry.h"

#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

void Bounds::expand(const Vec2& p) {
    if (!valid) {
        min = max = p;
        valid = true;
        return;
    }
    min.x = std::min(min.x, p.x);
    min.y = std::min(min.y, p.y);
    max.x = std::max(max.x, p.x);
    max.y = std::max(max.y, p.y);
}

void Bounds::expand(const Bounds& b) {
    if (!b.valid) return;
    expand(b.min);
    expand(b.max);
}

Bounds boundsOf(const std::vector<Vec2>& pts) {
    Bounds b;
    for (const auto& p : pts) b.expand(p);
    return b;
}

ClosestOnSegment closestPointOnSegment(const Vec2& p, const Vec2& a, const Vec2& b) {
    const Vec2 ab = b - a;
    const double lenSq = ab.lengthSq();
    double t = 0.0;
    if (lenSq > 1e-12) {
        t = dot(p - a, ab) / lenSq;
        t = std::clamp(t, 0.0, 1.0);
    }
    const Vec2 point = a + ab * t;
    return {point, t, distance(p, point)};
}

std::optional<Vec2> lineLineIntersection(const Vec2& a1, const Vec2& a2,
                                         const Vec2& b1, const Vec2& b2) {
    const Vec2 d1 = a2 - a1;
    const Vec2 d2 = b2 - b1;
    const double denom = cross(d1, d2);
    if (std::abs(denom) < 1e-12) return std::nullopt; // parallel
    const double t = cross(b1 - a1, d2) / denom;
    return a1 + d1 * t;
}

std::optional<Vec2> segmentSegmentIntersection(const Vec2& a1, const Vec2& a2,
                                               const Vec2& b1, const Vec2& b2) {
    const Vec2 d1 = a2 - a1;
    const Vec2 d2 = b2 - b1;
    const double denom = cross(d1, d2);
    if (std::abs(denom) < 1e-12) return std::nullopt;
    const double t = cross(b1 - a1, d2) / denom;
    const double u = cross(b1 - a1, d1) / denom;
    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) return std::nullopt;
    return a1 + d1 * t;
}

double shortAngleDelta(double current, double target) {
    double d = target - current;
    constexpr double kTwoPi = 2.0 * 3.14159265358979323846;
    while (d <= -kTwoPi / 2.0) d += kTwoPi;
    while (d > kTwoPi / 2.0) d -= kTwoPi;
    return d;
}

std::vector<Vec2> offsetPolyline(const std::vector<Vec2>& pts, double dist) {
    const int n = static_cast<int>(pts.size());
    if (n < 2) return pts;

    // Offset each segment perpendicular by `dist`.
    std::vector<Vec2> segA(n - 1), segB(n - 1);
    for (int i = 0; i < n - 1; ++i) {
        const Vec2 off = (pts[i + 1] - pts[i]).normalized().perp() * dist;
        segA[i] = pts[i] + off;
        segB[i] = pts[i + 1] + off;
    }

    // Vertices = ends of the offset chain, with mitered interior joints.
    std::vector<Vec2> out;
    out.reserve(n);
    out.push_back(segA[0]);
    for (int i = 1; i < n - 1; ++i) {
        const auto x = lineLineIntersection(segA[i - 1], segB[i - 1], segA[i], segB[i]);
        out.push_back(x.value_or(segB[i - 1])); // parallel -> just use the seam point
    }
    out.push_back(segB[n - 2]);
    return out;
}

std::vector<Vec2> roundedPolyline(const std::vector<Vec2>& pts, double radius,
                                  int arcSegments) {
    const int n = static_cast<int>(pts.size());
    if (n < 3 || radius <= 0.0) return pts;
    constexpr double kPi = 3.14159265358979323846;

    std::vector<Vec2> out;
    out.push_back(pts[0]);
    for (int i = 1; i < n - 1; ++i) {
        const Vec2 p0 = pts[i - 1], p1 = pts[i], p2 = pts[i + 1];
        const Vec2 d1 = (p0 - p1).normalized(); // corner -> previous vertex
        const Vec2 d2 = (p2 - p1).normalized(); // corner -> next vertex
        const double cosT = std::clamp(dot(d1, d2), -1.0, 1.0);
        const double theta = std::acos(cosT);   // interior angle at the corner
        if (theta < 1e-3 || theta > kPi - 1e-3) { out.push_back(p1); continue; }

        double t = radius / std::tan(theta / 2.0); // tangent setback from corner
        const double maxT = 0.5 * std::min(distance(p0, p1), distance(p1, p2));
        t = std::min(t, maxT);
        const double r = t * std::tan(theta / 2.0); // radius after clamping

        const Vec2 t1 = p1 + d1 * t;            // tangent point toward previous
        const Vec2 t2 = p1 + d2 * t;            // tangent point toward next
        const Vec2 bis = (d1 + d2).normalized();
        const Vec2 c = p1 + bis * (r / std::sin(theta / 2.0)); // arc centre

        double a1 = std::atan2(t1.y - c.y, t1.x - c.x);
        const double a2 = std::atan2(t2.y - c.y, t2.x - c.x);
        double sweep = a2 - a1;
        while (sweep <= -kPi) sweep += 2 * kPi;
        while (sweep > kPi) sweep -= 2 * kPi;

        out.push_back(t1);
        for (int s = 1; s < arcSegments; ++s) {
            const double a = a1 + sweep * s / arcSegments;
            out.push_back({c.x + r * std::cos(a), c.y + r * std::sin(a)});
        }
        out.push_back(t2);
    }
    out.push_back(pts[n - 1]);
    return out;
}

std::vector<Vec2> splinePoints(const std::vector<Vec2>& pts, int samplesPerSegment) {
    const int n = static_cast<int>(pts.size());
    if (n < 3) return pts;
    const int seg = std::max(2, samplesPerSegment);
    std::vector<Vec2> out;
    out.reserve(static_cast<size_t>((n - 1) * seg + 1));
    // Catmull-Rom through every point; duplicate the ends so the curve reaches them.
    auto at = [&](int i) -> Vec2 { return pts[std::clamp(i, 0, n - 1)]; };
    out.push_back(pts[0]);
    for (int i = 0; i < n - 1; ++i) {
        const Vec2 p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
        for (int s = 1; s <= seg; ++s) {
            const double t = static_cast<double>(s) / seg;
            const double t2 = t * t, t3 = t2 * t;
            // Catmull-Rom basis (tension 0.5).
            const double b0 = -0.5 * t3 + t2 - 0.5 * t;
            const double b1 = 1.5 * t3 - 2.5 * t2 + 1.0;
            const double b2 = -1.5 * t3 + 2.0 * t2 + 0.5 * t;
            const double b3 = 0.5 * t3 - 0.5 * t2;
            out.push_back({p0.x * b0 + p1.x * b1 + p2.x * b2 + p3.x * b3,
                           p0.y * b0 + p1.y * b1 + p2.y * b2 + p3.y * b3});
        }
    }
    return out;
}

std::vector<Vec2> bsplinePoints(const std::vector<Vec2>& ctrl, int samplesPerSegment) {
    const int n = static_cast<int>(ctrl.size());
    if (n < 3) return ctrl;
    const int seg = std::max(2, samplesPerSegment);
    // Clamp the ends by tripling the first and last control points so the curve
    // starts and ends exactly on them (uniform cubic B-spline otherwise floats).
    std::vector<Vec2> p;
    p.reserve(n + 4);
    p.push_back(ctrl.front());
    p.push_back(ctrl.front());
    for (const auto& c : ctrl) p.push_back(c);
    p.push_back(ctrl.back());
    p.push_back(ctrl.back());
    std::vector<Vec2> out;
    out.push_back(ctrl.front());
    for (size_t i = 0; i + 3 < p.size(); ++i) {
        for (int s = 1; s <= seg; ++s) {
            const double t = static_cast<double>(s) / seg;
            const double t2 = t * t, t3 = t2 * t;
            const double ompt = 1.0 - t;
            const double b0 = ompt * ompt * ompt / 6.0;
            const double b1 = (3.0 * t3 - 6.0 * t2 + 4.0) / 6.0;
            const double b2 = (-3.0 * t3 + 3.0 * t2 + 3.0 * t + 1.0) / 6.0;
            const double b3 = t3 / 6.0;
            out.push_back({p[i].x * b0 + p[i + 1].x * b1 + p[i + 2].x * b2 + p[i + 3].x * b3,
                           p[i].y * b0 + p[i + 1].y * b1 + p[i + 2].y * b2 + p[i + 3].y * b3});
        }
    }
    return out;
}

std::vector<Vec2> lineCircleIntersection(const Vec2& p, const Vec2& d,
                                         const Vec2& c, double r) {
    std::vector<Vec2> out;
    const double len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len < 1e-12) return out;
    const Vec2 u{d.x / len, d.y / len};
    const double proj = (c.x - p.x) * u.x + (c.y - p.y) * u.y;
    const Vec2 foot{p.x + u.x * proj, p.y + u.y * proj};
    const double fx = c.x - foot.x, fy = c.y - foot.y;
    const double dist2 = fx * fx + fy * fy;
    if (dist2 > r * r + 1e-7) return out;
    const double h = std::sqrt(std::max(0.0, r * r - dist2));
    out.push_back({foot.x + u.x * h, foot.y + u.y * h});
    if (h > 1e-9) out.push_back({foot.x - u.x * h, foot.y - u.y * h});
    return out;
}

std::vector<Vec2> bulgeArcPoints(const Vec2& p0, const Vec2& p1, double bulge, int samples) {
    if (std::abs(bulge) < 1e-9) return {p0, p1};
    const double dx = p1.x - p0.x, dy = p1.y - p0.y;
    const double chord = std::sqrt(dx * dx + dy * dy);
    if (chord < 1e-12) return {p0};
    const double delta = 4.0 * std::atan(bulge); // signed central angle
    const double half = delta / 2.0;
    if (std::abs(std::tan(half)) < 1e-9) return {p0, p1};
    const Vec2 mid{(p0.x + p1.x) * 0.5, (p0.y + p1.y) * 0.5};
    const Vec2 n{-dy / chord, dx / chord}; // left normal of the chord
    const double dist = (chord / 2.0) / std::tan(half);
    const Vec2 c{mid.x + n.x * dist, mid.y + n.y * dist};
    const double r = std::sqrt((p0.x - c.x) * (p0.x - c.x) + (p0.y - c.y) * (p0.y - c.y));
    const double a0 = std::atan2(p0.y - c.y, p0.x - c.x);
    const double pi = std::acos(-1.0);
    const int steps = std::max(4, static_cast<int>(std::ceil(std::abs(delta) / (pi / 24.0))));
    std::vector<Vec2> out;
    out.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const double a = a0 + delta * (static_cast<double>(i) / steps);
        out.push_back({c.x + r * std::cos(a), c.y + r * std::sin(a)});
    }
    return out;
}

double tangentBulge(const Vec2& p0, const Vec2& p1, const Vec2& tangent) {
    const double cx = p1.x - p0.x, cy = p1.y - p0.y;
    const double clen = std::sqrt(cx * cx + cy * cy);
    const double tlen = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y);
    if (clen < 1e-12 || tlen < 1e-12) return 0.0;
    const Vec2 ch{cx / clen, cy / clen};
    const Vec2 t{tangent.x / tlen, tangent.y / tlen};
    // Signed angle from the tangent to the chord; the tangent-arc bulge is tan(angle/2).
    const double alpha = std::atan2(t.x * ch.y - t.y * ch.x, t.x * ch.x + t.y * ch.y);
    return std::tan(alpha / 2.0);
}

std::vector<Vec2> expandBulgePolyline(const std::vector<Vec2>& points,
                                      const std::vector<double>& bulges) {
    std::vector<Vec2> out;
    if (points.empty()) return out;
    out.push_back(points[0]);
    for (size_t i = 1; i < points.size(); ++i) {
        const double b = (i < bulges.size()) ? bulges[i] : 0.0;
        if (std::abs(b) > 1e-9) {
            const auto arc = bulgeArcPoints(points[i - 1], points[i], b);
            for (size_t k = 1; k < arc.size(); ++k) out.push_back(arc[k]);
        } else {
            out.push_back(points[i]);
        }
    }
    return out;
}

std::vector<Vec2> circleCircleIntersection(const Vec2& c1, double r1,
                                           const Vec2& c2, double r2) {
    std::vector<Vec2> out;
    const double dx = c2.x - c1.x, dy = c2.y - c1.y;
    const double d = std::sqrt(dx * dx + dy * dy);
    if (d < 1e-9 || d > r1 + r2 + 1e-7 || d < std::abs(r1 - r2) - 1e-7) return out;
    const double a = (r1 * r1 - r2 * r2 + d * d) / (2.0 * d);
    const double h = std::sqrt(std::max(0.0, r1 * r1 - a * a));
    const Vec2 mid{c1.x + dx * a / d, c1.y + dy * a / d};
    const Vec2 perp{-dy / d * h, dx / d * h};
    out.push_back({mid.x + perp.x, mid.y + perp.y});
    if (h > 1e-9) out.push_back({mid.x - perp.x, mid.y - perp.y});
    return out;
}

} // namespace Edraw_Desktop
