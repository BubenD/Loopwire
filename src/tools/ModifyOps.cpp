#include "tools/ModifyOps.h"

#include "geometry/Geometry.h"
#include "harness/Harness.h"
#include "tools/EntityTransforms.h"

#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Edraw_Desktop {

namespace {

QString newId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

// Which endpoint of the line is nearer the given point? Returns reference index
// (0 -> a, 1 -> b) of the NEAR endpoint and sets far accordingly.
void nearFar(const Entity& line, const Vec2& p, Vec2& nearPt, Vec2& farPt,
             bool& nearIsA) {
    if (distance(line.a, p) <= distance(line.b, p)) { nearPt = line.a; farPt = line.b; nearIsA = true; }
    else { nearPt = line.b; farPt = line.a; nearIsA = false; }
}

void setNear(Entity& line, bool nearIsA, const Vec2& v) {
    if (nearIsA) line.a = v; else line.b = v;
}

// Gather all segment-segment intersection params of `line` with `others`.
std::vector<Vec2> lineIntersections(const Entity& line,
                                    const std::vector<Entity>& others) {
    std::vector<Vec2> out;
    auto addSeg = [&](const Vec2& a, const Vec2& b) {
        if (auto x = segmentSegmentIntersection(line.a, line.b, a, b)) out.push_back(*x);
    };
    for (const auto& e : others) {
        if (&e == &line) continue;
        switch (e.type) {
        case EntityType::Line:
            addSeg(e.a, e.b);
            break;
        case EntityType::DoubleLine: {
            // Use the two VISIBLE physical lines, not the centerline, so a trim
            // stops at the physical line (not in the gap between them).
            const double gap = e.meta.value("gap").toDouble(10.0);
            const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
            addSeg(e.a + off, e.b + off);
            addSeg(e.a - off, e.b - off);
            break;
        }
        case EntityType::Polyline:
            // A DOUBLE polyline cuts on its two physical runs, not the centerline.
            if (e.meta.value("double").toBool() && e.points.size() >= 2) {
                const double gap = e.meta.value("gap").toDouble(10.0);
                for (double s : {gap / 2.0, -gap / 2.0}) {
                    const auto run = offsetPolyline(e.points, s);
                    for (size_t i = 1; i < run.size(); ++i) addSeg(run[i - 1], run[i]);
                }
                break;
            }
            for (size_t i = 1; i < e.points.size(); ++i) addSeg(e.points[i - 1], e.points[i]);
            break;
        case EntityType::Spline:
        case EntityType::Wire:
            for (size_t i = 1; i < e.points.size(); ++i) addSeg(e.points[i - 1], e.points[i]);
            break;
        case EntityType::Rect:
        case EntityType::Polygon:
            for (size_t i = 0; i < e.points.size(); ++i)
                addSeg(e.points[i], e.points[(i + 1) % e.points.size()]);
            break;
        default:
            break;
        }
    }
    return out;
}

} // namespace

