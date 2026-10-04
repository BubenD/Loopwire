#include "app/View3DPane.h"
#include "app/View3DHelpDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QSplitter>
#include <QSpinBox>
#include <QTableWidget>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

namespace {
// AN ANGLE BOX: a step past 180 comes round to -179 and on, as an angle does - not to a stop at the end.
class AngleSpinBox : public QSpinBox {
public:
    using QSpinBox::QSpinBox;
    void stepBy(int steps) override {
        if (!wrapping()) {
            QSpinBox::stepBy(steps);
            return;
        }
        setValue(static_cast<int>(std::lround(wrapDegrees(value() + steps * singleStep()))));
    }
};
} // namespace

View3DPane::View3DPane(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("view3d"));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);
    auto* bar = new QHBoxLayout;
    bar->setSpacing(3);
    source_ = new QComboBox(this);
    source_->setObjectName(QStringLiteral("view3dSource"));
    source_->addItems({QStringLiteral("The drawing"), QStringLiteral("The harness wizard")});
    source_->setToolTip(QStringLiteral("Which harness to show: the one placed on the sheet, or the one open in the harness wizard"));
    connect(source_, &QComboBox::activated, this, [this](int) { rebuild(true); });
    bar->addWidget(source_);
    // A PART WITH NO 3D MODEL: a box, or a solid made from its 2D drawing.
    look_ = new QComboBox(this);
    look_->setObjectName(QStringLiteral("view3dPartLook"));
    look_->addItems({QStringLiteral("Parts as boxes"), QStringLiteral("Parts turned from the drawing"),
                     QStringLiteral("Parts extruded from the drawing")});
    look_->setCurrentIndex(1);
    look_->setToolTip(QStringLiteral("A part with no 3D model attached: a box as big as it is drawn, or a solid made from "
                                     "its 2D drawing - turned round the way it goes in (a round part), or given depth (a "
                                     "square one)"));
    connect(look_, &QComboBox::activated, this, [this](int i) {
        partLook_ = i == 0 ? PartLook::Box : i == 1 ? PartLook::Revolved : PartLook::Extruded;
        rebuild(false);
    });
    bar->addWidget(look_);
    auto button = [this](QBoxLayout* row, const char* name, const QString& text, const QString& tip, auto fn) {
        auto* b = new QToolButton(this);
        b->setObjectName(QLatin1String(name));
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, fn);
        row->addWidget(b);
        return b;
    };
    button(bar, "view3dRefresh", QStringLiteral("Refresh"), QStringLiteral("Build it again from the drawing"), [this] { rebuild(false); });
    button(bar, "view3dFit", QStringLiteral("Fit"), QStringLiteral("Everything shown in view (or double-click)"), [this] { canvas_->fit(); });
    button(bar, "view3dIso", QStringLiteral("Iso"), QStringLiteral("At an angle"), [this] { canvas_->viewFrom(QStringLiteral("iso")); });
    button(bar, "view3dTop", QStringLiteral("Top"), QStringLiteral("From above"), [this] { canvas_->viewFrom(QStringLiteral("top")); });
    button(bar, "view3dFront", QStringLiteral("Front"), QStringLiteral("From the front"), [this] { canvas_->viewFrom(QStringLiteral("front")); });
    button(bar, "view3dSide", QStringLiteral("Side"), QStringLiteral("From the side"), [this] { canvas_->viewFrom(QStringLiteral("side")); });
    auto* wire = new QCheckBox(QStringLiteral("Wireframe"), this);
    wire->setObjectName(QStringLiteral("view3dWireframe"));
    connect(wire, &QCheckBox::toggled, this, [this](bool on) { canvas_->setWireframe(on); });
    bar->addWidget(wire);
    // THE VIEW'S THREE TURNS, in degrees, each all the way round: round the upright, up and over, and round the way it
    // looks. The mouse does the same - a left drag turns and tilts, with Shift it rolls.
    auto angle = [&](const char* name, const QString& label, const QString& tip) {
        bar->addWidget(new QLabel(label, this));
        auto* s = new AngleSpinBox(this);
        s->setObjectName(QLatin1String(name));
        s->setRange(-179, 180);   // -180 is 180: one stop for the one angle, so a step round wraps exactly
        s->setWrapping(true);
        s->setSingleStep(15);
        s->setSuffix(QStringLiteral("°"));
        s->setToolTip(tip);
        s->setKeyboardTracking(false);
        bar->addWidget(s);
        return s;
    };
    turn_ = angle("view3dTurn", QStringLiteral("Turn"), QStringLiteral("Round the upright (Z), all the way round - or drag left and right"));
    tilt_ = angle("view3dTilt", QStringLiteral("Tilt"), QStringLiteral("Up and over, all the way round - past the top it looks from the other "
                                                                       "side, upside down - or drag up and down"));
    roll_ = angle("view3dRoll", QStringLiteral("Roll"), QStringLiteral("Round the way it looks - or drag with Shift held"));
    for (QSpinBox* s : {turn_, tilt_, roll_})
        connect(s, &QSpinBox::valueChanged, this, [this] {
            Camera3D& cam = canvas_->camera();
            cam.yawDeg = turn_->value();
            cam.pitchDeg = tilt_->value();
            cam.rollDeg = roll_->value();
            canvas_->update();
        });
    bar->addStretch(1);
    // THE WHOLE SCREEN, for routing with room to see: everything here goes with it. Esc, F11 or this again, back.
    fullBtn_ = button(bar, "view3dFullScreen", QStringLiteral("Full screen"),
                      QStringLiteral("The 3D view on the whole screen, with all its settings (F11; Esc to come back)"),
                      [this] { setFullScreen(!fullScreen_); });
    fullBtn_->setCheckable(true);
    auto* f11 = new QShortcut(QKeySequence(Qt::Key_F11), this);
    f11->setContext(Qt::WidgetWithChildrenShortcut);
    connect(f11, &QShortcut::activated, this, [this] { setFullScreen(!fullScreen_); });
    lay->addLayout(bar);

    // THE ENVIRONMENT'S ROW: the model the harness is routed in, how it shows, and a section through it.
    envRow_ = new QWidget(this);
    auto* env = new QHBoxLayout(envRow_);
    env->setContentsMargins(0, 0, 0, 0);
    env->setSpacing(3);
    env->addWidget(new QLabel(QStringLiteral("Environment:"), envRow_));
    envLoad_ = button(env, "view3dEnvLoad", QStringLiteral("Load..."),
                      QStringLiteral("Load the CAD model the harness is routed in - a chassis, a panel, an enclosure - from "
                                     "a STEP, STL or OBJ file"),
                      [this] {
                          const QString dir = QSettings().value(QStringLiteral("view3d/environmentDir")).toString();
                          const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Load an environment"), dir,
                                                                            environment3DFileFilter());
                          if (path.isEmpty()) return;
                          QSettings().setValue(QStringLiteral("view3d/environmentDir"), QFileInfo(path).absolutePath());
                          loadEnvironment(path);
                      });
    envClear_ = button(env, "view3dEnvClear", QStringLiteral("Clear"), QStringLiteral("Take the environment away"),
                       [this] { clearEnvironment(); });
    envShown_ = new QCheckBox(QStringLiteral("Show"), envRow_);
    envShown_->setObjectName(QStringLiteral("view3dEnvShown"));
    envShown_->setChecked(true);
    envShown_->setToolTip(QStringLiteral("Show the environment round the harness"));
    connect(envShown_, &QCheckBox::toggled, this, [this](bool on) { canvas_->setEnvironmentShown(on); });
    env->addWidget(envShown_);
    env->addWidget(new QLabel(QStringLiteral("Opacity"), envRow_));
    envOpacity_ = new QSlider(Qt::Horizontal, envRow_);
    envOpacity_->setObjectName(QStringLiteral("view3dEnvOpacity"));
    envOpacity_->setRange(10, 100);
    envOpacity_->setValue(100);
    envOpacity_->setMaximumWidth(90);
    envOpacity_->setToolTip(QStringLiteral("How solid the environment is - less, to see the harness through it"));
    connect(envOpacity_, &QSlider::valueChanged, this, [this](int v) { canvas_->setEnvironmentOpacity(v / 100.0); });
    env->addWidget(envOpacity_);
    harnessShown_ = new QCheckBox(QStringLiteral("Harness"), envRow_);
    harnessShown_->setObjectName(QStringLiteral("view3dHarnessShown"));
    harnessShown_->setChecked(true);
    harnessShown_->setToolTip(QStringLiteral("Show the harness drawn on the sheet - off, to look at the environment and the "
                                             "route alone"));
    connect(harnessShown_, &QCheckBox::toggled, this, [this](bool on) { canvas_->setHarnessShown(on); });
    env->addWidget(harnessShown_);
    section_ = new QComboBox(envRow_);
    section_->setObjectName(QStringLiteral("view3dSection"));
    section_->addItems({QStringLiteral("No section"), QStringLiteral("Section X"), QStringLiteral("Section Y"),
                        QStringLiteral("Section Z")});
    section_->setToolTip(QStringLiteral("Cut the environment open across X, Y or Z, to see inside it - the harness is not cut"));
    connect(section_, &QComboBox::activated, this, [this](int) { applySection(); });
    env->addWidget(section_);
    sectionAt_ = new QSlider(Qt::Horizontal, envRow_);
    sectionAt_->setObjectName(QStringLiteral("view3dSectionAt"));
    sectionAt_->setRange(0, 1000);
    sectionAt_->setValue(500);
    sectionAt_->setMaximumWidth(120);
    sectionAt_->setToolTip(QStringLiteral("Where the section cuts: everything beyond it is cut away"));
    connect(sectionAt_, &QSlider::valueChanged, this, [this](int) { applySection(); });
    env->addWidget(sectionAt_);
    env->addStretch(1);
    lay->addWidget(envRow_);

    // THE ROUTE'S ROW: what a click does, and the bundles' diameter, bend radius and lead out of a connector.
    routeRow_ = new QWidget(this);
    auto* rr = new QHBoxLayout(routeRow_);
    rr->setContentsMargins(0, 0, 0, 0);
    rr->setSpacing(3);
    rr->addWidget(new QLabel(QStringLiteral("Route:"), routeRow_));
    tools_ = new QButtonGroup(this);
    tools_->setExclusive(true);
    const struct {
        RouteTool tool;
        const char* name;
        const char* text;
        const char* tip;
    } tools[] = {
        {RouteTool::Select, "view3dToolSelect", "Select", "Pick a point on the environment, or select a node or a bundle"},
        {RouteTool::Connector, "view3dToolConnector", "Connector", "Put a connector on the face clicked - its bundle leaves it "
                                                                   "along the face's normal"},
        {RouteTool::Branch, "view3dToolBranch", "Branch", "Put a branch point down - on a bundle, dividing it, or on a face"},
        {RouteTool::Clip, "view3dToolClip", "Clip", "Put a clip down - on a bundle, holding it there, or on a face"},
        {RouteTool::Route, "view3dToolRoute", "Route", "Lay a bundle: click the node it starts from (or a face, for a new "
                                                       "branch), the faces it runs over, and the node it ends at - or "
                                                       "double-click to end at a new branch. Esc: give up; Backspace: the "
                                                       "last point back"},
        {RouteTool::Delete, "view3dToolDelete", "Delete", "Take away the node or bundle clicked (a node takes its bundles "
                                                          "with it)"},
    };
    for (const auto& t : tools) {
        auto* b = new QToolButton(routeRow_);
        b->setObjectName(QLatin1String(t.name));
        b->setText(QString::fromLatin1(t.text));
        b->setToolTip(QString::fromLatin1(t.tip));
        b->setCheckable(true);
        b->setAutoRaise(true);
        tools_->addButton(b, static_cast<int>(t.tool));
        rr->addWidget(b);
    }
    tools_->button(static_cast<int>(RouteTool::Select))->setChecked(true);
    connect(tools_, &QButtonGroup::idClicked, this, [this](int id) { canvas_->setRouteTool(static_cast<RouteTool>(id)); });
    auto spin = [&](const char* name, const QString& label, double lo, double hi, double value, const QString& tip) {
        auto* text = new QLabel(label, routeRow_);
        rr->addWidget(text);
        auto* s = new QDoubleSpinBox(routeRow_);
        s->setObjectName(QLatin1String(name));
        s->setRange(lo, hi);
        s->setDecimals(1);
        s->setValue(value);
        s->setSuffix(QStringLiteral(" mm"));
        s->setToolTip(tip);
        s->setKeyboardTracking(false);
        rr->addWidget(s);
        return s;
    };
    diameter_ = spin("view3dRouteDiameter", QStringLiteral("Ø"), 0.5, 200.0, route_.diameter,
                     QStringLiteral("A bundle's diameter, where it is given none of its own (the table sets one bundle's)"));
    bend_ = spin("view3dRouteBend", QStringLiteral("Bend R"), 0.0, 2000.0, route_.bendRadius,
                 QStringLiteral("The tightest a bundle may be bent - Auto: six times its diameter"));
    bend_->setSpecialValueText(QStringLiteral("Auto"));
    lead_ = spin("view3dRouteLead", QStringLiteral("Lead"), 0.0, 500.0, route_.lead,
                 QStringLiteral("How far a bundle comes straight out of a connector before it may turn"));
    auto settings = [this] {
        if (filling_) return;
        beforeRouteChange();
        route_.diameter = diameter_->value();
        route_.bendRadius = bend_->value();
        route_.lead = lead_->value();
        saveRouting();
        refreshRoute();
        emit routeChanged();
    };
    for (QDoubleSpinBox* s : {diameter_, bend_, lead_}) connect(s, &QDoubleSpinBox::valueChanged, this, settings);
    undo_ = button(rr, "view3dRouteUndo", QStringLiteral("Undo"), QStringLiteral("Take back the last change to the route (Ctrl+Z)"),
                   [this] { undoRoute(); });
    // HOW IT IS DONE, SHOWN: the view itself recorded being used - the plane a click lands on, a cable routed from
    // one connector to the next, and how it bends - beside the same steps in words.
    button(rr, "view3dHelp", QStringLiteral("Help"),
           QStringLiteral("How to work in the 3D view: the plane, routing a cable from connector to connector, bending it"),
           [this] { showHelp(); });
    rr->addStretch(1);
    lay->addWidget(routeRow_);

    // THE WORK PLANE: where a click lands. On the model's faces (and the ground where there is no model) - or on a
    // plane, level or upright, put at any height or place: how a bundle is laid through the air, up a riser, over a
    // gap, at the height it really runs.
    planeRow_ = new QWidget(this);
    auto* pr = new QHBoxLayout(planeRow_);
    pr->setContentsMargins(0, 0, 0, 0);
    pr->setSpacing(3);
    pr->addWidget(new QLabel(QStringLiteral("Clicks land on:"), planeRow_));
    plane_ = new QComboBox(planeRow_);
    plane_->setObjectName(QStringLiteral("view3dWorkPlane"));
    plane_->addItems({QStringLiteral("the model's faces (the ground, Z = 0, beyond it)"), QStringLiteral("a plane across X (upright)"),
                      QStringLiteral("a plane across Y (upright)"), QStringLiteral("a plane across Z (level - a height)")});
    plane_->setToolTip(QStringLiteral("Where a click lands. A plane: the points of a bundle at that height (Z) or place (X, Y), in "
                                      "the air if need be - drawn see-through where it is"));
    pr->addWidget(plane_);
    pr->addWidget(new QLabel(QStringLiteral("at"), planeRow_));
    planeAt_ = new QDoubleSpinBox(planeRow_);
    planeAt_->setObjectName(QStringLiteral("view3dWorkPlaneAt"));
    planeAt_->setRange(-100000.0, 100000.0);
    planeAt_->setDecimals(1);
    planeAt_->setSingleStep(10.0);
    planeAt_->setSuffix(QStringLiteral(" mm"));
    planeAt_->setKeyboardTracking(false);
    planeAt_->setToolTip(QStringLiteral("Where the plane is along its axis - for a level plane, its height"));
    pr->addWidget(planeAt_);
    button(pr, "view3dWorkPlaneHere", QStringLiteral("Through the point picked"),
           QStringLiteral("The plane put through the node selected, or the point last picked on the model"), [this] {
               const int axis = plane_->currentIndex() - 1;
               if (axis < 0) return;
               V3 p;
               bool have = false;
               if (const RouteNode3D* n = route_.node(canvas_->selectedNode())) {
                   p = n->at;
                   have = true;
               } else if (canvas_->lastPick().hit) {
                   p = canvas_->lastPick().point;
                   have = true;
               }
               if (have) planeAt_->setValue(axis == 0 ? p.x : axis == 1 ? p.y : p.z);
           });
    pr->addStretch(1);
    lay->addWidget(planeRow_);
    connect(plane_, &QComboBox::currentIndexChanged, this, [this](int i) {
        // A plane chosen afresh is put through the middle of the model, where it can be seen to be.
        const int axis = i - 1;
        V3 lo, hi;
        if (axis >= 0 && env_.bounds(lo, hi)) {
            const QSignalBlocker hold(planeAt_);
            const double mid = axis == 0 ? (lo.x + hi.x) / 2 : axis == 1 ? (lo.y + hi.y) / 2 : (lo.z + hi.z) / 2;
            planeAt_->setValue(mid);
        }
        canvas_->setWorkPlane(axis, planeAt_->value());
    });
    connect(planeAt_, &QDoubleSpinBox::valueChanged, this,
            [this](double v) { canvas_->setWorkPlane(plane_->currentIndex() - 1, v); });

    // THE PICTURE, and beside it the bundles with their lengths.
    auto* split = new QSplitter(Qt::Horizontal, this);
    canvas_ = new View3DCanvas(split);
    canvas_->setObjectName(QStringLiteral("view3dCanvas"));
    split->addWidget(canvas_);
    side_ = new QWidget(split);
    auto* sl = new QVBoxLayout(side_);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->setSpacing(2);
    // THE HARNESS'S PARTS: each put in 3D or not - the one chosen is put where the Connector tool clicks next.
    harnessSaid_ = new QLabel(side_);
    harnessSaid_->setObjectName(QStringLiteral("view3dHarness"));
    harnessSaid_->setWordWrap(true);
    sl->addWidget(harnessSaid_);
    parts_ = new QListWidget(side_);
    parts_->setObjectName(QStringLiteral("view3dParts"));
    parts_->setToolTip(QStringLiteral("The harness's parts. Choose one and click a face with the Connector tool to put it "
                                      "there (again, to move it); the next part not yet put is chosen for you"));
    parts_->setMaximumHeight(140);
    sl->addWidget(parts_);
    auto* linkRow = new QHBoxLayout;
    linkRow->setSpacing(2);
    link_ = button(linkRow, "view3dLinkPart", QStringLiteral("Link"),
                   QStringLiteral("Put the part chosen at the node selected in the picture"), [this] {
                       if (QListWidgetItem* it = parts_->currentItem()) linkSelectedNode(it->data(Qt::UserRole).toString());
                   });
    unlink_ = button(linkRow, "view3dUnlinkPart", QStringLiteral("Unlink"),
                     QStringLiteral("The node selected in the picture put for no part"), [this] { linkSelectedNode(QString()); });
    lengths_ = button(linkRow, "view3dLengthsToHarness", QStringLiteral("Lengths to harness"),
                      QStringLiteral("Give each cable of the harness its length along the route, between the parts it joins"),
                      [this] { applyLengths(true); });
    linkRow->addStretch(1);
    sl->addLayout(linkRow);
    // THE CONNECTOR SELECTED, TURNED: which way it points - the way its bundle leaves it - and how it is turned round
    // that, a rectangular housing lying one way or another. Shown while a connector is selected.
    connBox_ = new QWidget(side_);
    connBox_->setObjectName(QStringLiteral("view3dConnector"));
    auto* cg = new QGridLayout(connBox_);
    cg->setContentsMargins(0, 4, 0, 4);
    cg->setHorizontalSpacing(4);
    cg->setVerticalSpacing(2);
    connSaid_ = new QLabel(connBox_);
    cg->addWidget(connSaid_, 0, 0, 1, 2);
    cg->addWidget(new QLabel(QStringLiteral("Points"), connBox_), 1, 0);
    aim_ = new QComboBox(connBox_);
    aim_->setObjectName(QStringLiteral("view3dConnAim"));
    aim_->addItems({QStringLiteral("out of its face"), QStringLiteral("+X"), QStringLiteral("-X"), QStringLiteral("+Y"),
                    QStringLiteral("-Y"), QStringLiteral("+Z (up)"), QStringLiteral("-Z (down)"), QStringLiteral("as turned")});
    aim_->setToolTip(QStringLiteral("Which way it points - the way its bundle leaves it"));
    cg->addWidget(aim_, 1, 1);
    auto connSpin = [&](const char* name, const QString& label, int lo, int hi, bool wrap, const QString& tip, int row) {
        cg->addWidget(new QLabel(label, connBox_), row, 0);
        QSpinBox* s = wrap ? new AngleSpinBox(connBox_) : new QSpinBox(connBox_);
        s->setObjectName(QLatin1String(name));
        s->setRange(lo, hi);
        s->setWrapping(wrap);
        s->setSingleStep(15);
        s->setSuffix(QStringLiteral("°"));
        s->setKeyboardTracking(false);
        s->setToolTip(tip);
        cg->addWidget(s, row, 1);
        return s;
    };
    aimTurn_ = connSpin("view3dConnTurn", QStringLiteral("Turned"), -179, 180, true,
                        QStringLiteral("The way it points, round the upright (Z), all the way round"), 2);
    aimTilt_ = connSpin("view3dConnTilt", QStringLiteral("Tilted"), -90, 90, false,
                        QStringLiteral("The way it points, up (+) or down (-) from level"), 3);
    spin_ = connSpin("view3dConnSpin", QStringLiteral("Spun"), -179, 180, true,
                     QStringLiteral("Round its own axis: how its housing lies (R in the picture: a quarter turn)"), 4);
    connBox_->setVisible(false);
    sl->addWidget(connBox_);
    connect(aim_, &QComboBox::activated, this, [this](int i) {
        static const V3 dirs[] = {{0, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        if (i >= 0 && i < 7) applyAim(dirs[i], spin_->value());
    });
    for (QSpinBox* s : {aimTurn_, aimTilt_})
        connect(s, &QSpinBox::valueChanged, this, [this] {
            const double t = aimTurn_->value() * M_PI / 180.0, p = aimTilt_->value() * M_PI / 180.0;
            applyAim(V3{std::cos(p) * std::cos(t), std::cos(p) * std::sin(t), std::sin(p)}, spin_->value());
        });
    connect(spin_, &QSpinBox::valueChanged, this, [this](int v) {
        if (const RouteNode3D* n = route_.node(canvas_->selectedNode())) applyAim(n->normal, v);
    });

    table_ = new QTableWidget(0, 3, side_);
    table_->setObjectName(QStringLiteral("view3dRoutes"));
    table_->setHorizontalHeaderLabels({QStringLiteral("Bundle"), QStringLiteral("Length"), QStringLiteral("Ø")});
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setToolTip(QStringLiteral("Each bundle routed, its length along its path - the Ø column sets one bundle's diameter"));
    sl->addWidget(table_, 1);
    totals_ = new QLabel(side_);
    totals_->setObjectName(QStringLiteral("view3dRouteTotals"));
    totals_->setWordWrap(true);
    sl->addWidget(totals_);
    split->addWidget(side_);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setSizes({800, 230});
    lay->addWidget(split, 1);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("view3dStatus"));
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(status_);
    canvas_->setScene(&scene_);
    canvas_->setRoute(&route_);
    connect(canvas_, &View3DCanvas::picked, this, [this](const Pick3D& pick) { showPick(pick); });
    connect(canvas_, &View3DCanvas::routeAboutToChange, this, [this] { beforeRouteChange(); });
    connect(canvas_, &View3DCanvas::routeChanged, this, [this] {
        saveRouting();
        refreshRoute();
        refreshParts();
        emit routeChanged();
    });
    connect(parts_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* it) {
        if (filling_) return;
        setPartToPlace(it ? it->data(Qt::UserRole).toString() : QString());
    });
    connect(canvas_, &View3DCanvas::undoRequested, this, [this] { undoRoute(); });
    connect(canvas_, &View3DCanvas::cameraChanged, this, [this] { syncCamera(); });
    connect(canvas_, &View3DCanvas::escapePressed, this, [this] {
        if (fullScreen_) setFullScreen(false);
    });
    connect(canvas_, &View3DCanvas::routeChanged, this, [this] { syncConnector(); });
    connect(canvas_, &View3DCanvas::selectionChanged, this, [this] { syncConnector(); });
    connect(canvas_, &View3DCanvas::selectionChanged, this, [this](const QString& id) {
        const QSignalBlocker hold(table_);
        table_->clearSelection();
        for (int r = 0; r < table_->rowCount(); ++r)
            if (table_->item(r, 0) && table_->item(r, 0)->data(Qt::UserRole).toString() == id) table_->selectRow(r);
    });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        const QList<QTableWidgetItem*> sel = table_->selectedItems();
        if (!sel.isEmpty()) canvas_->selectSegment(table_->item(sel.first()->row(), 0)->data(Qt::UserRole).toString());
    });
    // ONE BUNDLE'S DIAMETER, from the table.
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (filling_ || item->column() != 2) return;
        const QString id = table_->item(item->row(), 0)->data(Qt::UserRole).toString();
        if (!route_.segment(id)) return;
        bool ok = false;
        const double d = item->text().trimmed().remove(QStringLiteral("mm")).trimmed().toDouble(&ok);
        beforeRouteChange();
        route_.segment(id)->diameter = ok && d > 0.0 ? d : 0.0;
        saveRouting();
        refreshRoute();
        emit routeChanged();
    });
    applySection();
    refreshRoute();
    syncCamera();
}

