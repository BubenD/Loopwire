#include "app/HarnessStepsDialog.h"

#include "app/DrawingPreview.h"
#include "app/Editor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace Edraw_Desktop {

namespace {

// The cross-sections a harness is actually built from, smallest first. Typed in as well when the one
// wanted is not here - the field is editable - because a list is a convenience and not a rule.
const char* const kGauges[] = {"0.05", "0.08", "0.14", "0.22", "0.34", "0.5",
                               "0.75", "1.0",  "1.5",  "2.5",  "4.0",  "6.0"};

// What each step is called, and the question it asks. The numbering is the build sheet's.
struct StepText {
    const char* title;
    const char* hint;
};

StepText textFor(HarnessStepsDialog::Step s) {
    using Step = HarnessStepsDialog::Step;
    switch (s) {
    case Step::StartEnd:
        return {"1 — the end you start from",
                "What is on the end in your hand: leads left open, a connector (with a cap or "
                "without), or a terminal."};
    case Step::StartDress:
        return {"2 — backshell, and the wires without jacket",
                "What dresses that end, and how far back the conductors run bare before anything "
                "covers them."};
    case Step::StartTransition:
        return {"3 — transition heatshrink",
                "Over the step from the cable to the conductors coming out of it. A run of loose "
                "wires has no transition to cover."};
    case Step::MainRun:
        return {"4 — the main run",
                "What runs from that end: a cable, or so many loose conductors - and how long it is."};
    case Step::MainOver:
        return {"5 — over the main run",
                "A braided shield, heatshrink, either, or both - and how much of the run each one "
                "covers."};
    case Step::Split:
        return {"6 — the split",
                "How many ways the harness divides, and what the split itself is made of."};
    case Step::BranchRun:
        return {"7 — this branch",
                "Which of the conductors go this way, how long the branch is, and which way it "
                "leaves the split."};
    case Step::BranchOver:
        return {"8 — over this branch", "A braided shield, heatshrink, either, or both."};
    case Step::BranchTransition:
        return {"9 — transition heatshrink", "At the far end of the branch, where the cable opens up."};
    case Step::BranchDress:
        return {"10 — backshell, and the wires without jacket",
                "What dresses this branch's end, and how far back the conductors run bare."};
    case Step::BranchEnd:
        return {"11 — the end it finishes at",
                "Open leads, a connector (with a cap or without), or a terminal."};
    }
    return {"", ""};
}

// The parts of a kind, for a step to choose from. `needsDrawing` marks the ones with no mechanical
// drawing of their own: for a connector or a backshell that is what will be drawn, so a part without one
// says so in the list rather than quietly coming out as a plain body.
void fillParts(QComboBox* box, const PartsCatalog& catalog, PartKind kind, bool needsDrawing = false) {
    if (!box) return;
    const QString had = box->currentData().toString();
    box->clear();
    box->addItem(QStringLiteral("— typed in below —"), QString());
    for (const LibraryPart* p : catalog.ofKind(kind)) {
        const bool drawn = p->hasMechanical();
        box->addItem(needsDrawing && !drawn
                         ? QStringLiteral("%1  (no mechanical drawing)").arg(p->title())
                         : p->title(),
                     p->id);
    }
    const int at = box->findData(had);
    box->setCurrentIndex(at >= 0 ? at : 0);
}

QDoubleSpinBox* mmSpin(double max = 5000.0, double step = 1.0) {
    auto* s = new QDoubleSpinBox;
    s->setRange(0.0, max);
    s->setDecimals(1);
    s->setSingleStep(step);
    s->setSuffix(QStringLiteral(" mm"));
    s->setKeyboardTracking(false);
    return s;
}

} // namespace

namespace {

// The model's own accessors answer with const pointers, which is right for reading a definition and no
// use for building one. These are the same lookups, mutable.
MechNode* nodeIn(MechHarnessDef& def, const QString& id) {
    for (MechNode& n : def.nodes)
        if (n.id == id) return &n;
    return nullptr;
}
MechSegment* segmentIn(MechHarnessDef& def, const QString& id) {
    for (MechSegment& s : def.segments)
        if (s.id == id) return &s;
    return nullptr;
}

} // namespace

HarnessStepsDialog::HarnessStepsDialog(Editor* editor, QWidget* parent)
    : QDialog(parent), editor_(editor) {
    setWindowTitle(QStringLiteral("Harness build wizard"));
    setModal(false);
    catalog_.openRemembered();

    // A harness with one end, one run and one end at the other side of it: the smallest thing the
    // steps describe, so the first page has something to be about rather than nothing.
    MechNode start;
    start.id = QStringLiteral("n1");
    start.kind = MechNodeKind::Connector;
    start.name = QStringLiteral("P1");
    start.pinCount = 4;
    def_.nodes.push_back(start);
    MechSegment trunk;
    trunk.id = QStringLiteral("main");
    trunk.fromNode = QStringLiteral("n1");
    trunk.toNode = QStringLiteral("split");
    trunk.lengthMm = 300.0;
    def_.segments.push_back(trunk);
    MechNode split;
    split.id = QStringLiteral("split");
    split.kind = MechNodeKind::Breakout;
    split.name = QStringLiteral("BR1");
    split.parentSegment = QStringLiteral("main");
    def_.nodes.push_back(split);
    // To scale in both directions: a width taken off this drawing is the width of the thing. The
    // builder proper can exaggerate the bodies to make a formboard sheet readable, and says so on the
    // drawing when it does; a harness being described for the first time is drawn as it is.
    def_.draw.diameterExaggeration = 1.0;
    def_.draw.scale = 1.0;
    setConductors(4, QStringLiteral("0.5"));
    setBranchCount(2);

    buildUi();
    rebuildSteps();
    goToStep(0);
}

// ---------------------------------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------------------------------

