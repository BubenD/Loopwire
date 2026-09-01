#include "app/MechWizardDialog.h"

#include "app/DrawingPreview.h"
#include "app/Editor.h"
#include "harness/MechanicalHarness.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

namespace {

// The gauges a harness is actually laid up from, as the drawings write them. Offered as a list with
// free text allowed, because a drawing may say something the list does not.
// THE GAUGES A HARNESS IS LAID UP FROM, IN AWG - which is the size a wire is ordered and crimped by on
// these drawings, and the number a contact's range is quoted in. The mm2 equivalent follows in brackets
// because the two are used side by side: a datasheet gives one, a European cable schedule the other, and
// making somebody convert in their head between the list and the part they are checking it against is
// how the wrong contact gets fitted.
//
// What is STORED is the bare AWG ("20 AWG"), never the bracketed caption: gaugeMm2() has to parse it, and
// the brackets are a reading aid rather than part of the size. See gaugeCanon() / setGauge().
const int kAwgGauges[] = {26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4, 2};

// What the palette says under each category name: what the step IS, in the language of the shop.
QString categoryHint(MechWizardDialog::Category c) {
    using C = MechWizardDialog::Category;
    switch (c) {
    case C::Connector:
        return QStringLiteral("A connector on this end. Its contacts are chosen here too, and checked "
                              "against the conductors they are crimped onto.");
    case C::Terminal:
        return QStringLiteral("A ring, fork or blade terminal. Like a connector, it is where the run "
                              "ends.");
    case C::Backshell:
        return QStringLiteral("A backshell at this point. Sized on what is behind it - the jacket "
                              "where there is one.");
    case C::Boot:
        return QStringLiteral("A heatshrink boot at this point.");
    case C::Label:
        return QStringLiteral("A marker or printed sleeve at this point.");
    case C::Heatshrink:
        return QStringLiteral("Heatshrink over the run that arrives here.");
    case C::Braid:
        return QStringLiteral("Braided screen over the run that arrives here.");
    case C::Wires:
        return QStringLiteral("Loose hook-up wires: a run with no jacket, drawn at the bundle its own "
                              "conductors make.");
    case C::Cable:
        return QStringLiteral("A jacketed cable. Drawn at its jacket, which is what a boot over it has "
                              "to pass - not what its cores add up to.");
    case C::HarnessBody:
        return QStringLiteral("The trunk: a length of harness carrying the conductors declared for it.");
    case C::HarnessBranch:
        return QStringLiteral("A run leaving this point. Add a node first if the harness splits here.");
    case C::NodePoint:
        return QStringLiteral("A breakout: the harness goes several ways from here. Each branch is a "
                              "step that follows this one.");
    }
    return QString();
}

// Declared here, defined with the other combo helpers further down: editStep() needs them and sits
// above them, and moving a whole block of helpers to satisfy an ordering rule reads worse than saying
// once that they exist.
void selectByData(QComboBox* box, const QString& id);
QString partLabelFor(const LibraryPart& p);

// The designators typed for a run's wires, one per wire in the order the conductors are counted.
//
// Commas or spaces, because a person typing five wire numbers will use whichever they habitually use
// and being told off for the wrong separator is not what the field is for. Blank entries are dropped
// rather than kept as unnamed wires: a trailing comma is a typing artefact, not a wire.
QStringList splitDesignators(const QString& text) {
    QStringList out;
    for (const QString& piece : text.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                           Qt::SkipEmptyParts)) {
        const QString one = piece.trimmed();
        if (!one.isEmpty()) out << one;
    }
    return out;
}

// The positions a library part STATES, or 0 when it states none.
//
// THE PIN TABLE COMES FIRST. A connector's "Its own pins" rows say which pins are which size ("1-3",
// "1,2,5"), and that table is the connector as somebody entered it off the datasheet - so the highest
// pin it names is a harder figure than the single "Pins" box, which on a part that was captured for its
// pin table is often left at whatever it started as. Where the table names no pins the box is used.
//
// Zero rather than one where neither says anything, which is the difference that matters: wayCount()
// answers 1 for everything so that a connector always has one contact to order, and 1 is a fine
// fallback for ordering and a bad one for overwriting a figure the user typed.
int declaredWays(const LibraryPart& p) {
    int highest = 0;
    for (const PartPinType& t : partPinTypes(p))
        // No upper bound: the list is being asked HOW MANY ways there are, so it cannot be clamped to
        // an answer that is not known yet. An empty list means "all of them" and names none, which is
        // silence about the count rather than a count of zero.
        for (int pin : pinsNamedBy(t.pins, 0))
            highest = std::max(highest, pin);
    if (highest > 0) return highest;
    for (const QString& key : {QStringLiteral("pins"), QStringLiteral("ways")}) {
        const int n = p.value(key).toInt();
        if (n > 0) return n;
    }
    return 0;
}

} // namespace

// ==================================================================================================
// Categories
// ==================================================================================================

QString MechWizardDialog::categoryName(Category c) {
    switch (c) {
    case Category::Connector:     return QStringLiteral("Connector");
    case Category::Terminal:      return QStringLiteral("Terminal");
    case Category::Backshell:     return QStringLiteral("Backshell");
    case Category::Boot:          return QStringLiteral("Boot");
    case Category::Label:         return QStringLiteral("Label");
    case Category::Heatshrink:    return QStringLiteral("Heatshrink");
    case Category::Braid:         return QStringLiteral("Braided screen");
    case Category::Wires:         return QStringLiteral("Wires");
    case Category::Cable:         return QStringLiteral("Cable");
    case Category::HarnessBody:   return QStringLiteral("Harness body");
    case Category::HarnessBranch: return QStringLiteral("Harness branch");
    case Category::NodePoint:     return QStringLiteral("Node point");
    }
    return QString();
}

PartKind MechWizardDialog::categoryKind(Category c) {
    switch (c) {
    case Category::Connector:     return PartKind::Connector;
    case Category::Terminal:      return PartKind::Crimp;
    case Category::Backshell:     return PartKind::Backshell;
    case Category::Boot:          return PartKind::HeatshrinkBoot;
    case Category::Label:         return PartKind::Marker;
    case Category::Heatshrink:    return PartKind::Heatshrink;
    case Category::Braid:         return PartKind::ShieldBraid;
    case Category::Wires:         return PartKind::Wire;
    case Category::Cable:         return PartKind::Cable;
    // The trunk and a branch are both a run of conductors. They differ in where the user is, not in
    // what gets built, which is why they are two categories over one kind.
    case Category::HarnessBody:   return PartKind::Wire;
    case Category::HarnessBranch: return PartKind::Wire;
    case Category::NodePoint:     return PartKind::YSplit;
    }
    return PartKind::Other;
}

std::vector<MechWizardDialog::Category> MechWizardDialog::categories() {
    return {Category::Connector, Category::Terminal,
            Category::Backshell, Category::Boot,      Category::Label,
            Category::Heatshrink, Category::Braid,
            Category::Wires,     Category::Cable,     Category::HarnessBody,
            Category::HarnessBranch, Category::NodePoint};
}

// ==================================================================================================
// Construction
// ==================================================================================================

MechWizardDialog::MechWizardDialog(Editor* editor, QWidget* parent)
    : QDialog(parent), editor_(editor) {
    setWindowTitle(QStringLiteral("Harness build wizard"));
    setModal(false);   // the drawing stays usable while a harness is being described
    resize(1080, 900);
    catalog_.openRemembered();
    buildUi();
    refreshStepList();
    refreshFieldsForCategory();
    refreshPreview();
    refreshIssues();
}