void View3DPane::syncCamera() {
    const Camera3D& cam = canvas_->camera();
    const QSignalBlocker a(turn_), b(tilt_), c(roll_);
    turn_->setValue(static_cast<int>(std::lround(wrapDegrees(cam.yawDeg))));
    tilt_->setValue(static_cast<int>(std::lround(wrapDegrees(cam.pitchDeg))));
    roll_->setValue(static_cast<int>(std::lround(wrapDegrees(cam.rollDeg))));
}

void View3DPane::syncConnector() {
    const RouteNode3D* n = route_.node(canvas_->selectedNode());
    const bool show = n && n->kind == RouteNodeKind::Connector && !routeRow_->isHidden();
    connBox_->setVisible(show);
    if (!show) return;
    const QSignalBlocker a(aim_), b(aimTurn_), c(aimTilt_), d(spin_);
    connSaid_->setText(QStringLiteral("<b>Connector %1</b>").arg(n->label.toHtmlEscaped()));
    static const V3 dirs[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    int which = 7;
    if ((n->normal - n->face).length() < 1e-6) which = 0;
    else
        for (int i = 0; i < 6; ++i)
            if ((n->normal - dirs[i]).length() < 1e-6) which = i + 1;
    aim_->setCurrentIndex(which);
    aimTurn_->setValue(static_cast<int>(std::lround(std::atan2(n->normal.y, n->normal.x) * 180.0 / M_PI)));
    aimTilt_->setValue(static_cast<int>(std::lround(std::asin(std::clamp(n->normal.z, -1.0, 1.0)) * 180.0 / M_PI)));
    spin_->setValue(static_cast<int>(std::lround(wrapDegrees(n->spinDeg))));
}

void View3DPane::applyAim(const V3& dir, double spinDeg) {
    const QString id = canvas_->selectedNode();
    if (!route_.node(id)) return;
    beforeRouteChange();
    route_.aimNode(id, dir, wrapDegrees(spinDeg));
    saveRouting();
    refreshRoute();
    canvas_->routeRedraw();
    syncConnector();
    emit routeChanged();
}

void View3DPane::setFullScreen(bool on) {
    if (on == fullScreen_) return;
    fullScreen_ = on;
    if (on) {
        // OUT OF WHEREVER IT IS - the dock, the wizard - as a window of its own on the whole screen; back where it was
        // when it comes back.
        normalFlags_ = windowFlags();
        setWindowFlags(Qt::Window);
        setWindowTitle(QStringLiteral("3D view"));
        showFullScreen();
    } else {
        setWindowFlags(normalFlags_);
        showNormal();
        show();
    }
    const QSignalBlocker hold(fullBtn_);
    fullBtn_->setChecked(on);
    fullBtn_->setText(on ? QStringLiteral("Back") : QStringLiteral("Full screen"));
    canvas_->setFocus();
}

void View3DPane::setWorkPlane(int axis, double at) {
    {
        const QSignalBlocker a(plane_), b(planeAt_);
        plane_->setCurrentIndex(std::clamp(axis, -1, 2) + 1);
        planeAt_->setValue(at);
    }
    canvas_->setWorkPlane(axis, at);
}

bool View3DPane::loadExample() {
    // The resource copied out to a file of its own: a model is read from a file, and kept by its path.
    const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw-3d-example.obj"));
    QFile::remove(path);
    if (!QFile::copy(exampleResource(), path)) return false;
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return loadEnvironment(path, true);
}

View3DPane::~View3DPane() {
    // A model still being read is waited for: the reading has the pane's results to hand over.
    if (loading_) loading_->wait();
}

void View3DPane::setSources(std::function<std::vector<Entity>()> drawing, std::function<std::vector<Entity>()> wizard) {
    drawing_ = std::move(drawing);
    wizard_ = std::move(wizard);
}

void View3DPane::setModelLookup(std::function<QString(const QString&)> modelOf) { modelOf_ = std::move(modelOf); }

void View3DPane::setSourceChoice(bool shown) { source_->setVisible(shown); }

void View3DPane::setEnvironmentChoice(bool shown) {
    envRow_->setVisible(shown);
    routeRow_->setVisible(shown);
    planeRow_->setVisible(shown);
    side_->setVisible(shown);
    canvas_->setRoute(shown ? &route_ : nullptr);
}

void View3DPane::showHelp() {
    // One window, kept: asked for again, it comes to the front where it was.
    if (!help_) {
        help_ = new View3DHelpDialog(this);
        connect(help_, &View3DHelpDialog::exampleRequested, this, [this] {
            if (routeRow_->isHidden()) setEnvironmentChoice(true);
            loadExample();
        });
    }
    help_->show();
    help_->raise();
    help_->activateWindow();
}

void View3DPane::setPinsLookup(std::function<PartPins(const QString&)> pinsOf) { pinsOf_ = std::move(pinsOf); }

void View3DPane::setPartLook(PartLook look) {
    partLook_ = look;
    look_->setCurrentIndex(look == PartLook::Box ? 0 : look == PartLook::Revolved ? 1 : 2);
    rebuild(false);
}

void View3DPane::setHighlight(const QStringList& bricks) { canvas_->setHighlight(bricks); }

void View3DCanvas::setHighlight(const QStringList& bricks) {
    lit_ = bricks;
    update();
}

void View3DPane::rebuild(bool fit) {
    const bool fromWizard = source_->currentIndex() == 1;
    std::vector<Entity> ents = fromWizard ? (wizard_ ? wizard_() : std::vector<Entity>{})
                                          : (drawing_ ? drawing_() : std::vector<Entity>{});
    // NOTHING PLACED ON THE SHEET YET: the wizard's harness, if one is open.
    bool fell = false;
    if (!fromWizard && wizard_) {
        bool any = false;
        for (const Entity& e : ents) any = any || e.type == EntityType::TwistedPair;
        if (!any) {
            ents = wizard_();
            fell = true;
        }
    }
    scene_ = buildHarness3D(ents, modelOf_, partLook_, pinsOf_);
    canvas_->setScene(&scene_);
    if (fit) canvas_->fit();
    canvas_->update();
    QString said = scene_.runs == 0 && scene_.parts == 0
                       ? QStringLiteral("No harness to show - place one from the harness wizard, or open one in it.")
                       : QStringLiteral("%1 part(s), %2 with a 3D model, the rest %5; %3 run(s)%4.")
                             .arg(scene_.parts)
                             .arg(scene_.modelled)
                             .arg(scene_.runs)
                             .arg(fell ? QStringLiteral(" - from the harness wizard, nothing being placed on the sheet") : QString())
                             .arg(partLook_ == PartLook::Box        ? QStringLiteral("as boxes")
                                  : partLook_ == PartLook::Revolved ? QStringLiteral("turned from their drawings")
                                                                    : QStringLiteral("extruded from their drawings"));
    if (!scene_.notes.isEmpty()) said += QLatin1Char('\n') + scene_.notes.join(QLatin1Char('\n'));
    sceneSaid_ = said;
    sayStatus();
    refreshParts();
}

void View3DPane::sayStatus() {
    QStringList lines{sceneSaid_};
    if (loading_)
        lines << QStringLiteral("Environment: reading %1...").arg(loadingName_);
    else if (!env_.parts.empty() || !env_.how.isEmpty())
        lines << QStringLiteral("Environment: %1").arg(env_.how);
    if (!pickSaid_.isEmpty()) lines << pickSaid_;
    status_->setText(lines.join(QLatin1Char('\n')));
}

// ---- The environment ------------------------------------------------------------------------------

bool View3DPane::loadEnvironment(const QString& path, bool wait) {
    if (loading_) return false;
    envRef_ = path;
    auto env = std::make_shared<Environment3D>();
    auto index = std::make_shared<RayIndex3D>();
    auto work = [path, env, index] {
        *env = loadEnvironment3D(path);
        std::vector<const Mesh3D*> meshes;
        for (const EnvPart3D& p : env->parts) meshes.push_back(&p.mesh);
        index->build(meshes);
    };
    if (wait) {
        work();
        takeEnvironment(std::move(*env), index);
        return !env_.empty();
    }
    // READ IN THE BACKGROUND: a big assembly takes a while, and the drawing is not to stop while it does.
    loadingName_ = QFileInfo(path).fileName();
    envLoad_->setEnabled(false);
    loading_ = QThread::create(work);
    connect(loading_, &QThread::finished, this, [this, env, index] {
        loading_->deleteLater();
        loading_ = nullptr;
        envLoad_->setEnabled(true);
        takeEnvironment(std::move(*env), index);
    });
    loading_->start();
    sayStatus();
    return true;
}

void View3DPane::takeEnvironment(Environment3D env, std::shared_ptr<RayIndex3D> index) {
    env_ = std::move(env);
    envIndex_ = std::move(index);
    pickSaid_.clear();
    canvas_->clearPick();
    canvas_->setEnvironment(&env_, envIndex_.get());
    applySection();
    V3 lo, hi;
    if (env_.bounds(lo, hi)) canvas_->fitBox(lo, hi);
    // Asked for and not read: the drawing is not to name it. Named by the drawing and not read: it still names it.
    if (!quietLoad_) {
        if (env_.empty()) envRef_.clear();
        saveRouting();
    }
    quietLoad_ = false;
    sayStatus();
    emit environmentChanged();
}

void View3DPane::clearEnvironment() {
    if (loading_) return;
    env_ = Environment3D();
    envIndex_.reset();
    envRef_.clear();
    pickSaid_.clear();
    canvas_->clearPick();
    canvas_->setEnvironment(nullptr, nullptr);
    applySection();
    saveRouting();
    sayStatus();
    emit environmentChanged();
}

void View3DPane::applySection() {
    Section3D s;
    const int axis = section_->currentIndex() - 1;
    sectionAt_->setEnabled(axis >= 0);
    V3 lo, hi;
    if (axis >= 0 && env_.bounds(lo, hi)) {
        s.on = true;
        s.normal = axis == 0 ? V3{1, 0, 0} : axis == 1 ? V3{0, 1, 0} : V3{0, 0, 1};
        const double a = dot3(lo, s.normal), b = dot3(hi, s.normal);
        s.offset = a + (b - a) * sectionAt_->value() / 1000.0;
    }
    canvas_->setSection(s);
}

void View3DPane::showPick(const Pick3D& pick) {
    if (!pick.hit) {
        pickSaid_.clear();
    } else {
        QString name = pick.part >= 0 && pick.part < static_cast<int>(env_.parts.size())
                           ? env_.parts[static_cast<size_t>(pick.part)].name
                           : QString();
        if (name.isEmpty()) name = QStringLiteral("solid %1").arg(pick.part + 1);
        pickSaid_ = QStringLiteral("Picked %1 at %2, %3, %4 mm - its face looking %5, %6, %7")
                        .arg(name)
                        .arg(pick.point.x, 0, 'f', 1)
                        .arg(pick.point.y, 0, 'f', 1)
                        .arg(pick.point.z, 0, 'f', 1)
                        .arg(pick.normal.x, 0, 'f', 2)
                        .arg(pick.normal.y, 0, 'f', 2)
                        .arg(pick.normal.z, 0, 'f', 2);
    }
    sayStatus();
}

// ---- The route ------------------------------------------------------------------------------------

void View3DPane::setRoutingStore(std::function<QJsonObject()> load, std::function<void(const QJsonObject&)> save,
                                 std::function<QString()> drawingDir) {
    loadRouting_ = std::move(load);
    saveRouting_ = std::move(save);
    drawingDir_ = std::move(drawingDir);
}

void View3DPane::reloadRouting(bool wait) {
    const QJsonObject o = loadRouting_ ? loadRouting_() : QJsonObject();
    route_ = Route3D::fromJson(o);
    history_.clear();
    canvas_->setRoute(routeRow_->isHidden() ? nullptr : &route_);
    // THE ENVIRONMENT IT NAMES: read again only when it is another one - where it was, or beside the drawing.
    const QString named = o.value(QStringLiteral("environment")).toObject().value(QStringLiteral("path")).toString();
    if (named.isEmpty()) {
        if (!loading_ && (!env_.parts.empty() || !envRef_.isEmpty())) {
            env_ = Environment3D();
            envIndex_.reset();
            canvas_->clearPick();
            canvas_->setEnvironment(nullptr, nullptr);
            applySection();
        }
        envRef_.clear();
    } else if (env_.empty() || QFileInfo(named).absoluteFilePath() != QFileInfo(env_.path).absoluteFilePath()) {
        QString path = named;
        if (!QFileInfo::exists(path) && drawingDir_) {
            const QString beside = QDir(drawingDir_()).filePath(QFileInfo(named).fileName());
            if (QFileInfo::exists(beside)) path = beside;
        }
        if (QFileInfo::exists(path)) {
            quietLoad_ = true;
            if (!loadEnvironment(path, wait)) quietLoad_ = false;
        } else {
            env_ = Environment3D();
            envIndex_.reset();
            canvas_->setEnvironment(nullptr, nullptr);
            env_.how = QStringLiteral("the drawing's environment %1 was not found").arg(QFileInfo(named).fileName());
            envRef_ = named;
        }
    }
    {
        filling_ = true;
        diameter_->setValue(route_.diameter);
        bend_->setValue(route_.bendRadius);
        lead_->setValue(route_.lead);
        filling_ = false;
    }
    refreshRoute();
    refreshParts();
    sayStatus();
}

void View3DPane::saveRouting() {
    if (!saveRouting_) return;
    QJsonObject o;
    if (!route_.empty() || !envRef_.isEmpty()) o = route_.toJson();
    if (!envRef_.isEmpty())
        o[QStringLiteral("environment")] = QJsonObject{{QStringLiteral("path"), QFileInfo(envRef_).absoluteFilePath()},
                                                      {QStringLiteral("file"), QFileInfo(envRef_).fileName()}};
    saveRouting_(o);
}

void View3DPane::beforeRouteChange() {
    history_.push_back(route_.toJson());
    if (history_.size() > 100) history_.erase(history_.begin());
}

void View3DPane::undoRoute() {
    if (history_.empty()) return;
    route_ = Route3D::fromJson(history_.back());
    history_.pop_back();
    {
        filling_ = true;
        diameter_->setValue(route_.diameter);
        bend_->setValue(route_.bendRadius);
        lead_->setValue(route_.lead);
        filling_ = false;
    }
    saveRouting();
    refreshRoute();
    refreshParts();
    emit routeChanged();
}

// ---- The harness the route is for -----------------------------------------------------------------

void View3DPane::setHarnessLink(std::function<RouteHarness()> harness,
                                std::function<void(const QString&, const BrickDesign&)> apply) {
    harness_ = std::move(harness);
    applyHarness_ = std::move(apply);
    refreshParts();
}

void View3DPane::refreshParts() {
    const RouteHarness h = harness_ ? harness_() : RouteHarness();
    const QString was = parts_->currentItem() ? parts_->currentItem()->data(Qt::UserRole).toString() : QString();
    filling_ = true;
    parts_->clear();
    int put = 0, all = 0;
    int wasRow = -1;
    if (h.ok)
        for (const Brick& b : h.design.bricks) {
            if (b.type != BrickType::End) continue;
            const QString name = b.name.isEmpty() ? partKindName(b.kind) : b.name;
            const QString node = routeNodeOfPart(route_, b.id);
            const RouteNode3D* n = route_.node(node);
            auto* item = new QListWidgetItem(n ? QStringLiteral("%1  %2 - at %3").arg(name, partKindName(b.kind), n->label)
                                               : QStringLiteral("%1  %2 - not put in 3D").arg(name, partKindName(b.kind)),
                                             parts_);
            item->setData(Qt::UserRole, b.id);
            item->setForeground(n ? QColor(60, 150, 80) : palette().color(QPalette::Text));
            ++all;
            put += n ? 1 : 0;
            if (b.id == was) wasRow = parts_->count() - 1;
        }
    harnessSaid_->setText(!h.ok ? QStringLiteral("No harness to put in 3D - open one in the harness wizard, or place one.")
                                : QStringLiteral("Harness %1: %2 of %3 part(s) put in 3D.")
                                      .arg(h.name.isEmpty() ? QStringLiteral("(unnamed)") : QStringLiteral("\"%1\"").arg(h.name))
                                      .arg(put)
                                      .arg(all));
    // THE PART JUST PUT: on to the next one not put yet - the one being put is kept while it is not put.
    int row = wasRow;
    const QString placing = canvas_->partToPlace();
    if (!placing.isEmpty() && !routeNodeOfPart(route_, placing).isEmpty()) {
        row = -1;
        for (int i = 0; i < parts_->count(); ++i)
            if (routeNodeOfPart(route_, parts_->item(i)->data(Qt::UserRole).toString()).isEmpty()) {
                row = i;
                break;
            }
    }
    if (row >= 0) parts_->setCurrentRow(row);
    filling_ = false;
    if (!placing.isEmpty() && !routeNodeOfPart(route_, placing).isEmpty())
        setPartToPlace(row >= 0 ? parts_->item(row)->data(Qt::UserRole).toString() : QString());
    link_->setEnabled(h.ok);
    unlink_->setEnabled(h.ok);
    lengths_->setEnabled(h.ok && !route_.segments.empty());
}

void View3DPane::setPartToPlace(const QString& brick) {
    const RouteHarness h = harness_ ? harness_() : RouteHarness();
    const Brick* b = h.ok ? h.design.brick(brick) : nullptr;
    if (!b) {
        canvas_->setPartToPlace(QString(), QString(), RouteNodeKind::Connector);
        return;
    }
    canvas_->setPartToPlace(b->id, b->name.isEmpty() ? partKindName(b->kind) : b->name,
                            b->kind == PartKind::Connector ? RouteNodeKind::Connector : RouteNodeKind::Branch);
}

bool View3DPane::linkSelectedNode(const QString& brick) {
    RouteNode3D* n = route_.node(canvas_->selectedNode());
    if (!n) return false;
    const RouteHarness h = harness_ ? harness_() : RouteHarness();
    const Brick* b = h.ok && !brick.isEmpty() ? h.design.brick(brick) : nullptr;
    if (!brick.isEmpty() && !b) return false;
    beforeRouteChange();
    // One node a part: it leaves any other it was put at.
    if (b)
        for (RouteNode3D& o : route_.nodes)
            if (o.brick == b->id) {
                o.brick.clear();
                o.part.clear();
            }
    n = route_.node(canvas_->selectedNode());
    n->brick = b ? b->id : QString();
    n->part = b ? (b->name.isEmpty() ? partKindName(b->kind) : b->name) : QString();
    if (b) n->label = n->part;
    saveRouting();
    refreshRoute();
    refreshParts();
    emit routeChanged();
    return true;
}

RouteLengths View3DPane::proposedLengths() const {
    const RouteHarness h = harness_ ? harness_() : RouteHarness();
    return h.ok ? routeLengthsFor(h.design, route_) : RouteLengths();
}

bool View3DPane::applyLengths(bool ask) {
    RouteHarness h = harness_ ? harness_() : RouteHarness();
    if (!h.ok) return false;
    const RouteLengths lengths = routeLengthsFor(h.design, route_);
    QStringList lines;
    for (const RouteLengthChange& c : lengths.changes)
        lines << (c.now > 0.0 ? QStringLiteral("%1: %2 mm to %3 mm").arg(c.what).arg(c.was, 0, 'f', 0).arg(c.now, 0, 'f', 0)
                              : QStringLiteral("%1: with the cable's length again").arg(c.what));
    const QString notes = lengths.notes.isEmpty() ? QString() : QStringLiteral("\n\nLeft as they are:\n") + lengths.notes.join(QLatin1Char('\n'));
    if (lengths.changes.empty()) {
        if (ask)
            QMessageBox::information(this, QStringLiteral("Lengths to harness"),
                                     QStringLiteral("The harness's lengths are already as the route has them (%1 cable(s) measured).%2")
                                         .arg(lengths.measured)
                                         .arg(notes));
        return false;
    }
    if (ask && QMessageBox::question(this, QStringLiteral("Lengths to harness"),
                                     QStringLiteral("Give harness %1 its lengths along the route?\n\n%2%3\n\nA harness already "
                                                    "placed on the sheet keeps its old lengths until it is placed again from the "
                                                    "wizard.")
                                         .arg(h.name.isEmpty() ? QStringLiteral("(unnamed)") : QStringLiteral("\"%1\"").arg(h.name),
                                              lines.join(QLatin1Char('\n')), notes)) != QMessageBox::Yes)
        return false;
    applyRouteLengths(h.design, lengths);
    if (applyHarness_) applyHarness_(h.name, h.design);
    pickSaid_ = QStringLiteral("%1 length(s) given to the harness from the route.").arg(lengths.changes.size());
    sayStatus();
    return true;
}

void View3DPane::setRouteTool(RouteTool tool) {
    if (QAbstractButton* b = tools_->button(static_cast<int>(tool))) b->setChecked(true);
    canvas_->setRouteTool(tool);
}

void View3DPane::refreshRoute() {
    filling_ = true;
    const QString selected = canvas_->selectedSegment();
    table_->setRowCount(0);
    double total = 0.0;
    int tight = 0;
    for (const RouteSegment3D& s : route_.segments) {
        const RouteNode3D* a = route_.node(s.a);
        const RouteNode3D* b = route_.node(s.b);
        int t = 0;
        route_.centreline(s, &t);
        tight += t;
        const double len = route_.length(s);
        total += len;
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto* name = new QTableWidgetItem(QStringLiteral("%1 - %2").arg(a ? a->label : s.a, b ? b->label : s.b));
        name->setData(Qt::UserRole, s.id);
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        auto* length = new QTableWidgetItem(QStringLiteral("%1 mm").arg(len, 0, 'f', 1));
        length->setFlags(length->flags() & ~Qt::ItemIsEditable);
        length->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* dia = new QTableWidgetItem(s.diameter > 0.0 ? QString::number(s.diameter, 'f', 1)
                                                          : QStringLiteral("%1 (route)").arg(route_.diameter, 0, 'f', 1));
        dia->setToolTip(QStringLiteral("This bundle's diameter - empty for the route's"));
        table_->setItem(row, 0, name);
        table_->setItem(row, 1, length);
        table_->setItem(row, 2, dia);
        if (s.id == selected) table_->selectRow(row);
    }
    int connectors = 0;
    for (const RouteNode3D& n : route_.nodes) connectors += n.kind == RouteNodeKind::Connector ? 1 : 0;
    QString said = route_.empty() ? QStringLiteral("Nothing routed yet - put connectors on the environment, then lay bundles "
                                                   "between them.")
                                  : QStringLiteral("%1 bundle(s), %2 mm in all; %3 connector(s), %4 node(s).")
                                        .arg(route_.segments.size())
                                        .arg(total, 0, 'f', 1)
                                        .arg(connectors)
                                        .arg(route_.nodes.size());
    if (tight > 0)
        said += QStringLiteral(" %1 bend(s) tighter than the bend radius - not room for it between the points laid.").arg(tight);
    totals_->setText(said);
    undo_->setEnabled(!history_.empty());
    filling_ = false;
    canvas_->routeRedraw();
}

// ---- The picture ----------------------------------------------------------------------------------

View3DCanvas::View3DCanvas(QWidget* parent) : QWidget(parent) {
    setMinimumSize(200, 160);
    setMouseTracking(false);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(QStringLiteral("Left drag: turn - right or middle drag: move - wheel: zoom - double-click: fit - click on the "
                              "environment: pick a point on it, or use the route tool chosen"));
}

void View3DCanvas::setScene(const Harness3DScene* scene) { scene_ = scene; }

void View3DCanvas::setEnvironment(const Environment3D* env, const RayIndex3D* index) {
    env_ = env;
    envIndex_ = index;
    update();
}

void View3DCanvas::setEnvironmentShown(bool on) {
    envShown_ = on;
    update();
}

void View3DCanvas::setEnvironmentOpacity(double alpha) {
    envAlpha_ = std::clamp(alpha, 0.05, 1.0);
    update();
}

void View3DCanvas::setHarnessShown(bool on) {
    harnessShown_ = on;
    update();
}

void View3DCanvas::setSection(const Section3D& section) {
    section_ = section;
    update();
}

void View3DCanvas::setUseGpu(bool on) {
    useGpu_ = on;
    update();
}

QString View3DCanvas::rendererSaid() const {
    if (lastOnGpu_) return QStringLiteral("graphics card");
    if (!useGpu_) return QStringLiteral("software");
    return gl_ && !gl_->why().isEmpty() ? QStringLiteral("software (%1)").arg(gl_->why()) : QStringLiteral("software");
}

void View3DCanvas::fitBox(const V3& lo, const V3& hi) {
    cam_.fit(lo, hi, static_cast<double>(std::max(1, width())) / std::max(1, height()));
    update();
}

void View3DCanvas::fit() {
    V3 lo, hi;
    bool any = false;
    auto take = [&](const V3& a, const V3& b) {
        lo = any ? V3{std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)} : a;
        hi = any ? V3{std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)} : b;
        any = true;
    };
    if (scene_ && harnessShown_)
        for (const Harness3DItem& it : scene_->items) {
            V3 a, b;
            if (it.mesh.bounds(a, b)) take(a, b);
        }
    V3 a, b;
    if (env_ && envShown_ && env_->bounds(a, b)) take(a, b);
    if (route_)
        for (const RouteNode3D& n : route_->nodes) take(n.at, n.at);
    if (!any) {
        lo = {-50, -50, -50};
        hi = {50, 50, 50};
    }
    fitBox(lo, hi);
}

