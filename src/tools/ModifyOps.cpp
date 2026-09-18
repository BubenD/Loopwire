#include "tools/ModifyOps.h"

// applyDoubleRunCuts lives with the drawing code because the cap helpers do; it is
// geometry, not painting, and one definition of where a line IS beats two.
#include "render/EntityRenderer.h"

#include "geometry/Geometry.h"
#include "harness/Harness.h"
#include "tools/EntityTransforms.h"

#include <QJsonArray>
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

// Invoke f(p1,p2) for each straight boundary segment of a cutting-edge entity.
// Every cutter type is reduced to segments here — double lines/polylines yield
// their two VISIBLE physical lines; circles/arcs/ellipses are tessellated — so ALL
// trim/intersection code can treat cutters uniformly (and stay consistent).
template <class F>
void forEachCutSegment(const Entity& e, F&& f) {
    constexpr double kPi = 3.14159265358979323846;
    auto onCircle = [&](double a) {
        return Vec2{e.center.x + e.radius * std::cos(a), e.center.y + e.radius * std::sin(a)};
    };
    switch (e.type) {
    case EntityType::Line:
        f(e.a, e.b);
        break;
    case EntityType::DoubleLine: {
        // A double run CUTS with its two visible lines - and "visible" includes what a cap adds. A line
        // extended to meet something is drawn out to it, so it crosses things out there; leaving the caps
        // off here meant the cutter stopped short of where the drawing shows it, and a trim against it
        // answered "nothing crosses that line".
        const double gap = e.meta.value("gap").toDouble(10.0);
        const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
        for (int side : {+1, -1}) {
            std::vector<Vec2> rail = {e.a + off * side, e.b + off * side};
            applyDoubleRunCuts(rail, e, side);
            for (const auto& piece : splitRailAtHoles(rail, e, side))
                for (size_t i = 1; i < piece.size(); ++i) f(piece[i - 1], piece[i]);
        }
        break;
    }
    case EntityType::Polyline:
        if (e.meta.value("shrunkEnds").toBool()) {
            // A sleeve cuts and is extended to by the shape it draws, not by its invisible route or by
            // two plain offset rails. This is also what a transition's square and recovered faces give.
            for (const auto& run : shrunkSleeveRunsOf(e))
                for (size_t i = 1; i < run.size(); ++i) f(run[i - 1], run[i]);
        } else if (e.meta.value("double").toBool() && e.points.size() >= 2) {
            const double gap = e.meta.value("gap").toDouble(10.0);
            for (double s : {gap / 2.0, -gap / 2.0}) {
                std::vector<Vec2> run = offsetPolyline(e.points, s);
                const int side = s > 0 ? +1 : -1;
                applyDoubleRunCuts(run, e, side);   // as drawn: caps in, holes out
                for (const auto& piece : splitRailAtHoles(run, e, side))
                    for (size_t i = 1; i < piece.size(); ++i) f(piece[i - 1], piece[i]);
            }
        } else {
            for (size_t i = 1; i < e.points.size(); ++i) f(e.points[i - 1], e.points[i]);
        }
        break;
    case EntityType::Spline: {
        // The curve AS DRAWN, tessellated - not the straight chords between its points.
        //
        // A spline was being cut against its own control polygon, which is not where it is drawn: the
        // crossing came out beside the visible curve, and the cut FACE was a chord whose lean can be
        // the opposite of the curve's at that point. Trimming a double line against a hand-drawn
        // S-curve therefore clipped its two rails on a face tilted the wrong way, so the rail that
        // should have been the longer one came out the shorter - the "swapped polarity" on the
        // straight side of the cut. Circles and arcs were already tessellated here for the same
        // reason; a spline is no different.
        const std::vector<Vec2> drawn =
            e.meta.value("splineMode").toString() == QLatin1String("cv")
                ? bsplinePoints(e.points)
                : splinePoints(e.points);
        const std::vector<Vec2>& use = drawn.size() >= 2 ? drawn : e.points;
        for (size_t i = 1; i < use.size(); ++i) f(use[i - 1], use[i]);
        break;
    }
    case EntityType::Wire:
        for (size_t i = 1; i < e.points.size(); ++i) f(e.points[i - 1], e.points[i]);
        break;
    case EntityType::TwistedPair:
        // A harness cuts with the WIRES it draws, not with its route. The route is the line down the
        // middle of the bundle and is never drawn: a boundary nobody can see is a boundary nothing
        // appears to stop at. Its cut ends count too - they are drawn, and something extended to the
        // end of a harness stops at the line across it.
        for (const auto& strand : twistedPairStrands(e))
            for (size_t i = 1; i < strand.size(); ++i) f(strand[i - 1], strand[i]);
        for (const auto& cap : twistedPairEndCaps(e))
            if (cap.size() == 2) f(cap.front(), cap.back());
        for (const auto& outline : twistedPairBody(e))
            for (size_t i = 1; i < outline.size(); ++i) f(outline[i - 1], outline[i]);
        break;
    case EntityType::Rect:
    case EntityType::Polygon:
        for (size_t i = 0; i < e.points.size(); ++i)
            f(e.points[i], e.points[(i + 1) % e.points.size()]);
        break;
    case EntityType::Circle: {
        const int N = 96;
        Vec2 prev = onCircle(0.0);
        for (int i = 1; i <= N; ++i) { Vec2 cur = onCircle(2.0 * kPi * i / N); f(prev, cur); prev = cur; }
        break;
    }
    case EntityType::Arc: {
        double sweep = e.endAngle - e.startAngle;
        while (sweep < 0) sweep += 2.0 * kPi;
        const int N = std::max(8, static_cast<int>(std::ceil(sweep / (kPi / 48.0))));
        Vec2 prev = onCircle(e.startAngle);
        for (int i = 1; i <= N; ++i) {
            Vec2 cur = onCircle(e.startAngle + sweep * i / N);
            f(prev, cur);
            prev = cur;
        }
        break;
    }
    case EntityType::Ellipse: {
        const double cs = std::cos(e.rotation), sn = std::sin(e.rotation);
        auto ept = [&](double t) {
            const double lx = e.radius * std::cos(t), ly = e.radiusY * std::sin(t);
            return Vec2{e.center.x + lx * cs - ly * sn, e.center.y + lx * sn + ly * cs};
        };
        double sa = 0.0, ea = 2.0 * kPi;
        if (e.meta.value("ellipticalArc").toBool()) {
            sa = e.startAngle;
            ea = e.endAngle;
            while (ea <= sa) ea += 2.0 * kPi;
        }
        const int N = 96;
        Vec2 prev = ept(sa);
        for (int i = 1; i <= N; ++i) { Vec2 cur = ept(sa + (ea - sa) * i / N); f(prev, cur); prev = cur; }
        break;
    }
    default:
        break;
    }
}

bool isCurvedCutter(const Entity& e) {
    if ((e.type == EntityType::Circle || e.type == EntityType::Arc) && e.radius > 1e-9) return true;
    return e.type == EntityType::Ellipse && e.radius > 1e-9 && e.radiusY > 1e-9;
}

// Is `p` within the swept part of a cutter that is only part of its curve? A whole circle or a whole
// ellipse takes everything.
//
// A crossing AT one of the arc's own two ends is the case that has to be right, because it is the
// commonest one there is: trim an ellipse with a line and the half left behind ENDS on that line, so
// trimming the line by it crosses it exactly at both ends. Measured from the start of the sweep, such a
// crossing lands at 0 or at the sweep - and the angle is worked out from the crossing point, so it
// carries a rounding error whose SIGN decides everything. A hair below the start wraps round to nearly
// a whole turn and reads as "off the arc": the crossing is dropped, and a trim then runs past that end
// to whatever the next boundary is - the far side of a rectangle, the corner of a run. Which is exactly
// the fault this tolerance fixes. It is measured as a length along the arc rather than as an angle, so
// it means the same thing whatever the radius (see arcAngleSlack).
// The slack an angle gets when deciding whether a point is on an arc: an eyelash of ARC LENGTH, turned
// into an angle for that radius - so it means the same thing on a 2 mm arc and a 2 metre one. Geometry
// this close together is meant to touch.
double arcAngleSlack(double radius) {
    return std::clamp(1e-4 / std::max(1e-6, radius), 1e-9, 1e-3);
}

