#include "app/Part3DEditorDialog.h"

#include "app/Icons3D.h"
#include "app/NavWheel.h"
#include "app/Chrome.h"
#include "app/View3DPane.h"
#include "render/Render3D.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace Loopwire {

namespace {
const char* kAxes[3] = {"X", "Y", "Z"};
} // namespace

Part3DEditorDialog::Part3DEditorDialog(const LibraryPart& part, const PartModel3D& start, QWidget* parent)
    : QDialog(parent), part_(part), model_(start) {
    setWindowTitle(QStringLiteral("3D model - %1").arg(part.title()));
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    resize(1400, 860);
    auto* lay = new QVBoxLayout(this);

    // ---- ADD: the simple solids, and what is made from the part ----
    auto* addRow = new QHBoxLayout;
    addRow->addWidget(new QLabel(QStringLiteral("Add:"), this));
    // AS A 3D PROGRAM'S BUTTONS ARE: each a picture of what it does, its name under it.
    auto cadButton = [this](const QString& text) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setIconSize(QSize(26, 26));
        b->setAutoRaise(true);
        return b;
    };
    auto addButton = [&](BodyShape shape, const char* name) {
        auto* b = cadButton(bodyShapeName(shape));
        b->setObjectName(QLatin1String(name));
        connect(b, &QToolButton::clicked, this, [this, shape] { addBody(shape); });
        addRow->addWidget(b);
    };
    addButton(BodyShape::Box, "part3dAddBox");
    addButton(BodyShape::Cylinder, "part3dAddCylinder");
    addButton(BodyShape::Tube, "part3dAddTube");
    addButton(BodyShape::Cone, "part3dAddCone");
    addButton(BodyShape::Sphere, "part3dAddSphere");
    addButton(BodyShape::Torus, "part3dAddTorus");
    addButton(BodyShape::Hex, "part3dAddHex");
    addRow->addSpacing(12);
    addButton(BodyShape::Turned, "part3dAddTurned");
    addButton(BodyShape::Extruded, "part3dAddExtruded");
    addButton(BodyShape::Pins, "part3dAddPins");
    addRow->addSpacing(12);
    // DRAWN IN THE VIEW: an outline sketched on a plane and given depth; pins and sockets put on a face where clicked.
    {
        QToolButton* sk = cadButton(QStringLiteral("Sketch"));
        sk->setObjectName(QStringLiteral("part3dSketch"));
        sk->setToolTip(QStringLiteral("Draw outlines flat on a plane - lines, rectangles, circles, polygons, slots - then extrude "
                                      "or revolve them: a new solid, or added to, cut out of, or kept where it meets the one selected"));
        connect(sk, &QToolButton::clicked, this, [this] { openSketch2D(0); });
        addRow->addWidget(sk);
        faceSketchButton_ = cadButton(QStringLiteral("Sketch on face"));
        faceSketchButton_->setObjectName(QStringLiteral("part3dSketchFace"));
        faceSketchButton_->setCheckable(true);
        faceSketchButton_->setToolTip(QStringLiteral("Click a face of a solid to sketch on its plane - what is made goes with that solid"));
        connect(faceSketchButton_, &QToolButton::toggled, this, [this](bool on) { setFaceSketchTool(on); });
        addRow->addWidget(faceSketchButton_);
        for (int kind = 1; kind <= 2; ++kind) {
            QToolButton* b = cadButton(kind == 1 ? QStringLiteral("Pin") : QStringLiteral("Socket"));
            b->setObjectName(kind == 1 ? QStringLiteral("part3dPinTool") : QStringLiteral("part3dSocketTool"));
            b->setCheckable(true);
            b->setToolTip(kind == 1 ? QStringLiteral("Click a face to put a pin on it, numbered in turn (click again to stop)")
                                    : QStringLiteral("Click a face to put a socket in it - a hole, numbered in turn (click again to stop)"));
            connect(b, &QToolButton::toggled, this, [this, kind](bool on) { setPinTool(on ? kind : (pinTool_ == kind ? 0 : pinTool_)); });
            addRow->addWidget(b);
            pinButtons_[kind - 1] = b;
        }
    }
    auto* automatic = cadButton(QStringLiteral("Auto create 3D"));
    automatic->setObjectName(QStringLiteral("part3dAutoCreate"));
    automatic->setToolTip(QStringLiteral("Use the pinout outline as the cross-section, following the mechanical outline along its length, with pins and sockets."));
    connect(automatic, &QToolButton::clicked, this, [this] { autoCreateFromDrawings(); });
    addRow->addWidget(automatic);
    addRow->addStretch(1);
    lay->addLayout(addRow);

    // ---- MODIFY: the one selected ----
    auto* modRow = new QHBoxLayout;
    modRow->addWidget(new QLabel(QStringLiteral("Modify:"), this));
    auto modButton = [&](const char* name, const QString& text, const QString& tip, auto fn) {
        auto* b = cadButton(text);
        b->setObjectName(QLatin1String(name));
        b->setToolTip(tip);
        connect(b, &QToolButton::clicked, this, fn);
        modRow->addWidget(b);
        return b;
    };
    modButton("part3dDuplicate", QStringLiteral("Duplicate"), QStringLiteral("A copy of it, a little to one side"),
              [this] { duplicateSelected(); });
    modButton("part3dDelete", QStringLiteral("Delete"), QStringLiteral("Take it away (Delete)"), [this] { deleteSelected(); });
    for (int ax = 0; ax < 3; ++ax)
        modButton(QStringLiteral("part3dMirror%1").arg(QLatin1String(kAxes[ax])).toLatin1().constData(),
                  QStringLiteral("Mirror %1").arg(QLatin1String(kAxes[ax])),
                  QStringLiteral("Its mirror image across the plane square to %1, through the middle").arg(QLatin1String(kAxes[ax])),
                  [this, ax] { mirrorSelected(ax); });
    for (int ax = 0; ax < 3; ++ax)
        modButton(QStringLiteral("part3dTurn%1").arg(QLatin1String(kAxes[ax])).toLatin1().constData(),
                  QStringLiteral("90° %1").arg(QLatin1String(kAxes[ax])),
                  QStringLiteral("Turn it a quarter turn round %1").arg(QLatin1String(kAxes[ax])), [this, ax] { turnSelected(ax, 90.0); });
    modButton("part3dArray", QStringLiteral("Array..."), QStringLiteral("Copies of it in a row, or round an axis"), [this] {
        if (selected_ < 0) return;
        QDialog d(this);
        d.setWindowTitle(QStringLiteral("Array"));
        auto* f = new QFormLayout(&d);
        auto* count = new QSpinBox(&d);
        count->setRange(2, 200);
        count->setValue(4);
        f->addRow(QStringLiteral("Copies, the first with them"), count);
        QDoubleSpinBox* step[3];
        for (int ax = 0; ax < 3; ++ax) {
            step[ax] = new QDoubleSpinBox(&d);
            step[ax]->setRange(-10000, 10000);
            step[ax]->setSuffix(QStringLiteral(" mm"));
            step[ax]->setValue(ax == 0 ? 10.0 : 0.0);
            f->addRow(QStringLiteral("Each one on along %1").arg(QLatin1String(kAxes[ax])), step[ax]);
        }
        auto* round = new QComboBox(&d);
        round->addItems({QStringLiteral("not turned"), QStringLiteral("round X"), QStringLiteral("round Y"), QStringLiteral("round Z")});
        f->addRow(QStringLiteral("And turned"), round);
        auto* turnStep = new QDoubleSpinBox(&d);
        turnStep->setRange(-360, 360);
        turnStep->setValue(90);
        turnStep->setSuffix(QStringLiteral("°"));
        f->addRow(QStringLiteral("Each one by"), turnStep);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
        f->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        if (d.exec() != QDialog::Accepted) return;
        arraySelected(count->value(), {step[0]->value(), step[1]->value(), step[2]->value()}, round->currentIndex() - 1,
                      turnStep->value());
    });
    modButton("part3dCentre", QStringLiteral("To the middle"), QStringLiteral("Put it at the model's origin"), [this] { centreSelected(); });
    modButton("part3dRenumber", QStringLiteral("Number pins"), QStringLiteral("The pins and sockets numbered 1, 2, 3... in the order they are listed"),
              [this] { renumberPins(); });
    modRow->addSpacing(12);
    // COMBINE: the one selected with another, chosen from a list of them.
    modButton("part3dUnion", QStringLiteral("Add"), QStringLiteral("Join it to another solid - one solid of both"),
              [this] { combineMenu(BodyOp::Add); });
    modButton("part3dSubtract", QStringLiteral("Subtract"), QStringLiteral("Take it out of another solid - a hole, a pocket, a slot"),
              [this] { combineMenu(BodyOp::Subtract); });
    modButton("part3dIntersect", QStringLiteral("Intersect"), QStringLiteral("Keep only where it and another solid meet"),
              [this] { combineMenu(BodyOp::Intersect); });
    modButton("part3dSeparate", QStringLiteral("Separate"), QStringLiteral("A solid of its own again"), [this] { separateSelected(); });
    modRow->addSpacing(12);
    modButton("part3dUndo", QStringLiteral("Undo"), QStringLiteral("Take back the last change (Ctrl+Z)"), [this] { undo(); });
    modButton("part3dRedo", QStringLiteral("Redo"), QStringLiteral("Do it again (Ctrl+Y)"), [this] { redo(); });
    modRow->addSpacing(12);
    for (const auto& [name, key] : {std::pair<const char*, const char*>{"Iso", "iso"}, {"Front", "front"}, {"Top", "top"}, {"Side", "side"}})
        modButton(QStringLiteral("part3dView%1").arg(QLatin1String(name)).toLatin1().constData(), QString::fromLatin1(name),
                  QStringLiteral("View it from there"), [this, k = QString::fromLatin1(key)] { canvas_->viewFrom(k); });
    modButton("part3dFit", QStringLiteral("Fit"), QStringLiteral("All of it in view"), [this] { canvas_->fit(); });
    modRow->addWidget(new QLabel(QStringLiteral("Step"), this));
    moveStep_ = new QDoubleSpinBox(this);
    moveStep_->setObjectName(QStringLiteral("part3dStep"));
    moveStep_->setRange(0.01, 1000);
    moveStep_->setDecimals(2);
    moveStep_->setValue(1.0);
    moveStep_->setSuffix(QStringLiteral(" mm"));
    moveStep_->setToolTip(QStringLiteral("How far each arrow of the wheel moves the one selected"));
    modRow->addWidget(moveStep_);
    modRow->addStretch(1);
    lay->addLayout(modRow);
    {
        // THE PICTURES, by what each button does.
        const QColor ink = Chrome::ink();
        const struct {
            const char* name;
            const char* icon;
        } icons[] = {
            {"part3dAddBox", "box"},       {"part3dAddCylinder", "cylinder"}, {"part3dAddTube", "tube"},
            {"part3dAddCone", "cone"},     {"part3dAddSphere", "sphere"},     {"part3dAddTorus", "torus"},
            {"part3dAddHex", "hex"},       {"part3dAddTurned", "turned"},     {"part3dAddExtruded", "extruded"},
            {"part3dAddPins", "pins"},     {"part3dDuplicate", "duplicate"},  {"part3dDelete", "delete"},
            {"part3dMirrorX", "mirrorx"},  {"part3dMirrorY", "mirrory"},      {"part3dMirrorZ", "mirrorz"},
            {"part3dTurnX", "turnx"},      {"part3dTurnY", "turny"},          {"part3dTurnZ", "turnz"},
            {"part3dArray", "array"},      {"part3dCentre", "middle"},        {"part3dUndo", "undo"},
            {"part3dRedo", "redo"},        {"part3dViewIso", "iso"},          {"part3dViewFront", "front"},
            {"part3dViewTop", "top"},      {"part3dViewSide", "side"},        {"part3dFit", "fit"},
            {"part3dSketch", "sketch"},    {"part3dPinTool", "pin"},          {"part3dSocketTool", "socket"},
            {"part3dRenumber", "renumber"}, {"part3dSketchFace", "sketchface"}, {"part3dUnion", "union"},
            {"part3dSubtract", "subtract"}, {"part3dIntersect", "intersect"}, {"part3dSeparate", "separate"},
        };
        for (const auto& i : icons)
            if (auto* b = findChild<QToolButton*>(QLatin1String(i.name))) b->setIcon(icon3D(QLatin1String(i.icon), ink));
    }
    // ---- THE SKETCH: its plane, what is drawn, how deep - shown while one is being drawn ----
    sketchBar_ = new QWidget(this);
    sketchBar_->setObjectName(QStringLiteral("part3dSketchBar"));
    {
        auto* sb = new QHBoxLayout(sketchBar_);
        sb->setContentsMargins(0, 0, 0, 0);
        sb->addWidget(new QLabel(QStringLiteral("<b>Sketch</b> on"), sketchBar_));
        sketchPlaneBox_ = new QComboBox(sketchBar_);
        sketchPlaneBox_->setObjectName(QStringLiteral("part3dSketchPlane"));
        sketchPlaneBox_->addItems({QStringLiteral("Top (X, Y)"), QStringLiteral("Front (X, Z)"), QStringLiteral("Side (Y, Z)")});
        connect(sketchPlaneBox_, &QComboBox::activated, this, [this](int i) { startSketch(i); });
        sb->addWidget(sketchPlaneBox_);
        sb->addWidget(new QLabel(QStringLiteral("at"), sketchBar_));
        sketchAtBox_ = new QDoubleSpinBox(sketchBar_);
        sketchAtBox_->setObjectName(QStringLiteral("part3dSketchAt"));
        sketchAtBox_->setRange(-10000, 10000);
        sketchAtBox_->setSuffix(QStringLiteral(" mm"));
        sketchAtBox_->setToolTip(QStringLiteral("Where the plane is, along the way the outline is given depth"));
        connect(sketchAtBox_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
            sketchAt_ = v;
            rebuild(false);
        });
        sb->addWidget(sketchAtBox_);
        sketchShape_ = new QComboBox(sketchBar_);
        sketchShape_->setObjectName(QStringLiteral("part3dSketchShape"));
        sketchShape_->addItems({QStringLiteral("Lines - click each corner"), QStringLiteral("Rectangle - two corners"),
                                QStringLiteral("Circle - its middle, then its edge")});
        connect(sketchShape_, &QComboBox::activated, this, [this](int) {
            sketch_.clear();
            sketchFirst_.reset();
            rebuild(false);
        });
        sb->addWidget(sketchShape_);
        sb->addWidget(new QLabel(QStringLiteral("Depth"), sketchBar_));
        sketchDepth_ = new QDoubleSpinBox(sketchBar_);
        sketchDepth_->setObjectName(QStringLiteral("part3dSketchDepth"));
        sketchDepth_->setRange(-10000, 10000);
        sketchDepth_->setValue(5.0);
        sketchDepth_->setSuffix(QStringLiteral(" mm"));
        sketchDepth_->setToolTip(QStringLiteral("How deep: toward you as you look at the plane - or away from you, below nothing"));
        sb->addWidget(sketchDepth_);
        auto* back = new QPushButton(QStringLiteral("Point back"), sketchBar_);
        connect(back, &QPushButton::clicked, this, [this] {
            if (!sketch_.empty()) sketch_.pop_back();
            rebuild(false);
        });
        sb->addWidget(back);
        auto* extrude = new QPushButton(QStringLiteral("Extrude"), sketchBar_);
        extrude->setObjectName(QStringLiteral("part3dSketchExtrude"));
        extrude->setIcon(icon3D(QStringLiteral("extrude"), Chrome::ink()));
        connect(extrude, &QPushButton::clicked, this, [this] { finishSketch(sketchDepth_->value()); });
        sb->addWidget(extrude);
        auto* cancel = new QPushButton(QStringLiteral("Cancel"), sketchBar_);
        connect(cancel, &QPushButton::clicked, this, [this] { cancelSketch(); });
        sb->addWidget(cancel);
        sb->addStretch(1);
    }
    sketchBar_->hide();
    lay->addWidget(sketchBar_);

    // ---- THE BODIES, THE VIEW, THE ONE SELECTED ----
    auto* split = new QSplitter(Qt::Horizontal, this);
    auto* left = new QWidget(split);
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->addWidget(new QLabel(QStringLiteral("Its solids (tick: shown)"), left));
    list_ = new QListWidget(left);
    list_->setObjectName(QStringLiteral("part3dBodies"));
    ll->addWidget(list_, 1);
    split->addWidget(left);
    canvas_ = new View3DCanvas(split);
    canvas_->setObjectName(QStringLiteral("part3dCanvas"));
    canvas_->setMinimumSize(560, 420);
    canvas_->installEventFilter(this);
    split->addWidget(canvas_);
    // THE WHEEL over the view, as in the 3D view of a harness: the view's ring, and - its middle clicked - the arrows
    // that move the one selected.
    wheel_ = new NavWheel(canvas_);
    wheel_->setObjectName(QStringLiteral("part3dWheel"));
    viewWheel_ = {
        {QStringLiteral("up"), QStringLiteral("Tilt up"), QStringLiteral("Tilt the view up and over"), [this] { canvas_->nudge(0, wheelTurn_, 0); }, {}},
        {QStringLiteral("plus"), QStringLiteral("Zoom in"), QStringLiteral("Nearer"), [this] { canvas_->zoomBy(0.8); }, {}},
        {QStringLiteral("cw"), QStringLiteral("Roll"), QStringLiteral("Roll the view clockwise round the way it looks"), [this] { canvas_->nudge(0, 0, wheelTurn_); }, {}},
        {QStringLiteral("right"), QStringLiteral("Turn"), QStringLiteral("Turn the view to the right, round the upright"), [this] { canvas_->nudge(-wheelTurn_, 0, 0); }, {}},
        {QStringLiteral("fit"), QStringLiteral("Fit"), QStringLiteral("Everything in view"), [this] { canvas_->fit(); }, {}},
        {QStringLiteral("lock"), QStringLiteral("Lock"), QStringLiteral("Hold the view still - a drag moves the one selected"),
         [this] { canvas_->setViewLocked(!canvas_->viewLocked()); }, [this] { return canvas_->viewLocked(); }},
        {QStringLiteral("down"), QStringLiteral("Tilt down"), QStringLiteral("Tilt the view down and under"), [this] { canvas_->nudge(0, -wheelTurn_, 0); }, {}},
        {QStringLiteral("gear"), QStringLiteral("Options"), QStringLiteral("How far each click turns the view, and moves the one selected"),
         [this] {
             QMenu menu(this);
             for (int deg : {5, 15, 30, 45, 90}) {
                 QAction* a = menu.addAction(QStringLiteral("Turn the view %1° a click").arg(deg));
                 a->setCheckable(true);
                 a->setChecked(std::abs(wheelTurn_ - deg) < 1e-9);
                 connect(a, &QAction::triggered, this, [this, deg] { wheelTurn_ = deg; });
             }
             menu.addSeparator();
             for (double mm : {0.1, 0.5, 1.0, 5.0, 10.0}) {
                 QAction* a = menu.addAction(QStringLiteral("Move %1 mm a click").arg(mm));
                 a->setCheckable(true);
                 a->setChecked(std::abs(moveStep_->value() - mm) < 1e-9);
                 connect(a, &QAction::triggered, this, [this, mm] { setMoveStep(mm); });
             }
             menu.exec(QCursor::pos());
         }, {}},
        {QStringLiteral("iso"), QStringLiteral("Iso"), QStringLiteral("At an angle, the whole of it"), [this] { canvas_->viewFrom(QStringLiteral("iso")); }, {}},
        {QStringLiteral("left"), QStringLiteral("Turn"), QStringLiteral("Turn the view to the left, round the upright"), [this] { canvas_->nudge(wheelTurn_, 0, 0); }, {}},
        {QStringLiteral("ccw"), QStringLiteral("Roll"), QStringLiteral("Roll the view counter-clockwise round the way it looks"), [this] { canvas_->nudge(0, 0, -wheelTurn_); }, {}},
        {QStringLiteral("minus"), QStringLiteral("Zoom out"), QStringLiteral("Further off"), [this] { canvas_->zoomBy(1.25); }, {}},
    };
    wheelMoving_ = true;
    setWheelMoving(false);
    // THE ARROWS the way the axes run as the view is turned.
    connect(canvas_, &View3DCanvas::cameraChanged, this, [this] {
        if (wheelMoving_) buildMoveWheel();
    });

    auto* scroll = new QScrollArea(split);
    scroll->setWidgetResizable(true);
    panel_ = new QWidget(scroll);
    auto* pf = new QVBoxLayout(panel_);
    shapeSaid_ = new QLabel(panel_);
    pf->addWidget(shapeSaid_);
    auto* form = new QFormLayout;
    name_ = new QLineEdit(panel_);
    name_->setObjectName(QStringLiteral("part3dName"));
    form->addRow(QStringLiteral("Name"), name_);
    auto spin = [&](double lo, double hi, const QString& suffix) {
        auto* s = new QDoubleSpinBox(panel_);
        s->setRange(lo, hi);
        s->setDecimals(2);
        s->setSuffix(suffix);
        s->setKeyboardTracking(false);
        return s;
    };
    for (int i = 0; i < 3; ++i) {
        sizeLabel_[i] = new QLabel(panel_);
        size_[i] = spin(0.0, 10000.0, QStringLiteral(" mm"));
        size_[i]->setObjectName(QStringLiteral("part3dSize%1").arg(i));
        form->addRow(sizeLabel_[i], size_[i]);
    }
    auto triple = [&](const QString& what, QDoubleSpinBox** boxes, double lo, double hi, const QString& suffix, const char* name) {
        auto* row = new QWidget(panel_);
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        for (int i = 0; i < 3; ++i) {
            rl->addWidget(new QLabel(QLatin1String(kAxes[i]), row));
            boxes[i] = spin(lo, hi, suffix);
            boxes[i]->setObjectName(QStringLiteral("%1%2").arg(QLatin1String(name), QLatin1String(kAxes[i])));
            rl->addWidget(boxes[i], 1);
        }
        form->addRow(what, row);
    };
    triple(QStringLiteral("Where"), at_, -10000, 10000, QStringLiteral(" mm"), "part3dAt");
    triple(QStringLiteral("Turned"), turn_, -360, 360, QStringLiteral("°"), "part3dTurn");
    triple(QStringLiteral("Scaled"), scale_, -100, 100, QString(), "part3dScale");
    for (QDoubleSpinBox* s : scale_) s->setSingleStep(0.1);
    color_ = new QPushButton(panel_);
    color_->setObjectName(QStringLiteral("part3dColor"));
    form->addRow(QStringLiteral("Colour"), color_);
    material_ = new QComboBox(panel_);
    material_->setObjectName(QStringLiteral("part3dMaterial"));
    material_->addItem(QStringLiteral("Plain"), Texture3D::None);
    material_->addItem(QStringLiteral("Metal"), Texture3D::Metal);
    material_->addItem(QStringLiteral("Plastic"), Texture3D::Plastic);
    material_->addItem(QStringLiteral("Matte (rubber)"), Texture3D::Matte);
    form->addRow(QStringLiteral("Made of"), material_);
    cut_ = new QCheckBox(QStringLiteral("A hole - cut out of the solids it meets"), panel_);
    cut_->setObjectName(QStringLiteral("part3dCut"));
    form->addRow(QString(), cut_);
    shown_ = new QCheckBox(QStringLiteral("Shown"), panel_);
    form->addRow(QString(), shown_);
    // ITS EDGES: rounded, or cut off at 45 degrees - every edge of it.
    fillet_ = spin(0.0, 1000.0, QStringLiteral(" mm"));
    fillet_->setObjectName(QStringLiteral("part3dFillet"));
    fillet_->setToolTip(QStringLiteral("Every edge rounded to this radius - 0 sharp (an edge it is too big for is left sharp)"));
    filletLabel_ = new QLabel(QStringLiteral("Fillet"), panel_);
    form->addRow(filletLabel_, fillet_);
    chamfer_ = spin(0.0, 1000.0, QStringLiteral(" mm"));
    chamfer_->setObjectName(QStringLiteral("part3dChamfer"));
    chamfer_->setToolTip(QStringLiteral("Every edge cut off at 45 degrees, this far back - 0 sharp (a fillet goes first)"));
    chamferLabel_ = new QLabel(QStringLiteral("Chamfer"), panel_);
    form->addRow(chamferLabel_, chamfer_);
    // A PIN: a socket or not, and its number in the pinout.
    female_ = new QCheckBox(QStringLiteral("A socket (female) - a hole in what it is on"), panel_);
    female_->setObjectName(QStringLiteral("part3dFemale"));
    form->addRow(QString(), female_);
    number_ = new QSpinBox(panel_);
    number_->setObjectName(QStringLiteral("part3dPinNumber"));
    number_->setRange(0, 999);
    numberLabel_ = new QLabel(QStringLiteral("Pin number"), panel_);
    form->addRow(numberLabel_, number_);
    // A SKETCH: given depth both ways; a revolved one, the axis it turns round; and drawn again in the 2D sketch.
    both_ = new QCheckBox(QStringLiteral("Both ways - half each side of its plane"), panel_);
    both_->setObjectName(QStringLiteral("part3dBoth"));
    form->addRow(QString(), both_);
    axis_ = new QComboBox(panel_);
    axis_->setObjectName(QStringLiteral("part3dAxis"));
    axis_->addItems({QStringLiteral("Its plane's first axis"), QStringLiteral("Its plane's second axis")});
    axisLabel_ = new QLabel(QStringLiteral("Turned round"), panel_);
    form->addRow(axisLabel_, axis_);
    editSketch_ = new QPushButton(QStringLiteral("Edit sketch..."), panel_);
    editSketch_->setObjectName(QStringLiteral("part3dEditSketch"));
    editSketch_->setIcon(icon3D(QStringLiteral("sketch"), Chrome::ink()));
    editSketch_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    connect(editSketch_, &QPushButton::clicked, this, [this] { editSelectedSketch(); });
    form->addRow(QString(), editSketch_);
    // COMBINED WITH ANOTHER: how, and with which of the others.
    op_ = new QComboBox(panel_);
    op_->setObjectName(QStringLiteral("part3dOp"));
    op_->addItem(QStringLiteral("A solid of its own"), static_cast<int>(BodyOp::None));
    op_->addItem(QStringLiteral("Added to"), static_cast<int>(BodyOp::Add));
    op_->addItem(QStringLiteral("Subtracted from"), static_cast<int>(BodyOp::Subtract));
    op_->addItem(QStringLiteral("Intersected with"), static_cast<int>(BodyOp::Intersect));
    form->addRow(QStringLiteral("Combined"), op_);
    target_ = new QComboBox(panel_);
    target_->setObjectName(QStringLiteral("part3dTarget"));
    targetLabel_ = new QLabel(QStringLiteral("With"), panel_);
    form->addRow(targetLabel_, target_);
    connect(both_, &QCheckBox::toggled, this, [this] { fromPanel(); });
    connect(axis_, &QComboBox::currentIndexChanged, this, [this] { fromPanel(); });
    connect(op_, &QComboBox::currentIndexChanged, this, [this] { fromPanel(); });
    connect(target_, &QComboBox::currentIndexChanged, this, [this] { fromPanel(); });
    connect(fillet_, &QDoubleSpinBox::valueChanged, this, [this] { fromPanel(); });
    connect(chamfer_, &QDoubleSpinBox::valueChanged, this, [this] { fromPanel(); });
    connect(female_, &QCheckBox::toggled, this, [this] { fromPanel(); });
    connect(number_, &QSpinBox::valueChanged, this, [this] { fromPanel(); });
    pf->addLayout(form);
    pf->addWidget(new QLabel(QStringLiteral("<i>Ctrl + drag in the view (or any drag, the view locked) moves the one "
                                            "selected across it; a click selects. The wheel's middle: its arrows.</i>"),
                             panel_));
    pf->addStretch(1);
    scroll->setWidget(panel_);
    split->addWidget(scroll);
    split->setStretchFactor(1, 1);
    split->setSizes({220, 860, 320});
    lay->addWidget(split, 1);

    said_ = new QLabel(this);
    said_->setObjectName(QStringLiteral("part3dSaid"));
    said_->setWordWrap(true);
    lay->addWidget(said_);
    auto* bottom = new QHBoxLayout;
    auto* exportBtn = new QPushButton(QStringLiteral("Export STL / OBJ..."), this);
    exportBtn->setIcon(icon3D(QStringLiteral("export"), Chrome::ink()));
    connect(exportBtn, &QPushButton::clicked, this, [this] { exportModel(); });
    bottom->addWidget(exportBtn);
    bottom->addStretch(1);
    auto* save = new QPushButton(QStringLiteral("Save to the part"), this);
    save->setIcon(icon3D(QStringLiteral("save"), Chrome::ink()));
    save->setObjectName(QStringLiteral("part3dSave"));
    save->setDefault(true);
    connect(save, &QPushButton::clicked, this, [this] {
        emit saveRequested();
        accept();
    });
    bottom->addWidget(save);
    auto* close = new QPushButton(QStringLiteral("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    bottom->addWidget(close);
    lay->addLayout(bottom);

    // ---- THE PANEL INTO THE BODY ----
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!filling_) select(row);
    });
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        if (filling_) return;
        const int row = list_->row(it);
        if (row < 0 || row >= static_cast<int>(model_.bodies.size())) return;
        remember();
        model_.bodies[static_cast<size_t>(row)].shown = it->checkState() == Qt::Checked;
        changed();
    });
    auto edited = [this] { fromPanel(); };
    connect(name_, &QLineEdit::editingFinished, this, edited);
    for (int i = 0; i < 3; ++i)
        for (QDoubleSpinBox* s : {size_[i], at_[i], turn_[i], scale_[i]}) connect(s, &QDoubleSpinBox::valueChanged, this, edited);
    connect(material_, &QComboBox::currentIndexChanged, this, edited);
    connect(cut_, &QCheckBox::toggled, this, edited);
    connect(shown_, &QCheckBox::toggled, this, edited);
    connect(color_, &QPushButton::clicked, this, [this] {
        if (selected_ < 0) return;
        const QColor c = QColorDialog::getColor(model_.bodies[static_cast<size_t>(selected_)].color, this, QStringLiteral("Colour"));
        if (!c.isValid()) return;
        remember();
        model_.bodies[static_cast<size_t>(selected_)].color = c;
        changed();
    });

    // A NEW MODEL starts from the part's drawing, turned - what the 3D view showed it as.
    if (model_.bodies.empty() && !part_.mechanical.isEmpty()) {
        addBody(BodyShape::Turned);
        undo_.clear();
    }
    rebuild(true);
    fillList();
    select(model_.bodies.empty() ? -1 : 0);
}