void HarnessStepsDialog::buildUi() {
    auto* outer = new QHBoxLayout(this);

    // The steps, in order, with the branches under their own headings.
    list_ = new QListWidget;
    list_->setMinimumWidth(260);
    list_->setMaximumWidth(320);
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (syncing_ || row < 0) return;
        const QVariant v = list_->item(row)->data(Qt::UserRole);
        if (!v.isValid()) return;   // a heading, not a step
        goToStep(v.toInt());
    });
    outer->addWidget(list_);

    auto* right = new QVBoxLayout;
    heading_ = new QLabel;
    QFont hf = heading_->font();
    hf.setPointSizeF(hf.pointSizeF() * 1.25);
    hf.setBold(true);
    heading_->setFont(hf);
    right->addWidget(heading_);
    hint_ = new QLabel;
    hint_->setWordWrap(true);
    right->addWidget(hint_);

    pages_ = new QStackedWidget;
    pages_->addWidget(buildStartEndPage());     // index 0: an end (steps 1 and 11)
    pages_->addWidget(buildDressPage());        // 1: backshell / open wires (2 and 10)
    pages_->addWidget(buildTransitionPage());   // 2: transition heatshrink (3 and 9)
    pages_->addWidget(buildRunPage());          // 3: a run (4 and 7)
    pages_->addWidget(buildOverPage());         // 4: over a run (5 and 8)
    pages_->addWidget(buildSplitPage());        // 5: the split (6)
    right->addWidget(pages_);

    preview_ = new DrawingPreview(QStringLiteral("The harness so far"));
    preview_->setMinimumHeight(220);
    right->addWidget(preview_, 1);

    summary_ = new QLabel;
    summary_->setWordWrap(true);
    right->addWidget(summary_);

    auto* buttons = new QHBoxLayout;
    backBtn_ = new QPushButton(QStringLiteral("‹ Back"));
    connect(backBtn_, &QPushButton::clicked, this, &HarnessStepsDialog::back);
    buttons->addWidget(backBtn_);
    nextBtn_ = new QPushButton(QStringLiteral("Next ›"));
    connect(nextBtn_, &QPushButton::clicked, this, &HarnessStepsDialog::next);
    buttons->addWidget(nextBtn_);
    buttons->addStretch(1);
    auto* placeBtn = new QPushButton(QStringLiteral("Place the drawing"));
    connect(placeBtn, &QPushButton::clicked, this, &HarnessStepsDialog::place);
    buttons->addWidget(placeBtn);
    auto* closeBtn = new QPushButton(QStringLiteral("Close"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    buttons->addWidget(closeBtn);
    right->addLayout(buttons);

    outer->addLayout(right, 1);
    resize(1100, 780);
}

// Every page writes straight back into the definition and redraws, so the harness is built in front of
// you rather than after a press of something.
#define ON_CHANGE(w, sig) connect(w, sig, this, [this] { if (!syncing_) apply(); })

QWidget* HarnessStepsDialog::buildStartEndPage() {
    auto* page = new QWidget;
    auto* form = new QFormLayout(page);
    endKind_ = new QComboBox;
    endKind_->setObjectName(QStringLiteral("endKind"));
    endKind_->addItem(QStringLiteral("Open leads"));
    endKind_->addItem(QStringLiteral("Connector"));
    endKind_->addItem(QStringLiteral("Other terminal"));
    ON_CHANGE(endKind_, &QComboBox::currentIndexChanged);
    form->addRow(QStringLiteral("What is on this end"), endKind_);

    endName_ = new QLineEdit;

    endName_->setObjectName(QStringLiteral("endName"));
    ON_CHANGE(endName_, &QLineEdit::editingFinished);
    form->addRow(QStringLiteral("Called"), endName_);

    endPart_ = new QComboBox;

    endPart_->setObjectName(QStringLiteral("endPart"));
    ON_CHANGE(endPart_, &QComboBox::currentIndexChanged);
    form->addRow(QStringLiteral("Part"), endPart_);

    endPins_ = new QSpinBox;

    endPins_->setObjectName(QStringLiteral("endPins"));
    endPins_->setRange(0, 512);
    endPins_->setKeyboardTracking(false);
    ON_CHANGE(endPins_, &QSpinBox::valueChanged);
    form->addRow(QStringLiteral("Ways"), endPins_);

    endBodyW_ = mmSpin(500.0);

    endBodyW_->setObjectName(QStringLiteral("endBodyW"));
    ON_CHANGE(endBodyW_, &QDoubleSpinBox::valueChanged);
    form->addRow(QStringLiteral("Body, along the harness"), endBodyW_);
    endBodyH_ = mmSpin(500.0);
    endBodyH_->setObjectName(QStringLiteral("endBodyH"));
    ON_CHANGE(endBodyH_, &QDoubleSpinBox::valueChanged);
    form->addRow(QStringLiteral("Body, across it"), endBodyH_);

    endCap_ = new QCheckBox(QStringLiteral("With a protective cap"));

    endCap_->setObjectName(QStringLiteral("endCap"));
    ON_CHANGE(endCap_, &QCheckBox::toggled);
    form->addRow(QString(), endCap_);

    endLeadLen_ = mmSpin(2000.0);

    endLeadLen_->setObjectName(QStringLiteral("endLeadLen"));
    ON_CHANGE(endLeadLen_, &QDoubleSpinBox::valueChanged);
    form->addRow(QStringLiteral("Leads left open"), endLeadLen_);
    return page;
}

QWidget* HarnessStepsDialog::buildDressPage() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);

    auto* bs = new QGroupBox(QStringLiteral("Backshell"));
    auto* bsf = new QFormLayout(bs);
    dressBackshell_ = new QCheckBox(QStringLiteral("There is one"));
    dressBackshell_->setObjectName(QStringLiteral("backshellOn"));
    ON_CHANGE(dressBackshell_, &QCheckBox::toggled);
    bsf->addRow(QString(), dressBackshell_);
    dressBackshellPart_ = new QComboBox;
    dressBackshellPart_->setObjectName(QStringLiteral("backshellPart"));
    ON_CHANGE(dressBackshellPart_, &QComboBox::currentIndexChanged);
    bsf->addRow(QStringLiteral("Part"), dressBackshellPart_);
    dressBackshellLen_ = mmSpin(300.0);
    dressBackshellLen_->setObjectName(QStringLiteral("backshellLength"));
    ON_CHANGE(dressBackshellLen_, &QDoubleSpinBox::valueChanged);
    bsf->addRow(QStringLiteral("Length"), dressBackshellLen_);
    dressBackshellOd_ = mmSpin(200.0, 0.5);
    dressBackshellOd_->setObjectName(QStringLiteral("backshellOd"));
    ON_CHANGE(dressBackshellOd_, &QDoubleSpinBox::valueChanged);
    bsf->addRow(QStringLiteral("Outside diameter"), dressBackshellOd_);
    v->addWidget(bs);

    auto* bt = new QGroupBox(QStringLiteral("Boot behind it"));
    auto* btf = new QFormLayout(bt);
    dressBoot_ = new QCheckBox(QStringLiteral("There is one"));
    dressBoot_->setObjectName(QStringLiteral("bootOn"));
    ON_CHANGE(dressBoot_, &QCheckBox::toggled);
    btf->addRow(QString(), dressBoot_);
    dressBootPart_ = new QComboBox;
    dressBootPart_->setObjectName(QStringLiteral("bootPart"));
    ON_CHANGE(dressBootPart_, &QComboBox::currentIndexChanged);
    btf->addRow(QStringLiteral("Part"), dressBootPart_);
    dressBootLen_ = mmSpin(300.0);
    dressBootLen_->setObjectName(QStringLiteral("bootLength"));
    ON_CHANGE(dressBootLen_, &QDoubleSpinBox::valueChanged);
    btf->addRow(QStringLiteral("Length"), dressBootLen_);
    dressBootOd_ = mmSpin(200.0, 0.5);
    dressBootOd_->setObjectName(QStringLiteral("bootOd"));
    ON_CHANGE(dressBootOd_, &QDoubleSpinBox::valueChanged);
    btf->addRow(QStringLiteral("Outside diameter"), dressBootOd_);
    v->addWidget(bt);

    auto* op = new QGroupBox(QStringLiteral("The wires without jacket"));
    auto* opf = new QFormLayout(op);
    dressOpen_ = mmSpin(500.0);
    dressOpen_->setObjectName(QStringLiteral("openLength"));
    ON_CHANGE(dressOpen_, &QDoubleSpinBox::valueChanged);
    opf->addRow(QStringLiteral("They run bare for"), dressOpen_);
    auto* note = new QLabel(QStringLiteral(
        "Behind the bodies and before anything covers them, so they can fan out to the contacts and "
        "be inspected. It is dimensioned on the drawing."));
    note->setWordWrap(true);
    opf->addRow(QString(), note);
    v->addWidget(op);
    v->addStretch(1);
    return page;
}