CornerResult filletLines(Entity& l1, Entity& l2, double radius,
                        const Vec2& near1, const Vec2& near2) {
    CornerResult res;
    if (l1.type != EntityType::Line || l2.type != EntityType::Line) return res;
    auto inter = lineLineIntersection(l1.a, l1.b, l2.a, l2.b);
    if (!inter) return res;
    const Vec2 P = *inter;

    Vec2 n1, f1, n2, f2; bool a1, a2;
    nearFar(l1, near1, n1, f1, a1);
    nearFar(l2, near2, n2, f2, a2);

    // The pick marks the kept side; direction runs from the corner toward the
    // picked (kept) endpoint. The corner-side endpoint is trimmed to the tangent.
    const Vec2 u1 = (n1 - P).normalized();
    const Vec2 u2 = (n2 - P).normalized();
    double cosT = std::clamp(dot(u1, u2), -1.0, 1.0);
    const double theta = std::acos(cosT);
    if (theta < 1e-4 || std::abs(theta - M_PI) < 1e-4) return res; // collinear

    // Radius 0 (AutoCAD): no arc — just trim/extend both lines so they meet at the
    // corner P, keeping the picked sides.
    if (radius <= 1e-9) {
        setNear(l1, !a1, P);
        setNear(l2, !a2, P);
        res.hasConnector = false;
        res.ok = true;
        return res;
    }

    // Tangent setback from the corner. If the radius is too large to fit on either
    // kept line, reject it (don't apply) so the caller can warn the user.
    const double d = radius / std::tan(theta / 2.0);
    const double maxD = std::min(distance(P, n1), distance(P, n2));
    if (d > maxD) { res.tooBig = true; return res; }
    const double r = radius;
    const Vec2 T1 = P + u1 * d;
    const Vec2 T2 = P + u2 * d;

    // Move the corner-side (non-picked) endpoint to the tangent point.
    setNear(l1, !a1, T1);
    setNear(l2, !a2, T2);

    // Arc connecting T1->T2.
    const Vec2 bis = (u1 + u2).normalized();
    const double cdist = r / std::sin(theta / 2.0);
    const Vec2 C = P + bis * cdist;
    Entity arc;
    arc.id = newId();
    arc.type = EntityType::Arc;
    arc.layer = l1.layer;
    arc.center = C;
    arc.radius = r;
    double sa = std::atan2(T1.y - C.y, T1.x - C.x);
    double ea = std::atan2(T2.y - C.y, T2.x - C.x);
    // The renderer draws counter-clockwise from sa to ea with a positive sweep
    // (it adds 2π if sweep <= 0). We need the SHORT arc (< π for a fillet).
    // Normalize so that ea - sa is in (0, 2π), then pick the direction that
    // gives the smaller sweep.
    constexpr double kTwoPi = 2.0 * M_PI;
    double sweep = ea - sa;
    while (sweep <= 0.0) sweep += kTwoPi;
    while (sweep > kTwoPi) sweep -= kTwoPi;
    if (sweep > M_PI) {
        // The short arc is the other way: swap start and end.
        std::swap(sa, ea);
    }
    arc.startAngle = sa;
    arc.endAngle = ea;
    res.connector = arc;
    res.ok = true;
    return res;
}

CornerResult chamferLines(Entity& l1, Entity& l2, double d1, double d2,
                         const Vec2& near1, const Vec2& near2) {
    CornerResult res;
    if (l1.type != EntityType::Line || l2.type != EntityType::Line) return res;
    auto inter = lineLineIntersection(l1.a, l1.b, l2.a, l2.b);
    if (!inter) return res;
    const Vec2 P = *inter;

    Vec2 n1, f1, n2, f2; bool a1, a2;
    nearFar(l1, near1, n1, f1, a1);
    nearFar(l2, near2, n2, f2, a2);
    const Vec2 u1 = (n1 - P).normalized();
    const Vec2 u2 = (n2 - P).normalized();
    const Vec2 T1 = P + u1 * d1;
    const Vec2 T2 = P + u2 * d2;

    setNear(l1, !a1, T1);
    setNear(l2, !a2, T2);

    Entity line;
    line.id = newId();
    line.type = EntityType::Line;
    line.layer = l1.layer;
    line.a = T1;
    line.b = T2;
    res.connector = line;
    res.ok = true;
    return res;
}

bool trimLineAt(Entity& line, const Vec2& pick, const std::vector<Entity>& others) {
    if (line.type != EntityType::Line) return false;
    auto xs = lineIntersections(line, others);
    if (xs.empty()) return false;

    // Nearest intersection to the pick point.
    Vec2 best;
    double bestD = std::numeric_limits<double>::max();
    for (const auto& x : xs) {
        const double d = distance(x, pick);
        if (d < bestD) { bestD = d; best = x; }
    }
    // Remove the side of the cut that contains the pick point.
    if (distance(pick, line.a) < distance(pick, line.b)) line.a = best;
    else line.b = best;
    return true;
}

