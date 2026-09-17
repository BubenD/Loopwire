#include <QTimer>
#include "render/CanvasWidget.h"

#include "app/Editor.h"
#include "geometry/Geometry.h"

#include <QChildEvent>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QEnterEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QLineEdit>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QSet>
#include <QAbstractItemView>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QUuid>
#include <QWheelEvent>
#include <algorithm>
#include <QPolygonF>
#include <QVector>
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
    const QPoint anchor = crosshairScreenPos().toPoint();
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

void CanvasWidget::setDrawingDocument(Document* doc) {
    drawingDoc_ = doc;
    invalidateScene();
}

void CanvasWidget::setLayoutMode(bool on) {
    if (layoutMode_ == on) return;
    layoutMode_ = on;
    selectedViewports_.clear();
    activeViewport_ = -1;
    EntityRenderer::setWhitePaper(on);
    EntityRenderer::setBackgroundColor(on ? QColor(Qt::white) : bgColor_);
    invalidateScene();
    syncEditorScale();   // the sheet and its windows are what there is to snap to now

}

void CanvasWidget::setLayoutIndex(int index) {
    layoutIndex_ = std::max(0, index);
    selectedViewports_.clear();
    activeViewport_ = -1;
    invalidateScene();
    syncEditorScale();   // a different sheet, a different edge and different windows

}

DrawingLayout* CanvasWidget::currentLayout() {
    Document* drawing = drawingDocument();
    if (!drawing || layoutIndex_ < 0 ||
        layoutIndex_ >= static_cast<int>(drawing->layouts().size()))
        return nullptr;
    return &drawing->layouts()[static_cast<size_t>(layoutIndex_)];
}

const DrawingLayout* CanvasWidget::currentLayout() const {
    const Document* drawing = drawingDocument();
    if (!drawing || layoutIndex_ < 0 ||
        layoutIndex_ >= static_cast<int>(drawing->layouts().size()))
        return nullptr;
    return &drawing->layouts()[static_cast<size_t>(layoutIndex_)];
}

