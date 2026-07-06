#include "app/Editor.h"

#include "geometry/Geometry.h"
#include "tools/EntityTransforms.h"
#include "tools/ModifyOps.h"

#include <QApplication>
#include <QJsonArray>
#include <QPainter>
#include <QPolygonF>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Edraw_Desktop {

namespace {
// Hold this long during a drag before the rectangle becomes a lasso (Edraw's
// LASSO_DELAY_MS).
constexpr qint64 kLassoDelayMs = 200;

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
                      ArcStart, ArcEnd, ArcMid, EllipseMajor, EllipseMinor, TextWidth, TextHeight,
                      DimText, LeaderTip, LeaderLanding, LeaderKnee };

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
Vec2 cappedRunEnd(const Entity& e, const QString& key, const Vec2& thisEnd, const Vec2& otherEnd) {
    if (!e.meta.value(key).toBool()) return thisEnd;
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
    auto x = lineLineIntersection(otherEnd, thisEnd, a, b);
    return x ? *x : thisEnd;
}

std::vector<Grip> gripsOf(const Entity& e) {
    std::vector<Grip> g;
    switch (e.type) {
    case EntityType::Line:
        g.push_back({e.a, GripRole::LineA, 0});
        g.push_back({lerp(e.a, e.b, 0.5), GripRole::LineMid, 0});
        g.push_back({e.b, GripRole::LineB, 0});
        break;
    case EntityType::Dimension: {
        // Linear/aligned: a grip at each measured point + at both dimension-line
        // ends (which move the dimension-line offset). Index 2 = the offset point.
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
            g.push_back({p1, GripRole::Vertex, 0});
            g.push_back({p2, GripRole::Vertex, 1});
            g.push_back({d1, GripRole::Vertex, 2});
            g.push_back({d2, GripRole::Vertex, 2});
            // A grip on the dimension TEXT — drag it to reposition the measurement
            // text; a dashed leader then connects it back to the dimension line.
            Vec2 tpos = lerp(d1, d2, 0.5);
            if (e.meta.contains("textPosX"))
                tpos = {e.meta.value("textPosX").toDouble(), e.meta.value("textPosY").toDouble()};
            g.push_back({tpos, GripRole::DimText, 0});
        } else {
            g.push_back({e.a, GripRole::LineA, 0});
            g.push_back({e.b, GripRole::LineB, 0});
        }
        break;
    }
    case EntityType::DoubleLine: {
        // Joints ONLY on the two VISIBLE parallel lines — never on the centerline
        // between them. Each end grip carries its offset so dragging it keeps that
        // physical point under the cursor; each physical mid moves the whole line.
        const double gap = e.meta.value("gap").toDouble(10.0);
        const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
        const Vec2 noff = off * -1.0;
        // Endpoints sit on the ACTUAL physical-line ends (after trim/extend caps),
        // so a joint follows the drawn line rather than floating at the raw offset.
        const Vec2 s1 = cappedRunEnd(e, "cutStart", e.a + off, e.b + off);
        const Vec2 en1 = cappedRunEnd(e, "cutEnd", e.b + off, e.a + off);
        const Vec2 s2 = cappedRunEnd(e, "cutStart", e.a + noff, e.b + noff);
        const Vec2 en2 = cappedRunEnd(e, "cutEnd", e.b + noff, e.a + noff);
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
                // Marker positions follow the ACTUAL run, with its ends capped to a
                // trim/extend boundary (offsets from the centreline are kept for drag).
                std::vector<Vec2> pos = *run;
                if (pos.size() >= 2) {
                    pos.front() = cappedRunEnd(e, "cutStart", pos.front(), (*run)[1]);
                    pos.back() = cappedRunEnd(e, "cutEnd", pos.back(), (*run)[run->size() - 2]);
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
    case EntityType::Wire:
    case EntityType::Wipeout:
        for (int i = 0; i < static_cast<int>(e.points.size()); ++i)
            g.push_back({e.points[i], GripRole::Vertex, i});
        break;
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
    case EntityType::Rect:
        for (int i = 0; i < static_cast<int>(e.points.size()); ++i) {
            g.push_back({e.points[i], GripRole::RectCorner, i});
            // Wall midpoint grip for resizing one side.
            const Vec2 mid = lerp(e.points[i], e.points[(i + 1) % e.points.size()], 0.5);
            g.push_back({mid, GripRole::RectEdge, i});
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
        g.push_back({axisPt(e.radius, 0), GripRole::EllipseMajor, 0});
        g.push_back({axisPt(-e.radius, 0), GripRole::EllipseMajor, 0});
        g.push_back({axisPt(0, e.radiusY), GripRole::EllipseMinor, 0});
        g.push_back({axisPt(0, -e.radiusY), GripRole::EllipseMinor, 0});
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
    default: // splice / netlabel / notconnected / connector
        g.push_back({e.a, GripRole::Move, 0});
        break;
    }
    return g;
}
} // namespace

Editor::Editor(QObject* parent) : QObject(parent) {
    dragTimer_ = new QTimer(this);
    dragTimer_->setInterval(30); // animate the countdown + check the hold threshold
    connect(dragTimer_, &QTimer::timeout, this, [this] { onDragTick(); });
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
    cancelDraw();
    modifyPts_.clear();
    pickIds_.clear();
    pickPts_.clear();
    offsetHasDist_ = false;
    offsetDist_ = 0.0;
    offsetDistPts_.clear();
    offsetSourceId_.clear();
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
}

void Editor::repeatLastTool() {
    if (lastTool_ != ToolId::Select) setTool(lastTool_);
}

double Editor::worldTolerance() const {
    const double scale = doc_ ? doc_->view().scale : 1.0;
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
    case EntityType::Dimension:
        // Select by touching the actual drawn geometry (dimension line, extension
        // lines, radius/diameter line, angular arc, ...).
        for (const auto& s : dimensionSegments(e)) best = std::min(best, segDist(s.first, s.second));
        break;
    case EntityType::DoubleLine: {
        // Only the two VISIBLE parallel lines select the double line — NOT the
        // centerline / gap between them (clicking the empty middle won't pick it).
        const double gap = e.meta.value("gap").toDouble(10.0);
        const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
        best = std::min(segDist(e.a + off, e.b + off), segDist(e.a - off, e.b - off));
        break;
    }
    case EntityType::Polyline:
        // A DOUBLE polyline is picked on its two VISIBLE physical runs (offset by
        // ±gap/2), not the centerline in the empty gap.
        if (e.meta.value("double").toBool() && e.points.size() >= 2) {
            const double gap = e.meta.value("gap").toDouble(10.0);
            for (double s : {gap / 2.0, -gap / 2.0}) {
                const auto run = offsetPolyline(e.points, s);
                for (size_t i = 1; i < run.size(); ++i) best = std::min(best, segDist(run[i - 1], run[i]));
            }
            break;
        }
        [[fallthrough]];
    case EntityType::Spline:
    case EntityType::Wire:
        for (size_t i = 1; i < e.points.size(); ++i)
            best = std::min(best, segDist(e.points[i - 1], e.points[i]));
        break;
    case EntityType::Rect:
    case EntityType::Polygon:
        for (size_t i = 0; i < e.points.size(); ++i)
            best = std::min(best, segDist(e.points[i], e.points[(i + 1) % e.points.size()]));
        // Tables / note / revision boxes are pickable anywhere INSIDE (so a cell or
        // the box body can be clicked and double-clicked to edit, not only its border).
        if (e.meta.value("table").toBool() || e.meta.value("note").toBool() ||
            e.meta.value("revision").toBool()) {
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
    case EntityType::NetLabel:
    case EntityType::NotConnected:
        best = distance(e.a, p);
        break;
    case EntityType::Connector: {
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
        const ConnectorGeometry g = connectorGeometry(e.a, spec);
        const Vec2 c0{g.rectMin.x, g.rectMin.y}, c1{g.rectMax.x, g.rectMin.y},
            c2{g.rectMax.x, g.rectMax.y}, c3{g.rectMin.x, g.rectMax.y};
        best = std::min({segDist(c0, c1), segDist(c1, c2), segDist(c2, c3), segDist(c3, c0)});
        // Inside the body also counts as a hit.
        if (p.x >= g.rectMin.x && p.x <= g.rectMax.x && p.y >= g.rectMin.y && p.y <= g.rectMax.y)
            best = 0.0;
        break;
    }
    default:
        break;
    }
    return best;
}

} // namespace

