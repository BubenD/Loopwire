#include "render/CanvasWidget.h"

#include "app/Editor.h"
#include "geometry/Geometry.h"

#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QSet>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

CanvasWidget::CanvasWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor("#10141b"));
    setPalette(pal);

    // Floating "dynamic input" box that appears next to the cursor while drawing.
    dynInput_ = new QLineEdit(this);
    dynInput_->setFixedHeight(22);
    dynInput_->setMinimumWidth(96);
    dynInput_->setFocusPolicy(Qt::ClickFocus);
    dynInput_->setStyleSheet(QStringLiteral(
        "QLineEdit{background:#0b0f16;color:#e6f0ff;border:1px solid #3d8bfd;"
        "border-radius:3px;padding:1px 5px;font-size:12px;}"));
    dynInput_->hide();
    dynInput_->installEventFilter(this);
    connect(dynInput_, &QLineEdit::returnPressed, this, [this] {
        const QString t = dynInput_->text();
        hideDynInput();
        if (!t.isEmpty()) emit commandSubmitted(t);
    });
}

void CanvasWidget::showDynInput(const QString& firstChar) {
    // Position just to the lower-right of the cursor, kept inside the widget.
    QPoint pos = lastMousePos_ + QPoint(16, 8);
    pos.setX(std::min(pos.x(), width() - dynInput_->width() - 4));
    pos.setY(std::min(pos.y(), height() - dynInput_->height() - 4));
    dynInput_->move(pos);
    dynInput_->setText(firstChar);
    dynInput_->show();
    dynInput_->raise();
    dynInput_->setFocus(Qt::OtherFocusReason);
    dynInput_->setCursorPosition(dynInput_->text().length());
}

void CanvasWidget::hideDynInput() {
    if (!dynInput_) return;
    dynInput_->clear();
    dynInput_->hide();
    setFocus(Qt::OtherFocusReason);
}

bool CanvasWidget::eventFilter(QObject* obj, QEvent* e) {
    if (obj == dynInput_) {
        if (e->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(e);
            if (ke->key() == Qt::Key_Escape) {
                hideDynInput();
                if (editor_) editor_->onKey(Qt::Key_Escape, Qt::NoModifier);
                return true;
            }
        } else if (e->type() == QEvent::FocusOut) {
            dynInput_->hide(); // dismiss if the user clicks away
        }
    }
    return QWidget::eventFilter(obj, e);
}

void CanvasWidget::invalidateScene() {
    sceneDirty_ = true;
    update();
}

void CanvasWidget::setDocument(Document* doc) {
    doc_ = doc;
    invalidateScene();
}

void CanvasWidget::setEditor(Editor* editor) {
    editor_ = editor;
    if (editor_) {
        // Model changed -> rebuild the static scene cache and repaint.
        connect(editor_, &Editor::changed, this, [this] { invalidateScene(); });
        // Selection changed -> cache must re-include/exclude the selected set.
        connect(editor_, &Editor::selectionChanged, this, [this] { invalidateScene(); });
        // Interactive preview (cursor move / live drag) -> overlay repaint only.
        connect(editor_, &Editor::overlayChanged, this, QOverload<>::of(&QWidget::update));
    }
    invalidateScene();
}

void CanvasWidget::setGridSize(double g) {
    gridSize_ = std::max(0.001, g);
    invalidateScene();
}

void CanvasWidget::setGridVisible(bool v) {
    gridVisible_ = v;
    invalidateScene();
}

ViewTransform CanvasWidget::transform() const {
    ViewTransform vt;
    if (doc_) vt.view = doc_->view();
    return vt;
}

void CanvasWidget::fitView() {
    if (!doc_) return;
    ViewState& view = doc_->view();
    const QSizeF sz = size();

    Bounds box;
    for (const auto& e : doc_->entities()) box.expand(e.bounds());

    if (!box.valid) {
        view = {sz.width() / 2.0, sz.height() / 2.0, 1.0};
    } else {
        const double w = std::max(1.0, box.width());
        const double h = std::max(1.0, box.height());
        view.scale = std::min({kMaxViewScale, sz.width() / (w * 1.35),
                               sz.height() / (h * 1.35)});
        const Vec2 c = box.center();
        view.x = sz.width() / 2.0 - c.x * view.scale;
        view.y = sz.height() / 2.0 + c.y * view.scale;
    }
    invalidateScene();
}

void CanvasWidget::drawGrid(QPainter& p, const ViewTransform& vt) const {
    if (!gridVisible_ || gridSize_ <= 0.0) return;

    // Skip drawing when the grid would be denser than ~6 px to avoid a wall of lines.
    double stepPx = gridSize_ * vt.scale();
    double step = gridSize_;
    while (stepPx > 0.0 && stepPx < 6.0) { step *= 10.0; stepPx = step * vt.scale(); }

    const Vec2 topLeft = vt.toWorld(QPointF(0, 0));
    const Vec2 botRight = vt.toWorld(QPointF(width(), height()));
    const double minX = std::min(topLeft.x, botRight.x);
    const double maxX = std::max(topLeft.x, botRight.x);
    const double minY = std::min(topLeft.y, botRight.y);
    const double maxY = std::max(topLeft.y, botRight.y);

    // Minor grid every `step`; brighter secondary (major) grid every 5 squares.
    QPen minorPen(QColor("#1a2230"));
    minorPen.setCosmetic(true);
    QPen majorPen(QColor("#2c3a52"));
    majorPen.setCosmetic(true);
    constexpr int kMajorEvery = 5;

    const long long i0 = static_cast<long long>(std::floor(minX / step));
    for (long long i = i0; i * step <= maxX; ++i) {
        const double x = i * step;
        p.setPen((i % kMajorEvery == 0) ? majorPen : minorPen);
        p.drawLine(vt.toScreen({x, minY}), vt.toScreen({x, maxY}));
    }
    const long long j0 = static_cast<long long>(std::floor(minY / step));
    for (long long j = j0; j * step <= maxY; ++j) {
        const double y = j * step;
        p.setPen((j % kMajorEvery == 0) ? majorPen : minorPen);
        p.drawLine(vt.toScreen({minX, y}), vt.toScreen({maxX, y}));
    }
}