bool Part3DEditorDialog::autoCreateFromDrawings() {
    Body3D housing;
    housing.id = QStringLiteral("auto-housing"); housing.name = QStringLiteral("Housing from pinout / mechanical outline");
    housing.shape = BodyShape::Extruded; housing.made = partDrawingSolid(part_, false);
    V3 lo, hi;
    if (!housing.made.bounds(lo, hi)) {
        said_->setText(QStringLiteral("Add a mechanical outline to the part first. A closed pinout outline defines the cross-section."));
        return false;
    }
    housing.color = QColor(150, 158, 170); housing.texture = Texture3D::Plastic;
    PartModel3D made; made.bodies.push_back(housing);
    Body3D contacts;
    contacts.id = QStringLiteral("auto-contacts"); contacts.name = QStringLiteral("Pins and sockets from pinout");
    contacts.shape = BodyShape::Pins; contacts.texture = Texture3D::Metal;
    const double faceDiameter = std::max(0.2, std::min(hi.y - lo.y, hi.z - lo.z));
    partPinMeshes(part_, faceDiameter, std::max(1.0, faceDiameter * 0.3), contacts.made, contacts.madeSockets);
    contacts.at = {hi.x, (lo.y + hi.y)/2.0, (lo.z + hi.z)/2.0};
    if (!contacts.made.empty() || !contacts.madeSockets.empty()) made.bodies.push_back(contacts);
    remember(); model_ = std::move(made); selected_ = 0;
    changed(); rebuild(true);
    said_->setText(QStringLiteral("Auto-created from the pinout cross-section and mechanical outline. Review, modify, then Save to part."));
    return true;
}

