#include "render/CanvasWidget.h"

#include "app/Editor.h"
#include "geometry/Geometry.h"

#include <QChildEvent>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QEnterEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QSet>
#include <QAbstractItemView>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

namespace {
// Transparent, click-through child that paints nothing but the crosshair, kept
// stacked above every other canvas child so the cursor is always visible — even
// over the on-canvas text editor and its toolbar/ruler.
class CrosshairTop : public QWidget {
public:
    explicit CrosshairTop(CanvasWidget* canvas) : QWidget(canvas), canvas_(canvas) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        canvas_->renderCrosshair(p);
    }

private:
    CanvasWidget* canvas_;
};
} // namespace

CanvasWidget::CanvasWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
    // We drive the right-click menu (and right-drag move/copy) from the mouse
    // events, so suppress the OS/Qt context-menu event.
    setContextMenuPolicy(Qt::PreventContextMenu);
    setCursor(Qt::BlankCursor); // hide the OS cursor; we draw an AutoCAD crosshair
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor("#10141b"));
    setPalette(pal);
    EntityRenderer::setBackgroundColor(bgColor_); // Wipeout masks match the bg

    // Floating "dynamic input" box that appears next to the cursor while drawing.
    dynInput_ = new QLineEdit(this);
    dynInput_->setFixedHeight(22);
    dynInput_->setMinimumWidth(96);
    dynInput_->setFocusPolicy(Qt::ClickFocus);
    dynInput_->setStyleSheet(QStringLiteral(
        "QLineEdit{background:#0b0f16;color:#e6f0ff;border:1px solid #3d8bfd;"
        "border-radius:3px;padding:1px 5px;font-size:12px;}"));
    dynInput_->hide();
    // A text cursor over a field being typed into, for the same reason: without one it inherits the
    // canvas's blank cursor and there is nothing on screen while a length is being typed.
    dynInput_->setCursor(Qt::IBeamCursor);
    dynInput_->installEventFilter(this);

    // The crosshair paints on a click-through overlay that we keep on top of all
    // other children, so the cursor is never hidden behind the inline text editor.
    crosshairTop_ = new CrosshairTop(this);
    crosshairTop_->setGeometry(rect());
    crosshairTop_->raise();
    crosshairTop_->show();
    // NB: Enter is handled in eventFilter() and consumed there. QLineEdit does NOT
    // accept Return/Enter (it lets it bubble for dialog default buttons), so if we
    // relied on returnPressed the same Enter would propagate to the canvas and
    // finish the draft (e.g. ending a polyline right after typing a fillet radius).
}

void CanvasWidget::positionDynInput() {
    if (!dynInput_) return;
    // Anchor just off the visible crosshair: the snapped cursor while drawing, else
    // the raw mouse — so the field stays at the point you're drawing to.
    QPoint anchor = lastMousePos_;
    if (editor_ && editor_->currentSnap().snapped)
        anchor = transform().toScreen(editor_->currentSnap().point).toPoint();
    QPoint pos = anchor + QPoint(16, 8);
    pos.setX(std::clamp(pos.x(), 4, std::max(4, width() - dynInput_->width() - 4)));
    pos.setY(std::clamp(pos.y(), 4, std::max(4, height() - dynInput_->height() - 4)));
    dynInput_->move(pos);
}

