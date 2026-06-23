#pragma once

#include "geometry/Vec2.h"
#include <optional>
#include <vector>

namespace Edraw_Desktop {

// Axis-aligned bounding box in world coordinates.
struct Bounds {
    Vec2 min;
    Vec2 max;
    bool valid = false;

    void expand(const Vec2& p);
    void expand(const Bounds& b);
    Vec2 center() const { return (min + max) * 0.5; }
    double width() const { return max.x - min.x; }
    double height() const { return max.y - min.y; }
};

Bounds boundsOf(const std::vector<Vec2>& pts);

// Nearest point on the finite segment [a,b] to p; returns the point and the
// parameter t in [0,1].
struct ClosestOnSegment {
    Vec2 point;
    double t;
    double distance;
};
ClosestOnSegment closestPointOnSegment(const Vec2& p, const Vec2& a, const Vec2& b);

// Intersection of two infinite lines through (a1,a2) and (b1,b2).
// std::nullopt when parallel.
std::optional<Vec2> lineLineIntersection(const Vec2& a1, const Vec2& a2,
                                          const Vec2& b1, const Vec2& b2);

// Intersection of two finite segments, if they actually cross.
std::optional<Vec2> segmentSegmentIntersection(const Vec2& a1, const Vec2& a2,
                                                const Vec2& b1, const Vec2& b2);

// Smallest signed difference target-current wrapped to (-pi, pi].
double shortAngleDelta(double current, double target);

// Offset a polyline sideways by `dist` (perpendicular; sign picks the side),
// mitering interior vertices. Used to draw double polylines.
std::vector<Vec2> offsetPolyline(const std::vector<Vec2>& pts, double dist);

// Replace each interior corner of a polyline with a tangent fillet arc of the
// given radius (arc approximated by line segments). radius <= 0 returns the
// points unchanged. The setback is clamped to neighbouring segment lengths.
std::vector<Vec2> roundedPolyline(const std::vector<Vec2>& pts, double radius,
                                  int arcSegments = 10);

// Point on a circle/arc at the given angle (radians).
inline Vec2 pointOnCircle(const Vec2& center, double radius, double angle) {
    return {center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)};
}

// Smooth interpolating spline (Catmull-Rom) PASSING THROUGH the control points —
// the "fit" spline. Returns a finely sampled polyline (the control points are
// included). `samplesPerSegment` controls smoothness. <3 points: unchanged.
std::vector<Vec2> splinePoints(const std::vector<Vec2>& pts, int samplesPerSegment = 16);

// Clamped uniform cubic B-spline shaped by the control points (the "CV" spline):
// it is pulled toward, but does NOT pass through, the interior control points; the
// ends are clamped so it starts/ends at the first/last control point. <3 points:
// unchanged.
std::vector<Vec2> bsplinePoints(const std::vector<Vec2>& ctrl, int samplesPerSegment = 16);

} // namespace Edraw_Desktop