namespace {
// Parameters t in (0,1) along `line` (a..b) where cutting edges in `others` cross
// it. Supports line/polyline/rect/polygon/hatch segments and circles/arcs.
std::vector<double> intersectionParams(const Entity& line, const std::vector<Entity>& others) {
    std::vector<double> ts;
    const Vec2 a = line.a, b = line.b, d = b - a;
    const double L2 = d.lengthSq();
    if (L2 < 1e-12) return ts;
    auto addPt = [&](const Vec2& x) {
        const double t = dot(x - a, d) / L2;
        if (t > 1e-6 && t < 1.0 - 1e-6) ts.push_back(t);
    };
    auto addSeg = [&](const Vec2& p1, const Vec2& p2) {
        if (auto x = segmentSegmentIntersection(a, b, p1, p2)) addPt(*x);
    };
    auto addCircle = [&](const Vec2& c, double r, bool arc, double sa, double ea) {
        for (const auto& x : lineCircleIntersection(a, d, c, r)) {
            const double t = dot(x - a, d) / L2;
            if (t <= 1e-6 || t >= 1.0 - 1e-6) continue;
            if (arc) {
                double sweep = ea - sa;
                while (sweep < 0) sweep += 2.0 * M_PI;
                double rel = std::atan2(x.y - c.y, x.x - c.x) - sa;
                while (rel < 0) rel += 2.0 * M_PI;
                if (rel > sweep + 1e-9) continue; // outside the arc's sweep
            }
            ts.push_back(t);
        }
    };
    for (const auto& e : others) {
        if (&e == &line) continue;
        switch (e.type) {
        case EntityType::Line:
            addSeg(e.a, e.b);
            break;
        case EntityType::DoubleLine: {
            const double gap = e.meta.value("gap").toDouble(10.0);
            const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
            addSeg(e.a + off, e.b + off);
            addSeg(e.a - off, e.b - off);
            break;
        }
        case EntityType::Polyline:
            if (e.meta.value("double").toBool() && e.points.size() >= 2) {
                const double gap = e.meta.value("gap").toDouble(10.0);
                for (double s : {gap / 2.0, -gap / 2.0}) {
                    const auto run = offsetPolyline(e.points, s);
                    for (size_t i = 1; i < run.size(); ++i) addSeg(run[i - 1], run[i]);
                }
                break;
            }
            [[fallthrough]];
        case EntityType::Spline:
        case EntityType::Wire:
            for (size_t i = 1; i < e.points.size(); ++i) addSeg(e.points[i - 1], e.points[i]);
            break;
        case EntityType::Rect:
        case EntityType::Polygon:
        case EntityType::Hatch:
        case EntityType::Wipeout:
            for (size_t i = 0; i < e.points.size(); ++i)
                addSeg(e.points[i], e.points[(i + 1) % e.points.size()]);
            break;
        case EntityType::Circle:
            addCircle(e.center, e.radius, false, 0, 0);
            break;
        case EntityType::Arc:
            addCircle(e.center, e.radius, true, e.startAngle, e.endAngle);
            break;
        default:
            break;
        }
    }
    return ts;
}
} // namespace

namespace {
// True if point x (on circle C,R) lies within an arc's CCW sweep [sa, ea].
bool angleOnArc(const Vec2& x, const Vec2& C, double sa, double ea) {
    double sweep = ea - sa;
    while (sweep < 0) sweep += 2.0 * M_PI;
    double rel = std::atan2(x.y - C.y, x.x - C.x) - sa;
    while (rel < 0) rel += 2.0 * M_PI;
    return rel <= sweep + 1e-9;
}

// Angles (radians) where the cutting edges cross circle (C,R). For arc cutters /
// the trimmed arc, callers filter by sweep separately.
std::vector<double> circleCutAngles(const Vec2& C, double R, const Entity& self,
                                    const std::vector<Entity>& cutters) {
    std::vector<double> angs;
    auto addPt = [&](const Vec2& x) { angs.push_back(std::atan2(x.y - C.y, x.x - C.x)); };
    auto cutSeg = [&](const Vec2& a, const Vec2& b) {
        const Vec2 d = b - a;
        const double L2 = d.lengthSq();
        if (L2 < 1e-12) return;
        for (const auto& x : lineCircleIntersection(a, d, C, R)) {
            const double t = dot(x - a, d) / L2;
            if (t < -1e-9 || t > 1.0 + 1e-9) continue; // outside the cutter segment
            addPt(x);
        }
    };
    for (const auto& e : cutters) {
        if (&e == &self || (!self.id.isEmpty() && e.id == self.id)) continue; // not self
        switch (e.type) {
        case EntityType::Line:
            cutSeg(e.a, e.b);
            break;
        case EntityType::DoubleLine: {
            const double gap = e.meta.value("gap").toDouble(10.0);
            const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
            cutSeg(e.a + off, e.b + off);
            cutSeg(e.a - off, e.b - off);
            break;
        }
        case EntityType::Polyline:
            if (e.meta.value("double").toBool() && e.points.size() >= 2) {
                const double gap = e.meta.value("gap").toDouble(10.0);
                for (double s : {gap / 2.0, -gap / 2.0}) {
                    const auto run = offsetPolyline(e.points, s);
                    for (size_t i = 1; i < run.size(); ++i) cutSeg(run[i - 1], run[i]);
                }
                break;
            }
            for (size_t i = 1; i < e.points.size(); ++i) cutSeg(e.points[i - 1], e.points[i]);
            break;
        case EntityType::Spline:
        case EntityType::Wire:
            for (size_t i = 1; i < e.points.size(); ++i) cutSeg(e.points[i - 1], e.points[i]);
            break;
        case EntityType::Rect:
        case EntityType::Polygon:
            for (size_t i = 0; i < e.points.size(); ++i)
                cutSeg(e.points[i], e.points[(i + 1) % e.points.size()]);
            break;
        case EntityType::Circle:
            for (const auto& x : circleCircleIntersection(e.center, e.radius, C, R)) addPt(x);
            break;
        case EntityType::Arc:
            for (const auto& x : circleCircleIntersection(e.center, e.radius, C, R))
                if (angleOnArc(x, e.center, e.startAngle, e.endAngle)) addPt(x);
            break;
        default:
            break;
        }
    }
    return angs;
}
} // namespace

