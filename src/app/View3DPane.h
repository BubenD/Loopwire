#pragma once

#include "geometry/Environment3D.h"
#include "harness/Harness3D.h"
#include "harness/Route3D.h"
#include "harness/RouteLink.h"
#include "render/GlRender3D.h"
#include "render/Render3D.h"

#include <QJsonObject>
#include <QPointer>
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
class QDialog;
class QProgressBar;
class QSpinBox;
class QTableWidget;
class QThread;
class QTimer;
class QCloseEvent;
class QToolButton;
class QWidget;

namespace Edraw_Desktop {

class View3DCanvas;
class View3DHelpDialog;
class NavWheel;

// WHAT A CLICK IN THE 3D VIEW DOES: picks (and selects a node or a bundle), puts a connector on a face, puts a branch
// or a clip down (on a bundle, dividing it), lays a bundle from node to node through the points clicked, or takes a
// node or a bundle away.
// MOVE takes a node somewhere else on the model, its bundles following from where they were laid; STRETCH pulls a node -
// an end, to make the harness shorter or longer - and the points each of its bundles was laid through go with it, the
// nearer the more, up to the node at the bundle's other end: a clip or a branch there holds the harness where it is.
enum class RouteTool { Select, Connector, Branch, Clip, Route, Delete, Move, Stretch, Measure };

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
    // Taken away - a change Undo takes back, where remember is set.
    void clearEnvironment(bool remember = true);
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
    // An environment chosen from a file dialog and read.
    void chooseEnvironment();
    RouteTool routeTool() const;
    // HOW TO WORK IN IT: the help window, its recordings and its steps.
    void showHelp();
    // THE EXAMPLE the help works through, loaded as the environment: a plate, two boxes and a rib.
    bool loadExample();
    static QString exampleResource() { return QStringLiteral(":/help3d/example.obj"); }
    // THE WHOLE SCREEN: the view, with every row and the side panel, filling it - and back.
    void setFullScreen(bool on);
    // THE MODEL'S SOLIDS, each shown or hidden - a list of them to tick.
    void showPartsList();
    // THE PANE'S OWN ROWS shown, or not - the 3D Preview tab has all of them on its ribbon.
    void setRowsShown(bool on);
    // THE VIEW LOCKED (and the button and the wheel saying so).
    void setViewLocked(bool on);
    // BUILT AGAIN A MOMENT AFTER the drawing last changed - what is put on the sheet shows in 3D without a Refresh.
    void scheduleRebuild();
    bool fullScreenView() const { return fullScreen_; }
    // THE WORK PLANE a click lands on: -1 the model's faces (the ground where there is none), 0/1/2 a plane across X,
    // Y or Z, at `at` along that axis.
    void setWorkPlane(int axis, double at);
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

protected:
    void closeEvent(QCloseEvent* e) override;

signals:
    // An environment read, or cleared.
    void environmentChanged();
    // The route changed (and written back to the drawing).
    void routeChanged();
    // The route tool chosen - here, or from outside.
    void routeToolChanged(RouteTool tool);
    // The view locked, or not.
    void viewLockChanged(bool locked);

private:
    void takeEnvironment(Environment3D env, std::shared_ptr<RayIndex3D> index);
    void applyIcons();
    void applySection();
    void showPick(const Pick3D& pick);
    void sayStatus();
    void saveRouting();
    void refreshRoute();
    void beforeRouteChange();
    // The route told what it cannot pass through: the model's solids, those shown.
    void armSolids();
    // THE LENGTHS EITHER SIDE OF THE NODE SELECTED - to the connector each way, through clips and branches.
    QString nodeLengths() const;
    // The turn, tilt and roll boxes brought into step with the camera.
    void syncCamera();
    // The connector selected - how it points, and how it is turned - into its boxes; and the boxes into it.
    void syncConnector();
    void applyAim(const V3& dir, double spinDeg);

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
    QProgressBar* envProgress_ = nullptr;   // shown while a model is read
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
    QWidget* bundleRow_ = nullptr;
    QToolButton* grow_ = nullptr;   // Add loose
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
    QPointer<View3DHelpDialog> help_;
    // The whole screen. Not saved.
    bool fullScreen_ = false;
    QTimer* rebuildSoon_ = nullptr;
    QWidget* topRow_ = nullptr;
    QPointer<QDialog> partsList_;
    bool envChoice_ = true;    // the environment's and the route's rows wanted (none in the wizard's view)
    bool rowsShown_ = true;    // the pane's own rows shown (none while the 3D Preview tab has them on its ribbon)
    QToolButton* lockBtn_ = nullptr;
    // THE WHEEL, in the picture's corner, and how far a click on it turns the view. Not saved.
    NavWheel* wheel_ = nullptr;
    QCheckBox* wheelShown_ = nullptr;
    int wheelStep_ = 15;
    void wheelOptions(const QPoint& at);
    void placeWheel();
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    Qt::WindowFlags normalFlags_;
    QToolButton* fullBtn_ = nullptr;
    // The camera's three turns, in degrees, all the way round.
    QWidget* viewBits_ = nullptr;   // the view's three turns, on the work plane's row
    QSpinBox* turn_ = nullptr;
    QSpinBox* tilt_ = nullptr;
    QSpinBox* roll_ = nullptr;
    // The work plane. Not saved.
    QWidget* planeRow_ = nullptr;
    QComboBox* plane_ = nullptr;
    QDoubleSpinBox* planeAt_ = nullptr;
    // The connector selected.
    QWidget* connBox_ = nullptr;
    QLabel* connSaid_ = nullptr;
    QComboBox* aim_ = nullptr;
    QSpinBox* aimTurn_ = nullptr;
    QSpinBox* aimTilt_ = nullptr;
    QSpinBox* spin_ = nullptr;
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
    // THE WORK PLANE: a click lands on it rather than on the model - a plane across X, Y or Z (`axis` 0, 1, 2) at `at`
    // along it, drawn see-through so it is seen where it is; -1, none.
    void setWorkPlane(int axis, double at);
    // THE VIEW LOCKED: the mouse no longer turns, moves or zooms it - a drag moves what is under it instead: a node, a
    // corner of a bundle, or a bundle (a corner put where it was taken hold of), across the picture as far as the
    // pointer goes, at the depth it is.
    void setViewLocked(bool on);
    bool viewLocked() const { return viewLocked_; }
    // THE VIEW TURNED by so many degrees round the upright, up and over, and round the way it looks; brought nearer
    // (under 1) or further (over 1).
    void nudge(double turnDeg, double tiltDeg, double rollDeg);
    // THE OTHER SIDE: looked at from straight opposite where it is looked at from now, through the same middle.
    void flip();
    // A SOLID OF THE MODEL HIDDEN, or shown again: not drawn, and a click and a bundle go through it.
    void setEnvPartHidden(int part, bool hidden);
    void setAllEnvPartsHidden(bool hidden);
    bool envPartHidden(int part) const;
    // THE RULER: its two ends, set by two clicks with the Measure tool; and taken away.
    bool measured() const { return measureStage_ == 2; }
    V3 measureFrom() const { return measureA_; }
    V3 measureTo() const { return measureB_; }
    void clearMeasure();
    // What the ruler says: the distance straight, and along and across each axis and plane.
    static QString measureSaid(const V3& a, const V3& b);
    void zoomBy(double factor);
    int workPlaneAxis() const { return planeAxis_; }
    double workPlaneAt() const { return planeAt_; }

signals:
    void picked(const Pick3D& pick);
    void routeAboutToChange();
    void routeChanged();
    void selectionChanged(const QString& segment);
    void undoRequested();
    // The route being moved under the pointer - not yet a change to keep.
    void routeMoving();
    // Something to tell the user.
    void message(const QString& text);
    // The camera turned, moved or zoomed.
    void cameraChanged();
    // Esc with nothing being laid.
    void escapePressed();

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
        int texture = Texture3D::None;
    };
    mutable std::vector<Drawn> routeMeshes_;
    mutable bool routeDirty_ = true;
    Mesh3D layMesh_;
    // The release that ends a double-click is not another click.
    bool swallowRelease_ = false;
    // The solids of the model hidden, and the ruler. Not saved.
    std::vector<char> envHidden_;
    int measureStage_ = 0;      // 0 none, 1 its first end set, 2 both
    V3 measureA_, measureB_;
    mutable Mesh3D measureMesh_;
    mutable bool measureDirty_ = true;
    // The section's see-through plane, over only the solids it cuts. Not saved.
    mutable Mesh3D sectionMesh_;
    mutable bool sectionDirty_ = true;
    bool skipPart(int part, const V3& point) const;
    // Locked, and what a drag is moving. Not saved.
    bool viewLocked_ = false;
    // A NODE TAKEN HOLD OF WITH MOVE OR STRETCH: where it was. Stretched, it stays where it is: its bundles are pulled
    // tight through it, as far as the pointer goes, and what comes through is cut off.
    bool beginNodeDrag(const QPointF& at, bool stretch);
    void tightenKept(double by);
    double stretchCut_ = 0.0;
    // PULLED, ITS LENGTH KEPT: from the point taken hold of out to the node at each bundle's far end, the points in
    // between (by their places in the bundle's laid points), the far node where it holds, and each step's length -
    // the cable laid as a cable is, pulled along behind the point and no longer than it was.
    struct Kept {
        QString segment;
        std::vector<int> via;   // outward from the point held
        V3 anchor;
        std::vector<double> step;   // from the point held to via[0], ..., to the anchor
    };
    std::vector<Kept> kept_;
    // EACH BUNDLE'S LENGTH AS DRAWN when it was taken hold of - with its bends and what it is laid over - and how much
    // longer or shorter than they were its steps are laid, to keep it so.
    struct KeptLength {
        QString segment;
        double length = 0.0;
        double steps = 0.0;
        double scale = 1.0;
    };
    std::vector<KeptLength> keptLen_;
    bool keepLength() const;
    void beginKeepLength();
    void pullKept(V3 target);
    void layKept(const Kept& k, const V3& start, double scale);
    bool grow_ = false;
public:
    // ADD LOOSE: on, the point pulled (a corner of a cable, a node moved) goes alone - the cable is made longer there,
    // the rest of it left where it is; off (as it starts), the bundles keep their lengths and are pulled along behind it.
    void setGrowLength(bool on) { grow_ = on; }
    bool growLength() const { return grow_; }
private:
    V3 dragFrom_;
    int dragKind_ = 0;        // 0 nothing, 1 a node, 2 a corner of a bundle, 3 a bundle (a corner to be put in it),
                              // 4 a node stretched, 5 a node moved over the model
    QString dragId_;
    int dragVia_ = -1;
    RouteVia3D dragNew_;
    double dragPerPixel_ = 0.0;
    bool dragStarted_ = false;
    bool beginItemDrag(const QPointF& at);
    // The work plane. Not saved.
    int planeAxis_ = -1;
    double planeAt_ = 0.0;
    mutable Mesh3D planeMesh_;
    mutable bool planeDirty_ = true;
};

} // namespace Edraw_Desktop