void MechWizardDialog::buildUi() {
    auto* outer = new QVBoxLayout(this);
    // THE WINDOW DOES NOT RESIZE ITSELF WHEN THE CATEGORY CHANGES.
    //
    // Each category shows a different set of figures - a cable asks for conductors, a gauge, a jacket
    // diameter and its groups; a node asks for almost nothing - and the panel's minimum height went up
    // and down with them. A layout is allowed to grow the window it is in to satisfy that minimum, so
    // picking a category moved the window, and the preview underneath moved and resized with it: the
    // drawing jumped about while somebody was looking at it to decide what to place next.
    //
    // Two things stop it. SetNoConstraint means the layout never resizes the dialog, and the fields
    // panel goes in a scroll area below, so its minimum height is constant no matter how many rows are
    // showing. Together they fix the window and the preview in place; a tall category scrolls.
    outer->setSizeConstraint(QLayout::SetNoConstraint);

    auto* top = new QHBoxLayout();
    outer->addLayout(top, 1);

    // ---- LEFT: what has been built, and what to place next ---------------------------------------
    auto* left = new QVBoxLayout();
    left->setSpacing(6);

    // WHAT HAS BEEN BUILT, not a plan of what might be. The old wizard listed eleven steps before a
    // single one had been answered, which asked the user to read a description of a harness they had
    // not given yet.
    auto* builtLabel = new QLabel(QStringLiteral("The steps so far"), this);
    builtLabel->setStyleSheet(QStringLiteral("font-weight:600;"));
    left->addWidget(builtLabel);
    stepList_ = new QListWidget(this);
    stepList_->setMinimumWidth(240);
    left->addWidget(stepList_, 1);

    auto* catLabel = new QLabel(QStringLiteral("Place next"), this);
    catLabel->setStyleSheet(QStringLiteral("font-weight:600;"));
    left->addWidget(catLabel);
    catList_ = new QListWidget(this);
    for (Category c : categories()) {
        auto* item = new QListWidgetItem(categoryName(c), catList_);
        item->setData(Qt::UserRole, static_cast<int>(c));
    }
    catList_->setCurrentRow(0);
    left->addWidget(catList_, 1);
    top->addLayout(left);

    // ---- RIGHT: the part, its figures, and what is wrong with it ----------------------------------
    //
    // IN A SCROLL AREA, which is what keeps the window still. The panel is as tall as the category it
    // is showing needs; the box it sits in is always the same height, so a category with twelve rows
    // and one with three leave the window and the preview exactly where they were. Horizontal
    // scrolling is off - the panel is told to fit the width, so only the height can ever overflow.
    auto* fields = new QScrollArea(this);
    fields->setWidget(buildFieldsPanel());
    fields->setWidgetResizable(true);
    fields->setFrameShape(QFrame::NoFrame);
    fields->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    fields->setMinimumWidth(440);
    fields->setMinimumHeight(320);
    top->addWidget(fields, 1);

    // ---- The drawing so far ----------------------------------------------------------------------
    preview_ = new DrawingPreview(QStringLiteral("The harness so far"), this);
    preview_->setEmptyText(QStringLiteral("Nothing described yet."));
    preview_->setMinimumHeight(240);
    outer->addWidget(preview_, 1);

    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    outer->addWidget(summary_);

    auto* row = new QHBoxLayout();
    backBtn_ = new QPushButton(QStringLiteral("Take the last step back"), this);
    deleteBtn_ = new QPushButton(QStringLiteral("Delete this step"), this);
    applyBtn_ = new QPushButton(QStringLiteral("Apply to this step"), this);
    // ONE GENERAL "ADD SUBPART", replacing the connector-only "bank this one, add another".
    //
    // That button could only ever add a second CONNECTOR, and it did it by a trick - banking whatever
    // the fields happened to say and clearing them. But the thing somebody actually wants at a station
    // is any number of parts OF it: a second housing, the backshell on it, the boot behind that, the
    // wires leaving it. So there is one button, it works for every category, and what it adds is a step
    // that belongs to the step it was pressed on.
    //
    // The difference from Next is where the harness carries on FROM. Next moves along - the new step
    // becomes what the one after it follows. Add subpart does not move: the anchor stays on the parent,
    // so pressing it again adds another part to the SAME station rather than a chain of one thing after
    // another. That is what makes several housings at one end, and their dressing, describable at all.
    subpartBtn_ = new QPushButton(QStringLiteral("Add subpart"), this);
    subpartBtn_->setToolTip(QStringLiteral(
        "Adds what is described above as a part OF the selected step rather than the next station "
        "along - a second connector at this end, a backshell on it, the wires leaving it. The harness "
        "carries on from the parent, so pressing this again adds another part to the same place."));
    subpartBtn_->setAutoDefault(false);
    nextBtn_ = new QPushButton(QStringLiteral("Next ›"), this);
    // DELIBERATELY NOT THE DEFAULT BUTTON. With Next as default, pressing Enter after correcting a
    // length committed a whole new step - the opposite of what the person correcting it wanted. Enter
    // now applies the change to the step being edited; adding a step is an explicit click.
    nextBtn_->setAutoDefault(false);
    row->addWidget(backBtn_);
    row->addWidget(deleteBtn_);
    row->addWidget(applyBtn_);
    row->addStretch(1);
    auto* placeBtn = new QPushButton(QStringLiteral("Place the drawing"), this);
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    row->addWidget(subpartBtn_);
    row->addWidget(nextBtn_);
    row->addSpacing(16);
    row->addWidget(placeBtn);
    row->addWidget(closeBtn);
    outer->addLayout(row);

    connect(catList_, &QListWidget::currentRowChanged, this, [this](int) {
        if (auto* it = catList_->currentItem())
            category_ = static_cast<Category>(it->data(Qt::UserRole).toInt());
        refreshFieldsForCategory();
        fieldChanged();          // the category is a change like any other: the preview follows it
    });
    connect(nextBtn_, &QPushButton::clicked, this, [this] { commitStep(); });
    connect(backBtn_, &QPushButton::clicked, this, [this] { undoStep(); });
    connect(deleteBtn_, &QPushButton::clicked, this, [this] {
        if (!editing_.isEmpty()) deleteStep(editing_);
    });
    connect(applyBtn_, &QPushButton::clicked, this, [this] { applyToEditedStep(); });
    // Item 2: CHANGING THE PART CHANGES THE DRAWING. The combo had no connection at all, so picking a
    // connector left the preview showing the default box - and the one thing a preview is for is
    // showing what the choice just made looks like.
    connect(partCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (loading_) return;
        // Only the CONTACTS are rebuilt. Rebuilding the part list here cleared the very selection that
        // had just been made, so a library part could be picked and never stuck - it snapped back to
        // Default before anything read it.
        refreshContactCombo();
        // THE WAYS COME FROM THE PART, when the part states them. See declaredWaysOfChosenPart: what a
        // datasheet already says is not worth asking a person to retype, and a way count typed by hand
        // beside a chosen part is a way count that will disagree with it.
        if (const int ways = declaredWaysOfChosenPart(); ways > 0) {
            const bool was = loading_;
            loading_ = true;
            waysSpin_->setValue(ways);
            loading_ = was;
        }
        fieldChanged();
    });
    connect(contactCombo_, &QComboBox::currentIndexChanged, this, [this](int) { fieldChanged(); });
    connect(gaugeCombo_, &QComboBox::currentTextChanged, this,
            [this](const QString&) { fieldChanged(); });
    connect(addGroupBtn_, &QPushButton::clicked, this, [this] { addConductorGroup(); });
    connect(dropGroupBtn_, &QPushButton::clicked, this, [this] { removeConductorGroup(); });
    connect(subpartBtn_, &QPushButton::clicked, this, [this] { commitSubpart(); });

    // Clicking a step in the list EDITS it. That is the whole of item 2: correcting a figure must not
    // mean describing a new step.
    connect(stepList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (loading_) return;
        if (row < 0) return;
        // THE LAST ROW IS THE STEP BEING DESCRIBED - see refreshStepList. Clicking it goes back to
        // describing a new step, which is the natural inverse of clicking a described one to edit it.
        if (row >= static_cast<int>(script_.steps.size())) {
            if (editing_.isEmpty()) return;
            editing_.clear();
            refreshStepList();
            refreshFieldsForCategory();
            refreshPreview();
            return;
        }
        editStep(script_.steps[static_cast<size_t>(row)].id);
    });

    // EVERY CHANGE REDRAWS THE PREVIEW, and applies to the step being edited when one is.
    //
    // The signals are the live ones - textChanged, valueChanged - and not editingFinished, because the
    // preview exists to show the effect of the change being made rather than the effect of the last one
    // that happened to lose focus. Adding a step is still only ever an explicit click on Next.
    const auto changed = [this] { fieldChanged(); };
    connect(nameEdit_, &QLineEdit::textChanged, this, changed);
    connect(dimText_, &QLineEdit::textChanged, this, changed);
    connect(desigEdit_, &QLineEdit::textChanged, this, changed);
    connect(lengthSpin_, &QDoubleSpinBox::valueChanged, this, changed);
    connect(odSpin_, &QDoubleSpinBox::valueChanged, this, changed);
    connect(countSpin_, &QSpinBox::valueChanged, this, changed);
    connect(waysSpin_, &QSpinBox::valueChanged, this, changed);
    connect(dimCheck_, &QCheckBox::toggled, this, changed);
    connect(breakCheck_, &QCheckBox::toggled, this, changed);
    connect(showNameCheck_, &QCheckBox::toggled, this, changed);
    connect(continueCheck_, &QCheckBox::toggled, this, changed);
    // The balloon answer is a habit: remembered per category so it is not asked again every step.
    connect(balloonCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (loading_) return;
        balloonByCategory_.insert(static_cast<int>(category_), on);
        fieldChanged();
    });
    connect(placeBtn, &QPushButton::clicked, this, [this] { place(); });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