bool trimCircleAt(Entity& e, const Vec2& pick, const std::vector<Entity>& cutters,
                  std::optional<Entity>& extra) {
    extra.reset();
    if ((e.type != EntityType::Circle && e.type != EntityType::Arc) || e.radius < 1e-9)
        return false;
    const Vec2 C = e.center;
    auto norm = [](double a) {
        while (a < 0) a += 2.0 * M_PI;
        while (a >= 2.0 * M_PI) a -= 2.0 * M_PI;
        return a;
    };
    const double ap = norm(std::atan2(pick.y - C.y, pick.x - C.x));

    if (e.type == EntityType::Circle) {
        std::vector<double> cuts;
        for (double a : circleCutAngles(C, e.radius, e, cutters)) cuts.push_back(norm(a));
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end(),
                               [](double x, double y) { return std::abs(x - y) < 1e-7; }),
                   cuts.end());
        if (cuts.size() < 2) return false; // need two cuts to bound an arc
        // Find the gap (consecutive cut angles, cyclic) that contains the pick; that
        // arc is removed, leaving an arc spanning the rest (from hi CCW to lo).
        double lo = cuts.back(), hi = cuts.front();
        for (size_t i = 0; i + 1 < cuts.size(); ++i) {
            if (ap >= cuts[i] && ap < cuts[i + 1]) { lo = cuts[i]; hi = cuts[i + 1]; break; }
        }
        e.type = EntityType::Arc;
        e.startAngle = hi;
        e.endAngle = lo; // CCW from hi to lo = the kept (complement) arc
        return true;
    }

    // Arc: work in relative sweep parameter u in [0, sweep].
    double sweep = e.endAngle - e.startAngle;
    while (sweep <= 0) sweep += 2.0 * M_PI;
    while (sweep > 2.0 * M_PI) sweep -= 2.0 * M_PI;
    auto rel = [&](double a) {
        double r = a - e.startAngle;
        while (r < 0) r += 2.0 * M_PI;
        while (r >= 2.0 * M_PI) r -= 2.0 * M_PI;
        return r;
    };
    const double up = rel(ap);
    if (up > sweep + 1e-9) return false; // pick isn't on the arc
    std::vector<double> cuts;
    for (double a : circleCutAngles(C, e.radius, e, cutters)) {
        const double u = rel(a);
        if (u > 1e-9 && u < sweep - 1e-9) cuts.push_back(u); // strictly inside the arc
    }
    if (cuts.empty()) return false;
    std::sort(cuts.begin(), cuts.end());
    double lo = -1.0, hi = sweep + 1.0;
    bool hasLo = false, hasHi = false;
    for (double u : cuts) {
        if (u <= up && u > lo) { lo = u; hasLo = true; }
        if (u >= up && u < hi) { hi = u; hasHi = true; }
    }
    const double s0 = e.startAngle;
    if (hasLo && hasHi && hi - lo > 1e-7) {
        // Split: keep [start, start+lo] and [start+hi, end].
        Entity rem = e;
        rem.id = newId();
        rem.startAngle = s0 + hi;
        rem.endAngle = s0 + sweep;
        e.endAngle = s0 + lo;
        extra = rem;
        return true;
    }
    if (hasHi) { e.startAngle = s0 + hi; return true; } // remove the start-side piece
    if (hasLo) { e.endAngle = s0 + lo; return true; }   // remove the end-side piece
    return false;
}