bool onCutterCurve(const Entity& cutter, const Vec2& p) {
    const double kEps = arcAngleSlack(std::max(cutter.radius, cutter.radiusY));
    const bool isEllipse = cutter.type == EntityType::Ellipse;
    if (isEllipse && !cutter.meta.value(QStringLiteral("ellipticalArc")).toBool()) return true;
    if (!isEllipse && cutter.type != EntityType::Arc) return true;
    double sweep = cutter.endAngle - cutter.startAngle;
    while (sweep < 0.0) sweep += 2.0 * M_PI;
    const double at = isEllipse ? ellipseParamAt(p, cutter.center, cutter.radius, cutter.radiusY,
                                                 cutter.rotation)
                                : std::atan2(p.y - cutter.center.y, p.x - cutter.center.x);
    double rel = std::fmod(at - cutter.startAngle, 2.0 * M_PI);
    if (rel < 0.0) rel += 2.0 * M_PI;
    if (rel > 2.0 * M_PI - kEps) rel -= 2.0 * M_PI;   // a hair BEFORE the start is the start
    return rel >= -kEps && rel <= sweep + kEps;
}

// Where the segment a..a+d crosses a curved cutter, as parameters along it. Only crossings strictly
// inside the segment count - an end that already sits on the curve is not a cut.
//
// Worked out AGAINST THE CURVE ITSELF, not against the many-sided polygon forEachCutSegment reduces it
// to. Two reasons, and both of them bite. Every chord of that polygon lies inside the curve, so the cut
// lands a little short of it - close enough to look right, and wrong enough that snapping or filleting
// to that end afterwards misses the curve. And where a chord ENDS on the object being trimmed - which
// is exactly what happens after trimming an ellipse with a line, since the piece left behind then ends
// on that line - the chord-against-segment test is the degenerate case that may report the crossing
// twice or not at all. Not at all is what left one cut instead of two, so trimming the line took
// everything from the one cut it found to the far end of the line instead of the piece between them.
void appendCurvedCuts(const Entity& cutter, const Vec2& a, const Vec2& d, double L2, double offset,
                      std::vector<double>& out) {
    const std::vector<Vec2> xs =
        cutter.type == EntityType::Ellipse
            ? lineEllipseIntersection(a, d, cutter.center, cutter.radius, cutter.radiusY,
                                      cutter.rotation)
            : lineCircleIntersection(a, d, cutter.center, cutter.radius);
    for (const Vec2& x : xs) {
        const double t = dot(x - a, d) / L2;
        if (t <= 1e-6 || t >= 1.0 - 1e-6) continue;
        if (!onCutterCurve(cutter, x)) continue;
        out.push_back(offset + t);
    }
}