QWidget* MechWizardDialog::buildFieldsPanel() {
    auto* panel = new QWidget(this);
    auto* col = new QVBoxLayout(panel);
    col->setContentsMargins(0, 0, 0, 0);

    heading_ = new QLabel(panel);
    heading_->setStyleSheet(QStringLiteral("font-size:15px;font-weight:600;"));
    col->addWidget(heading_);
    hint_ = new QLabel(panel);
    hint_->setWordWrap(true);
    hint_->setStyleSheet(QStringLiteral("color:#9aa7b8;"));
    col->addWidget(hint_);
    col->addSpacing(10);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    partCombo_ = new QComboBox(panel);
    form->addRow(QStringLiteral("Part"), partCombo_);

    // The label is set per category in refreshFieldsForCategory: DESIGNATOR on a connector or a
    // terminal, where the answer is a controlled reference and not a caption. See nameLabel_.
    nameLabel_ = new QLabel(QStringLiteral("Designator"), panel);
    nameEdit_ = new QLineEdit(panel);
    form->addRow(nameLabel_, nameEdit_);

    // ---- DIRECTION: per step, and it starts out the way the last step went --------------------
    //
    // Two buttons rather than a spin box of degrees, because the answer is nearly always one of two
    // and a harness that runs straight should cost no decision at all. What is stored is the TURN
    // relative to the previous step (MechStep::turnDeg); what is chosen here is the direction on the
    // paper. The wizard converts, so a branch moved later takes its own sub-branches with it.
    auto* dirRow = new QWidget(panel);
    auto* dirLay = new QHBoxLayout(dirRow);
    dirLay->setContentsMargins(0, 0, 0, 0);
    leftBtn_ = new QToolButton(dirRow);
    leftBtn_->setText(QStringLiteral("◀  Left"));
    leftBtn_->setCheckable(true);
    rightBtn_ = new QToolButton(dirRow);
    rightBtn_->setText(QStringLiteral("Right  ▶"));
    rightBtn_->setCheckable(true);
    rightBtn_->setChecked(true);
    dirLay->addWidget(leftBtn_);
    dirLay->addWidget(rightBtn_);
    dirLay->addStretch(1);
    form->addRow(QStringLiteral("Drawn"), dirRow);

    // PICKING A DIRECTION TURNS THE PART IN THE PREVIEW, at once.
    //
    // Two things were wrong. The buttons redrew nothing at all - they set the flag and stopped, so the
    // preview went on showing the part facing whichever way it had been. And on the FIRST step the flag
    // could not have changed the drawing anyway: that step lands on the ROOT node, and a turn on the
    // root is discarded by the compiler, so the only place the answer can live is the script's own
    // handedness. See directionSetsHandedness.
    //
    // With both fixed, Left and Right swing the connector so the face the harness leaves by points the
    // way the harness is going - which is what somebody is looking at the preview to check.
    const auto pickDirection = [this](bool right) {
        setCurrentGoesRight(right);
        if (directionSetsHandedness()) script_.leftToRight = right;
        fieldChanged();
    };
    connect(leftBtn_, &QToolButton::clicked, this, [pickDirection] { pickDirection(false); });
    connect(rightBtn_, &QToolButton::clicked, this, [pickDirection] { pickDirection(true); });

    lengthLabel_ = new QLabel(QStringLiteral("Length"), panel);
    lengthSpin_ = new QDoubleSpinBox(panel);
    lengthSpin_->setRange(0.0, 100000.0);
    lengthSpin_->setSuffix(QStringLiteral(" mm"));
    lengthSpin_->setValue(200.0);
    form->addRow(lengthLabel_, lengthSpin_);

    countLabel_ = new QLabel(QStringLiteral("Conductors"), panel);
    countSpin_ = new QSpinBox(panel);
    countSpin_->setRange(0, 500);
    countSpin_->setValue(4);
    form->addRow(countLabel_, countSpin_);

    gaugeLabel_ = new QLabel(QStringLiteral("Gauge"), panel);
    gaugeCombo_ = new QComboBox(panel);
    gaugeCombo_->setEditable(true);   // a drawing may say something the list does not
    for (int awg : kAwgGauges) {
        const double mm2 = mechAwgMm2(awg);
        gaugeCombo_->addItem(QStringLiteral("%1 AWG  (%2 mm²)").arg(awg).arg(mm2, 0, 'g', 2),
                             QStringLiteral("%1 AWG").arg(awg));
    }
    // 20 AWG is 0.5 mm2 - the size these harnesses are mostly laid up from, and what the list used to
    // open on before it spoke AWG.
    setGauge(QStringLiteral("20 AWG"));
    form->addRow(gaugeLabel_, gaugeCombo_);

    odLabel_ = new QLabel(QStringLiteral("Jacket ø"), panel);
    odSpin_ = new QDoubleSpinBox(panel);
    odSpin_->setRange(0.0, 500.0);
    odSpin_->setSuffix(QStringLiteral(" mm"));
    form->addRow(odLabel_, odSpin_);

    // Item 7: WHAT "WAYS" IS. The number of positions in the connector - how many contacts it can
    // hold. Named on the drawing side too: it sets the connector body's height (2.5 mm a way plus a
    // margin), so a 9-way shell is drawn taller than a 2-way one, and it is what the contact quantity
    // in the bill is counted against. Renamed from the bare "Ways" because nobody should have to guess.
    waysLabel_ = new QLabel(QStringLiteral("Ways (positions)"), panel);
    waysSpin_ = new QSpinBox(panel);
    waysSpin_->setRange(0, 500);
    waysSpin_->setToolTip(QStringLiteral(
        "How many positions the connector has. Sets how tall the body is drawn, and is what the "
        "contact count in the bill of materials is measured against."));
    form->addRow(waysLabel_, waysSpin_);

    contactLabel_ = new QLabel(QStringLiteral("Contact"), panel);
    contactCombo_ = new QComboBox(panel);
    form->addRow(contactLabel_, contactCombo_);

    // SEVERAL GROUPS OF CONDUCTORS in one run. A hook-up bundle is whatever the circuit needs - two
    // of 0.5 and one of 1.5 - and the count/gauge fields above describe ONE group. Adding it puts it
    // on this list; the list is what the run carries.
    groupsLabel_ = new QLabel(QStringLiteral("Carrying"), panel);
    auto* groupsBox = new QWidget(panel);
    auto* groupsLay = new QVBoxLayout(groupsBox);
    groupsLay->setContentsMargins(0, 0, 0, 0);
    groupList_ = new QListWidget(groupsBox);
    groupList_->setMaximumHeight(84);
    groupsLay->addWidget(groupList_);
    auto* groupBtns = new QHBoxLayout();
    addGroupBtn_ = new QPushButton(QStringLiteral("Add the group above"), groupsBox);
    dropGroupBtn_ = new QPushButton(QStringLiteral("Remove"), groupsBox);
    groupBtns->addWidget(addGroupBtn_);
    groupBtns->addWidget(dropGroupBtn_);
    groupBtns->addStretch(1);
    groupsLay->addLayout(groupBtns);
    form->addRow(groupsLabel_, groupsBox);

    // A DESIGNATOR PER WIRE, and only where the harness starts open.
    //
    // A run out of a connector needs none: the wires are known by the way they go into, so "P1-3" names
    // a conductor without anybody typing it. A harness that starts at BARE WIRES, ferrules or ring
    // terminals has no connector to be named from, and then these are the only identification the
    // conductors have - on those drawings they are not a convenience, they are the document.
    desigLabel_ = new QLabel(QStringLiteral("Wire designators"), panel);
    desigEdit_ = new QLineEdit(panel);
    desigEdit_->setPlaceholderText(QStringLiteral("one per wire, in order: W1, W2, W3"));
    desigEdit_->setToolTip(QStringLiteral(
        "One designator per wire, separated by commas, in the order the conductors are counted. Asked "
        "because this harness starts open: there is no connector for the wires to be named from."));
    form->addRow(desigLabel_, desigEdit_);

    // The conductors carry on rather than starting again - see MechStep::continuesConductors. This is
    // what keeps two lengths of one bundle from being drawn as a splice.
    continueCheck_ = new QCheckBox(QStringLiteral("The same conductors carry on through this run"), panel);
    form->addRow(QString(), continueCheck_);

    breakCheck_ = new QCheckBox(QStringLiteral("Break mark: longer than it is drawn"), panel);
    form->addRow(QString(), breakCheck_);

    // WRITE THE DESIGNATOR ON THE DRAWING. Off by default: a released harness does not letter every leg
    // of itself, and a designator against each of thirty branches buries the sheet.
    showNameCheck_ = new QCheckBox(QStringLiteral("Show the designator on the drawing"), panel);
    showNameCheck_->setToolTip(QStringLiteral(
        "Writes this run's designator beside it. Not available on a run between two parts: a wire from "
        "P1 to P2 is already identified by its ends, and the wire list calls it P1-3 to P2-5."));
    form->addRow(QString(), showNameCheck_);

    dimCheck_ = new QCheckBox(QStringLiteral("Dimension this step"), panel);
    form->addRow(QString(), dimCheck_);
    dimText_ = new QLineEdit(panel);
    dimText_->setPlaceholderText(QStringLiteral("what the dimension reads (blank = the measured size)"));
    form->addRow(QStringLiteral("Reads"), dimText_);

    balloonCheck_ = new QCheckBox(QStringLiteral("Balloon it (points at its line in the bill)"), panel);
    balloonCheck_->setChecked(true);
    form->addRow(QString(), balloonCheck_);

    col->addLayout(form);
    col->addSpacing(12);

    // ---- What is wrong with it -------------------------------------------------------------------
    auto* box = new QGroupBox(QStringLiteral("Checks"), panel);
    auto* boxLay = new QVBoxLayout(box);
    issueLabel_ = new QLabel(box);
    issueLabel_->setWordWrap(true);
    issueLabel_->setTextFormat(Qt::RichText);
    boxLay->addWidget(issueLabel_);
    col->addWidget(box);

    col->addStretch(1);
    return panel;
}

