#pragma once

#include "geometry/Environment3D.h"
#include "harness/Harness3D.h"
#include "render/GlRender3D.h"
#include "render/Render3D.h"

#include <QWidget>

#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QThread;
class QToolButton;
class QWidget;

namespace Edraw_Desktop {

class View3DCanvas;

// THE 3D VIEW OF THE HARNESS: the harness drawn on the sheet - or the one open in the harness wizard - in 3D, its runs
// as tubes and its parts as their 3D models (a box where a part has none). Turned with the left button, moved with the
// right (or the middle), zoomed with the wheel; Fit, and the view from the top, the front, the side or at an angle.
//
// THE ENVIRONMENT: the CAD model the harness is routed in - a chassis, a panel, an enclosure - loaded from a STEP (or
// STL, OBJ) file and shown round it, see-through as far as wanted, cut open by a section plane; a click on it says
// which solid was hit, where, and which way its face looks. Not saved with the drawing.
class View3DPane : public QWidget {
    Q_OBJECT
public:
    explicit View3DPane(QWidget* parent = nullptr);
    ~View3DPane() override;

    // Where the harness comes from: the objects on the sheet, and the wizard's drawing (empty when it is not open).
    void setSources(std::function<std::vector<Entity>()> drawing, std::function<std::vector<Entity>()> wizard);
    // The 3D model file a library part has attached, or nothing.
    void setModelLookup(std::function<QString(const QString& libPart)> modelOf);
    // A connector's pins and gender, for its pinout on its front face.
    void setPinsLookup(std::function<PartPins(const QString& libPart)> pinsOf);
    // Built again from its source, and brought round to show the whole of it.
    void rebuild(bool fit = true);
    // The harness from one place only - the wizard's own view: no choice of where from.
    void setSourceChoice(bool shown);
    // The environment's row of controls shown or not - the wizard's own view has none.
    void setEnvironmentChoice(bool shown);
    // How a part with no 3D model is shown: a box, or a solid made from its 2D drawing.
    void setPartLook(PartLook look);
    // THE BRICKS PICKED: their parts and runs lit in the picture.
    void setHighlight(const QStringList& bricks);
    const Harness3DScene& scene() const { return scene_; }
    View3DCanvas* canvas() const { return canvas_; }

    // THE ENVIRONMENT from a file - read in the background, or at once where `wait` (false when there is nothing in
    // it). Asked for again while one is being read: refused.
    bool loadEnvironment(const QString& path, bool wait = false);
    void clearEnvironment();
    const Environment3D& environment() const { return env_; }
    bool loadingEnvironment() const { return loading_ != nullptr; }

signals:
    // An environment read, or cleared.
    void environmentChanged();

private:
    void takeEnvironment(Environment3D env, std::shared_ptr<RayIndex3D> index);
    void applySection();
    void showPick(const Pick3D& pick);
    void sayStatus();

    std::function<std::vector<Entity>()> drawing_, wizard_;
    std::function<QString(const QString&)> modelOf_;
    std::function<PartPins(const QString&)> pinsOf_;
    Harness3DScene scene_;
    QString sceneSaid_;
    View3DCanvas* canvas_ = nullptr;
    QComboBox* source_ = nullptr;
    QComboBox* look_ = nullptr;
    PartLook partLook_ = PartLook::Revolved;
    QLabel* status_ = nullptr;
    // The environment.
    Environment3D env_;
    std::shared_ptr<RayIndex3D> envIndex_;
    QThread* loading_ = nullptr;
    QString loadingName_;
    QString pickSaid_;
    QWidget* envRow_ = nullptr;
    QToolButton* envLoad_ = nullptr;
    QToolButton* envClear_ = nullptr;
    QCheckBox* envShown_ = nullptr;
    QSlider* envOpacity_ = nullptr;
    QCheckBox* harnessShown_ = nullptr;
    QComboBox* section_ = nullptr;
    QSlider* sectionAt_ = nullptr;
};

// The picture itself, and the mouse on it.
class View3DCanvas : public QWidget {
    Q_OBJECT
public:
    explicit View3DCanvas(QWidget* parent = nullptr);
    void setScene(const Harness3DScene* scene);
    // The whole of what is shown in view: the harness and the environment.
    void fit();
    // Just this box in view.
    void fitBox(const V3& lo, const V3& hi);
    // From the top, the front, the side, or at an angle (isometric).
    void viewFrom(const QString& which);
    void setWireframe(bool on);
    void setHighlight(const QStringList& bricks);
    QStringList highlighted() const { return lit_; }
    Camera3D& camera() { return cam_; }
    // THE ENVIRONMENT: what to draw round the harness, and the tree its rays are cast against.
    void setEnvironment(const Environment3D* env, const RayIndex3D* index);
    void setEnvironmentShown(bool on);
    void setEnvironmentOpacity(double alpha);
    void setHarnessShown(bool on);
    void setSection(const Section3D& section);
    const Section3D& section() const { return section_; }
    // WHAT IS UNDER A POINT OF THE PICTURE: the environment where the ray from the eye through it first meets it -
    // beyond the section, where one cuts it open. Marked in the picture, and said by `picked`.
    Pick3D pickAt(const QPointF& at);
    const Pick3D& lastPick() const { return pick_; }
    void clearPick();
    // Drawn by the graphics card where it can be (the default), or always in software.
    void setUseGpu(bool on);
    // Which drew the last picture: "graphics card" or "software", and why not the card.
    QString rendererSaid() const;
    // The picture as it is now, for the tests.
    QImage render(const QSize& size) const;

signals:
    void picked(const Pick3D& pick);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    std::vector<DrawItem3D> drawItems() const;
    void remark();

    const Harness3DScene* scene_ = nullptr;
    const Environment3D* env_ = nullptr;
    const RayIndex3D* envIndex_ = nullptr;
    bool envShown_ = true;
    double envAlpha_ = 1.0;
    bool harnessShown_ = true;
    Section3D section_;
    Pick3D pick_;
    Mesh3D marker_;
    Camera3D cam_;
    bool wire_ = false;
    QStringList lit_;
    QPoint last_;
    QPoint pressedAt_;
    bool moved_ = false;
    Qt::MouseButton dragging_ = Qt::NoButton;
    bool useGpu_ = true;
    mutable std::unique_ptr<GlRenderer3D> gl_;
    mutable bool lastOnGpu_ = false;
};

} // namespace Edraw_Desktop
