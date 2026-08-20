#pragma once

#include <QElapsedTimer>
#include "model/Document.h"
#include "render/EntityRenderer.h"

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QPoint>
#include <QStringList>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <vector>

class QLineEdit;
class QCompleter;

namespace Edraw_Desktop {

class Editor;

// The drawing canvas: a custom-painted widget that owns the view transform and
// handles pan (middle/right drag), zoom (wheel), and fit. Input and interaction
// overlays are delegated to the Editor.
class CanvasWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanvasWidget(QWidget* parent = nullptr);

    // The canvas draws whatever document it is pointed at; ownership stays with
    // the caller (MainWindow / DocumentManager).
    void setDocument(Document* doc);
    Document* document() const { return doc_; }

    // In layout mode there are TWO documents in play, and they are different things.
    //
    // `document()` is what is being EDITED and drawn as content: in model space the drawing itself, on a
    // layout the paper space of that layout - so the editor, the tools, the selection and the undo stack
    // all work on a layout exactly as they work on the model, because to them it IS just a document.
    //
    // `drawingDocument()` is the DRAWING: the one that owns the layouts, their windows and the model
    // geometry those windows look at. On the model they are the same document; on a layout they are not.
    void setDrawingDocument(Document* doc);
    Document* drawingDocument() const { return drawingDoc_ ? drawingDoc_ : doc_; }

    void setLayoutMode(bool on);
    bool layoutMode() const { return layoutMode_; }
    void setLayoutIndex(int index);
    int layoutIndex() const { return layoutIndex_; }
    void addLayoutViewport();
    // Take a window off the sheet. -1 means the one picked out.
    void removeLayoutViewport(int index = -1);
    // The window last picked out, and whether a given one is picked. Several can be, because a selection
    // zone drawn on the sheet takes every window it catches.
    int selectedLayoutViewport() const {
        return selectedViewports_.empty() ? -1 : selectedViewports_.back();
    }
    const std::vector<int>& selectedLayoutViewports() const { return selectedViewports_; }
    bool layoutViewportPicked(int index) const {
        return std::find(selectedViewports_.begin(), selectedViewports_.end(), index) !=
               selectedViewports_.end();
    }
    // Take every window a selection zone caught: wholly inside it for the blue window zone, touched at
    // all for the green crossing zone. Shift (subtract) lets them go again.
    void selectLayoutWindowsIn(const std::vector<Vec2>& region, bool crossing, bool subtract);
    // The window under a point on the canvas, or -1. Public because the canvas menu asks it: right-clicking a
    // window has to offer something about THAT window, picked out first or not.
    int layoutViewportAt(const QPointF& point) const { return viewportAt(point); }
    int activeLayoutViewport() const { return activeViewport_; }
    // Step into a window (so what is drawn goes into the model, seen through it) or back out onto the
    // paper. Stepping in and out is what changes which document the editor is working in, so it is
    // announced with viewportEntered().
    void enterLayoutViewport(int index);
    void leaveLayoutViewport();
    // Where the editor's coordinates come from: the paper, or - inside a window that has been stepped
    // into - the model as that window sees it.
    ViewTransform inputTransform() const;
    // Where the crosshair is: the cursor, or the point a snap has caught - put on the screen the same way
    // the editor's own world is, so that inside a window on a layout it stays where the cursor is instead
    // of jumping off to wherever that model point would fall if it were paper.
    QPointF crosshairScreenPos() const;
    // Tell the editor how big its world is on the screen, so a pick or a snap covers the same few pixels
    // wherever it is made. Called wherever the transform in use can have changed.
    void syncEditorScale();

    // Where a window's rectangle falls, and the view it shows, given how the paper is being mapped onto
    // the device. Static and public because PRINTING needs exactly the same two answers as the screen: a
    // sheet must come off the printer laid out as it looks here, and two copies of this arithmetic would
    // be two chances to differ.
    static QRectF layoutViewportRect(const LayoutViewport& viewport, const ViewTransform& paper);
    static ViewTransform layoutViewportView(const LayoutViewport& viewport, const QRectF& rect,
                                            const ViewTransform& paper, const Bounds& modelExtents);
    void setBottomOverlay(QWidget* overlay);

    // The editor receives input and paints interaction overlays.
    void setEditor(Editor* editor);

    // Zoom to fit all entities (matches Edraw fitView()).
    void fitView();
    // Center the view on a world point (keeping the current zoom, but at least
    // `minScale` if given) — used to jump to a validation discrepancy.
    void centerOn(const Vec2& world, double minScale = 0.0);