QWidget* HarnessStepsDialog::buildTransitionPage() {
    auto* page = new QWidget;
    auto* form = new QFormLayout(page);
    transOn_ = new QCheckBox(QStringLiteral("There is heatshrink over the transition"));
    transOn_->setObjectName(QStringLiteral("transitionOn"));
    ON_CHANGE(transOn_, &QCheckBox::toggled);
    form->addRow(QString(), transOn_);
    transPart_ = new QComboBox;
    transPart_->setObjectName(QStringLiteral("transitionPart"));
    ON_CHANGE(transPart_, &QComboBox::currentIndexChanged);
    form->addRow(QStringLiteral("Part"), transPart_);
    transLen_ = mmSpin(300.0);
    transLen_->setObjectName(QStringLiteral("transitionLength"));
    ON_CHANGE(transLen_, &QDoubleSpinBox::valueChanged);
    form->addRow(QStringLiteral("Length"), transLen_);
    transOd_ = mmSpin(200.0, 0.5);
    transOd_->setObjectName(QStringLiteral("transitionOd"));
    ON_CHANGE(transOd_, &QDoubleSpinBox::valueChanged);
    form->addRow(QStringLiteral("Outside diameter, shrunk"), transOd_);
    transStrip_ = mmSpin(500.0);
    transStrip_->setObjectName(QStringLiteral("jacketCutBack"));
    ON_CHANGE(transStrip_, &QDoubleSpinBox::valueChanged);
    form->addRow(QStringLiteral("Jacket cut back by"), transStrip_);
    auto* note = new QLabel(QStringLiteral(
        "The jacket stops there and the conductors carry on, so the run narrows from the cable to "
        "the bundle inside it. The heatshrink is what covers that step."));
    note->setWordWrap(true);
    form->addRow(QString(), note);
    return page;
}

QWidget* HarnessStepsDialog::buildRunPage() {
    auto* page = new QWidget;
    auto* form = new QFormLayout(page);
    runMake_ = new QComboBox;
    runMake_->setObjectName(QStringLiteral("runMake"));
    runMake_->addItem(QStringLiteral("Loose conductors"));
    runMake_->addItem(QStringLiteral("A cable"));
    ON_CHANGE(runMake_, &QComboBox::currentIndexChanged);
    form->addRow(QStringLiteral("What runs here"), runMake_);

    runCount_ = new QSpinBox;

    runCount_->setObjectName(QStringLiteral("runCount"));
    runCount_->setRange(1, 512);
    runCount_->setKeyboardTracking(false);
    ON_CHANGE(runCount_, &QSpinBox::valueChanged);
    form->addRow(QStringLiteral("How many conductors"), runCount_);

    runGauge_ = new QComboBox;

    runGauge_->setObjectName(QStringLiteral("runGauge"));
    runGauge_->setEditable(true);
    for (const char* g : kGauges) runGauge_->addItem(QString::fromLatin1(g));
    ON_CHANGE(runGauge_, &QComboBox::currentTextChanged);
    form->addRow(QStringLiteral("Cross-section, mm²"), runGauge_);

    runCablePart_ = new QComboBox;

    runCablePart_->setObjectName(QStringLiteral("runCablePart"));
    ON_CHANGE(runCablePart_, &QComboBox::currentIndexChanged);
    form->addRow(QStringLiteral("Cable"), runCablePart_);
    runJacket_ = mmSpin(200.0, 0.5);
    runJacket_->setObjectName(QStringLiteral("runJacket"));
    ON_CHANGE(runJacket_, &QDoubleSpinBox::valueChanged);
    form->addRow(QStringLiteral("Over the jacket"), runJacket_);

    runLength_ = mmSpin(20000.0, 10.0);

    runLength_->setObjectName(QStringLiteral("runLength"));
    ON_CHANGE(runLength_, &QDoubleSpinBox::valueChanged);
    form->addRow(QStringLiteral("Length"), runLength_);

    runAngle_ = new QDoubleSpinBox;

    runAngle_->setObjectName(QStringLiteral("runAngle"));
    runAngle_->setRange(-180.0, 180.0);
    runAngle_->setDecimals(0);
    runAngle_->setSuffix(QStringLiteral("°"));
    runAngle_->setKeyboardTracking(false);
    ON_CHANGE(runAngle_, &QDoubleSpinBox::valueChanged);
    form->addRow(QStringLiteral("Leaves the split at"), runAngle_);

    runBundle_ = new QLabel;
    runBundle_->setWordWrap(true);
    form->addRow(QStringLiteral("Drawn at"), runBundle_);
    return page;
}

