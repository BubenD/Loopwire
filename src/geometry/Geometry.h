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

// Like roundedPolyline but with an independent radius per interior corner.
// radii[i] is the fillet radius at vertex i (0 = sharp); entries [0] and the last
// (the endpoints) are ignored. Each arc is tangent to both adjoining straight runs.
std::vector<Vec2> roundedPolylinePerCorner(const std::vector<Vec2>& pts,
                                           const std::vector<double>& radii,
                                           int arcSegments = 10);

// Like roundedPolylinePerCorner but for a CLOSED polygon: EVERY vertex is a corner
// (the loop wraps), so rects/polygons can have per-corner fillets. Returns the
// rounded closed outline (not repeating the first point).
std::vector<Vec2> roundedPolygonPerCorner(const std::vector<Vec2>& pts,
                                          const std::vector<double>& radii,
                                          int arcSegments = 10);

// Trace the smallest closed loop of edges that encloses `seed`, given a soup of
// boundary segments (from any number of separate entities). The segments are split
// at their mutual intersections, built into a planar graph, and the face containing
// the seed is walked (ray-cast + left-hand rule). Returns the loop's vertices
// (CCW, not repeating the first point) or nullopt if the seed isn't enclosed.
std::optional<std::vector<Vec2>> traceEnclosingLoop(
    const std::vector<std::pair<Vec2, Vec2>>& segments, const Vec2& seed);

// True if `radius` is small enough to fit (tangent setback within the segment
// lengths) at every interior corner of the polyline. radius <= 0 always fits.
bool filletRadiusFits(const std::vector<Vec2>& pts, double radius);

// Point on a circle/arc at the given angle (radians).
inline Vec2 pointOnCircle(const Vec2& center, double radius, double angle) {
    return {center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)};
}

// Intersections of an infinite line (point + direction) with a circle (0/1/2).
std::vector<Vec2> lineCircleIntersection(const Vec2& p, const Vec2& d,
                                         const Vec2& c, double r);

// Intersections of two circles (0/1/2 points).
std::vector<Vec2> circleCircleIntersection(const Vec2& c1, double r1,
                                           const Vec2& c2, double r2);

// Arc between two points expressed as a polyline "bulge" = tan(included_angle/4)
// (CCW positive; |bulge|<eps => straight). Returns the tessellated arc from p0 to
// p1 (both endpoints included).
std::vector<Vec2> bulgeArcPoints(const Vec2& p0, const Vec2& p1, double bulge,
                                 int samples = 24);

// Bulge for an arc that LEAVES p0 along `tangent` (unit direction of travel) and
// ends at p1 — used to draw polyline arc segments tangent to the previous one.
double tangentBulge(const Vec2& p0, const Vec2& p1, const Vec2& tangent);

// Expand a polyline whose segments may be arcs (bulges[i] = bulge of the segment
// ENDING at point i; index 0 unused) into a flat tessellated point list.
std::vector<Vec2> expandBulgePolyline(const std::vector<Vec2>& points,
                                      const std::vector<double>& bulges);

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