void CanvasWidget::addLayoutViewport() {
    DrawingLayout* layout = currentLayout();
    if (!layout) return;
    LayoutViewport viewport;
    viewport.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    const double inset = 20.0 + 10.0 * layout->viewports.size();
    viewport.x = inset;
    viewport.y = inset;
    viewport.width = std::max(40.0, layout->paperWidth - inset * 2.0);
    viewport.height = std::max(40.0, layout->paperHeight - inset * 2.0);
    layout->viewports.push_back(viewport);
    selectedViewports_ = {static_cast<int>(layout->viewports.size()) - 1};
    doc_->setDirty(true);
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
    if (editor)
        connect(editor, &Editor::regionSelected, this,
                [this](const std::vector<Vec2>& region, bool crossing, bool subtract) {
                    selectLayoutWindowsIn(region, crossing, subtract);
                });

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
    // A sheet with nothing drawn on it is not the sheet. A file can state a drawing area it has long since
    // outgrown - limits of 12 by 9 with the drawing out at 20 to 47 - and fitting to that opens the window
    // on empty paper with the drawing off the screen, which is indistinguishable from a file that did not
    // open at all.
    if (box.valid && !layoutMode_) {
        bool anythingOnIt = false;
        for (const auto& e : doc_->entities()) {
            const Bounds b = e.bounds();
            if (!b.valid) continue;
            if (b.max.x < box.min.x || b.min.x > box.max.x) continue;
            if (b.max.y < box.min.y || b.min.y > box.max.y) continue;
            anythingOnIt = true;
            break;
        }
        if (!anythingOnIt) box = {};
    }
    if (layoutMode_) {
        box = {};
        const DrawingLayout* layout = currentLayout();
        box.expand({0.0, 0.0});
        box.expand({layout ? layout->paperWidth : 420.0, layout ? layout->paperHeight : 297.0});
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

QRectF CanvasWidget::layoutViewportRect(const LayoutViewport& viewport,
                                        const ViewTransform& paper) {
    return QRectF(paper.toScreen({viewport.x, viewport.y + viewport.height}),
                  paper.toScreen({viewport.x + viewport.width, viewport.y})).normalized();
}

QRectF CanvasWidget::viewportScreenRect(const LayoutViewport& viewport,
                                        const ViewTransform& paper) const {
    return layoutViewportRect(viewport, paper);
}

ViewTransform CanvasWidget::viewportTransform(const LayoutViewport& viewport, const QRectF& rect,
                                              const ViewTransform& paper) const {
    Bounds model;
    const Document* drawing = drawingDocument();
    for (const Entity& entity : drawing->entities()) model.expand(entity.bounds());
    return layoutViewportView(viewport, rect, paper, model);
}

ViewTransform CanvasWidget::layoutViewportView(const LayoutViewport& viewport, const QRectF& rect,
                                               const ViewTransform& paper,
                                               const Bounds& model) {
    const Vec2 center = viewport.modelScale > 0.0 || !model.valid ? viewport.modelCenter : model.center();
    double scale = viewport.modelScale > 0.0 ? viewport.modelScale * paper.scale() : 1.0;
    if (viewport.modelScale <= 0.0 && model.valid) {
        scale = std::min(rect.width() / std::max(1.0, model.width() * 1.1),
                         rect.height() / std::max(1.0, model.height() * 1.1));
    }
    ViewTransform modelView;
    modelView.view.scale = std::max(0.0005, scale);
    modelView.view.x = rect.center().x() - center.x * modelView.view.scale;
    modelView.view.y = rect.center().y() + center.y * modelView.view.scale;
    return modelView;
}

ViewTransform CanvasWidget::inputTransform() const {
    // On the paper, the paper's. Inside a window, the model's AS THAT WINDOW SEES IT - which is what lets
    // the tools work in the harness through a window: a line drawn there is a line in the model, at the
    // size the model is, however small the window shows it.
    if (!layoutMode_ || activeViewport_ < 0) return transform();
    const DrawingLayout* layout = currentLayout();
    if (!layout || activeViewport_ >= static_cast<int>(layout->viewports.size())) return transform();
    const LayoutViewport& viewport = layout->viewports[static_cast<size_t>(activeViewport_)];
    const ViewTransform paper = transform();
    return viewportTransform(viewport, viewportScreenRect(viewport, paper), paper);
}

void CanvasWidget::enterLayoutViewport(int index) {
    const DrawingLayout* layout = currentLayout();
    if (!layout || index < 0 || index >= static_cast<int>(layout->viewports.size())) return;
    if (activeViewport_ == index) return;
    activeViewport_ = index;
    selectedViewports_ = {index};
    viewportPanning_ = false;
    viewportMoving_ = false;
    viewportDragCorner_ = -1;
    syncEditorScale();
    invalidateScene();
    emit viewportEntered(true);
}

void CanvasWidget::leaveLayoutViewport() {
    if (activeViewport_ < 0) return;
    activeViewport_ = -1;
    viewportPanning_ = false;
    syncEditorScale();
    invalidateScene();
    emit viewportEntered(false);
}

void CanvasWidget::removeLayoutViewport(int index) {
    DrawingLayout* layout = currentLayout();
    if (!layout) return;
    // The one named, or - when none is - every window picked out. Taken from the back so that the
    // indices of the ones still to go do not shift under it.
    std::vector<int> going;
    if (index >= 0) going.push_back(index);
    else going = selectedViewports_;
    std::sort(going.begin(), going.end(), std::greater<int>());
    going.erase(std::unique(going.begin(), going.end()), going.end());
    bool wasInside = false;
    bool any = false;
    for (const int at : going) {
        if (at < 0 || at >= static_cast<int>(layout->viewports.size())) continue;
        if (activeViewport_ == at) wasInside = true;
        layout->viewports.erase(layout->viewports.begin() + at);
        any = true;
    }
    if (!any) return;
    selectedViewports_.clear();
    viewportDragCorner_ = -1;
    viewportMoving_ = false;
    viewportPanning_ = false;
    const bool wasIn = activeViewport_ >= 0;
    activeViewport_ = -1;
    drawingDocument()->setDirty(true);
    invalidateScene();
    if (wasInside || wasIn) emit viewportEntered(false);
}

void CanvasWidget::selectLayoutWindowsIn(const std::vector<Vec2>& region, bool crossing,
                                         bool subtract) {
    // Only out on the paper. Inside a window the zone belongs to the model being worked in through it,
    // and picking the window itself with it would be picking the thing being looked through.
    if (!layoutMode_ || activeViewport_ >= 0) return;
    const DrawingLayout* layout = currentLayout();
    if (!layout || region.size() < 3) return;
    QPolygonF zone;
    for (const Vec2& p : region) zone << QPointF(p.x, p.y);
    if (!zone.isClosed()) zone << zone.first();
    for (int i = 0; i < static_cast<int>(layout->viewports.size()); ++i) {
        const LayoutViewport& viewport = layout->viewports[static_cast<size_t>(i)];
        QPolygonF frame;
        frame << QPointF(viewport.x, viewport.y)
              << QPointF(viewport.x + viewport.width, viewport.y)
              << QPointF(viewport.x + viewport.width, viewport.y + viewport.height)
              << QPointF(viewport.x, viewport.y + viewport.height)
              << QPointF(viewport.x, viewport.y);
        bool caught = false;
        if (crossing) {
            caught = zone.intersects(frame);   // green: a touch anywhere is enough
        } else {
            caught = true;                     // blue: the whole window, or none of it
            for (const QPointF& corner : frame)
                if (!zone.containsPoint(corner, Qt::OddEvenFill)) { caught = false; break; }
        }
        if (!caught) continue;
        auto at = std::find(selectedViewports_.begin(), selectedViewports_.end(), i);
        if (subtract) {
            if (at != selectedViewports_.end()) selectedViewports_.erase(at);
        } else if (at == selectedViewports_.end()) {
            selectedViewports_.push_back(i);
        }
    }
    invalidateScene();
}

void CanvasWidget::freezeViewportView(LayoutViewport& viewport) {
    // A window with no scale of its own fits the drawing to its rectangle - so changing the rectangle
    // would change how big the drawing is drawn. Pinning the fit as a scale of its own first means the
    // rectangle can then be resized without the drawing inside it changing size: the window crops.
    if (viewport.modelScale > 0.0) return;
    const ViewTransform paper = transform();
    const QRectF rect = viewportScreenRect(viewport, paper);
    const ViewTransform fitted = viewportTransform(viewport, rect, paper);
    Bounds model;
    for (const Entity& entity : drawingDocument()->entities()) model.expand(entity.bounds());
    if (model.valid) viewport.modelCenter = model.center();
    viewport.modelScale = fitted.scale() / paper.scale();
}

int CanvasWidget::viewportGrabAt(const QPointF& point, const DrawingLayout& layout,
                                 int* corner) const {
    // A window is taken hold of BY ITS FRAME - its edge, or one of the four corner handles. Its inside is
    // paper like the rest of the sheet, which is what lets a line be drawn across a window, something be
    // selected through one, and a note be moved over one. Were the whole rectangle a handle, a window
    // would be a hole in the sheet that nothing could be put on.
    if (corner) *corner = -1;
    const ViewTransform paper = transform();
    for (int i = static_cast<int>(layout.viewports.size()) - 1; i >= 0; --i) {
        const QRectF rect = viewportScreenRect(layout.viewports[static_cast<size_t>(i)], paper);
        const QPointF corners[] = {rect.topLeft(), rect.topRight(), rect.bottomRight(),
                                   rect.bottomLeft()};
        for (int c = 0; c < 4; ++c)
            if (QLineF(corners[c], point).length() <= 9.0) {
                if (corner) *corner = c;
                return i;
            }
        const QLineF edges[] = {QLineF(rect.topLeft(), rect.topRight()),
                                QLineF(rect.topRight(), rect.bottomRight()),
                                QLineF(rect.bottomRight(), rect.bottomLeft()),
                                QLineF(rect.bottomLeft(), rect.topLeft())};
        for (const QLineF& edge : edges) {
            const QPointF a = edge.p1(), b = edge.p2();
            const QPointF ab = b - a;
            const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
            if (len2 <= 1e-9) continue;
            const QPointF ap = point - a;
            const double u = std::clamp((ap.x() * ab.x() + ap.y() * ab.y()) / len2, 0.0, 1.0);
            if (QLineF(a + u * ab, point).length() <= 5.0) return i;
        }
    }
    return -1;
}

int CanvasWidget::viewportAt(const QPointF& point) const {
    const DrawingLayout* layout = currentLayout();
    if (!layout) return -1;
    const ViewTransform paper = transform();
    for (int i = static_cast<int>(layout->viewports.size()) - 1; i >= 0; --i)
        if (viewportScreenRect(layout->viewports[static_cast<size_t>(i)], paper).contains(point))
            return i;
    return -1;
}

void CanvasWidget::drawLayoutScene(QPainter& painter, const ViewTransform& paper) {
    const DrawingLayout* layout = currentLayout();
    if (!layout) return;
    const QRectF sheet = QRectF(paper.toScreen({0.0, layout->paperHeight}),
                                paper.toScreen({layout->paperWidth, 0.0})).normalized();
    painter.setPen(QPen(QColor("#555b63"), 1.0));
    painter.setBrush(Qt::white);
    painter.drawRect(sheet);
    painter.save();
    // The windows are NOT clipped to the sheet - one can be moved off the paper, and half a window over
    // the edge must show as half a window rather than disappearing.
    for (int i = 0; i < static_cast<int>(layout->viewports.size()); ++i) {
        const LayoutViewport& viewport = layout->viewports[static_cast<size_t>(i)];
        const QRectF rect = viewportScreenRect(viewport, paper);
        const ViewTransform modelView = viewportTransform(viewport, rect, paper);
        painter.save();
        painter.setClipRect(rect, Qt::IntersectClip);
        painter.fillRect(rect, Qt::white);
        drawGrid(painter, modelView);
        const Document& drawing = *drawingDocument();
        for (const Entity& entity : drawing.entities()) {
            if (const Layer* layer = drawing.findLayer(entity.layer); layer && !layer->visible) continue;
            EntityRenderer::draw(painter, entity, drawing, modelView);
        }
        painter.restore();
        painter.setBrush(Qt::NoBrush);
        const bool picked = layoutViewportPicked(i);
        painter.setPen(QPen(i == activeViewport_ ? QColor("#24a8ff")
                            : picked             ? QColor("#3d8bfd")
                                                 : QColor("#606a75"),
                            i == activeViewport_ || picked ? 2.0 : 1.0));
        painter.drawRect(rect);
        if (picked && activeViewport_ < 0) {
            painter.setBrush(QColor("#3d8bfd"));
            for (const QPointF& corner : {rect.topLeft(), rect.topRight(), rect.bottomRight(),
                                          rect.bottomLeft()})
                painter.drawRect(QRectF(corner.x() - 4.0, corner.y() - 4.0, 8.0, 8.0));
        }
    }
    // What is ON the paper - drawn in the document being edited, paper space - goes on last, OVER the
    // windows. A window is filled with the view through it, so anything drawn under one would be painted
    // out: and a note, a leader or a border drawn on the sheet is meant to be read across the view it
    // points at. Drawn last, it is there as soon as it is drawn, wherever on the sheet it was put.
    if (doc_) {
        painter.setClipRect(sheet, Qt::IntersectClip);   // what is drawn on the sheet stays on the sheet
        for (const Entity& entity : doc_->entities()) {
            if (const Layer* layer = doc_->findLayer(entity.layer); layer && !layer->visible) continue;
            EntityRenderer::draw(painter, entity, *doc_, paper);
        }
    }
    painter.restore();
}

void CanvasWidget::drawSceneContent(QPainter& sp, const ViewTransform& vt, const QRectF& area) {
    if (!doc_) return;
    drawGrid(sp, vt);
    drawOriginMarker(sp, vt);
    // Only what `area` can show. When the whole widget is being drawn that is the whole widget; when a
    // pan has brought a strip into view it is that strip, and the culling below then throws away
    // everything the strip cannot contain - which is what makes a pan cost the strip and not the sheet.
    const Vec2 c0 = vt.toWorld(area.topLeft());
    const Vec2 c1 = vt.toWorld(area.bottomRight());
    double vMinX = std::min(c0.x, c1.x), vMaxX = std::max(c0.x, c1.x);
    double vMinY = std::min(c0.y, c1.y), vMaxY = std::max(c0.y, c1.y);
    const double mx = (vMaxX - vMinX) * 0.05 + 2.0;
    const double my = (vMaxY - vMinY) * 0.05 + 2.0;
    vMinX -= mx; vMaxX += mx; vMinY -= my; vMaxY += my;
    QSet<QString> selected;
    if (editor_)
        for (const auto& id : editor_->selection()) selected.insert(id);

    // A BIG SELECTION IS DRAWN HERE, HIGHLIGHT AND ALL, INSTEAD OF ON EVERY FRAME.
    //
    // The selected objects are normally left out of this cache so the overlay can paint them in the
    // selection colour - which is the right trade while the selection is small, because then picking
    // something costs no cache rebuild. It stops being the right trade when the selection is most of
    // the drawing: the cache ends up holding almost nothing, and the whole sheet is re-rendered on
    // every mouse move.
    //
    // Above the threshold the highlight is baked in. The canvas already invalidates this cache when
    // the selection changes, so the extra cost is one rebuild per change and every frame in between
    // is a blit. Editor::selectionIsBaked decides, and the overlay skips the same objects.
    const bool bakeSelection = editor_ && editor_->selectionIsBaked();

    for (const auto& e : doc_->entities()) {
        if (const Layer* layer = doc_->findLayer(e.layer); layer && !layer->visible) continue;
        const bool isSelected = selected.contains(e.id);
        if (isSelected && !bakeSelection) continue;   // the overlay will put it back
        const Bounds b = drawnBounds(e);
        if (b.valid && (b.max.x < vMinX || b.min.x > vMaxX || b.max.y < vMinY || b.min.y > vMaxY))
            continue;
        EntityRenderer::draw(sp, e, *doc_, vt,
                             isSelected ? editor_->selectionOverrideFor(e) : QColor());
    }
}

// A PAN, DONE THE WAY A WINDOW IS SCROLLED.
//
// Dragging the paper does not change one line of the drawing: every pixel of it is still valid, just
// somewhere else on the screen. So it is not drawn again - it is COPIED to where it now belongs, and only
// the strip along the leading edge, which was off-screen and has never been drawn, is drawn. That costs
// the strip. Re-rendering the sheet costs the sheet, sixty times a second, and on a drawing where one
// render takes longer than a frame the paper cannot help but fall behind the hand.
bool CanvasWidget::scrollScene(const QPoint& delta) {
    // A window's contents are the model's, drawn through it, and shifting the paper does not shift those.
    if (!doc_ || layoutMode_ || sceneDirty_) return false;
    const qreal dpr = devicePixelRatioF();
    if (sceneCache_.isNull() || sceneCache_.size() != size() * dpr) return false;
    // A jump bigger than the window leaves nothing to keep.
    if (std::abs(delta.x()) >= width() || std::abs(delta.y()) >= height()) return false;

    // Shift by a WHOLE number of screen pixels, so the copy is a copy and not a resample - on a display
    // at 125% a shift of seven of the widget's pixels is eight and three quarters of the screen's. What
    // is left over is carried to the next frame, where it is spent, so it cannot build up into a drawing
    // that has quietly slid a pixel or two away from where the cursor says it is.
    const QPointF want = QPointF(delta) + scrollResidual_;
    const QPoint dev(qRound(want.x() * dpr), qRound(want.y() * dpr));
    const QPointF shift(dev.x() / dpr, dev.y() / dpr);
    scrollResidual_ = want - shift;
    if (dev.isNull()) return true;   // less than a screen pixel: nothing to do but remember it

    if (scrollScratch_.size() != sceneCache_.size()) {
        scrollScratch_ = QPixmap(sceneCache_.size());
        scrollScratch_.setDevicePixelRatio(dpr);
    }
    const ViewTransform vt = transform();
    {
        QPainter sp(&scrollScratch_);
        sp.setRenderHint(QPainter::SmoothPixmapTransform, false);
        sp.drawPixmap(shift, sceneCache_);   // the paper, moved with the hand

        // The strip - or two, on a diagonal drag - that came into view. Each is taken on its own rather
        // than as one L-shaped region, because the box round an L is the whole window and culling
        // against that would draw the entire drawing again, which is the thing being avoided.
        //
        // Grown a couple of pixels so no line is left drawn hard against a clip edge: the extra is
        // painted over content that was already right, from the background up, to the same result.
        constexpr double kGrow = 2.0;
        const double w = width(), h = height();
        QVector<QRectF> strips;
        if (shift.x() > 0.0) strips.append(QRectF(0.0, 0.0, shift.x() + kGrow, h));
        else if (shift.x() < 0.0) strips.append(QRectF(w + shift.x() - kGrow, 0.0, -shift.x() + kGrow, h));
        if (shift.y() > 0.0) strips.append(QRectF(0.0, 0.0, w, shift.y() + kGrow));
        else if (shift.y() < 0.0) strips.append(QRectF(0.0, h + shift.y() - kGrow, w, -shift.y() + kGrow));
        for (const QRectF& strip : strips) {
            sp.save();
            sp.setClipRect(strip);
            sp.fillRect(strip, bgColor_);
            sp.setRenderHint(QPainter::Antialiasing, true);
            drawSceneContent(sp, vt, strip);
            sp.restore();
        }
    }
    // The scratch is now the drawing and the old cache is the scratch: swapped, not copied, so a pan
    // allocates nothing per frame.
    std::swap(sceneCache_, scrollScratch_);
    return true;
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
    // A PAN MOVES THE PAPER, IT DOES NOT REDRAW IT. The pixels the hand has moved since the last frame
    // are applied to the cache by shifting it and drawing only the edge that came into view; re-rendering
    // the whole sheet on every tick of a middle-drag is what made the paper trail behind the cursor.
    if (!pendingScroll_.isNull()) {
        if (!scrollScene(pendingScroll_)) sceneDirty_ = true;
        pendingScroll_ = QPoint();
    }

    if (sceneDirty_ || sceneCache_.size() != devSize) {
        sceneCache_ = QPixmap(devSize);
        sceneCache_.setDevicePixelRatio(dpr);
        sceneCache_.fill(bgColor_);
        QPainter sp(&sceneCache_);
        sp.setRenderHint(QPainter::Antialiasing, true);
        if (doc_ && layoutMode_) {
            drawLayoutScene(sp, vt);
        } else if (doc_) {
            drawSceneContent(sp, vt, QRectF(rect()));
        }
        ++sceneRebuilds_;
        sceneDirty_ = false;
        sceneOffset_ = QPointF(0, 0);   // cache now matches the current view
        scrollResidual_ = QPointF(0, 0);
    }

    QPainter p(this);
    // During a pan the cache is blitted shifted (no per-tick rebuild); 0 otherwise.
    // Fill the whole viewport with the canvas colour first so the strip exposed on the
    // leading edge by the shift shows the drawing background, not the widget's grey.
    if (!sceneOffset_.isNull()) p.fillRect(rect(), bgColor_);
    p.drawPixmap(sceneOffset_, sceneCache_);
    // The editor's overlay - the selection, the grips, the line being drawn, the snap marker - on the
    // sheet as well as on the model, because the sheet is drawn on the same way. Not while a window is
    // being dragged about: that is the canvas's own doing and the editor has no part in it.
    if (doc_ && editor_ &&
        !(layoutMode_ && (viewportPanning_ || viewportMoving_ || viewportDragCorner_ >= 0))) {
        p.setRenderHint(QPainter::Antialiasing, true);
        if (layoutMode_ && activeViewport_ >= 0) {
            // Inside a window: the selection, the grips and the line being drawn are the model's, so they
            // are drawn through the window and kept inside its frame.
            const DrawingLayout* layout = currentLayout();
            if (layout && activeViewport_ < static_cast<int>(layout->viewports.size())) {
                const QRectF rect =
                    viewportScreenRect(layout->viewports[static_cast<size_t>(activeViewport_)], vt);
                p.save();
                p.setClipRect(rect, Qt::IntersectClip);
                editor_->drawOverlay(p, inputTransform());
                p.restore();
            }
        } else {
            editor_->drawOverlay(p, vt);
        }
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
void CanvasWidget::syncEditorScale() {
    if (!editor_) return;
    // Inside a window the model is shown at the window's scale; anywhere else the document's own view is
    // the whole story, and zero says so.
    editor_->setInputScale(layoutMode_ && activeViewport_ >= 0 ? inputTransform().scale() : 0.0);

    // And what there is to snap to that is not an entity. On a sheet that is the sheet's own edge and the
    // frame of every window on it: a plot window, a border, a note is lined up against those far more
    // often than against anything drawn. Inside a window the work is in the model, where the paper's edge
    // means nothing, so the list is emptied.
    std::vector<Bounds> rects;
    if (layoutMode_ && activeViewport_ < 0) {
        if (const DrawingLayout* layout = currentLayout()) {
            Bounds sheet;
            sheet.expand(Vec2{0.0, 0.0});
            sheet.expand(Vec2{layout->paperWidth, layout->paperHeight});
            rects.push_back(sheet);
            for (const LayoutViewport& viewport : layout->viewports) {
                Bounds frame;
                frame.expand(Vec2{viewport.x, viewport.y});
                frame.expand(Vec2{viewport.x + viewport.width, viewport.y + viewport.height});
                rects.push_back(frame);
            }
        }
    }
    editor_->setSnapRectangles(std::move(rects));
}

QPointF CanvasWidget::crosshairScreenPos() const {
    // The snapped point comes from whatever the editor is working in, so it must be put on the screen the
    // same way: inside a window on a layout that is the MODEL through that window. Mapped as paper, the
    // crosshair jumped away from the cursor the moment a snap caught - which is what it looked like.
    if (editor_ && editor_->currentSnap().snapped)
        return inputTransform().toScreen(editor_->currentSnap().point);
    return lastMousePos_;
}

void CanvasWidget::drawCrosshair(QPainter& p) const {
    const QPointF c = crosshairScreenPos();
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
    holdTheDrawingStill();
    QWidget::resizeEvent(e);
}

void CanvasWidget::moveEvent(QMoveEvent* e) {
    holdTheDrawingStill();
    QWidget::moveEvent(e);
}

// A panel on the left made wider takes its room from the canvas: the canvas's left edge moves right, and
// with it everything drawn on it - so widening a panel appeared to drag the whole drawing sideways. The
// view is pulled back by however far the canvas moved INSIDE ITS WINDOW, which leaves the drawing exactly
// where it was on the screen. Inside its window, so that moving the window itself changes nothing.
void CanvasWidget::holdTheDrawingStill() {
    const QWidget* top = window();
    if (!top) return;
    const QPoint now = mapTo(const_cast<QWidget*>(top), QPoint(0, 0));
    if (whereInTheWindow_ == QPoint(-1, -1)) { whereInTheWindow_ = now; return; }
    const QPoint moved = now - whereInTheWindow_;
    whereInTheWindow_ = now;
    if (moved.isNull() || !doc_) return;
    doc_->view().x -= moved.x();
    doc_->view().y -= moved.y();
    invalidateScene();
    emit viewChanged();
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
    if (layoutMode_ && activeViewport_ >= 0) {
        DrawingLayout* layout = currentLayout();
        if (layout && activeViewport_ < static_cast<int>(layout->viewports.size())) {
            LayoutViewport& viewport = layout->viewports[static_cast<size_t>(activeViewport_)];
            const QRectF rect = viewportScreenRect(viewport, transform());
            if (rect.contains(e->position())) {
                const ViewTransform current = viewportTransform(viewport, rect, transform());
                if (viewport.modelScale <= 0.0)
                    viewport.modelScale = current.scale() / transform().scale();
                viewport.modelScale = std::clamp(viewport.modelScale *
                                                     std::pow(1.0015, e->angleDelta().y()),
                                                 0.0001, 10000.0);
                drawingDocument()->setDirty(true);
                syncEditorScale();   // a step of zoom inside the window changes what a pick covers
                invalidateScene();
                e->accept();
                return;
            }
        }
    }
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
    syncEditorScale();
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
    // A layout is a sheet that can be drawn on. A press on it is a press on the sheet - the tools, the
    // selection and the grips all work as they do on the model - EXCEPT when it lands on the frame of a
    // window, or inside a window that has been stepped into. Those two go to the window; everything else
    // falls through to the editor below.
    if (layoutMode_ && e->button() == Qt::LeftButton) {
        DrawingLayout* layout = currentLayout();
        // While a rectangle is being picked - the plot window - the windows on the sheet stand aside. The
        // corner being picked is often ON a window's frame, and taking hold of the window instead would
        // eat the very click that was asked for.
        if (editor_ && editor_->pickingRectangle()) layout = nullptr;
        if (layout) {
            if (activeViewport_ >= 0) {
                // Stepped inside a window: presses in it belong to the MODEL, seen through the window -
                // they fall through to the editor below, which is working in the drawing and is handed
                // the window's own coordinates. What moves the view inside a window is a middle-drag or
                // the wheel, exactly as in model space. A press outside the window steps back out onto
                // the paper, and is then handled as paper.
                if (viewportAt(e->position()) == activeViewport_) {
                    // fall through to the editor
                } else {
                    leaveLayoutViewport();
                    selectedViewports_.clear();
                }
            } else {
                int corner = -1;
                const int grab = viewportGrabAt(e->position(), *layout, &corner);
                if (grab >= 0) {
                    // A frame that is already picked keeps the rest of the picked windows with it, so a
                    // set caught by a zone is carried about together. Any other frame picks just itself.
                    if (!layoutViewportPicked(grab)) selectedViewports_ = {grab};
                    else if (selectedLayoutViewport() != grab) {
                        selectedViewports_.erase(
                            std::remove(selectedViewports_.begin(), selectedViewports_.end(), grab),
                            selectedViewports_.end());
                        selectedViewports_.push_back(grab);   // the one grabbed leads
                    }
                    viewportDragCorner_ = corner;
                    // A corner resizes the window; an edge MOVES it, anywhere on or off the sheet.
                    viewportMoving_ = corner < 0;
                    viewportLastPos_ = e->pos();
                    if (grab < static_cast<int>(layout->viewports.size()))
                        freezeViewportView(layout->viewports[static_cast<size_t>(grab)]);
                    setCursor(viewportMoving_ ? Qt::SizeAllCursor : Qt::CrossCursor);
                    invalidateScene();
                    e->accept();
                    return;
                }
                if (!selectedViewports_.empty()) {   // pressing the paper lets the windows go
                    selectedViewports_.clear();
                    viewportDragCorner_ = -1;
                    viewportMoving_ = false;
                    invalidateScene();
                }
            }
        }
    }
    if (e->button() == Qt::MiddleButton && layoutMode_ && activeViewport_ >= 0) {
        // Inside a window, the middle drag moves what the window looks at - the same gesture as in model
        // space, applied to the view through the glass rather than to the sheet.
        selectedViewports_ = {activeViewport_};
        viewportPanning_ = true;
        viewportLastPos_ = e->pos();
        setCursor(Qt::ClosedHandCursor);
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
        editor_->onPointerPress(inputTransform().toWorld(e->position()), e->button(),
                                e->modifiers());
        e->accept();
    }
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* e) {
    lastMousePos_ = e->pos();
    syncEditorScale();
    if (capturing_) {
        if (captureActive_) { captureCur_ = e->pos(); update(); }
        e->accept();
        return; // no drawing/selection while capturing a print window
    }
    if (layoutMode_ && doc_ && (viewportPanning_ || viewportMoving_ || viewportDragCorner_ >= 0)) {
        DrawingLayout* layout = currentLayout();
        const int leading = selectedLayoutViewport();
        if (layout && leading >= 0 && leading < static_cast<int>(layout->viewports.size())) {
            LayoutViewport& viewport = layout->viewports[static_cast<size_t>(leading)];
            if (viewportPanning_ && activeViewport_ == leading) {
                const QPoint delta = e->pos() - viewportLastPos_;
                viewportLastPos_ = e->pos();
                const QRectF rect = viewportScreenRect(viewport, transform());
                const ViewTransform modelView = viewportTransform(viewport, rect, transform());
                if (viewport.modelScale <= 0.0) {
                    Bounds model;
                    for (const Entity& entity : drawingDocument()->entities())
                        model.expand(entity.bounds());
                    if (model.valid) viewport.modelCenter = model.center();
                    viewport.modelScale = modelView.scale() / transform().scale();
                }
                viewport.modelCenter.x -= delta.x() / modelView.scale();
                viewport.modelCenter.y += delta.y() / modelView.scale();
            } else if (viewportMoving_) {
                // The window is carried about the sheet. NOT clamped to the paper: a window is often
                // parked off the edge while a sheet is being laid out, and a drawing office moves one
                // half off to see what is under it.
                const QPoint delta = e->pos() - viewportLastPos_;
                viewportLastPos_ = e->pos();
                const double scale = transform().scale();
                if (scale > 1e-9)
                    for (const int at : selectedViewports_) {
                        if (at < 0 || at >= static_cast<int>(layout->viewports.size())) continue;
                        LayoutViewport& moved = layout->viewports[static_cast<size_t>(at)];
                        moved.x += delta.x() / scale;
                        moved.y -= delta.y() / scale;
                    }
            } else if (viewportDragCorner_ >= 0) {
                // Only the window's own edges move. What it looks at stays where it is ON THE PAPER, so
                // dragging a corner crops the view instead of rescaling the drawing inside it: the model
                // point that was under a given spot of paper is still under it afterwards.
                const Vec2 point = transform().toWorld(e->position());
                const double left = viewport.x, right = viewport.x + viewport.width;
                const double bottom = viewport.y, top = viewport.y + viewport.height;
                const double wasCx = viewport.x + viewport.width * 0.5;
                const double wasCy = viewport.y + viewport.height * 0.5;
                if (viewportDragCorner_ == 0 || viewportDragCorner_ == 3) {
                    viewport.x = std::min(point.x, right - 10.0);
                    viewport.width = right - viewport.x;
                } else {
                    viewport.width = std::max(10.0, point.x - left);
                }
                if (viewportDragCorner_ == 0 || viewportDragCorner_ == 1) {
                    viewport.height = std::max(10.0, point.y - bottom);
                } else {
                    viewport.y = std::min(point.y, top - 10.0);
                    viewport.height = top - viewport.y;
                }
                if (viewport.modelScale > 1e-9) {
                    // The rectangle's middle has moved by this much of paper; the view is shifted back by
                    // the same amount of model, which is what keeps the drawing still.
                    const double dx = viewport.x + viewport.width * 0.5 - wasCx;
                    const double dy = viewport.y + viewport.height * 0.5 - wasCy;
                    viewport.modelCenter.x += dx / viewport.modelScale;
                    viewport.modelCenter.y += dy / viewport.modelScale;
                }
            }
            drawingDocument()->setDirty(true);
            invalidateScene();
        }
        e->accept();
        return;
    }
    if (panning_ && doc_) {
        const QPoint delta = e->pos() - lastPanPos_;
        lastPanPos_ = e->pos();
        doc_->view().x += delta.x();
        doc_->view().y += delta.y();
        // The paper has moved and nothing on it has changed, so the drawing is NOT rendered again: the
        // pixels the hand moved are handed to the next paint, which shifts what is already drawn and draws
        // only the strip that came into view. See scrollScene(). Held until the paint rather than applied
        // here because a mouse reports its position far more often than a screen can show one, and this way
        // a pan costs one shift per frame however talkative the mouse is.
        pendingScroll_ += delta;
        emit viewChanged();
        // AND NOTHING ELSE. A middle-drag is dragging the paper: nothing is being pointed at, nothing is being
        // picked, and no object snap, grip search or overlay preview means anything until the paper stops moving.
        // Every one of those was being done on every tick of the pan - the whole snap pass over the drawing, on
        // the one gesture where the drawing is guaranteed not to have changed - which is why panning a big sheet
        // felt heavy while moving the cursor over the same sheet did not.
        emit cursorWorldMoved(inputTransform().toWorld(e->position()));
        requestRepaint();
        e->accept();
        return;
    }
    const Vec2 world = inputTransform().toWorld(e->position());
    if (editor_) editor_->onPointerMove(world);
    // Right-button drag: once moved past a small threshold, start (or continue)
    // dragging the selection / object under the press point.
    if (rightDown_ && editor_) {
        if (!rightDragging_ &&
            (e->pos() - rightPressPos_).manhattanLength() > 4) {
            rightDragging_ = editor_->beginRightDrag(inputTransform().toWorld(rightPressPos_));
            if (!rightDragging_) rightDown_ = false; // nothing to drag -> plain menu on release
        }
        if (rightDragging_) editor_->updateRightDrag(world);
    }
    // The flying input field tracks the cursor while it's open (after onPointerMove
    // so it follows the snapped crosshair).
    if (dynInput_ && dynInput_->isVisible()) positionDynInput();
    emit cursorWorldMoved(world);
    requestRepaint();   // the crosshair follows the cursor, at the rate a screen can show it
}

// A REPAINT, AT THE RATE A SCREEN CAN SHOW ONE.
//
// A mouse reports where it is far more often than a monitor can draw - a thousand times a second on some of them
// against sixty on the screen - and every report was asking for a full repaint. Most of those were thrown away
// unseen, and on a drawing heavy enough for a repaint to take a few milliseconds the extra ones are what makes the
// cursor lag behind the hand: the queue fills with work whose result nobody will ever look at.
//
// So repaints are held to one per frame. The trailing one always happens, which is the part that matters: the
// LAST position the mouse reported is the one the cursor has to be drawn at, or it stops just short of where the
// hand is and stays there.
void CanvasWidget::requestRepaint() {
    constexpr int kFrameMs = 8;   // about twice a 60 Hz screen: fast enough not to be seen, slow enough to help
    if (!frameClock_.isValid()) frameClock_.start();
    if (frameClock_.elapsed() >= kFrameMs) {
        frameClock_.restart();
        repaintPending_ = false;
        update();
        return;
    }
    if (repaintPending_) return;   // one already on its way: this position will be drawn by it
    repaintPending_ = true;
    QTimer::singleShot(kFrameMs - static_cast<int>(frameClock_.elapsed()), this, [this] {
        repaintPending_ = false;
        frameClock_.restart();
        update();
    });
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
    syncEditorScale();
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
    // Whichever button was doing it. The middle button is what moves what a window looks at, and
    // letting it go must let the drawing go: keyed to the left button alone, the drawing went on
    // following the cursor after the button was up, which is how it read as stuck to it.
    if (layoutMode_ && (viewportPanning_ || viewportMoving_ || viewportDragCorner_ >= 0) &&
        (e->button() == Qt::LeftButton || e->button() == Qt::MiddleButton)) {
        viewportPanning_ = false;
        viewportMoving_ = false;
        viewportDragCorner_ = -1;
        setCursor(inlineEditing_ ? Qt::ArrowCursor : Qt::BlankCursor);
        invalidateScene();
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
        editor_->onPointerRelease(inputTransform().toWorld(e->position()), e->button());
        e->accept();
    }
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    syncEditorScale();
    if (layoutMode_ && e->button() == Qt::LeftButton) {
        DrawingLayout* layout = currentLayout();
        int corner = -1;
        const int grab = layout ? viewportGrabAt(e->position(), *layout, &corner) : -1;
        const int hit = viewportAt(e->position());
        // Into a window: double-clicking its frame, or its inside once it has been picked out by the
        // frame. A double click anywhere else on the paper is a double click on the paper - it opens the
        // note under the cursor for editing, as it does on the model.
        if (grab >= 0 || hit >= 0) {
            enterLayoutViewport(grab >= 0 ? grab : hit);
            e->accept();
            return;
        }
    }
    if (e->button() == Qt::LeftButton && editor_) {
        editor_->onDoubleClick(inputTransform().toWorld(e->position()));
        e->accept();
    }
}

void CanvasWidget::keyPressEvent(QKeyEvent* e) {
    syncEditorScale();
    // Esc first lets go of a part or harness being placed inside a window. Leaving the viewport would
    // take the user out of the function while the ghost was still in hand, which is exactly the
    // half-cancelled state Esc exists to avoid.
    if (layoutMode_ && activeViewport_ >= 0 && e->key() == Qt::Key_Escape &&
        editor_ && editor_->placementPreviewActive()) {
        editor_->onKey(e->key(), e->modifiers());
        e->accept();
        return;
    }
    // Esc steps back out of a window onto the paper - the way out of anything else here.
    if (layoutMode_ && activeViewport_ >= 0 && e->key() == Qt::Key_Escape) {
        leaveLayoutViewport();
        e->accept();
        return;
    }
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
    if (editor_ && Editor::isTwistedTool(editor_->tool())) {
        if (next) editor_->freezeTwistedPreview();
        // SWALLOWED EITHER WAY, pinned or not.
        //
        // It used to fall through to focus traversal when there was nothing to pin - before the
        // first click, or on Backtab - and traversal moves the keyboard off the canvas onto the
        // ribbon. The drawing then looked frozen, and in every way that matters it was: the run was
        // half drawn and neither Enter nor Esc reached it any more, so it could not be finished and
        // could not be abandoned. The mouse still moved the preview, which made it worse - the
        // thing plainly alive and taking no notice.
        //
        // While one of these tools is armed, Tab belongs to the tool. There is nowhere else on the
        // ribbon a person wants it to go mid-run.
        return true;
    }
    // Tab while placing a break mark: pin the preview so the ribbon's numbers can be tried on it.
    if (editor_ && next && editor_->tool() == ToolId::LengthBreak) {
        editor_->freezeBreakPreview();
        return true;
    }
    // Tab while a label's stroke is down: pin it, so its width and height can be set on the ribbon with the
    // real thing on screen rather than with a preview jumping about under the cursor.
    if (editor_ && next && editor_->labelStrokeStarted()) {
        editor_->freezeLabelPreview();
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
