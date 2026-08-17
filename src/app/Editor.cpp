#include "app/Editor.h"

#include "model/EntityRefs.h"
#include "model/PartSymbols.h"

#include "render/EntityRenderer.h"

#include "harness/MechModels.h"   // kMechModelGroup: a default model is one object on its own canvas
#include "model/Block.h"
#include "model/DrawingTemplate.h"
#include "model/Picture.h"

#include "geometry/Geometry.h"
#include "tools/EntityTransforms.h"
#include "tools/ModifyOps.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QJsonArray>
#include <QPainter>
#include <QPolygonF>
#include <QRegularExpression>
#include <QTimer>
#include <QDateTime>
#include <QUuid>
#include <QtMath>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Edraw_Desktop {

// Soft orange: the colour every piece of table editing chrome is drawn in - the table's own
// highlight, its row / column headers, and the cells picked inside it. Table work is a different
// job from selecting objects, so it does not borrow the selection's blue.
static const QColor kTableChrome(230, 156, 74);

namespace {
// Hold this long during a drag before the rectangle becomes a lasso (Edraw's
// LASSO_DELAY_MS).
constexpr qint64 kLassoDelayMs = 200;

// Smallest spacing between adjacent caught wires (their crossing points projected onto
// the major axis, then sorted) — used as the "closest connected pins gap" for the shield
// margin. Returns 0 for fewer than two crossings.
double shieldWireGap(const std::vector<Vec2>& crossings, const Vec2& major) {
    if (crossings.size() < 2) return 0.0;
    std::vector<double> t;
    t.reserve(crossings.size());
    for (const Vec2& c : crossings) t.push_back(c.x * major.x + c.y * major.y);
    std::sort(t.begin(), t.end());
    double g = 1e18;
    for (size_t i = 1; i < t.size(); ++i) g = std::min(g, t[i] - t[i - 1]);
    return g < 1e17 ? g : 0.0;
}

// Shield angle: perpendicular to the caught wires, so the shield crosses them at 90° no
// matter how the crossing line was drawn. Averages the wire segment directions (folded to
// one half-plane so opposite senses don't cancel), takes the perpendicular, points it up.
double bundlePerpRot(const std::vector<Vec2>& segDirs) {
    Vec2 acc{0, 0};
    for (const Vec2& d : segDirs) {
        const double len = std::hypot(d.x, d.y);
        if (len < 1e-9) continue;
        Vec2 u{d.x / len, d.y / len};
        if (u.x < 0 || (std::abs(u.x) < 1e-9 && u.y < 0)) u = {-u.x, -u.y}; // consistent half
        acc.x += u.x;
        acc.y += u.y;
    }
    const double al = std::hypot(acc.x, acc.y);
    if (al < 1e-9) return M_PI / 2.0; // no usable direction -> vertical
    Vec2 major{-acc.y / al, acc.x / al}; // perpendicular to the mean wire direction
    if (major.y < 0) major = {-major.x, -major.y};
    return std::atan2(major.y, major.x);
}

// The caught wires lie on the (collinear) crossing line. Find the shield centre and full
// span from the two OUTERMOST crossings along the major axis — robust to the order the
// wires were caught in and to uneven spacing, so the shield always spans every caught wire.
void shieldExtent(const std::vector<Vec2>& crossings, const Vec2& major, Vec2& centerOut,
                  double& spanOut) {
    double tMin = 1e18, tMax = -1e18;
    Vec2 pMin{}, pMax{};
    for (const Vec2& c : crossings) {
        const double t = c.x * major.x + c.y * major.y;
        if (t < tMin) { tMin = t; pMin = c; }
        if (t > tMax) { tMax = t; pMax = c; }
    }
    centerOut = {(pMin.x + pMax.x) * 0.5, (pMin.y + pMax.y) * 0.5};
    spanOut = tMax - tMin;
}

// "On" as opposed to "near": a point the snap put on a run is on it to within rounding, so this is a
// rounding tolerance and not a reach. Anything looser and a run drawn close to a neighbour reads as
// connected to it.
static constexpr double kOnPointTol = 1e-6;

// Snap a point to a horizontal or vertical segment from `prev` (the wire "90° only"
// projection): keep whichever axis the cursor moved furthest along, zero the other.
Vec2 orthoWirePoint(const Vec2& prev, const Vec2& p) {
    if (std::abs(p.x - prev.x) >= std::abs(p.y - prev.y)) return {p.x, prev.y}; // horizontal
    return {prev.x, p.y};                                                        // vertical
}

// Horizontal half-width of a shield outline at a vertical offset dy from its centre.
// For a capsule the sides are straight (== rx) through the middle and round off in the
// semicircle caps; for an ellipse it's the usual rx*sqrt(1-(dy/ry)^2). Used to seat the
// per-wire joints exactly on the drawn outline for either shape.
double shieldEdgeHalfWidth(const QString& shape, double rx, double ry, double dy) {
    const double ay = std::abs(dy);
    if (shape == QLatin1String("capsule")) {
        const double straight = std::max(0.0, ry - rx); // half-length of the straight side
        if (ay <= straight) return rx;
        const double t = (ay - straight) / std::max(1e-9, rx);
        return rx * std::sqrt(std::max(0.0, 1.0 - t * t));
    }
    const double t = ry > 1e-9 ? std::clamp(dy / ry, -1.0, 1.0) : 0.0;
    return rx * std::sqrt(std::max(0.0, 1.0 - t * t));
}

// Circle through three points (circumcircle). Returns false if collinear.
bool circumcircle(const Vec2& a, const Vec2& b, const Vec2& c, Vec2& center, double& radius) {
    const double d = 2.0 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
    if (std::abs(d) < 1e-9) return false;
    const double a2 = a.x * a.x + a.y * a.y;
    const double b2 = b.x * b.x + b.y * b.y;
    const double c2 = c.x * c.x + c.y * c.y;
    center.x = (a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d;
    center.y = (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d;
    radius = distance(center, a);
    return true;
}

// Editable handles ("joints") for an entity, shown as grips when selected and
// draggable for direct manipulation.
enum class GripRole { Move, LineA, LineB, LineMid, Vertex, RectCorner, RectEdge, Center, Radius,
                      ArcStart, ArcEnd, ArcMid, EllipseMajor, EllipseMinor,
                      // The two ends of a PIECE of an ellipse: dragging one moves that end ROUND the
                      // ellipse, which is what an end of an arc does. Without them the only handles a
                      // piece had were its ellipse's four axis points, so pulling what looked like its
                      // end resized the whole ellipse instead of moving the end.
                      EllipseStart, EllipseEnd,
                      TextWidth, TextHeight,
                      DimText, LeaderTip, LeaderLanding, LeaderKnee, TableColEdge, TableRowEdge,
                      ConnPinText, ConnDesText, ConnPnText, ConnAllText, WireSegMove, TwistMove,
                      ShieldRy,
                      // A wire's colour/gauge marking, and a Wire label attached to a wire:
                      // both are constrained to slide ALONG the wire and nowhere else.
                      WireMark, WireMarkColor, WireMarkLabel, WireLabelSlide,
                      // The label a wire carries itself ("wireLabel"), slid along the run.
                      WireOwnLabel };

// A balloon's leader arrow tips (world points). New balloons store meta["leaders"]
// (array of [x,y]); older ones a single leaderX/leaderY — both are read here.
inline std::vector<Vec2> balloonLeaders(const Entity& e) {
    std::vector<Vec2> out;
    if (e.meta.contains(QStringLiteral("leaders"))) {
        for (const auto v : e.meta.value(QStringLiteral("leaders")).toArray()) {
            const QJsonArray a = v.toArray();
            if (a.size() >= 2) out.push_back({a[0].toDouble(), a[1].toDouble()});
        }
    } else if (e.meta.contains(QStringLiteral("leaderX"))) {
        out.push_back({e.meta.value(QStringLiteral("leaderX")).toDouble(),
                       e.meta.value(QStringLiteral("leaderY")).toDouble()});
    }
    return out;
}
inline void setBalloonLeaders(Entity& e, const std::vector<Vec2>& tips) {
    QJsonArray arr;
    for (const auto& t : tips) {
        QJsonArray p;
        p.append(t.x);
        p.append(t.y);
        arr.append(p);
    }
    e.meta.insert(QStringLiteral("leaders"), arr);
    e.meta.remove(QStringLiteral("leaderX")); // migrated to the array form
    e.meta.remove(QStringLiteral("leaderY"));
}
// Which horizontal side the leader leaves the balloon on: +1 (right) / -1 (left).
// Defaults toward the average arrow tip.
inline int balloonLandingSide(const Entity& e) {
    if (e.meta.contains(QStringLiteral("landingSide")))
        return e.meta.value(QStringLiteral("landingSide")).toDouble() >= 0 ? 1 : -1;
    const auto tips = balloonLeaders(e);
    if (tips.empty()) return 1;
    double sx = 0;
    for (const auto& t : tips) sx += t.x;
    return (sx / tips.size() >= e.center.x) ? 1 : -1;
}
// Free attachment angle (used only when landing==0). Defaults toward the avg tip.
inline double balloonLandingAngle(const Entity& e) {
    if (e.meta.contains(QStringLiteral("landingAngle")))
        return e.meta.value(QStringLiteral("landingAngle")).toDouble();
    const auto tips = balloonLeaders(e);
    if (tips.empty()) return 0.0;
    Vec2 avg{0, 0};
    for (const auto& t : tips) avg = avg + t;
    avg = avg * (1.0 / tips.size());
    return std::atan2(avg.y - e.center.y, avg.x - e.center.x);
}
// Where the leader ATTACHES to the circle: with a landing shelf it's the horizontal
// side midpoint; with NO landing (0) it's a free point around the ring (landingAngle).
inline Vec2 balloonAttach(const Entity& e) {
    const double landing = std::max(0.0, e.meta.value(QStringLiteral("landing")).toDouble(8.0));
    if (landing > 0) return {e.center.x + balloonLandingSide(e) * e.radius, e.center.y};
    const double a = balloonLandingAngle(e);
    return {e.center.x + std::cos(a) * e.radius, e.center.y + std::sin(a) * e.radius};
}
// The leader "knee": end of the horizontal landing shelf; coincides with the attach
// point when there is no landing.
inline Vec2 balloonKnee(const Entity& e) {
    const double landing = std::max(0.0, e.meta.value(QStringLiteral("landing")).toDouble(8.0));
    if (landing <= 0) return balloonAttach(e);
    return {e.center.x + balloonLandingSide(e) * (e.radius + landing), e.center.y};
}
// `off` is the grip's offset from its centerline reference (non-zero only for the
// double line's parallel-line joints) so dragging keeps the grabbed point under
// the cursor: e.a = target - off.
struct Grip { Vec2 pos; GripRole role; int index; Vec2 off{0, 0}; };

std::vector<Vec2> regularPolygonPoints(const Vec2& center, const Vec2& vertex, int sides, int vertexIndex = 0) {
    const int n = std::clamp(sides, 3, 64);
    const double r = distance(center, vertex);
    const double a0 = std::atan2(vertex.y - center.y, vertex.x - center.x) -
                      2.0 * M_PI * vertexIndex / n;
    std::vector<Vec2> pts;
    pts.reserve(n);
    for (int i = 0; i < n; ++i) {
        const double a = a0 + 2.0 * M_PI * i / n;
        pts.push_back({center.x + r * std::cos(a), center.y + r * std::sin(a)});
    }
    return pts;
}

Vec2 polygonCenter(const std::vector<Vec2>& points) {
    Vec2 c{0, 0};
    if (points.empty()) return c;
    for (const Vec2& p : points) c = c + p;
    return c * (1.0 / static_cast<double>(points.size()));
}

void drawDraftValueBox(QPainter& p, const QPointF& at, const QString& label, const QColor& textColor) {
    QFont f = p.font();
    f.setPixelSize(12);
    p.setFont(f);
    const QFontMetricsF fm(f);
    const QRectF box(at.x() + 14, at.y() - 28, fm.horizontalAdvance(label) + 10, 20);
    p.setPen(QPen(QColor("#3a3320")));
    p.setBrush(QColor(20, 24, 32, 220));
    p.drawRect(box);
    p.setPen(QPen(textColor));
    p.drawText(box, Qt::AlignCenter, label);
    p.setBrush(Qt::NoBrush);
}

// The capped endpoint of a straight physical run — mirrors the renderer's run cut
// (line / circle / poly), so a double line/polyline's endpoint GRIPS sit on the real
// (capped) physical-line ends, not the raw centreline offset.
// A cutter's straight segments, flattened to consecutive pairs. The physical lines of a double run are
// what a double run cuts with, which is why this is not simply its points.
void forEachCutSegmentOf(const Entity& e, std::vector<Vec2>& out) {
    auto add = [&out](const Vec2& a, const Vec2& b) { out.push_back(a); out.push_back(b); };
    switch (e.type) {
    case EntityType::Line:
    case EntityType::Dimension:
        add(e.a, e.b);
        break;
    case EntityType::DoubleLine: {
        // As drawn, caps included - see forEachCutSegment in ModifyOps for why.
        const double gap = e.meta.value("gap").toDouble(10.0);
        const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
        for (int side : {+1, -1}) {
            std::vector<Vec2> rail = {e.a + off * side, e.b + off * side};
            applyDoubleRunCuts(rail, e, side);
            for (const auto& piece : splitRailAtHoles(rail, e, side))
                for (size_t i = 1; i < piece.size(); ++i) add(piece[i - 1], piece[i]);
        }
        break;
    }
    case EntityType::Polyline:
        if (e.meta.value("double").toBool() && e.points.size() >= 2) {
            const double gap = e.meta.value("gap").toDouble(10.0);
            for (double s : {gap / 2.0, -gap / 2.0}) {
                std::vector<Vec2> run = offsetPolyline(e.points, s);
                const int side = s > 0 ? +1 : -1;
                applyDoubleRunCuts(run, e, side);
                for (const auto& piece : splitRailAtHoles(run, e, side))
                    for (size_t i = 1; i < piece.size(); ++i) add(piece[i - 1], piece[i]);
            }
        } else {
            for (size_t i = 1; i < e.points.size(); ++i) add(e.points[i - 1], e.points[i]);
        }
        break;
    case EntityType::Rect:
    case EntityType::Polygon:
        for (size_t i = 0; i < e.points.size(); ++i)
            add(e.points[i], e.points[(i + 1) % e.points.size()]);
        break;
    default:
        for (size_t i = 1; i < e.points.size(); ++i) add(e.points[i - 1], e.points[i]);
        break;
    }
}

// Where a rail MEETS a cut segment. The rail's own line is extended as far as it needs to go - a rail
// that falls short of the line it is meant to start on is the whole point of capping - but the meeting
// point has to lie ON that segment: between its knees, or between its ends. Past them the rail cannot
// reach it and nothing is capped, which is what "if it can reach it" means.
std::optional<Vec2> railMeetsSegment(const Vec2& thisEnd, const Vec2& otherEnd, const Vec2& a,
                                     const Vec2& b) {
    const auto x = lineLineIntersection(otherEnd, thisEnd, a, b);
    if (!x) return std::nullopt;                                   // parallel: never meets
    if (closestPointOnSegment(*x, a, b).distance > 1e-6) return std::nullopt;
    return *x;
}

// ... or crosses it ANYWHERE along its length, knees included. A cut clips the whole run now, so a
// boundary that crosses it past a knee is reachable too - it simply takes the knee with it.
bool railCrossesSegment(const std::vector<Vec2>& rail, const Vec2& a, const Vec2& b) {
    for (size_t i = 1; i < rail.size(); ++i)
        if (segmentSegmentIntersection(rail[i - 1], rail[i], a, b)) return true;
    return false;
}

// The two cut slots an end has, one per physical line: "cutEnd" and "cutEnd2". A cut with no side on it
// applies to both lines; a sided one only to its own. Two slots are what lets the two lines of a run
// come back to different places - which is what trimming one line of it means.
QString cutSlot2(const QString& key) { return key + QStringLiteral("2"); }

// `side` is which physical run this end belongs to (+1 / -1); a cut recorded for one side only leaves
// the other alone (see applyRunCut).
Vec2 cappedRunEnd(const Entity& e, const QString& key, const Vec2& thisEnd, const Vec2& otherEnd,
                  int side = 0) {
    if (!e.meta.value(key).toBool()) return thisEnd;
    const int onlySide = e.meta.value(key + QStringLiteral("Side")).toInt();
    if (onlySide != 0 && side != 0 && onlySide != side) return thisEnd;
    const QString kind = e.meta.value(key + "Kind").toString();
    if (kind == QLatin1String("circle")) {
        const Vec2 c{e.meta.value(key + "Cx").toDouble(), e.meta.value(key + "Cy").toDouble()};
        const double r = e.meta.value(key + "R").toDouble();
        const auto xs = lineCircleIntersection(otherEnd, thisEnd - otherEnd, c, r);
        double best = 1e299;
        Vec2 bp = thisEnd;
        bool f = false;
        for (const auto& x : xs) {
            const double d = distance(x, otherEnd);
            if (d < best) { best = d; bp = x; f = true; }
        }
        return f ? bp : thisEnd;
    }
    if (kind == QLatin1String("poly")) {
        const QJsonArray poly = e.meta.value(key + "Poly").toArray();
        double best = 1e299;
        Vec2 bp = thisEnd;
        bool f = false;
        for (int i = 0; i + 3 < poly.size(); i += 4) {
            const Vec2 a{poly[i].toDouble(), poly[i + 1].toDouble()};
            const Vec2 b{poly[i + 2].toDouble(), poly[i + 3].toDouble()};
            auto x = lineLineIntersection(otherEnd, thisEnd, a, b);
            if (!x) continue;
            if (closestPointOnSegment(*x, a, b).distance > 1e-6) continue;
            const double d = distance(*x, thisEnd);
            if (d < best) { best = d; bp = *x; f = true; }
        }
        return f ? bp : thisEnd;
    }
    const Vec2 a{e.meta.value(key + "Ax").toDouble(), e.meta.value(key + "Ay").toDouble()};
    const Vec2 b{e.meta.value(key + "Bx").toDouble(), e.meta.value(key + "By").toDouble()};
    const auto x = railMeetsSegment(thisEnd, otherEnd, a, b);
    return x ? *x : thisEnd;
}

// This end, capped by whichever of the two slots applies to this line.
Vec2 cappedRunEndAny(const Entity& e, const QString& key, const Vec2& thisEnd, const Vec2& otherEnd,
                     int side = 0) {
    const Vec2 first = cappedRunEnd(e, key, thisEnd, otherEnd, side);
    if (distance(first, thisEnd) > 1e-12) return first;
    return cappedRunEnd(e, cutSlot2(key), thisEnd, otherEnd, side);
}

// Every connector pin-end point in the document (used to tell whether a wire end is
// connected, which decides its grips).
std::vector<Vec2> collectPinEnds(const Document* doc) {
    std::vector<Vec2> out;
    if (!doc) return out;
    for (const auto& e : doc->entities()) {
        if (e.type != EntityType::Connector) continue;
        const ConnectorGeometry g = connectorGeometry(e.a, connectorSpecFromMeta(e.meta));
        for (const Vec2& pe : g.pinEnd) out.push_back(pe);
    }
    return out;
}

// A shield's local frame, derived exactly the way the renderer derives it so grips,
// hit-testing and resizing all agree: the perpendicular-to-wires rotation, the centre
// (midpoint of the OUTERMOST caught wires along the major axis), both radii, and how
// many leading points are caught wires (anything after them is the drain).
struct ShieldFrame {
    Vec2 center{}, major{}, minor{};
    double rot = 0.0, rx = 12.0, ry = 12.0;
    int k = 0;
    bool valid = false;
};

ShieldFrame shieldFrameOf(const Entity& e) {
    ShieldFrame f;
    f.k = std::min(e.meta.value("catch").toInt(static_cast<int>(e.points.size())),
                   static_cast<int>(e.points.size()));
    if (f.k < 1) return f;
    const std::vector<Vec2> crossings(e.points.begin(), e.points.begin() + f.k);
    f.rot = e.meta.contains("rot") ? e.meta.value("rot").toDouble()
                                   : EntityRenderer::shieldRotation(crossings);
    f.major = {std::cos(f.rot), std::sin(f.rot)};
    f.minor = {-std::sin(f.rot), std::cos(f.rot)};
    double tMin = 1e18, tMax = -1e18;
    Vec2 pMin{}, pMax{};
    for (const Vec2& c : crossings) {
        const double t = c.x * f.major.x + c.y * f.major.y;
        if (t < tMin) { tMin = t; pMin = c; }
        if (t > tMax) { tMax = t; pMax = c; }
    }
    f.center = {(pMin.x + pMax.x) * 0.5, (pMin.y + pMax.y) * 0.5};
    f.rx = e.meta.value("rx").toDouble(12.0);
    f.ry = e.meta.contains("ry") ? e.meta.value("ry").toDouble() : (tMax - tMin) / 2.0 + 6.0;
    f.valid = true;
    return f;
}

// Where the drain leg leaves the outline, for the leg side stored in meta["legDir"].
Vec2 shieldLegEdge(const ShieldFrame& f, const QString& legDir) {
    if (legDir == QLatin1String("top"))
        return {f.center.x + f.ry * f.major.x, f.center.y + f.ry * f.major.y};
    if (legDir == QLatin1String("left"))
        return {f.center.x + f.rx * f.minor.x, f.center.y + f.rx * f.minor.y};
    if (legDir == QLatin1String("right"))
        return {f.center.x - f.rx * f.minor.x, f.center.y - f.rx * f.minor.y};
    return {f.center.x - f.ry * f.major.x, f.center.y - f.ry * f.major.y}; // "bottom"
}

// The point a wire's run / knee handle sits on (nullopt for every other role). Used to
// measure how far the HELD wire actually moved, so the rest of a selected bundle can be
// pulled by exactly that — snapping, ortho and clearance included.
std::optional<Vec2> wireGripPoint(const Entity& w, GripRole role, int index) {
    if (w.type != EntityType::Wire) return std::nullopt;
    const int n = static_cast<int>(w.points.size());
    if (role == GripRole::Vertex) {
        if (index < 0 || index >= n) return std::nullopt;
        return w.points[index];
    }
    if (role == GripRole::WireSegMove) {
        if (index < 0 || index + 1 >= n) return std::nullopt;
        return lerp(w.points[index], w.points[index + 1], 0.5);
    }
    return std::nullopt;
}

// ---- A double run is drawn ALONG the cursor, not around it ---------------------------------------
//
// The clicked points used to be stored as the CENTRELINE, which put the crosshair in the empty gap
// between the two rails: you were pulling the space between the lines rather than a line. A double run
// is now drawn on the rail you can see under the crosshair - the one to the left of travel, which for a
// run drawn left to right is the top one - and the gap goes to the other side of it. AutoCAD's MLINE
// calls this Top justification and measures the side the same way, from the direction of travel.
//
// What is STORED is still the centreline, because everything else - the rails, the joints, trimming,
// snapping, filleting - is defined from it. Only where the run is created does the shift happen.
// `secondBelow` is which side of the drawn line the OTHER line goes: to the right of travel (below, on
// a run drawn left to right) or to the left of it. Ctrl swaps it while drawing, because which side the
// second line wants to be on depends on what the run is being drawn against, and that is only obvious
// once it is on screen.
Vec2 doubleRunCentreOffset(const Vec2& from, const Vec2& to, double gap, bool secondBelow = true) {
    const Vec2 d = to - from;
    if (d.lengthSq() < 1e-18 || gap <= 0.0) return {0.0, 0.0};
    return d.normalized().perp() * ((secondBelow ? -1.0 : 1.0) * gap / 2.0);
}

std::vector<Vec2> doubleRunCentre(const std::vector<Vec2>& drawn, double gap,
                                  bool secondBelow = true) {
    if (drawn.size() < 2 || gap <= 0.0) return drawn;
    const std::vector<Vec2> centre = offsetPolyline(drawn, (secondBelow ? -1.0 : 1.0) * gap / 2.0);
    // A bend too tight to offset changes the vertex count. Then the drawn line is taken as the
    // centreline, as it always was, rather than a run with vertices the drawing never had.
    return centre.size() == drawn.size() ? centre : drawn;
}

// `doc` is only needed for entities whose handle position is defined by ANOTHER entity
// (a Wire label attached to a wire sits wherever that wire's fraction lands).
std::vector<Grip> gripsOf(const Entity& e, const std::vector<Vec2>& pinEnds = {},
                          const Document* doc = nullptr) {
    std::vector<Grip> g;
    switch (e.type) {
    case EntityType::Line:
        g.push_back({e.a, GripRole::LineA, 0});
        g.push_back({lerp(e.a, e.b, 0.5), GripRole::LineMid, 0});
        g.push_back({e.b, GripRole::LineB, 0});
        break;
    case EntityType::Dimension: {
        // A dimension is held by the same joints AutoCAD gives one: the points it measures between, the
        // two ends of the dimension line - the arrow ends, which is what moves the whole dimension line
        // off the geometry - and the text.
        const QString ty = e.meta.value("dimType").toString(QStringLiteral("linear"));
        if (ty == QLatin1String("linear") || ty == QLatin1String("aligned")) {
            // A dimension saved by a version that wrote only a and b has no stand-off point; it is the
            // midpoint then, which is where the dimension line is drawn, so the arrow-end joints are
            // still there to be taken hold of and dragging one gives the dimension its stand-off back.
            const Vec2 p1 = e.points.size() >= 3 ? e.points[0] : e.a;
            const Vec2 p2 = e.points.size() >= 3 ? e.points[1] : e.b;
            Vec2 d1 = p1, d2 = p2;
            dimensionLineEnds(e, d1, d2);   // where the arrows are, worked out where the drawing does
            g.push_back({p1, GripRole::Vertex, 0});
            g.push_back({p2, GripRole::Vertex, 1});
            g.push_back({d1, GripRole::Vertex, 2});   // the arrow ends: both move the dimension line
            g.push_back({d2, GripRole::Vertex, 2});
            // A grip on the dimension TEXT — drag it to reposition the measurement
            // text; a dashed leader then connects it back to the dimension line.
            Vec2 tpos = lerp(d1, d2, 0.5);
            if (e.meta.contains("textPosX"))
                tpos = {e.meta.value("textPosX").toDouble(), e.meta.value("textPosY").toDouble()};
            g.push_back({tpos, GripRole::DimText, 0});
        } else if (ty == QLatin1String("angular") && e.points.size() >= 3) {
            // The corner it is taken about and the two points its rays run to. Without these an angular
            // dimension had no joint anywhere near it - only the two at a and b, which an angular one
            // never sets, so they sat at the origin of the drawing.
            g.push_back({e.points[0], GripRole::Vertex, 0});
            g.push_back({e.points[1], GripRole::Vertex, 1});
            g.push_back({e.points[2], GripRole::Vertex, 2});
        } else if (ty == QLatin1String("radius") || ty == QLatin1String("diameter") ||
                   ty == QLatin1String("jogged") || ty == QLatin1String("arclength")) {
            // The end of the leader, where the arrow and the text are: what there is to move on one of
            // these, since the centre and the radius are the circle's, not the dimension's. `a` is not a
            // point on these at all, and a joint there put one at the origin of the drawing.
            g.push_back({e.b, GripRole::LineB, 0});
        } else {
            g.push_back({e.a, GripRole::LineA, 0});
            g.push_back({e.b, GripRole::LineB, 0});
        }
        break;
    }
    case EntityType::TwistedPair:
        // A routed pair is held by its knees, like a polyline; a straight one by its two ends. Either
        // way the pair is re-laid along whatever the path becomes - it is a length of twisted pair,
        // not a drawn shape.
        if (e.points.size() >= 2) {
            for (size_t i = 0; i < e.points.size(); ++i)
                g.push_back({e.points[i], GripRole::Vertex, static_cast<int>(i)});
        } else {
            g.push_back({e.a, GripRole::LineA, 0});
            g.push_back({e.b, GripRole::LineB, 0});
        }
        break;
    case EntityType::DoubleLine: {
        // Joints ONLY on the two VISIBLE parallel lines — never on the centerline
        // between them. Each end grip carries its offset so dragging it keeps that
        // physical point under the cursor; each physical mid moves the whole line.
        const double gap = e.meta.value("gap").toDouble(10.0);
        const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
        const Vec2 noff = off * -1.0;
        // Endpoints sit on the ACTUAL physical-line ends (after trim/extend caps),
        // so a joint follows the drawn line rather than floating at the raw offset.
        const Vec2 s1 = cappedRunEndAny(e, "cutStart", e.a + off, e.b + off, +1);
        const Vec2 en1 = cappedRunEndAny(e, "cutEnd", e.b + off, e.a + off, +1);
        const Vec2 s2 = cappedRunEndAny(e, "cutStart", e.a + noff, e.b + noff, -1);
        const Vec2 en2 = cappedRunEndAny(e, "cutEnd", e.b + noff, e.a + noff, -1);
        g.push_back({s1, GripRole::LineA, 0, off});
        g.push_back({s2, GripRole::LineA, 0, noff});
        g.push_back({en1, GripRole::LineB, 0, off});
        g.push_back({en2, GripRole::LineB, 0, noff});
        // A midpoint joint on EACH physical line (moves the whole double line).
        g.push_back({lerp(s1, en1, 0.5), GripRole::LineMid, 0});
        g.push_back({lerp(s2, en2, 0.5), GripRole::LineMid, 0});
        break;
    }
    case EntityType::Polyline:
        // A DOUBLE polyline gets joints on its two PHYSICAL runs — a vertex joint at
        // each end/bend (drags the matching centerline vertex via its offset) and a
        // midpoint joint on each run segment (moves the whole double polyline).
        if (e.meta.value("double").toBool() && e.points.size() >= 2) {
            const double gap = e.meta.value("gap").toDouble(10.0);
            const std::vector<Vec2> run1 = offsetPolyline(e.points, gap / 2.0);
            const std::vector<Vec2> run2 = offsetPolyline(e.points, -gap / 2.0);
            bool mapped = false;
            for (const auto* run : {&run1, &run2}) {
                if (run->size() != e.points.size()) continue;
                mapped = true;
                const int side = (run == &run1) ? +1 : -1;   // which physical run this is
                // Marker positions follow the ACTUAL run, with its ends capped to a
                // trim/extend boundary (offsets from the centreline are kept for drag).
                std::vector<Vec2> pos = *run;
                if (pos.size() >= 2) {
                    pos.front() = cappedRunEndAny(e, "cutStart", pos.front(), (*run)[1], side);
                    pos.back() =
                        cappedRunEndAny(e, "cutEnd", pos.back(), (*run)[run->size() - 2], side);
                }
                for (int i = 0; i < static_cast<int>(run->size()); ++i)
                    g.push_back({pos[i], GripRole::Vertex, i, (*run)[i] - e.points[i]});
                for (size_t i = 1; i < pos.size(); ++i)
                    g.push_back({lerp(pos[i - 1], pos[i], 0.5), GripRole::LineMid, 0});
            }
            if (mapped) break;
            // Offsetting changed the vertex count (tight bends) — fall back to the
            // centerline vertices.
        }
        for (int i = 0; i < static_cast<int>(e.points.size()); ++i)
            g.push_back({e.points[i], GripRole::Vertex, i});
        break;
    case EntityType::Spline:
    case EntityType::Wipeout:
        for (int i = 0; i < static_cast<int>(e.points.size()); ++i)
            g.push_back({e.points[i], GripRole::Vertex, i});
        break;
    case EntityType::Wire: {
        const int n = static_cast<int>(e.points.size());
        for (int i = 0; i < n; ++i) g.push_back({e.points[i], GripRole::Vertex, i});
        if (n < 2) break;
        auto connected = [&](int idx) {
            for (const Vec2& pe : pinEnds)
                if (distance(e.points[idx], pe) < 1e-6) return true;
            return false;
        };
        const bool endConnected = connected(0) || connected(n - 1);
        if (n == 2) {
            // A straight wire gets a middle move joint only while BOTH ends are open
            // (free); once an end is on a pin, moving it would detach it — so no joint
            // until the wire has a knee (below) or the other end connects.
            if (!endConnected)
                g.push_back({lerp(e.points[0], e.points[1], 0.5), GripRole::WireSegMove, 0});
        } else {
            // A bent wire gets a joint on each run BETWEEN two knees (not a terminal
            // segment), to drag that run without moving the connected ends.
            for (int i = 1; i <= n - 3; ++i)
                g.push_back({lerp(e.points[i], e.points[i + 1], 0.5), GripRole::WireSegMove, i});
        }
        // One grip per instance of each marking, so every one can be slid along the run (and
        // nowhere else — a marking belongs to the wire). One role per marking, because the three
        // are placed independently of each other.
        {
            const GripRole roles[] = {GripRole::WireMark, GripRole::WireMarkColor,
                                      GripRole::WireMarkLabel};
            int which = 0;
            for (const QString& part : wireAnnotParts()) {
                const GripRole role = roles[std::min(which++, 2)];
                const auto marks = wireAnnotsFor(e, part);
                for (int i = 0; i < static_cast<int>(marks.size()); ++i)
                    g.push_back({marks[i].pos, role, i});
            }
        }
        break;
    }
    case EntityType::NetLabel: {
        // An attached Wire label slides along its wire; a free one moves normally. Its
        // handle sits where the wire actually puts it, not at the original click point.
        const QString wid = e.meta.value("wireId").toString();
        if (wid.isEmpty() || !doc) break;
        for (const auto& w : doc->entities()) {
            if (w.id != wid || w.type != EntityType::Wire) continue;
            Vec2 at, dir;
            if (wirePointAtFraction(w, e.meta.value("annT").toDouble(0.5), at, dir))
                g.push_back({at, GripRole::WireLabelSlide, 0});
            break;
        }
        break;
    }
    case EntityType::Twist: {
        // A single grip that SLIDES the twist along its wires (constrained: dragging it
        // only changes its position along the wire run, never off the wires).
        if (e.points.empty()) break;
        Vec2 c{0, 0};
        for (const Vec2& pt : e.points) c = c + pt;
        c = c * (1.0 / static_cast<double>(e.points.size()));
        g.push_back({c, GripRole::TwistMove, 0});
        break;
    }
    case EntityType::Screen: {
        // TWO joints per caught wire — one at EACH side where that wire's height
        // actually crosses the ellipse boundary (left AND right), since a wire drawn
        // through the shield truly pierces the outline at both points. Both grips for
        // a wire share its index (dragging either just moves that wire's connection
        // point/height; the side is re-derived automatically next time grips are drawn).
        // Plus the drain endpoint (at its own raw position, outside the ellipse) and a
        // Move handle at the centre.
        const ShieldFrame f = shieldFrameOf(e);
        const int k = f.valid ? f.k
                             : std::min(e.meta.value("catch").toInt(
                                            static_cast<int>(e.points.size())),
                                        static_cast<int>(e.points.size()));
        if (f.valid) {
            const QString shape = e.meta.value("shape").toString(QStringLiteral("ellipse"));
            for (int i = 0; i < f.k; ++i) {
                const Vec2 rel = e.points[i] - f.center;
                const double along = rel.x * f.major.x + rel.y * f.major.y; // up the major axis
                const double half = shieldEdgeHalfWidth(shape, f.rx, f.ry, along);
                const Vec2 base{f.center.x + along * f.major.x, f.center.y + along * f.major.y};
                g.push_back({{base.x - half * f.minor.x, base.y - half * f.minor.y},
                             GripRole::Vertex, i});
                g.push_back({{base.x + half * f.minor.x, base.y + half * f.minor.y},
                             GripRole::Vertex, i});
            }
            // The two LARGE-RADIUS extremes (the outline's major-axis tips). Dragging
            // either resizes the shield's half-height so it can reach further wires.
            g.push_back({{f.center.x + f.ry * f.major.x, f.center.y + f.ry * f.major.y},
                         GripRole::ShieldRy, 0});
            g.push_back({{f.center.x - f.ry * f.major.x, f.center.y - f.ry * f.major.y},
                         GripRole::ShieldRy, 1});
            g.push_back({f.center, GripRole::Move, 0});
        }
        for (int i = k; i < static_cast<int>(e.points.size()); ++i)
            g.push_back({e.points[i], GripRole::Vertex, i}); // drain endpoint
        break;
    }
    case EntityType::Polygon:
        for (int i = 0; i < static_cast<int>(e.points.size()); ++i)
            g.push_back({e.points[i], GripRole::Vertex, i});
        // Centre joint: a move handle (and an object-snap point) at the centroid.
        if (!e.points.empty()) {
            Vec2 c{0, 0};
            for (const auto& v : e.points) c = c + v;
            c = c * (1.0 / static_cast<double>(e.points.size()));
            g.push_back({c, GripRole::Center, 0});
        }
        break;
    case EntityType::Hatch:
        if (e.meta.value("associative").toBool(false)) {
            // Associative hatch: a single grip at the centre (the boundary is tied
            // to its parent objects, so the vertices aren't individually editable).
            const Bounds b = e.bounds();
            g.push_back({b.valid ? b.center() : (e.points.empty() ? Vec2{0, 0} : e.points[0]),
                         GripRole::Move, 0});
        } else if (e.meta.value("shape").toString() == QLatin1String("circle")) {
            // Circle-boundary hatch: 4 radius joints that resize the circle.
            g.push_back({{e.center.x + e.radius, e.center.y}, GripRole::Radius, 0});
            g.push_back({{e.center.x - e.radius, e.center.y}, GripRole::Radius, 0});
            g.push_back({{e.center.x, e.center.y + e.radius}, GripRole::Radius, 0});
            g.push_back({{e.center.x, e.center.y - e.radius}, GripRole::Radius, 0});
        } else {
            // Non-associative polygon: full boundary vertex grips for reshaping.
            for (int i = 0; i < static_cast<int>(e.points.size()); ++i)
                g.push_back({e.points[i], GripRole::Vertex, i});
        }
        break;
    case EntityType::Image:   // a picture is resized by dragging a corner, or a side
    case EntityType::Rect:
        for (int i = 0; i < static_cast<int>(e.points.size()); ++i) {
            g.push_back({e.points[i], GripRole::RectCorner, i});
            // Wall midpoint grip for resizing one side.
            const Vec2 mid = lerp(e.points[i], e.points[(i + 1) % e.points.size()], 0.5);
            g.push_back({mid, GripRole::RectEdge, i});
        }
        // A table also gets grips on each INTERIOR grid line: drag a vertical line to
        // resize the columns either side of it, a horizontal line to resize the rows.
        if (e.meta.value("table").toBool() && e.points.size() == 4) {
            Bounds tb; for (const auto& pt : e.points) tb.expand(pt);
            const QJsonArray cf = e.meta.value("colFrac").toArray();
            const QJsonArray rf = e.meta.value("rowFrac").toArray();
            const double midY = (tb.min.y + tb.max.y) / 2.0;
            const double midX = (tb.min.x + tb.max.x) / 2.0;
            double x = tb.min.x;
            for (int c = 0; c + 1 < cf.size(); ++c) { // interior column boundaries
                x += cf[c].toDouble() * tb.width();
                g.push_back({{x, midY}, GripRole::TableColEdge, c + 1});
            }
            double y = tb.max.y; // rows run top -> down (world y-up)
            for (int r = 0; r + 1 < rf.size(); ++r) { // interior row boundaries
                y -= rf[r].toDouble() * tb.height();
                g.push_back({{midX, y}, GripRole::TableRowEdge, r + 1});
            }
        }
        break;
    case EntityType::Circle:
        g.push_back({e.center, GripRole::Center, 0});
        g.push_back({{e.center.x + e.radius, e.center.y}, GripRole::Radius, 0});
        g.push_back({{e.center.x - e.radius, e.center.y}, GripRole::Radius, 0});
        g.push_back({{e.center.x, e.center.y + e.radius}, GripRole::Radius, 0});
        g.push_back({{e.center.x, e.center.y - e.radius}, GripRole::Radius, 0});
        // Balloon: a grip on each leader arrow tip (drag to reposition; it stays
        // attached to the balloon). Radius grips are dropped for balloons — the
        // circle is a fixed callout size, so only its centre + leader tips move.
        if (e.meta.value("balloon").toBool()) {
            g.clear();
            g.push_back({e.center, GripRole::Center, 0});
            const auto tips = balloonLeaders(e);
            if (!tips.empty()) {
                for (int i = 0; i < static_cast<int>(tips.size()); ++i)
                    g.push_back({tips[i], GripRole::LeaderTip, i}); // arrow tips
                // Landing joint ON the circle (flip side / re-connect), and a KNEE
                // joint at the shelf end (drag to set the horizontal landing length).
                g.push_back({balloonAttach(e), GripRole::LeaderLanding, 0});
                if (e.meta.value("landing").toDouble(8.0) > 0)
                    g.push_back({balloonKnee(e), GripRole::LeaderKnee, 0});
            }
        }
        break;
    case EntityType::Arc: {
        // Center + the two endpoints + the midpoint of the arc.
        g.push_back({e.center, GripRole::Center, 0});
        g.push_back({pointOnCircle(e.center, e.radius, e.startAngle), GripRole::ArcStart, 0});
        g.push_back({pointOnCircle(e.center, e.radius, e.endAngle), GripRole::ArcEnd, 0});
        double sweep = e.endAngle - e.startAngle;
        while (sweep <= 0.0) sweep += 2.0 * M_PI;
        g.push_back({pointOnCircle(e.center, e.radius, e.startAngle + sweep / 2.0),
                     GripRole::ArcMid, 0});
        break;
    }
    case EntityType::Ellipse: {
        // Center + the four axis endpoints (rotated with the ellipse).
        const double c = std::cos(e.rotation), s = std::sin(e.rotation);
        auto axisPt = [&](double ax, double ay) {
            return Vec2{e.center.x + ax * c - ay * s, e.center.y + ax * s + ay * c};
        };
        g.push_back({e.center, GripRole::Center, 0});
        // A PIECE of one has two ends, and they are handles of their own: they move ROUND the ellipse
        // rather than resizing it. They go on FIRST because on a half ellipse an end sits exactly where
        // an axis point does, and at the same place the end is what was grabbed for.
        const bool piece = e.meta.value(QStringLiteral("ellipticalArc")).toBool();
        Vec2 ends[2]{};
        if (piece) {
            ends[0] = ellipsePointAt(e.startAngle, e.center, e.radius, e.radiusY, e.rotation);
            ends[1] = ellipsePointAt(e.endAngle, e.center, e.radius, e.radiusY, e.rotation);
            g.push_back({ends[0], GripRole::EllipseStart, 0});
            g.push_back({ends[1], GripRole::EllipseEnd, 0});
        }
        // ... and the axis points size it and turn it. One that lands on an end is left out: it could
        // never be grabbed there anyway, and two handles in one place with different jobs is worse than
        // one.
        auto axisGrip = [&](const Vec2& at, GripRole role) {
            if (piece && (distance(at, ends[0]) < 1e-9 || distance(at, ends[1]) < 1e-9)) return;
            g.push_back({at, role, 0});
        };
        axisGrip(axisPt(e.radius, 0), GripRole::EllipseMajor);
        axisGrip(axisPt(-e.radius, 0), GripRole::EllipseMajor);
        axisGrip(axisPt(0, e.radiusY), GripRole::EllipseMinor);
        axisGrip(axisPt(0, -e.radiusY), GripRole::EllipseMinor);
        break;
    }
    case EntityType::Text: {
        // Move grip at the anchor; for an MTEXT container, a width grip (right edge)
        // and a height grip (bottom edge) that resize the box in X and Y.
        g.push_back({e.a, GripRole::Move, 0});
        const double bw = e.meta.value("boxWidth").toDouble(0.0);
        const double bh = e.meta.value("boxHeight").toDouble(0.0);
        if (bw > 0.0) g.push_back({{e.a.x + bw, e.a.y}, GripRole::TextWidth, 0});
        if (bh > 0.0) g.push_back({{e.a.x, e.a.y - bh}, GripRole::TextHeight, 0});
        break;
    }
    case EntityType::Connector: {
        // Movement joints at the four rectangle corners (drag any to move the whole
        // connector). Plus movable-text grips: each text role (pin-label block,
        // designator, part number) drags separately. Offsets are relative to the body.
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
        const ConnectorGeometry cg = connectorGeometry(e.a, spec);
        g.push_back({cg.rectMin, GripRole::Move, 0});
        g.push_back({{cg.rectMax.x, cg.rectMin.y}, GripRole::Move, 0});
        g.push_back({cg.rectMax, GripRole::Move, 0});
        g.push_back({{cg.rectMin.x, cg.rectMax.y}, GripRole::Move, 0});
        if (!spec.designator.isEmpty())
            g.push_back({cg.designatorPos, GripRole::ConnDesText, 0});
        if (!spec.partNumber.isEmpty())
            g.push_back({cg.partPos, GripRole::ConnPnText, 0});
        if (!cg.pinLabelPos.empty())
            g.push_back({cg.pinLabelPos[cg.pinLabelPos.size() / 2], GripRole::ConnPinText, 0});
        break;
    }
    default: // splice / netlabel / notconnected
        g.push_back({e.a, GripRole::Move, 0});
        break;
    }
    return g;
}
} // namespace

Editor::Editor(QObject* parent) : QObject(parent) {
    hookSelectionCompanions();
    dragTimer_ = new QTimer(this);
    dragTimer_->setInterval(30); // animate the countdown + check the hold threshold
    connect(dragTimer_, &QTimer::timeout, this, [this] { onDragTick(); });
}

// Every path that changes the selection ends in selectionChanged(), so the companion set is
// refreshed from there rather than at each of the two dozen call sites - it cannot go stale.
void Editor::hookSelectionCompanions() {
    connect(this, &Editor::selectionChanged, this, [this] {
        // A block that has been placed is ONE object: picking any piece of it picks the whole copy,
        // and its pieces have no grips, so nothing inside it can be reshaped on the sheet. It is
        // changed by double-clicking it, which opens the block itself.
        //
        // Done from here for the same reason the companions are: every path that changes the
        // selection ends in this signal, so none of them can forget.
        if (expandBlockSelection()) {
            emit selectionChanged();   // it grew - go round once more, then settle
            return;
        }
        refreshBundleCompanions();
    });
}

// One copy of a block, or one placed template frame: either way a set of objects that belongs
// together and is picked as one thing.
static QString groupKeyOf(const Entity& e) {
    const QString block = entityBlockInstance(e);
    return block.isEmpty() ? entityTemplateGroup(e) : block;
}

bool Editor::expandBlockSelection() {
    if (!doc_ || selected_.empty()) return false;
    // Which groups are involved: a copy of a block, or a template frame that was applied.
    QStringList keys;
    for (const QString& id : selected_) {
        const Entity* e = entityById(id);
        if (!e) continue;
        const QString key = groupKeyOf(*e);
        if (!key.isEmpty() && !keys.contains(key)) keys << key;
    }
    if (keys.isEmpty()) return false;
    bool grew = false;
    for (const Entity& e : doc_->entities()) {
        if (!keys.contains(groupKeyOf(e))) continue;
        if (std::find(selected_.begin(), selected_.end(), e.id) != selected_.end()) continue;
        selected_.push_back(e.id);
        grew = true;
    }
    if (grew) {
        const Entity* first = entityById(selected_.front());
        const QString block = first ? entityBlockName(*first) : QString();
        const QString tplName = first ? entityTemplateName(*first) : QString();
        if (!block.isEmpty())
            emit message(QStringLiteral("%1 is a block — double-click it to change what is in it")
                             .arg(block));
        else if (first && entityBlockInstance(*first) == QLatin1String(kMechModelGroup))
            emit message(QStringLiteral(
                "The model moves as one — double-click it to open it up and edit the lines"));
        else if (!entityTemplateGroup(*first).isEmpty())
            emit message(
                tplName.isEmpty()
                    ? QStringLiteral("The sheet frame moves as one — double-click it to edit the "
                                     "template")
                    : QStringLiteral("%1 is the sheet's template — it moves as one, and a "
                                     "double-click edits it")
                          .arg(tplName));
    }
    return grew;
}

bool Editor::isBlockPiece(const Entity& e) {
    // No grips on a placed block, nor on a template frame: they move as one and are changed in their
    // own editor. A FIELD is the exception - it exists to be filled in on the drawing.
    if (entityIsTemplateField(e)) return false;
    return !groupKeyOf(e).isEmpty();
}

void Editor::setDocument(Document* doc) {
    doc_ = doc;
    selected_.clear();
    draftPts_.clear();
    modifyPts_.clear();
    drafting_ = false;
    editing_ = false;
    gripGrab_ = false;
    pressed_ = false;
    regionMode_ = RegionMode::None;
    undo_.clear();
    emit changed();
    emit selectionChanged();
}

void Editor::setTool(ToolId t) {
    if (t != ToolId::ModelConnectPoint) haveConnectFirst_ = false;
    clearRefusal();   // a fresh command starts with a clean drawing
    cancelDraw();
    modifyPts_.clear();
    pickIds_.clear();
    pickPts_.clear();
    offsetHasDist_ = false;
    offsetDist_ = 0.0;
    offsetDistPts_.clear();
    offsetSourceId_.clear();
    clearCellSelection();
    mirrorPending_ = false;
    awaitAlignRef_ = false;   // leaving/entering a tool cancels align-routing pick mode
    awaitTwistAlign_ = false; // ...and twist-align pick mode
    awaitTwistRemove_ = false; // ...and remove-twist mode
    awaitCrossSwitch_ = false; // ...and the cross-switch mode (Esc → Select disarms it)
    awaitQuickSelect_ = false; // ...and a Quick Select waiting for its sample objects
    const bool wasMatching = awaitMatchProps_;
    awaitMatchProps_ = false;  // ...and match-properties mode
    matchSourceId_.clear();
    if (wasMatching) emit matchPropsChanged(); // un-latch the ribbon's Match button
    const bool wasTwistArmed = !twistArmType_.isEmpty();
    twistArmType_.clear();    // ...and the twist / screen band modes
    twistFrozen_ = false;
    if (wasTwistArmed) emit twistArmChanged();
    const bool wasScreenArmed = screenArm_;
    screenArm_ = false;
    screenFrozen_ = false;
    wireBandHasStart_ = false;
    if (wasScreenArmed) emit screenArmedChanged();
    connectorArmed_ = false; // selecting/leaving the Connector tool hides the preview
    connectorFrozen_ = false;
    if (arrayActive_) cancelArray(); // ...and an un-placed array (its ribbon closes with it)
    editing_ = false;
    gripGrab_ = false;
    pressed_ = false;
    gapInputMode_ = false;
    filletInputMode_ = false;
    regionMode_ = RegionMode::None;
    // Starting to draw/place a new primitive clears any current selection (its
    // grips would otherwise linger). Modify tools keep the selection they act on.
    if ((isDrawTool(t) || isPlacementTool(t)) && !selected_.empty()) {
        selected_.clear();
        emit selectionChanged();
    }
    // Trim/Extend: the objects selected when the tool starts are the reference
    // (cutting/boundary edges). The user then draws a fence line; objects it crosses
    // are trimmed back to / extended to the reference. (Reused for both tools.)
    trimRefIds_.clear();
    breakFrozen_ = false;   // a pinned break preview belongs to the break tool
    trimFenceActive_ = false;
    if (t == ToolId::Trim || t == ToolId::Extend) trimRefIds_ = selected_;
    tool_ = t;
    if (t != ToolId::Select) lastTool_ = t; // remember for repeat (Space/Enter)
    emit toolChanged(t);
    emit message(toolLabel(t));
    emit changed();
    // Polygon: ask for the number of sides up front (default shown); the flying
    // field pops so the user types the count before picking the centre.
    if (t == ToolId::Polygon)
        emit dynamicPrompt(QStringLiteral("Number of sides <%1>").arg(polygonSides_));
    // Scale: pop the flying field so a scale factor can be TYPED (about the selection
    // centre) without touching the command bar — and it reappears every time Scale is
    // entered, so it stays repeatable.
    if (t == ToolId::Scale && !selected_.empty())
        emit dynamicPrompt(QStringLiteral("Scale factor (or pick base point)"));
    // Splice / X: pop the flying field so the marker size can be typed before placing.
    if (t == ToolId::Splice)
        emit dynamicPrompt(QStringLiteral("Joint size <%1>").arg(spliceSize_));
    else if (t == ToolId::NotConnected)
        emit dynamicPrompt(QStringLiteral("X size <%1>").arg(ncSize_));
}

void Editor::repeatLastTool() {
    if (lastTool_ != ToolId::Select) setTool(lastTool_);
}

double Editor::worldTolerance() const {
    // The scale the world is actually being seen at. Inside a window on a layout that is the window's,
    // not the model view's - a pick is so many pixels of screen wherever it is made, and the world it
    // covers follows what is on the screen.
    const double scale = inputScale_ > 0.0 ? inputScale_ : (doc_ ? doc_->view().scale : 1.0);
    return pixelTol_ / std::max(1e-9, scale);
}

QString Editor::uid() const {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

void Editor::pushUndo() {
    if (doc_) undo_.push(doc_->entities());
}

void Editor::addEntity(Entity e) {
    if (!doc_) return;
    if (e.id.isEmpty()) e.id = uid();
    if (e.layer.isEmpty()) e.layer = doc_->activeLayer();
    // Adopt the current draw color (empty => ByLayer) unless the entity already
    // carries its own explicit color (e.g. wires).
    if (e.color.isEmpty()) e.color = currentColor_;
    if (e.weight == 0.0) e.weight = currentWeight_; // 0 => ByLayer
    doc_->entities().push_back(std::move(e));
    doc_->setDirty(true);
}

// ---- Hit testing ----------------------------------------------------------

namespace {

// World-space line segments that make up a dimension's drawn geometry, so hit-test
// and region selection both react to the dimension line / extension lines / arc.
std::vector<std::pair<Vec2, Vec2>> dimensionSegments(const Entity& e) {
    std::vector<std::pair<Vec2, Vec2>> segs;
    const QString ty = e.meta.value("dimType").toString(QStringLiteral("linear"));
    if (ty == QLatin1String("radius") || ty == QLatin1String("jogged")) {
        segs.push_back({e.center, e.b});
    } else if (ty == QLatin1String("diameter")) {
        const Vec2 dir = (e.b - e.center).normalized();
        segs.push_back({e.center - dir * e.radius, e.center + dir * e.radius});
    } else if (ty == QLatin1String("angular") && e.points.size() >= 3) {
        const Vec2 v = e.points[0];
        segs.push_back({v, e.points[1]});
        segs.push_back({v, e.points[2]});
        const double rr = std::min(distance(v, e.points[1]), distance(v, e.points[2])) * 0.6;
        const double a0 = std::atan2(e.points[1].y - v.y, e.points[1].x - v.x);
        double a1 = std::atan2(e.points[2].y - v.y, e.points[2].x - v.x);
        double sweep = a1 - a0;
        while (sweep < 0) sweep += 2.0 * M_PI;
        const int n = 12;
        Vec2 prev = pointOnCircle(v, rr, a0);
        for (int i = 1; i <= n; ++i) {
            const Vec2 cur = pointOnCircle(v, rr, a0 + sweep * i / n);
            segs.push_back({prev, cur});
            prev = cur;
        }
    } else if (ty == QLatin1String("ordinate")) {
        segs.push_back({e.a, e.b});
    } else { // linear / aligned / arclength
        const Vec2 p1 = e.a, p2 = e.b;
        const Vec2 off = e.points.size() >= 3 ? e.points[2] : lerp(p1, p2, 0.5);
        Vec2 d1, d2;
        if (ty == QLatin1String("linear")) {
            const Vec2 mid = lerp(p1, p2, 0.5);
            const bool horiz = std::abs(off.y - mid.y) >= std::abs(off.x - mid.x);
            if (horiz) { d1 = {p1.x, off.y}; d2 = {p2.x, off.y}; }
            else { d1 = {off.x, p1.y}; d2 = {off.x, p2.y}; }
        } else {
            const Vec2 n = (p2 - p1).normalized().perp();
            const double t = dot(off - p1, n);
            d1 = p1 + n * t;
            d2 = p2 + n * t;
        }
        segs.push_back({d1, d2}); // dimension line
        segs.push_back({p1, d1}); // extension lines
        segs.push_back({p2, d2});
    }
    return segs;
}

double distanceToEntity(const Entity& e, const Vec2& p) {
    auto segDist = [&](const Vec2& a, const Vec2& b) {
        return closestPointOnSegment(p, a, b).distance;
    };
    double best = std::numeric_limits<double>::max();
    switch (e.type) {
    case EntityType::Line:
        best = segDist(e.a, e.b);
        break;
    case EntityType::TwistedPair: {
        // The two wires, not the line they were drawn along: the middle of a twisted pair is empty
        // paper between the crossings, and clicking there should no more pick it than the gap of a
        // double line picks that.
        //
        // A pair well clear of the point is answered from its PATH, which is a handful of segments
        // rather than a few hundred: every mouse movement asks every object how far away it is, and
        // walking the whole weave of every pair on the sheet each time is what made the cursor lag.
        // The answer is then a little large - by at most half the cable's width - which cannot make a
        // far-off pair beat something nearer.
        const double halfW =
            std::max(0.2, e.meta.value(QStringLiteral("twistWidth")).toDouble(kTwistedPairWidth)) / 2.0;
        const std::vector<Vec2>& along =
            e.points.size() >= 2 ? e.points : std::vector<Vec2>{e.a, e.b};
        double toPath = std::numeric_limits<double>::max();
        for (size_t i = 1; i < along.size(); ++i)
            toPath = std::min(toPath, segDist(along[i - 1], along[i]));
        if (toPath > halfW * 2.0) {
            best = toPath;
            break;
        }
        for (const auto& strand : twistedPairStrands(e))
            for (size_t i = 1; i < strand.size(); ++i)
                best = std::min(best, segDist(strand[i - 1], strand[i]));
        break;
    }
    case EntityType::Dimension:
        // Select by touching the actual drawn geometry (dimension line, extension
        // lines, radius/diameter line, angular arc, ...).
        for (const auto& s : dimensionSegments(e)) best = std::min(best, segDist(s.first, s.second));
        break;
    case EntityType::DoubleLine: {
        // Only the two VISIBLE parallel lines select the double line — NOT the centerline / gap between
        // them (clicking the empty middle won't pick it). Visible includes what a CAP adds: a line
        // extended to meet something is drawn out to it, and a part of the drawing that cannot be
        // clicked cannot be selected or trimmed either - which is what made an extended line untouchable.
        const double gap = e.meta.value("gap").toDouble(10.0);
        const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
        for (int side : {+1, -1}) {
            std::vector<Vec2> rail = {e.a + off * side, e.b + off * side};
            applyDoubleRunCuts(rail, e, side);
            // A piece trimmed out of the middle of a line is not there to be picked either.
            for (const auto& piece : splitRailAtHoles(rail, e, side))
                for (size_t i = 1; i < piece.size(); ++i)
                    best = std::min(best, segDist(piece[i - 1], piece[i]));
        }
        break;
    }
    case EntityType::Polyline:
        // A DOUBLE polyline is picked on its two VISIBLE physical runs (offset by
        // ±gap/2), not the centerline in the empty gap.
        if (e.meta.value("double").toBool() && e.points.size() >= 2) {
            const double gap = e.meta.value("gap").toDouble(10.0);
            for (double s : {gap / 2.0, -gap / 2.0}) {
                std::vector<Vec2> run = offsetPolyline(e.points, s);
                const int side = s > 0 ? +1 : -1;
                applyDoubleRunCuts(run, e, side);   // the line as DRAWN: caps in, holes out
                for (const auto& piece : splitRailAtHoles(run, e, side))
                    for (size_t i = 1; i < piece.size(); ++i)
                        best = std::min(best, segDist(piece[i - 1], piece[i]));
            }
            break;
        }
        [[fallthrough]];
    case EntityType::Spline:
        for (size_t i = 1; i < e.points.size(); ++i)
            best = std::min(best, segDist(e.points[i - 1], e.points[i]));
        break;
    case EntityType::Wire: {
        for (size_t i = 1; i < e.points.size(); ++i)
            best = std::min(best, segDist(e.points[i - 1], e.points[i]));
        // The rows of text the wire carries are part of it: a click on the colour, the
        // cross-section or the label picks the wire, which is what shows them in Properties.
        for (const QString& part : wireAnnotParts()) {
            if (wireAnnotPartText(e, part).isEmpty()) continue;
            // Each marking sits its own height above the run, along it: the text band is the run
            // shifted up, so its distance is measured against the same segments.
            const double gap = wireAnnotGapFor(e, part);
            const double annSize = wireAnnotSizeFor(e, part);
            for (const WireAnnot& a : wireAnnotsFor(e, part)) {
                const Vec2 up = a.dir.perp() * ((a.flip ? -1.0 : 1.0) * (gap + annSize * 0.5));
                // A short band either side of the anchor, the length text of this size runs to.
                const Vec2 half = a.dir * (annSize * 2.5);
                best = std::min(best, segDist(a.pos + up - half, a.pos + up + half));
            }
        }
        break;
    }
    case EntityType::Screen: {
        // Pick on the ellipse outline (approx) or the drain lead.
        const int k = std::min(e.meta.value("catch").toInt(static_cast<int>(e.points.size())),
                               static_cast<int>(e.points.size()));
        if (k < 1) break;
        double cx = 0.0, yMin = 1e18, yMax = -1e18;
        for (int i = 0; i < k; ++i) {
            cx += e.points[i].x;
            yMin = std::min(yMin, e.points[i].y);
            yMax = std::max(yMax, e.points[i].y);
        }
        cx /= k;
        const double cyMid = (yMin + yMax) / 2.0;
        const double rx = e.meta.value("rx").toDouble(12.0);
        const double ry = e.meta.contains("ry") ? e.meta.value("ry").toDouble()
                                                : (yMax - yMin) / 2.0 + 6.0;
        const Vec2 ctr{cx, cyMid};
        const Vec2 dv = p - ctr;
        const double ang = std::atan2(dv.y, dv.x);
        const double rAng = rx * ry / std::sqrt(std::pow(ry * std::cos(ang), 2) +
                                                std::pow(rx * std::sin(ang), 2));
        best = std::abs(dv.length() - rAng);
        if (static_cast<int>(e.points.size()) > k) {
            const Vec2 drain = e.points.back();
            const QString legDir = e.meta.value("legDir").toString(QStringLiteral("bottom"));
            Vec2 edge;
            if (legDir == QLatin1String("top")) edge = {cx, cyMid + ry};
            else if (legDir == QLatin1String("left")) edge = {cx - rx, cyMid};
            else if (legDir == QLatin1String("right")) edge = {cx + rx, cyMid};
            else edge = {cx, cyMid - ry};
            best = std::min(best, segDist(edge, drain));
        }
        break;
    }
    case EntityType::Image:
    case EntityType::Rect:
    case EntityType::Polygon:
        for (size_t i = 0; i < e.points.size(); ++i)
            best = std::min(best, segDist(e.points[i], e.points[(i + 1) % e.points.size()]));
        // Tables / note / revision boxes are pickable anywhere INSIDE (so a cell or
        // the box body can be clicked and double-clicked to edit, not only its border).
        // So is a picture: what you see of it is its whole area, so that is what you click.
        if (e.type == EntityType::Image || e.meta.value("table").toBool() ||
            e.meta.value("note").toBool() || e.meta.value("revision").toBool()) {
            const Bounds b = boundsOf(e.points);
            if (b.valid && p.x >= b.min.x && p.x <= b.max.x && p.y >= b.min.y && p.y <= b.max.y)
                best = 0.0;
        }
        break;
    case EntityType::Hatch:
    case EntityType::Wipeout: {
        if (e.type == EntityType::Hatch &&
            e.meta.value("shape").toString() == QLatin1String("circle")) {
            const double dc = distance(e.center, p);
            best = dc <= e.radius ? 0.0 : dc - e.radius; // pickable anywhere inside
            break;
        }
        for (size_t i = 0; i < e.points.size(); ++i)
            best = std::min(best, segDist(e.points[i], e.points[(i + 1) % e.points.size()]));
        // A filled region is also pickable anywhere inside it (ray-cast test).
        if (e.points.size() >= 3) {
            bool inside = false;
            for (size_t i = 0, j = e.points.size() - 1; i < e.points.size(); j = i++) {
                const Vec2& a = e.points[i];
                const Vec2& b = e.points[j];
                if (((a.y > p.y) != (b.y > p.y)) &&
                    (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x))
                    inside = !inside;
            }
            if (inside) best = 0.0;
        }
        break;
    }
    case EntityType::Circle:
    case EntityType::Arc:
        best = std::abs(distance(e.center, p) - e.radius);
        if (e.meta.value("balloon").toBool()) {
            // Pickable anywhere within the disc, AND along its leader lines — so
            // clicking the arrow/leader selects the balloon (its whole batch).
            if (distance(e.center, p) <= e.radius) best = 0.0;
            const auto tips = balloonLeaders(e);
            if (!tips.empty()) {
                const Vec2 attach = balloonAttach(e), knee = balloonKnee(e);
                best = std::min(best, segDist(attach, knee));
                for (const auto& tip : tips) best = std::min(best, segDist(knee, tip));
            }
        }
        break;
    case EntityType::Ellipse: {
        // Coarse: distance to the bounding ellipse via normalized radius.
        const double rx = std::max(1e-6, e.radius), ry = std::max(1e-6, e.radiusY);
        const Vec2 d = p - e.center;
        const double r = std::hypot(d.x / rx, d.y / ry);
        best = std::abs(r - 1.0) * std::min(rx, ry);
        // A TRIMMED ellipse is only there where it is still drawn: off its sweep, the nearest thing to
        // the click is one of its two ends. Without this, clicking the half that was trimmed away still
        // picked the ellipse - the object answering for geometry that is no longer on the drawing.
        if (e.meta.value(QStringLiteral("ellipticalArc")).toBool()) {
            double sweep = e.endAngle - e.startAngle;
            while (sweep <= 0.0) sweep += 2.0 * M_PI;
            double rel = ellipseParamAt(p, e.center, rx, ry, e.rotation) - e.startAngle;
            while (rel < 0.0) rel += 2.0 * M_PI;
            if (rel > sweep + 1e-9)
                best = std::min(distance(p, ellipsePointAt(e.startAngle, e.center, rx, ry, e.rotation)),
                                distance(p, ellipsePointAt(e.endAngle, e.center, rx, ry, e.rotation)));
        }
        break;
    }
    case EntityType::Text: {
        // Hit-test the actual text rectangle (bounds() = the glyph extent), so a
        // click/double-click lands on the text itself, not the empty container.
        const Bounds b = e.bounds();
        if (p.x >= b.min.x && p.x <= b.max.x && p.y >= b.min.y && p.y <= b.max.y) {
            best = 0.0;
        } else {
            const Vec2 c0{b.min.x, b.min.y}, c1{b.max.x, b.min.y}, c2{b.max.x, b.max.y},
                c3{b.min.x, b.max.y};
            best = std::min({segDist(c0, c1), segDist(c1, c2), segDist(c2, c3), segDist(c3, c0)});
        }
        break;
    }
    case EntityType::Splice:
        best = distance(e.a, p);
        // A two-conductor splice is one part: a click on either of its joints picks it.
        if (e.meta.value("pair").toBool(false)) best = std::min(best, distance(e.b, p));
        break;
    case EntityType::NetLabel:
    case EntityType::NotConnected:
        best = distance(e.a, p);
        break;
    case EntityType::LengthBreak:
        // A click on either STROKE picks it, the way a click on a line picks the line. Measured to the
        // centre instead, the mark could only be picked by clicking the empty middle between its two
        // strokes - which is where the run it interrupts is, so the click went to the run.
        for (const auto& stroke : breakStrokes(e))
            for (size_t i = 1; i < stroke.size(); ++i)
                best = std::min(best, segDist(stroke[i - 1], stroke[i]));
        break;
    case EntityType::Connector: {
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
        const ConnectorGeometry g = connectorGeometry(e.a, spec);
        const Vec2 c0{g.rectMin.x, g.rectMin.y}, c1{g.rectMax.x, g.rectMin.y},
            c2{g.rectMax.x, g.rectMax.y}, c3{g.rectMin.x, g.rectMax.y};
        best = std::min({segDist(c0, c1), segDist(c1, c2), segDist(c2, c3), segDist(c3, c0)});
        // Pins are pickable too (stub tip -> end). The INTERIOR is intentionally NOT a
        // hit: clicking the empty inside starts a region selection instead of picking
        // the connector.
        for (size_t i = 0; i < g.pinTip.size() && i < g.pinEnd.size(); ++i)
            best = std::min(best, segDist(g.pinTip[i], g.pinEnd[i]));
        break;
    }
    default:
        break;
    }
    return best;
}

} // namespace

std::optional<QString> Editor::hitTest(const Vec2& world, const std::vector<QString>& skip) const {
    if (!doc_) return std::nullopt;
    // Looking for the OTHER object at this place (skip is not empty) reaches a little further: the click
    // was made at a joint, one object answered first, and the second is by definition right there. Half
    // a pixel of difference in how two shapes measure their distance should not decide whether a corner
    // can be filleted.
    const double tol = worldTolerance() * (skip.empty() ? 1.0 : 1.6);
    double best = tol;
    std::optional<QString> hit;
    // Iterate in reverse so topmost (last drawn) wins - and it must WIN A TIE, which needs a strict
    // comparison. With "<=" the last object examined took an equal distance, which is the BOTTOM-most:
    // where a line ends on an arc, both are nought away from the click (the snap puts the cursor exactly
    // on the joint), so clicking the line picked whatever was underneath it. Which is why a line meeting
    // a curve at a joint could not be filleted: the two picks were the same object.
    const auto& ents = doc_->entities();
    for (auto it = ents.rbegin(); it != ents.rend(); ++it) {
        // A default model's connection/near/far markers live on a LOCKED layer - locked so nothing else
        // on it (the drawing's own guide lines) can be nudged by accident - but the markers themselves are
        // exactly the thing a drawing office needs to pick up and move, or decide was put in the wrong
        // place. So they are exempted from the lock, and only while the Select tool is the one asking:
        // every other tool keeps the lock exactly as it was, so a Trim or a Fillet never catches one by
        // mistake.
        const bool anchorMarker = tool_ == ToolId::Select && it->meta.contains(QStringLiteral("modelAnchor"));
        if (!anchorMarker)
            if (const Layer* l = doc_->findLayer(it->layer); l && (!l->visible || l->locked))
                continue;
        if (!skip.empty() && std::find(skip.begin(), skip.end(), it->id) != skip.end()) continue;
        // A stretch of run that a break mark hides is not there to be clicked either: the drawing does
        // not show it, so a click in it belongs to the mark, or to nothing.
        if (pointHiddenByBreak(*it, world)) continue;
        const double d = distanceToEntity(*it, world);
        if (d < best) { best = d; hit = it->id; }
    }
    return hit;
}

Entity* Editor::entityById(const QString& id) {
    if (!doc_) return nullptr;
    for (auto& e : doc_->entities())
        if (e.id == id) return &e;
    return nullptr;
}

std::vector<Entity*> Editor::selectedEntities() {
    std::vector<Entity*> out;
    for (const auto& id : selected_)
        if (Entity* e = entityById(id)) out.push_back(e);
    return out;
}

// ---- Pointer handling ------------------------------------------------------

namespace {
// Defined later in this file; forward-declared for tryHatchAt below.
std::vector<Vec2> boundaryPointsOf(const Entity& e, bool manyJoints);
} // namespace

void Editor::handleSelectPress(const Vec2& p, const Vec2& raw, Qt::KeyboardModifiers mods) {
    const bool shift = mods & Qt::ShiftModifier;
    const bool ctrl = mods & Qt::ControlModifier;
    clearCellSelection(); // any click clears the table-cell selection unless it re-picks one

    // Ctrl+click balloon leaders (AutoCAD Add/Remove Leader gestures):
    //  - near an existing arrow tip -> remove that leader (if the balloon has >1);
    //  - otherwise on the balloon circle -> add a new leader (drag its arrow to place).
    if (ctrl) {
        const double tol = worldTolerance() * 2.0;
        for (auto& e : doc_->entities()) {
            if (!e.meta.value("balloon").toBool()) continue;
            auto tips = balloonLeaders(e);
            for (int i = 0; i < static_cast<int>(tips.size()); ++i)
                if (distance(p, tips[i]) < tol && tips.size() > 1) {
                    pushUndo();
                    tips.erase(tips.begin() + i);
                    setBalloonLeaders(e, tips);
                    doc_->setDirty(true);
                    emit changed();
                    emit message(QStringLiteral("Leader removed"));
                    return;
                }
        }
        if (auto hit = hitTest(p))
            if (Entity* e = entityById(*hit); e && e->meta.value("balloon").toBool()) {
                addCoupledCircle(*hit); // add a circle opposite the arrow
                return;
            }
    }

    // (a) A grabbed joint is following the cursor: this click places it.
    if (editing_ && gripGrab_) {
        // The leader-landing joint: dropped inside ANOTHER balloon re-connects the
        // whole leader to that circle; otherwise it flips the landing to the side of
        // the circle the joint was dragged toward.
        if (editRole_ == static_cast<int>(GripRole::LeaderLanding)) {
            Entity* src = entityById(editEntityId_);
            Entity* tgt = nullptr;
            for (auto& x : doc_->entities())
                if (x.meta.value("balloon").toBool() && x.id != editEntityId_ &&
                    distance(x.center, p) <= x.radius) { tgt = &x; break; }
            if (src && tgt) {
                // Jump the leader to the neighbour circle it was dropped on.
                pushUndo();
                const double landing = std::max(0.0, src->meta.value("landing").toDouble(8.0));
                setBalloonLeaders(*tgt, balloonLeaders(*src)); // move the whole leader over
                tgt->meta.insert("landing", landing);
                if (landing > 0) tgt->meta.insert("landingSide", (p.x >= tgt->center.x) ? 1.0 : -1.0);
                else tgt->meta.insert("landingAngle",
                                      std::atan2(p.y - tgt->center.y, p.x - tgt->center.x));
                src->meta.remove("leaders");
                src->meta.remove("landingSide");
                src->meta.remove("landingAngle");
                doc_->setDirty(true);
                emit message(QStringLiteral("Leader re-connected to the other circle"));
            } else if (src) {
                pushUndo();
                const double landing = std::max(0.0, src->meta.value("landing").toDouble(8.0));
                if (landing > 0) {
                    src->meta.insert("landingSide", (p.x >= src->center.x) ? 1.0 : -1.0);
                    emit message(QStringLiteral("Leader landing side changed"));
                } else {
                    // No landing: the connection moves freely AROUND the circle.
                    src->meta.insert("landingAngle",
                                     std::atan2(p.y - src->center.y, p.x - src->center.x));
                    emit message(QStringLiteral("Leader attachment moved around the circle"));
                }
                doc_->setDirty(true);
            }
            editing_ = false;
            gripGrab_ = false;
            editMoved_ = false;
            editGhost_.clear();
            emit changed();
            return;
        }
        applyEditDrag(p);
        // A shield's large-radius joint: settle the new size against the wires it now
        // encloses (or snap back if it reached none). Must run while editGhost_ still
        // holds the pre-drag shield, which is what the revert restores.
        if (editRole_ == static_cast<int>(GripRole::ShieldRy)) commitShieldResize(editEntityId_);
        // What the placed joint LANDED ON decides whether it is now connected to something.
        if (editRole_ == static_cast<int>(GripRole::Vertex)) {
            if (Entity* pe = entityById(editEntityId_)) {
                const int k = std::min(pe->meta.value("catch").toInt(
                                           static_cast<int>(pe->points.size())),
                                       static_cast<int>(pe->points.size()));
                // A shield's drain leg dropped on a wire: the X goes and the shield can pull
                // that wire from here on.
                if (pe->type == EntityType::Screen && editIndex_ >= k) {
                    if (attachShieldLeg(*pe))
                        emit message(QStringLiteral("Shield drain connected to the wire"));
                } else if (pe->type == EntityType::Wire && !pe->points.empty() &&
                           (editIndex_ == 0 ||
                            editIndex_ == static_cast<int>(pe->points.size()) - 1)) {
                    // A wire END brought onto another wire's end: the two are one wire now.
                    // (mergeWireEnds pushes its own undo step and reports what it did.)
                    const Vec2 tip = pe->points[editIndex_];
                    if (!mergeWireEnds(editEntityId_)) {
                        // Not a wire, but it may be a shield's drain leg: connect to that.
                        for (auto& sc : doc_->entities()) {
                            if (sc.type != EntityType::Screen || sc.points.empty()) continue;
                            const int sk = std::min(sc.meta.value("catch").toInt(
                                                        static_cast<int>(sc.points.size())),
                                                    static_cast<int>(sc.points.size()));
                            if (static_cast<int>(sc.points.size()) <= sk) continue;
                            if (distance(sc.points.back(), tip) >
                                std::max(worldTolerance(), 1.0))
                                continue;
                            if (attachShieldLeg(sc)) {
                                emit message(QStringLiteral("Wire connected to the shield "
                                                          "drain"));
                                break;
                            }
                        }
                    }
                }
            }
        }
        // Shift while placing a double line/polyline END onto another line: cap BOTH
        // physical runs flush to that line (they miter to touch it) rather than ending
        // square at the centreline. Cap the MOVED end to whatever cutter passes through
        // the drop point `p` (which lands on the target line).
        bool capped = false;
        if (shift) {
            if (Entity* ge = entityById(editEntityId_)) {
                const bool dblLine = ge->type == EntityType::DoubleLine;
                const bool dblPoly = ge->type == EntityType::Polyline &&
                                     ge->meta.value("double").toBool();
                QString key;
                if (dblLine) {
                    if (editRole_ == static_cast<int>(GripRole::LineA)) key = QStringLiteral("cutStart");
                    else if (editRole_ == static_cast<int>(GripRole::LineB)) key = QStringLiteral("cutEnd");
                } else if (dblPoly && editRole_ == static_cast<int>(GripRole::Vertex)) {
                    const int n = static_cast<int>(ge->points.size());
                    if (editIndex_ == 0) key = QStringLiteral("cutStart");
                    else if (editIndex_ == n - 1) key = QStringLiteral("cutEnd");
                }
                if (!key.isEmpty()) {
                    capDoubleEndAt(*ge, key, p, doc_->entities());
                    capped = ge->meta.value(key).toBool();
                }
            }
        }
        editing_ = false;
        gripGrab_ = false;
        editMoved_ = false;
        editGhost_.clear(); // source ghost cleared now that it's placed
        emit message(capped
                         ? QStringLiteral("Connected — both physical lines capped to the target")
                         : QStringLiteral("Joint placed"));
        emit changed();
        return;
    }
    // (b) Pending rectangle waiting for its opposite corner.
    if (regionMode_ == RegionMode::RectPending) {
        finishRegionSelect(shift);
        regionMode_ = RegionMode::None;
        emit changed();
        return;
    }
    // (c0) A grabbed marking is dropped by the next click, the way a grabbed joint is.
    if (markGrab_) {
        markGrab_ = false;
        markGrabAt_ = -1;
        markMoved_ = false;
        emit message(QStringLiteral("Marking placed"));
        emit changed();
        return;
    }
    // (c1) Click on a picked marking -> take hold of it. It follows the cursor along its own
    // wire; with Shift, every picked marking follows, each along the wire it belongs to.
    if (const int at = markSelectionAt(p); at >= 0) {
        pushUndo();
        markGrab_ = true;
        markGrabAt_ = at;
        markGrabAll_ = shift;
        markMoved_ = false;
        pressed_ = true;
        pressMods_ = mods;
        emit message(shift && markSel_.size() > 1
                         ? QStringLiteral("Pulling %1 markings — each moves along its own wire")
                               .arg(markSel_.size())
                         : QStringLiteral("Pulling the marking along its wire"));
        return;
    }
    // (c) Click on a joint of a selected entity -> grab it (click to place).
    if (beginGripGrab(raw, shift)) return;

    // (d) Click on an entity body -> select only (Shift removes). Dragging the body
    // does NOT move the object — moving requires the Move command (or a grip).
    if (auto hit = hitTest(p)) {
        // Clicking inside a TABLE selects just that CELL (not the whole table),
        // enabling the row/column/alignment commands.
        if (Entity* he = entityById(*hit); he && he->meta.value("table").toBool() && !shift) {
            Vec2 tl; double cw = 0, ch = 0;
            const int idx = tableCellAt(*he, p, tl, cw, ch);
            if (idx >= 0) {
                // The "#" column of a bill of materials is a handle: clicking a line's number
                // starts the balloon that calls that line out on the drawing.
                const int cols = std::max(1, he->meta.value("cols").toInt(1));
                const bool bom = he->meta.value("bomTable").toBool();
                const int row = idx / cols, col = idx % cols;
                if (bom && col == 0 && row > 0) {
                    const QJsonArray cells = he->meta.value("cells").toArray();
                    const QString line = idx < cells.size() ? cells[idx].toString() : QString();
                    if (!line.isEmpty()) {
                        selected_ = {he->id};
                        setCellSelection(he->id, idx);
                        emit selectionChanged();
                        beginBalloonWithText(line);
                        return;
                    }
                }
                selected_ = {he->id};
                setCellSelection(he->id, idx);
                // Dragging from here picks the block of cells between this one and the one the
                // cursor ends on - that is how several are picked and then aligned together.
                cellDragFrom_ = idx;
                cellDragging_ = true;
                pressed_ = true;
                pressMods_ = mods;
                emit selectionChanged();
                return;
            }
        }
        // Clicking any balloon in a batch (a circle or its leader/arrow) selects the
        // WHOLE batch, so it moves/edits as one unit.
        if (Entity* he = entityById(*hit); he && he->meta.value("balloon").toBool() &&
                                           !he->meta.value("chain").toString().isEmpty() && !shift) {
            selected_.clear();
            selected_.push_back(he->id);
            for (Entity* m : chainMatesOf(*he)) selected_.push_back(m->id);
            emit selectionChanged();
            return;
        }
        auto it = std::find(selected_.begin(), selected_.end(), *hit);
        if (shift) {
            // Shift on a piece of a block takes the WHOLE copy out again - otherwise the expansion
            // above would put it straight back and a block could never be deselected.
            const Entity* he = entityById(*hit);
            const QString key = he ? entityBlockInstance(*he) : QString();
            if (!key.isEmpty()) {
                selected_.erase(std::remove_if(selected_.begin(), selected_.end(),
                                               [this, &key](const QString& id) {
                                                   const Entity* x = entityById(id);
                                                   return x && entityBlockInstance(*x) == key;
                                               }),
                                selected_.end());
            } else if (it != selected_.end()) {
                selected_.erase(it);
            }
        } else if (it == selected_.end()) {
            selected_.push_back(*hit);
        }
        emit selectionChanged();
        return; // selection only; no body-move-on-drag
    }
    // (e) Empty space -> region selection (rectangle / hold-for-lasso).
    pressed_ = true;
    anchor_ = p;
    pressMods_ = mods;
    dragPath_ = {p};
    regionMode_ = RegionMode::Deciding;
    pressTimer_.restart();
    dragTimer_->start();
}

bool Editor::tryHatchAt(const Vec2& p) {
    if (hatchStyle_.boundary == HatchBoundary::SelectObject) {
        if (auto hit = hitTest(p)) {
            if (Entity* src = entityById(*hit)) {
                // Only fill closed objects; otherwise let it fall through to select.
                if (src->type == EntityType::Circle ||
                    boundaryPointsOf(*src, hatchStyle_.manyJoints).size() >= 3) {
                    commitHatchFromSource(*src);
                    return true;
                }
            }
        }
        return false;
    }
    // Pick Points: fill the SMALLEST bounded region around the click — the single
    // enclosing object, or (when crossing objects subdivide it) the carved cell.
    HatchRegion region = pickHatchRegion(p);
    if (region.ent) {
        commitHatchFromSource(*region.ent);
        return true;
    }
    if (region.loop) {
        commitHatchBoundary(*region.loop);
        return true;
    }
    // Nothing enclosing: end the live-edit of the previous hatch.
    if (!lastHatchId_.isEmpty()) {
        lastHatchId_.clear();
        emit hatchTargetChanged();
    }
    return false;
}

void Editor::onPointerMove(const Vec2& raw) {
    if (!doc_) return;
    std::optional<Vec2> anchor;
    if (drafting_ && !draftPts_.empty()) anchor = draftPts_.back();
    else if (!modifyPts_.empty()) anchor = modifyPts_.back();
    else if (editing_ && editMode_ == 1) anchor = editAnchor_; // ortho/polar vs fixed end

    std::vector<QString> ignore;
    if (editing_ && editMode_ == 1) ignore.push_back(editEntityId_); // don't snap to self
    // Dimension point picks IGNORE polar/ortho tracking — the extension points must
    // land exactly where the geometry is, not be pulled onto a polar angle. Object
    // and grid snapping still apply so you can snap to real points.
    SnapSettings snap = snap_;
    if (tool_ == ToolId::Dimension) { snap.polar = false; snap.ortho = false; }
    // Wire "90° only" does NOT clamp the cursor (it moves freely); instead each wire
    // segment is snapped to the nearest axis when it is drawn (see orthoWirePoint), so
    // the crosshair tracks the mouse while the wire keeps 90° corners.
    // Holding Ctrl temporarily suspends ALL snapping (object/grid/polar/ortho) so a
    // point can be placed freely — e.g. picking a hatch location. Snap resumes when
    // Ctrl is released.
    const bool ctrlSwapsSide = drafting_ && (tool_ == ToolId::DoubleLine ||
                                             tool_ == ToolId::DoublePolyline);
    // Shift at ANY point while a double run is being drawn means "start both lines on the line this run
    // began on". The moment you notice the second line hanging in the air is not the moment you
    // clicked, so it arms then, and stays armed - pressing it is the answer to what is on screen.
    if (ctrlSwapsSide && !drawStartShift_ &&
        (QApplication::keyboardModifiers() & Qt::ShiftModifier)) {
        drawStartShift_ = true;
        emit message(QStringLiteral("Both lines will start on the line this run began on"));
    }
    if ((QApplication::keyboardModifiers() & Qt::ControlModifier) && !ctrlSwapsSide) {
        snap.objectSnap = false;
        snap.gridSnap = false;
        snap.polar = false;
        snap.ortho = false;
    }
    snapResult_ = resolveSnap(*doc_, raw, anchor, worldTolerance(), snap, ignore);
    // The sheet's own edge and the frames of the windows on it, which are not entities and so are not in
    // the search above. Taken only when nothing else was caught, or when one of them is CLOSER than what
    // was - the drawing itself still wins a tie, since that is what is usually being lined up with.
    if (const SnapResult rect = snapToRectangles(raw, worldTolerance()); rect.snapped) {
        const double toRect = std::hypot(rect.point.x - raw.x, rect.point.y - raw.y);
        const double toFound = snapResult_.snapped
                                   ? std::hypot(snapResult_.point.x - raw.x,
                                                snapResult_.point.y - raw.y)
                                   : 1e300;
        if (toRect < toFound) snapResult_ = rect;
    }
    cursor_ = snapResult_.point;

    // Dragging across a table's cells picks the block between the cell the drag started on and the
    // one under the cursor now.
    if (cellDragging_ && cellDragFrom_ >= 0) {
        if (Entity* t = entityById(cellTableId_); t && t->meta.value("table").toBool()) {
            Vec2 tl;
            double cw = 0, ch = 0;
            const int to = tableCellAt(*t, cursor_, tl, cw, ch);
            const int cols = std::max(1, t->meta.value("cols").toInt(1));
            if (to >= 0) {
                const int r0 = std::min(cellDragFrom_ / cols, to / cols);
                const int r1 = std::max(cellDragFrom_ / cols, to / cols);
                const int c0 = std::min(cellDragFrom_ % cols, to % cols);
                const int c1 = std::max(cellDragFrom_ % cols, to % cols);
                std::vector<int> block;
                for (int r = r0; r <= r1; ++r)
                    for (int c = c0; c <= c1; ++c) block.push_back(r * cols + c);
                if (block != cellSel_) {
                    cellSel_ = block;
                    cellIndex_ = cellDragFrom_;
                    emit cellSelectionChanged();
                    emit changed();
                }
            }
        }
        return;
    }

    // A grabbed marking follows the cursor along its own wire. With Shift held at the grab, the
    // whole pick follows: each one lands on its own wire nearest the cursor, so they line up.
    if (markGrab_ && markGrabAt_ >= 0) {
        dragPickedMarks(cursor_, markGrabAll_, markGrabAt_);
        markMoved_ = true;
        return;
    }

    // Placing a connector: the grab handle (a chosen pin/corner, or the top-left by
    // default) tracks the snapped cursor; the ghost is then snapped by ANY of its own
    // corners/pin ends onto existing geometry so features line up.
    if (tool_ == ToolId::Connector && connectorArmed_ && !connectorFrozen_) {
        Vec2 ghostAnchor = cursor_ - connectorGrab_; // hold the connector by the grab handle
        if (snap.objectSnap) {
            Vec2 catchAt;
            if (auto a = connectorFeatureSnap(ghostAnchor, &catchAt)) {
                ghostAnchor = *a;
                snapResult_.point = catchAt;         // marker on the actual connection point
                snapResult_.snapped = true;
                snapResult_.kind = SnapKind::Endpoint; // draw the amber snap square there
            }
        }
        cursor_ = ghostAnchor; // cursor_ now carries the ghost anchor (preview + placement)
    }
    // While frozen: show the osnap square on the pin/corner the cursor is over, so it's
    // clear which point a click will catch (the grab uses the same hit-test).
    if (tool_ == ToolId::Connector && connectorArmed_ && connectorFrozen_) {
        if (auto f = frozenGrabFeature(raw)) {
            snapResult_.point = *f;
            snapResult_.snapped = true;
            snapResult_.kind = SnapKind::Endpoint;
        }
    }

    // Apply an in-progress direct edit: a body move (button held) or a grabbed
    // joint that follows the cursor (no button) until clicked to place. Panels
    // refresh on commit, so emit the repaint-only overlay signal here.
    if (editing_ && (pressed_ || gripGrab_)) {
        applyEditDrag(cursor_);
        emit overlayChanged();
        return;
    }

    // While the lasso is engaged (button held past the 0.2s threshold), record
    // the freeform path. The pending rectangle just tracks the live cursor.
    if (selectionTool() && regionMode_ == RegionMode::Lasso && pressed_) {
        const double tol = worldTolerance();
        if (dragPath_.empty() || distance(dragPath_.back(), raw) > tol * 0.4)
            dragPath_.push_back(raw);
    }

    // Rubber-band preview (line/rect/etc. while drawing), modify reference
    // points, and region selection follow the cursor: repaint only, no panel
    // rebuild, so the preview tracks the cursor without lag.
    if (drafting_ || !modifyPts_.empty() || regionMode_ != RegionMode::None || pastePlacing_ ||
        trimFenceActive_ || (tool_ == ToolId::Connector && connectorArmed_) ||
        arrayAwaitingCenter() || // a polar array's centre tracks the cursor until clicked
        harnessPlacing_ ||       // the generated harness ghosts under the cursor
        ((!twistArmType_.isEmpty() || screenArm_) && wireBandHasStart_))
        emit overlayChanged(); // armed connector ghost / twist-screen band preview tracks cursor
}

void Editor::onDragTick() {
    // Fires only while the button is held during the Deciding phase. If the
    // hold passes the threshold, promote the rectangle into a lasso.
    if (!pressed_ || !selectionTool() || regionMode_ != RegionMode::Deciding) {
        dragTimer_->stop();
        return;
    }
    if (pressTimer_.elapsed() >= kLassoDelayMs) {
        regionMode_ = RegionMode::Lasso;
        lassoWindowMode_ = cursor_.x >= anchor_.x;
        dragPath_ = {anchor_, cursor_};
        emit message(lassoWindowMode_
                         ? QStringLiteral("Window lasso: objects must be fully inside")
                         : QStringLiteral("Crossing lasso: touching objects are selected"));
        dragTimer_->stop(); // cursor moves drive the repaint from here
    }
    emit overlayChanged(); // animate the countdown while still deciding (repaint only)
}

void Editor::setSnapRectangles(std::vector<Bounds> rects) { snapRects_ = std::move(rects); }

SnapResult Editor::snapToRectangles(const Vec2& raw, double tol) const {
    SnapResult best;
    if (!snap_.objectSnap || snapRects_.empty()) return best;
    // A CORNER beats the middle of an edge, and either beats a point somewhere along one - by KIND, not by
    // which happens to be a millimetre nearer. Ranked by distance alone, a click 1.5 mm along the bottom
    // edge and 1.3 mm up from it caught the edge rather than the corner it was plainly aiming at, which is
    // not how a snap is meant to behave: the definite point wins.
    for (int pass = 0; pass < 3; ++pass) {
        const SnapKind kind = pass == 0 ? SnapKind::Endpoint
                              : pass == 1 ? SnapKind::Midpoint
                                          : SnapKind::Nearest;
        if (pass == 0 && !snap_.snapEndpoint) continue;
        if (pass == 1 && !snap_.snapMidpoint) continue;
        if (pass == 2 && !(snap_.snapNearest || snap_.snapPerpendicular)) continue;
        double bestDist = tol;
        for (const Bounds& b : snapRects_) {
            if (!b.valid) continue;
            const Vec2 corners[4] = {{b.min.x, b.min.y}, {b.max.x, b.min.y},
                                     {b.max.x, b.max.y}, {b.min.x, b.max.y}};
            auto consider = [&](const Vec2& candidate) {
                const double d = std::hypot(candidate.x - raw.x, candidate.y - raw.y);
                if (d > bestDist) return;
                bestDist = d;
                best.snapped = true;
                best.point = candidate;
                best.kind = kind;
            };
            if (pass == 0) {
                for (const Vec2& c : corners) consider(c);
                continue;
            }
            for (int i = 0; i < 4; ++i) {
                const Vec2 a = corners[i], c = corners[(i + 1) % 4];
                if (pass == 1) {
                    consider(Vec2{(a.x + c.x) * 0.5, (a.y + c.y) * 0.5});
                    continue;
                }
                const double dx = c.x - a.x, dy = c.y - a.y;
                const double len2 = dx * dx + dy * dy;
                if (len2 < 1e-12) continue;
                double u = ((raw.x - a.x) * dx + (raw.y - a.y) * dy) / len2;
                u = std::clamp(u, 0.0, 1.0);
                consider(Vec2{a.x + dx * u, a.y + dy * u});
            }
        }
        if (best.snapped) break;   // this kind caught: no need of a looser one
    }
    return best;
}

void Editor::beginRectanglePick(std::function<void(Vec2, Vec2)> onDone,
                               std::function<void()> onCancel) {
    rectPick_ = true;
    rectPickHaveFirst_ = false;
    rectPickDone_ = std::move(onDone);
    rectPickCancel_ = std::move(onCancel);
    // Nothing else is going on while a rectangle is being picked: a half-drawn line or a live selection
    // would take the clicks that are meant for the corners.
    selected_.clear();
    draftPts_.clear();
    modifyPts_.clear();
    drafting_ = false;
    editing_ = false;
    regionMode_ = RegionMode::None;
    emit message(QStringLiteral("Pick the first corner"));
    emit selectionChanged();
    emit overlayChanged();
}

void Editor::endRectanglePick() {
    rectPick_ = false;
    rectPickHaveFirst_ = false;
    rectPickDone_ = nullptr;
    rectPickCancel_ = nullptr;
    emit overlayChanged();
}

void Editor::onPointerPress(const Vec2& raw, Qt::MouseButton button,
                            Qt::KeyboardModifiers mods) {
    if (!doc_ || button != Qt::LeftButton) return;
    onPointerMove(raw);
    const Vec2 p = cursor_;
    // A rectangle being picked takes the clicks before any tool sees them - and it takes the SNAPPED
    // point, because that is what `p` is: whatever the snaps caught, override menu included.
    if (rectPick_) {
        if (!rectPickHaveFirst_) {
            rectPickFirst_ = p;
            rectPickHaveFirst_ = true;
            emit message(QStringLiteral("Pick the opposite corner"));
            emit overlayChanged();
        } else {
            auto done = rectPickDone_;
            const Vec2 first = rectPickFirst_;
            endRectanglePick();
            if (done) done(first, p);
        }
        return;
    }
    pressMods_ = mods; // available to every press path (draw/modify), not just selection
    if (tool_ == ToolId::ModelNearPoint || tool_ == ToolId::ModelFarPoint) {
        emit modelAnchorPicked(p, tool_ == ToolId::ModelNearPoint);
        setTool(ToolId::Select);
        return;
    }
    if (tool_ == ToolId::ModelConnectPoint) {
        // The point first, then a second click anywhere along the direction the part runs from it. Both
        // clicks are SNAPPED points, so the polar tracking gives 0, 30, 45, 90 exactly and an object snap
        // takes the direction off the drawing itself.
        if (!haveConnectFirst_) {
            connectFirst_ = p;
            haveConnectFirst_ = true;
            emit message(QStringLiteral(
                "Now click along the direction the part runs from there — polar tracking gives the angle"));
            return;
        }
        haveConnectFirst_ = false;
        const double dx = p.x - connectFirst_.x, dy = p.y - connectFirst_.y;
        // Two clicks in the same place say nothing about a direction: along +x, which is how a model that
        // says nothing is drawn anyway.
        const double angle = (std::abs(dx) < 1e-9 && std::abs(dy) < 1e-9) ? 0.0 : std::atan2(dy, dx);
        emit modelConnectPicked(connectFirst_, angle);
        setTool(ToolId::Select);
        return;
    }

    // Clipboard base-point pick (after Ctrl+Shift+C / Ctrl+X): this click stores
    // the reference point the clipboard is relative to (then cuts if requested).
    if (awaitClipBase_) {
        clipboardBase_ = p;
        awaitClipBase_ = false;
        // For a cut, the originals STAY in place (visible) until the paste lands —
        // they are not deleted here; we just remember them and go straight into
        // ghost placement so the source remains until the contents are dropped.
        if (clipCut_) cutPending_ = selected_;
        clipCut_ = false;
        pastePlacing_ = true;
        emit message(QStringLiteral("Specify insertion point"));
        emit overlayChanged();
        return;
    }
    // Paste placement: drop the clipboard so its base point lands at this click.
    if (pastePlacing_) {
        pushUndo();
        const Vec2 off = p - clipboardBase_;
        // A cut only now removes the originals (kept visible until placed).
        if (!cutPending_.empty()) {
            auto& ents = doc_->entities();
            ents.erase(std::remove_if(ents.begin(), ents.end(), [&](const Entity& e) {
                           return std::find(cutPending_.begin(), cutPending_.end(), e.id) !=
                                  cutPending_.end();
                       }),
                       ents.end());
            cutPending_.clear();
        }
        selected_.clear();
        // A pasted set is a SET: the copies name each other, not the originals, and a copy of a placed
        // library part is another one of that part. Both of those are what remapEntityReferences says, and
        // this path - the one every paste and every Insert goes through - was not calling it. So a pasted
        // connector kept the instance key of the one it was copied from, and anything that counts instances
        // counted the two as one: the bill of materials showed one connector where the sheet had two.
        std::vector<Entity> copies;
        copies.reserve(clipboard_.size());
        QHash<QString, QString> oldToNew;
        for (Entity e : clipboard_) {
            const QString was = e.id;
            e.id = uid();
            oldToNew.insert(was, e.id);
            if (pasteMirrorX_) mirrorEntity(e, clipboardBase_, {clipboardBase_.x, clipboardBase_.y + 1});
            if (pasteMirrorY_) mirrorEntity(e, clipboardBase_, {clipboardBase_.x + 1, clipboardBase_.y});
            // Turned the way it was turned under the cursor, about the point it was held by.
            if (std::abs(placeRot_) > 1e-9) rotateEntity(e, clipboardBase_, placeRot_);
            translateEntity(e, off);
            copies.push_back(std::move(e));
        }
        remapEntityReferences(copies, oldToNew);
        for (Entity& e : copies) {
            selected_.push_back(e.id);
            doc_->entities().push_back(std::move(e));
        }
        // A component dropped on a net goes INTO it: the wire is cut between the symbol's two leads, so the
        // current has to go through the part. A line drawn straight through a resistor is a resistor with a
        // wire shorting it out, which is not what anybody means by putting one there.
        if (const int cut = cutNetsUnderSymbols(selected_))
            emit message(QStringLiteral("Placed - the net was cut between its leads (%1 wire%2)")
                             .arg(cut)
                             .arg(cut == 1 ? QString() : QStringLiteral("s")));
        pastePlacing_ = false;
        pasteMirrorX_ = pasteMirrorY_ = false;
        placeRot_ = 0.0;
        doc_->setDirty(true);
        emit message(QStringLiteral("Pasted %1 object(s)").arg(clipboard_.size()));
        emit selectionChanged();
        emit changed();
        return;
    }

    // Twist align: this click is the destination point (on a wire) to align to.
    if (awaitTwistAlign_) {
        twistAlignTo(raw);
        return;
    }

    // Align routing: this click picks the reference connector, then aligns the rest.
    if (awaitAlignRef_) {
        if (auto hit = hitTest(p)) alignRoutingFrom(*hit);
        else { awaitAlignRef_ = false; emit message(QStringLiteral("Align routing cancelled")); }
        return;
    }
    // Cross switch: each click toggles the hop at the nearest wire crossing (stays armed).
    if (awaitCrossSwitch_) {
        toggleWireCrossAt(raw);
        return;
    }
    // Match properties: first click picks the source, each later click applies to the
    // object under the cursor (stays armed until Esc).
    if (awaitMatchProps_) {
        matchPropsClick(p);
        return;
    }
    // Remove twist: click a twist sign to delete it (stays armed until Esc). A twist sign
    // sits on top of the wires, so pick the TWIST whose glyph bounds cover the click rather
    // than hit-testing (which would return the wire underneath).
    if (awaitTwistRemove_) {
        const double tol = worldTolerance();
        QString target;
        for (const auto& e : doc_->entities()) {
            if (e.type != EntityType::Twist) continue;
            const Bounds b = e.bounds();
            if (b.valid && p.x >= b.min.x - tol && p.x <= b.max.x + tol &&
                p.y >= b.min.y - tol && p.y <= b.max.y + tol) {
                target = e.id;
                break;
            }
        }
        if (!target.isEmpty()) {
            pushUndo();
            auto& ents = doc_->entities();
            ents.erase(std::remove_if(ents.begin(), ents.end(),
                                      [&](const Entity& x) { return x.id == target; }),
                       ents.end());
            doc_->setDirty(true);
            emit selectionChanged();
            emit changed();
            emit message(QStringLiteral("Twist removed — click another, or Esc to finish"));
        }
        return;
    }
    // Twist / Screen: two clicks define a crossing line across the wires to select them.
    // For Screen, once frozen (Tab), ANY click commits at the frozen end point — the
    // click no longer redefines the line, so ribbon fields can be tuned beforehand.
    // The break mark is marked out the same way: a line drawn across the runs to be broken. The first
    // click takes one end, the cursor draws the line and the mark it would make, and the second click
    // places it. Tab pins the line so the ribbon's numbers can be tried before committing.
    if (tool_ == ToolId::LengthBreak) {
        if (!wireBandHasStart_) {
            wireBandStart_ = raw;
            wireBandHasStart_ = true;
            emit message(QStringLiteral(
                "Now click the other end of the line across the runs you want to break"));
            emit overlayChanged();
            return;
        }
        Entity mark;
        const Vec2 end = breakFrozen_ ? breakFrozenAt_ : raw;
        if (!breakFromBand(wireBandStart_, end, mark)) {
            emit message(QStringLiteral("Draw the line ACROSS the runs you want to break"));
            return;
        }
        pushUndo();
        mark.layer = doc_->activeLayer();
        addEntity(std::move(mark));
        wireBandHasStart_ = false;
        breakFrozen_ = false;
        doc_->setDirty(true);
        emit changed();
        emit message(QStringLiteral("Break placed"));
        setTool(ToolId::Select);
        return;
    }
    if (!twistArmType_.isEmpty() || screenArm_) {
        // A FROZEN twist is checked before the "no start yet" case: a click must not
        // commit it. Only Enter places a frozen twist; a click discards the frozen
        // preview and goes back to placing, taking the click as the new first end.
        if (!twistArmType_.isEmpty() && twistFrozen_) {
            twistFrozen_ = false;
            wireBandStart_ = raw;
            wireBandHasStart_ = true;
            emit message(QStringLiteral(
                "Twist preview released — click the other end of the crossing line"));
            emit twistArmChanged();
            emit overlayChanged();
        } else if (!wireBandHasStart_) {
            wireBandStart_ = raw;
            wireBandHasStart_ = true;
            emit message(QStringLiteral("Click the other end of the crossing line"));
            emit overlayChanged();
        } else if (screenArm_ && screenFrozen_) {
            wireBandHasStart_ = false;
            finishWireBand(screenFrozenEnd_);
        } else {
            wireBandHasStart_ = false;
            finishWireBand(raw);
        }
        return;
    }

    // Placing a generated harness: this click drops it where the ghost sits.
    if (harnessPlacing_) {
        placeHarnessAt(p);
        return;
    }

    // A live array owns the canvas: the click supplies the polar centre or picks the
    // path object, and never falls through to selection (which would drop the very
    // objects being arrayed).
    if (arrayActive_) {
        handleArrayPress(p);
        return;
    }

    if (tool_ == ToolId::Select) { handleSelectPress(p, raw, mods); return; }
    // In Hatch mode a click first tries to fill a region/object; if there's
    // nothing to fill, fall through to normal selection (click or select-zone) so
    // other objects can still be picked while hatching. (Wipeout is drawn as an
    // explicit polygon — handled by handleDrawPress below, AutoCAD-style.)
    if (tool_ == ToolId::Hatch) {
        // A pending selection (rectangle 2nd corner) or a grabbed joint must finish
        // first — don't hatch instead of completing it.
        if (regionMode_ == RegionMode::RectPending || (editing_ && gripGrab_)) {
            handleSelectPress(p, raw, mods);
            return;
        }
        if (tryHatchAt(p)) return;
        handleSelectPress(p, raw, mods);
        return;
    }
    if (isDrawTool(tool_)) { handleDrawPress(p); return; }
    // TRIM / EXTEND (AutoCAD-style, persistent reference): first pick a REFERENCE
    // object — it stays selected/highlighted for the whole command. Then draw a LINE
    // (two clicks) across objects; every object the line crosses is trimmed to / (for
    // Extend) extended to the reference. The reference persists until Esc finishes.
    if (tool_ == ToolId::Trim || tool_ == ToolId::Extend) {
        const bool ext = (tool_ == ToolId::Extend);
        if (trimRefIds_.empty()) {
            if (auto hit = hitTest(p)) {
                trimRefIds_ = {*hit};
                selected_ = {*hit}; // stays highlighted until Esc
                emit selectionChanged();
                emit message(ext ? QStringLiteral("Reference set — draw a line across objects to "
                                                  "extend them to it; Esc to finish")
                                 : QStringLiteral("Reference set — draw cut lines across objects to "
                                                  "trim; Esc to finish"));
                emit changed();
            } else {
                emit message(ext ? QStringLiteral("Select the boundary object to extend to")
                                 : QStringLiteral("Select the reference object to trim from"));
            }
            return;
        }
        if (!trimFenceActive_) {
            trimFenceActive_ = true;
            trimFenceStart_ = p;
            emit message(QStringLiteral("Click the end point of the line"));
        } else {
            trimFenceActive_ = false;
            if (ext) applyExtendFence(trimFenceStart_, p);
            else applyTrimFence(trimFenceStart_, p);
        }
        emit overlayChanged();
        return;
    }
    if (isPickModifyTool(tool_)) { handlePickModifyPress(p); return; }
    if (tool_ == ToolId::Offset) { handleOffsetPress(p); return; }
    // A frozen connector preview (Tab pressed to edit): the next click re-attaches the
    // ghost to the cursor. If that click lands ON one of the frozen connector's pins or
    // corners, GRAB it there (that feature becomes the point held under the cursor);
    // clicking empty canvas keeps the current handle and jumps the ghost to the cursor.
    if (tool_ == ToolId::Connector && connectorArmed_ && connectorFrozen_) {
        // Hit-test against the RAW click (not the grid/object-snapped cursor_) so a pin/
        // corner is caught even with grid snap on.
        const auto grab = frozenGrabFeature(raw);
        const bool grabbed = grab.has_value();
        if (grabbed) connectorGrab_ = *grab - connectorFrozenPos_;
        connectorFrozen_ = false;
        emit message(grabbed
                         ? QStringLiteral("Connector grabbed by that point — click to place")
                         : QStringLiteral("Preview follows the cursor again — click to place"));
        emit overlayChanged();
        return;
    }
    // In Connector mode the FIRST click starts the preview (arms it) rather than
    // placing — the ghost then follows the cursor and the next click places it.
    if (tool_ == ToolId::Connector && !connectorArmed_) {
        connectorArmed_ = true;
        connectorGrab_ = {0.0, 0.0}; // fresh preview starts held by the top-left corner
        emit message(QStringLiteral("Connector preview started — click to place, "
                                    "Tab to edit, X/Y to flip"));
        emit connectorArmedChanged();
        emit overlayChanged();
        return;
    }
    if (isPlacementTool(tool_)) { handlePlacementPress(p); return; }
    handleModifyPress(p);
}

void Editor::onPointerRelease(const Vec2& raw, Qt::MouseButton button) {
    Q_UNUSED(raw);
    if (!doc_ || button != Qt::LeftButton) return;
    // Trim's cut line is two clicks (handled in onPointerPress), so the release does
    // nothing for it.
    if (!selectionTool()) return;
    // A drag across cells ends where the button was let go: the block stays picked.
    if (cellDragging_) {
        cellDragging_ = false;
        cellDragFrom_ = -1;
        pressed_ = false;
        if (cellSel_.size() > 1)
            emit message(QStringLiteral("%1 cell(s) picked").arg(cellSel_.size()));
        return;
    }

    // A marking dragged with the button down is placed where it was let go; one merely clicked
    // stays on the cursor and is placed by the next click, like a grabbed joint.
    if (markGrab_) {
        if (markMoved_) {
            markGrab_ = false;
            markGrabAt_ = -1;
            markMoved_ = false;
            pressed_ = false;
            emit message(QStringLiteral("Marking placed"));
            emit changed();
        }
        return;
    }
    // A grabbed joint stays attached to the cursor after release; it is placed
    // by the next click, not by releasing the button.
    if (gripGrab_) return;
    if (!pressed_) return;
    pressed_ = false;
    dragTimer_->stop();
    const bool shift = pressMods_ & Qt::ShiftModifier;

    // Finish a body move drag (a click without movement just selected).
    if (editing_) {
        editing_ = false;
        editGhost_.clear(); // committed at the new position; drop the source ghost
        if (editMoved_) { doc_->setDirty(true); emit message(QStringLiteral("Moved")); }
        emit changed();
        return;
    }

    if (regionMode_ == RegionMode::Lasso) {
        if (dragPath_.size() > 2) finishRegionSelect(shift);
        else emit message(QStringLiteral("Selection unchanged"));
        regionMode_ = RegionMode::None;
        dragPath_.clear();
    } else if (regionMode_ == RegionMode::Deciding) {
        // Released before the lasso threshold: this was the first click of a
        // two-click rectangle. Keep the anchor and await the opposite corner.
        regionMode_ = RegionMode::RectPending;
        emit message(QStringLiteral("Specify opposite corner (click), or Esc to cancel"));
    }
    emit changed();
}

namespace {

bool pointInPolygon(const std::vector<Vec2>& poly, const Vec2& p) {
    bool in = false;
    const size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        if (((poly[i].y > p.y) != (poly[j].y > p.y)) &&
            (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) /
                           (poly[j].y - poly[i].y + 1e-12) +
                       poly[i].x))
            in = !in;
    }
    return in;
}

double polygonArea(const std::vector<Vec2>& poly) {
    double a2 = 0.0;
    for (size_t i = 0, n = poly.size(); i < n; ++i) {
        const Vec2& a = poly[i];
        const Vec2& b = poly[(i + 1) % n];
        a2 += a.x * b.y - b.x * a.y;
    }
    return std::abs(a2) * 0.5;
}

// The rectangle a text actually INKS, measured with the same font the renderer draws it with.
//
// Entity::bounds() returns the text's line box - ascent plus descent, per line - which is what layout
// needs and what a container is sized to. It is not what a person points at: the box is around twice
// the height of a digit and most of the difference hangs below the baseline, empty. A window selection
// drawn snugly round a big "3" therefore enclosed everything that was drawn and still missed the
// corners of the box, so the text refused to be selected until a much larger area was swept.
Bounds textInkBounds(const Entity& e) {
    const QString txt = e.meta.value(QStringLiteral("text")).toString();
    const double size = e.meta.value(QStringLiteral("size"))
                            .toDouble(e.meta.value(QStringLiteral("height")).toDouble(2.5));
    const Bounds boxed = e.bounds();
    if (txt.isEmpty() || size <= 1e-9) return boxed;
    // Measuring glyphs needs a font engine, which needs a GUI application. Without one - the unit
    // tests run with no application object at all - the line box is the best that can be said.
    if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance())) return boxed;
    // Measured big and scaled down: font metrics at a two-unit pixel size are too coarse to trust,
    // and they scale linearly, so one probe size serves every text on the sheet.
    constexpr double kProbe = 96.0;
    QFont f;
    const QString family = e.meta.value(QStringLiteral("font")).toString();
    if (!family.isEmpty()) f.setFamily(family);
    f.setBold(e.meta.value(QStringLiteral("bold")).toBool());
    f.setItalic(e.meta.value(QStringLiteral("italic")).toBool());
    f.setPixelSize(static_cast<int>(kProbe));
    const QFontMetricsF fm(f);
    const double k = size / kProbe;                    // probe units -> world units
    const double spacing = std::max(1.0, e.meta.value(QStringLiteral("lineSpacing")).toDouble(1.0));
    const double lineH = fm.height() * spacing * k;    // the same step the renderer walks
    const double ascent = fm.ascent() * k;
    const double bw = e.meta.value(QStringLiteral("boxWidth")).toDouble(0.0);
    const double bh = e.meta.value(QStringLiteral("boxHeight")).toDouble(0.0);

    // The lines as the renderer lays them out: paragraphs, wrapped to the box when there is one.
    QStringList lines;
    for (const QString& para : txt.split(QLatin1Char('\n'))) {
        if (bw <= 1e-9) { lines << para; continue; }
        QString cur;
        for (const QString& word : para.split(QLatin1Char(' '))) {
            const QString test = cur.isEmpty() ? word : cur + QLatin1Char(' ') + word;
            if (!cur.isEmpty() && fm.horizontalAdvance(test) * k > bw) {
                lines << cur;
                cur = word;
            } else {
                cur = test;
            }
        }
        lines << cur;
    }
    if (lines.isEmpty()) return boxed;

    QString just = e.meta.value(QStringLiteral("justify")).toString();
    if (just.size() < 2) {
        const QString al = e.meta.value(QStringLiteral("align")).toString();
        just = QStringLiteral("T") + (al == QLatin1String("center") ? QStringLiteral("C")
                                      : al == QLatin1String("right") ? QStringLiteral("R")
                                                                     : QStringLiteral("L"));
    }
    const bool anchorJust = e.meta.value(QStringLiteral("anchorJustify")).toBool();
    const double totalH = lineH * lines.size();
    double top = e.a.y;                                // world y grows up; the anchor is the box top
    {
        // No box: the anchor is the middle or the foot of the block, as the renderer places it.
        const double avail = (bh > 1e-9 && !anchorJust) ? bh : 0.0;
        if (just[0] == QLatin1Char('M')) top -= (avail - totalH) / 2.0;
        else if (just[0] == QLatin1Char('B')) top -= avail - totalH;
    }
    Bounds ink;
    bool any = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString& ln = lines.at(i);
        if (ln.isEmpty()) continue;
        const double baseline = top - ascent - lineH * i;
        double x = e.a.x;
        {
            // No box: the anchor is the middle or the right-hand end of the line, as it is drawn.
            const double avail = (bw > 1e-9 && !anchorJust) ? bw : 0.0;
            const double lw = fm.horizontalAdvance(ln) * k;
            if (just[1] == QLatin1Char('C')) x += (avail - lw) / 2.0;
            else if (just[1] == QLatin1Char('R')) x += avail - lw;
        }
        // tightBoundingRect is in the font's own coordinates about the baseline origin, with y
        // growing DOWN, so its top is negative - above the baseline once the sign is flipped.
        const QRectF r = fm.tightBoundingRect(ln);
        ink.expand({x + r.left() * k, baseline - r.top() * k});
        ink.expand({x + r.right() * k, baseline - r.bottom() * k});
        any = true;
    }
    return any ? ink : boxed;
}

// Representative points used to test whether an entity is inside a region.
std::vector<Vec2> entitySelectPoints(const Entity& e) {
    std::vector<Vec2> pts;
    switch (e.type) {
    case EntityType::Line:
        pts = {e.a, lerp(e.a, e.b, 0.5), e.b};
        break;
    case EntityType::TwistedPair:
        for (const auto& strand : twistedPairStrands(e))
            for (const Vec2& v : strand) pts.push_back(v);
        if (pts.empty()) pts = {e.a, e.b};
        break;
    case EntityType::Dimension:
        // The endpoints of the drawn geometry, so a window/crossing reacts to the
        // dimension/extension lines (not just the measured points).
        for (const auto& s : dimensionSegments(e)) { pts.push_back(s.first); pts.push_back(s.second); }
        if (pts.empty()) pts = {e.a, e.b};
        break;
    case EntityType::DoubleLine: {
        // Region selection tests the two VISIBLE parallel lines' ends only — NOT
        // the centerline or its middle joint. So a crossing box that merely passes
        // through the gap between the lines (without touching either) won't select.
        const double gap = e.meta.value("gap").toDouble(10.0);
        const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
        pts = {e.a + off, e.b + off, e.a - off, e.b - off};
        break;
    }
    case EntityType::Hatch:
        if (e.meta.value("shape").toString() == QLatin1String("circle")) {
            pts = {{e.center.x + e.radius, e.center.y}, {e.center.x - e.radius, e.center.y},
                   {e.center.x, e.center.y + e.radius}, {e.center.x, e.center.y - e.radius}};
            break;
        }
        pts = e.points;
        break;
    case EntityType::Polyline:
    case EntityType::Spline:
    case EntityType::Wire:
    case EntityType::Rect:
    case EntityType::Polygon:
    case EntityType::Wipeout:
    case EntityType::Image:
        pts = e.points;
        break;
    case EntityType::Circle:
    case EntityType::Arc:
        pts = {e.center, {e.center.x + e.radius, e.center.y},
               {e.center.x - e.radius, e.center.y}, {e.center.x, e.center.y + e.radius},
               {e.center.x, e.center.y - e.radius}};
        break;
    case EntityType::Text: {
        // The corners of the INK - the glyphs as drawn - so a window drawn round what you can see
        // selects it. Entity::bounds() gives the text's LINE BOX, which is the right answer for
        // layout and the wrong one here: a line box is about twice the height of a digit, and most of
        // the extra hangs below the baseline where nothing is drawn, so a window drawn snugly round a
        // big "3" enclosed the glyph and missed the box.
        const Bounds b = textInkBounds(e);
        pts = {b.min, {b.max.x, b.min.y}, b.max, {b.min.x, b.max.y}};
        break;
    }
    case EntityType::LengthBreak: {
        const Bounds b = e.bounds();
        pts = {b.min, {b.max.x, b.min.y}, b.max, {b.min.x, b.max.y}, e.a};
        break;
    }
    case EntityType::Connector: {
        // The body-rectangle corners + each pin end/tip, so a window encloses the
        // connector and a crossing that touches its body or pins selects it.
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
        const ConnectorGeometry g = connectorGeometry(e.a, spec);
        pts = {g.rectMin, {g.rectMax.x, g.rectMin.y}, g.rectMax, {g.rectMin.x, g.rectMax.y}};
        for (const auto& pe : g.pinEnd) pts.push_back(pe);
        for (const auto& pt : g.pinTip) pts.push_back(pt);
        break;
    }
    case EntityType::Twist: {
        // The rendered glyph's bounding box (wider than the raw stored points — see
        // Entity::bounds()), all 4 corners, so a crossing box that merely touches the
        // drawn braid selects it.
        const Bounds b = e.bounds();
        if (b.valid) pts = {b.min, {b.max.x, b.min.y}, b.max, {b.min.x, b.max.y}};
        else pts = {e.a};
        break;
    }
    case EntityType::Screen: {
        // Cardinal points on the ACTUAL ellipse + the drain point (not the loose
        // bounding box) — a region must touch the real drawn shape, not just the
        // rectangle around it.
        const int k = std::min(e.meta.value("catch").toInt(static_cast<int>(e.points.size())),
                               static_cast<int>(e.points.size()));
        if (k < 1) { pts = {e.a}; break; }
        double cx = 0.0, yMin = 1e18, yMax = -1e18;
        for (int i = 0; i < k; ++i) {
            cx += e.points[i].x;
            yMin = std::min(yMin, e.points[i].y);
            yMax = std::max(yMax, e.points[i].y);
        }
        cx /= k;
        const double cyMid = (yMin + yMax) / 2.0;
        const double rx = e.meta.value("rx").toDouble(12.0);
        const double ry = e.meta.contains("ry") ? e.meta.value("ry").toDouble()
                                                : (yMax - yMin) / 2.0 + 6.0;
        pts = {{cx - rx, cyMid}, {cx + rx, cyMid}, {cx, cyMid - ry}, {cx, cyMid + ry}};
        if (static_cast<int>(e.points.size()) > k) pts.push_back(e.points.back()); // drain
        break;
    }
    default: {
        const Bounds b = e.bounds();
        if (b.valid) pts = {b.min, b.max, b.center()};
        else pts = {e.a};
        break;
    }
    }
    return pts;
}

// The two physical lines of a double run, built the way they are DRAWN: a run with rounded knees rounds
// its centreline at radius + half the gap and offsets that, so the lines curve through the bend with a
// constant gap. Testing against the mitred offset instead would answer for geometry not on the screen.
std::vector<Vec2> doubleRunRail(const Entity& ent, double s) {
    const bool dbl = ent.type == EntityType::DoubleLine;
    const double gap = ent.meta.value("gap").toDouble(10.0);
    std::vector<Vec2> centre = dbl ? std::vector<Vec2>{ent.a, ent.b} : ent.points;
    if (centre.size() < 2) return {};
    if (dbl) {
        const Vec2 off = (ent.b - ent.a).normalized().perp() * s;
        std::vector<Vec2> rail = {ent.a + off, ent.b + off};
        applyDoubleRunCuts(rail, ent, s > 0 ? +1 : -1);
        return rail;
    }
    if (ent.meta.contains(QStringLiteral("cornerRadii"))) {
        const QJsonArray arr = ent.meta.value(QStringLiteral("cornerRadii")).toArray();
        std::vector<double> radii(centre.size(), 0.0);
        bool any = false;
        for (int i = 0; i < arr.size() && i < static_cast<int>(radii.size()); ++i) {
            const double r = arr[i].toDouble();
            if (r > 0.0) { radii[static_cast<size_t>(i)] = r + gap / 2.0; any = true; }
        }
        if (any) {
            const std::vector<Vec2> rounded = roundedPolylinePerCorner(centre, radii);
            if (rounded.size() >= 2) centre = rounded;
        }
    }
    std::vector<Vec2> rail = offsetPolyline(centre, s);
    // ... and the caps, which is where an EXTENDED line's extra length lives. Without them the line this
    // answers about stops where the run's own points stop, so a line stretched to meet something could
    // not be trimmed at that something: the crossing was not on the line as far as the trim could see.
    applyDoubleRunCuts(rail, ent, s > 0 ? +1 : -1);
    return rail;
}

std::vector<std::pair<Vec2, Vec2>> entitySelectSegments(const Entity& e) {
    std::vector<std::pair<Vec2, Vec2>> segs;
    auto pts = entitySelectPoints(e);
    switch (e.type) {
    case EntityType::Line:
        segs.emplace_back(e.a, e.b);
        break;
    case EntityType::TwistedPair:
        for (const auto& strand : twistedPairStrands(e))
            for (size_t i = 1; i < strand.size(); ++i)
                segs.emplace_back(strand[i - 1], strand[i]);
        break;
    case EntityType::Dimension:
        // The real drawn lines, so a green crossing zone touching ANY of them selects.
        for (const auto& s : dimensionSegments(e)) segs.emplace_back(s.first, s.second);
        if (segs.empty()) segs.emplace_back(e.a, e.b);
        break;
    case EntityType::DoubleLine: {
        // Crossing-region edges are the two VISIBLE lines (not the centerline),
        // so only touching a real line — not the gap between them — selects it.
        const double gap = e.meta.value("gap").toDouble(10.0);
        const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
        segs.emplace_back(e.a + off, e.b + off);
        segs.emplace_back(e.a - off, e.b - off);
        break;
    }
    case EntityType::Polyline:
    case EntityType::Wire:
        // A DOUBLE polyline is picked on its two VISIBLE lines, like a double line - not on the
        // centreline, which is not drawn. A crossing region over one of its lines used to miss it
        // entirely: the region was tested against a line the drawing does not have.
        if (e.type == EntityType::Polyline && e.meta.value("double").toBool() &&
            e.points.size() >= 2) {
            const double gap = e.meta.value("gap").toDouble(10.0);
            bool any = false;
            for (double s : {gap / 2.0, -gap / 2.0}) {
                const std::vector<Vec2> rail = doubleRunRail(e, s);
                for (const auto& piece : splitRailAtHoles(rail, e, s > 0 ? +1 : -1))
                    for (size_t i = 1; i < piece.size(); ++i) {
                        segs.emplace_back(piece[i - 1], piece[i]);
                        any = true;
                    }
            }
            if (any) break;
        }
        for (size_t i = 1; i < e.points.size(); ++i) segs.emplace_back(e.points[i - 1], e.points[i]);
        break;
    case EntityType::Spline: {
        // Tessellate the smooth curve so a crossing region that touches the drawn
        // curve (not just the control polygon) selects it.
        const bool cv = e.meta.value("splineMode").toString() == QLatin1String("cv");
        const auto curve = cv ? bsplinePoints(e.points) : splinePoints(e.points);
        for (size_t i = 1; i < curve.size(); ++i) segs.emplace_back(curve[i - 1], curve[i]);
        break;
    }
    case EntityType::Hatch:
        if (e.meta.value("shape").toString() == QLatin1String("circle")) {
            const int n = 48;
            Vec2 prev = pointOnCircle(e.center, e.radius, 0.0);
            for (int i = 1; i <= n; ++i) {
                const Vec2 cur = pointOnCircle(e.center, e.radius, 2.0 * M_PI * i / n);
                segs.emplace_back(prev, cur);
                prev = cur;
            }
            break;
        }
        for (size_t i = 0; i < e.points.size(); ++i)
            segs.emplace_back(e.points[i], e.points[(i + 1) % e.points.size()]);
        break;
    case EntityType::Rect:
    case EntityType::Polygon:
    case EntityType::Wipeout:
    case EntityType::Image:
        for (size_t i = 0; i < e.points.size(); ++i)
            segs.emplace_back(e.points[i], e.points[(i + 1) % e.points.size()]);
        break;
    case EntityType::Circle: {
        // Tessellate the circle so touching the drawn ring selects it (crossing).
        const int n = 48;
        Vec2 prev = pointOnCircle(e.center, e.radius, 0.0);
        for (int i = 1; i <= n; ++i) {
            const Vec2 cur = pointOnCircle(e.center, e.radius, 2.0 * M_PI * i / n);
            segs.emplace_back(prev, cur);
            prev = cur;
        }
        // A balloon's leader lines count too — a crossing zone touching the arrow/
        // leader selects the balloon (attach -> knee -> each tip).
        if (e.meta.value("balloon").toBool()) {
            const auto tips = balloonLeaders(e);
            if (!tips.empty()) {
                const Vec2 attach = balloonAttach(e), knee = balloonKnee(e);
                segs.emplace_back(attach, knee);
                for (const auto& tip : tips) segs.emplace_back(knee, tip);
            }
        }
        break;
    }
    case EntityType::Arc: {
        double sweep = e.endAngle - e.startAngle;
        while (sweep <= 0.0) sweep += 2.0 * M_PI;
        const int n = std::max(6, static_cast<int>(std::ceil(sweep / (M_PI / 24.0))));
        Vec2 prev = pointOnCircle(e.center, e.radius, e.startAngle);
        for (int i = 1; i <= n; ++i) {
            const Vec2 cur = pointOnCircle(e.center, e.radius, e.startAngle + sweep * i / n);
            segs.emplace_back(prev, cur);
            prev = cur;
        }
        break;
    }
    case EntityType::Ellipse: {
        const int n = 64;
        const double c = std::cos(e.rotation), s = std::sin(e.rotation);
        auto pt = [&](double t) -> Vec2 {
            const double x = e.radius * std::cos(t), y = e.radiusY * std::sin(t);
            return {e.center.x + x * c - y * s, e.center.y + x * s + y * c};
        };
        Vec2 prev = pt(0.0);
        for (int i = 1; i <= n; ++i) {
            const Vec2 cur = pt(2.0 * M_PI * i / n);
            segs.emplace_back(prev, cur);
            prev = cur;
        }
        break;
    }
    case EntityType::Text: {
        // The 4 edges of the text rectangle, so a crossing region that touches the
        // text box selects it.
        for (size_t i = 0; i < pts.size(); ++i)
            segs.emplace_back(pts[i], pts[(i + 1) % pts.size()]);
        break;
    }
    case EntityType::LengthBreak:
        // The two strokes THEMSELVES, so a zone that touches either one takes the mark and a window
        // round the pair encloses it. Without them the mark fell back to its bounding-box corners,
        // and a zone drawn over the strokes - which is where anyone would draw it - held no corner
        // and selected nothing.
        for (const auto& stroke : breakStrokes(e))
            for (size_t i = 1; i < stroke.size(); ++i)
                segs.emplace_back(stroke[i - 1], stroke[i]);
        break;
    case EntityType::Connector: {
        // Body-rectangle edges + each pin stub, so a crossing zone touching the body
        // or a pin selects the connector (and a window enclosing it does too).
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
        const ConnectorGeometry g = connectorGeometry(e.a, spec);
        const Vec2 bl = g.rectMin, tr = g.rectMax, br{tr.x, bl.y}, tl{bl.x, tr.y};
        segs.emplace_back(bl, br); segs.emplace_back(br, tr);
        segs.emplace_back(tr, tl); segs.emplace_back(tl, bl);
        for (size_t i = 0; i < g.pinTip.size() && i < g.pinEnd.size(); ++i)
            segs.emplace_back(g.pinTip[i], g.pinEnd[i]);
        break;
    }
    case EntityType::Twist: {
        // The ACTUAL drawn twist line, so a crossing box only has to touch the line (not
        // the loose bounding box). For "crossed" this is the Z jog (matching the render);
        // other types fall back to the glyph's bounding-box edges.
        const QString ty = e.meta.value("twistType").toString();
        double cx = 0.0, yMin = 1e18, yMax = -1e18;
        for (const Vec2& pt : e.points) {
            cx += pt.x;
            yMin = std::min(yMin, pt.y);
            yMax = std::max(yMax, pt.y);
        }
        if (!e.points.empty()) cx /= static_cast<double>(e.points.size());
        if (ty == QLatin1String("crossed") && yMax > yMin + 1e-9) {
            std::vector<double> ys;
            for (const Vec2& pt : e.points) ys.push_back(pt.y);
            std::sort(ys.begin(), ys.end());
            double gap = 1e18;
            for (size_t i = 1; i < ys.size(); ++i) gap = std::min(gap, ys[i] - ys[i - 1]);
            if (gap >= 1e17) gap = yMax - yMin;
            const double ext = 0.30 * gap;
            const double yb = yMin - ext, yt = yMax + ext;
            const double ang = e.meta.value("diagAngle").toDouble(15.0) * M_PI / 180.0;
            const double capLen = e.meta.value("capLen").toDouble(8.0);
            const double off = ((yt - yb) / 2.0) * std::tan(ang);
            const Vec2 diagBot{cx - off, yb}, diagTop{cx + off, yt};
            segs.emplace_back(Vec2{diagBot.x - capLen, yb}, diagBot);
            segs.emplace_back(diagBot, diagTop);
            segs.emplace_back(diagTop, Vec2{diagTop.x + capLen, yt});
        } else {
            for (size_t i = 0; i < pts.size(); ++i)
                segs.emplace_back(pts[i], pts[(i + 1) % pts.size()]);
        }
        break;
    }
    case EntityType::Screen: {
        // Tessellate the ACTUAL ellipse outline + the drain leg — a crossing box must
        // touch the real drawn curve/line (not just the loose bounding box) to select it.
        const int k = std::min(e.meta.value("catch").toInt(static_cast<int>(e.points.size())),
                               static_cast<int>(e.points.size()));
        if (k < 1) break;
        double cx = 0.0, yMin = 1e18, yMax = -1e18;
        for (int i = 0; i < k; ++i) {
            cx += e.points[i].x;
            yMin = std::min(yMin, e.points[i].y);
            yMax = std::max(yMax, e.points[i].y);
        }
        cx /= k;
        const double cyMid = (yMin + yMax) / 2.0;
        const double rx = e.meta.value("rx").toDouble(12.0);
        const double ry = e.meta.contains("ry") ? e.meta.value("ry").toDouble()
                                                : (yMax - yMin) / 2.0 + 6.0;
        const int n = 48;
        Vec2 prev{cx + rx, cyMid};
        for (int i = 1; i <= n; ++i) {
            const double a = 2.0 * M_PI * i / n;
            const Vec2 cur{cx + rx * std::cos(a), cyMid + ry * std::sin(a)};
            segs.emplace_back(prev, cur);
            prev = cur;
        }
        if (static_cast<int>(e.points.size()) > k) {
            const Vec2 drain = e.points.back();
            const QString legDir = e.meta.value("legDir").toString(QStringLiteral("bottom"));
            Vec2 edge;
            if (legDir == QLatin1String("top")) edge = {cx, cyMid + ry};
            else if (legDir == QLatin1String("left")) edge = {cx - rx, cyMid};
            else if (legDir == QLatin1String("right")) edge = {cx + rx, cyMid};
            else edge = {cx, cyMid - ry};
            segs.emplace_back(edge, drain);
        }
        break;
    }
    default:
        break;
    }
    return segs;
}

} // namespace

void Editor::finishRegionSelect(bool subtract) {
    // Selection accumulates across clicks/regions; Shift removes (deselects).
    const bool isLasso = regionMode_ == RegionMode::Lasso;
    // The moving corner is the live cursor (the second click for a rectangle).
    const Vec2 last = isLasso ? (dragPath_.empty() ? anchor_ : dragPath_.back()) : cursor_;
    // Window (left->right) vs crossing (right->left): fixed at conversion for a
    // lasso; from the second-click direction for a rectangle.
    const bool crossing = isLasso ? !lassoWindowMode_ : (last.x < anchor_.x);

    // Region polygon: the lasso path, or the rectangle corners.
    std::vector<Vec2> region;
    if (isLasso) {
        region = dragPath_;
    } else {
        Bounds b;
        b.expand(anchor_);
        b.expand(last);
        region = {{b.min.x, b.min.y}, {b.max.x, b.min.y}, {b.max.x, b.max.y}, {b.min.x, b.max.y}};
    }
    if (region.size() < 3) { emit selectionChanged(); return; }
    // Announced before the entities are gone through, so that whoever else has something in this space
    // that is not an entity - the windows on a sheet - gets the same zone, of the same kind.
    emit regionSelected(region, crossing, subtract);

    // Region edges for crossing-intersection tests.
    std::vector<std::pair<Vec2, Vec2>> regionEdges;
    for (size_t i = 0; i < region.size(); ++i)
        regionEdges.emplace_back(region[i], region[(i + 1) % region.size()]);

    for (const auto& e : doc_->entities()) {
        if (const Layer* l = doc_->findLayer(e.layer); l && (!l->visible || l->locked))
            continue;
        const auto pts = entitySelectPoints(e);
        if (pts.empty()) continue;

        int inside = 0;
        for (const auto& p : pts)
            if (pointInPolygon(region, p)) ++inside;

        // The selection test runs against the entity's REAL drawn geometry (its
        // segments), so a region that merely catches an off-stroke point — a circle
        // centre, an arc's off-curve quadrant, a line's mid joint — does NOT select.
        // Only point-like entities (markers/connector) with no segments fall back to
        // the representative points.
        // What the drawing SHOWS of this run: a stretch a break mark hides is not there to be picked.
        const auto segs = unmaskedSegments(e, entitySelectSegments(e));
        bool hit = false;
        if (!segs.empty()) {
            if (crossing) {
                // Crossing: any segment endpoint inside the region, or any segment
                // crossing the region boundary.
                for (const auto& s : segs) {
                    if (pointInPolygon(region, s.first) || pointInPolygon(region, s.second)) {
                        hit = true;
                        break;
                    }
                    for (const auto& re : regionEdges)
                        if (segmentSegmentIntersection(s.first, s.second, re.first, re.second)) {
                            hit = true;
                            break;
                        }
                    if (hit) break;
                }
            } else {
                // Window: the whole geometry must be enclosed (every segment end in).
                hit = true;
                for (const auto& s : segs)
                    if (!(pointInPolygon(region, s.first) && pointInPolygon(region, s.second))) {
                        hit = false;
                        break;
                    }
            }
        } else {
            // Point-like entity: use its representative points.
            hit = crossing ? (inside > 0) : (inside == static_cast<int>(pts.size()));
        }
        if (!hit) continue;
        auto it = std::find(selected_.begin(), selected_.end(), e.id);
        if (subtract) {
            if (it != selected_.end()) selected_.erase(it); // deselect
        } else if (it == selected_.end()) {
            selected_.push_back(e.id); // accumulate
        }
    }
    // A zone drawn INSIDE a table picks the cells it covers, not the table: that is how a block of
    // them is aligned in one go. Nothing else in the zone is touched, and a zone that reaches
    // outside the table selects objects the ordinary way.
    {
        Bounds rb;
        for (const Vec2& q : region) rb.expand(q);
        for (const auto& e : doc_->entities()) {
            if (!e.meta.value("table").toBool()) continue;
            const Bounds tb = boundsOf(e.points);
            if (!tb.valid) continue;
            const bool inside = rb.min.x >= tb.min.x - 1e-9 && rb.max.x <= tb.max.x + 1e-9 &&
                                rb.min.y >= tb.min.y - 1e-9 && rb.max.y <= tb.max.y + 1e-9;
            if (!inside) continue;
            if (selectCellsInRegion(e.id, region)) return;
        }
    }

    // A zone that catches a wire's TEXT picks that marking on its own - the colour, the
    // cross-section or the label - so it can be slid along the wire without touching anything
    // else. A wire the zone already selected keeps its markings: they move with the wire.
    for (const auto& e : doc_->entities()) {
        if (e.type != EntityType::Wire) continue;
        if (const Layer* l = doc_->findLayer(e.layer); l && (!l->visible || l->locked)) continue;
        if (std::find(selected_.begin(), selected_.end(), e.id) != selected_.end()) continue;
        for (const QString& part : wireAnnotParts()) {
            const int n = wireAnnotCountFor(e, part);
            for (int i = 0; i < n; ++i) {
                std::vector<Vec2> quad;
                if (!markQuad(e, part, i, quad)) continue;
                bool hit = false;
                if (crossing) {
                    for (const Vec2& q : quad)
                        if (pointInPolygon(region, q)) { hit = true; break; }
                    for (size_t k = 0; !hit && k < quad.size(); ++k)
                        for (const auto& re : regionEdges)
                            if (segmentSegmentIntersection(quad[k], quad[(k + 1) % quad.size()],
                                                           re.first, re.second)) {
                                hit = true;
                                break;
                            }
                } else {
                    hit = true;
                    for (const Vec2& q : quad)
                        if (!pointInPolygon(region, q)) { hit = false; break; }
                }
                if (!hit) continue;
                const MarkPick pick{e.id, part, i};
                auto at = std::find(markSel_.begin(), markSel_.end(), pick);
                if (subtract) {
                    if (at != markSel_.end()) markSel_.erase(at);
                } else if (at == markSel_.end()) {
                    markSel_.push_back(pick);
                }
            }
        }
    }

    // Selecting any balloon of a batch selects the WHOLE batch, so a zone over the
    // circles catches them all even if the arrow circle's leader runs outside it.
    if (!subtract) {
        std::vector<QString> add;
        for (const QString& id : selected_)
            if (Entity* e = entityById(id); e && e->meta.value("balloon").toBool())
                for (Entity* m : chainMatesOf(*e))
                    if (std::find(selected_.begin(), selected_.end(), m->id) == selected_.end() &&
                        std::find(add.begin(), add.end(), m->id) == add.end())
                        add.push_back(m->id);
        for (const auto& id : add) selected_.push_back(id);
    }
    // A green region drawn tightly over a twist also catches the wires running under it.
    // If a selected wire's ONLY contact with the region falls inside a selected twist's
    // glyph band, the user meant just the twist — drop that wire from the selection. (A
    // wider region that also crosses the wire beyond the twist keeps the wire.)
    if (!subtract) {
        std::vector<Bounds> twistBands;
        for (const QString& id : selected_)
            if (Entity* t = entityById(id); t && t->type == EntityType::Twist) {
                const Bounds b = t->bounds();
                if (b.valid) twistBands.push_back(b);
            }
        if (!twistBands.empty()) {
            const double tol = worldTolerance();
            auto insideAnyBand = [&](const Vec2& q) {
                for (const Bounds& b : twistBands)
                    if (q.x >= b.min.x - tol && q.x <= b.max.x + tol && q.y >= b.min.y - tol &&
                        q.y <= b.max.y + tol)
                        return true;
                return false;
            };
            std::vector<QString> drop;
            for (const QString& id : selected_) {
                Entity* w = entityById(id);
                if (!w || w->type != EntityType::Wire) continue;
                bool anyContact = false, contactOutside = false;
                for (const auto& s : entitySelectSegments(*w)) {
                    for (const Vec2& q : {s.first, s.second})
                        if (pointInPolygon(region, q)) {
                            anyContact = true;
                            if (!insideAnyBand(q)) contactOutside = true;
                        }
                    for (const auto& re : regionEdges)
                        if (auto x = segmentSegmentIntersection(s.first, s.second, re.first,
                                                                re.second)) {
                            anyContact = true;
                            if (!insideAnyBand(*x)) contactOutside = true;
                        }
                }
                if (anyContact && !contactOutside) drop.push_back(id);
            }
            for (const QString& id : drop)
                selected_.erase(std::remove(selected_.begin(), selected_.end(), id),
                                selected_.end());
        }
    }
    emit selectionChanged();
}

QColor Editor::paperInk(const QColor& c) {
    if (!EntityRenderer::whitePaper()) return c;   // the dark canvas: the ink was chosen for it
    const QColor hsv = c.toHsv();
    QColor out;
    out.setHsv(std::max(0, hsv.hue()), std::min(255, static_cast<int>(hsv.saturation() * 1.35)),
               std::min(hsv.value(), 150), std::max(c.alpha(), 205));
    return out.toRgb();
}

void Editor::drawGrips(QPainter& p, const Entity& e, const ViewTransform& vt) const {
    // Show the MTEXT container outline (dashed) so its width/height are visible.
    if (e.type == EntityType::Text) {
        const double bw = e.meta.value("boxWidth").toDouble(0.0);
        const double bh = e.meta.value("boxHeight").toDouble(0.0);
        if (bw > 0.0 || bh > 0.0) {
            const QPointF tl = vt.toScreen(e.a);
            const QPointF br = vt.toScreen({e.a.x + (bw > 0 ? bw : 0), e.a.y - (bh > 0 ? bh : 0)});
            QPen bp(paperInk(QColor(120, 200, 255, 160)));
            bp.setCosmetic(true);
            bp.setStyle(Qt::DashLine);
            p.setPen(bp);
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(tl, br).normalized());
        }
    }
    // A placed block shows no grips: it moves and deletes as one object, and its contents are
    // changed in the block editor rather than on the sheet. (Anything done to a copy here would be
    // rebuilt away the next time the block was saved, so grips would promise what they cannot keep.)
    for (const auto& g : isBlockPiece(e) ? std::vector<Grip>{}
                                         : gripsOf(e, collectPinEnds(doc_), doc_)) {
        const QPointF s = vt.toScreen(g.pos);
        // Radius/midpoint handles drawn as small squares; vertices/ends larger.
        const double r = (g.role == GripRole::Center) ? 4.0 : 3.5;
        p.setPen(QPen(QColor("#0d1117")));
        // Table grid-line handles are cyan (drag to resize a column/row).
        const bool gridEdge =
            g.role == GripRole::TableColEdge || g.role == GripRole::TableRowEdge;
        p.setBrush(g.role == GripRole::Center ? paperInk(QColor("#ffd166"))
                   : gridEdge               ? paperInk(QColor("#2bd0ff"))
                                            : paperInk(QColor("#ffffff")));
        p.drawRect(QRectF(s.x() - r, s.y() - r, 2 * r, 2 * r));
    }
    p.setBrush(Qt::NoBrush);
}

bool Editor::beginGripGrab(const Vec2& at, bool shift) {
    if (!doc_) return false;
    const double tol = worldTolerance();
    // A "point joint" is a movable endpoint/vertex/corner whose position is set
    // directly to the cursor (shareable between coincident primitives).
    auto isPointJoint = [](GripRole r) {
        return r == GripRole::LineA || r == GripRole::LineB || r == GripRole::Vertex ||
               r == GripRole::RectCorner;
    };
    // Find the NEAREST grip (joint) of any selected entity within tolerance —
    // nearest, not first, so a joint isn't shadowed by another grip that merely
    // sits within tolerance too (e.g. a double line's corner vs its centerline end).
    QString bestId;
    Grip bestGrip;
    double bestDist = tol;
    bool found = false;
    const std::vector<Vec2> pinEnds = collectPinEnds(doc_);
    for (const auto& id : selected_) {
        Entity* e = entityById(id);
        if (!e) continue;
        if (isBlockPiece(*e)) continue;   // and none to grab, for the same reason
        for (const auto& gr : gripsOf(*e, pinEnds, doc_)) {
            const double d = distance(at, gr.pos);
            if (d <= bestDist) { bestDist = d; bestId = id; bestGrip = gr; found = true; }
        }
    }
    // A balloon's centre grip is grabbable even with Shift held (Shift = move just
    // that circle). For any OTHER grip, Shift is reserved for selection — don't grab.
    const bool balloonCenter = found && [&] {
        Entity* e = entityById(bestId);
        return e && e->meta.value("balloon").toBool() && bestGrip.role == GripRole::Center;
    }();
    if (found && shift && !balloonCenter) return false;
    if (found) {
        const QString& id = bestId;
        Entity* e = entityById(id);
        const Grip& gr = bestGrip;
        {
            editing_ = true;
            gripGrab_ = true; // follows the cursor until the next click places it
            editMode_ = 1;
            editEntityId_ = id;
            editRole_ = static_cast<int>(gr.role);
            editIndex_ = gr.index;
            // Anchor the drag from the joint's true position (not the raw click),
            // so the first move delta and the ortho/polar reference are exact.
            editLast_ = gr.pos;
            editMoved_ = false;
            editAnchor_ = gr.pos;

            // Snapshot originals so the source stays visible (ghost) until placed.
            editGhost_.clear();
            for (const auto& sid : selected_)
                if (Entity* se = entityById(sid)) editGhost_.push_back(*se);
            // Moving a connector drags its wires along, so they belong in the preview
            // too: ghost each attached wire at its original shape, and the live re-routed
            // wire is drawn over it — the wires' movement previews just like the
            // connector's. (They also become the "original" a Shift release restores to.)
            for (const auto& sid : selected_) {
                Entity* se = entityById(sid);
                if (!se || se->type != EntityType::Connector) continue;
                const ConnectorGeometry cg =
                    connectorGeometry(se->a, connectorSpecFromMeta(se->meta));
                for (const auto& w : doc_->entities()) {
                    if (w.type != EntityType::Wire || w.points.empty()) continue;
                    bool already = false;
                    for (const Entity& gh : editGhost_)
                        if (gh.id == w.id) { already = true; break; }
                    if (already) continue;
                    bool attached = false;
                    for (const Vec2& pt : w.points) {
                        for (const Vec2& pe : cg.pinEnd)
                            if (distance(pt, pe) < 1e-6) { attached = true; break; }
                        if (attached) break;
                    }
                    if (attached) editGhost_.push_back(w);
                }
            }

            // Collect every coincident point-joint across the SELECTED entities so
            // a shared joint moves on all of them at once. Non-point joints
            // (center/radius/edge/mid) act on the single grabbed entity only.
            editGrips_.clear();
            const bool nonArrowBalloon = balloonCenter && balloonLeaders(*e).empty();
            if (balloonCenter && !shift && nonArrowBalloon) {
                // Moving a batch circle drags it AND every circle further from the
                // arrow (the "tail") as a rigid group; Shift moves only this one.
                Entity* arrow = nullptr;
                for (Entity* m : chainMatesOf(*e))
                    if (!balloonLeaders(*m).empty()) { arrow = m; break; }
                const double dGrab = arrow ? distance(e->center, arrow->center) : 0.0;
                editGrips_.push_back({id, static_cast<int>(GripRole::Move), 0, {}});
                for (Entity* m : chainMatesOf(*e))
                    if (m != arrow && distance(m->center, arrow ? arrow->center : e->center) >=
                                          dGrab - 1e-6)
                        editGrips_.push_back({m->id, static_cast<int>(GripRole::Move), 0, {}});
            } else if (isPointJoint(gr.role)) {
                const Vec2 jp = gr.pos;
                for (const auto& sid : selected_) {
                    Entity* se = entityById(sid);
                    if (!se) continue;
                    for (const auto& g2 : gripsOf(*se)) {
                        if (isPointJoint(g2.role) && distance(g2.pos, jp) < 1e-6)
                            editGrips_.push_back({sid, static_cast<int>(g2.role), g2.index, g2.off});
                    }
                }
            } else {
                editGrips_.push_back({id, static_cast<int>(gr.role), gr.index, gr.off});
            }

            // Polar/ortho during a joint move tracks relative to the adjacent
            // UNTOUCHED point (the other end of the segment at this joint).
            if (gr.role == GripRole::LineA) editAnchor_ = e->b;
            else if (gr.role == GripRole::LineB) editAnchor_ = e->a;
            else if (gr.role == GripRole::Vertex) {
                if (gr.index > 0 && gr.index - 1 < static_cast<int>(e->points.size()))
                    editAnchor_ = e->points[gr.index - 1];
                else if (gr.index == 0 && e->points.size() > 1)
                    editAnchor_ = e->points[1]; // first vertex -> next point
            } else if (gr.role == GripRole::Radius || gr.role == GripRole::Center ||
                       gr.role == GripRole::ArcStart || gr.role == GripRole::ArcEnd ||
                       gr.role == GripRole::ArcMid || gr.role == GripRole::EllipseMajor ||
                       gr.role == GripRole::EllipseMinor)
                editAnchor_ = e->center;

            // On a DOUBLE run the joints sit on the two physical rails, and so must the tracking: the
            // reference is the adjacent point on THE SAME rail. Taken from the centreline it drew the
            // green lock line up the middle of the run - between the two lines, where nothing is drawn -
            // and measured the angle from a point that is not on the line being pulled, so a rail
            // dragged "horizontal" came out at a slight angle to itself.
            if (gr.off.lengthSq() > 1e-12) {
                bool onRail = false;
                if (e->type == EntityType::Polyline && e->meta.value("double").toBool() &&
                    gr.index >= 0 && gr.index < static_cast<int>(e->points.size())) {
                    const double gap = e->meta.value("gap").toDouble(10.0);
                    for (double s : {gap / 2.0, -gap / 2.0}) {
                        const std::vector<Vec2> run = offsetPolyline(e->points, s);
                        if (run.size() != e->points.size()) continue;
                        // The rail this joint belongs to is the one whose offset it carries. Read off
                        // the run itself rather than added on, so a mitred corner is exact.
                        if (distance(run[gr.index] - e->points[gr.index], gr.off) > 1e-6) continue;
                        if (gr.index > 0) editAnchor_ = run[gr.index - 1];
                        else if (run.size() > 1) editAnchor_ = run[1];
                        onRail = true;
                        break;
                    }
                }
                // A double LINE (and a polyline whose bend is too tight to offset): the rails are
                // parallel to the centreline, so the adjacent rail point is the anchor plus the offset.
                if (!onRail) editAnchor_ = editAnchor_ + gr.off;
            }
            // A wire run/knee with several wires selected can pull the whole bundle —
            // say so, since Shift is a live modifier the drag only reveals if you try it.
            const bool bundlePull =
                e->type == EntityType::Wire && selected_.size() > 1 &&
                (gr.role == GripRole::WireSegMove || gr.role == GripRole::Vertex);
            if (bundlePull)
                emit message(QStringLiteral("Joint grabbed — hold Shift to pull all %1 selected "
                                            "wires as a batch; release and the others return "
                                            "to where they were")
                                 .arg(selected_.size()));
            else
                emit message(editGrips_.size() > 1
                                 ? QStringLiteral("Shared joint grabbed (%1 objects) — click to place")
                                       .arg(editGrips_.size())
                                 : QStringLiteral("Joint grabbed — move and click to place, Esc to cancel"));
            return true;
        }
    }
    return false;
}

void Editor::dragAttachedWires(const Entity& conn, const Vec2& delta,
                               const std::vector<QString>& movingIds) {
    // Ctrl while moving DETACHES: the connector moves alone, wires stay put.
    if (QApplication::keyboardModifiers() & Qt::ControlModifier) return;
    if (!doc_ || conn.type != EntityType::Connector) return;
    const ConnectorGeometry g = connectorGeometry(conn.a, connectorSpecFromMeta(conn.meta));
    constexpr double kOn = 1e-6; // "sits exactly on the pin end"
    auto onPin = [&](const Vec2& pt) {
        for (const Vec2& pe : g.pinEnd)
            if (distance(pt, pe) < kOn) return true;
        return false;
    };
    for (auto& w : doc_->entities()) {
        if (w.type != EntityType::Wire || w.points.empty()) continue;
        if (std::find(movingIds.begin(), movingIds.end(), w.id) != movingIds.end()) continue;
        const int n = static_cast<int>(w.points.size());
        // Which ends sit on this connector's pins, decided BEFORE anything moves — squaring
        // a run can insert a knee, which shifts the later indices.
        const bool firstOnPin = n >= 1 && onPin(w.points.front());
        const bool lastOnPin = n >= 2 && onPin(w.points.back());
        // Interior vertices first, while the original indices still hold: one parked on a pin
        // follows it (there is no angle to keep in the middle of a run).
        for (int i = 1; i + 1 < n; ++i)
            if (onPin(w.points[i])) w.points[i] = w.points[i] + delta;
        // Then the ends, LAST one first: squaring the first end may insert points and renumber
        // everything after it, so the far end is dealt with while its index is still valid.
        // Each end's knee is re-squared (or given one) so the run keeps its right angle
        // instead of going diagonal the moment the connector moves off-axis.
        if (lastOnPin)
            dragWireEndKeepingRightAngle(w, static_cast<int>(w.points.size()) - 1, delta);
        if (firstOnPin) dragWireEndKeepingRightAngle(w, 0, delta);
    }
}

void Editor::repositionTwistAtX(Entity& twist, double x) {
    if (!doc_) return;
    const QJsonArray ids = twist.meta.value("wires").toArray();
    for (int i = 0; i < static_cast<int>(twist.points.size()) && i < ids.size(); ++i) {
        Entity* w = entityById(ids[i].toString());
        if (!w || w->points.size() < 2) continue;
        // Clamp to this wire's own x-extent — the twist can never slide off its wires.
        double xMin = 1e18, xMax = -1e18;
        for (const Vec2& pt : w->points) {
            xMin = std::min(xMin, pt.x);
            xMax = std::max(xMax, pt.x);
        }
        const double xc = std::clamp(x, xMin, xMax);
        // Interpolate the wire's y at xc (along whichever segment contains it).
        double y = w->points[0].y;
        for (size_t s = 1; s < w->points.size(); ++s) {
            const Vec2 a = w->points[s - 1], b = w->points[s];
            const double lo = std::min(a.x, b.x), hi = std::max(a.x, b.x);
            if (xc >= lo - 1e-9 && xc <= hi + 1e-9) {
                const double t = (b.x != a.x) ? (xc - a.x) / (b.x - a.x) : 0.0;
                y = a.y + (b.y - a.y) * t;
                break;
            }
        }
        twist.points[i] = {xc, y};
    }
}

// Slide a shield ALONG the wires it catches: each caught crossing point is re-derived
// from its own wire at the new x (clamped to that wire's extent, so the shield can never
// leave the wires), and the drain leg follows by the centre's actual movement so the leg
// stays attached. A decorative copy with no wire references is left alone (the caller
// then falls back to a free translate).
bool Editor::repositionScreenAtX(Entity& screen, double x) {
    if (!doc_) return false;
    const QJsonArray ids = screen.meta.value("wires").toArray();
    const int k = std::min(screen.meta.value("catch").toInt(static_cast<int>(screen.points.size())),
                           static_cast<int>(screen.points.size()));
    if (k < 1 || ids.isEmpty()) return false;
    auto meanOfCaught = [&] {
        Vec2 c{0, 0};
        for (int i = 0; i < k; ++i) c = c + screen.points[i];
        return c * (1.0 / static_cast<double>(k));
    };
    const Vec2 before = meanOfCaught();
    bool moved = false;
    for (int i = 0; i < k && i < ids.size(); ++i) {
        Entity* w = entityById(ids[i].toString());
        if (!w || w->points.size() < 2) continue;
        double xMin = 1e18, xMax = -1e18;
        for (const Vec2& pt : w->points) {
            xMin = std::min(xMin, pt.x);
            xMax = std::max(xMax, pt.x);
        }
        const double xc = std::clamp(x, xMin, xMax);
        double y = w->points[0].y;
        for (size_t s = 1; s < w->points.size(); ++s) {
            const Vec2 a = w->points[s - 1], b = w->points[s];
            const double lo = std::min(a.x, b.x), hi = std::max(a.x, b.x);
            if (xc >= lo - 1e-9 && xc <= hi + 1e-9) {
                const double t = (b.x != a.x) ? (xc - a.x) / (b.x - a.x) : 0.0;
                y = a.y + (b.y - a.y) * t;
                break;
            }
        }
        screen.points[i] = {xc, y};
        moved = true;
    }
    if (!moved) return false;
    const Vec2 shift = meanOfCaught() - before;
    for (int i = k; i < static_cast<int>(screen.points.size()); ++i)
        screen.points[i] = screen.points[i] + shift; // drain leg rides along
    pullShieldLegWire(screen); // ...and so does the wire joined to the leg
    return true;
}

bool Editor::shieldCatchAt(const Entity& screen, double ry, std::vector<Vec2>& crossings,
                           std::vector<QString>& wireIds) const {
    crossings.clear();
    wireIds.clear();
    const ShieldFrame f = shieldFrameOf(screen);
    if (!doc_ || !f.valid) return false;
    // The wires the outline spans = those crossing a segment along the major axis, from
    // one large-radius tip to the other. Exactly the geometry the placement fence used.
    const Vec2 a{f.center.x - ry * f.major.x, f.center.y - ry * f.major.y};
    const Vec2 b{f.center.x + ry * f.major.x, f.center.y + ry * f.major.y};
    for (const auto& w : doc_->entities()) {
        if (w.type != EntityType::Wire || w.points.size() < 2) continue;
        for (size_t i = 1; i < w.points.size(); ++i)
            if (auto x = segmentSegmentIntersection(a, b, w.points[i - 1], w.points[i])) {
                crossings.push_back(*x);
                wireIds.push_back(w.id);
                break; // one crossing per wire
            }
    }
    return true;
}

void Editor::commitShieldResize(const QString& id) {
    Entity* e = entityById(id);
    if (!e || e->type != EntityType::Screen) return;
    const Entity* orig = nullptr;
    for (const Entity& g : editGhost_)
        if (g.id == id) orig = &g;
    if (!orig) return;

    const double draggedRy = e->meta.value("ry").toDouble();
    std::vector<Vec2> xs;
    std::vector<QString> ids;
    shieldCatchAt(*orig, draggedRy, xs, ids);

    std::vector<QString> prevIds;
    for (const auto v : orig->meta.value("wires").toArray()) prevIds.push_back(v.toString());
    auto keyOf = [](std::vector<QString> v) {
        std::sort(v.begin(), v.end());
        return v;
    };
    // The resize only sticks if it actually changed which wires are enclosed. A drag that
    // stops short of the next wire (or that would enclose none) snaps back, so a shield's
    // size always corresponds to a real set of caught wires.
    if (xs.empty() || keyOf(ids) == keyOf(prevIds)) {
        *e = *orig;
        emit message(QStringLiteral("Shield size unchanged — no other wire reached"));
        return;
    }

    // Adopt the new wire set. The leg keeps its LENGTH, re-anchored to the new outline.
    const QString legDir = orig->meta.value("legDir").toString(QStringLiteral("bottom"));
    const ShieldFrame oldF = shieldFrameOf(*orig);
    double legLen = screenLeg_;
    if (static_cast<int>(orig->points.size()) > oldF.k && oldF.valid)
        legLen = distance(orig->points.back(), shieldLegEdge(oldF, legDir));

    Entity next = *orig;
    next.points.assign(xs.begin(), xs.end());
    QJsonArray idArr;
    for (const QString& wid : ids) idArr.append(wid);
    next.meta.insert("wires", idArr);
    next.meta.insert("catch", static_cast<int>(xs.size()));
    // Re-centre on the new set, then keep the dragged half-height — but never smaller than
    // what it takes to actually enclose that set.
    ShieldFrame probe = shieldFrameOf(next);
    double span = 0.0;
    if (probe.valid) {
        double tMin = 1e18, tMax = -1e18;
        for (const Vec2& c : xs) {
            const double t = c.x * probe.major.x + c.y * probe.major.y;
            tMin = std::min(tMin, t);
            tMax = std::max(tMax, t);
        }
        span = tMax - tMin;
    }
    next.meta.insert("ry", std::max(draggedRy, span / 2.0 + 3.0));
    const ShieldFrame newF = shieldFrameOf(next);
    if (newF.valid) {
        const Vec2 edge = shieldLegEdge(newF, legDir);
        const Vec2 dir = (edge - newF.center).normalized();
        next.points.push_back({edge.x + dir.x * legLen, edge.y + dir.y * legLen});
    }
    *e = next;
    emit message(QStringLiteral("Shield resized over %1 wires").arg(xs.size()));
}

// What Align can line up: anything that lives ON a wire and can only slide along it.
bool Editor::isAlignable(const Entity& e) const {
    switch (e.type) {
    case EntityType::Twist:
    case EntityType::Screen:
        return true;
    case EntityType::NetLabel:
        // Only a label ATTACHED to a wire slides; a free-standing one has nothing to slide on.
        return !e.meta.value(QStringLiteral("wireId")).toString().isEmpty();
    case EntityType::Wire:
        // A wire itself is alignable when it carries markings to line up.
        return wireAnnotCount(e) > 0 ||
               !e.meta.value(QStringLiteral("wireLabel")).toString().isEmpty();
    default:
        return false;
    }
}

void Editor::beginTwistAlign() {
    if (!doc_) return;
    bool any = false;
    for (const QString& id : selected_)
        if (const Entity* e = entityById(id); e && isAlignable(*e)) any = true;
    if (!any) {
        emit message(QStringLiteral(
            "Select twist marks, shields, or wires carrying a colour/gauge marking or a label "
            "first, then Align"));
        return;
    }
    awaitTwistAlign_ = true;
    emit message(QStringLiteral("Align: click the column to line them up on"));
}

void Editor::twistAlignTo(const Vec2& dest) {
    awaitTwistAlign_ = false;
    if (!doc_) return;
    pushUndo();
    // Everything that rides on a wire is lined up on the clicked column: twist marks, shields,
    // a wire's colour/gauge marking and its label. Each stays ON its own wire - the column only
    // decides how far along - so aligning can never pull a symbol off the run it belongs to.
    int n = 0;
    for (const QString& id : selected_) {
        Entity* e = entityById(id);
        if (!e) continue;
        switch (e->type) {
        case EntityType::Twist:
            repositionTwistAtX(*e, dest.x);
            ++n;
            break;
        case EntityType::Screen:
            if (repositionScreenAtX(*e, dest.x)) ++n;
            break;
        case EntityType::NetLabel: {
            const QString wid = e->meta.value(QStringLiteral("wireId")).toString();
            const Entity* w = entityById(wid);
            if (!w || w->type != EntityType::Wire) break;
            e->meta.insert(QStringLiteral("annT"), wireFractionAtX(*w, dest.x));
            ++n;
            break;
        }
        case EntityType::Wire: {
            const double want = wireFractionAtX(*e, dest.x);
            const int count = wireAnnotCount(*e);
            if (count > 0) {
                // The instance NEAREST the column is the one that moves. Dragging every copy
                // onto one column would stack them on top of each other, which is not what a
                // row of markings lined up on a column means.
                std::vector<double> ts = wireAnnotFractions(*e, count);
                int nearest = 0;
                for (int i = 1; i < count; ++i)
                    if (std::abs(ts[i] - want) < std::abs(ts[nearest] - want)) nearest = i;
                ts[nearest] = want;
                setWireAnnotFractions(*e, ts);
                ++n;
            }
            // A label attached to this wire follows the same column.
            for (auto& lab : doc_->entities())
                if (lab.type == EntityType::NetLabel &&
                    lab.meta.value(QStringLiteral("wireId")).toString() == e->id)
                    lab.meta.insert(QStringLiteral("annT"), want);
            break;
        }
        default:
            break;
        }
    }
    doc_->setDirty(true);
    emit changed();
    emit message(QStringLiteral("Aligned %1 item(s) to the column").arg(n));
}

// How far along a wire the given x falls, as a fraction of its length. A wire that doubles
// back covers one x more than once; the nearest point on the run decides, which is what makes
// aligning a marking to a column land where the eye expects.
double Editor::wireFractionAtX(const Entity& wire, double x) const {
    if (wire.points.size() < 2) return 0.0;
    // Project the column onto the run: walk the segments and take the closest point to the
    // vertical line at x, then read its fraction.
    double bestDist = 1e300;
    Vec2 best = wire.points.front();
    for (size_t i = 1; i < wire.points.size(); ++i) {
        const Vec2 a = wire.points[i - 1], b = wire.points[i];
        const double lo = std::min(a.x, b.x), hi = std::max(a.x, b.x);
        Vec2 cand;
        if (x >= lo - 1e-9 && x <= hi + 1e-9) {
            const double t = (std::abs(b.x - a.x) < 1e-9) ? 0.0 : (x - a.x) / (b.x - a.x);
            cand = lerp(a, b, std::clamp(t, 0.0, 1.0));
        } else {
            cand = (std::abs(x - lo) < std::abs(x - hi)) ? (a.x < b.x ? a : b)
                                                         : (a.x < b.x ? b : a);
        }
        const double d = std::abs(cand.x - x);
        if (d < bestDist) { bestDist = d; best = cand; }
    }
    return wireFractionNearest(wire, best);
}

void Editor::beginMatchProps() {
    if (!doc_) return;
    // An object already selected IS the source: "click the object, then Match" has to
    // work just as well as "Match, then click the source" — otherwise the first click
    // after arming silently re-picks a source and the command looks like it does
    // nothing. Several objects selected is ambiguous, so that still asks for a pick.
    const QString preselected = (selected_.size() == 1) ? selected_.front() : QString();
    setTool(ToolId::Select); // uses the pick cursor; setTool clears the flags first...
    awaitMatchProps_ = true; // ...so arm AFTER it
    matchSourceId_ = preselected;
    matchCount_ = 0;
    if (!matchSourceId_.isEmpty()) {
        selected_ = {matchSourceId_};
        emit selectionChanged();
        emit message(QStringLiteral("Match properties from the selected object — click objects to "
                                    "paint them (Esc to finish)"));
    } else {
        emit message(QStringLiteral("Match properties: click the source object to copy from"));
    }
    emit matchPropsChanged();
    emit changed();
}

void Editor::matchPropsClick(const Vec2& p) {
    if (!doc_) return;
    auto hit = hitTest(p);
    if (!hit) {
        // Clicking empty space just re-prompts; the mode stays armed (Esc ends it).
        emit message(matchSourceId_.isEmpty()
                         ? QStringLiteral("Match properties: click the source object")
                         : QStringLiteral("Match properties: click an object to paint it "
                                          "(Esc to finish)"));
        return;
    }
    // First click captures the source.
    if (matchSourceId_.isEmpty()) {
        matchSourceId_ = *hit;
        selected_ = {matchSourceId_};
        emit selectionChanged();
        emit changed();
        emit message(QStringLiteral(
            "Match properties: now click objects to paint with these properties (Esc to finish)"));
        return;
    }
    // Later clicks apply to the target under the cursor.
    if (*hit == matchSourceId_) {
        emit message(QStringLiteral("That is the source object — click a different one to paint it"));
        return;
    }
    const Entity* src = entityById(matchSourceId_);
    Entity* dst = entityById(*hit);
    if (!src || !dst) return;
    pushUndo();
    // Style / technical properties always copy across.
    dst->layer = src->layer;
    dst->color = src->color;
    dst->weight = src->weight;
    // Geometry dimensions, font and type-specific technical data only make sense between
    // objects of the SAME type — copy the size fields + merge the source meta then.
    if (dst->type == src->type) {
        dst->radius = src->radius;
        dst->radiusY = src->radiusY;
        dst->rotation = src->rotation;
        // Merge every source meta key (font/height, ncSize, wire AWG/net/insulation,
        // twistType, shield radii, etc.) over the target's, preserving the target's
        // identity-ish keys that must not be shared (pin labels, refdes, part number).
        static const QStringList kKeepOnTarget = {
            QStringLiteral("refdes"), QStringLiteral("pn"), QStringLiteral("pins"),
            QStringLiteral("label"), QStringLiteral("net"), QStringLiteral("text")};
        QJsonObject merged = dst->meta;
        for (auto it = src->meta.begin(); it != src->meta.end(); ++it)
            if (!kKeepOnTarget.contains(it.key())) merged.insert(it.key(), it.value());
        dst->meta = merged;
    }
    doc_->setDirty(true);
    ++matchCount_;
    emit changed();
    // Say what actually travelled: between different types only the style can, which
    // otherwise looks like a half-working command.
    const bool sameType = dst->type == src->type;
    emit message(QStringLiteral("Matched %1 onto %2 object(s) — click more, or Esc to finish")
                     .arg(sameType ? QStringLiteral("properties")
                                   : QStringLiteral("layer / colour / lineweight"))
                     .arg(matchCount_));
}

// ---- Quick Select (AutoCAD QSELECT) ----------------------------------------
// Both orders work, like the other pick commands here: objects already selected open
// the dialog straight away, otherwise the command stays armed so objects can be picked
// on the canvas and Enter opens it. The first selected object is the "sample" the
// dialog reads its property list and starting values from.

void Editor::beginQuickSelect() {
    if (!doc_) return;
    if (!selected_.empty()) {
        openQuickSelect();
        return;
    }
    setTool(ToolId::Select); // so canvas clicks pick objects; setTool clears the flags...
    awaitQuickSelect_ = true; // ...so arm AFTER it
    emit message(QStringLiteral("Quick Select: click the objects to sample, then press Enter "
                                "(Esc cancels)"));
}

void Editor::openQuickSelect() {
    if (!doc_) return;
    awaitQuickSelect_ = false;
    // No sample: the dialog still opens, offering the properties every object has.
    emit quickSelectRequested(selected_.empty() ? QString() : selected_.front());
}

void Editor::cancelQuickSelect() {
    if (!awaitQuickSelect_) return;
    awaitQuickSelect_ = false;
    emit message(QStringLiteral("Quick Select cancelled"));
}

int Editor::applyQuickSelect(const QuickSelectQuery& q) {
    if (!doc_) return 0;
    selected_ = quickSelectMatchIds(doc_->entities(), q, selected_);
    emit selectionChanged();
    emit changed();
    emit message(selected_.empty()
                     ? QStringLiteral("Quick Select: nothing matched the filter")
                     : QStringLiteral("Quick Select: %1 object(s) selected").arg(selected_.size()));
    return static_cast<int>(selected_.size());
}

std::vector<QString> Editor::quickSelectValueChoices(const QString& key) const {
    if (!doc_) return {};
    return quickSelectDistinctValues(doc_->entities(), key);
}

std::vector<QString> Editor::presentEntityTypes() const {
    std::vector<QString> out;
    if (!doc_) return out;
    for (const auto& e : doc_->entities()) {
        const QString name = entityTypeToString(e.type);
        if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::optional<Entity> Editor::entityCopy(const QString& id) const {
    if (!doc_) return std::nullopt;
    for (const auto& e : doc_->entities())
        if (e.id == id) return e;
    return std::nullopt;
}

std::optional<Vec2> Editor::connectorFeatureSnap(const Vec2& rawAnchor, Vec2* catchOut) const {
    if (!doc_) return std::nullopt;
    // Ghost feature points (body corners + pin ends) at the candidate anchor.
    const ConnectorGeometry gg = connectorGeometry(rawAnchor, connectorSpec_);
    std::vector<Vec2> feats = {gg.rectMin,
                               {gg.rectMax.x, gg.rectMin.y},
                               gg.rectMax,
                               {gg.rectMin.x, gg.rectMax.y}};
    for (const Vec2& pe : gg.pinEnd) feats.push_back(pe);
    // Target points to catch onto — a broad set so the ghost's nearest feature can latch
    // onto ANY meaningful point: connector corners + pin ends, EVERY wire vertex (endpoints
    // AND interior joints/knees), splices, X markers, and the endpoints/corners/centres of
    // ordinary geometry (lines, polylines, rects, circles/arcs).
    std::vector<Vec2> targets;
    for (const auto& e : doc_->entities()) {
        switch (e.type) {
        case EntityType::Connector: {
            const ConnectorGeometry g = connectorGeometry(e.a, connectorSpecFromMeta(e.meta));
            targets.push_back(g.rectMin);
            targets.push_back({g.rectMax.x, g.rectMin.y});
            targets.push_back(g.rectMax);
            targets.push_back({g.rectMin.x, g.rectMax.y});
            for (const Vec2& pe : g.pinEnd) targets.push_back(pe);
            break;
        }
        case EntityType::Wire:
        case EntityType::Polyline:
        case EntityType::Rect:
            for (const Vec2& pt : e.points) targets.push_back(pt); // every vertex / joint
            break;
        case EntityType::Line:
        case EntityType::DoubleLine:
            targets.push_back(e.a);
            targets.push_back(e.b);
            break;
        case EntityType::Circle:
        case EntityType::Arc:
        case EntityType::Ellipse:
            targets.push_back(e.center);
            break;
        case EntityType::Splice:
        case EntityType::NotConnected:
        case EntityType::NetLabel:
            targets.push_back(e.a);
            break;
        default:
            for (const Vec2& pt : e.points) targets.push_back(pt);
            break;
        }
    }
    double best = worldTolerance();
    Vec2 off{0, 0};
    Vec2 catchAt{0, 0};
    bool found = false;
    for (const Vec2& f : feats)
        for (const Vec2& t : targets) {
            const double d = distance(f, t);
            if (d < best) { best = d; off = t - f; catchAt = t; found = true; }
        }
    if (!found) return std::nullopt;
    if (catchOut) *catchOut = catchAt; // the connection point the ghost latched onto
    return std::optional<Vec2>(rawAnchor + off);
}

std::optional<Vec2> Editor::frozenGrabFeature(const Vec2& p) const {
    if (tool_ != ToolId::Connector || !connectorArmed_ || !connectorFrozen_)
        return std::nullopt;
    const ConnectorGeometry fg = connectorGeometry(connectorFrozenPos_, connectorSpec_);
    std::vector<Vec2> feats = {fg.rectMin,
                               {fg.rectMax.x, fg.rectMin.y},
                               fg.rectMax,
                               {fg.rectMin.x, fg.rectMax.y}};
    for (const Vec2& pe : fg.pinEnd) feats.push_back(pe);
    double best = worldTolerance() * 1.8; // forgiving pick radius
    std::optional<Vec2> hit;
    for (const Vec2& f : feats) {
        const double d = distance(p, f);
        if (d < best) { best = d; hit = f; }
    }
    return hit;
}

// ---- Wire label & marking --------------------------------------------------

QString Editor::wireAt(const Vec2& p) const {
    if (!doc_) return {};
    const double tol = std::max(worldTolerance(), 1e-3);
    QString best;
    double bestDist = tol;
    for (const auto& e : doc_->entities()) {
        if (e.type != EntityType::Wire || e.points.size() < 2) continue;
        for (size_t i = 1; i < e.points.size(); ++i) {
            const double d = closestPointOnSegment(p, e.points[i - 1], e.points[i]).distance;
            if (d <= bestDist) { bestDist = d; best = e.id; }
        }
    }
    return best;
}

void Editor::setSelectionWireLabel(const QString& text) {
    if (!doc_) return;
    pushUndo();
    int n = 0;
    for (const QString& id : selected_) {
        Entity* w = entityById(id);
        if (!w || w->type != EntityType::Wire) continue;
        w->meta.insert(QStringLiteral("wireLabel"), text);
        ++n;
        // Any Wire label placed on this wire shows the same text.
        for (auto& lab : doc_->entities())
            if (lab.type == EntityType::NetLabel &&
                lab.meta.value(QStringLiteral("wireId")).toString() == id)
                lab.meta.insert(QStringLiteral("text"), text);
    }
    if (n == 0) return;
    doc_->setDirty(true);
    emit changed();
}

void Editor::applyWireMarkingToSelection() {
    if (!doc_ || selected_.empty()) return;
    bool any = false;
    for (const QString& id : selected_) {
        Entity* w = entityById(id);
        if (!w || w->type != EntityType::Wire) continue;
        if (!any) { pushUndo(); any = true; }
        w->meta.insert(QStringLiteral("showColorName"), wireShowColorName_);
        w->meta.insert(QStringLiteral("showGaugeName"), wireShowGaugeName_);
        w->meta.insert(QStringLiteral("colorNameMode"), wireColorNameMode_);
        const std::vector<double> ts = wireAnnotFractions(*w, std::clamp(wireAnnotCount_, 1, 20));
        w->meta.insert(QStringLiteral("annCount"), std::clamp(wireAnnotCount_, 1, 20));
        setWireAnnotFractions(*w, ts);
        w->meta.insert(QStringLiteral("annSize"), wireAnnotSize_);
    }
    if (!any) return;
    doc_->setDirty(true);
    emit changed();
}

void Editor::setSelectionWireAnnotCount(int count) {
    // No part named: the colour and the cross-section together, as one setting used to do.
    setSelectionWireAnnotCountFor(QStringLiteral("color"), count);
    setSelectionWireAnnotCountFor(QStringLiteral("gauge"), count);
}

void Editor::setSelectionWireAnnotSize(const QString& part, double height) {
    if (!doc_) return;
    pushUndo();
    int n = 0;
    for (const QString& id : selected_) {
        Entity* w = entityById(id);
        if (!w || w->type != EntityType::Wire) continue;
        setWireAnnotSizeFor(*w, part, height);
        ++n;
    }
    if (n == 0) return;
    doc_->setDirty(true);
    emit changed();
}

void Editor::setSelectionWireAnnotGap(const QString& part, double gap) {
    if (!doc_) return;
    pushUndo();
    int n = 0;
    for (const QString& id : selected_) {
        Entity* w = entityById(id);
        if (!w || w->type != EntityType::Wire) continue;
        setWireAnnotGapFor(*w, part, gap);
        ++n;
    }
    if (n == 0) return;
    doc_->setDirty(true);
    emit changed();
}

void Editor::setSelectionWireAnnotCountFor(const QString& part, int count) {
    if (!doc_) return;
    count = std::clamp(count, 1, 20);
    pushUndo();
    int n = 0;
    for (const QString& id : selected_) {
        Entity* w = entityById(id);
        if (!w || w->type != EntityType::Wire) continue;
        // Keep the places already set by dragging; new instances spread evenly.
        QString key = part;
        if (!key.isEmpty()) key[0] = key[0].toUpper();
        const std::vector<double> ts = wireAnnotFractionsFor(*w, part, count);
        w->meta.insert(QStringLiteral("annCount") + key, count);
        setWireAnnotFractionsFor(*w, part, ts);
        ++n;
    }
    if (n == 0) return;
    doc_->setDirty(true);
    emit changed();
}

void Editor::convertSelectionResistanceUnit(bool toFeet) {
    if (!doc_) return;
    bool any = false;
    for (const QString& id : selected_) {
        Entity* w = entityById(id);
        if (!w || w->type != EntityType::Wire) continue;
        if (!any) { pushUndo(); any = true; }
        const bool wasFeet =
            w->meta.value(QStringLiteral("resUnit")).toString(QStringLiteral("m")) ==
            QLatin1String("ft");
        double val = w->meta.value(QStringLiteral("resistance")).toDouble();
        // Only a real change of unit converts, and 0 stays 0.
        if (wasFeet != toFeet && val != 0.0) val = toFeet ? val * 0.3048 : val / 0.3048;
        w->meta.insert(QStringLiteral("resUnit"),
                       toFeet ? QStringLiteral("ft") : QStringLiteral("m"));
        w->meta.insert(QStringLiteral("resistance"), val);
    }
    if (!any) return;
    doc_->setDirty(true);
    emit changed();
}

void Editor::setWireLabelText(const QString& labelId, const QString& text) {
    if (!doc_) return;
    Entity* lab = entityById(labelId);
    if (!lab || lab->type != EntityType::NetLabel) return;
    pushUndo();
    lab->meta.insert(QStringLiteral("text"), text);
    // An attached label IS the wire's label, so the wire's property follows it.
    const QString wid = lab->meta.value(QStringLiteral("wireId")).toString();
    if (Entity* w = entityById(wid))
        if (w->type == EntityType::Wire) w->meta.insert(QStringLiteral("wireLabel"), text);
    doc_->setDirty(true);
    emit changed();
}

// A Screen / Twist annotation lists the wires it covers, so the wire's own status is
// simply "is this wire named by one of them".
bool Editor::wireCoveredBy(const QString& wireId, EntityType type) const {
    if (!doc_ || wireId.isEmpty()) return false;
    for (const auto& e : doc_->entities()) {
        if (e.type != type) continue;
        for (const auto& v : e.meta.value(QStringLiteral("wires")).toArray())
            if (v.toString() == wireId) return true;
    }
    return false;
}

bool Editor::wireIsShielded(const QString& wireId) const {
    return wireCoveredBy(wireId, EntityType::Screen);
}

bool Editor::wireIsTwisted(const QString& wireId) const {
    return wireCoveredBy(wireId, EntityType::Twist);
}

QString Editor::wireEndRef(const Entity& wire, bool firstEnd) const {
    if (!doc_ || wire.points.size() < 2) return QStringLiteral("unconnected");
    const Vec2 p = firstEnd ? wire.points.front() : wire.points.back();
    // A pin is the strongest answer — name it.
    const QString pin = pinRefAt(p);
    if (!pin.isEmpty()) return pin;
    // Otherwise look for a termination sitting on the end. A splice joins this wire to
    // others; an X marks the end as deliberately open. Anything else is simply not
    // connected to anything.
    const double tol = std::max(worldTolerance(), 1e-3);
    for (const auto& e : doc_->entities()) {
        // Either joint of a two-conductor splice terminates the conductor that runs through it.
        if (e.type == EntityType::Splice && e.meta.value("pair").toBool(false) &&
            distance(e.b, p) <= tol)
            return QStringLiteral("splice");
        if (e.type == EntityType::Splice && distance(e.a, p) <= tol)
            return QStringLiteral("splice");
        if (e.type == EntityType::NotConnected && distance(e.a, p) <= tol)
            return QStringLiteral("open");
    }
    return QStringLiteral("unconnected");
}

QString Editor::pinRefAt(const Vec2& p) const {
    if (!doc_) return {};
    for (const auto& e : doc_->entities()) {
        if (e.type != EntityType::Connector) continue;
        const ConnectorSpec s = connectorSpecFromMeta(e.meta);
        const ConnectorGeometry g = connectorGeometry(e.a, s);
        for (int i = 0; i < static_cast<int>(g.pinEnd.size()); ++i)
            if (distance(p, g.pinEnd[i]) < 1e-6) {
                const QString des = s.designator.isEmpty()
                                        ? QStringLiteral("#%1").arg(e.meta.value("entNum").toInt())
                                        : s.designator;
                const QString lbl = i < s.pins.size() ? s.pins[i] : QString::number(i + 1);
                return des + QLatin1Char('/') + lbl;
            }
    }
    return {};
}

int Editor::assignEntityNumber() const {
    int mx = 0;
    if (doc_)
        for (const auto& e : doc_->entities())
            mx = std::max(mx, e.meta.value("entNum").toInt(0));
    return mx + 1;
}

std::vector<ValidationIssue> Editor::validateSchematic() const {
    std::vector<ValidationIssue> issues;
    if (!doc_) return issues;
    const double tol = 1e-6;
    // Gather every wire endpoint, splice, and not-connected ("X") marker position.
    std::vector<Vec2> wireEnds, ncMarks, splices;
    for (const auto& e : doc_->entities()) {
        if (e.type == EntityType::Wire && e.points.size() >= 2) {
            wireEnds.push_back(e.points.front());
            wireEnds.push_back(e.points.back());
        } else if (e.type == EntityType::NotConnected) {
            ncMarks.push_back(e.a);
        } else if (e.type == EntityType::Splice) {
            splices.push_back(e.a);
        }
    }
    auto nearAny = [&](const Vec2& p, const std::vector<Vec2>& pts) {
        for (const Vec2& q : pts) if (distance(p, q) < tol) return true;
        return false;
    };
    for (const auto& e : doc_->entities()) {
        if (e.type != EntityType::Connector) continue;
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
        const ConnectorGeometry g = connectorGeometry(e.a, spec);
        const QString name = spec.designator.isEmpty()
                                 ? QStringLiteral("#%1").arg(e.meta.value("entNum").toInt())
                                 : spec.designator;
        const Vec2 bodyMid{(g.rectMin.x + g.rectMax.x) / 2.0,
                           (g.rectMin.y + g.rectMax.y) / 2.0};
        // (d) mixed numbering: some pins labelled with numbers and others with letters
        // (classified by the label's first character) — one warning for the connector.
        bool anyNumPin = false, anyLetterPin = false;
        for (const QString& pl : spec.pins) {
            if (pl.isEmpty()) continue;
            const QChar c0 = pl.at(0);
            if (c0.isDigit()) anyNumPin = true;
            else if (c0.isLetter()) anyLetterPin = true;
        }
        if (ruleEnabled("mixedpins") && anyNumPin && anyLetterPin)
            issues.push_back({QStringLiteral("mixedpins:%1").arg(e.id),
                              QStringLiteral("Connector %1 mixes numeric and letter pin labels")
                                  .arg(name),
                              e.id, bodyMid});
        // (e) missing reference designator.
        if (ruleEnabled("norefdes") && spec.designator.isEmpty())
            issues.push_back({QStringLiteral("norefdes:%1").arg(e.id),
                              QStringLiteral("Connector %1 has no reference designator")
                                  .arg(name),
                              e.id, g.designatorPos});
        // (f) missing part number.
        if (ruleEnabled("nopn") && spec.partNumber.isEmpty())
            issues.push_back({QStringLiteral("nopn:%1").arg(e.id),
                              QStringLiteral("Connector %1 has no part number (P/N)").arg(name),
                              e.id, g.partPos});
        // (b) duplicate pin labels within this connector.
        if (ruleEnabled("dup"))
            for (int i = 0; i < spec.pins.size(); ++i)
                for (int j = i + 1; j < spec.pins.size(); ++j)
                    if (!spec.pins[i].isEmpty() && spec.pins[i] == spec.pins[j]) {
                        const Vec2 at = j < static_cast<int>(g.pinEnd.size()) ? g.pinEnd[j] : e.a;
                        issues.push_back(
                            {QStringLiteral("dup:%1:%2:%3").arg(e.id).arg(i).arg(j),
                             QStringLiteral("Connector %1: pins %2 and %3 share the label \"%4\"")
                                 .arg(name).arg(i + 1).arg(j + 1).arg(spec.pins[i]),
                             e.id, at});
                    }
        // (a) each pin connected to a wire or terminated by an X.
        if (ruleEnabled("nc"))
            for (int i = 0; i < spec.pinCount && i < static_cast<int>(g.pinEnd.size()); ++i) {
                const Vec2 pe = g.pinEnd[i];
                if (!nearAny(pe, wireEnds) && !nearAny(pe, ncMarks)) {
                    const QString lbl =
                        i < spec.pins.size() ? spec.pins[i] : QString::number(i + 1);
                    issues.push_back({QStringLiteral("nc:%1:%2").arg(e.id).arg(i),
                                      QStringLiteral("Connector %1 pin %2 is not connected "
                                                     "(no wire and no X marker)")
                                          .arg(name, lbl),
                                      e.id, pe});
                }
            }
    }
    // (c) UNCONNECTED wire ends: an endpoint that is neither connected to anything (pin,
    // splice, shield drain, another wire) nor terminated by an X. An X-terminated end is
    // deliberately OPEN, which is a finished state — it is not reported.
    std::vector<Vec2> pinEnds = collectPinEnds(doc_);
    // A shield's drain point is a real connection target (a wire may run from it), so an
    // end landing there is connected, not dangling.
    std::vector<Vec2> shieldDrains;
    for (const auto& e : doc_->entities()) {
        if (e.type != EntityType::Screen) continue;
        const int k = std::min(e.meta.value("catch").toInt(static_cast<int>(e.points.size())),
                               static_cast<int>(e.points.size()));
        if (static_cast<int>(e.points.size()) > k) shieldDrains.push_back(e.points.back());
    }
    for (const auto& w : doc_->entities()) {
        if (!ruleEnabled("openwire")) break;
        if (w.type != EntityType::Wire || w.points.size() < 2) continue;
        for (int end = 0; end < 2; ++end) {
            const Vec2 p = end == 0 ? w.points.front() : w.points.back();
            int shares = 0; // how many wire endpoints sit here (this one + any others)
            for (const Vec2& q : wireEnds) if (distance(p, q) < tol) ++shares;
            const bool connected = nearAny(p, pinEnds) || nearAny(p, splices) ||
                                   nearAny(p, shieldDrains) || shares > 1;
            const bool terminated = nearAny(p, ncMarks); // an X = deliberately open
            if (!connected && !terminated)
                issues.push_back({QStringLiteral("openwire:%1:%2").arg(w.id).arg(end),
                                  QStringLiteral("Wire #%1 has an unconnected end (no wire, pin, "
                                                 "joint or X termination)")
                                      .arg(w.meta.value("entNum").toInt()),
                                  w.id, p});
        }
    }
    // (g)/(h) Shields and twist signs come in PAIRS: a shielded / twisted run is marked at
    // both ends, so a marker whose wires are covered only ONCE is incomplete (needs a
    // second marker, or none). A single unpaired marker over several wires is reported as
    // ONE discrepancy row listing all the pins/wires it covers (not one row per wire).
    auto pairCheck = [&](EntityType type, const char* rule, const char* what) {
        if (!ruleEnabled(QString::fromLatin1(rule))) return;
        QHash<QString, int> countOnWire; // wire id -> number of markers of this type on it
        for (const auto& e : doc_->entities()) {
            if (e.type != type) continue;
            for (const auto& v : e.meta.value(QStringLiteral("wires")).toArray())
                if (!v.toString().isEmpty()) countOnWire[v.toString()] += 1;
        }
        auto wireNum = [&](const QString& id) {
            for (const auto& w : doc_->entities())
                if (w.id == id) return w.meta.value(QStringLiteral("entNum")).toInt();
            return 0;
        };
        for (const auto& e : doc_->entities()) {
            if (e.type != type) continue;
            // Collect the covered wires that lack a pair (only this marker sits on them).
            QStringList pins;
            for (const auto& v : e.meta.value(QStringLiteral("wires")).toArray()) {
                const QString wid = v.toString();
                if (!wid.isEmpty() && countOnWire.value(wid) == 1)
                    pins << QStringLiteral("#%1").arg(wireNum(wid));
            }
            if (pins.isEmpty()) continue; // fully paired (or none) — fine
            const Vec2 at = e.points.empty() ? e.a : e.points.front();
            issues.push_back(
                {QStringLiteral("%1:%2").arg(QString::fromLatin1(rule), e.id),
                 QStringLiteral("Single %1 on wire(s) %2 — needs a second %1 (one at each end) "
                                "or none")
                     .arg(QString::fromLatin1(what), pins.join(QStringLiteral(", "))),
                 e.id, at});
        }
    };
    pairCheck(EntityType::Screen, "shieldpair", "shield");
    pairCheck(EntityType::Twist, "twistpair", "twist sign");
    // (i) A shield's drain lead must end on a wire, splice, X marker, or pin — otherwise
    // the shield is floating (its drain neither connected nor terminated).
    if (ruleEnabled("shielddrain")) {
        for (const auto& e : doc_->entities()) {
            if (e.type != EntityType::Screen) continue;
            const int k = std::min(e.meta.value("catch").toInt(static_cast<int>(e.points.size())),
                                   static_cast<int>(e.points.size()));
            if (static_cast<int>(e.points.size()) <= k) continue; // no drain point
            // A shield carrying its own "Not Connected" X IS terminated — the X is drawn
            // by the shield itself rather than as a separate NotConnected entity, so the
            // coincidence tests below would never see it.
            if (e.meta.value("notConnected").toBool()) continue;
            const Vec2 drain = e.points.back();
            if (nearAny(drain, wireEnds) || nearAny(drain, ncMarks) ||
                nearAny(drain, splices) || nearAny(drain, pinEnds))
                continue;
            issues.push_back({QStringLiteral("shielddrain:%1").arg(e.id),
                              QStringLiteral("Shield #%1 drain is not connected to a wire or "
                                             "terminated")
                                  .arg(e.meta.value("entNum").toInt()),
                              e.id, drain});
        }
    }
    return issues;
}

void Editor::flashHighlight(const Vec2& at) {
    flashPt_ = at;
    flashOn_ = true;
    if (!flashTimer_) {
        flashTimer_ = new QTimer(this);
        flashTimer_->setSingleShot(true);
        connect(flashTimer_, &QTimer::timeout, this, [this] {
            flashOn_ = false;
            emit changed();
        });
    }
    flashTimer_->start(1400);
    emit changed();
}

void Editor::beginAlignRouting() {
    if (!doc_) return;
    setTool(ToolId::Select);
    awaitAlignRef_ = true;
    emit message(QStringLiteral("Align routing: click the reference connector (it stays put)"));
}

void Editor::beginCrossSwitch() {
    if (!doc_) return;
    setTool(ToolId::Wire); // stay in the Wire tool/ribbon (Esc exits)
    awaitCrossSwitch_ = true;
    emit message(QStringLiteral("Cross switch: click a wire crossing to toggle its bridge (hop); "
                                "Esc to finish"));
}

void Editor::toggleWireCrossAt(const Vec2& p) {
    if (!doc_) return;
    // Gather every wire-over-wire crossing (segments that actually cross, not shared
    // endpoints), remembering the two wires and their segment directions.
    struct Cross { Vec2 pt; QString idA, idB; bool aHoriz; };
    std::vector<Cross> crosses;
    auto& ents = doc_->entities();
    for (size_t i = 0; i < ents.size(); ++i) {
        if (ents[i].type != EntityType::Wire || ents[i].points.size() < 2) continue;
        for (size_t j = i + 1; j < ents.size(); ++j) {
            if (ents[j].type != EntityType::Wire || ents[j].points.size() < 2) continue;
            const auto& A = ents[i].points;
            const auto& B = ents[j].points;
            for (size_t a = 1; a < A.size(); ++a)
                for (size_t b = 1; b < B.size(); ++b) {
                    auto x = segmentSegmentIntersection(A[a - 1], A[a], B[b - 1], B[b]);
                    if (!x) continue;
                    // Skip shared endpoints (a real junction, not a crossing).
                    if (distance(*x, A[a - 1]) < 1e-6 || distance(*x, A[a]) < 1e-6 ||
                        distance(*x, B[b - 1]) < 1e-6 || distance(*x, B[b]) < 1e-6)
                        continue;
                    const bool aHoriz = std::abs(A[a].x - A[a - 1].x) >=
                                        std::abs(A[a].y - A[a - 1].y);
                    crosses.push_back({*x, ents[i].id, ents[j].id, aHoriz});
                }
        }
    }
    // Nearest crossing to the click.
    double best = worldTolerance() * 2.5;
    const Cross* hit = nullptr;
    for (const auto& c : crosses) {
        const double d = distance(p, c.pt);
        if (d < best) { best = d; hit = &c; }
    }
    if (!hit) { emit message(QStringLiteral("No wire crossing here")); return; }

    auto hasHop = [&](Entity* w) {
        if (!w) return -1;
        const QJsonArray hops = w->meta.value("hops").toArray();
        for (int k = 0; k < hops.size(); ++k) {
            const QJsonArray pt = hops[k].toArray();
            if (pt.size() == 2 &&
                distance({pt[0].toDouble(), pt[1].toDouble()}, hit->pt) < 1e-6)
                return k;
        }
        return -1;
    };
    Entity* wa = entityById(hit->idA);
    Entity* wb = entityById(hit->idB);
    pushUndo();
    // If either wire already hops here, turn it OFF; else turn it ON for the more
    // horizontal wire (it hops over the vertical one).
    const int ka = hasHop(wa), kb = hasHop(wb);
    if (ka >= 0 || kb >= 0) {
        for (Entity* w : {wa, wb}) {
            const int k = hasHop(w);
            if (k < 0) continue;
            QJsonArray hops = w->meta.value("hops").toArray();
            hops.removeAt(k);
            w->meta["hops"] = hops;
        }
        emit message(QStringLiteral("Crossing set to plain"));
    } else {
        Entity* hop = hit->aHoriz ? wa : wb;
        if (hop) {
            QJsonArray hops = hop->meta.value("hops").toArray();
            hops.append(QJsonArray{hit->pt.x, hit->pt.y});
            hop->meta["hops"] = hops;
        }
        emit message(QStringLiteral("Crossing set to bridge (hop)"));
    }
    doc_->setDirty(true);
    emit changed();
}

void Editor::beginTwist(const QString& type) {
    if (!doc_) return;
    // Use the Select tool (NOT Wire) so the Wire ribbon does not pop open — the twist
    // crossing-line pick is dispatched from twistArmType_ before any tool handling, so
    // it works regardless of the active tool. setTool clears twistArmType_, so arm AFTER.
    setTool(ToolId::Select);
    twistArmType_ = type.isEmpty() ? QStringLiteral("cross") : type;
    lastTwistType_ = twistArmType_; // the chosen type becomes the default for next time
    wireBandHasStart_ = false;
    emit message(QStringLiteral("Twist: draw a crossing line across the wires to twist"));
    emit twistArmChanged();
}

void Editor::beginTwistRemove() {
    if (!doc_) return;
    setTool(ToolId::Select); // clears the other await flags first...
    awaitTwistRemove_ = true; // ...so arm AFTER
    emit message(QStringLiteral("Remove twist: click a twist sign to delete it; Esc to finish"));
}

void Editor::beginScreen() {
    if (!doc_) return;
    setTool(ToolId::Wire); // stay in the Wire tool/ribbon (Esc exits)
    screenArm_ = true;
    screenFrozen_ = false;
    wireBandHasStart_ = false;
    emit message(QStringLiteral("Shield: draw a crossing line across the wires to shield"));
    emit screenArmedChanged();
}

void Editor::freezeScreenPreview() {
    if (tool_ != ToolId::Wire || !screenArm_ || !wireBandHasStart_ || screenFrozen_) return;
    screenFrozen_ = true;
    screenFrozenEnd_ = cursor_;
    emit message(QStringLiteral("Shield preview frozen — edit the fields; click to place"));
    emit screenArmedChanged();
    emit overlayChanged();
}

void Editor::freezeTwistPreview() {
    if (twistArmType_.isEmpty() || !wireBandHasStart_ || twistFrozen_) return;
    twistFrozen_ = true;
    twistFrozenEnd_ = cursor_;
    emit message(
        QStringLiteral("Twist preview frozen — edit the fields, then Enter to place (a click "
                       "goes back to placing)"));
    emit twistArmChanged();
    emit overlayChanged();
}

void Editor::shieldAutoSize(double span, double pinGap, double& rxOut, double& ryOut) const {
    // Radii LOCKED (default): auto-fit the crossed wires — half-height = half-span plus a
    // margin of 30% of the gap between the closest wires (so it clearly clears the first and
    // last wire); the small radius follows the ratio. Radii UNLOCKED: use the typed Small /
    // Large radius verbatim, so editing them (incl. the large radius) shows in the preview.
    const double ratio = std::max(0.2, screenRatio_);
    if (screenRyLocked_) {
        const double margin = 0.30 * std::max(0.0, pinGap);
        ryOut = std::max(span / 2.0 + margin, 12.0);
        rxOut = ryOut / ratio;
    } else {
        ryOut = std::max(1.0, screenRy_);
        rxOut = std::max(1.0, screenRx_);
    }
}

bool Editor::connectorPinsAt(const Vec2& p, std::vector<Vec2>& pinEndsOut, int& anchorOut,
                            double* pitchOut) const {
    if (!doc_) return false;
    const double tol = std::max(worldTolerance(), 1e-3);
    for (const auto& e : doc_->entities()) {
        if (e.type != EntityType::Connector) continue;
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
        const ConnectorGeometry g = connectorGeometry(e.a, spec);
        int anchor = -1;
        for (int i = 0; i < static_cast<int>(g.pinEnd.size()); ++i)
            if (distance(p, g.pinEnd[i]) <= tol) { anchor = i; break; }
        if (anchor < 0) continue;
        pinEndsOut = g.pinEnd;
        anchorOut = anchor;
        if (pitchOut) *pitchOut = spec.pitch;
        return true;
    }
    return false;
}

double Editor::routingClearance() const {
    // The Pitch rule decides the clearance — the same rule that spaces the lanes after a
    // knee. "Custom" is an explicit distance; otherwise it is one pin pitch of the
    // connector this run was pulled from (falling back to the connector default when the
    // run did not start on a connector at all).
    if (wireGapRule_ == QLatin1String("custom")) return std::max(0.0, wireGapCustom_);
    if (wireRoutePitch_ > 0.0) return wireRoutePitch_;
    return std::max(0.0, connectorSpec_.pitch);
}

Vec2 Editor::applyWireClearance(const Vec2& prev, const Vec2& p) const {
    if (!doc_ || !wireClearanceOn_) return p;
    const double clear = routingClearance();
    if (clear <= 0.0) return p;
    const Vec2 d = p - prev;
    if (d.lengthSq() < 1e-12) return p;
    // Landing ON something is a deliberate connection and must never be nudged aside: a
    // point placed on a pin end, a wire end or a junction marker is where the user said the
    // wire goes. Without this the clearance rule pushed the click off the very feature it
    // was aimed at, so a wire could not be run onto a pin or carried on from another wire's
    // end at all.
    {
        const double snapTol = std::max(worldTolerance() * 0.5, 1e-6);
        // On another run's BODY too: that is a T-junction, which is a connection like any other. ON
        // it, mind - a snapped click lands exactly on the run, whereas a run drawn merely NEAR another
        // is the very thing this rule is here to push apart.
        if (wireBodyPointNear(p, kOnPointTol)) return p;
        for (const auto& e : doc_->entities()) {
            switch (e.type) {
            case EntityType::Connector: {
                const ConnectorGeometry g =
                    connectorGeometry(e.a, connectorSpecFromMeta(e.meta));
                for (const Vec2& pe : g.pinEnd)
                    if (distance(p, pe) <= snapTol) return p;
                break;
            }
            case EntityType::Wire:
                if (!e.points.empty() && (distance(p, e.points.front()) <= snapTol ||
                                          distance(p, e.points.back()) <= snapTol))
                    return p;
                break;
            case EntityType::Splice:
            case EntityType::NotConnected:
            case EntityType::NetLabel:
                if (distance(p, e.a) <= snapTol) return p;
                break;
            default:
                break;
            }
        }
    }
    // The run's own axis. Only its LATERAL position is adjusted, so a 90° route stays
    // 90° and the length the user dragged is untouched.
    const bool horiz = std::abs(d.x) >= std::abs(d.y);
    const double lo = horiz ? std::min(prev.x, p.x) : std::min(prev.y, p.y);
    const double hi = horiz ? std::max(prev.x, p.x) : std::max(prev.y, p.y);

    // Lateral positions of the existing runs parallel to this one that actually overlap
    // it lengthwise — those are the neighbours a clearance has to be kept from.
    std::vector<double> lanes;
    for (const auto& w : doc_->entities()) {
        if (w.type != EntityType::Wire || w.points.size() < 2) continue;
        for (size_t i = 1; i < w.points.size(); ++i) {
            const Vec2 a = w.points[i - 1], b = w.points[i];
            const Vec2 wd = b - a;
            if (wd.lengthSq() < 1e-12) continue;
            if ((std::abs(wd.x) >= std::abs(wd.y)) != horiz) continue; // not parallel
            const double wLo = horiz ? std::min(a.x, b.x) : std::min(a.y, b.y);
            const double wHi = horiz ? std::max(a.x, b.x) : std::max(a.y, b.y);
            if (wHi < lo - 1e-9 || wLo > hi + 1e-9) continue; // side by side, no overlap
            lanes.push_back(horiz ? a.y : a.x);
        }
    }
    if (lanes.empty()) return p;

    // Step out of the nearest occupied lane, away from it, until nothing is inside the
    // clearance. The loop is bounded so a dense bundle can never spin here.
    double coord = horiz ? p.y : p.x;
    for (int guard = 0; guard < 64; ++guard) {
        bool clash = false;
        double nearest = 0.0, nearestGap = 0.0;
        for (double lane : lanes) {
            const double gap = std::abs(coord - lane);
            if (gap >= clear - 1e-9) continue;
            if (!clash || gap < nearestGap) { clash = true; nearestGap = gap; nearest = lane; }
        }
        if (!clash) break;
        // Push to whichever side of that neighbour the cursor is already on (ties go out
        // along the direction the user is heading).
        const double side = (coord > nearest) ? 1.0 : (coord < nearest ? -1.0 : 1.0);
        coord = nearest + side * clear;
    }
    return horiz ? Vec2{p.x, coord} : Vec2{coord, p.y};
}

std::vector<Vec2> Editor::fanRoute(const std::vector<Vec2>& route, const Vec2& pinOffset) {
    if (route.size() < 2) return route;
    // Split the pin offset into "across the run" — which becomes this wire's own lane —
    // and "along the run", kept as a plain shift so the wire still starts on its own pin
    // even when the pins are not square to the first segment.
    const Vec2 perp0 = (route[1] - route[0]).normalized().perp();
    const double lateral = dot(pinOffset, perp0);
    const Vec2 alongPart = pinOffset - perp0 * lateral;
    std::vector<Vec2> lane = route;
    if (std::abs(lateral) > 1e-9) {
        // Offsetting parallel (rather than translating) is what pitches the turns: the
        // corners are mitred, so the lanes stay a constant pitch apart through a bend
        // instead of every wire turning at the same point and overlapping afterwards.
        std::vector<Vec2> parallel = offsetPolyline(route, lateral);
        if (parallel.size() == route.size()) lane = std::move(parallel);
    }
    std::vector<Vec2> out;
    out.reserve(lane.size());
    for (const Vec2& pt : lane) out.push_back(pt + alongPart);
    // Land exactly on this wire's own pin end, whatever the mitring produced.
    out.front() = route.front() + pinOffset;
    return out;
}

// How many WIRE ends land on this point. Two is a joint between two conductors; three or more
// is a junction where a bundle comes together and one wire carries on. `except` leaves a wire
// out of the count (its own end is not something it meets).
int Editor::wireEndsAt(const Vec2& at, const QString& except) const {
    if (!doc_) return 0;
    const double tol = std::max(worldTolerance() * 0.5, 1e-6);
    int n = 0;
    for (const auto& w : doc_->entities()) {
        if (w.type != EntityType::Wire || w.points.size() < 2 || w.id == except) continue;
        if (distance(w.points.front(), at) <= tol) ++n;
        if (distance(w.points.back(), at) <= tol) ++n;
    }
    return n;
}

std::optional<Vec2> Editor::wireBodyPointNear(const Vec2& q, double tol, QString* wireId,
                                              const QString& except) const {
    if (!doc_) return std::nullopt;
    std::optional<Vec2> best;
    double bestD = tol;
    for (const auto& w : doc_->entities()) {
        if (w.type != EntityType::Wire || w.points.size() < 2 || w.id == except) continue;
        for (size_t s = 1; s < w.points.size(); ++s) {
            const Vec2 a = w.points[s - 1], b = w.points[s];
            const Vec2 ab = b - a;
            const double len2 = ab.lengthSq();
            if (len2 < 1e-12) continue;
            double t = ((q - a).x * ab.x + (q - a).y * ab.y) / len2;
            t = std::clamp(t, 0.0, 1.0);
            const Vec2 on{a.x + ab.x * t, a.y + ab.y * t};
            const double d = distance(q, on);
            // Its ENDS are a different thing - two wires meeting end to end - and are already
            // handled as such, so only the run between them counts as a body here.
            const double endTol = std::max(tol, 1e-6);
            if (distance(on, w.points.front()) <= endTol || distance(on, w.points.back()) <= endTol)
                continue;
            if (d < bestD) {
                bestD = d;
                best = on;
                if (wireId) *wireId = w.id;
            }
        }
    }
    return best;
}

std::optional<double> Editor::runDirectionAt(const Vec2& q) const {
    if (!doc_) return std::nullopt;
    const double tol = std::max(worldTolerance(), 1e-6);
    std::optional<double> best;
    double bestD = tol;
    auto tryRun = [&](const Vec2& p1, const Vec2& p2) {
        const Vec2 d = p2 - p1;
        if (d.lengthSq() < 1e-12) return;
        const auto c = closestPointOnSegment(q, p1, p2);
        if (c.distance < bestD) {
            bestD = c.distance;
            best = std::atan2(d.y, d.x);
        }
    };
    for (const auto& e : doc_->entities()) {
        switch (e.type) {
        case EntityType::Line:
        case EntityType::DoubleLine:
            tryRun(e.a, e.b);
            break;
        case EntityType::Wire:
        case EntityType::Polyline:
        case EntityType::Spline:
            for (size_t i = 1; i < e.points.size(); ++i) tryRun(e.points[i - 1], e.points[i]);
            break;
        default:
            break;
        }
    }
    return best;
}

QString Editor::runAt(const Vec2& q) const {
    if (!doc_) return {};
    const Entity* best = nullptr;
    double bestD = std::max(worldTolerance(), 1e-6);
    auto tryRun = [&](const Entity& e, const Vec2& p1, const Vec2& p2) {
        const auto c = closestPointOnSegment(q, p1, p2);
        if (c.distance < bestD) { bestD = c.distance; best = &e; }
    };
    for (const auto& e : doc_->entities()) {
        switch (e.type) {
        case EntityType::Line:
            tryRun(e, e.a, e.b);
            break;
        case EntityType::DoubleLine: {
            // Its rails as well as its centreline: a bundle is picked by the lines that are drawn.
            const double gap = e.meta.value("gap").toDouble(10.0);
            const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
            tryRun(e, e.a, e.b);
            tryRun(e, e.a + off, e.b + off);
            tryRun(e, e.a - off, e.b - off);
            break;
        }
        case EntityType::Wire:
        case EntityType::Polyline:
        case EntityType::Spline:
            for (size_t i = 1; i < e.points.size(); ++i) tryRun(e, e.points[i - 1], e.points[i]);
            break;
        default:
            break;
        }
    }
    return best ? best->id : QString();
}

double Editor::runSpanAcross(const Vec2& q, double rot) const {
    if (!doc_) return 0.0;
    // What the mark has to span, taken from ONE decision rather than from whatever happens to be
    // nearby: the objects SELECTED when it is placed, or - nothing being selected - the run under the
    // point. Anything else and a mark on one bundle is sized by another bundle further down the sheet.
    std::vector<const Entity*> source;
    for (const QString& id : selected_)
        for (const auto& e : doc_->entities())
            if (e.id == id) source.push_back(&e);
    if (source.empty()) {
        // The run under the point, whatever kind it is. A double line brings its two rails with it,
        // which is the case this exists for: the mark spans the bundle, not its centreline.
        const Entity* under = nullptr;
        double bestD = std::max(worldTolerance(), 1e-6);
        auto tryRun = [&](const Entity& e, const Vec2& p1, const Vec2& p2) {
            const auto c = closestPointOnSegment(q, p1, p2);
            if (c.distance < bestD) { bestD = c.distance; under = &e; }
        };
        for (const auto& e : doc_->entities()) {
            switch (e.type) {
            case EntityType::Line:
                tryRun(e, e.a, e.b);
                break;
            case EntityType::DoubleLine: {
                const double gap = e.meta.value("gap").toDouble(10.0);
                const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
                tryRun(e, e.a, e.b);
                tryRun(e, e.a + off, e.b + off);
                tryRun(e, e.a - off, e.b - off);
                break;
            }
            case EntityType::Wire:
            case EntityType::Spline:
            case EntityType::Polyline:
                for (size_t i = 1; i < e.points.size(); ++i) tryRun(e, e.points[i - 1], e.points[i]);
                break;
            default:
                break;
            }
        }
        if (!under) return 0.0;
        source.push_back(under);
    }

    // How far those objects reach across a line through q, square to the run.
    const Vec2 across{-std::sin(rot), std::cos(rot)};
    const Vec2 along{std::cos(rot), std::sin(rot)};
    double lo = 0.0, hi = 0.0;
    bool any = false;
    auto consider = [&](const Vec2& p1, const Vec2& p2) {
        // Where this segment is level with q along the run: dot(p1 + t*d - q, along) = 0.
        const Vec2 d = p2 - p1;
        const double denom = d.x * along.x + d.y * along.y;
        if (std::abs(denom) < 1e-12) return;                 // it lies across the run, not along it
        const Vec2 w = q - p1;
        const double t = (w.x * along.x + w.y * along.y) / denom;
        if (t < -1e-9 || t > 1.0 + 1e-9) return;             // level with q outside this segment
        const Vec2 at{p1.x + d.x * t, p1.y + d.y * t};
        const double s = (at - q).x * across.x + (at - q).y * across.y;
        lo = any ? std::min(lo, s) : s;
        hi = any ? std::max(hi, s) : s;
        any = true;
    };
    for (const Entity* e : source) {
        switch (e->type) {
        case EntityType::Line:
            consider(e->a, e->b);
            break;
        case EntityType::DoubleLine: {
            const double gap = e->meta.value("gap").toDouble(10.0);
            const Vec2 off = (e->b - e->a).normalized().perp() * (gap / 2.0);
            consider(e->a + off, e->b + off);
            consider(e->a - off, e->b - off);
            break;
        }
        case EntityType::Wire:
        case EntityType::Spline:
            for (size_t i = 1; i < e->points.size(); ++i) consider(e->points[i - 1], e->points[i]);
            break;
        case EntityType::Polyline: {
            if (e->meta.value("double").toBool() && e->points.size() >= 2) {
                const double gap = e->meta.value("gap").toDouble(10.0);
                for (double s : {gap / 2.0, -gap / 2.0}) {
                    const auto run = offsetPolyline(e->points, s);
                    for (size_t i = 1; i < run.size(); ++i) consider(run[i - 1], run[i]);
                }
            } else {
                for (size_t i = 1; i < e->points.size(); ++i)
                    consider(e->points[i - 1], e->points[i]);
            }
            break;
        }
        default:
            break;
        }
    }
    return any ? (hi - lo) : 0.0;
}

bool Editor::isConnectablePoint(const Vec2& q) const {
    if (!doc_) return false;
    const double tol = std::max(worldTolerance() * 0.5, 1e-6);
    for (const Vec2& pe : collectPinEnds(doc_))
        if (distance(q, pe) <= tol) return true;
    for (const auto& e : doc_->entities()) {
        switch (e.type) {
        case EntityType::Wire:
            if (!e.points.empty() && (distance(q, e.points.front()) <= tol ||
                                      distance(q, e.points.back()) <= tol))
                return true;
            break;
        case EntityType::Splice:
        case EntityType::NotConnected:
        case EntityType::NetLabel:
            if (distance(q, e.a) <= tol) return true;
            break;
        default:
            break;
        }
    }
    return wireBodyPointNear(q, kOnPointTol).has_value();
}

// A joint marker at `at`, unless one is already there. Two wires joined end to end are one
// conductor with a joint in it, and without the dot the drawing cannot be told from one wire
// simply passing over another.
void Editor::addSpliceAt(const Vec2& at) {
    if (!doc_) return;
    const double tol = std::max(worldTolerance() * 0.5, 1e-6);
    for (const auto& e : doc_->entities())
        if (e.type == EntityType::Splice && distance(e.a, at) <= tol) return;
    Entity sp;
    sp.type = EntityType::Splice;
    sp.layer = QStringLiteral("Wires");
    sp.a = at;
    addEntity(std::move(sp));
}

// Two wires whose free ends have been brought together become ONE wire: the runs are joined
// into a single polyline (the shared point kept once, not twice) and a splice marks the joint.
// A tip that already terminates on a pin, a splice or an X is a proper end, so joining through
// one - which would quietly fuse two separate conductors - is left alone.
bool Editor::mergeWireEnds(const QString& wireId) {
    if (!doc_) return false;
    Entity* w = entityById(wireId);
    if (!w || w->type != EntityType::Wire || w->points.size() < 2) return false;
    const double tol = std::max(worldTolerance() * 0.5, 1e-6);
    const std::vector<Vec2> pinEnds = collectPinEnds(doc_);
    auto terminated = [&](const Vec2& pt) {
        for (const Vec2& pe : pinEnds)
            if (distance(pt, pe) < 1e-6) return true;
        for (const auto& m : doc_->entities())
            if ((m.type == EntityType::Splice || m.type == EntityType::NotConnected) &&
                distance(m.a, pt) <= tol)
                return true;
        return false;
    };
    // Three or more ends at one point is a junction: it gets a splice, and every wire keeps
    // its own identity through it. Only a plain TWO-end meeting is a continuation that can be
    // drawn as one run.
    for (int mine = 0; mine < 2; ++mine) {
        const Vec2 myTip = mine ? w->points.back() : w->points.front();
        if (terminated(myTip)) continue;
        const int met = wireEndsAt(myTip, w->id);
        if (met >= 2) {
            pushUndo();
            addSpliceAt(myTip); // ...after which `w` must not be read again
            doc_->setDirty(true);
            emit message(QStringLiteral("Jointed — %1 wires meet here").arg(met + 1));
            emit changed();
            return true;
        }
    }
    for (const auto& other : doc_->entities()) {
        if (other.id == w->id || other.type != EntityType::Wire || other.points.size() < 2)
            continue;
        for (int mine = 0; mine < 2; ++mine) {
            const Vec2 myTip = mine ? w->points.back() : w->points.front();
            if (terminated(myTip)) continue;
            for (int its = 0; its < 2; ++its) {
                const Vec2 itsTip = its ? other.points.back() : other.points.front();
                if (distance(myTip, itsTip) > tol) continue;
                if (terminated(itsTip)) continue;
                pushUndo();
                // Lay both runs out so the joint is in the middle: my joining tip last, its
                // joining tip first. The joint point itself is kept once.
                std::vector<Vec2> merged = w->points;
                if (!mine) std::reverse(merged.begin(), merged.end());
                std::vector<Vec2> add = other.points;
                if (its) std::reverse(add.begin(), add.end());
                merged.back() = myTip; // exactly on the joint, not a hair off it
                for (size_t i = 1; i < add.size(); ++i) merged.push_back(add[i]);
                const QString goneId = other.id;
                w->points = merged;
                // Everything wanted from the wire is taken NOW: erasing the absorbed wire and
                // adding the splice both reallocate the document's entity vector, which leaves
                // `w` (and `other`) dangling.
                const int mergedCount = static_cast<int>(merged.size());
                auto& es = doc_->entities();
                es.erase(std::remove_if(es.begin(), es.end(),
                                        [&goneId](const Entity& x) { return x.id == goneId; }),
                         es.end());
                addSpliceAt(myTip);
                selected_ = {wireId};
                doc_->setDirty(true);
                emit message(QStringLiteral("Wires joined — one wire of %1 points, jointed at "
                                          "the joint")
                                 .arg(mergedCount));
                emit selectionChanged();
                emit changed();
                return true;
            }
        }
    }
    return false;
}

// ---- Shield drain <-> wire ---------------------------------------------------
// Record what the drain leg has been dropped on. Landing on a wire END is a joint the shield
// can PULL: the wire keeps that end on the leg, so sliding the shield along the harness
// stretches the wire instead of tearing off it. Landing mid-run is a connection too, but there
// is no end to pull, so the leg just rides on the wire.
bool Editor::attachShieldLeg(Entity& screen) {
    if (!doc_ || screen.type != EntityType::Screen) return false;
    const int k = std::min(screen.meta.value("catch").toInt(
                               static_cast<int>(screen.points.size())),
                           static_cast<int>(screen.points.size()));
    if (static_cast<int>(screen.points.size()) <= k || k < 1) return false;
    const Vec2 drain = screen.points.back();
    const double tol = std::max(worldTolerance(), 1.0);
    for (auto& w : doc_->entities()) {
        if (w.type != EntityType::Wire || w.points.size() < 2 || w.id == screen.id) continue;
        for (int end = 0; end < 2; ++end) {
            Vec2& tip = end ? w.points.back() : w.points.front();
            if (distance(tip, drain) > tol) continue;
            tip = drain; // snap exactly, so it reads as connected and stays that way
            screen.meta.insert(QStringLiteral("legWireId"), w.id);
            screen.meta.insert(QStringLiteral("legWireEnd"), end);
            screen.meta.insert(QStringLiteral("notConnected"), false);
            return true;
        }
    }
    for (const auto& w : doc_->entities()) {
        if (w.type != EntityType::Wire || w.points.size() < 2) continue;
        for (size_t i = 1; i < w.points.size(); ++i)
            if (closestPointOnSegment(drain, w.points[i - 1], w.points[i]).distance <= tol) {
                screen.meta.insert(QStringLiteral("legWireId"), w.id);
                screen.meta.remove(QStringLiteral("legWireEnd"));
                screen.meta.insert(QStringLiteral("notConnected"), false);
                return true;
            }
    }
    screen.meta.remove(QStringLiteral("legWireId"));
    screen.meta.remove(QStringLiteral("legWireEnd"));
    return false;
}

// The drain has moved: bring the wire end joined to it along, which lengthens or shortens that
// wire. Without this, sliding a shield would leave its drain wire behind.
void Editor::pullShieldLegWire(Entity& screen) {
    if (!doc_ || screen.type != EntityType::Screen) return;
    const QString wid = screen.meta.value(QStringLiteral("legWireId")).toString();
    if (wid.isEmpty() || !screen.meta.contains(QStringLiteral("legWireEnd"))) return;
    Entity* w = entityById(wid);
    if (!w || w->type != EntityType::Wire || w->points.size() < 2) return;
    const int k = std::min(screen.meta.value("catch").toInt(
                               static_cast<int>(screen.points.size())),
                           static_cast<int>(screen.points.size()));
    if (static_cast<int>(screen.points.size()) <= k) return;
    const Vec2 drain = screen.points.back();
    if (screen.meta.value(QStringLiteral("legWireEnd")).toInt() == 0) w->points.front() = drain;
    else w->points.back() = drain;
}

bool Editor::extendExistingWire(const std::vector<Vec2>& pts) {
    if (!doc_ || pts.size() < 2) return false;
    // The run has to genuinely touch the end, not merely pass near it, or wires drawn
    // alongside each other would silently fuse.
    const double tol = std::max(worldTolerance() * 0.5, 1e-6);
    const std::vector<Vec2> pinEnds = collectPinEnds(doc_);
    auto terminated = [&](const Vec2& p) {
        // A pin end or a splice/X marker is a real termination: joining THROUGH one would
        // quietly turn two conductors into a single wire.
        for (const Vec2& pe : pinEnds)
            if (distance(p, pe) < 1e-6) return true;
        for (const auto& m : doc_->entities())
            if ((m.type == EntityType::Splice || m.type == EntityType::NotConnected) &&
                distance(m.a, p) <= tol)
                return true;
        return false;
    };
    for (auto& w : doc_->entities()) {
        if (w.type != EntityType::Wire || w.points.size() < 2) continue;
        for (int end = 0; end < 2; ++end) {
            const bool atBack = (end == 0);
            const Vec2 tip = atBack ? w.points.back() : w.points.front();
            if (terminated(tip)) continue;
            // A point where TWO OR MORE wires already end is a junction, not a loose tip: the
            // wires that meet there are joined by a splice and the run carrying on past it is a
            // wire of its own. Merging into whichever of them happened to be found first would
            // make one of the conductors continue through the junction and leave the others
            // hanging off it.
            if (wireEndsAt(tip) >= 2) continue;
            // The run may have been drawn away from the tip, or toward it.
            std::vector<Vec2> add;
            if (distance(pts.front(), tip) <= tol) {
                add.assign(pts.begin() + 1, pts.end());
            } else if (distance(pts.back(), tip) <= tol) {
                add.assign(pts.rbegin() + 1, pts.rend());
            } else {
                continue;
            }
            if (add.empty()) continue;
            pushUndo();
            if (atBack) {
                for (const Vec2& p : add) w.points.push_back(p);
            } else {
                // Inserting each point at the front reverses them as it goes, which is what
                // makes the joined run read continuously from its new far end.
                for (const Vec2& p : add) w.points.insert(w.points.begin(), p);
            }
            // No splice here: this run was drawn ON from the wire's own free tip, so it is one
            // conductor carrying on, not two separate wires meeting. A splice is for a JOINT -
            // see mergeWireEnds, where two wires that already existed are brought together.
            selected_ = {w.id};
            doc_->setDirty(true);
            emit message(QStringLiteral("Wire extended — now one wire of %1 points")
                             .arg(w.points.size()));
            emit selectionChanged();
            return true;
        }
    }
    return false;
}

void Editor::rebuildWireFanOffsets() {
    // Turn the selected pin indices into per-wire offsets from the anchor pin.
    if (wireFanAnchor_ < 0 || wireFanPins_.empty()) return;
    std::sort(wireFanSel_.begin(), wireFanSel_.end());
    wireFanSel_.erase(std::unique(wireFanSel_.begin(), wireFanSel_.end()), wireFanSel_.end());
    const Vec2 anchorPin = wireFanPins_[wireFanAnchor_];
    wireArrayOffsets_.clear();
    for (int i : wireFanSel_)
        if (i >= 0 && i < static_cast<int>(wireFanPins_.size()))
            wireArrayOffsets_.push_back(wireFanPins_[i] - anchorPin);
    wireArray_ = wireArrayOffsets_.size() > 1;
    const int count = static_cast<int>(wireArrayOffsets_.size());
    emit message(count > 1
                     ? QStringLiteral("Wiring %1 pins — click the route; Esc to finish").arg(count)
                     : QStringLiteral("Single wire — Shift+<pin no.> adds/removes that pin"));
    emit overlayChanged();
}

void Editor::toggleWireFanPin(int pinNumber) {
    if (wireFanAnchor_ < 0 || wireFanPins_.empty()) return;
    const int idx = pinNumber - 1; // pin numbers are 1-based (the displayed number)
    if (idx < 0 || idx >= static_cast<int>(wireFanPins_.size())) return;
    if (idx == wireFanAnchor_) return; // the drawn wire's own pin is always kept
    auto it = std::find(wireFanSel_.begin(), wireFanSel_.end(), idx);
    if (it != wireFanSel_.end()) wireFanSel_.erase(it); // remove that pin's wire
    else wireFanSel_.push_back(idx);                    // add that pin's wire
    rebuildWireFanOffsets();
}

void Editor::setWireFanAll() {
    if (wireFanAnchor_ < 0 || wireFanPins_.empty()) return;
    wireFanSel_.clear();
    for (int i = 0; i < static_cast<int>(wireFanPins_.size()); ++i) wireFanSel_.push_back(i);
    rebuildWireFanOffsets();
}

double Editor::twistMarkSpacing(double band) const {
    band = std::max(1.0, band);
    const bool crossed = twistArmType_ == QLatin1String("crossed");
    const int reps = std::max(1, twistRepeats_);
    const double flat = (twistArmType_ == QLatin1String("jog")) ? twistFlatLen_ : 0.0;
    // One mark's own drawn width (mirrors twistBandX for braids; a fixed estimate for crossed).
    const double markW = crossed ? std::max(28.0, band * 1.2)
                                 : std::max(28.0, reps * (band * 0.6 + flat) + 16.0);
    return markW + std::max(24.0, band * 0.7); // + a clear gap so marks never touch
}

void Editor::finishWireBand(const Vec2& fenceEnd) {
    const Vec2 f0 = wireBandStart_, f1 = fenceEnd;
    std::vector<Vec2> crossings;
    std::vector<QString> wireIds;
    std::vector<Vec2> segDirs; // direction of each crossed wire segment (for the shield angle)
    for (const auto& w : doc_->entities()) {
        if (w.type != EntityType::Wire || w.points.size() < 2) continue;
        for (size_t i = 1; i < w.points.size(); ++i)
            if (auto x = segmentSegmentIntersection(f0, f1, w.points[i - 1], w.points[i])) {
                crossings.push_back(*x);
                wireIds.push_back(w.id);
                segDirs.push_back(w.points[i] - w.points[i - 1]);
                break; // one crossing per wire
            }
    }
    const bool twist = !twistArmType_.isEmpty();
    if (crossings.size() < (twist ? 2u : 1u)) {
        emit message(QStringLiteral("Draw the line across the wires you want to %1")
                         .arg(twist ? QStringLiteral("twist") : QStringLiteral("screen")));
        twistArmType_.clear();
        twistFrozen_ = false;
        screenArm_ = false;
        screenFrozen_ = false;
        emit overlayChanged();
        emit screenArmedChanged();
        emit twistArmChanged();
        return;
    }
    pushUndo();
    if (twist) {
        QJsonArray ids;
        for (const QString& id : wireIds) ids.append(id);
        // The braid is drawn at the crossed wire's thickness, so it reads as part of the
        // wire — capture that wire's gauge and stamp it on every mark (incl. decorative copies).
        QString twistGauge;
        for (const auto& w : doc_->entities())
            if (w.id == wireIds.front()) { twistGauge = w.meta.value("wireGauge").toString(); break; }
        // Place `amount` twist marks side by side (shifted along the wires); the first
        // carries the caught-wire references, the copies are decorative like the shield's.
        const int amount = std::clamp(twistAmount_, 1, 9);
        double xMin = 1e18, xMax = -1e18, yMin = 1e18, yMax = -1e18;
        for (const Vec2& c : crossings) {
            xMin = std::min(xMin, c.x); xMax = std::max(xMax, c.x);
            yMin = std::min(yMin, c.y); yMax = std::max(yMax, c.y);
        }
        const double band = std::max(1.0, yMax - yMin);
        // "crossed" overlays a line and leaves the wires continuous, so only the first mark
        // needs the wire references (for validation); the copies are decorative. Type 2/3
        // braids REPLACE the wire within their band — so EVERY copy must reference the wires
        // it crosses, so the wire stops and restarts at each braid's ends, not just the first.
        const bool crossedType = twistArmType_ == QLatin1String("crossed");
        // Spacing = one mark's own width + a clear gap, so consecutive marks never touch
        // (wider braids — more repeats / longer flats — get proportionally more room).
        const double spacing = twistMarkSpacing(band);
        for (int k = 0; k < amount; ++k) {
            Entity e;
            e.type = EntityType::Twist;
            e.layer = QStringLiteral("Wires");
            e.points.reserve(crossings.size());
            for (const Vec2& c : crossings) e.points.push_back({c.x + k * spacing, c.y});
            e.meta.insert("twistType", twistArmType_);
            e.meta.insert("wires", (k == 0 || !crossedType) ? ids : QJsonArray());
            e.meta.insert("wireGauge", twistGauge); // draw at the wire's thickness
            if (twistArmType_ == QLatin1String("crossed")) {
                e.meta.insert("diagAngle", twistDiagAngle_);
                e.meta.insert("capLen", twistCapLen_);
            } else { // Type 2 (jog) / Type 3 braid
                e.meta.insert("repeats", twistRepeats_);
                if (twistArmType_ != QLatin1String("type3"))
                    e.meta.insert("flatLen", twistFlatLen_);
            }
            e.meta.insert("entNum", assignEntityNumber());
            addEntity(std::move(e)); // assigns a unique id (a raw push_back left it empty)
        }
        twistArmType_.clear();
        twistFrozen_ = false;
        emit twistArmChanged();
        emit message(QStringLiteral("Twisted %1 wires").arg(crossings.size()));
    } else {
        // Shield: an ellipse catching the crossed wires (a joint on each), plus a drain
        // lead exiting the chosen side of the ellipse to a terminable endpoint.
        // The shield's major axis is PERPENDICULAR to the wires (so it crosses them at 90°
        // regardless of how the crossing line was slanted); centre + span come from the
        // OUTERMOST caught wires along that axis so it always spans every wire.
        const double rot = bundlePerpRot(segDirs);
        const Vec2 major{std::cos(rot), std::sin(rot)};
        const Vec2 minor{-std::sin(rot), std::cos(rot)};
        Vec2 center;
        double span;
        shieldExtent(crossings, major, center, span);
        const double cx = center.x, cyMid = center.y;
        double rxActual, ryActual;
        shieldAutoSize(span, shieldWireGap(crossings, major), rxActual, ryActual);
        Vec2 drain;
        if (screenLegDir_ == QLatin1String("top"))
            drain = {cx + (ryActual + screenLeg_) * major.x, cyMid + (ryActual + screenLeg_) * major.y};
        else if (screenLegDir_ == QLatin1String("left"))
            drain = {cx + (rxActual + screenLeg_) * minor.x, cyMid + (rxActual + screenLeg_) * minor.y};
        else if (screenLegDir_ == QLatin1String("right"))
            drain = {cx - (rxActual + screenLeg_) * minor.x, cyMid - (rxActual + screenLeg_) * minor.y};
        else // "bottom" (default)
            drain = {cx - (ryActual + screenLeg_) * major.x, cyMid - (ryActual + screenLeg_) * major.y};
        QJsonArray ids;
        for (const QString& id : wireIds) ids.append(id);
        // Place `amount` shields side by side (task): the first catches the real wires;
        // extra copies are shifted along the minor axis by an ellipse width + a gap.
        const int amount = std::clamp(screenAmount_, 1, 5);
        const double spacing = 3.0 * rxActual; // ellipse width + one-radius gap
        for (int k = 0; k < amount; ++k) {
            const Vec2 shift{k * spacing * minor.x, k * spacing * minor.y};
            Entity e;
            e.type = EntityType::Screen;
            e.layer = QStringLiteral("Wires");
            e.points.reserve(crossings.size() + 1);
            for (const Vec2& c : crossings) e.points.push_back({c.x + shift.x, c.y + shift.y});
            e.points.push_back({drain.x + shift.x, drain.y + shift.y});
            e.meta.insert("catch", static_cast<int>(crossings.size()));
            e.meta.insert("wires", k == 0 ? ids : QJsonArray()); // only the first truly catches
            e.meta.insert("rx", rxActual);
            e.meta.insert("ry", ryActual); // store the exact size so render matches placement
            e.meta.insert("rot", rot);     // perpendicular-to-wires angle (render/grips reuse it)
            e.meta.insert("shape", screenShape_);
            e.meta.insert("legDir", screenLegDir_);
            e.meta.insert("showS", screenShowS_);
            e.meta.insert("sSize", screenSSize_);
            e.meta.insert("notConnected", screenNotConnected_);
            e.meta.insert("ncSize", screenNcSize_); // leg-end X half-size (default 3)
            if (!screenColor_.isEmpty()) e.color = screenColor_; // else ByLayer
            e.meta.insert("lineType", screenLineType_);
            e.meta.insert("dashScale", screenDashScale_);
            e.meta.insert("entNum", assignEntityNumber());
            addEntity(std::move(e)); // assigns a unique id (a raw push_back left it empty)
        }
        screenArm_ = false;
        screenFrozen_ = false;
        emit message(amount > 1
                         ? QStringLiteral("Placed %1 shields").arg(amount)
                         : QStringLiteral("Shield added over %1 wires").arg(wireIds.size()));
        emit screenArmedChanged();
        doc_->setDirty(true);
        emit changed();
        // Finished placing: return to the Select tool (region select). setTool(Select)
        // drops the Wire ribbon, which sends the tab back to Schematic build.
        setTool(ToolId::Select);
        return;
    }
    doc_->setDirty(true);
    emit changed();
}

void Editor::alignRoutingFrom(const QString& refConnectorId) {
    awaitAlignRef_ = false;
    Entity* ref = entityById(refConnectorId);
    if (!ref || ref->type != EntityType::Connector) {
        emit message(QStringLiteral("Align routing: pick a connector as the reference"));
        return;
    }
    const ConnectorGeometry rg = connectorGeometry(ref->a, connectorSpecFromMeta(ref->meta));
    // A link = a wire connecting a reference pin end to another connector's pin end.
    struct Link { QString wireId; bool refIsFirst; Vec2 refPin; QString otherId; Vec2 otherPin; };
    auto connectorPinAt = [&](const Vec2& p, const QString& skip, Vec2& pinOut) -> QString {
        for (const auto& c : doc_->entities()) {
            if (c.type != EntityType::Connector || c.id == skip) continue;
            const ConnectorGeometry cg = connectorGeometry(c.a, connectorSpecFromMeta(c.meta));
            for (const Vec2& cp : cg.pinEnd)
                if (distance(p, cp) < 1e-6) { pinOut = cp; return c.id; }
        }
        return {};
    };
    std::vector<Link> links;
    for (const auto& w : doc_->entities()) {
        if (w.type != EntityType::Wire || w.points.size() < 2) continue;
        for (bool first : {true, false}) {
            const Vec2 end = first ? w.points.front() : w.points.back();
            const Vec2 other = first ? w.points.back() : w.points.front();
            bool onRef = false;
            Vec2 refPin;
            for (const Vec2& rp : rg.pinEnd)
                if (distance(end, rp) < 1e-6) { onRef = true; refPin = rp; break; }
            if (!onRef) continue;
            Vec2 otherPin;
            const QString oid = connectorPinAt(other, refConnectorId, otherPin);
            if (!oid.isEmpty()) links.push_back({w.id, first, refPin, oid, otherPin});
            break; // one end matched; don't double-count
        }
    }
    if (links.empty()) {
        emit message(QStringLiteral("Align routing: no connectors are wired to that one"));
        return;
    }
    pushUndo();
    const ConnectorSpec refSpec = connectorSpecFromMeta(ref->meta);
    int straightened = 0, routed = 0;
    // Process each connected connector once. SMART DECISION: if the two connectors
    // face each other, are wired 1:1 in order (every wire needs the SAME small vertical
    // shift), and that shift is modest, MOVE the other connector so the wires become
    // straight (0°). Otherwise leave BOTH connectors put and re-route each wire with
    // right-angle (0°/90°) bends.
    std::vector<QString> done;
    for (const Link& seed : links) {
        if (std::find(done.begin(), done.end(), seed.otherId) != done.end()) continue;
        done.push_back(seed.otherId);
        std::vector<Link> group;
        for (const Link& l : links)
            if (l.otherId == seed.otherId) group.push_back(l);
        Entity* oc = entityById(seed.otherId);
        if (!oc) continue;
        const ConnectorSpec ocSpec = connectorSpecFromMeta(oc->meta);
        const ConnectorGeometry og = connectorGeometry(oc->a, ocSpec);
        const double refCx = (rg.rectMin.x + rg.rectMax.x) / 2.0;
        const double ocCx = (og.rectMin.x + og.rectMax.x) / 2.0;
        const double rdir = refSpec.mirrorX ? -1.0 : 1.0; // ref pins exit +x when not mirrored
        const double odir = ocSpec.mirrorX ? -1.0 : 1.0;
        const bool facing = rdir * (ocCx - refCx) > 0 && odir * (refCx - ocCx) > 0;
        // Every wire needs the same vertical shift to become straight?
        const double dy0 = group.front().refPin.y - group.front().otherPin.y;
        bool sameDy = true;
        for (const Link& l : group)
            if (std::abs((l.refPin.y - l.otherPin.y) - dy0) > 1e-6) { sameDy = false; break; }
        // Are the connecting wires straight (no knees) already? If so, MOVE the other
        // connector to line the wires up (any offset). If any wire is already bent
        // (has knees), leave the connector put and re-route the wires at 90° instead.
        bool anyKnees = false;
        for (const Link& l : group)
            if (const Entity* w = entityById(l.wireId); w && w->points.size() > 2) {
                anyKnees = true;
                break;
            }
        const bool straighten = !anyKnees && facing && sameDy;

        if (straighten) {
            const Vec2 delta{0, dy0};
            dragAttachedWires(*oc, delta, {}); // carry its wires along
            translateEntity(*oc, delta);
            for (const Link& l : group)
                if (Entity* w = entityById(l.wireId)) {
                    const Vec2 mo = l.otherPin + delta;
                    w->points = l.refIsFirst ? std::vector<Vec2>{l.refPin, mo}
                                             : std::vector<Vec2>{mo, l.refPin};
                }
            ++straightened;
        } else {
            // Orthogonal (Manhattan) re-route, connectors stay put. Each wire exits the
            // ref pin horizontally to its own vertical lane, drops/rises, then runs into
            // the other pin. The lane ORDER is chosen to minimise crossings: we try a few
            // candidate orderings and keep the one with the fewest segment intersections.
            const int k = static_cast<int>(group.size());
            const double toward = (group.front().otherPin.x >= group.front().refPin.x) ? 1.0 : -1.0;
            // Gap between the parallel wire lanes after the knee: the reference pin pitch
            // (uniform with the vertical gap) or a user-set custom value (Wire ribbon).
            const double laneGap = wireGapRule_ == QLatin1String("custom")
                                       ? std::max(1.0, wireGapCustom_)
                                       : std::max(4.0, refSpec.pitch);
            const double baseStub = std::max(refSpec.pinLength, laneGap);
            // Build the H-V-H route for a given lane assignment (lanePos[g] = 0..k-1).
            auto buildRoutes = [&](const std::vector<int>& lanePos) {
                std::vector<std::vector<Vec2>> routes(k);
                for (int gi = 0; gi < k; ++gi) {
                    const Link& l = group[gi];
                    const double laneX = l.refPin.x + toward * (baseStub + lanePos[gi] * laneGap);
                    routes[gi] = {l.refPin, {laneX, l.refPin.y}, {laneX, l.otherPin.y}, l.otherPin};
                }
                return routes;
            };
            auto crossings = [&](const std::vector<std::vector<Vec2>>& rs) {
                int c = 0;
                for (int a = 0; a < k; ++a)
                    for (int b = a + 1; b < k; ++b)
                        for (size_t i = 1; i < rs[a].size(); ++i)
                            for (size_t j = 1; j < rs[b].size(); ++j)
                                if (segmentSegmentIntersection(rs[a][i - 1], rs[a][i],
                                                               rs[b][j - 1], rs[b][j]))
                                    ++c;
                return c;
            };
            // Candidate lane orders: by source Y and by destination Y (both directions).
            std::vector<int> idx(k);
            for (int i = 0; i < k; ++i) idx[i] = i;
            auto orderBy = [&](auto key, bool asc) {
                std::vector<int> o = idx;
                std::sort(o.begin(), o.end(), [&](int a, int b) {
                    return asc ? key(group[a]) < key(group[b]) : key(group[a]) > key(group[b]);
                });
                std::vector<int> lanePos(k);
                for (int pos = 0; pos < k; ++pos) lanePos[o[pos]] = pos;
                return lanePos;
            };
            auto srcY = [](const Link& l) { return l.refPin.y; };
            auto dstY = [](const Link& l) { return l.otherPin.y; };
            std::vector<std::vector<int>> candidates = {
                orderBy(srcY, true), orderBy(srcY, false), orderBy(dstY, true), orderBy(dstY, false)};
            std::vector<std::vector<Vec2>> best;
            int bestC = std::numeric_limits<int>::max();
            for (const auto& lp : candidates) {
                auto rs = buildRoutes(lp);
                const int c = crossings(rs);
                if (c < bestC) { bestC = c; best = rs; }
            }
            for (int gi = 0; gi < k; ++gi) {
                std::vector<Vec2> pts = best[gi];
                if (!group[gi].refIsFirst) std::reverse(pts.begin(), pts.end());
                if (Entity* w = entityById(group[gi].wireId)) w->points = pts;
            }
            ++routed;
        }
    }
    doc_->setDirty(true);
    emit message(straightened && routed
                     ? QStringLiteral("Routing aligned (%1 straightened, %2 re-routed at 90°)")
                           .arg(straightened).arg(routed)
                 : routed ? QStringLiteral("Wires re-routed at 90° (connectors kept in place)")
                          : QStringLiteral("Routing straightened to the reference connector"));
    emit selectionChanged();
    emit changed();
}

// The strokes of a break mark, in world units: end and end for each. `curved` marks are approximated
// by the chord of each S - close enough for deciding what is hidden, and convex, which the clipping
// below needs.
std::vector<Vec2> Editor::breakMaskQuad(const Entity& mark) const {
    if (mark.type != EntityType::LengthBreak) return {};
    if (!mark.meta.value(QStringLiteral("mask")).toBool(true)) return {};
    const double half = std::max(0.5, mark.meta.value(QStringLiteral("size")).toDouble(6.0));
    const double gap = mark.meta.value(QStringLiteral("gap")).toDouble(half * 0.9);
    // Where the mark is DRAWN, which for one attached to a run is wherever that run has taken it.
    Vec2 at = mark.a;
    double rot = mark.rotation;
    if (const QString hostId = mark.meta.value(QStringLiteral("host")).toString();
        !hostId.isEmpty() && doc_)
        for (const auto& h : doc_->entities())
            if (h.id == hostId) {
                Vec2 pos, dir;
                if (runPointAtFraction(h, mark.meta.value(QStringLiteral("annT")).toDouble(0.5), pos,
                                       dir)) {
                    at = pos;
                    rot = std::atan2(dir.y, dir.x);
                }
            }
    const Vec2 along{std::cos(rot), std::sin(rot)};
    const Vec2 across{-std::sin(rot), std::cos(rot)};
    const bool curved =
        mark.meta.value(QStringLiteral("breakStyle")).toString() == QLatin1String("curved");
    const double lean = half * (curved ? 0.0 : breakLeanFactor(mark));
    std::vector<Vec2> quad;
    for (int k = 0; k < 2; ++k) {
        const Vec2 mid = at + along * (gap * (k == 0 ? -0.5 : 0.5));
        const Vec2 e1 = mid + across * half + along * lean;
        const Vec2 e2 = mid - across * half - along * lean;
        if (k == 0) { quad.push_back(e1); quad.push_back(e2); }
        else { quad.push_back(e2); quad.push_back(e1); }   // round the edge, not across it
    }
    return quad;
}

bool Editor::pointHiddenByBreak(const Entity& host, const Vec2& p) const {
    if (!doc_ || host.id.isEmpty() || host.type == EntityType::LengthBreak) return false;
    for (const auto& m : doc_->entities()) {
        if (m.type != EntityType::LengthBreak) continue;
        const std::vector<Vec2> quad = breakMaskQuad(m);
        if (quad.size() != 4) continue;
        // Inside the quad: on the inner side of all four edges.
        Vec2 centre{0, 0};
        for (const Vec2& c : quad) centre = centre + c;
        centre = {centre.x / 4.0, centre.y / 4.0};
        bool inside = true;
        for (size_t i = 0; i < 4 && inside; ++i) {
            const Vec2 p1 = quad[i], p2 = quad[(i + 1) % 4];
            const Vec2 edge = p2 - p1;
            Vec2 n{-edge.y, edge.x};
            if ((centre - p1).x * n.x + (centre - p1).y * n.y < 0.0) n = n * -1.0;
            if ((p - p1).x * n.x + (p - p1).y * n.y < 0.0) inside = false;
        }
        if (inside) return true;
    }
    return false;
}

std::vector<std::pair<Vec2, Vec2>> Editor::unmaskedSegments(
    const Entity& host, const std::vector<std::pair<Vec2, Vec2>>& segs) const {
    if (!doc_ || host.id.isEmpty()) return segs;
    // Every mark that hides part of this run. Not only the ones attached to it: a line drawn across
    // three wires breaks all three, and the mask is painted over all three, so all three have that
    // stretch taken out of what can be picked.
    std::vector<std::vector<Vec2>> quads;
    for (const auto& m : doc_->entities()) {
        if (m.type != EntityType::LengthBreak) continue;
        if (m.id == host.id) continue;
        if (auto q = breakMaskQuad(m); q.size() == 4) quads.push_back(std::move(q));
    }
    if (quads.empty()) return segs;

    std::vector<std::pair<Vec2, Vec2>> out = segs;
    for (const std::vector<Vec2>& quad : quads) {
        std::vector<std::pair<Vec2, Vec2>> kept;
        for (const auto& s : out) {
            // The stretch of this segment INSIDE the quad, as a parameter range, by clipping against
            // each of the quad's four edges in turn.
            const Vec2 d = s.second - s.first;
            double lo = 0.0, hi = 1.0;
            bool empty = false;
            for (size_t i = 0; i < quad.size() && !empty; ++i) {
                const Vec2 p1 = quad[i], p2 = quad[(i + 1) % quad.size()];
                // Inward normal: towards the quad's centre.
                Vec2 centre{0, 0};
                for (const Vec2& c : quad) centre = centre + c;
                centre = {centre.x / quad.size(), centre.y / quad.size()};
                const Vec2 edge = p2 - p1;
                Vec2 n{-edge.y, edge.x};
                if ((centre - p1).x * n.x + (centre - p1).y * n.y < 0.0) n = n * -1.0;
                // Inside where dot(p - p1, n) >= 0.
                const double f0 = (s.first - p1).x * n.x + (s.first - p1).y * n.y;
                const double fd = d.x * n.x + d.y * n.y;
                if (std::abs(fd) < 1e-12) {
                    if (f0 < 0.0) empty = true;             // parallel and outside
                    continue;
                }
                const double t = -f0 / fd;
                if (fd > 0.0) lo = std::max(lo, t);
                else hi = std::min(hi, t);
                if (lo > hi) empty = true;
            }
            if (empty || hi - lo < 1e-9) { kept.push_back(s); continue; }   // nothing hidden
            // What is left of the segment: before the hidden stretch, and after it.
            const Vec2 a{s.first.x + d.x * lo, s.first.y + d.y * lo};
            const Vec2 b{s.first.x + d.x * hi, s.first.y + d.y * hi};
            if (lo > 1e-9) kept.emplace_back(s.first, a);
            if (hi < 1.0 - 1e-9) kept.emplace_back(b, s.second);
        }
        out = std::move(kept);
    }
    return out;
}

bool Editor::slideBreakAlongHost(Entity& e, const Vec2& delta) {
    if (!doc_ || e.type != EntityType::LengthBreak) return false;
    const QString hostId = e.meta.value(QStringLiteral("host")).toString();
    if (hostId.isEmpty()) return false;                  // attached to nothing: it moves freely
    const Entity* host = nullptr;
    for (const auto& h : doc_->entities())
        if (h.id == hostId) host = &h;
    if (!host) return false;
    Vec2 pos, dir;
    const double t0 = e.meta.value(QStringLiteral("annT")).toDouble(0.5);
    if (!runPointAtFraction(*host, t0, pos, dir)) return false;
    // The drag is answered by the nearest place ON THE RUN to where it was taken. Whatever the cursor
    // did across the run is ignored: a break marks a place on a run, and one sitting beside its run
    // says nothing at all.
    const double t1 = runFractionNearest(*host, pos + delta);
    e.meta[QStringLiteral("annT")] = t1;
    Vec2 at, d2;
    if (runPointAtFraction(*host, t1, at, d2)) {
        e.a = at;                        // kept in step, since the hit-test works off it
        e.rotation = std::atan2(d2.y, d2.x);
    }
    return true;
}

void Editor::applyEditDrag(const Vec2& target) {
    // A leader-landing joint doesn't change geometry as it drags — it re-connects the
    // leader to whichever circle it's dropped on, handled at placement time.
    if (editMode_ == 1 && editRole_ == static_cast<int>(GripRole::LeaderLanding)) return;
    const Vec2 delta = target - editLast_;
    // Entities moving in this drag (a connector's attached wires follow it; a wire
    // already moving itself must not be shifted twice).
    std::vector<QString> movingIds;
    if (editMode_ == 0) movingIds = selected_;
    else for (const auto& eg : editGrips_) movingIds.push_back(eg.id);
    if (!editMoved_) {
        if (delta.lengthSq() < 1e-18) return; // ignore until the cursor truly moves
        pushUndo();
        editMoved_ = true;
    }
    // A double line is stored as a centerline (a..b) + gap; the two VISIBLE lines
    // are the centerline offset by ±perp*(gap/2). A grip sits on a visible corner
    // (a/b ± off), but the editable point is the centerline end. To make the
    // GRABBED corner follow the cursor (not the centerline, which is the "middle"
    // between the two lines), solve for the centerline end so that
    //   end + perp(unit(b-a))*(gap/2)*side == cursor.
    // The perpendicular rotates as the line direction changes, so iterate to a
    // fixed point. `off` is {0,0} for a plain line, which collapses to end=cursor.
    auto solveEnd = [&](bool movingIsA, Entity* e, const Vec2& off) -> Vec2 {
        const double half = off.length();
        if (half < 1e-9) return target;               // plain line: end = cursor
        const Vec2 fixed = movingIsA ? e->b : e->a;
        auto perpN = [&](const Vec2& moving) -> Vec2 {
            const Vec2 ba = movingIsA ? (fixed - moving) : (moving - fixed); // == b-a
            return ba.lengthSq() < 1e-18 ? Vec2{0, 0} : ba.normalized().perp();
        };
        Vec2 P = target - off;                        // initial guess
        double side = 1.0;
        if (const Vec2 n = perpN(P); n.lengthSq() > 0 && dot(off, n) < 0) side = -1.0;
        for (int k = 0; k < 8; ++k) {
            const Vec2 n = perpN(P);
            if (n.lengthSq() < 1e-18) break;
            P = target - n * (side * half);
        }
        return P;
    };
    // Apply one grip's mutation on its entity.
    auto applyOne = [&](Entity* e, GripRole role, int index, const Vec2& off) {
        // A hosted break mark slides along its run whichever way it was taken hold of - by its body or
        // by one of its grips. Without this, grabbing it by a grip lifted it off the run.
        if (e->type == EntityType::LengthBreak && slideBreakAlongHost(*e, delta)) return;
        switch (role) {
        case GripRole::LineA: e->a = solveEnd(true, e, off); break;
        case GripRole::LineB:
            e->b = solveEnd(false, e, off);
            // On a radius, diameter, jogged or arc-length dimension the leader end is kept in points
            // too (that is what it was placed with), so the two must not drift apart.
            if (e->type == EntityType::Dimension && e->points.size() == 1) e->points[0] = e->b;
            break;
        case GripRole::LineMid:
            // The middle joint moves the whole line (stays straight, no knee).
            translateEntity(*e, delta);
            break;
        case GripRole::WireSegMove:
            // Move one wire run: shift both vertices of segment `index` by the drag.
            // (For a straight open wire that segment is the whole wire; for a bent wire
            // it's the run between two knees, so the connected ends stay put.)
            if (index >= 0 && index + 1 < static_cast<int>(e->points.size())) {
                e->points[index] = e->points[index] + delta;
                e->points[index + 1] = e->points[index + 1] + delta;
            }
            break;
        case GripRole::TwistMove:
            // Constrained: only the position ALONG the wires (world x) changes; each
            // per-wire crossing point is re-interpolated from its own wire at that x.
            repositionTwistAtX(*e, target.x);
            break;
        case GripRole::WireMark:
        case GripRole::WireMarkColor:
        case GripRole::WireMarkLabel: {
            // A marking belongs to its wire: the drag only changes how far ALONG the run it sits
            // (the cursor is projected onto the wire), never its distance from it. Which marking
            // is being dragged follows from the role.
            const QString part = (role == GripRole::WireMark)
                                     ? QStringLiteral("gauge")
                                     : (role == GripRole::WireMarkColor ? QStringLiteral("color")
                                                                        : QStringLiteral("label"));
            const int count = wireAnnotCountFor(*e, part);
            if (index < 0 || index >= count) break;
            std::vector<double> ts = wireAnnotFractionsFor(*e, part, count);
            ts[index] = wireFractionNearest(*e, target);
            setWireAnnotFractionsFor(*e, part, ts);
            break;
        }
        case GripRole::WireLabelSlide: {
            // Same for an attached Wire label — it slides up and down its own wire.
            const QString wid = e->meta.value(QStringLiteral("wireId")).toString();
            if (const Entity* w = entityById(wid))
                if (w->type == EntityType::Wire)
                    e->meta.insert(QStringLiteral("annT"), wireFractionNearest(*w, target));
            break;
        }
        case GripRole::Vertex:
            // A linear or aligned dimension from a file that saved only a and b has no points to hold;
            // the first drag of one of its joints gives it the three it should have had, measured
            // between a and b with the dimension line on them, so the drag then moves what it took
            // hold of instead of doing nothing.
            if (e->type == EntityType::Dimension && e->points.size() < 3) {
                const QString dty = e->meta.value("dimType").toString(QStringLiteral("linear"));
                if (dty == QLatin1String("linear") || dty == QLatin1String("aligned"))
                    e->points = {e->a, e->b, lerp(e->a, e->b, 0.5)};
            }
            // The joint by the ARROW moves the dimension line the way the dimension stands off and no
            // other way. A dimension placed across a length stays across it: dragging that joint changes
            // how far off the geometry the line sits, not what is measured. Following the pointer freely
            // meant a sideways drag past the middle turned a length into a height - the dimension
            // suddenly measuring something else, without being asked to.
            if (e->type == EntityType::Dimension && index == 2 && e->points.size() >= 3) {
                const Vec2 dp1 = e->points[0], dp2 = e->points[1], cur = e->points[2];
                if (e->meta.value("dimType").toString(QStringLiteral("linear")) ==
                    QLatin1String("linear")) {
                    e->points[2] = dimensionAcrossX(*e) ? Vec2{cur.x, target.y}
                                                        : Vec2{target.x, cur.y};
                } else {
                    // An aligned one stands off along the perpendicular to what it measures.
                    const Vec2 n = (dp2 - dp1).normalized().perp();
                    e->points[2] = dp1 + n * dot(target - dp1, n);
                }
                break;   // a and b are what it measures between, and this joint does not touch them
            }
            if (index >= 0 && index < static_cast<int>(e->points.size())) {
                if (e->type == EntityType::Polygon && e->points.size() >= 3)
                    e->points = regularPolygonPoints(polygonCenter(e->points), target,
                                                     static_cast<int>(e->points.size()), index);
                else
                    // `off` is the grip's offset from the stored point (0 normally; for
                    // a double polyline's physical-run joint it's the perpendicular
                    // offset, so the centerline vertex follows under the cursor).
                    e->points[index] = target - off;
            }
            // A dimension keeps a/b in sync with its measured points.
            if (e->type == EntityType::Dimension && e->points.size() >= 2) {
                e->a = e->points[0];
                e->b = e->points[1];
            }
            break;
        case GripRole::RectCorner:
            if (e->points.size() == 4) {
                const Vec2 opp = e->points[(index + 2) % 4];
                Bounds b; b.expand(opp); b.expand(target);
                // A picture keeps its proportions while a corner is dragged: stretching a logo out of
                // shape is almost never what was meant, and the lock can be turned off in Properties
                // (or the side grips used) when it is.
                if (e->type == EntityType::Image &&
                    e->meta.value(QLatin1String(PictureKeys::kLockAspect)).toBool(true)) {
                    const double pw = e->meta.value(QLatin1String(PictureKeys::kPixW)).toDouble(0.0);
                    const double ph = e->meta.value(QLatin1String(PictureKeys::kPixH)).toDouble(0.0);
                    const double aspect = (pw > 0.0 && ph > 0.0) ? pw / ph : 0.0;
                    if (aspect > 0.0) {
                        // Whichever way it was dragged further decides the size; the other side
                        // follows, growing away from the corner that stayed put.
                        double w = b.width(), h = b.height();
                        if (w / aspect >= h) h = w / aspect;
                        else w = h * aspect;
                        const double x0 = (target.x >= opp.x) ? opp.x : opp.x - w;
                        const double y0 = (target.y >= opp.y) ? opp.y : opp.y - h;
                        b = Bounds{};
                        b.expand({x0, y0});
                        b.expand({x0 + w, y0 + h});
                    }
                }
                e->points = {{b.min.x, b.min.y}, {b.max.x, b.min.y},
                             {b.max.x, b.max.y}, {b.min.x, b.max.y}};
            } else if (index < static_cast<int>(e->points.size())) {
                e->points[index] = target;
            }
            break;
        case GripRole::RectEdge:
            // Move one wall: shift the shared coordinate of the edge's two
            // corners toward the cursor (horizontal wall -> y, vertical -> x).
            if (e->points.size() == 4) {
                const int i = index, j = (index + 1) % 4;
                Vec2& pa = e->points[i];
                Vec2& pb = e->points[j];
                if (std::abs(pb.x - pa.x) >= std::abs(pb.y - pa.y)) {
                    pa.y = target.y; pb.y = target.y; // horizontal wall
                } else {
                    pa.x = target.x; pb.x = target.x; // vertical wall
                }
            }
            break;
        case GripRole::Center:
            if (e->meta.value("balloon").toBool()) {
                auto mates = chainMatesOf(*e);
                const bool isArrow = !balloonLeaders(*e).empty();
                if (isArrow || mates.empty()) {
                    // The arrow circle (or a lone balloon) moves the WHOLE batch; the
                    // leader arrow TIPS stay anchored (only centres move — the landing/
                    // knee is derived from the centre, so it follows automatically).
                    e->center = e->center + delta;
                    e->a = e->a + delta;
                    for (Entity* m : mates) {
                        m->center = m->center + delta;
                        m->a = m->a + delta;
                    }
                } else {
                    // A non-arrow circle follows the cursor but stays TANGENT to its
                    // nearest neighbour, and may NOT overlap any other batch circle.
                    Vec2 nc = target;
                    Entity* nb = nullptr;
                    double bd = std::numeric_limits<double>::max();
                    for (Entity* m : mates) {
                        const double d = distance(nc, m->center);
                        if (d < bd) { bd = d; nb = m; }
                    }
                    if (nb) {
                        Vec2 dir = nc - nb->center;
                        const double L = dir.length();
                        dir = L > 1e-9 ? dir * (1.0 / L) : Vec2{1, 0};
                        nc = nb->center + dir * (e->radius + nb->radius); // tangent
                    }
                    // Push out of any circle it still overlaps (no overlaps allowed).
                    for (int iter = 0; iter < 8; ++iter) {
                        bool pushed = false;
                        for (Entity* m : mates) {
                            const double minD = e->radius + m->radius;
                            const double d = distance(nc, m->center);
                            if (d < minD - 1e-6) {
                                Vec2 dir = nc - m->center;
                                const double L = dir.length();
                                dir = L > 1e-9 ? dir * (1.0 / L) : Vec2{1, 0};
                                nc = m->center + dir * minD;
                                pushed = true;
                            }
                        }
                        if (!pushed) break;
                    }
                    e->center = nc;
                    e->a = nc;
                }
            } else {
                translateEntity(*e, delta);
            }
            break;
        case GripRole::Radius: e->radius = std::max(1e-6, distance(e->center, target)); break;
        case GripRole::Move:
            // Moving a connector drags the wires attached to its pin ends along.
            if (e->type == EntityType::Connector) dragAttachedWires(*e, delta, movingIds);
            // A shield's centre handle slides it ALONG its wires only (same constraint as
            // a whole-selection move); a decorative copy falls through to a free translate.
            if (e->type == EntityType::Screen && repositionScreenAtX(*e, target.x)) break;
            translateEntity(*e, delta);
            break;
        case GripRole::ShieldRy: {
            // Large-radius joint: the half-height follows the cursor's projection onto the
            // shield's major axis. WHICH wires that new size catches is settled on release
            // (commitShieldResize), so the drag itself is a free live preview.
            const ShieldFrame f = shieldFrameOf(*e);
            if (!f.valid) break;
            const Vec2 rel = target - f.center;
            e->meta["ry"] = std::max(1.0, std::abs(rel.x * f.major.x + rel.y * f.major.y));
            break;
        }
        case GripRole::DimText: // reposition the dimension's measurement text
            e->meta["textPosX"] = target.x;
            e->meta["textPosY"] = target.y;
            break;
        case GripRole::LeaderTip: { // move one balloon leader's arrow tip
            auto tips = balloonLeaders(*e);
            if (index >= 0 && index < static_cast<int>(tips.size())) {
                tips[index] = target;
                setBalloonLeaders(*e, tips);
            }
            break;
        }
        case GripRole::LeaderLanding: // handled on placement (around-circle / re-attach)
            break;
        case GripRole::LeaderKnee: // drag the knee horizontally: sets landing length/side
            e->meta["landingSide"] = (target.x >= e->center.x) ? 1.0 : -1.0;
            e->meta["landing"] = std::max(0.0, std::abs(target.x - e->center.x) - e->radius);
            break;
        case GripRole::TextWidth: // drag the MTEXT container's right edge
            e->meta["boxWidth"] = std::max(1.0, target.x - e->a.x);
            break;
        case GripRole::TextHeight: // drag the MTEXT container's bottom edge
            e->meta["boxHeight"] = std::max(1.0, e->a.y - target.y);
            break;
        // An arc's ends move ROUND it, keeping its radius: that is what an end of an arc does, and
        // the radius has a handle of its own (the middle of the arc). Changing both at once meant an
        // arc could only be resized, never swung.
        case GripRole::ArcStart:
            e->startAngle = std::atan2(target.y - e->center.y, target.x - e->center.x);
            break;
        case GripRole::ArcEnd:
            e->endAngle = std::atan2(target.y - e->center.y, target.x - e->center.x);
            break;
        case GripRole::ArcMid:
            e->radius = std::max(1e-6, distance(e->center, target));
            break;
        case GripRole::EllipseMajor:
            // The major axis follows the cursor: it sets both the semi-axis and rotation.
            e->radius = std::max(1e-6, distance(e->center, target));
            e->rotation = std::atan2(target.y - e->center.y, target.x - e->center.x);
            break;
        case GripRole::EllipseMinor: {
            // The minor semi-axis is the cursor's distance along the perpendicular.
            const Vec2 majorDir{std::cos(e->rotation), std::sin(e->rotation)};
            e->radiusY = std::max(1e-6, std::abs(dot(target - e->center, majorDir.perp())));
            break;
        }
        // An end of a PIECE of an ellipse moves ROUND it: the piece gets longer or shorter and the
        // ellipse itself - its size, its turn - is left alone. (Its axis grips are what size it.)
        case GripRole::EllipseStart:
            e->startAngle = ellipseParamAt(target, e->center, e->radius, e->radiusY, e->rotation);
            break;
        case GripRole::EllipseEnd:
            e->endAngle = ellipseParamAt(target, e->center, e->radius, e->radiusY, e->rotation);
            break;
        case GripRole::TableColEdge: {
            // Drag an interior vertical grid line: only the column to its LEFT resizes;
            // every other column keeps its width, so the columns to the right shift and
            // the table's right edge moves with them (top-left stays anchored).
            if (e->points.size() != 4) break;
            Bounds tb; for (const auto& pt : e->points) tb.expand(pt);
            QJsonArray cf = e->meta.value("colFrac").toArray();
            const int c = index; // boundary between column c-1 (left) and column c (right)
            if (c < 1 || c >= cf.size()) break;
            const double W = tb.width();
            if (W < 1e-9) break;
            std::vector<double> w(cf.size());
            double xB = tb.min.x; // current world x of boundary c
            for (int i = 0; i < cf.size(); ++i) { w[i] = cf[i].toDouble() * W; if (i < c) xB += w[i]; }
            const double minW = std::max(1e-4, W * 0.02);
            const double newX = std::max(xB - (w[c - 1] - minW), target.x); // keep col c-1 >= minW
            const double dx = newX - xB;
            const double Wp = W + dx;
            if (Wp < 1e-9) break;
            w[c - 1] += dx;
            const double maxX = tb.min.x + Wp;
            e->points = {{tb.min.x, tb.min.y}, {maxX, tb.min.y}, {maxX, tb.max.y}, {tb.min.x, tb.max.y}};
            for (int i = 0; i < cf.size(); ++i) cf[i] = w[i] / Wp;
            e->meta["colFrac"] = cf;
            break;
        }
        case GripRole::TableRowEdge: {
            // Drag an interior horizontal grid line: only the row ABOVE it resizes; every
            // other row keeps its height, so the rows below shift and the table's bottom
            // edge moves (top-left stays anchored). Rows run top -> down (world y-up).
            if (e->points.size() != 4) break;
            Bounds tb; for (const auto& pt : e->points) tb.expand(pt);
            QJsonArray rf = e->meta.value("rowFrac").toArray();
            const int r = index; // boundary between row r-1 (above) and row r (below)
            if (r < 1 || r >= rf.size()) break;
            const double H = tb.height();
            if (H < 1e-9) break;
            std::vector<double> h(rf.size());
            double yB = tb.max.y; // current world y of boundary r
            for (int i = 0; i < rf.size(); ++i) { h[i] = rf[i].toDouble() * H; if (i < r) yB -= h[i]; }
            const double minH = std::max(1e-4, H * 0.02);
            const double newY = std::min(yB + (h[r - 1] - minH), target.y); // keep row r-1 >= minH
            const double dy = yB - newY; // boundary moving down (y decreasing) grows row r-1
            const double Hp = H + dy;
            if (Hp < 1e-9) break;
            h[r - 1] += dy;
            const double minY = tb.max.y - Hp;
            e->points = {{tb.min.x, minY}, {tb.max.x, minY}, {tb.max.x, tb.max.y}, {tb.min.x, tb.max.y}};
            for (int i = 0; i < rf.size(); ++i) rf[i] = h[i] / Hp;
            e->meta["rowFrac"] = rf;
            break;
        }
        case GripRole::ConnDesText:
        case GripRole::ConnPnText:
        case GripRole::ConnPinText:
        case GripRole::ConnAllText: {
            // Nudge a connector text offset (relative to the connector) by the drag.
            auto bump = [&](const char* kx, const char* ky) {
                e->meta[kx] = e->meta.value(kx).toDouble() + delta.x;
                e->meta[ky] = e->meta.value(ky).toDouble() + delta.y;
            };
            if (role == GripRole::ConnDesText) bump("desTextDX", "desTextDY");
            else if (role == GripRole::ConnPnText) bump("pnTextDX", "pnTextDY");
            else if (role == GripRole::ConnPinText) bump("pinTextDX", "pinTextDY");
            else { bump("desTextDX", "desTextDY"); bump("pnTextDX", "pnTextDY");
                   bump("pinTextDX", "pinTextDY"); } // all text together
            break;
        }
        }
    };

    if (editMode_ == 0) {
        for (Entity* e : selectedEntities()) {
            // A break mark is pinned to the run it marks: dragging it slides it ALONG that run rather
            // than lifting it off. A break beside its run says nothing, and a break that has to be
            // nudged back on to the run by eye after every move is worse than one that cannot leave
            // it. The cursor is projected on to the run and the mark keeps the new place; its angle
            // comes from the run there, so it stays square through a corner.
            if (e->type == EntityType::LengthBreak && slideBreakAlongHost(*e, delta)) continue;
            // Twist marks are constrained to their wires: a whole-selection move slides each
            // one ALONG its wires by the drag's x-component (y re-derived from the wire), so
            // several selected twists move together yet stay on the wires. A decorative copy
            // with no wire references falls back to a free translate.
            if (e->type == EntityType::Twist && !e->meta.value("wires").toArray().isEmpty()) {
                double cx = 0.0;
                for (const Vec2& pt : e->points) cx += pt.x;
                if (!e->points.empty()) cx /= e->points.size();
                repositionTwistAtX(*e, cx + delta.x);
                continue;
            }
            // A shield is likewise pinned to the wires it catches: it slides along them
            // rather than translating freely off them.
            if (e->type == EntityType::Screen) {
                const int k = std::min(e->meta.value("catch").toInt(
                                          static_cast<int>(e->points.size())),
                                      static_cast<int>(e->points.size()));
                double cx = 0.0;
                for (int i = 0; i < k; ++i) cx += e->points[i].x;
                if (k > 0 && repositionScreenAtX(*e, cx / k + delta.x)) continue;
            }
            if (e->type == EntityType::Connector) dragAttachedWires(*e, delta, movingIds);
            translateEntity(*e, delta);
            // A shield carried bodily still keeps the wire joined to its drain.
            if (e->type == EntityType::Screen) pullShieldLegWire(*e);
        }
    } else {
        // Move every grabbed joint (one, or all coincident shared joints).
        // Reshaping a trimmed double line/polyline invalidates its trim caps (the
        // stored cut line/circle no longer matches the moved end), so drop them —
        // the joint then edits as a normal double-polyline vertex.
        for (const auto& eg : editGrips_)
            if (Entity* e = entityById(eg.id)) {
                for (const QString& base : {QStringLiteral("cutStart"), QStringLiteral("cutStart2"),
                                            QStringLiteral("cutEnd"), QStringLiteral("cutEnd2")})
                    for (const char* s : {"", "Kind", "Ax", "Ay", "Bx", "By", "Cx", "Cy", "R", "Poly",
                                          "Side"})
                        e->meta.remove(base + QLatin1String(s));
                applyOne(e, static_cast<GripRole>(eg.role), eg.index, eg.offset);
            }
        // With Shift held the rest of the selected wires travel with this one; the moment
        // it is released they all go back where they started. Read live, so pressing and
        // releasing Shift part-way through the drag works either way.
        pullSelectedWireBundle((QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0);
    }
    editLast_ = target;
    doc_->setDirty(true);
}

void Editor::pullSelectedWireBundle(bool shiftHeld) {
    if (!doc_ || selected_.size() < 2) return;
    const GripRole role = static_cast<GripRole>(editRole_);
    if (role != GripRole::WireSegMove && role != GripRole::Vertex) return;
    Entity* held = entityById(editEntityId_);
    if (!held || held->type != EntityType::Wire) return;
    auto ghostOf = [this](const QString& id) -> const Entity* {
        for (const Entity& g : editGhost_)
            if (g.id == id) return &g;
        return nullptr;
    };
    // Measure against the geometry snapshotted when the drag STARTED, not against the
    // last step: the whole displacement so far is what the bundle follows, and it is what
    // has to be undone the moment Shift is let go.
    const Entity* heldStart = ghostOf(editEntityId_);
    if (!heldStart) return;
    const auto from = wireGripPoint(*heldStart, role, editIndex_);
    const auto to = wireGripPoint(*held, role, editIndex_);
    if (!from || !to) return;
    // Shift held: the bundle sits at its start position plus the held wire's total travel.
    // Shift released: the same code with no travel — every other wire snaps back exactly
    // where it began and only the held wire keeps moving.
    const Vec2 travel = shiftHeld ? (*to - *from) : Vec2{0.0, 0.0};

    for (const QString& id : selected_) {
        if (id == editEntityId_) continue;
        // A wire whose joint was already moved as a shared/coincident joint is driven by
        // that joint, not by the bundle.
        bool alreadyMoved = false;
        for (const auto& eg : editGrips_)
            if (eg.id == id) { alreadyMoved = true; break; }
        if (alreadyMoved) continue;
        Entity* w = entityById(id);
        const Entity* start = ghostOf(id);
        if (!w || !start || w->type != EntityType::Wire) continue;
        const int n = static_cast<int>(w->points.size());
        if (static_cast<int>(start->points.size()) != n) continue;
        // Matching run / knee = the SAME index: the wires of a fan-out are lane copies of
        // one route, so index correspondence is what keeps the bundle in step. Wires
        // shaped differently (no such index) are simply left alone. Setting an ABSOLUTE
        // position (start + travel) rather than nudging is what makes the release exact.
        if (role == GripRole::Vertex) {
            if (editIndex_ >= 0 && editIndex_ < n)
                w->points[editIndex_] = start->points[editIndex_] + travel;
        } else if (editIndex_ >= 0 && editIndex_ + 1 < n) {
            w->points[editIndex_] = start->points[editIndex_] + travel;
            w->points[editIndex_ + 1] = start->points[editIndex_ + 1] + travel;
        }
    }
}

void Editor::onDoubleClick(const Vec2& world) {
    if (drafting_ && (tool_ == ToolId::Polyline || tool_ == ToolId::DoublePolyline ||
                      tool_ == ToolId::Spline || tool_ == ToolId::Wire ||
                      tool_ == ToolId::Hatch || tool_ == ToolId::Wipeout)) {
        finishDraw();
        return;
    }
    // Double-click a copy of a block to edit the block itself - the way a block is opened in every
    // drawing package. Checked before the text editors below, because a piece of text inside a block
    // belongs to the block: editing it on the sheet would be undone the next time the block changed.
    if (tool_ == ToolId::Select) {
        if (const auto hit = hitTest(world)) {
            if (const Entity* e = entityById(*hit)) {
                // A default model on its editing canvas is one object, so it can be placed as the part it
                // is. Double-clicking OPENS IT UP - the grouping comes off and every line of it has its
                // joints back - which is the same gesture a block is opened with and the only thing
                // "edit the model" can mean. It is not a block, so there is no definition to go and load.
                if (entityBlockName(*e).isEmpty() &&
                    entityBlockInstance(*e) == QLatin1String(kMechModelGroup)) {
                    pushUndo();
                    int opened = 0;
                    for (Entity& x : doc_->entities())
                        if (entityBlockInstance(x) == QLatin1String(kMechModelGroup)) {
                            clearEntityBlockStamp(x);
                            ++opened;
                        }
                    selected_.clear();
                    doc_->setDirty(true);
                    emit selectionChanged();
                    emit message(QStringLiteral("The model is open — %1 object(s) to edit").arg(opened));
                    emit changed();
                    return;
                }
                const QString block = entityBlockName(*e);
                if (!block.isEmpty()) {
                    emit editBlockRequested(block);
                    return;
                }
                // A field is filled in on the drawing, so it opens the text editor like any other
                // text - checked before the frame, since a field IS part of the frame.
                if (entityIsTemplateField(*e)) {
                    emit editTextRequested(e->id);
                    return;
                }
                if (!entityTemplateGroup(*e).isEmpty()) {
                    emit editTemplateRequested(entityTemplateName(*e));
                    return;
                }
            }
        }
    }
    // Double-click a text-bearing object (while idle) to re-open its editor: plain
    // text, or an annotation that carries editable text (balloon number, note /
    // revision body).
    if (tool_ == ToolId::Select) {
        // A connector pin label: double-click it (even over the empty body, which is
        // not otherwise pickable) to edit that pin's number/letter inline.
        {
            const double tol = worldTolerance() * 2.0;
            for (const auto& e : doc_->entities()) {
                if (e.type != EntityType::Connector) continue;
                const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
                const ConnectorGeometry g = connectorGeometry(e.a, spec);
                for (size_t i = 0; i < g.pinLabelPos.size(); ++i) {
                    if (distance(world, g.pinLabelPos[i]) < tol) {
                        const QString cur =
                            static_cast<int>(i) < spec.pins.size() ? spec.pins[i] : QString();
                        // Clear the region-select the first click of the double-click
                        // started over the (non-pickable) body, so no zone lingers.
                        regionMode_ = RegionMode::None;
                        pressed_ = false;
                        dragPath_.clear();
                        if (dragTimer_) dragTimer_->stop();
                        emit changed();
                        emit editConnectorPinRequested(e.id, static_cast<int>(i), cur,
                                                       g.pinLabelPos[i], spec.pinSize * 0.7,
                                                       spec.pinFont);
                        return;
                    }
                }
            }
        }
        if (auto hit = hitTest(world))
            if (Entity* e = entityById(*hit)) {
                // A table: edit the specific cell that was double-clicked.
                if (e->meta.value("table").toBool()) {
                    Vec2 tl; double cw = 0, ch = 0;
                    const int idx = tableCellAt(*e, world, tl, cw, ch);
                    if (idx >= 0) {
                        const QJsonArray cells = e->meta.value("cells").toArray();
                        const QString cur = idx < cells.size() ? cells[idx].toString() : QString();
                        emit editTableCellRequested(e->id, idx, cur, tl, cw, ch);
                    }
                    return;
                }
                if (e->type == EntityType::Text || e->meta.value("balloon").toBool() ||
                    e->meta.value("note").toBool() || e->meta.value("revision").toBool())
                    emit editTextRequested(*hit);
            }
    }
}

// ---- Draw tools ------------------------------------------------------------

namespace {
// Closed boundary of an entity, for "select object" hatch/wipeout. Circles and
// ellipses are sampled into a polygon; open shapes return empty. `manyJoints`
// chooses a dense sampling (smooth, many grips) vs a coarse one (circle -> 4).
std::vector<Vec2> boundaryPointsOf(const Entity& e, bool manyJoints = true) {
    switch (e.type) {
    case EntityType::Rect:
    case EntityType::Polygon:
    case EntityType::Polyline:
    case EntityType::Spline:
    case EntityType::Hatch:
    case EntityType::Wipeout:
        if (e.points.size() >= 3) return e.points;
        return {};
    case EntityType::Circle: {
        std::vector<Vec2> pts;
        const int n = manyJoints ? 64 : 4; // coarse = 4 quadrant joints
        for (int i = 0; i < n; ++i) {
            const double a = 2.0 * M_PI * i / n;
            pts.push_back({e.center.x + e.radius * std::cos(a),
                           e.center.y + e.radius * std::sin(a)});
        }
        return pts;
    }
    case EntityType::Ellipse: {
        std::vector<Vec2> pts;
        const int n = manyJoints ? 64 : 4;
        const double c = std::cos(e.rotation), s = std::sin(e.rotation);
        for (int i = 0; i < n; ++i) {
            const double a = 2.0 * M_PI * i / n;
            const double x = e.radius * std::cos(a), y = e.radiusY * std::sin(a);
            pts.push_back({e.center.x + x * c - y * s, e.center.y + x * s + y * c});
        }
        return pts;
    }
    default:
        return {};
    }
}
} // namespace

void Editor::handleDrawPress(const Vec2& p) {
    // Line-start ("ls"): this click picks the reference line whose start both runs
    // will reach. It does NOT place a vertex — the next click starts the run.
    if (dpolyLsArm_ && (tool_ == ToolId::DoubleLine || tool_ == ToolId::DoublePolyline)) {
        dpolyLsArm_ = false;
        if (auto hit = hitTest(p)) {
            if (Entity* r = entityById(*hit)) {
                Vec2 A{0, 0}, B{0, 0};
                bool ok = false;
                if (r->type == EntityType::Line || r->type == EntityType::DoubleLine ||
                    r->type == EntityType::Dimension) {
                    A = r->a; B = r->b; ok = true;
                } else if (r->points.size() >= 2) {
                    // Nearest segment of a polyline / rect / polygon / etc.
                    const bool closed = r->type == EntityType::Rect ||
                                        r->type == EntityType::Polygon ||
                                        r->type == EntityType::Hatch ||
                                        r->type == EntityType::Wipeout;
                    double best = std::numeric_limits<double>::max();
                    const size_t n = r->points.size();
                    for (size_t i = 0; i + 1 < n || (closed && i < n); ++i) {
                        const Vec2& s1 = r->points[i % n];
                        const Vec2& s2 = r->points[(i + 1) % n];
                        const double d = closestPointOnSegment(p, s1, s2).distance;
                        if (d < best) { best = d; A = s1; B = s2; ok = true; }
                        if (i + 1 >= n) break;
                    }
                }
                if (ok) {
                    lsRefA_ = A;
                    lsRefB_ = B;
                    dpolyHasLsRef_ = true;
                    emit message(QStringLiteral("Reference set — click the start point"));
                    emit overlayChanged();
                    return;
                }
            }
        }
        emit message(QStringLiteral("Click an existing line as the reference"));
        return;
    }
    switch (tool_) {
    case ToolId::Line: {
        // Two points make a line, and then the drawing is done with: back to Select, the same as after
        // every other drawing this application makes. It used to keep the run going, AutoCAD-style,
        // which left the tool armed after every single line - and a run of connected segments is what
        // the polyline tool is for. Space re-arms this one for the next line.
        draftPts_.push_back(p);
        drafting_ = true;
        if (draftPts_.size() >= 2) {
            Entity e;
            e.layer = doc_->activeLayer();
            e.type = EntityType::Line;
            e.a = draftPts_[0];
            e.b = draftPts_[1];
            pushUndo();
            addEntity(std::move(e));
            draftPts_.clear();
            drafting_ = false;
            setTool(ToolId::Select);
            emit message(QStringLiteral("Line placed — Space draws another"));
            break;
        }
        emit message(QStringLiteral("Line: click the second point — Esc exits"));
        break;
    }
    case ToolId::TwistedRoute:
        // Like a polyline: each click adds a knee and the pair keeps going until Enter.
        draftPts_.push_back(p);
        drafting_ = true;
        emit message(QStringLiteral(
            "Twisted route: click the next knee — Enter finishes the run, Esc exits"));
        break;
    case ToolId::TwistedHarness:
    case ToolId::TwistedPair:
        // Pinned (Tab), the click means "place it" rather than "put the far end here": the end stays
        // where it was pinned, which is the whole point of pinning it.
        draftPts_.push_back(twistedFrozen_ ? twistedFrozenAt_ : p);
        drafting_ = true;
        if (draftPts_.size() == 2) {
            twistedFrozen_ = false;
            finishDraw();
        }
        break;
    case ToolId::DoubleLine:
    case ToolId::Rect:
    case ToolId::Polygon:
    case ToolId::Note:
    case ToolId::Revision:
        if (tool_ == ToolId::DoubleLine) {
            // Shift-click on a line caps that run-end flush to it (applied in finishDraw).
            if (draftPts_.empty()) drawStartShift_ = (pressMods_ & Qt::ShiftModifier);
            drawEndShift_ = (pressMods_ & Qt::ShiftModifier);
        }
        draftPts_.push_back(p);
        drafting_ = true;
        if (draftPts_.size() == 2) finishDraw();
        break;
    case ToolId::Balloon:
    case ToolId::TextLeader:
        // First click = the arrow tip (what it points at); second = the content spot.
        draftPts_.push_back(p);
        drafting_ = true;
        emit message(draftPts_.size() < 2
                         ? QStringLiteral("Leader: click where it points")
                         : QStringLiteral("Leader: click the label location"));
        if (draftPts_.size() == 2) finishDraw();
        break;
    case ToolId::Dimension:
        handleDimensionPress(p);
        break;
    case ToolId::Circle:
        // Tangent modes pick objects to touch instead of placing points.
        if (circleMode_ == CircleMode::TanTanRadius || circleMode_ == CircleMode::TanTanTan) {
            handleTangentPick(p);
            return;
        }
        draftPts_.push_back(p);
        drafting_ = true;
        if (draftPts_.size() == (circleMode_ == CircleMode::ThreePoint ? 3u : 2u)) finishDraw();
        break;
    case ToolId::Ellipse: {
        // 3-point ellipse (axis + distance). Elliptical Arc adds 2 more clicks: the
        // start angle then the end angle (so the ellipse is "opened" into an arc).
        draftPts_.push_back(p);
        drafting_ = true;
        const size_t need = (ellipseMode_ == EllipseMode::EllipticalArc) ? 5u : 3u;
        if (draftPts_.size() == need) finishDraw();
        else if (ellipseMode_ == EllipseMode::EllipticalArc && draftPts_.size() == 3)
            emit message(QStringLiteral("Elliptical arc: pick the START angle"));
        else if (ellipseMode_ == EllipseMode::EllipticalArc && draftPts_.size() == 4)
            emit message(QStringLiteral("Elliptical arc: pick the END angle"));
        break;
    }
    case ToolId::Arc:
        draftPts_.push_back(p);
        drafting_ = true;
        if (draftPts_.size() == 3) finishDraw();
        break;
    case ToolId::Polyline:
    case ToolId::DoublePolyline:
    case ToolId::Spline:
    case ToolId::Wire: {
        // Wire "90° only": snap this vertex to a horizontal/vertical run from the previous
        // one (the cursor itself stayed free). The first point (on a pin, etc.) is kept as-is.
        //
        // A point the cursor SNAPPED to is not moved by either rule. It is a pin, another wire's end
        // or its body - a connection the user aimed at - and the two rules used to move it off:
        // "90° only" re-projected it on to an axis, and the clearance rule pushed it aside. Between
        // them, a wire being routed on to a pin would sometimes simply not land on it. The route still
        // turns square corners: when the point cannot be reached along one axis, a KNEE is put in
        // before it, so the last leg runs into the connection at 90° and lands exactly on it.
        Vec2 wp = p;
        const bool landing = (tool_ == ToolId::Wire && !draftPts_.empty() &&
                              isConnectablePoint(p));
        if (tool_ == ToolId::Wire && wireOrtho_ && !draftPts_.empty()) {
            const Vec2 square = orthoWirePoint(draftPts_.back(), p);
            if (!landing) {
                wp = square;
            } else if (distance(square, p) > 1e-9) {
                draftPts_.push_back(square);            // the corner
                draftBulges_.push_back(0.0);
                draftCornerRadii_.push_back(0.0);
            }
        }
        // Pitch rule while routing: hold a clearance from the runs already on the drawing
        // so this run can never land on top of a neighbour (single wire included).
        if (tool_ == ToolId::Wire && !draftPts_.empty() && !landing)
            wp = applyWireClearance(draftPts_.back(), wp);
        // In Arc mode each new vertex makes an arc segment tangent to the previous
        // one; draftBulges_ stays aligned with draftPts_ (bulge of the segment that
        // ENDS at this vertex; the very first segment is always straight).
        double bulge = 0.0;
        if (polyArcMode_ && tool_ == ToolId::Polyline && draftPts_.size() >= 2) {
            const Vec2 prevDir = draftPts_.back() - draftPts_[draftPts_.size() - 2];
            bulge = tangentBulge(draftPts_.back(), p, prevDir);
        }
        if (tool_ == ToolId::DoublePolyline) {
            // Shift-click on a line caps that run-end flush to it (applied in finishDraw).
            if (draftPts_.empty()) drawStartShift_ = (pressMods_ & Qt::ShiftModifier);
            drawEndShift_ = (pressMods_ & Qt::ShiftModifier);
        }
        // Starting a wire on a connector pin: remember the connector's pins so the fan-out
        // count can be set (Shift = all pins from here down; a number key = that many).
        if (tool_ == ToolId::Wire && draftPts_.empty()) {
            wireFanPins_.clear();
            wireFanAnchor_ = -1;
            wireFanSel_.clear();
            wireArray_ = false;
            wireArrayOffsets_.clear();
            std::vector<Vec2> pins;
            int anchor = -1;
            double pitch = 0.0;
            wireRoutePitch_ = 0.0;
            if (connectorPinsAt(p, pins, anchor, &pitch)) {
                wireFanPins_ = std::move(pins);
                wireFanAnchor_ = anchor;
                wireFanSel_ = {anchor};                       // just the drawn wire, to start
                // This connector's pin pitch is the clearance the run holds from its
                // neighbours (the Pitch rule), so a wire pulled off a connector lines up
                // with the pins it came from instead of using some unrelated distance.
                wireRoutePitch_ = pitch;
                if (pressMods_ & Qt::ShiftModifier) setWireFanAll(); // Shift-click = add all pins
            }
        }
        draftPts_.push_back(wp);
        draftBulges_.push_back(bulge);
        draftCornerRadii_.push_back(0.0); // this new vertex is an endpoint for now
        // Placing this point completes the corner at the PREVIOUS vertex; stamp it
        // with the current fillet radius so a fillet defined with "f" applies to
        // the next joint (and every later one) but never to corners already drawn.
        if (draftCornerRadii_.size() >= 3)
            draftCornerRadii_[draftCornerRadii_.size() - 2] = cornerRadius_;
        drafting_ = true;
        emit message(QStringLiteral("%1: click next point — [A]rc/[L]ine, [C]lose, "
                                    "[U]ndo, Enter to finish")
                         .arg(toolLabel(tool_)));
        break;
    }
    case ToolId::Wipeout:
        // AutoCAD WIPEOUT: pick polygon vertices; Enter / double-click / Close
        // finishes the masking polygon.
        draftPts_.push_back(p);
        drafting_ = true;
        emit message(QStringLiteral("WIPEOUT: click the next point — [C]lose, [U]ndo, "
                                    "Enter to finish"));
        break;
    case ToolId::Text:
        draftPts_ = {p};
        emit textPromptRequested(QStringLiteral("Text:"));
        break;
    case ToolId::MText:
        // AutoCAD MTEXT: drag a box (two corners) to define the wrap width, then
        // enter the text.
        draftPts_.push_back(p);
        drafting_ = true;
        if (draftPts_.size() >= 2) {
            drafting_ = false;
            emit textPromptRequested(QStringLiteral("Text:"));
        } else {
            emit message(QStringLiteral("MText: pick the opposite corner of the text box"));
        }
        break;
    default:
        break;
    }
    // The next step starts on a fresh set of reference fields.
    drawFieldIndex_ = 0;
    drawFieldVals_.clear();
    emit changed();
}

namespace {
const char* dimTypeString(Editor::DimMode m) {
    switch (m) {
    case Editor::DimMode::Linear: return "linear";
    case Editor::DimMode::Aligned: return "aligned";
    case Editor::DimMode::Angular: return "angular";
    case Editor::DimMode::ArcLength: return "arclength";
    case Editor::DimMode::Radius: return "radius";
    case Editor::DimMode::Diameter: return "diameter";
    case Editor::DimMode::Ordinate: return "ordinate";
    case Editor::DimMode::Jogged: return "jogged";
    }
    return "linear";
}
} // namespace

void Editor::handleDimensionPress(const Vec2& p) {
    drafting_ = true;
    switch (dimMode_) {
    case DimMode::Linear:
    case DimMode::Aligned:
        draftPts_.push_back(p);
        if (draftPts_.size() == 3) finishDraw();
        else
            emit message(draftPts_.size() == 1
                             ? QStringLiteral("Dimension: pick the second point")
                             : QStringLiteral("Dimension: pick the dimension-line position"));
        break;
    case DimMode::Angular:
        draftPts_.push_back(p);
        if (draftPts_.size() == 3) finishDraw();
        else
            emit message(QStringLiteral("Angular: pick the vertex, then the two endpoints"));
        break;
    case DimMode::Ordinate:
        draftPts_.push_back(p);
        if (draftPts_.size() == 2) finishDraw();
        else emit message(QStringLiteral("Ordinate: pick the leader endpoint"));
        break;
    case DimMode::Radius:
    case DimMode::Diameter:
    case DimMode::ArcLength:
    case DimMode::Jogged:
        if (!dimHasRef_) {
            if (auto hit = hitTest(p)) {
                if (Entity* e = entityById(*hit);
                    e && (e->type == EntityType::Circle || e->type == EntityType::Arc)) {
                    dimRefCenter_ = e->center;
                    dimRefRadius_ = e->radius;
                    dimRefStart_ = e->startAngle;
                    dimRefEnd_ = e->endAngle;
                    dimHasRef_ = true;
                    draftPts_ = {p};
                    emit message(QStringLiteral("Pick the dimension text position"));
                    break;
                }
            }
            drafting_ = false;
            emit message(QStringLiteral("Select a circle or arc to dimension"));
        } else {
            draftPts_.push_back(p);
            finishDraw();
        }
        break;
    }
    emit changed();
}

const Entity* Editor::enclosingEntity(const Vec2& seed) const {
    if (!doc_) return nullptr;
    const Entity* best = nullptr;
    double bestArea = std::numeric_limits<double>::max();
    for (const auto& e : doc_->entities()) {
        if (const Layer* l = doc_->findLayer(e.layer); l && (!l->visible || l->locked))
            continue;
        // Only existing CLOSED shapes form a boundary; skip fills and open shapes.
        std::vector<Vec2> poly;
        switch (e.type) {
        case EntityType::Rect:
        case EntityType::Polygon:
        case EntityType::Circle:
        case EntityType::Ellipse:
            poly = boundaryPointsOf(e, true); // dense for an accurate enclosure test
            break;
        case EntityType::Polyline:
        case EntityType::Spline:
            if (e.points.size() >= 3 &&
                distance(e.points.front(), e.points.back()) < worldTolerance())
                poly = boundaryPointsOf(e, true);
            break;
        default:
            break; // lines / double lines / hatch / wipeout / text are not boundaries
        }
        if (poly.size() < 3 || !pointInPolygon(poly, seed)) continue;
        // Shoelace area: keep the SMALLEST enclosing region (innermost island).
        double area2 = 0.0;
        for (size_t i = 0, n = poly.size(); i < n; ++i) {
            const Vec2& a = poly[i];
            const Vec2& b = poly[(i + 1) % n];
            area2 += a.x * b.y - b.x * a.y;
        }
        const double area = std::abs(area2) * 0.5;
        if (area > 1e-9 && area < bestArea) { bestArea = area; best = &e; }
    }
    return best;
}

std::optional<std::vector<Vec2>> Editor::enclosingRegionLoop(const Vec2& seed) const {
    if (!doc_) return std::nullopt;
    // Gather boundary segments from every visible line-like / closed object. The
    // tracer splits them at crossings and walks the cell containing the seed, so a
    // region bounded by SEVERAL separate objects still fills.
    std::vector<std::pair<Vec2, Vec2>> segs;
    auto addSeg = [&](const Vec2& a, const Vec2& b) {
        if ((b - a).lengthSq() > 1e-12) segs.emplace_back(a, b);
    };
    for (const auto& e : doc_->entities()) {
        if (const Layer* l = doc_->findLayer(e.layer); l && (!l->visible || l->locked)) continue;
        switch (e.type) {
        case EntityType::Hatch:
        case EntityType::Wipeout:
        case EntityType::Image:
        case EntityType::Text:
        case EntityType::Dimension:
            break; // fills / pictures / annotations are not boundaries
        case EntityType::Polyline:
            // A DOUBLE polyline bounds with its two VISIBLE physical runs, NOT the
            // centerline — so the hatch stops at the inner line, not in the gap.
            if (e.meta.value("double").toBool() && e.points.size() >= 2) {
                const double gap = e.meta.value("gap").toDouble(10.0);
                std::vector<double> radii(e.points.size(), 0.0);
                bool anyFillet = false;
                if (e.meta.contains("cornerRadii")) {
                    const QJsonArray rArr = e.meta.value("cornerRadii").toArray();
                    for (int i = 0; i < rArr.size() && i < static_cast<int>(radii.size()); ++i) {
                        radii[i] = rArr[i].toDouble();
                        if (radii[i] > 0) anyFillet = true;
                    }
                } else if (double cr = e.meta.value("cornerRadius").toDouble(0.0); cr > 0) {
                    std::fill(radii.begin(), radii.end(), cr);
                    anyFillet = true;
                }
                std::vector<double> centerRadii(radii.size(), 0.0);
                for (size_t i = 0; i < radii.size(); ++i)
                    if (radii[i] > 0) centerRadii[i] = radii[i] + gap / 2.0;
                const std::vector<Vec2> center =
                    anyFillet ? roundedPolylinePerCorner(e.points, centerRadii) : e.points;
                for (double s : {gap / 2.0, -gap / 2.0}) {
                    const auto run = offsetPolyline(center, s);
                    for (size_t i = 1; i < run.size(); ++i) addSeg(run[i - 1], run[i]);
                }
                break;
            }
            [[fallthrough]];
        default:
            for (const auto& s : entitySelectSegments(e)) addSeg(s.first, s.second);
            break;
        }
    }
    auto loop = traceEnclosingLoop(segs, seed);
    if (!loop || loop->size() < 3) return std::nullopt;
    // Safety: the walked cell must actually contain the seed (rejects the outer face).
    if (!pointInPolygon(*loop, seed)) return std::nullopt;
    return loop;
}

Editor::HatchRegion Editor::pickHatchRegion(const Vec2& seed, bool allowTrace) const {
    HatchRegion r;
    const Entity* ent = enclosingEntity(seed);
    std::optional<std::vector<Vec2>> loop =
        allowTrace ? enclosingRegionLoop(seed) : std::optional<std::vector<Vec2>>{};
    const double entArea =
        ent ? polygonArea(boundaryPointsOf(*ent, true)) : std::numeric_limits<double>::max();
    const double loopArea = loop ? polygonArea(*loop) : std::numeric_limits<double>::max();
    // A traced cell that is meaningfully SMALLER than the enclosing object means
    // other objects subdivide it (e.g. a line crossing a rectangle) — fill the cell.
    if (loop && loopArea + 1e-6 < entArea) { r.loop = std::move(loop); return r; }
    if (ent) { r.ent = ent; return r; }
    if (loop) { r.loop = std::move(loop); return r; }
    return r;
}

void Editor::commitHatchFromSource(const Entity& src) {
    // A circle filled with "few joints" stays a true circle (4 resize grips);
    // everything else becomes a polygon boundary.
    if (tool_ == ToolId::Hatch && !hatchStyle_.manyJoints && src.type == EntityType::Circle &&
        src.radius > 1e-6) {
        commitHatchCircle(src.center, src.radius);
        return;
    }
    std::vector<Vec2> bnd = boundaryPointsOf(src, hatchStyle_.manyJoints);
    if (bnd.size() >= 3) commitHatchBoundary(bnd);
}

void Editor::addHatchEntity(Entity e) {
    if (!doc_) return;
    if (e.id.isEmpty()) e.id = uid();
    e.layer = doc_->activeLayer();
    const EntityType ty = e.type;
    if (!hatchStyle_.color.isEmpty()) e.color = hatchStyle_.color; // else ByLayer
    if (ty == EntityType::Hatch) {
        e.meta.insert("pattern", hatchStyle_.pattern);
        e.meta.insert("hatchAngle", hatchStyle_.angle);
        e.meta.insert("hatchScale", hatchStyle_.scale);
        if (hatchStyle_.transparency > 0) e.meta.insert("transparency", hatchStyle_.transparency);
        e.meta.insert("associative", hatchStyle_.associative);
    }
    const QString newId = e.id;
    pushUndo();
    addEntity(std::move(e));
    if (ty == EntityType::Hatch) lastHatchId_ = newId; // live-edit target while creating
    emit message(ty == EntityType::Hatch ? QStringLiteral("Hatch created — adjust it in the Hatch tab.")
                                         : QStringLiteral("Wipeout created."));
    emit changed();
    emit hatchTargetChanged();
    // Stay in the tool so more regions can be filled (Esc / Close exits).
}

void Editor::commitHatchBoundary(const std::vector<Vec2>& pts) {
    if (!doc_ || pts.size() < 3) return;
    Entity e;
    e.type = (tool_ == ToolId::Hatch) ? EntityType::Hatch : EntityType::Wipeout;
    e.points = pts;
    addHatchEntity(std::move(e));
}

void Editor::commitHatchCircle(const Vec2& center, double radius) {
    if (!doc_ || radius <= 1e-6) return;
    Entity e;
    e.type = (tool_ == ToolId::Hatch) ? EntityType::Hatch : EntityType::Wipeout;
    e.center = center;
    e.radius = radius;
    e.meta.insert("shape", QStringLiteral("circle")); // fills a true circle; 4 resize joints
    addHatchEntity(std::move(e));
}

namespace {

// ---- Tangent-circle construction (Tan-Tan-Radius / Tan-Tan-Tan) -------------
// An object the new circle must be tangent to: either an (infinite) line or a circle.
struct TanTarget {
    bool isCircle = false;
    Vec2 p, dir;        // line: a point on it + a direction
    Vec2 c; double r{}; // circle: center + radius
};

std::optional<TanTarget> tanTargetOf(const Entity& e) {
    TanTarget t;
    switch (e.type) {
    case EntityType::Line:
    case EntityType::DoubleLine:
    case EntityType::Dimension:
        t.isCircle = false; t.p = e.a; t.dir = e.b - e.a;
        if (t.dir.lengthSq() < 1e-12) return std::nullopt;
        return t;
    case EntityType::Circle:
    case EntityType::Arc:
        t.isCircle = true; t.c = e.center; t.r = e.radius;
        return t;
    default:
        return std::nullopt;
    }
}

// The locus of centers at distance r from a target is a pair of lines (for a line
// target) or a pair of concentric circles (for a circle target). A "locus" here is
// either a line (point+dir) or a circle (center+radius).
struct CenterLocus { bool isCircle; Vec2 p, d; Vec2 c; double r; };

std::vector<CenterLocus> centerLoci(const TanTarget& t, double r) {
    std::vector<CenterLocus> L;
    if (!t.isCircle) {
        const Vec2 u = t.dir.normalized();
        const Vec2 n = u.perp();
        L.push_back({false, t.p + n * r, u, {}, 0});
        L.push_back({false, t.p - n * r, u, {}, 0});
    } else {
        L.push_back({true, {}, {}, t.c, t.r + r});          // externally tangent
        L.push_back({true, {}, {}, t.c, std::abs(t.r - r)}); // internally tangent
    }
    return L;
}

std::vector<Vec2> intersectLoci(const CenterLocus& A, const CenterLocus& B) {
    if (!A.isCircle && !B.isCircle) {
        if (auto x = lineLineIntersection(A.p, A.p + A.d, B.p, B.p + B.d)) return {*x};
        return {};
    }
    if (A.isCircle && !B.isCircle) return lineCircleIntersection(B.p, B.d, A.c, A.r);
    if (!A.isCircle && B.isCircle) return lineCircleIntersection(A.p, A.d, B.c, B.r);
    return circleCircleIntersection(A.c, A.r, B.c, B.r);
}

// Candidate centers for a circle of radius r tangent to both targets.
std::vector<Vec2> ttrCenters(const TanTarget& t1, const TanTarget& t2, double r) {
    std::vector<Vec2> out;
    for (const auto& A : centerLoci(t1, r))
        for (const auto& B : centerLoci(t2, r))
            for (const auto& x : intersectLoci(A, B)) out.push_back(x);
    return out;
}

// Candidate centers tangent to three LINE targets (incircle / excircles): the
// center lies on an angle bisector of each pair. Returns up to 4 points.
std::vector<Vec2> tttLineCenters(const TanTarget& a, const TanTarget& b, const TanTarget& c) {
    std::vector<Vec2> out;
    const Vec2 ua = a.dir.normalized(), ub = b.dir.normalized(), uc = c.dir.normalized();
    const auto q01 = lineLineIntersection(a.p, a.p + ua, b.p, b.p + ub);
    const auto q02 = lineLineIntersection(a.p, a.p + ua, c.p, c.p + uc);
    if (!q01 || !q02) return out; // some lines parallel
    for (const Vec2& b01 : {ua + ub, ua - ub})
        for (const Vec2& b02 : {ua + uc, ua - uc}) {
            if (b01.lengthSq() < 1e-12 || b02.lengthSq() < 1e-12) continue;
            if (auto x = lineLineIntersection(*q01, *q01 + b01, *q02, *q02 + b02)) out.push_back(*x);
        }
    return out;
}

// Distance from a point to a target (perp distance to a line, ring distance to a circle).
double distToTarget(const Vec2& pt, const TanTarget& t) {
    if (t.isCircle) return std::abs(distance(pt, t.c) - t.r);
    const Vec2 u = t.dir.normalized();
    return std::abs(dot(pt - t.p, u.perp()));
}

} // namespace

void Editor::handleTangentPick(const Vec2& p) {
    auto hit = hitTest(p);
    if (!hit) {
        emit message(QStringLiteral("Pick an object for the circle to touch (line or circle)"));
        return;
    }
    // Must be a tangentable object, and not already picked.
    if (Entity* e = entityById(*hit); !e || !tanTargetOf(*e)) {
        emit message(QStringLiteral("Tangent works on lines, circles and arcs"));
        return;
    }
    if (std::find(tanPickIds_.begin(), tanPickIds_.end(), *hit) != tanPickIds_.end()) {
        emit message(QStringLiteral("Object already chosen — pick a different one"));
        return;
    }
    tanPickIds_.push_back(*hit);
    tanPickPts_.push_back(p);

    const int need = (circleMode_ == CircleMode::TanTanTan) ? 3 : 2;
    if (static_cast<int>(tanPickIds_.size()) < need) {
        emit message(QStringLiteral("Pick object %1 of %2").arg(tanPickIds_.size() + 1).arg(need));
        return;
    }
    if (circleMode_ == CircleMode::TanTanRadius) {
        tanAwaitRadius_ = true;
        emit message(QStringLiteral("Enter the circle radius <%1>").arg(tanRadius_));
        return;
    }
    buildTangentCircle(); // Tan-Tan-Tan: enough objects, solve now
}

void Editor::buildTangentCircle() {
    if (!doc_) return;
    std::vector<TanTarget> targets;
    for (const auto& id : tanPickIds_)
        if (Entity* e = entityById(id))
            if (auto t = tanTargetOf(*e)) targets.push_back(*t);

    // Reference point to choose among the multiple tangent solutions.
    Vec2 ref{0, 0};
    for (const auto& q : tanPickPts_) ref = ref + q;
    if (!tanPickPts_.empty()) ref = ref * (1.0 / tanPickPts_.size());

    std::vector<Vec2> centers;
    double radius = tanRadius_;
    if (circleMode_ == CircleMode::TanTanRadius && targets.size() >= 2) {
        centers = ttrCenters(targets[0], targets[1], tanRadius_);
    } else if (circleMode_ == CircleMode::TanTanTan && targets.size() >= 3) {
        if (targets[0].isCircle || targets[1].isCircle || targets[2].isCircle) {
            emit message(QStringLiteral("Tan-Tan-Tan currently supports three lines"));
            tanPickIds_.clear(); tanPickPts_.clear(); tanAwaitRadius_ = false;
            return;
        }
        centers = tttLineCenters(targets[0], targets[1], targets[2]);
    }

    // Pick the candidate center nearest the average of the pick points.
    if (centers.empty()) {
        emit message(QStringLiteral("No tangent circle fits those objects — try a different radius"));
        tanPickIds_.clear(); tanPickPts_.clear(); tanAwaitRadius_ = false;
        return;
    }
    Vec2 best = centers[0];
    double bestD = distance(best, ref);
    for (const auto& cc : centers) {
        const double dd = distance(cc, ref);
        if (dd < bestD) { bestD = dd; best = cc; }
    }
    if (circleMode_ == CircleMode::TanTanTan)
        radius = distToTarget(best, targets[0]); // incircle radius = distance to a line

    if (radius < 1e-6) {
        emit message(QStringLiteral("Degenerate tangent circle"));
        tanPickIds_.clear(); tanPickPts_.clear(); tanAwaitRadius_ = false;
        return;
    }
    Entity e;
    e.type = EntityType::Circle;
    e.layer = doc_->activeLayer();
    e.center = best;
    e.radius = radius;
    pushUndo();
    addEntity(std::move(e));
    tanPickIds_.clear();
    tanPickPts_.clear();
    tanAwaitRadius_ = false;
    emit message(QStringLiteral("Tangent circle created"));
    emit changed();
    setTool(ToolId::Select);
}

namespace {

// Build an Arc from its construction points, set up so the renderer's CCW sweep
// traces the intended arc (AutoCAD arcs go counter-clockwise from start to end).
// For 3-point / start-end-bulge modes the middle point decides the direction.
// Needs 3 points; returns nullopt if degenerate.
std::optional<Entity> buildArcEntity(Editor::ArcMode mode, const std::vector<Vec2>& pts,
                                     bool mirror = false) {
    if (pts.size() < 3) return std::nullopt;
    const Vec2 p0 = pts[0], p1 = pts[1], p2 = pts[2];
    Entity e;
    e.type = EntityType::Arc;
    // start/end chosen so a CCW sweep from start to end passes through `mid`.
    auto ccwThrough = [&](const Vec2& c, const Vec2& s, const Vec2& mid, const Vec2& en) {
        e.center = c;
        e.radius = distance(c, s);
        auto norm = [](double a) {
            const double t = 2.0 * M_PI;
            a = std::fmod(a, t);
            if (a < 0) a += t;
            return a;
        };
        const double as = std::atan2(s.y - c.y, s.x - c.x);
        const double ae = std::atan2(en.y - c.y, en.x - c.x);
        const double am = std::atan2(mid.y - c.y, mid.x - c.x);
        if (norm(am - as) <= norm(ae - as)) { e.startAngle = as; e.endAngle = ae; }
        else { e.startAngle = ae; e.endAngle = as; }
    };
    switch (mode) {
    case Editor::ArcMode::CenterStartEnd: // center, start, end (CCW start->end)
        e.center = p0;
        e.radius = distance(p0, p1);
        e.startAngle = std::atan2(p1.y - p0.y, p1.x - p0.x);
        e.endAngle = std::atan2(p2.y - p0.y, p2.x - p0.x);
        break;
    case Editor::ArcMode::StartCenterEnd: // start, center, end (CCW start->end)
        e.center = p1;
        e.radius = distance(p1, p0);
        e.startAngle = std::atan2(p0.y - p1.y, p0.x - p1.x);
        e.endAngle = std::atan2(p2.y - p1.y, p2.x - p1.x);
        break;
    case Editor::ArcMode::StartEndRadius: { // start, end, point-on-arc (bulge)
        Vec2 cc; double rr = 0;
        if (!circumcircle(p0, p2, p1, cc, rr)) return std::nullopt;
        ccwThrough(cc, p0, p2, p1);
        break;
    }
    case Editor::ArcMode::ThreePoint: { // start, point-on-arc, end
        Vec2 cc; double rr = 0;
        if (!circumcircle(p0, p1, p2, cc, rr)) return std::nullopt;
        ccwThrough(cc, p0, p1, p2);
        break;
    }
    }
    if (e.radius < 1e-9) return std::nullopt;
    // Mirror: take the complementary arc (the renderer sweeps CCW, so swapping the
    // start/end angles flips the bulge to the opposite half).
    if (mirror) std::swap(e.startAngle, e.endAngle);
    return e;
}

// Build a (possibly rotated) Ellipse from its construction points, AutoCAD-style:
//  - Center:        p0=center, p1=axis endpoint (half-axis a + rotation), p2=other half-axis.
//  - Axis,End/Arc:  p0,p1=endpoints of axis 1, p2=distance for the other half-axis.
// The first axis sets the rotation; the third point's perpendicular distance to
// the center is the other semi-axis. Needs 3 points.
std::optional<Entity> buildEllipseEntity(Editor::EllipseMode mode, const std::vector<Vec2>& pts) {
    if (pts.size() < 3) return std::nullopt;
    const Vec2 p0 = pts[0], p1 = pts[1], p2 = pts[2];
    const Vec2 axis = p1 - p0;
    const double axisLen = axis.length();
    if (axisLen < 1e-9) return std::nullopt;
    const Vec2 center = (mode == Editor::EllipseMode::Center) ? p0 : lerp(p0, p1, 0.5);
    const double a = (mode == Editor::EllipseMode::Center) ? axisLen : axisLen / 2.0;
    const Vec2 u = axis / axisLen;
    const Vec2 n = u.perp();
    const double b = std::abs(dot(p2 - center, n));
    if (a < 1e-9 || b < 1e-9) return std::nullopt;
    Entity e;
    e.type = EntityType::Ellipse;
    e.center = center;
    e.radius = a;   // semi-axis along the rotated local X (the first axis)
    e.radiusY = b;  // semi-axis along the rotated local Y
    e.rotation = std::atan2(u.y, u.x);
    return e;
}

// Parametric angle (radians) on the ellipse boundary toward world point p — used
// to convert the picked start/end points into elliptical-arc angles.
double ellipseParamAngle(const Vec2& center, double rx, double ry, double rot, const Vec2& p) {
    const double dx = p.x - center.x, dy = p.y - center.y;
    const double c = std::cos(rot), s = std::sin(rot);
    const double lx = dx * c + dy * s;  // inverse rotation into the ellipse's frame
    const double ly = -dx * s + dy * c;
    return std::atan2(ly / std::max(1e-9, ry), lx / std::max(1e-9, rx));
}

} // namespace

void Editor::finishDraw() {
    if (!doc_ || draftPts_.empty()) { cancelDraw(); return; }
    Entity e;
    e.layer = doc_->activeLayer();
    bool filletTooBig = false; // set if a polyline fillet radius won't fit (warn after)

    switch (tool_) {
    case ToolId::TwistedRoute:
        // The same pair, routed: the knees ARE the object, so they are kept as its points.
        if (draftPts_.size() < 2) { cancelDraw(); return; }
        e.type = EntityType::TwistedPair;
        e.points = draftPts_;
        e.a = draftPts_.front();
        e.b = draftPts_.back();
        e.meta.insert(QStringLiteral("twistWidth"), kTwistedPairWidth);
        e.meta.insert(QStringLiteral("twistPitch"), kTwistedPairPitch);
        e.meta.insert(QStringLiteral("twistEnds"), kTwistedPairLead);
        break;
    case ToolId::TwistedHarness:
        // A whole bundle laid up together: the same drawing as a pair, with the number of wires the
        // ribbon is set to. Two wires IS a pair, so there is one description and not two.
        if (draftPts_.size() < 2) return;
        e.type = EntityType::TwistedPair;
        e.a = draftPts_[0];
        e.b = draftPts_[1];
        e.meta.insert(QStringLiteral("twistWires"), std::clamp(twistWires_, 2, kTwistedMaxWires));
        e.meta.insert(QStringLiteral("twistWidth"), kTwistedPairWidth * 1.5);
        e.meta.insert(QStringLiteral("twistPitch"), kTwistedPairPitch);
        e.meta.insert(QStringLiteral("twistEnds"), kTwistedPairLead);
        break;
    case ToolId::TwistedPair:
        // Drawn like a line, between the two points the pair runs between. What shapes it - how far
        // apart the wires are, how long a crossing is, how much straight end to leave - is carried on
        // the object, so it can be changed afterwards without re-drawing it.
        if (draftPts_.size() < 2) return;
        e.type = EntityType::TwistedPair;
        e.a = draftPts_[0];
        e.b = draftPts_[1];
        e.meta.insert(QStringLiteral("twistWidth"), kTwistedPairWidth);
        e.meta.insert(QStringLiteral("twistPitch"), kTwistedPairPitch);
        e.meta.insert(QStringLiteral("twistEnds"), kTwistedPairLead);
        break;
    case ToolId::Line:
    case ToolId::DoubleLine:
        if (draftPts_.size() < 2) return;
        e.type = (tool_ == ToolId::DoubleLine) ? EntityType::DoubleLine : EntityType::Line;
        e.a = draftPts_[0];
        e.b = draftPts_[1];
        if (e.type == EntityType::DoubleLine) {
            // Drawn along the cursor: the rail under the crosshair goes through the clicked points and
            // the gap goes to the other side of it.
            const Vec2 shift = doubleRunCentreOffset(draftPts_[0], draftPts_[1], doubleLineGap_,
                                                     doubleSecondBelow());
            e.a = e.a + shift;
            e.b = e.b + shift;
            e.meta.insert("gap", doubleLineGap_);
            if (dpolyHasLsRef_) {
                e.meta.insert("lsRef", true);
                e.meta.insert("lsRefAx", lsRefA_.x);
                e.meta.insert("lsRefAy", lsRefA_.y);
                e.meta.insert("lsRefBx", lsRefB_.x);
                e.meta.insert("lsRefBy", lsRefB_.y);
            }
        }
        break;
    case ToolId::Dimension:
        e.type = EntityType::Dimension;
        e.meta.insert("dimType", QString::fromLatin1(dimTypeString(dimMode_)));
        // AutoCAD-style style defaults (editable in Properties).
        e.meta.insert("textHeight", 5.0); // default text height
        e.meta.insert("arrowSize", 3.3);
        e.meta.insert("extOffset", 2.0);  // extension-line gap from the measured point
        e.meta.insert("decimals", 2);
        switch (dimMode_) {
        case DimMode::Linear:
        case DimMode::Aligned:
            if (draftPts_.size() < 3) { cancelDraw(); return; }
            e.a = draftPts_[0];
            e.b = draftPts_[1];
            e.points = {draftPts_[0], draftPts_[1], draftPts_[2]};
            // Which axis a linear one was placed measuring along is written down here, once, from where
            // the stand-off point was put. Everything afterwards reads it rather than working it out
            // again, so the dimension cannot turn from a height into a length while it is being moved.
            if (dimMode_ == DimMode::Linear)
                e.meta.insert("dimAcross", dimensionAcrossX(e));
            break;
        case DimMode::Angular:
            if (draftPts_.size() < 3) { cancelDraw(); return; }
            e.points = {draftPts_[0], draftPts_[1], draftPts_[2]};
            break;
        case DimMode::Ordinate:
            if (draftPts_.size() < 2) { cancelDraw(); return; }
            e.a = draftPts_[0];
            e.b = draftPts_[1];
            break;
        case DimMode::Radius:
        case DimMode::Diameter:
        case DimMode::ArcLength:
        case DimMode::Jogged:
            if (!dimHasRef_ || draftPts_.empty()) { cancelDraw(); return; }
            e.center = dimRefCenter_;
            e.radius = dimRefRadius_;
            e.startAngle = dimRefStart_;
            e.endAngle = dimRefEnd_;
            e.b = draftPts_.back(); // text / leader position
            e.points = {draftPts_.back()};
            break;
        }
        break;
    case ToolId::Rect: {
        if (draftPts_.size() < 2) return;
        const Vec2 a = draftPts_[0], b = draftPts_[1];
        e.type = EntityType::Rect;
        e.points = {{a.x, a.y}, {b.x, a.y}, {b.x, b.y}, {a.x, b.y}};
        break;
    }
    case ToolId::Note:
    case ToolId::Revision: {
        // A bordered annotation box (a Rect carrying its text). `a` = the top-left
        // corner so the text can be edited inline (double-click). Revision boxes also
        // get a title strip + a revision id.
        if (draftPts_.size() < 2) return;
        const Vec2 a = draftPts_[0], b = draftPts_[1];
        const double minX = std::min(a.x, b.x), maxX = std::max(a.x, b.x);
        const double minY = std::min(a.y, b.y), maxY = std::max(a.y, b.y);
        e.type = EntityType::Rect;
        e.points = {{minX, minY}, {maxX, minY}, {maxX, maxY}, {minX, maxY}};
        e.a = {minX, maxY}; // top-left in world (y up)
        e.meta.insert("textSize", 4.0);
        if (tool_ == ToolId::Note) {
            e.meta.insert("note", true);
            e.meta.insert("text", QStringLiteral("Note"));
        } else {
            e.meta.insert("revision", true);
            e.meta.insert("text", QStringLiteral("Revision description"));
            // Next revision letter (A, B, C, ...) across existing revision boxes.
            int idx = 0;
            for (const auto& x : doc_->entities())
                if (x.meta.value("revision").toBool()) ++idx;
            e.meta.insert("rev", QString(QChar('A' + std::min(idx, 25))));
        }
        break;
    }
    case ToolId::Balloon:
    case ToolId::TextLeader: {
        // A leader callout: first point = arrow tip, second = the label location.
        // Stored as a Circle carrying meta["balloon"] + the arrow tip + landing. A
        // Balloon shows a numbered circle; a TextLeader shows plain text (no circle).
        if (draftPts_.size() < 2) return;
        const Vec2 tip = draftPts_[0], c = draftPts_[1];
        const bool textOnly = (tool_ == ToolId::TextLeader);
        e.type = EntityType::Circle;
        e.center = c;
        e.a = c;
        e.radius = 6.0;
        e.meta.insert("balloon", true);
        if (textOnly) {
            e.meta.insert("textLeader", true);
            e.meta.insert("text", QStringLiteral("Text"));
        } else {
            if (!balloonTextNext_.isEmpty()) {
                // Started from a bill of materials line: it already knows its number.
                e.meta.insert("text", balloonTextNext_);
                balloonTextNext_.clear();
            } else {
                int next = 0;
                for (const auto& x : doc_->entities())
                    if (x.meta.value("balloon").toBool())
                        next = std::max(next, x.meta.value("text").toString().toInt());
                e.meta.insert("text", QString::number(next + 1));
            }
        }
        e.meta.insert("landing", 8.0); // horizontal landing length (editable in Properties)
        e.meta.insert("landingSide", (tip.x >= c.x) ? 1.0 : -1.0); // exits toward the tip's side
        setBalloonLeaders(e, {tip});   // one leader to start; Add Leader adds more
        break;
    }
    case ToolId::Polygon: {
        if (draftPts_.size() < 2) return;
        const Vec2 c = draftPts_[0];
        const Vec2 v = draftPts_[1];
        e.type = EntityType::Polygon;
        e.points = regularPolygonPoints(c, v, polygonSides_);
        break;
    }
    case ToolId::Circle:
        if (draftPts_.size() < 2) return;
        e.type = EntityType::Circle;
        switch (circleMode_) {
        case CircleMode::CenterRadius:
            e.center = draftPts_[0];
            e.radius = distance(draftPts_[0], draftPts_[1]);
            break;
        case CircleMode::CenterDiameter:
            e.center = draftPts_[0];
            e.radius = distance(draftPts_[0], draftPts_[1]) / 2.0;
            break;
        case CircleMode::TwoPoint:
            e.center = lerp(draftPts_[0], draftPts_[1], 0.5);
            e.radius = distance(draftPts_[0], draftPts_[1]) / 2.0;
            break;
        case CircleMode::ThreePoint: {
            if (draftPts_.size() < 3) return;
            Vec2 cc; double rr = 0;
            if (!circumcircle(draftPts_[0], draftPts_[1], draftPts_[2], cc, rr)) {
                emit message(QStringLiteral("Points are collinear — cannot make a circle"));
                cancelDraw();
                return;
            }
            e.center = cc;
            e.radius = rr;
            break;
        }
        }
        if (e.radius < 1e-9) { cancelDraw(); return; }
        break;
    case ToolId::Ellipse: {
        if (auto el = buildEllipseEntity(ellipseMode_, draftPts_)) {
            const QString layer = e.layer;
            e = *el;
            e.layer = layer;
            // Elliptical arc: the 4th/5th picks set the start/end angles, opening
            // the ellipse into an arc.
            if (ellipseMode_ == EllipseMode::EllipticalArc && draftPts_.size() >= 5) {
                e.startAngle =
                    ellipseParamAngle(e.center, e.radius, e.radiusY, e.rotation, draftPts_[3]);
                e.endAngle =
                    ellipseParamAngle(e.center, e.radius, e.radiusY, e.rotation, draftPts_[4]);
                e.meta.insert("ellipticalArc", true);
            }
        } else {
            cancelDraw();
            return;
        }
        break;
    }
    case ToolId::Arc: {
        if (auto ar = buildArcEntity(arcMode_, draftPts_, arcMirror_)) {
            const QString layer = e.layer;
            e = *ar;
            e.layer = layer;
        } else {
            cancelDraw();
            return;
        }
        break;
    }
    case ToolId::Polyline:
    case ToolId::DoublePolyline:
    case ToolId::Spline:
        if (draftPts_.size() < 2) { cancelDraw(); return; }
        e.type = (tool_ == ToolId::Spline) ? EntityType::Spline : EntityType::Polyline;
        e.points = draftPts_;
        if (tool_ == ToolId::DoublePolyline) {
            e.points = doubleRunCentre(draftPts_, doubleLineGap_, doubleSecondBelow());
            e.meta.insert("double", true);
            e.meta.insert("gap", doubleLineGap_);
            if (dpolyHasLsRef_) {
                e.meta.insert("lsRef", true);
                e.meta.insert("lsRefAx", lsRefA_.x);
                e.meta.insert("lsRefAy", lsRefA_.y);
                e.meta.insert("lsRefBx", lsRefB_.x);
                e.meta.insert("lsRefBy", lsRefB_.y);
            }
        }
        if (e.type == EntityType::Polyline) {
            // Per-corner fillet: each interior vertex carries the radius that was
            // active when its corner was completed (0 = sharp). A radius too big for
            // its corner is dropped to sharp (and flagged), never silently clamped.
            // Double poly rounds the centreline at radius + half the gap.
            std::vector<double> radii = draftCornerRadii_;
            radii.resize(e.points.size(), 0.0);
            const double extra =
                (tool_ == ToolId::DoublePolyline) ? doubleLineGap_ / 2.0 : 0.0;
            bool anyFillet = false;
            for (size_t i = 1; i + 1 < e.points.size(); ++i) {
                if (radii[i] <= 0.0) continue;
                const std::vector<Vec2> corner = {e.points[i - 1], e.points[i], e.points[i + 1]};
                if (filletRadiusFits(corner, radii[i] + extra))
                    anyFillet = true;
                else { radii[i] = 0.0; filletTooBig = true; }
            }
            if (anyFillet) {
                QJsonArray arr;
                for (double r : radii) arr.append(r);
                e.meta.insert("cornerRadii", arr);
            }
        }
        if (e.type == EntityType::Spline && splineMode_ == SplineMode::CV)
            e.meta.insert("splineMode", QStringLiteral("cv")); // else Fit (default)
        // Arc segments: store bulges if any segment is curved.
        if (e.type == EntityType::Polyline &&
            std::any_of(draftBulges_.begin(), draftBulges_.end(),
                        [](double b) { return std::abs(b) > 1e-9; })) {
            QJsonArray arr;
            for (size_t i = 0; i < e.points.size(); ++i)
                arr.append(i < draftBulges_.size() ? draftBulges_[i] : 0.0);
            e.meta.insert("bulges", arr);
        }
        break;
    case ToolId::Wire:
        if (draftPts_.size() < 2) { cancelDraw(); return; }
        e.type = EntityType::Wire;
        e.layer = QStringLiteral("Wires");
        e.points = draftPts_;
        e.meta.insert("wireColor", wireColor_);
        e.meta.insert("wireGauge", wireGauge_);
        e.meta.insert("netName", wireNet_);
        // Seed the wire's own colour from the code it was drawn with: the colour property is
        // what gets drawn, so the two must agree to begin with.
        if (!e.hasExplicitColor() && !wireColor_.isEmpty())
            e.color = wireColorHex(wireColor_);
        // Wire ribbon defaults carried onto the new wire (blank ones are simply absent).
        if (!wireAwg_.isEmpty()) e.meta.insert("awg", wireAwg_);
        if (!wireInsulation_.isEmpty()) e.meta.insert("insulation", wireInsulation_);
        if (!wireVoltage_.isEmpty()) e.meta.insert("insulationVoltage", wireVoltage_);
        if (!wireType_.isEmpty()) e.meta.insert("conductorType", wireType_);
        if (wireRes_ > 0.0) {
            e.meta.insert("resistance", wireRes_);
            e.meta.insert("resUnit", wireResUnit_);
        }
        e.meta.insert("showLabel", true);
        // Marking defaults from the Wire ribbon: colour / cross-section written along the
        // wire, independently switched, repeated `annCount` times.
        e.meta.insert("showColorName", wireShowColorName_);
        e.meta.insert("showGaugeName", wireShowGaugeName_);
        e.meta.insert("colorNameMode", wireColorNameMode_);
        e.meta.insert("annCount", std::clamp(wireAnnotCount_, 1, 20));
        e.meta.insert("annSize", wireAnnotSize_);
        e.meta.insert("entNum", assignEntityNumber()); // unique entity number
        // Shift-fanout: one wire per connector pin, each the drafted route offset by that
        // pin's distance from the anchor pin. Commits them all as one undo step.
        if (wireArray_ && wireArrayOffsets_.size() > 1) {
            const int n = static_cast<int>(wireArrayOffsets_.size());
            pushUndo();
            for (const Vec2& off : wireArrayOffsets_) {
                Entity w = e; // same wire properties (color/awg/net/...)
                w.id.clear();
                w.points = fanRoute(draftPts_, off);
                w.meta.insert("entNum", assignEntityNumber());
                addEntity(std::move(w));
            }
            wireArray_ = false;
            wireArrayOffsets_.clear();
            wireFanPins_.clear();
            wireFanAnchor_ = -1;
            wireFanSel_.clear();
            draftPts_.clear();
            draftBulges_.clear();
            draftCornerRadii_.clear();
            drafting_ = false;
            emit changed();
            emit message(
                QStringLiteral("Wired %1 pins — click to draw another; Esc to finish").arg(n));
            return;
        }
        // Drawn from (or onto) an existing wire's free end: carry that wire on instead of
        // leaving two objects lying end to end. The run becomes one whole wire, so it keeps
        // one number, one colour and one gauge — and reads as one conductor everywhere it
        // matters (properties, From/To, the BOM, shields and twists).
        if (extendExistingWire(draftPts_)) {
            draftPts_.clear();
            draftBulges_.clear();
            draftCornerRadii_.clear();
            drafting_ = false;
            emit changed();
            return;
        }
        break;
    case ToolId::Hatch:
    case ToolId::Wipeout:
        if (draftPts_.size() < 3) { cancelDraw(); return; }
        e.type = (tool_ == ToolId::Hatch) ? EntityType::Hatch : EntityType::Wipeout;
        e.points = draftPts_;
        if (e.type == EntityType::Hatch) {
            e.meta.insert("pattern", hatchStyle_.pattern);
            e.meta.insert("hatchAngle", hatchStyle_.angle);
            e.meta.insert("hatchScale", hatchStyle_.scale);
            if (hatchStyle_.transparency > 0)
                e.meta.insert("transparency", hatchStyle_.transparency);
            e.meta.insert("associative", hatchStyle_.associative);
        }
        break;
    default:
        cancelDraw();
        return;
    }

    // Shift-clicking a run-end onto a line while drawing a double line/polyline caps
    // that end flush to the line (both physical runs meet it), like the edit gesture.
    if (e.type == EntityType::DoubleLine ||
        (e.type == EntityType::Polyline && e.meta.value("double").toBool())) {
        // The line to start flush on is the one under the CLICK - which is on the rail the cursor rode,
        // not on the centreline the run is stored as. Looking for it under the centreline found nothing
        // the moment the run stopped being drawn down the middle, so Shift quietly did nothing.
        const bool dbl = e.type == EntityType::DoubleLine;
        const Vec2 startPt = draftPts_.empty()
                                 ? (dbl ? e.a : (e.points.empty() ? Vec2{0, 0} : e.points.front()))
                                 : draftPts_.front();
        const Vec2 endPt = draftPts_.empty()
                               ? (dbl ? e.b : (e.points.empty() ? Vec2{0, 0} : e.points.back()))
                               : draftPts_.back();
        if (drawStartShift_) {
            capDoubleEndAt(e, QStringLiteral("cutStart"), startPt, doc_->entities());
            if (!e.meta.value(QStringLiteral("cutStart")).toBool())
                emit message(QStringLiteral("Shift-click landed on nothing, so there is no line for "
                                            "the run to start from"));
        }
        if (drawEndShift_) capDoubleEndAt(e, QStringLiteral("cutEnd"), endPt, doc_->entities());
    }
    drawStartShift_ = drawEndShift_ = false;

    const bool keepWire = (tool_ == ToolId::Wire); // wire placement is continuous until Esc
    pushUndo();
    const bool isWire = (e.type == EntityType::Wire);
    const Vec2 wireStart = isWire && !e.points.empty() ? e.points.front() : Vec2{0, 0};
    const Vec2 wireEnd = isWire && !e.points.empty() ? e.points.back() : Vec2{0, 0};
    if (e.id.isEmpty()) e.id = uid(); // minted here so the wire can be told from the ones it meets
    const QString newId = e.id;
    addEntity(std::move(e));
    // Where this wire meets others, the meeting is a splice. Counted EXCLUDING itself, so a
    // wire only splices where it actually met something.
    if (isWire) {
        // A pin is not a joint: two wires on one pin is a wiring fault for the validator to
        // report, not something to mark as a splice.
        const std::vector<Vec2> pins = collectPinEnds(doc_);
        auto onPin = [&pins](const Vec2& pt) {
            for (const Vec2& pe : pins)
                if (distance(pt, pe) < 1e-6) return true;
            return false;
        };
        if (!onPin(wireStart) && wireEndsAt(wireStart, newId) >= 1) addSpliceAt(wireStart);
        if (!onPin(wireEnd) && wireEndsAt(wireEnd, newId) >= 1) addSpliceAt(wireEnd);
        // And where an end lands on the BODY of another run - a T-junction. Two conductors that meet
        // in the middle of a run are joined there just as surely as two that meet end to end, and
        // without the dot the drawing cannot be told from one wire crossing over another.
        if (!onPin(wireStart) && wireBodyPointNear(wireStart, kOnPointTol, nullptr, newId))
            addSpliceAt(wireStart);
        if (!onPin(wireEnd) && wireBodyPointNear(wireEnd, kOnPointTol, nullptr, newId))
            addSpliceAt(wireEnd);
    }
    draftPts_.clear();
    drafting_ = false;
    emit changed();
    if (keepWire) {
        // Stay in the Wire tool (and its ribbon) so more wires/twists can be added; Esc
        // ends the session. Reset the per-wire draft chrome without leaving the tool.
        draftBulges_.clear();
        draftCornerRadii_.clear();
        emit message(QStringLiteral("Wire placed — click to draw another; Esc to finish"));
    } else {
        setTool(ToolId::Select); // finished: return to Select (Space repeats last tool)
    }
    if (filletTooBig) // emit after setTool so the warning isn't overwritten
        emit message(QStringLiteral("Fillet radius %1 is too big for a corner — drawn with sharp corners")
                         .arg(cornerRadius_));
}

void Editor::cancelDraw() {
    twistedFrozen_ = false;   // nothing pinned once the draft is gone
    lastHatchId_.clear(); // end the hatch live-edit session
    dimHasRef_ = false;   // forget any picked circle/arc for a radius/dia dimension
    dpolyLsArm_ = false;  // forget a pending/active line-start reference
    dpolyHasLsRef_ = false;
    dpolySwap_ = false;   // and which side the second line was put on
    draftPts_.clear();
    drafting_ = false;
    drawStartShift_ = false;
    drawEndShift_ = false;
    rectAwaitY_ = false;
    drawFieldIndex_ = 0;
    drawFieldVals_.clear();
    draftBulges_.clear();
    draftCornerRadii_.clear();
    cornerRadius_ = 0.0; // a fresh (double) polyline starts with no fillet until defined
    polyArcMode_ = false;
    arcMirror_ = false;
    tanPickIds_.clear();
    tanPickPts_.clear();
    tanAwaitRadius_ = false;
    wireArray_ = false; // end any wire fan-out session
    wireArrayOffsets_.clear();
    wireFanPins_.clear();
    wireFanAnchor_ = -1;
    wireFanSel_.clear();
}

void Editor::toggleArcMirror() {
    arcMirror_ = !arcMirror_;
    emit message(arcMirror_ ? QStringLiteral("Arc mirrored — drawing the opposite sweep")
                            : QStringLiteral("Arc mirror off"));
    emit overlayChanged();
}

bool Editor::applyDrawInput(const QString& text) {
    if (!doc_ || !drafting_ || draftPts_.empty()) return false;
    QString t = text.trimmed();
    if (t.isEmpty()) return false;

    const Vec2 last = draftPts_.back();
    Vec2 target;
    if (t.contains(',')) {
        // Absolute "x,y" or relative "@dx,dy".
        const bool rel = t.startsWith('@');
        if (rel) t = t.mid(1);
        const QStringList parts = t.split(',');
        if (parts.size() < 2) return false;
        bool okx = false, oky = false;
        const double a = parts[0].trimmed().toDouble(&okx);
        const double b = parts[1].trimmed().toDouble(&oky);
        if (!okx || !oky) return false;
        target = rel ? Vec2{last.x + a, last.y + b} : Vec2{a, b};
    } else {
        // Direct distance entry: length along the current cursor direction.
        bool ok = false;
        const double dist = t.toDouble(&ok);
        if (!ok) return false;
        Vec2 dir = cursor_ - last;
        const double len = dir.length();
        if (len < 1e-9) {
            emit message(QStringLiteral("Move the cursor to set a direction, then type the distance"));
            return true; // consumed, but nothing drawn yet
        }
        dir = dir / len;
        target = last + dir * dist;
    }

    handleDrawPress(target); // advance the in-progress draw exactly like a click
    return true;
}

std::vector<QString> Editor::drawInputFieldLabels() const {
    switch (tool_) {
    case ToolId::Line:
    case ToolId::DoubleLine:
    case ToolId::TwistedPair:
    case ToolId::TwistedRoute:
    case ToolId::TwistedHarness:
    case ToolId::Polyline:
    case ToolId::DoublePolyline:
    case ToolId::Spline:
    case ToolId::Wire:
        // A segment from the last point: length + angle.
        if (drafting_ && !draftPts_.empty())
            return {QStringLiteral("Length"), QStringLiteral("Angle")};
        return {};
    case ToolId::Circle:
        if (drafting_ && draftPts_.size() == 1) {
            if (circleMode_ == CircleMode::CenterDiameter || circleMode_ == CircleMode::TwoPoint)
                return {QStringLiteral("Diameter")};
            if (circleMode_ == CircleMode::CenterRadius)
                return {QStringLiteral("Radius")};
        }
        return {};
    case ToolId::Ellipse:
        // Step 1 defines the first axis (length + angle); step 2 the other half-axis.
        if (drafting_ && draftPts_.size() == 1)
            return {QStringLiteral("Length"), QStringLiteral("Angle")};
        if (drafting_ && draftPts_.size() == 2)
            return {QStringLiteral("Distance")};
        return {};
    case ToolId::Arc:
        // Center-based modes: type the radius/angle in the flying field.
        if (drafting_ && arcMode_ == ArcMode::CenterStartEnd) {
            if (draftPts_.size() == 1) return {QStringLiteral("Radius"), QStringLiteral("Angle")};
            if (draftPts_.size() == 2) return {QStringLiteral("Angle")};
        } else if (drafting_ && arcMode_ == ArcMode::StartCenterEnd) {
            if (draftPts_.size() == 1) return {QStringLiteral("Length"), QStringLiteral("Angle")};
            if (draftPts_.size() == 2) return {QStringLiteral("Angle")};
        }
        return {};
    default:
        return {};
    }
}

QString Editor::drawCommandHint() const {
    switch (tool_) {
    case ToolId::Line:
        return QStringLiteral("LINE: click points  ·  Enter ends the run  ·  Esc exits");
    case ToolId::Polyline:
        return QStringLiteral("POLYLINE: %1  ·  [A] arc / [L] line  ·  [F] fillet%2  ·  [C] close  ·  [U] undo  ·  Enter")
            .arg(polyArcMode_ ? QStringLiteral("ARC segments") : QStringLiteral("click points"),
                 cornerRadius_ > 0 ? QStringLiteral(" r=%1 ([FC] off)").arg(cornerRadius_) : QString());
    case ToolId::DoublePolyline:
        return QStringLiteral("DOUBLE POLYLINE: [G] gap  ·  [F] fillet%1  ·  [LS] line-start%2  ·  [C] close  ·  [U] undo  ·  Enter")
            .arg(cornerRadius_ > 0 ? QStringLiteral(" r=%1 ([FC] off)").arg(cornerRadius_) : QString(),
                 dpolyLsArm_ ? QStringLiteral(" (pick reference)") : QString());
    case ToolId::TwistedPair:
        return QStringLiteral(
            "TWISTED PAIR: click the two ends  ·  straight ends, crossings between them");
    case ToolId::TwistedRoute:
        return QStringLiteral(
            "TWISTED ROUTE: click each knee  ·  Enter finishes  ·  straight ends, weave between");
    case ToolId::TwistedHarness:
        return QStringLiteral("TWISTED HARNESS: click the two ends  ·  %1 wires, all laid up  ·  "
                              "Tab pins it to set it up on the ribbon")
            .arg(std::clamp(twistWires_, 2, kTwistedMaxWires));
    case ToolId::DoubleLine:
        return QStringLiteral("DOUBLE LINE: [G] gap  ·  [LS] line-start%1  ·  click two points")
            .arg(dpolyLsArm_ ? QStringLiteral(" (pick reference)") : QString());
    case ToolId::Spline:
        return QStringLiteral("SPLINE: click points  ·  [C] close  ·  [U] undo  ·  Enter finishes");
    case ToolId::Wire:
        return QStringLiteral("WIRE: click points  ·  double-click / Enter finishes");
    case ToolId::Arc:
        if (arcMode_ == ArcMode::CenterStartEnd || arcMode_ == ArcMode::StartCenterEnd)
            return QStringLiteral("ARC: pick points  ·  type Radius/Angle (Tab switches)  ·  "
                                  "[MI] mirror  ·  Esc cancels");
        return QStringLiteral("ARC: pick 3 points  ·  [MI] mirror  ·  Esc cancels");
    case ToolId::Circle:
        if (circleMode_ == CircleMode::TanTanRadius)
            return QStringLiteral("CIRCLE Tan-Tan-Radius: pick two objects, then type the radius");
        if (circleMode_ == CircleMode::TanTanTan)
            return QStringLiteral("CIRCLE Tan-Tan-Tan: pick three objects (lines)");
        return QStringLiteral("CIRCLE: pick center then radius  ·  type Radius/Diameter (Tab switches)");
    case ToolId::Ellipse:
        return QStringLiteral("ELLIPSE: axis then distance  ·  type Length/Angle (Tab switches)");
    case ToolId::Rect:
        return QStringLiteral("RECTANGLE: two corners  ·  type X length then Y length");
    case ToolId::Polygon:
        return QStringLiteral("POLYGON (%1 sides): type a count, then pick the centre and a vertex")
            .arg(polygonSides_);
    case ToolId::Dimension:
        switch (dimMode_) {
        case DimMode::Linear:
            return QStringLiteral("LINEAR DIM: pick two points, then the dimension-line height");
        case DimMode::Aligned:
            return QStringLiteral("ALIGNED DIM: pick two points, then the offset");
        case DimMode::Angular:
            return QStringLiteral("ANGULAR DIM: pick the vertex, then the two endpoints");
        case DimMode::Ordinate:
            return QStringLiteral("ORDINATE: pick the feature point, then the leader end");
        case DimMode::Radius:
        case DimMode::Jogged:
            return QStringLiteral("RADIUS DIM: pick a circle/arc, then place the text");
        case DimMode::Diameter:
            return QStringLiteral("DIAMETER DIM: pick a circle/arc, then place the text");
        case DimMode::ArcLength:
            return QStringLiteral("ARC LENGTH: pick an arc, then place the text");
        }
        return QStringLiteral("DIMENSION");
    case ToolId::Fillet:
        return QStringLiteral("FILLET: click the first line then the second (radius=%1) — type R any time "
                              "to change the radius").arg(filletRadius_);
    case ToolId::Chamfer:
        return QStringLiteral("CHAMFER: click a point on each of two lines — cuts straight between them");
    case ToolId::Trim:
        return trimRefIds_.empty()
                   ? QStringLiteral("TRIM: click the REFERENCE object to trim from (it stays selected)")
                   // What the command actually does now: a click takes the piece under the pointer, a
                   // line across several takes them all, and nothing stops at a corner any more.
                   : QStringLiteral("TRIM: click the piece to remove — or two points to draw a line "
                                    "across several; Esc to finish");
    case ToolId::Extend:
        return trimRefIds_.empty()
                   ? QStringLiteral("EXTEND: click the boundary object to extend to (it stays selected)")
                   : QStringLiteral("EXTEND: draw a line (two clicks) across objects to extend them to the "
                                    "boundary; Esc to finish");
    case ToolId::Text:
        return QStringLiteral("TEXT: pick a point, then type the text");
    case ToolId::Hatch:
        return hatchStyle_.boundary == HatchBoundary::SelectObject
                   ? QStringLiteral("HATCH: click a closed object to fill  ·  Esc exits")
                   : QStringLiteral("HATCH: click a point inside an enclosed area to fill it  ·  Esc exits");
    case ToolId::Wipeout:
        return hatchStyle_.boundary == HatchBoundary::SelectObject
                   ? QStringLiteral("WIPEOUT: click a closed object to mask  ·  Esc exits")
                   : QStringLiteral("WIPEOUT: click a point inside an enclosed area to mask it  ·  Esc exits");
    default:
        return QString(); // Select / modify tools have no draw hint
    }
}

bool Editor::isDrawOptionChar(QChar ch) const {
    const QChar c = ch.toLower();
    switch (tool_) {
    case ToolId::Polyline:       return c == 'c' || c == 'u' || c == 'f' || c == 'a' || c == 'l';
    case ToolId::DoublePolyline: return c == 'c' || c == 'u' || c == 'f' || c == 'g' || c == 'l' || c == 's';
    case ToolId::Spline:         return c == 'c' || c == 'u';
    case ToolId::DoubleLine:     return c == 'g' || c == 'l' || c == 's';
    case ToolId::Arc:            return c == 'm' || c == 'i'; // "mi" mirror
    case ToolId::Fillet:          return c == 'r'; // [R] radius
    default:                     return false; // Line/Rect/Circle/Ellipse/...: no letter options
    }
}

QString Editor::currentDrawFieldLabel() const {
    const auto labels = drawInputFieldLabels();
    if (labels.empty()) return QString();
    return labels[std::clamp(drawFieldIndex_, 0, static_cast<int>(labels.size()) - 1)];
}

void Editor::cycleDrawInputField() {
    const auto labels = drawInputFieldLabels();
    if (labels.size() < 2) return; // nothing to switch between
    drawFieldIndex_ = (drawFieldIndex_ + 1) % static_cast<int>(labels.size());
    emit message(QStringLiteral("%1:").arg(currentDrawFieldLabel()));
    emit overlayChanged();
}

void Editor::lockDrawField(const QString& text) {
    const auto labels = drawInputFieldLabels();
    if (labels.empty()) return;
    if (static_cast<int>(drawFieldVals_.size()) != static_cast<int>(labels.size()))
        drawFieldVals_.assign(labels.size(), std::nullopt);
    bool ok = false;
    const double v = text.trimmed().toDouble(&ok);
    if (ok && drawFieldIndex_ >= 0 && drawFieldIndex_ < static_cast<int>(drawFieldVals_.size()))
        drawFieldVals_[drawFieldIndex_] = v;
}

bool Editor::commitDrawFields() {
    const auto labels = drawInputFieldLabels();
    if (labels.empty()) return false;
    if (static_cast<int>(drawFieldVals_.size()) != static_cast<int>(labels.size()))
        drawFieldVals_.assign(labels.size(), std::nullopt);

    Vec2 target;
    switch (tool_) {
    case ToolId::Line:
    case ToolId::DoubleLine:
    case ToolId::TwistedPair:
    case ToolId::TwistedRoute:
    case ToolId::TwistedHarness:
    case ToolId::Polyline:
    case ToolId::DoublePolyline:
    case ToolId::Spline:
    case ToolId::Wire: {
        const Vec2 base = draftPts_.back();
        const Vec2 d = cursor_ - base;
        const double len = drawFieldVals_[0].value_or(d.length());
        const double angDeg = drawFieldVals_[1].value_or(std::atan2(d.y, d.x) * 180.0 / M_PI);
        const double a = angDeg * M_PI / 180.0;
        target = {base.x + len * std::cos(a), base.y + len * std::sin(a)};
        break;
    }
    case ToolId::Circle: {
        const Vec2 c = draftPts_[0];
        const Vec2 d = cursor_ - c;
        const double dl = d.length();
        const Vec2 dir = dl > 1e-9 ? d / dl : Vec2{1, 0};
        // The typed value is the radius/diameter; placing the point at that
        // distance lets finishDraw derive the correct radius for the mode.
        target = c + dir * drawFieldVals_[0].value_or(dl);
        break;
    }
    case ToolId::Ellipse: {
        if (draftPts_.size() == 1) {
            // Defining the first axis endpoint: length + angle from the first point.
            const Vec2 base = draftPts_[0];
            const Vec2 d = cursor_ - base;
            const double len = drawFieldVals_[0].value_or(d.length());
            const double angDeg = drawFieldVals_[1].value_or(std::atan2(d.y, d.x) * 180.0 / M_PI);
            const double a = angDeg * M_PI / 180.0;
            target = {base.x + len * std::cos(a), base.y + len * std::sin(a)};
        } else {
            // Other half-axis: distance along the perpendicular to the first axis.
            const Vec2 p0 = draftPts_[0], p1 = draftPts_[1];
            const Vec2 center = (ellipseMode_ == EllipseMode::Center) ? p0 : lerp(p0, p1, 0.5);
            const Vec2 ax = p1 - p0;
            const Vec2 nrm = ax.lengthSq() > 1e-18 ? ax.normalized().perp() : Vec2{0, 1};
            const double defB = std::abs(dot(cursor_ - center, nrm));
            const double b = drawFieldVals_[0].value_or(defB);
            const double s = dot(cursor_ - center, nrm) >= 0 ? 1.0 : -1.0;
            target = center + nrm * (s * b);
        }
        break;
    }
    case ToolId::Arc: {
        // Build the next point from a radius and/or angle about the arc's center.
        auto onCircle = [&](const Vec2& c, double r, int angleIdx) {
            const Vec2 d = cursor_ - c;
            const double angDeg =
                drawFieldVals_[angleIdx].value_or(std::atan2(d.y, d.x) * 180.0 / M_PI);
            const double a = angDeg * M_PI / 180.0;
            return Vec2{c.x + r * std::cos(a), c.y + r * std::sin(a)};
        };
        if (arcMode_ == ArcMode::CenterStartEnd) {
            const Vec2 c = draftPts_[0];
            if (draftPts_.size() == 1) { // start point: Radius + Angle
                const double r = drawFieldVals_[0].value_or(distance(c, cursor_));
                target = onCircle(c, r, 1);
            } else { // end point: Angle at the fixed radius
                target = onCircle(c, distance(c, draftPts_[1]), 0);
            }
        } else if (arcMode_ == ArcMode::StartCenterEnd) {
            if (draftPts_.size() == 1) { // place the center: Length + Angle from start
                const Vec2 base = draftPts_[0];
                const Vec2 d = cursor_ - base;
                const double len = drawFieldVals_[0].value_or(d.length());
                const double angDeg =
                    drawFieldVals_[1].value_or(std::atan2(d.y, d.x) * 180.0 / M_PI);
                const double a = angDeg * M_PI / 180.0;
                target = {base.x + len * std::cos(a), base.y + len * std::sin(a)};
            } else { // end point: Angle at the fixed radius about the center
                target = onCircle(draftPts_[1], distance(draftPts_[1], draftPts_[0]), 0);
            }
        } else {
            return false; // ThreePoint / StartEndRadius are click-only
        }
        break;
    }
    default:
        return false;
    }
    handleDrawPress(target); // places the point/finishes; also resets the fields
    return true;
}

bool Editor::acceptCommandInput(const QString& text) {
    const QString t = text.trimmed();
    if (t.isEmpty()) return false;

    // "mi" / "mirror" flips the arc being drawn to the opposite sweep.
    if (tool_ == ToolId::Arc &&
        (t.compare(QStringLiteral("mi"), Qt::CaseInsensitive) == 0 ||
         t.compare(QStringLiteral("mirror"), Qt::CaseInsensitive) == 0)) {
        toggleArcMirror();
        return true;
    }

    // Tan-Tan-Radius: after the two objects are picked, a typed number is the radius.
    if (tanAwaitRadius_ && !t.contains(',')) {
        bool ok = false;
        const double r = t.toDouble(&ok);
        if (ok && r > 0) {
            tanRadius_ = r;
            buildTangentCircle();
        } else {
            emit message(QStringLiteral("Enter a positive radius"));
        }
        return true;
    }

    // Offset: type the offset distance (before picking an object to offset).
    if (tool_ == ToolId::Offset && !offsetHasDist_ && !t.contains(',')) {
        bool ok = false;
        const double dv = std::abs(t.toDouble(&ok));
        if (ok && dv > 1e-9) {
            offsetDist_ = dv;
            offsetHasDist_ = true;
            offsetDistPts_.clear();
            emit message(QStringLiteral("Offset %1 — pick an object to offset").arg(dv));
            emit overlayChanged();
        } else {
            emit message(QStringLiteral("Enter a positive offset distance"));
        }
        return true;
    }

    // Splice / X termination: a typed number sets the marker size (world units) for the
    // next placements; the flying field shows the current value.
    if ((tool_ == ToolId::Splice || tool_ == ToolId::NotConnected) && !t.contains(',')) {
        bool ok = false;
        const double v = t.toDouble(&ok);
        if (ok && v > 0) {
            if (tool_ == ToolId::Splice) spliceSize_ = v;
            else ncSize_ = v;
            emit message(QStringLiteral("Size set to %1 — click to place").arg(v));
            emit markerSizeChanged();
            emit dynamicPrompt(tool_ == ToolId::Splice
                                   ? QStringLiteral("Joint size <%1>").arg(spliceSize_)
                                   : QStringLiteral("X size <%1>").arg(ncSize_));
        } else {
            emit message(QStringLiteral("Enter a positive size"));
        }
        return true;
    }

    // Scale: type a scale factor instead of picking a size. 2 = double, 0.5 = half.
    // The base point is the one already picked, else the selection's bounding centre.
    if (tool_ == ToolId::Scale && !t.contains(',')) {
        bool ok = false;
        const double f = t.toDouble(&ok);
        if (ok && f > 0) {
            auto sel = selectedEntities();
            if (!sel.empty()) {
                Vec2 base;
                if (!modifyPts_.empty()) {
                    base = modifyPts_[0];
                } else {
                    Bounds b;
                    for (const Entity* e : sel) {
                        const Bounds eb = e->bounds();
                        if (eb.valid) { b.expand(eb.min); b.expand(eb.max); }
                    }
                    base = b.valid ? b.center() : Vec2{0, 0};
                }
                pushUndo();
                scaleSelection(sel, base, std::max(1e-4, f));
                modifyPts_.clear();
                doc_->setDirty(true);
                emit changed();
                emit message(QStringLiteral("Scaled by %1").arg(f));
                setTool(ToolId::Select);
            }
        } else {
            emit message(QStringLiteral("Enter a positive scale factor (e.g. 2 or 0.5)"));
        }
        return true;
    }

    // Polygon: a whole number typed before the centre is placed sets the side count.
    if (tool_ == ToolId::Polygon && draftPts_.empty() && !t.contains(',')) {
        bool ok = false;
        const int n = t.toInt(&ok);
        if (ok && n >= 3) {
            setPolygonSides(n);
            emit message(QStringLiteral("Polygon sides: %1 — pick the centre").arg(polygonSides_));
            return true;
        }
    }

    // Circle dimension entry while the first reference point is placed:
    //   Center/Radius:   bare number = radius, "D n" = diameter
    //   Center/Diameter: bare number = diameter, "R n" = radius
    //   2-Point:         bare number = diameter, "R n" = radius
    if (tool_ == ToolId::Circle && drafting_ && draftPts_.size() == 1 &&
        circleMode_ != CircleMode::ThreePoint && !t.contains(',')) {
        QString vtxt = t;
        bool forceRadius = false;
        bool forceDiameter = false;
        const QRegularExpressionMatch m =
            QRegularExpression(QStringLiteral("^([rRdD])\\s*[:=]?\\s*(.+)$")).match(t);
        if (m.hasMatch()) {
            const QString prefix = m.captured(1).toLower();
            forceRadius = prefix == QStringLiteral("r");
            forceDiameter = prefix == QStringLiteral("d");
            vtxt = m.captured(2).trimmed();
        }
        bool ok = false;
        const double value = std::abs(vtxt.toDouble(&ok));
        if (ok && value > 0) {
            Vec2 dir = cursor_ - draftPts_[0];
            const double len = dir.length();
            if (len < 1e-9) {
                emit message(QStringLiteral("Move the cursor to set circle direction, then type radius or diameter"));
                return true;
            }
            dir = dir / len;
            double targetDistance = value;
            if (forceRadius) targetDistance = value * 2.0;
            else if (forceDiameter) targetDistance = value;
            else if (circleMode_ == CircleMode::CenterRadius) targetDistance = value;
            else targetDistance = value; // Center/Diameter and 2-Point use diameter distance.

            if (circleMode_ == CircleMode::CenterRadius && forceRadius) targetDistance = value;
            if (circleMode_ == CircleMode::CenterRadius && forceDiameter) targetDistance = value / 2.0;
            if (circleMode_ == CircleMode::CenterDiameter && forceRadius) targetDistance = value * 2.0;
            if (circleMode_ == CircleMode::TwoPoint && forceRadius) targetDistance = value * 2.0;
            handleDrawPress(draftPts_[0] + dir * targetDistance);
            return true;
        }
    }

    // Rectangle dimensioned entry: after the first corner, a typed length sets the
    // X side (in the cursor's X direction); the next length sets the Y side.
    if (tool_ == ToolId::Rect && drafting_ && draftPts_.size() == 1 && !t.contains(',')) {
        bool ok = false;
        const double val = std::abs(t.toDouble(&ok));
        if (ok) {
            if (!rectAwaitY_) {
                rectX_ = (cursor_.x >= draftPts_[0].x ? 1.0 : -1.0) * val;
                rectAwaitY_ = true;
                emit message(QStringLiteral("X length %1 — now enter the Y length").arg(val));
                return true;
            }
            const double yv = (cursor_.y >= draftPts_[0].y ? 1.0 : -1.0) * val;
            const Vec2 opp{draftPts_[0].x + rectX_, draftPts_[0].y + yv};
            rectAwaitY_ = false;
            handleDrawPress(opp); // places the opposite corner -> builds the rect
            return true;
        }
    }

    // Awaiting a gap value after the user typed "g".
    if (gapInputMode_) {
        bool ok = false;
        const double g = t.toDouble(&ok);
        if (ok && g > 0) {
            doubleLineGap_ = g;
            emit message(QStringLiteral("Gap = %1").arg(g));
        } else {
            emit message(QStringLiteral("Enter a positive gap value"));
        }
        gapInputMode_ = false;
        emit overlayChanged();
        return true;
    }

    // Awaiting a fillet radius after the user typed "f" or "r" (0 = sharp corners).
    if (filletInputMode_) {
        bool ok = false;
        const double r = t.toDouble(&ok);
        if (ok && r >= 0) {
            if (tool_ == ToolId::Fillet) {
                filletRadius_ = r;
                emit message(pickIds_.empty()
                                 ? QStringLiteral("Fillet radius = %1 — click the first line").arg(r)
                                 : QStringLiteral("Fillet radius = %1 — click the second line").arg(r));
            } else {
                cornerRadius_ = r;
                emit message(r > 0 ? QStringLiteral("Fillet radius = %1").arg(r)
                                   : QStringLiteral("Sharp corners"));
            }
        } else {
            emit message(QStringLiteral("Enter a fillet radius (0 for sharp corners)"));
        }
        filletInputMode_ = false;
        emit overlayChanged();
        return true;
    }

    // "g" = gap option. Enters gap-input mode for the double tools; during any
    // other draft it is consumed with a hint (never switches tools mid-draw).
    if (t.compare(QStringLiteral("g"), Qt::CaseInsensitive) == 0) {
        if (tool_ == ToolId::DoubleLine || tool_ == ToolId::DoublePolyline) {
            gapInputMode_ = true;
            emit message(QStringLiteral("Gap <%1>: enter value and press Enter").arg(doubleLineGap_));
            emit dynamicPrompt(QStringLiteral("Gap"));
            return true;
        }
        // Any other active draw tool consumes "g" with a hint — it must never fall
        // through to run the "g" command word and switch tools mid-draw.
        if (drafting_ || isDrawTool(tool_)) {
            emit message(QStringLiteral("Gap applies to double line/polyline"));
            return true;
        }
    }

    // "ls" = line-start for the double tools: the NEXT click picks a reference line;
    // both physical runs are then extended to start from that line.
    if (t.compare(QStringLiteral("ls"), Qt::CaseInsensitive) == 0) {
        if (tool_ == ToolId::DoublePolyline || tool_ == ToolId::DoubleLine) {
            dpolyLsArm_ = true;
            emit message(QStringLiteral(
                "Line start: click a reference line — both runs will start from it"));
            emit overlayChanged();
            return true;
        }
    }

    // "fc" = fillet-cancel: stop rounding corners and go back to sharp knees.
    if (t.compare(QStringLiteral("fc"), Qt::CaseInsensitive) == 0) {
        if (tool_ == ToolId::Polyline || tool_ == ToolId::DoublePolyline) {
            cornerRadius_ = 0.0;
            emit message(QStringLiteral("Fillet cancelled — sharp corners"));
            emit overlayChanged();
            return true;
        }
    }

    // "f" = fillet option. Enters fillet-radius mode for the (double) polyline
    // tools; consumed with a hint during any other draft.
    if (t.compare(QStringLiteral("f"), Qt::CaseInsensitive) == 0) {
        if (tool_ == ToolId::Polyline || tool_ == ToolId::DoublePolyline) {
            filletInputMode_ = true;
            emit message(QStringLiteral("Fillet radius <%1>: enter value and press Enter")
                             .arg(cornerRadius_));
            emit dynamicPrompt(QStringLiteral("Fillet radius"));
            return true;
        }
        // Any other active draw tool (e.g. Line) consumes "f" with a hint instead
        // of running the "fillet" command and jumping into the Fillet tool.
        if (drafting_ || isDrawTool(tool_)) {
            emit message(QStringLiteral("Fillet applies to polylines"));
            return true;
        }
    }

    // "r" = radius option inside the Fillet tool. Enters fillet-radius input mode
    // so the user can set the radius before picking lines.
    if (t.compare(QStringLiteral("r"), Qt::CaseInsensitive) == 0) {
        if (tool_ == ToolId::Fillet) {
            filletInputMode_ = true;
            emit message(QStringLiteral("Fillet radius <%1>: enter value and press Enter")
                             .arg(filletRadius_));
            emit dynamicPrompt(QStringLiteral("Fillet radius"));
            return true;
        }
    }

    // Polyline-family options offered "attached" while drawing: Close / Undo
    // (AutoCAD PLINE). "c" closes the run back to the start; "u" removes the last
    // point. Only active while a polyline/spline/wire draft is in progress.
    const bool plineFamily = tool_ == ToolId::Polyline || tool_ == ToolId::DoublePolyline ||
                             tool_ == ToolId::Spline || tool_ == ToolId::Wire ||
                             tool_ == ToolId::Wipeout;
    if (plineFamily && drafting_) {
        if (t.compare(QStringLiteral("c"), Qt::CaseInsensitive) == 0 ||
            t.compare(QStringLiteral("close"), Qt::CaseInsensitive) == 0) {
            if (draftPts_.size() >= 3) {
                draftPts_.push_back(draftPts_.front()); // close back to the start
                draftBulges_.push_back(0.0);
                draftCornerRadii_.push_back(0.0);
                if (draftCornerRadii_.size() >= 3) // closing completes the prior corner
                    draftCornerRadii_[draftCornerRadii_.size() - 2] = cornerRadius_;
                finishDraw();
            } else {
                emit message(QStringLiteral("Need at least 3 points to close"));
            }
            return true;
        }
        if (t.compare(QStringLiteral("u"), Qt::CaseInsensitive) == 0 ||
            t.compare(QStringLiteral("undo"), Qt::CaseInsensitive) == 0) {
            if (!draftPts_.empty()) draftPts_.pop_back();
            if (!draftBulges_.empty()) draftBulges_.pop_back();
            if (!draftCornerRadii_.empty()) draftCornerRadii_.pop_back();
            if (draftPts_.empty()) {
                cancelDraw();
                emit message(QStringLiteral("Polyline cancelled"));
            } else {
                emit message(QStringLiteral("Undid last point — click next, [C]lose or Enter"));
            }
            emit overlayChanged();
            return true;
        }
        // Arc / Line segment mode (Polyline only).
        if (tool_ == ToolId::Polyline) {
            if (t.compare(QStringLiteral("a"), Qt::CaseInsensitive) == 0 ||
                t.compare(QStringLiteral("arc"), Qt::CaseInsensitive) == 0) {
                polyArcMode_ = true;
                emit message(QStringLiteral("Arc segments — each click arcs tangent to the previous; "
                                            "[L]ine to switch back"));
                emit overlayChanged();
                return true;
            }
            if (t.compare(QStringLiteral("l"), Qt::CaseInsensitive) == 0 ||
                t.compare(QStringLiteral("line"), Qt::CaseInsensitive) == 0) {
                polyArcMode_ = false;
                emit message(QStringLiteral("Line segments"));
                emit overlayChanged();
                return true;
            }
        }
    }

    // Typed reference value for the active dynamic-input field (Length/Angle,
    // Radius, Diameter, X/Y radius): a bare number feeds the current field and
    // builds the point. Coordinate entry ("x,y"/"@dx,dy") still falls through.
    if (drafting_ && !t.contains(',') && hasDrawInputFields()) {
        bool ok = false;
        t.toDouble(&ok);
        if (ok) {
            lockDrawField(t);
            if (commitDrawFields()) return true;
        }
    }

    // Otherwise treat it as direct distance / coordinate input (only while
    // drafting). If not consumed, the caller runs it as a command word.
    return applyDrawInput(t);
}

bool Editor::pendingTextBox(Vec2& topLeft, double& w, double& h) const {
    if (draftPts_.empty()) return false;
    if (tool_ == ToolId::MText && draftPts_.size() >= 2) {
        Bounds b;
        b.expand(draftPts_[0]);
        b.expand(draftPts_[1]);
        topLeft = {b.min.x, b.max.y};
        w = b.width();
        h = b.height();
    } else {
        topLeft = draftPts_[0];
        w = 0;
        h = 0;
    }
    return true;
}

void Editor::provideText(const QString& text) {
    commitText(text, TextStyle{});
}

// Apply font + paragraph style fields onto a text entity's meta.
static void writeTextStyle(Entity& e, const Editor::TextStyle& s) {
    e.meta.insert("size", s.size > 0 ? s.size : 5.0);
    e.meta.insert("font", s.font);
    e.meta.insert("bold", s.bold);
    e.meta.insert("italic", s.italic);
    e.meta.insert("underline", s.underline);
    e.meta.insert("strike", s.strike);
    e.meta.insert("justify", s.justify.isEmpty() ? QStringLiteral("TL") : s.justify);
    e.meta.insert("lineSpacing", s.lineSpacing > 0 ? s.lineSpacing : 1.0);
    e.meta.insert("list", s.list.isEmpty() ? QStringLiteral("none") : s.list);
    if (s.boxWidth > 0.0) e.meta.insert("boxWidth", s.boxWidth);
    else e.meta.remove("boxWidth");
    if (s.boxHeight > 0.0) e.meta.insert("boxHeight", s.boxHeight);
    else e.meta.remove("boxHeight");
}

void Editor::commitText(const QString& text, const TextStyle& s) {
    if ((tool_ == ToolId::Text || tool_ == ToolId::MText) && !draftPts_.empty() &&
        !text.isEmpty()) {
        Entity e;
        e.type = EntityType::Text;
        e.layer = doc_->activeLayer();
        TextStyle s2 = s;
        if (tool_ == ToolId::MText && draftPts_.size() >= 2) {
            // Anchor at the box's top-left; box dims come from the style if set
            // (in-place ruler resize), else from the drag box.
            Bounds b;
            b.expand(draftPts_[0]);
            b.expand(draftPts_[1]);
            e.a = {b.min.x, b.max.y};
            if (s2.boxWidth <= 0.0) s2.boxWidth = b.width();
            if (s2.boxHeight <= 0.0) s2.boxHeight = b.height();
        } else {
            e.a = draftPts_[0];
        }
        e.meta.insert("text", text);
        writeTextStyle(e, s2);
        pushUndo();
        addEntity(std::move(e));
    }
    draftPts_.clear();
    emit changed();
    setTool(ToolId::Select); // return to Select after placing text
}

void Editor::applyTextEdit(const QString& id, const QString& text, const TextStyle& s) {
    Entity* e = entityById(id);
    if (!e) return;
    // Annotations that merely carry a text value (balloon number, note / revision
    // body) just store it — they aren't real Text objects.
    if (e->type != EntityType::Text) {
        if (e->meta.value("balloon").toBool() || e->meta.value("note").toBool() ||
            e->meta.value("revision").toBool()) {
            pushUndo();
            e->meta.insert("text", text);
            doc_->setDirty(true);
            emit changed();
        }
        return;
    }
    pushUndo();
    if (text.isEmpty()) {
        // Cleared -> delete the text object.
        auto& ents = doc_->entities();
        ents.erase(std::remove_if(ents.begin(), ents.end(),
                                  [&](const Entity& x) { return x.id == id; }),
                   ents.end());
        selected_.clear();
        emit selectionChanged();
        emit changed();
        return;
    }
    e->meta.insert("text", text);
    writeTextStyle(*e, s);
    doc_->setDirty(true);
    emit changed();
}

// ---- Modify tools (interactive, reference-point based) ---------------------

void Editor::handleModifyPress(const Vec2& p) {
    auto sel = selectedEntities();
    if (sel.empty()) {
        emit message(QStringLiteral("Select objects first, then pick the tool."));
        setTool(ToolId::Select);
        return;
    }
    modifyPts_.push_back(p);

    // All modify tools are two-click: a reference point, then a finishing point.
    if (modifyPts_.size() < 2) {
        emit message(tool_ == ToolId::Rotate
                         ? QStringLiteral("Rotate: pick the base point, then click to set the angle")
                     : tool_ == ToolId::Scale
                         ? QStringLiteral("Scale: base point set — type a scale factor (e.g. 2, "
                                          "0.5) or click to set the size")
                     : tool_ == ToolId::Mirror
                         ? QStringLiteral("Mirror: pick the second point of the mirror line")
                         : QStringLiteral("%1: pick the destination").arg(toolLabel(tool_)));
        if (tool_ == ToolId::Scale) // pop the factor field at the picked base point
            emit dynamicPrompt(QStringLiteral("Scale factor"));
        emit changed();
        return;
    }

    // Mirror doesn't apply on the second click: it stores the mirror line and asks
    // the host for Copy / Move / Cancel (commitMirror / cancelMirror finish it).
    if (tool_ == ToolId::Mirror) {
        mirrorA_ = modifyPts_[0];
        mirrorB_ = modifyPts_[1];
        mirrorPending_ = true;
        modifyPts_.clear();
        emit changed(); // drop the rubber-band preview line
        emit mirrorChoiceRequested();
        return;
    }

    pushUndo();
    switch (tool_) {
    case ToolId::Move: {
        const Vec2 delta = modifyPts_[1] - modifyPts_[0];
        std::vector<QString> movingIds;
        for (Entity* e : sel) movingIds.push_back(e->id);
        for (Entity* e : sel) {
            if (e->type == EntityType::Connector) dragAttachedWires(*e, delta, movingIds);
            translateEntity(*e, delta);
        }
        break;
    }
    case ToolId::Rotate: {
        const Vec2 piv = modifyPts_[0];
        const double ang = std::atan2(modifyPts_[1].y - piv.y, modifyPts_[1].x - piv.x);
        for (Entity* e : sel) rotateEntity(*e, piv, ang);
        break;
    }
    case ToolId::Scale: {
        const Vec2 base = modifyPts_[0];
        const double ref = selectionRefLength(sel);
        const double f = ref > 1e-9 ? distance(base, modifyPts_[1]) / ref : 1.0;
        scaleSelection(sel, base, std::max(1e-4, f));
        break;
    }
    case ToolId::Offset: {
        // Offset each selected entity toward the picked side by the picked dist.
        const Vec2 side = modifyPts_[1];
        // Built first, added after: `sel` points into the vector being appended to (see commitMirror).
        std::vector<Entity> clones;
        clones.reserve(sel.size());
        QHash<QString, QString> offsetOldToNew;
        for (Entity* e : sel) {
            Entity clone = *e;
            clone.id = uid();
            offsetOldToNew.insert(e->id, clone.id);
            if (e->type == EntityType::Circle) {
                const double d = distance(e->center, side) - e->radius;
                clone.radius = std::max(1e-6, e->radius + d);
            } else if (e->type == EntityType::Line || e->type == EntityType::DoubleLine) {
                const auto c = closestPointOnSegment(side, e->a, e->b);
                const Vec2 off = side - c.point;
                translateEntity(clone, off);
            } else {
                continue;
            }
            clones.push_back(std::move(clone));
        }
        remapEntityReferences(clones, offsetOldToNew);   // the offset copies name each other
        for (Entity& c : clones) doc_->entities().push_back(std::move(c));
        doc_->setDirty(true);
        break;
    }
    default:
        break;
    }

    modifyPts_.clear();
    doc_->setDirty(true);
    emit changed();
    emit message(QStringLiteral("%1 done").arg(toolLabel(tool_)));
    setTool(ToolId::Select);
}

namespace {
// Signed area (shoelace) of a closed polygon; sign = winding.
double polygonSignedArea(const std::vector<Vec2>& p) {
    double a = 0.0;
    const int n = static_cast<int>(p.size());
    for (int i = 0; i < n; ++i) a += cross(p[i], p[(i + 1) % n]);
    return a * 0.5;
}
bool pointInPolygon(const Vec2& pt, const std::vector<Vec2>& poly) {
    bool in = false;
    const int n = static_cast<int>(poly.size());
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const Vec2 a = poly[i], b = poly[j];
        if (((a.y > pt.y) != (b.y > pt.y)) &&
            (pt.x < (b.x - a.x) * (pt.y - a.y) / (b.y - a.y) + a.x))
            in = !in;
    }
    return in;
}
// Offset `src` by `dist` toward the side of `side`. Returns nullopt for unsupported
// types. The copy has no id (caller assigns one).
std::optional<Entity> offsetEntityToward(const Entity& src, const Vec2& side, double dist) {
    Entity c = src;
    switch (src.type) {
    case EntityType::Line:
    case EntityType::DoubleLine: {
        const Vec2 nrm = (src.b - src.a).normalized().perp();
        const double s = dot(side - src.a, nrm) >= 0 ? 1.0 : -1.0;
        c.a = src.a + nrm * (s * dist);
        c.b = src.b + nrm * (s * dist);
        return c;
    }
    case EntityType::Circle:
    case EntityType::Arc: {
        const double d = distance(src.center, side);
        c.radius = d >= src.radius ? src.radius + dist : std::max(1e-6, src.radius - dist);
        return c;
    }
    case EntityType::Ellipse: {
        const double avg = 0.5 * (src.radius + src.radiusY);
        const bool out = distance(src.center, side) >= avg;
        c.radius = std::max(1e-6, src.radius + (out ? dist : -dist));
        c.radiusY = std::max(1e-6, src.radiusY + (out ? dist : -dist));
        return c;
    }
    case EntityType::Polyline:
    case EntityType::Spline:
    case EntityType::Wire: {
        if (src.points.size() < 2) return std::nullopt;
        // Pick the side from the segment nearest the cursor.
        int seg = 0;
        double bd = std::numeric_limits<double>::max();
        for (size_t i = 0; i + 1 < src.points.size(); ++i) {
            const double d = closestPointOnSegment(side, src.points[i], src.points[i + 1]).distance;
            if (d < bd) { bd = d; seg = static_cast<int>(i); }
        }
        const Vec2 nrm = (src.points[seg + 1] - src.points[seg]).normalized().perp();
        const double s = dot(side - src.points[seg], nrm) >= 0 ? 1.0 : -1.0;
        c.points = offsetPolyline(src.points, s * dist);
        return c;
    }
    case EntityType::Rect:
    case EntityType::Polygon: {
        if (src.points.size() < 3) return std::nullopt;
        // Grow if the cursor is outside the shape, shrink if inside.
        const bool inside = pointInPolygon(side, src.points);
        const auto plus = offsetClosedPolygon(src.points, dist);
        const auto minus = offsetClosedPolygon(src.points, -dist);
        const double ap = std::abs(polygonSignedArea(plus));
        const double am = std::abs(polygonSignedArea(minus));
        c.points = inside ? (ap < am ? plus : minus) : (ap > am ? plus : minus);
        return c;
    }
    default:
        return std::nullopt;
    }
}
} // namespace

void Editor::handleOffsetPress(const Vec2& p) {
    // Step 1: define the offset distance — two clicks (their separation) or a typed
    // value (handled in acceptCommandInput).
    if (!offsetHasDist_) {
        offsetDistPts_.push_back(p);
        if (offsetDistPts_.size() < 2) {
            emit message(QStringLiteral(
                "Offset distance: click the second point, or type a distance"));
        } else {
            offsetDist_ = distance(offsetDistPts_[0], offsetDistPts_[1]);
            offsetDistPts_.clear();
            if (offsetDist_ < 1e-9) {
                emit message(QStringLiteral("Distance too small — click two points again"));
            } else {
                offsetHasDist_ = true;
                emit message(QStringLiteral("Offset %1 — pick an object to offset")
                                 .arg(offsetDist_));
            }
        }
        emit overlayChanged();
        return;
    }
    // Step 2: pick the object to offset.
    if (offsetSourceId_.isEmpty()) {
        if (auto hit = hitTest(p)) {
            offsetSourceId_ = *hit;
            emit message(QStringLiteral("Move the cursor to a side, then click to place the "
                                        "offset (Esc to finish)"));
        } else {
            emit message(QStringLiteral("Nothing there — pick an object to offset"));
        }
        emit overlayChanged();
        return;
    }
    // Step 3: the click's side decides where the offset copy goes; place it and stay
    // in the tool for the next object (distance kept). Esc exits.
    if (const Entity* src = entityById(offsetSourceId_)) {
        if (auto copy = offsetEntityToward(*src, p, offsetDist_)) {
            copy->id = uid();
            copy->layer = src->layer;
            pushUndo();
            addEntity(std::move(*copy));
            emit message(QStringLiteral("Offset placed — pick another object, or Esc to finish"));
        } else {
            emit message(QStringLiteral("This object type can't be offset"));
        }
    }
    offsetSourceId_.clear();
    emit changed();
}

double Editor::selectionRefLength(const std::vector<Entity*>& sel) const {
    Bounds b;
    for (const Entity* e : sel) {
        const Bounds eb = e->bounds();
        if (eb.valid) { b.expand(eb.min); b.expand(eb.max); }
    }
    return b.valid ? std::max(1e-6, distance(b.min, b.max)) : 1.0;
}

void Editor::scaleSelection(const std::vector<Entity*>& sel, const Vec2& base, double sf) {
    if (!doc_) return;
    // 1) Record every wire endpoint that sits on a SELECTED connector's pin end BEFORE
    //    scaling (by wire + point index + which connector/pin). Both selected and
    //    non-selected wires are captured so the whole harness follows the connectors.
    struct Attach { Entity* wire; size_t ptIdx; Entity* conn; size_t pin; };
    std::vector<Attach> attaches;
    for (Entity* c : sel) {
        if (c->type != EntityType::Connector) continue;
        const std::vector<Vec2> ends =
            connectorGeometry(c->a, connectorSpecFromMeta(c->meta)).pinEnd;
        for (auto& w : doc_->entities()) {
            if (w.type != EntityType::Wire) continue;
            for (size_t pi = 0; pi < w.points.size(); ++pi)
                for (size_t k = 0; k < ends.size(); ++k)
                    if (distance(w.points[pi], ends[k]) < 1e-6)
                        attaches.push_back({&w, pi, c, k});
        }
    }

    // 2) Scale every selected entity uniformly about the base — connectors scale their
    //    structure parametrically (scaleEntity), wires/other geometry scale their points.
    for (Entity* e : sel) scaleEntity(*e, base, sf);

    // 3) Re-snap each recorded wire point to its connector's NEW pin end so connections
    //    hold exactly (even if the connector's internal pin spacing scaled differently
    //    from a uniform point scale, and even for wires that weren't in the selection).
    for (const Attach& a : attaches) {
        const std::vector<Vec2> ne =
            connectorGeometry(a.conn->a, connectorSpecFromMeta(a.conn->meta)).pinEnd;
        if (a.pin < ne.size() && a.ptIdx < a.wire->points.size())
            a.wire->points[a.ptIdx] = ne[a.pin];
    }
}

std::vector<Entity> Editor::modifyPreview() const {
    std::vector<Entity> ghosts;
    if (!doc_ || modifyPts_.empty() || selected_.empty()) return ghosts;
    for (const auto& e : doc_->entities())
        if (std::find(selected_.begin(), selected_.end(), e.id) != selected_.end())
            ghosts.push_back(e);

    const Vec2 cur = cursor_; // the live (snapped) cursor stands in for the last point
    switch (tool_) {
    case ToolId::Move: {
        const Vec2 delta = cur - modifyPts_[0];
        for (Entity& g : ghosts) translateEntity(g, delta);
        break;
    }
    case ToolId::Rotate: {
        const Vec2 piv = modifyPts_[0];
        const double ang = std::atan2(cur.y - piv.y, cur.x - piv.x);
        for (Entity& g : ghosts) rotateEntity(g, piv, ang);
        break;
    }
    case ToolId::Mirror:
        for (Entity& g : ghosts) mirrorEntity(g, modifyPts_[0], cur);
        break;
    case ToolId::Scale: {
        const Vec2 base = modifyPts_[0];
        // Same bbox-diagonal reference as the commit, computed from the ghosts.
        Bounds b;
        for (const Entity& g : ghosts) {
            const Bounds eb = g.bounds();
            if (eb.valid) { b.expand(eb.min); b.expand(eb.max); }
        }
        const double ref = b.valid ? std::max(1e-6, distance(b.min, b.max)) : 1.0;
        const double f = std::max(1e-4, distance(base, cur) / ref);
        for (Entity& g : ghosts) scaleEntity(g, base, f);
        break;
    }
    case ToolId::Offset: {
        const Vec2 side = cur;
        std::vector<Entity> off;
        for (const Entity& e : ghosts) {
            Entity clone = e;
            if (e.type == EntityType::Circle) {
                const double d = distance(e.center, side) - e.radius;
                clone.radius = std::max(1e-6, e.radius + d);
                off.push_back(clone);
            } else if (e.type == EntityType::Line || e.type == EntityType::DoubleLine) {
                const auto c = closestPointOnSegment(side, e.a, e.b);
                translateEntity(clone, side - c.point);
                off.push_back(clone);
            }
        }
        ghosts = off;
        break;
    }
    default:
        ghosts.clear();
        break;
    }
    return ghosts;
}

// ---- Pick-modify tools (trim / extend / fillet / chamfer) ------------------

// One pass of the trim: everything in `ids` (or everything that is not a boundary) cut back to
// `boundaries`, where the fence [a,b] crosses it. Returns how many objects were changed, and the first
// reason it had for changing none.
int Editor::trimPass(const Vec2& a, const Vec2& b, const std::vector<QString>& onlyIds,
                     const std::vector<Entity>& boundaries, QString* whyOut) {
    const std::vector<Entity>& refs = boundaries;
    const std::vector<Entity>& joints = boundaries;
    // Which objects this pass may trim: the ones named, or - when none are named - everything that is
    // not one of the boundaries. Snapshotted first, so trimming does not disturb the iteration.
    std::vector<QString> ids = onlyIds;
    if (ids.empty())
        for (const auto& e : doc_->entities()) {
            bool isBoundary = false;
            for (const Entity& bnd : boundaries) isBoundary = isBoundary || bnd.id == e.id;
            if (!isBoundary) ids.push_back(e.id);
        }
    int count = 0;
    // Why nothing happened, when nothing happens. A trim that quietly does nothing cannot be told from a
    // trim that is broken, and the difference is usually one sentence.
    QString why;
    std::vector<Entity> additions; // split pieces, added after the loop
    std::vector<QString> removals; // objects with nothing left, removed after the loop
    for (const QString& id : ids) {
        Entity* obj = entityById(id);
        if (!obj) continue;
        // An object that lies WHOLLY INSIDE a circular reference has nothing outside the ring to
        // keep: trimming it back to the circle leaves nothing, so the whole of it goes. This is the
        // case that could not be trimmed at all otherwise - a chord between two points of the
        // circle, or anything drawn inside it, has no crossing to cut at.
        {
            bool insideRef = false;
            for (const Entity& r : refs) insideRef = insideRef || whollyInsideCircle(*obj, r);
            if (insideRef) {
                // Still only what the cut line actually crossed, so a fence takes what it is drawn
                // over and not every object sitting inside the circle. The fence IS a line, so the
                // ordinary crossing test answers it - no second copy of the geometry.
                Entity fence;
                fence.type = EntityType::Line;
                fence.a = a;
                fence.b = b;
                if (objectCrosses(*obj, {fence})) {
                    removals.push_back(id);
                    ++count;
                    continue;
                }
            }
        }
        // Where the cut line crosses a centreline (segment list) — the picked side.
        auto cutCrossOn = [&](const std::vector<Vec2>& cpts) -> std::optional<Vec2> {
            for (size_t i = 1; i < cpts.size(); ++i)
                if (auto x = segmentSegmentIntersection(a, b, cpts[i - 1], cpts[i])) return x;
            return std::nullopt;
        };
        auto storeCutMeta = [&](Entity& ent, const QString& selfId) {
            recordCutMeta(ent, selfId, joints);
        };
        if (obj->type == EntityType::Line) {
            // Only objects the cut line crosses AND that meet the reference are trimmed.
            auto x = segmentSegmentIntersection(a, b, obj->a, obj->b);
            if (!x || !objectCrosses(*obj, refs)) continue;
            std::optional<Entity> extra;
            if (trimLineQuick(*obj, *x, joints, extra)) {
                ++count;
                if (extra) additions.push_back(std::move(*extra));
            }
        } else if (obj->type == EntityType::DoubleLine) {
            // One line of it, when that is what the fence crossed.
            if (objectCrosses(*obj, refs) && trimOneRailOfDouble(*obj, a, b, refs, &why)) {
                ++count;
                continue;
            }
            // Trim the CENTERLINE; both physical lines follow. [a,b] is a 2-pt line.
            const std::vector<Vec2> cl = {obj->a, obj->b};
            auto pick = cutCrossOn(cl);
            if (!pick || !objectCrosses(*obj, refs)) continue;
            const QString selfId = obj->id;
            auto pieces = trimPolylinePieces(cl, *pick, joints, selfId);
            if (pieces.empty()) continue;
            obj->a = pieces[0].front();
            obj->b = pieces[0].back();
            storeCutMeta(*obj, selfId);
            ++count;
            if (pieces.size() == 2) {
                Entity rem = *obj;
                rem.id.clear();
                rem.a = pieces[1].front();
                rem.b = pieces[1].back();
                storeCutMeta(rem, selfId);
                additions.push_back(std::move(rem));
            }
        } else if (obj->type == EntityType::Rect || obj->type == EntityType::Polygon) {
            // A closed shape is trimmed as the run of its own edges, and what is left is open - so it
            // becomes a polyline, which is what an open run IS. (The same thing every CAD does with a
            // trimmed rectangle: it stops being a rectangle.)
            if (obj->points.size() < 3) continue;
            std::vector<Vec2> run = obj->points;
            run.push_back(run.front());   // round to where it started
            auto pick = cutCrossOn(run);
            if (!pick || !objectCrosses(*obj, refs)) continue;
            const QString selfId = obj->id;
            auto pieces = trimPolylinePieces(run, *pick, joints, selfId);
            if (pieces.empty()) continue;
            obj->type = EntityType::Polyline;
            obj->meta.remove(QStringLiteral("cornerRadii"));
            obj->meta.remove(QStringLiteral("cornerRadius"));
            obj->points = pieces[0];
            storeCutMeta(*obj, selfId);
            ++count;
            if (pieces.size() == 2) {
                Entity rem = *obj;
                rem.id.clear();
                rem.points = pieces[1];
                storeCutMeta(rem, selfId);
                additions.push_back(std::move(rem));
            }
        } else if (obj->type == EntityType::Polyline) {
            // A double polyline whose fence crossed ONE of its two lines: that line alone comes back to
            // the reference. Tried first, because the centreline test below would find nothing at all
            // for that gesture - which is why trimming one line of a double run did nothing.
            if (obj->meta.value("double").toBool() && objectCrosses(*obj, refs) &&
                trimOneRailOfDouble(*obj, a, b, refs, &why)) {
                ++count;
                continue;
            }
            // Trim the polyline centreline (double polylines: both runs follow).
            auto pick = cutCrossOn(obj->points);
            if (!pick || !objectCrosses(*obj, refs)) continue;
            const QString selfId = obj->id;
            const bool isDouble = obj->meta.value("double").toBool();
            auto pieces = trimPolylinePieces(obj->points, *pick, joints, selfId);
            if (pieces.empty()) continue;
            // Per-corner fillet / bulge data no longer matches the new vertices.
            obj->meta.remove(QStringLiteral("cornerRadii"));
            obj->meta.remove(QStringLiteral("cornerRadius"));
            obj->meta.remove(QStringLiteral("bulges"));
            obj->points = pieces[0];
            if (isDouble) storeCutMeta(*obj, selfId);
            ++count;
            if (pieces.size() == 2) {
                Entity rem = *obj;
                rem.id.clear();
                rem.points = pieces[1];
                if (isDouble) storeCutMeta(rem, selfId);
                additions.push_back(std::move(rem));
            }
        } else if (obj->type == EntityType::Ellipse) {
            // Where the cut line crosses the ellipse is the side that comes off - the same rule as for
            // a circle, on the ellipse's own parameter.
            const Vec2 d = b - a;
            std::optional<Vec2> pick;
            for (const auto& x : lineEllipseIntersection(a, d, obj->center, obj->radius, obj->radiusY,
                                                         obj->rotation)) {
                const double t = (d.lengthSq() > 1e-12) ? dot(x - a, d) / d.lengthSq() : -1.0;
                if (t < -1e-9 || t > 1.0 + 1e-9) continue;   // off the cut line's own length
                if (obj->meta.value(QStringLiteral("ellipticalArc")).toBool()) {
                    double sweep = obj->endAngle - obj->startAngle;
                    while (sweep < 0) sweep += 2.0 * M_PI;
                    double rel = ellipseParamAt(x, obj->center, obj->radius, obj->radiusY,
                                                obj->rotation) -
                                 obj->startAngle;
                    while (rel < 0) rel += 2.0 * M_PI;
                    if (rel > sweep + 1e-9) continue;   // not on the piece that is there
                }
                pick = x;
                break;
            }
            if (!pick || !objectCrosses(*obj, refs)) continue;
            if (trimEllipseAt(*obj, *pick, joints)) ++count;
        } else if (obj->type == EntityType::Circle || obj->type == EntityType::Arc) {
            // The cut line must cross the curve; that crossing is the picked side.
            const Vec2 d = b - a;
            std::optional<Vec2> pick;
            for (const auto& x : lineCircleIntersection(a, d, obj->center, obj->radius)) {
                const double t = (d.lengthSq() > 1e-12) ? dot(x - a, d) / d.lengthSq() : -1.0;
                if (t < -1e-9 || t > 1.0 + 1e-9) continue; // off the cut-line segment
                if (obj->type == EntityType::Arc) {
                    double sweep = obj->endAngle - obj->startAngle;
                    while (sweep < 0) sweep += 2.0 * M_PI;
                    double rel = std::atan2(x.y - obj->center.y, x.x - obj->center.x) - obj->startAngle;
                    while (rel < 0) rel += 2.0 * M_PI;
                    if (rel > sweep + 1e-9) continue; // not on the arc
                }
                pick = x;
                break;
            }
            if (!pick || !objectCrosses(*obj, refs)) continue;
            std::optional<Entity> extra;
            if (trimCircleAt(*obj, *pick, joints, extra)) {
                ++count;
                if (extra) additions.push_back(std::move(*extra));
            }
        }
    }
    for (const QString& gone : removals) {
        auto& ents = doc_->entities();
        ents.erase(std::remove_if(ents.begin(), ents.end(),
                                  [&gone](const Entity& e) { return e.id == gone; }),
                   ents.end());
    }
    selected_.erase(std::remove_if(selected_.begin(), selected_.end(),
                                   [&removals](const QString& s) {
                                       return std::find(removals.begin(), removals.end(), s) !=
                                              removals.end();
                                   }),
                    selected_.end());
    for (auto& add : additions) addEntity(std::move(add));
    if (whyOut && whyOut->isEmpty()) *whyOut = why;
    return count;
}

void Editor::applyTrimFence(const Vec2& a, const Vec2& b) {
    if (!doc_) return;
    std::vector<Entity> refs;
    for (const QString& id : trimRefIds_)
        if (const Entity* r = entityById(id)) refs.push_back(*r);
    if (refs.empty()) { emit message(QStringLiteral("Select the reference object first.")); return; }

    // The boundaries are the REFERENCE objects and nothing else. Trimming to a circle means removing
    // what is inside THAT circle, whatever else the drawing happens to have lying across the same
    // object.
    //
    // It used to be every object on the sheet ("until any joint"), which meant a second circle
    // overlapping the reference cut the removal short: the piece left behind still had material inside
    // the reference, and its new end sat on a ring nobody had selected. Nor does an object's own corner
    // bound it (see trimPolylinePieces): a corner is part of the thing being cut, not a cutting edge.
    const std::vector<Entity>& joints = refs;
    pushUndo();
    QString why;
    QString picked;      // what a click took, in words, for the message
    int count = trimPass(a, b, {}, refs, &why);

    // Nothing? Then the selection is very likely what was MEANT to be trimmed - it is the object the
    // fence was drawn across - and the rest of the drawing is what bounds it. AutoCAD offers the same
    // thing the other way round: press Enter with nothing selected and every object becomes a cutting
    // edge. Retrying like this means the command works whichever way round the two were picked, which is
    // worth more than being strict about which comes first.
    bool secondWay = false;
    if (count == 0) {
        std::vector<Entity> others;
        for (const auto& e : doc_->entities()) {
            bool isRef = false;
            for (const Entity& r : refs) isRef = isRef || r.id == e.id;
            if (!isRef) others.push_back(e);
        }
        QString why2;
        count = trimPass(a, b, trimRefIds_, others, &why2);
        secondWay = count > 0;
        if (count == 0 && why.isEmpty()) why = why2;
        // And if the two clicks crossed nothing at all - a click rather than a drag, or a short line
        // that stayed inside the gap of a run - then what was meant is the piece UNDER the pointer.
        // That is how trimming is asked for first, and it used to be the one gesture that did nothing.
        if (count == 0) {
            QString why3;
            bool nothingUnderPointer = false;
            for (const Vec2& at : {b, a}) {
                // If the pointer is on one of the SELECTED objects, that is the thing being trimmed -
                // it is what was picked out and then clicked on - and the rest of the drawing bounds
                // it. Trying it the other way round first trimmed whatever else happened to lie under
                // the pointer, reported a success, and left the line that was clicked untouched.
                // The object under the pointer is THE ONE BEING TRIMMED. Everything else bounds it:
                // the selection when the pointer is not on it, the rest of the drawing when it is.
                //
                // Trying one boundary set after another and keeping whatever any of them managed to
                // trim is what made a click report "trimmed one line of a double run" having quietly
                // cut a DIFFERENT run a few millimetres away - the clicked line was refused, the next
                // attempt looked past it, and something else moved. If the line under the pointer
                // cannot be trimmed, the answer is why, not a different object.
                const auto hit = hitTest(at);
                if (!hit) nothingUnderPointer = true;
                if (hit) {
                    const QString target = *hit;
                    const bool targetIsRef = std::find(trimRefIds_.begin(), trimRefIds_.end(),
                                                       target) != trimRefIds_.end();
                    std::vector<Entity> bnds;
                    if (targetIsRef) {
                        for (const auto& e : doc_->entities())
                            if (e.id != target) bnds.push_back(e);
                    } else {
                        for (const Entity& r : refs)
                            if (r.id != target) bnds.push_back(r);
                    }
                    if (trimPickAt(at, bnds, &why3, &picked, &target)) {
                        count = 1;
                        secondWay = targetIsRef;
                        break;
                    }
                }
                // Only when the thing under the pointer had nothing to say for itself - something that
                // cannot be trimmed at all, or nothing under the pointer - is anything else tried.
                if (!why3.isEmpty()) continue;
                if (trimPickAt(at, refs, &why3, &picked)) { count = 1; break; }
                if (trimPickAt(at, others, &why3, &picked)) { count = 1; secondWay = true; break; }
            }
            if (count == 0 && !why3.isEmpty()) why = why3;
            if (count == 0 && why.isEmpty() && nothingUnderPointer)
                why = QStringLiteral("There is nothing under the pointer to trim — click on the piece "
                                     "of a line you want to take off.");
        }
    }
    if (count == 0) {
        // undo() clears the selection, and losing it for a trim that did nothing means re-picking the
        // reference before every retry - which is most of the work of trimming.
        const std::vector<QString> keep = selected_;
        undo();
        selected_ = keep;
        emit selectionChanged();
        // Whatever the trim itself worked out, in preference to the general answer. "The cut line
        // crossed nothing" is true of a great many different mistakes, and the one being made is
        // usually a sentence away - which line has no boundary on it, which one already ends on the
        // boundary, which one the reference does not reach.
        emit message(!why.isEmpty()
                         ? why
                         : QStringLiteral("The cut line crossed nothing that meets the reference."));
        emit changed();
        return;
    }
    doc_->setDirty(true);
    // Keep the reference selected so more cut lines can be drawn; Esc finishes.
    if (!picked.isEmpty()) {
        emit message(QStringLiteral("Trimmed %1 — click another piece, or Esc to finish").arg(picked));
        emit changed();
        return;
    }
    emit message(secondWay
                     ? QStringLiteral("Trimmed the selected object against the rest of the drawing "
                                      "(%1) — draw another cut line or Esc to finish").arg(count)
                     : QStringLiteral("Trimmed %1 object(s) — draw another cut line or Esc to finish")
                           .arg(count));
    emit changed();
}

namespace {

// Where something crosses a run, as a distance along it and the point itself.
std::optional<std::pair<double, Vec2>> crossAlong(const std::vector<Vec2>& run, const Vec2& p1,
                                                 const Vec2& p2) {
    double along = 0.0;
    for (size_t i = 1; i < run.size(); ++i) {
        if (auto x = segmentSegmentIntersection(p1, p2, run[i - 1], run[i]))
            return std::make_pair(along + distance(run[i - 1], *x), *x);
        along += distance(run[i - 1], run[i]);
    }
    return std::nullopt;
}

// How far along a run its nearest point to `p` is, and how far away that is.
std::pair<double, double> nearestAlong(const std::vector<Vec2>& run, const Vec2& p) {
    double along = 0.0, bestAlong = 0.0, bestDist = 1e299;
    for (size_t i = 1; i < run.size(); ++i) {
        const auto c = closestPointOnSegment(p, run[i - 1], run[i]);
        if (c.distance < bestDist) {
            bestDist = c.distance;
            bestAlong = along + distance(run[i - 1], c.point);
        }
        along += distance(run[i - 1], run[i]);
    }
    return {bestAlong, bestDist};
}

} // namespace

bool Editor::trimRailPiece(Entity& ent, const std::vector<Vec2>& rail, int side, double alongPick,
                           const std::vector<Entity>& refs, QString* why) {
    auto say = [why](const QString& text) {
        if (why && why->isEmpty()) *why = text;
        return false;
    };
    // EVERY crossing of this line by a boundary, as a distance along it. More than one is the normal
    // case, not a special one: a wire that ends on a body edge is crossed there as well as wherever
    // else something meets it.
    std::vector<std::pair<double, Vec2>> crossings;
    for (const Entity& r : refs) {
        std::vector<Vec2> segs;
        forEachCutSegmentOf(r, segs);
        for (size_t k = 1; k < segs.size(); k += 2) {
            double along = 0.0;
            for (size_t i = 1; i < rail.size(); ++i) {
                if (auto x = segmentSegmentIntersection(segs[k - 1], segs[k], rail[i - 1], rail[i]))
                    crossings.push_back({along + distance(rail[i - 1], *x), *x});
                along += distance(rail[i - 1], rail[i]);
            }
        }
    }
    // A hole already in this line bounds what is next to it, exactly as a crossing does - it IS where
    // the line stops - and there is nothing inside it left to take off.
    for (const auto& hv : ent.meta.value(QStringLiteral("holes")).toArray()) {
        const QJsonArray h = hv.toArray();
        if (h.size() < 5 || h[0].toInt() != side) continue;
        const Vec2 p1{h[1].toDouble(), h[2].toDouble()};
        const Vec2 p2{h[3].toDouble(), h[4].toDouble()};
        double d1 = nearestAlong(rail, p1).first, d2 = nearestAlong(rail, p2).first;
        if (d2 < d1) { std::swap(d1, d2); }
        if (d2 - d1 < 1e-9) continue;
        if (alongPick > d1 + 1e-9 && alongPick < d2 - 1e-9)
            return say(QStringLiteral("That part of the line has already been taken out."));
        crossings.push_back({d1, p1});
        crossings.push_back({d2, p2});
    }
    // The part of the line that only a CAP provides - where it was stretched to meet something - is a
    // piece with a definite start: the end of the line's own run. Clicking THERE and trimming means "take
    // this extension off". It is the fallback for when there is nothing out on the extension to cut back
    // to, which is the usual case: an extension reaches something, and between that something and the
    // line's own end there is normally nothing at all. Without it, extending a line by accident could
    // only be undone with undo.
    auto tryDropExtension = [&]() -> bool {
        const double railLen0 = polylineLength(rail);
        std::vector<Vec2> bare;
        {
            const double gap = ent.meta.value(QStringLiteral("gap")).toDouble(10.0);
            const double s = (side >= 0 ? 1.0 : -1.0) * gap / 2.0;
            if (ent.type == EntityType::DoubleLine) {
                const Vec2 o = (ent.b - ent.a).normalized().perp() * s;
                bare = {ent.a + o, ent.b + o};
            } else {
                bare = offsetPolyline(ent.points, s);
            }
        }
        if (bare.size() < 2 || rail.size() < 2) return false;
        const double startExt = distance(rail.front(), bare.front());
        const double endExt = distance(rail.back(), bare.back());
        const bool inEndExt = endExt > 1e-6 && alongPick > railLen0 - endExt;
        const bool inStartExt = startExt > 1e-6 && alongPick < startExt;
        if (!inEndExt && !inStartExt) return false;
        const QString key = inEndExt ? QStringLiteral("cutEnd") : QStringLiteral("cutStart");
        // Whichever slot at that end is doing it for THIS line. An unsided one is doing it for both, so
        // it stays and becomes the other line's.
        bool dropped = false;
        for (const QString& base : {key, key + QStringLiteral("2")}) {
            if (!ent.meta.value(base).toBool()) continue;
            const int sd = ent.meta.value(base + QStringLiteral("Side")).toInt();
            if (sd == side) {
                for (const char* k : {"", "Kind", "Ax", "Ay", "Bx", "By", "Cx", "Cy", "R", "Poly",
                                      "Side"})
                    ent.meta.remove(base + QLatin1String(k));
                dropped = true;
            } else if (sd == 0) {
                ent.meta.insert(base + QStringLiteral("Side"), -side);
                dropped = true;
            }
        }
        if (dropped && why)
            *why = QStringLiteral("Took the extension off that line — it ends where its own run ends "
                                  "again.");
        return dropped;
    };
    if (crossings.empty()) {
        if (tryDropExtension()) return true;
        return say(QStringLiteral("Nothing crosses that line, so there is no boundary to trim it back "
                                  "to — select the object you want it to stop at."));
    }

    // The piece the pick is IN: bounded by the nearest crossing on each side of it, and by the line's
    // own ends where there is none. Taking the nearest crossing on either hand instead - one number
    // rather than two - is what made a line that already ends on something untrimmable: the crossing at
    // its own end came out nearest, and cutting back to THAT removes the whole line.
    const double railLen = polylineLength(rail);
    double lo = 0.0, hi = railLen;
    bool loCut = false, hiCut = false;
    Vec2 loAt = rail.front(), hiAt = rail.back();
    for (const auto& c : crossings) {
        if (c.first <= alongPick + 1e-9 && c.first >= lo) { lo = c.first; loAt = c.second; loCut = true; }
        if (c.first >= alongPick - 1e-9 && c.first <= hi) { hi = c.first; hiAt = c.second; hiCut = true; }
    }
    if (hi - lo < 1e-9) {
        // The pick landed exactly ON a crossing. That is the normal case rather than a freak one: object
        // snapping pulls the pointer onto the intersection precisely when you aim at the piece beside it,
        // so the interval collapses and there is nothing between the bounds. What was meant is the piece
        // NEXT to that crossing, and the one a trimmed end can say is the one towards the nearer end of
        // the line - so that is the one taken.
        if (railLen - hi <= lo) {
            hi = railLen;
            hiCut = false;
        } else {
            lo = 0.0;
            loCut = false;
        }
    }
    if (hi - lo < 1e-9)
        return say(QStringLiteral("There is nothing of that line to remove where you picked."));

    // Which end of the line the piece is at decides which cap says it. A piece in the MIDDLE - a
    // boundary on both sides of the pick, neither at an end - cannot be said by a trimmed end at all.
    const bool atStart = !loCut || lo < 1e-9;
    const bool atEnd = !hiCut || hi > railLen - 1e-9;
    if (atStart && atEnd) {
        if (tryDropExtension()) return true;
        return say(QStringLiteral("That would take the whole of that line: there is no boundary "
                                  "between the point you picked and its ends."));
    }
    if (!atStart && !atEnd) {
        // A piece out of the MIDDLE of one line, boundary either side of it: the piece of a line that
        // runs through the gap of the run crossing it, which is the most ordinary trim on a harness
        // drawing there is. No trimmed end can say it, so the line keeps it as a HOLE - the two points
        // where it stops and starts again. Both lines of a run can be opened this way, which is what
        // "the first line trims and the second one does not" was about: the second line's piece was
        // always the one in the middle.
        QJsonArray holes = ent.meta.value(QStringLiteral("holes")).toArray();
        QJsonArray h;
        h.append(side);
        h.append(loAt.x);
        h.append(loAt.y);
        h.append(hiAt.x);
        h.append(hiAt.y);
        holes.append(h);
        ent.meta.insert(QStringLiteral("holes"), holes);
        if (why)
            *why = QStringLiteral("Took that piece out of the middle of the line.");
        return true;
    }
    const QString key = atStart ? QStringLiteral("cutStart") : QStringLiteral("cutEnd");
    const Vec2 at = atStart ? hiAt : loAt;
    if (!capDoubleRailAt(ent, key, at, refs, side))
        return say(QStringLiteral("The boundary does not reach that line where it would have to, so "
                                  "the line cannot be brought back to it."));
    return true;
}

bool Editor::trimOneRailOfDouble(Entity& ent, const Vec2& fa, const Vec2& fb,
                                 const std::vector<Entity>& refs, QString* why) {
    const bool dbl = ent.type == EntityType::DoubleLine;
    const bool dpoly = ent.type == EntityType::Polyline && ent.meta.value("double").toBool();
    if (!dbl && !dpoly) return false;
    const double gap = ent.meta.value("gap").toDouble(10.0);
    if (gap <= 0.0) return false;

    // WHICH LINES the fence crosses decides what is cut: one of them means that line alone, both of them
    // mean the whole run. The centreline is deliberately not consulted - it is not drawn, and a fence
    // reaching the far line from inside the run has to cross it on the way.
    int side = 0;
    double alongPick = 0.0;
    std::vector<Vec2> rail;
    for (double s : {gap / 2.0, -gap / 2.0}) {
        const std::vector<Vec2> r = doubleRunRail(ent, s);
        if (r.size() < 2) continue;
        if (auto x = crossAlong(r, fa, fb)) {
            if (side != 0) return false;   // both crossed: not this gesture
            side = s > 0 ? +1 : -1;
            alongPick = x->first;
            rail = r;
        }
    }
    if (side == 0) return false;
    return trimRailPiece(ent, rail, side, alongPick, refs, why);
}

bool Editor::trimPickAt(const Vec2& pick, const std::vector<Entity>& boundaries, QString* why,
                        QString* what, const QString* onlyId) {
    if (!doc_) return false;
    auto say = [why](const QString& text) {
        if (why && why->isEmpty()) *why = text;
        return false;
    };
    auto isBoundary = [&boundaries](const QString& id) {
        for (const Entity& b : boundaries)
            if (b.id == id) return true;
        return false;
    };
    Entity* obj = nullptr;
    if (onlyId) {
        // The caller has already decided what was clicked, and nothing else may be trimmed instead.
        obj = entityById(*onlyId);
    } else {
        // What is under the pointer, ignoring the boundaries themselves.
        std::vector<QString> skip;
        for (int attempt = 0; attempt < 4 && !obj; ++attempt) {
            const auto hit = hitTest(pick, skip);
            if (!hit) break;
            if (isBoundary(*hit)) { skip.push_back(*hit); continue; }
            obj = entityById(*hit);
            if (obj) break;
            skip.push_back(*hit);
        }
    }
    if (!obj) return false;

    // What is being trimmed, in words. A report that "nothing happened" can then be checked against
    // what the command thought it was doing, which is the difference between a bug and a mis-click.
    if (what) {
        const bool isDbl = obj->type == EntityType::DoubleLine ||
                           (obj->type == EntityType::Polyline && obj->meta.value("double").toBool());
        *what = isDbl ? QStringLiteral("one line of a double run")
                : obj->type == EntityType::Line ? QStringLiteral("a line")
                : obj->type == EntityType::Polyline ? QStringLiteral("a polyline")
                                                    : QStringLiteral("an object");
    }
    const bool dbl = obj->type == EntityType::DoubleLine;
    const bool dpoly = obj->type == EntityType::Polyline && obj->meta.value("double").toBool();
    if (dbl || dpoly) {
        // WHICH line was clicked: the nearer of the two. On a click that is the whole question - there
        // is no fence to read it from - and it is also the more direct answer.
        const double gap = obj->meta.value("gap").toDouble(10.0);
        if (gap <= 0.0) return false;
        int side = 0;
        double alongPick = 0.0, bestDist = 1e299;
        std::vector<Vec2> rail;
        for (double s : {gap / 2.0, -gap / 2.0}) {
            const std::vector<Vec2> r = doubleRunRail(*obj, s);
            if (r.size() < 2) continue;
            const auto n = nearestAlong(r, pick);
            if (n.second < bestDist) {
                bestDist = n.second;
                alongPick = n.first;
                side = s > 0 ? +1 : -1;
                rail = r;
            }
        }
        if (side == 0) return false;
        return trimRailPiece(*obj, rail, side, alongPick, boundaries, why);
    }
    if (obj->type == EntityType::Line) {
        if (!objectCrosses(*obj, boundaries))
            return say(QStringLiteral("Nothing crosses that line, so there is no boundary to trim it "
                                      "back to — select the object you want it to stop at."));
        std::optional<Entity> extra;
        if (!trimLineQuick(*obj, pick, boundaries, extra))
            return say(QStringLiteral("There is no boundary between the point you picked and the ends "
                                      "of that line, so there is no piece to take off."));
        if (extra) addEntity(std::move(*extra));
        return true;
    }
    if (obj->type == EntityType::Polyline || obj->type == EntityType::Rect ||
        obj->type == EntityType::Polygon) {
        std::vector<Vec2> run = obj->points;
        const bool closed = obj->type == EntityType::Rect || obj->type == EntityType::Polygon;
        if (closed && run.size() >= 3) run.push_back(run.front());
        if (run.size() < 2) return false;
        const QString selfId = obj->id;
        auto pieces = trimPolylinePieces(run, pick, boundaries, selfId);
        if (pieces.empty())
            return say(QStringLiteral("Nothing the reference crosses lies either side of the point "
                                      "you picked, so there is no piece of that polyline to take "
                                      "off."));
        obj->type = EntityType::Polyline;
        obj->meta.remove(QStringLiteral("cornerRadii"));
        obj->meta.remove(QStringLiteral("cornerRadius"));
        obj->meta.remove(QStringLiteral("bulges"));
        obj->points = pieces[0];
        if (pieces.size() == 2) {
            Entity rem = *obj;
            rem.id.clear();
            rem.points = pieces[1];
            addEntity(std::move(rem));
        }
        return true;
    }
    return false;
}

namespace {

// A cut as the drawing stores it: a ring, or a straight segment.
struct CutRecord {
    bool circle = false;
    Vec2 c{0.0, 0.0};
    double r = 0.0;
    Vec2 a{0.0, 0.0}, b{0.0, 0.0};
};

void clearCutSlot(Entity& ent, const QString& base) {
    for (const char* s : {"", "Kind", "Ax", "Ay", "Bx", "By", "Cx", "Cy", "R", "Poly", "Side"})
        ent.meta.remove(base + QLatin1String(s));
}

void writeCutSlot(Entity& ent, const QString& base, const CutRecord& cut, int side) {
    clearCutSlot(ent, base);
    ent.meta.insert(base, true);
    if (cut.circle) {
        ent.meta.insert(base + "Kind", QStringLiteral("circle"));
        ent.meta.insert(base + "Cx", cut.c.x);
        ent.meta.insert(base + "Cy", cut.c.y);
        ent.meta.insert(base + "R", cut.r);
    } else {
        ent.meta.insert(base + "Kind", QStringLiteral("line"));
        ent.meta.insert(base + "Ax", cut.a.x);
        ent.meta.insert(base + "Ay", cut.a.y);
        ent.meta.insert(base + "Bx", cut.b.x);
        ent.meta.insert(base + "By", cut.b.y);
    }
    if (side != 0) ent.meta.insert(base + "Side", side);
}

// The whole physical line(s) named by `onlySide` (0 = both), as drawn.
std::vector<std::vector<Vec2>> railsOf(const Entity& ent, int onlySide) {
    std::vector<std::vector<Vec2>> out;
    const double gap = ent.meta.value(QStringLiteral("gap")).toDouble(10.0);
    for (double s : {gap / 2.0, -gap / 2.0}) {
        if (onlySide != 0 && (s > 0 ? +1 : -1) != onlySide) continue;
        const std::vector<Vec2> r = doubleRunRail(ent, s);
        if (r.size() >= 2) out.push_back(r);
    }
    return out;
}

// The two physical lines at one end of a run, each as (that end, the next point along it).
std::vector<std::pair<Vec2, Vec2>> railEndsAt(const Entity& ent, bool startEnd, int onlySide) {
    std::vector<std::pair<Vec2, Vec2>> rails;
    const double gap = ent.meta.value(QStringLiteral("gap")).toDouble(10.0);
    auto want = [onlySide](double s) { return onlySide == 0 || (s > 0 ? +1 : -1) == onlySide; };
    if (ent.type == EntityType::DoubleLine) {
        const Vec2 off = (ent.b - ent.a).normalized().perp() * (gap / 2.0);
        for (double s : {gap / 2.0, -gap / 2.0}) {
            if (!want(s)) continue;
            const Vec2 o = s > 0 ? off : off * -1.0;
            if (startEnd) rails.push_back({ent.a + o, ent.b + o});
            else rails.push_back({ent.b + o, ent.a + o});
        }
    } else if (ent.points.size() >= 2) {
        for (double s : {gap / 2.0, -gap / 2.0}) {
            if (!want(s)) continue;
            const std::vector<Vec2> run = doubleRunRail(ent, s);
            if (run.size() < 2) continue;
            if (startEnd) rails.push_back({run.front(), run[1]});
            else rails.push_back({run.back(), run[run.size() - 2]});
        }
    }
    return rails;
}

// The cut through `at`, for the lines named by `side` (0 = both). A ring wins when there is one; among
// straight candidates - and at a corner there are two through one point - the one every line named can
// reach within its own length wins, then one that any of them reaches. Nothing reachable, no cut.
std::optional<CutRecord> findCutAt(const Entity& ent, const QString& key, const Vec2& at,
                                  const std::vector<Entity>& refs, int side) {
    if (auto cc = cutCircleThrough(at, refs, ent.id)) {
        CutRecord r;
        r.circle = true;
        r.c = cc->first;
        r.r = cc->second;
        return r;
    }
    const std::vector<std::pair<Vec2, Vec2>> cands = cutSegmentsThrough(at, refs, ent.id);
    if (cands.empty()) return std::nullopt;
    const std::vector<std::pair<Vec2, Vec2>> rails =
        railEndsAt(ent, key == QLatin1String("cutStart"), side);
    const std::vector<std::vector<Vec2>> whole = railsOf(ent, side);
    auto reached = [&rails, &whole](const std::pair<Vec2, Vec2>& seg) {
        int n = 0;
        for (const auto& rl : rails)
            if (railMeetsSegment(rl.first, rl.second, seg.first, seg.second)) ++n;
        if (n > 0) return n;
        // Or it crosses the line somewhere along its length - past a knee, say. A cut clips the whole
        // line, so that is reachable too.
        for (const auto& rl : whole)
            if (railCrossesSegment(rl, seg.first, seg.second)) ++n;
        return n;
    };
    const std::pair<Vec2, Vec2>* pick = nullptr;
    if (!rails.empty()) {
        for (const auto& c : cands)
            if (reached(c) == static_cast<int>(rails.size())) { pick = &c; break; }
        if (!pick)
            for (const auto& c : cands)
                if (reached(c) > 0) { pick = &c; break; }
    } else if (!cands.empty()) {
        pick = &cands.front();
    }
    if (!pick) return std::nullopt;
    CutRecord r;
    r.a = pick->first;
    r.b = pick->second;
    return r;
}

} // namespace

void Editor::capDoubleEndAt(Entity& ent, const QString& key, const Vec2& at,
                            const std::vector<Entity>& joints) {
    // A cut for the whole end: it replaces anything either line had there, sides included.
    clearCutSlot(ent, key);
    clearCutSlot(ent, cutSlot2(key));
    if (auto cut = findCutAt(ent, key, at, joints, 0)) writeCutSlot(ent, key, *cut, 0);
}

bool Editor::capDoubleRailAt(Entity& ent, const QString& key, const Vec2& at,
                             const std::vector<Entity>& refs, int side) {
    const auto cut = findCutAt(ent, key, at, refs, side);
    if (!cut) return false;
    const QString slotB = cutSlot2(key);
    const bool haveA = ent.meta.value(key).toBool();
    const int sideA = ent.meta.value(key + QStringLiteral("Side")).toInt();
    if (!haveA) {                       // nothing there yet
        writeCutSlot(ent, key, *cut, side);
        return true;
    }
    if (sideA == side) {                // this line already had one: it moves
        writeCutSlot(ent, key, *cut, side);
        return true;
    }
    if (sideA == 0) {
        // What is there applies to BOTH lines. The other line keeps it - so it becomes that line's -
        // and this line's new cut goes in the second slot. Without this the other line's end would
        // silently jump to wherever this one was just trimmed to.
        ent.meta.insert(key + QStringLiteral("Side"), -side);
    }
    writeCutSlot(ent, slotB, *cut, side);
    return true;
}

void Editor::recordCutMeta(Entity& ent, const QString& selfId, const std::vector<Entity>& joints) {
    Q_UNUSED(selfId); // capDoubleEndAt excludes ent by id
    // Only meaningful for a DOUBLE entity (its physical runs get clipped to the cut).
    const std::vector<Vec2> pts =
        (ent.type == EntityType::DoubleLine) ? std::vector<Vec2>{ent.a, ent.b} : ent.points;
    if (pts.size() < 2) return;
    capDoubleEndAt(ent, QStringLiteral("cutStart"), pts.front(), joints);
    capDoubleEndAt(ent, QStringLiteral("cutEnd"), pts.back(), joints);
}

void Editor::applyExtendFence(const Vec2& a, const Vec2& b) {
    if (!doc_) return;
    std::vector<Entity> refs;
    for (const QString& id : trimRefIds_)
        if (const Entity* r = entityById(id)) refs.push_back(*r);
    if (refs.empty()) { emit message(QStringLiteral("Select the boundary object first.")); return; }

    pushUndo();
    int count = 0;
    std::vector<QString> ids;
    for (const auto& e : doc_->entities()) ids.push_back(e.id);
    for (const QString& id : ids) {
        if (std::find(trimRefIds_.begin(), trimRefIds_.end(), id) != trimRefIds_.end()) continue;
        Entity* obj = entityById(id);
        if (!obj) continue;
        const bool line = obj->type == EntityType::Line;
        const bool dbl = obj->type == EntityType::DoubleLine;
        const bool poly = obj->type == EntityType::Polyline;
        if (!line && !dbl && !poly) continue;
        // The fence must cross the object's centreline.
        std::optional<Vec2> x;
        if (poly) {
            for (size_t i = 1; i < obj->points.size() && !x; ++i)
                x = segmentSegmentIntersection(a, b, obj->points[i - 1], obj->points[i]);
        } else {
            x = segmentSegmentIntersection(a, b, obj->a, obj->b);
        }
        if (!x) continue;
        if (extendEntityTo(*obj, *x, refs)) {
            ++count; // extendEntityTo caps a double line/polyline's runs to the target
        }
    }
    if (count == 0) {
        undo();
        emit message(QStringLiteral("Nothing the line crossed can reach the reference."));
        return;
    }
    doc_->setDirty(true);
    emit message(QStringLiteral("Extended %1 object(s) — draw another line or Esc to finish").arg(count));
    emit changed();
}

void Editor::applyCornerModAt(Entity& e, int knee, const std::vector<Vec2>& corner, double gap) {
    const int n = static_cast<int>(e.points.size());
    if (tool_ == ToolId::Chamfer) {
        if (chamferDist_ > 0 && !chamferDistFits(corner, chamferDist_ + gap / 2.0)) {
            emit message(
                QStringLiteral("Chamfer distance %1 is too big for that corner").arg(chamferDist_));
            return;
        }
        pushUndo();
        QJsonArray arr = e.meta.value("cornerChamfers").toArray();
        while (arr.size() < n) arr.append(0.0);
        arr[knee] = chamferDist_;
        e.meta.insert(QStringLiteral("cornerChamfers"), arr);
        // A chamfer and a fillet on the same knee are mutually exclusive.
        if (e.meta.contains("cornerRadii")) {
            QJsonArray r = e.meta.value("cornerRadii").toArray();
            if (knee < r.size()) { r[knee] = 0.0; e.meta.insert(QStringLiteral("cornerRadii"), r); }
        }
        doc_->setDirty(true);
        emit message(QStringLiteral("Chamfer applied to the corner (distance %1)").arg(chamferDist_));
    } else {
        if (filletRadius_ > 0 && !filletRadiusFits(corner, filletRadius_ + gap / 2.0)) {
            emit message(
                QStringLiteral("Fillet radius %1 is too big for that corner").arg(filletRadius_));
            return;
        }
        pushUndo();
        QJsonArray arr = e.meta.value("cornerRadii").toArray();
        while (arr.size() < n) arr.append(0.0);
        arr[knee] = filletRadius_;
        e.meta.insert(QStringLiteral("cornerRadii"), arr);
        if (e.meta.contains("cornerChamfers")) {
            QJsonArray c = e.meta.value("cornerChamfers").toArray();
            if (knee < c.size()) { c[knee] = 0.0; e.meta.insert(QStringLiteral("cornerChamfers"), c); }
        }
        doc_->setDirty(true);
        emit message(QStringLiteral("Fillet applied to the corner (radius %1)").arg(filletRadius_));
    }
}

std::vector<Entity*> Editor::chainMatesOf(const Entity& e) {
    std::vector<Entity*> out;
    const QString chain = e.meta.value(QStringLiteral("chain")).toString();
    if (chain.isEmpty() || !doc_) return out;
    for (auto& x : doc_->entities())
        if (x.id != e.id && x.meta.value(QStringLiteral("chain")).toString() == chain)
            out.push_back(&x);
    return out;
}

QString Editor::balloonAt(const Vec2& p) const {
    if (!doc_) return {};
    for (const auto& e : doc_->entities()) {
        if (!e.meta.value("balloon").toBool()) continue;
        if (distance(e.center, p) <= e.radius) return e.id; // inside the circle
        // Or on one of its leader lines (attach -> knee -> tip).
        const auto tips = balloonLeaders(e);
        if (!tips.empty()) {
            const Vec2 attach = balloonAttach(e), knee = balloonKnee(e);
            if (closestPointOnSegment(p, attach, knee).distance <= worldTolerance()) return e.id;
            for (const auto& tip : tips)
                if (closestPointOnSegment(p, knee, tip).distance <= worldTolerance()) return e.id;
        }
    }
    return {};
}

std::pair<Vec2, Vec2> Editor::visibleWall(const Entity& e, int seg) const {
    const int n = static_cast<int>(e.points.size());
    if (n < 2) return {e.points.empty() ? Vec2{} : e.points[0], e.points.empty() ? Vec2{} : e.points[0]};
    const bool closed = e.type == EntityType::Rect || e.type == EntityType::Polygon;
    Vec2 a = e.points[seg], b = e.points[(seg + 1) % n];
    const Vec2 ab = (b - a).normalized();
    const QJsonArray radii = e.meta.value("cornerRadii").toArray();
    const QJsonArray chIn = e.meta.value("cornerChamfers").toArray();
    const QJsonArray chOut = e.meta.value("cornerChamfers2").toArray();
    // How far the drawn edge starts away from vertex v; `outEdge` = this edge leaves v
    // (uses the chamfer "out" setback), otherwise it arrives at v (chamfer "in").
    auto setbackAt = [&](int v, bool outEdge) -> double {
        if (!closed && (v == 0 || v == n - 1)) return 0.0; // open ends: no corner
        const auto val = [&](const QJsonArray& arr) {
            return v < arr.size() ? arr[v].toDouble() : 0.0;
        };
        const double cOut = val(chOut.isEmpty() ? chIn : chOut);
        const double cIn = val(chIn);
        const double cham = outEdge ? cOut : cIn;
        if (cham > 0.0) return cham;
        const double r = val(radii);
        if (r > 0.0) {
            const Vec2 p0 = e.points[(v - 1 + n) % n], p1 = e.points[v], p2 = e.points[(v + 1) % n];
            const double cosT = std::clamp(dot((p0 - p1).normalized(), (p2 - p1).normalized()), -1.0, 1.0);
            const double theta = std::acos(cosT);
            if (theta > 1e-3 && theta < 3.14159265 - 1e-3) {
                double tset = r / std::tan(theta / 2.0);
                tset = std::min(tset, 0.5 * std::min(distance(p0, p1), distance(p1, p2)));
                return tset;
            }
        }
        return 0.0;
    };
    const double edgeLen = distance(a, b);
    const double sA = std::min(setbackAt(seg, true), edgeLen);
    const double sB = std::min(setbackAt((seg + 1) % n, false), edgeLen);
    return {a + ab * sA, b - ab * sB};
}

void Editor::handlePickModifyPress(const Vec2& p) {
    auto hit = hitTest(p);
    // The second pick has to be the OTHER object. Where the two meet at a joint they are both under the
    // cursor, and whichever is on top answers first - so at a joint the second click would land on the
    // one already picked and the command would wait for ever. Asked again, ignoring what is already
    // picked, it finds the other one.
    if (hit && !pickIds_.empty() &&
        std::find(pickIds_.begin(), pickIds_.end(), *hit) != pickIds_.end()) {
        if (auto other = hitTest(p, pickIds_)) hit = other;
    }

    // Trim and Extend are handled in onPointerPress (reference + fence line).
    // Fillet / Chamfer: pick two lines.
    if (!hit) { emit message(QStringLiteral("Pick the first line.")); return; }
    pickIds_.push_back(*hit);
    pickPts_.push_back(p);

    // DOUBLE POLYLINE FILLET: a SINGLE click edits the nearest knee (interior vertex)
    // directly — apply/redefine its radius — so you don't pick both runs (and nothing
    // is left marked orange waiting for a 2nd click). Chamfer needs two points to cut
    // "exactly between" them, so it uses the two-pick path below.
    if (tool_ == ToolId::Fillet) {
        if (Entity* dp = entityById(pickIds_.back());
            dp && dp->type == EntityType::Polyline && dp->meta.value("double").toBool() &&
            dp->points.size() >= 3) {
            const int n = static_cast<int>(dp->points.size());
            int knee = -1;
            double bd = std::numeric_limits<double>::max();
            for (int i = 1; i < n - 1; ++i) {
                const double d = distance(p, dp->points[i]);
                if (d < bd) { bd = d; knee = i; }
            }
            // Only when the click is actually AT that knee. Taking the nearest knee however far away it
            // was meant a click anywhere on a double polyline rounded a corner somewhere else - and it
            // swallowed the click, so a double polyline could not be paired with anything either.
            if (knee >= 0 && bd <= worldTolerance() * 2.5) {
                const double gap = dp->meta.value("gap").toDouble(10.0);
                const std::vector<Vec2> corner = {dp->points[knee - 1], dp->points[knee],
                                                  dp->points[knee + 1]};
                applyCornerModAt(*dp, knee, corner, gap);
                pickIds_.clear();
                pickPts_.clear();
                emit changed();
                return;
            }
        }
    }

    if (pickIds_.size() < 2) {
        emit message(QStringLiteral("%1: pick the second line.").arg(toolLabel(tool_)));
        emit changed();
        return;
    }

    Entity* l1 = entityById(pickIds_[0]);
    Entity* l2 = entityById(pickIds_[1]);

    // Fillet a VERTEX of a multi-joint object: both picks land on the SAME polyline /
    // rect / polygon — round the corner between the two picked segments by setting
    // that vertex's per-corner radius. For a double polyline BOTH physical runs round
    // (renderer rounds the centreline at radius + gap/2), keeping the gap. Rect/
    // polygon are closed, so segments wrap and every vertex is filletable.
    if ((tool_ == ToolId::Fillet || tool_ == ToolId::Chamfer) && l1 && l1 == l2 &&
        (l1->type == EntityType::Polyline || l1->type == EntityType::Rect ||
         l1->type == EntityType::Polygon) &&
        l1->points.size() >= 3) {
        const int n = static_cast<int>(l1->points.size());
        const bool closed = (l1->type == EntityType::Rect || l1->type == EntityType::Polygon);
        auto segOf = [&](const Vec2& pk) {
            int best = -1;
            double bd = std::numeric_limits<double>::max();
            const int edges = closed ? n : n - 1;
            for (int i = 0; i < edges; ++i) {
                const double d =
                    closestPointOnSegment(pk, l1->points[i], l1->points[(i + 1) % n]).distance;
                if (d < bd) { bd = d; best = i; }
            }
            return best;
        };
        const int s1 = segOf(pickPts_[0]), s2 = segOf(pickPts_[1]);
        int knee = -1;
        if (s1 >= 0 && s2 >= 0) {
            if (std::abs(s1 - s2) == 1) knee = std::max(s1, s2); // adjacent edges share the higher vertex
            else if (closed && ((s1 == 0 && s2 == n - 1) || (s1 == n - 1 && s2 == 0)))
                knee = 0; // wrap-around corner (edge 0 and edge n-1 meet at vertex 0)
        }
        if (knee >= 0 && tool_ == ToolId::Chamfer) {
            // Chamfer EXACTLY between the two clicked points: the setback on each edge
            // is that edge's clicked point's distance from the shared corner (knee), so
            // the cut passes through where the user clicked. Stored asymmetrically.
            const int inEdge = (knee - 1 + n) % n; // edge ENDING at the knee
            auto setback = [&](int seg, const Vec2& pk) {
                const Vec2 cp =
                    closestPointOnSegment(pk, l1->points[seg], l1->points[(seg + 1) % n]).point;
                return distance(cp, l1->points[knee]);
            };
            double dIn, dOut;
            if (s1 == inEdge) { dIn = setback(s1, pickPts_[0]); dOut = setback(s2, pickPts_[1]); }
            else { dIn = setback(s2, pickPts_[1]); dOut = setback(s1, pickPts_[0]); }
            const double inLen = distance(l1->points[inEdge], l1->points[knee]);
            const double outLen = distance(l1->points[knee], l1->points[(knee + 1) % n]);
            if (dIn < 1e-6 || dOut < 1e-6 || dIn > inLen + 1e-6 || dOut > outLen + 1e-6) {
                emit message(QStringLiteral("Pick two points along the walls of one corner."));
            } else {
                pushUndo();
                QJsonArray inArr = l1->meta.value("cornerChamfers").toArray();
                QJsonArray outArr = l1->meta.value("cornerChamfers2").toArray();
                while (inArr.size() < n) inArr.append(0.0);
                while (outArr.size() < n) outArr.append(0.0);
                inArr[knee] = dIn;
                outArr[knee] = dOut;
                l1->meta.insert(QStringLiteral("cornerChamfers"), inArr);
                l1->meta.insert(QStringLiteral("cornerChamfers2"), outArr);
                if (l1->meta.contains("cornerRadii")) { // fillet/chamfer exclusive per knee
                    QJsonArray r = l1->meta.value("cornerRadii").toArray();
                    if (knee < r.size()) { r[knee] = 0.0; l1->meta.insert(QStringLiteral("cornerRadii"), r); }
                }
                doc_->setDirty(true);
                emit message(QStringLiteral("Chamfer applied between the picked points"));
            }
        } else if (knee >= 0) {
            const double gap =
                l1->meta.value("double").toBool() ? l1->meta.value("gap").toDouble(10.0) : 0.0;
            const std::vector<Vec2> corner = {l1->points[(knee - 1 + n) % n], l1->points[knee],
                                              l1->points[(knee + 1) % n]};
            applyCornerModAt(*l1, knee, corner, gap);
        } else {
            emit message(QStringLiteral("Pick two segments that share a corner."));
        }
        pickIds_.clear();
        pickPts_.clear();
        emit changed();
        return;
    }

    // Fillet TWO connected DOUBLE RUNS - a double line or a double polyline, in any combination: if
    // they share an end, they merge into a single double polyline with a rounded knee at the join, so
    // both rails round together and the gap stays constant. (A double run and a SINGLE one are refused
    // further down, with the reason: two edges cannot be rounded into one.)
    auto doubleRunPoints = [](const Entity& e) {
        std::vector<Vec2> pts;
        if (e.type == EntityType::DoubleLine) pts = {e.a, e.b};
        else if (e.type == EntityType::Polyline && e.meta.value("double").toBool()) pts = e.points;
        return pts;
    };
    if ((tool_ == ToolId::Fillet || tool_ == ToolId::Chamfer) && l1 && l2 && l1 != l2 &&
        !doubleRunPoints(*l1).empty() && !doubleRunPoints(*l2).empty()) {
        std::vector<Vec2> r1 = doubleRunPoints(*l1), r2 = doubleRunPoints(*l2);
        // Which ends meet: orient both so r1 ENDS at the join and r2 STARTS there.
        const Vec2 e1[2] = {r1.front(), r1.back()};
        const Vec2 e2[2] = {r2.front(), r2.back()};
        int bi = 0, bj = 0;
        double bd = std::numeric_limits<double>::max();
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j) {
                const double dd = distance(e1[i], e2[j]);
                if (dd < bd) { bd = dd; bi = i; bj = j; }
            }
        if (bd > worldTolerance() * 4.0) {
            refuseAt(pickPts_.back(),
                     QStringLiteral("those two runs do not share an end - a corner is where two runs "
                                    "meet"));
            pickIds_.clear();
            pickPts_.clear();
            emit changed();
            return;
        }
        if (bi == 0) std::reverse(r1.begin(), r1.end());     // r1 must END at the join
        if (bj == 1) std::reverse(r2.begin(), r2.end());     // r2 must START there
        const Vec2 shared = lerp(r1.back(), r2.front(), 0.5);
        r1.back() = shared;
        r2.front() = shared;
        const double gap = l1->meta.value("gap").toDouble(10.0);
        const std::vector<Vec2> corner = {r1[r1.size() - 2], shared, r2[1]};
        const bool cham = tool_ == ToolId::Chamfer;
        const double amt = cham ? chamferDist_ : filletRadius_;
        const bool fits = cham ? chamferDistFits(corner, amt + gap / 2.0)
                               : filletRadiusFits(corner, amt + gap / 2.0);
        if (amt > 0 && !fits) {
            emit message(QStringLiteral("%1 %2 is too big for that corner")
                             .arg(cham ? QStringLiteral("Chamfer distance")
                                       : QStringLiteral("Fillet radius"))
                             .arg(amt));
            pickIds_.clear(); pickPts_.clear();
            return;
        }
        pushUndo();
        Entity merged;
        merged.type = EntityType::Polyline;
        merged.layer = l1->layer;
        merged.color = l1->color; // keep the picked lines' colour (never the pick mark)
        merged.weight = l1->weight;
        merged.points = r1;
        for (size_t i = 1; i < r2.size(); ++i) merged.points.push_back(r2[i]);
        merged.meta.insert(QStringLiteral("double"), true);
        merged.meta.insert(QStringLiteral("gap"), gap);
        // The radius goes at the JOIN, and every other corner the two runs already had is left as it
        // was: merging them must not straighten anything.
        QJsonArray arr;
        const QJsonArray was1 = l1->meta.value(cham ? "cornerChamfers" : "cornerRadii").toArray();
        const QJsonArray was2 = l2->meta.value(cham ? "cornerChamfers" : "cornerRadii").toArray();
        const int joinIdx = static_cast<int>(r1.size()) - 1;
        for (int i = 0; i < static_cast<int>(merged.points.size()); ++i) {
            if (i == joinIdx) { arr.append(amt); continue; }
            // Whichever run that vertex came from, in the order it now sits in.
            const int from1 = (bi == 0) ? (static_cast<int>(r1.size()) - 1 - i) : i;
            const int from2 = (bj == 1) ? (static_cast<int>(r2.size()) - 1 - (i - joinIdx)) : (i - joinIdx);
            const QJsonArray& src = (i < joinIdx) ? was1 : was2;
            const int idx = (i < joinIdx) ? from1 : from2;
            arr.append(idx >= 0 && idx < src.size() ? src[idx].toDouble() : 0.0);
        }
        merged.meta.insert(cham ? QStringLiteral("cornerChamfers") : QStringLiteral("cornerRadii"),
                           arr);
        const QString id1 = pickIds_[0], id2 = pickIds_[1];
        auto& ents = doc_->entities();
        ents.erase(std::remove_if(ents.begin(), ents.end(),
                                  [&](const Entity& e) { return e.id == id1 || e.id == id2; }),
                   ents.end());
        addEntity(std::move(merged));
        emit message(QStringLiteral("%1 the double-line corner (%2 %3)")
                         .arg(cham ? QStringLiteral("Chamfered") : QStringLiteral("Filleted"),
                              cham ? QStringLiteral("distance") : QStringLiteral("radius"))
                         .arg(amt));
        pickIds_.clear();
        pickPts_.clear();
        emit changed();
        return;
    }

    // FILLET where the END OF A RUN meets something else: a polyline (or a rect, or a polygon) picked
    // with a line, a curve, or ANOTHER RUN. The run's own corners are rounded by picking two of its
    // segments (above); this is the other case, and it is the ordinary one - a shape drawn as a polyline
    // for three sides and a line or a notch for the fourth has its corners BETWEEN objects, not inside
    // one. The picked segment of each run stands in as a line, the fillet is worked out from those, and
    // each run's END VERTEX is taken back to where the fillet starts.
    if ((tool_ == ToolId::Fillet) && l1 && l2 && l1 != l2) {
        auto isRun = [](const Entity& e) {
            return (e.type == EntityType::Polyline || e.type == EntityType::Rect ||
                    e.type == EntityType::Polygon) &&
                   e.points.size() >= 2 && !e.meta.value(QStringLiteral("double")).toBool();
        };
        auto isCurve = [](const Entity& e) {
            return e.type == EntityType::Circle || e.type == EntityType::Arc ||
                   e.type == EntityType::Ellipse;
        };
        // A run's picked END segment, standing in as a line: `inner` is the end that must not move,
        // `outer` the one the fillet takes back. Empty when the pick is not near an end.
        struct Stand {
            Entity line;          // the stand-in
            Entity* src = nullptr;// the run it came from (null: it IS a line or a curve)
            Entity* plain = nullptr;   // ... and then THIS is the line itself, which has to be written back
            int endIdx = -1;      // which vertex of the run moves
        };
        auto standFor = [&](Entity* e, const Vec2& pick, const Vec2& otherPick, Stand& s,
                           QString& why) {
            if (!isRun(*e)) {     // a plain line stands for itself
                s.line = *e;
                s.src = nullptr;
                // A COPY stands in, and the fillet trims the copy. Kept only as a copy - which is what
                // happened here - the real line was left at its full length while the arc left it early:
                // the stub the fillet is supposed to have taken off, still on the drawing. So the line it
                // came from is remembered, and written back with the rest.
                s.plain = (e->type == EntityType::Line) ? e : nullptr;
                return true;
            }
            const int n = static_cast<int>(e->points.size());
            const bool closed = e->type == EntityType::Rect || e->type == EntityType::Polygon;
            int seg = -1;
            double bd = std::numeric_limits<double>::max();
            const int edges = closed ? n : n - 1;
            for (int i = 0; i < edges; ++i) {
                const double dd =
                    closestPointOnSegment(pick, e->points[i], e->points[(i + 1) % n]).distance;
                if (dd < bd) { bd = dd; seg = i; }
            }
            bool atStart = !closed && seg == 0;
            bool atEnd = !closed && seg == n - 2;
            // A run of a single segment is BOTH its first and its last: which of its two ends is the
            // corner is then said by the other object - the corner is where the two of them are.
            if (atStart && atEnd)
                (distance(e->points.front(), otherPick) <= distance(e->points.back(), otherPick)
                     ? atEnd
                     : atStart) = false;
            if (!atStart && !atEnd) {
                why = closed ? QStringLiteral("a closed shape cannot be rounded against another object "
                                              "- pick two of its own sides instead")
                             : QStringLiteral("pick the run near one of its two ENDS: only an end can "
                                              "be taken back to a fillet");
                return false;
            }
            s.endIdx = atStart ? 0 : n - 1;
            const int innerIdx = atStart ? 1 : n - 2;
            s.line = Entity{};
            s.line.type = EntityType::Line;
            s.line.layer = e->layer;
            s.line.a = e->points[innerIdx];       // stays put
            s.line.b = e->points[s.endIdx];       // taken back
            s.src = e;
            return true;
        };

        const bool anyRun = isRun(*l1) || isRun(*l2);
        const bool okPair = anyRun && (isRun(*l1) || l1->type == EntityType::Line || isCurve(*l1)) &&
                            (isRun(*l2) || l2->type == EntityType::Line || isCurve(*l2));
        if (okPair) {
            // A curve cannot be one of a pair of runs: the curve case is handled below, with the run
            // standing in as a line, so only one of the two may be curved.
            const bool bothCurved = isCurve(*l1) && isCurve(*l2);
            Stand s1, s2;
            QString why;
            if (bothCurved || !standFor(l1, pickPts_[0], pickPts_[1], s1, why) ||
                !standFor(l2, pickPts_[1], pickPts_[0], s2, why)) {
                refuseAt(pickPts_.back(),
                         why.isEmpty() ? QStringLiteral("those two cannot be rounded together") : why);
                pickIds_.clear();
                pickPts_.clear();
                emit changed();
                return;
            }
            pushUndo();
            CornerResult r;
            if (isCurve(*l1))
                r = filletLineCurve(s2.line, *l1, filletRadius_, pickPts_[1], pickPts_[0]);
            else if (isCurve(*l2))
                r = filletLineCurve(s1.line, *l2, filletRadius_, pickPts_[0], pickPts_[1]);
            else
                r = filletLines(s1.line, s2.line, filletRadius_, pickPts_[0], pickPts_[1]);
            // Each stand-in must have kept the end that belongs to the rest of its run.
            auto writeBack = [&](Stand& s) {
                if (s.plain) {
                    // Whichever end the fillet took back: a plain line is trimmed at either end, and which
                    // one is decided by the side that was picked.
                    s.plain->a = s.line.a;
                    s.plain->b = s.line.b;
                    return true;
                }
                if (!s.src) return true;
                const int n = static_cast<int>(s.src->points.size());
                const int innerIdx = (s.endIdx == 0) ? 1 : n - 2;
                if (distance(s.line.a, s.src->points[innerIdx]) > 1e-9) return false;
                s.src->points[s.endIdx] = s.line.b;
                s.src->meta.remove(QStringLiteral("cornerRadii"));
                s.src->meta.remove(QStringLiteral("cornerChamfers"));
                return true;
            };
            if (r.ok && writeBack(s1) && writeBack(s2)) {
                if (r.hasConnector) {
                    Entity arc = r.connector;
                    addEntity(std::move(arc));
                }
                doc_->setDirty(true);
                clearRefusal();
                emit message(r.hasConnector
                                 ? QStringLiteral("Rounded the corner (radius %1)").arg(filletRadius_)
                                 : QStringLiteral("Fillet (radius 0): brought them together"));
            } else {
                undo();
                refuseAt(pickPts_.back(),
                         r.tooBig ? QStringLiteral("radius %1 is too big for that corner")
                                        .arg(filletRadius_)
                                  : QStringLiteral("they meet at the other end - pick each nearer the "
                                                   "corner"));
            }
            pickIds_.clear();
            pickPts_.clear();
            emit changed();
            return;
        }
    }

    // FILLET a LINE to a CURVE: a circle, an arc, an ellipse or a piece of one. Picked in either order.
    // The same idea as two lines - an arc of the set radius tangent to both, made near where each was
    // picked - and the reason this was worth having: a harness drawing is full of runs that meet a
    // radius, and a half ellipse is what is left after trimming one.
    if (tool_ == ToolId::Fillet && l1 && l2 && l1 != l2) {
        auto curved = [](const Entity& e) {
            return e.type == EntityType::Circle || e.type == EntityType::Arc ||
                   e.type == EntityType::Ellipse;
        };
        Entity* ln = nullptr;
        Entity* cv = nullptr;
        Vec2 pl{0.0, 0.0}, pc{0.0, 0.0};
        if (l1->type == EntityType::Line && curved(*l2)) {
            ln = l1; cv = l2; pl = pickPts_[0]; pc = pickPts_[1];
        } else if (l2->type == EntityType::Line && curved(*l1)) {
            ln = l2; cv = l1; pl = pickPts_[1]; pc = pickPts_[0];
        }
        if (ln && cv) {
            pushUndo();
            const CornerResult r = filletLineCurve(*ln, *cv, filletRadius_, pl, pc);
            if (r.ok) {
                if (r.hasConnector) {
                    Entity arc = r.connector;
                    addEntity(std::move(arc));
                }
                emit message(r.hasConnector
                                 ? QStringLiteral("Filleted the line to the curve (radius %1)")
                                       .arg(filletRadius_)
                                 : QStringLiteral("Fillet (radius 0): brought them together"));
            } else {
                undo();
                refuseAt(pickPts_.back(),
                         r.tooBig ? QStringLiteral("no arc of radius %1 touches both - try a smaller "
                                                   "one").arg(filletRadius_)
                                  : QStringLiteral("those two cannot be filleted"));
            }
            pickIds_.clear();
            pickPts_.clear();
            emit changed();
            return;
        }
    }

    if (l1 && l2 && l1 != l2 && l1->type == EntityType::Line && l2->type == EntityType::Line) {
        pushUndo();
        CornerResult r;
        if (tool_ == ToolId::Fillet) {
            r = filletLines(*l1, *l2, filletRadius_, pickPts_[0], pickPts_[1]);
        } else {
            // Chamfer cuts a straight line between the two PICKED points: the cut
            // distance on each line is the picked point's distance from the corner.
            // The kept side is the end farthest from the corner (the picked point may
            // sit nearer the corner than the far end, so don't use it as the side).
            if (auto P = lineLineIntersection(l1->a, l1->b, l2->a, l2->b)) {
                const Vec2 c1 = closestPointOnSegment(pickPts_[0], l1->a, l1->b).point;
                const Vec2 c2 = closestPointOnSegment(pickPts_[1], l2->a, l2->b).point;
                const Vec2 keep1 = distance(l1->a, *P) >= distance(l1->b, *P) ? l1->a : l1->b;
                const Vec2 keep2 = distance(l2->a, *P) >= distance(l2->b, *P) ? l2->a : l2->b;
                r = chamferLines(*l1, *l2, distance(*P, c1), distance(*P, c2), keep1, keep2);
            }
        }
        if (r.ok) {
            if (r.hasConnector) addEntity(std::move(r.connector));
            emit message(r.hasConnector ? QStringLiteral("%1 applied").arg(toolLabel(tool_))
                                        : QStringLiteral("Fillet (radius 0): corner joined"));
        } else if (r.tooBig) {
            undo();
            refuseAt(pickPts_.back(), QStringLiteral("radius %1 is too big for these lines")
                                          .arg(filletRadius_));
        } else {
            undo();
            refuseAt(pickPts_.back(), QStringLiteral("these two do not form a corner"));
        }
        if (r.ok) clearRefusal();
    } else {
        // Say WHAT was picked, and MARK the place: a line of text at the bottom of the window is easy to
        // miss with the eye on the drawing, so the refusal is drawn where it happened as well.
        const QString a = l1 ? entityTypeToString(l1->type) : QStringLiteral("nothing");
        const QString b = l2 ? entityTypeToString(l2->type) : QStringLiteral("nothing");
        auto isDouble = [](const Entity* e) {
            return e && (e->type == EntityType::DoubleLine ||
                         e->meta.value(QStringLiteral("double")).toBool());
        };
        const bool dbl = isDouble(l1) || isDouble(l2);
        if (l1 && l1 == l2)
            refuseAt(pickPts_.back(),
                     QStringLiteral("both clicks landed on the same %1 - click the other object, or "
                                    "two sides of one run to round its own corner").arg(a));
        else if (dbl)
            refuseAt(pickPts_.back(),
                     QStringLiteral("a double line has two edges and a single one has one - round a "
                                    "double to another double, and they join into one run"));
        else
            refuseAt(pickPts_.back(),
                     QStringLiteral("%1 cannot be rounded to a %2 yet - lines, arcs, circles, ellipses "
                                    "and the ends of a run can").arg(a, b));
    }
    pickIds_.clear();
    pickPts_.clear();
    emit changed();
}

// ---- Array (AutoCAD ARRAYRECT / ARRAYPOLAR / ARRAYPATH) --------------------
// An array is a live SESSION, not a one-shot dialog: arming it snapshots the
// selection, everything is ghost-previewed against the current spec, and the
// document is only touched when the session is committed. That way counts,
// spacings, the polar centre and the path can all be re-tuned while watching the
// result — the way AutoCAD's "Array Creation" ribbon works.

void Editor::beginArray(ArrayKind kind) {
    if (!doc_) return;
    auto sel = selectedEntities();
    if (sel.empty()) {
        emit message(QStringLiteral("Select objects to array."));
        return;
    }
    arraySrc_.clear();
    arraySrcIds_.clear();
    Bounds b;
    for (const Entity* e : sel) {
        arraySrc_.push_back(*e);
        arraySrcIds_.push_back(e->id);
        b.expand(e->bounds());
    }
    // The base point: everything (rect offsets, the polar radius, where the source
    // lands on a path) is measured from the selection's centre.
    arrayBase_ = b.valid ? b.center() : Vec2{0.0, 0.0};
    arrayActive_ = true;
    arrayHasCenter_ = false;
    arrayPathId_.clear();
    arrayPathPts_.clear();
    arrayPathClosed_ = false;
    setArrayKind(kind);
}

void Editor::setArrayKind(ArrayKind kind) {
    if (!arrayActive_) return;
    arraySpec_.kind = kind;
    lastArrayKind_ = kind;
    seedArrayDefaults();
    switch (kind) {
    case ArrayKind::Rect:
        emit message(QStringLiteral("Rectangular array — set columns / rows in the Array ribbon, "
                                    "then Enter to place (Esc cancels)"));
        break;
    case ArrayKind::Polar:
        emit message(arrayHasCenter_
                         ? QStringLiteral("Polar array — click to move the centre, Enter to place")
                         : QStringLiteral("Polar array — click the centre point of the array"));
        break;
    case ArrayKind::Path:
        emit message(arrayPathPts_.size() >= 2
                         ? QStringLiteral("Path array — set the items in the Array ribbon, "
                                          "then Enter to place")
                         : QStringLiteral("Path array — click the path the items should track along"));
        break;
    }
    emit arrayChanged();
    emit overlayChanged();
}

void Editor::seedArrayDefaults() {
    Bounds b;
    for (const Entity& e : arraySrc_) b.expand(e.bounds());
    const double w = (b.valid && b.width() > 1e-6) ? b.width() : 20.0;
    const double h = (b.valid && b.height() > 1e-6) ? b.height() : 20.0;
    // One and a half object widths apart, so the very first preview shows copies
    // standing clear of each other instead of overlapping.
    const double stepX = w * 1.5;
    const double stepY = h * 1.5;
    switch (arraySpec_.kind) {
    case ArrayKind::Rect:
        arraySpec_.count = 4;
        arraySpec_.rows = 3;
        arraySpec_.colSpacing = stepX;
        arraySpec_.rowSpacing = stepY;
        arraySpec_.axisAngle = 0.0;
        break;
    case ArrayKind::Polar:
        arraySpec_.count = 6;
        arraySpec_.rows = 1;
        arraySpec_.rowSpacing = std::max(stepX, stepY); // radial ring spacing
        arraySpec_.fillAngle = 360.0;
        arraySpec_.rotateItems = true;
        arraySpec_.ccw = true;
        break;
    case ArrayKind::Path:
        arraySpec_.count = 6;
        arraySpec_.rows = 1;
        arraySpec_.rowSpacing = stepY; // rows run perpendicular to the path
        arraySpec_.pathSpacing = stepX;
        arraySpec_.divide = true;
        arraySpec_.startOffset = 0.0;
        arraySpec_.alignToPath = true;
        break;
    }
}

bool Editor::arrayAwaitingCenter() const {
    return arrayActive_ && arraySpec_.kind == ArrayKind::Polar && !arrayHasCenter_;
}

bool Editor::arrayAwaitingPath() const {
    return arrayActive_ && arraySpec_.kind == ArrayKind::Path && arrayPathPts_.size() < 2;
}

// Until the centre is clicked it tracks the cursor, so the ring of items sweeps
// around live and can be judged before being pinned.
Vec2 Editor::arrayCenterInUse() const {
    return arrayHasCenter_ ? arraySpec_.center : cursor_;
}

std::vector<ArrayPlacement> Editor::arrayPreview() const {
    if (!arrayActive_ || arraySrc_.empty()) return {};
    ArraySpec s = arraySpec_;
    if (s.kind == ArrayKind::Polar) s.center = arrayCenterInUse();
    if (s.kind == ArrayKind::Path && arrayPathPts_.size() < 2) return {};
    return arrayPlacements(s, arrayBase_, arrayPathPts_, arrayPathClosed_);
}

int Editor::arrayItemCount() const {
    return static_cast<int>(arrayPreview().size());
}

double Editor::arrayPathLength() const {
    if (!arrayActive_ || arraySpec_.kind != ArrayKind::Path || arrayPathPts_.size() < 2) return 0.0;
    return polylineLength(arrayPathPts_);
}

double Editor::arrayItemStep() const {
    const double len = arrayPathLength();
    if (len <= 0.0) return 0.0;
    return arrayPathStep(arraySpec_, len, arrayPathClosed_);
}

void Editor::arrayParamsChanged() {
    if (arrayActive_) emit overlayChanged();
}

void Editor::handleArrayPress(const Vec2& p) {
    if (arraySpec_.kind == ArrayKind::Polar) {
        // Every click (re)pins the centre — the ring can be re-aimed as often as needed.
        arraySpec_.center = p;
        arrayHasCenter_ = true;
        emit message(QStringLiteral("Centre set (%1 items) — tune the Array ribbon, Enter to place")
                         .arg(arrayItemCount()));
        emit arrayChanged();
        emit overlayChanged();
        return;
    }
    if (arraySpec_.kind == ArrayKind::Path) {
        auto hit = hitTest(p);
        if (!hit) {
            emit message(QStringLiteral("Click the path object the items should track along"));
            return;
        }
        // An object being arrayed cannot also be its own path.
        if (std::find(arraySrcIds_.begin(), arraySrcIds_.end(), *hit) != arraySrcIds_.end()) {
            emit message(QStringLiteral("That object is part of the array — pick another path"));
            return;
        }
        const Entity* pathEnt = nullptr;
        for (const auto& e : doc_->entities())
            if (e.id == *hit) { pathEnt = &e; break; }
        bool closed = false;
        std::vector<Vec2> pts;
        if (pathEnt) pts = arrayPathPoints(*pathEnt, &closed);
        if (pts.size() < 2) {
            emit message(QStringLiteral("That object can't be a path — pick a line, polyline, arc, "
                                        "circle, ellipse or spline"));
            return;
        }
        arrayPathId_ = *hit;
        arrayPathPts_ = std::move(pts);
        arrayPathClosed_ = closed;
        emit message(QStringLiteral("Path set (%1 items) — tune the Array ribbon, Enter to place")
                         .arg(arrayItemCount()));
        emit arrayChanged();
        emit overlayChanged();
        return;
    }
    // Rectangular: the grid is entirely ribbon-driven, so a stray canvas click must
    // neither move nor commit it.
    emit message(QStringLiteral("Rectangular array — set columns / rows in the Array ribbon, "
                                "then Enter to place"));
}

void Editor::commitArray() {
    if (!arrayActive_) return;
    if (arrayAwaitingPath()) {
        emit message(QStringLiteral("Pick the path first, or press Esc to cancel"));
        return;
    }
    ArraySpec s = arraySpec_;
    if (s.kind == ArrayKind::Polar) {
        s.center = arrayCenterInUse(); // Enter with no click yet pins the cursor's centre
        arraySpec_.center = s.center;
        arrayHasCenter_ = true;
    }
    const auto places = arrayPlacements(s, arrayBase_, arrayPathPts_, arrayPathClosed_);
    if (places.empty()) {
        emit message(QStringLiteral("Array produced no items — check the count and spacing"));
        return;
    }
    // The source objects can have gone (an undo, or a delete from another path) while
    // the session was open — there is then nothing to array.
    bool anySourceLeft = false;
    for (const QString& id : arraySrcIds_)
        if (entityById(id)) { anySourceLeft = true; break; }
    if (!anySourceLeft) {
        emit message(QStringLiteral("The objects being arrayed are gone — array cancelled"));
        cancelArray();
        return;
    }
    pushUndo();
    const Vec2 pivot = arrayPivot(s, arrayBase_);
    // Item 0 IS the source selection: identity for a rect/polar array, and for a path
    // array the move that drops the originals onto the path (as ARRAYPATH does), so the
    // objects keep their ids and stay attached to whatever referenced them.
    for (const QString& id : arraySrcIds_)
        if (Entity* e = entityById(id)) {
            if (std::abs(places[0].rotate) > 1e-12) rotateEntity(*e, pivot, places[0].rotate);
            if (places[0].delta.lengthSq() > 1e-24) translateEntity(*e, places[0].delta);
        }
    std::vector<QString> ids = arraySrcIds_;
    for (size_t i = 1; i < places.size(); ++i) {
        // Each item of the array is its own copy set: the shields, labels and twists in item three name
        // item three's wires, not the originals'.
        std::vector<Entity> item;
        item.reserve(arraySrc_.size());
        QHash<QString, QString> oldToNew;
        for (const Entity& src : arraySrc_) {
            Entity clone = arrayItem(src, places[i], pivot);
            clone.id = uid();
            oldToNew.insert(src.id, clone.id);
            item.push_back(std::move(clone));
        }
        remapEntityReferences(item, oldToNew);
        for (Entity& clone : item) {
            ids.push_back(clone.id);
            doc_->entities().push_back(std::move(clone));
        }
    }
    const int items = static_cast<int>(places.size());
    const QString kindName = s.kind == ArrayKind::Rect    ? QStringLiteral("Rectangular")
                             : s.kind == ArrayKind::Polar ? QStringLiteral("Polar")
                                                          : QStringLiteral("Path");
    arrayActive_ = false;
    arraySrc_.clear();
    arraySrcIds_.clear();
    arrayPathId_.clear();
    arrayPathPts_.clear();
    arrayHasCenter_ = false;
    selected_ = ids; // the finished array is left selected, ready to move/copy
    doc_->setDirty(true);
    emit message(QStringLiteral("%1 array: %2 items").arg(kindName).arg(items));
    emit arrayChanged();
    emit selectionChanged();
    emit changed();
}

void Editor::arrayRepick() {
    if (!arrayActive_) return;
    if (arraySpec_.kind == ArrayKind::Polar) {
        arrayHasCenter_ = false; // the centre tracks the cursor again until clicked
        emit message(QStringLiteral("Click the centre point of the array"));
    } else if (arraySpec_.kind == ArrayKind::Path) {
        arrayPathId_.clear();
        arrayPathPts_.clear();
        arrayPathClosed_ = false;
        emit message(QStringLiteral("Click the path the items should track along"));
    } else {
        return; // a rectangular grid has nothing to pick
    }
    emit arrayChanged();
    emit overlayChanged();
}

void Editor::cancelArray() {
    if (!arrayActive_) return;
    arrayActive_ = false;
    arraySrc_.clear();
    arraySrcIds_.clear();
    arrayPathId_.clear();
    arrayPathPts_.clear();
    arrayHasCenter_ = false;
    emit message(QStringLiteral("Array cancelled"));
    emit arrayChanged();
    emit overlayChanged();
}

void Editor::explodeSelected() {
    auto sel = selectedEntities();
    if (sel.empty()) { emit message(QStringLiteral("Select objects to explode.")); return; }
    pushUndo();
    std::vector<Entity> additions;
    std::vector<QString> remove;
    // What is held together by a GROUP stamp comes apart by having the stamp taken off - there is nothing
    // to break into pieces, because the pieces are already there and merely being treated as one. That is
    // a copy of a block, a placed fitting, or a default model, and Explode is what anybody reaches for.
    int ungrouped = 0;
    for (const Entity* e : sel) {
        if (!entityBlockInstance(*e).isEmpty()) {
            if (Entity* live = entityById(e->id)) {
                clearEntityBlockStamp(*live);
                ++ungrouped;
            }
            continue;
        }
        auto parts = explodeEntity(*e);
        if (!parts.empty()) {
            additions.insert(additions.end(), parts.begin(), parts.end());
            remove.push_back(e->id);
        }
    }
    if (ungrouped > 0 && additions.empty() && remove.empty()) {
        selected_.clear();
        doc_->setDirty(true);
        emit selectionChanged();
        emit message(QStringLiteral("Broken apart into %1 separate object(s)").arg(ungrouped));
        emit changed();
        return;
    }
    auto& ents = doc_->entities();
    ents.erase(std::remove_if(ents.begin(), ents.end(),
                              [&](const Entity& e) {
                                  return std::find(remove.begin(), remove.end(), e.id) != remove.end();
                              }),
               ents.end());
    for (auto& a : additions) ents.push_back(std::move(a));
    selected_.clear();
    doc_->setDirty(true);
    emit selectionChanged();
    emit message(QStringLiteral("Exploded into %1 parts").arg(additions.size()));
    emit changed();
}

void Editor::purgeUnused() {
    if (!doc_) return;
    pushUndo();
    auto& ents = doc_->entities();

    // 1. Remove stray/degenerate geometry that carries nothing: zero-length lines,
    //    zero-radius circles/arcs/ellipses, empty text, and point-less runs.
    const int before = static_cast<int>(ents.size());
    ents.erase(std::remove_if(ents.begin(), ents.end(),
                              [](const Entity& e) {
                                  switch (e.type) {
                                  case EntityType::Line:
                                  case EntityType::DoubleLine:
                                      return distance(e.a, e.b) < 1e-9;
                                  case EntityType::Circle:
                                  case EntityType::Arc:
                                  case EntityType::Ellipse:
                                      return e.radius < 1e-9;
                                  case EntityType::Text:
                                      return e.meta.value(QStringLiteral("text"))
                                          .toString()
                                          .trimmed()
                                          .isEmpty();
                                  case EntityType::Polyline:
                                  case EntityType::Spline:
                                  case EntityType::Wire:
                                  case EntityType::Rect:
                                  case EntityType::Polygon:
                                  case EntityType::Hatch:
                                  case EntityType::Wipeout:
                                  case EntityType::Image:
                                      return e.points.size() < 2;
                                  default:
                                      return false;
                                  }
                              }),
               ents.end());
    const int removedEnts = before - static_cast<int>(ents.size());

    // 2. Remove unused (empty) layers — never the active layer or the default "0"
    //    (AutoCAD PURGE cannot remove layer 0 or the current layer either).
    auto& layers = doc_->layers();
    int removedLayers = 0;
    layers.erase(std::remove_if(layers.begin(), layers.end(),
                                [&](const Layer& l) {
                                    if (l.name == doc_->activeLayer() ||
                                        l.name == QLatin1String("0"))
                                        return false;
                                    const bool used = std::any_of(
                                        ents.begin(), ents.end(),
                                        [&](const Entity& e) { return e.layer == l.name; });
                                    if (!used) { ++removedLayers; return true; }
                                    return false;
                                }),
                 layers.end());

    if (removedEnts == 0 && removedLayers == 0) {
        undo();
        emit message(QStringLiteral("Nothing to purge — no empty layers or stray objects"));
        return;
    }
    selected_.clear();
    doc_->setDirty(true);
    emit selectionChanged();
    emit message(QStringLiteral("Purged %1 empty layer(s) and %2 stray object(s)")
                     .arg(removedLayers)
                     .arg(removedEnts));
    emit changed();
}

int Editor::tableCellAt(const Entity& e, const Vec2& p, Vec2& cellTopLeft, double& cw,
                        double& ch) const {
    if (!e.meta.value("table").toBool()) return -1;
    const Bounds b = boundsOf(e.points);
    if (!b.valid || p.x < b.min.x || p.x > b.max.x || p.y < b.min.y || p.y > b.max.y) return -1;
    const QJsonArray cf = e.meta.value("colFrac").toArray();
    const QJsonArray rf = e.meta.value("rowFrac").toArray();
    const int cols = cf.size(), rows = rf.size();
    if (cols == 0 || rows == 0) return -1;
    // Columns run left->right (x increasing).
    int col = cols - 1;
    double xL = b.min.x, wcol = cf[cols - 1].toDouble() * b.width();
    { double x = b.min.x;
      for (int c = 0; c < cols; ++c) {
          const double w = cf[c].toDouble() * b.width();
          if (p.x <= x + w + 1e-9) { col = c; xL = x; wcol = w; break; }
          x += w;
      } }
    // Rows run top->down (y decreasing, since world y is up).
    int row = rows - 1;
    double yT = b.max.y, hrow = rf[rows - 1].toDouble() * b.height();
    { double top = b.max.y;
      for (int r = 0; r < rows; ++r) {
          const double h = rf[r].toDouble() * b.height();
          if (p.y >= top - h - 1e-9) { row = r; yT = top; hrow = h; break; }
          top -= h;
      } }
    // The merged Title row (row 0 when present) is a single cell spanning all columns.
    if (e.meta.value("titleRow").toBool() && row == 0) {
        cellTopLeft = {b.min.x, b.max.y};
        cw = b.width();
        ch = hrow;
        return 0;
    }
    cellTopLeft = {xL, yT};
    cw = wcol;
    ch = hrow;
    return row * cols + col;
}

void Editor::applyTableCellEdit(const QString& id, int cell, const QString& text) {
    if (Entity* e = entityById(id); e && e->meta.value("table").toBool()) {
        pushUndo();
        QJsonArray cells = e->meta.value("cells").toArray();
        while (cells.size() <= cell) cells.append(QString());
        cells[cell] = text;
        e->meta.insert(QStringLiteral("cells"), cells);
        doc_->setDirty(true);
        emit changed();
    }
}

void Editor::setTableCellTextHeight(const QString& tableId, int cell, double height) {
    Entity* e = entityById(tableId);
    if (!e || !e->meta.value("table").toBool() || cell < 0 || height <= 0.0) return;
    const int cols = std::max(1, e->meta.value("cols").toInt(1));
    const int rows = std::max(1, e->meta.value("rows").toInt(1));
    if (cell >= rows * cols) return;
    const int row = cell / cols, col = cell % cols;
    pushUndo();
    // The height is the cell's own, so one cell can read larger than the rest of its table.
    QJsonArray sizes = e->meta.value("cellTextSize").toArray();
    while (sizes.size() < rows * cols) sizes.append(0.0);
    sizes[cell] = height;
    e->meta.insert(QStringLiteral("cellTextSize"), sizes);

    // What the text now needs: a row at least as tall as the tallest text in it, and a column wide
    // enough for the longest text across it (the same character-count estimate the rest of the
    // drawing uses for a proportional font).
    const QJsonArray cells = e->meta.value("cells").toArray();
    auto textHeightAt = [&](int idx) {
        const double own = idx < sizes.size() ? sizes[idx].toDouble() : 0.0;
        return own > 0.0 ? own : e->meta.value("textSize").toDouble(4.0);
    };
    double needH = 0.0, needW = 0.0;
    for (int c = 0; c < cols; ++c) {
        const int idx = row * cols + c;
        needH = std::max(needH, textHeightAt(idx) * 1.6);
    }
    for (int r = 0; r < rows; ++r) {
        const int idx = r * cols + col;
        const QString text = idx < cells.size() ? cells[idx].toString() : QString();
        needW = std::max(needW, textHeightAt(idx) * (1.6 + 0.62 * text.size()));
    }

    const Bounds b = boundsOf(e->points);
    if (!b.valid) return;
    QJsonArray rf = e->meta.value("rowFrac").toArray();
    QJsonArray cf = e->meta.value("colFrac").toArray();
    if (rf.size() != rows || cf.size() != cols) return;
    // Grow the table by whatever that row / column is short of, and keep every OTHER row and
    // column the size it already was - so widening one column does not squeeze the rest.
    std::vector<double> rh, cw;
    for (int r = 0; r < rows; ++r) rh.push_back(rf[r].toDouble() * b.height());
    for (int c = 0; c < cols; ++c) cw.push_back(cf[c].toDouble() * b.width());
    bool grew = false;
    if (rh[static_cast<size_t>(row)] < needH) { rh[static_cast<size_t>(row)] = needH; grew = true; }
    if (cw[static_cast<size_t>(col)] < needW) { cw[static_cast<size_t>(col)] = needW; grew = true; }
    double totalH = 0.0, totalW = 0.0;
    for (double v : rh) totalH += v;
    for (double v : cw) totalW += v;
    QJsonArray nrf, ncf;
    for (double v : rh) nrf.append(v / totalH);
    for (double v : cw) ncf.append(v / totalW);
    e->meta.insert(QStringLiteral("rowFrac"), nrf);
    e->meta.insert(QStringLiteral("colFrac"), ncf);
    if (grew) {
        // The table is anchored at its top-left, so it grows right and down.
        const double x0 = b.min.x, y1 = b.max.y;
        e->points = {{x0, y1 - totalH}, {x0 + totalW, y1 - totalH}, {x0 + totalW, y1}, {x0, y1}};
        e->a = {x0, y1};
    }
    doc_->setDirty(true);
    emit changed();
    emit message(grew ? QStringLiteral("Cell text %1 — the row and column grew to hold it")
                            .arg(height, 0, 'f', 2)
                      : QStringLiteral("Cell text %1").arg(height, 0, 'f', 2));
}

void Editor::setCellSelection(const QString& id, int cell) {
    cellTableId_ = id;
    cellIndex_ = cell;
    cellSel_.clear();
    if (cell >= 0) cellSel_.push_back(cell);
    emit cellSelectionChanged();
}

void Editor::clearCellSelection() {
    cellSel_.clear();
    if (cellTableId_.isEmpty() && cellIndex_ < 0) return;
    cellTableId_.clear();
    cellIndex_ = -1;
    emit cellSelectionChanged();
}

namespace {
// Rebuild a table's rect + row/col fractions from absolute row heights / col widths,
// anchored at its top-left corner.
void rebuildTable(Entity& e, const std::vector<double>& h, const std::vector<double>& w) {
    const Bounds b = boundsOf(e.points);
    double totalH = 0, totalW = 0;
    for (double v : h) totalH += v;
    for (double v : w) totalW += v;
    const double minX = b.min.x, maxY = b.max.y;
    const double maxX = minX + totalW, minY = maxY - totalH;
    e.points = {{minX, minY}, {maxX, minY}, {maxX, maxY}, {minX, maxY}};
    e.a = {minX, maxY};
    QJsonArray rf, cf;
    for (double v : h) rf.append(totalH > 0 ? v / totalH : 0.0);
    for (double v : w) cf.append(totalW > 0 ? v / totalW : 0.0);
    e.meta.insert("rowFrac", rf);
    e.meta.insert("colFrac", cf);
}
} // namespace

void Editor::tableInsertRow(bool below) {
    Entity* e = entityById(cellTableId_);
    if (!e || !e->meta.value("table").toBool() || cellIndex_ < 0) return;
    const int cols = e->meta.value("cols").toInt();
    int rows = e->meta.value("rows").toInt();
    if (cols <= 0 || rows <= 0) return;
    const Bounds b = boundsOf(e->points);
    const QJsonArray rf = e->meta.value("rowFrac").toArray();
    const QJsonArray cf = e->meta.value("colFrac").toArray();
    std::vector<double> h(rows), w(cols);
    for (int i = 0; i < rows; ++i) h[i] = (i < rf.size() ? rf[i].toDouble() : 1.0 / rows) * b.height();
    for (int c = 0; c < cols; ++c) w[c] = (c < cf.size() ? cf[c].toDouble() : 1.0 / cols) * b.width();
    const int r = cellIndex_ / cols;
    const int at = std::clamp(below ? r + 1 : r, 0, rows);
    pushUndo();
    h.insert(h.begin() + at, h[std::clamp(r, 0, rows - 1)]); // new row matches the current row
    const QJsonArray cells = e->meta.value("cells").toArray();
    const QJsonArray align = e->meta.value("cellAlign").toArray();
    QJsonArray ncells, nalign;
    auto row = [&](int rr) {
        for (int c = 0; c < cols; ++c) {
            const int idx = rr * cols + c;
            ncells.append(idx < cells.size() ? cells[idx] : QJsonValue(QString()));
            nalign.append(idx < align.size() ? align[idx] : QJsonValue(QString()));
        }
    };
    auto empty = [&] { for (int c = 0; c < cols; ++c) { ncells.append(QString()); nalign.append(QString()); } };
    for (int rr = 0; rr < rows; ++rr) { if (rr == at) empty(); row(rr); }
    if (at >= rows) empty();
    e->meta.insert("rows", rows + 1);
    e->meta.insert("cells", ncells);
    e->meta.insert("cellAlign", nalign);
    rebuildTable(*e, h, w);
    if (at <= r) cellIndex_ += cols; // keep pointing at the same cell
    doc_->setDirty(true);
    emit cellSelectionChanged();
    emit changed();
    emit message(QStringLiteral("Row inserted"));
}

void Editor::tableDeleteRow() {
    Entity* e = entityById(cellTableId_);
    if (!e || !e->meta.value("table").toBool() || cellIndex_ < 0) return;
    const int cols = e->meta.value("cols").toInt();
    int rows = e->meta.value("rows").toInt();
    if (cols <= 0 || rows <= 1) { emit message(QStringLiteral("A table keeps at least one row.")); return; }
    const Bounds b = boundsOf(e->points);
    const QJsonArray rf = e->meta.value("rowFrac").toArray();
    const QJsonArray cf = e->meta.value("colFrac").toArray();
    std::vector<double> h(rows), w(cols);
    for (int i = 0; i < rows; ++i) h[i] = (i < rf.size() ? rf[i].toDouble() : 1.0 / rows) * b.height();
    for (int c = 0; c < cols; ++c) w[c] = (c < cf.size() ? cf[c].toDouble() : 1.0 / cols) * b.width();
    const int r = cellIndex_ / cols;
    pushUndo();
    h.erase(h.begin() + r);
    const QJsonArray cells = e->meta.value("cells").toArray();
    const QJsonArray align = e->meta.value("cellAlign").toArray();
    QJsonArray ncells, nalign;
    for (int rr = 0; rr < rows; ++rr) {
        if (rr == r) continue;
        for (int c = 0; c < cols; ++c) {
            const int idx = rr * cols + c;
            ncells.append(idx < cells.size() ? cells[idx] : QJsonValue(QString()));
            nalign.append(idx < align.size() ? align[idx] : QJsonValue(QString()));
        }
    }
    e->meta.insert("rows", rows - 1);
    e->meta.insert("cells", ncells);
    e->meta.insert("cellAlign", nalign);
    rebuildTable(*e, h, w);
    cellIndex_ = std::min(cellIndex_, (rows - 1) * cols - 1);
    if (cellIndex_ < 0) clearCellSelection(); else emit cellSelectionChanged();
    doc_->setDirty(true);
    emit changed();
    emit message(QStringLiteral("Row deleted"));
}

void Editor::tableInsertColumn(bool right) {
    Entity* e = entityById(cellTableId_);
    if (!e || !e->meta.value("table").toBool() || cellIndex_ < 0) return;
    const int cols = e->meta.value("cols").toInt();
    const int rows = e->meta.value("rows").toInt();
    if (cols <= 0 || rows <= 0) return;
    const Bounds b = boundsOf(e->points);
    const QJsonArray rf = e->meta.value("rowFrac").toArray();
    const QJsonArray cf = e->meta.value("colFrac").toArray();
    std::vector<double> h(rows), w(cols);
    for (int i = 0; i < rows; ++i) h[i] = (i < rf.size() ? rf[i].toDouble() : 1.0 / rows) * b.height();
    for (int c = 0; c < cols; ++c) w[c] = (c < cf.size() ? cf[c].toDouble() : 1.0 / cols) * b.width();
    const int col = cellIndex_ % cols;
    const int at = std::clamp(right ? col + 1 : col, 0, cols);
    pushUndo();
    w.insert(w.begin() + at, w[std::clamp(col, 0, cols - 1)]);
    const QJsonArray cells = e->meta.value("cells").toArray();
    const QJsonArray align = e->meta.value("cellAlign").toArray();
    QJsonArray ncells, nalign;
    for (int rr = 0; rr < rows; ++rr)
        for (int c = 0; c <= cols; ++c) {
            if (c == at) { ncells.append(QString()); nalign.append(QString()); }
            if (c < cols) {
                const int idx = rr * cols + c;
                ncells.append(idx < cells.size() ? cells[idx] : QJsonValue(QString()));
                nalign.append(idx < align.size() ? align[idx] : QJsonValue(QString()));
            }
        }
    e->meta.insert("cols", cols + 1);
    e->meta.insert("cells", ncells);
    e->meta.insert("cellAlign", nalign);
    rebuildTable(*e, h, w);
    const int r = cellIndex_ / cols;
    cellIndex_ = r * (cols + 1) + col + (at <= col ? 1 : 0);
    doc_->setDirty(true);
    emit cellSelectionChanged();
    emit changed();
    emit message(QStringLiteral("Column inserted"));
}

void Editor::tableDeleteColumn() {
    Entity* e = entityById(cellTableId_);
    if (!e || !e->meta.value("table").toBool() || cellIndex_ < 0) return;
    const int cols = e->meta.value("cols").toInt();
    const int rows = e->meta.value("rows").toInt();
    if (cols <= 1 || rows <= 0) { emit message(QStringLiteral("A table keeps at least one column.")); return; }
    const Bounds b = boundsOf(e->points);
    const QJsonArray rf = e->meta.value("rowFrac").toArray();
    const QJsonArray cf = e->meta.value("colFrac").toArray();
    std::vector<double> h(rows), w(cols);
    for (int i = 0; i < rows; ++i) h[i] = (i < rf.size() ? rf[i].toDouble() : 1.0 / rows) * b.height();
    for (int c = 0; c < cols; ++c) w[c] = (c < cf.size() ? cf[c].toDouble() : 1.0 / cols) * b.width();
    const int col = cellIndex_ % cols;
    pushUndo();
    w.erase(w.begin() + col);
    const QJsonArray cells = e->meta.value("cells").toArray();
    const QJsonArray align = e->meta.value("cellAlign").toArray();
    QJsonArray ncells, nalign;
    for (int rr = 0; rr < rows; ++rr)
        for (int c = 0; c < cols; ++c) {
            if (c == col) continue;
            const int idx = rr * cols + c;
            ncells.append(idx < cells.size() ? cells[idx] : QJsonValue(QString()));
            nalign.append(idx < align.size() ? align[idx] : QJsonValue(QString()));
        }
    e->meta.insert("cols", cols - 1);
    e->meta.insert("cells", ncells);
    e->meta.insert("cellAlign", nalign);
    rebuildTable(*e, h, w);
    const int r = cellIndex_ / cols;
    cellIndex_ = r * (cols - 1) + std::min(col, cols - 2);
    doc_->setDirty(true);
    emit cellSelectionChanged();
    emit changed();
    emit message(QStringLiteral("Column deleted"));
}

void Editor::setCellAlignment(const QString& align) {
    Entity* e = entityById(cellTableId_);
    if (!e || !e->meta.value("table").toBool() || cellIndex_ < 0) return;
    const int total = e->meta.value("rows").toInt() * e->meta.value("cols").toInt();
    pushUndo();
    QJsonArray a = e->meta.value("cellAlign").toArray();
    while (a.size() < total) a.append(QString());
    // Every cell picked, not just the one clicked: a zone over a block of them is how a column or
    // a corner of a table is aligned in one go.
    std::vector<int> which = cellSel_;
    if (which.empty()) which.push_back(cellIndex_);
    int n = 0;
    for (int idx : which)
        if (idx >= 0 && idx < a.size()) { a[idx] = align; ++n; }
    e->meta.insert("cellAlign", a);
    doc_->setDirty(true);
    emit changed();
    emit message(n > 1 ? QStringLiteral("Cell alignment: %1 (%2 cells)").arg(align).arg(n)
                       : QStringLiteral("Cell alignment: %1").arg(align));
}

bool Editor::selectCellsInRegion(const QString& tableId, const std::vector<Vec2>& region) {
    Entity* e = entityById(tableId);
    if (!e || !e->meta.value("table").toBool() || region.size() < 3) return false;
    const Bounds b = boundsOf(e->points);
    if (!b.valid) return false;
    const QJsonArray cf = e->meta.value("colFrac").toArray();
    const QJsonArray rf = e->meta.value("rowFrac").toArray();
    const int cols = cf.size(), rows = rf.size();
    if (cols == 0 || rows == 0) return false;
    // A cell is picked when its own middle is inside the zone: dragging across a block of them
    // takes exactly the block, and a zone that merely grazes an edge takes nothing.
    std::vector<int> picked;
    double top = b.max.y;
    for (int r = 0; r < rows; ++r) {
        const double h = rf[r].toDouble() * b.height();
        double x = b.min.x;
        for (int c = 0; c < cols; ++c) {
            const double w = cf[c].toDouble() * b.width();
            const Vec2 mid{x + w / 2.0, top - h / 2.0};
            if (pointInPolygon(region, mid)) picked.push_back(r * cols + c);
            x += w;
        }
        top -= h;
    }
    if (picked.empty()) return false;
    cellTableId_ = tableId;
    cellSel_ = picked;
    cellIndex_ = picked.front();
    selected_ = {tableId}; // the table is what the ribbon's cell tab hangs off
    emit cellSelectionChanged();
    emit selectionChanged();
    emit changed();
    emit message(QStringLiteral("%1 cell(s) picked").arg(picked.size()));
    return true;
}

void Editor::beginBalloonWithText(const QString& text) {
    balloonTextNext_ = text;
    setTool(ToolId::Balloon);
    emit message(QStringLiteral(
                     "Balloon %1: click what it points at, then where the label sits").arg(text));
}

void Editor::addCoupledCircle(const QString& id) {
    Entity* e = entityById(id);
    if (!e || !e->meta.value("balloon").toBool()) return;
    pushUndo();
    QString chain = e->meta.value("chain").toString();
    if (chain.isEmpty()) { chain = uid(); e->meta.insert("chain", chain); }
    std::vector<Entity*> batch = chainMatesOf(*e);
    batch.push_back(e);
    Entity* arrow = nullptr; // the batch member that carries the leader
    for (Entity* m : batch)
        if (!balloonLeaders(*m).empty()) { arrow = m; break; }
    if (!arrow) arrow = e;
    const double r = e->radius;
    double minX = 1e18, maxX = -1e18;
    for (Entity* m : batch) {
        minX = std::min(minX, m->center.x);
        maxX = std::max(maxX, m->center.x);
    }
    // Leader exits to the right -> grow the row left (opposite the arrow), else right.
    const double newX = (balloonLandingSide(*arrow) >= 0) ? (minX - r * 2.0) : (maxX + r * 2.0);
    Entity nb;
    nb.id = uid();
    nb.type = EntityType::Circle;
    nb.layer = e->layer;
    nb.color = e->color;
    nb.radius = r;
    nb.center = {newX, arrow->center.y}; // same row as the arrow circle
    nb.a = nb.center;
    int next = 0;
    for (const auto& x : doc_->entities())
        if (x.meta.value("balloon").toBool())
            next = std::max(next, x.meta.value("text").toString().toInt());
    nb.meta.insert("balloon", true);
    nb.meta.insert("text", QString::number(next + 1));
    nb.meta.insert("chain", chain); // coupled — no leader of its own
    const QString newId = nb.id;
    addEntity(std::move(nb));
    selected_ = {newId};
    emit selectionChanged();
    doc_->setDirty(true);
    emit changed();
    emit message(QStringLiteral("Circle added opposite the leader (batch moves together)"));
}

void Editor::addLeaderToSelection() {
    // Prefer the batch's arrow circle (the one that already carries leaders).
    Entity* b = nullptr;
    for (Entity* e : selectedEntities())
        if (e->meta.value("balloon").toBool() && !balloonLeaders(*e).empty()) { b = e; break; }
    if (!b)
        for (Entity* e : selectedEntities())
            if (e->meta.value("balloon").toBool()) { b = e; break; }
    if (!b) { emit message(QStringLiteral("Select a balloon, then Add Leader.")); return; }
    pushUndo();
    auto tips = balloonLeaders(*b);
    // New arrow drops below the balloon (drag its tip to aim it).
    tips.push_back({b->center.x, b->center.y - b->radius - 30.0 - 10.0 * tips.size()});
    setBalloonLeaders(*b, tips);
    doc_->setDirty(true);
    emit changed();
    emit message(QStringLiteral("Leader added — drag its arrow to position"));
}

void Editor::removeLeaderFromSelection() {
    Entity* b = nullptr;
    for (Entity* e : selectedEntities())
        if (e->meta.value("balloon").toBool() && !balloonLeaders(*e).empty()) { b = e; break; }
    if (!b) { emit message(QStringLiteral("Select a balloon with a leader first.")); return; }
    auto tips = balloonLeaders(*b);
    if (tips.empty()) return;
    pushUndo();
    tips.pop_back();
    setBalloonLeaders(*b, tips);
    if (tips.empty()) b->meta.remove("landingSide");
    doc_->setDirty(true);
    emit changed();
    emit message(QStringLiteral("Leader removed"));
}

void Editor::alignBalloons() {
    std::vector<Entity*> bs;
    for (Entity* e : selectedEntities())
        if (e->meta.value("balloon").toBool()) bs.push_back(e);
    if (bs.size() < 2) { emit message(QStringLiteral("Select two or more balloons to align.")); return; }
    // Line them up on one axis (whichever they're more spread along) at the average
    // of the other coordinate, evenly spaced between the extremes, keeping order.
    double minX = 1e18, maxX = -1e18, minY = 1e18, maxY = -1e18, sumX = 0, sumY = 0;
    for (Entity* e : bs) {
        minX = std::min(minX, e->center.x); maxX = std::max(maxX, e->center.x);
        minY = std::min(minY, e->center.y); maxY = std::max(maxY, e->center.y);
        sumX += e->center.x; sumY += e->center.y;
    }
    const bool horiz = (maxX - minX) >= (maxY - minY);
    std::sort(bs.begin(), bs.end(), [&](const Entity* a, const Entity* b) {
        return horiz ? a->center.x < b->center.x : a->center.y < b->center.y;
    });
    pushUndo();
    const int n = static_cast<int>(bs.size());
    for (int i = 0; i < n; ++i) {
        Vec2 c;
        if (horiz) c = {minX + (maxX - minX) * i / (n - 1), sumY / n};
        else c = {sumX / n, minY + (maxY - minY) * i / (n - 1)};
        translateEntity(*bs[i], c - bs[i]->center);
    }
    doc_->setDirty(true);
    emit changed();
    emit message(QStringLiteral("Aligned %1 balloons").arg(n));
}

void Editor::collectBalloons() {
    std::vector<Entity*> bs;
    for (Entity* e : selectedEntities())
        if (e->meta.value("balloon").toBool()) bs.push_back(e);
    if (bs.size() < 2) { emit message(QStringLiteral("Select two or more balloons to collect.")); return; }
    // Pack them into a touching horizontal row starting at the left-most balloon.
    std::sort(bs.begin(), bs.end(),
              [](const Entity* a, const Entity* b) { return a->center.x < b->center.x; });
    pushUndo();
    const Vec2 start = bs.front()->center;
    double x = start.x;
    for (Entity* e : bs) {
        translateEntity(*e, Vec2{x, start.y} - e->center);
        x += e->radius * 2.0; // adjacent, touching
    }
    doc_->setDirty(true);
    emit changed();
    emit message(QStringLiteral("Collected %1 balloons into a row").arg(bs.size()));
}

void Editor::orderBalloonRow(const QString& id) {
    Entity* e = entityById(id);
    if (!e || !e->meta.value("balloon").toBool()) return;
    std::vector<Entity*> batch = chainMatesOf(*e);
    batch.push_back(e);
    if (batch.size() < 2) {
        emit message(QStringLiteral("This balloon isn't part of a batch."));
        return;
    }
    Entity* arrow = nullptr;
    for (Entity* m : batch)
        if (!balloonLeaders(*m).empty()) { arrow = m; break; }
    if (!arrow) arrow = e;
    // Numeric order, but keep the arrow circle at the leader end of the row.
    std::sort(batch.begin(), batch.end(), [](const Entity* a, const Entity* b) {
        return a->meta.value("text").toString().toInt() < b->meta.value("text").toString().toInt();
    });
    std::vector<Entity*> order;
    order.push_back(arrow);
    for (Entity* m : batch)
        if (m != arrow) order.push_back(m);
    pushUndo();
    const int dir = -balloonLandingSide(*arrow); // row grows OPPOSITE the leader
    const double y = arrow->center.y, x0 = arrow->center.x, r = arrow->radius;
    for (size_t i = 0; i < order.size(); ++i) {
        const Vec2 c{x0 + dir * 2.0 * r * static_cast<double>(i), y}; // touching row
        order[i]->center = c;
        order[i]->a = c;
    }
    doc_->setDirty(true);
    emit changed();
    emit message(QStringLiteral("Balloons ordered into a row (minimum area)"));
}

void Editor::reorderBalloonMinimize(const QString& id) {
    Entity* e = entityById(id);
    if (!e || !e->meta.value("balloon").toBool()) return;
    std::vector<Entity*> batch = chainMatesOf(*e);
    batch.push_back(e);
    if (batch.size() < 2) {
        emit message(QStringLiteral("This balloon isn't part of a batch."));
        return;
    }
    Entity* arrow = nullptr;
    for (Entity* m : batch)
        if (!balloonLeaders(*m).empty()) { arrow = m; break; }
    if (!arrow) arrow = e;
    std::sort(batch.begin(), batch.end(), [](const Entity* a, const Entity* b) {
        return a->meta.value("text").toString().toInt() < b->meta.value("text").toString().toInt();
    });
    std::vector<Entity*> order;
    order.push_back(arrow); // arrow at the leader end of the top row
    for (Entity* m : batch)
        if (m != arrow) order.push_back(m);
    pushUndo();
    const int N = static_cast<int>(order.size());
    const double r = arrow->radius;
    const double rowH = r * std::sqrt(3.0); // hex row spacing => adjacent rows tangent
    const Vec2 anchor = arrow->center;
    // A downward-narrowing triangular hex pack (widest row on top): each lower row
    // nestles in the POCKETS of the row above, so no two circles ever overlap.
    const int base = std::max(1, static_cast<int>(std::ceil((std::sqrt(8.0 * N + 1) - 1) / 2.0)));
    int placed = 0, row = 0;
    while (placed < N) {
        const int nominal = std::max(1, base - row);       // full width of this row
        const int inRow = std::min(nominal, N - placed);
        const double x0 = anchor.x - (nominal - 1) * r;    // pockets aligned to the full row
        for (int i = 0; i < inRow; ++i) {
            const Vec2 c{x0 + i * 2.0 * r, anchor.y - row * rowH};
            order[placed]->center = c;
            order[placed]->a = c;
            ++placed;
        }
        ++row;
    }
    // Move the leader to the circle now closest to where it points (its arrow tip),
    // so it connects from the corner of the cluster facing the harness.
    const auto tips = balloonLeaders(*arrow);
    if (!tips.empty()) {
        Vec2 avg{0, 0};
        for (const auto& t : tips) avg = avg + t;
        avg = avg * (1.0 / tips.size());
        Entity* best = arrow;
        double bd = distance(arrow->center, avg);
        for (Entity* m : order) {
            const double d = distance(m->center, avg);
            if (d < bd) { bd = d; best = m; }
        }
        if (best != arrow) {
            setBalloonLeaders(*best, tips);
            best->meta.insert("landing", arrow->meta.value("landing").toDouble(8.0));
            arrow->meta.remove("leaders");
            arrow->meta.remove("landingSide");
            arrow->meta.remove("landingAngle");
        }
    }
    doc_->setDirty(true);
    emit changed();
    emit message(QStringLiteral("Balloons packed to minimum area"));
}

// ---- Harness placement -----------------------------------------------------

void Editor::handlePlacementPress(const Vec2& p) {
    Entity e;
    e.layer = doc_->activeLayer();
    switch (tool_) {
    case ToolId::Connector:
        e.type = EntityType::Connector;
        e.a = p;
        writeConnectorSpec(e.meta, connectorSpec_);
        e.meta.insert("entNum", assignEntityNumber()); // unique entity number
        break;
    case ToolId::Splice:
        e.type = EntityType::Splice;
        e.layer = QStringLiteral("Wires");
        e.a = p;
        e.meta.insert("size", spliceSize_); // dot radius (world units)
        break;
    case ToolId::NotConnected: {
        // Shift + click on a connector pin: terminate EVERY still-unconnected pin of that
        // connector at once (pins already wired or already X'd are skipped).
        if (pressMods_ & Qt::ShiftModifier) {
            std::vector<Vec2> pins;
            int anchor = -1;
            if (connectorPinsAt(p, pins, anchor)) {
                const double tol = std::max(worldTolerance(), 1e-3);
                auto occupied = [&](const Vec2& pe) {
                    for (const auto& w : doc_->entities()) {
                        if (w.type == EntityType::Wire && !w.points.empty()) {
                            if (distance(w.points.front(), pe) <= tol ||
                                distance(w.points.back(), pe) <= tol)
                                return true;
                        } else if (w.type == EntityType::NotConnected &&
                                   distance(w.a, pe) <= tol) {
                            return true;
                        }
                    }
                    return false;
                };
                pushUndo();
                int n = 0;
                for (const Vec2& pe : pins) {
                    if (occupied(pe)) continue;
                    Entity x;
                    x.type = EntityType::NotConnected;
                    x.layer = e.layer;
                    x.a = pe;
                    x.meta.insert("ncSize", ncSize_);
                    addEntity(std::move(x));
                    ++n;
                }
                doc_->setDirty(true);
                emit changed();
                emit message(QStringLiteral("Terminated %1 unconnected pin(s)").arg(n));
                return;
            }
        }
        e.type = EntityType::NotConnected;
        e.a = p;
        e.meta.insert("ncSize", ncSize_); // X half-size (world units)
        break;
    }
    case ToolId::NetLabel: {
        // A Wire label placed ON a wire ATTACHES to it: the label is stored as a
        // position along that wire (so it follows the wire when the wire is edited or
        // moved) and the wire's own "Wire label" property is set to the same text —
        // the label on the drawing and the property are one and the same thing.
        e.type = EntityType::NetLabel;
        e.a = p;
        e.layer = QStringLiteral("Wires");
        const QString wid = wireAt(p);
        QString text = wireNet_;
        if (!wid.isEmpty()) {
            if (const Entity* w = entityById(wid)) {
                // Keep the wire's existing label if it has one; otherwise the ribbon's
                // net/label text seeds it.
                const QString existing = w->meta.value(QStringLiteral("wireLabel")).toString();
                if (!existing.isEmpty()) text = existing;
                e.meta.insert("wireId", wid);
                e.meta.insert("annT", wireFractionNearest(*w, p));
            }
        }
        if (text.isEmpty()) text = QStringLiteral("NET");
        e.meta.insert("text", text);
        // Write the label onto the wire so its properties show it straight away.
        if (!wid.isEmpty())
            if (Entity* w = entityById(wid)) w->meta.insert(QStringLiteral("wireLabel"), text);
        break;
    }
    case ToolId::Table: {
        // Insert Table at the picked insertion point (top-left). Built from the spec
        // (Insert Table dialog): a Title row (merged), a Header row, then data rows.
        // Stored as a Rect + per-row/col FRACTIONS so the grid scales with the rect.
        const TableSpec& s = tableSpec_;
        const int cols = std::max(1, s.cols);
        const int dataRows = std::max(1, s.dataRows);
        const int rows = (s.title ? 1 : 0) + (s.header ? 1 : 0) + dataRows;
        const double W = cols * s.colWidth, H = rows * s.rowHeight;
        const double minX = p.x, maxX = p.x + W, maxY = p.y, minY = p.y - H;
        e.type = EntityType::Rect;
        e.points = {{minX, minY}, {maxX, minY}, {maxX, maxY}, {minX, maxY}};
        e.a = {minX, maxY};
        QJsonArray colFrac, rowFrac, cells;
        for (int i = 0; i < cols; ++i) colFrac.append(1.0 / cols);
        for (int i = 0; i < rows; ++i) rowFrac.append(1.0 / rows);
        for (int i = 0; i < rows * cols; ++i) cells.append(QString());
        e.meta.insert("table", true);
        e.meta.insert("rows", rows);
        e.meta.insert("cols", cols);
        e.meta.insert("colFrac", colFrac);
        e.meta.insert("rowFrac", rowFrac);
        e.meta.insert("cells", cells);
        e.meta.insert("titleRow", s.title);
        e.meta.insert("headerRow", s.header);
        e.meta.insert("textSize", std::max(2.0, s.rowHeight * 0.5));
        break;
    }
    default:
        return;
    }
    pushUndo();
    // A non-associative connector is placed as loose primitives (lines + text), not a
    // re-editable Connector object — "explode on drop".
    if (tool_ == ToolId::Connector && !connectorSpec_.associative) {
        auto parts = explodeEntity(e);
        for (Entity& part : parts) addEntity(std::move(part));
        emit message(QStringLiteral("Connector placed (exploded to primitives)"));
    } else {
        addEntity(std::move(e));
        emit message(QStringLiteral("%1 placed").arg(toolLabel(tool_)));
    }
    emit changed();
    // The X (not-connected) terminator keeps placing until Esc, so all of a connector's
    // unused pins can be X'd in a row. So does the break mark: a layout that is drawn short in one
    // place is usually drawn short in several, and going back to the ribbon between each one is worse
    // than pressing Esc at the end. Every other placement tool returns to Select.
    if (tool_ == ToolId::NotConnected)
        emit message(QStringLiteral("X placed — click to terminate more pins; Esc to finish"));
    else
        setTool(ToolId::Select); // return to Select after placing (Space repeats)
}

QString Editor::selectedConnectorId() const {
    if (!doc_ || selected_.size() != 1) return {};
    for (const auto& e : doc_->entities())
        if (e.id == selected_.front() && e.type == EntityType::Connector) return e.id;
    return {};
}

void Editor::applyConnectorSpecToSelection(const ConnectorSpec& spec) {
    const QString id = selectedConnectorId();
    if (id.isEmpty()) { connectorSpec_ = spec; return; }
    pushUndo();
    if (Entity* e = entityById(id)) writeConnectorSpec(e->meta, spec);
    connectorSpec_ = spec;
    doc_->setDirty(true);
    emit changed();
}

void Editor::stampHarnessIds(std::vector<Entity>& ents, const HarnessDef& def) {
    // The generator refers to wires by their DEFINITION id, which is not an entity id, so map
    // definition id -> real entity id as the wires are stamped and then re-point every
    // shield/twist "wires" list at the entities. Without this the annotations would name ids
    // that do not exist and would not follow their wires.
    QHash<QString, QString> wireIdMap;
    for (Entity& e : ents) {
        if (e.type != EntityType::Wire) continue;
        e.id = uid();
        // A shield's DRAIN is a wire too, but it is not one of the definition's wires - it has
        // no definition id to map, and letting it take one would re-point a shield or a twist
        // at the drain instead of at the conductor it catches.
        if (e.meta.contains(QStringLiteral("shieldBond")) ||
            e.meta.contains(QStringLiteral("shieldLeg")))
            continue;
        // The generator stamps the row it drew onto every run it drew, so there is nothing to
        // guess: the run that says "w5" IS w5. Runs with no row of their own - a drain, a cable
        // pigtail - carry no id and are left out, which is what keeps a twist from catching them.
        const QString def_id = e.meta.value(QStringLiteral("defWire")).toString();
        if (!def_id.isEmpty()) {
            if (!wireIdMap.contains(def_id)) wireIdMap.insert(def_id, e.id);
            continue;
        }
        // Older definitions (and anything generated before the id was stamped) fall back to
        // matching on what the row looks like.
        const QString label = e.meta.value(QStringLiteral("wireLabel")).toString();
        QString match;
        for (const auto& dw : def.wires)
            if (!wireIdMap.contains(dw.id) && dw.label == label &&
                dw.color == e.meta.value(QStringLiteral("wireColor")).toString() &&
                dw.gauge == e.meta.value(QStringLiteral("wireGauge")).toString()) {
                match = dw.id;
                break;
            }
        if (match.isEmpty())
            for (const auto& dw : def.wires)
                if (!wireIdMap.contains(dw.id)) { match = dw.id; break; }
        if (!match.isEmpty()) wireIdMap.insert(match, e.id);
    }
    for (Entity& e : ents) {
        if (e.type != EntityType::Screen && e.type != EntityType::Twist) continue;
        QJsonArray mapped;
        for (const auto& v : e.meta.value(QStringLiteral("wires")).toArray()) {
            const QString real = wireIdMap.value(v.toString());
            if (!real.isEmpty()) mapped.append(real);
        }
        e.meta.insert(QStringLiteral("wires"), mapped);
    }
    // Join each screen to the drain that terminates it, now that both have real ids: the X
    // marker goes, and sliding the shield afterwards pulls the drain along with it.
    for (Entity& sc : ents) {
        if (sc.type != EntityType::Screen) continue;
        const QString key = sc.meta.value(QStringLiteral("overallShield")).toString();
        if (key.isEmpty() || sc.points.empty()) continue;
        const int k = std::min(sc.meta.value(QStringLiteral("catch")).toInt(
                                   static_cast<int>(sc.points.size())),
                               static_cast<int>(sc.points.size()));
        if (static_cast<int>(sc.points.size()) <= k) continue;
        const Vec2 leg = sc.points.back();
        for (const Entity& dw : ents) {
            if (dw.type != EntityType::Wire || dw.points.size() < 2) continue;
            const bool isBond = dw.meta.value(QStringLiteral("shieldBond")).toString() == key;
            // A wire the definition ran to this screen lands on the same leg; either one
            // terminates it.
            const bool isLeg = dw.meta.contains(QStringLiteral("shieldLeg")) &&
                               (distance(dw.points.front(), leg) < 1e-6 ||
                                distance(dw.points.back(), leg) < 1e-6);
            if (!isBond && !isLeg) continue;
            const int end = (distance(dw.points.front(), leg) <= distance(dw.points.back(), leg))
                                ? 0
                                : 1;
            if (distance(end == 0 ? dw.points.front() : dw.points.back(), leg) > 1e-6) continue;
            sc.meta.insert(QStringLiteral("legWireId"), dw.id);
            sc.meta.insert(QStringLiteral("legWireEnd"), end);
            sc.meta.insert(QStringLiteral("notConnected"), false);
            break;
        }
    }
    for (Entity& e : ents)
        if (e.id.isEmpty()) e.id = uid();
}

void Editor::beginHarnessPlacement(const HarnessDef& def) {
    if (!doc_) return;
    HarnessBuild built = buildHarness(def);
    if (built.entities.empty()) return;
    setTool(ToolId::Select); // a click must place, not draw
    harnessGhost_ = std::move(built.entities);
    // Ids and the shield/twist cross-references are settled now, while the definition is
    // still to hand; placing then only has to move the finished objects into the drawing.
    stampHarnessIds(harnessGhost_, def);
    // Held by the top-left of its own extent, so the crosshair sits on the corner of the
    // harness and the whole thing follows as one rigid piece.
    Bounds b;
    for (const Entity& e : harnessGhost_) b.expand(e.bounds());
    harnessBase_ = b.valid ? Vec2{b.min.x, b.max.y} : Vec2{0.0, 0.0};
    placeRot_ = 0.0;   // it starts the way it was built; R turns it before it lands
    harnessPlacing_ = true;
    emit message(QStringLiteral("Harness ready — click to place it, Esc to go back to the builder"));
    emit overlayChanged();
}

void Editor::cancelHarnessPlacement() {
    if (!harnessPlacing_) return;
    harnessPlacing_ = false;
    placeRot_ = 0.0;
    harnessGhost_.clear();
    emit message(QStringLiteral("Harness placement cancelled"));
    emit harnessPlacementFinished(false);
    emit overlayChanged();
}

// Drop the ghosted harness so its held corner lands at `at`.
void Editor::placeHarnessAt(const Vec2& at) {
    if (!doc_ || !harnessPlacing_ || harnessGhost_.empty()) return;
    const Vec2 shift = at - harnessBase_;
    pushUndo();
    std::vector<QString> ids;
    for (Entity e : harnessGhost_) {
        // Placed the way round it was held: turned about the corner it hangs from, then moved.
        if (std::abs(placeRot_) > 1e-9) rotateEntity(e, harnessBase_, placeRot_);
        translateEntity(e, shift);
        if (e.id.isEmpty()) e.id = uid();
        if (e.type == EntityType::Connector || e.type == EntityType::Wire)
            e.meta.insert(QStringLiteral("entNum"), assignEntityNumber());
        ids.push_back(e.id);
        doc_->entities().push_back(std::move(e));
    }
    const int n = static_cast<int>(ids.size());
    harnessPlacing_ = false;
    placeRot_ = 0.0;
    harnessGhost_.clear();
    selected_ = ids;
    doc_->setDirty(true);
    emit message(QStringLiteral("Harness placed: %1 object(s)").arg(n));
    emit harnessPlacementFinished(true);
    emit selectionChanged();
    emit changed();
}

int Editor::insertHarness(const HarnessDef& def, std::vector<HarnessIssue>* issuesOut) {
    if (!doc_) return 0;
    HarnessBuild built = buildHarness(def);
    if (issuesOut) *issuesOut = built.issues;
    if (built.entities.empty()) return 0;
    pushUndo();
    std::vector<QString> added;
    stampHarnessIds(built.entities, def);
    for (Entity& e : built.entities) {
        if (e.type == EntityType::Connector || e.type == EntityType::Wire)
            e.meta.insert(QStringLiteral("entNum"), assignEntityNumber());
        added.push_back(e.id);
        doc_->entities().push_back(std::move(e));
    }
    selected_ = added;
    doc_->setDirty(true);
    emit message(QStringLiteral("Harness generated: %1 object(s)%2")
                     .arg(added.size())
                     .arg(built.issues.empty()
                              ? QString()
                              : QStringLiteral(" — %1 issue(s) to review").arg(built.issues.size())));
    emit selectionChanged();
    emit changed();
    return static_cast<int>(added.size());
}

void Editor::editSelectedConnectorSpecs(const std::function<void(ConnectorSpec&)>& mut) {
    if (!doc_ || !mut) return;
    // Each connector's OWN spec is changed in place, so setting one field across several
    // connectors edits just that field on each — their designators, part numbers and pin
    // labels are left alone. (Writing one whole spec onto all of them would copy the first
    // connector's identity over the rest.)
    bool any = false;
    for (const QString& id : selected_) {
        Entity* e = entityById(id);
        if (!e || e->type != EntityType::Connector) continue;
        if (!any) { pushUndo(); any = true; }
        ConnectorSpec s = connectorSpecFromMeta(e->meta);
        mut(s);
        s.textSize = std::max({s.pinSize, s.desSize, s.pnSize});
        regenPinLabels(s);
        writeConnectorSpec(e->meta, s);
        connectorSpec_ = s; // the last one edited becomes the default for the next placed
    }
    if (!any) return;
    doc_->setDirty(true);
    emit changed();
}

void Editor::applyConnectorPinLabel(const QString& id, int pin, const QString& text,
                                    const QString& font) {
    Entity* e = entityById(id);
    if (!e || e->type != EntityType::Connector || pin < 0) return;
    QJsonArray pins = e->meta.value("pins").toArray();
    while (pins.size() <= pin) pins.append(QString());
    pins[pin] = text;
    pushUndo();
    e->meta["pins"] = pins;
    if (!font.isEmpty()) e->meta["pinFont"] = font; // font library applies to the pin text
    doc_->setDirty(true);
    emit changed();
}

void Editor::armConnectorPlacement() {
    setTool(ToolId::Connector);   // resets connectorArmed_/connectorFrozen_ to false...
    connectorArmed_ = true;       // ...then arm so the preview ghost appears
    connectorGrab_ = {0.0, 0.0};  // held by the top-left corner until a pin/corner is grabbed
    emit connectorArmedChanged();
    emit overlayChanged();
}

// A break is described by a LINE DRAWN ACROSS the runs it interrupts - the same way a screen and a
// twist are marked out. Which runs are broken is then a fact about the drawing rather than a guess
// from one click: the line says so, and the mark takes its width, its angle and the run it belongs to
// from what the line actually crossed.
bool Editor::breakFromBand(const Vec2& a, const Vec2& b, Entity& out) const {
    if (!doc_) return false;
    // Everything the line crosses, and which way that run was going there. A double line is crossed
    // at its RAILS - the lines that are drawn - so a band over a bundle measures the bundle.
    std::vector<Vec2> crossings;
    std::vector<Vec2> dirs;
    std::vector<QString> owners;
    auto tryLine = [&](const Entity& e, const Vec2& p1, const Vec2& p2) {
        if (auto x = segmentSegmentIntersection(a, b, p1, p2)) {
            crossings.push_back(*x);
            dirs.push_back(p2 - p1);
            owners.push_back(e.id);
        }
    };
    for (const auto& e : doc_->entities()) {
        switch (e.type) {
        case EntityType::Line:
            tryLine(e, e.a, e.b);
            break;
        case EntityType::DoubleLine: {
            const double gap = e.meta.value(QStringLiteral("gap")).toDouble(10.0);
            const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
            tryLine(e, e.a + off, e.b + off);
            tryLine(e, e.a - off, e.b - off);
            break;
        }
        case EntityType::Wire:
        case EntityType::Spline:
            for (size_t i = 1; i < e.points.size(); ++i) tryLine(e, e.points[i - 1], e.points[i]);
            break;
        case EntityType::Polyline: {
            if (e.meta.value(QStringLiteral("double")).toBool() && e.points.size() >= 2) {
                const double gap = e.meta.value(QStringLiteral("gap")).toDouble(10.0);
                for (double s : {gap / 2.0, -gap / 2.0}) {
                    const auto run = offsetPolyline(e.points, s);
                    for (size_t i = 1; i < run.size(); ++i) tryLine(e, run[i - 1], run[i]);
                }
            } else {
                for (size_t i = 1; i < e.points.size(); ++i) tryLine(e, e.points[i - 1], e.points[i]);
            }
            break;
        }
        default:
            break;
        }
    }
    if (crossings.empty()) return false;

    // The runs' own direction, averaged, which is what the mark lies square across. bundlePerpRot
    // gives the perpendicular of that mean, so the mean itself is a quarter turn back.
    const double rot = bundlePerpRot(dirs) - M_PI / 2.0;
    // The band the crossings occupy, measured ACROSS the runs: that is what the mark has to span.
    const Vec2 across{-std::sin(rot), std::cos(rot)};
    Vec2 centre;
    double span = 0.0;
    shieldExtent(crossings, across, centre, span);

    out = Entity();
    out.type = EntityType::LengthBreak;
    out.a = centre;
    out.rotation = rot;
    out.meta.insert(QStringLiteral("breakStyle"), breakStyle_);
    // A size given by hand wins; otherwise the mark overhangs the outermost run it crosses, the way
    // it is drawn by hand. A single run has no width of its own, so it gets a workable default.
    double half = breakSize_;
    if (half <= 1e-6)
        half = span > 1e-6 ? span * 0.5 + std::max(span * 0.22, 2.0) : 6.0;
    out.meta.insert(QStringLiteral("size"), half);
    out.meta.insert(QStringLiteral("gap"),
                    breakGap_ > 1e-6 ? breakGap_ : std::max(half * 0.8, 3.0));
    if (breakAngle_ >= 0.0) out.meta.insert(QStringLiteral("angle"), breakAngle_);
    // Attached to the run crossed nearest the middle of the band: the mark then follows that run and
    // slides along it, and with one run crossed that is simply the run it is on.
    QString host;
    double bestD = 1e18;
    for (size_t i = 0; i < crossings.size(); ++i) {
        const double d = distance(crossings[i], centre);
        if (d < bestD) { bestD = d; host = owners[i]; }
    }
    if (!host.isEmpty()) {
        out.meta.insert(QStringLiteral("host"), host);
        for (const auto& h : doc_->entities())
            if (h.id == host)
                out.meta.insert(QStringLiteral("annT"), runFractionNearest(h, centre));
    }
    return true;
}

Entity Editor::breakPreview() const {
    Entity e;
    if (tool_ != ToolId::LengthBreak || !doc_ || !wireBandHasStart_) return e;
    Entity built;
    if (breakFromBand(wireBandStart_, breakBandEnd(), built)) return built;
    return e;
}

void Editor::freezeTwistedPreview() {
    // Pins the loose end where it is, so the bundle stops following the cursor while the number of
    // wires and the rest are tried on it. Nothing to pin before it has a start.
    if (!isTwistedTool(tool_) || twistedFrozen_ || draftPts_.empty()) return;
    twistedFrozen_ = true;
    twistedFrozenAt_ = cursor_;
    emit message(QStringLiteral(
        "Pinned - set it up on the ribbon; click the drawing (or Enter) to place it"));
    emit overlayChanged();
}

void Editor::freezeBreakPreview() {
    // Pins the loose end of the marking line, so the mark it describes stops moving while the numbers
    // on the ribbon are tried on it. There is nothing to pin before the line has a start.
    if (tool_ != ToolId::LengthBreak || breakFrozen_ || !wireBandHasStart_) return;
    breakFrozen_ = true;
    breakFrozenAt_ = cursor_;
    emit message(QStringLiteral(
        "Line pinned — try the numbers on the ribbon; click the drawing to place the break"));
    emit overlayChanged();
}

void Editor::freezeConnectorPreview() {
    if (tool_ != ToolId::Connector || !connectorArmed_ || connectorFrozen_) return;
    connectorFrozen_ = true;
    connectorFrozenPos_ = cursor_; // pin the ghost here while the settings are edited
    emit message(QStringLiteral("Preview frozen — edit the settings; click the drawing to resume"));
    emit overlayChanged();
}

void Editor::toggleConnectorMirror(bool xAxis) {
    if (xAxis) connectorSpec_.mirrorX = !connectorSpec_.mirrorX;
    else connectorSpec_.mirrorY = !connectorSpec_.mirrorY;
    emit message(xAxis ? QStringLiteral("Connector mirrored across the Y axis (X to flip back)")
                       : QStringLiteral("Connector mirrored across the X axis (Y to flip back)"));
    emit overlayChanged();
}

std::vector<Editor::BomRow> Editor::buildBom() const {
    std::vector<BomRow> rows;
    if (!doc_) return rows;
    for (const auto& e : doc_->entities()) {
        if (e.type == EntityType::Connector) {
            const ConnectorSpec s = connectorSpecFromMeta(e.meta);
            rows.push_back({QStringLiteral("Connector"),
                            s.designator.isEmpty() ? QStringLiteral("?") : s.designator,
                            s.partNumber, QStringLiteral("%1 pins").arg(s.pinCount),
                            QStringLiteral("1")});
        } else if (e.type == EntityType::Wire) {
            double len = 0;
            for (size_t i = 1; i < e.points.size(); ++i)
                len += distance(e.points[i - 1], e.points[i]);
            const QString code = e.meta.value("wireColor").toString("WH");
            rows.push_back({QStringLiteral("Wire"),
                            e.meta.value("netName").toString(),
                            QStringLiteral("%1 (%2)").arg(code, wireColorName(code)),
                            QStringLiteral("%1 mm²").arg(e.meta.value("wireGauge").toString("0.5")),
                            QStringLiteral("%1").arg(len, 0, 'f', 1)});
        }
    }
    return rows;
}

// ---- Edit actions ----------------------------------------------------------

void Editor::selectAll() {
    if (!doc_) return;
    selected_.clear();
    for (const auto& e : doc_->entities()) selected_.push_back(e.id);
    emit selectionChanged();
    emit changed();
}

// The other wires of the bundle a selected wire belongs to. They are not selected - selecting
// them would mean an edit hit all of them - they are only lit, so what travels with this wire is
// visible while it is being worked on.
void Editor::refreshBundleCompanions() {
    bundleCompanions_.clear();
    if (!doc_) return;
    QStringList bundles;
    for (const QString& id : selected_) {
        const Entity* e = entityById(id);
        if (!e || e->type != EntityType::Wire) continue;
        const QString b = e->meta.value(QStringLiteral("bundle")).toString();
        if (!b.isEmpty() && !bundles.contains(b)) bundles << b;
    }
    if (bundles.isEmpty()) return;
    for (const auto& e : doc_->entities()) {
        if (e.type != EntityType::Wire) continue;
        if (std::find(selected_.begin(), selected_.end(), e.id) != selected_.end())
            continue; // the selection itself is drawn as selected
        const QString b = e.meta.value(QStringLiteral("bundle")).toString();
        if (!b.isEmpty() && bundles.contains(b)) bundleCompanions_.insert(e.id);
    }
}

void Editor::selectById(const QString& id, bool additive) {
    if (!additive) selected_.clear();
    if (!entityById(id)) { emit selectionChanged(); emit changed(); return; }
    auto it = std::find(selected_.begin(), selected_.end(), id);
    if (it == selected_.end()) selected_.push_back(id);
    emit selectionChanged();
    emit changed();
}

bool Editor::markQuad(const Entity& wire, const QString& part, int index,
                      std::vector<Vec2>& quad) const {
    if (wire.type != EntityType::Wire) return false;
    const QString text = wireAnnotPartText(wire, part);
    if (text.isEmpty()) return false;
    const std::vector<WireAnnot> marks = wireAnnotsFor(wire, part);
    if (index < 0 || index >= static_cast<int>(marks.size())) return false;
    const WireAnnot& a = marks[index];
    const double h = wireAnnotSizeFor(wire, part);
    // The same width estimate the layout uses, so the box is the text's own footprint.
    const double w = 0.62 * h * static_cast<double>(text.size());
    const Vec2 along = a.dir;
    const Vec2 up = a.dir.perp() * (a.flip ? -1.0 : 1.0);
    const Vec2 base = a.pos + up * wireAnnotGapFor(wire, part);
    quad = {base - along * (w / 2.0), base + along * (w / 2.0),
            base + along * (w / 2.0) + up * h, base - along * (w / 2.0) + up * h};
    return true;
}

int Editor::markSelectionAt(const Vec2& p) const {
    if (!doc_) return -1;
    for (int i = 0; i < static_cast<int>(markSel_.size()); ++i) {
        const MarkPick& m = markSel_[i];
        const Entity* w = nullptr;
        for (const auto& e : doc_->entities())
            if (e.id == m.wireId) { w = &e; break; }
        if (!w) continue;
        std::vector<Vec2> quad;
        if (!markQuad(*w, m.part, m.index, quad)) continue;
        if (pointInPolygon(quad, p)) return i;
    }
    return -1;
}

void Editor::dragPickedMarks(const Vec2& to, bool all, int only) {
    if (!doc_) return;
    for (int i = 0; i < static_cast<int>(markSel_.size()); ++i) {
        if (!all && i != only) continue;
        const MarkPick& m = markSel_[i];
        Entity* w = entityById(m.wireId);
        if (!w || w->type != EntityType::Wire) continue;
        // Only the place ALONG the wire changes: the cursor is projected onto the run, so a
        // marking can never be dragged off the wire it names. With the whole pick moving, each
        // one lands on its own wire nearest the cursor - so they line up under it.
        const int count = wireAnnotCountFor(*w, m.part);
        if (m.index < 0 || m.index >= count) continue;
        std::vector<double> ts = wireAnnotFractionsFor(*w, m.part, count);
        ts[m.index] = wireFractionNearest(*w, to);
        setWireAnnotFractionsFor(*w, m.part, ts);
    }
    doc_->setDirty(true);
    emit changed();
}

void Editor::clearMarkSelection() {
    if (markSel_.empty() && !markGrab_) return;
    markSel_.clear();
    markGrab_ = false;
    markGrabAt_ = -1;
    markMoved_ = false;
    emit changed();
}

void Editor::clearSelection() {
    selected_.clear();
    markSel_.clear();
    markGrab_ = false;
    markGrabAt_ = -1;
    clearCellSelection();
    emit selectionChanged();
    emit changed();
}

void Editor::setSelectionLayer(const QString& layer) {
    auto sel = selectedEntities();
    if (sel.empty()) return;
    pushUndo();
    for (Entity* e : sel) e->layer = layer;
    doc_->setDirty(true);
    emit changed();
}

void Editor::setSelectionColor(const QString& hex) {
    auto sel = selectedEntities();
    if (sel.empty()) return;
    pushUndo();
    for (Entity* e : sel) e->color = hex; // empty => ByLayer
    doc_->setDirty(true);
    emit changed();
}

void Editor::setSelectionWeight(double weight) {
    auto sel = selectedEntities();
    if (sel.empty()) return;
    pushUndo();
    for (Entity* e : sel) e->weight = weight;
    doc_->setDirty(true);
    emit changed();
}

void Editor::setColorsByLayer() {
    if (!doc_) return;
    pushUndo();
    auto sel = selectedEntities();
    if (sel.empty()) {
        for (auto& e : doc_->entities()) e.color.clear(); // every object -> ByLayer
    } else {
        for (Entity* e : sel) e->color.clear();
    }
    doc_->setDirty(true);
    emit changed();
    emit message(QStringLiteral("Color set to ByLayer"));
}

void Editor::setSelectionMeta(const QString& key, const QString& value) {
    auto sel = selectedEntities();
    if (sel.empty()) return;
    pushUndo();
    for (Entity* e : sel) e->meta[key] = value;
    doc_->setDirty(true);
    emit changed();
}

void Editor::setSelectionScreenLegDir(const QString& dir) {
    auto sel = selectedEntities();
    if (sel.empty()) return;
    pushUndo();
    int moved = 0;
    for (Entity* e : sel) {
        if (e->type != EntityType::Screen) {
            e->meta[QStringLiteral("legDir")] = dir;
            continue;
        }
        // The leg is the entity's LAST point, past the crossings it catches. Setting the side in the
        // meta and leaving that point where it was is what made the leg stay at the bottom while the
        // drawing claimed it was at the top: the side has to carry the point with it. It keeps the
        // length it had, measured from the outline it used to leave.
        const ShieldFrame f = shieldFrameOf(*e);
        const QString was = e->meta.value(QStringLiteral("legDir")).toString(QStringLiteral("bottom"));
        const bool hasLeg = f.valid && static_cast<int>(e->points.size()) > f.k;
        double legLen = screenLeg_;
        if (hasLeg) legLen = distance(e->points.back(), shieldLegEdge(f, was));
        if (legLen <= 1e-6) legLen = std::max(1.0, screenLeg_);
        e->meta[QStringLiteral("legDir")] = dir;
        if (!f.valid) continue;
        const Vec2 edge = shieldLegEdge(f, dir);
        const Vec2 out = (edge - f.center).normalized();
        const Vec2 tip{edge.x + out.x * legLen, edge.y + out.y * legLen};
        if (hasLeg) e->points.back() = tip;
        else e->points.push_back(tip);
        ++moved;
    }
    doc_->setDirty(true);
    emit changed();
    if (moved > 0)
        emit message(QStringLiteral("Drain leg moved to the %1").arg(dir));
}

void Editor::setSelectionMetaNumber(const QString& key, double value) {
    auto sel = selectedEntities();
    if (sel.empty()) return;
    pushUndo();
    for (Entity* e : sel) e->meta[key] = value;
    doc_->setDirty(true);
    emit changed();
}

void Editor::resizeSelectedShield(const QString& key, double value) {
    auto sel = selectedEntities();
    if (sel.empty()) return;
    value = std::max(1.0, value);
    pushUndo();
    for (Entity* e : sel) {
        if (e->type != EntityType::Screen) { e->meta[key] = value; continue; }
        const int k = std::min(e->meta.value("catch").toInt(static_cast<int>(e->points.size())),
                               static_cast<int>(e->points.size()));
        if (k < 1) { e->meta[key] = value; continue; }
        std::vector<Vec2> crossings(e->points.begin(), e->points.begin() + k);
        const double rot = EntityRenderer::shieldRotation(crossings);
        const Vec2 major{std::cos(rot), std::sin(rot)};
        Vec2 center;
        double span;
        shieldExtent(crossings, major, center, span);
        // Old value of the radius being changed (auto-fit for ry when absent).
        double oldR = e->meta.value(key).toDouble(0.0);
        if (oldR <= 0.0) oldR = (key == QLatin1String("ry")) ? span / 2.0 + 6.0 : 12.0;
        const double f = oldR > 1e-6 ? value / oldR : 1.0;
        e->meta[key] = value;
        // Scale the drain about the centre by the same factor, but only when the leg runs
        // along the axis that changed (ry -> top/bottom legs, rx -> left/right legs).
        const QString legDir = e->meta.value("legDir").toString(QStringLiteral("bottom"));
        const bool alongMajor = legDir == QLatin1String("top") || legDir == QLatin1String("bottom");
        const bool relevant = (key == QLatin1String("ry") && alongMajor) ||
                              (key == QLatin1String("rx") && !alongMajor);
        if (relevant && static_cast<int>(e->points.size()) > k) {
            Vec2& drain = e->points.back();
            drain = center + (drain - center) * f;
        }
    }
    doc_->setDirty(true);
    emit changed();
}

// ---- Re-ordering the pinout of connectors already on the drawing --------------
// The builder can untangle a harness before it is drawn; this does the same to one already on
// the sheet. Nothing about the WIRING changes - every wire keeps the pin it is connected to -
// only where each pin sits on its connector, and the wires are carried to their pin's new row.
//
// The work of deciding the order is the builder's: the selection is read into a HarnessDef, the
// same optimiser runs over it, and the result is written back. That way the drawing and the
// builder cannot disagree about what "untangled" means.

namespace {
// One connector of the selection, with what is needed to move its pins.
struct PinoutConn {
    QString id;              // entity id
    ConnectorSpec spec;
    ConnectorGeometry geom;
    bool left = false;       // which side of the harness it sits on
    double x = 0.0;
};
} // namespace

// Read the selected connectors and the wires running between them into a definition.
bool Editor::buildPinoutModel(std::vector<QString>& connIds, HarnessDef& def) const {
    if (!doc_) return false;
    connIds.clear();
    def = HarnessDef();
    std::vector<PinoutConn> conns;
    for (const QString& id : selected_) {
        const Entity* e = nullptr;
        for (const auto& x : doc_->entities())
            if (x.id == id) e = &x;
        if (!e || e->type != EntityType::Connector) continue;
        PinoutConn pc;
        pc.id = id;
        pc.spec = connectorSpecFromMeta(e->meta);
        pc.geom = connectorGeometry(e->a, pc.spec);
        pc.x = e->a.x;
        conns.push_back(pc);
    }
    if (conns.size() < 2) return false;
    // Which side each one is on: the left-hand half of the group is "left". The optimiser only
    // uses the sides to know which pairs share a corridor.
    double minX = conns.front().x, maxX = conns.front().x;
    for (const PinoutConn& c : conns) {
        minX = std::min(minX, c.x);
        maxX = std::max(maxX, c.x);
    }
    const double mid = (minX + maxX) / 2.0;
    for (PinoutConn& c : conns) c.left = c.x <= mid;

    for (const PinoutConn& c : conns) {
        HarnessConnector hc;
        hc.id = c.id;
        hc.side = c.left ? HarnessSide::Left : HarnessSide::Right;
        hc.designator = c.spec.designator;
        hc.pinCount = std::max(1, c.spec.pinCount);
        hc.pitch = c.spec.pitch;
        hc.pinLength = c.spec.pinLength;
        hc.annotation = c.spec.annotation;
        def.connectors.push_back(hc);
        connIds.push_back(c.id);
    }
    // Every wire whose two ends land on pins of two of these connectors.
    auto pinAt = [&conns](const Vec2& pt, QString& connId, int& pin) {
        for (const PinoutConn& c : conns)
            for (int i = 0; i < static_cast<int>(c.geom.pinEnd.size()); ++i)
                if (distance(c.geom.pinEnd[i], pt) < 1e-6) {
                    connId = c.id;
                    pin = i + 1; // pin NUMBER = its index in the current label order
                    return true;
                }
        return false;
    };
    int seq = 0;
    for (const auto& w : doc_->entities()) {
        if (w.type != EntityType::Wire || w.points.size() < 2) continue;
        QString fromId, toId;
        int fromPin = 0, toPin = 0;
        if (!pinAt(w.points.front(), fromId, fromPin)) continue;
        if (!pinAt(w.points.back(), toId, toPin)) continue;
        if (fromId == toId) continue; // both ends on one connector: no corridor to cross
        HarnessWire hw;
        hw.id = QStringLiteral("w%1").arg(++seq);
        hw.fromConn = fromId;
        hw.fromPin = fromPin;
        hw.toConn = toId;
        hw.toPin = toPin;
        hw.color = w.meta.value(QStringLiteral("wireColor")).toString();
        hw.gauge = w.meta.value(QStringLiteral("wireGauge")).toString();
        // Bundles hold together through a re-order, as they do in the builder.
        hw.shieldGroup = w.meta.value(QStringLiteral("shieldGroup")).toString();
        hw.twistGroup = w.meta.value(QStringLiteral("twistGroup")).toString();
        def.wires.push_back(hw);
    }
    return !def.wires.empty();
}

bool Editor::canReorderSelectedPinout() const {
    std::vector<QString> ids;
    HarnessDef def;
    return buildPinoutModel(ids, def);
}

int Editor::reorderSelectedPinout(const PinoutGoal& goal) {
    std::vector<QString> ids;
    HarnessDef def;
    if (!buildPinoutModel(ids, def)) {
        emit message(QStringLiteral("Select two or more connectors with wires between them"));
        return 0;
    }
    // What to aim for. Crossings always count; grouping pulls the wires onto neighbouring pins,
    // and "from top" additionally puts that group at the first pin.
    HarnessOptimizeWeights w;
    w.crossing = 1;
    w.wireSpan = goal.group ? 60 : 0;
    w.pinOrder = goal.fromTop ? 40 : 0;
    const int before = harnessLayoutCost(def, w);
    optimizeHarnessPinout(def, w);
    const int after = harnessLayoutCost(def, w);

    pushUndo();
    int moved = 0;
    for (const QString& id : ids) {
        const HarnessConnector* hc = harnessConnById(def, id);
        if (!hc) continue;
        const std::vector<int> order = harnessPinSequence(*hc); // pin numbers, top-down
        Entity* e = entityById(id);
        if (!e) continue;
        ConnectorSpec spec = connectorSpecFromMeta(e->meta);
        const ConnectorGeometry oldGeom = connectorGeometry(e->a, spec);
        // Where each pin NUMBER ends up. A pin's number is its index in the CURRENT order, so
        // the new row of pin p is wherever p appears in `order`.
        std::vector<int> newRowOf(order.size(), 0);
        bool changed = false;
        for (int row = 0; row < static_cast<int>(order.size()); ++row) {
            const int pin = order[row];
            if (pin >= 1 && pin <= static_cast<int>(newRowOf.size())) newRowOf[pin - 1] = row;
            if (pin != row + 1) changed = true;
        }
        if (!changed) continue;
        // The labels travel with their pins, so a pin keeps its NAME at its new row.
        QStringList labels = spec.pins;
        while (labels.size() < spec.pinCount) labels << QString::number(labels.size() + 1);
        QStringList movedLabels = labels;
        for (int row = 0; row < static_cast<int>(order.size()) && row < labels.size(); ++row) {
            const int pin = order[row];
            if (pin >= 1 && pin <= labels.size()) movedLabels[row] = labels[pin - 1];
        }
        spec.pins = movedLabels;
        writeConnectorSpec(e->meta, spec);
        const ConnectorGeometry newGeom = connectorGeometry(e->a, spec);
        // Carry every wire end on this connector to the row its pin moved to.
        for (auto& wire : doc_->entities()) {
            if (wire.type != EntityType::Wire || wire.points.empty()) continue;
            for (Vec2* tip : {&wire.points.front(), &wire.points.back()}) {
                for (int oldRow = 0; oldRow < static_cast<int>(oldGeom.pinEnd.size()); ++oldRow) {
                    if (distance(oldGeom.pinEnd[oldRow], *tip) >= 1e-6) continue;
                    const int newRow = newRowOf[oldRow];
                    if (newRow >= 0 && newRow < static_cast<int>(newGeom.pinEnd.size()))
                        *tip = newGeom.pinEnd[newRow];
                    break;
                }
            }
        }
        ++moved;
    }
    doc_->setDirty(true);
    emit changed();
    emit message(moved == 0
                     ? QStringLiteral("The pinout is already as good as this can make it")
                     : QStringLiteral("Re-ordered %1 connector(s) — cost %2 → %3")
                           .arg(moved).arg(before).arg(after));
    return moved;
}

bool Editor::shieldLegIsWired(const Entity& shield) const {
    if (!doc_ || shield.type != EntityType::Screen) return false;
    const int k = std::min(shield.meta.value("catch").toInt(static_cast<int>(shield.points.size())),
                           static_cast<int>(shield.points.size()));
    if (k < 1 || static_cast<int>(shield.points.size()) <= k) return false;
    const Vec2 drain = shield.points.back();
    // A wire counts as landing on the leg if any of its vertices sits on the drain end, or
    // the drain end sits on one of its runs (a leg dropped onto the middle of a wire is
    // just as connected as one dropped on its end).
    for (const auto& w : doc_->entities()) {
        if (w.type != EntityType::Wire || w.points.empty()) continue;
        for (const Vec2& pt : w.points)
            if (distance(pt, drain) < 1e-6) return true;
        for (size_t i = 1; i < w.points.size(); ++i)
            if (closestPointOnSegment(drain, w.points[i - 1], w.points[i]).distance < 1e-6)
                return true;
    }
    return false;
}

double Editor::selectedShieldLegLength() const {
    if (!doc_ || selected_.size() != 1) return 0.0;
    for (const auto& e : doc_->entities()) {
        if (e.id != selected_.front() || e.type != EntityType::Screen) continue;
        const ShieldFrame f = shieldFrameOf(e);
        if (!f.valid || static_cast<int>(e.points.size()) <= f.k) return 0.0;
        // The leg IS the last point, so its length is simply how far that point sits from
        // where the leg leaves the outline.
        const Vec2 edge = shieldLegEdge(f, e.meta.value("legDir").toString(QStringLiteral("bottom")));
        return distance(edge, e.points.back());
    }
    return 0.0;
}

void Editor::setSelectedShieldLegLength(double length) {
    if (!doc_) return;
    length = std::max(0.0, length);
    bool any = false;
    for (const QString& id : selected_) {
        Entity* e = entityById(id);
        if (!e || e->type != EntityType::Screen) continue;
        const ShieldFrame f = shieldFrameOf(*e);
        if (!f.valid || static_cast<int>(e->points.size()) <= f.k) continue;
        if (!any) { pushUndo(); any = true; }
        // Slide the leg end along the leg's OWN direction, so changing the length never
        // swings the leg to a different side.
        const Vec2 edge = shieldLegEdge(f, e->meta.value("legDir").toString(QStringLiteral("bottom")));
        Vec2 dir = e->points.back() - edge;
        if (dir.lengthSq() < 1e-12) {
            // A leg of zero length has no direction of its own; take the one its side implies.
            const QString legDir = e->meta.value("legDir").toString(QStringLiteral("bottom"));
            if (legDir == QLatin1String("top")) dir = f.major;
            else if (legDir == QLatin1String("left")) dir = f.minor;
            else if (legDir == QLatin1String("right")) dir = f.minor * -1.0;
            else dir = f.major * -1.0;
        }
        e->points.back() = edge + dir.normalized() * length;
    }
    if (!any) return;
    doc_->setDirty(true);
    emit changed();
}

void Editor::setSelectionMetaBool(const QString& key, bool value) {
    auto sel = selectedEntities();
    if (sel.empty()) return;
    pushUndo();
    for (Entity* e : sel) e->meta[key] = value;
    doc_->setDirty(true);
    emit changed();
}

void Editor::setSelectionTextMeta(const QString& key, const QJsonValue& value) {
    auto sel = selectedEntities();
    if (sel.empty()) return;
    pushUndo();
    for (Entity* e : sel)
        if (e->type == EntityType::Text) e->meta[key] = value;
    doc_->setDirty(true);
    emit changed();
}

void Editor::setSelectionHatchMeta(const QString& key, const QJsonValue& value) {
    auto sel = selectedEntities();
    if (sel.empty()) return;
    pushUndo();
    for (Entity* e : sel)
        if (e->type == EntityType::Hatch) e->meta[key] = value;
    doc_->setDirty(true);
    emit changed();
}

int Editor::markSelectionAsConnectionLine(bool on) {
    auto sel = selectedEntities();
    if (sel.empty() || !doc_) return 0;
    pushUndo();
    int n = 0;
    for (Entity* e : sel) {
        if (on) e->meta.insert(QLatin1String(kConnectionLineKey), true);
        else e->meta.remove(QLatin1String(kConnectionLineKey));
        ++n;
    }
    doc_->setDirty(true);
    emit changed();
    return n;
}

bool Editor::selectionIsConnectionLine() const {
    if (!doc_) return false;
    bool any = false;
    for (const QString& id : selected_) {
        const Entity* e = nullptr;
        for (const Entity& x : doc_->entities())
            if (x.id == id) e = &x;
        if (!e) continue;
        any = true;
        if (!e->meta.value(QLatin1String(kConnectionLineKey)).toBool()) return false;
    }
    return any;
}

void Editor::setSelectionDimMeta(const QString& key, const QJsonValue& value) {
    auto sel = selectedEntities();
    if (sel.empty()) return;
    pushUndo();
    // An emptied text field means "no override", not "an override that is blank" - so the key goes
    // rather than being written empty, and the dimension reads its measurement again.
    const bool clear = value.isString() && value.toString().isEmpty();
    for (Entity* e : sel) {
        if (e->type != EntityType::Dimension) continue;
        if (clear) e->meta.remove(key);
        else e->meta[key] = value;
    }
    doc_->setDirty(true);
    emit changed();
}

void Editor::setHatchMeta(const QString& id, const QString& key, const QJsonValue& value) {
    if (!doc_) return;
    if (Entity* e = entityById(id); e && e->type == EntityType::Hatch) {
        pushUndo();
        e->meta[key] = value;
        doc_->setDirty(true);
        emit changed();
    }
}

void Editor::setEntityColor(const QString& id, const QString& hex) {
    if (!doc_) return;
    if (Entity* e = entityById(id)) {
        pushUndo();
        e->color = hex; // empty = ByLayer
        doc_->setDirty(true);
        emit changed();
    }
}

bool Editor::beginRightDrag(const Vec2& world) {
    if (!doc_ || tool_ != ToolId::Select) return false;
    // Drag the current selection; if nothing is selected, grab the object under
    // the cursor (and select it).
    if (selected_.empty()) {
        if (auto hit = hitTest(world)) {
            selected_ = {*hit};
            emit selectionChanged();
        } else {
            return false;
        }
    }
    // A balloon in a batch drags the WHOLE batch (all leaders move together).
    std::vector<QString> batchAdd;
    for (const QString& id : selected_)
        if (Entity* e = entityById(id); e && e->meta.value("balloon").toBool())
            for (Entity* m : chainMatesOf(*e))
                if (std::find(selected_.begin(), selected_.end(), m->id) == selected_.end() &&
                    std::find(batchAdd.begin(), batchAdd.end(), m->id) == batchAdd.end())
                    batchAdd.push_back(m->id);
    for (const auto& id : batchAdd) selected_.push_back(id);
    if (!batchAdd.empty()) emit selectionChanged();
    rightDragGhost_.clear();
    for (const auto& id : selected_)
        if (Entity* e = entityById(id)) rightDragGhost_.push_back(*e);
    if (rightDragGhost_.empty()) return false;
    rightDrag_ = true;
    rdMirrorX_ = false;
    rdMirrorY_ = false;
    rightDragStart_ = world;
    rightDragCur_ = world;
    emit overlayChanged();
    return true;
}

void Editor::updateRightDrag(const Vec2& world) {
    if (!rightDrag_) return;
    rightDragCur_ = world;
    emit overlayChanged();
}

void Editor::rightDragMirror(bool xAxis) {
    if (!rightDrag_) return;
    if (xAxis) rdMirrorX_ = !rdMirrorX_;
    else rdMirrorY_ = !rdMirrorY_;
    emit message(xAxis ? QStringLiteral("Mirror across the X axis") : QStringLiteral("Mirror across the Y axis"));
    emit overlayChanged();
}

// Apply the active mirror(s) about the catch location, then translate to the cursor.
Entity Editor::rightDragResult(const Entity& src) const {
    Entity e = src;
    auto reflectAnchor = [&](const Vec2& u) { // text: reflect position only
        const Vec2 v = e.a - rightDragStart_;
        const double d = v.x * u.x + v.y * u.y;
        const Vec2 proj{rightDragStart_.x + u.x * d, rightDragStart_.y + u.y * d};
        e.a = {2.0 * proj.x - e.a.x, 2.0 * proj.y - e.a.y};
    };
    if (rdMirrorX_) { // X axis = horizontal mirror line through the catch point
        if (e.type == EntityType::Text) reflectAnchor({1, 0});
        else mirrorEntity(e, rightDragStart_, {rightDragStart_.x + 1.0, rightDragStart_.y});
    }
    if (rdMirrorY_) { // Y axis = vertical mirror line
        if (e.type == EntityType::Text) reflectAnchor({0, 1});
        else mirrorEntity(e, rightDragStart_, {rightDragStart_.x, rightDragStart_.y + 1.0});
    }
    translateEntity(e, rightDragCur_ - rightDragStart_);
    return e;
}

void Editor::commitRightDrag(bool copy) {
    if (!rightDrag_ || !doc_) { cancelRightDrag(); return; }
    pushUndo();
    if (copy) {
        // The copies are made, given their ids, and then made to point at EACH OTHER rather than at the
        // originals: a shield is drawn across the wires it names, and a copied shield still naming the
        // originals' wires draws a line straight across the sheet from the copy to them. See
        // remapEntityReferences.
        std::vector<Entity> copies;
        copies.reserve(rightDragGhost_.size());
        QHash<QString, QString> oldToNew;
        for (const Entity& g : rightDragGhost_) {
            Entity e = rightDragResult(g);
            e.id = uid();
            oldToNew.insert(g.id, e.id);
            copies.push_back(std::move(e));
        }
        remapEntityReferences(copies, oldToNew);
        std::vector<QString> newSel;
        for (Entity& e : copies) {
            newSel.push_back(e.id);
            doc_->entities().push_back(std::move(e));
        }
        selected_ = newSel; // select the copies
    } else {
        for (const Entity& g : rightDragGhost_)
            if (Entity* ent = entityById(g.id)) {
                const QString id = ent->id;
                *ent = rightDragResult(g);
                ent->id = id;
            }
    }
    rightDrag_ = false;
    rightDragGhost_.clear();
    doc_->setDirty(true);
    emit message(copy ? QStringLiteral("Copied here") : QStringLiteral("Moved here"));
    emit selectionChanged();
    emit changed();
}

void Editor::cancelRightDrag() {
    if (!rightDrag_) return;
    rightDrag_ = false;
    rightDragGhost_.clear();
    emit overlayChanged();
}

void Editor::commitMirror(bool copy) {
    if (!mirrorPending_ || !doc_) { cancelMirror(); return; }
    mirrorPending_ = false;
    auto sel = selectedEntities();
    if (sel.empty()) { setTool(ToolId::Select); return; }
    pushUndo();
    if (copy) {
        // Keep the originals; add a mirrored copy of each and select the copies.
        //
        // Every copy is made BEFORE any of them is added. `sel` holds pointers INTO the document's own
        // vector, and appending to that vector may reallocate it - after which every pointer still to
        // be read is dangling. Mirroring two or more objects therefore copied freed memory for the
        // second one onwards: the ghost was mirrored from the live selection and looked right, and what
        // was placed was garbage that draws nothing. It had grips, which is how it could be seen to be
        // there at all.
        std::vector<Entity> copies;
        copies.reserve(sel.size());
        std::vector<QString> newSel;
        QHash<QString, QString> oldToNew;
        for (Entity* e : sel) {
            Entity c = *e;
            c.id = uid();
            mirrorEntity(c, mirrorA_, mirrorB_);
            oldToNew.insert(e->id, c.id);
            newSel.push_back(c.id);
            copies.push_back(std::move(c));
        }
        remapEntityReferences(copies, oldToNew);   // the copies name each other, not the originals
        for (Entity& c : copies) doc_->entities().push_back(std::move(c));
        selected_ = newSel;
        emit selectionChanged();
    } else {
        for (Entity* e : sel) mirrorEntity(*e, mirrorA_, mirrorB_);
    }
    doc_->setDirty(true);
    emit message(copy ? QStringLiteral("Mirrored (copy)") : QStringLiteral("Mirrored"));
    emit changed();
    setTool(ToolId::Select);
}

void Editor::cancelMirror() {
    if (!mirrorPending_) return;
    mirrorPending_ = false;
    modifyPts_.clear();
    emit message(QStringLiteral("Mirror cancelled"));
    emit changed();
    setTool(ToolId::Select);
}

void Editor::setGeometryValue(const QString& key, double v) {
    auto sel = selectedEntities();
    if (sel.size() != 1) return;
    Entity* e = sel.front();
    const Bounds b = e->bounds();
    const Vec2 c = b.valid ? b.center() : e->center;
    pushUndo();
    auto scaleAxis = [&](bool xAxis, double f) {
        auto sp = [&](Vec2& p) {
            if (xAxis) p.x = c.x + (p.x - c.x) * f;
            else p.y = c.y + (p.y - c.y) * f;
        };
        sp(e->a); sp(e->b); sp(e->center);
        for (auto& p : e->points) sp(p);
    };
    if (key == "Center X") translateEntity(*e, {v - c.x, 0});
    else if (key == "Center Y") translateEntity(*e, {0, v - c.y});
    else if (key == "Radius") { if (e->type == EntityType::Circle || e->type == EntityType::Arc) e->radius = std::max(1e-6, v); }
    else if (key == "Width") {
        if (e->type == EntityType::Circle) e->radius = std::max(1e-6, v / 2.0);
        else if (e->type == EntityType::Ellipse) e->radius = std::max(1e-6, v / 2.0);
        else { const double w = b.width(); if (w > 1e-9) scaleAxis(true, v / w); }
    } else if (key == "Height") {
        if (e->type == EntityType::Circle) e->radius = std::max(1e-6, v / 2.0);
        else if (e->type == EntityType::Ellipse) e->radiusY = std::max(1e-6, v / 2.0);
        else { const double h = b.height(); if (h > 1e-9) scaleAxis(false, v / h); }
    }
    doc_->setDirty(true);
    emit changed();
}

void Editor::deleteSelected() {
    if (!doc_ || selected_.empty()) return;
    // Deleting the objects an un-placed array is built from ends that array.
    if (arrayActive_) cancelArray();
    pushUndo();
    auto& ents = doc_->entities();
    ents.erase(std::remove_if(ents.begin(), ents.end(),
                              [&](const Entity& e) {
                                  return std::find(selected_.begin(), selected_.end(),
                                                   e.id) != selected_.end();
                              }),
               ents.end());
    selected_.clear();
    doc_->setDirty(true);
    emit selectionChanged();
    emit changed();
}

void Editor::copySelection() {
    clipboard_.clear();
    clipStamp_ = QDateTime::currentMSecsSinceEpoch();
    Bounds b;
    for (const auto& id : selected_)
        if (const Entity* e = entityById(id)) {
            clipboard_.push_back(*e);
            const Bounds eb = e->bounds();
            if (eb.valid) { b.expand(eb.min); b.expand(eb.max); }
        }
    // Quick copy: base point = lower-left of the selection extents.
    clipboardBase_ = b.valid ? b.min : Vec2{0, 0};
    awaitClipBase_ = false;
    emit message(QStringLiteral("Copied %1 object(s) — Ctrl+V to place").arg(clipboard_.size()));
}

void Editor::cutSelection() {
    if (selected_.empty()) {
        emit message(QStringLiteral("Select objects first"));
        return;
    }
    copySelection();   // fills the clipboard (base = selection bbox min)
    deleteSelected();  // removes the originals (one undo step) — Ctrl+V re-places them
    emit message(QStringLiteral("Cut %1 object(s) — Ctrl+V to place").arg(clipboard_.size()));
}

void Editor::copyWithBase(bool cut) {
    if (selected_.empty()) {
        emit message(QStringLiteral("Select objects first"));
        return;
    }
    clipboard_.clear();
    for (const auto& id : selected_)
        if (const Entity* e = entityById(id)) clipboard_.push_back(*e);
    awaitClipBase_ = true;
    clipCut_ = cut;
    pastePlacing_ = false;
    emit message(cut ? QStringLiteral("Cut: specify base point")
                     : QStringLiteral("Copy: specify base point"));
}

void Editor::refuseAt(const Vec2& where, const QString& why) {
    refused_ = true;
    refusedAt_ = where;
    emit message(why);
    emit overlayChanged();
}

void Editor::clearRefusal() {
    if (!refused_) return;
    refused_ = false;
    emit overlayChanged();
}

void Editor::rotateMoveBy90(bool clockwise) {
    if (!doc_ || !movingSomething()) return;
    // About the CURSOR, so whatever is under it stays under it - the same as turning something that is
    // held before being placed.
    const Vec2 pivot = cursor_;
    const double by = clockwise ? -M_PI / 2.0 : M_PI / 2.0;
    if (!editMoved_) {   // the drag has not written anything yet: this turn is the first change
        pushUndo();
        editMoved_ = true;
    }
    std::vector<QString> ids;
    if (editMode_ == 0) ids = selected_;
    else for (const auto& eg : editGrips_) ids.push_back(eg.id);
    int turned = 0;
    for (const QString& id : ids)
        if (Entity* e = entityById(id)) {
            rotateEntity(*e, pivot, by);
            ++turned;
        }
    if (turned == 0) return;
    doc_->setDirty(true);
    emit message(QStringLiteral("Turned %1 object(s) a quarter turn %2 - Space turns again, "
                                "Shift+Space the other way")
                     .arg(turned)
                     .arg(clockwise ? QStringLiteral("clockwise") : QStringLiteral("anticlockwise")));
    emit changed();
}

void Editor::rotatePlacementPreview(bool clockwise) {
    // A quarter turn at a time, about the point the thing is held by - which is the paste base or the
    // corner a harness hangs from, so what is under the crosshair stays under it.
    if (!placementPreviewActive()) return;
    placeRot_ += clockwise ? -M_PI / 2.0 : M_PI / 2.0;
    // Kept in one turn, so the number reads as an angle rather than as a tally of key presses.
    while (placeRot_ > M_PI + 1e-9) placeRot_ -= 2.0 * M_PI;
    while (placeRot_ < -M_PI - 1e-9) placeRot_ += 2.0 * M_PI;
    const int deg = static_cast<int>(std::lround(placeRot_ * 180.0 / M_PI));
    emit message(QStringLiteral("Turned %1° — R turns it clockwise, Shift+R the other way")
                     .arg(deg > 0 ? QStringLiteral("+%1").arg(deg) : QString::number(deg)));
    emit overlayChanged();
}

void Editor::placeEntities(const std::vector<Entity>& ents, const std::optional<Vec2>& heldAt) {
    if (!doc_ || ents.empty()) {
        emit message(QStringLiteral("Nothing to place"));
        return;
    }
    clipboard_ = ents;
    clipStamp_ = QDateTime::currentMSecsSinceEpoch();
    Bounds b;
    for (const Entity& e : clipboard_) {
        const Bounds eb = e.bounds();
        if (eb.valid) { b.expand(eb.min); b.expand(eb.max); }
    }
    clipboardBase_ = heldAt ? *heldAt : (b.valid ? b.min : Vec2{0, 0});
    awaitClipBase_ = false;
    paste(); // the contents now follow the cursor until clicked into place
}

void Editor::appendEntities(const std::vector<Entity>& ents) {
    if (!doc_ || ents.empty()) return;
    pushUndo();
    QSet<QString> ids;
    for (const Entity& e : doc_->entities()) ids.insert(e.id);
    selected_.clear();
    // Whatever is being appended is a set in its own right - a paste, a block, a part's drawing - and the
    // references inside it are between ITS members. Anything given a fresh id here (because it was empty or
    // because that id is already on the sheet) is re-mapped so the set stays self-contained.
    std::vector<Entity> incoming;
    incoming.reserve(ents.size());
    QHash<QString, QString> oldToNew;
    for (Entity e : ents) {
        const QString was = e.id;
        if (e.id.isEmpty() || ids.contains(e.id)) e.id = uid();
        if (!was.isEmpty() && was != e.id) oldToNew.insert(was, e.id);
        ids.insert(e.id);
        incoming.push_back(std::move(e));
    }
    if (!oldToNew.isEmpty()) {
        // Only the ones that were renamed: a reference to something in the set that KEPT its id still
        // resolves, and one to something outside the set is left alone here - appending is not copying.
        for (Entity& e : incoming) {
            for (const QString& key : entityRefKeys()) {
                const QString was = e.meta.value(key).toString();
                const auto found = oldToNew.constFind(was);
                if (!was.isEmpty() && found != oldToNew.constEnd()) e.meta.insert(key, *found);
            }
            for (const QString& key : entityRefListKeys()) {
                if (!e.meta.contains(key)) continue;
                QJsonArray now;
                for (const QJsonValue& v : e.meta.value(key).toArray()) {
                    const QString id = v.toString();
                    const auto found = oldToNew.constFind(id);
                    now.append(found != oldToNew.constEnd() ? *found : id);
                }
                e.meta.insert(key, now);
            }
        }
    }
    for (Entity& e : incoming) {
        selected_.push_back(e.id);
        doc_->entities().push_back(std::move(e));
    }
    doc_->setDirty(true);
    emit selectionChanged();
    emit changed();
    emit message(QStringLiteral("Imported %1 object(s)").arg(ents.size()));
}

std::vector<Entity> Editor::copyOfSelection() const {
    std::vector<Entity> out;
    if (!doc_) return out;
    for (const QString& id : selected_)
        for (const auto& e : doc_->entities())
            if (e.id == id) { out.push_back(e); break; }
    return out;
}

void Editor::paste() {
    if (!doc_ || clipboard_.empty()) {
        emit message(QStringLiteral("Clipboard is empty"));
        return;
    }
    awaitClipBase_ = false;
    placeRot_ = 0.0;      // something new picked up starts the way it was drawn
    pastePlacing_ = true; // the contents now follow the cursor until clicked to place
    pasteMirrorX_ = pasteMirrorY_ = false;
    emit message(QStringLiteral("Specify insertion point — X / Y mirror before placing"));
    emit overlayChanged();
}

int Editor::cutNetsUnderSymbols(const std::vector<QString>& placedIds) {
    if (!doc_ || placedIds.empty()) return 0;
    // The two leads of each symbol that was just put down. A symbol's entities share one key, so several
    // symbols pasted at once are still told apart - and the tip of a lead is its `a`, which has been turned
    // and mirrored with the rest of the drawing.
    struct Pins { std::optional<Vec2> one, two; };
    QHash<QString, Pins> bySymbol;
    QSet<QString> placed;
    for (const QString& id : placedIds) placed.insert(id);
    for (const Entity& e : doc_->entities()) {
        if (!placed.contains(e.id)) continue;
        const QString inst = e.meta.value(QLatin1String(kPartSymbolInstKey)).toString();
        const QString pin = e.meta.value(QLatin1String(kPartSymbolPinKey)).toString();
        if (inst.isEmpty() || pin.isEmpty()) continue;
        Pins& p = bySymbol[inst];
        if (pin == QLatin1String("1")) p.one = e.a;
        else if (pin == QLatin1String("2")) p.two = e.a;
    }
    if (bySymbol.isEmpty()) return 0;

    const double tol = std::max(1e-6, worldTolerance());
    int cuts = 0;
    for (auto it = bySymbol.cbegin(); it != bySymbol.cend(); ++it) {
        const Pins& pins = it.value();
        if (!pins.one || !pins.two) continue;
        // Every wire or line the symbol landed on with BOTH leads. Both, because that is what "the same net
        // between the connection points" means: a symbol touching a net with one lead is drawn beside it, not
        // in it, and cutting anything there would be inventing an intention.
        std::vector<Entity> added;
        std::vector<QString> gone;
        for (Entity& e : doc_->entities()) {
            if (placed.contains(e.id)) continue;   // itself, and anything else just put down
            if (e.type != EntityType::Line && e.type != EntityType::Polyline && e.type != EntityType::Wire)
                continue;
            std::vector<std::vector<Vec2>> pieces;
            if (!cutPathBetweenPoints(e, *pins.one, *pins.two, tol, pieces)) continue;
            ++cuts;
            // The first piece stays in place, keeping the wire's own id, layer and everything written on it;
            // any second piece is the same wire again, cut. Nothing left means the symbol spans the whole of
            // it, and then the wire goes: the symbol is the connection now.
            if (pieces.empty()) {
                gone.push_back(e.id);
                continue;
            }
            auto shape = [](Entity& target, const std::vector<Vec2>& pts) {
                if (target.type == EntityType::Line) {
                    target.a = pts.front();
                    target.b = pts.back();
                } else {
                    target.points = pts;
                }
            };
            if (pieces.size() > 1) {
                Entity second = e;
                second.id = uid();
                shape(second, pieces[1]);
                added.push_back(std::move(second));
            }
            shape(e, pieces[0]);
        }
        if (!gone.empty()) {
            auto& ents = doc_->entities();
            ents.erase(std::remove_if(ents.begin(), ents.end(),
                                      [&gone](const Entity& e) {
                                          return std::find(gone.begin(), gone.end(), e.id) != gone.end();
                                      }),
                       ents.end());
        }
        for (Entity& e : added) doc_->entities().push_back(std::move(e));
    }
    return cuts;
}

void Editor::togglePasteMirror(bool xAxis) {
    if (!pastePlacing_) return;
    if (xAxis) pasteMirrorX_ = !pasteMirrorX_;
    else pasteMirrorY_ = !pasteMirrorY_;
    emit message(xAxis ? QStringLiteral("Paste mirrored across the Y axis (X to flip back)")
                       : QStringLiteral("Paste mirrored across the X axis (Y to flip back)"));
    emit overlayChanged();
}

void Editor::undo() {
    if (!doc_ || !undo_.canUndo()) return;
    doc_->entities() = undo_.undo(doc_->entities());
    selected_.clear();
    doc_->setDirty(true);
    emit selectionChanged();
    emit changed();
}

void Editor::redo() {
    if (!doc_ || !undo_.canRedo()) return;
    doc_->entities() = undo_.redo(doc_->entities());
    selected_.clear();
    doc_->setDirty(true);
    emit selectionChanged();
    emit changed();
}

bool Editor::onKey(int key, Qt::KeyboardModifiers mods) {
    const bool ctrl = mods & Qt::ControlModifier;
    // Esc gives up on a rectangle being picked, and tells whoever asked for it so that it can put itself
    // back together (the page setup reopens either way).
    if (rectPick_ && key == Qt::Key_Escape) {
        auto cancel = rectPickCancel_;
        endRectanglePick();
        emit message(QStringLiteral("Cancelled"));
        if (cancel) cancel();
        return true;
    }
    // Ctrl while drawing an arc flips it to the opposite (mirrored) sweep, AutoCAD-
    // style. A bare Ctrl press arrives as Key_Control.
    if (key == Qt::Key_Control && tool_ == ToolId::Arc && drafting_) {
        toggleArcMirror();
        return true;
    }
    // And Ctrl while drawing a DOUBLE run swaps which side of the drawn line the second line goes.
    // The run is drawn on the line under the crosshair; which side its partner wants to be on depends
    // on what the run is being drawn against, and that is only obvious once it is on screen. Allowed
    // before the first click too, so the side can be set before starting.
    if (key == Qt::Key_Control &&
        (tool_ == ToolId::DoubleLine || tool_ == ToolId::DoublePolyline)) {
        dpolySwap_ = !dpolySwap_;
        emit message(dpolySwap_
                         ? QStringLiteral("Second line ABOVE the line you draw — Ctrl to swap")
                         : QStringLiteral("Second line BELOW the line you draw — Ctrl to swap"));
        emit overlayChanged();
        return true;
    }
    if (key == Qt::Key_Escape) {
        // Cancel any in-progress operation and clear the whole selection.
        // (Trim finishes here too: setTool(Select) below clears the reference/cut
        // line, so Esc ends the trim command.)
        // A grabbed joint that already moved is restored via undo.
        if (editing_ && editMoved_) undo();
        // A harness waiting to be placed is abandoned, and the builder comes back so the
        // definition is never lost.
        if (harnessPlacing_) {
            cancelHarnessPlacement();
            return true;
        }
        // An un-placed array is abandoned, keeping the selection so it can be re-armed.
        if (arrayActive_) {
            cancelArray();
            return true;
        }
        // Quick Select waiting for its sample objects: drop the command, keep the pick.
        if (awaitQuickSelect_) {
            cancelQuickSelect();
            return true;
        }
        // A twist in progress (armed / live preview / frozen): Esc CANCELS the twist function
        // and returns to Select — the Schematic ribbon menu comes back (setTool clears
        // twistArmType_ and emits twistArmChanged, which restores the hidden ribbon groups).
        // A break being marked out: Esc gives up the line and the tool, like a twist.
        if (tool_ == ToolId::LengthBreak) {
            wireBandHasStart_ = false;
            breakFrozen_ = false;
            clearSelection();
            setTool(ToolId::Select);
            emit message(QStringLiteral("Break cancelled"));
            emit changed();
            return true;
        }
        if (!twistArmType_.isEmpty()) {
            wireBandHasStart_ = false;
            twistFrozen_ = false;
            clearSelection();
            setTool(ToolId::Select);
            emit message(QStringLiteral("Twist cancelled"));
            emit changed();
            return true;
        }
        // Wire session: while a wire function is in progress (drawing / twist / screen /
        // cross-switch), Esc ends that function and RETURNS TO SELECT. A wire draft with
        // at least 2 confirmed points is COMMITTED as-is (keeping whatever was already
        // placed) rather than thrown away; Esc only cancels the pending next segment.
        // Fewer points (nothing to keep yet) cancels outright, as does an in-progress
        // Twist/Screen/Cross-switch pick.
        if (tool_ == ToolId::Wire &&
            (drafting_ || !twistArmType_.isEmpty() || screenArm_ || awaitCrossSwitch_)) {
            if (drafting_ && draftPts_.size() >= 2) finishDraw();
            else cancelDraw();
            twistArmType_.clear();
            const bool wasScreenArmed = screenArm_;
            screenArm_ = false;
            screenFrozen_ = false;
            awaitCrossSwitch_ = false;
            wireBandHasStart_ = false;
            editing_ = false;
            gripGrab_ = false;
            editMoved_ = false;
            modifyPts_.clear();
            clearSelection();
            setTool(ToolId::Select); // leave the Wire function, back to region-select
            emit message(QStringLiteral("Select"));
            emit changed();
            if (wasScreenArmed) emit screenArmedChanged();
            return true;
        }
        cancelRightDrag();
        cancelDraw();
        modifyPts_.clear();
        pickIds_.clear();
        pickPts_.clear();
        pressed_ = false;
        editing_ = false;
        gripGrab_ = false;
        editMoved_ = false;
        gapInputMode_ = false;
        filletInputMode_ = false;
        awaitClipBase_ = false;
        pastePlacing_ = false;
        placeRot_ = 0.0;
        clipCut_ = false;
        cutPending_.clear();
        editGhost_.clear();
        regionMode_ = RegionMode::None;
        dragPath_.clear();
        if (dragTimer_) dragTimer_->stop();
        clearSelection();
        setTool(ToolId::Select);
        emit changed();
        return true;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) {
        // While a value is being awaited (fillet radius / gap), a stray Enter must
        // NOT finish the draft — just consume it.
        if (filletInputMode_ || gapInputMode_) return true;
        // Enter places the previewed array (AutoCAD's "Close Array").
        if (arrayActive_) {
            commitArray();
            return true;
        }
        // Quick Select armed: Enter finishes picking the sample objects and opens the
        // filter dialog on them.
        if (awaitQuickSelect_) {
            if (selected_.empty()) {
                emit message(QStringLiteral("Quick Select: pick at least one object first, "
                                            "or Esc to cancel"));
                return true;
            }
            openQuickSelect();
            return true;
        }
        // A frozen shield preview (Tab): Enter places it at the frozen crossing line.
        if (screenArm_ && screenFrozen_ && wireBandHasStart_) {
            wireBandHasStart_ = false;
            finishWireBand(screenFrozenEnd_);
            return true;
        }
        // A frozen twist preview (Tab): Enter places it at the frozen crossing line.
        if (!twistArmType_.isEmpty() && twistFrozen_ && wireBandHasStart_) {
            wireBandHasStart_ = false;
            finishWireBand(twistFrozenEnd_);
            return true;
        }
        // A twisted bundle with only its start down - pinned or not - is finished by Enter, taking
        // the end from where it was pinned or from the cursor.
        if (isTwistedTool(tool_) && drafting_ && draftPts_.size() == 1) {
            draftPts_.push_back(twistedFrozen_ ? twistedFrozenAt_ : cursor_);
            twistedFrozen_ = false;
            finishDraw();
            return true;
        }
        // Finish an in-progress multi-point draw; otherwise repeat the last tool.
        if (drafting_) {
            // A line with only its start down: Enter gives up on it and hands back the pointer, which
            // is where the tool leaves you once a line IS placed.
            if (tool_ == ToolId::Line) {
                draftPts_.clear();
                drafting_ = false;
                setTool(ToolId::Select);
                emit changed();
                return true;
            }
            finishDraw();
            return true;
        }
        if (regionMode_ == RegionMode::None && !editing_ && modifyPts_.empty()) {
            repeatLastTool();
            return true;
        }
    }
    if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
        deleteSelected();
        return true;
    }
    const bool shift = mods & Qt::ShiftModifier;
    if (ctrl && key == Qt::Key_A) { selectAll(); return true; }
    if (ctrl && shift && key == Qt::Key_C) { copyWithBase(false); return true; } // copy w/ base
    if (ctrl && key == Qt::Key_C) { copySelection(); return true; }
    if (ctrl && key == Qt::Key_X) { cutSelection(); return true; }               // cut to clipboard
    if (ctrl && key == Qt::Key_V) {
        // The window may have something better to paste than what the editor is holding - a picture on
        // the system clipboard, say. When nobody is listening (the editor driven on its own), paste
        // what the editor has.
        if (receivers(SIGNAL(pasteRequested())) > 0) emit pasteRequested();
        else paste();
        return true;
    }
    if (ctrl && key == Qt::Key_Z) { undo(); return true; }
    if (ctrl && (key == Qt::Key_Y)) { redo(); return true; }
    return false;
}

// ---- Overlay painting ------------------------------------------------------

void Editor::drawOverlay(QPainter& p, const ViewTransform& vt) const {
    if (rectPick_ && rectPickHaveFirst_) {
        // The window as it stands: from the corner already picked to the snapped cursor.
        const QPointF a = vt.toScreen(rectPickFirst_);
        const QPointF b = vt.toScreen(cursor_);
        const QRectF box = QRectF(a, b).normalized();
        QPen pen(paperInk(QColor(120, 200, 255, 220)));
        pen.setCosmetic(true);
        pen.setWidthF(1.4);
        pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.setBrush(paperInk(QColor(120, 200, 255, 40)));
        p.drawRect(box);
        p.setBrush(Qt::NoBrush);
    }
    if (!doc_) return;

    // The break mark that a click would place, drawn where it would go: sized to the run under the
    // cursor and lying square across it, so it can be judged before it is committed rather than placed
    // twice. Tab pins it (breakFrozen_) so the numbers on the ribbon can be tried on the real thing
    // without the cursor moving it.
    if (tool_ == ToolId::LengthBreak) {
        // The marking line itself, dashed, while it is being drawn: it is what says which runs are
        // being broken, so it has to be visible as it is drawn.
        if (wireBandHasStart_) {
            p.save();
            QPen bandPen(paperInk(QColor(120, 200, 255, breakFrozen_ ? 235 : 170)));
            bandPen.setCosmetic(true);
            bandPen.setWidthF(1.2);
            bandPen.setStyle(Qt::DashLine);
            p.setPen(bandPen);
            p.drawLine(vt.toScreen(wireBandStart_), vt.toScreen(breakBandEnd()));
            p.restore();
        }
        const Entity ghost = breakPreview();
        if (ghost.type == EntityType::LengthBreak) {
            p.save();
            QPen pv(paperInk(QColor(120, 200, 255, breakFrozen_ ? 235 : 190)));
            pv.setCosmetic(true);
            pv.setWidthF(breakFrozen_ ? 1.8 : 1.4);
            if (!breakFrozen_) pv.setStyle(Qt::DashLine);
            p.setPen(pv);
            p.setBrush(Qt::NoBrush);
            for (const std::vector<Vec2>& stroke : breakStrokes(ghost)) {
                QPolygonF poly;
                for (const Vec2& q : stroke) poly << vt.toScreen(q);
                p.drawPolyline(poly);
            }
            p.restore();
        }
    }

    // Twist / Screen: preview the crossing line, and (for Screen) a ghost ellipse + drain
    // leg over the wires it currently crosses, so the shield can be judged before
    // committing. Once frozen (Tab), the END is pinned (screenFrozenEnd_) so ribbon
    // fields can be tuned without the drag changing which wires are caught.
    if ((!twistArmType_.isEmpty() || screenArm_) && wireBandHasStart_) {
        const Vec2 bandEnd = (screenArm_ && screenFrozen_)          ? screenFrozenEnd_
                             : (!twistArmType_.isEmpty() && twistFrozen_) ? twistFrozenEnd_
                                                                          : cursor_;
        p.save();
        QPen pv(paperInk(QColor(120, 200, 255, 200)));
        pv.setCosmetic(true);
        pv.setWidthF(1.4);
        pv.setStyle(Qt::DashLine);
        p.setPen(pv);
        p.setBrush(Qt::NoBrush);
        p.drawLine(vt.toScreen(wireBandStart_), vt.toScreen(bandEnd));
        if (screenArm_) {
            std::vector<Vec2> xs;
            std::vector<Vec2> segDirs;
            for (const auto& w : doc_->entities()) {
                if (w.type != EntityType::Wire || w.points.size() < 2) continue;
                for (size_t i = 1; i < w.points.size(); ++i)
                    if (auto x = segmentSegmentIntersection(wireBandStart_, bandEnd,
                                                            w.points[i - 1], w.points[i])) {
                        xs.push_back(*x);
                        segDirs.push_back(w.points[i] - w.points[i - 1]);
                        break;
                    }
            }
            if (!xs.empty()) {
                // Orient PERPENDICULAR to the wires (crosses them at 90°); centre + span
                // from the outermost crossings so the ghost spans EVERY caught wire.
                const double rot = bundlePerpRot(segDirs);
                const Vec2 major{std::cos(rot), std::sin(rot)};
                const Vec2 minor{-std::sin(rot), std::cos(rot)};
                Vec2 center;
                double span;
                shieldExtent(xs, major, center, span);
                const double cx = center.x, cyMid = center.y;
                double rx, ry;
                shieldAutoSize(span, shieldWireGap(xs, major), rx, ry);
                const bool capsule = screenShape_ == QLatin1String("capsule");
                const int amount = std::clamp(screenAmount_, 1, 5);
                const double spacing = 3.0 * rx; // ellipse width + one-radius gap
                // Preview the actual outline style: dashed (with the dash scale) or solid,
                // so a dashed shield reads as dashed before it's placed. The leg stays solid.
                QPen outlinePen(paperInk(QColor(120, 200, 255, 220)));
                outlinePen.setCosmetic(true);
                outlinePen.setWidthF(1.6);
                if (screenLineType_ == QLatin1String("dashed")) {
                    const double ds = std::max(0.1, screenDashScale_);
                    outlinePen.setDashPattern({6.0 * ds / 1.6, 4.0 * ds / 1.6});
                }
                QPen legPen(outlinePen);
                legPen.setStyle(Qt::SolidLine);
                for (int k = 0; k < amount; ++k) {
                    const Vec2 ctr{cx + k * spacing * minor.x, cyMid + k * spacing * minor.y};
                    QPolygonF poly;
                    for (const Vec2& w : EntityRenderer::shieldOutline(ctr, rx, ry, rot, capsule))
                        poly << vt.toScreen(w);
                    p.setPen(outlinePen);
                    p.drawPolyline(poly);
                    p.setPen(legPen);
                    Vec2 edge, drain;
                    if (screenLegDir_ == QLatin1String("top")) {
                        edge = {ctr.x + ry * major.x, ctr.y + ry * major.y};
                        drain = {ctr.x + (ry + screenLeg_) * major.x, ctr.y + (ry + screenLeg_) * major.y};
                    } else if (screenLegDir_ == QLatin1String("left")) {
                        edge = {ctr.x + rx * minor.x, ctr.y + rx * minor.y};
                        drain = {ctr.x + (rx + screenLeg_) * minor.x, ctr.y + (rx + screenLeg_) * minor.y};
                    } else if (screenLegDir_ == QLatin1String("right")) {
                        edge = {ctr.x - rx * minor.x, ctr.y - rx * minor.y};
                        drain = {ctr.x - (rx + screenLeg_) * minor.x, ctr.y - (rx + screenLeg_) * minor.y};
                    } else {
                        edge = {ctr.x - ry * major.x, ctr.y - ry * major.y};
                        drain = {ctr.x - (ry + screenLeg_) * major.x, ctr.y - (ry + screenLeg_) * major.y};
                    }
                    p.drawLine(vt.toScreen(edge), vt.toScreen(drain));
                }
            }
        } else if (!twistArmType_.isEmpty()) {
            // Twist preview: the actual twist symbol over the wires the crossing line
            // currently catches, so it can be judged before committing.
            std::vector<Vec2> xs;
            QString previewGauge;
            for (const auto& w : doc_->entities()) {
                if (w.type != EntityType::Wire || w.points.size() < 2) continue;
                for (size_t i = 1; i < w.points.size(); ++i)
                    if (auto x = segmentSegmentIntersection(wireBandStart_, bandEnd,
                                                            w.points[i - 1], w.points[i])) {
                        xs.push_back(*x);
                        if (previewGauge.isEmpty())
                            previewGauge = w.meta.value("wireGauge").toString();
                        break;
                    }
            }
            if (xs.size() >= 2) {
                // Preview `amount` marks side by side, exactly as finishWireBand will place
                // them (same band-derived spacing), so a multi-twist choice reads correctly.
                const int amount = std::clamp(twistAmount_, 1, 9);
                double yMin = 1e18, yMax = -1e18;
                for (const Vec2& c : xs) { yMin = std::min(yMin, c.y); yMax = std::max(yMax, c.y); }
                const double band = std::max(1.0, yMax - yMin);
                const double spacing = twistMarkSpacing(band);
                for (int k = 0; k < amount; ++k) {
                    Entity temp;
                    temp.type = EntityType::Twist;
                    temp.points.reserve(xs.size());
                    for (const Vec2& c : xs) temp.points.push_back({c.x + k * spacing, c.y});
                    temp.meta.insert("twistType", twistArmType_);
                    temp.meta.insert("wireGauge", previewGauge); // preview at wire thickness
                    if (twistArmType_ == QLatin1String("crossed")) {
                        temp.meta.insert("diagAngle", twistDiagAngle_);
                        temp.meta.insert("capLen", twistCapLen_);
                    } else {
                        temp.meta.insert("repeats", twistRepeats_);
                        if (twistArmType_ != QLatin1String("type3"))
                            temp.meta.insert("flatLen", twistFlatLen_);
                    }
                    EntityRenderer::draw(p, temp, *doc_, vt, paperInk(QColor(120, 200, 255, 220)));
                }
            }
        }
        p.restore();
    }

    // Locate-highlight: a bright ring flashed at a discrepancy the user clicked in the
    // validation dialog, so the jumped-to object is easy to spot.
    if (flashOn_) {
        const QPointF c = vt.toScreen(flashPt_);
        p.save();
        QPen fp(paperInk(QColor(255, 190, 40)));
        fp.setCosmetic(true);
        fp.setWidthF(2.6);
        p.setPen(fp);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, 15.0, 15.0);
        p.drawEllipse(c, 26.0, 26.0);
        p.restore();
    }

    // Right-button drag: preview the selection mirrored (X/Y) about the catch point
    // and translated to the current offset.
    if (rightDrag_) {
        const QColor ghost = paperInk(paperInk(QColor(120, 200, 255, 160)));
        for (const Entity& g : rightDragGhost_) {
            Entity e = rightDragResult(g);
            EntityRenderer::draw(p, e, *doc_, vt, ghost);
        }
    }

    // Polar tracking guide: when the cursor has locked onto a polar angle, draw a
    // bright green ray FROM the reference point TOWARD the drawing direction,
    // continuing to the canvas edge. The reference is the draft anchor while
    // drawing, or the untouched joint point while moving a joint.
    if (snapResult_.snapped && snapResult_.kind == SnapKind::Polar) {
        std::optional<Vec2> ref;
        if (drafting_ && !draftPts_.empty()) ref = draftPts_.back();
        else if (editing_ && editMode_ == 1) ref = editAnchor_;
        else if (!modifyPts_.empty()) ref = modifyPts_.back(); // during a modify command
        if (ref) {
            const QPointF as = vt.toScreen(*ref);
            const QPointF cs = vt.toScreen(cursor_);
            QPointF dir = cs - as;
            const double L = std::hypot(dir.x(), dir.y());
            if (L > 1e-6) {
                dir /= L;
                constexpr double kFar = 6000.0; // beyond any canvas; Qt clips it
                QPen gp(paperInk(QColor("#19ff7e"))); // bright solid green polar lock line
                gp.setCosmetic(true);
                gp.setWidthF(1.2);
                p.setPen(gp);
                p.setBrush(Qt::NoBrush);
                // From the reference point toward (and past) the cursor.
                p.drawLine(as, as + dir * kFar);
            }
        }
    }

    // Paste placement: ghost the clipboard contents so its base point follows the
    // cursor, with a small cross at the insertion point.
    if (pastePlacing_ && !clipboard_.empty()) {
        const Vec2 off = cursor_ - clipboardBase_;
        const QColor ghost = paperInk(paperInk(QColor(120, 200, 255, 170)));
        for (Entity e : clipboard_) {
            if (pasteMirrorX_) mirrorEntity(e, clipboardBase_, {clipboardBase_.x, clipboardBase_.y + 1});
            if (pasteMirrorY_) mirrorEntity(e, clipboardBase_, {clipboardBase_.x + 1, clipboardBase_.y});
            if (std::abs(placeRot_) > 1e-9) rotateEntity(e, clipboardBase_, placeRot_);
            translateEntity(e, off);
            EntityRenderer::draw(p, e, *doc_, vt, ghost);
        }
        const QPointF c = vt.toScreen(cursor_);
        QPen mp(paperInk(QColor("#ffd166")));
        mp.setCosmetic(true);
        p.setPen(mp);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(c.x() - 7, c.y()), QPointF(c.x() + 7, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - 7), QPointF(c.x(), c.y() + 7));
    }

    // A generated harness waiting to be placed: ghost the whole thing under the cursor so it
    // can be positioned before it lands, with a marker on the corner it is held by.
    if (harnessPlacing_ && !harnessGhost_.empty()) {
        const Vec2 shift = cursor_ - harnessBase_;
        const QColor ghost = paperInk(paperInk(QColor(120, 200, 255, 170)));
        for (Entity e : harnessGhost_) {
            if (std::abs(placeRot_) > 1e-9) rotateEntity(e, harnessBase_, placeRot_);
            translateEntity(e, shift);
            EntityRenderer::draw(p, e, *doc_, vt, ghost);
        }
        const QPointF c = vt.toScreen(cursor_);
        QPen mp(paperInk(QColor("#ffd166")));
        mp.setCosmetic(true);
        p.setPen(mp);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(c.x() - 8, c.y()), QPointF(c.x() + 8, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - 8), QPointF(c.x(), c.y() + 8));
    }

    // Array session: ghost every item of the current spec, plus the construction
    // helpers for the active kind (the grid axes, the polar centre + orbit, or the
    // picked path), so the whole layout can be judged before it is placed.
    if (arrayActive_ && !arraySrc_.empty()) {
        const QColor ghost = paperInk(paperInk(QColor(120, 200, 255, 150)));
        const QColor guide = paperInk(paperInk(QColor(255, 209, 102, 190)));
        const auto places = arrayPreview();
        ArraySpec s = arraySpec_;
        if (s.kind == ArrayKind::Polar) s.center = arrayCenterInUse();
        const Vec2 pivot = arrayPivot(s, arrayBase_);

        p.save();
        QPen gp(guide);
        gp.setCosmetic(true);
        gp.setWidthF(1.2);
        gp.setStyle(Qt::DashLine);
        p.setPen(gp);
        p.setBrush(Qt::NoBrush);
        if (s.kind == ArrayKind::Rect) {
            // The two grid axes out of the base point (they show the array angle).
            const double a = s.axisAngle * M_PI / 180.0;
            const Vec2 colAxis{std::cos(a), std::sin(a)};
            const Vec2 rowAxis = colAxis.perp();
            const double colLen = s.colSpacing * std::max(0, s.count - 1);
            const double rowLen = s.rowSpacing * std::max(0, s.rows - 1);
            p.drawLine(vt.toScreen(arrayBase_), vt.toScreen(arrayBase_ + colAxis * colLen));
            p.drawLine(vt.toScreen(arrayBase_), vt.toScreen(arrayBase_ + rowAxis * rowLen));
        } else if (s.kind == ArrayKind::Polar) {
            // The orbit the items ride on, and a spoke out to the source item.
            const double r = distance(arrayBase_, s.center);
            const QPointF c = vt.toScreen(s.center);
            if (r > 1e-9) {
                const double rs = r * vt.scale();
                p.drawEllipse(c, rs, rs);
                p.drawLine(c, vt.toScreen(arrayBase_));
            }
            QPen cp(guide);
            cp.setCosmetic(true);
            cp.setWidthF(1.6);
            p.setPen(cp);
            p.drawLine(QPointF(c.x() - 8, c.y()), QPointF(c.x() + 8, c.y()));
            p.drawLine(QPointF(c.x(), c.y() - 8), QPointF(c.x(), c.y() + 8));
        } else if (arrayPathPts_.size() >= 2) {
            QPolygonF poly;
            for (const Vec2& v : arrayPathPts_) poly << vt.toScreen(v);
            p.drawPolyline(poly);
        }

        // Ghost the items. A very large array would cost more to preview than to
        // place, so past a cap the remaining items are marked with a dot instead.
        constexpr int kMaxGhostEntities = 1500;
        const int perItem = std::max(1, static_cast<int>(arraySrc_.size()));
        const int ghostItems = std::max(1, kMaxGhostEntities / perItem);
        for (size_t i = 0; i < places.size(); ++i) {
            if (static_cast<int>(i) < ghostItems) {
                for (const Entity& src : arraySrc_) {
                    Entity item = arrayItem(src, places[i], pivot);
                    EntityRenderer::draw(p, item, *doc_, vt, ghost);
                }
            } else {
                // Where this item's base point lands (rotation about the pivot, then
                // the offset) — the same transform the item itself would get.
                const double cs = std::cos(places[i].rotate), sn = std::sin(places[i].rotate);
                const Vec2 d = arrayBase_ - pivot;
                const Vec2 at{pivot.x + d.x * cs - d.y * sn + places[i].delta.x,
                              pivot.y + d.x * sn + d.y * cs + places[i].delta.y};
                p.setPen(QPen(ghost));
                p.drawEllipse(vt.toScreen(at), 2.0, 2.0);
            }
        }
        p.restore();
    }

    // Region select: rectangle (Deciding / RectPending) or freeform lasso, with
    // Edraw's exact colors. Window (left->right): blue, solid, fully enclose.
    // Crossing (right->left): green, dashed, touch.
    const bool isLasso = regionMode_ == RegionMode::Lasso;
    const bool isRect = regionMode_ == RegionMode::Deciding ||
                        regionMode_ == RegionMode::RectPending;
    if (isLasso || isRect) {
        const bool windowMode = isLasso ? lassoWindowMode_ : (cursor_.x >= anchor_.x);
        const QColor fill = windowMode ? QColor(55, 125, 220, 90) : QColor(22, 150, 72, 90);
        const QColor line = windowMode ? paperInk(QColor("#cfe3ff")) : paperInk(QColor("#d9ffe6"));
        QPen boxPen(line);
        boxPen.setCosmetic(true);
        boxPen.setWidthF(1.2);
        boxPen.setStyle(windowMode ? Qt::SolidLine : Qt::DashLine);
        p.setPen(boxPen);
        p.setBrush(fill);
        if (isLasso) {
            QPolygonF poly;
            for (const auto& v : dragPath_) poly << vt.toScreen(v);
            p.drawPolygon(poly);
        } else {
            p.drawRect(QRectF(vt.toScreen(anchor_), vt.toScreen(cursor_)).normalized());
        }
        p.setBrush(Qt::NoBrush);
    }

    // Countdown badge near the cursor: time left before the hold becomes a lasso.
    if (pressed_ && regionMode_ == RegionMode::Deciding && tool_ == ToolId::Select) {
        const qint64 remaining = kLassoDelayMs - pressTimer_.elapsed();
        if (remaining > 0) {
            const QPointF c = vt.toScreen(cursor_);
            const QString label = QStringLiteral("%1s").arg(remaining / 1000.0, 0, 'f', 2);
            QFont f = p.font();
            f.setPixelSize(12);
            p.setFont(f);
            const QRectF box(c.x() + 10, c.y() - 30, 42, 18);
            p.setPen(QPen(paperInk(QColor("#9ec8ff"))));
            p.setBrush(QColor("#101720"));
            p.drawRect(box);
            p.setPen(QPen(paperInk(QColor("#dcecff"))));
            p.drawText(box, Qt::AlignCenter, label);
            p.setBrush(Qt::NoBrush);
        }
    }

    // Source ghost: while a move / joint drag is in progress, show the entities at
    // their ORIGINAL position (faint) so it's clear where they were taken from,
    // until the placement click commits the new position.
    if (editing_ && !editGhost_.empty()) {
        const QColor ghost = paperInk(paperInk(QColor(150, 162, 178, 110)));
        for (const auto& e : editGhost_)
            EntityRenderer::draw(p, e, *doc_, vt, ghost);
    }

    // Selected table CELL: a filled highlight over just that cell.
    if (!cellTableId_.isEmpty() && cellIndex_ >= 0) {
        for (const auto& e : doc_->entities()) {
            if (e.id != cellTableId_ || !e.meta.value("table").toBool()) continue;
            const Bounds b = boundsOf(e.points);
            const QJsonArray cf = e.meta.value("colFrac").toArray();
            const QJsonArray rf = e.meta.value("rowFrac").toArray();
            const int cols = cf.size();
            if (cols <= 0) break;
            const int col = cellIndex_ % cols, r = cellIndex_ / cols;
            double x0 = b.min.x;
            for (int c = 0; c < col && c < cf.size(); ++c) x0 += cf[c].toDouble() * b.width();
            const double cw = (col < cf.size() ? cf[col].toDouble() : 0) * b.width();
            double yTop = b.max.y;
            for (int i = 0; i < r && i < rf.size(); ++i) yTop -= rf[i].toDouble() * b.height();
            const double chh = (r < rf.size() ? rf[r].toDouble() : 0) * b.height();
            QRectF rr(vt.toScreen({x0, yTop}), vt.toScreen({x0 + cw, yTop - chh}));
            p.save();
            QPen cp(paperInk(QColor("#2bd0ff"))); cp.setCosmetic(true); cp.setWidthF(1.8);
            p.setPen(cp);
            p.setBrush(QColor(43, 208, 255, 60));
            p.drawRect(rr.normalized());
            p.restore();
            break;
        }
    }

    // The rest of a selected wire's BUNDLE, lit in a dimmer colour. They are not selected - an
    // edit still applies to the wire that was clicked - this only shows what travels with it.
    if (!bundleCompanions_.empty()) {
        // Orange: nothing else in the drawing uses it, so "these run in the same cable" cannot be
        // mistaken for "these are selected" (cyan) or for any layer colour.
        const QColor companion = paperInk(paperInk(QColor(255, 150, 40, 200)));
        for (const auto& e : doc_->entities())
            if (bundleCompanions_.contains(e.id))
                EntityRenderer::draw(p, e, *doc_, vt, companion);
    }

    // Table work is soft orange throughout - the table itself, its headers and the picked cells -
    // so it reads as "editing a table" rather than as an ordinary selection.
    // Cells picked in a table: a box round each. The table is selected too (the ribbon's cell tab
    // hangs off it), so these are drawn on top of its own highlight.
    if (!cellSel_.empty()) {
        const Entity* t = nullptr;
        for (const auto& x : doc_->entities())
            if (x.id == cellTableId_) { t = &x; break; }
        if (t) {
            const Bounds b = boundsOf(t->points);
            const QJsonArray cf = t->meta.value("colFrac").toArray();
            const QJsonArray rf = t->meta.value("rowFrac").toArray();
            const int cols = cf.size(), rows = rf.size();
            if (b.valid && cols > 0 && rows > 0) {
                p.save();
                p.setPen(QPen(kTableChrome, 1.5));
                p.setBrush(QColor(kTableChrome.red(), kTableChrome.green(), kTableChrome.blue(), 46));
                double top = b.max.y;
                for (int r = 0; r < rows; ++r) {
                    const double h = rf[r].toDouble() * b.height();
                    double x = b.min.x;
                    for (int c = 0; c < cols; ++c) {
                        const double w = cf[c].toDouble() * b.width();
                        if (std::find(cellSel_.begin(), cellSel_.end(), r * cols + c) !=
                            cellSel_.end()) {
                            const QPointF a = vt.toScreen({x, top});
                            const QPointF d = vt.toScreen({x + w, top - h});
                            p.drawRect(QRectF(a, d));
                        }
                        x += w;
                    }
                    top -= h;
                }
                p.restore();
            }
        }
    }

    // Markings picked on their own: a box round each, in the selection colour. The wire itself is
    // NOT selected, so nothing else can be edited by accident - only these can be slid along.
    if (!markSel_.empty()) {
        p.save();
        const QColor hi = paperInk(QColor("#2bd0ff"));
        for (const MarkPick& m : markSel_) {
            const Entity* w = nullptr;
            for (const auto& e : doc_->entities())
                if (e.id == m.wireId) { w = &e; break; }
            if (!w) continue;
            std::vector<Vec2> quad;
            if (!markQuad(*w, m.part, m.index, quad)) continue;
            QPolygonF poly;
            for (const Vec2& q : quad) poly << vt.toScreen(q);
            p.setPen(QPen(hi, markGrab_ ? 2.0 : 1.2, Qt::DashLine));
            p.setBrush(QColor(43, 208, 255, 30));
            p.drawPolygon(poly);
        }
        p.restore();
    }

    // Selection marking: highlight each selected entity's geometry + draw grips.
    if (!selected_.empty()) {
        const QColor hi = paperInk(QColor("#2bd0ff"));
        for (const auto& e : doc_->entities()) {
            if (std::find(selected_.begin(), selected_.end(), e.id) == selected_.end())
                continue;
            // A table is highlighted in the table colour, not the ordinary selection blue.
            const bool isTable = e.meta.value("table").toBool();
            EntityRenderer::draw(p, e, *doc_, vt, isTable ? kTableChrome : hi);
            drawGrips(p, e, vt);
            // A selected TABLE shows Excel-style headers: column letters (A,B,C…)
            // along the top and row numbers (1,2,3…) down the left, as editing chrome.
            if (e.type == EntityType::Rect && e.meta.value("table").toBool() &&
                e.points.size() >= 4) {
                const Bounds tb = boundsOf(e.points);
                const QJsonArray colFrac = e.meta.value("colFrac").toArray();
                const QJsonArray rowFrac = e.meta.value("rowFrac").toArray();
                p.save();
                QFont hf = p.font();
                hf.setPixelSize(11);
                hf.setBold(true);
                p.setFont(hf);
                p.setPen(kTableChrome.lighter(115));
                // Column letters, centred above each column.
                double x = tb.min.x;
                for (int c = 0; c < colFrac.size(); ++c) {
                    const double w = colFrac[c].toDouble() * tb.width();
                    const QPointF at = vt.toScreen({x + w / 2.0, tb.max.y});
                    const QString s(QChar('A' + std::min(c, 25)));
                    const QFontMetricsF fm(hf);
                    p.drawText(QPointF(at.x() - fm.horizontalAdvance(s) / 2.0, at.y() - 4), s);
                    x += w;
                }
                // Row numbers, at the left of each row.
                double y = tb.max.y;
                for (int r = 0; r < rowFrac.size(); ++r) {
                    const double h = rowFrac[r].toDouble() * tb.height();
                    const QPointF at = vt.toScreen({tb.min.x, y - h / 2.0});
                    p.drawText(QPointF(at.x() - 14, at.y() + 4), QString::number(r + 1));
                    y -= h;
                }
                p.restore();
            }
        }
    }

    // Connector placement: ghost the connector at the cursor until it's placed — but
    // only once armed (after "Place connector"), not merely when the tool is selected
    // for editing settings. X flips across the Y axis, Y across the X axis.
    if (tool_ == ToolId::Connector && connectorArmed_) {
        Entity ghost;
        ghost.type = EntityType::Connector;
        // Frozen (Tab): stay pinned; otherwise follow the cursor.
        ghost.a = connectorFrozen_ ? connectorFrozenPos_ : cursor_;
        ghost.layer = doc_->activeLayer();
        writeConnectorSpec(ghost.meta, connectorSpec_);
        EntityRenderer::draw(p, ghost, *doc_, vt, paperInk(QColor(120, 200, 255, 170)));
    }

    // In-progress draft preview (white, matching the line being drawn).
    if (drafting_ && !draftPts_.empty()) {
        QPen prev(paperInk(QColor("#ffffff")));
        prev.setCosmetic(true);
        prev.setWidthF(1.4);
        prev.setStyle(Qt::DashLine);
        p.setPen(prev);
        p.setBrush(Qt::NoBrush);

        std::vector<Vec2> pts = draftPts_;
        // Wire "90° only": the previewed segment snaps to a horizontal/vertical run even
        // though the crosshair (cursor_) tracks the mouse freely.
        if (tool_ == ToolId::Wire && wireOrtho_ && !draftPts_.empty())
            pts.push_back(orthoWirePoint(draftPts_.back(), cursor_));
        else
            pts.push_back(cursor_);
        // Preview the routing clearance too, so the run is shown exactly where it will be
        // placed rather than jumping aside on the click.
        if (tool_ == ToolId::Wire && draftPts_.size() >= 1 && pts.size() >= 2)
            pts.back() = applyWireClearance(pts[pts.size() - 2], pts.back());
        // Per-corner fillet radii for the live preview: placed corners keep their
        // stored radius; the corner now forming under the cursor previews with the
        // current (pending) radius. Parallels `pts`.
        std::vector<double> previewRadii = draftCornerRadii_;
        previewRadii.resize(draftPts_.size(), 0.0);
        if (!draftPts_.empty()) previewRadii.back() = cornerRadius_;
        previewRadii.push_back(0.0); // the cursor vertex is an endpoint
        const bool previewAnyFillet =
            std::any_of(previewRadii.begin(), previewRadii.end(), [](double r) { return r > 0.0; });
        // ls line-start: move a run's start onto the picked reference line.
        auto extendLsStart = [&](std::vector<Vec2>& run) {
            if (dpolyHasLsRef_ && run.size() >= 2)
                if (auto x = lineLineIntersection(run[0], run[1], lsRefA_, lsRefB_)) run[0] = *x;
        };
        // Shift-started run: show BOTH rails reaching the line the run was started on, the way they
        // will be placed. Only the first rail ever landed on it while drawing - the second one hung in
        // space, short of the drawing - because the cap was worked out on the click that ends the run
        // and never on the way there. The segment is chosen once, for both rails together, exactly as
        // capDoubleEndAt chooses it.
        std::optional<std::pair<Vec2, Vec2>> shiftStartSeg;
        if (drawStartShift_ && !draftPts_.empty() &&
            (tool_ == ToolId::DoubleLine || tool_ == ToolId::DoublePolyline)) {
            const std::vector<std::pair<Vec2, Vec2>> cands =
                cutSegmentsThrough(draftPts_.front(), doc_->entities(), QString());
            const double h = doubleLineGap_ / 2.0;
            // The two rail starts, and the direction each runs in.
            std::vector<std::pair<Vec2, Vec2>> rails;
            if (pts.size() >= 2) {
                const std::vector<Vec2> centre =
                    doubleRunCentre(pts, doubleLineGap_, doubleSecondBelow());
                for (double s : {h, -h}) {
                    const std::vector<Vec2> run = offsetPolyline(centre, s);
                    if (run.size() >= 2) rails.push_back({run.front(), run[1]});
                }
            }
            auto reach = [&rails](const std::pair<Vec2, Vec2>& seg) {
                int n = 0;
                for (const auto& r : rails)
                    if (railMeetsSegment(r.first, r.second, seg.first, seg.second)) ++n;
                return n;
            };
            for (const auto& c : cands)
                if (reach(c) == static_cast<int>(rails.size()) && !rails.empty()) {
                    shiftStartSeg = c;
                    break;
                }
            if (!shiftStartSeg)
                for (const auto& c : cands)
                    if (reach(c) > 0) { shiftStartSeg = c; break; }
        }
        auto capShiftStart = [&](std::vector<Vec2>& run) {
            if (!shiftStartSeg || run.size() < 2) return;
            if (auto x = railMeetsSegment(run[0], run[1], shiftStartSeg->first,
                                          shiftStartSeg->second))
                run[0] = *x;
        };
        if ((tool_ == ToolId::Rect || tool_ == ToolId::MText || tool_ == ToolId::Note ||
             tool_ == ToolId::Revision) &&
            pts.size() >= 2) {
            // Rectangle (and the MTEXT container box) rubber-band.
            const Vec2 a = pts[0], b = pts[1];
            QPolygonF poly;
            for (const Vec2& v : {Vec2{a.x, a.y}, Vec2{b.x, a.y}, Vec2{b.x, b.y}, Vec2{a.x, b.y}})
                poly << vt.toScreen(v);
            p.drawPolygon(poly);
        } else if (tool_ == ToolId::Polygon && pts.size() >= 2) {
            QPolygonF poly;
            for (const Vec2& v : regularPolygonPoints(pts[0], pts[1], polygonSides_))
                poly << vt.toScreen(v);
            p.drawPolygon(poly);
        } else if ((tool_ == ToolId::Balloon || tool_ == ToolId::TextLeader) && pts.size() >= 2) {
            // Preview the leader from the arrow tip to the cursor; a Balloon also shows
            // its circle (a text leader shows just the leader).
            p.drawLine(vt.toScreen(pts[0]), vt.toScreen(pts[1]));
            if (tool_ == ToolId::Balloon) {
                const QPointF cc = vt.toScreen(pts[1]);
                const double rr = 6.0 * vt.scale();
                p.drawEllipse(cc, rr, rr);
            }
        } else if (tool_ == ToolId::Hatch || tool_ == ToolId::Wipeout) {
            // Closed boundary rubber-band with a faint fill preview.
            QPolygonF poly;
            for (const auto& v : pts) poly << vt.toScreen(v);
            if (poly.size() >= 2) {
                p.save();
                p.setBrush(tool_ == ToolId::Hatch ? QColor(80, 160, 255, 55)
                                                  : QColor(40, 48, 60, 150));
                p.drawPolygon(poly); // auto-closes the outline
                p.restore();
            }
        } else if (tool_ == ToolId::Dimension) {
            // Live dimension preview that follows the cursor "all the way" — the
            // dimension-line height (linear/aligned), the placed text (radius/dia),
            // the second ray (angular) or the leader (ordinate).
            Entity temp;
            temp.type = EntityType::Dimension;
            temp.meta.insert("dimType", QString::fromLatin1(dimTypeString(dimMode_)));
            bool ok = false;
            switch (dimMode_) {
            case DimMode::Linear:
            case DimMode::Aligned:
                if (draftPts_.size() >= 2) {
                    temp.a = draftPts_[0];
                    temp.b = draftPts_[1];
                    temp.points = {draftPts_[0], draftPts_[1], cursor_};
                    ok = true;
                } else {
                    p.drawLine(vt.toScreen(draftPts_[0]), vt.toScreen(cursor_));
                }
                break;
            case DimMode::Angular:
                if (draftPts_.size() >= 2) {
                    temp.points = {draftPts_[0], draftPts_[1], cursor_};
                    ok = true;
                } else {
                    p.drawLine(vt.toScreen(draftPts_[0]), vt.toScreen(cursor_));
                }
                break;
            case DimMode::Ordinate:
                temp.a = draftPts_[0];
                temp.b = cursor_;
                ok = true;
                break;
            case DimMode::Radius:
            case DimMode::Diameter:
            case DimMode::ArcLength:
            case DimMode::Jogged:
                if (dimHasRef_) {
                    temp.center = dimRefCenter_;
                    temp.radius = dimRefRadius_;
                    temp.startAngle = dimRefStart_;
                    temp.endAngle = dimRefEnd_;
                    temp.b = cursor_;
                    temp.points = {cursor_};
                    ok = true;
                }
                break;
            }
            if (ok) EntityRenderer::draw(p, temp, *doc_, vt, paperInk(QColor("#ffffff")));
        } else if (tool_ == ToolId::TwistedRoute && pts.size() >= 2) {
            Entity tp;
            tp.type = EntityType::TwistedPair;
            tp.points = pts;
            tp.a = pts.front();
            tp.b = pts.back();
            tp.meta.insert(QStringLiteral("twistWidth"), kTwistedPairWidth);
            tp.meta.insert(QStringLiteral("twistPitch"), kTwistedPairPitch);
            tp.meta.insert(QStringLiteral("twistEnds"), kTwistedPairLead);
            EntityRenderer::draw(p, tp, *doc_, vt, paperInk(QColor("#ffffff")));
        } else if (tool_ == ToolId::TwistedHarness && pts.size() >= 2) {
            Entity tp;
            tp.type = EntityType::TwistedPair;
            tp.a = pts[0];
            tp.b = twistedFrozen_ ? twistedFrozenAt_ : pts[1];
            tp.meta.insert(QStringLiteral("twistWires"), std::clamp(twistWires_, 2, kTwistedMaxWires));
            tp.meta.insert(QStringLiteral("twistWidth"), kTwistedPairWidth * 1.5);
            tp.meta.insert(QStringLiteral("twistPitch"), kTwistedPairPitch);
            tp.meta.insert(QStringLiteral("twistEnds"), kTwistedPairLead);
            EntityRenderer::draw(p, tp, *doc_, vt, paperInk(QColor("#ffffff")));
        } else if (tool_ == ToolId::TwistedPair && pts.size() >= 2) {
            // Previewed as the thing itself, drawn by the same code that will draw it once placed -
            // so what is on screen while placing it is what is placed.
            Entity tp;
            tp.type = EntityType::TwistedPair;
            tp.a = pts[0];
            tp.b = pts[1];
            tp.meta.insert(QStringLiteral("twistWidth"), kTwistedPairWidth);
            tp.meta.insert(QStringLiteral("twistPitch"), kTwistedPairPitch);
            tp.meta.insert(QStringLiteral("twistEnds"), kTwistedPairLead);
            EntityRenderer::draw(p, tp, *doc_, vt, paperInk(QColor("#ffffff")));
        } else if (tool_ == ToolId::DoubleLine && pts.size() >= 2) {
            // One rail runs through the cursor and the gap goes to the other side, which is how it
            // will be placed - the preview and the result are the same line.
            const Vec2 perp = (pts[1] - pts[0]).normalized().perp();
            const Vec2 off = perp * (doubleLineGap_ / 2.0);
            const Vec2 shift =
                doubleRunCentreOffset(pts[0], pts[1], doubleLineGap_, doubleSecondBelow());
            std::vector<Vec2> r1 = {pts[0] + shift + off, pts[1] + shift + off};
            std::vector<Vec2> r2 = {pts[0] + shift - off, pts[1] + shift - off};
            extendLsStart(r1);
            extendLsStart(r2);
            capShiftStart(r1);
            capShiftStart(r2);
            p.drawLine(vt.toScreen(r1[0]), vt.toScreen(r1[1]));
            p.drawLine(vt.toScreen(r2[0]), vt.toScreen(r2[1]));
        } else if (tool_ == ToolId::DoublePolyline && pts.size() >= 2) {
            // Round each filleted corner of the centerline at radius + gap/2 so both
            // runs fillet, then offset.
            const double h = doubleLineGap_ / 2.0;
            std::vector<double> centerRadii(previewRadii.size(), 0.0);
            for (size_t i = 0; i < previewRadii.size(); ++i)
                if (previewRadii[i] > 0) centerRadii[i] = previewRadii[i] + h;
            const std::vector<Vec2> shifted =
                doubleRunCentre(pts, doubleLineGap_, doubleSecondBelow());
            const std::vector<Vec2> center =
                previewAnyFillet ? roundedPolylinePerCorner(shifted, centerRadii) : shifted;
            std::vector<Vec2> r1 = offsetPolyline(center, h);
            std::vector<Vec2> r2 = offsetPolyline(center, -h);
            extendLsStart(r1); // ls: start both runs on the reference line
            extendLsStart(r2);
            capShiftStart(r1); // Shift: and so does the line the run was started on
            capShiftStart(r2);
            for (const auto& run : {r1, r2}) {
                QPolygonF poly;
                for (const auto& v : run) poly << vt.toScreen(v);
                p.drawPolyline(poly);
            }
        } else if (tool_ == ToolId::Circle) {
            QPointF cc;
            double r = 0;
            Vec2 centerWorld;
            double radiusWorld = 0;
            bool ok = false;
            if (circleMode_ == CircleMode::ThreePoint) {
                if (pts.size() >= 3) {
                    Vec2 c2; double rr = 0;
                    if (circumcircle(pts[0], pts[1], pts[2], c2, rr)) {
                        centerWorld = c2; radiusWorld = rr;
                        cc = vt.toScreen(c2); r = rr * vt.scale(); ok = true;
                    }
                }
            } else if (pts.size() >= 2) {
                Vec2 center; double rr = 0;
                if (circleMode_ == CircleMode::CenterRadius) {
                    center = pts[0]; rr = distance(pts[0], pts[1]);
                } else if (circleMode_ == CircleMode::CenterDiameter) {
                    center = pts[0]; rr = distance(pts[0], pts[1]) / 2.0;
                } else { // TwoPoint
                    center = lerp(pts[0], pts[1], 0.5); rr = distance(pts[0], pts[1]) / 2.0;
                }
                centerWorld = center; radiusWorld = rr;
                cc = vt.toScreen(center); r = rr * vt.scale(); ok = true;
            }
            if (ok) {
                p.drawEllipse(cc, r, r);
                p.drawLine(cc, vt.toScreen(pts.back()));
                const QString valueLabel =
                    QStringLiteral("R %1  D %2").arg(radiusWorld, 0, 'f', 2)
                                                .arg(radiusWorld * 2.0, 0, 'f', 2);
                const QPointF labelAt = vt.toScreen(centerWorld + Vec2{radiusWorld, 0});
                drawDraftValueBox(p, labelAt, valueLabel, paperInk(QColor("#ffd166")));
            }
            else {
                QPolygonF poly;
                for (const auto& v : pts) poly << vt.toScreen(v);
                p.drawPolyline(poly);
            }
        } else if (tool_ == ToolId::Arc) {
            // Live arc preview built from the placed points + the cursor.
            auto a = buildArcEntity(arcMode_, pts, arcMirror_);
            if (a) {
                EntityRenderer::draw(p, *a, *doc_, vt, paperInk(QColor("#ffffff")));
            } else {
                QPolygonF poly;
                for (const auto& v : pts) poly << vt.toScreen(v);
                p.drawPolyline(poly);
            }
            // Indication line + value readout, like the other construction tools.
            // Prefer the explicit centre of the center-based modes (so the radius is
            // shown live and can be typed); otherwise use the arc's computed centre
            // so EVERY arc mode shows a radius line + value.
            std::optional<Vec2> centre;
            double guideR = 0.0;
            if (arcMode_ == ArcMode::CenterStartEnd && !draftPts_.empty()) {
                centre = draftPts_[0];
                guideR = draftPts_.size() >= 2 ? distance(*centre, draftPts_[1])
                                               : distance(*centre, cursor_);
            } else if (arcMode_ == ArcMode::StartCenterEnd && draftPts_.size() >= 2) {
                centre = draftPts_[1];
                guideR = distance(*centre, draftPts_[0]);
            } else if (a) {
                centre = a->center;
                guideR = a->radius;
            }
            const Vec2 from = centre.value_or(draftPts_.back());
            const Vec2 d = cursor_ - from;
            const double len = d.length();
            if (len > 1e-9) {
                const QPointF fs = vt.toScreen(from);
                const QPointF qs = vt.toScreen(cursor_);
                // Bright radius/direction line from the centre to the cursor.
                QPen gp(centre ? paperInk(QColor("#19ff7e")) : paperInk(QColor("#9aa6b6")));
                gp.setCosmetic(true);
                gp.setWidthF(1.4);
                if (!centre) gp.setStyle(Qt::DotLine);
                p.setPen(gp);
                p.setBrush(Qt::NoBrush);
                p.drawLine(fs, qs);
                if (centre) { // mark the centre
                    p.setBrush(paperInk(QColor("#19ff7e")));
                    p.drawEllipse(fs, 2.6, 2.6);
                    p.setBrush(Qt::NoBrush);
                }
                double deg = std::atan2(d.y, d.x) * 180.0 / M_PI;
                if (deg < 0) deg += 360.0;
                const QString lbl =
                    centre ? QStringLiteral("R %1   %2\xC2\xB0").arg(guideR, 0, 'f', 2).arg(deg, 0, 'f', 1)
                           : QStringLiteral("%1   %2\xC2\xB0").arg(len, 0, 'f', 2).arg(deg, 0, 'f', 1);
                drawDraftValueBox(p, qs, lbl, paperInk(QColor("#ffd166")));
            }
        } else if (tool_ == ToolId::Ellipse) {
            // Live (rotated) ellipse preview; before the 3rd point, a rubber line.
            // For an elliptical arc, once the start angle is placed (4th point) the
            // preview opens into an arc sweeping to the cursor.
            auto el = buildEllipseEntity(ellipseMode_, pts);
            if (el) {
                if (ellipseMode_ == EllipseMode::EllipticalArc && draftPts_.size() >= 4) {
                    el->startAngle = ellipseParamAngle(el->center, el->radius, el->radiusY,
                                                       el->rotation, draftPts_[3]);
                    el->endAngle =
                        ellipseParamAngle(el->center, el->radius, el->radiusY, el->rotation, cursor_);
                    el->meta.insert("ellipticalArc", true);
                }
                EntityRenderer::draw(p, *el, *doc_, vt, paperInk(QColor("#ffffff")));
            } else {
                QPolygonF poly;
                for (const auto& v : pts) poly << vt.toScreen(v);
                p.drawPolyline(poly);
            }
            // Indication line + value readout. The line runs from the centre toward
            // the LARGEST radius (major axis) once both axes exist — not toward the
            // crosshair; step 1 follows the axis being dragged.
            Vec2 from{0, 0};
            Vec2 to = cursor_;
            QString lbl;
            bool show = false;
            if (draftPts_.size() == 1) { // first axis: length + angle (toward the cursor)
                from = draftPts_[0]; // Center mode: this IS the centre
                const Vec2 d = cursor_ - from;
                const double L = d.length();
                double deg = std::atan2(d.y, d.x) * 180.0 / M_PI;
                if (deg < 0) deg += 360.0;
                lbl = QStringLiteral("Length %1   Angle %2\xC2\xB0").arg(L, 0, 'f', 2).arg(deg, 0, 'f', 1);
                show = L > 1e-9;
            } else if (draftPts_.size() == 2 && el) { // along the axis being dragged (changeable radius)
                from = el->center;
                // Perpendicular to the first axis = the half-axis the cursor sets;
                // point to its on-axis endpoint on the cursor's side (not the cross).
                const Vec2 n{-std::sin(el->rotation), std::cos(el->rotation)};
                const double side = dot(cursor_ - el->center, n) >= 0 ? 1.0 : -1.0;
                to = {el->center.x + n.x * el->radiusY * side,
                      el->center.y + n.y * el->radiusY * side};
                lbl = QStringLiteral("Radius %1").arg(el->radiusY, 0, 'f', 2);
                show = el->radiusY > 1e-9;
            } else if (el && draftPts_.size() >= 3) { // elliptical-arc sweep angle (toward cursor)
                from = el->center;
                const Vec2 d = cursor_ - from;
                double deg = std::atan2(d.y, d.x) * 180.0 / M_PI;
                if (deg < 0) deg += 360.0;
                lbl = QStringLiteral("%1\xC2\xB0").arg(deg, 0, 'f', 1);
                show = true;
            }
            if (show) {
                const QPointF fs = vt.toScreen(from);
                const QPointF qs = vt.toScreen(to);
                QPen gp(paperInk(QColor("#19ff7e")));
                gp.setCosmetic(true);
                gp.setWidthF(1.4);
                p.setPen(gp);
                p.setBrush(paperInk(QColor("#19ff7e")));
                p.drawEllipse(fs, 2.6, 2.6); // mark the centre / axis start
                p.setBrush(Qt::NoBrush);
                p.drawLine(fs, qs);
                drawDraftValueBox(p, qs, lbl, paperInk(QColor("#ffd166")));
            }
        } else if (tool_ == ToolId::Spline) {
            // Control polygon (the "vertices lines"): dashed frame through the
            // placed points + cursor, with a square marker at each placed vertex.
            QPen frame(QColor("#d98a4e"));
            frame.setCosmetic(true);
            frame.setStyle(Qt::DashLine);
            p.setPen(frame);
            QPolygonF cp;
            for (const auto& v : pts) cp << vt.toScreen(v);
            p.drawPolyline(cp);
            p.setBrush(QColor("#4ea3ff"));
            QPen vpen(QColor("#0d1117"));
            vpen.setCosmetic(true);
            p.setPen(vpen);
            for (const auto& v : draftPts_) { // placed vertices (not the live cursor)
                const QPointF s = vt.toScreen(v);
                p.drawRect(QRectF(s.x() - 2.5, s.y() - 2.5, 5, 5));
            }
            p.setBrush(Qt::NoBrush);

            // The smooth curve itself (fit through the points, or the CV B-spline).
            const std::vector<Vec2> curve =
                (pts.size() >= 3)
                    ? (splineMode_ == SplineMode::CV ? bsplinePoints(pts) : splinePoints(pts))
                    : pts;
            QPen cpen(paperInk(QColor("#ffffff")));
            cpen.setCosmetic(true);
            cpen.setWidthF(1.6);
            p.setPen(cpen);
            QPolygonF poly;
            for (const auto& v : curve) poly << vt.toScreen(v);
            p.drawPolyline(poly);
        } else if (tool_ == ToolId::Polyline &&
                   (polyArcMode_ ||
                    std::any_of(draftBulges_.begin(), draftBulges_.end(),
                                [](double b) { return std::abs(b) > 1e-9; }))) {
            // Arc-segment polyline preview: placed bulges + the live (tangent) arc
            // for the current segment to the cursor.
            std::vector<double> bulges = draftBulges_;
            double cur = 0.0;
            if (polyArcMode_ && draftPts_.size() >= 2) {
                const Vec2 prevDir = draftPts_.back() - draftPts_[draftPts_.size() - 2];
                cur = tangentBulge(draftPts_.back(), cursor_, prevDir);
            }
            bulges.push_back(cur);
            QPolygonF poly;
            for (const auto& v : expandBulgePolyline(pts, bulges)) poly << vt.toScreen(v);
            p.drawPolyline(poly);
        } else {
            const std::vector<Vec2> show =
                (tool_ == ToolId::Polyline && previewAnyFillet)
                    ? roundedPolylinePerCorner(pts, previewRadii)
                    : pts;
            QPolygonF poly;
            for (const auto& v : show) poly << vt.toScreen(v);
            p.drawPolyline(poly);
        }

        // Shift-fanout wire: ghost the same route from every OTHER pin of the connector.
        if (wireArray_ && tool_ == ToolId::Wire && wireArrayOffsets_.size() > 1) {
            QPen gp(QColor(127, 191, 255, 200));
            gp.setCosmetic(true);
            gp.setWidthF(1.2);
            gp.setStyle(Qt::DashLine);
            p.setPen(gp);
            for (const Vec2& off : wireArrayOffsets_) {
                if (std::abs(off.x) < 1e-9 && std::abs(off.y) < 1e-9) continue; // anchor drawn above
                QPolygonF poly;
                // Same lane routing as the commit, so a turn previews pitched.
                for (const Vec2& v : fanRoute(pts, off)) poly << vt.toScreen(v);
                p.drawPolyline(poly);
            }
        }

        // Live length / angle readout of the current segment (toggle: Angles).
        if (showAngles_ && tool_ != ToolId::Rect && tool_ != ToolId::Circle &&
            tool_ != ToolId::Ellipse && tool_ != ToolId::Arc && tool_ != ToolId::Dimension) {
            const Vec2 anchor = draftPts_.back();
            const Vec2 v = cursor_ - anchor;
            const double len = v.length();
            if (len > 1e-6) {
                double deg = std::atan2(v.y, v.x) * 180.0 / M_PI; // CCW from +X
                if (deg < 0) deg += 360.0;

                // Reference geometry: the 0-degree baseline from the anchor and a
                // small arc sweeping from it to the segment, illustrating the angle.
                const QPointF as = vt.toScreen(anchor);
                const QPointF cs = vt.toScreen(cursor_);
                QPen refPen(QColor("#6b7a90"));
                refPen.setCosmetic(true);
                refPen.setStyle(Qt::DotLine);
                p.setPen(refPen);
                p.setBrush(Qt::NoBrush);
                const QPointF baseEnd = vt.toScreen(anchor + Vec2{len, 0}); // +X reference
                p.drawLine(as, baseEnd);
                const double rpx = std::min(40.0, std::max(14.0, len * vt.scale() * 0.4));
                // Sweep from the +X baseline to the drawn segment, the short way.
                // drawArc: 0 = 3 o'clock, positive = CCW (up) — matching our
                // Y-up world, so the span is the angle itself.
                double arcDeg = deg;
                if (arcDeg > 180.0) arcDeg -= 360.0;
                p.drawArc(QRectF(as.x() - rpx, as.y() - rpx, 2 * rpx, 2 * rpx),
                          0, static_cast<int>(arcDeg * 16.0));

                const QString label =
                    QStringLiteral("%1  <  %2°").arg(len, 0, 'f', 1).arg(deg, 0, 'f', 1);
                const QPointF c = cs;
                QFont f = p.font();
                f.setPixelSize(12);
                p.setFont(f);
                const QFontMetricsF fm(f);
                const QRectF box(c.x() + 14, c.y() - 26,
                                 fm.horizontalAdvance(label) + 10, 18);
                p.setPen(QPen(QColor("#3a3320")));
                p.setBrush(QColor(20, 24, 32, 220));
                p.drawRect(box);
                p.setPen(QPen(paperInk(QColor("#ffd166"))));
                p.drawText(box, Qt::AlignCenter, label);
                p.setBrush(Qt::NoBrush);
            }
        }
    }

    // Hatch/Wipeout "Pick Points": preview the enclosed boundary under the cursor
    // so it's clear which region a click would fill (a circle stays a circle when
    // "multiple joints" is off).
    if (tool_ == ToolId::Hatch && hatchStyle_.boundary == HatchBoundary::PickPoints) {
        auto previewLoop = [&](const std::vector<Vec2>& pts) {
            QPolygonF poly;
            for (const auto& v : pts) poly << vt.toScreen(v);
            if (poly.size() >= 3) {
                p.save();
                QPen hp(paperInk(QColor("#2bd0ff")));
                hp.setCosmetic(true);
                hp.setWidthF(1.6);
                p.setPen(hp);
                p.setBrush(QColor(80, 160, 255, 60));
                p.drawPolygon(poly);
                p.restore();
            }
        };
        // Live preview: on big drawings skip the O(n^2) multi-object tracer so the
        // cursor stays smooth (the click still traces and fills the cell).
        const bool allowTrace = doc_->entities().size() <= 80;
        HatchRegion region = pickHatchRegion(cursor_, allowTrace);
        if (region.ent) {
            if (!hatchStyle_.manyJoints && region.ent->type == EntityType::Circle) {
                p.save();
                QPen hp(paperInk(QColor("#2bd0ff")));
                hp.setCosmetic(true);
                hp.setWidthF(1.6);
                p.setPen(hp);
                p.setBrush(QColor(80, 160, 255, 60));
                const QPointF c = vt.toScreen(region.ent->center);
                const double r = region.ent->radius * vt.scale();
                p.drawEllipse(c, r, r);
                p.restore();
            } else {
                previewLoop(boundaryPointsOf(*region.ent, hatchStyle_.manyJoints));
            }
        } else if (region.loop) {
            previewLoop(*region.loop); // region bounded by several separate objects
        }
    }

    // Fillet / Chamfer: MARK the walls picked so far. For a polyline / rect / polygon
    // only the picked WALL (the edge between the two nearest joints) is highlighted —
    // not the whole shape — so it's clear which segment the corner op will use. Other
    // primitives (single lines) are re-stroked whole.
    if ((tool_ == ToolId::Fillet || tool_ == ToolId::Chamfer) && !pickIds_.empty()) {
        const QColor mark = paperInk(QColor("#ffcf3f"));
        QPen wp(mark);
        wp.setCosmetic(true);
        wp.setWidthF(2.2);
        p.save();
        p.setPen(wp);
        p.setBrush(Qt::NoBrush);
        for (size_t k = 0; k < pickIds_.size(); ++k) {
            const Entity* e = nullptr;
            for (const auto& ent : doc_->entities())
                if (ent.id == pickIds_[k]) { e = &ent; break; }
            if (!e) continue;
            const bool polyish = e->type == EntityType::Polyline || e->type == EntityType::Rect ||
                                 e->type == EntityType::Polygon;
            if (polyish && e->points.size() >= 2) {
                const Vec2 pk = k < pickPts_.size() ? pickPts_[k] : cursor_;
                const int n = static_cast<int>(e->points.size());
                const bool closed =
                    e->type == EntityType::Rect || e->type == EntityType::Polygon;
                const int edges = closed ? n : n - 1;
                int seg = 0;
                double bd = std::numeric_limits<double>::max();
                for (int i = 0; i < edges; ++i) {
                    const double d =
                        closestPointOnSegment(pk, e->points[i], e->points[(i + 1) % n]).distance;
                    if (d < bd) { bd = d; seg = i; }
                }
                // Clip the marked wall to the DRAWN outline (past fillets/chamfers) so a
                // re-edit never shows the old sharp-corner stub of a modified corner.
                const auto [a, b] = visibleWall(*e, seg);
                if (e->meta.value("double").toBool()) { // mark both physical runs of the wall
                    const Vec2 off = (b - a).normalized().perp() * (e->meta.value("gap").toDouble(10.0) / 2.0);
                    p.drawLine(vt.toScreen(a + off), vt.toScreen(b + off));
                    p.drawLine(vt.toScreen(a - off), vt.toScreen(b - off));
                } else {
                    p.drawLine(vt.toScreen(a), vt.toScreen(b));
                }
            } else {
                EntityRenderer::draw(p, *e, *doc_, vt, mark);
            }
        }
        // Chamfer preview: after the first pick, a dashed line from the first clicked
        // point to the cursor previews the cut that will pass between the two points.
        if (tool_ == ToolId::Chamfer && !pickPts_.empty() && pickIds_.size() < 2) {
            QPen pv(paperInk(QColor("#19ff7e")));
            pv.setCosmetic(true);
            pv.setWidthF(1.4);
            pv.setStyle(Qt::DashLine);
            p.setPen(pv);
            p.drawLine(vt.toScreen(pickPts_.front()), vt.toScreen(cursor_));
        }
        p.restore();
    }

    // Trim/Extend fence line (AutoCAD-style): a bright dashed line the user drags
    // across objects. For Extend, also preview each crossed line extended to the
    // reference (ghost) so it's clear which ones can reach it.
    if (trimFenceActive_) {
        p.save();
        if (tool_ == ToolId::Extend) {
            std::vector<Entity> refs;
            for (const auto& r : doc_->entities())
                if (std::find(trimRefIds_.begin(), trimRefIds_.end(), r.id) != trimRefIds_.end())
                    refs.push_back(r);
            QPen gp(paperInk(QColor(120, 200, 255, 200))); // ghost of the extended result
            gp.setCosmetic(true);
            gp.setWidthF(1.4);
            p.setPen(gp);
            for (const auto& e : doc_->entities()) {
                const bool ln = e.type == EntityType::Line;
                const bool dl = e.type == EntityType::DoubleLine;
                const bool pl = e.type == EntityType::Polyline;
                if (!ln && !dl && !pl) continue;
                if (std::find(trimRefIds_.begin(), trimRefIds_.end(), e.id) != trimRefIds_.end())
                    continue;
                std::optional<Vec2> x;
                if (pl) {
                    for (size_t i = 1; i < e.points.size() && !x; ++i)
                        x = segmentSegmentIntersection(trimFenceStart_, cursor_, e.points[i - 1], e.points[i]);
                } else {
                    x = segmentSegmentIntersection(trimFenceStart_, cursor_, e.a, e.b);
                }
                if (!x) continue;
                Entity ghost = e;
                if (extendEntityTo(ghost, *x, refs)) EntityRenderer::draw(p, ghost, *doc_, vt, gp.color());
            }
        }
        QPen fp(paperInk(QColor("#19ff7e")));
        fp.setCosmetic(true);
        fp.setWidthF(1.6);
        fp.setStyle(Qt::DashLine);
        p.setPen(fp);
        p.drawLine(vt.toScreen(trimFenceStart_), vt.toScreen(cursor_));
        p.restore();
    }

    // Modify reference points.
    if (!modifyPts_.empty()) {
        QPen mp(QColor("#f0a23b"));
        mp.setCosmetic(true);
        mp.setWidthF(1.4);
        p.setPen(mp);
        QPolygonF poly;
        for (const auto& v : modifyPts_) poly << vt.toScreen(v);
        poly << vt.toScreen(cursor_);
        p.drawPolyline(poly);

        // Live ghost of the result (Move/Rotate/Mirror/Scale/Offset) until the
        // command finishes, so the outcome is visible as the cursor moves.
        const QColor ghost = paperInk(paperInk(QColor(120, 200, 255, 170)));
        for (const Entity& g : modifyPreview()) EntityRenderer::draw(p, g, *doc_, vt, ghost);
    }

    // Offset tool preview: while defining the distance, rubber-band the two points;
    // once an object is picked, ghost the offset copy on the cursor's side.
    if (tool_ == ToolId::Offset) {
        if (!offsetHasDist_ && offsetDistPts_.size() == 1) {
            QPen pv(paperInk(QColor("#19ff7e")));
            pv.setCosmetic(true);
            pv.setWidthF(1.4);
            pv.setStyle(Qt::DashLine);
            p.setPen(pv);
            p.drawLine(vt.toScreen(offsetDistPts_[0]), vt.toScreen(cursor_));
        } else if (offsetHasDist_ && !offsetSourceId_.isEmpty()) {
            const Entity* src = nullptr;
            for (const auto& e : doc_->entities())
                if (e.id == offsetSourceId_) { src = &e; break; }
            if (src)
                if (auto g = offsetEntityToward(*src, cursor_, offsetDist_))
                    EntityRenderer::draw(p, *g, *doc_, vt, paperInk(QColor(120, 200, 255, 210)));
        }
    }

    // Snap marker.
    if (snapResult_.snapped && snapResult_.kind != SnapKind::Grid &&
        snapResult_.kind != SnapKind::Ortho && snapResult_.kind != SnapKind::Polar) {
        const QPointF s = vt.toScreen(snapResult_.point);
        QPen sp(paperInk(QColor("#ffd166")));
        sp.setCosmetic(true);
        sp.setWidthF(1.6);
        p.setPen(sp);
        p.setBrush(Qt::NoBrush);
        // The MIDDLE of something - the midpoint of a line, the centre of a circle - is marked with a
        // triangle, and an end or a crossing with a square. One shape for every kind of snap said
        // "something was caught" without saying WHAT, which is the one thing worth knowing when the end
        // and the middle of a short line are a few pixels apart.
        if (snapResult_.kind == SnapKind::Midpoint || snapResult_.kind == SnapKind::Center) {
            QPolygonF tri;
            tri << QPointF(s.x(), s.y() - 6) << QPointF(s.x() + 6, s.y() + 4)
                << QPointF(s.x() - 6, s.y() + 4);
            p.drawPolygon(tri);
        } else {
            p.drawRect(QRectF(s.x() - 5, s.y() - 5, 10, 10));
        }
    }

    // A command that has just refused, marked where it happened: a struck-out circle, in the colour
    // nothing else on a drawing uses. It stays until something works, so it can be looked at.
    if (refused_) {
        const QPointF c = vt.toScreen(refusedAt_);
        p.save();
        QPen np(QColor("#ff6b6b"));
        np.setCosmetic(true);
        np.setWidthF(2.0);
        p.setPen(np);
        p.setBrush(Qt::NoBrush);
        const double r = 9.0;
        p.drawEllipse(c, r, r);
        const double k = r * 0.7071;
        p.drawLine(c + QPointF(-k, k), c + QPointF(k, -k));
        p.restore();
    }

    // Drawn LAST, so nothing paints over it: the joints where a selected break mark cuts its run sit
    // exactly where the mark's own strokes are, and the selection highlight is stroked over that.
    // The joints where a SELECTED break mark cuts its run. They say which conductors the mark
    // interrupts and where their cut ends are - which is worth seeing while the mark is being placed
    // or adjusted, and clutter the rest of the time: a finished drawing shows the interruption, and the
    // ends of the run are where the strokes are. So they belong to the selection, not to the drawing,
    // and they are drawn here rather than by the renderer.
    for (const QString& id : selected_) {
        const Entity* mark = nullptr;
        for (const auto& m : doc_->entities())
            if (m.id == id && m.type == EntityType::LengthBreak) mark = &m;
        if (!mark || !mark->meta.value(QStringLiteral("endJoints")).toBool(true)) continue;
        // Where the mark actually lies: from its run when it is attached to one.
        Entity shown = *mark;
        const Entity* host = nullptr;
        if (const QString hostId = mark->meta.value(QStringLiteral("host")).toString();
            !hostId.isEmpty())
            for (const auto& h : doc_->entities())
                if (h.id == hostId) host = &h;
        if (host) {
            Vec2 pos, dir;
            if (runPointAtFraction(*host, mark->meta.value(QStringLiteral("annT")).toDouble(0.5), pos,
                                   dir)) {
                shown.a = pos;
                shown.rotation = std::atan2(dir.y, dir.x);
            } else {
                host = nullptr;
            }
        }
        // The lines of the run it interrupts: a double line's two rails, anything else's centreline.
        std::vector<std::vector<Vec2>> lines;
        auto addRunLines = [&lines](const Entity& r) {
            if (r.type == EntityType::DoubleLine) {
                const double g = r.meta.value(QStringLiteral("gap")).toDouble(10.0);
                const Vec2 off = (r.b - r.a).normalized().perp() * (g / 2.0);
                lines.push_back({r.a + off, r.b + off});
                lines.push_back({r.a - off, r.b - off});
            } else if (r.type == EntityType::Polyline && r.meta.value(QStringLiteral("double")).toBool()) {
                const double g = r.meta.value(QStringLiteral("gap")).toDouble(10.0);
                for (double s : {g / 2.0, -g / 2.0}) lines.push_back(offsetPolyline(r.points, s));
            } else if (std::vector<Vec2> cl = runCenterline(r); cl.size() >= 2) {
                lines.push_back(std::move(cl));
            }
        };
        if (host) {
            addRunLines(*host);
        } else {
            const Bounds b = shown.bounds();
            for (const auto& r : doc_->entities()) {
                if (r.id == mark->id || r.type == EntityType::LengthBreak) continue;
                const Bounds rb = r.bounds();
                if (rb.max.x < b.min.x || rb.min.x > b.max.x || rb.max.y < b.min.y ||
                    rb.min.y > b.max.y)
                    continue;
                addRunLines(r);
            }
        }
        const double halfWorld = std::max(0.5, shown.meta.value(QStringLiteral("size")).toDouble(6.0));
        const double dotWorld = std::clamp(halfWorld * 0.07, 0.9, 2.6);
        const double dot = std::clamp(dotWorld * vt.scale(), 2.8, 5.5);
        p.save();
        // Amber, with a dark rim: the selected mark is drawn in cyan and the run in white, so a joint in
        // either of those colours is a dot nobody can see. It is telling the user something - here is a
        // conductor this mark cuts, and here is its end - so it has to be legible against both.
        p.setPen(QPen(QColor(20, 26, 34), 1.0));
        p.setBrush(QColor(255, 196, 92));
        for (const std::vector<Vec2>& stroke : breakStrokes(shown))
            for (size_t i = 1; i < stroke.size(); ++i)
                for (const std::vector<Vec2>& ln : lines)
                    for (size_t j = 1; j < ln.size(); ++j)
                        if (auto x = segmentSegmentIntersection(stroke[i - 1], stroke[i], ln[j - 1],
                                                               ln[j]))
                            p.drawEllipse(vt.toScreen(*x), dot, dot);
        p.restore();
    }
}

} // namespace Edraw_Desktop
