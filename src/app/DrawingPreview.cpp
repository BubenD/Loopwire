#include "app/DrawingPreview.h"

#include "geometry/Geometry.h"
#include "harness/PartsLibrary.h"
#include "model/Document.h"
#include "render/EntityRenderer.h"

#include <QMouseEvent>
#include <QResizeEvent>
#include <QToolButton>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>

namespace Loopwire {

DrawingPreview::DrawingPreview(const QString& caption, QWidget* parent)
    : QWidget(parent), caption_(caption), emptyText_(QStringLiteral("no drawing yet")) {
    setMinimumSize(190, 130);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setToolTip(QStringLiteral(
        "Scroll to zoom, drag to move, double-click to fit the whole drawing again"));
}

void DrawingPreview::setPickable(bool on) {
    pickable_ = on;
    setCursor(on ? Qt::PointingHandCursor : Qt::ArrowCursor);
    setToolTip(on ? QStringLiteral("Click the line the harness attaches to — one line only")
                  : QStringLiteral("Scroll to zoom, drag to move, double-click to fit the whole "
                                   "drawing again"));
    update();
}

void DrawingPreview::setBoxSelect(bool on) {
    boxSelect_ = on;
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    setToolTip(on ? QStringLiteral("Drag a box round what to take. Scroll to zoom, drag with the middle or right "
                                   "button to move, double-click to fit")
                  : QStringLiteral("Scroll to zoom, drag to move, double-click to fit the whole drawing again"));
    update();
}

void DrawingPreview::setSelectedBox(const Bounds& box) {
    box_ = box;
    update();
}

void DrawingPreview::setPickedIndex(int index) {
    if (picked_ == index) return;
    picked_ = index;
    update();
    emit pickedChanged(picked_);
}

namespace {

// How far a point is from an object, for picking one out of a part's drawing. Its own drawn shape, not
// its bounding box: a click inside the shell of a connector must not take the shell in preference to
// the line running through it.
double distanceToDrawnEntity(const Entity& e, const Vec2& at) {
    auto seg = [&at](const Vec2& a, const Vec2& b) {
        return closestPointOnSegment(at, a, b).distance;
    };
    double best = 1e299;
    switch (e.type) {
    case EntityType::Line:
    case EntityType::DoubleLine:
    case EntityType::Dimension:
        best = seg(e.a, e.b);
        break;
    case EntityType::Circle:
    case EntityType::Arc:
        best = std::abs(distance(at, e.center) - e.radius);
        break;
    case EntityType::Rect:
    case EntityType::Polygon:
        for (size_t i = 0; i < e.points.size(); ++i)
            best = std::min(best, seg(e.points[i], e.points[(i + 1) % e.points.size()]));
        break;
    default:
        for (size_t i = 1; i < e.points.size(); ++i) best = std::min(best, seg(e.points[i - 1], e.points[i]));
        if (e.points.empty()) best = distance(at, e.a);
        break;
    }
    return best;
}

} // namespace

void DrawingPreview::setDrawing(const QJsonArray& drawing) {
    ents_ = partDrawingEntities(drawing);
    update();
}

void DrawingPreview::setEntities(const std::vector<Entity>& ents) {
    holdView_ = keepView_ && lastVtValid_ && !ents_.empty() && !ents.empty();
    ents_ = ents;
    update();
}

void DrawingPreview::setCaption(const QString& caption) {
    caption_ = caption;
    update();
}

void DrawingPreview::setEmptyText(const QString& text) {
    emptyText_ = text;
    update();
}

void DrawingPreview::zoomToBounds(const Bounds& t, double share) {
    if (!t.valid || !lastBounds_.valid || lastFit_ <= 0.0 || lastBox_.isEmpty()) return;
    const double w = std::max(1e-6, t.max.x - t.min.x), h = std::max(1e-6, t.max.y - t.min.y);
    const double scale = share * std::min(lastBox_.width() / w, lastBox_.height() / h);
    zoom_ = std::max(0.05, scale / lastFit_);
    const double cxAll = (lastBounds_.min.x + lastBounds_.max.x) / 2.0, cyAll = (lastBounds_.min.y + lastBounds_.max.y) / 2.0;
    const double cx = (t.min.x + t.max.x) / 2.0, cy = (t.min.y + t.max.y) / 2.0;
    pan_ = QPointF(-(cx - cxAll) * scale, (cy - cyAll) * scale);
    holdView_ = false;
    update();
}

void DrawingPreview::fitToView() {
    zoom_ = 1.0;
    pan_ = QPointF(0.0, 0.0);
    holdView_ = false;
    // A deferred resize must not restore the transform that Fit just discarded.
    lastVtValid_ = false;
    update();
}

void DrawingPreview::wheelEvent(QWheelEvent* e) {
    const double steps = e->angleDelta().y() / 120.0;
    if (steps == 0.0) {
        QWidget::wheelEvent(e);
        return;
    }
    const double factor = std::pow(1.2, steps);
    // Out no further than the whole drawing - unless a kept view already shows more than that.
    const double next = std::clamp(zoom_ * factor, std::min(1.0, zoom_), 80.0);
    // Zoom about the cursor, so whatever is under it stays under it - the whole point of scrolling to
    // look at a title block rather than re-finding it after every step.
    const QPointF c(width() / 2.0, height() / 2.0);
    const QPointF at = e->position();
    const double applied = next / zoom_;
    pan_ = (pan_ + c - at) * applied + at - c;
    zoom_ = next;
    if (zoom_ <= 1.0 + 1e-9 && steps < 0.0 && zoom_ * 1.2 > 1.0) {   // fully out means fitted again
        zoom_ = 1.0;
        pan_ = QPointF(0.0, 0.0);
    }
    update();
    e->accept();
}

QRectF DrawingPreview::minimapRect() const {
    return QRectF(width() - 190.0, height() - 124.0, 180.0, 114.0);
}

void DrawingPreview::setMinimapAvailable(bool on) {
    if (on && !mapBtn_) {
        mapBtn_ = new QToolButton(this);
        mapBtn_->setObjectName(QStringLiteral("previewMap"));
        mapBtn_->setText(QStringLiteral("Map"));
        mapBtn_->setCheckable(true);
        mapBtn_->setAutoRaise(true);
        mapBtn_->setToolTip(QStringLiteral("The whole drawing small, with the part in view outlined"));
        mapBtn_->setStyleSheet(QStringLiteral("QToolButton{color:#8fb0d6;font-size:10px;padding:1px 4px;}"
                                              "QToolButton:checked{background:#2b6cb0;color:white;}"));
        connect(mapBtn_, &QToolButton::toggled, this, [this](bool v) { setMinimap(v); });
        mapBtn_->move(width() - 44, height() - 22);
        mapBtn_->show();
    }
    if (mapBtn_) mapBtn_->setVisible(on);
}

void DrawingPreview::setFitButtonShown(bool on) {
    // FIT, as a button: double-clicking the pane does the same, but nobody finds that by looking.
    if (on && !fitBtn_) {
        fitBtn_ = new QToolButton(this);
        fitBtn_->setObjectName(QStringLiteral("previewFit"));
        fitBtn_->setText(QStringLiteral("Fit"));
        fitBtn_->setAutoRaise(true);
        fitBtn_->setFocusPolicy(Qt::NoFocus);
        fitBtn_->setToolTip(QStringLiteral("The whole drawing in view (the wheel zooms about the cursor)"));
        fitBtn_->setStyleSheet(QStringLiteral("QToolButton{color:#8fb0d6;font-size:10px;padding:1px 4px;}"));
        connect(fitBtn_, &QToolButton::clicked, this, [this] { fitToView(); });
        placeCornerButtons();
        fitBtn_->show();
    }
    if (fitBtn_) fitBtn_->setVisible(on);
}

void DrawingPreview::placeCornerButtons() {
    // Map at the right, Fit just to the left of it - asked whether the map is HIDDEN rather than visible,
    // since before the pane is first shown nothing on it counts as visible and Fit landed on top of Map.
    int right = width() - 6;
    if (mapBtn_) {
        const int w = std::max(mapBtn_->sizeHint().width(), 38);
        mapBtn_->move(right - w, 4 + 14);
        if (!mapBtn_->isHidden()) right -= w + 4;
    }
    if (fitBtn_) fitBtn_->move(right - fitBtn_->sizeHint().width(), 4 + 14);
}

void DrawingPreview::setMinimap(bool on) {
    minimap_ = on;
    if (mapBtn_ && mapBtn_->isChecked() != on) mapBtn_->setChecked(on);
    update();
}

void DrawingPreview::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (keepView_ && lastVtValid_) holdView_ = true;
    placeCornerButtons();
}