void View3DCanvas::viewFrom(const QString& which) {
    cam_.rollDeg = 0.0;
    if (which == QLatin1String("top")) {
        cam_.yawDeg = -90.0;
        cam_.pitchDeg = 90.0;
    } else if (which == QLatin1String("front")) {
        cam_.yawDeg = -90.0;
        cam_.pitchDeg = 0.0;
    } else if (which == QLatin1String("side")) {
        cam_.yawDeg = 0.0;
        cam_.pitchDeg = 0.0;
    } else {
        cam_.yawDeg = -55.0;
        cam_.pitchDeg = 35.0;
    }
    fit();
    emit cameraChanged();
}

void View3DCanvas::setWireframe(bool on) {
    wire_ = on;
    update();
}

Pick3D View3DCanvas::pickAt(const QPointF& at) {
    Pick3D pick;
    if (env_ && envIndex_ && envShown_ && !envIndex_->empty()) {
        V3 origin, dir;
        cam_.rayAt(at.x(), at.y(), width(), height(), origin, dir);
        // ON PAST WHAT THE SECTION CUTS AWAY: the first face left standing.
        for (int tries = 0; tries < 256; ++tries) {
            pick = envIndex_->cast(origin, dir);
            if (!pick.hit || !section_.cuts(pick.point)) break;
            origin = pick.point + dir * std::max(1e-6, pick.distance * 1e-7);
            pick = Pick3D();
        }
    }
    pick_ = pick;
    remark();
    update();
    emit picked(pick_);
    return pick_;
}