// ==================================================================================================
// State
// ==================================================================================================

void MechWizardDialog::setScript(const MechBuildScript& script) {
    script_ = script;
    anchor_.clear();
    // The anchor is the last step described: the one a new step follows.
    if (!script_.steps.empty()) anchor_ = script_.steps.back().id;
    goRight_ = inheritedGoesRight();
    refreshStepList();
    refreshFieldsForCategory();
    refreshPreview();
    refreshIssues();
}

void MechWizardDialog::setCurrentCategory(Category c) {
    category_ = c;
    for (int i = 0; i < catList_->count(); ++i) {
        if (static_cast<Category>(catList_->item(i)->data(Qt::UserRole).toInt()) == c) {
            catList_->setCurrentRow(i);
            break;
        }
    }
    refreshFieldsForCategory();
}

void MechWizardDialog::setCurrentGoesRight(bool right) {
    goRight_ = right;
    if (leftBtn_) leftBtn_->setChecked(!right);
    if (rightBtn_) rightBtn_->setChecked(right);
}

bool MechWizardDialog::inheritedGoesRight() const {
    // A new step starts out going whichever way the last one went, which is what makes a straight
    // harness no work and a turn one click. With nothing built yet it follows the script's own
    // handedness.
    if (anchor_.isEmpty()) return script_.leftToRight;
    // Walk back up the chain accumulating turns to get the last step's ABSOLUTE direction: turnDeg is
    // relative, and "the way the last step went" is not a figure the script stores directly.
    double absDeg = script_.leftToRight ? 0.0 : 180.0;
    std::vector<const MechStep*> chain;
    for (const MechStep* s = script_.step(anchor_); s; s = s->parentStep.isEmpty()
                                                            ? nullptr
                                                            : script_.step(s->parentStep))
        chain.push_back(s);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) absDeg += (*it)->turnDeg;
    // Normalised to (-180, 180]: anything pointing into the left half of the paper is "left".
    while (absDeg > 180.0) absDeg -= 360.0;
    while (absDeg <= -180.0) absDeg += 360.0;
    return std::abs(absDeg) < 90.0;
}

MechStep MechWizardDialog::stepFromFields() const {
    MechStep s;
    s.id = script_.nextStepId();
    s.kind = categoryKind(category_);
    s.parentStep = anchor_;
    s.name = nameEdit_->text().trimmed();
    if (partCombo_->currentIndex() > 0) s.libPart = partCombo_->currentData().toString();

    // THE TURN, from the direction chosen. What the user picks is where this step points on the
    // paper; what the script keeps is the turn away from the step before it.
    const double wantAbs = goRight_ ? 0.0 : 180.0;
    const double haveAbs = inheritedGoesRight() ? 0.0 : 180.0;
    double turn = wantAbs - haveAbs;
    while (turn > 180.0) turn -= 360.0;
    while (turn <= -180.0) turn += 360.0;
    s.turnDeg = turn;

    switch (mechStepRole(s.kind)) {
    case MechStepRole::Run: {
        s.lengthMm = lengthSpin_->value();
        s.conductors = pending_;
        const QStringList desigs = splitDesignators(desigEdit_->text());
        // The count/gauge fields describe ONE group. They are used only when no groups were added,
        // so a run carrying a single size stays as simple to describe as it was.
        if (s.conductors.empty()) {
            s.wireCount = countSpin_->value();
            s.gauge = gaugeCanon();
            // ONE GROUP, MATERIALISED, when the wires were named one at a time. The count/gauge
            // shorthand has nowhere to keep a designator, so without this a name typed per wire would
            // be the first thing lost - on the very drawings where it is the only identification the
            // conductor has.
            if (!desigs.isEmpty() && s.wireCount > 0) {
                MechConductorGroup only;
                only.count = s.wireCount;
                only.gauge = s.gauge;
                only.designators = desigs;
                s.conductors.push_back(only);
            }
        } else {
            // THE FIELD IS THE AUTHORITY FOR THE WHOLE RUN. Its contents are spread across the groups
            // in the order wiresForRun counts the conductors in, and a group the field does not reach
            // is left unnamed.
            //
            // Deliberately not "each group keeps whatever it was given": then emptying the field could
            // not empty a designator, and the drawing would go on carrying a wire name with nothing on
            // the screen saying where it came from. One field, one answer, and deleting works.
            int at = 0;
            for (MechConductorGroup& g : s.conductors) {
                const int n = std::max(0, g.count);
                g.designators = at < desigs.size() ? desigs.mid(at, n) : QStringList();
                at += n;
            }
        }
        if (category_ == Category::Cable) s.odMm = odSpin_->value();
        s.continuesConductors = continueCheck_->isChecked();
        s.lengthBreak = breakCheck_->isChecked();
        s.showName = showNameCheck_->isChecked();
        break;
    }
    case MechStepRole::End:
        s.pinCount = waysSpin_->value();
        if (contactCombo_->currentIndex() > 0)
            s.contactPart = contactCombo_->currentData().toString();
        break;
    case MechStepRole::AtNode:
    case MechStepRole::AlongRun:
        s.lengthMm = lengthSpin_->value();
        break;
    case MechStepRole::Node:
        break;
    }

    s.dimension = dimCheck_->isChecked();
    s.dimText = dimText_->text().trimmed();
    s.balloon = balloonCheck_->isChecked();
    return s;
}