QWidget* HarnessStepsDialog::buildOverPage() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);

    auto* br = new QGroupBox(QStringLiteral("Braided shield"));
    auto* brf = new QFormLayout(br);
    overBraid_ = new QCheckBox(QStringLiteral("There is braid over this run"));
    overBraid_->setObjectName(QStringLiteral("braidOn"));
    ON_CHANGE(overBraid_, &QCheckBox::toggled);
    brf->addRow(QString(), overBraid_);
    overBraidPart_ = new QComboBox;
    overBraidPart_->setObjectName(QStringLiteral("braidPart"));
    ON_CHANGE(overBraidPart_, &QComboBox::currentIndexChanged);
    brf->addRow(QStringLiteral("Part"), overBraidPart_);
    overBraidFrom_ = mmSpin(20000.0, 10.0);
    overBraidFrom_->setObjectName(QStringLiteral("braidFrom"));
    ON_CHANGE(overBraidFrom_, &QDoubleSpinBox::valueChanged);
    brf->addRow(QStringLiteral("Starts at"), overBraidFrom_);
    overBraidLen_ = mmSpin(20000.0, 10.0);
    overBraidLen_->setObjectName(QStringLiteral("braidLength"));
    ON_CHANGE(overBraidLen_, &QDoubleSpinBox::valueChanged);
    brf->addRow(QStringLiteral("Covers (0 = all of it)"), overBraidLen_);
    v->addWidget(br);

    auto* hs = new QGroupBox(QStringLiteral("Heatshrink"));
    auto* hsf = new QFormLayout(hs);
    overShrink_ = new QCheckBox(QStringLiteral("There is heatshrink over this run"));
    overShrink_->setObjectName(QStringLiteral("shrinkOn"));
    ON_CHANGE(overShrink_, &QCheckBox::toggled);
    hsf->addRow(QString(), overShrink_);
    overShrinkPart_ = new QComboBox;
    overShrinkPart_->setObjectName(QStringLiteral("shrinkPart"));
    ON_CHANGE(overShrinkPart_, &QComboBox::currentIndexChanged);
    hsf->addRow(QStringLiteral("Part"), overShrinkPart_);
    overShrinkFrom_ = mmSpin(20000.0, 10.0);
    overShrinkFrom_->setObjectName(QStringLiteral("shrinkFrom"));
    ON_CHANGE(overShrinkFrom_, &QDoubleSpinBox::valueChanged);
    hsf->addRow(QStringLiteral("Starts at"), overShrinkFrom_);
    overShrinkLen_ = mmSpin(20000.0, 10.0);
    overShrinkLen_->setObjectName(QStringLiteral("shrinkLength"));
    ON_CHANGE(overShrinkLen_, &QDoubleSpinBox::valueChanged);
    hsf->addRow(QStringLiteral("Covers (0 = all of it)"), overShrinkLen_);
    v->addWidget(hs);
    v->addStretch(1);
    return page;
}

QWidget* HarnessStepsDialog::buildSplitPage() {
    auto* page = new QWidget;
    auto* form = new QFormLayout(page);
    splitCount_ = new QSpinBox;
    splitCount_->setObjectName(QStringLiteral("branchCount"));
    splitCount_->setRange(1, 12);
    splitCount_->setKeyboardTracking(false);
    ON_CHANGE(splitCount_, &QSpinBox::valueChanged);
    form->addRow(QStringLiteral("How many branches"), splitCount_);

    splitKind_ = new QComboBox;

    splitKind_->setObjectName(QStringLiteral("splitKind"));
    splitKind_->addItem(QStringLiteral("Nothing — the bundle simply divides"));
    splitKind_->addItem(QStringLiteral("Heatshrink over the split"));
    splitKind_->addItem(QStringLiteral("Y-split boot"));
    ON_CHANGE(splitKind_, &QComboBox::currentIndexChanged);
    form->addRow(QStringLiteral("The split is"), splitKind_);

    splitPart_ = new QComboBox;

    splitPart_->setObjectName(QStringLiteral("splitPart"));
    ON_CHANGE(splitPart_, &QComboBox::currentIndexChanged);
    form->addRow(QStringLiteral("Part"), splitPart_);
    splitLen_ = mmSpin(300.0);
    splitLen_->setObjectName(QStringLiteral("splitLength"));
    ON_CHANGE(splitLen_, &QDoubleSpinBox::valueChanged);
    form->addRow(QStringLiteral("Length"), splitLen_);
    splitOd_ = mmSpin(200.0, 0.5);
    splitOd_->setObjectName(QStringLiteral("splitOd"));
    ON_CHANGE(splitOd_, &QDoubleSpinBox::valueChanged);
    form->addRow(QStringLiteral("Outside diameter"), splitOd_);

    auto* note = new QLabel(QStringLiteral(
        "The branches leave through a funnel: the run stops short and is closed off, and each branch "
        "starts on its own stretch of the throat. Each branch has its own steps 7 to 11 below."));
    note->setWordWrap(true);
    form->addRow(QString(), note);
    return page;
}

#undef ON_CHANGE

// ---------------------------------------------------------------------------------------------------
// The steps
// ---------------------------------------------------------------------------------------------------

void HarnessStepsDialog::rebuildSteps() {
    steps_.clear();
    steps_.push_back({Step::StartEnd, -1});
    steps_.push_back({Step::StartDress, -1});
    steps_.push_back({Step::StartTransition, -1});
    steps_.push_back({Step::MainRun, -1});
    steps_.push_back({Step::MainOver, -1});
    steps_.push_back({Step::Split, -1});
    for (int b = 0; b < branchCount(); ++b) {
        steps_.push_back({Step::BranchRun, b});
        steps_.push_back({Step::BranchOver, b});
        steps_.push_back({Step::BranchTransition, b});
        steps_.push_back({Step::BranchDress, b});
        steps_.push_back({Step::BranchEnd, b});
    }
    if (current_ >= static_cast<int>(steps_.size()))
        current_ = static_cast<int>(steps_.size()) - 1;
    refreshList();
}

void HarnessStepsDialog::refreshList() {
    if (!list_) return;
    const bool was = syncing_;
    syncing_ = true;
    list_->clear();
    int lastBranch = -1;
    for (size_t i = 0; i < steps_.size(); ++i) {
        const StepRef& ref = steps_[i];
        if (ref.branch >= 0 && ref.branch != lastBranch) {
            lastBranch = ref.branch;
            auto* head = new QListWidgetItem(QStringLiteral("Branch %1").arg(ref.branch + 1));
            QFont f = head->font();
            f.setBold(true);
            head->setFont(f);
            head->setFlags(Qt::ItemIsEnabled);   // a heading is not a step
            list_->addItem(head);
        }
        auto* item = new QListWidgetItem(QStringLiteral("   %1").arg(textFor(ref.step).title));
        item->setData(Qt::UserRole, static_cast<int>(i));
        list_->addItem(item);
        if (static_cast<int>(i) == current_) list_->setCurrentItem(item);
    }
    syncing_ = was;
}

HarnessStepsDialog::StepRef HarnessStepsDialog::stepAt(int index) const {
    if (index < 0 || index >= static_cast<int>(steps_.size())) return {Step::StartEnd, -1};
    return steps_[static_cast<size_t>(index)];
}

void HarnessStepsDialog::goToStep(int index) {
    if (steps_.empty()) return;
    current_ = std::clamp(index, 0, static_cast<int>(steps_.size()) - 1);
    showStep();
}

void HarnessStepsDialog::next() {
    // Moving on is taking the step as answered - by its defaults, if they were not changed - so the
    // drawing starts here too.
    started_ = true;
    goToStep(current_ + 1);
}
void HarnessStepsDialog::back() { goToStep(current_ - 1); }

