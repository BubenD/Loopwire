#include "app/Part3DEditorDialog.h"

#include "app/Icons3D.h"
#include "app/NavWheel.h"
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
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace Edraw_Desktop {

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
    modRow->addSpacing(12);
    modButton("part3dUndo", QStringLiteral("Undo"), QStringLiteral("Take back the last change (Ctrl+Z)"), [this] { undo(); });
    modButton("part3dRedo", QStringLiteral("Redo"), QStringLiteral("Do it again (Ctrl+Y)"), [this] { redo(); });
    modRow->addSpacing(12);
    for (const auto& [name, key] : {std::pair<const char*, const char*>{"Iso", "iso"}, {"Front", "front"}, {"Top", "top"}, {"Side", "side"}})
        modButton(QStringLiteral("part3dView%1").arg(QLatin1String(name)).toLatin1().constData(), QString::fromLatin1(name),
                  QStringLiteral("View it from there"), [this, k = QString::fromLatin1(key)] { canvas_->viewFrom(k); });
    modButton("part3dFit", QStringLiteral("Fit"), QStringLiteral("All of it in view"), [this] { canvas_->fit(); });
    modRow->addStretch(1);
    lay->addLayout(modRow);
    {
        // THE PICTURES, by what each button does.
        const QColor ink = palette().color(QPalette::WindowText);
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
        };
        for (const auto& i : icons)
            if (auto* b = findChild<QToolButton*>(QLatin1String(i.name))) b->setIcon(icon3D(QLatin1String(i.icon), ink));
    }

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
    // THE WHEEL over the view, as in the 3D view of a harness.
    wheel_ = new NavWheel(canvas_);
    wheel_->setItems({
        {QStringLiteral("up"), QStringLiteral("Tilt up"), QString(), [this] { canvas_->nudge(0, 15, 0); }, {}},
        {QStringLiteral("plus"), QStringLiteral("Zoom in"), QString(), [this] { canvas_->zoomBy(0.8); }, {}},
        {QStringLiteral("right"), QStringLiteral("Turn"), QString(), [this] { canvas_->nudge(-15, 0, 0); }, {}},
        {QStringLiteral("fit"), QStringLiteral("Fit"), QString(), [this] { canvas_->fit(); }, {}},
        {QStringLiteral("down"), QStringLiteral("Tilt down"), QString(), [this] { canvas_->nudge(0, -15, 0); }, {}},
        {QStringLiteral("iso"), QStringLiteral("Iso"), QString(), [this] { canvas_->viewFrom(QStringLiteral("iso")); }, {}},
        {QStringLiteral("left"), QStringLiteral("Turn"), QString(), [this] { canvas_->nudge(15, 0, 0); }, {}},
        {QStringLiteral("minus"), QStringLiteral("Zoom out"), QString(), [this] { canvas_->zoomBy(1.25); }, {}},
    });
    wheel_->setMiddle(QStringLiteral("View"), {});

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
    pf->addLayout(form);
    pf->addWidget(new QLabel(QStringLiteral("<i>Ctrl + drag in the view moves the one selected across it; a click "
                                            "selects.</i>"),
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
    exportBtn->setIcon(icon3D(QStringLiteral("export"), palette().color(QPalette::WindowText)));
    connect(exportBtn, &QPushButton::clicked, this, [this] { exportModel(); });
    bottom->addWidget(exportBtn);
    bottom->addStretch(1);
    auto* save = new QPushButton(QStringLiteral("Save to the part"), this);
    save->setIcon(icon3D(QStringLiteral("save"), palette().color(QPalette::WindowText)));
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
        cut_->setEnabled(bodyIsPrimitive(b.shape));
        shown_->setChecked(b.shown);
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
    b.cut = cut_->isChecked() && bodyIsPrimitive(b.shape);
    b.shown = shown_->isChecked();
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
    // CTRL AND A DRAG: the one selected moved across the view, at its own depth.
    if (event->type() == QEvent::MouseButtonPress && m->button() == Qt::LeftButton && (m->modifiers() & Qt::ControlModifier) &&
        selected_ >= 0) {
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

} // namespace Edraw_Desktop