// ==================================================================================================
// Editing, applying and deleting
// ==================================================================================================

void MechWizardDialog::editStep(const QString& stepId) {
    const MechStep* s = script_.step(stepId);
    if (!s) return;
    editing_ = stepId;

    // loading_ guards every field-changed handler while the fields are FILLED, or loading a step would
    // immediately write it back over itself - and the first thing that write would do is clobber the
    // conductor groups with an empty pending list.
    loading_ = true;
    for (Category c : categories())
        if (categoryKind(c) == s->kind) { setCurrentCategory(c); break; }

    // THE PART AND ITS CONTACT, PUT BACK. Without this, clicking a step to edit it left both combos on
    // Default - and the first apply then wrote that emptiness back, so merely SELECTING a step erased
    // the part somebody had chosen for it. The combos are rebuilt for the step's own category first,
    // or the part would be selected in a list that does not contain it.
    refreshPartCombo();
    selectByData(partCombo_, s->libPart);
    refreshContactCombo();
    selectByData(contactCombo_, s->contactPart);

    nameEdit_->setText(s->name);
    lengthSpin_->setValue(s->lengthMm);
    odSpin_->setValue(s->odMm);
    waysSpin_->setValue(s->pinCount);
    countSpin_->setValue(s->wireCount);
    if (!s->gauge.isEmpty()) setGauge(s->gauge);
    showNameCheck_->setChecked(s->showName);
    dimCheck_->setChecked(s->dimension);
    dimText_->setText(s->dimText);
    balloonCheck_->setChecked(s->balloon);
    breakCheck_->setChecked(s->lengthBreak);
    continueCheck_->setChecked(s->continuesConductors);
    pending_ = s->conductors;

    // THE WIRE DESIGNATORS, PUT BACK, for the same reason the part is: a field that is not restored is
    // a field the next apply writes back empty, so merely selecting a step would erase the wire names.
    // They live on the groups, and the field shows them run together in conductor order.
    QStringList desigs;
    for (const MechConductorGroup& g : s->conductors) desigs << g.designators;
    desigEdit_->setText(desigs.join(QStringLiteral(", ")));

    // The direction as this step points on the paper, which is its parent's direction plus its turn.
    const QString keep = editing_;
    const QString anchorWas = anchor_;
    anchor_ = s->parentStep;
    const bool parentRight = inheritedGoesRight();
    anchor_ = anchorWas;
    editing_ = keep;
    double abs = (parentRight ? 0.0 : 180.0) + s->turnDeg;
    while (abs > 180.0) abs -= 360.0;
    while (abs <= -180.0) abs += 360.0;
    setCurrentGoesRight(std::abs(abs) < 90.0);

    loading_ = false;
    refreshFieldsForCategory();
    refreshIssues();
}

void MechWizardDialog::applyToEditedStep() {
    if (loading_ || editing_.isEmpty()) return;
    for (MechStep& s : script_.steps) {
        if (s.id != editing_) continue;

        // The step's PLACE in the script is not the fields' business: its id and its parent are what
        // make the tree, and a field edit must not re-hang the harness.
        const QString id = s.id;
        const QString parent = s.parentStep;
        const QString anchorWas = anchor_;
        anchor_ = parent;
        MechStep next = stepFromFields();
        anchor_ = anchorWas;
        next.id = id;
        next.parentStep = parent;
        s = next;
        break;
    }
    refreshStepList();
    refreshPreview();
    refreshIssues();
}

void MechWizardDialog::deleteStep(const QString& stepId) {
    const MechStep* victim = script_.step(stepId);
    if (!victim) return;
    const QString parent = victim->parentStep;

    // THE HARNESS CLOSES UP ROUND THE GAP. Whatever followed this step is re-parented onto its parent,
    // because the alternative is a branch pointing at a step that no longer exists - which compiles to
    // nothing and drops that whole limb off the drawing without a word.
    for (MechStep& s : script_.steps)
        if (s.parentStep == stepId) s.parentStep = parent;

    script_.steps.erase(std::remove_if(script_.steps.begin(), script_.steps.end(),
                                       [&](const MechStep& s) { return s.id == stepId; }),
                        script_.steps.end());

    if (editing_ == stepId) editing_.clear();
    if (anchor_ == stepId) anchor_ = parent;
    setCurrentGoesRight(inheritedGoesRight());
    refreshStepList();
    refreshPreview();
    refreshIssues();
}

// ==================================================================================================
// Conductor groups
// ==================================================================================================

void MechWizardDialog::addConductorGroup() {
    MechConductorGroup g;
    g.count = countSpin_->value();
    g.gauge = gaugeCanon();
    if (g.count <= 0) return;   // a group of nothing is not a group
    pending_.push_back(g);
    refreshFieldsForCategory();
    fieldChanged();
}

void MechWizardDialog::removeConductorGroup() {
    const int row = groupList_->currentRow();
    if (row < 0 || row >= static_cast<int>(pending_.size())) return;
    pending_.erase(pending_.begin() + row);
    refreshFieldsForCategory();
    fieldChanged();
}

// ==================================================================================================
// More than one connector at this end
// ==================================================================================================

void MechWizardDialog::commitSubpart() {
    // THE SAME COMMIT AS NEXT, with two differences that are the whole point of the button.
    //
    // The step is marked as a part OF its parent rather than the next station along - which is what the
    // list indents and what lets a second housing at one end be told apart from a connector further
    // down the harness. And the ANCHOR IS PUT BACK afterwards, so the harness still carries on from the
    // parent: press it twice and you get two parts at one place, not a chain of two.
    const QString parent = anchor_;
    MechStep s = stepFromFields();
    s.isSubpart = !parent.isEmpty();     // nothing to be a part of at the very start
    script_.steps.push_back(s);

    editing_.clear();
    anchor_ = parent.isEmpty() ? s.id : parent;
    setCurrentGoesRight(inheritedGoesRight());

    loading_ = true;
    nameEdit_->clear();
    dimCheck_->setChecked(false);
    dimText_->clear();
    breakCheck_->setChecked(false);
    showNameCheck_->setChecked(false);
    desigEdit_->clear();
    pending_.clear();
    loading_ = false;

    refreshStepList();
    refreshFieldsForCategory();
    refreshPreview();
    refreshIssues();
}

// ==================================================================================================
// What the fields say, and what the preview shows
// ==================================================================================================

void MechWizardDialog::fieldChanged() {
    if (loading_) return;
    // Writes back when a step is being edited; does nothing when a new one is being described.
    applyToEditedStep();
    // AND EITHER WAY the preview and the checks are brought up to date. applyToEditedStep already does
    // both for an edited step, but a step being DESCRIBED is not in the script at all - it reaches the
    // preview through previewScript(), and nothing else would ever ask for it to be redrawn.
    if (editing_.isEmpty()) {
        refreshPreview();
        refreshIssues();
    }
}

QString MechWizardDialog::gaugeCanon() const {
    // The list shows "20 AWG  (0.5 mm²)" and carries "20 AWG" as its data. Where the current text is
    // still the list's own caption, the data is the answer; where somebody has TYPED something - the
    // combo is editable on purpose, because a drawing may say a size the list does not - their words
    // are the answer exactly as written.
    const int i = gaugeCombo_->currentIndex();
    if (i >= 0 && gaugeCombo_->itemText(i) == gaugeCombo_->currentText()) {
        const QString data = gaugeCombo_->itemData(i).toString();
        if (!data.isEmpty()) return data;
    }
    return gaugeCombo_->currentText().trimmed();
}

