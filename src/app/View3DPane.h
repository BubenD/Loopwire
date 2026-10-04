#pragma once

#include "geometry/Environment3D.h"
#include "harness/Harness3D.h"
#include "harness/Route3D.h"
#include "harness/RouteLink.h"
#include "render/GlRender3D.h"
#include "render/Render3D.h"

#include <QJsonObject>
#include <QWidget>

#include <functional>
#include <memory>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QSlider;
class QTableWidget;
class QThread;
class QToolButton;
class QWidget;

namespace Edraw_Desktop {

class View3DCanvas;

// WHAT A CLICK IN THE 3D VIEW DOES: picks (and selects a node or a bundle), puts a connector on a face, puts a branch
// or a clip down (on a bundle, dividing it), lays a bundle from node to node through the points clicked, or takes a
// node or a bundle away.
enum class RouteTool { Select, Connector, Branch, Clip, Route, Delete };

// THE HARNESS THE ROUTE IS FOR: its name and the wizard's design of it - the one open in the wizard, or the one this
// drawing has placed (or keeps, where it keeps one only). `ok` false: none.
struct RouteHarness {
    QString name;
    BrickDesign design;
    bool ok = false;
};

// THE 3D VIEW OF THE HARNESS: the harness drawn on the sheet - or the one open in the harness wizard - in 3D, its runs
// as tubes and its parts as their 3D models (a box where a part has none). Turned with the left button, moved with the
// right (or the middle), zoomed with the wheel; Fit, and the view from the top, the front, the side or at an angle.
//
// THE ENVIRONMENT: the CAD model the harness is routed in - a chassis, a panel, an enclosure - loaded from a STEP (or
// STL, OBJ) file and shown round it, see-through as far as wanted, cut open by a section plane; a click on it says
// which solid was hit, where, and which way its face looks.
//
// THE ROUTE: the harness laid through the environment by hand - connectors put on its faces, bundles laid from one to
// the next over the faces clicked, branches and clips put down - each bundle's length read off its path, in the table
// beside the picture. The environment's file and the route are saved with the drawing.
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
    // The environment's and the route's controls shown or not - the wizard's own view has none.
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

    // WHERE THE ROUTING IS KEPT: read from, and written back to, the drawing (Document::routing3d); and the drawing's
    // folder, where an environment moved along with the drawing is looked for.
    void setRoutingStore(std::function<QJsonObject()> load, std::function<void(const QJsonObject&)> save,
                         std::function<QString()> drawingDir = {});
    // Read again from the drawing - another drawing made current, or one opened. The environment it names is read
    // in the background, or at once where `wait`.
    void reloadRouting(bool wait = false);
    const Route3D& route() const { return route_; }
    void setRouteTool(RouteTool tool);
    // The last change to the route taken back.
    void undoRoute();
    // The file of the environment the drawing names, found or not.
    const QString& environmentFile() const { return envRef_; }

    // THE HARNESS THE ROUTE IS PUT FOR - its parts to put in 3D - and where the lengths the route gives it go.
    void setHarnessLink(std::function<RouteHarness()> harness, std::function<void(const QString& name, const BrickDesign&)> apply);
    // The parts of that harness again, each put in 3D or not.
    void refreshParts();
    // THE PART THE NEXT CONNECTOR CLICKED IS PUT FOR, by its brick id - empty: a node of the route's own.
    void setPartToPlace(const QString& brick);
    // The node selected in the picture put for a part (empty: for none).
    bool linkSelectedNode(const QString& brick);
    // WHAT THE ROUTE WOULD MAKE THE HARNESS'S LENGTHS, and made so - asked first where `ask`. False when nothing changed.
    RouteLengths proposedLengths() const;
    bool applyLengths(bool ask = true);

signals:
    // An environment read, or cleared.
    void environmentChanged();
    // The route changed (and written back to the drawing).
    void routeChanged();

private:
    void takeEnvironment(Environment3D env, std::shared_ptr<RayIndex3D> index);
    void applySection();
    void showPick(const Pick3D& pick);
    void sayStatus();
    void saveRouting();
    void refreshRoute();
    void beforeRouteChange();

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
    QString envRef_;
    bool quietLoad_ = false;
    QWidget* envRow_ = nullptr;
    QToolButton* envLoad_ = nullptr;
    QToolButton* envClear_ = nullptr;
    QCheckBox* envShown_ = nullptr;
    QSlider* envOpacity_ = nullptr;
    QCheckBox* harnessShown_ = nullptr;
    QComboBox* section_ = nullptr;
    QSlider* sectionAt_ = nullptr;
    // The route.
    Route3D route_;
    std::vector<QJsonObject> history_;
    std::function<QJsonObject()> loadRouting_;
    std::function<void(const QJsonObject&)> saveRouting_;
    std::function<QString()> drawingDir_;
    QWidget* routeRow_ = nullptr;
    QButtonGroup* tools_ = nullptr;
    QDoubleSpinBox* diameter_ = nullptr;
    QDoubleSpinBox* bend_ = nullptr;
    QDoubleSpinBox* lead_ = nullptr;
    QToolButton* undo_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* totals_ = nullptr;
    QWidget* side_ = nullptr;
    // The harness the route is for.
    std::function<RouteHarness()> harness_;
    std::function<void(const QString&, const BrickDesign&)> applyHarness_;
    QLabel* harnessSaid_ = nullptr;
    QListWidget* parts_ = nullptr;
    QToolButton* link_ = nullptr;
    QToolButton* unlink_ = nullptr;
    QToolButton* lengths_ = nullptr;
    bool filling_ = false;
};