void View3DCanvas::clearPick() {
    pick_ = Pick3D();
    marker_ = Mesh3D();
    update();
}

// THE MARK ON A PICKED POINT: a small block on it and a pin standing out along its face's normal.
void View3DCanvas::remark() {
    marker_ = Mesh3D();
    if (!pick_.hit) return;
    V3 lo, hi;
    double size = 2.0;
    if (env_ && env_->bounds(lo, hi)) size = std::max(1e-3, (hi - lo).length() * 0.006);
    const V3 p = pick_.point;
    marker_ = boxMesh(p - V3{size, size, size} * 0.6, p + V3{size, size, size} * 0.6);
    marker_.append(tubeMesh({p, p + pick_.normal * (size * 6.0)}, size * 0.25, 10, true));
}

void View3DCanvas::setWorkPlane(int axis, double at) {
    planeAxis_ = std::clamp(axis, -1, 2);
    planeAt_ = at;
    planeDirty_ = true;
    update();
}

bool View3DCanvas::surfaceAt(const QPointF& at, V3& point, V3& normal) {
    V3 origin, dir;
    cam_.rayAt(at.x(), at.y(), width(), height(), origin, dir);
    // ON THE WORK PLANE, where there is one - in front of the model or behind it, in the air or not: the plane is
    // where the point is meant to be. Its face looks back at the eye.
    if (planeAxis_ >= 0) {
        const double o = planeAxis_ == 0 ? origin.x : planeAxis_ == 1 ? origin.y : origin.z;
        const double d = planeAxis_ == 0 ? dir.x : planeAxis_ == 1 ? dir.y : dir.z;
        if (std::abs(d) < 1e-9) return false;
        const double t = (planeAt_ - o) / d;
        if (t <= 0.0) return false;
        point = origin + dir * t;
        normal = V3{planeAxis_ == 0 ? 1.0 : 0.0, planeAxis_ == 1 ? 1.0 : 0.0, planeAxis_ == 2 ? 1.0 : 0.0} * (d > 0.0 ? -1.0 : 1.0);
        return true;
    }
    if (env_ && envIndex_ && envShown_ && !envIndex_->empty()) {
        for (int tries = 0; tries < 256; ++tries) {
            const Pick3D p = envIndex_->cast(origin, dir);
            if (!p.hit) break;
            if (!section_.cuts(p.point)) {
                point = p.point;
                normal = p.normal;
                return true;
            }
            origin = p.point + dir * std::max(1e-6, p.distance * 1e-7);
        }
    }
    // NOTHING THERE: on the ground, the plane the drawing lies in.
    if (std::abs(dir.z) < 1e-9) return false;
    const double t = -origin.z / dir.z;
    if (t <= 0.0) return false;
    point = origin + dir * t;
    normal = V3{0.0, 0.0, origin.z > 0.0 ? 1.0 : -1.0};
    return true;
}