void MechWizardDialog::setGauge(const QString& gauge) {
    for (int i = 0; i < gaugeCombo_->count(); ++i) {
        if (gaugeCombo_->itemData(i).toString() != gauge) continue;
        gaugeCombo_->setCurrentIndex(i);
        return;
    }
    // Not one of the listed sizes: a gauge typed by hand, or one saved before the list spoke AWG. It is
    // shown as it was written rather than snapped to the nearest listed size, which would silently
    // change a figure somebody chose.
    gaugeCombo_->setCurrentText(gauge);
}

QString MechWizardDialog::nextRunDesignator() const {
    // W1, W2, W3 - the next one not already used. Scanned rather than counted, because steps can be
    // deleted out of the middle and a count would then hand out a designator that is already on the
    // drawing. A run the user has lettered some other way is simply not in the W series and is skipped.
    for (int n = 1;; ++n) {
        const QString want = QStringLiteral("W%1").arg(n);
        bool taken = false;
        for (const MechStep& s : script_.steps)
            if (s.id != editing_ && s.name.compare(want, Qt::CaseInsensitive) == 0) {
                taken = true;
                break;
            }
        if (!taken) return want;
    }
}

bool MechWizardDialog::directionSetsHandedness() const {
    if (!editing_.isEmpty()) {
        const MechStep* s = script_.step(editing_);
        return s && s->parentStep.isEmpty();
    }
    return anchor_.isEmpty();
}

bool MechWizardDialog::runIsBetweenTwoParts() const {
    if (mechStepRole(categoryKind(category_)) != MechStepRole::Run) return false;
    // BOTH ENDS ARE PARTS. Asked of the compiled harness rather than of the script, because "what is at
    // the far end of this run" is a question about the tree: the far node is a free end until some later
    // step lands a connector on it, and that step names the RUN as its parent rather than the node.
    const QString id = editing_.isEmpty() ? QString() : editing_;
    if (id.isEmpty()) {
        // A run being described has nothing after it yet, so its far end is open by definition. The
        // renderer withholds the designator later if a connector does arrive there - see
        // MechSegment::showName - so ticking it now is never a promise that cannot be kept.
        return false;
    }
    const MechCompiled c = compileMechScript(script_);
    const MechSegment* seg = c.def.segment(c.stepToSegment.value(id));
    if (!seg) return false;
    const MechNode* from = c.def.node(seg->fromNode);
    const MechNode* to = c.def.node(seg->toNode);
    const auto isPart = [](const MechNode* n) {
        return n && n->kind == MechNodeKind::Connector;
    };
    return isPart(from) && isPart(to);
}

bool MechWizardDialog::startsOpen() const {
    // Nothing built yet, and the thing being described is the run itself: the harness begins at bare
    // wires. The other open start is a run whose parent is not an end - a run off a node with no
    // connector behind it - but that one has a connector somewhere further back to be named from, so
    // the question is only asked at the very start, where there is genuinely nothing else.
    if (mechStepRole(categoryKind(category_)) != MechStepRole::Run) return false;
    if (!editing_.isEmpty()) {
        const MechStep* s = script_.step(editing_);
        return s && s->parentStep.isEmpty();
    }
    return anchor_.isEmpty();
}

int MechWizardDialog::declaredWaysOfChosenPart() const {
    if (mechStepRole(categoryKind(category_)) != MechStepRole::End) return 0;
    const QString id = partCombo_->currentData().toString();
    if (id.isEmpty()) return 0;
    const LibraryPart* p = catalog_.byId(id);
    return p ? declaredWays(*p) : 0;
}

MechBuildScript MechWizardDialog::previewScript() const {
    MechBuildScript sc = script_;
    // A step being EDITED is already in the script - applyToEditedStep put it there - so there is
    // nothing to add. Only the step being described is missing.
    if (!editing_.isEmpty()) return sc;

    const MechStep s = stepFromFields();
    // A run of no length would compile to a segment going nowhere, which draws as a smear at the
    // previous node rather than as a preview of anything. Everything else is worth showing the moment
    // it is chosen, which is the whole point: a connector picked from the library appears at once.
    if (mechStepRole(s.kind) == MechStepRole::Run && s.lengthMm <= 0.0) return sc;
    sc.steps.push_back(s);
    return sc;
}

void MechWizardDialog::commitStep() {
    const MechStep s = stepFromFields();
    script_.steps.push_back(s);
    anchor_ = s.id;
    editing_.clear();          // a committed step is no longer the one being edited

    // The next step starts out going the same way as this one - see inheritedGoesRight.
    setCurrentGoesRight(inheritedGoesRight());

    loading_ = true;
    nameEdit_->clear();
    dimCheck_->setChecked(false);
    dimText_->clear();
    breakCheck_->setChecked(false);
    showNameCheck_->setChecked(false);
    desigEdit_->clear();       // the wire names belonged to the run just added
    pending_.clear();          // and so did the groups
    // AND THE CONDUCTORS CARRY ON BY DEFAULT once there is a run behind them. This is the setting that
    // stops the next length of the same bundle being drawn as a splice; it only makes sense when there
    // is something to continue from, which is why it is decided here rather than left ticked.
    continueCheck_->setChecked(mechStepRole(s.kind) == MechStepRole::Run);
    loading_ = false;

    refreshStepList();
    refreshFieldsForCategory();
    refreshPreview();
    refreshIssues();
}

void MechWizardDialog::undoStep() {
    if (script_.steps.empty()) return;
    // Anything that followed the step being removed would be orphaned, so the LAST step is the only
    // one that can come off without leaving the script describing something else.
    const QString gone = script_.steps.back().id;
    script_.steps.pop_back();
    if (anchor_ == gone) anchor_ = script_.steps.empty() ? QString() : script_.steps.back().id;
    setCurrentGoesRight(inheritedGoesRight());
    refreshStepList();
    refreshPreview();
    refreshIssues();
}

// ==================================================================================================
// What the window currently shows
// ==================================================================================================

QStringList MechWizardDialog::shownSteps() const {
    QStringList out;
    for (int i = 0; i < stepList_->count(); ++i) out << stepList_->item(i)->text();
    return out;
}

QStringList MechWizardDialog::shownGauges() const {
    QStringList out;
    for (int i = 0; i < gaugeCombo_->count(); ++i) out << gaugeCombo_->itemText(i);
    return out;
}

QString MechWizardDialog::shownNameLabel() const {
    return nameLabel_ ? nameLabel_->text() : QString();
}

bool MechWizardDialog::asksForWireDesignators() const {
    return desigEdit_ && desigEdit_->isVisibleTo(const_cast<MechWizardDialog*>(this));
}

int MechWizardDialog::shownWays() const { return waysSpin_ ? waysSpin_->value() : 0; }

void MechWizardDialog::typeName(const QString& text) { nameEdit_->setText(text); }
void MechWizardDialog::typeWays(int ways) { waysSpin_->setValue(ways); }
void MechWizardDialog::typeWireDesignators(const QString& text) { desigEdit_->setText(text); }

std::vector<MechStepIssue> MechWizardDialog::issues() const {
    const MechCompiled c = compileMechScript(script_);
    return mechScriptIssues(script_, c, &catalog_);
}

// ==================================================================================================
// Refreshing
// ==================================================================================================

