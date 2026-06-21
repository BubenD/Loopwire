#include "geometry/Geometry.h"
#include "geometry/Vec2.h"

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

    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