// ---------------------------------------------------------------------------------------------------
// The definition, by step
// ---------------------------------------------------------------------------------------------------

int HarnessStepsDialog::branchCount() const {
    int n = 0;
    for (const MechSegment& s : def_.segments)
        if (s.fromNode == QLatin1String("split")) ++n;   // one spread leg per branch
    return n;
}

MechSegment* HarnessStepsDialog::branchRun(int branch) {
    // The straight leg: what the branch's length and its coverings are about. The spread leg beside it
    // is the short piece that turns it off the trunk.
    return segmentIn(def_, QStringLiteral("br%1").arg(branch + 1));
}

MechSegment* HarnessStepsDialog::branchSpread(int branch) {
    return segmentIn(def_, QStringLiteral("sp%1").arg(branch + 1));
}

MechNode* HarnessStepsDialog::branchTip(int branch) {
    MechSegment* run = branchRun(branch);
    return run ? nodeIn(def_, run->toNode) : nullptr;
}

// Which way each branch leaves the split, for a fan of this many: symmetric about the trunk, and with
// the middle one carrying straight on when there is an odd number of them. Two branches make a Y; three
// make a Y with a straight through the middle of it, which is what a harness of three actually looks
// like and what the sample drawing draws.
double HarnessStepsDialog::branchAngleFor(int index, int count) {
    if (count <= 1) return 0.0;
    const double spread = 35.0;
    return spread - 2.0 * spread * index / (count - 1);
}

void HarnessStepsDialog::setBranchCount(int count) {
    count = std::clamp(count, 1, 12);
    // Away with the ones no longer wanted, from the end, so the branches already described keep their
    // figures rather than being renumbered under the person's hands.
    while (branchCount() > count) {
        const int last = branchCount() - 1;
        const QString sp = QStringLiteral("sp%1").arg(last + 1);
        const QString el = QStringLiteral("el%1").arg(last + 1);
        const QString br = QStringLiteral("br%1").arg(last + 1);
        const QString tip = QStringLiteral("tip%1").arg(last + 1);
        const int before = branchCount();
        def_.segments.erase(std::remove_if(def_.segments.begin(), def_.segments.end(),
                                           [&](const MechSegment& s) {
                                               return s.id == sp || s.id == br;
                                           }),
                            def_.segments.end());
        def_.nodes.erase(std::remove_if(def_.nodes.begin(), def_.nodes.end(),
                                        [&](const MechNode& n) {
                                            return n.id == el || n.id == tip;
                                        }),
                         def_.nodes.end());
        if (branchCount() >= before) break;   // nothing came off: stop rather than spin
    }
    while (branchCount() < count) {
        const int index = branchCount();
        // TWO legs, not one: a short piece out of the split at an angle, then a straight one parallel
        // to the trunk again. That is the shape a branch has - it comes off the trunk and then runs -
        // and a single leg at an angle draws a harness whose branches spread for ever.
        MechSegment spread;
        spread.id = QStringLiteral("sp%1").arg(index + 1);
        spread.fromNode = QStringLiteral("split");
        spread.toNode = QStringLiteral("el%1").arg(index + 1);
        spread.lengthMm = 50.0;
        def_.segments.push_back(spread);
        MechNode elbow;
        elbow.id = spread.toNode;
        elbow.kind = MechNodeKind::Breakout;      // a bend, not a junction: two legs meet here
        elbow.name = QStringLiteral("K%1").arg(index + 1);
        elbow.parentSegment = spread.id;
        elbow.angleDeg = 0.0;                     // set below, with the rest of the fan
        def_.nodes.push_back(elbow);
        MechSegment run;
        run.id = QStringLiteral("br%1").arg(index + 1);
        run.fromNode = elbow.id;
        run.toNode = QStringLiteral("tip%1").arg(index + 1);
        run.lengthMm = 150.0;
        def_.segments.push_back(run);
        MechNode tip;
        tip.id = run.toNode;
        tip.kind = MechNodeKind::Connector;
        tip.name = QStringLiteral("P%1").arg(index + 2);
        tip.pinCount = 4;
        tip.parentSegment = run.id;
        tip.angleDeg = 0.0;                       // the straight leg carries on the way the spread went
        def_.nodes.push_back(tip);
    }
    // The fan, set on every branch: the spread leg turns off the trunk, and the straight leg turns
    // back by the same amount so it runs parallel to it again.
    for (int b = 0; b < count; ++b) {
        const double angle = branchAngleFor(b, count);
        if (MechNode* elbow = nodeIn(def_, QStringLiteral("el%1").arg(b + 1))) elbow->angleDeg = angle;
        if (MechNode* tip = nodeIn(def_, QStringLiteral("tip%1").arg(b + 1))) tip->angleDeg = -angle;
    }
    // The conductors are shared out among the branches: whatever is not said, evenly.
    const int branches = branchCount();
    const int wires = static_cast<int>(def_.wires.size());
    int given = 0;
    for (int b = 0; b < branches; ++b) {
        MechSegment* run = branchRun(b);
        if (!run) continue;
        const int want = b == branches - 1 ? wires - given
                                          : std::max(1, wires / std::max(1, branches));
        QStringList ids;
        for (int k = 0; k < want && given + k < wires; ++k)
            ids << def_.wires[static_cast<size_t>(given + k)].id;
        given += ids.size();
        run->wireIds = ids;
        // Both legs of the branch carry the same conductors: they are one branch drawn in two pieces.
        if (MechSegment* spread = branchSpread(b)) spread->wireIds = ids;
    }
}

void HarnessStepsDialog::setConductors(int count, const QString& gauge) {
    count = std::max(1, count);
    def_.wires.clear();
    for (int i = 0; i < count; ++i) {
        MechWire w;
        w.id = QStringLiteral("w%1").arg(i + 1);
        w.label = w.id;
        w.gauge = gauge;
        def_.wires.push_back(w);
    }
    if (MechSegment* main = segmentIn(def_, QStringLiteral("main"))) {
        QStringList ids;
        for (const MechWire& w : def_.wires) ids << w.id;
        main->wireIds = ids;
    }
    setBranchCount(branchCount());   // share them out again
}

MechNode* HarnessStepsDialog::nodeForStep(const StepRef& ref) {
    switch (ref.step) {
    case Step::StartEnd:
    case Step::StartDress:
    case Step::StartTransition:
        return nodeIn(def_, QStringLiteral("n1"));
    case Step::Split:
        return nodeIn(def_, QStringLiteral("split"));
    case Step::BranchTransition:
    case Step::BranchDress:
    case Step::BranchEnd:
        return branchTip(ref.branch);
    default:
        return nullptr;
    }
}