bool View3DCanvas::toScreen(const V3& p, QPointF& at) const {
    V3 right, up, forward;
    cam_.axes(right, up, forward);
    const V3 d = p - cam_.eye();
    const double z = dot3(d, forward);
    if (z < cam_.nearPlane()) return false;
    const double focal = 0.5 * std::max(1, height()) / std::tan(cam_.fovDeg * M_PI / 360.0);
    at = QPointF(width() * 0.5 + focal * dot3(d, right) / z, height() * 0.5 - focal * dot3(d, up) / z);
    return true;
}

// ---- The route in the picture ---------------------------------------------------------------------

void View3DCanvas::setRoute(Route3D* route) {
    route_ = route;
    cancelLaying();
    selSegment_.clear();
    selNode_.clear();
    routeRedraw();
}

void View3DCanvas::setRouteTool(RouteTool tool) {
    if (tool != RouteTool::Route) cancelLaying();
    tool_ = tool;
    setMouseTracking(tool == RouteTool::Route);
    setCursor(tool == RouteTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
}

void View3DCanvas::routeRedraw() {
    if (route_) {
        if (!selSegment_.isEmpty() && !route_->segment(selSegment_)) selSegment_.clear();
        if (!selNode_.isEmpty() && !route_->node(selNode_)) selNode_.clear();
        if (!layFrom_.isEmpty() && !route_->node(layFrom_)) cancelLaying();
    }
    routeDirty_ = true;
    update();
}

void View3DCanvas::setPartToPlace(const QString& brick, const QString& designator, RouteNodeKind kind) {
    placeBrick_ = brick;
    placePart_ = designator;
    placeKind_ = kind;
}

void View3DCanvas::selectSegment(const QString& id) {
    selSegment_ = id;
    selNode_.clear();
    routeRedraw();
}

V3 View3DCanvas::nodeShown(const RouteNode3D& n) const {
    if (!route_) return n.at;
    return n.kind == RouteNodeKind::Connector ? n.at + n.normal * (route_->lead * 0.5) : n.at + n.normal * (route_->diameter * 0.5);
}

void View3DCanvas::buildRouteMeshes() const {
    routeMeshes_.clear();
    routeDirty_ = false;
    if (!route_) return;
    const QColor lit(80, 220, 255);
    for (const RouteSegment3D& s : route_->segments) {
        const std::vector<V3> c = route_->centreline(s);
        if (c.size() < 2) continue;
        routeMeshes_.push_back({tubeMesh(c, route_->diameterOf(s) / 2.0, 14, true), s.id == selSegment_ ? lit : QColor(235, 140, 40)});
    }
    const double d = std::max(0.5, route_->diameter);
    for (const RouteNode3D& n : route_->nodes) {
        const bool on = n.id == selNode_ || n.id == layFrom_;
        if (n.kind == RouteNodeKind::Connector) {
            // A CONNECTOR: a housing standing out of the face the way it points, as far as the lead - wider one way than
            // the other, so how it is turned round its axis is seen - and a key on its top edge.
            const V3 ax = n.normal;
            V3 u = cross3(ax, V3{0, 0, 1});
            if (u.length() < 1e-6) u = cross3(ax, V3{1, 0, 0});
            u = u.normalized();
            V3 v = cross3(ax, u).normalized();
            const double s = n.spinDeg * M_PI / 180.0;
            const V3 su = u * std::cos(s) + v * std::sin(s), sv = v * std::cos(s) - u * std::sin(s);
            const double len = std::max(route_->lead, d), w = d * 2.2, h = d * 1.3;
            Mesh3D body = boxMesh({0, 0, 0}, {1, 1, 1});
            for (V3& q : body.v) q = n.at + ax * (q.x * len) + su * ((q.y - 0.5) * w) + sv * ((q.z - 0.5) * h);
            Mesh3D key = boxMesh({0, 0, 0}, {1, 1, 1});
            for (V3& q : key.v) q = n.at + ax * (q.x * len) + su * ((q.y - 0.5) * w * 0.3) + sv * (h * 0.5 + q.z * h * 0.25);
            body.append(key);
            routeMeshes_.push_back({body, on ? lit : QColor(205, 208, 214)});
        } else {
            const double h = n.kind == RouteNodeKind::Branch ? d * 0.6 : d * 0.5;
            const V3 c = nodeShown(n);
            routeMeshes_.push_back({boxMesh(c - V3{h, h, h}, c + V3{h, h, h}),
                                    on ? lit : n.kind == RouteNodeKind::Branch ? QColor(250, 210, 60) : QColor(70, 190, 110)});
        }
    }
}

std::vector<DrawItem3D> View3DCanvas::drawItems() const {
    std::vector<DrawItem3D> items;
    if (scene_ && harnessShown_)
        for (const Harness3DItem& it : scene_->items) {
            // PICKED: lit in the pane's highlight colour, the part still read through it.
            QColor c = it.color;
            if (!it.brick.isEmpty() && lit_.contains(it.brick))
                c = QColor((c.red() + 2 * 43) / 3, (c.green() + 2 * 208) / 3, (c.blue() + 2 * 255) / 3);
            items.push_back({&it.mesh, c});
        }
    if (env_ && envShown_)
        for (size_t i = 0; i < env_->parts.size(); ++i) {
            const EnvPart3D& part = env_->parts[i];
            QColor c = part.color.isValid() ? part.color : QColor(168, 174, 184);
            // The solid last picked, lit.
            if (pick_.hit && pick_.part == static_cast<int>(i))
                c = QColor((c.red() + 43) / 2, (c.green() + 208) / 2, (c.blue() + 255) / 2);
            items.push_back({&part.mesh, c, envAlpha_, true});
        }
    if (routeDirty_) buildRouteMeshes();
    for (const Drawn& d : routeMeshes_) items.push_back({&d.mesh, d.color});
    if (!layMesh_.empty()) items.push_back({&layMesh_, QColor(255, 255, 255), 0.75});
    if (!marker_.empty()) items.push_back({&marker_, QColor(255, 196, 40)});
    // THE WORK PLANE, see-through: over the model and a margin round it, so it is seen where a click will land.
    if (planeAxis_ >= 0) {
        if (planeDirty_) {
            planeDirty_ = false;
            planeMesh_ = Mesh3D();
            V3 lo{-100, -100, -100}, hi{100, 100, 100};
            bool any = false;
            V3 a, b;
            if (env_ && env_->bounds(a, b)) {
                lo = a;
                hi = b;
                any = true;
            }
            if (route_)
                for (const RouteNode3D& n : route_->nodes) {
                    lo = any ? V3{std::min(lo.x, n.at.x), std::min(lo.y, n.at.y), std::min(lo.z, n.at.z)} : n.at;
                    hi = any ? V3{std::max(hi.x, n.at.x), std::max(hi.y, n.at.y), std::max(hi.z, n.at.z)} : n.at;
                    any = true;
                }
            const V3 m = (hi - lo) * 0.15 + V3{10, 10, 10};
            lo = lo - m;
            hi = hi + m;
            double* lop = planeAxis_ == 0 ? &lo.x : planeAxis_ == 1 ? &lo.y : &lo.z;
            double* hip = planeAxis_ == 0 ? &hi.x : planeAxis_ == 1 ? &hi.y : &hi.z;
            *lop = planeAt_ - 0.05;
            *hip = planeAt_ + 0.05;
            planeMesh_ = boxMesh(lo, hi);
        }
        items.push_back({&planeMesh_, QColor(80, 170, 255), 0.22});
    }
    return items;
}

QString View3DCanvas::nodeAt(const QPointF& at) const {
    if (!route_) return QString();
    QString best;
    double nearest = 10.0;
    for (const RouteNode3D& n : route_->nodes) {
        QPointF p;
        if (!toScreen(nodeShown(n), p)) continue;
        const double d = std::hypot(p.x() - at.x(), p.y() - at.y());
        if (d < nearest) {
            nearest = d;
            best = n.id;
        }
    }
    return best;
}

QString View3DCanvas::segmentAt(const QPointF& at, V3* point, V3* normal, int* afterVia) const {
    if (!route_) return QString();
    QString best;
    double nearest = 8.0;
    for (const RouteSegment3D& s : route_->segments) {
        const std::vector<V3> c = route_->corners(s);
        const RouteNode3D* a = route_->node(s.a);
        const RouteNode3D* b = route_->node(s.b);
        if (!a || !b || c.size() < 2) continue;
        // The normal at each corner, as the corners are made: a connector's twice, a via's, the far node's.
        std::vector<V3> normals;
        if (a->kind == RouteNodeKind::Connector) normals.push_back(a->normal);
        normals.push_back(a->normal);
        for (const RouteVia3D& v : s.via) normals.push_back(v.normal);
        normals.push_back(b->normal);
        if (b->kind == RouteNodeKind::Connector) normals.push_back(b->normal);
        const int offset = a->kind == RouteNodeKind::Connector ? 2 : 1;
        for (size_t k = 0; k + 1 < c.size(); ++k) {
            QPointF p, q;
            if (!toScreen(c[k], p) || !toScreen(c[k + 1], q)) continue;
            const QPointF e = q - p;
            const double ll = e.x() * e.x() + e.y() * e.y();
            const double t = ll < 1e-9 ? 0.0 : std::clamp(((at.x() - p.x()) * e.x() + (at.y() - p.y()) * e.y()) / ll, 0.0, 1.0);
            const double d = std::hypot(p.x() + e.x() * t - at.x(), p.y() + e.y() * t - at.y());
            if (d >= nearest) continue;
            nearest = d;
            best = s.id;
            const V3 n = normals[std::min(t < 0.5 ? k : k + 1, normals.size() - 1)];
            const V3 mid = c[k] + (c[k + 1] - c[k]) * t;
            if (point) *point = mid - n * (route_->diameterOf(s) / 2.0);
            if (normal) *normal = n;
            if (afterVia) *afterVia = std::clamp(static_cast<int>(k) + 1 - offset, 0, static_cast<int>(s.via.size()));
        }
    }
    return best;
}

void View3DCanvas::cancelLaying() {
    layFrom_.clear();
    layVia_.clear();
    layMesh_ = Mesh3D();
    hoverOk_ = false;
    routeDirty_ = true;
    update();
}

void View3DCanvas::finishLaying(const QString& endNode) {
    emit routeAboutToChange();
    const QString id = route_->addSegment(layFrom_, endNode, layVia_);
    layFrom_.clear();
    layVia_.clear();
    layMesh_ = Mesh3D();
    selSegment_ = id;
    selNode_.clear();
    routeDirty_ = true;
    emit routeChanged();
    emit selectionChanged(selSegment_);
}

void View3DCanvas::routeClick(const QPointF& at) {
    if (!route_) return;
    const QString node = nodeAt(at);
    V3 p, n;
    switch (tool_) {
    case RouteTool::Select:
        break;
    case RouteTool::Connector:
        if (!node.isEmpty()) {
            selNode_ = node;
            selSegment_.clear();
            routeRedraw();
            emit selectionChanged(selSegment_);
        } else if (surfaceAt(at, p, n)) {
            emit routeAboutToChange();
            // A HARNESS PART BEING PUT: there, or moved there where it was put already.
            RouteNode3D* put = nullptr;
            for (RouteNode3D& o : route_->nodes)
                if (!placeBrick_.isEmpty() && o.brick == placeBrick_) put = &o;
            if (put) {
                put->at = p;
                put->normal = n.normalized();
                put->face = put->normal;
                selNode_ = put->id;
            } else {
                selNode_ = route_->addNode(placeBrick_.isEmpty() ? RouteNodeKind::Connector : placeKind_, p, n);
                if (!placeBrick_.isEmpty()) {
                    RouteNode3D* made = route_->node(selNode_);
                    made->brick = placeBrick_;
                    made->part = placePart_;
                    made->label = placePart_;
                }
            }
            selSegment_.clear();
            routeDirty_ = true;
            emit routeChanged();
        }
        break;
    case RouteTool::Branch:
    case RouteTool::Clip: {
        const RouteNodeKind kind = tool_ == RouteTool::Branch ? RouteNodeKind::Branch : RouteNodeKind::Clip;
        int after = 0;
        if (!node.isEmpty()) {
            selNode_ = node;
            routeRedraw();
        } else if (const QString seg = segmentAt(at, &p, &n, &after); !seg.isEmpty()) {
            // ON A BUNDLE: it is divided there.
            emit routeAboutToChange();
            selNode_ = route_->splitSegment(seg, p, n, after, kind);
            selSegment_.clear();
            routeDirty_ = true;
            emit routeChanged();
        } else if (surfaceAt(at, p, n)) {
            emit routeAboutToChange();
            selNode_ = route_->addNode(kind, p, n);
            selSegment_.clear();
            routeDirty_ = true;
            emit routeChanged();
        }
        break;
    }
    case RouteTool::Route:
        if (layFrom_.isEmpty()) {
            if (!node.isEmpty()) {
                layFrom_ = node;
                routeDirty_ = true;
            } else if (surfaceAt(at, p, n)) {
                // FROM A FACE: a new branch there to start from.
                emit routeAboutToChange();
                layFrom_ = route_->addNode(RouteNodeKind::Branch, p, n);
                routeDirty_ = true;
                emit routeChanged();
            }
        } else if (!node.isEmpty() && node != layFrom_) {
            finishLaying(node);
        } else if (node.isEmpty() && surfaceAt(at, p, n)) {
            layVia_.push_back({p, n});
        }
        update();
        break;
    case RouteTool::Delete:
        if (!node.isEmpty()) {
            emit routeAboutToChange();
            route_->removeNode(node);
            routeDirty_ = true;
            emit routeChanged();
        } else if (const QString seg = segmentAt(at); !seg.isEmpty()) {
            emit routeAboutToChange();
            route_->removeSegment(seg);
            routeDirty_ = true;
            emit routeChanged();
        }
        break;
    }
}

QImage View3DCanvas::render(const QSize& size) const {
    QImage img(size.expandedTo(QSize(1, 1)), QImage::Format_RGB32);
    const std::vector<DrawItem3D> items = drawItems();
    const QColor background(30, 36, 44);
    if (useGpu_) {
        if (!gl_) gl_ = std::make_unique<GlRenderer3D>();
        if (gl_->render(img, items, cam_, wire_, background, section_)) {
            lastOnGpu_ = true;
            return img;
        }
    }
    lastOnGpu_ = false;
    renderScene3D(img, items, cam_, wire_, background, section_);
    return img;
}

void View3DCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.drawImage(0, 0, render(size()));
    p.setRenderHint(QPainter::Antialiasing, true);
    // THE ROUTE'S NAMES AND LENGTHS, over the picture: each node's label, each bundle's length at its middle.
    if (route_) {
        QFont f = p.font();
        f.setPointSizeF(std::max(7.0, f.pointSizeF() * 0.9));
        p.setFont(f);
        for (const RouteNode3D& n : route_->nodes) {
            QPointF at;
            if (!toScreen(nodeShown(n), at)) continue;
            p.setPen(QColor(255, 255, 255));
            p.drawText(at + QPointF(8, -6), n.label);
        }
        for (const RouteSegment3D& s : route_->segments) {
            const std::vector<V3> c = route_->centreline(s);
            if (c.size() < 2) continue;
            // At the middle of its length.
            double total = 0.0;
            for (size_t i = 1; i < c.size(); ++i) total += (c[i] - c[i - 1]).length();
            double gone = 0.0;
            V3 mid = c.front();
            for (size_t i = 1; i < c.size(); ++i) {
                const double l = (c[i] - c[i - 1]).length();
                if (gone + l >= total / 2.0 && l > 0.0) {
                    mid = c[i - 1] + (c[i] - c[i - 1]) * ((total / 2.0 - gone) / l);
                    break;
                }
                gone += l;
            }
            QPointF at;
            if (!toScreen(mid, at)) continue;
            const QString text = QStringLiteral("%1 mm").arg(total, 0, 'f', total < 100.0 ? 1 : 0);
            const QRectF box = QRectF(at + QPointF(6, 4), QSizeF(p.fontMetrics().horizontalAdvance(text) + 8, p.fontMetrics().height() + 2));
            p.fillRect(box, QColor(20, 24, 30, 190));
            p.setPen(s.id == selSegment_ ? QColor(80, 220, 255) : QColor(255, 200, 140));
            p.drawText(box, Qt::AlignCenter, text);
        }
        if (!layFrom_.isEmpty()) {
            const RouteNode3D* from = route_->node(layFrom_);
            p.setPen(QColor(255, 255, 255));
            p.drawText(QPointF(10, 18), QStringLiteral("Laying a bundle from %1 - click the faces it runs over, then the node it "
                                                       "ends at (or double-click for a new branch); Esc gives up")
                                            .arg(from ? from->label : QString()));
        }
    }
    // THE AXES, small in the corner: which way is which as it is turned.
    V3 right, up, forward;
    cam_.axes(right, up, forward);
    const QPointF o(34, height() - 34);
    const struct {
        V3 d;
        QColor c;
        const char* n;
    } ax[3] = {{{1, 0, 0}, QColor(230, 90, 90), "X"}, {{0, 1, 0}, QColor(90, 210, 110), "Y"}, {{0, 0, 1}, QColor(90, 140, 240), "Z"}};
    for (const auto& a : ax) {
        const QPointF tip = o + QPointF(dot3(a.d, right), -dot3(a.d, up)) * 22.0;
        p.setPen(QPen(a.c, 2.0));
        p.drawLine(o, tip);
        p.drawText(tip + QPointF(3, 4), QString::fromLatin1(a.n));
    }
}