void Part3DEditorDialog::remember() {
    undo_.push_back(model_);
    if (undo_.size() > 200) undo_.erase(undo_.begin());
    redo_.clear();
}

void Part3DEditorDialog::undo() {
    if (undo_.empty()) return;
    redo_.push_back(model_);
    model_ = undo_.back();
    undo_.pop_back();
    if (selected_ >= static_cast<int>(model_.bodies.size())) selected_ = static_cast<int>(model_.bodies.size()) - 1;
    changed();
}

void Part3DEditorDialog::redo() {
    if (redo_.empty()) return;
    undo_.push_back(model_);
    model_ = redo_.back();
    redo_.pop_back();
    changed();
}

void Part3DEditorDialog::addBody(BodyShape shape) {
    Body3D b;
    b.id = model_.freshId();
    b.shape = shape;
    b.name = bodyShapeName(shape);
    bodyDefaultSizes(shape, b.a, b.b, b.c);
    // MADE FROM THE PART: its drawing, turned or given depth; its pins and sockets on a face.
    if (shape == BodyShape::Turned || shape == BodyShape::Extruded) {
        b.made = partDrawingSolid(part_, shape == BodyShape::Turned);
        if (b.made.empty()) {
            said_->setText(QStringLiteral("The part has no mechanical drawing to make it from."));
            return;
        }
        b.color = QColor(150, 158, 170);
    } else if (shape == BodyShape::Pins) {
        // On the face of what there is - its far end, as wide as it is - or on a face 10 across.
        V3 lo, hi;
        double faceD = 10.0, faceX = 0.0;
        if (modelBounds(items_, lo, hi)) {
            faceD = std::min(hi.y - lo.y, hi.z - lo.z);
            faceX = hi.x;
        }
        partPinMeshes(part_, faceD, std::max(1.0, faceD * 0.3), b.made, b.madeSockets);
        if (b.made.empty() && b.madeSockets.empty()) {
            said_->setText(QStringLiteral("The part has no pins to make - it is not a connector, or says no way count."));
            return;
        }
        b.at = {faceX, 0.0, 0.0};
    } else {
        // A simple solid, in the middle of what there is.
        V3 lo, hi;
        if (modelBounds(items_, lo, hi)) b.at = (lo + hi) * 0.5;
    }
    remember();
    model_.bodies.push_back(b);
    selected_ = static_cast<int>(model_.bodies.size()) - 1;
    changed();
}