// Gather all segment-segment intersection params of `line` with `others`.
std::vector<Vec2> lineIntersections(const Entity& line,
                                    const std::vector<Entity>& others) {
    std::vector<Vec2> out;
    auto addSeg = [&](const Vec2& a, const Vec2& b) {
        if (auto x = segmentSegmentIntersection(line.a, line.b, a, b)) out.push_back(*x);
    };
    const Vec2 dir = line.b - line.a;
    const double L2 = dir.lengthSq();
    for (const auto& e : others) {
        if (&e == &line) continue;
        // A circle, an arc or an ellipse is crossed exactly - and was not handled here at all before,
        // so a line could not be trimmed to a curve by this path.
        if (isCurvedCutter(e) && L2 > 1e-12) {
            std::vector<double> ts;
            appendCurvedCuts(e, line.a, dir, L2, 0.0, ts);
            for (const double t : ts) out.push_back({line.a.x + dir.x * t, line.a.y + dir.y * t});
            continue;
        }
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

    // For each line, the KEPT direction u runs from the corner P toward the SIDE the
    // user clicked (not merely the nearest endpoint) — so clicking anywhere along a
    // line, including mid-line or near the corner, keeps the far part. The endpoint
    // on the other side is trimmed to the tangent point.
    auto pickSide = [&](const Entity& l, const Vec2& pick, Vec2& u, double& maxAlong,
                        bool& moveIsA) -> bool {
        Vec2 dir = l.b - l.a;
        if (dir.lengthSq() < 1e-18) return false;
        dir = dir.normalized();
        const Vec2 proj = closestPointOnSegment(pick, l.a, l.b).point;
        double along = dot(proj - P, dir);
        // A pick AT the corner says nothing about which side to keep - and that is where one naturally
        // clicks to round a corner, with the snap putting the click exactly on it. The side to keep is
        // then the side the line is ON: the end further from the corner. Read as "no side", the kept
        // length came out as nothing and the whole command answered "these do not form a corner",
        // which is why two lines meeting at a joint could not be filleted at all.
        if (std::abs(along) < 1e-9) {
            const double da = dot(l.a - P, dir), db = dot(l.b - P, dir);
            along = (std::abs(da) >= std::abs(db)) ? da : db;
        }
        if (std::abs(along) < 1e-9) return false;   // both ends at the corner: nothing to keep
        u = (along >= 0.0) ? dir : Vec2{-dir.x, -dir.y};
        const double da = dot(l.a - P, u), db = dot(l.b - P, u);
        maxAlong = std::max(da, db); // distance from P to the kept (far) endpoint
        moveIsA = da < db;           // trim the endpoint that's less far along u
        return maxAlong > 1e-9;
    };
    Vec2 u1, u2;
    double maxD1, maxD2;
    bool move1IsA, move2IsA;
    if (!pickSide(l1, near1, u1, maxD1, move1IsA)) return res;
    if (!pickSide(l2, near2, u2, maxD2, move2IsA)) return res;
    const double cosT = std::clamp(dot(u1, u2), -1.0, 1.0);
    const double theta = std::acos(cosT);
    if (theta < 1e-4 || std::abs(theta - M_PI) < 1e-4) return res; // collinear

    // Radius 0 (AutoCAD): no arc — just trim/extend both lines so they meet at the
    // corner P, keeping the picked sides.
    if (radius <= 1e-9) {
        setNear(l1, move1IsA, P);
        setNear(l2, move2IsA, P);
        res.hasConnector = false;
        res.ok = true;
        return res;
    }

    // Tangent setback from the corner. If the radius is too large to fit on either
    // kept line, reject it (don't apply) so the caller can warn the user.
    const double d = radius / std::tan(theta / 2.0);
    const double maxD = std::min(maxD1, maxD2);
    if (d > maxD) { res.tooBig = true; return res; }
    const double r = radius;
    const Vec2 T1 = P + u1 * d;
    const Vec2 T2 = P + u2 * d;

    // Trim the corner-side endpoint of each line to its tangent point.
    setNear(l1, move1IsA, T1);
    setNear(l2, move2IsA, T2);

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

// ---- Filleting a line to a curve ------------------------------------------
//
// Rounding the corner where a line meets a circle, an arc, an ellipse or a piece of one. Between two
// lines the answer is a formula; against a curve it is not, because the fillet has to be tangent to a
// curve whose own direction changes along it. So it is SOLVED rather than derived, and the thing solved
// for is the place on the curve.
//
// At a place t on the curve, a circle of the fillet's radius that touches the curve there has its centre
// on the curve's normal, r away - two of them, one each side. That centre must also be r from the line,
// which is one number to drive to zero. Sampled all along the curve and refined wherever the sign turns
// over, that finds every fillet there is; the one nearest where the two objects were PICKED is the one
// meant, which is how filleting has always chosen between the four corners two lines make.
namespace {

struct CurveGeom {
    Vec2 c;
    double rx = 0.0, ry = 0.0, rot = 0.0;
    bool closed = true;
    double from = 0.0, sweep = 2.0 * M_PI;   // the piece that is there, when it is not closed
};

bool curveGeomOf(const Entity& e, CurveGeom& g) {
    if (e.type == EntityType::Circle || e.type == EntityType::Arc) {
        if (e.radius < 1e-9) return false;
        g = {e.center, e.radius, e.radius, 0.0, e.type == EntityType::Circle, 0.0, 2.0 * M_PI};
    } else if (e.type == EntityType::Ellipse) {
        if (e.radius < 1e-9 || e.radiusY < 1e-9) return false;
        g = {e.center, e.radius, e.radiusY, e.rotation,
             !e.meta.value(QStringLiteral("ellipticalArc")).toBool(), 0.0, 2.0 * M_PI};
    } else {
        return false;
    }
    if (!g.closed) {
        g.from = e.startAngle;
        double sweep = e.endAngle - e.startAngle;
        while (sweep <= 0.0) sweep += 2.0 * M_PI;
        while (sweep > 2.0 * M_PI) sweep -= 2.0 * M_PI;
        g.sweep = sweep;
    }
    return true;
}

Vec2 curveAt(const CurveGeom& g, double t) { return ellipsePointAt(t, g.c, g.rx, g.ry, g.rot); }

// Which way the curve runs at t (not normalised).
Vec2 curveDir(const CurveGeom& g, double t) {
    const double cc = std::cos(g.rot), ss = std::sin(g.rot);
    const double lx = -g.rx * std::sin(t), ly = g.ry * std::cos(t);
    return {lx * cc - ly * ss, lx * ss + ly * cc};
}

double distToInfiniteLine(const Vec2& p, const Vec2& a, const Vec2& b) {
    const Vec2 d = b - a;
    const double len = d.length();
    if (len < 1e-12) return distance(p, a);
    return std::abs(cross(d, p - a)) / len;
}

Vec2 footOnInfiniteLine(const Vec2& p, const Vec2& a, const Vec2& b) {
    const Vec2 d = b - a;
    const double L2 = d.lengthSq();
    if (L2 < 1e-12) return a;
    return a + d * (dot(p - a, d) / L2);
}

} // namespace

CornerResult filletLineCurve(Entity& line, Entity& curve, double radius, const Vec2& pickLine,
                             const Vec2& pickCurve) {
    CornerResult out;
    CurveGeom g;
    if (line.type != EntityType::Line || !curveGeomOf(curve, g)) return out;

    // Radius 0 is not a fillet: it is "bring these two together", so both are taken to the crossing
    // nearest the picks and nothing is added between them.
    if (radius < 1e-9) {
        const std::vector<Vec2> xs =
            lineEllipseIntersection(line.a, line.b - line.a, g.c, g.rx, g.ry, g.rot);
        const Vec2* best = nullptr;
        double bestScore = std::numeric_limits<double>::max();
        for (const Vec2& x : xs) {
            const double s = distance(x, pickLine) + distance(x, pickCurve);
            if (s < bestScore) { bestScore = s; best = &x; }
        }
        if (!best) return out;
        // The end that MOVES is the one nearer where they are being joined - not the one nearer the
        // pick, which at a joint is the same end and would have moved the wrong one.
        if (distance(line.a, *best) <= distance(line.b, *best)) line.a = *best;
        else line.b = *best;
        out.ok = true;
        out.hasConnector = false;
        return out;
    }

    // Every fillet there is: the sign turning over in "how far the tangent centre is from the line,
    // less the radius" says one lies between those two samples.
    struct Found { double t = 0.0; Vec2 c; };
    std::vector<Found> found;
    constexpr int kSamples = 1440;
    for (const double side : {1.0, -1.0}) {
        auto centreAt = [&](double t) {
            const Vec2 d = curveDir(g, t);
            const double len = d.length();
            const Vec2 n = len < 1e-12 ? Vec2{0.0, 0.0} : Vec2{-d.y / len, d.x / len};
            return curveAt(g, t) + n * (side * radius);
        };
        auto err = [&](double t) {
            return distToInfiniteLine(centreAt(t), line.a, line.b) - radius;
        };
        double prevT = g.from, prevE = err(prevT);
        for (int i = 1; i <= kSamples; ++i) {
            const double t = g.from + g.sweep * i / kSamples;
            const double e = err(t);
            if ((prevE <= 0.0 && e >= 0.0) || (prevE >= 0.0 && e <= 0.0)) {
                // Halve the interval down to where it changes: 60 rounds is far past exact.
                double lo = prevT, hi = t, elo = prevE;
                for (int k = 0; k < 60; ++k) {
                    const double mid = (lo + hi) / 2.0;
                    const double em = err(mid);
                    if ((elo <= 0.0 && em >= 0.0) || (elo >= 0.0 && em <= 0.0)) hi = mid;
                    else { lo = mid; elo = em; }
                }
                const double root = (lo + hi) / 2.0;
                found.push_back({root, centreAt(root)});
            }
            prevT = t;
            prevE = e;
        }
    }
    if (found.empty()) {
        out.tooBig = true;   // no circle of that radius touches both
        return out;
    }

    // The one meant: nearest to where the two objects were picked.
    const Found* best = nullptr;
    double bestScore = std::numeric_limits<double>::max();
    Vec2 bestFoot{0.0, 0.0}, bestTouch{0.0, 0.0};
    for (const Found& f : found) {
        const Vec2 touch = curveAt(g, f.t);
        const Vec2 foot = footOnInfiniteLine(f.c, line.a, line.b);
        const double s = distance(touch, pickCurve) + distance(foot, pickLine);
        if (s < bestScore) {
            bestScore = s;
            best = &f;
            bestFoot = foot;
            bestTouch = touch;
        }
    }
    if (!best) return out;

    // The line is taken back (or out) to where the fillet meets it: the end that moves is the one
    // NEARER the fillet, and the far end is what is kept. (Nearer the pick is not the same thing - at a
    // joint the pick is on that end, and the far end would have been dragged in instead.) Where that
    // end WAS is the corner being rounded, which is what tells the curve which of its own ends to give
    // up.
    const bool moveA = distance(line.a, bestFoot) <= distance(line.b, bestFoot);
    const Vec2 corner = moveA ? line.a : line.b;
    if (moveA) line.a = bestFoot;
    else line.b = bestFoot;

    // A piece of a curve is taken back to where the fillet meets it, keeping the side that was picked.
    // A WHOLE circle or ellipse is left alone - it has no end to shorten, which is what filleting a
    // line to a circle does everywhere else.
    if (!g.closed) {
        auto rel = [&](double t) {
            double r = t - g.from;
            while (r < 0.0) r += 2.0 * M_PI;
            while (r >= 2.0 * M_PI) r -= 2.0 * M_PI;
            return r;
        };
        const double tCut = rel(best->t);
        // Which of its own two ends the curve gives up: the one at the CORNER - the place the line's
        // moved end came from. A fillet rounds a corner, so what comes off is the bit between the
        // tangent point and that corner, whichever end of the arc it happens to be. (Taken from the
        // pick instead, a click at the joint - the natural place to click - names the very bit that is
        // about to be removed, and the arc was cut back to a sliver.)
        const Vec2 startPt = curveAt(g, g.from);
        const Vec2 endPt = curveAt(g, g.from + g.sweep);
        if (distance(startPt, corner) <= distance(endPt, corner))
            curve.startAngle = curve.startAngle + tCut;   // the start end is at the corner: it goes
        else
            curve.endAngle = curve.startAngle + tCut;
        if (curve.type == EntityType::Arc) {
            // An arc says its ends as angles about its centre; keep them in one turn.
            while (curve.endAngle - curve.startAngle > 2.0 * M_PI) curve.endAngle -= 2.0 * M_PI;
        }
    }

    // And the fillet itself: the shorter way round between the two places it touches.
    Entity arc;
    arc.type = EntityType::Arc;
    arc.layer = line.layer;
    arc.color = line.color;
    arc.weight = line.weight;
    arc.center = best->c;
    arc.radius = radius;
    double a0 = std::atan2(bestTouch.y - best->c.y, bestTouch.x - best->c.x);
    double a1 = std::atan2(bestFoot.y - best->c.y, bestFoot.x - best->c.x);
    double sweep = a1 - a0;
    while (sweep <= 0.0) sweep += 2.0 * M_PI;
    if (sweep > M_PI) std::swap(a0, a1);
    arc.startAngle = a0;
    arc.endAngle = a1;
    out.connector = std::move(arc);
    out.hasConnector = true;
    out.ok = true;
    return out;
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
// it. All cutter types (incl. circles/arcs/ellipses) are reduced to segments.
// A circle or an arc is cut against EXACTLY rather than against the 96-sided polygon
// forEachCutSegment reduces it to. The difference matters: every chord of that polygon lies inside
// the circle, so a line trimmed against the polygon stops short of the ring - close enough to look
// right, and wrong enough that snapping or filleting to that end afterwards misses the circle. Two
// adjacent chords can also report the same corner twice, which turns one cut into a pair and leaves
// a hair of geometry behind.
// A cut, and the cutting edge that made it. The owner matters for one decision: when the two
// boundaries either side of the pick are the ENTRY AND EXIT OF THE SAME CIRCLE, the material taken
// goes back only to the nearer of them - a fence inside a ring should not swallow the whole chord out
// to the far side. Two DIFFERENT cutting edges keep the ordinary behaviour: the piece between them is
// what goes, which is what trimming between two lines has always meant.
struct CutAt {
    double g = 0.0;             // parameter along the object being trimmed
    const Entity* from = nullptr;   // the cutting edge, or null for the object's own knee
    bool circular = false;
};

std::vector<CutAt> intersectionCuts(const Entity& line, const std::vector<Entity>& others) {
    std::vector<CutAt> cuts;
    const Vec2 a = line.a, b = line.b, d = b - a;
    const double L2 = d.lengthSq();
    if (L2 < 1e-12) return cuts;
    for (const auto& e : others) {
        if (&e == &line) continue;
        if (isCurvedCutter(e)) {
            std::vector<double> ts;
            appendCurvedCuts(e, a, d, L2, 0.0, ts);
            for (double t : ts) cuts.push_back({t, &e, true});
            continue;
        }
        forEachCutSegment(e, [&](const Vec2& p1, const Vec2& p2) {
            if (auto x = segmentSegmentIntersection(a, b, p1, p2)) {
                const double t = dot(*x - a, d) / L2;
                if (t > 1e-6 && t < 1.0 - 1e-6) cuts.push_back({t, &e, false});
            }
        });
    }
    return cuts;
}

std::vector<double> intersectionParams(const Entity& line, const std::vector<Entity>& others) {
    std::vector<double> ts;
    const Vec2 a = line.a, b = line.b, d = b - a;
    const double L2 = d.lengthSq();
    if (L2 < 1e-12) return ts;
    for (const auto& e : others) {
        if (&e == &line) continue;
        if (isCurvedCutter(e)) {
            appendCurvedCuts(e, a, d, L2, 0.0, ts);
            continue;
        }
        forEachCutSegment(e, [&](const Vec2& p1, const Vec2& p2) {
            if (auto x = segmentSegmentIntersection(a, b, p1, p2)) {
                const double t = dot(*x - a, d) / L2;
                if (t > 1e-6 && t < 1.0 - 1e-6) ts.push_back(t);
            }
        });
    }
    return ts;
}
} // namespace

namespace {
// Angles (radians) where the cutting edges cross circle (C,R). Every cutter is
// reduced to segments (forEachCutSegment), so this handles all cutter types.
std::vector<double> circleCutAngles(const Vec2& C, double R, const Entity& self,
                                    const std::vector<Entity>& cutters) {
    std::vector<double> angs;
    for (const auto& e : cutters) {
        if (&e == &self || (!self.id.isEmpty() && e.id == self.id)) continue; // not self
        forEachCutSegment(e, [&](const Vec2& a, const Vec2& b) {
            const Vec2 d = b - a;
            const double L2 = d.lengthSq();
            if (L2 < 1e-12) return;
            for (const auto& x : lineCircleIntersection(a, d, C, R)) {
                const double t = dot(x - a, d) / L2;
                if (t < -1e-9 || t > 1.0 + 1e-9) continue; // outside the cutter segment
                angs.push_back(std::atan2(x.y - C.y, x.x - C.x));
            }
        });
    }
    return angs;
}
// The parameters, on an ellipse, where the cutters cross it. Same shape as circleCutAngles: only
// crossings that lie ON a cutter's own segment count, so a line that stops short of the ellipse does
// not cut it.
std::vector<double> ellipseCutParams(const Entity& self, const std::vector<Entity>& cutters) {
    std::vector<double> out;
    for (const auto& e : cutters) {
        if (&e == &self || (!self.id.isEmpty() && e.id == self.id)) continue;   // not self
        forEachCutSegment(e, [&](const Vec2& a, const Vec2& b) {
            const Vec2 d = b - a;
            const double L2 = d.lengthSq();
            if (L2 < 1e-12) return;
            for (const auto& x : lineEllipseIntersection(a, d, self.center, self.radius, self.radiusY,
                                                         self.rotation)) {
                const double t = dot(x - a, d) / L2;
                if (t < -1e-9 || t > 1.0 + 1e-9) continue;   // beyond the cutter's own ends
                out.push_back(ellipseParamAt(x, self.center, self.radius, self.radiusY, self.rotation));
            }
        });
    }
    return out;
}

} // namespace

bool trimEllipseAt(Entity& e, const Vec2& pick, const std::vector<Entity>& cutters) {
    if (e.type != EntityType::Ellipse || e.radius < 1e-9 || e.radiusY < 1e-9) return false;
    auto norm = [](double a) {
        while (a < 0) a += 2.0 * M_PI;
        while (a >= 2.0 * M_PI) a -= 2.0 * M_PI;
        return a;
    };
    const bool whole = !e.meta.value(QStringLiteral("ellipticalArc")).toBool();
    const double pp = norm(ellipseParamAt(pick, e.center, e.radius, e.radiusY, e.rotation));

    std::vector<double> cuts;
    for (const double a : ellipseCutParams(e, cutters)) cuts.push_back(norm(a));
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end(),
                           [](double x, double y) { return std::abs(x - y) < 1e-7; }),
               cuts.end());

    if (whole) {
        // A whole ellipse: two crossings are needed to bound the stretch that comes off. The pick lies
        // between two of them (going round), and what is kept is everything else.
        if (cuts.size() < 2) return false;
        double lo = cuts.back(), hi = cuts.front();
        for (size_t i = 0; i + 1 < cuts.size(); ++i)
            if (pp >= cuts[i] && pp < cuts[i + 1]) { lo = cuts[i]; hi = cuts[i + 1]; break; }
        e.meta.insert(QStringLiteral("ellipticalArc"), true);
        e.startAngle = hi;
        e.endAngle = lo;   // round from hi to lo: the part that is not the picked one
        return true;
    }

    // Already a piece of one: measure along its own sweep, from its start.
    double sweep = e.endAngle - e.startAngle;
    while (sweep <= 0) sweep += 2.0 * M_PI;
    while (sweep > 2.0 * M_PI) sweep -= 2.0 * M_PI;
    auto rel = [&](double a) {
        double r = a - e.startAngle;
        while (r < 0) r += 2.0 * M_PI;
        while (r >= 2.0 * M_PI) r -= 2.0 * M_PI;
        return r;
    };
    const double up = rel(pp);
    if (up > sweep + 1e-9) return false;   // the pick is not on this piece
    std::vector<double> us{0.0, sweep};
    for (const double a : cuts) {
        const double u = rel(a);
        if (u > 1e-9 && u < sweep - 1e-9) us.push_back(u);
    }
    std::sort(us.begin(), us.end());
    us.erase(std::unique(us.begin(), us.end(),
                         [](double x, double y) { return std::abs(x - y) < 1e-7; }),
             us.end());
    if (us.size() < 3) return false;   // nothing crosses it, so there is nothing to take off
    size_t k = 0;
    for (; k + 1 < us.size(); ++k)
        if (up >= us[k] && up <= us[k + 1]) break;
    if (k + 1 >= us.size()) return false;
    // The piece the pick is on comes off. Trimming out of the MIDDLE would leave two pieces, which the
    // ends of the sweep cannot say, so the longer of the two is kept - the same choice the rest of
    // this file makes when a cut cannot be expressed.
    if (k == 0) {
        e.startAngle = e.startAngle + us[1];
    } else if (k + 2 == us.size()) {
        e.endAngle = e.startAngle + us[k];
    } else {
        const double before = us[k], after = sweep - us[k + 1];
        if (before >= after) e.endAngle = e.startAngle + us[k];
        else e.startAngle = e.startAngle + us[k + 1];
    }
    return true;
}

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