MechSegment* HarnessStepsDialog::runForStep(const StepRef& ref) {
    switch (ref.step) {
    case Step::MainRun:
    case Step::MainOver:
    case Step::StartTransition:
        return segmentIn(def_, QStringLiteral("main"));
    case Step::BranchRun:
    case Step::BranchOver:
    case Step::BranchTransition:
        return branchRun(ref.branch);
    default:
        return nullptr;
    }
}

MechFitting* HarnessStepsDialog::fittingOn(std::vector<MechFitting>& list, PartKind kind, bool make) {
    for (MechFitting& f : list)
        if (f.kind == kind) return &f;
    if (!make) return nullptr;
    MechFitting f;
    f.kind = kind;
    f.name = partKindName(kind);
    list.push_back(f);
    return &list.back();
}

void HarnessStepsDialog::setFitting(std::vector<MechFitting>& list, PartKind kind, bool on) {
    if (on) {
        fittingOn(list, kind, true);
        return;
    }
    list.erase(std::remove_if(list.begin(), list.end(),
                              [kind](const MechFitting& f) { return f.kind == kind; }),
               list.end());
}

// ---------------------------------------------------------------------------------------------------
// Showing a step, and reading it back
// ---------------------------------------------------------------------------------------------------

void HarnessStepsDialog::showStep() {
    if (steps_.empty()) return;
    const StepRef ref = steps_[static_cast<size_t>(current_)];
    const StepText text = textFor(ref.step);
    if (heading_) heading_->setText(QString::fromLatin1(text.title));
    if (hint_) hint_->setText(QString::fromLatin1(text.hint));
    if (backBtn_) backBtn_->setEnabled(current_ > 0);
    if (nextBtn_) nextBtn_->setEnabled(current_ + 1 < static_cast<int>(steps_.size()));

    syncing_ = true;
    switch (ref.step) {
    case Step::StartEnd:
    case Step::BranchEnd: {
        pages_->setCurrentIndex(0);
        MechNode* n = nodeForStep(ref);
        fillParts(endPart_, catalog_,
                  n && n->kind == MechNodeKind::Connector ? PartKind::Connector : PartKind::Terminal,
                  /*needsDrawing=*/true);
        if (n) {
            const bool leads = n->kind == MechNodeKind::FreeEnd;
            const bool terminal = n->kind == MechNodeKind::Connector &&
                                  n->name.startsWith(QLatin1Char('T'));
            endKind_->setCurrentIndex(leads ? 0 : terminal ? 2 : 1);
            endName_->setText(n->name);
            const int at = endPart_->findData(n->libPart);
            endPart_->setCurrentIndex(at >= 0 ? at : 0);
            endPins_->setValue(n->pinCount);
            endBodyW_->setValue(n->bodyWidthMm);
            endBodyH_->setValue(n->bodyHeightMm);
            endCap_->setChecked(fittingOn(n->fittings, PartKind::Overmould, false) != nullptr);
            endLeadLen_->setValue(n->openLengthMm);
            const bool isConn = !leads;
            endPart_->setEnabled(isConn);
            endPins_->setEnabled(isConn);
            endBodyW_->setEnabled(isConn);
            endBodyH_->setEnabled(isConn);
            endCap_->setEnabled(isConn);
        }
        break;
    }
    case Step::StartDress:
    case Step::BranchDress: {
        pages_->setCurrentIndex(1);
        fillParts(dressBackshellPart_, catalog_, PartKind::Backshell, /*needsDrawing=*/true);
        // A boot is heatshrink: drawn from its figures, as a taper over what it goes on, so its own
        // picture is not wanted and not asked for.
        fillParts(dressBootPart_, catalog_, PartKind::HeatshrinkBoot);
        MechNode* n = nodeForStep(ref);
        if (n) {
            const MechFitting* bs = fittingOn(n->fittings, PartKind::Backshell, false);
            dressBackshell_->setChecked(bs != nullptr);
            if (bs) {
                const int at = dressBackshellPart_->findData(bs->libPart);
                dressBackshellPart_->setCurrentIndex(at >= 0 ? at : 0);
                dressBackshellLen_->setValue(bs->lengthMm);
                dressBackshellOd_->setValue(bs->odMm);
            }
            const MechFitting* bt = fittingOn(n->fittings, PartKind::HeatshrinkBoot, false);
            dressBoot_->setChecked(bt != nullptr);
            if (bt) {
                const int at = dressBootPart_->findData(bt->libPart);
                dressBootPart_->setCurrentIndex(at >= 0 ? at : 0);
                dressBootLen_->setValue(bt->lengthMm);
                dressBootOd_->setValue(bt->odMm);
            }
            dressOpen_->setValue(n->openLengthMm);
            for (QWidget* w : {static_cast<QWidget*>(dressBackshellPart_),
                               static_cast<QWidget*>(dressBackshellLen_),
                               static_cast<QWidget*>(dressBackshellOd_)})
                w->setEnabled(bs != nullptr);
            for (QWidget* w : {static_cast<QWidget*>(dressBootPart_),
                               static_cast<QWidget*>(dressBootLen_),
                               static_cast<QWidget*>(dressBootOd_)})
                w->setEnabled(bt != nullptr);
        }
        break;
    }
    case Step::StartTransition:
    case Step::BranchTransition: {
        pages_->setCurrentIndex(2);
        fillParts(transPart_, catalog_, PartKind::Heatshrink);
        MechNode* n = nodeForStep(ref);
        MechSegment* run = runForStep(ref);
        const bool atToEnd = ref.step == Step::BranchTransition;
        if (n) {
            const MechFitting* hs = fittingOn(n->fittings, PartKind::Heatshrink, false);
            transOn_->setChecked(hs != nullptr);
            if (hs) {
                const int at = transPart_->findData(hs->libPart);
                transPart_->setCurrentIndex(at >= 0 ? at : 0);
                transLen_->setValue(hs->lengthMm);
                transOd_->setValue(hs->odMm);
            }
            for (QWidget* w : {static_cast<QWidget*>(transPart_), static_cast<QWidget*>(transLen_),
                               static_cast<QWidget*>(transOd_)})
                w->setEnabled(hs != nullptr);
        }
        if (run) {
            transStrip_->setValue(atToEnd ? run->stripToMm : run->stripFromMm);
            // Nothing to cut back on loose wires: there is no jacket there to stop.
            transStrip_->setEnabled(run->isCable() || run->cableWithoutJacket());
        }
        break;
    }
    case Step::MainRun:
    case Step::BranchRun: {
        pages_->setCurrentIndex(3);
        fillParts(runCablePart_, catalog_, PartKind::Cable);
        MechSegment* run = runForStep(ref);
        if (run) {
            const bool cable = run->isCable() || !run->cableName.isEmpty();
            runMake_->setCurrentIndex(cable ? 1 : 0);
            const bool main = ref.step == Step::MainRun;
            runCount_->setValue(main ? static_cast<int>(def_.wires.size())
                                     : static_cast<int>(run->wireIds.size()));
            runCount_->setEnabled(true);
            const MechWire* w = run->wireIds.isEmpty() ? nullptr : def_.wire(run->wireIds.front());
            runGauge_->setCurrentText(w ? w->gauge : QStringLiteral("0.5"));
            runGauge_->setEnabled(main);   // the conductors are described once, on the main run
            const int at = runCablePart_->findData(run->cablePart);
            runCablePart_->setCurrentIndex(at >= 0 ? at : 0);
            runJacket_->setValue(run->jacketOdMm);
            runCablePart_->setEnabled(cable);
            runJacket_->setEnabled(cable);
            const MechSegment* sp = main ? nullptr : branchSpread(ref.branch);
            runLength_->setValue(run->lengthMm + (sp ? sp->lengthMm : 0.0));
            const MechNode* elbow = sp ? def_.node(sp->toNode) : nullptr;
            runAngle_->setValue(elbow ? elbow->angleDeg : 0.0);
            runAngle_->setEnabled(!main);
            const double dia = segmentBodyDiameterMm(def_, *run);
            runBundle_->setText(
                run->isCable()
                    ? QStringLiteral("Ø%1 over the jacket").arg(run->jacketOdMm, 0, 'f', 1)
                    : QStringLiteral("Ø%1, worked out from the conductors in it").arg(dia, 0, 'f', 1));
        }
        break;
    }
    case Step::MainOver:
    case Step::BranchOver: {
        pages_->setCurrentIndex(4);
        fillParts(overBraidPart_, catalog_, PartKind::ShieldBraid);
        fillParts(overShrinkPart_, catalog_, PartKind::Heatshrink);
        MechSegment* run = runForStep(ref);
        if (run) {
            const MechFitting* br = fittingOn(run->coverings, PartKind::ShieldBraid, false);
            overBraid_->setChecked(br != nullptr);
            if (br) {
                const int at = overBraidPart_->findData(br->libPart);
                overBraidPart_->setCurrentIndex(at >= 0 ? at : 0);
                overBraidFrom_->setValue(br->atMm);
                overBraidLen_->setValue(br->lengthMm);
            }
            const MechFitting* hs = fittingOn(run->coverings, PartKind::Heatshrink, false);
            overShrink_->setChecked(hs != nullptr);
            if (hs) {
                const int at = overShrinkPart_->findData(hs->libPart);
                overShrinkPart_->setCurrentIndex(at >= 0 ? at : 0);
                overShrinkFrom_->setValue(hs->atMm);
                overShrinkLen_->setValue(hs->lengthMm);
            }
            for (QWidget* w : {static_cast<QWidget*>(overBraidPart_),
                               static_cast<QWidget*>(overBraidFrom_),
                               static_cast<QWidget*>(overBraidLen_)})
                w->setEnabled(br != nullptr);
            for (QWidget* w : {static_cast<QWidget*>(overShrinkPart_),
                               static_cast<QWidget*>(overShrinkFrom_),
                               static_cast<QWidget*>(overShrinkLen_)})
                w->setEnabled(hs != nullptr);
        }
        break;
    }
    case Step::Split: {
        pages_->setCurrentIndex(5);
        MechNode* n = nodeForStep(ref);
        splitCount_->setValue(branchCount());
        const MechFitting* y = n ? fittingOn(n->fittings, PartKind::YSplit, false) : nullptr;
        const MechFitting* hs = n ? fittingOn(n->fittings, PartKind::Heatshrink, false) : nullptr;
        splitKind_->setCurrentIndex(y ? 2 : hs ? 1 : 0);
        fillParts(splitPart_, catalog_, y ? PartKind::YSplit : PartKind::Heatshrink,
                  /*needsDrawing=*/y != nullptr);
        const MechFitting* f = y ? y : hs;
        if (f) {
            const int at = splitPart_->findData(f->libPart);
            splitPart_->setCurrentIndex(at >= 0 ? at : 0);
            splitLen_->setValue(f->lengthMm);
            splitOd_->setValue(f->odMm);
        }
        for (QWidget* w : {static_cast<QWidget*>(splitPart_), static_cast<QWidget*>(splitLen_),
                           static_cast<QWidget*>(splitOd_)})
            w->setEnabled(f != nullptr);
        break;
    }
    }
    syncing_ = false;
    refreshList();
    refreshPreview();
    refreshSummary();
}