void Part3DEditorDialog::select(int index) {
    selected_ = index >= 0 && index < static_cast<int>(model_.bodies.size()) ? index : -1;
    if (list_->currentRow() != selected_) {
        filling_ = true;
        list_->setCurrentRow(selected_);
        filling_ = false;
    }
    canvas_->setHighlight(selected_ >= 0 ? QStringList{model_.bodies[static_cast<size_t>(selected_)].id} : QStringList());
    fillPanel();
}

void Part3DEditorDialog::duplicateSelected() {
    if (selected_ < 0) return;
    remember();
    Body3D b = model_.bodies[static_cast<size_t>(selected_)];
    b.id = model_.freshId();
    b.name += QStringLiteral(" (copy)");
    V3 lo, hi;
    const Mesh3D m = bodyMesh(b);
    if (m.bounds(lo, hi)) b.at.y += (hi.y - lo.y) * 1.1;
    model_.bodies.push_back(b);
    selected_ = static_cast<int>(model_.bodies.size()) - 1;
    changed();
}

void Part3DEditorDialog::deleteSelected() {
    if (selected_ < 0) return;
    remember();
    model_.bodies.erase(model_.bodies.begin() + selected_);
    selected_ = std::min(selected_, static_cast<int>(model_.bodies.size()) - 1);
    changed();
}