bool whollyInsideCircle(const Entity& e, const Entity& ref) {
    // Only a CLOSED boundary can have an inside: a whole circle or a whole ellipse. An arc of either
    // does not enclose anything, and treating one as though it did would delete objects that are
    // merely near it.
    const bool ring = ref.type == EntityType::Circle && ref.radius > 1e-9;
    const bool oval = ref.type == EntityType::Ellipse && ref.radius > 1e-9 && ref.radiusY > 1e-9 &&
                      !ref.meta.value(QStringLiteral("ellipticalArc")).toBool();
    if (!ring && !oval) return false;
    // A hair of tolerance, so an end drawn onto the boundary counts as inside rather than outside.
    const double R = ref.radius + 1e-6;
    auto inside = [&](const Vec2& p) {
        if (ring) return distance(p, ref.center) <= R;
        const double cc = std::cos(ref.rotation), ss = std::sin(ref.rotation);
        const Vec2 d = p - ref.center;
        const double lx = (d.x * cc + d.y * ss) / ref.radius;
        const double ly = (-d.x * ss + d.y * cc) / ref.radiusY;
        return std::hypot(lx, ly) <= 1.0 + 1e-9;
    };

    switch (e.type) {
    case EntityType::Line:
    case EntityType::DoubleLine:
        // A straight run is inside when both ends are: the ring is convex, so everything between
        // two inside points is inside too.
        return inside(e.a) && inside(e.b);
    case EntityType::Polyline:
    case EntityType::Spline:
    case EntityType::Wire:
    case EntityType::Rect:
    case EntityType::Polygon:
        if (e.points.size() < 2) return false;
        for (const Vec2& p : e.points)
            if (!inside(p)) return false;
        return true;
    case EntityType::Circle:
    case EntityType::Arc:
        // The whole of the smaller ring within the larger one. Only judged against a round boundary -
        // whether a circle fits inside an ellipse is not a distance.
        return ring && e.radius > 1e-9 && distance(e.center, ref.center) + e.radius <= R;
    default:
        return false;
    }
}