void CanvasWidget::drawOriginMarker(QPainter& p, const ViewTransform& vt) const {
    const QPointF o = vt.toScreen({0, 0});
    // Full-canvas axes through the origin (red X, green Y), Edraw-style.
    QPen xPen(QColor("#c0413a"));
    xPen.setCosmetic(true);
    xPen.setWidthF(1.2);
    p.setPen(xPen);
    p.drawLine(QPointF(0, o.y()), QPointF(width(), o.y()));
    QPen yPen(QColor("#4f9d54"));
    yPen.setCosmetic(true);
    yPen.setWidthF(1.2);
    p.setPen(yPen);
    p.drawLine(QPointF(o.x(), 0), QPointF(o.x(), height()));

    // Small origin square + "0,0" label.
    p.setPen(QPen(QColor("#9aa7b8")));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(o.x() - 4, o.y() - 4, 8, 8));
    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);
    p.drawText(o + QPointF(6, 16), QStringLiteral("0,0"));
}

void CanvasWidget::paintEvent(QPaintEvent*) {
    const ViewTransform vt = transform();
    const qreal dpr = devicePixelRatioF();
    const QSize devSize = size() * dpr;

    // Rebuild the static scene (grid + entities) only when it actually changed;
    // cursor-move previews reuse this cache so the overlay tracks without lag.
    if (sceneDirty_ || sceneCache_.size() != devSize) {
        sceneCache_ = QPixmap(devSize);
        sceneCache_.setDevicePixelRatio(dpr);
        sceneCache_.fill(QColor("#10141b"));
        QPainter sp(&sceneCache_);
        sp.setRenderHint(QPainter::Antialiasing, true);
        if (doc_) {
            drawGrid(sp, vt);
            drawOriginMarker(sp, vt);
            // Selected entities are drawn live in the overlay (so they can be
            // dragged without rebuilding the cache); skip them here.
            QSet<QString> selected;
            if (editor_)
                for (const auto& id : editor_->selection()) selected.insert(id);
            for (const auto& e : doc_->entities()) {
                if (const Layer* layer = doc_->findLayer(e.layer); layer && !layer->visible)
                    continue;
                if (selected.contains(e.id)) continue;
                EntityRenderer::draw(sp, e, *doc_, vt);
            }
        }
        sceneDirty_ = false;
    }

    QPainter p(this);
    p.drawPixmap(0, 0, sceneCache_);
    if (doc_ && editor_) {
        p.setRenderHint(QPainter::Antialiasing, true);
        editor_->drawOverlay(p, vt);
    }
}

void CanvasWidget::wheelEvent(QWheelEvent* e) {
    if (!doc_) return;
    ViewState& view = doc_->view();

    const double factor = std::pow(1.0015, e->angleDelta().y());
    const double newScale = std::clamp(view.scale * factor, kMinViewScale, kMaxViewScale);

    // Keep the point under the cursor fixed during zoom.
    const QPointF cursor = e->position();
    const ViewTransform before{view};
    const Vec2 world = before.toWorld(cursor);
    view.scale = newScale;
    view.x = cursor.x() - world.x * view.scale;
    view.y = cursor.y() + world.y * view.scale;

    invalidateScene();
    e->accept();
}

void CanvasWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
        panning_ = true;
        lastPanPos_ = e->pos();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton && editor_) {
        if (dynInput_->isVisible()) hideDynInput(); // a click discards pending typed input
        setFocus(Qt::MouseFocusReason);
        editor_->onPointerPress(transform().toWorld(e->position()), e->button(),
                                e->modifiers());
        e->accept();
    }
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* e) {
    lastMousePos_ = e->pos();
    if (panning_ && doc_) {
        const QPoint delta = e->pos() - lastPanPos_;
        lastPanPos_ = e->pos();
        doc_->view().x += delta.x();
        doc_->view().y += delta.y();
        invalidateScene();
    }
    const Vec2 world = transform().toWorld(e->position());
    if (editor_) editor_->onPointerMove(world);
    emit cursorWorldMoved(world);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (panning_ && (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton)) {
        panning_ = false;
        unsetCursor();
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton && editor_) {
        editor_->onPointerRelease(transform().toWorld(e->position()), e->button());
        e->accept();
    }
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && editor_) {
        editor_->onDoubleClick(transform().toWorld(e->position()));
        e->accept();
    }
}

void CanvasWidget::keyPressEvent(QKeyEvent* e) {
    // With a draw tool active, typing a value or command character routes to the
    // command line (direct distance entry, "g" gap option, commands, etc.).
    if (editor_ && editor_->wantsTextInput()) {
        const QString t = e->text();
        if (!t.isEmpty() && (t[0].isLetterOrNumber() || t == "." || t == "-" || t == "@")) {
            showDynInput(t); // floating input box next to the cursor
            e->accept();
            return;
        }
    }
    if (editor_ && editor_->onKey(e->key(), e->modifiers())) {
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

} // namespace Edraw_Desktop
