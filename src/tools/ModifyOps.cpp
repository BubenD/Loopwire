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
        case EntityType::DoubleLine:
            addSeg(e.a, e.b);
            break;
        case EntityType::Polyline:
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
        if (e.type == EntityType::Line || e.type == EntityType::DoubleLine) tryBoundary(e.a, e.b);
        else if (e.type == EntityType::Rect || e.type == EntityType::Polygon)
            for (size_t i = 0; i < e.points.size(); ++i)
                tryBoundary(e.points[i], e.points[(i + 1) % e.points.size()]);
        else if (e.type == EntityType::Polyline)
            for (size_t i = 1; i < e.points.size(); ++i) tryBoundary(e.points[i - 1], e.points[i]);
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