void HarnessStepsDialog::apply() {
    if (steps_.empty()) return;
    started_ = true;   // something has been said: the harness exists from here on
    const StepRef ref = steps_[static_cast<size_t>(current_)];
    switch (ref.step) {
    case Step::StartEnd:
    case Step::BranchEnd: {
        MechNode* n = nodeForStep(ref);
        if (!n) break;
        const int kind = endKind_->currentIndex();
        n->kind = kind == 0 ? MechNodeKind::FreeEnd : MechNodeKind::Connector;
        n->name = endName_->text().trimmed();
        if (n->name.isEmpty()) n->name = kind == 0 ? QStringLiteral("open") : QStringLiteral("P?");
        n->libPart = endPart_->currentData().toString();
        n->pinCount = endPins_->value();
        n->bodyWidthMm = endBodyW_->value();
        n->bodyHeightMm = endBodyH_->value();
        // A cap is a part in its own right, fitted at the end like anything else, so it reaches the
        // table and gets a balloon rather than being a tick nobody can buy.
        setFitting(n->fittings, PartKind::Overmould, kind != 0 && endCap_->isChecked());
        n->openLengthMm = endLeadLen_->value();
        break;
    }
    case Step::StartDress:
    case Step::BranchDress: {
        MechNode* n = nodeForStep(ref);
        if (!n) break;
        setFitting(n->fittings, PartKind::Backshell, dressBackshell_->isChecked());
        if (MechFitting* bs = fittingOn(n->fittings, PartKind::Backshell, false)) {
            bs->libPart = dressBackshellPart_->currentData().toString();
            bs->lengthMm = dressBackshellLen_->value();
            bs->odMm = dressBackshellOd_->value();
        }
        setFitting(n->fittings, PartKind::HeatshrinkBoot, dressBoot_->isChecked());
        if (MechFitting* bt = fittingOn(n->fittings, PartKind::HeatshrinkBoot, false)) {
            bt->libPart = dressBootPart_->currentData().toString();
            bt->lengthMm = dressBootLen_->value();
            bt->odMm = dressBootOd_->value();
        }
        n->openLengthMm = dressOpen_->value();
        break;
    }
    case Step::StartTransition:
    case Step::BranchTransition: {
        MechNode* n = nodeForStep(ref);
        MechSegment* run = runForStep(ref);
        if (n) {
            setFitting(n->fittings, PartKind::Heatshrink, transOn_->isChecked());
            if (MechFitting* hs = fittingOn(n->fittings, PartKind::Heatshrink, false)) {
                hs->libPart = transPart_->currentData().toString();
                hs->lengthMm = transLen_->value();
                hs->odMm = transOd_->value();
            }
        }
        if (run) {
            if (ref.step == Step::BranchTransition) run->stripToMm = transStrip_->value();
            else run->stripFromMm = transStrip_->value();
        }
        break;
    }
    case Step::MainRun:
    case Step::BranchRun: {
        MechSegment* run = runForStep(ref);
        if (!run) break;
        const bool cable = runMake_->currentIndex() == 1;
        run->lengthMm = runLength_->value();
        // On a branch the figure asked for is how long the BRANCH is, and it is drawn in two pieces:
        // the piece that turns it off the trunk and the straight piece after it. A quarter and
        // three quarters, which is about what the sample drawings use, bounded so a short branch is
        // not all corner and a long one does not spread half way across the sheet.
        if (ref.step == Step::BranchRun) {
            const double total = std::max(1.0, runLength_->value());
            const double spread = std::clamp(total * 0.25, 10.0, 150.0);
            if (MechSegment* sp = branchSpread(ref.branch)) sp->lengthMm = spread;
            run->lengthMm = std::max(1.0, total - spread);
        }
        if (cable) {
            run->cablePart = runCablePart_->currentData().toString();
            run->jacketOdMm = runJacket_->value();
            if (run->cableName.isEmpty()) run->cableName = QStringLiteral("cable");
        } else {
            run->cablePart.clear();
            run->cableName.clear();
            run->jacketOdMm = 0.0;
        }
        if (ref.step == Step::MainRun) {
            setConductors(runCount_->value(), runGauge_->currentText().trimmed());
        } else if (true) {
            // How many of the conductors go this way. The rest stay with the other branches.
            const int want = std::clamp(runCount_->value(), 0,
                                        static_cast<int>(def_.wires.size()));
            QStringList ids;
            for (int i = 0; i < want; ++i) ids << def_.wires[static_cast<size_t>(i)].id;
            run->wireIds = ids;
            // Down both legs of the branch: they are one branch drawn in two pieces, and a leg with
            // different conductors in it would be drawn at a different width from the piece beside it.
            if (MechSegment* sp = branchSpread(ref.branch)) sp->wireIds = ids;
            // The angle is the spread leg's; the straight leg turns back by as much, so it stays
            // parallel to the trunk however the branch is aimed.
            if (MechSegment* sp = branchSpread(ref.branch)) {
                if (MechNode* elbow = nodeIn(def_, sp->toNode)) elbow->angleDeg = runAngle_->value();
                if (MechNode* tip = nodeIn(def_, run->toNode)) tip->angleDeg = -runAngle_->value();
            }
        }
        break;
    }
    case Step::MainOver:
    case Step::BranchOver: {
        MechSegment* run = runForStep(ref);
        if (!run) break;
        setFitting(run->coverings, PartKind::ShieldBraid, overBraid_->isChecked());
        if (MechFitting* br = fittingOn(run->coverings, PartKind::ShieldBraid, false)) {
            br->libPart = overBraidPart_->currentData().toString();
            br->atMm = overBraidFrom_->value();
            br->lengthMm = overBraidLen_->value();
        }
        setFitting(run->coverings, PartKind::Heatshrink, overShrink_->isChecked());
        if (MechFitting* hs = fittingOn(run->coverings, PartKind::Heatshrink, false)) {
            hs->libPart = overShrinkPart_->currentData().toString();
            hs->atMm = overShrinkFrom_->value();
            hs->lengthMm = overShrinkLen_->value();
        }
        break;
    }
    case Step::Split: {
        MechNode* n = nodeForStep(ref);
        const int kind = splitKind_->currentIndex();
        if (n) {
            setFitting(n->fittings, PartKind::YSplit, kind == 2);
            setFitting(n->fittings, PartKind::Heatshrink, kind == 1);
            MechFitting* f = kind == 2 ? fittingOn(n->fittings, PartKind::YSplit, false)
                                       : fittingOn(n->fittings, PartKind::Heatshrink, false);
            if (f) {
                f->libPart = splitPart_->currentData().toString();
                f->lengthMm = splitLen_->value();
                f->odMm = splitOd_->value();
            }
        }
        if (splitCount_->value() != branchCount()) {
            setBranchCount(splitCount_->value());
            rebuildSteps();
        }
        break;
    }
    }
    showStep();   // the page read back, so what a change implies is on the screen at once
}