namespace {
// Invoke f(p1,p2) for each straight boundary segment of a cutting-edge entity —
// double lines/polylines yield their two VISIBLE physical lines.
template <class F>
void forEachCutSegment(const Entity& e, F&& f) {
    switch (e.type) {
    case EntityType::Line:
        f(e.a, e.b);
        break;
    case EntityType::DoubleLine: {
        const double gap = e.meta.value("gap").toDouble(10.0);
        const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
        f(e.a + off, e.b + off);
        f(e.a - off, e.b - off);
        break;
    }
    case EntityType::Polyline:
        if (e.meta.value("double").toBool() && e.points.size() >= 2) {
            const double gap = e.meta.value("gap").toDouble(10.0);
            for (double s : {gap / 2.0, -gap / 2.0}) {
                const auto run = offsetPolyline(e.points, s);
                for (size_t i = 1; i < run.size(); ++i) f(run[i - 1], run[i]);
            }
        } else {
            for (size_t i = 1; i < e.points.size(); ++i) f(e.points[i - 1], e.points[i]);
        }
        break;
    case EntityType::Spline:
    case EntityType::Wire:
        for (size_t i = 1; i < e.points.size(); ++i) f(e.points[i - 1], e.points[i]);
        break;
    case EntityType::Rect:
    case EntityType::Polygon:
        for (size_t i = 0; i < e.points.size(); ++i)
            f(e.points[i], e.points[(i + 1) % e.points.size()]);
        break;
    default:
        break;
    }
}
} // namespace

bool objectCrosses(const Entity& e, const std::vector<Entity>& others) {
    if (e.type == EntityType::Line || e.type == EntityType::DoubleLine)
        return !intersectionParams(e, others).empty(); // uses the centreline a,b
    if (e.type == EntityType::Circle || e.type == EntityType::Arc) {
        for (double ang : circleCutAngles(e.center, e.radius, e, others)) {
            if (e.type == EntityType::Circle) return true;
            double sweep = e.endAngle - e.startAngle;
            while (sweep < 0) sweep += 2.0 * M_PI;
            double rel = ang - e.startAngle;
            while (rel < 0) rel += 2.0 * M_PI;
            if (rel <= sweep + 1e-9) return true;
        }
        return false;
    }
    if (e.type == EntityType::Polyline || e.type == EntityType::Spline ||
        e.type == EntityType::Wire) {
        for (size_t i = 1; i < e.points.size(); ++i) {
            const Vec2 A = e.points[i - 1], B = e.points[i], D = B - A;
            const double L2 = D.lengthSq();
            for (const auto& c : others) {
                if (!e.id.isEmpty() && c.id == e.id) continue;
                if (c.type == EntityType::Circle || c.type == EntityType::Arc) {
                    for (const auto& x : lineCircleIntersection(A, D, c.center, c.radius)) {
                        const double t = (L2 > 1e-12) ? dot(x - A, D) / L2 : -1.0;
                        if (t <= 1e-6 || t >= 1.0 - 1e-6) continue;
                        if (c.type == EntityType::Arc) {
                            double sweep = c.endAngle - c.startAngle;
                            while (sweep < 0) sweep += 2.0 * M_PI;
                            double rel = std::atan2(x.y - c.center.y, x.x - c.center.x) - c.startAngle;
                            while (rel < 0) rel += 2.0 * M_PI;
                            if (rel > sweep + 1e-9) continue;
                        }
                        return true;
                    }
                    continue;
                }
                bool hit = false;
                forEachCutSegment(c, [&](const Vec2& p1, const Vec2& p2) {
                    if (segmentSegmentIntersection(A, B, p1, p2)) hit = true;
                });
                if (hit) return true;
            }
        }
    }
    return false;
}

std::optional<std::pair<Vec2, Vec2>> cutSegmentThrough(const Vec2& p,
                                                       const std::vector<Entity>& cutters,
                                                       const QString& selfId) {
    std::optional<std::pair<Vec2, Vec2>> best;
    double bestd = 1e-4;
    for (const auto& c : cutters) {
        if (!selfId.isEmpty() && c.id == selfId) continue;
        forEachCutSegment(c, [&](const Vec2& a, const Vec2& b) {
            const auto cp = closestPointOnSegment(p, a, b);
            if (cp.distance < bestd) { bestd = cp.distance; best = std::make_pair(a, b); }
        });
    }
    return best;
}

std::optional<std::pair<Vec2, double>> cutCircleThrough(const Vec2& p,
                                                        const std::vector<Entity>& cutters,
                                                        const QString& selfId) {
    std::optional<std::pair<Vec2, double>> best;
    double bestErr = 1e-3;
    for (const auto& c : cutters) {
        if (!selfId.isEmpty() && c.id == selfId) continue;
        if (c.type != EntityType::Circle && c.type != EntityType::Arc) continue;
        const double err = std::abs(distance(p, c.center) - c.radius);
        if (err < bestErr) { bestErr = err; best = std::make_pair(c.center, c.radius); }
    }
    return best;
}