void View3DCanvas::mousePressEvent(QMouseEvent* e) {
    dragging_ = e->button();
    last_ = e->position().toPoint();
    pressedAt_ = last_;
    moved_ = false;
}

void View3DCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (dragging_ == Qt::NoButton) {
        // LAYING A BUNDLE: where it would go next, under the pointer.
        if (route_ && !layFrom_.isEmpty()) {
            layMesh_ = Mesh3D();
            const RouteNode3D* from = route_->node(layFrom_);
            V3 p, n;
            if (from && surfaceAt(e->position(), p, n)) {
                const double r = route_->diameter / 2.0;
                std::vector<V3> path{nodeShown(*from)};
                for (const RouteVia3D& v : layVia_) path.push_back(v.at + v.normal * r);
                path.push_back(p + n * r);
                layMesh_ = tubeMesh(path, std::max(0.2, r * 0.5), 10, false);
            }
            update();
        }
        return;
    }
    const QPoint now = e->position().toPoint();
    if ((now - pressedAt_).manhattanLength() > 3) moved_ = true;
    if (!moved_) return;
    const QPoint d = now - last_;
    last_ = now;
    if (dragging_ == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier)) {
        // ROLLED round the way it looks.
        cam_.rollDeg = wrapDegrees(cam_.rollDeg + d.x() * 0.4);
    } else if (dragging_ == Qt::LeftButton) {
        // TURNED: round the upright, and up and over - all the way round, upside down past the top. Upside down, a drag
        // to the right still turns the picture to the right.
        const double over = std::cos(cam_.pitchDeg * M_PI / 180.0) < 0.0 ? -1.0 : 1.0;
        cam_.yawDeg = wrapDegrees(cam_.yawDeg - d.x() * 0.4 * over);
        cam_.pitchDeg = wrapDegrees(cam_.pitchDeg + d.y() * 0.4);
    } else {
        // MOVED: the point it looks at, across the picture as far as the pointer went.
        V3 right, up, forward;
        cam_.axes(right, up, forward);
        const double perPixel = 2.0 * cam_.distance * std::tan(cam_.fovDeg * M_PI / 360.0) / std::max(1, height());
        cam_.target = cam_.target - right * (d.x() * perPixel) + up * (d.y() * perPixel);
    }
    update();
    emit cameraChanged();
}