void Part3DEditorDialog::mirrorSelected(int axis) {
    if (selected_ < 0) return;
    remember();
    Body3D& b = model_.bodies[static_cast<size_t>(selected_)];
    // Across the plane through the middle: where it is, and its own shape, the other way.
    if (axis == 0) {
        b.at.x = -b.at.x;
        b.scale.x = -b.scale.x;
        b.turnDeg.y = -b.turnDeg.y;
        b.turnDeg.z = -b.turnDeg.z;
    } else if (axis == 1) {
        b.at.y = -b.at.y;
        b.scale.y = -b.scale.y;
        b.turnDeg.x = -b.turnDeg.x;
        b.turnDeg.z = -b.turnDeg.z;
    } else {
        b.at.z = -b.at.z;
        b.scale.z = -b.scale.z;
        b.turnDeg.x = -b.turnDeg.x;
        b.turnDeg.y = -b.turnDeg.y;
    }
    changed();
}

void Part3DEditorDialog::turnSelected(int axis, double degrees) {
    if (selected_ < 0) return;
    remember();
    Body3D& b = model_.bodies[static_cast<size_t>(selected_)];
    double& t = axis == 0 ? b.turnDeg.x : axis == 1 ? b.turnDeg.y : b.turnDeg.z;
    t = std::fmod(t + degrees, 360.0);
    changed();
}

void Part3DEditorDialog::moveSelected(const V3& by) {
    if (selected_ < 0) return;
    remember();
    model_.bodies[static_cast<size_t>(selected_)].at = model_.bodies[static_cast<size_t>(selected_)].at + by;
    changed();
}

void Part3DEditorDialog::arraySelected(int count, const V3& step, int turnAxis, double turnStep) {
    if (selected_ < 0 || count < 2) return;
    remember();
    const Body3D first = model_.bodies[static_cast<size_t>(selected_)];
    for (int k = 1; k < count; ++k) {
        Body3D b = first;
        b.id = model_.freshId();
        b.name = QStringLiteral("%1 %2").arg(first.name).arg(k + 1);
        b.at = first.at + step * k;
        // ROUND AN AXIS: where it is turned round the model's axis through the origin, and itself turned with it.
        if (turnAxis >= 0) {
            const double a = turnStep * k * M_PI / 180.0, ca = std::cos(a), sa = std::sin(a);
            V3& p = b.at;
            if (turnAxis == 0) p = {p.x, p.y * ca - p.z * sa, p.y * sa + p.z * ca};
            if (turnAxis == 1) p = {p.x * ca + p.z * sa, p.y, -p.x * sa + p.z * ca};
            if (turnAxis == 2) p = {p.x * ca - p.y * sa, p.x * sa + p.y * ca, p.z};
            double& t = turnAxis == 0 ? b.turnDeg.x : turnAxis == 1 ? b.turnDeg.y : b.turnDeg.z;
            t += turnStep * k;
        }
        model_.bodies.push_back(b);
    }
    changed();
}

void Part3DEditorDialog::centreSelected() {
    if (selected_ < 0) return;
    remember();
    model_.bodies[static_cast<size_t>(selected_)].at = {0, 0, 0};
    changed();
}

void Part3DEditorDialog::setSelectedCut(bool on) {
    if (selected_ < 0) return;
    remember();
    model_.bodies[static_cast<size_t>(selected_)].cut = on;
    changed();
}

void Part3DEditorDialog::changed() {
    rebuild(false);
    fillList();
    select(selected_);
}

void Part3DEditorDialog::rebuild(bool fit) {
    QString note;
    items_ = modelItems(model_, &note);
    scene_ = Harness3DScene();
    pickMeshes_.clear();
    pickBody_.clear();
    for (const ModelItem3D& it : items_) {
        Harness3DItem h;
        h.mesh = it.mesh;
        h.color = it.alpha < 0.99 ? QColor(230, 90, 90) : it.color;
        h.texture = it.texture;
        h.brick = it.body;
        h.what = it.body;
        scene_.items.push_back(std::move(h));
    }
    for (const Harness3DItem& h : scene_.items) {
        pickMeshes_.push_back(&h.mesh);
        int body = -1;
        for (size_t i = 0; i < model_.bodies.size(); ++i)
            if (model_.bodies[i].id == h.brick) body = static_cast<int>(i);
        pickBody_.push_back(body);
    }
    pickIndex_.build(pickMeshes_);
    // THE SKETCH BEING DRAWN: its outline and its points, on its plane.
    if (sketchPlane_ >= 0 && !sketch_.empty()) {
        V3 u, v, w;
        sketchAxes(sketchPlane_, u, v, w);
        V3 lo, hi;
        const double size = modelBounds(items_, lo, hi) ? std::max(1.0, (hi - lo).length()) : 20.0;
        std::vector<V3> path;
        for (const auto& [pu, pv] : sketch_) path.push_back(u * pu + v * pv + w * sketchAt_);
        if (path.size() >= 3) path.push_back(path.front());
        Harness3DItem line;
        if (path.size() >= 2) line.mesh = tubeMesh(path, size * 0.004, 8, false);
        for (const V3& p : path) line.mesh.append(boxMesh(p - V3{1, 1, 1} * (size * 0.008), p + V3{1, 1, 1} * (size * 0.008)));
        line.color = QColor(80, 220, 255);
        line.what = QStringLiteral("sketch");
        scene_.items.push_back(std::move(line));
    }
    canvas_->setScene(&scene_);
    if (fit) canvas_->fit();
    canvas_->update();
    V3 lo, hi;
    const bool any = modelBounds(items_, lo, hi);
    said_->setText(any ? QStringLiteral("%1 solid(s); %2 x %3 x %4 mm overall.%5")
                             .arg(model_.bodies.size())
                             .arg(hi.x - lo.x, 0, 'f', 1)
                             .arg(hi.y - lo.y, 0, 'f', 1)
                             .arg(hi.z - lo.z, 0, 'f', 1)
                             .arg(note.isEmpty() ? QString() : QStringLiteral("  ") + note)
                       : QStringLiteral("Nothing yet - add a solid, or make it from the part's drawing."));
    if (wheel_) wheel_->move(canvas_->width() - wheel_->width() - 8, 8);
}

void Part3DEditorDialog::fillList() {
    filling_ = true;
    list_->clear();
    for (const Body3D& b : model_.bodies) {
        auto* it = new QListWidgetItem(b.cut ? QStringLiteral("%1  (cut)").arg(b.name) : b.name, list_);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(b.shown ? Qt::Checked : Qt::Unchecked);
    }
    list_->setCurrentRow(selected_);
    filling_ = false;
}