std::vector<std::vector<Vec2>> trimPolylinePieces(const std::vector<Vec2>& pts,
                                                  const Vec2& pick,
                                                  const std::vector<Entity>& cutters,
                                                  const QString& selfId) {
    std::vector<std::vector<Vec2>> result;
    const int n = static_cast<int>(pts.size());
    if (n < 2) return result;

    // Boundaries along the polyline as a global parameter (segment index + t):
    // CROSSINGS with cutters (preferred) and the polyline's own interior vertices
    // (KNEES, used only as a fallback so a lone crossing doesn't trim to the end).
    std::vector<double> crossings;
    for (int i = 0; i < n - 1; ++i) {
        const Vec2 A = pts[i], B = pts[i + 1], D = B - A;
        const double L2 = D.lengthSq();
        if (L2 < 1e-12) continue;
        for (const auto& c : cutters) {
            if (!selfId.isEmpty() && c.id == selfId) continue;
            if (c.type == EntityType::Circle || c.type == EntityType::Arc) {
                for (const auto& x : lineCircleIntersection(A, D, c.center, c.radius)) {
                    const double t = dot(x - A, D) / L2;
                    if (t <= 1e-6 || t >= 1.0 - 1e-6) continue;
                    if (c.type == EntityType::Arc) {
                        double sweep = c.endAngle - c.startAngle;
                        while (sweep < 0) sweep += 2.0 * M_PI;
                        double rel = std::atan2(x.y - c.center.y, x.x - c.center.x) - c.startAngle;
                        while (rel < 0) rel += 2.0 * M_PI;
                        if (rel > sweep + 1e-9) continue;
                    }
                    crossings.push_back(i + t);
                }
            } else {
                forEachCutSegment(c, [&](const Vec2& p1, const Vec2& p2) {
                    if (auto x = segmentSegmentIntersection(A, B, p1, p2)) {
                        const double t = dot(*x - A, D) / L2;
                        if (t > 1e-6 && t < 1.0 - 1e-6) crossings.push_back(i + t);
                    }
                });
            }
        }
    }
    if (crossings.empty()) return result; // nothing actually cut it
    std::vector<double> knees;
    for (int i = 1; i < n - 1; ++i) knees.push_back(static_cast<double>(i));

    // Where the pick sits along the polyline.
    double gp = 0.0, bestd = std::numeric_limits<double>::max();
    for (int i = 0; i < n - 1; ++i) {
        const auto c = closestPointOnSegment(pick, pts[i], pts[i + 1]);
        if (c.distance < bestd) {
            bestd = c.distance;
            const double L = distance(pts[i], pts[i + 1]);
            gp = i + (L > 1e-9 ? distance(pts[i], c.point) / L : 0.0);
        }
    }
    // Nearest boundary on each side of the pick: a cutter CROSSING wins; only if
    // there's no crossing on that side does the nearest knee bound it. This removes
    // the whole inside-circle span (ignoring interior knees) yet a single crossing
    // still stops at the nearest knee instead of the far end.
    auto nearestBelow = [&](const std::vector<double>& v, double& out) {
        bool found = false;
        for (double g : v)
            if (g <= gp + 1e-9 && (!found || g > out)) { out = g; found = true; }
        return found;
    };
    auto nearestAbove = [&](const std::vector<double>& v, double& out) {
        bool found = false;
        for (double g : v)
            if (g >= gp - 1e-9 && (!found || g < out)) { out = g; found = true; }
        return found;
    };
    double lo = 0.0, hi = static_cast<double>(n - 1);
    bool hasLo = nearestBelow(crossings, lo) || nearestBelow(knees, lo);
    bool hasHi = nearestAbove(crossings, hi) || nearestAbove(knees, hi);
    auto ptAt = [&](double g) {
        int i = std::clamp(static_cast<int>(std::floor(g)), 0, n - 2);
        return pts[i] + (pts[i + 1] - pts[i]) * (g - i);
    };
    auto sub = [&](double g0, double g1) {
        std::vector<Vec2> out;
        out.push_back(ptAt(g0));
        for (int j = static_cast<int>(std::ceil(g0)); j <= static_cast<int>(std::floor(g1)); ++j)
            if (j > g0 + 1e-9 && j < g1 - 1e-9) out.push_back(pts[j]);
        out.push_back(ptAt(g1));
        return out;
    };
    if (hasLo && hasHi && hi - lo > 1e-6) {
        result.push_back(sub(0.0, lo));
        result.push_back(sub(hi, static_cast<double>(n - 1)));
    } else if (hasHi) {
        result.push_back(sub(hi, static_cast<double>(n - 1)));
    } else if (hasLo) {
        result.push_back(sub(0.0, lo));
    }
    result.erase(std::remove_if(result.begin(), result.end(),
                                [](const std::vector<Vec2>& p) {
                                    return p.size() < 2 ||
                                           (p.size() == 2 && distance(p[0], p[1]) < 1e-9);
                                }),
                 result.end());
    return result;
}