void View3DCanvas::mouseReleaseEvent(QMouseEvent* e) {
    const bool click = dragging_ == Qt::LeftButton && !moved_ && e->button() == Qt::LeftButton;
    dragging_ = Qt::NoButton;
    if (swallowRelease_) {
        swallowRelease_ = false;
        return;
    }
    if (!click) return;
    // A CLICK, NOT A DRAG: the route tool's, or - selecting - a node or bundle, else the point of the environment under
    // the pointer.
    if (route_ && tool_ != RouteTool::Select) {
        routeClick(e->position());
        return;
    }
    if (route_) {
        const QString node = nodeAt(e->position());
        const QString seg = node.isEmpty() ? segmentAt(e->position()) : QString();
        if (!node.isEmpty() || !seg.isEmpty()) {
            selNode_ = node;
            selSegment_ = seg;
            routeRedraw();
            emit selectionChanged(selSegment_);
            return;
        }
        if (!selNode_.isEmpty() || !selSegment_.isEmpty()) {
            selNode_.clear();
            selSegment_.clear();
            routeRedraw();
            emit selectionChanged(QString());
        }
    }
    if (env_) pickAt(e->position());
}

void View3DCanvas::mouseDoubleClickEvent(QMouseEvent* e) {
    if (route_ && tool_ == RouteTool::Route) {
        swallowRelease_ = true;
        // ENDED AT A NEW BRANCH where it was double-clicked - the point the double-click's first click laid is that
        // branch, not a point on the way to it.
        V3 p, n;
        if (!layFrom_.isEmpty() && nodeAt(e->position()).isEmpty() && surfaceAt(e->position(), p, n)) {
            if (!layVia_.empty() && (layVia_.back().at - p).length() < 1e-6 + route_->diameter) layVia_.pop_back();
            emit routeAboutToChange();
            const QString end = route_->addNode(RouteNodeKind::Branch, p, n);
            const QString id = route_->addSegment(layFrom_, end, layVia_);
            layFrom_.clear();
            layVia_.clear();
            layMesh_ = Mesh3D();
            selSegment_ = id;
            routeDirty_ = true;
            emit routeChanged();
            emit selectionChanged(selSegment_);
        }
        return;
    }
    if (route_ && tool_ != RouteTool::Select) {
        swallowRelease_ = true;
        return;
    }
    fit();
}

