#include "app/View3DPane.h"

#include "app/Icons3D.h"
#include "app/View3DHelpDialog.h"
#include "app/NavWheel.h"
#include "app/Chrome.h"

#include <QCursor>
#include <QMenu>

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
#include <QActionGroup>
#include <QColorDialog>
#include <QInputDialog>
#include <QContextMenuEvent>
#include <limits>
#include <QMenu>
#include <QMouseEvent>
#include <QTreeWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QPainter>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QSplitter>
#include <QSpinBox>
#include <QTableWidget>
#include <QThread>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QElapsedTimer>
#include <QDateTime>
#include <QDialog>
#include <QPixmap>
#include <atomic>
#include <mutex>
#include <QTimer>
#include <QCloseEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

namespace {

// THE "A" BESIDE A FIELD that can be Auto: pressed in, the field shows the real figure Auto comes to, and is not typed in.
QToolButton* autoButton(QWidget* parent, const char* name, const QString& tip) {
    auto* b = new QToolButton(parent);
    b->setObjectName(QLatin1String(name));
    b->setText(QStringLiteral("A"));
    b->setCheckable(true);
    b->setToolTip(tip);
    b->setFixedWidth(22);
    b->setStyleSheet(QStringLiteral("QToolButton{font-weight:bold;color:#e6ecf4;background:#3a4656;border:1px solid #5a6878;border-radius:3px;}"
                                    "QToolButton:checked{background:#2f855a;border-color:#3fa36f;color:white;}"));
    return b;
}

// A WIRE'S COLOUR IN TURN, where it is given none: red, blue, green, yellow, black, white, brown, orange, violet, grey.
QColor wireColourOf(int w) {
    static const QColor palette[] = {QColor(200, 40, 40), QColor(40, 90, 200), QColor(40, 150, 60), QColor(230, 200, 40),
                                     QColor(30, 30, 34), QColor(235, 235, 235), QColor(150, 80, 30), QColor(240, 130, 30),
                                     QColor(140, 60, 170), QColor(120, 120, 130)};
    return palette[static_cast<size_t>(std::max(0, w)) % std::size(palette)];
}

} // namespace

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

// THE GROUPS OF CONTROLS SIDE BY SIDE, on as few lines as the width allows: the next group on the same line while it
// fits, on the next line where it does not - so the strip is one or two lines deep in a wide pane, more only where it
// is narrow. Each group kept whole, never squeezed.
class FlowLayout : public QLayout {
public:
    FlowLayout(QWidget* parent, int hGap, int vGap) : QLayout(parent), hGap_(hGap), vGap_(vGap) { setContentsMargins(0, 0, 0, 0); }
    ~FlowLayout() override {
        while (QLayoutItem* it = takeAt(0)) delete it;
    }
    void addItem(QLayoutItem* item) override { items_.append(item); }
    int count() const override { return static_cast<int>(items_.size()); }
    QLayoutItem* itemAt(int i) const override { return i >= 0 && i < items_.size() ? items_.at(i) : nullptr; }
    QLayoutItem* takeAt(int i) override { return i >= 0 && i < items_.size() ? items_.takeAt(i) : nullptr; }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return place(QRect(0, 0, width, 0), false); }
    void setGeometry(const QRect& rect) override {
        QLayout::setGeometry(rect);
        place(rect, true);
    }
    QSize sizeHint() const override { return minimumSize(); }
    // As wide as its widest group; as tall as one line.
    QSize minimumSize() const override {
        QSize s;
        for (QLayoutItem* it : items_)
            if (!it->isEmpty()) s = s.expandedTo(it->sizeHint());
        return s;
    }

private:
    int place(const QRect& rect, bool apply) const {
        int x = rect.x(), y = rect.y(), line = 0;
        for (QLayoutItem* it : items_) {
            if (it->isEmpty()) continue;
            const QSize h = it->sizeHint();
            if (x > rect.x() && x + h.width() > rect.right() + 1) {
                x = rect.x();
                y += line + vGap_;
                line = 0;
            }
            if (apply) it->setGeometry(QRect(QPoint(x, y), h));
            x += h.width() + hGap_;
            line = std::max(line, h.height());
        }
        return y + line - rect.y();
    }
    QList<QLayoutItem*> items_;
    int hGap_, vGap_;
};
} // namespace