void MechWizardDialog::refreshStepList() {
    // Filling the list must not be read as the user picking a row. SAVED AND RESTORED rather than
    // set-then-cleared, because refreshFieldsForCategory calls this to keep the "being described" row
    // in step with the palette - and that runs inside editStep, which holds the guard down while it
    // fills every field. Clearing it here let the second half of editStep write a half-loaded step back
    // over itself, which lost the wire designators and the banked connectors on a click.
    const bool wasLoading = loading_;
    loading_ = true;
    stepList_->clear();
    int n = 0;
    for (const MechStep& s : script_.steps) {
        ++n;
        // A DESCRIBED STEP READS AS ITS TYPE AND ITS DESIGNATORS, both, and not one or the other.
        //
        // It used to show the name INSTEAD of the type where there was a name, so a list of a real
        // harness read "P1, W1, P2" - three references and no indication of what any of them was. The
        // type is what makes the list a build sequence; the designator is what ties a line of it to the
        // wire list. An end with two housings on it names them all, because that end is both of them.
        QString what = partKindName(s.kind);
        if (!s.name.isEmpty()) what += QStringLiteral("  %1").arg(s.name);
        if (mechStepRole(s.kind) == MechStepRole::Run && s.lengthMm > 0.0)
            what += QStringLiteral(", %1 mm").arg(s.lengthMm, 0, 'f', 0);
        if (s.turnDeg != 0.0) what += QStringLiteral("  (turns %1°)").arg(s.turnDeg, 0, 'f', 0);

        // A SUBPART IS INDENTED UNDER WHAT IT IS A PART OF, to whatever depth the harness has. A flat
        // numbered run of everything anybody typed reads as a sequence of stations, and "P1, backshell,
        // wires, P2" is not four stations - it is two, one of them described in three parts. The depth
        // is walked rather than stored, so re-parenting a step by deleting its parent re-indents it
        // without anything having to be kept in step.
        int depth = 0;
        for (const MechStep* up = script_.step(s.parentStep); up && up->isSubpart;
             up = script_.step(up->parentStep))
            ++depth;
        if (s.isSubpart) ++depth;
        const QString lead = s.isSubpart ? QStringLiteral("    ").repeated(depth) +
                                               QStringLiteral("└ ")
                                         : QString();
        new QListWidgetItem(s.isSubpart ? QStringLiteral("%1%2").arg(lead, what)
                                        : QStringLiteral("%1  %2").arg(n).arg(what),
                            stepList_);
    }

    // THE STEP BEING DESCRIBED GETS A ROW OF ITS OWN, at the bottom, before it has been added.
    //
    // The list used to start empty, so at the very moment somebody was describing step 1 there was
    // nothing anywhere on the window saying so - the panel said "Step 1" and the list said nothing.
    // Showing it as a row means the sequence is visible from the first keystroke, and pressing Next
    // does not make a row appear out of nowhere: it settles this one and opens the next.
    //
    // It is a placeholder and reads like one. Clicking it goes back to describing a new step; it is not
    // in script_.steps, so nothing downstream can mistake it for something that has been agreed.
    if (editing_.isEmpty()) {
        auto* pendingRow = new QListWidgetItem(
            QStringLiteral("%1  %2 — being described").arg(n + 1).arg(categoryName(category_)),
            stepList_);
        QFont f = pendingRow->font();
        f.setItalic(true);
        pendingRow->setFont(f);
    }

    // The row for the step being edited stays selected, so applying a change does not move the
    // selection off the thing that was just changed. Otherwise it is the step being described.
    int want = stepList_->count() - 1;
    if (!editing_.isEmpty()) {
        for (size_t i = 0; i < script_.steps.size(); ++i)
            if (script_.steps[i].id == editing_) want = static_cast<int>(i);
    }
    if (stepList_->count() > 0) stepList_->setCurrentRow(want);
    backBtn_->setEnabled(!script_.steps.empty());
    deleteBtn_->setEnabled(!editing_.isEmpty());
    applyBtn_->setEnabled(!editing_.isEmpty());
    loading_ = wasLoading;
}

void MechWizardDialog::refreshFieldsForCategory() {
    heading_->setText(QStringLiteral("Step %1 — %2")
                          .arg(script_.steps.size() + 1)
                          .arg(categoryName(category_)));
    hint_->setText(categoryHint(category_));

    const PartKind kind = categoryKind(category_);
    const MechStepRole role = mechStepRole(kind);

    const bool isRun = role == MechStepRole::Run;
    const bool isEnd = role == MechStepRole::End;
    const bool isNode = role == MechStepRole::Node;
    const bool isFitting = role == MechStepRole::AtNode || role == MechStepRole::AlongRun;

    const auto show = [](QLabel* l, QWidget* w, bool on) {
        if (l) l->setVisible(on);
        if (w) w->setVisible(on);
    };
    show(lengthLabel_, lengthSpin_, isRun || isFitting);
    lengthLabel_->setText(isRun ? QStringLiteral("Length") : QStringLiteral("Its own length"));
    show(countLabel_, countSpin_, isRun);
    show(gaugeLabel_, gaugeCombo_, isRun);
    show(odLabel_, odSpin_, category_ == Category::Cable);
    show(waysLabel_, waysSpin_, isEnd);
    show(contactLabel_, contactCombo_, isEnd);
    // A node is a place, not a thing: it has no part, no length and no size of its own.
    partCombo_->setVisible(!isNode);

    // WHAT THE NAME FIELD IS CALLED. On an end - a connector, a terminal - and at a node, the answer is
    // a DESIGNATOR: P1, E7, BR1, the reference the wire list and the schematic know the part by. On a
    // length of cable or a boot it is a caption, and "Called" is the honest word for that.
    // A RUN IS DESIGNATED TOO. W1, W2 - the reference the wire list and the schematic know the run by,
    // exactly as P1 is for a connector. It is only a fitting - a boot, a label, a length of heatshrink -
    // that is CALLED something, because there the name really is a caption.
    if (nameLabel_)
        nameLabel_->setText((isEnd || isNode || isRun) ? QStringLiteral("Designator")
                                                       : QStringLiteral("Called"));
    if (nameEdit_)
        nameEdit_->setPlaceholderText(isEnd ? QStringLiteral("P1")
                                            : (isNode ? QStringLiteral("BR1")
                                                      : (isRun ? nextRunDesignator()
                                                               : QStringLiteral("what the drawing "
                                                                                "calls it"))));

    // THE NEXT W NUMBER, OFFERED RATHER THAN ASKED FOR. A run's designator is a number in a series and
    // not a decision, so the wizard fills it in; typing over it is still the answer where a drawing
    // letters its runs some other way. Only ever into an EMPTY field of a NEW step - filling it while a
    // step is being edited would rename a run somebody had already named.
    if (isRun && nameEdit_ && editing_.isEmpty() && nameEdit_->text().trimmed().isEmpty()) {
        const bool was = loading_;
        loading_ = true;
        nameEdit_->setText(nextRunDesignator());
        loading_ = was;
    }

    // AND WHETHER IT CAN BE SHOWN. A run between two parts is already identified by its ends, so the
    // box is greyed rather than quietly ignored: a tick that does nothing is worse than no tick.
    if (showNameCheck_) {
        showNameCheck_->setVisible(isRun);
        const bool between = runIsBetweenTwoParts();
        showNameCheck_->setEnabled(!between);
        showNameCheck_->setText(between
                                    ? QStringLiteral("Show the designator on the drawing  "
                                                     "(not for a run between two parts)")
                                    : QStringLiteral("Show the designator on the drawing"));
    }

    // WIRE DESIGNATORS, only where the harness starts open - see startsOpen(). Hidden rather than
    // greyed, because on a run out of a connector the wires already have names and the field would be
    // inviting somebody to give them second ones.
    const bool openStart = startsOpen();
    show(desigLabel_, desigEdit_, openStart);

    // The run-only controls.
    show(groupsLabel_, nullptr, isRun);
    if (groupList_) groupList_->parentWidget()->setVisible(isRun);
    if (continueCheck_) continueCheck_->setVisible(isRun);
    if (breakCheck_) breakCheck_->setVisible(isRun);

    // A node is not a bought part, so there is nothing to balloon and nothing to look up.
    if (balloonCheck_) balloonCheck_->setVisible(!isNode);

    // The groups this run is carrying so far.
    if (groupList_) {
        groupList_->clear();
        for (const MechConductorGroup& g : pending_) {
            QString label = QStringLiteral("%1 × %2").arg(g.count).arg(
                g.gauge.isEmpty() ? QStringLiteral("(no gauge)") : g.gauge);
            if (!g.name.isEmpty()) label += QStringLiteral("  %1").arg(g.name);
            new QListWidgetItem(label, groupList_);
        }
    }
    // The count/gauge fields are the ONE-group shorthand, so they are only meaningful while no groups
    // have been added. Said in the label rather than by disabling them, because they are also how a
    // group is typed before being added.
    if (countLabel_)
        countLabel_->setText(pending_.empty() ? QStringLiteral("Conductors")
                                              : QStringLiteral("Another group"));

    // THE BALLOON HABIT, per category. Not asked again every step: a shop balloons its connectors and
    // does not balloon its ties, and that is a standing answer rather than a fresh decision.
    if (balloonCheck_ && editing_.isEmpty()) {
        const bool remembered =
            balloonByCategory_.value(static_cast<int>(category_), true);
        const bool was = loading_;
        loading_ = true;
        balloonCheck_->setChecked(remembered);
        loading_ = was;
    }

    refreshPartCombo();
    // The row for the step being described names its category, so it moves with the palette.
    refreshStepList();
}