    // World<->screen for overlays (e.g. the on-canvas text editor).
    QPointF mapToScreen(const Vec2& world) const { return transform().toScreen(world); }
    double viewScale() const { return transform().scale(); }

    double gridSize() const { return gridSize_; }
    void setGridSize(double g);
    void setGridVisible(bool v);
    bool gridVisible() const { return gridVisible_; }

    // ---- Display options (Options dialog) ----
    int crosshairSize() const { return crosshairPct_; }   // 1..100 (% of viewport)
    void setCrosshairSize(int pct) { crosshairPct_ = std::clamp(pct, 1, 100); update(); }
    QColor crosshairColor() const { return crosshairColor_; }
    void setCrosshairColor(const QColor& c) { crosshairColor_ = c; update(); }
    int pickboxSize() const { return pickboxPx_; }
    void setPickboxSize(int px) { pickboxPx_ = std::clamp(px, 1, 30); update(); }
    QColor backgroundColor() const { return bgColor_; }
    // Setting the background also re-derives the grid colors so the minor and
    // major grid lines stay visible (lighter than a dark bg, darker than a light
    // bg), the secondary grid a touch stronger.
    void setBackgroundColor(const QColor& c) {
        bgColor_ = c;
        EntityRenderer::setBackgroundColor(layoutMode_ ? QColor(Qt::white) : c);
        const int lum = (c.red() * 299 + c.green() * 587 + c.blue() * 114) / 1000;
        const QColor target = lum < 128 ? QColor(255, 255, 255) : QColor(0, 0, 0);
        auto mix = [&](double t) {
            return QColor(int(c.red() + (target.red() - c.red()) * t + 0.5),
                          int(c.green() + (target.green() - c.green()) * t + 0.5),
                          int(c.blue() + (target.blue() - c.blue()) * t + 0.5));
        };
        // Five per cent quieter than it was (0.08 -> 0.076, 0.20 -> 0.19): the grid is there to
        // judge distance by, not to compete with the drawing on top of it.
        gridMinor_ = mix(0.076);
        gridMajor_ = mix(0.19);
        invalidateScene();
    }
    void setGridColors(const QColor& minor, const QColor& major) {
        gridMinor_ = minor; gridMajor_ = major; invalidateScene();
    }
    QColor gridMinorColor() const { return gridMinor_; }
    QColor gridMajorColor() const { return gridMajor_; }

    // Command words for the dynamic-input autocomplete (suggests/filters as you type).
    // Command autocomplete: `words` are the inserted completions (aliases/names),
    // `labels` the matching preview text shown in the popup ("tr — Trim …").
    void setCommandWords(const QStringList& words, const QStringList& labels);

signals:
    void cursorWorldMoved(Vec2 world);
    // Emitted when the user commits text in the floating dynamic-input field
    // (value, coordinate, or command) — the host routes it like the command bar.
    void commandSubmitted(const QString& text);
    // Right-click on the canvas: the host shows a context menu at this global pos.
    void contextMenuRequested(const QPoint& globalPos);
    // Right-button DRAG dropped: the host offers Move / Copy / Cancel at this pos.
    void rightDragDropped(const QPoint& globalPos);
    // Bare-canvas click while the on-canvas text editor is open -> commit it.
    void inlineCommit();
    // Stepped into a window on a layout (true), or back out onto the paper (false). The host points the
    // editor at the model or at the sheet accordingly: inside a window, what is drawn is the harness.
    void viewportEntered(bool inside);
    // The view changed (pan/zoom) — overlays anchored to world coords re-position.
    void viewChanged();
    // Tab pressed while placing a connector: pause placement and let the host focus
    // the Connector build ribbon so its preferences can be edited (preview stays live).
    void connectorEditRequested();

public:
    // True while the host's on-canvas text editor is active (so a canvas click
    // commits it instead of starting a new action). While editing text we hide
    // the AutoCAD crosshair and restore the normal OS cursor.
    void setInlineEditing(bool on) {
        inlineEditing_ = on;
        setCursor(on ? Qt::ArrowCursor : Qt::BlankCursor);
        update();
    }

    // Paint the crosshair onto an arbitrary painter. Called by the transparent
    // top-overlay child so the crosshair always renders ABOVE every other child
    // widget (the on-canvas text editor, the flying input field, ...).
    void renderCrosshair(QPainter& p) const {
        if (cursorInside_ && !panning_ && !inlineEditing_) drawCrosshair(p);
    }

