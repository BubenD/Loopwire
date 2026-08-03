#include "app/DrawingPreview.h"

#include "harness/PartsLibrary.h"
#include "model/Document.h"
#include "render/EntityRenderer.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>

namespace Edraw_Desktop {

DrawingPreview::DrawingPreview(const QString& caption, QWidget* parent)
    : QWidget(parent), caption_(caption), emptyText_(QStringLiteral("no drawing yet")) {
    setMinimumSize(190, 130);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setToolTip(QStringLiteral(
        "Scroll to zoom, drag to move, double-click to fit the whole drawing again"));
}

void DrawingPreview::setDrawing(const QJsonArray& drawing) {
    ents_ = partDrawingEntities(drawing);
    update();
}

void DrawingPreview::setEntities(const std::vector<Entity>& ents) {
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

void DrawingPreview::fitToView() {
    zoom_ = 1.0;
    pan_ = QPointF(0.0, 0.0);
    update();
}

void DrawingPreview::wheelEvent(QWheelEvent* e) {
    const double steps = e->angleDelta().y() / 120.0;
    if (steps == 0.0) {
        QWidget::wheelEvent(e);
        return;
    }
    const double factor = std::pow(1.2, steps);
    const double next = std::clamp(zoom_ * factor, 1.0, 80.0);
    // Zoom about the cursor, so whatever is under it stays under it - the whole point of scrolling to
    // look at a title block rather than re-finding it after every step.
    const QPointF c(width() / 2.0, height() / 2.0);
    const QPointF at = e->position();
    const double applied = next / zoom_;
    pan_ = (pan_ + c - at) * applied + at - c;
    zoom_ = next;
    if (zoom_ <= 1.0 + 1e-9) pan_ = QPointF(0.0, 0.0);   // fully out means fitted again
    update();
    e->accept();
}

void DrawingPreview::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton || e->button() == Qt::MiddleButton) {
        dragging_ = true;
        dragFrom_ = e->position();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void DrawingPreview::mouseMoveEvent(QMouseEvent* e) {
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
        doc.entities().push_back(e);
        const Bounds eb = e.bounds();
        if (eb.valid) { b.expand(eb.min); b.expand(eb.max); }
    }
    if (!b.valid) return;
    const double w = std::max(1e-6, b.max.x - b.min.x);
    const double h = std::max(1e-6, b.max.y - b.min.y);
    const QRect box = r.adjusted(10, 18, -10, -10);
    const double fit = std::min(box.width() / w, box.height() / h) * 0.9;
    const double scale = fit * zoom_;
    ViewTransform vt;
    vt.view.scale = scale;
    vt.view.x = box.center().x() + pan_.x() - (b.min.x + b.max.x) / 2.0 * scale;
    vt.view.y = box.center().y() + pan_.y() + (b.min.y + b.max.y) / 2.0 * scale;
    p.save();
    p.setClipRect(box);
    EntityRenderer::setBackgroundColor(QColor("#2b2e33"));
    for (const Entity& e : doc.entities())
        EntityRenderer::draw(p, e, doc, vt, QColor("#d7dade"));
    p.restore();
    // What the zoom is, once it is not the whole drawing - so it is never a mystery why only part of
    // the sheet is showing.
    if (zoom_ > 1.0 + 1e-9) {
        p.setPen(QColor("#8fb0d6"));
        p.drawText(r.adjusted(6, 4, -6, 0), Qt::AlignRight | Qt::AlignTop,
                   QStringLiteral("×%1  (double-click to fit)").arg(zoom_, 0, 'f', 1));
    }
}

} // namespace Edraw_Desktop