bool trimLineQuick(Entity& line, const Vec2& pick, const std::vector<Entity>& others,
                   std::optional<Entity>& extra) {
    extra.reset();
    if (line.type != EntityType::Line) return false;
    auto ts = intersectionParams(line, others);
    if (ts.empty()) return false;

    const Vec2 a = line.a, b = line.b, d = b - a;
    const double L2 = d.lengthSq();
    const double tp = std::clamp(dot(pick - a, d) / L2, 0.0, 1.0);
    auto P = [&](double t) { return a + d * t; };

    // Nearest cut on each side of the pick.
    double lo = -1e9, hi = 1e9;
    bool hasLo = false, hasHi = false;
    for (double t : ts) {
        if (t <= tp && t > lo) { lo = t; hasLo = true; }
        if (t >= tp && t < hi) { hi = t; hasHi = true; }
    }
    if (hasLo && hasHi && hi - lo > 1e-6) {
        // Picked a middle piece bounded on both sides: split, removing it.
        Entity rem = line;
        rem.id = newId();
        rem.a = P(hi);
        rem.b = b;
        line.b = P(lo);
        extra = rem;
        return true;
    }
    if (hasHi) { line.a = P(hi); return true; } // dangling start piece
    if (hasLo) { line.b = P(lo); return true; } // dangling end piece
    return false;
}

bool extendLineTo(Entity& line, const Vec2& pick, const std::vector<Entity>& others) {
    if (line.type != EntityType::Line) return false;
    // The end to extend is the one nearer the pick.
    const bool extendA = distance(pick, line.a) < distance(pick, line.b);
    const Vec2 fixed = extendA ? line.b : line.a;
    const Vec2 moving = extendA ? line.a : line.b;
    const Vec2 dir = (moving - fixed).normalized();
    if (dir.lengthSq() < 1e-12) return false;

    // Intersect the infinite ray from `moving` along `dir` with boundaries.
    double bestT = std::numeric_limits<double>::max();
    Vec2 bestPt;
    bool found = false;
    auto tryBoundary = [&](const Vec2& a, const Vec2& b) {
        auto x = lineLineIntersection(fixed, moving, a, b);
        if (!x) return;
        // Must be beyond `moving` in the dir direction, and within boundary seg.
        const double t = dot(*x - moving, dir);
        if (t <= 1e-9) return;
        const auto c = closestPointOnSegment(*x, a, b);
        if (c.distance > 1e-6) return; // not actually on the boundary segment
        if (t < bestT) { bestT = t; bestPt = *x; found = true; }
    };
    for (const auto& e : others) {
        if (&e == &line) continue;
        if (e.type == EntityType::Line) {
            tryBoundary(e.a, e.b);
        } else if (e.type == EntityType::DoubleLine) {
            // Extend to the physical line, not the centerline.
            const double gap = e.meta.value("gap").toDouble(10.0);
            const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
            tryBoundary(e.a + off, e.b + off);
            tryBoundary(e.a - off, e.b - off);
        } else if (e.type == EntityType::Rect || e.type == EntityType::Polygon) {
            for (size_t i = 0; i < e.points.size(); ++i)
                tryBoundary(e.points[i], e.points[(i + 1) % e.points.size()]);
        } else if (e.type == EntityType::Polyline) {
            if (e.meta.value("double").toBool() && e.points.size() >= 2) {
                const double gap = e.meta.value("gap").toDouble(10.0);
                for (double s : {gap / 2.0, -gap / 2.0}) {
                    const auto run = offsetPolyline(e.points, s);
                    for (size_t i = 1; i < run.size(); ++i) tryBoundary(run[i - 1], run[i]);
                }
            } else {
                for (size_t i = 1; i < e.points.size(); ++i) tryBoundary(e.points[i - 1], e.points[i]);
            }
        }
    }
    if (!found) return false;
    if (extendA) line.a = bestPt; else line.b = bestPt;
    return true;
}

