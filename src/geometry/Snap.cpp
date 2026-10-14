#include "geometry/Snap.h"

#include "geometry/Geometry.h"
#include "harness/Harness.h" // connector geometry for pin/corner snap joints
#include "model/Document.h"
#include "model/Entity.h"

#include <QJsonArray>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace Loopwire {

namespace {

// World coordinates of a table's grid lines: column boundaries xb[0..cols] (left ->
// right) and row boundaries yb[0..rows] (top -> down, world y-up). False if `e` isn't
// a table with fraction arrays.
bool tableGridLines(const Entity& e, std::vector<double>& xb, std::vector<double>& yb) {
    if (e.type != EntityType::Rect || !e.meta.value("table").toBool() || e.points.size() != 4)
        return false;
    const QJsonArray cf = e.meta.value("colFrac").toArray();
    const QJsonArray rf = e.meta.value("rowFrac").toArray();
    if (cf.isEmpty() || rf.isEmpty()) return false;
    double minX = e.points[0].x, maxX = e.points[0].x;
    double minY = e.points[0].y, maxY = e.points[0].y;
    for (const auto& p : e.points) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    const double w = maxX - minX, h = maxY - minY;
    xb.clear(); yb.clear();
    double x = minX; xb.push_back(x);
    for (const auto& v : cf) { x += v.toDouble() * w; xb.push_back(x); }
    double y = maxY; yb.push_back(y);
    for (const auto& v : rf) { y -= v.toDouble() * h; yb.push_back(y); }
    return true;
}

// Collect the segment list that represents an entity's linear skeleton, used
// for midpoint/nearest/intersection snapping.
void entitySegments(const Entity& e, std::vector<std::pair<Vec2, Vec2>>& out) {
    // A HEATSHRINK SLEEVE SNAPS TO ITS OUTLINE. It is a double polyline by type, so the rails below
    // would be worked out from its route at one width from end to end - which is not the shape it
    // draws: a sleeve recovers down to a narrower face at each end. The cursor caught a line that
    // was never on the screen. See shrunkSleeveOutlineOf.
    if (const auto sleeve = shrunkSleeveRunsOf(e); !sleeve.empty()) {
        for (const auto& run : sleeve)
            for (size_t i = 1; i < run.size(); ++i) out.emplace_back(run[i - 1], run[i]);
        return;
    }
    switch (e.type) {
    case EntityType::Line:
        out.emplace_back(e.a, e.b);
        break;
    case EntityType::ConnectionPin:
        if (connectionPinIsStub(e)) out.emplace_back(e.a, e.b);   // the stub is a line
        break;
    case EntityType::TwistedPair: {
        // EVERY LINE THE HARNESS IS DRAWN AS: each wire's two edges, curve and all, the cut ends, the
        // body and its boots, a laid-up bundle's outline, the heatshrink on a wire's end. A dimension
        // is taken to the edge of a wire wherever it is, and to where two wires cross - not only to
        // the straight stretches, which were all a laid-up bundle or a fan's bend ever offered.
        //
        // Never the ROUTE, the line down the middle of the bundle, which is not drawn: the cursor
        // caught something between the wires and on none of them.
        //
        // A curve is many little facets, and each facet's MIDDLE is no place on the drawing at all -
        // so midpoints are taken from the straight stretches alone (see twistedPairMidSegments). What
        // the facets cost a mouse movement is kept down by the reach box (snapReachBox) and by the
        // crossing test, which only ever looks at the segments under the cursor.
        if (const RunSnap* snap = twistedPairSnap(e)) {
            out.insert(out.end(), snap->lines.begin(), snap->lines.end());
            for (const std::vector<Vec2>& ring : twistedPairEndSleeves(e))
                for (size_t i = 1; i < ring.size(); ++i) out.emplace_back(ring[i - 1], ring[i]);
            break;
        }
        const std::vector<Vec2>& along =
            e.points.size() >= 2 ? e.points : std::vector<Vec2>{e.a, e.b};
        for (size_t i = 1; i < along.size(); ++i) out.emplace_back(along[i - 1], along[i]);
        break;
    }
    case EntityType::DoubleLine: {
        // Only the two VISIBLE parallel lines — NOT the centerline. A centerline
        // segment would put a phantom midpoint/nearest snap in the empty gap
        // between the lines, off the grips, so grabbing a visible end would snap
        // the cursor away and miss the joint.
        const double gap = e.meta.value("gap").toDouble(10.0);
        const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
        out.emplace_back(e.a + off, e.b + off);
        out.emplace_back(e.a - off, e.b - off);
        break;
    }
    case EntityType::Polyline:
        // A DOUBLE polyline snaps to its two VISIBLE runs (so you can measure from
        // anywhere along the lines), not the centerline in the empty gap.
        if (e.meta.value("double").toBool() && e.points.size() >= 2) {
            const double gap = e.meta.value("gap").toDouble(10.0);
            for (double s : {gap / 2.0, -gap / 2.0}) {
                const auto run = offsetPolyline(e.points, s);
                for (size_t i = 1; i < run.size(); ++i) out.emplace_back(run[i - 1], run[i]);
            }
            break;
        }
        [[fallthrough]];
    case EntityType::Spline:
    case EntityType::Wire:
        for (size_t i = 1; i < e.points.size(); ++i)
            out.emplace_back(e.points[i - 1], e.points[i]);
        break;
    case EntityType::Rect:
    case EntityType::Polygon:
    case EntityType::Hatch:
    case EntityType::Wipeout:
    case EntityType::Image:
        for (size_t i = 0; i < e.points.size(); ++i)
            out.emplace_back(e.points[i], e.points[(i + 1) % e.points.size()]);
        // A table's INTERIOR grid lines are deliberately not snapped to.
        //
        // They used to be, broken per cell so every cell edge offered its own endpoint, midpoint and
        // nearest. On a real table that is thousands of snap points packed a row apart: a bill of
        // materials of thirty-four rows by six columns put a marker on top of every cell corner and
        // every cell edge midpoint, so the cursor could not be moved across the table without being
        // caught, and nothing behind the table could be reached at all. The grid is a thing to read and
        // type into, not a thing to draw to. The table's OUTLINE is still snapped to, above, like any
        // other rectangle - which is what anybody lining a drawing up against a table actually wants.
        break;
    case EntityType::Connector: {
        // Body-rectangle edges + each pin stub, so nearest / midpoint snap works along
        // the connector (the corner + pin-end joints come from entityEndpoints).
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
        const ConnectorGeometry g = connectorGeometry(e.a, spec);
        const Vec2 bl = g.rectMin, tr = g.rectMax;
        const Vec2 br{tr.x, bl.y}, tl{bl.x, tr.y};
        out.emplace_back(bl, br);
        out.emplace_back(br, tr);
        out.emplace_back(tr, tl);
        out.emplace_back(tl, bl);
        for (size_t i = 0; i < g.pinTip.size() && i < g.pinEnd.size(); ++i)
            out.emplace_back(g.pinTip[i], g.pinEnd[i]);
        break;
    }
    case EntityType::Dimension: {
        // The drawn dimension line (+ extension lines) so midpoint/nearest snap works.
        const QString ty = e.meta.value("dimType").toString(QStringLiteral("linear"));
        if ((ty == QLatin1String("linear") || ty == QLatin1String("aligned") ||
             ty == QLatin1String("arclength")) &&
            e.points.size() >= 3) {
            const Vec2 p1 = e.points[0], p2 = e.points[1], off = e.points[2];
            Vec2 d1, d2;
            if (ty == QLatin1String("linear")) {
                const Vec2 mid = lerp(p1, p2, 0.5);
                const bool h = std::abs(off.y - mid.y) >= std::abs(off.x - mid.x);
                if (h) { d1 = {p1.x, off.y}; d2 = {p2.x, off.y}; }
                else { d1 = {off.x, p1.y}; d2 = {off.x, p2.y}; }
            } else {
                const Vec2 n = (p2 - p1).normalized().perp();
                const double t = dot(off - p1, n);
                d1 = p1 + n * t;
                d2 = p2 + n * t;
            }
            out.emplace_back(d1, d2);   // dimension line
            out.emplace_back(p1, d1);   // extension lines
            out.emplace_back(p2, d2);
        }
        break;
    }
    default:
        break;
    }
}

void entityEndpoints(const Entity& e, std::vector<Vec2>& out) {
    // AND ITS CORNERS ARE THE OUTLINE'S. The corners of a sleeve are where it recovers - the face
    // corners and the two shoulders at each end - and those are points somebody genuinely wants to
    // catch. The route's own two ends are inside the shape and are not drawn.
    if (const auto sleeve = shrunkSleeveRunsOf(e); !sleeve.empty()) {
        for (const auto& run : sleeve)
            for (const Vec2& p : run) out.push_back(p);
        return;
    }
    switch (e.type) {
    case EntityType::Line:
        out.push_back(e.a);
        out.push_back(e.b);
        break;
    case EntityType::TwistedPair:
        // Every place a drawn line of it ends or turns a corner: the wire ends, a fan's knees, the
        // step where the insulation stops, the corners of a body and its boots. Not the centre of the
        // run, which is between the wires and on none of them.
        if (const RunSnap* snap = twistedPairSnap(e)) {
            out.insert(out.end(), snap->points.begin(), snap->points.end());
            for (const std::vector<Vec2>& ring : twistedPairEndSleeves(e))
                for (size_t i = 0; i + 1 < ring.size(); ++i) out.push_back(ring[i]);
        }
        break;
    case EntityType::DoubleLine: {
        // The four ends of the two VISIBLE parallel lines only — no centerline
        // ends, which would float in the gap between the lines (off the grips)
        // and pull the cursor away when grabbing a visible joint.
        const double gap = e.meta.value("gap").toDouble(10.0);
        const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
        out.push_back(e.a + off);
        out.push_back(e.a - off);
        out.push_back(e.b + off);
        out.push_back(e.b - off);
        break;
    }
    case EntityType::Circle:
        // The 4 quadrant joints (top/bottom/left/right), like AutoCAD QUAdrant snap.
        out.push_back({e.center.x + e.radius, e.center.y});
        out.push_back({e.center.x - e.radius, e.center.y});
        out.push_back({e.center.x, e.center.y + e.radius});
        out.push_back({e.center.x, e.center.y - e.radius});
        // A balloon's leader joints: the attach point, the landing knee, and each
        // arrow tip, so object snap catches them.
        if (e.meta.value("balloon").toBool()) {
            std::vector<Vec2> tips;
            for (const auto v : e.meta.value("leaders").toArray()) {
                const QJsonArray a = v.toArray();
                if (a.size() >= 2) tips.push_back({a[0].toDouble(), a[1].toDouble()});
            }
            if (e.meta.contains("leaderX"))
                tips.push_back({e.meta.value("leaderX").toDouble(),
                                e.meta.value("leaderY").toDouble()});
            if (!tips.empty()) {
                int side;
                if (e.meta.contains("landingSide"))
                    side = e.meta.value("landingSide").toDouble() >= 0 ? 1 : -1;
                else {
                    double sx = 0;
                    for (const auto& t : tips) sx += t.x;
                    side = (sx / tips.size() >= e.center.x) ? 1 : -1;
                }
                const double landing = std::max(0.0, e.meta.value("landing").toDouble(8.0));
                out.push_back({e.center.x + side * e.radius, e.center.y}); // attach
                if (landing > 0) out.push_back({e.center.x + side * (e.radius + landing), e.center.y});
                for (const auto& t : tips) out.push_back(t); // arrow tips
            }
        }
        break;
    case EntityType::Arc: {
        // Arc endpoints + the quadrant points that fall within the sweep.
        auto on = [&](double a) {
            return Vec2{e.center.x + e.radius * std::cos(a), e.center.y + e.radius * std::sin(a)};
        };
        out.push_back(on(e.startAngle));
        out.push_back(on(e.endAngle));
        double sweep = e.endAngle - e.startAngle;
        while (sweep < 0.0) sweep += 2.0 * M_PI;
        for (int k = 0; k < 4; ++k) {
            const double q = k * M_PI / 2.0;
            double rel = q - e.startAngle;
            while (rel < 0.0) rel += 2.0 * M_PI;
            if (rel <= sweep) out.push_back(on(q));
        }
        break;
    }
    case EntityType::Ellipse: {
        // The 4 axis endpoints (rotated with the ellipse).
        const double c = std::cos(e.rotation), s = std::sin(e.rotation);
        auto axis = [&](double ax, double ay) {
            return Vec2{e.center.x + ax * c - ay * s, e.center.y + ax * s + ay * c};
        };
        out.push_back(axis(e.radius, 0));
        out.push_back(axis(-e.radius, 0));
        out.push_back(axis(0, e.radiusY));
        out.push_back(axis(0, -e.radiusY));
        break;
    }
    case EntityType::Hatch:
        // A circle-boundary hatch snaps to its 4 quadrant joints too.
        if (e.meta.value("shape").toString() == QLatin1String("circle")) {
            out.push_back({e.center.x + e.radius, e.center.y});
            out.push_back({e.center.x - e.radius, e.center.y});
            out.push_back({e.center.x, e.center.y + e.radius});
            out.push_back({e.center.x, e.center.y - e.radius});
            break;
        }
        for (const auto& p : e.points) out.push_back(p);
        break;
    case EntityType::Polygon:
        for (const auto& p : e.points) out.push_back(p);
        // Centre joint of the polygon (snap to the middle).
        if (!e.points.empty()) {
            Vec2 c{0, 0};
            for (const auto& p : e.points) c = c + p;
            out.push_back(c * (1.0 / static_cast<double>(e.points.size())));
        }
        break;
    case EntityType::Polyline:
    case EntityType::Spline:
    case EntityType::Wire:
    case EntityType::Rect:
    case EntityType::Wipeout:
    case EntityType::Image:
        for (const auto& p : e.points) out.push_back(p);
        // Every grid INTERSECTION of a table used to be an endpoint snap - all the cell corners, not
        // just the four outer ones. A thirty-four by six bill of materials is two hundred and forty five
        // of them, a row apart, and the cursor could not cross the table without being seized by one.
        // The four corners of the table are its endpoints, like any other rectangle; the cells are for
        // reading and typing into.
        break;
    case EntityType::Text:
    case EntityType::Splice:
    case EntityType::NetLabel:
    case EntityType::NotConnected:
        out.push_back(e.a);
        break;
    case EntityType::ConnectionPin:
        // Where it stands, and - on a pin drawn as a connector's - its free end, the connection point.
        out.push_back(e.a);
        if (connectionPinIsStub(e)) out.push_back(e.b);
        break;
    case EntityType::Screen:
        // The drain-leg termination point (last point, after the caught-wire joints)
        // is a snappable endpoint, so a wire can connect to it.
        if (!e.points.empty()) out.push_back(e.points.back());
        break;
    case EntityType::Connector: {
        // Endpoint joints: the four body-rectangle corners and each pin's ends (the
        // wire-connection tip and the body-edge tip).
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
        const ConnectorGeometry g = connectorGeometry(e.a, spec);
        out.push_back(g.rectMin);
        out.push_back({g.rectMax.x, g.rectMin.y});
        out.push_back(g.rectMax);
        out.push_back({g.rectMin.x, g.rectMax.y});
        for (const auto& pe : g.pinEnd) out.push_back(pe);
        for (const auto& pt : g.pinTip) out.push_back(pt);
        break;
    }
    case EntityType::Dimension: {
        // Snap to a dimension's joints: the measured points, and the dimension-line
        // ends (for radius/diameter, the centre + the leader tip).
        const QString ty = e.meta.value("dimType").toString(QStringLiteral("linear"));
        if ((ty == QLatin1String("linear") || ty == QLatin1String("aligned") ||
             ty == QLatin1String("arclength")) &&
            e.points.size() >= 3) {
            const Vec2 p1 = e.points[0], p2 = e.points[1], off = e.points[2];
            out.push_back(p1);
            out.push_back(p2);
            Vec2 d1, d2;
            if (ty == QLatin1String("linear")) {
                const Vec2 mid = lerp(p1, p2, 0.5);
                const bool h = std::abs(off.y - mid.y) >= std::abs(off.x - mid.x);
                if (h) { d1 = {p1.x, off.y}; d2 = {p2.x, off.y}; }
                else { d1 = {off.x, p1.y}; d2 = {off.x, p2.y}; }
            } else {
                const Vec2 n = (p2 - p1).normalized().perp();
                const double t = dot(off - p1, n);
                d1 = p1 + n * t;
                d2 = p2 + n * t;
            }
            out.push_back(d1);
            out.push_back(d2);
        } else {
            out.push_back(e.a);
            out.push_back(e.b);
            if (ty == QLatin1String("radius") || ty == QLatin1String("diameter") ||
                ty == QLatin1String("jogged"))
                out.push_back(e.center);
        }
        break;
    }
    default:
        break;
    }
}

} // namespace

