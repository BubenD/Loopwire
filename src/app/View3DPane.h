#pragma once

#include "app/NavWheel.h"
#include "geometry/Environment3D.h"
#include "harness/Harness3D.h"
#include "harness/Route3D.h"
#include "harness/RouteGravity.h"
#include "harness/RouteKeepOut.h"
#include "harness/RouteLink.h"
#include "render/GlRender3D.h"
#include "render/Render3D.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QPointer>
#include <QSet>
#include <QVariantAnimation>
#include <QPointer>
#include <QWidget>

#include <functional>
#include <map>
#include <memory>

class QButtonGroup;
class QScrollArea;
class QTreeWidget;
class QVBoxLayout;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QMenu;
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

class QSplitter;
class QBoxLayout;

namespace Loopwire {

class View3DCanvas;
class View3DHelpDialog;

// WHAT A CLICK IN THE 3D VIEW DOES: picks (and selects a node or a bundle), puts a connector on a face, puts a branch
// or a clip down (on a bundle, dividing it), lays a bundle from node to node through the points clicked, or takes a
// node or a bundle away.
// MOVE takes a node somewhere else on the model, its bundles following from where they were laid; STRETCH pulls a node -
// an end, to make the harness shorter or longer - and the points each of its bundles was laid through go with it, the
// nearer the more, up to the node at the bundle's other end: a clip or a branch there holds the harness where it is.
enum class RouteTool { Select, Connector, Branch, Clip, Route, Delete, Move, Stretch, Measure, Pinpoints };

// THE HARNESS THE ROUTE IS FOR: its name and the wizard's design of it - the one open in the wizard, or the one this
// drawing has placed (or keeps, where it keeps one only). `ok` false: none.
struct RouteHarness {
    QString name;
    BrickDesign design;
    bool ok = false;
    bool freestyle = false;
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
    // Each object of the sheet's colour as its part has it, by entity id (see partColoursFromBom).
    void setColourLookup(std::function<QHash<QString, QColor>()> coloursOf) { coloursOf_ = std::move(coloursOf); }
    // Built again from its source, and brought round to show the whole of it.
    void rebuild(bool fit = true);
    // The harness from one place only - the wizard's own view: no choice of where from.
    void setSourceChoice(bool shown);
    // The environment's and the route's controls shown or not - the wizard's own view has none.
    void setEnvironmentChoice(bool shown);
    // How a part with no 3D model is shown: a box, or a solid made from its 2D drawing.
    void setPartLook(PartLook look);
    NavWheel* navigationWheel() const { return wheel_; }
    bool navigationWheelShown() const;
    void setNavigationWheelShown(bool shown);
    int wheelTurnStep() const { return wheelStep_; }
    void setWheelTurnStep(int degrees);
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
    void transformDrawingRoute(const std::function<Vec2(const Vec2&)>& map);
    const Route3D& route() const { return route_; }
    void setRouteTool(RouteTool tool);
    // The last change to the route taken back.
    void undoRoute();
    // Whether there is a change to take back, or one to put back (the window's Undo and Redo, while the 3D Preview is open).
    bool canUndoRoute() const { return !history_.empty(); }
    bool canRedoRoute() const { return !future_.empty(); }
    // TAKEN BACK, PUT BACK AGAIN: what Undo took back, in turn (Ctrl+Y); none once anything else is changed.
    void redoRoute();
    bool canRedo() const { return !future_.empty(); }
    // LAID ON THE FACES UNDER IT: the bundle selected (every bundle, where none is) dropped the way gravity pulls onto the
    // face under each point of it - onto a wall beside it where there is nothing under it near - and held there.
    // How many bundles were laid.
    int attachToSurface();
    // A BUNDLE OF SO MANY WIRES, each so thick: the bundle given (none: the route's own, for every bundle that gives none);
    // nought wires - a plain bundle again.
    void setWires(const QString& seg, int wires, double wireD);
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
    void showPartsPanel();
    void showPropertiesPanel();
    void setSystemDesignLinked(bool on);
    bool systemDesignLinked() const { return systemLinked_; }
    bool addSystemDrawing(const QString& path, bool routed, QString* error = nullptr);
    bool removeSystemDrawing(int row);
    QJsonArray systemDrawings() const { return systemFeeds_; }
    void chooseSystemDrawings();
    // THE PANE'S OWN ROWS shown, or not - the 3D Preview tab has all of them on its ribbon.
    void setRowsShown(bool on);
    // AT THE SIDE OF THE WINDOW: the picture alone, with a button to open it below (dockBelowRequested); below, a button
    // back to the side it came from (dockSideRequested), shown while `sideDocked` says it came from one.
    void setCompact(bool on);
    // ITS OPTIONS IN A PANEL OF THEIR OWN: its rows and its route table moved into `host` (a column; its layout a QVBoxLayout),
    // the picture alone left here - and put back with nullptr.
    void setOptionsHost(QWidget* host);
    bool optionsOutside() const { return optionsHost_ != nullptr; }
    bool compact() const { return compact_; }
    void setSideDocked(bool fromSide) {
        sideDocked_ = fromSide;
        applyLayout();
    }
    // BELOW THE DRAWING (or floating): buttons to put it at the left or the right of the window (dockToSideRequested).
    void setDockedBelow(bool below) {
        dockedBelow_ = below;
        applyLayout();
    }
    // THE ROUTE AND HOW IT IS SHOWN written to the drawing now - before the drawing is saved.
    void storeNow() { saveRouting(); }
    // THE HARNESS PUT IN AS A ROUTE: its parts and its cables between them, where it is drawn in 3D - to be moved into the
    // model. How many cables were laid.
    int routeFromHarness(bool fixedLengths = false);
    bool resetHarnessPosition();
    // THE CABLE SELECTED made so long along its path - its ends where they are; as near as it will go. False where none.
    bool setSegmentLength(const QString& seg, double mm);
    // A cable's wires' colours: all one, or each its own (`colours` as far as given).
    void setWireColours(const QString& seg, bool same, const std::vector<QColor>& colours);
    // WHAT IS SAID UNDER THE WHEEL: the thing the pointer is on.
    QString hoverText() const;
    // The solids and parts in view listed again, each ticked as shown.
    void refreshSolids();
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
    bool harnessRouteActive() const { return harnessRouteActive_; }
    void exportPlaneDrawings();

protected:
    void closeEvent(QCloseEvent* e) override;

signals:
    void saveDrawingRequested();
    void routingTargetChanged(bool system);
    // An environment read, or cleared.
    void environmentChanged();
    // The route changed (and written back to the drawing).
    void routeChanged();
    // The route tool chosen - here, or from outside.
    void routeToolChanged(RouteTool tool);
    void harnessRouteStateChanged(bool active);
    void dockBelowRequested();
    void dockSideRequested();
    void dockToSideRequested(bool left);
    // A MODEL BEING READ: how far (a hundredth), what it is doing, and whether it still is.
    void loadProgress(int percent, const QString& said, bool running);
    // The view locked, or not.
    void viewLockChanged(bool locked);

private:
    void takeEnvironment(Environment3D env, std::shared_ptr<RayIndex3D> index);
    // THE NODE SELECTED changed - one step to undo, everything shown again.
    void editSelectedNode(const std::function<void(RouteNode3D&)>& change);
    void syncBend();
    void setBackgroundMode(int mode, const QColor& colour);
public:
    // A BUNDLE'S COLOUR; MORE LENGTH for it, its ends where they are.
    void setSegmentColour(const QString& seg, const QColor& c);
    bool addSegmentLength(const QString& seg, double mm);
    // THE FORM of the next connector put; how the ruler measures (0 point to point, 1 surface to surface).
    void setPlaceForm(const QString& form);
    void setMeasureMode(int mode);
    int measureMode() const;
    // THE DISPLAY OPTIONS - background and shading - into a menu (the wheel's, the ribbon's).
    void fillDisplayMenu(QMenu* menu);
    // MORE LENGTH for the bundle selected - asked how much.
    void addLengthToSelected();
private:
    void syncMove();
    void updateWheel();
    void buildMoveWheel();
public:
    // THE NODE SELECTED moved by so much, turned round a world axis, coloured.
    void moveSelectedNode(const V3& by);
    void rotateSelectedNode(int axis, double degrees);
    void setSelectedNodeColor(const QColor& c);
    bool wheelMoving() const { return wheelMoving_; }
    // A BUNDLE STARTED FROM A CONNECTOR, its cable out of its front or its back.
    void startBranchFrom(const QString& node, bool back);
    // A connector's cable out of its back, or its front.
    void setConnectorBack(const QString& node, bool back);
    // How see-through each solid (or harness part) is - into a menu.
    void fillOpacityMenu(QMenu* menu, int envPart, const QString& brick);
private:
    void applyIcons();
    void applySection();
    void showPick(const Pick3D& pick);
    void sayStatus();
    void fitRows();
    void applyLayout();
    void partsShownChanged();
    QString envPartName(int part) const;
    QStringList harnessBricks() const;
    QString harnessPartName(const QString& brick) const;
    void saveRouting();
    void setHarnessRouteActive(bool active);
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
    std::function<QHash<QString, QColor>()> coloursOf_;
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
    std::vector<QJsonObject> future_;   // what Undo took back, the last on top. Not saved.
    void restoreRoute(const QJsonObject& state);
    QJsonObject routeState() const;
    std::function<QJsonObject()> loadRouting_;
    std::function<void(const QJsonObject&)> saveRouting_;
    std::function<QString()> drawingDir_;
    QWidget* routeRow_ = nullptr;
    QWidget* editRow_ = nullptr;
    QWidget* bundleRow_ = nullptr;
    // THE CABLE SELECTED: its own tension, chosen here (the route's where it gives none).
    QComboBox* cableTension_ = nullptr;
    void syncCableTension();
    // HOW TAUT THE BUNDLES ARE PUT IN (the route's), GRAVITY shown on them, and which way it pulls.
    QComboBox* tension_ = nullptr;
    QToolButton* gravity_ = nullptr;
    QComboBox* down_ = nullptr;
    QString gravitySaid_;
    void syncTension();
public:
    // THE ROUTE HUNG UNDER GRAVITY - each bundle by the tension it is put in with - and shown where it falls, while
    // Gravity is on; taken away when it is off.
    void runGravity();
    const std::vector<CableSag3D>& sags() const;
    // KEEP-OUTS: levels and zones no cable may go through - added (at the middle of the model, or where said), listed
    // to be set (Keep-outs...), shown or hidden. A cable through one that blocks is refused; through one that alerts,
    // warned of.
    QString addKeepOutLevel(int axis, double at, bool block);
    QString addKeepOutLevel(int axis);
    QString addKeepOutZone(const V3& lo, const V3& hi, bool block);
    QString addKeepOutZone();
    void showKeepOuts();
    void setKeepOutsShown(bool on);
    std::vector<KeepOutHit> keepOutHitsNow() const { return keepOutHits(route_); }
private:
    QToolButton* keepShown_ = nullptr;
    QString keepSaid_;
    QPointer<QDialog> keepDialog_;
    void refreshKeepOuts();
    void fillKeepOutTable();
    // Where a new keep-out goes: the model's middle along that axis (or the route's, or nought), its extent.
    bool sceneBounds(V3& lo, V3& hi) const;
    QButtonGroup* tools_ = nullptr;
    QCheckBox* selectMove_ = nullptr;
    bool harnessRouteActive_ = false;
    QDoubleSpinBox* diameter_ = nullptr;
    QToolButton* diameterAuto_ = nullptr;
    QSpinBox* wires_ = nullptr;
    QDoubleSpinBox* wireD_ = nullptr;
    QDoubleSpinBox* bend_ = nullptr;
    QDoubleSpinBox* lead_ = nullptr;
    QToolButton* undo_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* totals_ = nullptr;
    QWidget* side_ = nullptr;
    QSplitter* split_ = nullptr;   // the picture, and the side panels either side of it
    QWidget* optionsHost_ = nullptr;
    // THE OPTIONS PANEL: each control taken out of its row - where it was, to be put back - into a section of its own.
    struct OptionHome {
        QPointer<QWidget> widget;
        QPointer<QBoxLayout> row;
        int index = 0;
        QSizePolicy policy;
        int style = -1;   // a tool button's own way of showing itself
    };
    std::vector<OptionHome> optionHomes_;
    QList<QPair<QPointer<QWidget>, QPointer<QWidget>>> optionSections_;   // each section, and the row it stands for
    QPointer<QWidget> optionsPanel_;
    void buildOptionsPanel(QVBoxLayout* column);
    void takeDownOptionsPanel();
    // The harness the route is for.
    std::function<RouteHarness()> harness_;
    RouteHarness currentRouteHarness() const;
    QJsonObject initialHarnessRoute_;
    QCheckBox* fixedHarness_ = nullptr;
    QCheckBox* keepConnectors_ = nullptr;
    QToolButton* resetHarness_ = nullptr;
    std::function<void(const QString&, const BrickDesign&)> applyHarness_;
    QLabel* harnessSaid_ = nullptr;
    QComboBox* partNames_ = nullptr;
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
    QScrollArea* rowsArea_ = nullptr;   // the rows, scrolled across where the pane is narrower than they are
    QWidget* rowsBox_ = nullptr;
    QLayout* rowsLay_ = nullptr;
    QWidget* solidsPanel_ = nullptr;          // what is in view, on the left (the 3D Preview tab)
    QToolButton* panelsButton_ = nullptr;
    QToolButton* routePanelButton_ = nullptr;
    QToolButton* partsPin_ = nullptr;
    QToolButton* routePin_ = nullptr;
    QTimer* panelsFoldTimer_ = nullptr;
    bool partsPanelOpen_ = true, routePanelOpen_ = true;
    bool systemLinked_ = false;
    QJsonArray systemFeeds_;
    QTableWidget* systemFeedTable_ = nullptr;
    QWidget* systemFeedBox_ = nullptr;
    std::vector<Entity> systemEntities() const;
    void refreshSystemFeeds();
    bool compact_ = false;                    // at the side of the window: the picture alone
    bool sideDocked_ = false;                 // below, having come from the side
    bool dockedBelow_ = false;                // below the drawing, or floating
    QToolButton* toLeft_ = nullptr;
    QToolButton* toRight_ = nullptr;
    QToolButton* sideRefresh_ = nullptr;
    // THE CABLE SELECTED: its own length, diameter, wires and their colours, tension and colour.
    QWidget* cableBox_ = nullptr;
    QLabel* cableSaid_ = nullptr;
    QLabel* cableLength_ = nullptr;
    QDoubleSpinBox* cableBodyDia_ = nullptr;
    QComboBox* cableLabels_ = nullptr;
    QComboBox* labelFlex_ = nullptr;
    QSlider* labelStiffness_ = nullptr;
    QCheckBox* labelStiffnessAll_ = nullptr;
    void editLabelStiffness(const std::function<void(RouteLabel3D&)>& change);
    QLabel* labelSize_ = nullptr;
    QToolButton* labelColor_ = nullptr;
    QToolButton* labelTextColor_ = nullptr;
    QDoubleSpinBox* cableDia_ = nullptr;
    QToolButton* cableDiaAuto_ = nullptr;
    QSpinBox* cableWires_ = nullptr;
    QDoubleSpinBox* cableWireD_ = nullptr;
    QComboBox* cableWireLook_ = nullptr;
    void syncCable();
    void editSegment(const QString& seg, const std::function<void(RouteSegment3D&)>& change);
    void chooseWireColours(const QString& seg);
    // UNDER THE WHEEL: what the pointer is on.
    QLabel* hoverSaid_ = nullptr;
    QTimer* partHoverTimer_ = nullptr;
    QString pendingHoverText_;
    int pendingHoverPart_ = -1;
    QString pendingHoverBrick_, pendingHoverNode_;
    // HOW IT WAS SHOWN, saved with the route: the view, gravity, what is hidden or see-through, the section, the rulers -
    // what needs the model put back once it is read.
    QJsonObject viewState() const;
    void applyView(const QJsonObject& view);
    QJsonObject pendingView_;
    QToolButton* toBottom_ = nullptr;
    // The connector's form and sizes; the node being moved - where, the step, its colour and turns; the wheel's two sets.
    QComboBox* connForm_ = nullptr;
    QComboBox* connBack_ = nullptr;
    QDoubleSpinBox* connSize_[3] = {};
    QToolButton* connAuto_[3] = {};
    QToolButton* bendAuto_ = nullptr;
    QWidget* moveBox_ = nullptr;
    QLabel* moveSaid_ = nullptr;
    QDoubleSpinBox* moveAt_[3] = {};
    QDoubleSpinBox* moveStep_ = nullptr;
    bool fillingMove_ = false;
    std::vector<NavWheel::Item> viewWheel_;
    bool wheelMoving_ = false;
    QTreeWidget* solidsTree_ = nullptr;
    bool fillingSolids_ = false;
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
    void viewFrom(const QString& which, bool animate = false);
    // Orthographic editable edges, in millimetres: 0 XY, 1 XZ, 2 YZ, 3 current camera orientation.
    std::vector<Entity> projectedDrawing(int plane, bool includeEnvironment = true,
                                         const std::function<bool(int)>& progress = {}) const;
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
    bool geometryBounds(V3& lo, V3& hi) const;
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
    void setOriginalConnectors(const QJsonObject& initial, bool keep);
    void setPartNamesMode(int mode);
    void setPartNameHover(int part, const QString& brick, const QString& node);
    void clearPartHover();
    bool keepsOriginalConnectors() const { return keepOriginalConnectors_; }
    void setRouteTool(RouteTool tool);
    RouteTool routeTool() const { return tool_; }
    void addStretchPinpoint(const V3& point);
    bool stretchByPinpoints();
    const std::vector<V3>& stretchPinpoints() const { return stretchPinpoints_; }
    void setSelectAndMove(bool enabled) { selectAndMove_ = enabled; }
    bool selectAndMove() const { return selectAndMove_; }
    void setJointsShown(bool shown) { jointsShown_ = shown; update(); }
    bool jointsShown() const { return jointsShown_; }
    // 0: camera plane, 1: XY, 2: XZ, 3: YZ.
    void setDragPlane(int plane) { dragPlane_ = std::clamp(plane, 0, 3); update(); }
    int dragPlane() const { return dragPlane_; }
    // The route changed from outside: drawn again.
    void routeRedraw();
    QString selectedSegment() const { return selSegment_; }
    QString selectedNode() const { return selNode_; }
    QString selectedCoordinatesText() const;
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
    void nudge(double turnDeg, double tiltDeg, double rollDeg, bool animate = false);
    void turnTo(double yaw, double pitch, double roll);
    // THE OTHER SIDE: looked at from straight opposite where it is looked at from now, through the same middle.
    void flip();
    // A SOLID OF THE MODEL HIDDEN, or shown again: not drawn, and a click and a bundle go through it.
    void setEnvPartHidden(int part, bool hidden);
    // HARNESS PARTS HIDDEN, by the brick each is; and every solid and part shown again.
    void setBrickHidden(const QString& brick, bool hidden);
    bool brickHidden(const QString& brick) const { return hiddenBricks_.contains(brick); }
    void showAllParts();
    // Being turned round (Flip), still.
    bool turning() const;
    void setAllEnvPartsHidden(bool hidden);
    bool envPartHidden(int part) const;
    // THE RULERS: each by two clicks with the Measure tool - point to point, or (surface to surface) square from the
    // first face to the second - kept in the picture until each is selected and deleted, or all are taken away. The
    // last one's two ends; the one being made given up.
    struct Measure3D {
        V3 a, b;
        bool faces = false;
        // SURFACE TO SURFACE: the two faces measured between, to light them.
        Mesh3D faceA, faceB;
        double radius = 0.0;
    };
    bool measured() const { return !measures_.empty(); }
    V3 measureFrom() const { return measures_.empty() ? V3{} : measures_.back().a; }
    V3 measureTo() const { return measures_.empty() ? V3{} : measures_.back().b; }
    const std::vector<Measure3D>& measures() const { return measures_; }
    int measureCount() const { return static_cast<int>(measures_.size()); }
    void clearMeasure();
    void clearMeasures();
    // GRAVITY'S PREVIEW: each bundle as it hangs, drawn faint over it, and each place it falls from marked with how far.
    void setSags(std::vector<CableSag3D> sags);
    // THE KEEP-OUTS SHOWN (each faint, red where it blocks, orange where it alerts), each cable through one marked, and
    // the alert along the top of the picture.
    void setKeepOuts(std::vector<KeepOutHit> hits, const QString& alert);
    void setKeepOutsShown(bool on);
    bool keepOutsShown() const { return keepShown_; }
    const QString& keepOutAlert() const { return keepAlert_; }
    const std::vector<CableSag3D>& sags() const { return sags_; }
    void removeMeasure(int index);
    int selectedMeasure() const { return selMeasure_; }
    void selectMeasure(int index);
    // The rulers put back as they were saved.
    void setMeasures(const std::vector<Measure3D>& measures) {
        measures_ = measures;
        selMeasure_ = -1;
        measureDirty_ = true;
        update();
    }
    // WHAT IS UNDER A POINT OF THE PICTURE past the route: a solid of the model (its index) or a part of the harness (its
    // brick), whichever is nearer; -1 and empty for neither.
    void whatIsAt(const QPointF& at, int& part, QString& brick) const;
    // A NODE TAKEN HOLD OF to drag - pressed on something standing for it (a part of the harness drawn).
    bool holding() const { return dragKind_ != 0; }
    QString heldNode() const { return dragKind_ != 0 ? dragId_ : QString(); }
    bool takeHoldOfNode(const QString& node, const QPointF& at) {
        if (!beginNodeDrag(at, false, node)) return false;
        setCursor(Qt::ClosedHandCursor);
        return true;
    }
    void setMeasureFaces(bool on) { measureFaces_ = on; }
    bool measureFaces() const { return measureFaces_; }
    void setMeasureRadius(bool on) { measureRadius_ = on; clearMeasure(); }
    bool measureRadius() const { return measureRadius_; }
    // THE FACE OF A SOLID round a point of it: the triangles it is meshed in that run on from the one there without a
    // corner between - a flat face, or a round one - lifted a little off it, to be lit.
    Mesh3D faceAround(int part, const V3& point, const V3& normal) const;
    // EACH SOLID'S OWN OPACITY (0: as the model's), and each harness part's. Not saved.
    void setEnvPartOpacity(int part, double alpha);
    double envPartOpacity(int part) const;
    void setBrickOpacity(const QString& brick, double alpha);
    double brickOpacity(const QString& brick) const;
    // A BUNDLE STARTED from a node - the Route tool in hand.
    void beginLaying(const QString& node);
    QString layingFrom() const { return layFrom_; }
    // A NODE OR A BUNDLE TAKEN AWAY - a node with its bundles: one change, to undo.
    void removeRouteItem(const QString& node, const QString& segment);
    // THE NEXT CONNECTOR PUT: its form, size, how it lies and its colour (the template); frozen where it would go with
    // Tab, to change them first.
    RouteNode3D& placeTemplate() { return placeTemplate_; }
    void setPlaceForm(const QString& form);
    bool ghostFrozen() const { return ghostFrozen_; }
    bool ghostShown() const { return ghostOn_; }
    void setGhostFrozen(bool on);
    // SAME AS: the next connector clicked copied to this one.
    void startSameAs(const QString& node);
    bool sameAsPending() const { return !sameAs_.isEmpty(); }
    // HEATSHRINK SLEEVES drawn at branches - on the routed cable and on each leg.
    int branchSleeves() const {
        if (routeDirty_) buildRouteMeshes();
        return branchSleeves_;
    }
    // HOW THE PICTURE IS SHOWN behind what is in it: 0 dark, 1 light, 2 a dark gradient, 3 a light gradient, 4 a colour.
    void setBackground(int mode, const QColor& colour = QColor());
    int backgroundMode() const { return bgMode_; }
    // What the ruler says: the distance straight, and along and across each axis and plane.
    static QString measureSaid(const V3& a, const V3& b);
    void zoomBy(double factor);
    // The view as it stands: "Top", "Front", "Back", "Left", "Right", "Bottom", "Iso" - or "Custom".
    QString viewName() const;
    int workPlaneAxis() const { return planeAxis_; }
    double workPlaneAt() const { return planeAt_; }

signals:
    void picked(const Pick3D& pick);
    void routeAboutToChange();
    void routeChanged();
    void selectionChanged(const QString& segment);
    void undoRequested();
    void redoRequested();
    // The route being moved under the pointer - not yet a change to keep.
    void routeMoving();
    // A RIGHT CLICK on a solid of the model (its index, or -1) or a part of the harness (its brick, or empty).
    void partMenuAsked(int envPart, const QString& brick, const QPoint& globalPos);
    // A RIGHT CLICK on the route: a node or a bundle (or neither), and a ruler (or -1).
    void routeMenuAsked(const QString& node, const QString& segment, int measure, const QPoint& globalPos);
    // A connector being put frozen with Tab - or let go.
    void ghostFrozenChanged(bool frozen);
    // THE POINTER OVER SOMETHING: a node, a cable, a solid of the model or a part of the harness (none: all empty, -1).
    void hovered(int envPart, const QString& brick, const QString& node, const QString& segment);
    // A PART OF THE HARNESS DRAWN pressed to move it (Move, or the view locked) - or clicked to select it.
    void harnessPartPressed(const QString& brick, const QPointF& at);
    void harnessPartClicked(const QString& brick);
    // THE BRANCH TOOL ON A CONNECTOR: a bundle to start from it, out of its front or its back - asked at that point.
    void connectorBranchAsked(const QString& node, const QPoint& globalPos);
    // Something to tell the user.
    void message(const QString& text);
    // The camera turned, moved or zoomed.
    void cameraChanged();
    // Esc with nothing being laid.
    void escapePressed();
    // Esc with a tool in hand and nothing half done: back to Select.
    void toolCancelRequested();
    void rotatePartAsked(int axis, double degrees);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    bool event(QEvent* e) override;

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
    bool sourceHarnessVisible() const;
    bool routedHarnessVisible() const;
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
    std::vector<V3> stretchPinpoints_;
    QToolButton* pinpointStretch_ = nullptr;
    bool selectAndMove_ = true;
    bool jointsShown_ = false;
    int dragPlane_ = 0;
    V3 dragPlaneOrigin_;
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
        QString node;   // the node it is the body of - clicked anywhere on it, it is that node
        QString segment;   // the bundle it is (or a wire of) - clicked anywhere on it, it is that bundle
        double alpha = 1.0;
        QString brick;   // a label's source part, where it is not a route node
    };
    // The bundle whose drawn body the ray through a point of the picture meets first (empty: none), and where.
    QString segmentUnder(const QPointF& at, V3* hit = nullptr) const;
    Route3D originalRoute_;
    bool keepOriginalConnectors_ = true;
    int partNamesMode_ = 1;
    int nameHoverPart_ = -1;
    QString nameHoverBrick_, nameHoverNode_;
    QSet<QString> dragNodes_;
    mutable std::vector<Drawn> routeMeshes_;
    mutable V3 textEye_{1e300, 0, 0};
    mutable std::map<int, Mesh3D> sourceTextMeshes_;
    mutable bool routeDirty_ = true;
    Mesh3D layMesh_;
    // The release that ends a double-click is not another click.
    bool swallowRelease_ = false;
    bool layMadeStart_ = false;   // the bundle being laid started from a branch made for it
    mutable int branchSleeves_ = 0;
    RouteNode3D ghost_, placeTemplate_;
    bool ghostOn_ = false, ghostFrozen_ = false;
    QString sameAs_;
    int bgMode_ = 0;
    QColor bgColour_{30, 36, 44};
    QSet<QString> hiddenBricks_;
    QPointer<QVariantAnimation> turnAnim_;
    // The solids of the model hidden, and the ruler. Not saved.
    std::vector<char> envHidden_;
    // The harness's parts in a tree of boxes, for what the pointer is over - made again when they change. Not saved.
    mutable std::unique_ptr<RayIndex3D> harnessIndex_;
    mutable std::vector<QString> harnessIndexBricks_;
    mutable QString harnessIndexKey_;
    QString hoverKey_;
    qint64 hoverAt_ = 0;
    std::vector<double> envOwnAlpha_;
    QHash<QString, double> brickAlpha_;
    // The solid the last point found on the model is on (-1: none - the ground, the work plane).
    int surfacePart_ = -1;
    Mesh3D measureFaceA_;
    int measureStage_ = 0;      // 0 none being made, 1 its first end set
    V3 measureA_, measureB_, measureNormal_;
    std::vector<Measure3D> measures_;
    int selMeasure_ = -1;
    bool measureFaces_ = false;
    bool measureRadius_ = false;
    mutable Mesh3D measureMesh_, measureLitMesh_;
    mutable bool measureDirty_ = true;
    // The ruler near a point of the picture - its line or its label - or -1.
    int measureAt(const QPointF& at) const;
    // A ruler being made: from its first end, to where the pointer is - square off the first face, surface to surface.
    Measure3D measureMaking(const V3& to) const;
    // The section's see-through plane, over only the solids it cuts. Not saved.
    mutable Mesh3D sectionMesh_;
    mutable bool sectionDirty_ = true;
    std::vector<CableSag3D> sags_;
    mutable Mesh3D sagMesh_, fallMesh_;
    mutable bool sagDirty_ = true;
    std::vector<KeepOutHit> keepHits_;
    QString keepAlert_;
    bool keepShown_ = true;
    mutable Mesh3D keepBlockMesh_, keepAlertMesh_, keepHitMesh_;
    mutable bool keepDirty_ = true;
    bool skipPart(int part, const V3& point) const;
    // Locked, and what a drag is moving. Not saved.
    bool viewLocked_ = false;
    // A NODE TAKEN HOLD OF WITH MOVE OR STRETCH: where it was. Stretched, it stays where it is: its bundles are pulled
    // tight through it, as far as the pointer goes, and what comes through is cut off.
    bool beginNodeDrag(const QPointF& at, bool stretch, const QString& forceNode = QString());
    void tightenKept(double by);
    // THE CABLE STRETCHED: the one clicked, or the one of a node's cables selected - pulled through its connector.
    QString stretchSeg_;
    double stretchCut_ = 0.0;
    // PULLED, ITS LENGTH KEPT: from the point taken hold of out to the node at each bundle's far end, the points in
    // between (by their places in the bundle's laid points), the far node where it holds, and each step's length -
    // the cable laid as a cable is, pulled along behind the point and no longer than it was.
    struct Kept {
        QString segment;
        std::vector<int> via;   // outward from the point held
        V3 anchor;
        std::vector<double> step;   // from the point held to via[0], ..., to the anchor
        std::vector<double> bend;   // how far it turned at each point as it was taken hold of, in radians (point held first)
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
    // THE SAME CABLE BEYOND ITS CLIPS: each stretch, as long as it was, as short as it could be pulled, as it was laid -
    // by the bundle at the node held that runs into them. And each bundle's length as it was drawn when taken hold of.
    struct Through {
        QString segment;
        double length = 0.0, shortest = 0.0;
        std::vector<RouteVia3D> was;
    };
    std::map<QString, std::vector<Through>> through_;
    std::map<QString, double> keptBase_;
    // Length given to (or taken from) a bundle out of the stretches beyond its clips; how much there was.
    double slideThroughClips(const QString& segment, double take);
    bool keepLength() const;
    void beginKeepLength();
    void pullKept(V3 target);
    void pullKeptOnce(V3 target);
    void settleDrag(const Route3D& before);
    Route3D dragBaseline_;
    // Where the point pulled is reached to - `start`, or as near it as a stiff cable bends.
    V3 layKept(const Kept& k, const V3& start, double scale);
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

} // namespace Loopwire