bool objectCrosses(const Entity& e, const std::vector<Entity>& others) {
    // A sleeve is the outline it draws, not its stored route or a plain pair of rails.
    if (const auto sleeve = shrunkSleeveRunsOf(e); !sleeve.empty()) {
        for (const auto& run : sleeve) {
            for (size_t i = 1; i < run.size(); ++i) {
                const Vec2 A = run[i - 1], B = run[i], D = B - A;
                const double L2 = D.lengthSq();
                for (const auto& c : others) {
                    if (!e.id.isEmpty() && c.id == e.id) continue;
                    if (isCurvedCutter(c) && L2 > 1e-12) {
                        std::vector<double> ts;
                        appendCurvedCuts(c, A, D, L2, 0.0, ts);
                        if (!ts.empty()) return true;
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
    // A HARNESS IS THE WIRES IT DRAWS, for the same reason a sleeve is its outline: its route runs
    // down the middle of the bundle and is never drawn, so asking whether THAT meets the reference
    // answers about a line nobody can see. A harness laid across a boundary meets it where its wires
    // do, which is what the eye is looking at.
    if (e.type == EntityType::TwistedPair) {
        std::vector<std::vector<Vec2>> drawn = twistedPairStrands(e);
        for (auto& outline : twistedPairBody(e)) drawn.push_back(outline);
        for (const auto& strand : drawn)
            for (size_t i = 1; i < strand.size(); ++i) {
                const Vec2 A = strand[i - 1], B = strand[i], D = B - A;
                const double L2 = D.lengthSq();
                for (const auto& c : others) {
                    if (!e.id.isEmpty() && c.id == e.id) continue;
                    if (isCurvedCutter(c) && L2 > 1e-12) {
                        std::vector<double> ts;
                        appendCurvedCuts(c, A, D, L2, 0.0, ts);
                        if (!ts.empty()) return true;
                        continue;
                    }
                    bool hit = false;
                    forEachCutSegment(c, [&](const Vec2& p1, const Vec2& p2) {
                        if (segmentSegmentIntersection(A, B, p1, p2)) hit = true;
                    });
                    if (hit) return true;
                }
            }
        return false;
    }
    // A DOUBLE run IS its two physical lines: they are what is drawn, and they are what a reference
    // meets. Asking its centreline instead answers "they do not meet" for a run whose two lines straddle
    // the reference - which is most of them, the centreline being half a gap away from each line - and
    // the object was then not even considered for trimming.
    const bool dbl = e.type == EntityType::DoubleLine ||
                     (e.type == EntityType::Polyline && e.meta.value("double").toBool());
    if (dbl) {
        const double gap = e.meta.value("gap").toDouble(10.0);
        const std::vector<Vec2> centre =
            e.type == EntityType::DoubleLine ? std::vector<Vec2>{e.a, e.b} : e.points;
        if (centre.size() >= 2 && gap > 0.0) {
            for (double s : {gap / 2.0, -gap / 2.0}) {
                std::vector<Vec2> rail = offsetPolyline(centre, s);
                applyDoubleRunCuts(rail, e, s > 0 ? +1 : -1);   // as drawn: the caps count here too
                for (size_t i = 1; i < rail.size(); ++i) {
                    for (const auto& c : others) {
                        if (!e.id.isEmpty() && c.id == e.id) continue;
                        const Vec2 A = rail[i - 1], B = rail[i];
                        if (isCurvedCutter(c)) {
                            const Vec2 D = B - A;
                            const double L2 = D.lengthSq();
                            if (L2 <= 1e-12) continue;
                            std::vector<double> ts;
                            appendCurvedCuts(c, A, D, L2, 0.0, ts);
                            if (!ts.empty()) return true;
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
        }
        // Its centreline may still be what meets the reference (a fence inside the gap).
    }
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
    if (e.type == EntityType::Ellipse) {
        for (const double t : ellipseCutParams(e, others)) {
            if (!e.meta.value(QStringLiteral("ellipticalArc")).toBool()) return true;
            double sweep = e.endAngle - e.startAngle;
            while (sweep < 0) sweep += 2.0 * M_PI;
            double rel = t - e.startAngle;
            while (rel < 0) rel += 2.0 * M_PI;
            if (rel <= sweep + 1e-9) return true;
        }
        return false;
    }
    if (e.type == EntityType::Polyline || e.type == EntityType::Spline ||
        e.type == EntityType::Wire || e.type == EntityType::Rect ||
        e.type == EntityType::Polygon) {
        // A rect and a polygon are closed, so their last edge - back to where they started - counts too.
        const bool closed = e.type == EntityType::Rect || e.type == EntityType::Polygon;
        const size_t edges = e.points.size() + (closed ? 1 : 0);
        for (size_t i = 1; i < edges; ++i) {
            const Vec2 A = e.points[i - 1], B = e.points[i % e.points.size()];
            const Vec2 D = B - A;
            const double L2 = D.lengthSq();
            for (const auto& c : others) {
                if (!e.id.isEmpty() && c.id == e.id) continue;
                // A curve is met exactly, not through the polygon it gets reduced to. Half an ellipse
                // ENDS on the line it was trimmed by, and a chord that ends on the run it is tested
                // against is the degenerate case that answers "they do not meet" - so a polyline lying
                // across a trimmed ellipse could not be trimmed by it at all.
                if (isCurvedCutter(c) && L2 > 1e-12) {
                    std::vector<double> ts;
                    appendCurvedCuts(c, A, D, L2, 0.0, ts);
                    if (!ts.empty()) return true;
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

std::vector<std::pair<Vec2, Vec2>> cutSegmentsThrough(const Vec2& p,
                                                     const std::vector<Entity>& cutters,
                                                     const QString& selfId) {
    // EVERY knee-to-knee segment through the point, nearest first. At a corner two of them pass
    // through it, and which one is wanted depends on what can actually be reached from it - a question
    // only the caller can answer, so it gets the choice rather than the nearest.
    std::vector<std::pair<double, std::pair<Vec2, Vec2>>> found;
    for (const auto& c : cutters) {
        if (!selfId.isEmpty() && c.id == selfId) continue;
        forEachCutSegment(c, [&](const Vec2& a, const Vec2& b) {
            const auto cp = closestPointOnSegment(p, a, b);
            if (cp.distance < 1e-4) found.push_back({cp.distance, {a, b}});
        });
    }
    std::stable_sort(found.begin(), found.end(),
                     [](const auto& x, const auto& y) { return x.first < y.first; });
    std::vector<std::pair<Vec2, Vec2>> out;
    for (const auto& f : found) out.push_back(f.second);
    return out;
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
    double bestErr = std::numeric_limits<double>::max();
    for (const auto& c : cutters) {
        if (!selfId.isEmpty() && c.id == selfId) continue;
        if (c.type != EntityType::Circle && c.type != EntityType::Arc) continue;
        const double err = std::abs(distance(p, c.center) - c.radius);
        // The crossing sits on a tessellated chord, ~radius*5e-4 inside the true ring.
        const double tol = 0.5 + c.radius * 0.01;
        if (err < tol && err < bestErr) { bestErr = err; best = std::make_pair(c.center, c.radius); }
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
            if (isCurvedCutter(c)) {
                std::vector<double> ts;
                appendCurvedCuts(c, A, D, L2, static_cast<double>(i), ts);
                for (double t : ts) crossings.push_back(t);
                continue;
            }
            forEachCutSegment(c, [&](const Vec2& p1, const Vec2& p2) {
                if (auto x = segmentSegmentIntersection(A, B, p1, p2)) {
                    const double t = dot(*x - A, D) / L2;
                    if (t > 1e-6 && t < 1.0 - 1e-6) crossings.push_back(i + t);
                }
            });
        }
    }
    if (crossings.empty()) return result; // nothing actually cut it

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
    // The removed span is bounded on each side of the pick by the nearest CUTTER CROSSING, and by
    // nothing else. A polyline's own corner is not a cutting edge: it is part of the thing being cut.
    // Bounding the removal at knees as well used to leave a stub every time the piece picked ran past a
    // bend - the span from the fence to the corner came away and the rest of what was picked stayed
    // behind - so trimming a bent run took two or three goes and left ends nobody asked for. Where the
    // removed span crosses a corner, the corner goes with it and the pieces either side keep their own.
    const std::vector<double>& bounds = crossings;
    auto nearestBelow = [&](double& out) {
        bool found = false;
        for (double g : bounds)
            if (g <= gp + 1e-9 && (!found || g > out)) { out = g; found = true; }
        return found;
    };
    auto nearestAbove = [&](double& out) {
        bool found = false;
        for (double g : bounds)
            if (g >= gp - 1e-9 && (!found || g < out)) { out = g; found = true; }
        return found;
    };
    double lo = 0.0, hi = static_cast<double>(n - 1);
    bool hasLo = nearestBelow(lo);
    bool hasHi = nearestAbove(hi);
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
        // As for a line: the whole span between the two boundaries goes, so a double line crossing a
        // circle is left clipped to the ring at both ends rather than stopping at the fence.
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
    auto cuts = intersectionCuts(line, others);
    if (cuts.empty()) return false;

    const Vec2 a = line.a, b = line.b, d = b - a;
    const double L2 = d.lengthSq();
    const double tp = std::clamp(dot(pick - a, d) / L2, 0.0, 1.0);
    auto P = [&](double t) { return a + d * t; };

    // Nearest cut on each side of the pick, and which edge made each.
    double lo = -1e9, hi = 1e9;
    bool hasLo = false, hasHi = false;
    const CutAt* loCut = nullptr;
    const CutAt* hiCut = nullptr;
    for (const CutAt& c : cuts) {
        if (c.g <= tp && c.g > lo) { lo = c.g; hasLo = true; loCut = &c; }
        if (c.g >= tp && c.g < hi) { hi = c.g; hasHi = true; hiCut = &c; }
    }
    if (hasLo && hasHi && hi - lo > 1e-6) {
        // The piece between the two boundaries is what goes - including when they are the two
        // crossings of one circle, so everything inside the ring is removed and both ends of what is
        // left sit ON the ring. Stopping at the fence instead leaves a piece inside the circle with a
        // raw end where the fence happened to fall, which is not a trim.
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

namespace {

// Where a point sits along a path: which segment, and how far along it. The distance is what says whether the
// point is on the path at all.
struct AlongPath {
    int segment = -1;
    double t = 0.0;
    double distance = 1e18;
};

AlongPath alongPath(const std::vector<Vec2>& pts, const Vec2& q) {
    AlongPath best;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const Vec2 a = pts[i], b = pts[i + 1];
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double len2 = dx * dx + dy * dy;
        double t = 0.0;
        if (len2 > 1e-18) t = std::clamp(((q.x - a.x) * dx + (q.y - a.y) * dy) / len2, 0.0, 1.0);
        const Vec2 on{a.x + dx * t, a.y + dy * t};
        const double d = std::hypot(q.x - on.x, q.y - on.y);
        if (d < best.distance) {
            best.distance = d;
            best.segment = static_cast<int>(i);
            best.t = t;
        }
    }
    return best;
}

double pathLength(const std::vector<Vec2>& pts) {
    double sum = 0.0;
    for (size_t i = 0; i + 1 < pts.size(); ++i) sum += std::hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
    return sum;
}

} // namespace

bool cutPathBetweenPoints(const Entity& path, const Vec2& p1, const Vec2& p2, double tol,
                          std::vector<std::vector<Vec2>>& pieces) {
    std::vector<Vec2> pts;
    if (path.type == EntityType::Line) pts = {path.a, path.b};
    else if (path.type == EntityType::Polyline || path.type == EntityType::Wire) pts = path.points;
    if (pts.size() < 2) return false;

    AlongPath a = alongPath(pts, p1);
    AlongPath b = alongPath(pts, p2);
    if (a.distance > tol || b.distance > tol) return false;
    // In the order they come along the path, not the order they were given: a symbol can be dropped either
    // way round, and the piece between them is the same piece.
    if (b.segment < a.segment || (b.segment == a.segment && b.t < a.t)) std::swap(a, b);

    auto at = [&pts](const AlongPath& w) {
        const Vec2 s = pts[static_cast<size_t>(w.segment)], e = pts[static_cast<size_t>(w.segment) + 1];
        return Vec2{s.x + (e.x - s.x) * w.t, s.y + (e.y - s.y) * w.t};
    };
    const Vec2 cutA = at(a), cutB = at(b);

    std::vector<Vec2> first, second;
    for (int i = 0; i <= a.segment; ++i) first.push_back(pts[static_cast<size_t>(i)]);
    first.push_back(cutA);
    second.push_back(cutB);
    for (size_t i = static_cast<size_t>(b.segment) + 1; i < pts.size(); ++i) second.push_back(pts[i]);

    pieces.clear();
    // A piece of no length is not a piece: a cut exactly at an end leaves nothing that side, and leaving a
    // zero-length line behind is worse than leaving a gap - it cannot be seen and it can be picked.
    if (pathLength(first) > tol) pieces.push_back(std::move(first));
    if (pathLength(second) > tol) pieces.push_back(std::move(second));
    return true;
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
        if (isCurvedCutter(e)) {
            // Exactly onto the curve, for the same reason trimming is: a chord of the approximating
            // polygon stops inside it, so the extended end lands short of what it was extended to.
            const std::vector<Vec2> xs =
                e.type == EntityType::Ellipse
                    ? lineEllipseIntersection(moving, dir, e.center, e.radius, e.radiusY, e.rotation)
                    : lineCircleIntersection(moving, dir, e.center, e.radius);
            for (const Vec2& x : xs) {
                if (!onCutterCurve(e, x)) continue;
                const double t = dot(x - moving, dir);
                if (t <= 1e-9) continue;              // must be BEYOND the end being extended
                if (t < bestT) { bestT = t; bestPt = x; found = true; }
            }
            continue;
        }
        // Every other boundary type reduced to segments.
        forEachCutSegment(e, tryBoundary);
    }
    if (!found) return false;
    if (extendA) line.a = bestPt; else line.b = bestPt;
    return true;
}

namespace {
// First boundary point reached by the ray from `fixed` through `moving` (beyond
// `moving`). All boundary types are reduced to segments (curves tessellated).
// The boundary a ray reached: the touch point, plus the target itself (a circle/arc
// ring, or a straight edge) so the double-line runs can be capped flush to it.
struct BoundaryHit {
    Vec2 pt;
    bool circle = false;
    Vec2 center;
    double radius = 0.0;
    std::vector<std::pair<Vec2, Vec2>> segs; // all edges of the target (when not a circle)
};

std::optional<BoundaryHit> rayToBoundary(const Vec2& fixed, const Vec2& moving,
                                         const std::vector<Entity>& others, const QString& selfId) {
    const Vec2 dir = (moving - fixed).normalized();
    if (dir.lengthSq() < 1e-12) return std::nullopt;
    double bestT = std::numeric_limits<double>::max();
    BoundaryHit best;
    bool found = false;
    for (const auto& e : others) {
        if (!selfId.isEmpty() && e.id == selfId) continue;
        // This entity's nearest valid crossing along the ray beyond `moving`.
        double et = std::numeric_limits<double>::max();
        Vec2 ept;
        bool ehit = false;
        forEachCutSegment(e, [&](const Vec2& p1, const Vec2& p2) {
            auto x = lineLineIntersection(fixed, moving, p1, p2);
            if (!x) return;
            const double t = dot(*x - moving, dir);
            if (t <= 1e-9) return; // must be beyond `moving`
            if (closestPointOnSegment(*x, p1, p2).distance > 1e-6) return; // on the edge
            if (t < et) { et = t; ept = *x; ehit = true; }
        });
        if (ehit && et < bestT) {
            bestT = et;
            found = true;
            best.pt = ept;
            best.segs.clear();
            if (e.type == EntityType::Circle || e.type == EntityType::Arc) {
                best.circle = true; best.center = e.center; best.radius = e.radius;
            } else {
                // Capture EVERY edge of the target so each physical run caps to the
                // edge it actually crosses (no gap near a corner/vertex).
                best.circle = false;
                forEachCutSegment(e, [&](const Vec2& p1, const Vec2& p2) {
                    best.segs.emplace_back(p1, p2);
                });
            }
        }
    }
    return found ? std::optional<BoundaryHit>(best) : std::nullopt;
}

// Record the exact boundary a double entity was extended to, so the renderer caps its
// two physical runs flush to it — a circle ring, or the set of target edges (each run
// is clipped to whichever edge it crosses, so a corner never leaves a gap).
void setExtendCap(Entity& e, const QString& key, const BoundaryHit& h) {
    for (const char* s : {"", "Kind", "Ax", "Ay", "Bx", "By", "Cx", "Cy", "R", "Poly"})
        e.meta.remove(key + QString::fromLatin1(s));
    e.meta.insert(key, true);
    if (h.circle) {
        e.meta.insert(key + "Kind", QStringLiteral("circle"));
        e.meta.insert(key + "Cx", h.center.x);
        e.meta.insert(key + "Cy", h.center.y);
        e.meta.insert(key + "R", h.radius);
    } else {
        e.meta.insert(key + "Kind", QStringLiteral("poly"));
        QJsonArray segs; // flat [ax,ay,bx,by, ...] per edge
        for (const auto& s : h.segs) {
            segs.append(s.first.x); segs.append(s.first.y);
            segs.append(s.second.x); segs.append(s.second.y);
        }
        e.meta.insert(key + "Poly", segs);
    }
}
} // namespace

bool extendEntityTo(Entity& e, const Vec2& pick, const std::vector<Entity>& others) {
    if (e.type == EntityType::Line || e.type == EntityType::DoubleLine) {
        const bool extendA = distance(pick, e.a) < distance(pick, e.b);
        const Vec2 fixed = extendA ? e.b : e.a;
        const Vec2 moving = extendA ? e.a : e.b;
        auto r = rayToBoundary(fixed, moving, others, e.id);
        if (!r) return false;
        if (extendA) e.a = r->pt; else e.b = r->pt;
        // Cap the double line's physical runs flush to the boundary it reached.
        if (e.type == EntityType::DoubleLine)
            setExtendCap(e, extendA ? QStringLiteral("cutStart") : QStringLiteral("cutEnd"), *r);
        return true;
    }
    // A HARNESS IS ITS ROUTE. The object is parametric - a route and the figures the wires are laid
    // out from - so making it longer means making the route longer, and the wires, the fan and the
    // cut ends are all laid out again from it. Its a and b are the route's two ends and are kept
    // level with it, because everything that reads a run's ends without unpacking it reads those.
    if (e.type == EntityType::TwistedPair) {
        std::vector<Vec2> route = e.points.size() >= 2 ? e.points : std::vector<Vec2>{e.a, e.b};
        const int n = static_cast<int>(route.size());
        if (n < 2) return false;
        const bool front = distance(pick, route.front()) < distance(pick, route.back());
        const Vec2 moving = front ? route.front() : route.back();
        const Vec2 fixed = front ? route[1] : route[n - 2];
        auto r = rayToBoundary(fixed, moving, others, e.id);
        if (!r) return false;
        if (front) route.front() = r->pt; else route.back() = r->pt;
        e.points = route;
        e.a = route.front();
        e.b = route.back();
        return true;
    }
    if (e.type == EntityType::Polyline || e.type == EntityType::Spline ||
        e.type == EntityType::Wire) {
        const int n = static_cast<int>(e.points.size());
        if (n < 2) return false;
        const bool front = distance(pick, e.points.front()) < distance(pick, e.points.back());
        const Vec2 moving = front ? e.points.front() : e.points.back();
        const Vec2 fixed = front ? e.points[1] : e.points[n - 2];
        auto r = rayToBoundary(fixed, moving, others, e.id);
        if (!r) return false;
        if (front) e.points.front() = r->pt; else e.points.back() = r->pt;
        // A double polyline caps its physical runs flush to the boundary too. A sleeve instead
        // derives its whole outline from the route just moved, so no stale rail cap belongs on it.
        if (e.meta.value("double").toBool() && !e.meta.value("shrunkEnds").toBool())
            setExtendCap(e, front ? QStringLiteral("cutStart") : QStringLiteral("cutEnd"), *r);
        return true;
    }
    return false;
}

// ---- Array (ARRAYRECT / ARRAYPOLAR / ARRAYPATH) ----------------------------

namespace {

constexpr double kArrPi = 3.14159265358979323846;
// Ceiling on generated items, so a mistyped count (or a tiny "Between" on a long
// path) can never lock the app up building millions of clones.
constexpr int kMaxArrayItems = 4096;

Vec2 rotatedAbout(const Vec2& p, const Vec2& pivot, double angle) {
    const double cs = std::cos(angle), sn = std::sin(angle);
    const Vec2 d = p - pivot;
    return {pivot.x + d.x * cs - d.y * sn, pivot.y + d.x * sn + d.y * cs};
}

// Per-corner fillet radii / chamfer setbacks of a polyline, as the renderer reads
// them — so a path array follows the outline actually drawn.
void cornerMods(const Entity& e, std::vector<double>& radii, std::vector<double>& chamIn,
                std::vector<double>& chamOut, bool& any) {
    const size_t n = e.points.size();
    radii.assign(n, 0.0);
    chamIn.assign(n, 0.0);
    chamOut.assign(n, 0.0);
    any = false;
    const QJsonArray rArr = e.meta.value("cornerRadii").toArray();
    for (size_t i = 0; i < n && i < static_cast<size_t>(rArr.size()); ++i) {
        radii[i] = rArr[static_cast<int>(i)].toDouble();
        if (radii[i] > 0.0) any = true;
    }
    const QJsonArray cArr = e.meta.value("cornerChamfers").toArray();
    const QJsonArray cArr2 = e.meta.value("cornerChamfers2").toArray();
    for (size_t i = 0; i < n && i < static_cast<size_t>(cArr.size()); ++i) {
        chamIn[i] = cArr[static_cast<int>(i)].toDouble();
        chamOut[i] = (i < static_cast<size_t>(cArr2.size()))
                         ? cArr2[static_cast<int>(i)].toDouble()
                         : chamIn[i];
        if (chamIn[i] > 0.0 && chamOut[i] > 0.0) any = true;
    }
}

} // namespace

Vec2 arrayPivot(const ArraySpec& s, const Vec2& base) {
    return s.kind == ArrayKind::Polar ? s.center : base;
}

std::vector<Vec2> arrayPathPoints(const Entity& e, bool* closedOut) {
    std::vector<Vec2> pts;
    bool closed = false;
    auto onCircle = [&](double a) {
        return Vec2{e.center.x + e.radius * std::cos(a), e.center.y + e.radius * std::sin(a)};
    };
    switch (e.type) {
    case EntityType::Line:
    case EntityType::DoubleLine: // its centreline
        pts = {e.a, e.b};
        break;
    case EntityType::Polyline:
    case EntityType::Wire: {
        if (e.points.size() < 2) break;
        const QJsonArray bulgeArr = e.meta.value("bulges").toArray();
        std::vector<double> radii, chamIn, chamOut;
        bool anyMod = false;
        cornerMods(e, radii, chamIn, chamOut, anyMod);
        if (!bulgeArr.isEmpty()) {
            std::vector<double> bulges;
            for (const auto& v : bulgeArr) bulges.push_back(v.toDouble());
            pts = expandBulgePolyline(e.points, bulges);
        } else if (anyMod) {
            pts = filletChamferPolyline(e.points, radii, chamIn, chamOut, false);
        } else {
            pts = e.points;
        }
        break;
    }
    case EntityType::Spline:
        if (e.points.size() < 2) break;
        pts = e.meta.value("splineMode").toString() == QLatin1String("cv")
                  ? bsplinePoints(e.points)
                  : splinePoints(e.points);
        break;
    case EntityType::Rect:
    case EntityType::Polygon: {
        if (e.points.size() < 3) break;
        std::vector<double> radii, chamIn, chamOut;
        bool anyMod = false;
        cornerMods(e, radii, chamIn, chamOut, anyMod);
        pts = anyMod ? filletChamferPolyline(e.points, radii, chamIn, chamOut, true) : e.points;
        pts.push_back(pts.front()); // close the loop so the last edge carries items too
        closed = true;
        break;
    }
    case EntityType::Circle: {
        if (e.radius <= 0.0) break;
        const int n = 180;
        for (int i = 0; i <= n; ++i) pts.push_back(onCircle(2.0 * kArrPi * i / n));
        closed = true;
        break;
    }
    case EntityType::Arc: {
        if (e.radius <= 0.0) break;
        double sweep = e.endAngle - e.startAngle;
        while (sweep <= 0.0) sweep += 2.0 * kArrPi;
        const int n = std::max(16, static_cast<int>(std::ceil(sweep / (kArrPi / 90.0))));
        for (int i = 0; i <= n; ++i) pts.push_back(onCircle(e.startAngle + sweep * i / n));
        break;
    }
    case EntityType::Ellipse: {
        if (e.radius <= 0.0 || e.radiusY <= 0.0) break;
        const double cs = std::cos(e.rotation), sn = std::sin(e.rotation);
        auto ept = [&](double t) {
            const double lx = e.radius * std::cos(t), ly = e.radiusY * std::sin(t);
            return Vec2{e.center.x + lx * cs - ly * sn, e.center.y + lx * sn + ly * cs};
        };
        double sa = 0.0, ea = 2.0 * kArrPi;
        const bool arc = e.meta.value("ellipticalArc").toBool();
        if (arc) {
            sa = e.startAngle;
            ea = e.endAngle;
            while (ea <= sa) ea += 2.0 * kArrPi;
        }
        const int n = 180;
        for (int i = 0; i <= n; ++i) pts.push_back(ept(sa + (ea - sa) * i / n));
        closed = !arc;
        break;
    }
    default:
        break;
    }
    if (pts.size() < 2 || polylineLength(pts) < 1e-9) pts.clear();
    if (closedOut) *closedOut = closed && !pts.empty();
    return pts;
}

double arrayPathStep(const ArraySpec& s, double pathLength, bool pathClosed) {
    if (!s.divide) return s.pathSpacing;
    const int count = std::max(1, s.count);
    if (count < 2) return 0.0;
    // A closed path rejoins itself, so the last gap is the one back to the first item.
    return pathClosed ? pathLength / count : pathLength / (count - 1);
}

std::vector<ArrayPlacement> arrayPlacements(const ArraySpec& s, const Vec2& base,
                                            const std::vector<Vec2>& path, bool pathClosed) {
    std::vector<ArrayPlacement> out;
    const int count = std::max(1, s.count);
    const int rows = std::max(1, s.rows);

    switch (s.kind) {
    case ArrayKind::Rect: {
        // Both axes turn together with the array angle, so a rotated grid stays
        // rectangular (AutoCAD's "Axis angle").
        const double a = s.axisAngle * kArrPi / 180.0;
        const Vec2 colAxis{std::cos(a), std::sin(a)};
        const Vec2 rowAxis = colAxis.perp();
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < count; ++c) {
                ArrayPlacement pl;
                pl.delta = colAxis * (c * s.colSpacing) + rowAxis * (r * s.rowSpacing);
                out.push_back(pl);
                if (static_cast<int>(out.size()) >= kMaxArrayItems) return out;
            }
        break;
    }
    case ArrayKind::Polar: {
        // A full sweep divides by the item count (no duplicate item on top of the
        // first); a partial sweep spans it end to end, dividing by count-1.
        const bool full = std::abs(s.fillAngle) >= 360.0 - 1e-9;
        double stepDeg = 0.0;
        if (count > 1) stepDeg = full ? s.fillAngle / count : s.fillAngle / (count - 1);
        const double dir = s.ccw ? 1.0 : -1.0;
        const double baseRadius = (base - s.center).length();
        for (int ring = 0; ring < rows; ++ring) {
            const double ringOff = ring * s.rowSpacing;
            for (int i = 0; i < count; ++i) {
                const double th = dir * stepDeg * i * kArrPi / 180.0;
                const Vec2 orbit = rotatedAbout(base, s.center, th);
                // Rings step outward along the item's OWN radius, so each ring is a
                // concentric copy of the first.
                const Vec2 outward = baseRadius > 1e-9 ? (orbit - s.center).normalized()
                                                       : Vec2{1.0, 0.0};
                ArrayPlacement pl;
                if (s.rotateItems) {
                    // Turning the source about the centre both orbits and spins it —
                    // exactly AutoCAD's "Rotate items".
                    pl.rotate = th;
                    pl.delta = outward * ringOff;
                } else {
                    pl.delta = orbit + outward * ringOff - base;
                }
                out.push_back(pl);
                if (static_cast<int>(out.size()) >= kMaxArrayItems) return out;
            }
        }
        break;
    }
    case ArrayKind::Path: {
        if (path.size() < 2) break;
        const double total = polylineLength(path);
        if (total < 1e-9) break;
        const double step = arrayPathStep(s, total, pathClosed);
        Vec2 firstPt, firstTan;
        if (!samplePolyline(path, s.startOffset, pathClosed, firstPt, firstTan)) break;
        const double refAngle = angleOf(firstTan);
        for (int row = 0; row < rows; ++row) {
            for (int i = 0; i < count; ++i) {
                const double d = s.startOffset + i * step;
                // Items that run past the end of an open path simply don't exist
                // (AutoCAD stops rather than piling them on the last point).
                if (!pathClosed && (d < -1e-9 || d > total + 1e-9)) continue;
                Vec2 pt, tan;
                if (!samplePolyline(path, d, pathClosed, pt, tan)) continue;
                ArrayPlacement pl;
                // Aligned items turn by the tangent's change from the first item, so
                // item 0 keeps the source's own orientation.
                if (s.alignToPath) pl.rotate = angleOf(tan) - refAngle;
                pl.delta = pt + tan.perp() * (row * s.rowSpacing) - base;
                out.push_back(pl);
                if (static_cast<int>(out.size()) >= kMaxArrayItems) return out;
            }
        }
        break;
    }
    }
    return out;
}

Entity arrayItem(const Entity& src, const ArrayPlacement& pl, const Vec2& pivot) {
    Entity clone = src;
    clone.id = newId();
    if (std::abs(pl.rotate) > 1e-12) rotateEntity(clone, pivot, pl.rotate);
    if (pl.delta.lengthSq() > 1e-24) translateEntity(clone, pl.delta);
    return clone;
}

std::vector<Entity> rectArray(const Entity& src, int rows, int cols,
                              double dx, double dy) {
    ArraySpec spec;
    spec.kind = ArrayKind::Rect;
    spec.rows = rows;
    spec.count = cols;
    spec.colSpacing = dx;
    spec.rowSpacing = dy;
    const auto places = arrayPlacements(spec, {0.0, 0.0});
    std::vector<Entity> out;
    out.reserve(places.empty() ? 0 : places.size() - 1);
    for (size_t i = 1; i < places.size(); ++i) // [0] is the source itself, left in place
        out.push_back(arrayItem(src, places[i], {0.0, 0.0}));
    return out;
}

// Break a compound object into its drawn primitives (AutoCAD EXPLODE): double lines
// become their two physical lines, double polylines both offset runs, fillets are
// tessellated, connectors become body + pin stubs + labels, and simple polylines/
// rects/polygons become straight segments. Types that are already primitive (line,
// circle, arc, text, ...) return empty.
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
    case EntityType::TwistedPair:
        // A HARNESS COMES APART INTO THE LINES IT DRAWS: every wire's two edges, and the line across
        // each end where it is cut. Not into its route, which is never drawn - a harness broken into
        // a single invisible line down the middle of where it used to be is not what anybody means.
        //
        // What comes out is no longer a harness: the wires are lines now, and the count and the
        // pitch and the fan that made them are gone with the object. That is what breaking a thing
        // apart IS, and it is the only honest thing to do with half a harness - see the trim.
        for (const auto& strand : twistedPairStrands(e)) polylineToLines(strand, false);
        for (const auto& cap : twistedPairEndCaps(e))
            if (cap.size() == 2) makeLine(cap.front(), cap.back());
        for (const auto& outline : twistedPairBody(e)) polylineToLines(outline, false);
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
        // Pin forks (the ">" at the body edge) + stubs + labels (match the renderer).
        const double xDir = spec.mirrorX ? -1.0 : 1.0;
        const double fork = std::min(8.0, g.effectiveWidth * 0.28);
        const double forkY = std::min(spec.pitch * 0.36, spec.textSize * 0.85);
        for (int i = 0; i < spec.pinCount; ++i) {
            const Vec2 tip = g.pinTip[i];
            const double rootX = tip.x - xDir * fork;
            makeLine({rootX, tip.y + forkY}, tip);
            makeLine({rootX, tip.y - forkY}, tip);
            makeLine(tip, g.pinEnd[i]);
            if (i < spec.pins.size()) makeText(g.pinLabelPos[i], spec.pins[i], spec.pinSize * 0.7);
        }
        makeText(g.designatorPos, spec.designator, spec.desSize);
        makeText(g.partPos, spec.partNumber, spec.pnSize * 0.8);
        break;
    }
    default:
        break; // already a single primitive (line / circle / arc / text / ...)
    }
    return out;
}

} // namespace Edraw_Desktop