void View3DCanvas::keyPressEvent(QKeyEvent* e) {
    if (e->matches(QKeySequence::Undo)) {
        emit undoRequested();
        return;
    }
    if (!route_) {
        QWidget::keyPressEvent(e);
        return;
    }
    if (e->key() == Qt::Key_Escape && !layFrom_.isEmpty()) {
        cancelLaying();
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        emit escapePressed();
        return;
    }
    // R: THE CONNECTOR SELECTED A QUARTER TURN round its own axis.
    if (e->key() == Qt::Key_R && !selNode_.isEmpty()) {
        const RouteNode3D* n = route_->node(selNode_);
        if (n && n->kind == RouteNodeKind::Connector) {
            emit routeAboutToChange();
            route_->aimNode(selNode_, n->normal, wrapDegrees(n->spinDeg + 90.0));
            routeDirty_ = true;
            emit routeChanged();
            return;
        }
    }
    if (e->key() == Qt::Key_Backspace && !layFrom_.isEmpty()) {
        if (!layVia_.empty()) layVia_.pop_back();
        layMesh_ = Mesh3D();
        update();
        return;
    }
    if (e->key() == Qt::Key_Delete && (!selNode_.isEmpty() || !selSegment_.isEmpty())) {
        emit routeAboutToChange();
        if (!selNode_.isEmpty())
            route_->removeNode(selNode_);
        else
            route_->removeSegment(selSegment_);
        selNode_.clear();
        selSegment_.clear();
        routeDirty_ = true;
        emit routeChanged();
        return;
    }
    QWidget::keyPressEvent(e);
}

void View3DCanvas::wheelEvent(QWheelEvent* e) {
    // ZOOMED: nearer or further, a step a notch.
    const double notches = e->angleDelta().y() / 120.0;
    cam_.distance = std::clamp(cam_.distance * std::pow(0.85, notches), 1.0, 1e7);
    update();
}

} // namespace Edraw_Desktop
