#include "app/Editor.h"

#include "geometry/Geometry.h"
#include "tools/EntityTransforms.h"
#include "tools/ModifyOps.h"

#include <QApplication>
#include <QPainter>
#include <QPolygonF>
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

// Editable handles ("joints") for an entity, shown as grips when selected and
// draggable for direct manipulation.
enum class GripRole { Move, LineA, LineB, LineMid, Vertex, RectCorner, RectEdge, Center, Radius };
struct Grip { Vec2 pos; GripRole role; int index; };

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
        const Vec2 mid = lerp(e.a, e.b, 0.5);
        g.push_back({e.a, GripRole::LineA, 0});
        g.push_back({mid, GripRole::LineMid, 0});
        g.push_back({e.b, GripRole::LineB, 0});
        // Joints at the ends and middles of the two actual parallel lines.
        const double gap = e.meta.value("gap").toDouble(10.0);
        const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
        g.push_back({e.a + off, GripRole::LineA, 0});
        g.push_back({e.a - off, GripRole::LineA, 0});
        g.push_back({e.b + off, GripRole::LineB, 0});
        g.push_back({e.b - off, GripRole::LineB, 0});
        g.push_back({mid + off, GripRole::LineMid, 0}); // middle of top line
        g.push_back({mid - off, GripRole::LineMid, 0}); // middle of bottom line
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
    case EntityType::Arc:
    case EntityType::Ellipse:
        g.push_back({e.center, GripRole::Center, 0});
        break;
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
    case EntityType::DoubleLine:
    case EntityType::Dimension:
        best = segDist(e.a, e.b);
        break;
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
    if (drafting_ || !modifyPts_.empty() || regionMode_ != RegionMode::None)
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

    if (tool_ == ToolId::Select) {
        const bool shift = mods & Qt::ShiftModifier;

        // (a) A grabbed joint is following the cursor: this click places it.
        if (editing_ && gripGrab_) {
            applyEditDrag(p);
            editing_ = false;
            gripGrab_ = false;
            editMoved_ = false;
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
        // Shift is reserved for deselecting, so it never grabs a joint.
        if (!shift && beginGripGrab(p)) return;

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
    case EntityType::DoubleLine:
    case EntityType::Dimension:
        pts = {e.a, lerp(e.a, e.b, 0.5), e.b};
        break;
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
    case EntityType::DoubleLine:
    case EntityType::Dimension:
        segs.emplace_back(e.a, e.b);
        break;
    case EntityType::Polyline:
    case EntityType::Spline:
    case EntityType::Wire:
        for (size_t i = 1; i < e.points.size(); ++i) segs.emplace_back(e.points[i - 1], e.points[i]);
        break;
    case EntityType::Rect:
    case EntityType::Polygon:
    case EntityType::Hatch:
    case EntityType::Wipeout:
        for (size_t i = 0; i < e.points.size(); ++i)
            segs.emplace_back(e.points[i], e.points[(i + 1) % e.points.size()]);
        break;
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
        if (crossing) {
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
    // Grab the first grip (joint) of any selected entity within tolerance.
    for (const auto& id : selected_) {
        Entity* e = entityById(id);
        if (!e) continue;
        for (const auto& gr : gripsOf(*e)) {
            if (distance(at, gr.pos) > tol) continue;
            editing_ = true;
            gripGrab_ = true; // follows the cursor until the next click places it
            editMode_ = 1;
            editEntityId_ = id;
            editRole_ = static_cast<int>(gr.role);
            editIndex_ = gr.index;
            editLast_ = at;
            editMoved_ = false;
            editAnchor_ = at;
            if (gr.role == GripRole::LineA) editAnchor_ = e->b;
            else if (gr.role == GripRole::LineB) editAnchor_ = e->a;
            else if (gr.role == GripRole::Vertex && gr.index > 0 &&
                     gr.index - 1 < static_cast<int>(e->points.size()))
                editAnchor_ = e->points[gr.index - 1];
            else if (gr.role == GripRole::Radius || gr.role == GripRole::Center)
                editAnchor_ = e->center;
            emit message(QStringLiteral("Joint grabbed — move and click to place, Esc to cancel"));
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
    if (editMode_ == 0) {
        for (Entity* e : selectedEntities()) translateEntity(*e, delta);
    } else if (Entity* e = entityById(editEntityId_)) {
        switch (static_cast<GripRole>(editRole_)) {
        case GripRole::LineA: e->a = target; break;
        case GripRole::LineB: e->b = target; break;
        case GripRole::LineMid: translateEntity(*e, delta); break;
        case GripRole::Vertex:
            if (editIndex_ >= 0 && editIndex_ < static_cast<int>(e->points.size()))
                e->points[editIndex_] = target;
            break;
        case GripRole::RectCorner:
            if (e->points.size() == 4) {
                const Vec2 opp = e->points[(editIndex_ + 2) % 4];
                Bounds b; b.expand(opp); b.expand(target);
                e->points = {{b.min.x, b.min.y}, {b.max.x, b.min.y},
                             {b.max.x, b.max.y}, {b.min.x, b.max.y}};
            } else if (editIndex_ < static_cast<int>(e->points.size())) {
                e->points[editIndex_] = target;
            }
            break;
        case GripRole::RectEdge:
            // Move one wall: shift the shared coordinate of the edge's two
            // corners toward the cursor (horizontal wall -> y, vertical -> x).
            if (e->points.size() == 4) {
                const int i = editIndex_, j = (editIndex_ + 1) % 4;
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
        }
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
    case ToolId::Line:
    case ToolId::DoubleLine:
    case ToolId::Rect:
    case ToolId::Circle:
    case ToolId::Ellipse:
    case ToolId::Polygon:
    case ToolId::Dimension:
        draftPts_.push_back(p);
        drafting_ = true;
        if (draftPts_.size() == 2) finishDraw();
        break;
    case ToolId::Arc:
        draftPts_.push_back(p);
        drafting_ = true;
        if (draftPts_.size() == 3) finishDraw();
        break;
    case ToolId::Polyline:
    case ToolId::DoublePolyline:
    case ToolId::Spline:
    case ToolId::Wire:
        draftPts_.push_back(p);
        drafting_ = true;
        emit message(QStringLiteral("%1: click next point, double-click or Enter to finish")
                         .arg(toolLabel(tool_)));
        break;
    case ToolId::Text:
        draftPts_ = {p};
        emit textPromptRequested(QStringLiteral("Text:"));
        break;
    default:
        break;
    }
    emit changed();
}

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
        const int n = 5; // sides; wired to document setting in a later phase
        const double r = distance(c, v);
        const double a0 = std::atan2(v.y - c.y, v.x - c.x);
        e.type = EntityType::Polygon;
        for (int i = 0; i < n; ++i) {
            const double a = a0 + 2.0 * M_PI * i / n;
            e.points.push_back({c.x + r * std::cos(a), c.y + r * std::sin(a)});
        }
        break;
    }
    case ToolId::Circle:
        if (draftPts_.size() < 2) return;
        e.type = EntityType::Circle;
        e.center = draftPts_[0];
        e.radius = distance(draftPts_[0], draftPts_[1]);
        break;
    case ToolId::Ellipse: {
        if (draftPts_.size() < 2) return;
        e.type = EntityType::Ellipse;
        e.center = draftPts_[0];
        e.radius = std::abs(draftPts_[1].x - draftPts_[0].x);
        e.radiusY = std::abs(draftPts_[1].y - draftPts_[0].y);
        break;
    }
    case ToolId::Arc: {
        if (draftPts_.size() < 3) return;
        const Vec2 c = draftPts_[0];
        e.type = EntityType::Arc;
        e.center = c;
        e.radius = distance(c, draftPts_[1]);
        e.startAngle = std::atan2(draftPts_[1].y - c.y, draftPts_[1].x - c.x);
        e.endAngle = std::atan2(draftPts_[2].y - c.y, draftPts_[2].x - c.x);
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

bool Editor::acceptCommandInput(const QString& text) {
    const QString t = text.trimmed();
    if (t.isEmpty()) return false;

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
        if (drafting_) { emit message(QStringLiteral("Gap applies to double line/polyline")); return true; }
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
        if (drafting_) { emit message(QStringLiteral("Fillet applies to polylines")); return true; }
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
    for (const auto& id : selected_)
        if (const Entity* e = entityById(id)) clipboard_.push_back(*e);
    emit message(QStringLiteral("Copied %1 object(s)").arg(clipboard_.size()));
}

void Editor::paste() {
    if (!doc_ || clipboard_.empty()) return;
    pushUndo();
    selected_.clear();
    // Offset pasted copies slightly so they are distinguishable.
    const Vec2 off{10.0, -10.0};
    for (Entity e : clipboard_) {
        e.id = uid();
        translateEntity(e, off);
        selected_.push_back(e.id);
        doc_->entities().push_back(std::move(e));
    }
    doc_->setDirty(true);
    emit selectionChanged();
    emit changed();
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
        if (drafting_) { finishDraw(); return true; }
        if (regionMode_ == RegionMode::None && !editing_ && modifyPts_.empty()) {
            repeatLastTool();
            return true;
        }
    }
    if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
        deleteSelected();
        return true;
    }
    if (ctrl && key == Qt::Key_A) { selectAll(); return true; }
    if (ctrl && key == Qt::Key_C) { copySelection(); return true; }
    if (ctrl && key == Qt::Key_V) { paste(); return true; }
    if (ctrl && key == Qt::Key_Z) { undo(); return true; }
    if (ctrl && (key == Qt::Key_Y)) { redo(); return true; }
    return false;
}

// ---- Overlay painting ------------------------------------------------------

void Editor::drawOverlay(QPainter& p, const ViewTransform& vt) const {
    if (!doc_) return;

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

    // In-progress draft preview.
    if (drafting_ && !draftPts_.empty()) {
        QPen prev(QColor("#9ad27a"));
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
        } else if (tool_ == ToolId::Circle && pts.size() >= 2) {
            const QPointF c = vt.toScreen(pts[0]);
            const double r = distance(pts[0], pts[1]) * vt.scale();
            p.drawEllipse(c, r, r);
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