// The picture itself, and the mouse on it.
class View3DCanvas : public QWidget {
    Q_OBJECT
public:
    explicit View3DCanvas(QWidget* parent = nullptr);
    void setScene(const Harness3DScene* scene);
    // The whole of what is shown in view: the harness, the environment and the route.
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

    // THE ROUTE it shows and edits in place - none in the wizard's view.
    void setRoute(Route3D* route);
    void setRouteTool(RouteTool tool);
    RouteTool routeTool() const { return tool_; }
    // The route changed from outside: drawn again.
    void routeRedraw();
    QString selectedSegment() const { return selSegment_; }
    QString selectedNode() const { return selNode_; }
    void selectSegment(const QString& id);
    // THE PART THE NEXT CONNECTOR CLICKED IS PUT FOR: its brick id, its designator, and the kind of node it makes.
    void setPartToPlace(const QString& brick, const QString& designator, RouteNodeKind kind);
    QString partToPlace() const { return placeBrick_; }
    // A bundle being laid: the node it starts from, and the points clicked so far.
    bool layingRoute() const { return !layFrom_.isEmpty(); }
    // WHERE A POINT OF THE PICTURE LANDS: on the environment, or - where there is none there - on the ground (Z = 0).
    bool surfaceAt(const QPointF& at, V3& point, V3& normal);
    // Where a point in space is drawn in the picture; false behind the eye.
    bool toScreen(const V3& p, QPointF& at) const;

signals:
    void picked(const Pick3D& pick);
    void routeAboutToChange();
    void routeChanged();
    void selectionChanged(const QString& segment);
    void undoRequested();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    std::vector<DrawItem3D> drawItems() const;
    void remark();
    void routeClick(const QPointF& at);
    void finishLaying(const QString& endNode);
    void cancelLaying();
    QString nodeAt(const QPointF& at) const;
    // The bundle under a point of the picture: where on it (on its face), and how many of its laid points come first.
    QString segmentAt(const QPointF& at, V3* point = nullptr, V3* normal = nullptr, int* afterVia = nullptr) const;
    V3 nodeShown(const RouteNode3D& n) const;
    void buildRouteMeshes() const;

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
    // The route.
    Route3D* route_ = nullptr;
    RouteTool tool_ = RouteTool::Select;
    QString selSegment_, selNode_;
    QString layFrom_;
    QString placeBrick_, placePart_;
    RouteNodeKind placeKind_ = RouteNodeKind::Connector;
    std::vector<RouteVia3D> layVia_;
    bool hoverOk_ = false;
    V3 hover_, hoverNormal_;
    struct Drawn {
        Mesh3D mesh;
        QColor color;
    };
    mutable std::vector<Drawn> routeMeshes_;
    mutable bool routeDirty_ = true;
    Mesh3D layMesh_;
    // The release that ends a double-click is not another click.
    bool swallowRelease_ = false;
};

} // namespace Edraw_Desktop