namespace {

// Select the entry carrying this id. Falls back to the first row - the Default - when the id is not in
// the list, which is the honest answer for a part whose library has been disconnected since.
void selectByData(QComboBox* box, const QString& id) {
    if (!box) return;
    for (int i = 0; i < box->count(); ++i) {
        if (box->itemData(i).toString() == id) {
            box->setCurrentIndex(i);
            return;
        }
    }
    box->setCurrentIndex(0);
}

// A part as a person would recognise it: what it is, and its part number when it has one.
QString partLabelFor(const LibraryPart& p) {
    QString label = p.description.trimmed();
    if (label.isEmpty()) label = p.mfrPn.trimmed();
    if (label.isEmpty()) label = p.id;
    if (!p.mfrPn.trimmed().isEmpty() && label != p.mfrPn.trimmed())
        label += QStringLiteral("  (%1)").arg(p.mfrPn.trimmed());
    return label;
}

} // namespace

bool MechWizardDialog::choosePart(const QString& libPartId) {
    for (int i = 0; i < partCombo_->count(); ++i) {
        if (partCombo_->itemData(i).toString() != libPartId) continue;
        partCombo_->setCurrentIndex(i);      // fires the handler, exactly as clicking it does
        return true;
    }
    return false;
}

bool MechWizardDialog::chooseContact(const QString& contactId) {
    for (int i = 0; i < contactCombo_->count(); ++i) {
        if (contactCombo_->itemData(i).toString() != contactId) continue;
        contactCombo_->setCurrentIndex(i);
        return true;
    }
    return false;
}

QString MechWizardDialog::chosenPart() const { return partCombo_->currentData().toString(); }
QString MechWizardDialog::chosenContact() const { return contactCombo_->currentData().toString(); }

void MechWizardDialog::refreshPartCombo() {
    const PartKind kind = categoryKind(category_);

    // REBUILDING A COMBO FIRES ITS OWN currentIndexChanged, and that handler reads the fields - so the
    // guard goes round the rebuild. Saved and restored rather than set-then-cleared, because this is
    // called from inside refreshFieldsForCategory, which may itself be loading a step.
    const bool wasLoading = loading_;
    loading_ = true;

    // WHAT WAS CHOSEN IS KEPT. The list is rebuilt whenever the category changes, and a rebuild that
    // forgot the selection would undo a choice the user had already made - which is exactly the bug
    // this function used to cause when it was called from its own handler.
    const QString had = partCombo_->currentData().toString();

    partCombo_->clear();
    // THE DEFAULT, FIRST, FOR EVERY CATEGORY. Choosing nothing is a real answer and not a blank: the
    // step is drawn from the office's own standard model for that kind of thing, which is how a
    // harness gets described before anybody has settled on part numbers.
    partCombo_->addItem(QStringLiteral("Default (the standard drawing for this kind)"), QString());
    for (const LibraryPart* p : catalog_.ofKind(kind)) {
        if (!p) continue;
        partCombo_->addItem(partLabelFor(*p), p->id);
    }
    selectByData(partCombo_, had);

    loading_ = wasLoading;
    refreshContactCombo();
}

void MechWizardDialog::refreshContactCombo() {
    const bool wasLoading = loading_;
    loading_ = true;

    const QString had = contactCombo_->currentData().toString();
    contactCombo_->clear();
    contactCombo_->addItem(QStringLiteral("Default (none chosen)"), QString());

    // The contacts, for an end. Only those tied to the chosen connector when one is chosen, because a
    // connector's own library entry already says which contacts go in it - and offering every contact
    // in the library is how the wrong one gets picked.
    if (mechStepRole(categoryKind(category_)) == MechStepRole::End) {
        const QString chosen = partCombo_->currentData().toString();
        const QStringList tied =
            chosen.isEmpty() ? QStringList() : tiedContactsOf(catalog_.active(), chosen);
        for (const LibraryPart* p : catalog_.ofKind(PartKind::ContactPin)) {
            if (!p) continue;
            if (!tied.isEmpty() && !tied.contains(p->id)) continue;
            contactCombo_->addItem(partLabelFor(*p), p->id);
        }
    }
    selectByData(contactCombo_, had);

    loading_ = wasLoading;
}

void MechWizardDialog::refreshPreview() {
    // THE STEP BEING DESCRIBED IS IN THE PREVIEW - see previewScript. The preview used to show only
    // what had been committed, which meant choosing a connector from the library changed nothing on
    // screen until Next was pressed: the one question a preview is asked is what the choice just made
    // looks like, and it could not answer it.
    const MechCompiled c = compileMechScript(previewScript());
    const std::vector<Entity> ents = buildMechanicalHarness(c.def, &catalog_);
    preview_->setEntities(ents);
    summary_->setText(script_.steps.empty()
                          ? QStringLiteral("Pick what goes on the first end, then press Next.")
                          : QStringLiteral("%1 step(s) described, %2 object(s) drawn.")
                                .arg(script_.steps.size())
                                .arg(ents.size()));
}

void MechWizardDialog::refreshIssues() {
    // THE CHECKS COVER THE STEP BEING DESCRIBED TOO, not only what has been committed. Comparing a
    // contact's gauge range against the conductor it is about to be crimped onto is worth most BEFORE
    // the step is added - which is the whole point of checking neighbours rather than suggesting steps.
    //
    // issues() itself stays on the committed script: it answers "what is wrong with this harness", and
    // a caller asking that should not be told about a half-typed step.
    const MechBuildScript shown = previewScript();
    const MechCompiled compiled = compileMechScript(shown);
    const auto found = mechScriptIssues(shown, compiled, &catalog_);
    if (found.empty()) {
        issueLabel_->setText(QStringLiteral(
            "<span style='color:#5fc294;'>Nothing to report.</span> Where two parts state their sizes, "
            "they have been compared."));
        return;
    }
    // VIOLATIONS FIRST, AND KEPT APART FROM GAPS. A figure that is missing and a figure that is wrong
    // are different news, and running them together trains people to ignore both.
    QString html;
    for (const MechStepIssue& i : found) {
        if (i.kind != MechIssueKind::Violation) continue;
        html += QStringLiteral("<div style='color:#f2766b;'>⚠ %1 %2</div>")
                    .arg(i.stepName.isEmpty() ? QString() : i.stepName + QStringLiteral(":"),
                         i.message.toHtmlEscaped());
    }
    for (const MechStepIssue& i : found) {
        if (i.kind == MechIssueKind::Violation) continue;
        html += QStringLiteral("<div style='color:#9aa7b8;'>%1 %2</div>")
                    .arg(i.stepName.isEmpty() ? QString() : i.stepName + QStringLiteral(":"),
                         i.message.toHtmlEscaped());
    }
    issueLabel_->setText(html);
}

void MechWizardDialog::place() {
    if (!editor_) return;
    const MechCompiled c = compileMechScript(script_);
    const std::vector<Entity> ents = buildMechanicalHarness(c.def, &catalog_);
    if (ents.empty()) {
        summary_->setText(QStringLiteral("Nothing to place yet."));
        return;
    }
    editor_->placeEntities(ents);
    summary_->setText(QStringLiteral("%1 object(s) ready — click on the sheet to put them down.")
                          .arg(ents.size()));
}

} // namespace Edraw_Desktop