    // Restack the crosshair overlay on top of every sibling. The host calls this
    // after it raise()s its own overlays (e.g. the inline text editor toolbar), so
    // the cursor is never hidden behind them.
    void raiseCrosshairOverlay() {
        if (crosshairTop_) crosshairTop_->raise();
    }

    // World point under a GLOBAL screen position (e.g. a context-menu request point).
    // Inside a window on a layout that is the MODEL's world point, seen through that window - so the
    // context menu and the coordinate readout answer about the thing being worked on.
    Vec2 worldFromGlobal(const QPoint& globalPos) const {
        return inputTransform().toWorld(QPointF(mapFromGlobal(globalPos)));
    }

protected:
    void resizeEvent(QResizeEvent* e) override;  // keep the crosshair overlay sized
    void moveEvent(QMoveEvent* e) override;      // a panel widened beside it must not shift the drawing
    void childEvent(QChildEvent* e) override;    // keep the crosshair overlay on top
    void paintEvent(QPaintEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void enterEvent(QEnterEvent* e) override; // show the drawn crosshair
    void leaveEvent(QEvent* e) override;      // hide it
    bool eventFilter(QObject* obj, QEvent* e) override; // dynamic-input field keys
    // Keep Tab/Backtab for cycling the dynamic-input fields (Length<->Angle, ...)
    // instead of letting Qt move keyboard focus off the canvas.
    bool focusNextPrevChild(bool next) override;

public:
    // Mark the cached static scene (grid + entities) stale so it is re-rendered on the next paint.
    // Cursor-move previews skip this for a lag-free overlay. Public because anything that changes what
    // the drawing LOOKS like - a layer switched off, a layer recoloured - has to say so: a plain
    // update() repaints the cache as it stands and shows no change at all.
    void invalidateScene();
    // A repaint at the rate a screen can show one. A mouse reports where it is far more often than a monitor can
    // draw, and every report was asking for a full repaint - most of them thrown away unseen, and on a heavy
    // drawing the extra ones are what makes the cursor lag behind the hand.
    void requestRepaint();
    // The frame clock and whether one is already on its way. See requestRepaint.
    QElapsedTimer frameClock_;
    bool repaintPending_ = false;
    // How many times the whole drawing has been re-rendered. For a test that panning does not.
    int sceneRebuildsForTest() const { return sceneRebuilds_; }
    // What the drawing currently looks like, without the overlay. For a test that a pan leaves the
    // same picture as re-rendering would have.
    QImage sceneImageForTest() const { return sceneCache_.toImage(); }

    // Begin an interactive rubber-band capture of a rectangular world region (used by
    // "Export PDF (window)"). The next left-drag defines the window; on release `onDone`
    // is called with its two opposite world corners. Esc cancels (onDone not called).
    void beginWindowCapture(std::function<void(Vec2, Vec2)> onDone);

    // How the canvas is mapping the space it is showing onto itself. Public because printing "what is on
    // screen" has to ask that question: the view is the canvas's, and nobody else can answer it.
    ViewTransform transform() const;

private:
    DrawingLayout* currentLayout();
    const DrawingLayout* currentLayout() const;
    QRectF viewportScreenRect(const LayoutViewport& viewport, const ViewTransform& paper) const;
    ViewTransform viewportTransform(const LayoutViewport& viewport, const QRectF& rect,
                                    const ViewTransform& paper) const;
    int viewportAt(const QPointF& point) const;
    // The window whose FRAME is under the point (edge or corner handle), and which corner if any.
    int viewportGrabAt(const QPointF& point, const DrawingLayout& layout, int* corner) const;
    void drawLayoutScene(QPainter& painter, const ViewTransform& paper);
    void drawGrid(QPainter& p, const ViewTransform& vt) const;
    // The static drawing - grid, origin, entities - into `sp` for the part of the widget `area` covers.
    // Both the full re-render and a pan's edge strip go through here, so a strip is drawn by exactly the
    // code that would have drawn it as part of the whole.
    void drawSceneContent(QPainter& sp, const ViewTransform& vt, const QRectF& area);
    // A pan: shift what is already drawn by the pixels the hand moved and draw only the strip that came
    // into view. False if there is nothing worth keeping and the whole thing has to be drawn again.
    bool scrollScene(const QPoint& delta);
    void drawOriginMarker(QPainter& p, const ViewTransform& vt) const;
    void drawCrosshair(QPainter& p) const; // AutoCAD-style full crosshair + pickbox
    // Pop the field near the cursor. `placeholder` (when non-empty) labels it for
    // an inline value prompt (fillet radius / gap); otherwise the active reference
    // field label is used.
    void showDynInput(const QString& firstChar, const QString& placeholder = QString());
    void positionDynInput(); // place the flying field just off the (snapped) cursor
    void hideDynInput();
    void positionBottomOverlay();

    QLineEdit* dynInput_ = nullptr; // floating "dynamic input" box near the cursor
    QWidget* crosshairTop_ = nullptr; // transparent overlay that paints the crosshair on top
    QWidget* bottomOverlay_ = nullptr;
    QCompleter* completer_ = nullptr; // command autocomplete for the dynamic input
    QPoint lastMousePos_;           // canvas-local cursor position
    bool cursorInside_ = false;     // mouse over the canvas (draw the crosshair)

    Document* doc_ = nullptr;
    Document* drawingDoc_ = nullptr;   // the drawing that owns the layouts, when a layout is being edited
    Editor* editor_ = nullptr;
    double gridSize_ = 10.0;
    bool gridVisible_ = true;

    // Display options.
    // Where the canvas sits INSIDE its own window. A panel on the left growing wider moves the canvas
    // right; the drawing is pulled back by the same amount so it stays where it is on the screen.
    void holdTheDrawingStill();
    QPoint whereInTheWindow_{-1, -1};
    int crosshairPct_ = 15;             // crosshair arm length (% of viewport)
    QColor crosshairColor_{"#cdd6e4"};
    int pickboxPx_ = 3;                 // half-size of the central pick box
    QColor bgColor_{"#1e2832"};         // canvas (model space) background (dark slate)
    QColor gridMinor_{"#2b3743"};   // 5% quieter than the #2c3845 it was
    bool layoutMode_ = false;
    int layoutIndex_ = 0;
    std::vector<int> selectedViewports_;   // the windows picked out on the sheet, in the order picked
    int activeViewport_ = -1;
    int viewportDragCorner_ = -1;
    bool viewportPanning_ = false;
    bool viewportMoving_ = false;   // dragging a window's edge: the window moves on the paper
    QPoint viewportLastPos_;
    // Pin the view a window shows before its rectangle is changed, so resizing the window crops the
    // drawing rather than rescaling it.
    void freezeViewportView(LayoutViewport& viewport);
    QColor gridMajor_{"#394856"};

    QPixmap sceneCache_;       // grid + entities, rebuilt only when they change
    bool sceneDirty_ = true;
    // A fingerprint of the layer table as the cache was built from it. Layers are changed from several
    // places - the panel, the properties, a command, a file being loaded - and any of them switching a
    // layer off has to show immediately, so the check is here rather than at every one of those sites.
    quint64 layerStamp_ = 0;
    quint64 layerFingerprint() const;
    // While panning we blit the cached scene shifted by this offset instead of
    // rebuilding it every mouse tick; reset to 0 whenever the cache is rebuilt.
    QPointF sceneOffset_;
    // A pan shifts the cached drawing instead of re-rendering it. The scratch is the other half of the
    // shift (swapped with the cache, so no pixmap is allocated per frame); the pixels the hand has moved
    // and the cache has not yet been told about are held here until the next paint, so a mouse reporting
    // a thousand times a second still costs one shift per frame; and the residual is what was left over
    // after rounding the shift to whole SCREEN pixels - carried on so it cannot build up into a drift.
    QPixmap scrollScratch_;
    QPoint pendingScroll_;
    QPointF scrollResidual_;
    int sceneRebuilds_ = 0;

    bool panning_ = false;
    QPoint lastPanPos_;
    // Rubber-band window capture (Export PDF window). captureActive_ becomes true once
    // the drag starts; the two corners are in canvas-local pixels.
    bool capturing_ = false;
    bool captureActive_ = false;
    QPoint captureStart_;
    QPoint captureCur_;
    std::function<void(Vec2, Vec2)> captureCb_;
    // Right-button drag (move/copy via drop menu) vs plain right-click (menu).
    bool rightDown_ = false;
    bool rightDragging_ = false;
    QPoint rightPressPos_;
    bool inlineEditing_ = false; // host's on-canvas text editor is open

    static constexpr double kMaxViewScale = 4000.0;
    static constexpr double kMinViewScale = 0.0005;
};

} // namespace Edraw_Desktop