void Part3DEditorDialog::fillPanel() {
    filling_ = true;
    const bool any = selected_ >= 0;
    panel_->setEnabled(any);
    if (any) {
        const Body3D& b = model_.bodies[static_cast<size_t>(selected_)];
        shapeSaid_->setText(QStringLiteral("<b>%1</b>").arg(bodyShapeName(b.shape)));
        name_->setText(b.name);
        const QStringList labels = bodySizeLabels(b.shape);
        const double sizes[3] = {b.a, b.b, b.c};
        // A SKETCH'S DEPTH may go either way from its plane; a revolved one's is an angle; every other size is more than
        // nothing.
        if (b.shape == BodyShape::Revolved) {
            size_[0]->setRange(0.5, 360.0);
            size_[0]->setSuffix(QStringLiteral("°"));
        } else {
            size_[0]->setRange(b.shape == BodyShape::Sketch ? -10000.0 : 0.0, 10000.0);
            size_[0]->setSuffix(QStringLiteral(" mm"));
        }
        for (int i = 0; i < 3; ++i) {
            sizeLabel_[i]->setText(labels.value(i));
            sizeLabel_[i]->setVisible(!labels.value(i).isEmpty());
            size_[i]->setVisible(!labels.value(i).isEmpty());
            size_[i]->setValue(sizes[i]);
        }
        const double ats[3] = {b.at.x, b.at.y, b.at.z}, turns[3] = {b.turnDeg.x, b.turnDeg.y, b.turnDeg.z},
                     scales[3] = {b.scale.x, b.scale.y, b.scale.z};
        for (int i = 0; i < 3; ++i) {
            at_[i]->setValue(ats[i]);
            turn_[i]->setValue(turns[i]);
            scale_[i]->setValue(scales[i]);
        }
        color_->setText(b.color.name());
        color_->setStyleSheet(QStringLiteral("QPushButton{background:%1;color:%2;}")
                                  .arg(b.color.name(), b.color.lightness() > 128 ? QStringLiteral("#000") : QStringLiteral("#fff")));
        material_->setCurrentIndex(std::max(0, material_->findData(b.texture)));
        cut_->setChecked(b.cut);
        cut_->setEnabled(bodyIsSolid(b.shape) && b.shape != BodyShape::Pin);
        both_->setVisible(b.shape == BodyShape::Sketch);
        both_->setChecked(b.both);
        axis_->setVisible(b.shape == BodyShape::Revolved);
        axisLabel_->setVisible(b.shape == BodyShape::Revolved);
        axis_->setCurrentIndex(b.axis);
        editSketch_->setVisible(bodyIsSketch(b.shape));
        // WHAT IT CAN BE COMBINED WITH: every other solid not made from it.
        const bool solid = bodyIsSolid(b.shape) && b.shape != BodyShape::Pin;
        op_->setEnabled(solid);
        op_->setCurrentIndex(std::max(0, op_->findData(static_cast<int>(b.op))));
        target_->clear();
        for (size_t i = 0; i < model_.bodies.size(); ++i)
            if (model_.canCombine(selected_, static_cast<int>(i))) target_->addItem(model_.bodies[i].name, model_.bodies[i].id);
        target_->setCurrentIndex(target_->findData(b.target));
        target_->setVisible(b.op != BodyOp::None);
        targetLabel_->setVisible(b.op != BodyOp::None);
        shown_->setChecked(b.shown);
        const bool edges = bodyIsPrimitive(b.shape) && b.shape != BodyShape::Pin;
        for (QWidget* w : {static_cast<QWidget*>(fillet_), static_cast<QWidget*>(filletLabel_), static_cast<QWidget*>(chamfer_),
                           static_cast<QWidget*>(chamferLabel_)})
            w->setVisible(edges);
        fillet_->setValue(b.fillet);
        chamfer_->setValue(b.chamfer);
        const bool pin = b.shape == BodyShape::Pin;
        for (QWidget* w : {static_cast<QWidget*>(female_), static_cast<QWidget*>(number_), static_cast<QWidget*>(numberLabel_)}) w->setVisible(pin);
        female_->setChecked(b.female);
        number_->setValue(b.number);
    } else {
        shapeSaid_->setText(QStringLiteral("<i>Nothing selected - click a solid, or choose one in the list.</i>"));
    }
    filling_ = false;
}

void Part3DEditorDialog::fromPanel() {
    if (filling_ || selected_ < 0) return;
    remember();
    Body3D& b = model_.bodies[static_cast<size_t>(selected_)];
    b.name = name_->text().trimmed().isEmpty() ? bodyShapeName(b.shape) : name_->text().trimmed();
    b.a = size_[0]->value();
    b.b = size_[1]->value();
    b.c = size_[2]->value();
    b.at = {at_[0]->value(), at_[1]->value(), at_[2]->value()};
    b.turnDeg = {turn_[0]->value(), turn_[1]->value(), turn_[2]->value()};
    // A scale of nothing is no scale: it keeps the last that was something.
    auto keep = [](double v, double was) { return std::abs(v) < 1e-6 ? was : v; };
    b.scale = {keep(scale_[0]->value(), b.scale.x), keep(scale_[1]->value(), b.scale.y), keep(scale_[2]->value(), b.scale.z)};
    b.texture = material_->currentData().toInt();
    b.cut = cut_->isChecked() && bodyIsSolid(b.shape);
    b.shown = shown_->isChecked();
    b.fillet = fillet_->value();
    b.chamfer = chamfer_->value();
    b.both = both_->isChecked();
    b.axis = axis_->currentIndex();
    // COMBINED: with the one chosen - or, newly combined, the first it can be.
    const BodyOp op = static_cast<BodyOp>(op_->currentData().toInt());
    QString target = target_->currentData().toString();
    if (op != BodyOp::None && target.isEmpty() && target_->count() > 0) target = target_->itemData(0).toString();
    if (op != BodyOp::None && model_.canCombine(selected_, model_.indexOf(target))) {
        b.op = op;
        b.target = target;
        b.cut = false;
    } else {
        b.op = BodyOp::None;
        b.target.clear();
    }
    if (b.shape == BodyShape::Pin) {
        // A SOCKET IS A HOLE: cut out of what it is on, dark; a pin gold, standing out.
        if (b.female != female_->isChecked()) b.color = female_->isChecked() ? QColor(30, 30, 34) : QColor(212, 175, 80);
        b.female = female_->isChecked();
        b.cut = b.female;
        b.number = number_->value();
        b.name = QStringLiteral("%1 %2").arg(b.female ? QStringLiteral("Socket") : QStringLiteral("Pin")).arg(b.number);
    }
    changed();
}

bool Part3DEditorDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched != canvas_) return QDialog::eventFilter(watched, event);
    if (event->type() == QEvent::Resize && wheel_) wheel_->move(canvas_->width() - wheel_->width() - 8, 8);
    if (event->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(event);
        if (k->key() == Qt::Key_Delete) {
            deleteSelected();
            return true;
        }
        if (k->matches(QKeySequence::Undo)) {
            undo();
            return true;
        }
        if (k->matches(QKeySequence::Redo)) {
            redo();
            return true;
        }
    }
    auto* m = dynamic_cast<QMouseEvent*>(event);
    if (!m) return false;
    // SKETCHING, OR PUTTING PINS: a click (not a drag) is a point of the outline, or where a pin goes.
    if ((sketchPlane_ >= 0 || pinTool_ > 0) && m->button() == Qt::LeftButton && !(m->modifiers() & Qt::ControlModifier)) {
        if (event->type() == QEvent::MouseButtonPress) dragLast_ = m->position().toPoint();
        if (event->type() == QEvent::MouseButtonRelease && (m->position().toPoint() - dragLast_).manhattanLength() <= 3) {
            V3 origin, dir;
            canvas_->camera().rayAt(m->position().x(), m->position().y(), canvas_->width(), canvas_->height(), origin, dir);
            if (sketchPlane_ >= 0) {
                V3 u, v, w;
                sketchAxes(sketchPlane_, u, v, w);
                const double along = dot3(dir, w);
                if (std::abs(along) > 1e-9) {
                    const double t = (sketchAt_ - dot3(origin, w)) / along;
                    const V3 p = origin + dir * t;
                    // On a half-millimetre grid.
                    sketchClick(std::round(dot3(p, u) * 2.0) / 2.0, std::round(dot3(p, v) * 2.0) / 2.0);
                }
            } else {
                const Pick3D pick = pickIndex_.cast(origin, dir);
                if (pick.hit) placePinAt(pick.point, pick.normal, pinTool_ == 2);
            }
            return true;
        }
    }
    // SKETCH ON A FACE: the face clicked, its plane the sketch's - and the solid it is a face of, what it goes with.
    if (faceSketch_ && m->button() == Qt::LeftButton) {
        if (event->type() == QEvent::MouseButtonPress) dragLast_ = m->position().toPoint();
        if (event->type() == QEvent::MouseButtonRelease && (m->position().toPoint() - dragLast_).manhattanLength() <= 3) {
            V3 origin, dir;
            canvas_->camera().rayAt(m->position().x(), m->position().y(), canvas_->width(), canvas_->height(), origin, dir);
            const Pick3D pick = pickIndex_.cast(origin, dir);
            if (!pick.hit || pick.normal.length() < 0.5) {
                said_->setText(QStringLiteral("Click a face of a solid to sketch on it."));
                return true;
            }
            const int body = pick.part >= 0 && pick.part < static_cast<int>(pickBody_.size()) ? pickBody_[static_cast<size_t>(pick.part)] : -1;
            setFaceSketchTool(false);
            select(body);
            const QString target = body >= 0 ? model_.bodies[static_cast<size_t>(body)].id : QString();
            Sketch2DDialog d(items_, body >= 0 ? model_.bodies[static_cast<size_t>(body)].name : QString(), this);
            d.setFace(pick.point, pick.normal);
            if (d.exec() == QDialog::Accepted) addSketchResult(d.result(), target);
            return true;
        }
    }
    // CTRL AND A DRAG - or a drag, the view locked: the one selected moved across the view, at its own depth.
    if (event->type() == QEvent::MouseButtonPress && m->button() == Qt::LeftButton &&
        ((m->modifiers() & Qt::ControlModifier) || canvas_->viewLocked()) && selected_ >= 0) {
        remember();
        dragging_ = true;
        dragLast_ = m->position().toPoint();
        V3 right, up, forward;
        canvas_->camera().axes(right, up, forward);
        const V3 at = model_.bodies[static_cast<size_t>(selected_)].at;
        const double z = std::max(canvas_->camera().nearPlane(), dot3(at - canvas_->camera().eye(), forward));
        dragPerPixel_ = 2.0 * z * std::tan(canvas_->camera().fovDeg * M_PI / 360.0) / std::max(1, canvas_->height());
        return true;
    }
    if (dragging_ && event->type() == QEvent::MouseMove) {
        const QPoint d = m->position().toPoint() - dragLast_;
        dragLast_ = m->position().toPoint();
        V3 right, up, forward;
        canvas_->camera().axes(right, up, forward);
        Body3D& b = model_.bodies[static_cast<size_t>(selected_)];
        b.at = b.at + right * (d.x() * dragPerPixel_) - up * (d.y() * dragPerPixel_);
        rebuild(false);
        fillPanel();
        return true;
    }
    if (dragging_ && event->type() == QEvent::MouseButtonRelease) {
        dragging_ = false;
        return true;
    }
    // A CLICK: the solid under it selected.
    if (event->type() == QEvent::MouseButtonPress && m->button() == Qt::LeftButton) dragLast_ = m->position().toPoint();
    if (event->type() == QEvent::MouseButtonRelease && m->button() == Qt::LeftButton &&
        (m->position().toPoint() - dragLast_).manhattanLength() <= 3) {
        V3 origin, dir;
        canvas_->camera().rayAt(m->position().x(), m->position().y(), canvas_->width(), canvas_->height(), origin, dir);
        const Pick3D pick = pickIndex_.cast(origin, dir);
        select(pick.hit && pick.part >= 0 && pick.part < static_cast<int>(pickBody_.size()) ? pickBody_[static_cast<size_t>(pick.part)] : -1);
    }
    return false;
}