QString snapKindLabel(SnapKind k) {
    switch (k) {
    case SnapKind::Grid: return QStringLiteral("Grid");
    case SnapKind::Endpoint: return QStringLiteral("Endpoint");
    case SnapKind::Midpoint: return QStringLiteral("Midpoint");
    case SnapKind::Center: return QStringLiteral("Center");
    case SnapKind::GeometricCenter: return QStringLiteral("Geometric Center");
    case SnapKind::Node: return QStringLiteral("Node");
    case SnapKind::Quadrant: return QStringLiteral("Quadrant");
    case SnapKind::Intersection: return QStringLiteral("Intersection");
    case SnapKind::Insertion: return QStringLiteral("Insertion");
    case SnapKind::Perpendicular: return QStringLiteral("Perpendicular");
    case SnapKind::Tangent: return QStringLiteral("Tangent");
    case SnapKind::Nearest: return QStringLiteral("Nearest");
    case SnapKind::Ortho: return QStringLiteral("Ortho");
    case SnapKind::Polar: return QStringLiteral("Polar");
    default: return QString();
    }
}

SnapMarker snapMarkerFor(SnapKind k) {
    switch (k) {
    // THE MIDDLE OF SOMETHING. Derived from the object rather than built into it, and the three of
    // these are the same kind of answer: halfway along, the centre of the ring, the centroid of the
    // outline.
    case SnapKind::Midpoint:
    case SnapKind::Center:
    case SnapKind::GeometricCenter:
        return SnapMarker::Triangle;

    // A CROSSING - a point that belongs to neither object. It used to be drawn as a joint, which is
    // the one confusion worth spending a shape on: connecting a line to where two others happen to
    // cross is a different act from connecting it to the end of one of them, and on a busy drawing the
    // two candidates are often within a pixel of each other.
    case SnapKind::Intersection:
        return SnapMarker::Hourglass;

    // Everything else is A JOINT: a point that is part of the object. Ends and vertices obviously so;
    // a quadrant, a perpendicular foot, a tangent point and the nearest point on a run are all a
    // definite place ON the object, which is what the square says.
    default:
        return SnapMarker::Square;
    }
}

