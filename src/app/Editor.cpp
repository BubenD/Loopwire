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
                      ArcStart, ArcEnd, ArcMid, EllipseMajor, EllipseMinor };
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

std::vector<Grip> gripsOf(const Entity& e) {
    std::vector<Grip> g;
    switch (e.type) {
    case EntityType::Line:
    case EntityType::Dimension:
        g.push_back({e.a, GripRole::LineA, 0});
        g.push_back({lerp(e.a, e.b, 0.5), GripRole::LineMid, 0});
        g.push_back({e.b, GripRole::LineB, 0});
        break;
    case EntityType::DoubleLine: {
        // Joints on the two VISIBLE parallel lines only (no confusing centerline
        // joint between them). Each end grip carries its offset so dragging it
        // keeps that point under the cursor.
        const Vec2 mid = lerp(e.a, e.b, 0.5);
        const double gap = e.meta.value("gap").toDouble(10.0);
        const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
        const Vec2 noff = off * -1.0;
        g.push_back({e.a + off, GripRole::LineA, 0, off});
        g.push_back({e.a + noff, GripRole::LineA, 0, noff});
        g.push_back({e.b + off, GripRole::LineB, 0, off});
        g.push_back({e.b + noff, GripRole::LineB, 0, noff});
        // Middle joint at each END (on the centerline, between that end's two
        // corners): grabbing it moves the whole end (both lines) to the cursor,
        // keeping it centered. off={0,0} so it tracks the centerline directly.
        g.push_back({e.a, GripRole::LineA, 0});
        g.push_back({e.b, GripRole::LineB, 0});
        // A single middle joint on the centerline length-center: moves the whole
        // double line. None of these centerline joints are region-selection points,
        // so a crossing box passing between the lines won't select via them.
        g.push_back({mid, GripRole::LineMid, 0});
        break;
    }
    case EntityType::Polyline:
    case EntityType::Spline:
    case EntityType::Wire:
    case EntityType::Polygon:
    case EntityType::Hatch:
    case EntityType::Wipeout:
        for (int i = 0; i < static_cast<int>(e.points.size()); ++i)
            g.push_back({e.points[i], GripRole::Vertex, i});
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
    default: // text / splice / netlabel / notconnected / connector
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
    tool_ = t;
    if (t != ToolId::Select) lastTool_ = t; // remember for repeat (Space/Enter)
    emit toolChanged(t);
    emit message(toolLabel(t));
    emit changed();
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
    doc_->entities().push_back(std::move(e));
    doc_->setDirty(true);
}

// ---- Hit testing ----------------------------------------------------------

namespace {

double distanceToEntity(const Entity& e, const Vec2& p) {
    auto segDist = [&](const Vec2& a, const Vec2& b) {
        return closestPointOnSegment(p, a, b).distance;
    };
    double best = std::numeric_limits<double>::max();
    switch (e.type) {
    case EntityType::Line:
    case EntityType::Dimension:
        best = segDist(e.a, e.b);
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
    case EntityType::Spline:
    case EntityType::Wire:
        for (size_t i = 1; i < e.points.size(); ++i)
            best = std::min(best, segDist(e.points[i - 1], e.points[i]));
        break;
    case EntityType::Rect:
    case EntityType::Polygon:
    case EntityType::Hatch:
    case EntityType::Wipeout:
        for (size_t i = 0; i < e.points.size(); ++i)
            best = std::min(best, segDist(e.points[i], e.points[(i + 1) % e.points.size()]));
        break;
    case EntityType::Circle:
    case EntityType::Arc:
        best = std::abs(distance(e.center, p) - e.radius);
        break;
    case EntityType::Ellipse: {
        // Coarse: distance to the bounding ellipse via normalized radius.
        const double rx = std::max(1e-6, e.radius), ry = std::max(1e-6, e.radiusY);
        const Vec2 d = p - e.center;
        const double r = std::hypot(d.x / rx, d.y / ry);
        best = std::abs(r - 1.0) * std::min(rx, ry);
        break;
    }
    case EntityType::Text:
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

void Editor::onPointerMove(const Vec2& raw) {
    if (!doc_) return;
    std::optional<Vec2> anchor;
    if (drafting_ && !draftPts_.empty()) anchor = draftPts_.back();
    else if (!modifyPts_.empty()) anchor = modifyPts_.back();
    else if (editing_ && editMode_ == 1) anchor = editAnchor_; // ortho/polar vs fixed end

    std::vector<QString> ignore;
    if (editing_ && editMode_ == 1) ignore.push_back(editEntityId_); // don't snap to self
    snapResult_ = resolveSnap(*doc_, raw, anchor, worldTolerance(), snap_, ignore);
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
    if (tool_ == ToolId::Select && regionMode_ == RegionMode::Lasso && pressed_) {
        const double tol = worldTolerance();
        if (dragPath_.empty() || distance(dragPath_.back(), raw) > tol * 0.4)
            dragPath_.push_back(raw);
    }

    // Rubber-band preview (line/rect/etc. while drawing), modify reference
    // points, and region selection follow the cursor: repaint only, no panel
    // rebuild, so the preview tracks the cursor without lag.
    if (drafting_ || !modifyPts_.empty() || regionMode_ != RegionMode::None || pastePlacing_)
        emit overlayChanged();
}

void Editor::onDragTick() {
    // Fires only while the button is held during the Deciding phase. If the
    // hold passes the threshold, promote the rectangle into a lasso.
    if (!pressed_ || tool_ != ToolId::Select || regionMode_ != RegionMode::Deciding) {
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

    if (tool_ == ToolId::Select) {
        const bool shift = mods & Qt::ShiftModifier;

        // (a) A grabbed joint is following the cursor: this click places it.
        if (editing_ && gripGrab_) {
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
        // Shift is reserved for deselecting, so it never grabs a joint. Use the
        // RAW click (not the snapped cursor) so the grip you clicked is the one
        // grabbed — snapping can pull the cursor off the visible joint (e.g. a
        // double line's parallel end) and otherwise cause the grab to miss.
        if (!shift && beginGripGrab(raw)) return;

        // (d) Click on an entity body -> accumulate selection (Shift removes),
        // and arm a body move on drag.
        if (auto hit = hitTest(p)) {
            auto it = std::find(selected_.begin(), selected_.end(), *hit);
            if (shift) {
                if (it != selected_.end()) selected_.erase(it);
                emit selectionChanged();
                return; // deselected; no move
            }
            if (it == selected_.end()) { selected_.push_back(*hit); emit selectionChanged(); }
            pressed_ = true;
            editing_ = true;
            editMode_ = 0; // body move on drag
            gripGrab_ = false;
            editEntityId_.clear();
            editLast_ = p;
            editMoved_ = false;
            // Snapshot the originals so they stay visible (ghost) at the source
            // position until the move is committed on release.
            editGhost_.clear();
            for (const auto& sid : selected_)
                if (Entity* se = entityById(sid)) editGhost_.push_back(*se);
            return;
        }
        // (e) Empty space -> region selection (rectangle / hold-for-lasso).
        pressed_ = true;
        anchor_ = p;
        pressMods_ = mods;
        dragPath_ = {p};
        regionMode_ = RegionMode::Deciding;
        pressTimer_.restart();
        dragTimer_->start();
        return;
    }
    if (isDrawTool(tool_)) { handleDrawPress(p); return; }
    if (isPickModifyTool(tool_)) { handlePickModifyPress(p); return; }
    if (isPlacementTool(tool_)) { handlePlacementPress(p); return; }
    handleModifyPress(p);
}

void Editor::onPointerRelease(const Vec2& raw, Qt::MouseButton button) {
    Q_UNUSED(raw);
    if (!doc_ || button != Qt::LeftButton || tool_ != ToolId::Select) return;
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

// Representative points used to test whether an entity is inside a region.
std::vector<Vec2> entitySelectPoints(const Entity& e) {
    std::vector<Vec2> pts;
    switch (e.type) {
    case EntityType::Line:
    case EntityType::Dimension:
        pts = {e.a, lerp(e.a, e.b, 0.5), e.b};
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
    case EntityType::Polyline:
    case EntityType::Spline:
    case EntityType::Wire:
    case EntityType::Rect:
    case EntityType::Polygon:
    case EntityType::Hatch:
    case EntityType::Wipeout:
        pts = e.points;
        break;
    case EntityType::Circle:
    case EntityType::Arc:
        pts = {e.center, {e.center.x + e.radius, e.center.y},
               {e.center.x - e.radius, e.center.y}, {e.center.x, e.center.y + e.radius},
               {e.center.x, e.center.y - e.radius}};
        break;
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
    case EntityType::Dimension:
        segs.emplace_back(e.a, e.b);
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
    case EntityType::Rect:
    case EntityType::Polygon:
    case EntityType::Hatch:
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

        bool hit = false;
        if (e.type == EntityType::DoubleLine) {
            // A crossing region must TOUCH a visible line (cross its body or contain
            // it) — not merely catch a joint. So decide purely from the two visible
            // segments: crossing = a segment crosses the region boundary or lies
            // inside; window = both segments fully enclosed.
            const auto segs = entitySelectSegments(e);
            if (crossing) {
                for (const auto& s : segs) {
                    if (pointInPolygon(region, s.first) || pointInPolygon(region, s.second)) { hit = true; break; }
                    for (const auto& re : regionEdges)
                        if (segmentSegmentIntersection(s.first, s.second, re.first, re.second)) { hit = true; break; }
                    if (hit) break;
                }
            } else {
                hit = !segs.empty();
                for (const auto& s : segs)
                    if (!(pointInPolygon(region, s.first) && pointInPolygon(region, s.second))) { hit = false; break; }
            }
        } else if (crossing) {
            hit = inside > 0;
            if (!hit) { // also catch segments that cross the region boundary
                for (const auto& s : entitySelectSegments(e)) {
                    for (const auto& re : regionEdges)
                        if (segmentSegmentIntersection(s.first, s.second, re.first, re.second)) { hit = true; break; }
                    if (hit) break;
                }
            }
        } else {
            hit = inside == static_cast<int>(pts.size()); // fully enclosed
        }
        if (!hit) continue;
        auto it = std::find(selected_.begin(), selected_.end(), e.id);
        if (subtract) {
            if (it != selected_.end()) selected_.erase(it); // deselect
        } else if (it == selected_.end()) {
            selected_.push_back(e.id); // accumulate
        }
    }
    emit selectionChanged();
}

void Editor::drawGrips(QPainter& p, const Entity& e, const ViewTransform& vt) const {
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

bool Editor::beginGripGrab(const Vec2& at) {
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
            if (isPointJoint(gr.role)) {
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
                    e->points[index] = target;
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
        case GripRole::Center: translateEntity(*e, delta); break;
        case GripRole::Radius: e->radius = std::max(1e-6, distance(e->center, target)); break;
        case GripRole::Move: translateEntity(*e, delta); break;
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
        for (const auto& eg : editGrips_)
            if (Entity* e = entityById(eg.id))
                applyOne(e, static_cast<GripRole>(eg.role), eg.index, eg.offset);
    }
    editLast_ = target;
    doc_->setDirty(true);
}

void Editor::onDoubleClick(const Vec2&) {
    if (drafting_ && (tool_ == ToolId::Polyline || tool_ == ToolId::DoublePolyline ||
                      tool_ == ToolId::Spline || tool_ == ToolId::Wire))
        finishDraw();
}

// ---- Draw tools ------------------------------------------------------------

void Editor::handleDrawPress(const Vec2& p) {
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
    case ToolId::Dimension:
        draftPts_.push_back(p);
        drafting_ = true;
        if (draftPts_.size() == 2) finishDraw();
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
    case ToolId::Ellipse:
        // All modes are 3-point (AutoCAD): axis (two points or center+endpoint)
        // then a distance for the other half-axis.
        draftPts_.push_back(p);
        drafting_ = true;
        if (draftPts_.size() == 3) finishDraw();
        break;
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
        drafting_ = true;
        emit message(QStringLiteral("%1: click next point — [A]rc/[L]ine, [C]lose, "
                                    "[U]ndo, Enter to finish")
                         .arg(toolLabel(tool_)));
        break;
    }
    case ToolId::Text:
        draftPts_ = {p};
        emit textPromptRequested(QStringLiteral("Text:"));
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

} // namespace

void Editor::finishDraw() {
    if (!doc_ || draftPts_.empty()) { cancelDraw(); return; }
    Entity e;
    e.layer = doc_->activeLayer();

    switch (tool_) {
    case ToolId::Line:
    case ToolId::DoubleLine:
        if (draftPts_.size() < 2) return;
        e.type = (tool_ == ToolId::DoubleLine) ? EntityType::DoubleLine : EntityType::Line;
        e.a = draftPts_[0];
        e.b = draftPts_[1];
        if (e.type == EntityType::DoubleLine) e.meta.insert("gap", doubleLineGap_);
        break;
    case ToolId::Dimension:
        if (draftPts_.size() < 2) return;
        e.type = EntityType::Dimension;
        e.a = draftPts_[0];
        e.b = draftPts_[1];
        e.points = {draftPts_[0], draftPts_[1]};
        break;
    case ToolId::Rect: {
        if (draftPts_.size() < 2) return;
        const Vec2 a = draftPts_[0], b = draftPts_[1];
        e.type = EntityType::Rect;
        e.points = {{a.x, a.y}, {b.x, a.y}, {b.x, b.y}, {a.x, b.y}};
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
            if (ellipseMode_ == EllipseMode::EllipticalArc)
                emit message(QStringLiteral("Elliptical arc: drawn as a full ellipse"));
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
        }
        if (e.type == EntityType::Polyline && cornerRadius_ > 0.0)
            e.meta.insert("cornerRadius", cornerRadius_);
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
}

void Editor::cancelDraw() {
    draftPts_.clear();
    drafting_ = false;
    rectAwaitY_ = false;
    drawFieldIndex_ = 0;
    drawFieldVals_.clear();
    draftBulges_.clear();
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
    case ToolId::Dimension:
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
        return QStringLiteral("DOUBLE POLYLINE: [G] gap  ·  [F] fillet  ·  [C] close  ·  [U] undo  ·  Enter finishes");
    case ToolId::DoubleLine:
        return QStringLiteral("DOUBLE LINE: [G] gap  ·  click two points");
    case ToolId::Spline:
        return QStringLiteral("SPLINE: click points  ·  [C] close  ·  [U] undo  ·  Enter finishes");
    case ToolId::Wire:
        return QStringLiteral("WIRE: click points  ·  double-click / Enter finishes");
    case ToolId::Arc:
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
        return QStringLiteral("POLYGON: type # sides, pick centre then a vertex");
    case ToolId::Dimension:
        return QStringLiteral("DIMENSION: pick two points");
    case ToolId::Text:
        return QStringLiteral("TEXT: pick a point, then type the text");
    default:
        return QString(); // Select / modify tools have no draw hint
    }
}

bool Editor::isDrawOptionChar(QChar ch) const {
    const QChar c = ch.toLower();
    switch (tool_) {
    case ToolId::Polyline:       return c == 'c' || c == 'u' || c == 'f' || c == 'a' || c == 'l';
    case ToolId::DoublePolyline: return c == 'c' || c == 'u' || c == 'f' || c == 'g';
    case ToolId::Spline:         return c == 'c' || c == 'u';
    case ToolId::DoubleLine:     return c == 'g';
    case ToolId::Arc:            return c == 'm' || c == 'i'; // "mi" mirror
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
    case ToolId::Wire:
    case ToolId::Dimension: {
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

    // Awaiting a fillet radius after the user typed "f" (0 = sharp corners).
    if (filletInputMode_) {
        bool ok = false;
        const double r = t.toDouble(&ok);
        if (ok && r >= 0) {
            cornerRadius_ = r;
            emit message(r > 0 ? QStringLiteral("Fillet radius = %1").arg(r)
                               : QStringLiteral("Sharp corners"));
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
            return true;
        }
        // Any other active draw tool consumes "g" with a hint — it must never fall
        // through to run the "g" command word and switch tools mid-draw.
        if (drafting_ || isDrawTool(tool_)) {
            emit message(QStringLiteral("Gap applies to double line/polyline"));
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
            return true;
        }
        // Any other active draw tool (e.g. Line) consumes "f" with a hint instead
        // of running the "fillet" command and jumping into the Fillet tool.
        if (drafting_ || isDrawTool(tool_)) {
            emit message(QStringLiteral("Fillet applies to polylines"));
            return true;
        }
    }

    // Polyline-family options offered "attached" while drawing: Close / Undo
    // (AutoCAD PLINE). "c" closes the run back to the start; "u" removes the last
    // point. Only active while a polyline/spline/wire draft is in progress.
    const bool plineFamily = tool_ == ToolId::Polyline || tool_ == ToolId::DoublePolyline ||
                             tool_ == ToolId::Spline || tool_ == ToolId::Wire;
    if (plineFamily && drafting_) {
        if (t.compare(QStringLiteral("c"), Qt::CaseInsensitive) == 0 ||
            t.compare(QStringLiteral("close"), Qt::CaseInsensitive) == 0) {
            if (draftPts_.size() >= 3) {
                draftPts_.push_back(draftPts_.front()); // close back to the start
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

void Editor::provideText(const QString& text) {
    if (tool_ == ToolId::Text && !draftPts_.empty() && !text.isEmpty()) {
        Entity e;
        e.type = EntityType::Text;
        e.layer = doc_->activeLayer();
        e.a = draftPts_[0];
        e.meta.insert("text", text);
        e.meta.insert("size", 5.0);
        pushUndo();
        addEntity(std::move(e));
    }
    draftPts_.clear();
    emit changed();
    setTool(ToolId::Select); // return to Select after placing text
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

    const size_t need = (tool_ == ToolId::Move || tool_ == ToolId::Offset) ? 2
                        : (tool_ == ToolId::Mirror) ? 2
                                                    : 3; // Rotate/Scale
    if (modifyPts_.size() < need) {
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
        const double a0 = std::atan2(modifyPts_[1].y - piv.y, modifyPts_[1].x - piv.x);
        const double a1 = std::atan2(modifyPts_[2].y - piv.y, modifyPts_[2].x - piv.x);
        for (Entity* e : sel) rotateEntity(*e, piv, a1 - a0);
        break;
    }
    case ToolId::Mirror: {
        for (Entity* e : sel) mirrorEntity(*e, modifyPts_[0], modifyPts_[1]);
        break;
    }
    case ToolId::Scale: {
        const Vec2 base = modifyPts_[0];
        const double ref = distance(base, modifyPts_[1]);
        const double cur = distance(base, modifyPts_[2]);
        const double f = ref > 1e-9 ? cur / ref : 1.0;
        for (Entity* e : sel) scaleEntity(*e, base, f);
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

// ---- Pick-modify tools (trim / extend / fillet / chamfer) ------------------

void Editor::handlePickModifyPress(const Vec2& p) {
    auto hit = hitTest(p);

    if (tool_ == ToolId::Trim || tool_ == ToolId::Extend) {
        if (!hit) { emit message(QStringLiteral("Pick a line to modify.")); return; }
        Entity* line = entityById(*hit);
        if (!line || line->type != EntityType::Line) {
            emit message(QStringLiteral("%1 currently supports straight lines.").arg(toolLabel(tool_)));
            return;
        }
        pushUndo();
        const bool ok = (tool_ == ToolId::Trim)
                            ? trimLineAt(*line, p, doc_->entities())
                            : extendLineTo(*line, p, doc_->entities());
        if (!ok) { undo(); emit message(QStringLiteral("No boundary found.")); return; }
        doc_->setDirty(true);
        emit changed();
        return;
    }

    // Fillet / Chamfer: pick two lines.
    if (!hit) { emit message(QStringLiteral("Pick the first line.")); return; }
    pickIds_.push_back(*hit);
    pickPts_.push_back(p);
    if (pickIds_.size() < 2) {
        emit message(QStringLiteral("%1: pick the second line.").arg(toolLabel(tool_)));
        emit changed();
        return;
    }

    Entity* l1 = entityById(pickIds_[0]);
    Entity* l2 = entityById(pickIds_[1]);
    if (l1 && l2 && l1 != l2 && l1->type == EntityType::Line && l2->type == EntityType::Line) {
        pushUndo();
        CornerResult r = (tool_ == ToolId::Fillet)
                             ? filletLines(*l1, *l2, filletRadius_, pickPts_[0], pickPts_[1])
                             : chamferLines(*l1, *l2, chamferDist_, chamferDist_, pickPts_[0], pickPts_[1]);
        if (r.ok) {
            addEntity(std::move(r.connector));
            emit message(QStringLiteral("%1 applied").arg(toolLabel(tool_)));
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

void Editor::purgeSelected() {
    auto sel = selectedEntities();
    if (sel.empty()) { emit message(QStringLiteral("Select objects to purge.")); return; }
    pushUndo();
    std::vector<Entity> additions;
    std::vector<QString> remove;
    for (const Entity* e : sel) {
        auto parts = purgeEntity(*e);
        if (!parts.empty()) {
            additions.insert(additions.end(), parts.begin(), parts.end());
            remove.push_back(e->id);
        }
    }
    if (additions.empty()) {
        undo();
        emit message(QStringLiteral("Selection is already made of single primitives"));
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
    emit message(QStringLiteral("Purged into %1 primitives").arg(additions.size()));
    emit changed();
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
    if (key == Qt::Key_Escape) {
        // Cancel any in-progress operation and clear the whole selection.
        // A grabbed joint that already moved is restored via undo.
        if (editing_ && editMoved_) undo();
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
    if (ctrl && key == Qt::Key_X) { copyWithBase(true); return true; }           // cut w/ base
    if (ctrl && key == Qt::Key_V) { paste(); return true; }
    if (ctrl && key == Qt::Key_Z) { undo(); return true; }
    if (ctrl && (key == Qt::Key_Y)) { redo(); return true; }
    return false;
}

// ---- Overlay painting ------------------------------------------------------

void Editor::drawOverlay(QPainter& p, const ViewTransform& vt) const {
    if (!doc_) return;

    // Polar tracking guide: when the cursor has locked onto a polar angle, draw a
    // bright green ray FROM the reference point TOWARD the drawing direction,
    // continuing to the canvas edge. The reference is the draft anchor while
    // drawing, or the untouched joint point while moving a joint.
    if (snapResult_.snapped && snapResult_.kind == SnapKind::Polar) {
        std::optional<Vec2> ref;
        if (drafting_ && !draftPts_.empty()) ref = draftPts_.back();
        else if (editing_ && editMode_ == 1) ref = editAnchor_;
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
        if (tool_ == ToolId::Rect && pts.size() >= 2) {
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
        } else if (tool_ == ToolId::DoubleLine && pts.size() >= 2) {
            const Vec2 off = (pts[1] - pts[0]).normalized().perp() * (doubleLineGap_ / 2.0);
            p.drawLine(vt.toScreen(pts[0] + off), vt.toScreen(pts[1] + off));
            p.drawLine(vt.toScreen(pts[0] - off), vt.toScreen(pts[1] - off));
        } else if (tool_ == ToolId::DoublePolyline && pts.size() >= 2) {
            const double h = doubleLineGap_ / 2.0;
            for (double s : {h, -h}) {
                std::vector<Vec2> run = offsetPolyline(pts, s);
                if (cornerRadius_ > 0) run = roundedPolyline(run, cornerRadius_);
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
            if (auto a = buildArcEntity(arcMode_, pts, arcMirror_)) {
                EntityRenderer::draw(p, *a, *doc_, vt, QColor("#ffffff"));
            } else {
                QPolygonF poly;
                for (const auto& v : pts) poly << vt.toScreen(v);
                p.drawPolyline(poly);
            }
        } else if (tool_ == ToolId::Ellipse) {
            // Live (rotated) ellipse preview; before the 3rd point, a rubber line.
            if (auto el = buildEllipseEntity(ellipseMode_, pts)) {
                EntityRenderer::draw(p, *el, *doc_, vt, QColor("#ffffff"));
            } else {
                QPolygonF poly;
                for (const auto& v : pts) poly << vt.toScreen(v);
                p.drawPolyline(poly);
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
                (tool_ == ToolId::Polyline && cornerRadius_ > 0)
                    ? roundedPolyline(pts, cornerRadius_)
                    : pts;
            QPolygonF poly;
            for (const auto& v : show) poly << vt.toScreen(v);
            p.drawPolyline(poly);
        }

        // Live length / angle readout of the current segment (toggle: Angles).
        if (showAngles_ && tool_ != ToolId::Rect && tool_ != ToolId::Circle &&
            tool_ != ToolId::Ellipse) {
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
