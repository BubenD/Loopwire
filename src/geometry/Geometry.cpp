#include "geometry/Geometry.h"

#include <algorithm>

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

} // namespace Edraw_Desktop