namespace {

// Whether everything an object can be snapped to is guaranteed to lie inside its own bounds().
//
// This is what lets the snap throw an object away before looking at it. EVERY snap kind here answers
// with a point that has to be within the pick tolerance of the cursor to be chosen at all - an endpoint,
// a midpoint, a centre, the foot of a perpendicular, a tangent point, the nearest point on a segment,
// an intersection. So if an object's whole reach is further than the tolerance from the cursor, it
// cannot supply the answer, and building its geometry is wasted work.
//
// The ones that are NOT guaranteed are the runs and the harness objects: their drawn shape is worked out
// from a path and can reach well outside it - a double line's cap stretched to meet something, a
// connector's pins, a shield's drain legs. Those are never thrown away, so nothing can be lost by this.
// Answers with a box round everything this object could be snapped to, or false when it cannot be said
// cheaply - in which case the object is kept and worked out in full, so nothing is ever lost.
//
// Built from the object's OWN STORED POINTS: no derived geometry, no offsetting a run, and above all no
// measuring of text. Entity::bounds() would answer the same question, but it is the wrong tool here -
// asking it of every object costs more than the work this is meant to save, because for some kinds it
// builds the shape to measure it. A cull has to be cheaper than what it culls.
// WHERE A RUN'S MIDPOINTS ARE TAKEN FROM: its straight stretches and its cut ends. Every other kind takes
// them from its segments.
void twistedPairMidSegments(const Entity& e, std::vector<std::pair<Vec2, Vec2>>& out) {
    for (const auto& seg : twistedPairStraights(e)) out.push_back(seg);
    for (const auto& cap : twistedPairEndCaps(e))
        if (cap.size() == 2) out.emplace_back(cap.front(), cap.back());
}

bool snapReachBox(const Entity& e, Vec2& lo, Vec2& hi) {
    // A RUN REACHES AS FAR AS WHAT IT DRAWS - its fans spread well past its route.
    if (e.type == EntityType::TwistedPair) {
        const RunSnap* snap = twistedPairSnap(e);
        if (!snap) return false;
        lo = snap->lo;
        hi = snap->hi;
        return true;
    }
    if (e.meta.value(QStringLiteral("double")).toBool()) return false;   // a double run, whatever its type
    auto fromPoints = [&] {
        if (e.points.empty()) return false;
        lo = hi = e.points.front();
        for (const Vec2& p : e.points) {
            lo.x = std::min(lo.x, p.x);
            lo.y = std::min(lo.y, p.y);
            hi.x = std::max(hi.x, p.x);
            hi.y = std::max(hi.y, p.y);
        }
        return true;
    };
    switch (e.type) {
    case EntityType::Text:
    case EntityType::Splice:
    case EntityType::NetLabel:
    case EntityType::NotConnected:
        lo = hi = e.a;   // its anchor is the only thing on it that is ever snapped to
        return true;
    case EntityType::Circle:
    case EntityType::Arc:
    case EntityType::Ellipse: {
        // Centre, quadrants, endpoints and tangent points all lie within one radius of the centre.
        const double r = std::max(std::abs(e.radius), std::abs(e.radiusY));
        lo = {e.center.x - r, e.center.y - r};
        hi = {e.center.x + r, e.center.y + r};
        return true;
    }
    case EntityType::Line:
        lo = {std::min(e.a.x, e.b.x), std::min(e.a.y, e.b.y)};
        hi = {std::max(e.a.x, e.b.x), std::max(e.a.y, e.b.y)};
        return true;
    case EntityType::Polyline:
    case EntityType::Rect:
    case EntityType::Polygon:
    case EntityType::Spline:
    case EntityType::Hatch:
    case EntityType::Wipeout:
    case EntityType::Image:
    case EntityType::Leader:
    case EntityType::Wire:
        return fromPoints();
    default:
        return false;
    }
}

} // namespace