// ---------------------------------------------------------------------------------------------------

void HarnessStepsDialog::refreshPreview() {
    if (!preview_) return;
    if (!started_) {
        preview_->setEntities({});
        preview_->setEmptyText(QStringLiteral("nothing yet — answer the step beside this"));
        return;
    }
    preview_->setEntities(buildMechanicalHarness(def_, &catalog_));
}

void HarnessStepsDialog::refreshSummary() {
    if (!summary_) return;
    double total = 0.0;
    for (const MechSegment& s : def_.segments) total = std::max(total, s.lengthMm);
    double longest = 0.0;
    if (const MechSegment* main = segmentIn(def_, QStringLiteral("main"))) {
        for (int b = 0; b < branchCount(); ++b) {
            const MechSegment* run = const_cast<HarnessStepsDialog*>(this)->branchRun(b);
            if (run) longest = std::max(longest, main->lengthMm + run->lengthMm);
        }
    }
    summary_->setText(QStringLiteral("%1 conductor(s), %2 branch(es), longest way through %3 mm — "
                                     "drawn to scale, %4 — step %5 of %6")
                          .arg(def_.wires.size())
                          .arg(branchCount())
                          .arg(longest, 0, 'f', 0)
                          .arg(std::abs(def_.draw.diameterExaggeration - 1.0) < 1e-9
                                   ? QStringLiteral("widths and lengths alike")
                                   : QStringLiteral("widths ×%1")
                                         .arg(def_.draw.diameterExaggeration, 0, 'f', 1))
                          .arg(current_ + 1)
                          .arg(steps_.size()));
}

void HarnessStepsDialog::setDefinition(const MechHarnessDef& def) {
    def_ = def;
    rebuildSteps();
    goToStep(0);
}

void HarnessStepsDialog::place() {
    if (!editor_) return;
    const std::vector<Entity> ents = buildMechanicalHarness(def_, &catalog_);
    if (ents.empty()) {
        if (summary_) summary_->setText(QStringLiteral("Nothing to place yet."));
        return;
    }
    editor_->placeEntities(ents);
    if (summary_)
        summary_->setText(QStringLiteral("%1 object(s) ready — click on the sheet to put them down.")
                              .arg(ents.size()));
}

} // namespace Edraw_Desktop