std::vector<Entity> rectArray(const Entity& src, int rows, int cols,
                              double dx, double dy) {
    std::vector<Entity> out;
    rows = std::max(1, rows);
    cols = std::max(1, cols);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (r == 0 && c == 0) continue;
            Entity clone = src;
            clone.id = newId();
            translateEntity(clone, {dx * c, dy * r});
            out.push_back(std::move(clone));
        }
    }
    return out;
}

std::vector<Entity> explodeEntity(const Entity& e) {
    std::vector<Entity> out;
    auto makeLine = [&](const Vec2& a, const Vec2& b) {
        Entity l;
        l.id = newId();
        l.type = EntityType::Line;
        l.layer = e.layer;
        l.color = e.color;
        l.weight = e.weight;
        l.a = a;
        l.b = b;
        out.push_back(std::move(l));
    };
    switch (e.type) {
    case EntityType::Polyline:
    case EntityType::Spline:
    case EntityType::Wire:
        for (size_t i = 1; i < e.points.size(); ++i) makeLine(e.points[i - 1], e.points[i]);
        break;
    case EntityType::Rect:
    case EntityType::Polygon:
        for (size_t i = 0; i < e.points.size(); ++i)
            makeLine(e.points[i], e.points[(i + 1) % e.points.size()]);
        break;
    default:
        break;
    }
    return out;
}

std::vector<Entity> purgeEntity(const Entity& e) {
    std::vector<Entity> out;
    auto makeLine = [&](const Vec2& a, const Vec2& b) {
        Entity l;
        l.id = newId();
        l.type = EntityType::Line;
        l.layer = e.layer;
        l.color = e.color;
        l.weight = e.weight;
        l.a = a;
        l.b = b;
        out.push_back(std::move(l));
    };
    auto polylineToLines = [&](const std::vector<Vec2>& pts, bool closed) {
        const int n = static_cast<int>(pts.size());
        for (int i = 1; i < n; ++i) makeLine(pts[i - 1], pts[i]);
        if (closed && n > 2) makeLine(pts[n - 1], pts[0]);
    };
    auto makeText = [&](const Vec2& pos, const QString& text, double size) {
        if (text.isEmpty()) return;
        Entity t;
        t.id = newId();
        t.type = EntityType::Text;
        t.layer = e.layer;
        t.a = pos;
        t.meta.insert("text", text);
        t.meta.insert("size", size);
        out.push_back(std::move(t));
    };

    switch (e.type) {
    case EntityType::DoubleLine: {
        const double gap = e.meta.value("gap").toDouble(10.0);
        const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
        makeLine(e.a + off, e.b + off);
        makeLine(e.a - off, e.b - off);
        break;
    }
    case EntityType::Polyline: {
        const double cr = e.meta.value("cornerRadius").toDouble(0.0);
        if (e.meta.value("double").toBool()) {
            const double gap = e.meta.value("gap").toDouble(10.0);
            // Round centerline at cr+gap/2 so both runs fillet (inner radius = cr).
            const std::vector<Vec2> center =
                cr > 0.0 ? roundedPolyline(e.points, cr + gap / 2.0) : e.points;
            polylineToLines(offsetPolyline(center, gap / 2.0), false);
            polylineToLines(offsetPolyline(center, -gap / 2.0), false);
        } else {
            polylineToLines(cr > 0.0 ? roundedPolyline(e.points, cr) : e.points, false);
        }
        break;
    }
    case EntityType::Spline:
    case EntityType::Wire:
        polylineToLines(e.points, false);
        break;
    case EntityType::Rect:
    case EntityType::Polygon:
        polylineToLines(e.points, true);
        break;
    case EntityType::Connector: {
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
        const ConnectorGeometry g = connectorGeometry(e.a, spec);
        // Body rectangle as four lines.
        const Vec2 bl = g.rectMin, tr = g.rectMax;
        const Vec2 br{tr.x, bl.y}, tl{bl.x, tr.y};
        makeLine(bl, br); makeLine(br, tr); makeLine(tr, tl); makeLine(tl, bl);
        // Pin stubs + labels.
        for (int i = 0; i < spec.pinCount; ++i) {
            makeLine(g.pinTip[i], g.pinEnd[i]);
            if (i < spec.pins.size()) makeText(g.pinLabelPos[i], spec.pins[i], spec.textSize * 0.7);
        }
        makeText(g.designatorPos, spec.designator, spec.textSize);
        makeText(g.partPos, spec.partNumber, spec.textSize * 0.8);
        break;
    }
    default:
        break; // already a single primitive (line / circle / arc / text / ...)
    }
    return out;
}

} // namespace Edraw_Desktop
