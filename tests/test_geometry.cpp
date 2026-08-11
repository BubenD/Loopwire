#include "geometry/Geometry.h"
#include "geometry/Vec2.h"
#include "model/Entity.h"

#include <cmath>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {
int g_fail = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  [FAIL] %s\n", what); ++g_fail; }
}
bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }
} // namespace

int run_geometry_tests() {
    g_fail = 0;
    std::printf("geometry tests:\n");

    // Vector basics.
    check(near((Vec2{3, 4}).length(), 5.0), "vec length");
    check(near(dot({1, 0}, {0, 1}), 0.0), "dot perpendicular");
    check(near(cross({1, 0}, {0, 1}), 1.0), "cross unit");

    // Closest point on a segment.
    auto c = closestPointOnSegment({5, 5}, {0, 0}, {10, 0});
    check(near(c.point.x, 5.0) && near(c.point.y, 0.0), "closest mid");
    check(near(c.distance, 5.0), "closest dist");

    // Line intersection.
    auto x = lineLineIntersection({0, 0}, {10, 0}, {5, -5}, {5, 5});
    check(x.has_value() && near(x->x, 5.0) && near(x->y, 0.0), "line intersection");

    // Parallel lines -> none.
    auto par = lineLineIntersection({0, 0}, {10, 0}, {0, 1}, {10, 1});
    check(!par.has_value(), "parallel none");

    // Segment intersection that crosses.
    auto s = segmentSegmentIntersection({0, 0}, {10, 10}, {0, 10}, {10, 0});
    check(s.has_value() && near(s->x, 5.0) && near(s->y, 5.0), "segment cross");

    // Bounds expansion.
    Bounds b;
    b.expand({1, 2});
    b.expand({-3, 5});
    check(near(b.min.x, -3.0) && near(b.max.y, 5.0), "bounds expand");

    {
        Entity ellipse;
        ellipse.type = EntityType::Ellipse;
        ellipse.center = {10.0, -4.0};
        ellipse.radius = 10.0;
        ellipse.radiusY = 30.0;
        ellipse.rotation = M_PI / 2.0;
        const Bounds eb = ellipse.bounds();
        check(eb.valid && near(eb.min.x, -20.0) && near(eb.max.x, 40.0) &&
                  near(eb.min.y, -14.0) && near(eb.max.y, 6.0),
              "rotated ellipse bounds");
    }

    // Spline: a smooth curve that passes through its control points.
    {
        std::vector<Vec2> ctrl = {{0, 0}, {10, 10}, {20, 0}};
        auto curve = splinePoints(ctrl, 8);
        check(curve.size() > ctrl.size(), "spline: tessellated into many points");
        check(near(curve.front().x, 0.0) && near(curve.front().y, 0.0) &&
                  near(curve.back().x, 20.0) && near(curve.back().y, 0.0),
              "spline: passes through the endpoints");
        // Fewer than 3 points: returned unchanged.
        std::vector<Vec2> two = {{0, 0}, {5, 5}};
        check(splinePoints(two).size() == 2, "spline: <3 points unchanged");

        // CV B-spline: clamped to the endpoints, but pulled toward (not through) the
        // interior control point.
        auto cv = bsplinePoints(ctrl, 8);
        check(cv.size() > ctrl.size(), "bspline: tessellated");
        check(near(cv.front().x, 0.0) && near(cv.front().y, 0.0) &&
                  near(cv.back().x, 20.0) && near(cv.back().y, 0.0),
              "bspline: clamped to the endpoints");
        // The apex stays below the control point (10,10) since CV doesn't interpolate it.
        double maxY = 0;
        for (const auto& q : cv) maxY = std::max(maxY, q.y);
        check(maxY < 10.0 - 1e-3, "bspline: does not pass through interior control points");
    }

    // Line/circle intersection helpers (used by tangent-circle construction).
    {
        auto lc = lineCircleIntersection({-50, 0}, {1, 0}, {0, 0}, 10); // x-axis through origin circle r=10
        check(lc.size() == 2, "lineCircle: secant -> 2 points");
        auto cc = circleCircleIntersection({0, 0}, 5, {8, 0}, 5); // overlap
        check(cc.size() == 2 && near(cc[0].x, 4.0), "circleCircle: 2 points at x=4");
        auto none = circleCircleIntersection({0, 0}, 1, {100, 0}, 1);
        check(none.empty(), "circleCircle: far apart -> none");
    }

    // Polyline arc bulges.
    {
        auto arc = bulgeArcPoints({0, 0}, {10, 0}, 1.0); // bulge 1 => semicircle, r=5
        check(arc.size() > 2 && near(arc.front().x, 0.0) && near(arc.back().x, 10.0) &&
                  near(arc.back().y, 0.0),
              "bulge: arc runs from p0 to p1");
        double maxd = 0;
        for (const auto& q : arc) maxd = std::max(maxd, std::abs(q.y));
        check(near(maxd, 5.0, 0.1), "bulge=1 -> semicircle of radius 5");
        check(near(tangentBulge({0, 0}, {10, 0}, {1, 0}), 0.0, 1e-9),
              "tangentBulge: chord along the tangent -> straight");
        check(near(tangentBulge({0, 0}, {0, 10}, {1, 0}), 1.0, 1e-6),
              "tangentBulge: 90-degree turn -> bulge 1");
        auto flat = expandBulgePolyline({{0, 0}, {10, 0}, {10, 10}}, {0, 0, 0});
        check(flat.size() == 3, "expandBulge: no bulges -> points unchanged");
        auto curved = expandBulgePolyline({{0, 0}, {10, 0}}, {0, 1.0});
        check(curved.size() > 2, "expandBulge: a bulged segment is tessellated");
    }

    // traceEnclosingLoop: find a closed region built from separate segments.
    {
        // A square made of FOUR separate line segments (not one closed entity).
        std::vector<std::pair<Vec2, Vec2>> segs = {
            {{0, 0}, {10, 0}}, {{10, 0}, {10, 10}}, {{10, 10}, {0, 10}}, {{0, 10}, {0, 0}}};
        auto loop = traceEnclosingLoop(segs, {5, 5});
        check(loop && loop->size() == 4, "trace: square from 4 separate lines -> 4-pt loop");
        // A point OUTSIDE the square encloses nothing.
        check(!traceEnclosingLoop(segs, {20, 5}), "trace: outside point -> no loop");

        // Region bounded by overlapping/crossing lines (edges cross, not just meet):
        // two long crossing lines plus a top and bottom make a small central cell.
        std::vector<std::pair<Vec2, Vec2>> crs = {
            {{0, 0}, {10, 0}},   // bottom
            {{0, 8}, {10, 8}},   // top
            {{2, -2}, {2, 10}},  // left vertical (overshoots)
            {{8, -2}, {8, 10}}}; // right vertical (overshoots)
        auto cell = traceEnclosingLoop(crs, {5, 4});
        check(cell && cell->size() == 4, "trace: cell from overshooting crossing lines");
    }

    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
