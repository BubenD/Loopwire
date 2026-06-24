#pragma once

#include "model/Document.h"
#include "render/EntityRenderer.h"

#include <QColor>
#include <QPixmap>
#include <QPoint>
#include <QStringList>
#include <QWidget>

#include <algorithm>

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

    // The editor receives input and paints interaction overlays.
    void setEditor(Editor* editor);

    // Zoom to fit all entities (matches Edraw fitView()).
    void fitView();

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
        const int lum = (c.red() * 299 + c.green() * 587 + c.blue() * 114) / 1000;
        const QColor target = lum < 128 ? QColor(255, 255, 255) : QColor(0, 0, 0);
        auto mix = [&](double t) {
            return QColor(int(c.red() + (target.red() - c.red()) * t + 0.5),
                          int(c.green() + (target.green() - c.green()) * t + 0.5),
                          int(c.blue() + (target.blue() - c.blue()) * t + 0.5));
        };
        gridMinor_ = mix(0.08);
        gridMajor_ = mix(0.20);
        invalidateScene();
    }
    void setGridColors(const QColor& minor, const QColor& major) {
        gridMinor_ = minor; gridMajor_ = major; invalidateScene();
    }
    QColor gridMinorColor() const { return gridMinor_; }
    QColor gridMajorColor() const { return gridMajor_; }

    // Command words for the dynamic-input autocomplete (suggests/filters as you type).
    void setCommandWords(const QStringList& words);

signals:
    void cursorWorldMoved(Vec2 world);
    // Emitted when the user commits text in the floating dynamic-input field
    // (value, coordinate, or command) — the host routes it like the command bar.
    void commandSubmitted(const QString& text);
    // Right-click on the canvas: the host shows a context menu at this global pos.
    void contextMenuRequested(const QPoint& globalPos);
    // Bare-canvas click while the on-canvas text editor is open -> commit it.
    void inlineCommit();
    // The view changed (pan/zoom) — overlays anchored to world coords re-position.
    void viewChanged();

public:
    // True while the host's on-canvas text editor is active (so a canvas click
    // commits it instead of starting a new action).
    void setInlineEditing(bool on) { inlineEditing_ = on; }

    // Paint the crosshair onto an arbitrary painter. Called by the transparent
    // top-overlay child so the crosshair always renders ABOVE every other child
    // widget (the on-canvas text editor, the flying input field, ...).
    void renderCrosshair(QPainter& p) const {
        if (cursorInside_ && !panning_) drawCrosshair(p);
    }

    // Restack the crosshair overlay on top of every sibling. The host calls this
    // after it raise()s its own overlays (e.g. the inline text editor toolbar), so
    // the cursor is never hidden behind them.
    void raiseCrosshairOverlay() {
        if (crosshairTop_) crosshairTop_->raise();
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

    // Mark the cached static scene (grid + entities) stale so it is re-rendered
    // on the next paint. Cursor-move previews skip this for a lag-free overlay.
    void invalidateScene();

private:
    ViewTransform transform() const;
    void drawGrid(QPainter& p, const ViewTransform& vt) const;
    void drawOriginMarker(QPainter& p, const ViewTransform& vt) const;
    void drawCrosshair(QPainter& p) const; // AutoCAD-style full crosshair + pickbox
    // Pop the field near the cursor. `placeholder` (when non-empty) labels it for
    // an inline value prompt (fillet radius / gap); otherwise the active reference
    // field label is used.
    void showDynInput(const QString& firstChar, const QString& placeholder = QString());
    void positionDynInput(); // place the flying field just off the (snapped) cursor
    void hideDynInput();

    QLineEdit* dynInput_ = nullptr; // floating "dynamic input" box near the cursor
    QWidget* crosshairTop_ = nullptr; // transparent overlay that paints the crosshair on top
    QCompleter* completer_ = nullptr; // command autocomplete for the dynamic input
    QPoint lastMousePos_;           // canvas-local cursor position
    bool cursorInside_ = false;     // mouse over the canvas (draw the crosshair)

    Document* doc_ = nullptr;
    Editor* editor_ = nullptr;
    double gridSize_ = 10.0;
    bool gridVisible_ = true;

    // Display options.
    int crosshairPct_ = 15;             // crosshair arm length (% of viewport)
    QColor crosshairColor_{"#cdd6e4"};
    int pickboxPx_ = 3;                 // half-size of the central pick box
    QColor bgColor_{"#1e2832"};         // canvas (model space) background (dark slate)
    QColor gridMinor_{"#2c3845"};
    QColor gridMajor_{"#3a4a58"};

    QPixmap sceneCache_;       // grid + entities, rebuilt only when they change
    bool sceneDirty_ = true;

    bool panning_ = false;
    QPoint lastPanPos_;
    bool inlineEditing_ = false; // host's on-canvas text editor is open

    static constexpr double kMaxViewScale = 4000.0;
    static constexpr double kMinViewScale = 0.0005;
};

} // namespace Edraw_Desktop