void DrawingPreview::mousePressEvent(QMouseEvent* e) {
    // A CLICK ON THE MAP goes there: that point to the middle of the view.
    if (minimap_ && e->button() == Qt::LeftButton && lastBounds_.valid &&
        minimapRect().contains(e->position())) {
        const QRectF m = minimapRect().adjusted(6, 6, -6, -6);
        const double w = std::max(1e-6, lastBounds_.max.x - lastBounds_.min.x);
        const double h = std::max(1e-6, lastBounds_.max.y - lastBounds_.min.y);
        const double s = std::min(m.width() / w, m.height() / h);
        const double cx = (lastBounds_.min.x + lastBounds_.max.x) / 2.0;
        const double cy = (lastBounds_.min.y + lastBounds_.max.y) / 2.0;
        const double wx = cx + (e->position().x() - m.center().x()) / s;
        const double wy = cy - (e->position().y() - m.center().y()) / s;
        pan_ = QPointF((cx - wx) * lastScale_, (wy - cy) * lastScale_);
        update();
        return;
    }
    // Picking one object: the nearest to the click, and only if the click is anywhere near it - a click
    // on empty space takes nothing rather than the least distant thing on the sheet.
    if (pickable_ && e->button() == Qt::LeftButton && lastVtValid_ && !ents_.empty()) {
        const Vec2 at = lastVt_.toWorld(e->position());
        const double reach = 8.0 / std::max(1e-9, lastVt_.scale());   // eight pixels, in drawing units
        int best = -1;
        double bestDist = 1e299;
        for (int i = 0; i < static_cast<int>(ents_.size()); ++i) {
            const double d = distanceToDrawnEntity(ents_[static_cast<size_t>(i)], at);
            if (d < bestDist) {
                bestDist = d;
                best = i;
            }
        }
        if (best >= 0 && bestDist <= reach) {
            setPickedIndex(best);
            e->accept();
            return;
        }
    }
    if (boxSelect_ && e->button() == Qt::LeftButton && lastVtValid_) {
        boxing_ = true;
        boxFrom_ = lastVt_.toWorld(e->position());
        box_ = Bounds{boxFrom_, boxFrom_, true};
        update();
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton || e->button() == Qt::MiddleButton ||
        (boxSelect_ && e->button() == Qt::RightButton)) {
        dragging_ = true;
        dragFrom_ = e->position();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void DrawingPreview::mouseMoveEvent(QMouseEvent* e) {
    if (boxing_) {
        const Vec2 to = lastVt_.toWorld(e->position());
        box_ = Bounds{};
        box_.expand(boxFrom_);
        box_.expand(to);
        update();
        e->accept();
        return;
    }
    if (!dragging_) {
        QWidget::mouseMoveEvent(e);
        return;
    }
    pan_ += e->position() - dragFrom_;
    dragFrom_ = e->position();
    update();
    e->accept();
}

void DrawingPreview::mouseReleaseEvent(QMouseEvent* e) {
    if (boxing_) {
        boxing_ = false;
        // A click, not a drag, draws no box.
        if (box_.width() * lastScale_ < 4.0 || box_.height() * lastScale_ < 4.0) box_ = Bounds{};
        update();
        emit boxChanged();
        e->accept();
        return;
    }
    if (dragging_) {
        dragging_ = false;
        unsetCursor();
        e->accept();
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

void DrawingPreview::mouseDoubleClickEvent(QMouseEvent* e) {
    fitToView();
    e->accept();
}

void DrawingPreview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRect r = rect().adjusted(0, 0, -1, -1);
    p.fillRect(r, QColor("#2b2e33"));
    p.setPen(QPen(QColor("#4a4e55"), 1));
    p.drawRect(r);
    QFont f = p.font();
    f.setPixelSize(10);
    p.setFont(f);
    p.setPen(QColor("#8fa2b8"));
    p.drawText(r.adjusted(6, 4, -6, 0), Qt::AlignLeft | Qt::AlignTop, caption_);
    if (ents_.empty()) {
        p.setPen(QColor("#6f7681"));
        p.drawText(r, Qt::AlignCenter, emptyText_);
        return;
    }
    // Frame everything, then draw it through the same renderer the canvas uses.
    Document doc;
    doc.entities().clear();
    Bounds b;
    for (const Entity& e : ents_) {
        Entity shown = e;
        // AN ORANGE LINE IS NOT A MARK. The old way of saying where a harness attaches was to colour one of
        // the part's OWN lines orange - which reads as part of the part (an orange line on a connector face
        // looks like a feature of the connector), says nothing about which way the part runs from there, and
        // cannot say where the wires leave at all. The marks say all of it, in the colours the editing canvas
        // uses: blue where it connects with an arrow along the run, orange and green ends, purple faces.
        //
        // So the flag is taken off the copy being drawn, always. The part on the shelf still carries it -
        // the generator reads it when it places a part that has one - it simply is not what the eye is
        // pointed at any more.
        shown.meta.remove(QLatin1String(kConnectionLineKey));
        // NOT APPLIED YET, SAID WITH A DASHED LINE and not with dimness alone.
        //
        // A ghost was a darker grey than the drawing, and grey is not a strong enough signal on a dark
        // ground - especially now that the selected step is drawn in a bright colour, which leaves the
        // ghost as merely the faintest of three greys. Faint reads as "far away" or "badly drawn", not
        // as "not on the drawing yet".
        //
        // Dashed says provisional, which is what it is, and says it in the one language a drawing
        // office already reads: a dashed outline is something that is not there. It survives being
        // printed, photocopied and looked at on a projector, none of which a shade of grey does.
        if (shown.meta.value(QLatin1String(kPreviewGhostKey)).toBool())
            shown.meta.insert(QStringLiteral("linetype"), QStringLiteral("HIDDEN"));
        doc.entities().push_back(shown);
        const Bounds eb = e.bounds();
        if (eb.valid) { b.expand(eb.min); b.expand(eb.max); }
    }
    if (!b.valid) return;
    const double w = std::max(1e-6, b.max.x - b.min.x);
    const double h = std::max(1e-6, b.max.y - b.min.y);
    const QRect box = r.adjusted(10, 18, -10, -10);
    const double fit = std::min(box.width() / w, box.height() / h) * 0.9;
    lastFit_ = fit;
    lastBox_ = box;
    // A kept view: the zoom and pan that put the new drawing exactly where the last one was drawn.
    if (holdView_ && lastVtValid_ && lastScale_ > 0.0) {
        holdView_ = false;
        zoom_ = lastScale_ / fit;
        const double cx = (b.min.x + b.max.x) / 2.0, cy = (b.min.y + b.max.y) / 2.0;
        pan_ = QPointF(lastVt_.view.x - box.center().x() + cx * lastScale_,
                       lastVt_.view.y - box.center().y() - cy * lastScale_);
    }
    const double scale = fit * zoom_;
    ViewTransform vt;
    vt.view.scale = scale;
    vt.view.x = box.center().x() + pan_.x() - (b.min.x + b.max.x) / 2.0 * scale;
    vt.view.y = box.center().y() + pan_.y() + (b.min.y + b.max.y) / 2.0 * scale;
    lastVt_ = vt;
    lastVtValid_ = true;
    lastBounds_ = b;
    lastScale_ = scale;
    p.save();
    p.setClipRect(box);
    EntityRenderer::setBackgroundColor(QColor("#2b2e33"));
    for (int i = 0; i < static_cast<int>(doc.entities().size()); ++i) {
        const Entity& e = doc.entities()[static_cast<size_t>(i)];
        // Everything in one quiet grey, so a preview reads as a drawing rather than as a colour chart -
        // EXCEPT the line the harness attaches to, which keeps the orange that says so. Overriding that
        // as well is what hid the mark in the library, where it is most wanted.
        const bool conn = pickable_ && i == picked_;
        // A MARK KEEPS ITS OWN COLOUR. The grey is so that a preview reads as a drawing rather than as a
        // colour chart, and it was being applied to the marks as well - so the manager's blue connection
        // point, its orange and green ends and its purple output faces all came out the same grey as the
        // part, which is three or four marks nobody can tell apart.
        // A MASK THAT SAYS IT HAS NO FRAME IS DRAWN WITHOUT ONE: a colour laid over it is what asks the renderer to
        // frame a mask (so a picked one can be seen) - and every mask here was coming out framed, lines across a
        // bridge wire's bend and down the middle of what it covers.
        if (e.type == EntityType::Wipeout && e.meta.value(QStringLiteral("noFrame")).toBool())
            EntityRenderer::draw(p, e, doc, vt, QColor());
        else if (e.meta.value(QLatin1String(kPreviewMarkKey)).toBool())
            EntityRenderer::draw(p, e, doc, vt, QColor());
        else if (conn) EntityRenderer::draw(p, e, doc, vt, QColor("#ff9f40"));
        // NOT APPLIED YET - see kPreviewGhostKey. Dimmed to about a third of the contrast the applied
        // objects have, which is enough to read the shape and not enough to mistake it for the drawing.
        // THE STEP SELECTED IN THE LIST - see kPreviewSelectedKey. Brighter than the drawing rather
        // than dimmer than it: this one is being pointed at, where a ghost is being held back.
        else if (e.meta.value(QLatin1String(kPreviewSelectedKey)).toBool())
            EntityRenderer::draw(p, e, doc, vt, QColor("#2bd0ff"));
        else if (e.meta.value(QLatin1String(kPreviewGhostKey)).toBool())
            // Bright enough to READ, because the dashes are what say it is not applied - a ghost that
            // is both faint and dashed is a shape nobody can make out well enough to judge, which is
            // the one thing a preview of the step being described has to allow.
            EntityRenderer::draw(p, e, doc, vt, QColor("#8a93a0"));
        else EntityRenderer::draw(p, e, doc, vt, QColor("#d7dade"));
    }
    // THE BOX, over the drawing: what will be taken.
    if (box_.valid) {
        const QRectF shown = QRectF(vt.toScreen(box_.min), vt.toScreen(box_.max)).normalized();
        p.setPen(QPen(QColor("#2bd0ff"), 1.4, Qt::DashLine));
        p.setBrush(QColor(43, 208, 255, 28));
        p.drawRect(shown);
    }
    p.restore();
    // THE MAP: the whole drawing, small, and the part of it in view outlined.
    if (minimap_) {
        const QRectF m = minimapRect();
        p.save();
        p.setPen(QPen(QColor("#4a4e55"), 1));
        p.setBrush(QColor(30, 33, 38, 235));
        p.drawRect(m);
        const QRectF in = m.adjusted(6, 6, -6, -6);
        const double s = std::min(in.width() / w, in.height() / h);
        ViewTransform mv;
        mv.view.scale = s;
        mv.view.x = in.center().x() - (b.min.x + b.max.x) / 2.0 * s;
        mv.view.y = in.center().y() + (b.min.y + b.max.y) / 2.0 * s;
        p.setClipRect(m);
        for (const Entity& e : doc.entities()) EntityRenderer::draw(p, e, doc, mv, QColor("#9aa3ad"));
        // what is in view
        const Vec2 tl = vt.toWorld(QPointF(box.left(), box.top()));
        const Vec2 br = vt.toWorld(QPointF(box.right(), box.bottom()));
        p.setPen(QPen(QColor("#2bd0ff"), 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(mv.toScreen(tl), mv.toScreen(br)).normalized());
        p.restore();
    }
    // What the zoom is, once it is not the whole drawing - so it is never a mystery why only part of
    // the sheet is showing.
    if (zoom_ > 1.0 + 1e-9) {
        p.setPen(QColor("#8fb0d6"));
        p.drawText(r.adjusted(6, 4, -6, 0), Qt::AlignRight | Qt::AlignTop,
                   QStringLiteral("×%1  (double-click to fit)").arg(zoom_, 0, 'f', 1));
    }
}

void DrawingPreview::setShowConnection(bool on) {
    if (showConn_ == on) return;
    showConn_ = on;
    update();
}

} // namespace Loopwire