View3DPane::View3DPane(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("view3d"));
    auto* lay = new QVBoxLayout(this);
    // THE ROWS OF CONTROLS, side to side in a strip of their own: never squeezed - each button its picture and its name,
    // each box as wide as what is in it - and the strip scrolled across where the pane is narrower than they are.
    rowsArea_ = new QScrollArea(this);
    rowsArea_->setObjectName(QStringLiteral("view3dRows"));
    rowsArea_->setFrameShape(QFrame::NoFrame);
    rowsArea_->setWidgetResizable(true);
    rowsArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    rowsArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rowsArea_->horizontalScrollBar()->setStyleSheet(QStringLiteral(
        "QScrollBar:horizontal{height:8px;background:transparent;border:none;}"
        "QScrollBar::handle:horizontal{background:#6b7480;border-radius:3px;min-width:40px;}"
        "QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;}"
        "QScrollBar::add-page:horizontal,QScrollBar::sub-page:horizontal{background:transparent;}"));
    rowsBox_ = new QWidget(rowsArea_);
    rowsLay_ = new FlowLayout(rowsBox_, 14, 3);
    rowsArea_->setWidget(rowsBox_);
    rowsArea_->installEventFilter(this);
    lay->addWidget(rowsArea_);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);
    topRow_ = new QWidget(this);
    auto* bar = new QHBoxLayout(topRow_);
    bar->setContentsMargins(0, 0, 0, 0);
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
    button(bar, "view3dFlip", QStringLiteral("Flip"), QStringLiteral("Look from straight opposite - the other side, underneath"),
           [this] { canvas_->flip(); });
    auto* wire = new QCheckBox(QStringLiteral("Wireframe"), this);
    wire->setObjectName(QStringLiteral("view3dWireframe"));
    connect(wire, &QCheckBox::toggled, this, [this](bool on) { canvas_->setWireframe(on); });
    bar->addWidget(wire);
    // THE VIEW'S THREE TURNS, in degrees, each all the way round: round the upright, up and over, and round the way it
    // looks. The mouse does the same - a left drag turns and tilts, with Shift it rolls.
    // On a row of their own words, "View:" - the top row has room for no more.
    viewBits_ = new QWidget(this);
    auto* viewRow = new QHBoxLayout(viewBits_);
    viewRow->setContentsMargins(12, 0, 0, 0);
    viewRow->setSpacing(3);
    viewRow->addWidget(new QLabel(QStringLiteral("View:"), viewBits_));
    auto angle = [&](const char* name, const QString& label, const QString& tip) {
        viewRow->addWidget(new QLabel(label, viewBits_));
        auto* s = new AngleSpinBox(this);
        s->setObjectName(QLatin1String(name));
        s->setRange(-179, 180);   // -180 is 180: one stop for the one angle, so a step round wraps exactly
        s->setWrapping(true);
        s->setSingleStep(15);
        s->setSuffix(QStringLiteral("°"));
        s->setToolTip(tip);
        s->setKeyboardTracking(false);
        viewRow->addWidget(s);
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
                      QStringLiteral("The 3D view in a window of its own, filling the screen - drag it, size it, put it on "
                                     "another screen; all its settings go with it (F11, Back, or closing it, brings it back)"),
                      [this] { setFullScreen(!fullScreen_); });
    fullBtn_->setCheckable(true);
    // LOCKED: the view held still, so a drag moves a cable, a corner or a connector across it.
    lockBtn_ = button(bar, "view3dLock", QStringLiteral("Lock view"),
                      QStringLiteral("Hold the view still: the mouse no longer turns, moves or zooms it - drag a cable, a "
                                     "corner or a connector to move it across the view, at the depth it is"),
                      [this] { setViewLocked(!canvas_->viewLocked()); });
    lockBtn_->setCheckable(true);
    // BELOW THE DRAWING (or floating): to the left or the right of the window, the picture alone there.
    toLeft_ = button(bar, "view3dToLeft", QStringLiteral("To the left"),
                     QStringLiteral("Put the 3D view at the left of the window - the picture alone, without its options"),
                     [this] { emit dockToSideRequested(true); });
    toRight_ = button(bar, "view3dToRight", QStringLiteral("To the right"),
                      QStringLiteral("Put the 3D view at the right of the window - the picture alone, without its options"),
                      [this] { emit dockToSideRequested(false); });
    for (QToolButton* b : {toRight_, toLeft_}) {
        b->hide();
        bar->removeWidget(b);
        bar->insertWidget(0, b);
    }
    wheelShown_ = new QCheckBox(QStringLiteral("Wheel"), this);
    wheelShown_->setObjectName(QStringLiteral("view3dWheelShown"));
    wheelShown_->setChecked(true);
    wheelShown_->setToolTip(QStringLiteral("The navigation wheel in the corner of the picture: turn, tilt, roll, zoom, fit, lock"));
    bar->addWidget(wheelShown_);
    auto* f11 = new QShortcut(QKeySequence(Qt::Key_F11), this);
    f11->setContext(Qt::WidgetWithChildrenShortcut);
    connect(f11, &QShortcut::activated, this, [this] { setFullScreen(!fullScreen_); });
    rowsLay_->addWidget(topRow_);

    // THE ENVIRONMENT'S ROW: the model the harness is routed in, how it shows, and a section through it.
    envRow_ = new QWidget(this);
    auto* env = new QHBoxLayout(envRow_);
    env->setContentsMargins(0, 0, 0, 0);
    env->setSpacing(3);
    env->addWidget(new QLabel(QStringLiteral("Environment:"), envRow_));
    envLoad_ = button(env, "view3dEnvLoad", QStringLiteral("Load..."),
                      QStringLiteral("Load the CAD model the harness is routed in - a chassis, a panel, an enclosure - from "
                                     "a STEP, STL or OBJ file"),
                      [this] { chooseEnvironment(); });
    envClear_ = button(env, "view3dEnvClear", QStringLiteral("Clear"), QStringLiteral("Take the environment away"),
                       [this] { clearEnvironment(); });
    button(env, "view3dParts", QStringLiteral("Parts..."), QStringLiteral("The model's solids, each to show or hide - a solid hidden is "
                                                                     "not drawn, and clicks and bundles go through it"),
           [this] { showPartsList(); });
    envProgress_ = new QProgressBar(envRow_);
    envProgress_->setObjectName(QStringLiteral("view3dEnvProgress"));
    envProgress_->setRange(0, 100);
    envProgress_->setMinimumWidth(260);
    envProgress_->setTextVisible(true);
    envProgress_->hide();
    env->addWidget(envProgress_);
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
    rowsLay_->addWidget(envRow_);

    // THE ROUTE'S ROW: what a click does, and the bundles' diameter, bend radius and lead out of a connector.
    // TWO GROUPS, each kept whole in the strip: placing and laying; changing and measuring.
    routeRow_ = new QWidget(this);
    auto* rr = new QHBoxLayout(routeRow_);
    rr->setContentsMargins(0, 0, 0, 0);
    rr->setSpacing(3);
    editRow_ = new QWidget(this);
    editRow_->setObjectName(QStringLiteral("view3dEditRow"));
    auto* rr2 = new QHBoxLayout(editRow_);
    rr2->setContentsMargins(0, 0, 0, 0);
    rr2->setSpacing(3);
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
        {RouteTool::Route, "view3dToolRoute", "Route", "Lay a bundle: click the node it starts from (or a face, for a new "
                                                       "branch), the faces it runs over, and the node it ends at - or "
                                                       "double-click to end at a new branch. Esc: give up; Backspace: the "
                                                       "last point back"},
        {RouteTool::Branch, "view3dToolBranch", "Branch", "Put a branch point down - on a bundle, dividing it, or on a face; "
                                                          "on a connector, a bundle started from its front or its back"},
        {RouteTool::Clip, "view3dToolClip", "Clip", "Put a clip down - on a bundle, holding it there, or on a face"},
        {RouteTool::Move, "view3dToolMove", "Move", "Drag a node - a connector, a part, a branch, a clip - to somewhere else "
                                                    "on the model; its bundles follow from where they were laid"},
        {RouteTool::Stretch, "view3dToolStretch", "Stretch", "Take hold of a connector and pull: it stays where it is, and its "
                                                             "cable is pulled tight through it - as far as the pointer goes, "
                                                             "up to straight - and what comes through is cut off, the cable "
                                                             "that much shorter; a clip or a branch at its other end holds it"},
        {RouteTool::Delete, "view3dToolDelete", "Delete", "Take away the node or bundle clicked (a node takes its bundles "
                                                          "with it)"},
        {RouteTool::Measure, "view3dToolMeasure", "Measure", "A ruler: click one end, then the other - the distance straight, "
                                                             "and along X, Y and Z and across each plane (Esc takes it away)"},
    };
    for (const auto& t : tools) {
        const bool editing = t.tool == RouteTool::Move || t.tool == RouteTool::Stretch || t.tool == RouteTool::Delete || t.tool == RouteTool::Measure;
        auto* b = new QToolButton(editing ? editRow_ : routeRow_);
        b->setObjectName(QLatin1String(t.name));
        b->setText(QString::fromLatin1(t.text));
        b->setToolTip(QString::fromLatin1(t.tip));
        b->setCheckable(true);
        b->setAutoRaise(true);
        tools_->addButton(b, static_cast<int>(t.tool));
        // Select, Connector, Route, Branch, Clip; then Move, Stretch, Delete, Measure.
        (editing ? rr2 : rr)->addWidget(b);
    }
    tools_->button(static_cast<int>(RouteTool::Select))->setChecked(true);
    // HOW THE RULER MEASURES: point to point, or square from the first face to the second.
    auto* measureMode = new QComboBox(editRow_);
    measureMode->setObjectName(QStringLiteral("view3dMeasureMode"));
    measureMode->addItems({QStringLiteral("Point to point"), QStringLiteral("Surface to surface")});
    measureMode->setToolTip(QStringLiteral("Point to point: straight between the two points clicked. Surface to surface: from the "
                                           "second face square back to the first - the gap between two faces"));
    connect(measureMode, &QComboBox::currentIndexChanged, this, [this](int i) { canvas_->setMeasureFaces(i == 1); });
    rr2->addWidget(measureMode);
    connect(tools_, &QButtonGroup::idClicked, this, [this](int id) {
        canvas_->setRouteTool(static_cast<RouteTool>(id));
        emit routeToolChanged(static_cast<RouteTool>(id));
    });
    // THE BUNDLES' SIZES, on a row of their own - the tools before it want the room for their names.
    bundleRow_ = new QWidget(this);
    auto* br = new QHBoxLayout(bundleRow_);
    br->setContentsMargins(0, 0, 0, 0);
    br->setSpacing(3);
    br->addWidget(new QLabel(QStringLiteral("Bundle:"), bundleRow_));
    auto spin = [&](const char* name, const QString& label, double lo, double hi, double value, const QString& tip) {
        auto* text = new QLabel(label, bundleRow_);
        br->addWidget(text);
        auto* s = new QDoubleSpinBox(bundleRow_);
        s->setObjectName(QLatin1String(name));
        s->setRange(lo, hi);
        s->setDecimals(1);
        s->setValue(value);
        s->setSuffix(QStringLiteral(" mm"));
        s->setToolTip(tip);
        s->setKeyboardTracking(false);
        br->addWidget(s);
        return s;
    };
    diameter_ = spin("view3dRouteDiameter", QStringLiteral("Ø"), 0.5, 200.0, route_.diameter,
                     QStringLiteral("A bundle's diameter, where it is given none of its own (the table sets one bundle's)"));
    // AUTO, BY ITS "A": the diameter the wires below pack to - the field showing it. Off: a plain bundle of the Ø typed.
    diameterAuto_ = autoButton(bundleRow_, "view3dRouteDiameterAuto",
                               QStringLiteral("Auto: a bundle of wires - as thick as its wires pack to; the field shows it"));
    br->addWidget(diameterAuto_);
    connect(diameterAuto_, &QToolButton::toggled, this, [this](bool on) {
        if (filling_) return;
        setWires(QString(), on ? std::max(2, wires_->value() > 0 ? wires_->value() : 7) : 0, wireD_->value());
    });
    // A BUNDLE OF WIRES: how many, how thick each - drawn wire by wire, its diameter what they pack to.
    br->addWidget(new QLabel(QStringLiteral("Wires"), bundleRow_));
    wires_ = new QSpinBox(bundleRow_);
    wires_->setObjectName(QStringLiteral("view3dRouteWires"));
    wires_->setRange(0, 999);
    wires_->setKeyboardTracking(false);
    wires_->setToolTip(QStringLiteral("A bundle of so many wires, drawn wire by wire, as thick as they pack to (Ø's A on); 0: a "
                                      "plain bundle of the Ø typed. One cable's own: right-click it, Wires in it..."));
    br->addWidget(wires_);
    br->addWidget(new QLabel(QStringLiteral("Wire Ø"), bundleRow_));
    wireD_ = new QDoubleSpinBox(bundleRow_);
    wireD_->setObjectName(QStringLiteral("view3dRouteWireD"));
    wireD_->setRange(0.05, 50.0);
    wireD_->setDecimals(2);
    wireD_->setSingleStep(0.1);
    wireD_->setValue(route_.wireD);
    wireD_->setSuffix(QStringLiteral(" mm"));
    wireD_->setKeyboardTracking(false);
    wireD_->setToolTip(QStringLiteral("Each wire's diameter, over its insulation"));
    br->addWidget(wireD_);
    connect(wires_, &QSpinBox::valueChanged, this, [this](int n) {
        if (!filling_) setWires(QString(), n, wireD_->value());
    });
    connect(wireD_, &QDoubleSpinBox::valueChanged, this, [this](double d) {
        if (!filling_) setWires(QString(), wires_->value(), d);
    });
    bend_ = spin("view3dRouteBend", QStringLiteral("Bend R"), 0.0, 2000.0, route_.bendRadius,
                 QStringLiteral("The tightest a bundle may be bent - Auto: six times its diameter"));
    // AUTO, BY ITS "A": six times the bundle's diameter - the field showing that, and not to be typed in while it is on.
    bendAuto_ = autoButton(bundleRow_, "view3dRouteBendAuto", QStringLiteral("Auto: six times the bundle's diameter - the field shows it"));
    br->addWidget(bendAuto_);
    connect(bendAuto_, &QToolButton::toggled, this, [this](bool on) {
        if (filling_) return;
        beforeRouteChange();
        route_.bendRadius = on ? 0.0 : std::max(0.1, route_.diameter * 6.0);
        saveRouting();
        refreshRoute();
        syncBend();
        emit routeChanged();
    });
    lead_ = spin("view3dRouteLead", QStringLiteral("Lead"), 0.0, 500.0, route_.lead,
                 QStringLiteral("How far a bundle comes straight out of a connector before it may turn"));
    auto settings = [this] {
        if (filling_) return;
        beforeRouteChange();
        // While the wires make it, the field shows what they pack to - not a diameter of its own.
        if (route_.wires <= 0) route_.diameter = diameter_->value();
        route_.bendRadius = bendAuto_->isChecked() ? 0.0 : bend_->value();
        route_.lead = lead_->value();
        saveRouting();
        refreshRoute();
        syncBend();
        syncConnector();
        emit routeChanged();
    };
    for (QDoubleSpinBox* s : {diameter_, bend_, lead_}) connect(s, &QDoubleSpinBox::valueChanged, this, settings);
    // HOW TAUT A BUNDLE IS PUT IN - the route's here; the cable selected its own, beside it and in the table.
    br->addWidget(new QLabel(QStringLiteral("Cable tension"), bundleRow_));
    tension_ = new QComboBox(bundleRow_);
    tension_->setObjectName(QStringLiteral("view3dTension"));
    for (const CableTension& t : cableTensions()) tension_->addItem(t.name, t.newtons);
    tension_->setToolTip(QStringLiteral("How taut a bundle is put in, where the table gives it none of its own - the less, the "
                                        "more it sags between where it is held; Slack: it hangs by its own length"));
    connect(tension_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (filling_ || i < 0) return;
        beforeRouteChange();
        route_.tension = tension_->itemData(i).toDouble();
        saveRouting();
        refreshRoute();
        emit routeChanged();
    });
    br->addWidget(tension_);
    // ONE CABLE'S OWN TENSION: in the Cable box on the right, with the rest of what is its own.
    cableTension_ = new QComboBox(bundleRow_);
    cableTension_->setObjectName(QStringLiteral("view3dCableTension"));
    cableTension_->addItem(QStringLiteral("As all cables"), -1.0);
    for (const CableTension& t : cableTensions()) cableTension_->addItem(t.name, t.newtons);
    cableTension_->setToolTip(QStringLiteral("The tension of the cable selected - select a cable (click it), then choose; Route's: "
                                             "the route's Tension"));
    cableTension_->setEnabled(false);
    connect(cableTension_, &QComboBox::currentIndexChanged, this, [this](int i) {
        const QString id = canvas_->selectedSegment();
        if (filling_ || i < 0 || !route_.segment(id)) return;
        beforeRouteChange();
        route_.segment(id)->tension = cableTension_->itemData(i).toDouble();
        saveRouting();
        refreshRoute();
        emit routeChanged();
    });
    // GRAVITY: the route hung under its weight, each bundle by its tension, and where it would fall shown.
    gravity_ = new QToolButton(bundleRow_);
    gravity_->setObjectName(QStringLiteral("view3dGravity"));
    gravity_->setText(QStringLiteral("Gravity"));
    gravity_->setCheckable(true);
    gravity_->setAutoRaise(true);
    gravity_->setToolTip(QStringLiteral("Simulation preview: each bundle hung under its own weight, held at its nodes and out of "
                                        "its connectors, pulled by its tension and resting on what it falls onto - drawn faint "
                                        "in red where it would hang, each place it falls from marked with how far"));
    connect(gravity_, &QToolButton::toggled, this, [this] {
        runGravity();
        sayStatus();
    });
    br->addWidget(gravity_);
    down_ = new QComboBox(bundleRow_);
    down_->setObjectName(QStringLiteral("view3dGravityDown"));
    for (const char* d : {"Down -Z", "Down -Y", "Down -X", "Down +Z", "Down +Y", "Down +X"}) down_->addItem(QLatin1String(d));
    down_->setToolTip(QStringLiteral("Which way gravity pulls, in the model's own axes"));
    connect(down_, &QComboBox::currentIndexChanged, this, [this] {
        runGravity();
        sayStatus();
    });
    br->addWidget(down_);
    // KEEP-OUTS: levels and zones no cable may go through - listed and set, shown or hidden.
    button(rr2, "view3dKeepOuts", QStringLiteral("Keep-outs..."),
           QStringLiteral("Levels (a plane across X, Y or Z) and zones (a box) no cable may go through - each set to block "
                          "a cable (the change refused) or to alert (allowed, and warned of)"),
           [this] { showKeepOuts(); });
    keepShown_ = button(rr2, "view3dKeepOutsShown", QStringLiteral("Show keep-outs"),
                        QStringLiteral("Show or hide the keep-out levels and zones"), [this] {});
    keepShown_->setCheckable(true);
    keepShown_->setChecked(true);
    connect(keepShown_, &QToolButton::toggled, this, [this](bool on) { canvas_->setKeepOutsShown(on); });
    undo_ = button(rr2, "view3dRouteUndo", QStringLiteral("Undo"), QStringLiteral("Take back the last change to the route (Ctrl+Z)"),
                   [this] { undoRoute(); });
    // LAID ON THE FACES UNDER IT, the way gravity pulls - the cable selected, or all of them.
    button(rr2, "view3dAttach", QStringLiteral("Attach to surface"),
           QStringLiteral("Lay the cable selected (every cable, where none is) on the faces under it, the way gravity pulls "
                          "(Down) - onto a wall beside it where nothing is under it - and hold it there"),
           [this] {
               const int n = attachToSurface();
               emit canvas_->message(n > 0 ? QStringLiteral("%1 cable(s) laid on the faces under them (Undo takes it back)").arg(n)
                                           : QStringLiteral("Attach to surface: no model to lay the cables on"));
           });
    // HOW IT IS DONE, SHOWN: the view itself recorded being used - the plane a click lands on, a cable routed from
    // one connector to the next, and how it bends - beside the same steps in words.
    button(rr2, "view3dHelp", QStringLiteral("Help"),
           QStringLiteral("How to work in the 3D view: the plane, routing a cable from connector to connector, bending it"),
           [this] { showHelp(); });
    rr->addStretch(1);
    rr2->addStretch(1);
    br->addStretch(1);
    rowsLay_->addWidget(routeRow_);
    rowsLay_->addWidget(editRow_);
    rowsLay_->addWidget(bundleRow_);

    // THE WORK PLANE: where a click lands. On the model's faces (and the ground where there is no model) - or on a
    // plane, level or upright, put at any height or place: how a bundle is laid through the air, up a riser, over a
    // gap, at the height it really runs.
    // WHERE A ROUTE LANDS, and the view's angles beside it.
    planeRow_ = new QWidget(this);
    auto* pv = new QHBoxLayout(planeRow_);
    pv->setContentsMargins(0, 0, 0, 0);
    pv->setSpacing(1);
    auto* pr = new QHBoxLayout;
    pr->setSpacing(3);
    auto* pr2 = new QHBoxLayout;
    pr2->setSpacing(3);
    pv->addLayout(pr);
    pv->addLayout(pr2);
    pr->addWidget(new QLabel(QStringLiteral("Route to land:"), planeRow_));
    plane_ = new QComboBox(planeRow_);
    plane_->setObjectName(QStringLiteral("view3dWorkPlane"));
    plane_->addItems({QStringLiteral("Model faces"), QStringLiteral("Plane across X (upright)"),
                      QStringLiteral("Plane across Y (upright)"), QStringLiteral("Plane across Z (a height)")});
    plane_->setItemData(0, QStringLiteral("The model's faces - the ground (Z = 0) beyond the model"), Qt::ToolTipRole);
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
    button(pr, "view3dWorkPlaneHere", QStringLiteral("Through point"),
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
    viewRow->setContentsMargins(0, 0, 0, 0);
    pr2->addWidget(viewBits_);
    pr->addStretch(1);
    pr2->addStretch(1);
    rowsLay_->addWidget(planeRow_);
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
    // ON THE LEFT, WHAT IS IN VIEW: the model's solids and the harness's parts, each ticked to show - while the 3D
    // Preview tab is open (the panels round the drawing put away).
    solidsPanel_ = new QWidget(split);
    solidsPanel_->setObjectName(QStringLiteral("view3dSolidsPanel"));
    {
        auto* sv = new QVBoxLayout(solidsPanel_);
        sv->setContentsMargins(0, 0, 0, 0);
        sv->setSpacing(2);
        auto* title = new QLabel(QStringLiteral("<b>In view</b> - tick to show; right-click a part in the picture to hide it"), solidsPanel_);
        title->setWordWrap(true);
        sv->addWidget(title);
        auto* filter = new QLineEdit(solidsPanel_);
        filter->setPlaceholderText(QStringLiteral("Filter by name"));
        sv->addWidget(filter);
        solidsTree_ = new QTreeWidget(solidsPanel_);
        solidsTree_->setObjectName(QStringLiteral("view3dSolids"));
        solidsTree_->setHeaderHidden(true);
        solidsTree_->setColumnCount(1);
        sv->addWidget(solidsTree_, 1);
        auto* all = new QPushButton(QStringLiteral("Show all"), solidsPanel_);
        all->setObjectName(QStringLiteral("view3dShowAll"));
        sv->addWidget(all);
        connect(filter, &QLineEdit::textChanged, this, [this](const QString& t) {
            for (int g = 0; g < solidsTree_->topLevelItemCount(); ++g) {
                QTreeWidgetItem* top = solidsTree_->topLevelItem(g);
                for (int i = 0; i < top->childCount(); ++i)
                    top->child(i)->setHidden(!t.isEmpty() && !top->child(i)->text(0).contains(t, Qt::CaseInsensitive));
            }
        });
        connect(all, &QPushButton::clicked, this, [this] {
            canvas_->showAllParts();
            partsShownChanged();
        });
        // TICKED: shown. A group's tick shows or hides all of it.
        connect(solidsTree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int) {
            if (fillingSolids_) return;
            const bool shown = it->checkState(0) == Qt::Checked;
            const int kind = it->data(0, Qt::UserRole).toInt();
            auto apply = [this, shown](QTreeWidgetItem* c) {
                if (c->data(0, Qt::UserRole).toInt() == 1) canvas_->setEnvPartHidden(c->data(0, Qt::UserRole + 1).toInt(), !shown);
                else if (c->data(0, Qt::UserRole).toInt() == 2) canvas_->setBrickHidden(c->data(0, Qt::UserRole + 1).toString(), !shown);
            };
            if (kind == 0)
                for (int i = 0; i < it->childCount(); ++i) apply(it->child(i));
            else
                apply(it);
            partsShownChanged();
        });
    }
    solidsPanel_->hide();
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
    // THE REAL HARNESS AS A ROUTE: its parts and cables put in where it is drawn in 3D, to move into the model.
    button(linkRow, "view3dRouteFromHarness", QStringLiteral("Route from harness"),
           QStringLiteral("Put the harness in as a route: a connector for each of its parts not put yet, where the harness is "
                          "drawn in 3D, and its cables between them (through a branch where a cable goes to several) - then "
                          "move the connectors to where they go in the model, and Lengths to harness"),
           [this] {
               const int n = routeFromHarness();
               emit canvas_->message(n < 0 ? QStringLiteral("No harness to route - open one in the harness wizard, or place one")
                                           : QStringLiteral("%1 cable(s) of the harness laid - move its connectors to where they go").arg(n));
           });
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
    // ITS FORM AND SIZE: a box, a cylinder, a flange and body, a right angle, a D-shaped shell - as wide, tall and long as
    // said, or Auto (from the bundle and the lead).
    cg->addWidget(new QLabel(QStringLiteral("Form"), connBox_), 5, 0);
    connForm_ = new QComboBox(connBox_);
    connForm_->setObjectName(QStringLiteral("view3dConnForm"));
    connForm_->addItem(QStringLiteral("Rectangular"), QStringLiteral("rect"));
    connForm_->addItem(QStringLiteral("Cylindrical"), QStringLiteral("round"));
    connForm_->addItem(QStringLiteral("T (flange and body)"), QStringLiteral("tee"));
    connForm_->addItem(QStringLiteral("Right angle (L)"), QStringLiteral("angle"));
    connForm_->addItem(QStringLiteral("D-sub shell"), QStringLiteral("dsub"));
    connForm_->setToolTip(QStringLiteral("The shape of its housing"));
    cg->addWidget(connForm_, 5, 1);
    const char* sizeNames[3] = {"view3dConnW", "view3dConnH", "view3dConnL"};
    const QString sizeLabels[3] = {QStringLiteral("Width"), QStringLiteral("Height"), QStringLiteral("Length")};
    const char* autoNames[3] = {"view3dConnWAuto", "view3dConnHAuto", "view3dConnLAuto"};
    for (int i = 0; i < 3; ++i) {
        cg->addWidget(new QLabel(sizeLabels[i], connBox_), 6 + i, 0);
        connSize_[i] = new QDoubleSpinBox(connBox_);
        connSize_[i]->setObjectName(QLatin1String(sizeNames[i]));
        connSize_[i]->setRange(0.1, 1000.0);
        connSize_[i]->setDecimals(1);
        connSize_[i]->setSuffix(QStringLiteral(" mm"));
        connSize_[i]->setKeyboardTracking(false);
        connSize_[i]->setToolTip(i == 2 ? QStringLiteral("How far it stands out of its face") : QStringLiteral("Across its housing"));
        cg->addWidget(connSize_[i], 6 + i, 1);
        // AUTO, BY ITS "A": from the bundle's diameter (and the lead) - the field showing what that comes to.
        connAuto_[i] = autoButton(connBox_, autoNames[i],
                                  i == 2 ? QStringLiteral("Auto: as far as the lead - the field shows it")
                                         : QStringLiteral("Auto: from the bundle's diameter - the field shows it"));
        cg->addWidget(connAuto_[i], 6 + i, 2);
        connect(connSize_[i], &QDoubleSpinBox::valueChanged, this, [this, i](double v) {
            if (connAuto_[i]->isChecked()) return;
            editSelectedNode([i, v](RouteNode3D& n) { (i == 0 ? n.formW : i == 1 ? n.formH : n.formL) = v; });
        });
        connect(connAuto_[i], &QToolButton::toggled, this, [this, i](bool on) {
            // Off: from what it showed - the real figure - to change.
            const double shown = connSize_[i]->value();
            editSelectedNode([i, on, shown](RouteNode3D& n) { (i == 0 ? n.formW : i == 1 ? n.formH : n.formL) = on ? 0.0 : shown; });
        });
    }
    connect(connForm_, &QComboBox::activated, this, [this](int) {
        const QString form = connForm_->currentData().toString();
        editSelectedNode([form](RouteNode3D& n) { n.form = form; });
    });
    // ITS CABLE OUT OF ITS FRONT, or out of its back - through the panel it is fitted in.
    cg->addWidget(new QLabel(QStringLiteral("Cable out of"), connBox_), 9, 0);
    connBack_ = new QComboBox(connBox_);
    connBack_->setObjectName(QStringLiteral("view3dConnBack"));
    connBack_->addItems({QStringLiteral("Its front"), QStringLiteral("Its back (through the panel)")});
    connBack_->setToolTip(QStringLiteral("Front: the cable leaves the way it points. Back: out behind its face - a connector "
                                         "fitted through a panel, mated outside, its wires inside"));
    cg->addWidget(connBack_, 9, 1);
    connect(connBack_, &QComboBox::activated, this, [this](int i) { editSelectedNode([i](RouteNode3D& n) { n.back = i == 1; }); });
    connBox_->setVisible(false);
    sl->addWidget(connBox_);
    // THE CABLE SELECTED, ITS OWN: how long it is (typed, it is laid so long), how thick (its A: as all cables, or as its
    // wires pack to), its wires and their colours, its tension and its colour. Shown while a cable is selected.
    cableBox_ = new QWidget(side_);
    cableBox_->setObjectName(QStringLiteral("view3dCableBox"));
    {
        auto* g = new QGridLayout(cableBox_);
        g->setContentsMargins(0, 4, 0, 4);
        g->setHorizontalSpacing(4);
        g->setVerticalSpacing(2);
        cableSaid_ = new QLabel(cableBox_);
        cableSaid_->setWordWrap(true);
        g->addWidget(cableSaid_, 0, 0, 1, 3);
        auto mm = [this](const char* name, double lo, double hi, int decimals, const QString& tip) {
            auto* s = new QDoubleSpinBox(cableBox_);
            s->setObjectName(QLatin1String(name));
            s->setRange(lo, hi);
            s->setDecimals(decimals);
            s->setSuffix(QStringLiteral(" mm"));
            s->setKeyboardTracking(false);
            s->setToolTip(tip);
            return s;
        };
        g->addWidget(new QLabel(QStringLiteral("Length"), cableBox_), 1, 0);
        cableLength_ = mm("view3dCableLength", 1.0, 100000.0, 1,
                          QStringLiteral("How long the cable is along its path - type a length and it is laid so long, its ends "
                                         "where they are (no shorter than straight)"));
        g->addWidget(cableLength_, 1, 1, 1, 2);
        connect(cableLength_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
            if (!filling_) setSegmentLength(canvas_->selectedSegment(), v);
        });
        g->addWidget(new QLabel(QString::fromUtf8("Ø"), cableBox_), 2, 0);
        cableDia_ = mm("view3dCableDia", 0.1, 200.0, 1, QStringLiteral("This cable's diameter"));
        g->addWidget(cableDia_, 2, 1);
        cableDiaAuto_ = autoButton(cableBox_, "view3dCableDiaAuto",
                                   QStringLiteral("Auto: as all cables, or as its wires pack to - the field shows it"));
        g->addWidget(cableDiaAuto_, 2, 2);
        connect(cableDia_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
            if (filling_ || cableDiaAuto_->isChecked()) return;
            editSegment(canvas_->selectedSegment(), [v](RouteSegment3D& s) {
                s.diameter = v;
                s.wires = 0;
            });
        });
        connect(cableDiaAuto_, &QToolButton::toggled, this, [this](bool on) {
            if (filling_) return;
            const double shown = cableDia_->value();
            editSegment(canvas_->selectedSegment(), [on, shown](RouteSegment3D& s) {
                s.diameter = on ? 0.0 : shown;
                if (!on) s.wires = 0;
            });
        });
        g->addWidget(new QLabel(QStringLiteral("Wires"), cableBox_), 3, 0);
        cableWires_ = new QSpinBox(cableBox_);
        cableWires_->setObjectName(QStringLiteral("view3dCableWires"));
        cableWires_->setRange(0, 999);
        cableWires_->setKeyboardTracking(false);
        cableWires_->setToolTip(QStringLiteral("A bundle of so many wires, drawn wire by wire, as thick as they pack to; 0: as all "
                                               "cables"));
        g->addWidget(cableWires_, 3, 1, 1, 2);
        connect(cableWires_, &QSpinBox::valueChanged, this, [this](int n) {
            if (filling_) return;
            editSegment(canvas_->selectedSegment(), [n](RouteSegment3D& s) {
                s.wires = n;
                if (n > 0) s.diameter = 0.0;
            });
        });
        g->addWidget(new QLabel(QString::fromUtf8("Wire Ø"), cableBox_), 4, 0);
        cableWireD_ = mm("view3dCableWireD", 0.05, 50.0, 2, QStringLiteral("Each of its wires' diameter"));
        cableWireD_->setSingleStep(0.1);
        g->addWidget(cableWireD_, 4, 1, 1, 2);
        connect(cableWireD_, &QDoubleSpinBox::valueChanged, this, [this](double d) {
            if (!filling_) editSegment(canvas_->selectedSegment(), [d](RouteSegment3D& s) { s.wireD = d; });
        });
        g->addWidget(new QLabel(QStringLiteral("Wire colours"), cableBox_), 5, 0);
        cableWireLook_ = new QComboBox(cableBox_);
        cableWireLook_->setObjectName(QStringLiteral("view3dCableWireLook"));
        cableWireLook_->addItems({QStringLiteral("Each its own"), QStringLiteral("All one colour")});
        cableWireLook_->setToolTip(QStringLiteral("Its wires each a colour of its own (as chosen, or in turn), or all the cable's colour"));
        g->addWidget(cableWireLook_, 5, 1);
        connect(cableWireLook_, &QComboBox::activated, this, [this](int i) {
            editSegment(canvas_->selectedSegment(), [i](RouteSegment3D& s) { s.wireSame = i == 1; });
        });
        auto* each = new QToolButton(cableBox_);
        each->setObjectName(QStringLiteral("view3dCableWireEach"));
        each->setText(QStringLiteral("Each..."));
        each->setToolTip(QStringLiteral("Choose the colour of each wire"));
        g->addWidget(each, 5, 2);
        connect(each, &QToolButton::clicked, this, [this] { chooseWireColours(canvas_->selectedSegment()); });
        g->addWidget(new QLabel(QStringLiteral("Tension"), cableBox_), 6, 0);
        g->addWidget(cableTension_, 6, 1, 1, 2);
        auto* colour = new QToolButton(cableBox_);
        colour->setObjectName(QStringLiteral("view3dCableColour"));
        colour->setText(QStringLiteral("Colour..."));
        colour->setToolTip(QStringLiteral("The cable's colour (and its wires', all one colour)"));
        g->addWidget(colour, 7, 1);
        connect(colour, &QToolButton::clicked, this, [this] {
            const QString seg = canvas_->selectedSegment();
            const RouteSegment3D* t = route_.segment(seg);
            if (!t) return;
            const QColor c = QColorDialog::getColor(t->color.isValid() ? t->color : QColor(235, 140, 40), this, QStringLiteral("The cable's colour"));
            if (c.isValid()) setSegmentColour(seg, c);
        });
    }
    cableBox_->setVisible(false);
    sl->addWidget(cableBox_);
    // MOVING A NODE (the Move tool, a node clicked): where it is, by so much a step - or by the wheel's arrows - turned
    // round each axis, and its colour.
    moveBox_ = new QWidget(side_);
    moveBox_->setObjectName(QStringLiteral("view3dMoveBox"));
    {
        auto* mg = new QGridLayout(moveBox_);
        mg->setContentsMargins(0, 4, 0, 4);
        mg->setHorizontalSpacing(4);
        mg->setVerticalSpacing(2);
        moveSaid_ = new QLabel(moveBox_);
        mg->addWidget(moveSaid_, 0, 0, 1, 4);
        const char* axisNames[3] = {"view3dMoveX", "view3dMoveY", "view3dMoveZ"};
        for (int i = 0; i < 3; ++i) {
            mg->addWidget(new QLabel(QString(QChar(QLatin1Char("XYZ"[i]))), moveBox_), 1, i * 0 + 0 + i, Qt::AlignLeft);
        }
        for (int i = 0; i < 3; ++i) {
            moveAt_[i] = new QDoubleSpinBox(moveBox_);
            moveAt_[i]->setObjectName(QLatin1String(axisNames[i]));
            moveAt_[i]->setRange(-100000.0, 100000.0);
            moveAt_[i]->setDecimals(1);
            moveAt_[i]->setSuffix(QStringLiteral(" mm"));
            moveAt_[i]->setKeyboardTracking(false);
            moveAt_[i]->setToolTip(QStringLiteral("Where it is along %1").arg(QChar(QLatin1Char("XYZ"[i]))));
            mg->addWidget(moveAt_[i], 2, i);
            connect(moveAt_[i], &QDoubleSpinBox::valueChanged, this, [this] {
                if (fillingMove_) return;
                const V3 to{moveAt_[0]->value(), moveAt_[1]->value(), moveAt_[2]->value()};
                editSelectedNode([to](RouteNode3D& n) { n.at = to; });
            });
        }
        mg->addWidget(new QLabel(QStringLiteral("Step"), moveBox_), 3, 0);
        moveStep_ = new QDoubleSpinBox(moveBox_);
        moveStep_->setObjectName(QStringLiteral("view3dMoveStep"));
        moveStep_->setRange(0.1, 1000.0);
        moveStep_->setValue(5.0);
        moveStep_->setSuffix(QStringLiteral(" mm"));
        moveStep_->setToolTip(QStringLiteral("How far each arrow on the wheel moves it"));
        mg->addWidget(moveStep_, 3, 1);
        auto* colour = new QPushButton(QStringLiteral("Colour..."), moveBox_);
        colour->setObjectName(QStringLiteral("view3dNodeColor"));
        connect(colour, &QPushButton::clicked, this, [this] {
            const RouteNode3D* n = route_.node(canvas_->selectedNode());
            if (!n) return;
            const QColor c = QColorDialog::getColor(n->color.isValid() ? n->color : QColor(205, 208, 214), this, QStringLiteral("Its colour"));
            if (c.isValid()) setSelectedNodeColor(c);
        });
        mg->addWidget(colour, 3, 2);
        mg->addWidget(new QLabel(QStringLiteral("Turn"), moveBox_), 4, 0);
        auto* turns = new QWidget(moveBox_);
        auto* tl = new QHBoxLayout(turns);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->setSpacing(2);
        for (int axis = 0; axis < 3; ++axis)
            for (int sign : {1, -1}) {
                auto* b = new QToolButton(turns);
                const QString name = QStringLiteral("view3dRot%1%2").arg(QChar(QLatin1Char("XYZ"[axis]))).arg(sign > 0 ? QLatin1Char('+') : QLatin1Char('-'));
                b->setObjectName(name);
                b->setText(QStringLiteral("%1%2").arg(QChar(QLatin1Char("XYZ"[axis]))).arg(sign > 0 ? QStringLiteral("↻") : QStringLiteral("↺")));
                b->setToolTip(QStringLiteral("Turn it 15° round %1, %2").arg(QChar(QLatin1Char("XYZ"[axis]))).arg(sign > 0 ? QStringLiteral("one way") : QStringLiteral("the other")));
                b->setAutoRaise(true);
                connect(b, &QToolButton::clicked, this, [this, axis, sign] { rotateSelectedNode(axis, 15.0 * sign); });
                tl->addWidget(b);
            }
        mg->addWidget(turns, 4, 1, 1, 3);
    }
    moveBox_->setVisible(false);
    sl->addWidget(moveBox_);
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

    table_ = new QTableWidget(0, 4, side_);
    table_->setObjectName(QStringLiteral("view3dRoutes"));
    table_->setHorizontalHeaderLabels({QStringLiteral("Bundle"), QStringLiteral("Length"), QStringLiteral("Ø"), QStringLiteral("Tension")});
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setToolTip(QStringLiteral("Each bundle routed, its length along its path - the Ø column sets one bundle's diameter, "
                                      "the Tension column how taut it is put in"));
    sl->addWidget(table_, 1);
    totals_ = new QLabel(side_);
    totals_->setObjectName(QStringLiteral("view3dRouteTotals"));
    totals_->setWordWrap(true);
    sl->addWidget(totals_);
    split->addWidget(side_);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 0);
    split->setSizes({230, 800, 230});
    lay->addWidget(split, 1);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("view3dStatus"));
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(status_);
    canvas_->setScene(&scene_);
    canvas_->setRoute(&route_);
    // THE WHEEL, over the picture's top right corner: each segment a turn, a zoom or a view; the middle, the options.
    wheel_ = new NavWheel(canvas_);
    wheel_->setObjectName(QStringLiteral("view3dWheel"));
    auto step = [this] { return static_cast<double>(wheelStep_); };
    viewWheel_ = {
        {QStringLiteral("up"), QStringLiteral("Tilt up"), QStringLiteral("Tilt the view up and over"), [this, step] { canvas_->nudge(0, step(), 0); }, {}},
        {QStringLiteral("plus"), QStringLiteral("Zoom in"), QStringLiteral("Nearer"), [this] { canvas_->zoomBy(0.8); }, {}},
        {QStringLiteral("cw"), QStringLiteral("Roll"), QStringLiteral("Roll the view clockwise round the way it looks"), [this, step] { canvas_->nudge(0, 0, step()); }, {}},
        {QStringLiteral("right"), QStringLiteral("Turn"), QStringLiteral("Turn the view to the right, round the upright"), [this, step] { canvas_->nudge(-step(), 0, 0); }, {}},
        {QStringLiteral("fit"), QStringLiteral("Fit"), QStringLiteral("Everything in view"), [this] { canvas_->fit(); }, {}},
        {QStringLiteral("lock"), QStringLiteral("Lock"), QStringLiteral("Hold the view still - a drag moves what is under it"),
         [this] { setViewLocked(!canvas_->viewLocked()); }, [this] { return canvas_->viewLocked(); }},
        {QStringLiteral("down"), QStringLiteral("Tilt down"), QStringLiteral("Tilt the view down and under"), [this, step] { canvas_->nudge(0, -step(), 0); }, {}},
        {QStringLiteral("gear"), QStringLiteral("Options"), QStringLiteral("How far each click turns it, the views, the wheel itself"),
         [this] { wheelOptions(QCursor::pos()); }, {}},
        {QStringLiteral("iso"), QStringLiteral("Iso"), QStringLiteral("At an angle, the whole of it"), [this] { canvas_->viewFrom(QStringLiteral("iso")); }, {}},
        {QStringLiteral("left"), QStringLiteral("Turn"), QStringLiteral("Turn the view to the left, round the upright"), [this, step] { canvas_->nudge(step(), 0, 0); }, {}},
        {QStringLiteral("ccw"), QStringLiteral("Roll"), QStringLiteral("Roll the view counter-clockwise round the way it looks"), [this, step] { canvas_->nudge(0, 0, -step()); }, {}},
        {QStringLiteral("minus"), QStringLiteral("Zoom out"), QStringLiteral("Further off"), [this] { canvas_->zoomBy(1.25); }, {}},
    };
    wheel_->setItems(viewWheel_);
    wheel_->setMiddle(QStringLiteral("View"), [this](const QPoint& at) { wheelOptions(at); });
    canvas_->installEventFilter(this);
    connect(wheelShown_, &QCheckBox::toggled, wheel_, &QWidget::setVisible);
    placeWheel();
    connect(canvas_, &View3DCanvas::picked, this, [this](const Pick3D& pick) { showPick(pick); });
    connect(canvas_, &View3DCanvas::routeAboutToChange, this, [this] { beforeRouteChange(); });
    connect(canvas_, &View3DCanvas::routeChanged, this, [this] {
        // A KEEP-OUT THAT BLOCKS: a change that takes a cable through one it did not go through is refused - the route
        // put back as it was, and said why.
        if (!history_.empty()) {
            const Route3D was = Route3D::fromJson(history_.back());
            auto blocked = [](const Route3D& r) {
                QStringList through;
                for (const KeepOutHit& h : keepOutHits(r))
                    if (h.block) through << h.segment + QLatin1Char('|') + h.keepOut;
                return through;
            };
            const QStringList before = blocked(was);
            for (const KeepOutHit& h : keepOutHits(route_)) {
                if (!h.block || before.contains(h.segment + QLatin1Char('|') + h.keepOut)) continue;
                const RouteSegment3D* s = route_.segment(h.segment);
                const RouteNode3D* a = s ? route_.node(s->a) : nullptr;
                const RouteNode3D* b = s ? route_.node(s->b) : nullptr;
                const KeepOut3D* k = route_.keepOut(h.keepOut);
                // Said before the route is put back - what it points into goes with it.
                const QString cable = a && b ? QStringLiteral("%1 - %2").arg(a->label, b->label) : QStringLiteral("A cable");
                const QString zone = k ? keepOutName(*k) : h.keepOut;
                const QJsonObject back = history_.back();
                history_.pop_back();
                restoreRoute(back);
                pickSaid_ = QStringLiteral("Not done: %1 would pass through the keep-out %2, which blocks it.").arg(cable, zone);
                sayStatus();
                return;
            }
        }
        if (pickSaid_.startsWith(QLatin1String("Not done:"))) pickSaid_.clear();   // what was refused, said once
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
    connect(canvas_, &View3DCanvas::redoRequested, this, [this] { redoRoute(); });
    // STRETCHED OR MOVED, under the pointer: the lengths as they are now - kept when it is let go.
    connect(canvas_, &View3DCanvas::routeMoving, this, [this] { refreshRoute(); });
    connect(canvas_, &View3DCanvas::message, this, [this](const QString& text) {
        pickSaid_ = text;
        sayStatus();
    });
    applyIcons();
    // HOW IT WAS SHOWN LAST TIME: its background.
    {
        const int mode = QSettings().value(QStringLiteral("view3d/background"), 0).toInt();
        canvas_->setBackground(mode, QColor(QSettings().value(QStringLiteral("view3d/backgroundColour")).toString()));
    }
    connect(canvas_, &View3DCanvas::ghostFrozenChanged, this, [this] { syncConnector(); });
    // A PART OF THE HARNESS DRAWN, TAKEN HOLD OF: the harness put in as a route where it is not yet (its drawing then
    // hidden - the route stands for it), and the part's node dragged.
    connect(canvas_, &View3DCanvas::harnessPartPressed, this, [this](const QString& brick, const QPointF& at) {
        QString node = routeNodeOfPart(route_, brick);
        if (node.isEmpty()) {
            if (routeFromHarness() < 0) {
                emit canvas_->message(QStringLiteral("No harness to route - open one in the harness wizard, or place one"));
                return;
            }
            node = routeNodeOfPart(route_, brick);
            if (auto* shown = findChild<QAbstractButton*>(QStringLiteral("view3dHarnessShown"))) shown->setChecked(false);
            emit canvas_->message(QStringLiteral("The harness put in as a route - drag each of its connectors to where it goes"));
        }
        if (!node.isEmpty()) canvas_->takeHoldOfNode(node, at);
    });
    connect(canvas_, &View3DCanvas::harnessPartClicked, this, [this](const QString& brick) {
        for (int i = 0; i < parts_->count(); ++i)
            if (parts_->item(i)->data(Qt::UserRole).toString() == brick) parts_->setCurrentRow(i);
        emit canvas_->message(routeNodeOfPart(route_, brick).isEmpty()
                                  ? QStringLiteral("%1 - a part of the harness drawn: Move and drag it to put the harness in as a route "
                                                   "and move it, or Route from harness")
                                        .arg(harnessPartName(brick))
                                  : QStringLiteral("%1 - put in the route as %2").arg(harnessPartName(brick), route_.node(routeNodeOfPart(route_, brick))->label));
    });
    // THE BRANCH TOOL ON A CONNECTOR: from its front, or its back.
    connect(canvas_, &View3DCanvas::connectorBranchAsked, this, [this](const QString& node, const QPoint& at) {
        const RouteNode3D* n = route_.node(node);
        if (!n) return;
        QMenu menu(this);
        QAction* front = menu.addAction(QStringLiteral("Start a bundle from the front of %1").arg(n->label));
        front->setObjectName(QStringLiteral("view3dBranchFront"));
        connect(front, &QAction::triggered, this, [this, node] { startBranchFrom(node, false); });
        QAction* back = menu.addAction(QStringLiteral("Start a bundle from the back of %1").arg(n->label));
        back->setObjectName(QStringLiteral("view3dBranchBack"));
        connect(back, &QAction::triggered, this, [this, node] { startBranchFrom(node, true); });
        menu.exec(at);
    });
    // AT THE SIDE: one button over the picture, to open it below with everything.
    toBottom_ = new QToolButton(canvas_);
    toBottom_->setObjectName(QStringLiteral("view3dToBottom"));
    toBottom_->setText(QStringLiteral("Open below"));
    toBottom_->setToolTip(QStringLiteral("The 3D view under the drawing, with all its options - and back to the side from there"));
    toBottom_->setIcon(icon3D(QStringLiteral("expand"), QColor(230, 236, 244)));
    toBottom_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toBottom_->setStyleSheet(QStringLiteral("QToolButton{background:rgba(20,24,30,200);color:#e6ecf4;border:1px solid #4a5868;"
                                            "border-radius:3px;padding:2px 6px;}"));
    toBottom_->hide();
    connect(toBottom_, &QToolButton::clicked, this, [this] { emit dockBelowRequested(); });
    // AT THE SIDE, REFRESH too: the 3D harness built again from the drawing.
    sideRefresh_ = new QToolButton(canvas_);
    sideRefresh_->setObjectName(QStringLiteral("view3dSideRefresh"));
    sideRefresh_->setText(QStringLiteral("Refresh"));
    sideRefresh_->setToolTip(QStringLiteral("Build the 3D harness again from the drawing"));
    sideRefresh_->setIcon(icon3D(QStringLiteral("refresh"), QColor(230, 236, 244)));
    sideRefresh_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    sideRefresh_->setStyleSheet(toBottom_->styleSheet());
    sideRefresh_->hide();
    connect(sideRefresh_, &QToolButton::clicked, this, [this] { rebuild(false); });
    // UNDER THE WHEEL: what the pointer is on.
    hoverSaid_ = new QLabel(canvas_);
    hoverSaid_->setObjectName(QStringLiteral("view3dHover"));
    hoverSaid_->setStyleSheet(QStringLiteral("QLabel{background:rgba(20,24,30,190);color:#e6ecf4;border:1px solid #4a5868;"
                                             "border-radius:3px;padding:2px 6px;}"));
    hoverSaid_->setAttribute(Qt::WA_TransparentForMouseEvents);
    hoverSaid_->hide();
    connect(canvas_, &View3DCanvas::hovered, this, [this](int part, const QString& brick, const QString& node, const QString& seg) {
        QString said;
        if (const RouteNode3D* n = route_.node(node)) {
            const QString kind = n->kind == RouteNodeKind::Connector ? QStringLiteral("connector")
                                 : n->kind == RouteNodeKind::Clip    ? QStringLiteral("clip")
                                                                     : QStringLiteral("branch");
            said = QStringLiteral("%1 - %2").arg(n->label, kind);
        } else if (const RouteSegment3D* s = route_.segment(seg)) {
            const RouteNode3D* a = route_.node(s->a);
            const RouteNode3D* b = route_.node(s->b);
            said = QStringLiteral("Cable %1 - %2   %3 mm, Ø %4")
                       .arg(a ? a->label : s->a, b ? b->label : s->b)
                       .arg(route_.length(*s), 0, 'f', 1)
                       .arg(route_.diameterOf(*s), 0, 'f', 1);
            if (const int nw = route_.wiresOf(*s); nw > 0) said += QStringLiteral(", %1 wires").arg(nw);
        } else if (part >= 0) {
            said = envPartName(part);
        } else if (!brick.isEmpty()) {
            said = harnessPartName(brick);
        }
        hoverSaid_->setText(said);
        hoverSaid_->adjustSize();
        placeWheel();
    });
    // RIGHT-CLICKED IN THE PICTURE: that solid or part hidden, the others, or all shown again.
    connect(canvas_, &View3DCanvas::partMenuAsked, this, [this](int part, const QString& brick, const QPoint& at) {
        QMenu menu(this);
        const QString name = part >= 0 ? envPartName(part) : brick.isEmpty() ? QString() : harnessPartName(brick);
        if (!name.isEmpty()) {
            QAction* hide = menu.addAction(QStringLiteral("Hide %1").arg(name));
            hide->setObjectName(QStringLiteral("view3dHidePart"));
            connect(hide, &QAction::triggered, this, [this, part, brick] {
                if (part >= 0) canvas_->setEnvPartHidden(part, true);
                else canvas_->setBrickHidden(brick, true);
                partsShownChanged();
            });
            QAction* others = menu.addAction(QStringLiteral("Hide the others"));
            connect(others, &QAction::triggered, this, [this, part, brick] {
                for (size_t i = 0; i < env_.parts.size(); ++i) canvas_->setEnvPartHidden(static_cast<int>(i), static_cast<int>(i) != part);
                for (const QString& b : harnessBricks()) canvas_->setBrickHidden(b, b != brick);
                partsShownChanged();
            });
            QMenu* see = menu.addMenu(QStringLiteral("Opacity of %1").arg(name));
            see->setObjectName(QStringLiteral("view3dPartOpacity"));
            fillOpacityMenu(see, part, brick);
            menu.addSeparator();
        }
        QAction* all = menu.addAction(QStringLiteral("Show all"));
        connect(all, &QAction::triggered, this, [this] {
            canvas_->showAllParts();
            partsShownChanged();
        });
        // THE RULERS: all taken away - there always, greyed while there are none.
        QAction* rulers = menu.addAction(QStringLiteral("Remove all measures"));
        rulers->setObjectName(QStringLiteral("view3dRemoveMeasures"));
        rulers->setEnabled(canvas_->measureCount() > 0);
        connect(rulers, &QAction::triggered, this, [this] { canvas_->clearMeasures(); });
        menu.exec(at);
    });
    fitRows();
    connect(canvas_, &View3DCanvas::cameraChanged, this, [this] { syncCamera(); });
    connect(canvas_, &View3DCanvas::routeChanged, this, [this] { syncConnector(); });
    connect(canvas_, &View3DCanvas::selectionChanged, this, [this] {
        syncConnector();
        syncMove();
        sayStatus();
    });
    connect(this, &View3DPane::routeToolChanged, this, [this] { syncMove(); });
    // Esc with a tool in hand: Select again.
    connect(canvas_, &View3DCanvas::toolCancelRequested, this, [this] { setRouteTool(RouteTool::Select); });
    // THE WHEEL'S ARROWS the way the axes run as the view is turned.
    connect(canvas_, &View3DCanvas::cameraChanged, this, [this] {
        if (wheelMoving_) buildMoveWheel();
    });
    connect(canvas_, &View3DCanvas::routeChanged, this, [this] { syncMove(); });
    // RIGHT-CLICKED ON THE ROUTE: the ruler, the node or the bundle under it deleted - or every ruler taken away.
    connect(canvas_, &View3DCanvas::routeMenuAsked, this, [this](const QString& node, const QString& seg, int measure, const QPoint& at) {
        QMenu menu(this);
        if (measure >= 0) {
            QAction* del = menu.addAction(QStringLiteral("Delete this measure"));
            del->setObjectName(QStringLiteral("view3dDeleteMeasure"));
            connect(del, &QAction::triggered, this, [this, measure] { canvas_->removeMeasure(measure); });
        }
        if (const RouteNode3D* n = route_.node(node)) {
            QAction* del = menu.addAction(QStringLiteral("Delete %1").arg(n->label));
            del->setObjectName(QStringLiteral("view3dDeleteNode"));
            connect(del, &QAction::triggered, this, [this, node] { canvas_->removeRouteItem(node, QString()); });
            if (n->kind == RouteNodeKind::Connector) {
                // SAME AS: the connector clicked next copied to this one.
                QAction* same = menu.addAction(QStringLiteral("Same as..."));
                same->setObjectName(QStringLiteral("view3dSameAs"));
                connect(same, &QAction::triggered, this, [this, node] { canvas_->startSameAs(node); });
                // ITS CABLE OUT OF ITS BACK (through the panel it is fitted in) or its front; a bundle started from either.
                QAction* back = menu.addAction(QStringLiteral("Cable out of its back"));
                back->setObjectName(QStringLiteral("view3dConnBackAction"));
                back->setCheckable(true);
                back->setChecked(n->back);
                connect(back, &QAction::toggled, this, [this, node](bool on) { setConnectorBack(node, on); });
                QAction* fromFront = menu.addAction(QStringLiteral("Start a bundle from its front"));
                connect(fromFront, &QAction::triggered, this, [this, node] { startBranchFrom(node, false); });
                QAction* fromBack = menu.addAction(QStringLiteral("Start a bundle from its back"));
                connect(fromBack, &QAction::triggered, this, [this, node] { startBranchFrom(node, true); });
            }
            QAction* colour = menu.addAction(QStringLiteral("Colour..."));
            connect(colour, &QAction::triggered, this, [this, node] {
                const RouteNode3D* m = route_.node(node);
                const QColor c = QColorDialog::getColor(m && m->color.isValid() ? m->color : QColor(205, 208, 214), this, QStringLiteral("Its colour"));
                if (!c.isValid() || !route_.node(node)) return;
                beforeRouteChange();
                route_.node(node)->color = c;
                saveRouting();
                refreshRoute();
                emit routeChanged();
            });
        } else if (const RouteSegment3D* s = route_.segment(seg)) {
            const RouteNode3D* a = route_.node(s->a);
            const RouteNode3D* b = route_.node(s->b);
            QAction* del = menu.addAction(QStringLiteral("Delete the bundle %1 - %2").arg(a ? a->label : s->a, b ? b->label : s->b));
            del->setObjectName(QStringLiteral("view3dDeleteSegment"));
            connect(del, &QAction::triggered, this, [this, seg] { canvas_->removeRouteItem(QString(), seg); });
            // ITS COLOUR, and MORE LENGTH - laid looser, its ends where they are.
            QAction* colour = menu.addAction(QStringLiteral("Colour..."));
            colour->setObjectName(QStringLiteral("view3dSegmentColour"));
            connect(colour, &QAction::triggered, this, [this, seg] {
                const RouteSegment3D* t = route_.segment(seg);
                const QColor c = QColorDialog::getColor(t && t->color.isValid() ? t->color : QColor(235, 140, 40), this, QStringLiteral("The bundle's colour"));
                if (c.isValid()) setSegmentColour(seg, c);
            });
            // A BUNDLE OF WIRES: how many, how thick - or a plain bundle again.
            QAction* wired = menu.addAction(QStringLiteral("Wires in it..."));
            wired->setObjectName(QStringLiteral("view3dSegmentWires"));
            connect(wired, &QAction::triggered, this, [this, seg] {
                const RouteSegment3D* t = route_.segment(seg);
                if (!t) return;
                bool ok = false;
                const int n = QInputDialog::getInt(this, QStringLiteral("Wires in it"), QStringLiteral("How many wires (0: a plain bundle)"),
                                                   std::max(route_.wiresOf(*t), 7), 0, 999, 1, &ok);
                if (!ok) return;
                double d = route_.wireDOf(*t);
                if (n > 0) {
                    d = QInputDialog::getDouble(this, QStringLiteral("Wires in it"), QStringLiteral("Each wire's diameter (mm)"), d, 0.05,
                                                50.0, 2, &ok);
                    if (!ok) return;
                }
                setWires(seg, n, d);
            });
            QAction* longer = menu.addAction(QStringLiteral("Add length..."));
            longer->setObjectName(QStringLiteral("view3dAddLength"));
            connect(longer, &QAction::triggered, this, [this, seg] {
                bool ok = false;
                const double mm = QInputDialog::getDouble(this, QStringLiteral("Add length"), QStringLiteral("Make the bundle longer by (mm)"),
                                                          10.0, 0.1, 10000.0, 1, &ok);
                if (ok) addSegmentLength(seg, mm);
            });
        }
        menu.addSeparator();
        QAction* rulers = menu.addAction(QStringLiteral("Remove all measures"));
        rulers->setObjectName(QStringLiteral("view3dRemoveMeasures"));
        rulers->setEnabled(canvas_->measureCount() > 0);
        connect(rulers, &QAction::triggered, this, [this] { canvas_->clearMeasures(); });
        menu.exec(at);
    });
    connect(canvas_, &View3DCanvas::selectionChanged, this, [this] { syncCableTension(); });
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

void View3DPane::setViewLocked(bool on) {
    canvas_->setViewLocked(on);
    const QSignalBlocker hold(lockBtn_);
    lockBtn_->setChecked(on);
    lockBtn_->setText(on ? QStringLiteral("View locked") : QStringLiteral("Lock view"));
    emit viewLockChanged(on);
    if (wheel_) wheel_->update();
}

void View3DPane::placeWheel() {
    if (toBottom_) {
        toBottom_->adjustSize();
        toBottom_->move(canvas_->width() - toBottom_->width() - 6, 6);
        toBottom_->raise();
    }
    if (sideRefresh_ && toBottom_) {
        sideRefresh_->adjustSize();
        sideRefresh_->move(toBottom_->x() - sideRefresh_->width() - 4, toBottom_->y());
        sideRefresh_->raise();
    }
    if (!wheel_) return;
    wheel_->move(canvas_->width() - wheel_->width() - 8, 8);
    wheel_->raise();
    // UNDER THE WHEEL, what the pointer is on - under it, kept inside the picture.
    if (hoverSaid_) {
        const bool show = !compact_ && !hoverSaid_->text().isEmpty();
        hoverSaid_->setVisible(show);
        if (show) {
            const int wide = hoverSaid_->width();
            const int x = std::clamp(wheel_->x() + wheel_->width() / 2 - wide / 2, 4, std::max(4, canvas_->width() - wide - 4));
            hoverSaid_->move(x, (wheel_->isVisible() ? wheel_->y() + wheel_->height() : 8) + 4);
            hoverSaid_->raise();
        }
    }
}

QString View3DPane::hoverText() const { return hoverSaid_ ? hoverSaid_->text() : QString(); }

bool View3DPane::eventFilter(QObject* watched, QEvent* event) {
    if (watched == canvas_ && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) placeWheel();
    // The strip as wide as the pane now: its groups on more lines, or fewer.
    if (watched == rowsArea_ && event->type() == QEvent::Resize) {
        const auto* r = static_cast<QResizeEvent*>(event);
        if (r->size().width() != r->oldSize().width()) QTimer::singleShot(0, this, [this] { fitRows(); });
    }
    return QWidget::eventFilter(watched, event);
}

void View3DPane::setSegmentColour(const QString& seg, const QColor& c) {
    if (!route_.segment(seg)) return;
    beforeRouteChange();
    route_.segment(seg)->color = c;
    saveRouting();
    refreshRoute();
    emit routeChanged();
}

bool View3DPane::addSegmentLength(const QString& seg, double mm) {
    const RouteSegment3D* s = route_.segment(seg);
    if (!s || mm <= 0.0) return false;
    const double target = route_.length(*s) + mm;
    beforeRouteChange();
    route_.lengthen(seg, target);
    saveRouting();
    refreshRoute();
    emit routeChanged();
    return true;
}

void View3DPane::setPlaceForm(const QString& form) { canvas_->setPlaceForm(form); }

void View3DPane::addLengthToSelected() {
    const QString seg = canvas_->selectedSegment();
    if (!route_.segment(seg)) {
        pickSaid_ = QStringLiteral("Select a bundle first - then Add length makes it longer.");
        sayStatus();
        return;
    }
    bool ok = false;
    const double mm = QInputDialog::getDouble(this, QStringLiteral("Add length"), QStringLiteral("Make the bundle longer by (mm)"), 10.0, 0.1,
                                              10000.0, 1, &ok);
    if (ok) addSegmentLength(seg, mm);
}

void View3DPane::setMeasureMode(int mode) {
    if (auto* c = findChild<QComboBox*>(QStringLiteral("view3dMeasureMode"))) c->setCurrentIndex(std::clamp(mode, 0, 1));
}

int View3DPane::measureMode() const { return canvas_->measureFaces() ? 1 : 0; }

void View3DPane::fillDisplayMenu(QMenu* m) {
    // WHAT IS BEHIND IT: dark, light, a gradient either way, or a colour of one's own - kept for next time.
    QMenu* back = m->addMenu(QStringLiteral("Background"));
    auto* group = new QActionGroup(back);
    const char* names[] = {"Dark", "Light", "Gradient, dark", "Gradient, light"};
    for (int i = 0; i < 4; ++i) {
        QAction* a = back->addAction(QString::fromLatin1(names[i]), this, [this, i] { setBackgroundMode(i, QColor()); });
        a->setCheckable(true);
        a->setChecked(canvas_->backgroundMode() == i);
        group->addAction(a);
    }
    QAction* own = back->addAction(QStringLiteral("A colour of my own..."), this, [this] {
        const QColor c = QColorDialog::getColor(QColor(30, 36, 44), this, QStringLiteral("The background"));
        if (c.isValid()) setBackgroundMode(4, c);
    });
    own->setCheckable(true);
    own->setChecked(canvas_->backgroundMode() == 4);
    group->addAction(own);
    // HOW IT IS SHADED: solid, or its edges alone; and the model see-through, to see the harness in it.
    QMenu* shade = m->addMenu(QStringLiteral("Shading"));
    auto* wire = findChild<QCheckBox*>(QStringLiteral("view3dWireframe"));
    QAction* solid = shade->addAction(QStringLiteral("Shaded"), this, [wire] { if (wire) wire->setChecked(false); });
    QAction* edges = shade->addAction(QStringLiteral("Wireframe"), this, [wire] { if (wire) wire->setChecked(true); });
    solid->setCheckable(true);
    edges->setCheckable(true);
    solid->setChecked(wire && !wire->isChecked());
    edges->setChecked(wire && wire->isChecked());
    shade->addSeparator();
    QAction* through = shade->addAction(QStringLiteral("The model see-through"), this, [this](bool on) {
        if (auto* s = findChild<QSlider*>(QStringLiteral("view3dEnvOpacity"))) s->setValue(on ? 35 : 100);
    });
    through->setCheckable(true);
    if (auto* s = findChild<QSlider*>(QStringLiteral("view3dEnvOpacity"))) through->setChecked(s->value() < 100);
}

void View3DPane::setBackgroundMode(int mode, const QColor& colour) {
    canvas_->setBackground(mode, colour);
    QSettings().setValue(QStringLiteral("view3d/background"), mode);
    if (mode == 4 && colour.isValid()) QSettings().setValue(QStringLiteral("view3d/backgroundColour"), colour.name());
}

void View3DPane::wheelOptions(const QPoint& at) {
    QMenu menu(this);
    fillDisplayMenu(menu.addMenu(QStringLiteral("Display")));
    QMenu* stepMenu = menu.addMenu(QStringLiteral("Each click turns it"));
    for (const int s : {5, 15, 45, 90}) {
        QAction* a = stepMenu->addAction(QStringLiteral("%1°").arg(s), this, [this, s] { wheelStep_ = s; });
        a->setCheckable(true);
        a->setChecked(wheelStep_ == s);
    }
    QMenu* views = menu.addMenu(QStringLiteral("View from"));
    for (const auto& [name, key] : {std::pair<const char*, const char*>{"Top", "top"}, {"Front", "front"}, {"Side", "side"}, {"Iso", "iso"}})
        views->addAction(QString::fromLatin1(name), this, [this, k = QString::fromLatin1(key)] { canvas_->viewFrom(k); });
    menu.addAction(QStringLiteral("Fit everything in"), this, [this] { canvas_->fit(); });
    menu.addSeparator();
    QAction* lock = menu.addAction(QStringLiteral("Lock the view"), this, [this](bool on) { setViewLocked(on); });
    lock->setCheckable(true);
    lock->setChecked(canvas_->viewLocked());
    auto* wire = findChild<QCheckBox*>(QStringLiteral("view3dWireframe"));
    QAction* wf = menu.addAction(QStringLiteral("Wireframe"), this, [wire](bool on) { if (wire) wire->setChecked(on); });
    wf->setCheckable(true);
    wf->setChecked(wire && wire->isChecked());
    menu.addSeparator();
    menu.addAction(QStringLiteral("Hide the wheel"), this, [this] { wheelShown_->setChecked(false); });
    menu.exec(at);
}

void View3DPane::armSolids() {
    route_.solidsChanged();
    route_.solids = [this](const V3& origin, const V3& dir, double far, V3& hit) {
        if (!envIndex_ || envIndex_->empty() || !canvas_) return false;
        // A hidden solid is no obstacle: on past it.
        const Pick3D p = envIndex_->cast(origin, dir, [this](int part, const V3&) { return canvas_->envPartHidden(part); });
        if (!p.hit || p.distance > far) return false;
        hit = p.point;
        return true;
    };
    // INSIDE A SOLID: a ray out of the point, the faces of each solid it crosses counted - an odd number for a solid,
    // and the point is in it. Aimed a little askew, so it does not run along an edge.
    route_.insideSolid = [this](const V3& point) {
        if (!envIndex_ || envIndex_->empty() || !canvas_) return false;
        const V3 dir = V3{0.1234, 0.2711, 0.9545}.normalized();
        std::map<int, int> crossed;
        V3 from = point;
        for (int n = 0; n < 64; ++n) {
            const Pick3D p = envIndex_->cast(from, dir, [this](int part, const V3&) { return canvas_->envPartHidden(part); });
            if (!p.hit) break;
            ++crossed[p.part];
            from = p.point + dir * 1e-4;
        }
        for (const auto& c : crossed)
            if (c.second % 2 == 1) return true;
        return false;
    };
}

QString View3DPane::nodeLengths() const {
    const RouteNode3D* n = route_.node(canvas_->selectedNode());
    if (!n || n->kind == RouteNodeKind::Connector) return {};
    // EACH WAY FROM IT: the bundle to the next node, and on through clips and branches with one way on, to the end.
    QStringList ways;
    for (const RouteSegment3D& s : route_.segments) {
        if (s.a != n->id && s.b != n->id) continue;
        double first = route_.length(s), total = first;
        QString at = s.a == n->id ? s.b : s.a, from = s.id;
        QString firstTo = route_.node(at) ? route_.node(at)->label : QString();
        for (int guard = 0; guard < 200; ++guard) {
            const RouteNode3D* next = route_.node(at);
            if (!next || next->kind == RouteNodeKind::Connector || route_.degree(at) != 2) break;
            const RouteSegment3D* on = nullptr;
            for (const RouteSegment3D& t : route_.segments)
                if (t.id != from && (t.a == at || t.b == at)) on = &t;
            if (!on) break;
            total += route_.length(*on);
            from = on->id;
            at = on->a == at ? on->b : on->a;
        }
        const RouteNode3D* end = route_.node(at);
        ways << (end && end->label != firstTo
                     ? QStringLiteral("to %1 %2 mm (to %3 %4 mm)").arg(end->label).arg(total, 0, 'f', 1).arg(firstTo).arg(first, 0, 'f', 1)
                     : QStringLiteral("to %1 %2 mm").arg(firstTo).arg(first, 0, 'f', 1));
    }
    return ways.isEmpty() ? QString() : QStringLiteral("%1: %2").arg(n->label, ways.join(QStringLiteral(";  ")));
}

void View3DPane::setRowsShown(bool on) {
    rowsShown_ = on;
    applyLayout();
    if (!on) refreshSolids();
}

void View3DPane::setCompact(bool on) {
    compact_ = on;
    applyLayout();
}

void View3DPane::applyLayout() {
    // AT THE SIDE (compact): the picture alone - no rows, no panels, no wheel, the words under it put away - and a button
    // to open it below with all of them. In the 3D Preview tab: the rows on the ribbon, what is in view on the left.
    const bool rows = rowsShown_ && !compact_;
    topRow_->setVisible(rows);
    for (QWidget* w : {envRow_, routeRow_, editRow_, bundleRow_, planeRow_})
        if (w) w->setVisible(rows && envChoice_);
    solidsPanel_->setVisible(!rowsShown_ && envChoice_ && !compact_);
    side_->setVisible(envChoice_ && !compact_);
    status_->setVisible(!compact_);
    if (wheel_) wheel_->setVisible(!compact_ && wheelShown_->isChecked());
    if (toBottom_) toBottom_->setVisible(compact_);
    for (QToolButton* b : {toLeft_, toRight_})
        if (b) b->setVisible(!compact_ && (dockedBelow_ || sideDocked_));
    if (sideRefresh_) sideRefresh_->setVisible(compact_);
    if (hoverSaid_ && compact_) hoverSaid_->hide();
    fitRows();
    placeWheel();
}

void View3DPane::showPartsList() {
    // IN THE 3D PREVIEW TAB: the list on the left shown or put away.
    if (!rowsShown_ && envChoice_) {
        solidsPanel_->setVisible(!solidsPanel_->isVisible());
        if (solidsPanel_->isVisible()) refreshSolids();
        return;
    }
    if (partsList_) {
        partsList_->raise();
        partsList_->activateWindow();
        return;
    }
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QStringLiteral("Parts in view - %1").arg(QFileInfo(envRef_).fileName()));
    dlg->resize(420, 620);
    auto* v = new QVBoxLayout(dlg);
    auto* filter = new QLineEdit(dlg);
    filter->setPlaceholderText(QStringLiteral("Filter by name"));
    v->addWidget(filter);
    auto* list = new QListWidget(dlg);
    list->setObjectName(QStringLiteral("view3dPartsList"));
    for (size_t i = 0; i < env_.parts.size(); ++i) {
        const QString name = env_.parts[i].name.isEmpty() ? QStringLiteral("Solid %1").arg(i + 1) : env_.parts[i].name;
        auto* it = new QListWidgetItem(name, list);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(canvas_->envPartHidden(static_cast<int>(i)) ? Qt::Unchecked : Qt::Checked);
        it->setData(Qt::UserRole, static_cast<int>(i));
    }
    v->addWidget(list, 1);
    auto* row = new QHBoxLayout;
    auto* all = new QPushButton(QStringLiteral("Show all"), dlg);
    auto* none = new QPushButton(QStringLiteral("Hide all"), dlg);
    auto* picked = new QPushButton(QStringLiteral("Only the one picked"), dlg);
    picked->setToolTip(QStringLiteral("Hide every solid but the one last picked in the view"));
    row->addWidget(all);
    row->addWidget(none);
    row->addWidget(picked);
    v->addLayout(row);
    auto apply = [this] {
        canvas_->routeRedraw();
        refreshRoute();
    };
    connect(filter, &QLineEdit::textChanged, list, [list](const QString& t) {
        for (int r = 0; r < list->count(); ++r) list->item(r)->setHidden(!t.isEmpty() && !list->item(r)->text().contains(t, Qt::CaseInsensitive));
    });
    connect(list, &QListWidget::itemChanged, this, [this, apply](QListWidgetItem* it) {
        canvas_->setEnvPartHidden(it->data(Qt::UserRole).toInt(), it->checkState() != Qt::Checked);
        apply();
    });
    auto setAll = [this, list, apply](bool shown) {
        const QSignalBlocker hold(list);
        for (int r = 0; r < list->count(); ++r)
            if (!list->item(r)->isHidden()) {
                list->item(r)->setCheckState(shown ? Qt::Checked : Qt::Unchecked);
                canvas_->setEnvPartHidden(list->item(r)->data(Qt::UserRole).toInt(), !shown);
            }
        apply();
    };
    connect(all, &QPushButton::clicked, this, [setAll] { setAll(true); });
    connect(none, &QPushButton::clicked, this, [setAll] { setAll(false); });
    connect(picked, &QPushButton::clicked, this, [this, list, apply] {
        const int keep = canvas_->lastPick().hit ? canvas_->lastPick().part : -1;
        if (keep < 0) return;
        const QSignalBlocker hold(list);
        for (int r = 0; r < list->count(); ++r) {
            const int i = list->item(r)->data(Qt::UserRole).toInt();
            list->item(r)->setCheckState(i == keep ? Qt::Checked : Qt::Unchecked);
            canvas_->setEnvPartHidden(i, i != keep);
        }
        apply();
    });
    partsList_ = dlg;
    dlg->show();
}

void View3DPane::syncCamera() {
    const Camera3D& cam = canvas_->camera();
    const QSignalBlocker a(turn_), b(tilt_), c(roll_);
    turn_->setValue(static_cast<int>(std::lround(wrapDegrees(cam.yawDeg))));
    tilt_->setValue(static_cast<int>(std::lround(wrapDegrees(cam.pitchDeg))));
    roll_->setValue(static_cast<int>(std::lround(wrapDegrees(cam.rollDeg))));
}

void View3DPane::syncBend() {
    // AUTO: the field showing six times the diameter, and not to be typed in; otherwise its own figure.
    const bool automatic = route_.bendRadius <= 0.0;
    const QSignalBlocker a(bendAuto_), b(bend_);
    bendAuto_->setChecked(automatic);
    bend_->setEnabled(!automatic);
    const double across = route_.wires > 0 ? Route3D::packedDiameter(route_.wires, route_.wireD) : route_.diameter;
    bend_->setValue(automatic ? across * 6.0 : route_.bendRadius);
    // THE DIAMETER: the wires' (its A on, the field showing what they pack to), or its own.
    if (diameterAuto_ && wires_ && wireD_) {
        const QSignalBlocker c(diameterAuto_), d(diameter_), e(wires_), f(wireD_);
        diameterAuto_->setChecked(route_.wires > 0);
        diameter_->setEnabled(route_.wires <= 0);
        diameter_->setValue(across);
        wires_->setValue(route_.wires);
        wireD_->setValue(route_.wireD);
    }
}

void View3DPane::editSelectedNode(const std::function<void(RouteNode3D&)>& change) {
    // A CONNECTOR BEING PUT, FROZEN WITH TAB: the change is to it - nothing yet in the route to undo.
    if (canvas_->ghostFrozen()) {
        change(canvas_->placeTemplate());
        canvas_->routeRedraw();
        syncConnector();
        return;
    }
    const QString id = canvas_->selectedNode();
    if (!route_.node(id) || fillingMove_) return;
    beforeRouteChange();
    change(*route_.node(id));
    saveRouting();
    refreshRoute();
    syncConnector();
    syncMove();
    emit routeChanged();
}

void View3DPane::moveSelectedNode(const V3& by) { editSelectedNode([by](RouteNode3D& n) { n.at = n.at + by; }); }

void View3DPane::setSelectedNodeColor(const QColor& c) { editSelectedNode([c](RouteNode3D& n) { n.color = c; }); }

void View3DPane::rotateSelectedNode(int axis, double degrees) {
    const QString id = canvas_->selectedNode();
    const RouteNode3D* n = route_.node(id);
    if (!n) return;
    // ROUND A WORLD AXIS: the way it points turned (Rodrigues), how it lies round itself kept.
    const V3 k = axis == 0 ? V3{1, 0, 0} : axis == 1 ? V3{0, 1, 0} : V3{0, 0, 1};
    const double a = degrees * M_PI / 180.0;
    const V3 v = n->normal;
    const V3 r = (v * std::cos(a) + cross3(k, v) * std::sin(a) + k * (dot3(k, v) * (1.0 - std::cos(a)))).normalized();
    const double spin = n->spinDeg;
    beforeRouteChange();
    route_.aimNode(id, r, spin);
    saveRouting();
    refreshRoute();
    syncConnector();
    syncMove();
    emit routeChanged();
}

void View3DPane::syncMove() {
    const RouteNode3D* n = route_.node(canvas_->selectedNode());
    const bool show = n && canvas_->routeTool() == RouteTool::Move && envChoice_;
    moveBox_->setVisible(show);
    updateWheel();
    if (!show) return;
    fillingMove_ = true;
    moveSaid_->setText(QStringLiteral("<b>Moving %1</b> - drag it, type where it goes, or use the wheel's arrows").arg(n->label.toHtmlEscaped()));
    const double at[3] = {n->at.x, n->at.y, n->at.z};
    for (int i = 0; i < 3; ++i) moveAt_[i]->setValue(at[i]);
    fillingMove_ = false;
}

void View3DPane::updateWheel() {
    // THE MOVE TOOL IN HAND: the wheel's segments its arrows along X, Y and Z and its turns round each - for the node
    // clicked; otherwise the view's.
    const bool moving = canvas_->routeTool() == RouteTool::Move && envChoice_;
    if (moving == wheelMoving_) return;
    wheelMoving_ = moving;
    if (moving) {
        buildMoveWheel();
        wheel_->setMiddle(QStringLiteral("Move"), [this](const QPoint&) {
            // The middle: done moving - the tool put down, the view's wheel back.
            setRouteTool(RouteTool::Select);
        });
    } else {
        wheel_->setItems(viewWheel_);
        wheel_->setMiddle(QStringLiteral("View"), [this](const QPoint& at) { wheelOptions(at); });
    }
    wheel_->update();
}

void View3DPane::buildMoveWheel() {
    // EACH ARROW WHERE ITS AXIS POINTS ON THE SCREEN: +X in the segment on the side +X runs to as the view is turned (left
    // where it goes left, right where it goes right), and drawn that way; the turns round each axis in the segments left.
    auto stepOf = [this] { return moveStep_->value(); };
    V3 right, up, forward;
    canvas_->camera().axes(right, up, forward);
    const int ring = 12;
    std::vector<NavWheel::Item> items(static_cast<size_t>(ring));
    std::vector<bool> taken(static_cast<size_t>(ring), false);
    struct Way {
        int axis, sign;
        double deg, shown;
    };
    std::vector<Way> ways;
    for (int axis = 0; axis < 3; ++axis)
        for (int sign : {1, -1}) {
            const V3 d = (axis == 0 ? V3{1, 0, 0} : axis == 1 ? V3{0, 1, 0} : V3{0, 0, 1}) * sign;
            const double sx = dot3(d, right), sy = dot3(d, up);
            ways.push_back({axis, sign, std::atan2(sy, sx) * 180.0 / M_PI, std::hypot(sx, sy)});
        }
    // Those that show longest first; one pointing straight at the eye takes what is left.
    std::sort(ways.begin(), ways.end(), [](const Way& a, const Way& b) { return a.shown > b.shown; });
    for (const Way& w : ways) {
        int best = -1;
        double nearest = 1e9;
        for (int i = 0; i < ring; ++i) {
            if (taken[static_cast<size_t>(i)]) continue;
            const double slot = 90.0 - i * 360.0 / ring;
            const double off = std::abs(std::remainder(w.deg - slot, 360.0));
            if (off < nearest) {
                nearest = off;
                best = i;
            }
        }
        taken[static_cast<size_t>(best)] = true;
        const QString key = QStringLiteral("%1%2").arg(QLatin1Char("xyz"[w.axis])).arg(w.sign > 0 ? QLatin1Char('+') : QLatin1Char('-'));
        const QString name = QStringLiteral("%1%2").arg(QChar(QLatin1Char("XYZ"[w.axis]))).arg(w.sign > 0 ? QLatin1Char('+') : QLatin1Char('-'));
        NavWheel::Item it{key, QStringLiteral("Move %1").arg(name), QStringLiteral("Move it a step along %1").arg(name),
                          [this, axis = w.axis, sign = w.sign, stepOf] {
                              if (!route_.node(canvas_->selectedNode())) {
                                  emit canvas_->message(QStringLiteral("Move: click the connector, clip or branch to move first"));
                                  return;
                              }
                              V3 by;
                              (axis == 0 ? by.x : axis == 1 ? by.y : by.z) = stepOf() * sign;
                              moveSelectedNode(by);
                          },
                          {}};
        it.arrowDeg = w.shown > 0.05 ? w.deg : 1e9;
        items[static_cast<size_t>(best)] = it;
    }
    int next = 0;
    for (int axis = 0; axis < 3; ++axis)
        for (int sign : {1, -1}) {
            while (next < ring && taken[static_cast<size_t>(next)]) ++next;
            if (next >= ring) break;
            taken[static_cast<size_t>(next)] = true;
            const QString key = QStringLiteral("r%1%2").arg(QLatin1Char("xyz"[axis])).arg(sign > 0 ? QLatin1Char('+') : QLatin1Char('-'));
            items[static_cast<size_t>(next)] = {key, QStringLiteral("Turn %1").arg(QChar(QLatin1Char("XYZ"[axis]))),
                                                QStringLiteral("Turn it 15° round %1").arg(QChar(QLatin1Char("XYZ"[axis]))),
                                                [this, axis, sign] {
                                                    if (!route_.node(canvas_->selectedNode())) {
                                                        emit canvas_->message(QStringLiteral("Move: click the connector, clip or branch to turn first"));
                                                        return;
                                                    }
                                                    rotateSelectedNode(axis, 15.0 * sign);
                                                }, {}};
        }
    wheel_->setItems(items);
    wheel_->update();
}

void View3DPane::syncConnector() {
    // THE CONNECTOR SELECTED - or the one being put, frozen with Tab.
    const bool frozen = canvas_->ghostFrozen();
    const RouteNode3D* n = frozen ? &canvas_->placeTemplate() : route_.node(canvas_->selectedNode());
    const bool show = n && (frozen || n->kind == RouteNodeKind::Connector) && envChoice_;
    connBox_->setVisible(show);
    if (!show) return;
    const QSignalBlocker a(aim_), b(aimTurn_), c(aimTilt_), d(spin_), e(connForm_);
    const QSignalBlocker f0(connSize_[0]), f1(connSize_[1]), f2(connSize_[2]), g0(connAuto_[0]), g1(connAuto_[1]), g2(connAuto_[2]);
    connForm_->setCurrentIndex(std::max(0, connForm_->findData(n->form.isEmpty() ? QStringLiteral("rect") : n->form)));
    {
        const QSignalBlocker hold(connBack_);
        connBack_->setCurrentIndex(n->back ? 1 : 0);
    }
    // EACH SIZE: its own, or - its "A" on - the real figure Auto comes to, not to be typed in.
    const double dd = std::max(0.5, route_.diameter);
    const double own[3] = {n->formW, n->formH, n->formL}, real[3] = {dd * 2.2, dd * 1.3, route_.housingLength(*n)};
    for (int i = 0; i < 3; ++i) {
        const bool automatic = own[i] <= 0.0;
        connAuto_[i]->setChecked(automatic);
        connSize_[i]->setEnabled(!automatic);
        connSize_[i]->setValue(automatic ? real[i] : own[i]);
    }
    for (QWidget* w : {static_cast<QWidget*>(aim_), static_cast<QWidget*>(aimTurn_), static_cast<QWidget*>(aimTilt_)}) w->setEnabled(!frozen);
    if (frozen) {
        connSaid_->setText(QStringLiteral("<b>The connector being put</b> - frozen (Tab): set it, then click or Enter to put it"));
        spin_->setValue(static_cast<int>(std::lround(wrapDegrees(n->spinDeg))));
        return;
    }
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
    if (canvas_->ghostFrozen()) {
        canvas_->placeTemplate().spinDeg = wrapDegrees(spinDeg);
        canvas_->routeRedraw();
        return;
    }
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

void View3DPane::scheduleRebuild() {
    // A burst of changes - a drag, a paste of many objects - is one rebuild, a moment after the last of them.
    if (!rebuildSoon_) {
        rebuildSoon_ = new QTimer(this);
        rebuildSoon_->setSingleShot(true);
        rebuildSoon_->setInterval(350);
        connect(rebuildSoon_, &QTimer::timeout, this, [this] { rebuild(false); });
    }
    rebuildSoon_->start();
}

void View3DPane::closeEvent(QCloseEvent* e) {
    // ITS OWN WINDOW CLOSED: back where it came from, not gone.
    if (fullScreen_) {
        e->ignore();
        setFullScreen(false);
        return;
    }
    QWidget::closeEvent(e);
}

// EVERY BUTTON WITH ITS PICTURE, its name beside it - as a 3D program's are.
void View3DPane::applyIcons() {
    const QColor ink = Chrome::ink();
    const struct {
        const char* name;
        const char* icon;
    } icons[] = {
        {"view3dRefresh", "refresh"},     {"view3dFit", "fit"},           {"view3dIso", "iso"},
        {"view3dTop", "top"},             {"view3dFront", "front"},       {"view3dSide", "side"},
        {"view3dFullScreen", "fullscreen"}, {"view3dLock", "lock"},       {"view3dHelp", "help"},
        {"view3dEnvLoad", "load"},        {"view3dEnvClear", "clear"},    {"view3dToolSelect", "select"},
        {"view3dToolConnector", "connector"}, {"view3dToolBranch", "branch"}, {"view3dToolClip", "clip"},
        {"view3dToolRoute", "route"},     {"view3dToolMove", "move"},     {"view3dToolStretch", "stretch"},
        {"view3dToolDelete", "delete"},   {"view3dRouteUndo", "undo"},    {"view3dLinkPart", "link"},
        {"view3dRouteRedo", "redo"},      {"view3dAttach", "attach"},
        {"view3dUnlinkPart", "unlink"},   {"view3dLengthsToHarness", "lengths"}, {"view3dWorkPlaneHere", "workplane"},
        {"view3dToolMeasure", "measure"}, {"view3dFlip", "flip"},         {"view3dParts", "parts"},
        {"view3dGravity", "gravity"},
    };
    for (const auto& i : icons)
        if (auto* b = findChild<QToolButton*>(QLatin1String(i.name))) {
            b->setIcon(icon3D(QLatin1String(i.icon), ink));
            b->setIconSize(QSize(18, 18));
            // THE VIEWS AND WHAT ONLY MOVES THE VIEW: their pictures alone (the name on the tip), as a CAD program's view
            // cube buttons are - the strip that much thinner.
            const QLatin1String name(i.name);
            const bool pictureOnly = name == QLatin1String("view3dRefresh") || name == QLatin1String("view3dFit") ||
                                     name == QLatin1String("view3dIso") || name == QLatin1String("view3dTop") ||
                                     name == QLatin1String("view3dFront") || name == QLatin1String("view3dSide") ||
                                     name == QLatin1String("view3dFlip") || name == QLatin1String("view3dFullScreen") ||
                                     name == QLatin1String("view3dEnvClear") || name == QLatin1String("view3dRouteUndo") || name == QLatin1String("view3dRouteRedo") ||
                                     name == QLatin1String("view3dHelp");
            b->setToolButtonStyle(pictureOnly ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
            if (pictureOnly && !b->toolTip().startsWith(QLatin1String("<b>"))) b->setToolTip(QStringLiteral("<b>%1</b> - %2").arg(b->text(), b->toolTip()));
        }
}

void View3DPane::setFullScreen(bool on) {
    if (on == fullScreen_) return;
    fullScreen_ = on;
    if (on) {
        // OUT OF WHEREVER IT IS - the dock, the wizard - as a window of its own, opened to fill the screen: a window
        // still, with its title bar, so it can be dragged, made smaller, or put on another screen. Back where it was
        // when it comes back, or when it is closed.
        normalFlags_ = windowFlags();
        setWindowFlags(Qt::Window);
        setWindowTitle(QStringLiteral("3D view"));
        showMaximized();
    } else {
        setWindowFlags(normalFlags_);
        showNormal();
        show();
    }
    const QSignalBlocker hold(fullBtn_);
    fullBtn_->setChecked(on);
    fullBtn_->setText(on ? QStringLiteral("Back") : QStringLiteral("Full screen"));
    fullBtn_->setIcon(icon3D(on ? QStringLiteral("back") : QStringLiteral("fullscreen"), Chrome::ink()));
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
    envChoice_ = shown;
    canvas_->setRoute(shown ? &route_ : nullptr);
    applyLayout();
}

void View3DPane::fitRows() {
    bool any = false;
    for (QWidget* w : {topRow_, envRow_, routeRow_, editRow_, bundleRow_, planeRow_}) any = any || (w && !w->isHidden());
    rowsArea_->setVisible(any);
    if (!any) return;
    // AS DEEP AS THE GROUPS COME TO at the width there is - scrolled across only where one group is wider than that.
    rowsLay_->activate();
    const int wide = rowsLay_->minimumSize().width();
    rowsBox_->setMinimumWidth(wide);
    const int width = std::max(wide, rowsArea_->viewport()->width());
    const int tall = rowsLay_->heightForWidth(width);
    const bool across = rowsArea_->viewport()->width() < wide;
    rowsArea_->setFixedHeight(tall + 4 + (across ? rowsArea_->horizontalScrollBar()->sizeHint().height() : 0));
}

QString View3DPane::envPartName(int part) const {
    if (part < 0 || part >= static_cast<int>(env_.parts.size())) return QString();
    const QString name = env_.parts[static_cast<size_t>(part)].name;
    return name.isEmpty() ? QStringLiteral("solid %1").arg(part + 1) : name;
}

QStringList View3DPane::harnessBricks() const {
    QStringList out;
    for (const Harness3DItem& it : scene_.items)
        if (!it.brick.isEmpty() && !out.contains(it.brick)) out << it.brick;
    return out;
}

QString View3DPane::harnessPartName(const QString& brick) const {
    // A part's name where it is shown as a part; a run's where it is a run.
    QString any;
    for (const Harness3DItem& it : scene_.items) {
        if (it.brick != brick) continue;
        if ((it.model || it.cube) && !it.what.isEmpty()) return it.what;
        if (any.isEmpty()) any = it.what;
    }
    return any.isEmpty() ? brick : any;
}

void View3DPane::refreshSolids() {
    fillingSolids_ = true;
    solidsTree_->clear();
    if (!env_.parts.empty()) {
        auto* top = new QTreeWidgetItem(solidsTree_, {QStringLiteral("Model - %1").arg(QFileInfo(envRef_).fileName())});
        top->setData(0, Qt::UserRole, 0);
        top->setFlags(top->flags() | Qt::ItemIsUserCheckable);
        int shown = 0;
        for (size_t i = 0; i < env_.parts.size(); ++i) {
            auto* it = new QTreeWidgetItem(top, {envPartName(static_cast<int>(i))});
            it->setData(0, Qt::UserRole, 1);
            it->setData(0, Qt::UserRole + 1, static_cast<int>(i));
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            const bool on = !canvas_->envPartHidden(static_cast<int>(i));
            it->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
            shown += on ? 1 : 0;
        }
        top->setCheckState(0, shown == 0 ? Qt::Unchecked : shown == static_cast<int>(env_.parts.size()) ? Qt::Checked : Qt::PartiallyChecked);
        top->setExpanded(true);
    }
    const QStringList bricks = harnessBricks();
    if (!bricks.isEmpty()) {
        auto* top = new QTreeWidgetItem(solidsTree_, {QStringLiteral("Harness")});
        top->setData(0, Qt::UserRole, 0);
        top->setFlags(top->flags() | Qt::ItemIsUserCheckable);
        int shown = 0;
        for (const QString& b : bricks) {
            auto* it = new QTreeWidgetItem(top, {harnessPartName(b)});
            it->setData(0, Qt::UserRole, 2);
            it->setData(0, Qt::UserRole + 1, b);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            const bool on = !canvas_->brickHidden(b);
            it->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
            shown += on ? 1 : 0;
        }
        top->setCheckState(0, shown == 0 ? Qt::Unchecked : shown == bricks.size() ? Qt::Checked : Qt::PartiallyChecked);
        top->setExpanded(true);
    }
    fillingSolids_ = false;
}

void View3DPane::setWires(const QString& seg, int wires, double wireD) {
    beforeRouteChange();
    if (RouteSegment3D* s = route_.segment(seg)) {
        s->wires = std::max(0, wires);
        s->wireD = wires > 0 ? std::max(0.05, wireD) : 0.0;
        if (wires > 0) s->diameter = 0.0;   // the wires make it
    } else {
        route_.wires = std::max(0, wires);
        route_.wireD = std::max(0.05, wireD);
    }
    route_.solidsChanged();
    saveRouting();
    refreshRoute();
    syncBend();
    emit routeChanged();
}

int View3DPane::attachToSurface() {
    if (!envIndex_ || envIndex_->empty()) return 0;
    static const V3 downs[6] = {{0, 0, -1}, {0, -1, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, {1, 0, 0}};
    const V3 down = downs[std::clamp(down_ ? down_->currentIndex() : 0, 0, 5)];
    auto cast = [this](const V3& from, const V3& dir) {
        return envIndex_->cast(from, dir, [this](int part, const V3&) { return canvas_->envPartHidden(part); });
    };
    std::vector<QString> which;
    if (route_.segment(canvas_->selectedSegment())) which.push_back(canvas_->selectedSegment());
    else
        for (const RouteSegment3D& s : route_.segments) which.push_back(s.id);
    if (which.empty()) return 0;
    beforeRouteChange();
    int laid = 0;
    for (const QString& id : which) {
        const RouteSegment3D* s = route_.segment(id);
        if (!s) continue;
        const double r = route_.diameterOf(*s) / 2.0;
        const std::vector<V3> path = route_.centreline(*s);
        if (path.size() < 2) continue;
        // ALONG IT, A POINT EVERY SO OFTEN - clear of its two ends (held by their nodes, straight out of a connector).
        const RouteNode3D* na = route_.node(s->a);
        const RouteNode3D* nb = route_.node(s->b);
        const double skipA = na && na->kind == RouteNodeKind::Connector ? route_.leadOf(*na, r) : r * 2.0;
        const double skipB = nb && nb->kind == RouteNodeKind::Connector ? route_.leadOf(*nb, r) : r * 2.0;
        double total = 0.0;
        for (size_t i = 1; i < path.size(); ++i) total += (path[i] - path[i - 1]).length();
        const double step = std::max({4.0 * r, route_.bendRadiusOf(*s) * 0.5, total / 60.0});
        std::vector<RouteVia3D> via;
        double along = 0.0, next = skipA + step * 0.5;
        for (size_t i = 1; i < path.size(); ++i) {
            const V3 a = path[i - 1], b = path[i];
            const double l = (b - a).length();
            while (l > 1e-9 && next <= along + l && next < total - skipB) {
                const V3 p = a + (b - a) * ((next - along) / l);
                next += step;
                // UNDER IT, the way gravity pulls; or the nearest wall beside it, within a few of its diameters; or, with
                // nothing near, where it is.
                Pick3D hit = cast(p - down * r, down);
                if (!hit.hit || hit.distance > std::max(300.0, total)) {
                    hit = Pick3D();
                    for (const V3& side : {V3{1, 0, 0}, V3{-1, 0, 0}, V3{0, 1, 0}, V3{0, -1, 0}, V3{0, 0, 1}, V3{0, 0, -1}}) {
                        if (std::abs(dot3(side, down)) > 0.5) continue;
                        const Pick3D w = cast(p, side);
                        if (w.hit && w.distance < r * 8.0 && (!hit.hit || w.distance < hit.distance)) hit = w;
                    }
                }
                if (hit.hit) via.push_back({hit.point, hit.normal.normalized()});
                else via.push_back({p + down * r, down * -1.0});
            }
            along += l;
        }
        if (via.empty()) continue;
        route_.segment(id)->via = via;
        ++laid;
    }
    if (laid == 0) {
        history_.pop_back();
        return 0;
    }
    route_.solidsChanged();
    saveRouting();
    refreshRoute();
    emit routeChanged();
    return laid;
}

void View3DPane::startBranchFrom(const QString& node, bool back) {
    const RouteNode3D* n = route_.node(node);
    if (!n) return;
    if (n->kind == RouteNodeKind::Connector && n->back != back) setConnectorBack(node, back);
    setRouteTool(RouteTool::Route);
    canvas_->beginLaying(node);
    emit canvas_->message(QStringLiteral("A bundle from the %1 of %2 - click the points it runs through, then the node it ends on")
                              .arg(back ? QStringLiteral("back") : QStringLiteral("front"), n->label));
}

void View3DPane::setConnectorBack(const QString& node, bool back) {
    RouteNode3D* n = route_.node(node);
    if (!n || n->back == back) return;
    beforeRouteChange();
    n->back = back;
    saveRouting();
    refreshRoute();
    syncConnector();
    emit routeChanged();
}

void View3DPane::fillOpacityMenu(QMenu* menu, int envPart, const QString& brick) {
    const double now = envPart >= 0 ? canvas_->envPartOpacity(envPart) : canvas_->brickOpacity(brick);
    auto set = [this, envPart, brick](double a) {
        if (envPart >= 0) canvas_->setEnvPartOpacity(envPart, a);
        else canvas_->setBrickOpacity(brick, a);
    };
    QAction* same = menu->addAction(envPart >= 0 ? QStringLiteral("As the model (the Opacity slider)") : QStringLiteral("Solid, as drawn"));
    same->setCheckable(true);
    same->setChecked(now <= 0.0);
    connect(same, &QAction::triggered, this, [set] { set(0.0); });
    menu->addSeparator();
    for (int pc : {100, 75, 50, 25, 10}) {
        QAction* a = menu->addAction(QStringLiteral("%1%").arg(pc));
        a->setObjectName(QStringLiteral("view3dOpacity%1").arg(pc));
        a->setCheckable(true);
        a->setChecked(now > 0.0 && std::abs(now * 100.0 - pc) < 0.5);
        connect(a, &QAction::triggered, this, [set, pc] { set(pc / 100.0); });
    }
    QAction* own = menu->addAction(QStringLiteral("Other..."));
    connect(own, &QAction::triggered, this, [this, set, now] {
        bool ok = false;
        const int pc = QInputDialog::getInt(this, QStringLiteral("Opacity"), QStringLiteral("How solid it is drawn (%)"),
                                            now > 0.0 ? static_cast<int>(std::lround(now * 100.0)) : 100, 5, 100, 5, &ok);
        if (ok) set(pc / 100.0);
    });
}

void View3DPane::partsShownChanged() {
    canvas_->routeRedraw();
    refreshRoute();
    refreshSolids();
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
    const bool wasEmpty = scene_.runs == 0 && scene_.parts == 0;
    scene_ = buildHarness3D(ents, modelOf_, partLook_, pinsOf_);
    // NOTHING ON THE SHEET TO SHOW - no run, no part: the wizard's harness, if one is open. A connector put on the
    // sheet by itself is something to show, and is shown.
    bool fell = false;
    if (!fromWizard && wizard_ && scene_.runs == 0 && scene_.parts == 0) {
        ents = wizard_();
        scene_ = buildHarness3D(ents, modelOf_, partLook_, pinsOf_);
        fell = true;
    }
    // The first thing there is to see, brought into view.
    if (wasEmpty && (scene_.runs > 0 || scene_.parts > 0)) fit = true;
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
    if (solidsPanel_ && solidsPanel_->isVisible()) refreshSolids();
    refreshParts();
}

void View3DPane::sayStatus() {
    QStringList lines{sceneSaid_};
    if (loading_)
        lines << QStringLiteral("Environment: reading %1...").arg(loadingName_);
    else if (!env_.parts.empty() || !env_.how.isEmpty())
        lines << QStringLiteral("Environment: %1").arg(env_.how);
    if (!pickSaid_.isEmpty()) lines << pickSaid_;
    if (!gravitySaid_.isEmpty()) lines << gravitySaid_;
    if (!keepSaid_.isEmpty()) lines << keepSaid_;
    const QString either = nodeLengths();
    if (!either.isEmpty()) lines << either;
    status_->setText(lines.join(QLatin1Char('\n')));
}

// ---- The environment ------------------------------------------------------------------------------

bool View3DPane::loadEnvironment(const QString& path, bool wait) {
    if (loading_) return false;
    envRef_ = path;
    auto env = std::make_shared<Environment3D>();
    auto index = std::make_shared<RayIndex3D>();
    // HOW FAR IT HAS GOT, told by the reading thread and read off by the pane's own.
    struct Progress {
        std::atomic<int> pct{0};
        std::mutex m;
        QString stage;
    };
    auto prog = std::make_shared<Progress>();
    auto work = [path, env, index, prog] {
        *env = loadEnvironment3D(path, [prog](int pct, const QString& stage) {
            prog->pct = pct;
            const std::lock_guard<std::mutex> hold(prog->m);
            prog->stage = stage;
        });
        prog->pct = 96;
        {
            const std::lock_guard<std::mutex> hold(prog->m);
            prog->stage = QStringLiteral("Indexing it for clicking");
        }
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
    // THE BAR: shown while it reads, moved on as it goes, the stage and the time beside it.
    envProgress_->setValue(0);
    envProgress_->setFormat(QStringLiteral("%1 - starting").arg(loadingName_));
    envProgress_->show();
    emit loadProgress(0, envProgress_->format(), true);
    auto* tick = new QTimer(this);
    auto started = std::make_shared<QElapsedTimer>();
    started->start();
    connect(tick, &QTimer::timeout, this, [this, prog, started] {
        QString stage;
        {
            const std::lock_guard<std::mutex> hold(prog->m);
            stage = prog->stage;
        }
        envProgress_->setValue(prog->pct);
        envProgress_->setFormat(QStringLiteral("%1 - %2 (%3 s)").arg(loadingName_, stage).arg(started->elapsed() / 1000.0, 0, 'f', 1));
        emit loadProgress(prog->pct, envProgress_->format(), true);
    });
    tick->start(100);
    connect(loading_, &QThread::finished, this, [this, env, index, tick] {
        tick->stop();
        tick->deleteLater();
        envProgress_->setValue(100);
        envProgress_->setFormat(QStringLiteral("%1 - read").arg(loadingName_));
        emit loadProgress(100, envProgress_->format(), false);
        QTimer::singleShot(600, envProgress_, [bar = envProgress_] { bar->hide(); });
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
    canvas_->setAllEnvPartsHidden(false);
    armSolids();
    if (solidsPanel_ && solidsPanel_->isVisible()) QTimer::singleShot(0, this, [this] { refreshSolids(); });
    if (partsList_) partsList_->close();
    pickSaid_.clear();
    canvas_->clearPick();
    canvas_->setEnvironment(&env_, envIndex_.get());
    applySection();
    V3 lo, hi;
    if (env_.bounds(lo, hi)) canvas_->fitBox(lo, hi);
    // HOW THE DRAWING SHOWED IT, saved with it: put back now the model is read.
    if (!pendingView_.isEmpty()) {
        const QJsonObject view = pendingView_;
        pendingView_ = QJsonObject();
        applyView(view);
    }
    // Asked for and not read: the drawing is not to name it. Named by the drawing and not read: it still names it.
    if (!quietLoad_) {
        if (env_.empty()) envRef_.clear();
        saveRouting();
    }
    quietLoad_ = false;
    runGravity();
    sayStatus();
    emit environmentChanged();
}

void View3DPane::clearEnvironment(bool remember) {
    if (loading_) return;
    // A CHANGE TO UNDO: the model it was routed in comes back with Undo.
    if (remember && !envRef_.isEmpty()) {
        beforeRouteChange();
        if (undo_) undo_->setEnabled(true);
    }
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
    armSolids();
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
    // HOW IT WAS SHOWN, saved with it: put back - what needs the model again once it is read.
    {
        const QJsonObject view = o.value(QStringLiteral("view")).toObject();
        pendingView_ = loading_ ? view : QJsonObject();
        if (!view.isEmpty()) applyView(view);
    }
    {
        filling_ = true;
        diameter_->setValue(route_.diameter);
        lead_->setValue(route_.lead);
        filling_ = false;
        syncBend();
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
    // HOW IT IS SHOWN, with it: the view, gravity, what is hidden or see-through, the section, the rulers.
    if (!o.isEmpty()) o[QStringLiteral("view")] = viewState();
    saveRouting_(o);
}

QJsonObject View3DPane::routeState() const {
    QJsonObject was = route_.toJson();
    was.insert(QStringLiteral("environmentWas"), envRef_);   // what Undo reads the model back from
    return was;
}

void View3DPane::beforeRouteChange() {
    history_.push_back(routeState());
    if (history_.size() > 100) history_.erase(history_.begin());
    // Something new done: what Undo took back cannot be put back over it.
    future_.clear();
}

void View3DPane::undoRoute() {
    if (history_.empty()) return;
    const QJsonObject back = history_.back();
    history_.pop_back();
    future_.push_back(routeState());
    restoreRoute(back);
}

void View3DPane::redoRoute() {
    if (future_.empty()) return;
    const QJsonObject again = future_.back();
    future_.pop_back();
    history_.push_back(routeState());
    restoreRoute(again);
}

void View3DPane::restoreRoute(const QJsonObject& back) {
    route_ = Route3D::fromJson(back);
    armSolids();
    // THE MODEL AS IT WAS: read again where it has gone, taken away where it was not there.
    const QString envWas = back.value(QStringLiteral("environmentWas")).toString();
    if (back.contains(QStringLiteral("environmentWas")) && envWas != envRef_) {
        if (envWas.isEmpty()) clearEnvironment(false);
        else loadEnvironment(envWas);
    }
    {
        filling_ = true;
        diameter_->setValue(route_.diameter);
        lead_->setValue(route_.lead);
        filling_ = false;
        syncBend();
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
    emit routeToolChanged(tool);
}

RouteTool View3DPane::routeTool() const { return canvas_->routeTool(); }

void View3DPane::chooseEnvironment() {
    const QString dir = QSettings().value(QStringLiteral("view3d/environmentDir")).toString();
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Load an environment"), dir, environment3DFileFilter());
    if (path.isEmpty()) return;
    QSettings().setValue(QStringLiteral("view3d/environmentDir"), QFileInfo(path).absolutePath());
    beforeRouteChange();   // Undo puts the model back as it was
    loadEnvironment(path);
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
        // ITS DIAMETER: its own, its wires' (so many of so thick, and what they pack to), or the route's.
        const int nw = route_.wiresOf(s);
        const QString diaText = s.diameter > 0.0 ? QString::number(s.diameter, 'f', 1)
                                : nw > 0         ? QStringLiteral("%1×%2 → %3").arg(nw).arg(route_.wireDOf(s), 0, 'g', 3)
                                                       .arg(route_.diameterOf(s), 0, 'f', 1)
                                                 : QStringLiteral("%1 (route)").arg(route_.diameter, 0, 'f', 1);
        auto* dia = new QTableWidgetItem(diaText);
        dia->setToolTip(QStringLiteral("This bundle's diameter - empty for the route's; a bundle of wires: right-click it, Wires in it..."));
        table_->setItem(row, 0, name);
        table_->setItem(row, 1, length);
        table_->setItem(row, 2, dia);
        // HOW TAUT THIS ONE IS PUT IN: the route's, or one of the choices.
        auto* tension = new QComboBox(table_);
        tension->setObjectName(QStringLiteral("view3dTension_%1").arg(s.id));
        tension->addItem(QStringLiteral("As all (%1)").arg(cableTensionName(route_.tension)), -1.0);
        for (const CableTension& choice : cableTensions()) tension->addItem(choice.name, choice.newtons);
        int pick = 0;
        for (int i = 1; i < tension->count(); ++i)
            if (s.tension >= 0.0 && std::abs(tension->itemData(i).toDouble() - s.tension) < 1e-9) pick = i;
        if (s.tension >= 0.0 && pick == 0) {
            tension->addItem(cableTensionName(s.tension), s.tension);
            pick = tension->count() - 1;
        }
        tension->setCurrentIndex(pick);
        tension->setToolTip(QStringLiteral("How taut this bundle is put in - the less, the more it sags where it hangs"));
        const QString id = s.id;
        connect(tension, &QComboBox::currentIndexChanged, this, [this, tension, id](int i) {
            if (filling_ || i < 0 || !route_.segment(id)) return;
            beforeRouteChange();
            route_.segment(id)->tension = tension->itemData(i).toDouble();
            saveRouting();
            // The table built again after this choice is done with - not from under it.
            QTimer::singleShot(0, this, [this] {
                refreshRoute();
                emit routeChanged();
            });
        });
        table_->setCellWidget(row, 3, tension);
        if (s.id == selected) table_->selectRow(row);
    }
    // THE TOTAL, the last row, lit - every bundle's length added up.
    if (!route_.segments.empty()) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        const QColor lit(47, 133, 90), ink(255, 255, 255);
        QFont bold = table_->font();
        bold.setBold(true);
        const QString texts[4] = {QStringLiteral("Total"), QStringLiteral("%1 mm").arg(total, 0, 'f', 1), QString(), QString()};
        for (int c = 0; c < 4; ++c) {
            auto* it = new QTableWidgetItem(texts[c]);
            it->setFlags(Qt::ItemIsEnabled);
            it->setBackground(lit);
            it->setForeground(ink);
            it->setFont(bold);
            if (c == 1) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table_->setItem(row, c, it);
        }
        table_->item(row, 0)->setData(Qt::UserRole + 1, true);   // the total, not a bundle
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
    syncTension();
    syncCableTension();
    filling_ = false;
    canvas_->routeRedraw();
    runGravity();
    refreshKeepOuts();
    sayStatus();
}

// ---- Keep-outs --------------------------------------------------------------------------------------

void View3DPane::refreshKeepOuts() {
    const std::vector<KeepOutHit> hits = keepOutHits(route_);
    QStringList said;
    for (const KeepOutHit& h : hits) {
        const RouteSegment3D* s = route_.segment(h.segment);
        const RouteNode3D* a = s ? route_.node(s->a) : nullptr;
        const RouteNode3D* b = s ? route_.node(s->b) : nullptr;
        const KeepOut3D* k = route_.keepOut(h.keepOut);
        said << QStringLiteral("%1 - %2 passes through %3%4")
                    .arg(a ? a->label : QString(), b ? b->label : QString(), k ? keepOutName(*k) : h.keepOut,
                         h.block ? QStringLiteral(" (blocks)") : QString());
    }
    keepSaid_ = said.isEmpty() ? QString() : QStringLiteral("Keep-out: ") + said.join(QStringLiteral("; "));
    canvas_->setKeepOuts(hits, keepSaid_);
    if (keepDialog_ && keepDialog_->isVisible() && !filling_) fillKeepOutTable();
}

bool View3DPane::sceneBounds(V3& lo, V3& hi) const {
    if (env_.bounds(lo, hi)) return true;
    bool any = false;
    for (const RouteNode3D& n : route_.nodes) {
        lo = any ? V3{std::min(lo.x, n.at.x), std::min(lo.y, n.at.y), std::min(lo.z, n.at.z)} : n.at;
        hi = any ? V3{std::max(hi.x, n.at.x), std::max(hi.y, n.at.y), std::max(hi.z, n.at.z)} : n.at;
        any = true;
    }
    if (!any) {
        lo = {-100, -100, -100};
        hi = {100, 100, 100};
    }
    return any;
}

QString View3DPane::addKeepOutLevel(int axis, double at, bool block) {
    beforeRouteChange();
    KeepOut3D k;
    k.axis = std::clamp(axis, 0, 2);
    k.at = at;
    k.block = block;
    const QString id = route_.addKeepOut(k);
    saveRouting();
    refreshRoute();
    emit routeChanged();
    return id;
}

QString View3DPane::addKeepOutLevel(int axis) {
    V3 lo, hi;
    sceneBounds(lo, hi);
    const V3 mid = (lo + hi) * 0.5;
    return addKeepOutLevel(axis, axis == 0 ? mid.x : axis == 1 ? mid.y : mid.z, false);
}

QString View3DPane::addKeepOutZone(const V3& lo, const V3& hi, bool block) {
    beforeRouteChange();
    KeepOut3D k;
    k.zone = true;
    k.lo = lo;
    k.hi = hi;
    k.block = block;
    const QString id = route_.addKeepOut(k);
    saveRouting();
    refreshRoute();
    emit routeChanged();
    return id;
}

QString View3DPane::addKeepOutZone() {
    V3 lo, hi;
    sceneBounds(lo, hi);
    const V3 third = (hi - lo) * (1.0 / 3.0);
    return addKeepOutZone(lo + third, hi - third, false);
}

void View3DPane::setKeepOutsShown(bool on) {
    if (keepShown_) keepShown_->setChecked(on);
    canvas_->setKeepOutsShown(on);
}

void View3DPane::showKeepOuts() {
    if (!keepDialog_) {
        auto* dlg = new QDialog(this);
        dlg->setObjectName(QStringLiteral("view3dKeepOutsDialog"));
        dlg->setWindowTitle(QStringLiteral("Keep-outs"));
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        auto* lay = new QVBoxLayout(dlg);
        auto* say = new QLabel(QStringLiteral("Where no cable may go. A <b>level</b> is a plane across X, Y or Z at a place "
                                              "along it; a <b>zone</b> is a box. <b>Block</b>: a change that takes a cable "
                                              "through it is refused. <b>Alert</b>: it is allowed, and the cable is marked "
                                              "and warned of."),
                               dlg);
        say->setWordWrap(true);
        lay->addWidget(say);
        auto* table = new QTableWidget(0, 7, dlg);
        table->setObjectName(QStringLiteral("view3dKeepOutTable"));
        table->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Kind"), QStringLiteral("Axis"), QStringLiteral("At"),
                                          QStringLiteral("From (x, y, z)"), QStringLiteral("To (x, y, z)"), QStringLiteral("Rule")});
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table->horizontalHeader()->setStretchLastSection(true);
        lay->addWidget(table, 1);
        auto* row = new QHBoxLayout;
        auto add = [&](const char* name, const QString& text, auto fn) {
            auto* b = new QPushButton(text, dlg);
            b->setObjectName(QLatin1String(name));
            connect(b, &QPushButton::clicked, this, fn);
            row->addWidget(b);
            return b;
        };
        add("view3dAddLevelX", QStringLiteral("Add level X"), [this] { addKeepOutLevel(0); });
        add("view3dAddLevelY", QStringLiteral("Add level Y"), [this] { addKeepOutLevel(1); });
        add("view3dAddLevelZ", QStringLiteral("Add level Z"), [this] { addKeepOutLevel(2); });
        add("view3dAddZone", QStringLiteral("Add zone"), [this] { addKeepOutZone(); });
        add("view3dDeleteKeepOut", QStringLiteral("Delete"), [this, table] {
            const int r = table->currentRow();
            if (r < 0 || r >= static_cast<int>(route_.keepOuts.size())) return;
            beforeRouteChange();
            route_.keepOuts.erase(route_.keepOuts.begin() + r);
            saveRouting();
            refreshRoute();
            emit routeChanged();
        });
        row->addStretch(1);
        lay->addLayout(row);
        dlg->resize(860, 320);
        keepDialog_ = dlg;
    }
    fillKeepOutTable();
    keepDialog_->show();
    keepDialog_->raise();
}

void View3DPane::fillKeepOutTable() {
    auto* table = keepDialog_ ? keepDialog_->findChild<QTableWidget*>(QStringLiteral("view3dKeepOutTable")) : nullptr;
    if (!table) return;
    const int current = table->currentRow();
    table->setRowCount(0);
    // A CHANGE FROM THE TABLE: one step to undo; the table filled again after the edit is done with.
    auto change = [this](const QString& id, const std::function<void(KeepOut3D&)>& how) {
        KeepOut3D* k = route_.keepOut(id);
        if (!k) return;
        beforeRouteChange();
        how(*k);
        saveRouting();
        QTimer::singleShot(0, this, [this] {
            refreshRoute();
            emit routeChanged();
        });
    };
    auto vec = [](const V3& v) { return QStringLiteral("%1, %2, %3").arg(v.x, 0, 'f', 1).arg(v.y, 0, 'f', 1).arg(v.z, 0, 'f', 1); };
    auto parse = [](const QString& text, V3& v) {
        const QStringList p = QString(text).replace(QLatin1Char(';'), QLatin1Char(',')).split(QLatin1Char(','));
        if (p.size() != 3) return false;
        bool a = false, b = false, c = false;
        v = {p[0].trimmed().toDouble(&a), p[1].trimmed().toDouble(&b), p[2].trimmed().toDouble(&c)};
        return a && b && c;
    };
    for (const KeepOut3D& k : route_.keepOuts) {
        const int r = table->rowCount();
        table->insertRow(r);
        const QString id = k.id;
        auto* name = new QLineEdit(k.name, table);
        name->setPlaceholderText(keepOutName(k));
        connect(name, &QLineEdit::editingFinished, this, [change, id, name] {
            change(id, [name](KeepOut3D& o) { o.name = name->text().trimmed(); });
        });
        table->setCellWidget(r, 0, name);
        auto* kind = new QComboBox(table);
        kind->addItems({QStringLiteral("Level"), QStringLiteral("Zone")});
        kind->setCurrentIndex(k.zone ? 1 : 0);
        connect(kind, &QComboBox::currentIndexChanged, this, [this, change, id](int i) {
            change(id, [this, i](KeepOut3D& o) {
                o.zone = i == 1;
                if (o.zone && (o.hi - o.lo).length() < 1e-9) {
                    V3 lo, hi;
                    sceneBounds(lo, hi);
                    const V3 third = (hi - lo) * (1.0 / 3.0);
                    o.lo = lo + third;
                    o.hi = hi - third;
                }
            });
        });
        table->setCellWidget(r, 1, kind);
        auto* axis = new QComboBox(table);
        axis->addItems({QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")});
        axis->setCurrentIndex(k.axis);
        axis->setEnabled(!k.zone);
        connect(axis, &QComboBox::currentIndexChanged, this, [change, id](int i) { change(id, [i](KeepOut3D& o) { o.axis = i; }); });
        table->setCellWidget(r, 2, axis);
        auto* at = new QDoubleSpinBox(table);
        at->setRange(-1e6, 1e6);
        at->setDecimals(1);
        at->setSuffix(QStringLiteral(" mm"));
        at->setValue(k.at);
        at->setKeyboardTracking(false);
        at->setEnabled(!k.zone);
        connect(at, &QDoubleSpinBox::valueChanged, this, [change, id](double v) { change(id, [v](KeepOut3D& o) { o.at = v; }); });
        table->setCellWidget(r, 3, at);
        for (int c = 0; c < 2; ++c) {
            auto* corner = new QLineEdit(vec(c == 0 ? k.lo : k.hi), table);
            corner->setEnabled(k.zone);
            connect(corner, &QLineEdit::editingFinished, this, [change, parse, id, corner, c] {
                V3 v;
                if (!parse(corner->text(), v)) return;
                change(id, [v, c](KeepOut3D& o) { (c == 0 ? o.lo : o.hi) = v; });
            });
            table->setCellWidget(r, 4 + c, corner);
        }
        auto* rule = new QComboBox(table);
        rule->addItems({QStringLiteral("Alert"), QStringLiteral("Block")});
        rule->setCurrentIndex(k.block ? 1 : 0);
        connect(rule, &QComboBox::currentIndexChanged, this, [change, id](int i) { change(id, [i](KeepOut3D& o) { o.block = i == 1; }); });
        table->setCellWidget(r, 6, rule);
    }
    if (current >= 0 && current < table->rowCount()) table->setCurrentCell(current, 0);
}

void View3DCanvas::setKeepOuts(std::vector<KeepOutHit> hits, const QString& alert) {
    keepHits_ = std::move(hits);
    keepAlert_ = alert;
    keepDirty_ = true;
    update();
}

void View3DCanvas::setKeepOutsShown(bool on) {
    keepShown_ = on;
    keepDirty_ = true;
    update();
}

void View3DPane::syncCable() {
    const RouteSegment3D* s = route_.segment(canvas_->selectedSegment());
    const bool show = s && envChoice_;
    cableBox_->setVisible(show);
    if (!show) return;
    const bool was = filling_;
    filling_ = true;
    const QSignalBlocker a(cableLength_), b(cableDia_), c(cableDiaAuto_), d(cableWires_), e(cableWireD_), f(cableWireLook_);
    const RouteNode3D* na = route_.node(s->a);
    const RouteNode3D* nb = route_.node(s->b);
    cableSaid_->setText(QStringLiteral("<b>Cable %1 - %2</b>").arg((na ? na->label : s->a).toHtmlEscaped(), (nb ? nb->label : s->b).toHtmlEscaped()));
    cableLength_->setValue(route_.length(*s));
    const bool automatic = s->diameter <= 0.0;
    cableDiaAuto_->setChecked(automatic);
    cableDia_->setEnabled(!automatic);
    cableDia_->setValue(route_.diameterOf(*s));
    cableWires_->setValue(s->wires);
    cableWireD_->setValue(route_.wireDOf(*s));
    cableWireLook_->setCurrentIndex(s->wireSame ? 1 : 0);
    filling_ = was;
}

void View3DPane::editSegment(const QString& seg, const std::function<void(RouteSegment3D&)>& change) {
    RouteSegment3D* s = route_.segment(seg);
    if (!s) return;
    beforeRouteChange();
    change(*s);
    route_.solidsChanged();
    saveRouting();
    refreshRoute();
    syncCable();
    emit routeChanged();
}

bool View3DPane::setSegmentLength(const QString& seg, double mm) {
    if (!route_.segment(seg) || mm <= 0.0) return false;
    beforeRouteChange();
    route_.lengthen(seg, mm);
    saveRouting();
    refreshRoute();
    syncCable();
    emit routeChanged();
    return true;
}

void View3DPane::setWireColours(const QString& seg, bool same, const std::vector<QColor>& colours) {
    editSegment(seg, [same, colours](RouteSegment3D& s) {
        s.wireSame = same;
        s.wireColors = colours;
    });
}

void View3DPane::chooseWireColours(const QString& seg) {
    const RouteSegment3D* s = route_.segment(seg);
    if (!s) return;
    const int n = std::max(1, route_.wiresOf(*s));
    std::vector<QColor> colours = s->wireColors;
    colours.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        if (!colours[static_cast<size_t>(i)].isValid()) colours[static_cast<size_t>(i)] = wireColourOf(i);
    // EACH WIRE AND ITS COLOUR: double-click one (or choose it and Change...) for another.
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("The wires' colours"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* list = new QListWidget(&dlg);
    list->setObjectName(QStringLiteral("view3dWireColours"));
    auto fill = [&] {
        list->clear();
        for (int i = 0; i < n; ++i) {
            QPixmap swatch(16, 16);
            swatch.fill(colours[static_cast<size_t>(i)]);
            list->addItem(new QListWidgetItem(QIcon(swatch), QStringLiteral("Wire %1  (%2)").arg(i + 1).arg(colours[static_cast<size_t>(i)].name())));
        }
    };
    fill();
    lay->addWidget(list);
    auto change = [&] {
        const int row = list->currentRow();
        if (row < 0) return;
        const QColor c = QColorDialog::getColor(colours[static_cast<size_t>(row)], &dlg, QStringLiteral("Wire %1").arg(row + 1));
        if (!c.isValid()) return;
        colours[static_cast<size_t>(row)] = c;
        fill();
        list->setCurrentRow(row);
    };
    connect(list, &QListWidget::itemDoubleClicked, &dlg, [&](QListWidgetItem*) { change(); });
    auto* buttons = new QHBoxLayout;
    auto* again = new QPushButton(QStringLiteral("Change..."), &dlg);
    connect(again, &QPushButton::clicked, &dlg, [&] { change(); });
    buttons->addWidget(again);
    buttons->addStretch(1);
    auto* ok = new QPushButton(QStringLiteral("OK"), &dlg);
    auto* cancel = new QPushButton(QStringLiteral("Cancel"), &dlg);
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    lay->addLayout(buttons);
    if (dlg.exec() == QDialog::Accepted) setWireColours(seg, false, colours);
}

int View3DPane::routeFromHarness() {
    const RouteHarness h = harness_ ? harness_() : RouteHarness();
    if (!h.ok) return -1;
    beforeRouteChange();
    // WHERE EACH PART IS DRAWN IN 3D: the middle of what the harness draws for it.
    const int laid = addHarnessToRoute(route_, h.design, [this](const QString& brick, V3& at) {
        bool any = false;
        V3 lo, hi;
        for (const Harness3DItem& it : scene_.items) {
            if (it.brick != brick) continue;
            V3 a, b;
            if (!it.mesh.bounds(a, b)) continue;
            lo = any ? V3{std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)} : a;
            hi = any ? V3{std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)} : b;
            any = true;
        }
        if (any) at = (lo + hi) * 0.5;
        return any;
    });
    route_.solidsChanged();
    saveRouting();
    refreshRoute();
    refreshParts();
    emit routeChanged();
    return laid;
}

QJsonObject View3DPane::viewState() const {
    QJsonObject v;
    const Camera3D& c = canvas_->camera();
    v[QStringLiteral("camera")] = QJsonObject{{QStringLiteral("yaw"), c.yawDeg},
                                              {QStringLiteral("pitch"), c.pitchDeg},
                                              {QStringLiteral("roll"), c.rollDeg},
                                              {QStringLiteral("distance"), c.distance},
                                              {QStringLiteral("target"), QJsonArray{c.target.x, c.target.y, c.target.z}}};
    if (gravity_ && gravity_->isChecked()) v[QStringLiteral("gravity")] = true;
    if (down_ && down_->currentIndex() > 0) v[QStringLiteral("down")] = down_->currentIndex();
    if (envOpacity_ && envOpacity_->value() < 100) v[QStringLiteral("opacity")] = envOpacity_->value();
    if (section_ && section_->currentIndex() > 0) {
        v[QStringLiteral("section")] = section_->currentIndex();
        v[QStringLiteral("sectionAt")] = sectionAt_->value();
    }
    QJsonArray hidden;
    QJsonObject seeThrough;
    for (size_t i = 0; i < env_.parts.size(); ++i) {
        const int part = static_cast<int>(i);
        if (canvas_->envPartHidden(part)) hidden.append(envPartName(part));
        if (canvas_->envPartOpacity(part) > 0.0) seeThrough[envPartName(part)] = canvas_->envPartOpacity(part);
    }
    if (!hidden.isEmpty()) v[QStringLiteral("hidden")] = hidden;
    if (!seeThrough.isEmpty()) v[QStringLiteral("partOpacity")] = seeThrough;
    QJsonArray rulers;
    for (const View3DCanvas::Measure3D& m : canvas_->measures())
        rulers.append(QJsonArray{m.a.x, m.a.y, m.a.z, m.b.x, m.b.y, m.b.z, m.faces ? 1 : 0});
    if (!rulers.isEmpty()) v[QStringLiteral("measures")] = rulers;
    return v;
}

void View3DPane::applyView(const QJsonObject& v) {
    if (v.contains(QStringLiteral("camera"))) {
        const QJsonObject c = v.value(QStringLiteral("camera")).toObject();
        Camera3D& cam = canvas_->camera();
        cam.yawDeg = c.value(QStringLiteral("yaw")).toDouble(cam.yawDeg);
        cam.pitchDeg = c.value(QStringLiteral("pitch")).toDouble(cam.pitchDeg);
        cam.rollDeg = c.value(QStringLiteral("roll")).toDouble(cam.rollDeg);
        cam.distance = c.value(QStringLiteral("distance")).toDouble(cam.distance);
        const QJsonArray t = c.value(QStringLiteral("target")).toArray();
        if (t.size() == 3) cam.target = {t.at(0).toDouble(), t.at(1).toDouble(), t.at(2).toDouble()};
        canvas_->update();
        syncCamera();
    }
    if (down_) down_->setCurrentIndex(std::clamp(v.value(QStringLiteral("down")).toInt(0), 0, down_->count() - 1));
    if (gravity_) gravity_->setChecked(v.value(QStringLiteral("gravity")).toBool(false));
    if (envOpacity_) envOpacity_->setValue(v.value(QStringLiteral("opacity")).toInt(100));
    if (section_) {
        section_->setCurrentIndex(std::clamp(v.value(QStringLiteral("section")).toInt(0), 0, section_->count() - 1));
        if (v.contains(QStringLiteral("sectionAt"))) sectionAt_->setValue(v.value(QStringLiteral("sectionAt")).toInt());
    }
    std::vector<View3DCanvas::Measure3D> rulers;
    for (const QJsonValue& m : v.value(QStringLiteral("measures")).toArray()) {
        const QJsonArray a = m.toArray();
        if (a.size() < 7) continue;
        View3DCanvas::Measure3D r;
        r.a = {a.at(0).toDouble(), a.at(1).toDouble(), a.at(2).toDouble()};
        r.b = {a.at(3).toDouble(), a.at(4).toDouble(), a.at(5).toDouble()};
        r.faces = a.at(6).toInt() != 0;
        rulers.push_back(r);
    }
    canvas_->setMeasures(rulers);
    // WHAT NEEDS THE MODEL: its solids hidden or see-through, by name.
    if (!env_.parts.empty()) {
        const QStringList hidden = v.value(QStringLiteral("hidden")).toVariant().toStringList();
        const QJsonObject seeThrough = v.value(QStringLiteral("partOpacity")).toObject();
        for (size_t i = 0; i < env_.parts.size(); ++i) {
            const int part = static_cast<int>(i);
            const QString name = envPartName(part);
            canvas_->setEnvPartHidden(part, hidden.contains(name));
            canvas_->setEnvPartOpacity(part, seeThrough.value(name).toDouble(0.0));
        }
        partsShownChanged();
    }
}

void View3DPane::syncCableTension() {
    syncCable();
    if (!cableTension_) return;
    const QSignalBlocker hold(cableTension_);
    const RouteSegment3D* s = route_.segment(canvas_->selectedSegment());
    cableTension_->setEnabled(s != nullptr);
    cableTension_->setItemText(0, QStringLiteral("As all cables (%1)").arg(cableTensionName(route_.tension)));
    int pick = 0;
    for (int i = 1; s && s->tension >= 0.0 && i < cableTension_->count(); ++i)
        if (std::abs(cableTension_->itemData(i).toDouble() - s->tension) < 1e-9) pick = i;
    cableTension_->setCurrentIndex(pick);
}

void View3DPane::syncTension() {
    if (!tension_) return;
    const QSignalBlocker hold(tension_);
    int pick = -1;
    for (int i = 0; i < tension_->count(); ++i)
        if (std::abs(tension_->itemData(i).toDouble() - route_.tension) < 1e-9) pick = i;
    if (pick < 0) {
        tension_->addItem(cableTensionName(route_.tension), route_.tension);
        pick = tension_->count() - 1;
    }
    tension_->setCurrentIndex(pick);
}

const std::vector<CableSag3D>& View3DPane::sags() const { return canvas_->sags(); }

void View3DPane::runGravity() {
    gravitySaid_.clear();
    if (!gravity_ || !gravity_->isChecked() || route_.segments.empty()) {
        canvas_->setSags({});
        return;
    }
    static const V3 downs[6] = {{0, 0, -1}, {0, -1, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, {1, 0, 0}};
    std::vector<CableSag3D> sags = hangRoute(route_, downs[std::clamp(down_->currentIndex(), 0, 5)]);
    // WHAT IT SAYS: each bundle that falls, how far at most and where from, and what holds it.
    QStringList falling;
    for (const CableSag3D& g : sags) {
        if (g.falls.empty()) continue;
        const RouteSegment3D* s = route_.segment(g.segment);
        const RouteNode3D* a = s ? route_.node(s->a) : nullptr;
        const RouteNode3D* b = s ? route_.node(s->b) : nullptr;
        const double more = g.hungLength - g.laidLength;
        falling << QStringLiteral("%1 - %2 falls %3 mm (%4 place(s), %5)%6")
                       .arg(a ? a->label : QString(), b ? b->label : QString())
                       .arg(g.maxDrop, 0, 'f', 1)
                       .arg(g.falls.size())
                       .arg(g.byLength ? QStringLiteral("hanging by its length, %1 N").arg(g.tension, 0, 'f', 2)
                                       : QStringLiteral("at %1").arg(cableTensionName(g.tension)))
                       .arg(!g.byLength && std::abs(more) >= 0.5 ? QStringLiteral(", %1%2 mm of cable to hang so").arg(more > 0 ? "+" : "").arg(more, 0, 'f', 1)
                                                                 : QString());
    }
    gravitySaid_ = falling.isEmpty() ? QStringLiteral("Gravity: every bundle held where it is laid - none falls.")
                                     : QStringLiteral("Gravity: ") + falling.join(QStringLiteral("; "));
    canvas_->setSags(std::move(sags));
}

void View3DCanvas::setSags(std::vector<CableSag3D> sags) {
    sags_ = std::move(sags);
    sagDirty_ = true;
    update();
}

// ---- The picture ----------------------------------------------------------------------------------

View3DCanvas::View3DCanvas(QWidget* parent) : QWidget(parent) {
    setMinimumSize(200, 160);
    // The pointer followed always: what it is over is said under the wheel.
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(QStringLiteral("Wheel-button drag: turn (Shift: roll) - right drag: move - wheel: zoom - double-click: fit - click on the "
                              "environment: pick a point on it, or use the route tool chosen"));
}

void View3DCanvas::setScene(const Harness3DScene* scene) { scene_ = scene; }

void View3DCanvas::setEnvironment(const Environment3D* env, const RayIndex3D* index) {
    env_ = env;
    envIndex_ = index;
    envOwnAlpha_.clear();
    if (route_) route_->solidsChanged();
    routeDirty_ = true;
    update();
}

void View3DCanvas::setEnvPartOpacity(int part, double alpha) {
    if (part < 0) return;
    if (static_cast<size_t>(part) >= envOwnAlpha_.size()) envOwnAlpha_.resize(static_cast<size_t>(part) + 1, 0.0);
    envOwnAlpha_[static_cast<size_t>(part)] = alpha <= 0.0 ? 0.0 : std::clamp(alpha, 0.05, 1.0);
    update();
}

double View3DCanvas::envPartOpacity(int part) const {
    return part >= 0 && static_cast<size_t>(part) < envOwnAlpha_.size() ? envOwnAlpha_[static_cast<size_t>(part)] : 0.0;
}

void View3DCanvas::setBrickOpacity(const QString& brick, double alpha) {
    if (alpha <= 0.0 || alpha >= 1.0) brickAlpha_.remove(brick);
    else brickAlpha_[brick] = std::max(0.05, alpha);
    update();
}

double View3DCanvas::brickOpacity(const QString& brick) const { return brickAlpha_.value(brick, 0.0); }

void View3DCanvas::beginLaying(const QString& node) {
    if (!route_ || !route_->node(node)) return;
    layFrom_ = node;
    layVia_.clear();
    layMadeStart_ = false;
    routeDirty_ = true;
    update();
}

Mesh3D View3DCanvas::faceAround(int part, const V3& point, const V3& normal) const {
    Mesh3D out;
    if (!env_ || part < 0 || static_cast<size_t>(part) >= env_->parts.size()) return out;
    const Mesh3D& m = env_->parts[static_cast<size_t>(part)].mesh;
    if (m.f.empty()) return out;
    V3 lo, hi;
    m.bounds(lo, hi);
    const double scale = std::max(1e-3, (hi - lo).length());
    std::vector<V3> tn(m.f.size());
    for (size_t i = 0; i < m.f.size(); ++i)
        tn[i] = cross3(m.v[static_cast<size_t>(m.f[i][1])] - m.v[static_cast<size_t>(m.f[i][0])],
                       m.v[static_cast<size_t>(m.f[i][2])] - m.v[static_cast<size_t>(m.f[i][0])]).normalized();
    // THE TRIANGLE THE POINT IS ON: in its plane, inside its three edges.
    int seed = -1;
    double nearest = 1e300;
    for (size_t i = 0; i < m.f.size(); ++i) {
        const V3 a = m.v[static_cast<size_t>(m.f[i][0])], b = m.v[static_cast<size_t>(m.f[i][1])], c = m.v[static_cast<size_t>(m.f[i][2])];
        const double off = std::abs(dot3(point - a, tn[i]));
        if (off > scale * 1e-3 || off >= nearest) continue;
        const double tol = -scale * 1e-6;
        if (dot3(cross3(b - a, point - a), tn[i]) < tol || dot3(cross3(c - b, point - b), tn[i]) < tol ||
            dot3(cross3(a - c, point - c), tn[i]) < tol)
            continue;
        nearest = off;
        seed = static_cast<int>(i);
    }
    if (seed < 0) return out;
    // ON FROM IT across the edges it shares, while the surface turns gently (a round face) and no further than a right
    // angle from the way the face clicked looks - a corner between two faces ends it.
    std::vector<std::vector<int>> byVertex(m.v.size());
    for (size_t i = 0; i < m.f.size(); ++i)
        for (int k : m.f[i]) byVertex[static_cast<size_t>(k)].push_back(static_cast<int>(i));
    const V3 look = dot3(tn[static_cast<size_t>(seed)], normal) < 0.0 ? tn[static_cast<size_t>(seed)] * -1.0 : tn[static_cast<size_t>(seed)];
    std::vector<char> in(m.f.size(), 0);
    std::vector<int> todo{seed};
    in[static_cast<size_t>(seed)] = 1;
    const double lift = scale * 0.0015;
    while (!todo.empty()) {
        const int t = todo.back();
        todo.pop_back();
        const auto& f = m.f[static_cast<size_t>(t)];
        const V3 side = dot3(tn[static_cast<size_t>(t)], look) < 0.0 ? tn[static_cast<size_t>(t)] * -1.0 : tn[static_cast<size_t>(t)];
        const int base = static_cast<int>(out.v.size());
        for (int k : f) out.v.push_back(m.v[static_cast<size_t>(k)] + side * lift);
        out.addTri(base, base + 1, base + 2);
        if (out.f.size() > 400000) break;
        for (int k : f)
            for (int o : byVertex[static_cast<size_t>(k)]) {
                if (in[static_cast<size_t>(o)]) continue;
                const double bend = std::abs(dot3(tn[static_cast<size_t>(o)], tn[static_cast<size_t>(t)]));
                if (bend < 0.985 || std::abs(dot3(tn[static_cast<size_t>(o)], look)) < 0.7) continue;
                in[static_cast<size_t>(o)] = 1;
                todo.push_back(o);
            }
    }
    return out;
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
    sectionDirty_ = true;
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

QString View3DCanvas::viewName() const {
    // AS THE VIEW STANDS NOW: square on from one of the six sides, the iso view - or turned to a view of its own.
    auto near = [](double a, double b) { return std::abs(wrapDegrees(a - b)) < 1.0; };
    const double yaw = cam_.yawDeg, pitch = cam_.pitchDeg;
    if (!near(cam_.rollDeg, 0.0)) return QStringLiteral("Custom");
    if (near(pitch, 90.0)) return QStringLiteral("Top");
    if (near(pitch, -90.0)) return QStringLiteral("Bottom");
    if (near(pitch, 0.0)) {
        if (near(yaw, -90.0)) return QStringLiteral("Front");
        if (near(yaw, 90.0)) return QStringLiteral("Back");
        if (near(yaw, 0.0)) return QStringLiteral("Right");
        if (near(yaw, 180.0)) return QStringLiteral("Left");
    }
    if (near(yaw, -55.0) && near(pitch, 35.0)) return QStringLiteral("Iso");
    return QStringLiteral("Custom");
}

void View3DCanvas::viewFrom(const QString& which) {
    cam_.rollDeg = 0.0;
    if (which == QLatin1String("top")) {
        cam_.yawDeg = -90.0;
        cam_.pitchDeg = 90.0;
    } else if (which == QLatin1String("front")) {
        cam_.yawDeg = -90.0;
        cam_.pitchDeg = 0.0;
    } else if (which == QLatin1String("side") || which == QLatin1String("right")) {
        cam_.yawDeg = 0.0;
        cam_.pitchDeg = 0.0;
    } else if (which == QLatin1String("back")) {
        cam_.yawDeg = 90.0;
        cam_.pitchDeg = 0.0;
    } else if (which == QLatin1String("left")) {
        cam_.yawDeg = 180.0;
        cam_.pitchDeg = 0.0;
    } else if (which == QLatin1String("bottom")) {
        cam_.yawDeg = -90.0;
        cam_.pitchDeg = -90.0;
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
        // ON PAST WHAT THE SECTION CUTS AWAY AND WHAT IS HIDDEN: the first face left standing.
        pick = envIndex_->cast(origin, dir, [this](int part, const V3& p) { return skipPart(part, p); });
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
    surfacePart_ = -1;
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
        const Pick3D p = envIndex_->cast(origin, dir, [this](int part, const V3& q) { return skipPart(part, q); });
        if (p.hit) {
            point = p.point;
            normal = p.normal;
            surfacePart_ = p.part;
            return true;
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

void View3DCanvas::setPlaceForm(const QString& form) {
    placeTemplate_.form = form;
    routeDirty_ = true;
    update();
}

void View3DCanvas::setGhostFrozen(bool on) {
    if (on && !ghostOn_) return;
    ghostFrozen_ = on;
    routeDirty_ = true;
    update();
    emit ghostFrozenChanged(ghostFrozen_);
}

void View3DCanvas::startSameAs(const QString& node) {
    sameAs_ = node;
    const RouteNode3D* n = route_ ? route_->node(node) : nullptr;
    if (n) emit message(QStringLiteral("%1 the same as: click the connector to copy from (Esc: not)").arg(n->label));
}

bool View3DCanvas::event(QEvent* e) {
    // TAB, PUTTING A CONNECTOR: it stays where it is under the pointer - its options to change on the right - until Tab
    // again, or a click (or Enter) puts it there.
    if (e->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Tab && tool_ == RouteTool::Connector && route_) {
        setGhostFrozen(!ghostFrozen_);
        return true;
    }
    return QWidget::event(e);
}

void View3DCanvas::setRouteTool(RouteTool tool) {
    if (tool != RouteTool::Route) cancelLaying();
    if (tool != RouteTool::Connector) {
        ghostOn_ = false;
        if (ghostFrozen_) setGhostFrozen(false);
    }
    if (tool != RouteTool::Measure && measureStage_ == 1) clearMeasure();
    tool_ = tool;
    // ALWAYS: what the pointer is over is said under the wheel, whatever the tool.
    setMouseTracking(true);
    setCursor(tool == RouteTool::Select                                    ? Qt::ArrowCursor
              : tool == RouteTool::Move || tool == RouteTool::Stretch ? Qt::SizeAllCursor
                                                                           : Qt::CrossCursor);
}

bool View3DCanvas::keepLength() const { return !grow_ && route_ && (dragKind_ == 1 || dragKind_ == 2 || dragKind_ == 5); }

void View3DCanvas::beginKeepLength() {
    kept_.clear();
    if (!route_) return;
    // EACH BUNDLE'S LENGTH AS IT WAS DRAWN, before it is given more points to bend at - which round its bends
    // differently, and would draw it longer or shorter by themselves.
    std::vector<std::pair<QString, double>> drawn;
    for (const RouteSegment3D& s : route_->segments) drawn.push_back({s.id, route_->length(s)});
    // A CABLE BENDS ALONG ITS LENGTH: a bundle laid through few points is given more, so it can be pulled round.
    auto bendable = [&](RouteSegment3D& s) {
        const RouteNode3D* a = route_->node(s.a);
        const RouteNode3D* b = route_->node(s.b);
        if (!a || !b || s.via.size() >= 8) return;
        std::vector<RouteVia3D> pts{{a->at, a->normal}};
        pts.insert(pts.end(), s.via.begin(), s.via.end());
        pts.push_back({b->at, b->normal});
        const int pieces = std::max(1, static_cast<int>(std::ceil(9.0 / static_cast<double>(pts.size() - 1))));
        std::vector<RouteVia3D> more;
        for (size_t i = 0; i + 1 < pts.size(); ++i)
            for (int k = i == 0 ? 1 : 0; k < pieces; ++k) {
                const double t = static_cast<double>(k) / pieces;
                more.push_back({pts[i].at + (pts[i + 1].at - pts[i].at) * t, (pts[i].normal * (1.0 - t) + pts[i + 1].normal * t).normalized()});
            }
        s.via = more;
    };
    auto chain = [&](const RouteSegment3D& s, V3 from, std::vector<int> order, const V3& anchor) {
        Kept k;
        k.segment = s.id;
        k.via = std::move(order);
        k.anchor = anchor;
        std::vector<V3> pts{from};
        for (int i : k.via) {
            const V3 p = s.via[static_cast<size_t>(i)].at;
            k.step.push_back((p - from).length());
            from = p;
            pts.push_back(p);
        }
        k.step.push_back((anchor - from).length());
        pts.push_back(anchor);
        k.bend.assign(pts.size(), 0.0);
        for (size_t i = 1; i + 1 < pts.size(); ++i) {
            const V3 u = pts[i] - pts[i - 1], v = pts[i + 1] - pts[i];
            if (u.length() > 1e-9 && v.length() > 1e-9)
                k.bend[i] = std::acos(std::clamp(dot3(u.normalized(), v.normalized()), -1.0, 1.0));
        }
        kept_.push_back(std::move(k));
    };
    // EACH BUNDLE PULLED, the length it is to keep: as it was drawn when taken hold of - a corner's bundle as well as a
    // node's bundles.
    auto account = [&] {
        for (const Kept& k : kept_) {
            const RouteSegment3D* s = route_->segment(k.segment);
            if (!s) continue;
            double steps = 0.0;
            for (double d : k.step) steps += d;
            auto had = std::find_if(keptLen_.begin(), keptLen_.end(), [&](const KeptLength& l) { return l.segment == k.segment; });
            if (had != keptLen_.end()) had->steps += steps;
            else {
                double was = route_->length(*s);
                for (const auto& [id, length] : drawn)
                    if (id == k.segment) was = length;
                keptLen_.push_back({k.segment, was, steps, 1.0});
                keptBase_[k.segment] = was;
            }
        }
    };
    if (dragKind_ == 2) {
        // A CORNER OF A BUNDLE: pulled, the bundle both ways from it to its two ends.
        RouteSegment3D* s = route_->segment(dragId_);
        if (!s || dragVia_ < 0 || dragVia_ >= static_cast<int>(s->via.size())) return;
        const RouteNode3D* a = route_->node(s->a);
        const RouteNode3D* b = route_->node(s->b);
        if (!a || !b) return;
        const V3 held = s->via[static_cast<size_t>(dragVia_)].at;
        std::vector<int> back, ahead;
        for (int i = dragVia_ - 1; i >= 0; --i) back.push_back(i);
        for (int i = dragVia_ + 1; i < static_cast<int>(s->via.size()); ++i) ahead.push_back(i);
        chain(*s, held, back, a->at);
        chain(*s, held, ahead, b->at);
        account();
        return;
    }
    const RouteNode3D* node = route_->node(dragId_);
    if (!node) return;
    for (RouteSegment3D& s : route_->segments) {
        if (s.a != dragId_ && s.b != dragId_) continue;
        if (dragKind_ == 4 && !stretchSeg_.isEmpty() && s.id != stretchSeg_) continue;   // only the cable stretched
        bendable(s);
        const RouteNode3D* far = route_->node(s.a == dragId_ ? s.b : s.a);
        if (!far) continue;
        std::vector<int> order;
        if (s.a == dragId_)
            for (int i = 0; i < static_cast<int>(s.via.size()); ++i) order.push_back(i);
        else
            for (int i = static_cast<int>(s.via.size()) - 1; i >= 0; --i) order.push_back(i);
        chain(s, node->at, order, far->at);
        // THROUGH ITS CLIPS: the stretches of the same cable beyond each clip it runs through (a clip with two bundles),
        // as long as they are, and how short each could be pulled (straight) - the cable slides through a clip.
        std::vector<Through> list;
        QString at = far->id, from = s.id;
        for (int guard = 0; guard < 64; ++guard) {
            const RouteNode3D* clip = route_->node(at);
            if (!clip || clip->kind != RouteNodeKind::Clip || route_->degree(at) != 2) break;
            const RouteSegment3D* next = nullptr;
            for (const RouteSegment3D& o : route_->segments)
                if (o.id != from && (o.a == at || o.b == at)) next = &o;
            if (!next) break;
            const std::vector<V3> k = route_->corners(*next);
            list.push_back({next->id, route_->length(*next), k.size() >= 2 ? (k.back() - k.front()).length() : 0.0, next->via});
            from = next->id;
            at = next->a == at ? next->b : next->a;
        }
        through_[s.id] = list;
    }
    account();
}

double View3DCanvas::slideThroughClips(const QString& segment, double take) {
    // TAKEN FROM BEYOND THE CLIPS, the nearest stretch first, each down to straight - from each as it was taken hold of.
    auto t = through_.find(segment);
    if (t == through_.end()) return 0.0;
    double left = std::max(0.0, take);
    for (const Through& th : t->second) {
        RouteSegment3D* s = route_->segment(th.segment);
        if (!s) continue;
        s->via = th.was;
        const double cut = std::min(left, std::max(0.0, th.length - th.shortest - 0.05));
        if (cut > 0.01) route_->lengthen(th.segment, th.length - cut);
        // What it really gave - its bends may keep it from going as short as straight.
        left -= std::max(0.0, th.length - route_->length(*route_->segment(th.segment)));
    }
    return std::max(0.0, take) - left;
}

V3 View3DCanvas::layKept(const Kept& k, const V3& start, double scale) {
    // EACH BUNDLE PULLED ALONG BEHIND IT, every step as long as it was (times `scale`), its far end where it holds (FABRIK).
    RouteSegment3D* s = route_->segment(k.segment);
    if (!s) return start;
    std::vector<V3> p{start};
    for (int i : k.via) p.push_back(s->via[static_cast<size_t>(i)].at);
    p.push_back(k.anchor);
    const size_t n = p.size() - 1;
    // STIFFER THE TAUTER IT IS PUT IN: a cable under tension does not kink or loop as it is pulled - each step turns
    // from the one before by no more than it bends (Loose 60 degrees, Normal 30, Taut 10; slack, as far as it likes) or
    // than it was bent there already, every step as long as it was.
    const int stiff = stiffnessOf(tensionOf(*route_, *s));
    double limit = stiff > 0 ? std::max(10.0, 60.0 / stiff) * M_PI / 180.0 : M_PI;
    auto toward = [&](const V3& from, const V3& to, double d, const V3* before, size_t joint) {
        V3 dir = to - from;
        if (dir.length() < 1e-12) return from;
        dir = dir.normalized();
        const double most = joint < k.bend.size() ? std::max(limit, k.bend[joint] + 1e-3) : limit;
        if (before && most < M_PI) {
            const double c = std::clamp(dot3(*before, dir), -1.0, 1.0);
            if (std::acos(c) > most) {
                V3 side = dir - *before * c;
                if (side.length() < 1e-9) {
                    side = cross3(*before, V3{0, 0, 1});
                    if (side.length() < 1e-9) side = cross3(*before, V3{1, 0, 0});
                }
                dir = *before * std::cos(most) + side.normalized() * std::sin(most);
            }
        }
        return from + dir * d;
    };
    auto heading = [](const V3& a, const V3& b, V3& out) {
        const V3 d = b - a;
        if (d.length() < 1e-12) return false;
        out = d.normalized();
        return true;
    };
    // ITS SHAPE CARRIED ALONG FIRST: each point moved with the end pulled by as much as it lies near it (all of it at the
    // end, none at the far end) - a taut cable keeps its form as it is pulled, and is laid from there.
    if (stiff > 0 && n >= 2) {
        const V3 moved = start - p[0];
        double whole = 0.0;
        for (size_t i = 0; i < n; ++i) whole += k.step[i];
        double far = whole;
        for (size_t i = 1; i < n; ++i) {
            far -= k.step[i - 1];
            p[i] = p[i] + moved * (whole > 1e-9 ? far / whole : 0.0);
        }
    }
    const std::vector<V3> from = p;
    auto solve = [&] {
        for (int iter = 0; iter < (stiff > 0 ? 24 : 12); ++iter) {
            p[0] = start;
            for (size_t i = 1; i <= n; ++i) {
                V3 h;
                const bool has = i >= 2 && heading(p[i - 2], p[i - 1], h);
                p[i] = toward(p[i - 1], p[i], k.step[i - 1] * scale, has ? &h : nullptr, i - 1);
            }
            p[n] = k.anchor;
            for (size_t i = n; i-- > 0;) {
                V3 h;
                const bool has = i + 2 <= n && heading(p[i + 2], p[i + 1], h);
                p[i] = toward(p[i + 1], p[i], k.step[i] * scale, has ? &h : nullptr, i + 1);
            }
            if ((p[0] - start).length() < 1e-4) break;
        }
        return (p[0] - start).length();
    };
    // AS STIFF AS IT IS, AS FAR AS THAT LETS IT FOLLOW: where it cannot reach the pointer bending no more than its tension
    // allows, it is let bend a little more, and again - so a taut cable is pulled along, stiffly, rather than not at all.
    for (int loosen = 0; solve() > 0.05 && limit < M_PI && loosen < 6; ++loosen) {
        limit = std::min(M_PI, limit * 1.6);
        p = from;
    }
    for (size_t j = 0; j < k.via.size(); ++j) s->via[static_cast<size_t>(k.via[j])].at = p[j + 1];
    return p[0];
}

void View3DCanvas::tightenKept(double by) {
    stretchCut_ = 0.0;
    const RouteNode3D* node = route_ ? route_->node(dragId_) : nullptr;
    if (!node) return;
    const V3 start = node->at;
    for (KeptLength& l : keptLen_) {
        const RouteSegment3D* s = route_->segment(l.segment);
        if (!s || l.steps < 1e-9) continue;
        // NO SHORTER THAN STRAIGHT ACROSS to where its other end holds.
        double tight = 0.0;
        for (const Kept& k : kept_)
            if (k.segment == l.segment) tight = std::max(tight, (k.anchor - start).length() / l.steps);
        tight = std::min(1.0, tight * 1.0001);
        const double goal = l.length - by;
        // HOW MUCH SHORTER IT IS DRAWN for its steps laid shorter - its bends round them make it less than one for one -
        // taken from the last two tries (secant).
        double slope = l.steps, lastScale = -1.0, lastNow = 0.0;
        for (int fit = 0; fit < 10; ++fit) {
            // Up to twice as long a step: a stiff cable smoothed round as it is laid is shorter for it.
            l.scale = std::clamp(l.scale, tight, 2.0);
            for (const Kept& k : kept_)
                if (k.segment == l.segment) layKept(k, start, l.scale);
            const double now = route_->length(*s);
            if (std::abs(now - goal) < 0.05) break;
            if (lastScale >= 0.0 && std::abs(l.scale - lastScale) > 1e-9) {
                const double measured = (now - lastNow) / (l.scale - lastScale);
                if (measured > l.steps * 0.02) slope = measured;
            }
            if ((l.scale <= tight && now > goal) || (l.scale >= 2.0 && now < goal)) break;   // as far as it goes
            lastScale = l.scale;
            lastNow = now;
            l.scale += (goal - now) / slope;
        }
        stretchCut_ += l.length - route_->length(*s);
        // TIGHT, AND MORE PULLED: the rest through its clips - the stretches beyond them shortened in turn.
        const double short_ = by - (l.length - route_->length(*s));
        if (short_ > 0.05) stretchCut_ += slideThroughClips(l.segment, short_);
        else slideThroughClips(l.segment, 0.0);
    }
}

void View3DCanvas::pullKept(V3 target) {
    if (!route_ || kept_.empty()) return;
    auto scaleOf = [&](const QString& id) {
        for (const KeptLength& l : keptLen_)
            if (l.segment == id) return l.scale;
        return 1.0;
    };
    const bool slides = std::any_of(through_.begin(), through_.end(), [](const auto& t) { return !t.second.empty(); });
    // PULLED FURTHER THAN IT REACHES, THROUGH ITS CLIPS: the cable slides through each clip it runs through, the stretch
    // beyond giving it length - down to straight - the cable as a whole as long as it was.
    for (KeptLength& l : keptLen_) {
        const auto base = keptBase_.find(l.segment);
        if (base == keptBase_.end() || through_.find(l.segment) == through_.end()) continue;
        const Kept* k = nullptr;
        for (const Kept& c : kept_)
            if (c.segment == l.segment) k = &c;
        if (!k) continue;
        const RouteNode3D* held = route_->node(dragId_);
        const double lead = held && held->kind == RouteNodeKind::Connector ? route_->lead : 0.0;
        const double need = (target - k->anchor).length() + lead;
        l.length = base->second + slideThroughClips(l.segment, need - base->second);
    }
    // ITS LENGTH AS IT IS DRAWN KEPT: bends and what it is laid over draw a cable longer or shorter than the points it is
    // laid through, so its steps are laid longer or shorter until it is drawn as long as it was.
    for (int fit = 0; fit < 14; ++fit) {
        V3 t = target;
        // NO FURTHER THAN THE CABLE REACHES from where each bundle holds.
        for (int pass = 0; pass < 4; ++pass)
            for (const Kept& k : kept_) {
                double reach = 0.0;
                for (double d : k.step) reach += d;
                reach *= scaleOf(k.segment);
                const V3 off = t - k.anchor;
                if (off.length() > reach && off.length() > 1e-9) t = k.anchor + off.normalized() * reach;
            }
        // A STIFF CABLE THAT DOES NOT REACH at these steps: the last that did is kept, its steps as they were - a gap
        // between the point and the cable is not length to be taken out of it.
        const std::vector<RouteSegment3D> lastFit = route_->segments;
        const std::vector<KeptLength> lastLen = keptLen_;
        bool reachedAll = true;
        for (const Kept& k : kept_)
            if ((layKept(k, t, scaleOf(k.segment)) - t).length() > 0.5) reachedAll = false;
        // A STIFF CABLE STOPS IT: pushed where it will not bend to, the point stays where it last could go - the
        // cable as it was, no shorter and no kink - unless it slides through its clips.
        if (!reachedAll && (fit > 0 || !slides)) {
            route_->segments = lastFit;
            keptLen_ = lastLen;
            break;
        }
        if (dragKind_ == 2) {
            if (RouteSegment3D* s = route_->segment(dragId_))
                if (dragVia_ >= 0 && dragVia_ < static_cast<int>(s->via.size())) s->via[static_cast<size_t>(dragVia_)].at = t;
        } else if (RouteNode3D* n = route_->node(dragId_)) {
            n->at = t;
        }
        bool close = true;
        for (KeptLength& l : keptLen_) {
            const RouteSegment3D* s = route_->segment(l.segment);
            if (!s || l.steps < 1e-9) continue;
            const double now = route_->length(*s);
            if (std::abs(now - l.length) > 0.05) close = false;
            // Bends and drapes make up only so much; a cable sliding through its clips, more.
            l.scale = std::clamp(l.scale + (l.length - now) / l.steps, 0.3, 3.0);
        }
        if (close) break;
    }}

bool View3DCanvas::beginNodeDrag(const QPointF& at, bool stretch, const QString& forceNode) {
    dragKind_ = 0;
    dragStarted_ = false;
    kept_.clear();
    keptLen_.clear();
    through_.clear();
    keptBase_.clear();
    stretchSeg_.clear();
    if (!route_) return false;
    QString id = forceNode.isEmpty() ? nodeAt(at) : forceNode;
    if (stretch) {
        // STRETCH TAKES A CABLE: the one clicked, pulled through its connector (the nearer, where it has two); or, a
        // connector clicked, its one cable - or of its cables the one selected.
        const RouteNode3D* on = route_->node(id);
        if (on && on->kind == RouteNodeKind::Clip) {
            emit message(QStringLiteral("%1 is a clip - it holds the harness only at its point. Move it to move it.").arg(on->label));
            return false;
        }
        if (on) {
            QStringList cables;
            for (const RouteSegment3D& s : route_->segments)
                if (s.a == id || s.b == id) cables << s.id;
            if (cables.size() == 1) stretchSeg_ = cables.front();
            else if (cables.contains(selSegment_)) stretchSeg_ = selSegment_;
            else {
                emit message(QStringLiteral("%1 has %2 cables - click the cable to stretch").arg(on->label).arg(cables.size()));
                return false;
            }
        } else {
            V3 p, n;
            int after = 0;
            stretchSeg_ = segmentAt(at, &p, &n, &after);
            const RouteSegment3D* s = route_->segment(stretchSeg_);
            if (!s) return false;
            const RouteNode3D* a = route_->node(s->a);
            const RouteNode3D* b = route_->node(s->b);
            const bool ca = a && a->kind == RouteNodeKind::Connector, cb = b && b->kind == RouteNodeKind::Connector;
            if (!ca && !cb) {
                selSegment_ = stretchSeg_;
                selNode_.clear();
                routeRedraw();
                emit selectionChanged(selSegment_);
                emit message(QStringLiteral("%1 - %2 has no connector to be pulled through - a cable is stretched through its "
                                            "connector").arg(a ? a->label : QString(), b ? b->label : QString()));
                stretchSeg_.clear();
                return false;
            }
            id = ca && cb ? ((p - a->at).length() <= (p - b->at).length() ? s->a : s->b) : ca ? s->a : s->b;
        }
    }
    const RouteNode3D* node = route_->node(id);
    if (!node) return false;
    dragKind_ = stretch ? 4 : 5;
    dragId_ = id;
    dragFrom_ = node->at;
    selNode_ = stretch ? QString() : id;
    selSegment_ = stretch ? stretchSeg_ : QString();
    // Off the model, it goes across the picture at its own depth.
    V3 right, up, forward;
    cam_.axes(right, up, forward);
    const double z = std::max(cam_.nearPlane(), dot3(node->at - cam_.eye(), forward));
    dragPerPixel_ = 2.0 * z * std::tan(cam_.fovDeg * M_PI / 360.0) / std::max(1, height());
    routeRedraw();
    emit selectionChanged(selSegment_);
    return true;
}

void View3DCanvas::routeRedraw() {
    keepDirty_ = true;
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
    const bool changed = id != selSegment_ || !selNode_.isEmpty();
    selSegment_ = id;
    selNode_.clear();
    routeRedraw();
    if (changed) emit selectionChanged(selSegment_);
}

V3 View3DCanvas::nodeShown(const RouteNode3D& n) const {
    if (!route_) return n.at;
    return n.kind == RouteNodeKind::Connector ? n.at + n.normal * (route_->lead * 0.5) : n.at + n.normal * (route_->diameter * 0.5);
}

namespace {

// A CONNECTOR'S HOUSING as its form has it: standing out of its face the way it points, turned round that by its spin -
// a box (with a key on its top edge, so how it lies is seen), a cylinder, a flange and a narrower body, a right angle,
// or a D-shaped shell - as wide, as tall and as long as it is said to be, or as the bundle and the lead make it.
Mesh3D connectorHousing(const RouteNode3D& n, double d, const Route3D& route) {
    const V3 ax = n.normal;
    V3 u = cross3(ax, V3{0, 0, 1});
    if (u.length() < 1e-6) u = cross3(ax, V3{1, 0, 0});
    u = u.normalized();
    const V3 v = cross3(ax, u).normalized();
    const double s = n.spinDeg * M_PI / 180.0;
    const V3 su = u * std::cos(s) + v * std::sin(s), sv = v * std::cos(s) - u * std::sin(s);
    const double len = route.housingLength(n);
    const double w = n.formW > 0.0 ? n.formW : d * 2.2, h = n.formH > 0.0 ? n.formH : d * 1.3;
    auto box = [&](double x0, double x1, double y0, double y1, double z0, double z1) {
        Mesh3D b = boxMesh({0, 0, 0}, {1, 1, 1});
        for (V3& q : b.v) q = n.at + ax * (x0 + (x1 - x0) * q.x) + su * (y0 + (y1 - y0) * q.y) + sv * (z0 + (z1 - z0) * q.z);
        return b;
    };
    Mesh3D m;
    if (n.form == QLatin1String("round")) {
        m = tubeMesh({n.at, n.at + ax * len}, w / 2.0, 28, true);
        m.append(box(0.0, len, -w * 0.08, w * 0.08, w * 0.5 - w * 0.06, w * 0.5 + w * 0.06));
    } else if (n.form == QLatin1String("tee")) {
        // A flange on the face, a narrower body out of it.
        m = box(0.0, len * 0.22, -w / 2, w / 2, -h / 2, h / 2);
        m.append(box(0.0, len, -w * 0.3, w * 0.3, -h * 0.35, h * 0.35));
    } else if (n.form == QLatin1String("angle")) {
        // Out of the face, then a right angle round to the side.
        m = box(0.0, len * 0.6, -w / 2, w / 2, -h / 2, h / 2);
        m.append(box(len * 0.6 - h * 0.9, len * 0.6, w / 2 - h * 0.2, w / 2 + len * 0.6, -h * 0.4, h * 0.4));
    } else if (n.form == QLatin1String("dsub")) {
        // A D-shaped shell: wider at its top than its bottom.
        Mesh3D shell = prismMesh({{-w / 2, h / 2}, {w / 2, h / 2}, {w * 0.38, -h / 2}, {-w * 0.38, -h / 2}}, len);
        for (V3& q : shell.v) q = n.at + su * q.x + sv * q.y + ax * q.z;
        m = shell;
    } else {
        m = box(0.0, len, -w / 2, w / 2, -h / 2, h / 2);
        m.append(box(0.0, len, -w * 0.15, w * 0.15, h * 0.5, h * 0.75));
    }
    // ITS CABLE OUT OF ITS BACK: a narrower body behind its face, the cable out of the end of that.
    if (n.back) {
        const double body = route.bodyLength(n);
        if (n.form == QLatin1String("round")) m.append(tubeMesh({n.at, n.at - ax * body}, w * 0.34, 20, true));
        else m.append(box(-body, 0.0, -w * 0.34, w * 0.34, -h * 0.34, h * 0.34));
    }
    return m;
}

} // namespace

void View3DCanvas::buildRouteMeshes() const {
    routeMeshes_.clear();
    routeDirty_ = false;
    if (!route_) return;
    const QColor lit(80, 220, 255);
    for (const RouteSegment3D& s : route_->segments) {
        const std::vector<V3> c = route_->centreline(s);
        if (c.size() < 2) continue;
        const QColor own = s.id == selSegment_ ? lit : s.color.isValid() ? s.color : QColor(235, 140, 40);
        const int nw = route_->wiresOf(s);
        if (nw <= 1) {
            routeMeshes_.push_back({tubeMesh(c, route_->diameterOf(s) / 2.0, 14, true), own, Texture3D::Plastic, QString(), s.id});
            continue;
        }
        // A BUNDLE OF WIRES, WIRE BY WIRE: each where it lies across the bundle, carried along its middle without twisting
        // (each step's frame turned from the one before only as far as the bundle turns).
        const double wd = route_->wireDOf(s);
        const std::vector<std::pair<double, double>> lay = Route3D::wireLayout(nw, wd);
        std::vector<V3> us, vs;
        for (size_t i = 0; i < c.size(); ++i) {
            const V3 t = (c[std::min(i + 1, c.size() - 1)] - c[i > 0 ? i - 1 : 0]).normalized();
            V3 u;
            if (us.empty()) {
                u = cross3(t, V3{0, 0, 1});
                if (u.length() < 1e-6) u = cross3(t, V3{1, 0, 0});
            } else {
                u = us.back() - t * dot3(us.back(), t);
                if (u.length() < 1e-9) u = cross3(t, vs.back());
            }
            u = u.normalized();
            us.push_back(u);
            vs.push_back(cross3(t, u).normalized());
        }
        for (size_t w = 0; w < lay.size(); ++w) {
            std::vector<V3> path;
            for (size_t i = 0; i < c.size(); ++i) path.push_back(c[i] + us[i] * lay[w].first + vs[i] * lay[w].second);
            // ALL ONE COLOUR (the cable's), or each its own - as chosen, the rest a colour each in turn.
            const QColor wc = s.id == selSegment_ ? lit
                              : s.wireSame        ? (s.color.isValid() ? s.color : QColor(235, 140, 40))
                              : w < s.wireColors.size() && s.wireColors[w].isValid() ? s.wireColors[w]
                                                                                    : wireColourOf(static_cast<int>(w));
            routeMeshes_.push_back({tubeMesh(path, wd * 0.48, 8, true), wc, Texture3D::Plastic, QString(), s.id});
        }
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
            (void)ax;
            (void)u;
            (void)v;
            routeMeshes_.push_back({connectorHousing(n, d, *route_), on ? lit : n.color.isValid() ? n.color : QColor(205, 208, 214),
                                    Texture3D::Metal, n.id});
        } else if (n.kind == RouteNodeKind::Clip) {
            // A CLIP AS A CLIP IS: a band round the bundle where it is held, the way the bundle runs there, and its foot
            // down to the face it is fixed to.
            const V3 c = nodeShown(n);
            V3 along;
            for (const RouteSegment3D& s : route_->segments) {
                if (s.a != n.id && s.b != n.id) continue;
                const std::vector<V3> k = route_->corners(s);
                if (k.size() < 2) continue;
                along = s.a == n.id ? k[1] - k[0] : k[k.size() - 2] - k.back();
                if (along.length() > 1e-9) break;
            }
            if (along.length() < 1e-9) along = cross3(n.normal, V3{0, 0, 1}).length() > 1e-6 ? cross3(n.normal, V3{0, 0, 1}) : V3{1, 0, 0};
            along = along.normalized();
            const double r = route_->diameter / 2.0, band = std::max(1.5, d * 0.8);
            Mesh3D clip = tubeMesh({c - along * (band / 2.0), c + along * (band / 2.0)}, r * 1.35, 24, true);
            const V3 across = cross3(n.normal, along).normalized();
            Mesh3D foot = boxMesh({0, 0, 0}, {1, 1, 1});
            for (V3& q : foot.v)
                q = n.at + along * ((q.x - 0.5) * band) + across * ((q.y - 0.5) * r * 1.6) + n.normal * (q.z * r * 0.7);
            clip.append(foot);
            routeMeshes_.push_back({clip, on ? lit : n.color.isValid() ? n.color : QColor(70, 190, 110), Texture3D::Plastic, n.id});
        } else {
            const double h = d * 0.6;
            const V3 c = nodeShown(n);
            routeMeshes_.push_back({boxMesh(c - V3{h, h, h}, c + V3{h, h, h}), on ? lit : n.color.isValid() ? n.color : QColor(250, 210, 60),
                                    Texture3D::None, n.id});
        }
    }
    // HEATSHRINK AT A BRANCH: a sleeve over the routed cable where it divides, and over each leg out of it - each as long
    // as three times the bundle's diameter from the branch, a little wider than the bundle.
    branchSleeves_ = 0;
    for (const RouteNode3D& n : route_->nodes) {
        if (n.kind != RouteNodeKind::Branch || route_->degree(n.id) < 2) continue;
        for (const RouteSegment3D& s : route_->segments) {
            if (s.a != n.id && s.b != n.id) continue;
            std::vector<V3> c = route_->centreline(s);
            if (c.size() < 2) continue;
            if (s.b == n.id) std::reverse(c.begin(), c.end());
            const double dia = route_->diameterOf(s), reach = std::max(6.0, dia * 3.0);
            std::vector<V3> sleeve{c.front()};
            double gone = 0.0;
            for (size_t i = 1; i < c.size() && gone < reach; ++i) {
                const double l = (c[i] - c[i - 1]).length();
                if (gone + l >= reach && l > 1e-9) {
                    sleeve.push_back(c[i - 1] + (c[i] - c[i - 1]) * ((reach - gone) / l));
                    gone = reach;
                    break;
                }
                sleeve.push_back(c[i]);
                gone += l;
            }
            if (sleeve.size() < 2) continue;
            routeMeshes_.push_back({tubeMesh(sleeve, dia / 2.0 * 1.3, 18, true), QColor(28, 28, 32), Texture3D::Matte, QString()});
            ++branchSleeves_;
        }
    }
    // A CONNECTOR BEING PUT: where it would go under the pointer - frozen there with Tab - in its form, see-through.
    if (ghostOn_ && tool_ == RouteTool::Connector) {
        RouteNode3D g = placeTemplate_;
        g.at = ghost_.at;
        g.normal = ghost_.normal;
        routeMeshes_.push_back({connectorHousing(g, d, *route_), ghostFrozen_ ? QColor(80, 220, 255) : QColor(235, 240, 248),
                                Texture3D::None, QString()});
    }
}

std::vector<DrawItem3D> View3DCanvas::drawItems() const {
    std::vector<DrawItem3D> items;
    if (scene_ && harnessShown_)
        for (const Harness3DItem& it : scene_->items) {
            if (!it.brick.isEmpty() && hiddenBricks_.contains(it.brick)) continue;
            // PICKED: lit in the pane's highlight colour, the part still read through it.
            QColor c = it.color;
            if (!it.brick.isEmpty() && lit_.contains(it.brick))
                c = QColor((c.red() + 2 * 43) / 3, (c.green() + 2 * 208) / 3, (c.blue() + 2 * 255) / 3);
            items.push_back({&it.mesh, c, it.brick.isEmpty() ? 1.0 : brickAlpha_.value(it.brick, 1.0), false, it.texture});
        }
    if (env_ && envShown_)
        for (size_t i = 0; i < env_->parts.size(); ++i) {
            if (envPartHidden(static_cast<int>(i))) continue;
            const EnvPart3D& part = env_->parts[i];
            QColor c = part.color.isValid() ? part.color : QColor(168, 174, 184);
            // The solid last picked, lit.
            if (pick_.hit && pick_.part == static_cast<int>(i))
                c = QColor((c.red() + 43) / 2, (c.green() + 208) / 2, (c.blue() + 255) / 2);
            const double own = envPartOpacity(static_cast<int>(i));
            items.push_back({&part.mesh, c, own > 0.0 ? own : envAlpha_, true});
        }
    if (routeDirty_) buildRouteMeshes();
    for (const Drawn& d : routeMeshes_) items.push_back({&d.mesh, d.color, 1.0, false, d.texture});
    // GRAVITY: each bundle as it would hang, faint, and a red line down from where it is laid to where it falls.
    // THE KEEP-OUTS: each level a sheet across the model, each zone a box - faint, red where it blocks, orange where it
    // alerts - and each cable through one marked where it goes in.
    if (route_ && keepShown_ && !route_->keepOuts.empty()) {
        if (keepDirty_) {
            keepBlockMesh_ = Mesh3D();
            keepAlertMesh_ = Mesh3D();
            keepHitMesh_ = Mesh3D();
            V3 lo{-100, -100, -100}, hi{100, 100, 100};
            bool any = env_ && env_->bounds(lo, hi);
            for (const RouteNode3D& n : route_->nodes) {
                lo = any ? V3{std::min(lo.x, n.at.x), std::min(lo.y, n.at.y), std::min(lo.z, n.at.z)} : n.at;
                hi = any ? V3{std::max(hi.x, n.at.x), std::max(hi.y, n.at.y), std::max(hi.z, n.at.z)} : n.at;
                any = true;
            }
            const V3 pad = (hi - lo) * 0.1 + V3{10, 10, 10};
            lo = lo - pad;
            hi = hi + pad;
            for (const KeepOut3D& k : route_->keepOuts) {
                Mesh3D& into = k.block ? keepBlockMesh_ : keepAlertMesh_;
                if (k.zone) {
                    into.append(boxMesh({std::min(k.lo.x, k.hi.x), std::min(k.lo.y, k.hi.y), std::min(k.lo.z, k.hi.z)},
                                        {std::max(k.lo.x, k.hi.x), std::max(k.lo.y, k.hi.y), std::max(k.lo.z, k.hi.z)}));
                } else {
                    // A sheet a hair thick across the rest of the scene.
                    V3 a = lo, b = hi;
                    (k.axis == 0 ? a.x : k.axis == 1 ? a.y : a.z) = k.at - 0.25;
                    (k.axis == 0 ? b.x : k.axis == 1 ? b.y : b.z) = k.at + 0.25;
                    into.append(boxMesh(a, b));
                }
            }
            for (const KeepOutHit& h : keepHits_) {
                const double r = 0.8 * std::max(2.0, route_->diameter);
                keepHitMesh_.append(boxMesh(h.at - V3{r, r, r}, h.at + V3{r, r, r}));
            }
            keepDirty_ = false;
        }
        if (!keepBlockMesh_.empty()) items.push_back({&keepBlockMesh_, QColor(235, 60, 60), 0.16});
        if (!keepAlertMesh_.empty()) items.push_back({&keepAlertMesh_, QColor(255, 170, 40), 0.16});
        if (!keepHitMesh_.empty()) items.push_back({&keepHitMesh_, QColor(255, 40, 40)});
    }
    if (!sags_.empty()) {
        if (sagDirty_) {
            sagMesh_ = Mesh3D();
            fallMesh_ = Mesh3D();
            for (const CableSag3D& g : sags_) {
                const RouteSegment3D* s = route_ ? route_->segment(g.segment) : nullptr;
                const double r = s ? route_->diameterOf(*s) / 2.0 : 2.0;
                // Only the stretches in the air that fall - the rest of it lies as it is laid.
                for (const auto& [a, b] : g.spans) {
                    double most = 0.0;
                    for (int i = a; i <= b; ++i) most = std::max(most, g.drop[static_cast<size_t>(i)]);
                    if (most < 0.1) continue;
                    sagMesh_.append(tubeMesh(std::vector<V3>(g.hung.begin() + a, g.hung.begin() + b + 1), r, 10, true));
                }
                // HOW FAR IT FALLS: a line straight down, from where it is laid to where it hangs, and a mark there.
                for (int i : g.falls) {
                    const V3 to = g.hung[static_cast<size_t>(i)];
                    const V3 from = to - g.down * g.drop[static_cast<size_t>(i)];
                    fallMesh_.append(tubeMesh({from, to}, std::max(0.3, r * 0.25), 8, true));
                    fallMesh_.append(boxMesh(to - V3{r, r, r} * 0.9, to + V3{r, r, r} * 0.9));
                }
            }
            sagDirty_ = false;
        }
        if (!sagMesh_.empty()) items.push_back({&sagMesh_, QColor(255, 80, 70), 0.45});
        if (!fallMesh_.empty()) items.push_back({&fallMesh_, QColor(255, 60, 50)});
    }
    if (!layMesh_.empty()) items.push_back({&layMesh_, QColor(255, 255, 255), 0.75});
    if (!marker_.empty()) items.push_back({&marker_, QColor(255, 196, 40)});
    // THE RULERS: each a thin line between its two ends and a mark at each - the one selected lit; the one being made
    // from its first end to the pointer.
    if (measureStage_ != 0 || !measures_.empty()) {
        if (measureDirty_) {
            measureDirty_ = false;
            const double size = std::max(0.15, cam_.distance * 0.002);
            measureMesh_ = Mesh3D();
            measureLitMesh_ = Mesh3D();
            auto add = [&](Mesh3D& into, const Measure3D& m) {
                if ((m.b - m.a).length() > 1e-6) into.append(tubeMesh({m.a, m.b}, size * 0.6, 8, true));
                for (const V3& end : {m.a, m.b}) into.append(boxMesh(end - V3{size, size, size} * 2.0, end + V3{size, size, size} * 2.0));
            };
            for (size_t i = 0; i < measures_.size(); ++i) add(static_cast<int>(i) == selMeasure_ ? measureLitMesh_ : measureMesh_, measures_[i]);
            if (measureStage_ == 1) add(measureMesh_, measureMaking(measureB_));
        }
        if (!measureMesh_.empty()) items.push_back({&measureMesh_, QColor(255, 220, 60)});
        if (!measureLitMesh_.empty()) items.push_back({&measureLitMesh_, QColor(80, 220, 255)});
        // SURFACE TO SURFACE: the two faces measured between, lit - the one selected in the highlight colour.
        for (size_t i = 0; i < measures_.size(); ++i) {
            const QColor c = static_cast<int>(i) == selMeasure_ ? QColor(80, 220, 255) : QColor(255, 170, 40);
            for (const Mesh3D* f : {&measures_[i].faceA, &measures_[i].faceB})
                if (!f->empty()) items.push_back({f, c, 0.6});
        }
        if (measureStage_ == 1 && !measureFaceA_.empty()) items.push_back({&measureFaceA_, QColor(255, 170, 40), 0.6});
    }
    // THE SECTION'S PLANE, nearly see-through, over only the solids it cuts - so it is seen where the cut is.
    if (section_.on && env_ && envShown_) {
        if (sectionDirty_) {
            sectionDirty_ = false;
            sectionMesh_ = Mesh3D();
            V3 lo, hi;
            bool any = false;
            for (size_t i = 0; i < env_->parts.size(); ++i) {
                if (envPartHidden(static_cast<int>(i))) continue;
                V3 a, b;
                if (!env_->parts[i].mesh.bounds(a, b)) continue;
                const double d0 = dot3(a, section_.normal), d1 = dot3(b, section_.normal);
                if (std::min(d0, d1) > section_.offset || std::max(d0, d1) < section_.offset) continue;
                lo = any ? V3{std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)} : a;
                hi = any ? V3{std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)} : b;
                any = true;
            }
            if (any) {
                const V3 m = (hi - lo) * 0.03;
                lo = lo - m;
                hi = hi + m;
                // Flat on the plane: its own axis to the cut, the other two as wide as what it cuts.
                const double at = section_.offset;
                if (std::abs(section_.normal.x) > 0.5) lo.x = hi.x = at;
                else if (std::abs(section_.normal.y) > 0.5) lo.y = hi.y = at;
                else lo.z = hi.z = at;
                const V3 t{0.02, 0.02, 0.02};
                sectionMesh_ = boxMesh(lo - V3{lo.x == hi.x ? t.x : 0, lo.y == hi.y ? t.y : 0, lo.z == hi.z ? t.z : 0},
                                       hi + V3{lo.x == hi.x ? t.x : 0, lo.y == hi.y ? t.y : 0, lo.z == hi.z ? t.z : 0});
            }
        }
        if (!sectionMesh_.empty()) items.push_back({&sectionMesh_, QColor(140, 200, 255), 0.12});
    }
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
    // ITS BODY CLICKED, anywhere on it: a ray against each node's own shape - a connector's whole housing, not one point
    // in the middle of it. The nearest the ray meets.
    if (routeDirty_) buildRouteMeshes();
    {
        V3 origin, dir;
        cam_.rayAt(at.x(), at.y(), width(), height(), origin, dir);
        QString hit;
        double first = std::numeric_limits<double>::max();
        for (const Drawn& d : routeMeshes_) {
            if (d.node.isEmpty()) continue;
            RayIndex3D index;
            index.build({&d.mesh});
            const Pick3D p = index.cast(origin, dir);
            if (p.hit && p.distance < first) {
                first = p.distance;
                hit = d.node;
            }
        }
        if (!hit.isEmpty()) return hit;
    }
    // A CABLE UNDER THE POINTER is what is meant - not a node a few pixels off it.
    if (!segmentUnder(at).isEmpty()) return QString();
    // Or near where it is shown - a small one, from far off.
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

QString View3DCanvas::segmentUnder(const QPointF& at, V3* where) const {
    if (!route_) return QString();
    if (routeDirty_) buildRouteMeshes();
    V3 origin, dir;
    cam_.rayAt(at.x(), at.y(), width(), height(), origin, dir);
    QString hit;
    double first = std::numeric_limits<double>::max();
    for (const Drawn& d : routeMeshes_) {
        if (d.segment.isEmpty()) continue;
        RayIndex3D index;
        index.build({&d.mesh});
        const Pick3D p = index.cast(origin, dir);
        if (p.hit && p.distance < first) {
            first = p.distance;
            hit = d.segment;
            if (where) *where = p.point;
        }
    }
    return hit;
}

QString View3DCanvas::segmentAt(const QPointF& at, V3* point, V3* normal, int* afterVia) const {
    if (!route_) return QString();
    // WHICH BUNDLE: the one whose drawn body is under the pointer - or, a thin one from far off, the one whose drawn
    // middle passes nearest on the screen. Then where on it, from the points it was laid through.
    QString picked = segmentUnder(at);
    if (picked.isEmpty()) {
        double closest = 8.0;
        for (const RouteSegment3D& s : route_->segments) {
            const std::vector<V3> drawn = route_->centreline(s);
            for (size_t j = 0; j + 1 < drawn.size(); ++j) {
                QPointF dp, dq;
                if (!toScreen(drawn[j], dp) || !toScreen(drawn[j + 1], dq)) continue;
                const QPointF de = dq - dp;
                const double dl = de.x() * de.x() + de.y() * de.y();
                const double dt = dl < 1e-9 ? 0.0 : std::clamp(((at.x() - dp.x()) * de.x() + (at.y() - dp.y()) * de.y()) / dl, 0.0, 1.0);
                const double dd = std::hypot(dp.x() + de.x() * dt - at.x(), dp.y() + de.y() * dt - at.y());
                if (dd < closest) {
                    closest = dd;
                    picked = s.id;
                }
            }
        }
    }
    if (picked.isEmpty()) return QString();
    QString best;
    double nearest = std::numeric_limits<double>::max();
    for (const RouteSegment3D& s : route_->segments) {
        if (s.id != picked) continue;
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
            if (point) {
                const std::vector<V3> drawn = route_->centreline(s);
                double bestDrawn = 1e300;
                for (size_t j = 0; j + 1 < drawn.size(); ++j) {
                    QPointF dp, dq;
                    if (!toScreen(drawn[j], dp) || !toScreen(drawn[j + 1], dq)) continue;
                    const QPointF de = dq - dp;
                    const double dl = de.x() * de.x() + de.y() * de.y();
                    const double dt = dl < 1e-9 ? 0.0 : std::clamp(((at.x() - dp.x()) * de.x() + (at.y() - dp.y()) * de.y()) / dl, 0.0, 1.0);
                    const double dd = std::hypot(dp.x() + de.x() * dt - at.x(), dp.y() + de.y() * dt - at.y());
                    if (dd < bestDrawn) {
                        bestDrawn = dd;
                        *point = drawn[j] + (drawn[j + 1] - drawn[j]) * dt - n * (route_->diameterOf(s) / 2.0);
                    }
                }
            }
        }
    }
    return best;
}

void View3DCanvas::cancelLaying() {
    // GIVEN UP: a branch made only to start it from is not left behind on its own.
    if (route_ && layMadeStart_ && route_->node(layFrom_) && route_->degree(layFrom_) == 0) {
        const QString made = layFrom_;
        layFrom_.clear();
        layMadeStart_ = false;
        emit routeAboutToChange();
        route_->removeNode(made);
        routeDirty_ = true;
        emit routeChanged();
    }
    layMadeStart_ = false;
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
        // FROZEN WITH TAB: put where it was frozen, wherever the click is.
        if (ghostFrozen_ && ghostOn_) {
            emit routeAboutToChange();
            selNode_ = route_->addNode(RouteNodeKind::Connector, ghost_.at, ghost_.normal);
            if (RouteNode3D* made = route_->node(selNode_)) {
                made->form = placeTemplate_.form;
                made->formW = placeTemplate_.formW;
                made->formH = placeTemplate_.formH;
                made->formL = placeTemplate_.formL;
                made->color = placeTemplate_.color;
                route_->aimNode(selNode_, made->normal, placeTemplate_.spinDeg);
                if (!placeBrick_.isEmpty()) {
                    made = route_->node(selNode_);
                    made->brick = placeBrick_;
                    made->part = placePart_;
                    made->label = placePart_;
                }
            }
            selSegment_.clear();
            routeDirty_ = true;
            setGhostFrozen(false);
            emit routeChanged();
            break;
        }
        // A HARNESS PART BEING PUT goes where clicked - onto its own housing too (moved there); a free connector
        // clicked on is selected.
        if (!node.isEmpty() && placeBrick_.isEmpty()) {
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
                // IN THE FORM CHOSEN for the next connector (the Connector button's menu, or Tab and the panel).
                if (RouteNode3D* made = route_->node(selNode_); made && made->kind == RouteNodeKind::Connector) {
                    made->form = placeTemplate_.form;
                    made->formW = placeTemplate_.formW;
                    made->formH = placeTemplate_.formH;
                    made->formL = placeTemplate_.formL;
                    made->color = placeTemplate_.color;
                    if (std::abs(placeTemplate_.spinDeg) > 1e-9) route_->aimNode(selNode_, made->normal, placeTemplate_.spinDeg);
                }
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
        const RouteNode3D* on = node.isEmpty() ? nullptr : route_->node(node);
        if (tool_ == RouteTool::Branch && on && on->kind == RouteNodeKind::Connector) {
            // A CONNECTOR: a bundle to start from it - out of its front or its back.
            selNode_ = node;
            routeRedraw();
            emit connectorBranchAsked(node, mapToGlobal(at.toPoint()));
        } else if (!node.isEmpty()) {
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
                // FROM A FACE: a new branch there to start from - taken away again if the bundle is given up.
                emit routeAboutToChange();
                layFrom_ = route_->addNode(RouteNodeKind::Branch, p, n);
                layMadeStart_ = true;
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
    case RouteTool::Measure:
        // FIRST ONE END, THEN THE OTHER - kept, and the next two clicks another ruler.
        if (surfaceAt(at, p, n)) {
            // SURFACE TO SURFACE: the face clicked, to light it.
            const Mesh3D face = measureFaces_ ? faceAround(surfacePart_, p, n) : Mesh3D();
            if (measureStage_ != 1) {
                measureA_ = measureB_ = p;
                measureNormal_ = n.normalized();
                measureStage_ = 1;
                measureFaceA_ = face;
            } else {
                measures_.push_back(measureMaking(p));
                measures_.back().faceA = measureFaceA_;
                measures_.back().faceB = face;
                measureFaceA_ = Mesh3D();
                measureStage_ = 0;
                selMeasure_ = static_cast<int>(measures_.size()) - 1;
                emit message(measureSaid(measures_.back().a, measures_.back().b));
            }
            measureDirty_ = true;
            update();
        }
        break;
    case RouteTool::Delete:
        // A MEASURE, A NODE (WHEREVER ON ITS BODY), OR A BUNDLE: the first of them under the pointer.
        if (const int m = measureAt(at); m >= 0) removeMeasure(m);
        else if (!node.isEmpty()) removeRouteItem(node, QString());
        else if (const QString seg = segmentAt(at); !seg.isEmpty()) removeRouteItem(QString(), seg);
        break;
    }
}

void View3DCanvas::setBackground(int mode, const QColor& colour) {
    bgMode_ = std::clamp(mode, 0, 4);
    if (mode == 4 && colour.isValid()) bgColour_ = colour;
    else if (mode == 0) bgColour_ = QColor(30, 36, 44);
    else if (mode == 1) bgColour_ = QColor(226, 230, 236);
    update();
}

QImage View3DCanvas::render(const QSize& size) const {
    const QSize sz = size.expandedTo(QSize(1, 1));
    const std::vector<DrawItem3D> items = drawItems();
    // A GRADIENT BEHIND IT: the picture drawn on nothing, then laid over the gradient.
    const bool graded = bgMode_ == 2 || bgMode_ == 3;
    QImage img(sz, graded ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32);
    const QColor background = graded ? QColor(0, 0, 0, 0) : bgColour_;
    bool drawn = false;
    if (useGpu_) {
        if (!gl_) gl_ = std::make_unique<GlRenderer3D>();
        drawn = gl_->render(img, items, cam_, wire_, background, section_);
    }
    lastOnGpu_ = drawn;
    if (!drawn) renderScene3D(img, items, cam_, wire_, background, section_);
    if (!graded) return img;
    QImage out(sz, QImage::Format_RGB32);
    QPainter p(&out);
    QLinearGradient g(0, 0, 0, sz.height());
    g.setColorAt(0.0, bgMode_ == 2 ? QColor(70, 88, 112) : QColor(240, 243, 248));
    g.setColorAt(1.0, bgMode_ == 2 ? QColor(16, 19, 25) : QColor(150, 160, 178));
    p.fillRect(out.rect(), g);
    p.drawImage(0, 0, img);
    p.end();
    return out;
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
        // WHERE IT FALLS, by the lowest point of each stretch: how far.
        for (const CableSag3D& g : sags_)
            for (int i : g.falls) {
                QPointF at;
                if (!toScreen(g.hung[static_cast<size_t>(i)], at)) continue;
                const QString text = QStringLiteral("%1 mm down").arg(g.drop[static_cast<size_t>(i)], 0, 'f', 1);
                const QRectF box(at + QPointF(8, 2), QSizeF(p.fontMetrics().horizontalAdvance(text) + 8, p.fontMetrics().height() + 2));
                p.fillRect(box, QColor(20, 24, 30, 200));
                p.setPen(QColor(255, 110, 100));
                p.drawText(box, Qt::AlignCenter, text);
            }
        if (!layFrom_.isEmpty()) {
            const RouteNode3D* from = route_->node(layFrom_);
            p.setPen(QColor(255, 255, 255));
            p.drawText(QPointF(10, 40), QStringLiteral("Laying a bundle from %1 - click the faces it runs over, then the node it "
                                                       "ends at (or double-click for a new branch); Esc gives up")
                                            .arg(from ? from->label : QString()));
        }
    }
    // WHAT EACH RULER SAYS, by its middle - and the one selected (or being made) in full at the foot of the picture.
    if (measureStage_ != 0 || !measures_.empty()) {
        auto label = [&](const Measure3D& m, bool lit) {
            QPointF mid;
            if (!toScreen((m.a + m.b) * 0.5, mid)) return;
            const QString direct = QStringLiteral("%1 mm%2").arg((m.b - m.a).length(), 0, 'f', 1).arg(m.faces ? QStringLiteral(" face to face") : QString());
            const QRectF box(mid + QPointF(8, -22), QSizeF(p.fontMetrics().horizontalAdvance(direct) + 10, p.fontMetrics().height() + 4));
            p.fillRect(box, QColor(20, 24, 30, 210));
            p.setPen(lit ? QColor(80, 220, 255) : QColor(255, 220, 60));
            p.drawText(box, Qt::AlignCenter, direct);
        };
        for (size_t i = 0; i < measures_.size(); ++i) label(measures_[i], static_cast<int>(i) == selMeasure_);
        const Measure3D making = measureMaking(measureB_);
        if (measureStage_ == 1) label(making, false);
        const Measure3D* shown = measureStage_ == 1 ? &making
                                 : selMeasure_ >= 0 && selMeasure_ < static_cast<int>(measures_.size()) ? &measures_[static_cast<size_t>(selMeasure_)]
                                                                                                          : nullptr;
        if (shown) {
            const QString said = measureSaid(shown->a, shown->b);
            const QRectF foot(8, height() - 58, width() - 16, p.fontMetrics().height() + 6);
            p.fillRect(foot, QColor(20, 24, 30, 200));
            p.setPen(QColor(255, 220, 60));
            p.drawText(foot.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       measureStage_ == 1 ? QStringLiteral("Click the other end - ") + said
                                          : said + QStringLiteral("   (Delete takes it away)"));
        }
    }
    // A CABLE THROUGH A KEEP-OUT: the alert along the top of the picture, and each keep-out's name by it.
    if (route_ && keepShown_) {
        for (const KeepOut3D& k : route_->keepOuts) {
            QPointF at;
            const V3 where = k.zone ? V3{std::max(k.lo.x, k.hi.x), std::max(k.lo.y, k.hi.y), std::max(k.lo.z, k.hi.z)}
                                    : V3{k.axis == 0 ? k.at : 0.0, k.axis == 1 ? k.at : 0.0, k.axis == 2 ? k.at : 0.0};
            if (!toScreen(where, at)) continue;
            p.setPen(k.block ? QColor(255, 120, 120) : QColor(255, 190, 90));
            p.drawText(at + QPointF(6, -4), keepOutName(k) + (k.block ? QStringLiteral(" (block)") : QStringLiteral(" (alert)")));
        }
    }
    if (!keepAlert_.isEmpty()) {
        QFont f = p.font();
        f.setBold(true);
        p.setFont(f);
        const QString text = QStringLiteral("\u26A0 ") + keepAlert_;
        const QRectF box(width() * 0.5 - std::min(width() - 20.0, p.fontMetrics().horizontalAdvance(text) + 20.0) / 2.0, 40,
                         std::min(width() - 20.0, p.fontMetrics().horizontalAdvance(text) + 20.0), p.fontMetrics().height() + 8);
        p.fillRect(box, QColor(150, 20, 20, 220));
        p.setPen(QColor(255, 235, 235));
        p.drawText(box.adjusted(8, 0, -8, 0), Qt::AlignCenter | Qt::TextSingleLine,
                   p.fontMetrics().elidedText(text, Qt::ElideRight, static_cast<int>(box.width()) - 16));
        f.setBold(false);
        p.setFont(f);
    }
    // WHICH VIEW IT IS, in the top left corner: Top, Front, Back, Left, Right, Bottom, Iso - or Custom.
    {
        QFont f = p.font();
        f.setBold(true);
        p.setFont(f);
        const QString name = viewName();
        const QRectF box(8, 8, p.fontMetrics().horizontalAdvance(name) + 16, p.fontMetrics().height() + 6);
        p.fillRect(box, QColor(20, 24, 30, 190));
        p.setPen(QColor(230, 236, 244));
        p.drawText(box, Qt::AlignCenter, name);
        f.setBold(false);
        p.setFont(f);
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

void View3DCanvas::setViewLocked(bool on) {
    viewLocked_ = on;
    setCursor(on ? Qt::OpenHandCursor : tool_ == RouteTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}

void View3DCanvas::nudge(double turnDeg, double tiltDeg, double rollDeg) {
    cam_.yawDeg = wrapDegrees(cam_.yawDeg + turnDeg);
    cam_.pitchDeg = wrapDegrees(cam_.pitchDeg + tiltDeg);
    cam_.rollDeg = wrapDegrees(cam_.rollDeg + rollDeg);
    update();
    emit cameraChanged();
}

void View3DCanvas::flip() {
    // TURNED ROUND, SEEN TURNING: half way round the upright and over to the other side of level, over half a second,
    // easing in and out - so it is plain which way it went, and what is now in front was behind.
    const double yaw0 = cam_.yawDeg, pitch0 = cam_.pitchDeg;
    const double yaw1 = yaw0 + 180.0, pitch1 = -pitch0;
    if (turnAnim_) turnAnim_->stop();
    turnAnim_ = new QVariantAnimation(this);
    turnAnim_->setStartValue(0.0);
    turnAnim_->setEndValue(1.0);
    turnAnim_->setDuration(480);
    turnAnim_->setEasingCurve(QEasingCurve::InOutCubic);
    connect(turnAnim_, &QVariantAnimation::valueChanged, this, [this, yaw0, pitch0, yaw1, pitch1](const QVariant& v) {
        const double t = v.toDouble();
        cam_.yawDeg = wrapDegrees(yaw0 + (yaw1 - yaw0) * t);
        cam_.pitchDeg = wrapDegrees(pitch0 + (pitch1 - pitch0) * t);
        update();
        emit cameraChanged();
    });
    connect(turnAnim_, &QVariantAnimation::finished, this, [this, yaw1, pitch1] {
        cam_.yawDeg = wrapDegrees(yaw1);
        cam_.pitchDeg = wrapDegrees(pitch1);
        update();
        emit cameraChanged();
    });
    turnAnim_->start(QAbstractAnimation::DeleteWhenStopped);
}

bool View3DCanvas::turning() const { return turnAnim_ && turnAnim_->state() == QAbstractAnimation::Running; }

void View3DCanvas::setBrickHidden(const QString& brick, bool hidden) {
    if (brick.isEmpty()) return;
    if (hidden) hiddenBricks_.insert(brick);
    else hiddenBricks_.remove(brick);
    update();
}

void View3DCanvas::showAllParts() {
    hiddenBricks_.clear();
    setAllEnvPartsHidden(false);
}

void View3DCanvas::contextMenuEvent(QContextMenuEvent* e) {
    // A drag with the right button moved the view: no menu at the end of it.
    if (e->reason() == QContextMenuEvent::Mouse && moved_) return;
    // ON THE ROUTE - a ruler, a node, a bundle - its own menu.
    if (route_) {
        const QPointF at = e->pos();
        const int m = measureAt(at);
        const QString node = nodeAt(at);
        const QString seg = node.isEmpty() ? segmentAt(at) : QString();
        if (m >= 0 || !node.isEmpty() || !seg.isEmpty()) {
            if (m >= 0) selectMeasure(m);
            emit routeMenuAsked(node, seg, m, e->globalPos());
            return;
        }
    }
    int part = -1;
    QString brick;
    whatIsAt(e->pos(), part, brick);
    emit partMenuAsked(part, brick, e->globalPos());
}

void View3DCanvas::whatIsAt(const QPointF& at, int& part, QString& brick) const {
    V3 origin, dir;
    cam_.rayAt(at.x(), at.y(), width(), height(), origin, dir);
    // WHAT IS UNDER IT: the nearest solid of the model left showing - or a part of the harness, if that is nearer.
    part = -1;
    double nearest = std::numeric_limits<double>::max();
    if (env_ && envIndex_ && envShown_) {
        V3 o = origin;
        for (int tries = 0; tries < 256; ++tries) {
            const Pick3D p = envIndex_->cast(o, dir);
            if (!p.hit) break;
            if (!section_.cuts(p.point) && !envPartHidden(p.part)) {
                part = p.part;
                nearest = (p.point - origin).length();
                break;
            }
            o = p.point + dir * std::max(1e-6, p.distance * 1e-7);
        }
    }
    brick.clear();
    if (scene_ && harnessShown_) {
        // The harness's parts, in a tree kept until they change.
        QStringList hiddenNow(hiddenBricks_.begin(), hiddenBricks_.end());
        hiddenNow.sort();
        const QString key = QStringLiteral("%1/%2/%3").arg(reinterpret_cast<quintptr>(scene_)).arg(scene_->items.size()).arg(hiddenNow.join(QLatin1Char(',')));
        if (!harnessIndex_ || key != harnessIndexKey_) {
            std::vector<const Mesh3D*> meshes;
            harnessIndexBricks_.clear();
            for (const Harness3DItem& it : scene_->items)
                if (!it.brick.isEmpty() && !hiddenBricks_.contains(it.brick)) {
                    meshes.push_back(&it.mesh);
                    harnessIndexBricks_.push_back(it.brick);
                }
            harnessIndex_ = std::make_unique<RayIndex3D>();
            harnessIndex_->build(meshes);
            harnessIndexKey_ = key;
        }
        const Pick3D p = harnessIndex_->cast(origin, dir);
        if (p.hit && p.distance < nearest && p.part >= 0 && static_cast<size_t>(p.part) < harnessIndexBricks_.size()) {
            brick = harnessIndexBricks_[static_cast<size_t>(p.part)];
            part = -1;
        }
    }
}

void View3DCanvas::setEnvPartHidden(int part, bool hidden) {
    if (part < 0) return;
    if (static_cast<size_t>(part) >= envHidden_.size()) envHidden_.resize(static_cast<size_t>(part) + 1, 0);
    envHidden_[static_cast<size_t>(part)] = hidden ? 1 : 0;
    sectionDirty_ = true;
    routeDirty_ = true;
    if (route_) route_->solidsChanged();
    update();
}

void View3DCanvas::setAllEnvPartsHidden(bool hidden) {
    envHidden_.assign(env_ ? env_->parts.size() : 0, hidden ? 1 : 0);
    sectionDirty_ = true;
    routeDirty_ = true;
    if (route_) route_->solidsChanged();
    update();
}

bool View3DCanvas::envPartHidden(int part) const {
    return part >= 0 && static_cast<size_t>(part) < envHidden_.size() && envHidden_[static_cast<size_t>(part)] != 0;
}

bool View3DCanvas::skipPart(int part, const V3& point) const { return section_.cuts(point) || envPartHidden(part); }

void View3DCanvas::clearMeasure() {
    measureStage_ = 0;
    measureFaceA_ = Mesh3D();
    measureDirty_ = true;
    update();
}

void View3DCanvas::clearMeasures() {
    measures_.clear();
    measureFaceA_ = Mesh3D();
    selMeasure_ = -1;
    measureStage_ = 0;
    measureDirty_ = true;
    update();
}

void View3DCanvas::removeMeasure(int index) {
    if (index < 0 || index >= static_cast<int>(measures_.size())) return;
    measures_.erase(measures_.begin() + index);
    selMeasure_ = -1;
    measureDirty_ = true;
    update();
}

void View3DCanvas::selectMeasure(int index) {
    selMeasure_ = index >= 0 && index < static_cast<int>(measures_.size()) ? index : -1;
    measureDirty_ = true;
    update();
}

View3DCanvas::Measure3D View3DCanvas::measureMaking(const V3& to) const {
    Measure3D m{measureA_, to, measureFaces_};
    // SURFACE TO SURFACE: from the second point square back onto the first face - the gap between two faces facing
    // each other, however far apart the points were clicked.
    if (measureFaces_ && measureNormal_.length() > 0.5) m.a = to - measureNormal_ * dot3(to - measureA_, measureNormal_);
    return m;
}

int View3DCanvas::measureAt(const QPointF& at) const {
    int best = -1;
    double nearest = 7.0;
    for (size_t i = 0; i < measures_.size(); ++i) {
        QPointF p, q;
        if (!toScreen(measures_[i].a, p) || !toScreen(measures_[i].b, q)) continue;
        const QPointF e = q - p;
        const double ll = e.x() * e.x() + e.y() * e.y();
        const double t = ll < 1e-9 ? 0.0 : std::clamp(((at.x() - p.x()) * e.x() + (at.y() - p.y()) * e.y()) / ll, 0.0, 1.0);
        double d = std::hypot(p.x() + e.x() * t - at.x(), p.y() + e.y() * t - at.y());
        // Its label, by its middle, as good as its line.
        const QPointF mid = (p + q) * 0.5;
        if (QRectF(mid + QPointF(8, -22), QSizeF(90, 20)).contains(at)) d = 0.0;
        if (d < nearest) {
            nearest = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void View3DCanvas::removeRouteItem(const QString& node, const QString& segment) {
    if (!route_ || (node.isEmpty() && segment.isEmpty())) return;
    emit routeAboutToChange();
    if (!node.isEmpty()) route_->removeNode(node);
    else route_->removeSegment(segment);
    if (selNode_ == node || !route_->node(selNode_)) selNode_.clear();
    if (selSegment_ == segment || !route_->segment(selSegment_)) selSegment_.clear();
    routeDirty_ = true;
    emit routeChanged();
    emit selectionChanged(selSegment_);
}

QString View3DCanvas::measureSaid(const V3& a, const V3& b) {
    const V3 d = b - a;
    auto mm = [](double v) { return QString::number(v, 'f', 1); };
    return QStringLiteral("Direct %1 mm   |   X %2   Y %3   Z %4 mm   |   in the XY plane %5, YZ %6, XZ %7 mm")
        .arg(mm(d.length()), mm(d.x), mm(d.y), mm(d.z), mm(std::hypot(d.x, d.y)), mm(std::hypot(d.y, d.z)), mm(std::hypot(d.x, d.z)));
}

void View3DCanvas::zoomBy(double factor) {
    cam_.distance = std::clamp(cam_.distance * factor, 1.0, 1e7);
    update();
    emit cameraChanged();
}

bool View3DCanvas::beginItemDrag(const QPointF& at) {
    dragKind_ = 0;
    dragStarted_ = false;
    if (!route_) return false;
    V3 anchor;
    const QString node = nodeAt(at);
    if (!node.isEmpty()) {
        dragKind_ = 1;
        dragId_ = node;
        anchor = nodeShown(*route_->node(node));
        selNode_ = node;
        selSegment_.clear();
    } else {
        V3 p, n;
        int after = 0;
        const QString seg = segmentAt(at, &p, &n, &after);
        if (seg.isEmpty()) return false;
        const RouteSegment3D* s = route_->segment(seg);
        // A CORNER NEAR WHERE IT WAS TAKEN HOLD OF: that corner. Anywhere else on it: a corner put there.
        const double r = route_->diameterOf(*s) / 2.0;
        int best = -1;
        double nearest = 12.0;
        for (size_t i = 0; i < s->via.size(); ++i) {
            QPointF q;
            if (!toScreen(s->via[i].at + s->via[i].normal * r, q)) continue;
            const double d = std::hypot(q.x() - at.x(), q.y() - at.y());
            if (d < nearest) {
                nearest = d;
                best = static_cast<int>(i);
            }
        }
        dragId_ = seg;
        if (best >= 0) {
            dragKind_ = 2;
            dragVia_ = best;
            anchor = s->via[static_cast<size_t>(best)].at;
        } else {
            dragKind_ = 3;
            dragVia_ = after;
            dragNew_ = {p, n};
            anchor = p;
        }
        selSegment_ = seg;
        selNode_.clear();
    }
    // AS FAR ACROSS THE MODEL AS THE POINTER GOES ACROSS THE PICTURE, at the depth it is taken hold of.
    V3 right, up, forward;
    cam_.axes(right, up, forward);
    const double z = std::max(cam_.nearPlane(), dot3(anchor - cam_.eye(), forward));
    dragPerPixel_ = 2.0 * z * std::tan(cam_.fovDeg * M_PI / 360.0) / std::max(1, height());
    routeRedraw();
    emit selectionChanged(selSegment_);
    return true;
}

void View3DCanvas::mousePressEvent(QMouseEvent* e) {
    dragging_ = e->button();
    last_ = e->position().toPoint();
    pressedAt_ = last_;
    moved_ = false;
    // MOVE OR STRETCH: a press on a node takes hold of it - or, moving, on a bundle: that stretch of it between its two
    // nodes (a clip, a connector) pulled where it is dragged, its length kept unless pulling adds length.
    if (route_ && e->button() == Qt::LeftButton && (tool_ == RouteTool::Move || tool_ == RouteTool::Stretch)) {
        if (beginNodeDrag(e->position(), tool_ == RouteTool::Stretch)) setCursor(Qt::ClosedHandCursor);
        else if (tool_ == RouteTool::Move && segmentAt(e->position()).size() && beginItemDrag(e->position())) setCursor(Qt::ClosedHandCursor);
        else if (tool_ == RouteTool::Move) {
            // A PART OF THE HARNESS DRAWN: to be moved - the harness put in as a route first, where it is not yet.
            int part = -1;
            QString brick;
            whatIsAt(e->position(), part, brick);
            if (!brick.isEmpty()) emit harnessPartPressed(brick, e->position());
        }
        return;
    }
    // LOCKED: a press on something takes hold of it - a part of the harness drawn too.
    if (viewLocked_ && e->button() == Qt::LeftButton) {
        if (beginItemDrag(e->position())) {
            setCursor(Qt::ClosedHandCursor);
        } else {
            int part = -1;
            QString brick;
            whatIsAt(e->position(), part, brick);
            if (!brick.isEmpty()) emit harnessPartPressed(brick, e->position());
        }
    }
}

void View3DCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (tool_ == RouteTool::Measure && measureStage_ == 1 && dragging_ == Qt::NoButton) {
        V3 p, n;
        if (surfaceAt(e->position(), p, n)) {
            measureB_ = p;
            measureDirty_ = true;
            update();
        }
        return;
    }
    // PUTTING A CONNECTOR: where it would go, under the pointer - unless frozen there with Tab.
    if (dragging_ == Qt::NoButton && tool_ == RouteTool::Connector && route_ && !ghostFrozen_) {
        V3 p, n;
        ghostOn_ = surfaceAt(e->position(), p, n);
        if (ghostOn_) {
            ghost_.at = p;
            ghost_.normal = n.normalized();
        }
        routeDirty_ = true;
        update();
        return;
    }
    // WHAT THE POINTER IS OVER, said under the wheel - looked at no more than every 40 ms.
    if (dragging_ == Qt::NoButton) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - hoverAt_ > 40) {
            hoverAt_ = now;
            const QString node = route_ ? nodeAt(e->position()) : QString();
            const QString seg = route_ && node.isEmpty() ? segmentUnder(e->position()) : QString();
            int part = -1;
            QString brick;
            if (node.isEmpty() && seg.isEmpty()) whatIsAt(e->position(), part, brick);
            const QString key = QStringLiteral("%1|%2|%3|%4").arg(node, seg).arg(part).arg(brick);
            if (key != hoverKey_) {
                hoverKey_ = key;
                emit hovered(part, brick, node, seg);
            }
        }
    }
    // SELECTING: a hand over what a click would select - a connector, a clip, a cable.
    if (dragging_ == Qt::NoButton && tool_ == RouteTool::Select && route_) {
        const bool over = !nodeAt(e->position()).isEmpty() || !segmentUnder(e->position()).isEmpty();
        setCursor(over ? Qt::PointingHandCursor : Qt::ArrowCursor);
        return;
    }
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
    // SOMETHING HELD, MOVED: across the view, at its own depth. The first move is one change, to undo as one.
    if (dragKind_ != 0 && route_) {
        if (!dragStarted_) {
            dragStarted_ = true;
            emit routeAboutToChange();
            if (dragKind_ == 3) {
                if (RouteSegment3D* s = route_->segment(dragId_)) {
                    const int at = std::clamp(dragVia_, 0, static_cast<int>(s->via.size()));
                    s->via.insert(s->via.begin() + at, dragNew_);
                    dragVia_ = at;
                    dragKind_ = 2;
                }
            }
        }
        const QPoint dd = now - last_;
        last_ = now;
        // STRETCHED: the node where it is, its cables pulled tight through it as far as the pointer has gone.
        if (dragKind_ == 4) {
            if (kept_.empty()) beginKeepLength();
            const QPoint gone = now - pressedAt_;
            tightenKept(std::hypot(gone.x(), gone.y()) * dragPerPixel_);
            if (const RouteNode3D* n = route_->node(dragId_))
                emit message(QStringLiteral("%1: %2 mm of cable pulled through and cut off").arg(n->label).arg(stretchCut_, 0, 'f', 1));
            routeDirty_ = true;
            update();
            emit routeMoving();
            return;
        }
        V3 right, up, forward;
        cam_.axes(right, up, forward);
        // ACROSS THE VIEW - right, left, up, down as the pointer goes; CTRL HELD, the pointer up and down pushes it away
        // from the eye and pulls it back toward it.
        const V3 by = (e->modifiers() & Qt::ControlModifier) ? forward * (-dd.y() * dragPerPixel_)
                                                             : right * (dd.x() * dragPerPixel_) - up * (dd.y() * dragPerPixel_);
        // ITS LENGTH KEPT: the point where the pointer is - on the model, or across the picture - and the cable pulled
        // along behind it.
        if (keepLength()) {
            if (kept_.empty()) beginKeepLength();
            V3 held;
            if (dragKind_ == 2) {
                const RouteSegment3D* s = route_->segment(dragId_);
                if (s && dragVia_ >= 0 && dragVia_ < static_cast<int>(s->via.size())) held = s->via[static_cast<size_t>(dragVia_)].at;
            } else if (const RouteNode3D* n = route_->node(dragId_)) {
                held = n->at;
            }
            V3 target = held + by;
            V3 p, nn;
            // SHIFT: over the model's faces, where the pointer is; otherwise across the view (and Ctrl, into it).
            if (dragKind_ == 5 && (e->modifiers() & Qt::ShiftModifier) && surfaceAt(e->position(), p, nn)) {
                target = p;
                if (RouteNode3D* n = route_->node(dragId_)) n->normal = nn.normalized();
            }
            pullKept(target);
            routeDirty_ = true;
            update();
            emit routeMoving();
            return;
        }
        if (dragKind_ == 5) {
            // SHIFT: ON THE MODEL WHERE THE POINTER IS - otherwise across the picture (Ctrl: into it).
            if (RouteNode3D* n = route_->node(dragId_)) {
                V3 p, nn;
                if ((e->modifiers() & Qt::ShiftModifier) && surfaceAt(e->position(), p, nn)) {
                    n->at = p;
                    n->normal = nn.normalized();
                } else {
                    n->at = n->at + by;
                }
            }
            routeDirty_ = true;
            update();
            emit routeMoving();
            return;
        }
        if (dragKind_ == 1) {
            if (RouteNode3D* n = route_->node(dragId_)) n->at = n->at + by;
        } else if (dragKind_ == 2) {
            if (RouteSegment3D* s = route_->segment(dragId_))
                if (dragVia_ >= 0 && dragVia_ < static_cast<int>(s->via.size()))
                    s->via[static_cast<size_t>(dragVia_)].at = s->via[static_cast<size_t>(dragVia_)].at + by;
        }
        routeDirty_ = true;
        update();
        return;
    }
    // Locked: the view stays as it is.
    if (viewLocked_) return;
    const QPoint d = now - last_;
    last_ = now;
    // TURNED WITH THE WHEEL BUTTON held (Shift: rolled); moved with the right button, or Ctrl and the wheel button. The left
    // button is for picking and placing, and does not turn the view.
    const bool wheelButton = dragging_ == Qt::MiddleButton && !(e->modifiers() & Qt::ControlModifier);
    if (dragging_ == Qt::LeftButton) return;
    if (wheelButton && (e->modifiers() & Qt::ShiftModifier)) {
        // ROLLED round the way it looks.
        cam_.rollDeg = wrapDegrees(cam_.rollDeg + d.x() * 0.4);
    } else if (wheelButton) {
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
    // SOMETHING HELD, LET GO: moved - one change made; not moved - it is selected, and that is all.
    if (dragKind_ != 0) {
        const bool changed = dragStarted_;
        // TAKEN HOLD OF AND LET GO WITHOUT MOVING: a click - what was under it selected.
        if (!changed && route_) {
            const QString node = nodeAt(e->position());
            const QString seg = node.isEmpty() ? segmentAt(e->position()) : QString();
            if (!node.isEmpty() || !seg.isEmpty()) {
                selNode_ = node;
                selSegment_ = seg;
                routeRedraw();
                setFocus();
                emit selectionChanged(selSegment_);
            }
        }
        dragKind_ = 0;
        dragStarted_ = false;
        dragging_ = Qt::NoButton;
        kept_.clear();
        keptLen_.clear();
        through_.clear();
        keptBase_.clear();
        setCursor(tool_ == RouteTool::Move || tool_ == RouteTool::Stretch ? Qt::SizeAllCursor : Qt::OpenHandCursor);
        if (changed) emit routeChanged();
        return;
    }
    const bool click = dragging_ == Qt::LeftButton && !moved_ && e->button() == Qt::LeftButton;
    dragging_ = Qt::NoButton;
    if (swallowRelease_) {
        swallowRelease_ = false;
        return;
    }
    if (!click) return;
    // SAME AS: the connector clicked is copied - its form, size, how it lies and its colour - to the one asked for.
    if (!sameAs_.isEmpty() && route_) {
        const QString from = nodeAt(e->position());
        const RouteNode3D* src = route_->node(from);
        if (src && from != sameAs_ && route_->node(sameAs_)) {
            emit routeAboutToChange();
            RouteNode3D* to = route_->node(sameAs_);
            to->form = src->form;
            to->formW = src->formW;
            to->formH = src->formH;
            to->formL = src->formL;
            to->color = src->color;
            route_->aimNode(sameAs_, to->normal, src->spinDeg);
            routeDirty_ = true;
            emit message(QStringLiteral("%1 made the same as %2").arg(route_->node(sameAs_)->label, src->label));
            sameAs_.clear();
            emit routeChanged();
            return;
        }
        if (!src) {
            sameAs_.clear();
            emit message(QString());
        }
        return;
    }
    // A CLICK, NOT A DRAG: the route tool's, or - selecting - a node or bundle, else the point of the environment under
    // the pointer.
    if (route_ && tool_ != RouteTool::Select) {
        routeClick(e->position());
        return;
    }
    // A RULER CLICKED: selected, to delete.
    if (const int m = measureAt(e->position()); m >= 0) {
        selectMeasure(m);
        setFocus();
        return;
    }
    if (selMeasure_ >= 0) selectMeasure(-1);
    if (route_) {
        const QString node = nodeAt(e->position());
        const QString seg = node.isEmpty() ? segmentAt(e->position()) : QString();
        if (!node.isEmpty() || !seg.isEmpty()) {
            selNode_ = node;
            selSegment_ = seg;
            routeRedraw();
            setFocus();
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
    // A PART OF THE HARNESS DRAWN, clicked: selected - lit, and said what it is.
    {
        int part = -1;
        QString brick;
        whatIsAt(e->position(), part, brick);
        if (!brick.isEmpty()) {
            setHighlight({brick});
            emit harnessPartClicked(brick);
            return;
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
    if (viewLocked_) return;
    fit();
}

void View3DCanvas::keyPressEvent(QKeyEvent* e) {
    if (e->matches(QKeySequence::Undo)) {
        emit undoRequested();
        return;
    }
    if (e->matches(QKeySequence::Redo) || (e->key() == Qt::Key_Y && (e->modifiers() & Qt::ControlModifier))) {
        emit redoRequested();
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
    if (e->key() == Qt::Key_Escape && !sameAs_.isEmpty()) {
        sameAs_.clear();
        emit message(QString());
        return;
    }
    if (e->key() == Qt::Key_Escape && ghostFrozen_) {
        setGhostFrozen(false);
        return;
    }
    // ENTER, A CONNECTOR FROZEN WITH TAB: put there.
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && ghostFrozen_) {
        routeClick(QPointF());
        return;
    }
    if (e->key() == Qt::Key_Escape && measureStage_ != 0) {
        clearMeasure();
        return;
    }
    // THE RULER SELECTED, deleted.
    if (e->key() == Qt::Key_Delete && selMeasure_ >= 0) {
        removeMeasure(selMeasure_);
        return;
    }
    // ESC, NOTHING HALF DONE: the tool put down - Select again - then the selection let go.
    if (e->key() == Qt::Key_Escape && tool_ != RouteTool::Select) {
        emit toolCancelRequested();
        return;
    }
    if (e->key() == Qt::Key_Escape && (!selNode_.isEmpty() || !selSegment_.isEmpty())) {
        selNode_.clear();
        selSegment_.clear();
        routeRedraw();
        emit selectionChanged(QString());
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
        removeRouteItem(selNode_, selNode_.isEmpty() ? selSegment_ : QString());
        return;
    }
    QWidget::keyPressEvent(e);
}

void View3DCanvas::wheelEvent(QWheelEvent* e) {
    if (viewLocked_) return;   // locked: the view stays as it is
    // ZOOMED: nearer or further, a step a notch.
    const double notches = e->angleDelta().y() / 120.0;
    cam_.distance = std::clamp(cam_.distance * std::pow(0.85, notches), 1.0, 1e7);
    update();
}

} // namespace Edraw_Desktop