SnapResult resolveSnap(const Document& doc, const Vec2& raw,
                       const std::optional<Vec2>& anchor, double tolWorld,
                       const SnapSettings& s,
                       const std::vector<QString>& ignoreIds) {
    SnapResult result{raw, SnapKind::None, false};

    auto ignored = [&](const QString& id) {
        return std::find(ignoreIds.begin(), ignoreIds.end(), id) != ignoreIds.end();
    };

    // --- Object snap (highest priority): endpoints/centers, then mid/inter/near.
    if (s.objectSnap) {
        double best = tolWorld;
        Vec2 bestPt = raw;
        SnapKind bestKind = SnapKind::None;

        auto consider = [&](const Vec2& p, SnapKind kind) {
            const double d = distance(raw, p);
            if (d < best) { best = d; bestPt = p; bestKind = kind; }
        };

        std::vector<std::pair<Vec2, Vec2>> allSegs;
        for (const auto& e : doc.entities()) {
            if (ignored(e.id)) continue;
            // A harness run's reach box builds its rendered snap geometry, so
            // hidden runs must still be rejected before that work.
            if (e.type == EntityType::TwistedPair)
                if (const Layer* l = doc.findLayer(e.layer); l && !l->visible) continue;
            // Nothing further away than the pick tolerance can supply a point inside it. Without this,
            // every cursor move built the snap geometry of EVERY object on the drawing: an imported sheet
            // of two thousand objects cost about three and a half milliseconds a move, out of the eight a
            // mouse gives you, and the cursor visibly dragged behind the hand. Rejecting on the bounding
            // box first leaves only the handful of objects actually under the cursor to be worked out.
            if (Vec2 lo, hi; snapReachBox(e, lo, hi)) {
                if (raw.x < lo.x - tolWorld || raw.x > hi.x + tolWorld ||
                    raw.y < lo.y - tolWorld || raw.y > hi.y + tolWorld)
                    continue;
            }
            // Only nearby objects need a layer lookup. On a drawing with many
            // layers, looking each one up before rejecting distant geometry
            // multiplied the work of every pointer report.
            if (e.type != EntityType::TwistedPair)
                if (const Layer* l = doc.findLayer(e.layer); l && !l->visible) continue;

            if (s.snapEndpoint) {
                std::vector<Vec2> pts;
                entityEndpoints(e, pts);
                for (const auto& p : pts) consider(p, SnapKind::Endpoint);
            }
            if (s.snapCenter &&
                (e.type == EntityType::Circle || e.type == EntityType::Arc ||
                 e.type == EntityType::Ellipse)) {
                consider(e.center, SnapKind::Center);
            }
            // Quadrant: the 4 cardinal points of a circle/arc/ellipse.
            if (s.snapQuadrant &&
                (e.type == EntityType::Circle || e.type == EntityType::Arc ||
                 e.type == EntityType::Ellipse)) {
                const double rx = e.radius, ry = (e.radiusY > 0 ? e.radiusY : e.radius);
                consider({e.center.x + rx, e.center.y}, SnapKind::Quadrant);
                consider({e.center.x - rx, e.center.y}, SnapKind::Quadrant);
                consider({e.center.x, e.center.y + ry}, SnapKind::Quadrant);
                consider({e.center.x, e.center.y - ry}, SnapKind::Quadrant);
            }
            // Geometric centre: centroid of a closed shape.
            if (s.snapGeometricCenter &&
                (e.type == EntityType::Rect || e.type == EntityType::Polygon ||
                 e.type == EntityType::Hatch) &&
                !e.points.empty()) {
                Vec2 c{0, 0};
                for (const auto& pt : e.points) c = c + pt;
                consider(c * (1.0 / e.points.size()), SnapKind::GeometricCenter);
            }
            // Node: point-like objects (splices, markers, net labels, text anchors).
            if (s.snapNode &&
                (e.type == EntityType::Splice || e.type == EntityType::NotConnected ||
                 e.type == EntityType::NetLabel || e.type == EntityType::Text)) {
                consider(e.a, SnapKind::Node);
            }
            // A connection pin is caught where a wire lands on it: the end of a Y pin's leg, or the point
            // of a circle pin's ring nearest the cursor - its edge, not its middle.
            if (s.snapNode && e.type == EntityType::ConnectionPin)
                consider(connectionPinLanding(e, raw), SnapKind::Node);
            // Insertion: the insertion point of text / connectors / balloons.
            if (s.snapInsertion &&
                (e.type == EntityType::Text || e.type == EntityType::Connector ||
                 e.meta.value("balloon").toBool())) {
                consider(e.a, SnapKind::Insertion);
            }
            // Tangent: tangent points on a circle from the anchor (needs a from-point).
            if (s.snapTangent && anchor && e.type == EntityType::Circle) {
                const double d = distance(*anchor, e.center);
                if (d > e.radius + 1e-6) {
                    const double aBase = std::atan2(anchor->y - e.center.y, anchor->x - e.center.x);
                    const double off = std::acos(e.radius / d);
                    consider(pointOnCircle(e.center, e.radius, aBase + off), SnapKind::Tangent);
                    consider(pointOnCircle(e.center, e.radius, aBase - off), SnapKind::Tangent);
                }
            }
            std::vector<std::pair<Vec2, Vec2>> segs;
            entitySegments(e, segs);
            if (s.snapMidpoint) {
                std::vector<std::pair<Vec2, Vec2>> mids;
                if (e.type == EntityType::TwistedPair) twistedPairMidSegments(e, mids);
                for (const auto& seg : e.type == EntityType::TwistedPair ? mids : segs)
                    consider(lerp(seg.first, seg.second, 0.5), SnapKind::Midpoint);
            }
            // Perpendicular: foot of the perpendicular from the anchor onto a segment.
            if (s.snapPerpendicular && anchor)
                for (const auto& seg : segs) {
                    const Vec2 d = seg.second - seg.first;
                    const double L2 = d.lengthSq();
                    if (L2 < 1e-12) continue;
                    double t = dot(*anchor - seg.first, d) / L2;
                    t = std::clamp(t, 0.0, 1.0);
                    consider(seg.first + d * t, SnapKind::Perpendicular);
                }
            for (auto& seg : segs) allSegs.push_back(seg);
        }

        // Intersections between segments near the cursor - NEAR THE CURSOR, which is what this has
        // always claimed to do. It tested every segment in the drawing against every other, which is
        // the square of the whole drawing per mouse movement: a few hundred segments is nothing, a few
        // thousand (a hatch, a long polyline, a woven pair) is tens of millions of tests and the cursor
        // visibly lags. An intersection further from the cursor than the snap radius could never be
        // chosen anyway, so it is not worth testing for.
        if (s.snapIntersection) {
            std::vector<std::pair<Vec2, Vec2>> near;
            near.reserve(allSegs.size());
            std::vector<std::pair<double, size_t>> howNear;
            for (size_t i = 0; i < allSegs.size(); ++i) {
                const double dd = closestPointOnSegment(raw, allSegs[i].first, allSegs[i].second).distance;
                if (dd <= tolWorld) howNear.emplace_back(dd, i);
            }
            // ONLY THE NEAREST FEW HUNDRED. Every pair of them is tried, so the work goes as the square of how
            // many there are - and zoomed out over a sheet of twisted bundles, each drawn as hundreds of short
            // pieces, the cursor's reach takes in thousands: millions of tests a mouse movement, and the cursor
            // fell seconds behind the hand. The crossing being aimed at is among the pieces nearest the cursor.
            constexpr size_t kMostNear = 300;
            if (howNear.size() > kMostNear) {
                std::nth_element(howNear.begin(), howNear.begin() + kMostNear, howNear.end());
                howNear.resize(kMostNear);
            }
            for (const auto& h : howNear) near.push_back(allSegs[h.second]);
            for (size_t i = 0; i < near.size(); ++i)
                for (size_t j = i + 1; j < near.size(); ++j)
                    if (auto x = segmentSegmentIntersection(near[i].first, near[i].second,
                                                           near[j].first, near[j].second))
                        consider(*x, SnapKind::Intersection);

            // AND THE LINE BEING DRAWN, against what is already on the sheet.
            //
            // Two EXISTING segments crossing is only half of it, and the smaller half. The commonest
            // crossing on a drawing board is the one being MADE: a line taken across an existing one,
            // where the point wanted is exactly where the new line meets the old. There is no second
            // entity to test yet - the rubber band is not in the document until the click lands - so
            // the band itself is offered as a segment.
            //
            // Without this the cursor at that crossing caught Nearest instead, which is a point ON the
            // old line rather than the meeting of the two, and it drew as a joint. The reading was a
            // fraction out and the marker said the wrong thing about what had been caught.
            //
            // Extended a little past the cursor, because the crossing is usually just beyond where the
            // hand has got to. Nothing further from the cursor than the snap radius can be chosen
            // anyway, so the extension costs nothing and catches the approach rather than only the
            // arrival.
            //
            // ONLY WHERE NOTHING REAL HAS BEEN CAUGHT. A joint that belongs to the drawing outranks a
            // crossing with a line that does not exist yet: with the cursor all but on a corner, the
            // corner is what the hand is going for, and the rubber band happens to cross that object's
            // edge a fraction nearer. Offering the band's crossing there took the pick a hair off the
            // corner - which is exactly the failure the tracked-angle rule was written to stop.
            //
            // Guarded the same way Nearest is, and for the same reason: it is a fallback, not a rival.
            if (anchor && bestKind == SnapKind::None) {
                const Vec2 reach = raw - *anchor;
                if (reach.lengthSq() > 1e-12) {
                    const Vec2 tip = raw + reach.normalized() * (tolWorld * 2.0);
                    for (const auto& seg : near)
                        if (auto x = segmentSegmentIntersection(*anchor, tip, seg.first, seg.second))
                            consider(*x, SnapKind::Intersection);
                }
            }
        }

        // THE RING IS CATCHABLE ONLY IF "NEAREST" IS ASKED FOR.
        //
        // A point on a circle chosen by where the cursor happened to be is not the centre, not a
        // quadrant, not a crossing - and until this was behind its own tick it drowned all three,
        // because the ring is catchable along its whole length and a quadrant only in four places. That
        // is exactly what Nearest means, though, and somebody who has ticked it has asked for it: a
        // point anywhere along the thing, wherever the hand is pointing.
        //
        // So it is the LAST thing tried and only when the tick says so. With it off, touching a circle
        // catches nothing and the editor draws a small cross at the middle instead - see the overlay -
        // which says where the centre is without dragging the cursor to it.
        if (s.snapNearest && bestKind == SnapKind::None) {
            // Along a segment: lines, polylines, and everything else made of them.
            for (const auto& seg : allSegs) {
                const auto c = closestPointOnSegment(raw, seg.first, seg.second);
                if (c.distance < best) { best = c.distance; bestPt = c.point; bestKind = SnapKind::Nearest; }
            }
            // And round a ring, which is the same idea on something with no segments.
            for (const auto& e : doc.entities()) {
                if (e.type != EntityType::Circle && e.type != EntityType::Arc &&
                    e.type != EntityType::Ellipse) continue;
                // The nearest fallback was evaluating angles and trigonometry
                // for every ring on the sheet, including rings nowhere near
                // the cursor. A conservative box rejects those cheaply.
                const double radius = std::max(std::abs(e.radius), std::abs(e.radiusY));
                if (std::abs(raw.x - e.center.x) > radius + best ||
                    std::abs(raw.y - e.center.y) > radius + best) continue;
                if (ignored(e.id)) continue;
                if (const Layer* l = doc.findLayer(e.layer); l && !l->visible) continue;
                if (e.type == EntityType::Circle || e.type == EntityType::Arc) {
                    const double d = distance(raw, e.center);
                    if (d < 1e-9) continue;
                    double ang = std::atan2(raw.y - e.center.y, raw.x - e.center.x);
                    if (e.type == EntityType::Arc) {
                        // Clamp the angle to the arc's sweep (else its endpoints).
                        double s0 = e.startAngle, sweep = e.endAngle - e.startAngle;
                        while (sweep < 0) sweep += 2 * M_PI;
                        double rel = ang - s0;
                        while (rel < 0) rel += 2 * M_PI;
                        if (rel > sweep) ang = (rel - sweep < 2 * M_PI - rel) ? e.endAngle : s0;
                    }
                    const Vec2 pt = pointOnCircle(e.center, e.radius, ang);
                    const double dd = distance(raw, pt);
                    if (dd < best) { best = dd; bestPt = pt; bestKind = SnapKind::Nearest; }
                } else if (e.type == EntityType::Ellipse) {
                    // Approximate: map the cursor into the ellipse's local frame, take the parametric
                    // point in that direction, map back.
                    const double c = std::cos(e.rotation), sr = std::sin(e.rotation);
                    const Vec2 v = raw - e.center;
                    const double lx = v.x * c + v.y * sr, ly = -v.x * sr + v.y * c;
                    const double ry = e.radiusY > 0 ? e.radiusY : e.radius;
                    const double t = std::atan2(ly / std::max(1e-9, ry), lx / std::max(1e-9, e.radius));
                    const double px = e.radius * std::cos(t), py = ry * std::sin(t);
                    const Vec2 pt{e.center.x + px * c - py * sr, e.center.y + px * sr + py * c};
                    const double dd = distance(raw, pt);
                    if (dd < best) { best = dd; bestPt = pt; bestKind = SnapKind::Nearest; }
                }
            }
        }

        if (bestKind != SnapKind::None)
            return {bestPt, bestKind, true};
    }

    // --- Ortho / polar constrain relative to the anchor.
    if (anchor && (s.ortho || s.polar)) {
        const Vec2 d = raw - *anchor;
        const double len = d.length();
        if (len > 1e-9) {
            const double ang = std::atan2(d.y, d.x);
            if (s.ortho) {
                // Ortho is a hard constraint to the nearest 90 degrees.
                const double stepRad = M_PI / 2.0;
                const double snapped = std::round(ang / stepRad) * stepRad;
                result.point = {anchor->x + len * std::cos(snapped),
                                anchor->y + len * std::sin(snapped)};
                result.kind = SnapKind::Ortho;
                result.snapped = true;
                return result;
            }
            // Polar: free angle, but jump to the nearest tracking increment only
            // when the cursor is within ~2 degrees of it (AutoCAD-style).
            const double stepRad = qDegreesToRadians(s.polarStepDeg);
            if (stepRad > 1e-9) {
                const double snapped = std::round(ang / stepRad) * stepRad;
                if (std::abs(ang - snapped) <= qDegreesToRadians(2.0)) {
                    result.point = {anchor->x + len * std::cos(snapped),
                                    anchor->y + len * std::sin(snapped)};
                    result.kind = SnapKind::Polar;
                    result.snapped = true;
                    return result;
                }
            }
            // Otherwise leave the angle free (fall through to grid / none).
        }
    }

    // --- Grid snap (fallback).
    if (s.gridSnap && s.gridSize > 0.0) {
        result.point = {std::round(raw.x / s.gridSize) * s.gridSize,
                        std::round(raw.y / s.gridSize) * s.gridSize};
        result.kind = SnapKind::Grid;
        result.snapped = true;
        return result;
    }

    return result;
}

} // namespace Loopwire
