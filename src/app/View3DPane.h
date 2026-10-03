#pragma once

#include "harness/Harness3D.h"
#include "render/Render3D.h"

#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;

namespace Edraw_Desktop {

class View3DCanvas;

// THE 3D VIEW OF THE HARNESS: the harness drawn on the sheet - or the one open in the harness wizard - in 3D, its runs
// as tubes and its parts as their 3D models (a box where a part has none). Turned with the left button, moved with the
// right (or the middle), zoomed with the wheel; Fit, and the view from the top, the front, the side or at an angle.
class View3DPane : public QWidget {
    Q_OBJECT
public:
    explicit View3DPane(QWidget* parent = nullptr);

    // Where the harness comes from: the objects on the sheet, and the wizard's drawing (empty when it is not open).
    void setSources(std::function<std::vector<Entity>()> drawing, std::function<std::vector<Entity>()> wizard);
    // The 3D model file a library part has attached, or nothing.
    void setModelLookup(std::function<QString(const QString& libPart)> modelOf);
    // Built again from its source, and brought round to show the whole of it.
    void rebuild(bool fit = true);
    // The harness from one place only - the wizard's own view: no choice of where from.
    void setSourceChoice(bool shown);
    // THE BRICKS PICKED: their parts and runs lit in the picture.
    void setHighlight(const QStringList& bricks);
    const Harness3DScene& scene() const { return scene_; }
    View3DCanvas* canvas() const { return canvas_; }

private:
    std::function<std::vector<Entity>()> drawing_, wizard_;
    std::function<QString(const QString&)> modelOf_;
    Harness3DScene scene_;
    View3DCanvas* canvas_ = nullptr;
    QComboBox* source_ = nullptr;
    QLabel* status_ = nullptr;
};

// The picture itself, and the mouse on it.
class View3DCanvas : public QWidget {
    Q_OBJECT
public:
    explicit View3DCanvas(QWidget* parent = nullptr);
    void setScene(const Harness3DScene* scene);
    void fit();
    // From the top, the front, the side, or at an angle (isometric).
    void viewFrom(const QString& which);
    void setWireframe(bool on);
    void setHighlight(const QStringList& bricks);
    QStringList highlighted() const { return lit_; }
    Camera3D& camera() { return cam_; }
    // The picture as it is now, for the tests.
    QImage render(const QSize& size) const;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    const Harness3DScene* scene_ = nullptr;
    Camera3D cam_;
    bool wire_ = false;
    QStringList lit_;
    QPoint last_;
    Qt::MouseButton dragging_ = Qt::NoButton;
};

} // namespace Edraw_Desktop