// ---- Sketching ----------------------------------------------------------------------------------------------

void Part3DEditorDialog::startSketch(int plane) {
    plane = std::clamp(plane, 0, 2);
    setPinTool(0);
    sketchPlane_ = plane;
    sketch_.clear();
    sketchFirst_.reset();
    {
        const QSignalBlocker hold(sketchPlaneBox_);
        sketchPlaneBox_->setCurrentIndex(plane);
    }
    sketchBar_->show();
    // LOOKED AT SQUARE ON, the plane facing the one drawing.
    canvas_->viewFrom(plane == 0 ? QStringLiteral("top") : plane == 1 ? QStringLiteral("front") : QStringLiteral("side"));
    said_->setText(QStringLiteral("Sketching: click the outline's points on the plane (half-millimetre grid), then Extrude."));
    rebuild(false);
}

void Part3DEditorDialog::sketchClick(double u, double v) {
    if (sketchPlane_ < 0) return;
    const int shape = sketchShape_->currentIndex();
    if (shape == 0) {
        sketch_.emplace_back(u, v);
    } else if (!sketchFirst_) {
        sketchFirst_ = std::make_pair(u, v);
        sketch_ = {{u, v}};
    } else {
        const auto [u0, v0] = *sketchFirst_;
        sketch_.clear();
        if (shape == 1) {
            sketch_ = {{u0, v0}, {u, v0}, {u, v}, {u0, v}};
        } else {
            // A CIRCLE through the point clicked, round the first.
            const double r = std::hypot(u - u0, v - v0);
            for (int k = 0; k < 48; ++k) sketch_.emplace_back(u0 + r * std::cos(k * M_PI / 24.0), v0 + r * std::sin(k * M_PI / 24.0));
        }
        sketchFirst_.reset();
    }
    rebuild(false);
}

bool Part3DEditorDialog::finishSketch(double depth) {
    if (sketchPlane_ < 0 || sketch_.size() < 3) {
        said_->setText(QStringLiteral("An outline needs three points at least."));
        return false;
    }
    Body3D b;
    b.id = model_.freshId();
    b.shape = BodyShape::Sketch;
    b.name = QStringLiteral("Sketch, extruded");
    b.a = std::abs(depth) < 0.05 ? 0.05 : depth;
    b.plane = sketchPlane_;
    b.profile = sketch_;
    V3 u, v, w;
    sketchAxes(sketchPlane_, u, v, w);
    b.at = w * sketchAt_;
    remember();
    model_.bodies.push_back(b);
    selected_ = static_cast<int>(model_.bodies.size()) - 1;
    sketchPlane_ = -1;
    sketch_.clear();
    sketchBar_->hide();
    changed();
    return true;
}

void Part3DEditorDialog::cancelSketch() {
    sketchPlane_ = -1;
    sketch_.clear();
    sketchFirst_.reset();
    sketchBar_->hide();
    rebuild(false);
}

// ---- Pins put by hand ---------------------------------------------------------------------------------------

void Part3DEditorDialog::setPinTool(int tool) {
    pinTool_ = tool;
    if (tool > 0 && sketchPlane_ >= 0) cancelSketch();
    for (int i = 0; i < 2; ++i)
        if (pinButtons_[i]) {
            const QSignalBlocker hold(pinButtons_[i]);
            pinButtons_[i]->setChecked(tool == i + 1);
        }
    canvas_->setCursor(tool > 0 ? Qt::CrossCursor : Qt::ArrowCursor);
    if (tool > 0) said_->setText(tool == 1 ? QStringLiteral("Click a face to put a pin on it.") : QStringLiteral("Click a face to put a socket in it."));
}

bool Part3DEditorDialog::placePinAt(const V3& point, const V3& normal, bool female) {
    if (normal.length() < 0.5) return false;
    // AS BIG AS THE MODEL WANTS: a fifteenth of its size across, four times that long - or 1 by 4.
    double dia = 1.0;
    V3 lo, hi;
    if (modelBounds(items_, lo, hi)) dia = std::clamp((hi - lo).length() / 25.0, 0.3, 3.0);
    int most = 0;
    for (const Body3D& o : model_.bodies)
        if (o.shape == BodyShape::Pin) most = std::max(most, o.number);
    Body3D b;
    b.id = model_.freshId();
    b.shape = BodyShape::Pin;
    b.a = dia;
    b.b = dia * 4.0;
    b.female = female;
    b.cut = female;
    b.number = most + 1;
    b.name = QStringLiteral("%1 %2").arg(female ? QStringLiteral("Socket") : QStringLiteral("Pin")).arg(b.number);
    b.color = female ? QColor(30, 30, 34) : QColor(212, 175, 80);
    b.at = point;
    b.turnDeg = turnAlong(normal);
    remember();
    model_.bodies.push_back(b);
    selected_ = static_cast<int>(model_.bodies.size()) - 1;
    changed();
    return true;
}

void Part3DEditorDialog::renumberPins() {
    remember();
    int n = 0;
    for (Body3D& b : model_.bodies)
        if (b.shape == BodyShape::Pin) {
            b.number = ++n;
            b.name = QStringLiteral("%1 %2").arg(b.female ? QStringLiteral("Socket") : QStringLiteral("Pin")).arg(b.number);
        }
    changed();
}

// ---- The 2D sketch ----------------------------------------------------------------------------------------------

void Part3DEditorDialog::openSketch2D(int plane) {
    setPinTool(0);
    setFaceSketchTool(false);
    // WITH THE ONE SELECTED, if it is a solid: what is made can be added to it, cut out of it, or kept where they meet.
    const bool with = selected_ >= 0 && bodyIsSolid(model_.bodies[static_cast<size_t>(selected_)].shape) &&
                      model_.bodies[static_cast<size_t>(selected_)].shape != BodyShape::Pin;
    const QString target = with ? model_.bodies[static_cast<size_t>(selected_)].id : QString();
    Sketch2DDialog d(items_, with ? model_.bodies[static_cast<size_t>(selected_)].name : QString(), this);
    d.setPlane(plane, 0.0);
    if (d.exec() == QDialog::Accepted) addSketchResult(d.result(), target);
}

