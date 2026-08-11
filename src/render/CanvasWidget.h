#pragma once

#include "model/Document.h"
#include "render/EntityRenderer.h"

#include <QColor>
#include <QPixmap>
#include <QPoint>
#include <QStringList>
#include <QWidget>

#include <algorithm>
#include <functional>

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
    Vec2 worldFromGlobal(const QPoint& globalPos) const {
        return transform().toWorld(QPointF(mapFromGlobal(globalPos)));
    }

protected:
    void resizeEvent(QResizeEvent* e) override;  // keep the crosshair overlay sized
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

    // Begin an interactive rubber-band capture of a rectangular world region (used by
    // "Export PDF (window)"). The next left-drag defines the window; on release `onDone`
    // is called with its two opposite world corners. Esc cancels (onDone not called).
    void beginWindowCapture(std::function<void(Vec2, Vec2)> onDone);

private:
    ViewTransform transform() const;
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
    int crosshairPct_ = 15;             // crosshair arm length (% of viewport)
    QColor crosshairColor_{"#cdd6e4"};
    int pickboxPx_ = 3;                 // half-size of the central pick box
    QColor bgColor_{"#1e2832"};         // canvas (model space) background (dark slate)
    QColor gridMinor_{"#2b3743"};   // 5% quieter than the #2c3845 it was
    bool layoutMode_ = false;
    int layoutIndex_ = 0;
    int selectedViewport_ = -1;
    int activeViewport_ = -1;
    int viewportDragCorner_ = -1;
    bool viewportPanning_ = false;
    QPoint viewportLastPos_;
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