void CanvasWidget::showDynInput(const QString& firstChar, const QString& placeholder) {
    dynInput_->setText(firstChar);
    // Label the box: an explicit prompt (fillet radius / gap) wins, else the
    // active reference field (Length / Radius / ...).
    dynInput_->setPlaceholderText(
        !placeholder.isEmpty() ? placeholder
                               : (editor_ ? editor_->currentDrawFieldLabel() : QString()));
    dynInput_->show();
    positionDynInput(); // place it after show() so its size is known
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
            // Submit on Enter and CONSUME it (return true) so the key never bubbles
            // to the canvas, where it would be read as "finish the draft".
            if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                const QString t = dynInput_->text();
                hideDynInput();
                if (!t.isEmpty()) emit commandSubmitted(t);
                return true;
            }
            // Tab locks what's typed into the current reference field and moves to
            // the next one (Length->Angle, X radius->Y radius, ...) — the box stays
            // open so the next value can be typed.
            if ((ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) && editor_ &&
                editor_->drawInputFieldLabels().size() > 1) {
                editor_->lockDrawField(dynInput_->text());
                editor_->cycleDrawInputField();
                dynInput_->clear();
                dynInput_->setPlaceholderText(editor_->currentDrawFieldLabel());
                return true;
            }
        } else if (e->type() == QEvent::FocusOut) {
            dynInput_->hide(); // dismiss if the user clicks away
        }
    }
    // Enter while the command autocomplete popup is open submits the TYPED text in
    // one keystroke (runs the nearest command) instead of only accepting the
    // highlighted suggestion.
    if (completer_ && obj == completer_->popup() && e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            const QString t = dynInput_ ? dynInput_->text() : QString();
            completer_->popup()->hide();
            hideDynInput();
            if (!t.isEmpty()) emit commandSubmitted(t);
            return true;
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

void CanvasWidget::setLayoutMode(bool on) {
    if (layoutMode_ == on) return;
    layoutMode_ = on;
    EntityRenderer::setWhitePaper(on);
    EntityRenderer::setBackgroundColor(on ? QColor(Qt::white) : bgColor_);
    invalidateScene();
}

void CanvasWidget::setBottomOverlay(QWidget* overlay) {
    bottomOverlay_ = overlay;
    if (!bottomOverlay_) return;
    bottomOverlay_->setParent(this);
    // An ordinary arrow over it. The canvas hides the OS cursor because it draws its own crosshair, and
    // a child widget with no cursor of its own INHERITS that - so the moment the pointer moved onto the
    // Model / Layout tabs it disappeared: no crosshair, because the crosshair is only painted over the
    // drawing, and no arrow either. Anything put on the canvas as an overlay is an ordinary control and
    // wants an ordinary pointer, so it is set here rather than left to whoever hands it over.
    bottomOverlay_->setCursor(Qt::ArrowCursor);
    bottomOverlay_->show();
    positionBottomOverlay();
    bottomOverlay_->raise();
}

void CanvasWidget::positionBottomOverlay() {
    if (!bottomOverlay_) return;
    constexpr int height = 32;
    bottomOverlay_->setGeometry(0, std::max(0, this->height() - height), width(), height);
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
        // Inline value prompt (fillet radius / gap): pop the flying field at the
        // cursor, pre-labelled, so the user types the value and keeps drawing.
        // QUEUED: the prompt is emitted while the previous flying field is still
        // handling its own Enter; reopening it synchronously would let that Enter
        // leak to the canvas (finishDraw, ending the polyline). Deferring opens a
        // clean field on the next event-loop pass.
        connect(
            editor_, &Editor::dynamicPrompt, this,
            [this](const QString& label) { showDynInput(QString(), label); },
            Qt::QueuedConnection);
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

    // The sheet the file was drawn on, when it said: that is what the drawing is looked at at. A real
    // drawing keeps working geometry off the sheet - a symbol parked to one side, a block placed out in
    // the field - and fitting to every last object of it opens the sheet small and off to one side.
    Bounds box = doc_->sheet();
    if (layoutMode_ && !box.valid) {
        box.expand({0.0, 0.0});
        box.expand({420.0, 297.0});
    } else if (!box.valid) {
        for (const auto& e : doc_->entities()) box.expand(e.bounds());
    }

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

void CanvasWidget::centerOn(const Vec2& world, double minScale) {
    if (!doc_) return;
    ViewState& view = doc_->view();
    if (minScale > 0.0 && view.scale < minScale) view.scale = minScale;
    view.x = size().width() / 2.0 - world.x * view.scale;
    view.y = size().height() / 2.0 + world.y * view.scale;
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
    // Kept faint (close to the canvas background) so it stays subtle.
    QPen minorPen(gridMinor_);
    minorPen.setCosmetic(true);
    QPen majorPen(gridMajor_);
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

    // Small origin square.
    p.setPen(QPen(QColor("#9aa7b8")));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(o.x() - 4, o.y() - 4, 8, 8));

    // Axis letters: "Y" up the vertical axis, "X" along the horizontal axis.
    QFont af = p.font();
    af.setPixelSize(12);
    af.setBold(true);
    p.setFont(af);
    p.setPen(QPen(QColor("#dfe6f0")));
    p.drawText(QPointF(o.x() + 7, o.y() - 22), QStringLiteral("Y"));
    p.drawText(QPointF(o.x() + 24, o.y() - 5), QStringLiteral("X"));

    // "0,0" label below the origin.
    QFont f = p.font();
    f.setBold(false);
    f.setPixelSize(11);
    p.setFont(f);
    p.setPen(QPen(QColor("#9aa7b8")));
    p.drawText(o + QPointF(7, 16), QStringLiteral("0,0"));
}

// What the layer table says, boiled down to one number. Cheap enough to work out on every paint - a
// drawing has layers in the dozens, not the thousands - and it means a layer switched off, renamed,
// recoloured or given a different weight redraws the scene at once, from wherever it was changed.
quint64 CanvasWidget::layerFingerprint() const {
    if (!doc_) return 0;
    quint64 h = 1469598103934665603ULL;   // FNV-1a
    auto mix = [&h](quint64 v) { h = (h ^ v) * 1099511628211ULL; };
    for (const Layer& l : doc_->layers()) {
        mix(qHash(l.name));
        mix(qHash(l.color));
        mix(l.visible ? 1u : 2u);
        mix(l.locked ? 3u : 4u);
        mix(static_cast<quint64>(l.weight * 1000.0));
    }
    return h;
}

void CanvasWidget::paintEvent(QPaintEvent*) {
    const ViewTransform vt = transform();
    const qreal dpr = devicePixelRatioF();
    const QSize devSize = size() * dpr;

    // A layer that has been switched off (or recoloured, or reweighted) since the cache was built
    // means the cache is out of date, whoever changed it and whether or not they said so.
    if (const quint64 stamp = layerFingerprint(); stamp != layerStamp_) {
        layerStamp_ = stamp;
        sceneDirty_ = true;
    }

    // Rebuild the static scene (grid + entities) only when it actually changed;
    // cursor-move previews reuse this cache so the overlay tracks without lag.
    if (sceneDirty_ || sceneCache_.size() != devSize) {
        sceneCache_ = QPixmap(devSize);
        sceneCache_.setDevicePixelRatio(dpr);
        sceneCache_.fill(bgColor_);
        QPainter sp(&sceneCache_);
        sp.setRenderHint(QPainter::Antialiasing, true);
        bool paperClip = false;
        if (doc_) {
            if (layoutMode_) {
                Bounds paper = doc_->sheet();
                if (!paper.valid) {
                    paper.expand({0.0, 0.0});
                    paper.expand({420.0, 297.0});
                }
                const QPointF topLeft = vt.toScreen({paper.min.x, paper.max.y});
                const QPointF bottomRight = vt.toScreen({paper.max.x, paper.min.y});
                const QRectF paperRect = QRectF(topLeft, bottomRight).normalized();
                sp.setPen(QPen(QColor("#555b63"), 1.0));
                sp.setBrush(Qt::white);
                sp.drawRect(paperRect);
                sp.save();
                sp.setClipRect(paperRect, Qt::IntersectClip);
                paperClip = true;
            } else {
                drawGrid(sp, vt);
                drawOriginMarker(sp, vt);
            }
            // Viewport culling: only entities whose bounds intersect the visible
            // world rectangle (plus a margin for line widths / labels) are drawn, so
            // pan/zoom/edit on a large drawing don't re-render off-screen geometry.
            const Vec2 c0 = vt.toWorld(QPointF(0, 0));
            const Vec2 c1 = vt.toWorld(QPointF(width(), height()));
            double vMinX = std::min(c0.x, c1.x), vMaxX = std::max(c0.x, c1.x);
            double vMinY = std::min(c0.y, c1.y), vMaxY = std::max(c0.y, c1.y);
            // The margin is a share of what is on screen, so it shrinks as the view zooms in - which
            // is right for a pen width and wrong for anything measured in world units. It is the
            // object's own reach that has to cover those (see drawnBounds), with a floor here of a
            // millimetre or two for the symbols drawn at a fixed size in pixels.
            const double mx = (vMaxX - vMinX) * 0.05 + 2.0;
            const double my = (vMaxY - vMinY) * 0.05 + 2.0;
            vMinX -= mx; vMaxX += mx; vMinY -= my; vMaxY += my;

            // Selected entities are drawn live in the overlay (so they can be
            // dragged without rebuilding the cache); skip them here.
            QSet<QString> selected;
            if (editor_)
                for (const auto& id : editor_->selection()) selected.insert(id);
            for (const auto& e : doc_->entities()) {
                if (const Layer* layer = doc_->findLayer(e.layer); layer && !layer->visible)
                    continue;
                if (selected.contains(e.id)) continue;
                // Where the object is DRAWN, not where its geometry is: a double run's two lines are
                // gap/2 either side of the path this used to test, so zooming in on one of those lines
                // culled the object whose line fills the screen. That is what made lines disappear as
                // the view zoomed in.
                const Bounds b = drawnBounds(e);
                if (b.valid && (b.max.x < vMinX || b.min.x > vMaxX ||
                                b.max.y < vMinY || b.min.y > vMaxY))
                    continue; // entirely off-screen
                EntityRenderer::draw(sp, e, *doc_, vt);
            }
            if (paperClip) sp.restore();
        }
        sceneDirty_ = false;
        sceneOffset_ = QPointF(0, 0); // cache now matches the current view
    }

    QPainter p(this);
    // During a pan the cache is blitted shifted (no per-tick rebuild); 0 otherwise.
    // Fill the whole viewport with the canvas colour first so the strip exposed on the
    // leading edge by the shift shows the drawing background, not the widget's grey.
    if (!sceneOffset_.isNull()) p.fillRect(rect(), bgColor_);
    p.drawPixmap(sceneOffset_, sceneCache_);
    if (doc_ && editor_) {
        p.setRenderHint(QPainter::Antialiasing, true);
        editor_->drawOverlay(p, vt);
    }
    // Print-window rubber band (Export PDF window): a dashed rectangle with a light fill.
    if (capturing_ && captureActive_) {
        const QRectF r = QRectF(QPointF(captureStart_), QPointF(captureCur_)).normalized();
        p.setPen(QPen(QColor("#2bd0ff"), 1, Qt::DashLine));
        p.setBrush(QColor(43, 208, 255, 32));
        p.drawRect(r);
        p.setBrush(Qt::NoBrush);
    }
    // The crosshair is painted by the always-on-top `crosshairTop_` overlay (so it
    // stays visible above the inline text editor), not here.
    if (crosshairTop_) crosshairTop_->update();
}

// Full-viewport crosshair with a central pick box, AutoCAD style. Tracks the
// snapped cursor so it locks visibly onto snap points.
void CanvasWidget::drawCrosshair(QPainter& p) const {
    QPointF c = lastMousePos_;
    if (editor_ && editor_->currentSnap().snapped)
        c = transform().toScreen(editor_->currentSnap().point);
    p.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(crosshairColor_);
    pen.setCosmetic(true);
    pen.setWidth(1);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const double pick = pickboxPx_; // half-size of the central pick box
    // Arm length: 100% spans the whole viewport; smaller = a compact crosshair.
    const double arm = (crosshairPct_ >= 100)
                           ? std::max(width(), height())
                           : std::min(width(), height()) * 0.5 * crosshairPct_ / 100.0;
    p.drawLine(QPointF(std::max(0.0, c.x() - arm), c.y()), QPointF(c.x() - pick, c.y()));
    p.drawLine(QPointF(c.x() + pick, c.y()), QPointF(std::min<double>(width(), c.x() + arm), c.y()));
    p.drawLine(QPointF(c.x(), std::max(0.0, c.y() - arm)), QPointF(c.x(), c.y() - pick));
    p.drawLine(QPointF(c.x(), c.y() + pick), QPointF(c.x(), std::min<double>(height(), c.y() + arm)));
    p.drawRect(QRectF(c.x() - pick, c.y() - pick, 2 * pick, 2 * pick));
}

void CanvasWidget::resizeEvent(QResizeEvent* e) {
    if (crosshairTop_) crosshairTop_->setGeometry(rect());
    positionBottomOverlay();
    if (bottomOverlay_) bottomOverlay_->raise();
    QWidget::resizeEvent(e);
}

void CanvasWidget::childEvent(QChildEvent* e) {
    // Whenever any other child is added (e.g. the host's inline text editor), make
    // sure the crosshair overlay is restacked on top so the cursor stays visible.
    if (e->added() && crosshairTop_ && e->child() != crosshairTop_)
        crosshairTop_->raise();
    QWidget::childEvent(e);
}

void CanvasWidget::setCommandWords(const QStringList& words, const QStringList& labels) {
    if (!completer_) {
        completer_ = new QCompleter(this);
        completer_->setCaseSensitivity(Qt::CaseInsensitive);
        completer_->setFilterMode(Qt::MatchStartsWith);
        completer_->setCompletionMode(QCompleter::PopupCompletion);
        if (dynInput_) dynInput_->setCompleter(completer_);
        completer_->popup()->installEventFilter(this); // Enter submits typed text
    }
    // The popup shows the label ("tr — Trim …"); accepting inserts the bare word.
    auto* model = new QStandardItemModel(completer_);
    for (int i = 0; i < words.size(); ++i) {
        auto* it = new QStandardItem();
        it->setData(i < labels.size() ? labels[i] : words[i], Qt::DisplayRole);
        it->setData(words[i], Qt::EditRole); // completionRole (what gets inserted)
        model->appendRow(it);
    }
    completer_->setModel(model);
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
    emit viewChanged();
    e->accept();
}

void CanvasWidget::contextMenuEvent(QContextMenuEvent* e) {
    // Suppressed (PreventContextMenu); the menu is driven from mouseReleaseEvent so
    // a right-DRAG can offer Move/Copy instead.
    e->accept();
}

void CanvasWidget::beginWindowCapture(std::function<void(Vec2, Vec2)> onDone) {
    capturing_ = true;
    captureActive_ = false;
    captureCb_ = std::move(onDone);
    setCursor(Qt::CrossCursor);
    setFocus(Qt::OtherFocusReason);
    update();
}

void CanvasWidget::mousePressEvent(QMouseEvent* e) {
    // Rubber-band window capture (Export PDF window): the left press anchors the rect.
    if (capturing_ && e->button() == Qt::LeftButton) {
        captureActive_ = true;
        captureStart_ = e->pos();
        captureCur_ = e->pos();
        update();
        e->accept();
        return;
    }
    // A click on the bare canvas while the on-canvas text editor is open commits it
    // (clicks on the editor/toolbar children are handled by those widgets).
    if (inlineEditing_ && e->button() == Qt::LeftButton) {
        emit inlineCommit();
        e->accept();
        return;
    }
    if (e->button() == Qt::MiddleButton) { // middle-drag pans
        panning_ = true;
        lastPanPos_ = e->pos();
        sceneOffset_ = QPointF(0, 0); // cache matches the view at pan start
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    if (e->button() == Qt::RightButton) {
        // Right press: may become a drag (move/copy) or, if it doesn't move, the
        // context menu (decided on release).
        rightDown_ = true;
        rightDragging_ = false;
        rightPressPos_ = e->pos();
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
    if (capturing_) {
        if (captureActive_) { captureCur_ = e->pos(); update(); }
        e->accept();
        return; // no drawing/selection while capturing a print window
    }
    if (panning_ && doc_) {
        const QPoint delta = e->pos() - lastPanPos_;
        lastPanPos_ = e->pos();
        doc_->view().x += delta.x();
        doc_->view().y += delta.y();
        // Re-render the scene at the new view each tick so the grid + entities fill the
        // WHOLE viewport while panning (a shifted blit would leave a grid-less strip on
        // the leading edge).
        invalidateScene();
        emit viewChanged();
    }
    const Vec2 world = transform().toWorld(e->position());
    if (editor_) editor_->onPointerMove(world);
    // Right-button drag: once moved past a small threshold, start (or continue)
    // dragging the selection / object under the press point.
    if (rightDown_ && editor_) {
        if (!rightDragging_ &&
            (e->pos() - rightPressPos_).manhattanLength() > 4) {
            rightDragging_ = editor_->beginRightDrag(transform().toWorld(rightPressPos_));
            if (!rightDragging_) rightDown_ = false; // nothing to drag -> plain menu on release
        }
        if (rightDragging_) editor_->updateRightDrag(world);
    }
    // The flying input field tracks the cursor while it's open (after onPointerMove
    // so it follows the snapped crosshair).
    if (dynInput_ && dynInput_->isVisible()) positionDynInput();
    emit cursorWorldMoved(world);
    update(); // repaint so the drawn crosshair follows the cursor
}

void CanvasWidget::enterEvent(QEnterEvent* e) {
    cursorInside_ = true;
    lastMousePos_ = e->position().toPoint();
    // While placing/drawing, take keyboard focus as the cursor enters the canvas so
    // shortcuts (Tab to edit a connector, X/Y flip, typed values) are routed here even
    // if focus was left on the ribbon. Entering never fires mid-edit (the mouse stays
    // over the ribbon then), so this won't steal focus from a field being typed into.
    if (editor_ && !inlineEditing_ && !hasFocus() &&
        (isDrawTool(editor_->tool()) || isPlacementTool(editor_->tool())))
        setFocus(Qt::MouseFocusReason);
    update();
    QWidget::enterEvent(e);
}

void CanvasWidget::leaveEvent(QEvent* e) {
    cursorInside_ = false;
    update();
    QWidget::leaveEvent(e);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (capturing_ && e->button() == Qt::LeftButton) {
        const ViewTransform vt = transform();
        const Vec2 a = vt.toWorld(QPointF(captureStart_));
        const Vec2 b = vt.toWorld(QPointF(captureActive_ ? e->pos() : captureStart_));
        auto cb = captureCb_;
        capturing_ = false;
        captureActive_ = false;
        captureCb_ = nullptr;
        setCursor(inlineEditing_ ? Qt::ArrowCursor : Qt::BlankCursor);
        update();
        // Ignore a zero-area window (a click without a drag).
        if (cb && std::abs(b.x - a.x) > 1e-6 && std::abs(b.y - a.y) > 1e-6) cb(a, b);
        e->accept();
        return;
    }
    if (panning_ && e->button() == Qt::MiddleButton) {
        panning_ = false;
        setCursor(inlineEditing_ ? Qt::ArrowCursor
                                 : Qt::BlankCursor); // resume the drawn crosshair after panning
        invalidateScene(); // rebuild once for the final view (fills exposed edges)
        e->accept();
        return;
    }
    if (e->button() == Qt::RightButton && rightDown_) {
        rightDown_ = false;
        if (rightDragging_) {
            rightDragging_ = false;
            // Dropped after a drag: the host offers Move / Copy / Cancel.
            emit rightDragDropped(e->globalPosition().toPoint());
        } else {
            // A plain right-click: the normal canvas context menu.
            emit contextMenuRequested(e->globalPosition().toPoint());
        }
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
    // Pop the floating command field next to the crosshair:
    //   - while drafting/a draw tool is active: any value or option character
    //     (direct distance entry, "g" gap, "@dx,dy", commands, ...);
    //   - when idle with the crosshair on the canvas: a letter starts a command
    //     (AutoCAD-style command entry at the cursor).
    // Esc aborts an in-progress print-window capture.
    if (capturing_ && e->key() == Qt::Key_Escape) {
        capturing_ = false;
        captureActive_ = false;
        captureCb_ = nullptr;
        setCursor(inlineEditing_ ? Qt::ArrowCursor : Qt::BlankCursor);
        update();
        e->accept();
        return;
    }
    if (editor_) {
        // During a right-button drag, X / Y mirror the dragged set (about the catch
        // point): X mirrors across the Y axis (flips X), Y mirrors across the X axis
        // (flips Y). Handle first so the letters don't open the command field.
        if (editor_->isRightDragging() && (e->key() == Qt::Key_X || e->key() == Qt::Key_Y)) {
            editor_->rightDragMirror(e->key() == Qt::Key_Y); // Y key -> X-axis mirror
            e->accept();
            return;
        }
        // Placing a connector: X mirrors the preview across the Y axis, Y across the X
        // axis (both flip the ghost; the placed connector keeps the flip).
        if (editor_->tool() == ToolId::Connector &&
            (e->key() == Qt::Key_X || e->key() == Qt::Key_Y)) {
            editor_->toggleConnectorMirror(e->key() == Qt::Key_X);
            e->accept();
            return;
        }
        // Paste preview: X / Y mirror the clipboard contents before dropping them.
        if (editor_->isPastePlacing() && (e->key() == Qt::Key_X || e->key() == Qt::Key_Y)) {
            editor_->togglePasteMirror(e->key() == Qt::Key_X);
            e->accept();
            return;
        }
        // Space while something is being MOVED turns it a quarter turn: clockwise, or the other way
        // with Shift. Handled before anything else does something with Space (idle, it repeats the last
        // tool), and only while a move is actually under way.
        if (editor_->movingSomething() && e->key() == Qt::Key_Space) {
            editor_->rotateMoveBy90(!(e->modifiers() & Qt::ShiftModifier));
            e->accept();
            return;
        }
        // Anything held under the cursor waiting to be dropped - a paste, a block, a part out of the
        // library, a generated harness: R turns it a quarter turn clockwise, Shift+R the other way, so
        // it goes in the way round it is wanted. Handled here, before an idle letter opens the command
        // box, for the same reason X and Y are.
        if (editor_->placementPreviewActive() && e->key() == Qt::Key_R) {
            editor_->rotatePlacementPreview(!(e->modifiers() & Qt::ShiftModifier));
            e->accept();
            return;
        }
        // Tab switches the active dynamic-input reference field (Length<->Angle,
        // X radius<->Y radius, ...) even before the input box has been opened.
        // (focusNextPrevChild is overridden so Tab reaches us instead of moving focus.)
        if ((e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab) &&
            editor_->drawInputFieldLabels().size() > 1) {
            editor_->cycleDrawInputField();
            if (!dynInput_->isVisible()) showDynInput(QString());
            dynInput_->setPlaceholderText(editor_->currentDrawFieldLabel());
            e->accept();
            return;
        }
        // Wiring from a connector pin: Shift + a pin number toggles that ONE pin's wire in
        // or out of the fan (Shift+3 adds/removes wire 3). Shift+digit is reported either as
        // Key_1..Key_9 OR (US layout) as the shifted symbol key (!,@,#,...), so map both.
        if (editor_->wireFanArmed() && (e->modifiers() & Qt::ShiftModifier)) {
            int dgt = 0;
            switch (e->key()) {
            case Qt::Key_1: case Qt::Key_Exclam: dgt = 1; break;
            case Qt::Key_2: case Qt::Key_At: dgt = 2; break;
            case Qt::Key_3: case Qt::Key_NumberSign: dgt = 3; break;
            case Qt::Key_4: case Qt::Key_Dollar: dgt = 4; break;
            case Qt::Key_5: case Qt::Key_Percent: dgt = 5; break;
            case Qt::Key_6: case Qt::Key_AsciiCircum: dgt = 6; break;
            case Qt::Key_7: case Qt::Key_Ampersand: dgt = 7; break;
            case Qt::Key_8: case Qt::Key_Asterisk: dgt = 8; break;
            case Qt::Key_9: case Qt::Key_ParenLeft: dgt = 9; break;
            default: break;
            }
            if (dgt > 0) {
                editor_->toggleWireFanPin(dgt);
                e->accept();
                return;
            }
        }
        // Pressing Shift on its own while a fan-out is armed routes a wire from EVERY pin of
        // the connector (including the ones ABOVE the pin the wire started on).
        if (editor_->wireFanArmed() && e->key() == Qt::Key_Shift && !e->isAutoRepeat()) {
            editor_->setWireFanAll();
            e->accept();
            return;
        }
        const QString t = e->text();
        const bool isLetter = !t.isEmpty() && t[0].isLetter();
        const bool isValueChar =
            !t.isEmpty() && (t[0].isDigit() || t == "." || t == "-" || t == "@" || t == ",");
        const bool drafting = editor_->wantsTextInput();
        bool open = false;
        if (drafting) {
            // While a draw tool is active: numbers/coords always (distance & x,y
            // entry); letters ONLY if they are a real option for this tool — so a
            // stray "f" during a Line is ignored instead of popping a command box.
            open = isValueChar || (isLetter && editor_->isDrawOptionChar(t[0]));
        } else if (editor_->wantsValueInput() && isValueChar) {
            open = true; // modify tool (e.g. Scale) awaiting a typed value/factor
        } else if (cursorInside_ && isLetter) {
            open = true; // idle: a letter starts a command at the cursor
        }
        if (open) {
            showDynInput(t); // floating input box next to the cursor
            e->accept();
            return;
        }
        if (isLetter && drafting) { e->accept(); return; } // swallow irrelevant letters, no jump
    }
    if (editor_ && editor_->onKey(e->key(), e->modifiers())) {
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

bool CanvasWidget::focusNextPrevChild(bool next) {
    // While drafting with more than one dynamic-input field (Length/Angle, radius/
    // angle, ...), Tab/Backtab must cycle those fields — so refuse focus traversal and
    // let the key fall through to keyPressEvent (Qt tries focus nav BEFORE keyPress).
    if (editor_ && editor_->drawInputFieldLabels().size() > 1) return false;
    // Tab while a twisted bundle is being drawn: pin it, so the number of wires and the rest can be
    // set on the ribbon with the real thing on screen. Checked before the dynamic-input fields, because
    // for these tools pinning is what Tab is for. Nothing to pin -> falls through as usual.
    if (editor_ && next && Editor::isTwistedTool(editor_->tool())) {
        editor_->freezeTwistedPreview();
        if (editor_->twistedFrozen()) return true;
    }
    // Tab while placing a break mark: pin the preview so the ribbon's numbers can be tried on it.
    if (editor_ && next && editor_->tool() == ToolId::LengthBreak) {
        editor_->freezeBreakPreview();
        return true;
    }
    // Tab while placing a connector: hand focus to the Connector build ribbon so its
    // preferences can be edited; the placement preview stays live.
    if (editor_ && next && editor_->tool() == ToolId::Connector) {
        editor_->freezeConnectorPreview(); // pin the ghost in place while editing settings
        emit connectorEditRequested();
        return true;
    }
    // Tab mid-drag while placing a Screen: freeze the crossing-line end so the Wire
    // ribbon's Screen fields can be tuned without the drag changing the caught wires.
    if (editor_ && next && editor_->tool() == ToolId::Wire && editor_->screenArmed()) {
        editor_->freezeScreenPreview();
        return true;
    }
    // Tab mid-drag while placing a Twist: freeze the crossing-line end so the ribbon's
    // twist parameters can be tuned without the drag changing the caught wires.
    if (editor_ && next && !editor_->twistArmType().isEmpty()) {
        editor_->freezeTwistPreview();
        return true;
    }
    return QWidget::focusNextPrevChild(next);
}

} // namespace Edraw_Desktop
