#pragma once

#include "model/Document.h"
#include "render/EntityRenderer.h"

#include <QPixmap>
#include <QPoint>
#include <QWidget>

class QLineEdit;

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

    double gridSize() const { return gridSize_; }
    void setGridSize(double g);
    void setGridVisible(bool v);
    bool gridVisible() const { return gridVisible_; }

signals:
    void cursorWorldMoved(Vec2 world);
    // Emitted when the user commits text in the floating dynamic-input field
    // (value, coordinate, or command) — the host routes it like the command bar.
    void commandSubmitted(const QString& text);

protected:
    void paintEvent(QPaintEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override; // dynamic-input field keys

    // Mark the cached static scene (grid + entities) stale so it is re-rendered
    // on the next paint. Cursor-move previews skip this for a lag-free overlay.
    void invalidateScene();

private:
    ViewTransform transform() const;
    void drawGrid(QPainter& p, const ViewTransform& vt) const;
    void drawOriginMarker(QPainter& p, const ViewTransform& vt) const;
    void showDynInput(const QString& firstChar); // pop the field near the cursor
    void hideDynInput();

    QLineEdit* dynInput_ = nullptr; // floating "dynamic input" box near the cursor
    QPoint lastMousePos_;           // canvas-local cursor position

    Document* doc_ = nullptr;
    Editor* editor_ = nullptr;
    double gridSize_ = 10.0;
    bool gridVisible_ = true;

    QPixmap sceneCache_;       // grid + entities, rebuilt only when they change
    bool sceneDirty_ = true;

    bool panning_ = false;
    QPoint lastPanPos_;

    static constexpr double kMaxViewScale = 4000.0;
    static constexpr double kMinViewScale = 0.0005;
};

} // namespace Edraw_Desktop