std::optional<QString> Editor::hitTest(const Vec2& world) const {
    if (!doc_) return std::nullopt;
    const double tol = worldTolerance();
    double best = tol;
    std::optional<QString> hit;
    // Iterate in reverse so topmost (last drawn) wins.
    const auto& ents = doc_->entities();
    for (auto it = ents.rbegin(); it != ents.rend(); ++it) {
        if (const Layer* l = doc_->findLayer(it->layer); l && (!l->visible || l->locked))
            continue;
        const double d = distanceToEntity(*it, world);
        if (d <= best) { best = d; hit = it->id; }
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
        editing_ = false;
        gripGrab_ = false;
        editMoved_ = false;
        editGhost_.clear(); // source ghost cleared now that it's placed
        emit message(QStringLiteral("Joint placed"));
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
    // (c) Click on a joint of a selected entity -> grab it (click to place).
    if (beginGripGrab(raw, shift)) return;

    // (d) Click on an entity body -> select only (Shift removes). Dragging the body
    // does NOT move the object — moving requires the Move command (or a grip).
    if (auto hit = hitTest(p)) {
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
            if (it != selected_.end()) selected_.erase(it);
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
    // Holding Ctrl temporarily suspends ALL snapping (object/grid/polar/ortho) so a
    // point can be placed freely — e.g. picking a hatch location. Snap resumes when
    // Ctrl is released.
    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        snap.objectSnap = false;
        snap.gridSnap = false;
        snap.polar = false;
        snap.ortho = false;
    }
    snapResult_ = resolveSnap(*doc_, raw, anchor, worldTolerance(), snap, ignore);
    cursor_ = snapResult_.point;

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
        trimFenceActive_)
        emit overlayChanged();
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

void Editor::onPointerPress(const Vec2& raw, Qt::MouseButton button,
                            Qt::KeyboardModifiers mods) {
    if (!doc_ || button != Qt::LeftButton) return;
    onPointerMove(raw);
    const Vec2 p = cursor_;

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
        for (Entity e : clipboard_) {
            e.id = uid();
            translateEntity(e, off);
            selected_.push_back(e.id);
            doc_->entities().push_back(std::move(e));
        }
        pastePlacing_ = false;
        doc_->setDirty(true);
        emit message(QStringLiteral("Pasted %1 object(s)").arg(clipboard_.size()));
        emit selectionChanged();
        emit changed();
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
    if (isPlacementTool(tool_)) { handlePlacementPress(p); return; }
    handleModifyPress(p);
}

void Editor::onPointerRelease(const Vec2& raw, Qt::MouseButton button) {
    Q_UNUSED(raw);
    if (!doc_ || button != Qt::LeftButton) return;
    // Trim's cut line is two clicks (handled in onPointerPress), so the release does
    // nothing for it.
    if (!selectionTool()) return;
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

// Representative points used to test whether an entity is inside a region.
std::vector<Vec2> entitySelectPoints(const Entity& e) {
    std::vector<Vec2> pts;
    switch (e.type) {
    case EntityType::Line:
        pts = {e.a, lerp(e.a, e.b, 0.5), e.b};
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
        pts = e.points;
        break;
    case EntityType::Circle:
    case EntityType::Arc:
        pts = {e.center, {e.center.x + e.radius, e.center.y},
               {e.center.x - e.radius, e.center.y}, {e.center.x, e.center.y + e.radius},
               {e.center.x, e.center.y - e.radius}};
        break;
    case EntityType::Text: {
        // The text's bounding rectangle corners (so window selection encloses it
        // and crossing touches it via the edges below).
        const Bounds b = e.bounds();
        pts = {b.min, {b.max.x, b.min.y}, b.max, {b.min.x, b.max.y}};
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

std::vector<std::pair<Vec2, Vec2>> entitySelectSegments(const Entity& e) {
    std::vector<std::pair<Vec2, Vec2>> segs;
    auto pts = entitySelectPoints(e);
    switch (e.type) {
    case EntityType::Line:
        segs.emplace_back(e.a, e.b);
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
        const auto segs = entitySelectSegments(e);
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
    emit selectionChanged();
}

void Editor::drawGrips(QPainter& p, const Entity& e, const ViewTransform& vt) const {
    // Show the MTEXT container outline (dashed) so its width/height are visible.
    if (e.type == EntityType::Text) {
        const double bw = e.meta.value("boxWidth").toDouble(0.0);
        const double bh = e.meta.value("boxHeight").toDouble(0.0);
        if (bw > 0.0 || bh > 0.0) {
            const QPointF tl = vt.toScreen(e.a);
            const QPointF br = vt.toScreen({e.a.x + (bw > 0 ? bw : 0), e.a.y - (bh > 0 ? bh : 0)});
            QPen bp(QColor(120, 200, 255, 160));
            bp.setCosmetic(true);
            bp.setStyle(Qt::DashLine);
            p.setPen(bp);
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(tl, br).normalized());
        }
    }
    for (const auto& g : gripsOf(e)) {
        const QPointF s = vt.toScreen(g.pos);
        // Radius/midpoint handles drawn as small squares; vertices/ends larger.
        const double r = (g.role == GripRole::Center) ? 4.0 : 3.5;
        p.setPen(QPen(QColor("#0d1117")));
        p.setBrush(g.role == GripRole::Center ? QColor("#ffd166") : QColor("#ffffff"));
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
    for (const auto& id : selected_) {
        Entity* e = entityById(id);
        if (!e) continue;
        for (const auto& gr : gripsOf(*e)) {
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
            emit message(editGrips_.size() > 1
                             ? QStringLiteral("Shared joint grabbed (%1 objects) — click to place")
                                   .arg(editGrips_.size())
                             : QStringLiteral("Joint grabbed — move and click to place, Esc to cancel"));
            return true;
        }
    }
    return false;
}

void Editor::applyEditDrag(const Vec2& target) {
    // A leader-landing joint doesn't change geometry as it drags — it re-connects the
    // leader to whichever circle it's dropped on, handled at placement time.
    if (editMode_ == 1 && editRole_ == static_cast<int>(GripRole::LeaderLanding)) return;
    const Vec2 delta = target - editLast_;
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
        switch (role) {
        case GripRole::LineA: e->a = solveEnd(true, e, off); break;
        case GripRole::LineB: e->b = solveEnd(false, e, off); break;
        case GripRole::LineMid:
            // The middle joint moves the whole line (stays straight, no knee).
            translateEntity(*e, delta);
            break;
        case GripRole::Vertex:
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
        case GripRole::Move: translateEntity(*e, delta); break;
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
        case GripRole::ArcStart:
            e->radius = std::max(1e-6, distance(e->center, target));
            e->startAngle = std::atan2(target.y - e->center.y, target.x - e->center.x);
            break;
        case GripRole::ArcEnd:
            e->radius = std::max(1e-6, distance(e->center, target));
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
        }
    };

    if (editMode_ == 0) {
        for (Entity* e : selectedEntities()) translateEntity(*e, delta);
    } else {
        // Move every grabbed joint (one, or all coincident shared joints).
        // Reshaping a trimmed double line/polyline invalidates its trim caps (the
        // stored cut line/circle no longer matches the moved end), so drop them —
        // the joint then edits as a normal double-polyline vertex.
        for (const auto& eg : editGrips_)
            if (Entity* e = entityById(eg.id)) {
                for (const char* k :
                     {"cutStart", "cutStartKind", "cutStartAx", "cutStartAy", "cutStartBx", "cutStartBy",
                      "cutStartCx", "cutStartCy", "cutStartR", "cutStartPoly", "cutEnd", "cutEndKind",
                      "cutEndAx", "cutEndAy", "cutEndBx", "cutEndBy", "cutEndCx", "cutEndCy", "cutEndR",
                      "cutEndPoly"})
                    e->meta.remove(QLatin1String(k));
                applyOne(e, static_cast<GripRole>(eg.role), eg.index, eg.offset);
            }
    }
    editLast_ = target;
    doc_->setDirty(true);
}

void Editor::onDoubleClick(const Vec2& world) {
    if (drafting_ && (tool_ == ToolId::Polyline || tool_ == ToolId::DoublePolyline ||
                      tool_ == ToolId::Spline || tool_ == ToolId::Wire ||
                      tool_ == ToolId::Hatch || tool_ == ToolId::Wipeout)) {
        finishDraw();
        return;
    }
    // Double-click a text-bearing object (while idle) to re-open its editor: plain
    // text, or an annotation that carries editable text (balloon number, note /
    // revision body).
    if (tool_ == ToolId::Select) {
        if (auto hit = hitTest(world))
            if (Entity* e = entityById(*hit)) {
                // A table: edit the specific cell that was double-clicked.
                if (e->meta.value("table").toBool()) {
                    Vec2 tl; double cw = 0, ch = 0;
                    const int idx = tableCellAt(*e, world, tl, cw, ch);
                    if (idx >= 0) {
                        const QJsonArray cells = e->meta.value("cells").toArray();
                        const QString cur = idx < cells.size() ? cells[idx].toString() : QString();
                        emit editTableCellRequested(e->id, idx, cur, tl);
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
        // AutoCAD LINE: keep drawing connected straight segments until Enter/Esc.
        // Each completed segment is committed as its own straight line, then the
        // run continues from that point.
        draftPts_.push_back(p);
        drafting_ = true;
        if (draftPts_.size() >= 2) {
            Entity e;
            e.layer = doc_->activeLayer();
            e.type = EntityType::Line;
            e.a = draftPts_[draftPts_.size() - 2];
            e.b = draftPts_.back();
            pushUndo();
            addEntity(std::move(e));
            draftPts_ = {draftPts_.back()}; // continue from the last point
        }
        emit message(QStringLiteral("Line: click next point — Enter ends the run, Esc exits"));
        break;
    }
    case ToolId::DoubleLine:
    case ToolId::Rect:
    case ToolId::Polygon:
    case ToolId::Note:
    case ToolId::Revision:
        draftPts_.push_back(p);
        drafting_ = true;
        if (draftPts_.size() == 2) finishDraw();
        break;
    case ToolId::Balloon:
        // First click = the arrow tip (what it points at); second = balloon centre.
        draftPts_.push_back(p);
        drafting_ = true;
        emit message(draftPts_.size() < 2
                         ? QStringLiteral("Balloon: click where the leader points")
                         : QStringLiteral("Balloon: click the balloon location"));
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
        // In Arc mode each new vertex makes an arc segment tangent to the previous
        // one; draftBulges_ stays aligned with draftPts_ (bulge of the segment that
        // ENDS at this vertex; the very first segment is always straight).
        double bulge = 0.0;
        if (polyArcMode_ && tool_ == ToolId::Polyline && draftPts_.size() >= 2) {
            const Vec2 prevDir = draftPts_.back() - draftPts_[draftPts_.size() - 2];
            bulge = tangentBulge(draftPts_.back(), p, prevDir);
        }
        draftPts_.push_back(p);
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
        case EntityType::Text:
        case EntityType::Dimension:
            break; // fills / annotations are not boundaries
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
    case ToolId::Line:
    case ToolId::DoubleLine:
        if (draftPts_.size() < 2) return;
        e.type = (tool_ == ToolId::DoubleLine) ? EntityType::DoubleLine : EntityType::Line;
        e.a = draftPts_[0];
        e.b = draftPts_[1];
        if (e.type == EntityType::DoubleLine) {
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
    case ToolId::Balloon: {
        // A numbered callout with a leader: first point = arrow tip, second = centre.
        // Stored as a Circle carrying meta["balloon"], the arrow tip (leaderX/Y) and an
        // editable landing length. `a` == centre so the number is inline-editable.
        if (draftPts_.size() < 2) return;
        const Vec2 tip = draftPts_[0], c = draftPts_[1];
        e.type = EntityType::Circle;
        e.center = c;
        e.a = c;
        e.radius = 6.0;
        int next = 0;
        for (const auto& x : doc_->entities())
            if (x.meta.value("balloon").toBool())
                next = std::max(next, x.meta.value("text").toString().toInt());
        e.meta.insert("balloon", true);
        e.meta.insert("text", QString::number(next + 1));
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
        e.meta.insert("showLabel", true);
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

    pushUndo();
    addEntity(std::move(e));
    draftPts_.clear();
    drafting_ = false;
    emit changed();
    setTool(ToolId::Select); // finished: return to Select (Space repeats last tool)
    if (filletTooBig) // emit after setTool so the warning isn't overwritten
        emit message(QStringLiteral("Fillet radius %1 is too big for a corner — drawn with sharp corners")
                         .arg(cornerRadius_));
}

void Editor::cancelDraw() {
    lastHatchId_.clear(); // end the hatch live-edit session
    dimHasRef_ = false;   // forget any picked circle/arc for a radius/dia dimension
    dpolyLsArm_ = false;  // forget a pending/active line-start reference
    dpolyHasLsRef_ = false;
    draftPts_.clear();
    drafting_ = false;
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
                   : QStringLiteral("TRIM: click two points to draw a cut line across objects — each is "
                                    "trimmed to the next joint; Esc to finish");
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
                for (Entity* e : sel) scaleEntity(*e, base, std::max(1e-4, f));
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
                         : QStringLiteral("%1: pick the destination").arg(toolLabel(tool_)));
        if (tool_ == ToolId::Scale) // pop the factor field at the picked base point
            emit dynamicPrompt(QStringLiteral("Scale factor"));
        emit changed();
        return;
    }

    pushUndo();
    switch (tool_) {
    case ToolId::Move: {
        const Vec2 delta = modifyPts_[1] - modifyPts_[0];
        for (Entity* e : sel) translateEntity(*e, delta);
        break;
    }
    case ToolId::Rotate: {
        const Vec2 piv = modifyPts_[0];
        const double ang = std::atan2(modifyPts_[1].y - piv.y, modifyPts_[1].x - piv.x);
        for (Entity* e : sel) rotateEntity(*e, piv, ang);
        break;
    }
    case ToolId::Mirror: {
        for (Entity* e : sel) mirrorEntity(*e, modifyPts_[0], modifyPts_[1]);
        break;
    }
    case ToolId::Scale: {
        const Vec2 base = modifyPts_[0];
        const double ref = selectionRefLength(sel);
        const double f = ref > 1e-9 ? distance(base, modifyPts_[1]) / ref : 1.0;
        for (Entity* e : sel) scaleEntity(*e, base, std::max(1e-4, f));
        break;
    }
    case ToolId::Offset: {
        // Offset each selected entity toward the picked side by the picked dist.
        const Vec2 side = modifyPts_[1];
        for (Entity* e : sel) {
            Entity clone = *e;
            clone.id = uid();
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
            doc_->entities().push_back(clone);
        }
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

void Editor::applyTrimFence(const Vec2& a, const Vec2& b) {
    if (!doc_) return;
    std::vector<Entity> refs;
    for (const QString& id : trimRefIds_)
        if (const Entity* r = entityById(id)) refs.push_back(*r);
    if (refs.empty()) { emit message(QStringLiteral("Select the reference object first.")); return; }

    // "Until any joint": the trim boundaries are ALL intersections (the reference
    // plus every other object), so a piece is removed only up to the nearest joint,
    // not to the object's end. A snapshot keeps the cutter list stable while split
    // pieces are added.
    const std::vector<Entity> joints = doc_->entities();
    pushUndo();
    // Snapshot the ids first so trimming doesn't disturb the iteration.
    std::vector<QString> ids;
    for (const auto& e : doc_->entities()) ids.push_back(e.id);
    int count = 0;
    std::vector<Entity> additions; // split pieces, added after the loop
    for (const QString& id : ids) {
        if (std::find(trimRefIds_.begin(), trimRefIds_.end(), id) != trimRefIds_.end())
            continue; // never trim the reference itself
        Entity* obj = entityById(id);
        if (!obj) continue;
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
        } else if (obj->type == EntityType::Polyline) {
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
    for (auto& add : additions) addEntity(std::move(add));
    if (count == 0) {
        undo();
        emit message(QStringLiteral("The cut line crossed nothing that meets the reference."));
        return;
    }
    doc_->setDirty(true);
    // Keep the reference selected so more cut lines can be drawn; Esc finishes.
    emit message(QStringLiteral("Trimmed %1 object(s) — draw another cut line or Esc to finish").arg(count));
    emit changed();
}

void Editor::recordCutMeta(Entity& ent, const QString& selfId, const std::vector<Entity>& joints) {
    // Only meaningful for a DOUBLE entity (its physical runs get clipped to the cut).
    const std::vector<Vec2> pts =
        (ent.type == EntityType::DoubleLine) ? std::vector<Vec2>{ent.a, ent.b} : ent.points;
    if (pts.size() < 2) return;
    auto setEnd = [&](const QString& key, const Vec2& pt) {
        for (const char* s : {"", "Kind", "Ax", "Ay", "Bx", "By", "Cx", "Cy", "R"})
            ent.meta.remove(key + s);
        // Prefer a CIRCLE cut (clip runs onto the true ring) when the cutter through
        // this point is a circle/arc; otherwise a straight line cut.
        if (auto cc = cutCircleThrough(pt, joints, selfId)) {
            ent.meta.insert(key, true);
            ent.meta.insert(key + "Kind", QStringLiteral("circle"));
            ent.meta.insert(key + "Cx", cc->first.x);
            ent.meta.insert(key + "Cy", cc->first.y);
            ent.meta.insert(key + "R", cc->second);
        } else if (auto seg = cutSegmentThrough(pt, joints, selfId)) {
            ent.meta.insert(key, true);
            ent.meta.insert(key + "Kind", QStringLiteral("line"));
            ent.meta.insert(key + "Ax", seg->first.x);
            ent.meta.insert(key + "Ay", seg->first.y);
            ent.meta.insert(key + "Bx", seg->second.x);
            ent.meta.insert(key + "By", seg->second.y);
        }
    };
    setEnd(QStringLiteral("cutStart"), pts.front());
    setEnd(QStringLiteral("cutEnd"), pts.back());
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
            if (knee >= 0) {
                const double gap = dp->meta.value("gap").toDouble(10.0);
                const std::vector<Vec2> corner = {dp->points[knee - 1], dp->points[knee],
                                                  dp->points[knee + 1]};
                applyCornerModAt(*dp, knee, corner, gap);
            }
            pickIds_.clear();
            pickPts_.clear();
            emit changed();
            return;
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

    // Fillet TWO connected DOUBLE LINES: if they share a common endpoint, merge them
    // into a single double polyline with a rounded knee at the connection (so both
    // physical runs round with a constant gap).
    if ((tool_ == ToolId::Fillet || tool_ == ToolId::Chamfer) && l1 && l2 && l1 != l2 &&
        l1->type == EntityType::DoubleLine && l2->type == EntityType::DoubleLine) {
        const Vec2 e1[2] = {l1->a, l1->b};
        const Vec2 e2[2] = {l2->a, l2->b};
        int bi = 0, bj = 0;
        double bd = std::numeric_limits<double>::max();
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j) {
                const double dd = distance(e1[i], e2[j]);
                if (dd < bd) { bd = dd; bi = i; bj = j; }
            }
        if (bd > worldTolerance() * 4.0) {
            emit message(QStringLiteral("The two double lines must share a connected end."));
            pickIds_.clear(); pickPts_.clear();
            return;
        }
        const Vec2 shared = lerp(e1[bi], e2[bj], 0.5);
        const Vec2 free1 = e1[1 - bi], free2 = e2[1 - bj];
        const double gap = l1->meta.value("gap").toDouble(10.0);
        const std::vector<Vec2> corner = {free1, shared, free2};
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
        merged.points = {free1, shared, free2};
        merged.meta.insert(QStringLiteral("double"), true);
        merged.meta.insert(QStringLiteral("gap"), gap);
        QJsonArray arr;
        arr.append(0.0);
        arr.append(amt);
        arr.append(0.0);
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
            emit message(QStringLiteral("Fillet radius %1 is too big for these lines — not applied")
                             .arg(filletRadius_));
        } else {
            undo();
            emit message(QStringLiteral("Lines do not form a valid corner."));
        }
    } else {
        emit message(QStringLiteral("Pick two distinct lines."));
    }
    pickIds_.clear();
    pickPts_.clear();
    emit changed();
}

void Editor::arraySelected(int rows, int cols, double dx, double dy) {
    auto sel = selectedEntities();
    if (sel.empty()) { emit message(QStringLiteral("Select objects to array.")); return; }
    pushUndo();
    std::vector<Entity> clones;
    for (const Entity* e : sel) {
        auto part = rectArray(*e, rows, cols, dx, dy);
        clones.insert(clones.end(), part.begin(), part.end());
    }
    for (auto& c : clones) doc_->entities().push_back(std::move(c));
    doc_->setDirty(true);
    emit message(QStringLiteral("Array: %1 copies").arg(clones.size()));
    emit changed();
}

void Editor::explodeSelected() {
    auto sel = selectedEntities();
    if (sel.empty()) { emit message(QStringLiteral("Select objects to explode.")); return; }
    pushUndo();
    std::vector<Entity> additions;
    std::vector<QString> remove;
    for (const Entity* e : sel) {
        auto parts = explodeEntity(*e);
        if (!parts.empty()) {
            additions.insert(additions.end(), parts.begin(), parts.end());
            remove.push_back(e->id);
        }
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
        break;
    case ToolId::Splice:
        e.type = EntityType::Splice;
        e.layer = QStringLiteral("Wires");
        e.a = p;
        break;
    case ToolId::NotConnected:
        e.type = EntityType::NotConnected;
        e.a = p;
        break;
    case ToolId::NetLabel:
        e.type = EntityType::NetLabel;
        e.a = p;
        e.meta.insert("text", wireNet_.isEmpty() ? QStringLiteral("NET") : wireNet_);
        break;
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
    addEntity(std::move(e));
    emit message(QStringLiteral("%1 placed").arg(toolLabel(tool_)));
    emit changed();
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

void Editor::selectById(const QString& id, bool additive) {
    if (!additive) selected_.clear();
    if (!entityById(id)) { emit selectionChanged(); emit changed(); return; }
    auto it = std::find(selected_.begin(), selected_.end(), id);
    if (it == selected_.end()) selected_.push_back(id);
    emit selectionChanged();
    emit changed();
}

void Editor::clearSelection() {
    selected_.clear();
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

void Editor::setSelectionMetaNumber(const QString& key, double value) {
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

void Editor::setSelectionDimMeta(const QString& key, const QJsonValue& value) {
    auto sel = selectedEntities();
    if (sel.empty()) return;
    pushUndo();
    for (Entity* e : sel)
        if (e->type == EntityType::Dimension) e->meta[key] = value;
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
        std::vector<QString> newSel;
        for (const Entity& g : rightDragGhost_) {
            Entity e = rightDragResult(g);
            e.id = uid();
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

void Editor::paste() {
    if (!doc_ || clipboard_.empty()) {
        emit message(QStringLiteral("Clipboard is empty"));
        return;
    }
    awaitClipBase_ = false;
    pastePlacing_ = true; // the contents now follow the cursor until clicked to place
    emit message(QStringLiteral("Specify insertion point"));
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
    // Ctrl while drawing an arc flips it to the opposite (mirrored) sweep, AutoCAD-
    // style. A bare Ctrl press arrives as Key_Control.
    if (key == Qt::Key_Control && tool_ == ToolId::Arc && drafting_) {
        toggleArcMirror();
        return true;
    }
    if (key == Qt::Key_Escape) {
        // Cancel any in-progress operation and clear the whole selection.
        // (Trim finishes here too: setTool(Select) below clears the reference/cut
        // line, so Esc ends the trim command.)
        // A grabbed joint that already moved is restored via undo.
        if (editing_ && editMoved_) undo();
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
        // Finish an in-progress multi-point draw; otherwise repeat the last tool.
        if (drafting_) {
            // Line commits each segment as it goes, so Enter just ends the current
            // run and stays in the Line tool, ready for the next run (Esc exits).
            if (tool_ == ToolId::Line) {
                draftPts_.clear();
                drafting_ = false;
                emit message(QStringLiteral("Line: pick a start point, or Esc to exit"));
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
    if (ctrl && key == Qt::Key_V) { paste(); return true; }
    if (ctrl && key == Qt::Key_Z) { undo(); return true; }
    if (ctrl && (key == Qt::Key_Y)) { redo(); return true; }
    return false;
}

// ---- Overlay painting ------------------------------------------------------

void Editor::drawOverlay(QPainter& p, const ViewTransform& vt) const {
    if (!doc_) return;

    // Right-button drag: preview the selection mirrored (X/Y) about the catch point
    // and translated to the current offset.
    if (rightDrag_) {
        const QColor ghost(120, 200, 255, 160);
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
                QPen gp(QColor("#19ff7e")); // bright solid green polar lock line
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
        const QColor ghost(120, 200, 255, 170);
        for (Entity e : clipboard_) {
            translateEntity(e, off);
            EntityRenderer::draw(p, e, *doc_, vt, ghost);
        }
        const QPointF c = vt.toScreen(cursor_);
        QPen mp(QColor("#ffd166"));
        mp.setCosmetic(true);
        p.setPen(mp);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(c.x() - 7, c.y()), QPointF(c.x() + 7, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - 7), QPointF(c.x(), c.y() + 7));
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
        const QColor line = windowMode ? QColor("#cfe3ff") : QColor("#d9ffe6");
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
            p.setPen(QPen(QColor("#9ec8ff")));
            p.setBrush(QColor("#101720"));
            p.drawRect(box);
            p.setPen(QPen(QColor("#dcecff")));
            p.drawText(box, Qt::AlignCenter, label);
            p.setBrush(Qt::NoBrush);
        }
    }

    // Source ghost: while a move / joint drag is in progress, show the entities at
    // their ORIGINAL position (faint) so it's clear where they were taken from,
    // until the placement click commits the new position.
    if (editing_ && !editGhost_.empty()) {
        const QColor ghost(150, 162, 178, 110);
        for (const auto& e : editGhost_)
            EntityRenderer::draw(p, e, *doc_, vt, ghost);
    }

    // Selection marking: highlight each selected entity's geometry + draw grips.
    if (!selected_.empty()) {
        const QColor hi("#2bd0ff");
        for (const auto& e : doc_->entities()) {
            if (std::find(selected_.begin(), selected_.end(), e.id) == selected_.end())
                continue;
            EntityRenderer::draw(p, e, *doc_, vt, hi); // re-stroke in highlight color
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
                p.setPen(QColor("#bfe6ff"));
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

    // In-progress draft preview (white, matching the line being drawn).
    if (drafting_ && !draftPts_.empty()) {
        QPen prev(QColor("#ffffff"));
        prev.setCosmetic(true);
        prev.setWidthF(1.4);
        prev.setStyle(Qt::DashLine);
        p.setPen(prev);
        p.setBrush(Qt::NoBrush);

        std::vector<Vec2> pts = draftPts_;
        pts.push_back(cursor_);
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
        } else if (tool_ == ToolId::Balloon && pts.size() >= 2) {
            // Preview the leader from the arrow tip to the cursor + the balloon circle.
            p.drawLine(vt.toScreen(pts[0]), vt.toScreen(pts[1]));
            const QPointF cc = vt.toScreen(pts[1]);
            const double rr = 6.0 * vt.scale();
            p.drawEllipse(cc, rr, rr);
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
            if (ok) EntityRenderer::draw(p, temp, *doc_, vt, QColor("#ffffff"));
        } else if (tool_ == ToolId::DoubleLine && pts.size() >= 2) {
            const Vec2 perp = (pts[1] - pts[0]).normalized().perp();
            const Vec2 off = perp * (doubleLineGap_ / 2.0);
            std::vector<Vec2> r1 = {pts[0] + off, pts[1] + off};
            std::vector<Vec2> r2 = {pts[0] - off, pts[1] - off};
            extendLsStart(r1);
            extendLsStart(r2);
            p.drawLine(vt.toScreen(r1[0]), vt.toScreen(r1[1]));
            p.drawLine(vt.toScreen(r2[0]), vt.toScreen(r2[1]));
        } else if (tool_ == ToolId::DoublePolyline && pts.size() >= 2) {
            // Round each filleted corner of the centerline at radius + gap/2 so both
            // runs fillet, then offset.
            const double h = doubleLineGap_ / 2.0;
            std::vector<double> centerRadii(previewRadii.size(), 0.0);
            for (size_t i = 0; i < previewRadii.size(); ++i)
                if (previewRadii[i] > 0) centerRadii[i] = previewRadii[i] + h;
            const std::vector<Vec2> center =
                previewAnyFillet ? roundedPolylinePerCorner(pts, centerRadii) : pts;
            std::vector<Vec2> r1 = offsetPolyline(center, h);
            std::vector<Vec2> r2 = offsetPolyline(center, -h);
            extendLsStart(r1); // ls: start both runs on the reference line
            extendLsStart(r2);
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
                drawDraftValueBox(p, labelAt, valueLabel, QColor("#ffd166"));
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
                EntityRenderer::draw(p, *a, *doc_, vt, QColor("#ffffff"));
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
                QPen gp(centre ? QColor("#19ff7e") : QColor("#9aa6b6"));
                gp.setCosmetic(true);
                gp.setWidthF(1.4);
                if (!centre) gp.setStyle(Qt::DotLine);
                p.setPen(gp);
                p.setBrush(Qt::NoBrush);
                p.drawLine(fs, qs);
                if (centre) { // mark the centre
                    p.setBrush(QColor("#19ff7e"));
                    p.drawEllipse(fs, 2.6, 2.6);
                    p.setBrush(Qt::NoBrush);
                }
                double deg = std::atan2(d.y, d.x) * 180.0 / M_PI;
                if (deg < 0) deg += 360.0;
                const QString lbl =
                    centre ? QStringLiteral("R %1   %2\xC2\xB0").arg(guideR, 0, 'f', 2).arg(deg, 0, 'f', 1)
                           : QStringLiteral("%1   %2\xC2\xB0").arg(len, 0, 'f', 2).arg(deg, 0, 'f', 1);
                drawDraftValueBox(p, qs, lbl, QColor("#ffd166"));
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
                EntityRenderer::draw(p, *el, *doc_, vt, QColor("#ffffff"));
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
                QPen gp(QColor("#19ff7e"));
                gp.setCosmetic(true);
                gp.setWidthF(1.4);
                p.setPen(gp);
                p.setBrush(QColor("#19ff7e"));
                p.drawEllipse(fs, 2.6, 2.6); // mark the centre / axis start
                p.setBrush(Qt::NoBrush);
                p.drawLine(fs, qs);
                drawDraftValueBox(p, qs, lbl, QColor("#ffd166"));
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
            QPen cpen(QColor("#ffffff"));
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
                p.setPen(QPen(QColor("#ffd166")));
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
                QPen hp(QColor("#2bd0ff"));
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
                QPen hp(QColor("#2bd0ff"));
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
        const QColor mark("#ffcf3f");
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
            QPen pv(QColor("#19ff7e"));
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
            QPen gp(QColor(120, 200, 255, 200)); // ghost of the extended result
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
        QPen fp(QColor("#19ff7e"));
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
        const QColor ghost(120, 200, 255, 170);
        for (const Entity& g : modifyPreview()) EntityRenderer::draw(p, g, *doc_, vt, ghost);
    }

    // Offset tool preview: while defining the distance, rubber-band the two points;
    // once an object is picked, ghost the offset copy on the cursor's side.
    if (tool_ == ToolId::Offset) {
        if (!offsetHasDist_ && offsetDistPts_.size() == 1) {
            QPen pv(QColor("#19ff7e"));
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
                    EntityRenderer::draw(p, *g, *doc_, vt, QColor(120, 200, 255, 210));
        }
    }

    // Snap marker.
    if (snapResult_.snapped && snapResult_.kind != SnapKind::Grid &&
        snapResult_.kind != SnapKind::Ortho && snapResult_.kind != SnapKind::Polar) {
        const QPointF s = vt.toScreen(snapResult_.point);
        QPen sp(QColor("#ffd166"));
        sp.setCosmetic(true);
        sp.setWidthF(1.6);
        p.setPen(sp);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(s.x() - 5, s.y() - 5, 10, 10));
    }
}

} // namespace Edraw_Desktop