void Part3DEditorDialog::setFaceSketchTool(bool on) {
    if (on) {
        setPinTool(0);
        if (sketchPlane_ >= 0) cancelSketch();
    }
    faceSketch_ = on;
    if (faceSketchButton_) {
        const QSignalBlocker hold(faceSketchButton_);
        faceSketchButton_->setChecked(on);
    }
    canvas_->setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    if (on) said_->setText(QStringLiteral("Click a face of a solid to sketch on its plane."));
}

Sketch2DDialog::Result Part3DEditorDialog::sketchOf(const Body3D& b) {
    Sketch2DDialog::Result r;
    r.plane = b.plane;
    r.at = b.at;
    r.turnDeg = b.turnDeg;
    r.loops.push_back(b.profile);
    for (const SketchLoop& h : b.holes) r.loops.push_back(h);
    r.revolve = b.shape == BodyShape::Revolved;
    if (r.revolve) r.angle = b.a;
    else r.depth = b.a;
    r.both = b.both;
    r.axis = b.axis;
    r.op = b.op;
    r.cutAll = b.cut;
    return r;
}

void Part3DEditorDialog::editSelectedSketch() {
    if (selected_ < 0 || !bodyIsSketch(model_.bodies[static_cast<size_t>(selected_)].shape)) return;
    const Body3D& b = model_.bodies[static_cast<size_t>(selected_)];
    const int t = model_.indexOf(b.target);
    // THE MODEL WITHOUT IT, under the sketch.
    PartModel3D rest = model_;
    rest.bodies.erase(rest.bodies.begin() + selected_);
    Sketch2DDialog d(modelItems(rest), t >= 0 ? model_.bodies[static_cast<size_t>(t)].name : QString(), this);
    d.setStart(sketchOf(b));
    if (d.exec() == QDialog::Accepted) addSketchResult(d.result(), b.target, selected_);
}

bool Part3DEditorDialog::addSketchResult(const Sketch2DDialog::Result& r, const QString& target, int replace) {
    const std::vector<SketchRegion> regions = sketchRegions(r.loops);
    if (regions.empty()) {
        said_->setText(QStringLiteral("The sketch has no closed outline to make a solid of."));
        return false;
    }
    remember();
    int first = -1;
    for (size_t k = 0; k < regions.size(); ++k) {
        Body3D b;
        const bool again = k == 0 && replace >= 0 && replace < static_cast<int>(model_.bodies.size());
        if (again) {
            b = model_.bodies[static_cast<size_t>(replace)];
        } else {
            b.id = model_.freshId();
            b.color = QColor(150, 158, 170);
            b.texture = Texture3D::Plastic;
        }
        b.shape = r.revolve ? BodyShape::Revolved : BodyShape::Sketch;
        if (!again) b.name = regions.size() > 1 ? QStringLiteral("%1 %2").arg(bodyShapeName(b.shape)).arg(k + 1) : bodyShapeName(b.shape);
        b.a = r.revolve ? std::clamp(r.angle, 0.5, 360.0) : (std::abs(r.depth) < 0.05 ? 0.05 : r.depth);
        b.both = !r.revolve && r.both;
        b.axis = r.axis;
        b.plane = r.plane;
        b.profile = regions[k].outer;
        b.holes = regions[k].holes;
        b.at = r.at;
        b.turnDeg = r.turnDeg;
        b.scale = {1, 1, 1};
        b.cut = r.cutAll;
        b.op = BodyOp::None;
        b.target.clear();
        if (!r.cutAll && r.op != BodyOp::None && !target.isEmpty()) {
            b.op = r.op;
            b.target = target;
        }
        if (b.cut || b.op == BodyOp::Subtract) b.color = QColor(230, 90, 90);
        if (again) {
            model_.bodies[static_cast<size_t>(replace)] = b;
            first = replace;
        } else {
            model_.bodies.push_back(b);
            if (first < 0) first = static_cast<int>(model_.bodies.size()) - 1;
        }
        // NOT MADE FROM ITSELF: one that would be, a solid of its own.
        const int at = again ? replace : static_cast<int>(model_.bodies.size()) - 1;
        Body3D& made = model_.bodies[static_cast<size_t>(at)];
        if (made.op != BodyOp::None && !model_.canCombine(at, model_.indexOf(made.target))) {
            made.op = BodyOp::None;
            made.target.clear();
        }
    }
    selected_ = first;
    changed();
    return true;
}

// ---- Combining ------------------------------------------------------------------------------------------------

void Part3DEditorDialog::combineMenu(BodyOp op) {
    if (selected_ < 0) {
        said_->setText(QStringLiteral("Select the solid to %1 first.").arg(bodyOpName(op).toLower()));
        return;
    }
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("part3dCombineMenu"));
    for (size_t i = 0; i < model_.bodies.size(); ++i)
        if (model_.canCombine(selected_, static_cast<int>(i))) {
            const int t = static_cast<int>(i);
            QAction* a = menu.addAction(model_.bodies[i].name);
            connect(a, &QAction::triggered, this, [this, op, t] { combineSelected(op, t); });
        }
    if (menu.isEmpty()) {
        said_->setText(QStringLiteral("There is no other solid to %1 it with.").arg(bodyOpName(op).toLower()));
        return;
    }
    menu.exec(QCursor::pos());
}

bool Part3DEditorDialog::combineSelected(BodyOp op, int target) {
    if (op == BodyOp::None) {
        separateSelected();
        return true;
    }
    if (!model_.canCombine(selected_, target)) {
        said_->setText(QStringLiteral("It cannot be combined with that - not a solid, or made from it."));
        return false;
    }
    remember();
    Body3D& b = model_.bodies[static_cast<size_t>(selected_)];
    b.op = op;
    b.target = model_.bodies[static_cast<size_t>(target)].id;
    b.cut = false;
    said_->setText(QStringLiteral("%1 %2 %3.")
                       .arg(b.name, op == BodyOp::Add ? QStringLiteral("added to") : op == BodyOp::Subtract ? QStringLiteral("subtracted from")
                                                                                                           : QStringLiteral("intersected with"),
                            model_.bodies[static_cast<size_t>(target)].name));
    changed();
    return true;
}

void Part3DEditorDialog::separateSelected() {
    if (selected_ < 0) return;
    Body3D& b = model_.bodies[static_cast<size_t>(selected_)];
    if (b.op == BodyOp::None && !b.cut) return;
    remember();
    b.op = BodyOp::None;
    b.target.clear();
    if (b.shape != BodyShape::Pin) b.cut = false;
    changed();
}

// ---- The wheel ------------------------------------------------------------------------------------------------

void Part3DEditorDialog::setMoveStep(double mm) { moveStep_->setValue(mm); }

void Part3DEditorDialog::setWheelMoving(bool on) {
    if (on == wheelMoving_) return;
    wheelMoving_ = on;
    if (on) {
        buildMoveWheel();
        wheel_->setMiddle(QStringLiteral("Move"), [this](const QPoint&) { setWheelMoving(false); });
    } else {
        wheel_->setItems(viewWheel_);
        wheel_->setMiddle(QStringLiteral("View"), [this](const QPoint&) { setWheelMoving(true); });
    }
    wheel_->update();
}

void Part3DEditorDialog::buildMoveWheel() {
    V3 right, up, forward;
    canvas_->camera().axes(right, up, forward);
    const std::array<std::pair<double, double>, 3> screen{{{right.x, up.x}, {right.y, up.y}, {right.z, up.z}}};
    wheel_->setItems(NavWheel::axisItems(
        screen,
        [this](int axis, int sign) {
            if (selected_ < 0) {
                said_->setText(QStringLiteral("Move: select the solid to move first."));
                return;
            }
            V3 by;
            (axis == 0 ? by.x : axis == 1 ? by.y : by.z) = moveStep_->value() * sign;
            moveSelected(by);
        },
        [this](int axis, int sign) {
            if (selected_ < 0) {
                said_->setText(QStringLiteral("Turn: select the solid to turn first."));
                return;
            }
            turnSelected(axis, 15.0 * sign);
        }));
    wheel_->update();
}

void Part3DEditorDialog::exportModel() {
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export the 3D model"), part_.title(),
                                                      QStringLiteral("STL (*.stl);;OBJ (*.obj)"));
    if (path.isEmpty()) return;
    QString error;
    const bool ok = path.endsWith(QLatin1String(".obj"), Qt::CaseInsensitive) ? exportModelObj(items_, path, &error)
                                                                               : exportModelStl(items_, path, &error);
    if (!ok) QMessageBox::warning(this, QStringLiteral("Export"), error);
    else said_->setText(QStringLiteral("Written to %1.").arg(path));
}

} // namespace Loopwire
