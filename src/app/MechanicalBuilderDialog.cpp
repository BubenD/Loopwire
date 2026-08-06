#include "app/MechanicalBuilderDialog.h"

#include "app/UiBits.h"
#include "harness/CableSpec.h"
#include "app/DrawingPreview.h"
#include "app/Editor.h"
#include "model/Document.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace Edraw_Desktop {
namespace {

// The parts that go over the END of a run - a backshell, a boot, a gland - as against the ones that
// run ALONG it. Which list a fitting joins follows from what it is, so there is nothing to choose.
bool fittedAtEnd(PartKind kind) {
    switch (kind) {
        case PartKind::Backshell:
        case PartKind::HeatshrinkBoot:
        case PartKind::CableGland:
        case PartKind::Grommet:
        case PartKind::Overmould:
        case PartKind::YSplit:
            return true;
        default:
            return false;
    }
}

// Everything that can be fitted to a harness, in the order the picker lists them.
const std::vector<PartKind>& fittingKinds() {
    static const std::vector<PartKind> kinds = {
        PartKind::Backshell, PartKind::HeatshrinkBoot, PartKind::YSplit,   PartKind::Heatshrink,
        PartKind::Sleeve,    PartKind::Conduit,        PartKind::ShieldBraid,
        PartKind::CableTie,  PartKind::CableGland,     PartKind::Grommet,  PartKind::Overmould,
        PartKind::Tape,      PartKind::Marker};
    return kinds;
}

QLabel* caption(QWidget* parent, const QString& text) {
    auto* l = new QLabel(text, parent);
    l->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
    return l;
}

} // namespace

MechanicalBuilderDialog::MechanicalBuilderDialog(Editor* editor, QWidget* parent)
    : QDialog(parent), editor_(editor) {
    setWindowTitle(QStringLiteral("Mechanical builder"));
    resize(1180, 760);
    catalog_.openRemembered();
    buildUi();
    // Open on the harness that is drawn, if there is one: retyping what the schematic already says
    // is the one thing this window should never ask for.
    readFromDrawing();
}

void MechanicalBuilderDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

    auto* head = new QHBoxLayout;
    head->addWidget(caption(this, QStringLiteral("Harness")));
    nameEdit_ = new QLineEdit(this);
    nameEdit_->setMaximumWidth(220);
    connect(nameEdit_, &QLineEdit::textEdited, this, [this](const QString& t) {
        def_.name = t;
    });
    head->addWidget(nameEdit_);
    auto* readBtn = new QPushButton(QStringLiteral("Read from the drawing"), this);
    readBtn->setToolTip(QStringLiteral(
        "Take the connectors and the conductors from the schematic that is drawn, as a branch out to "
        "each connector. The lengths it starts with are what the schematic happens to be drawn at - "
        "putting the real ones in is what this window is for."));
    connect(readBtn, &QPushButton::clicked, this, [this] { readFromDrawing(); });
    head->addWidget(readBtn);
    head->addStretch(1);
    root->addLayout(head);

    auto* split = new QSplitter(Qt::Horizontal, this);

    // ---- The tree of runs and ends ----
    auto* leftBox = new QGroupBox(QStringLiteral("Runs and ends"), split);
    auto* leftLay = new QVBoxLayout(leftBox);
    tree_ = new QTreeWidget(leftBox);
    tree_->setColumnCount(6);
    tree_->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("What"),
                            QStringLiteral("Length (mm)"), QStringLiteral("Angle (°)"),
                            QStringLiteral("Bundle Ø"), QStringLiteral("Wires")});
    tree_->setRootIsDecorated(true);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* now, QTreeWidgetItem*) {
                if (syncing_) return;
                selected_ = now ? now->data(0, Qt::UserRole).toString() : QString();
                refreshDetails();
            });
    leftLay->addWidget(tree_, 1);

    auto* treeBtns = new QHBoxLayout;
    auto* addBranchBtn = makeAddButton(leftBox, QStringLiteral("Branch"));
    addBranchBtn->setToolTip(QStringLiteral("A run out of the selected point, to a connector"));
    connect(addBranchBtn, &QPushButton::clicked, this,
            [this] { addBranch(MechNodeKind::Connector); });
    treeBtns->addWidget(addBranchBtn);
    auto* addBreakBtn = makeAddButton(leftBox, QStringLiteral("Breakout"));
    addBreakBtn->setToolTip(QStringLiteral("A run out to a point the harness divides at again"));
    connect(addBreakBtn, &QPushButton::clicked, this,
            [this] { addBranch(MechNodeKind::Breakout); });
    treeBtns->addWidget(addBreakBtn);
    auto* addEndBtn = makeAddButton(leftBox, QStringLiteral("Cut end"));
    connect(addEndBtn, &QPushButton::clicked, this, [this] { addBranch(MechNodeKind::FreeEnd); });
    treeBtns->addWidget(addEndBtn);
    auto* delBtn = new QPushButton(QStringLiteral("Remove"), leftBox);
    connect(delBtn, &QPushButton::clicked, this, [this] { removeSelected(); });
    treeBtns->addWidget(delBtn);
    treeBtns->addStretch(1);
    leftLay->addLayout(treeBtns);
    split->addWidget(leftBox);

    // ---- The right-hand side: what is selected, and what it will look like ----
    auto* rightHost = new QWidget(split);
    auto* rightLay = new QVBoxLayout(rightHost);
    rightLay->setContentsMargins(0, 0, 0, 0);

    preview_ = new DrawingPreview(QStringLiteral("The drawing this will generate"), rightHost);
    preview_->setEmptyText(QStringLiteral("nothing to draw yet"));
    preview_->setMinimumHeight(210);
    rightLay->addWidget(preview_, 1);

    auto* selBox = new QGroupBox(QStringLiteral("The selected point, and the run arriving at it"),
                                rightHost);
    auto* grid = new QGridLayout(selBox);
    int r = 0;
    nodeName_ = new QLineEdit(selBox);
    connect(nodeName_, &QLineEdit::textEdited, this, [this](const QString& t) {
        if (syncing_) return;
        if (MechNode* n = currentNode()) { n->name = t; refreshTree(); refreshPreview(); }
    });
    nodeKind_ = new QComboBox(selBox);
    nodeKind_->addItem(mechNodeKindName(MechNodeKind::Connector),
                       static_cast<int>(MechNodeKind::Connector));
    nodeKind_->addItem(mechNodeKindName(MechNodeKind::Breakout),
                       static_cast<int>(MechNodeKind::Breakout));
    nodeKind_->addItem(mechNodeKindName(MechNodeKind::FreeEnd),
                       static_cast<int>(MechNodeKind::FreeEnd));
    connect(nodeKind_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (syncing_) return;
        if (MechNode* n = currentNode()) {
            n->kind = static_cast<MechNodeKind>(nodeKind_->currentData().toInt());
            refreshAll();
        }
    });
    grid->addWidget(caption(selBox, QStringLiteral("Name")), r, 0);
    grid->addWidget(nodeName_, r, 1);
    grid->addWidget(caption(selBox, QStringLiteral("What it is")), r, 2);
    grid->addWidget(nodeKind_, r++, 3);

    lengthSpin_ = new QDoubleSpinBox(selBox);
    lengthSpin_->setRange(0.0, 100000.0);
    lengthSpin_->setDecimals(1);
    lengthSpin_->setSuffix(QStringLiteral(" mm"));
    lengthSpin_->setToolTip(QStringLiteral("How long this run is - the cut length, not a drawn one"));
    connect(lengthSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (syncing_) return;
        if (MechSegment* s = currentSegment()) { s->lengthMm = v; refreshAll(); }
    });
    angleSpin_ = new QDoubleSpinBox(selBox);
    angleSpin_->setRange(-180.0, 180.0);
    angleSpin_->setDecimals(1);
    angleSpin_->setSuffix(QStringLiteral(" °"));
    angleSpin_->setToolTip(QStringLiteral(
        "Which way it leaves, measured from the run it comes off. 0 carries straight on, so moving a "
        "branch takes everything below it round with it."));
    connect(angleSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (syncing_) return;
        if (MechNode* n = currentNode()) { n->angleDeg = v; refreshAll(); }
    });
    grid->addWidget(caption(selBox, QStringLiteral("Length")), r, 0);
    grid->addWidget(lengthSpin_, r, 1);
    grid->addWidget(caption(selBox, QStringLiteral("Angle")), r, 2);
    grid->addWidget(angleSpin_, r++, 3);

    connPart_ = new QComboBox(selBox);
    connPart_->setToolTip(QStringLiteral(
        "The connector from the library. If the part has a mechanical drawing of its own, that is "
        "what gets drawn on the end of the run instead of a plain body."));
    connect(connPart_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (syncing_) return;
        if (MechNode* n = currentNode()) {
            n->libPart = connPart_->currentData().toString();
            refreshPreview();
        }
    });
    bundleLabel_ = new QLabel(QString(), selBox);
    bundleLabel_->setStyleSheet(QStringLiteral("color:#8fb0d6;font-weight:600;"));
    bundleLabel_->setWordWrap(true);
    grid->addWidget(caption(selBox, QStringLiteral("Connector")), r, 0);
    grid->addWidget(connPart_, r, 1);
    grid->addWidget(caption(selBox, QStringLiteral("Bundle")), r, 2);
    grid->addWidget(bundleLabel_, r++, 3);

    // ---- The run as a cable: the jacket, and how far it is cut back at this end ----
    cablePart_ = new QComboBox(selBox);
    cablePart_->setToolTip(QStringLiteral(
        "The cable this run is, when it is one. A cable is drawn at its JACKET, not at the bundle its "
        "cores would make loose: a Ø5.0 cable whose five conductors add up to Ø2.4 is a Ø5.0 run, and "
        "a boot at the end of it has to pass Ø5.0. Only where the jacket is cut back does the bundle "
        "inside become the diameter that matters."));
    connect(cablePart_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (syncing_) return;
        MechSegment* s = currentRun();
        if (!s) return;
        s->cablePart = cablePart_->currentData().toString();
        if (s->cablePart.isEmpty()) {
            s->jacketOdMm = 0.0;    // loose wires: the bundle is worked out from the conductors
        } else if (const LibraryPart* p = catalog_.byId(s->cablePart)) {
            // What the part states, and nothing more. A cable that does not say what it measures over
            // the jacket leaves the figure empty and is asked for by name in the list below - a jacket
            // guessed from the cores is the one number on this drawing nobody could check.
            const CableSpec spec = cableSpecOf(*p);
            if (spec.odMm > 0.0) s->jacketOdMm = spec.odMm;
            if (s->cableName.isEmpty())
                s->cableName = p->mfrPn.isEmpty() ? p->title() : p->mfrPn;
        }
        refreshAll();
    });
    jacketSpin_ = new QDoubleSpinBox(selBox);
    jacketSpin_->setRange(0.0, 300.0);
    jacketSpin_->setDecimals(2);
    jacketSpin_->setSuffix(QStringLiteral(" mm"));
    jacketSpin_->setToolTip(QStringLiteral(
        "Over the jacket. 0 means there is no jacket - loose wires, and the bundle is worked out from "
        "the conductors. Anything else is what the run is drawn at, and what has to go into a boot."));
    connect(jacketSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (syncing_) return;
        if (MechSegment* s = currentRun()) { s->jacketOdMm = v; refreshAll(); }
    });
    // The jacket is cut back at BOTH ends of a run, and for different reasons: at the connector so the
    // conductors can reach the contacts, at a breakout so the cable can divide into branches of loose
    // wires. Both are on this row, because the far end of a trunk is a breakout - and a breakout has no
    // run arriving at it, so there would otherwise be nowhere at all to say the jacket stops there.
    auto stripBox = [this, selBox](bool farEnd) {
        auto* s = new QDoubleSpinBox(selBox);
        s->setRange(0.0, 100000.0);
        s->setDecimals(1);
        s->setSuffix(QStringLiteral(" mm"));
        s->setToolTip(farEnd
            ? QStringLiteral("Wires without jacket at the FAR end of this run - the breakout or point "
                             "it comes off. This is the one to use when a cable arrives and its jacket "
                             "is cut at the spread point, with only the conductors carrying on into "
                             "the branches.")
            : QStringLiteral("Wires without jacket at THIS end: how far back the jacket is cut so the "
                             "conductors come out and reach the contacts. Over that length the run is "
                             "the BUNDLE, with a cone between the two diameters - and a braid or "
                             "heatshrink ticked below covers exactly that stretch."));
        connect(s, &QDoubleSpinBox::valueChanged, this, [this, farEnd](double v) {
            if (syncing_) return;
            bool atTo = true;
            MechSegment* seg = currentRun(&atTo);
            if (!seg) return;
            // A node sits at one END of its run, so "this end" is that one and "the far end" is the
            // other, whichever way round the run happens to be defined.
            if (atTo == !farEnd) seg->stripToMm = v;
            else seg->stripFromMm = v;
            refreshAll();
        });
        return s;
    };
    stripSpin_ = stripBox(false);
    stripFarSpin_ = stripBox(true);
    grid->addWidget(caption(selBox, QStringLiteral("Cable")), r, 0);
    grid->addWidget(cablePart_, r, 1);
    {
        auto* jrow = new QHBoxLayout;
        jrow->addWidget(caption(selBox, QStringLiteral("Jacket Ø")));
        jrow->addWidget(jacketSpin_);
        jrow->addWidget(caption(selBox, QStringLiteral("No jacket: this end")));
        jrow->addWidget(stripSpin_);
        jrow->addWidget(caption(selBox, QStringLiteral("far end")));
        jrow->addWidget(stripFarSpin_);
        grid->addLayout(jrow, r, 2, 1, 2);
    }
    ++r;

    // ---- What every end can have, as ticks rather than a trip to the library ----
    backshellChk_ = new QCheckBox(QStringLiteral("Backshell"), selBox);
    backshellChk_->setToolTip(QStringLiteral(
        "A backshell on the connector. It sits at the end and the cable enters it, so it is sized on "
        "the cable and not on the stripped bundle in front of it."));
    bootChk_ = new QCheckBox(QStringLiteral("Boot / heatshrink"), selBox);
    bootChk_->setToolTip(QStringLiteral(
        "A moulded boot or a heatshrink transition behind the backshell - over the jacket where the "
        "cable is whole, recovered down onto the bundle where it is not."));
    braidChk_ = new QCheckBox(QStringLiteral("Shield braid"), selBox);
    braidChk_->setToolTip(QStringLiteral(
        "Braid combed over the conductors that come out of the jacket. It covers the stripped stretch, "
        "so it is as long as \"wires without jacket\" says unless a part states its own length."));
    shrinkChk_ = new QCheckBox(QStringLiteral("Heatshrink over them"), selBox);
    shrinkChk_->setToolTip(QStringLiteral(
        "Heatshrink over the bare conductors - and over the braid, when there is one, rather than "
        "end to end with it."));
    {
        auto* endRow = new QHBoxLayout;
        endRow->addWidget(backshellChk_);
        endRow->addWidget(bootChk_);
        endRow->addWidget(braidChk_);
        endRow->addWidget(shrinkChk_);
        endRow->addStretch(1);
        grid->addWidget(caption(selBox, QStringLiteral("At this end")), r, 0);
        grid->addLayout(endRow, r++, 1, 1, 3);
    }
    const struct { QCheckBox** box; PartKind kind; } endTicks[] = {
        {&backshellChk_, PartKind::Backshell},
        {&bootChk_, PartKind::HeatshrinkBoot},
        {&braidChk_, PartKind::ShieldBraid},
        {&shrinkChk_, PartKind::Heatshrink},
    };
    for (const auto& t : endTicks) {
        const PartKind kind = t.kind;
        connect(*t.box, &QCheckBox::toggled, this, [this, kind](bool on) {
            if (syncing_) return;
            setEndFitting(kind, on);
        });
    }

    wireList_ = new QListWidget(selBox);
    wireList_->setMaximumHeight(110);
    wireList_->setToolTip(QStringLiteral(
        "The conductors running through this run. What is ticked here is what the bundle is drawn "
        "from, so this is where the diameter comes from."));
    connect(wireList_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (syncing_ || !item) return;
        MechSegment* s = currentRun();
        if (!s) return;
        const QString id = item->data(Qt::UserRole).toString();
        const bool on = item->checkState() == Qt::Checked;
        if (on && !s->wireIds.contains(id)) s->wireIds.push_back(id);
        else if (!on) s->wireIds.removeAll(id);
        refreshAll();
    });
    grid->addWidget(caption(selBox, QStringLiteral("Conductors")), r, 0, Qt::AlignTop);
    grid->addWidget(wireList_, r++, 1, 1, 3);

    fittingList_ = new QListWidget(selBox);
    fittingList_->setMaximumHeight(90);
    grid->addWidget(caption(selBox, QStringLiteral("Fitted")), r, 0, Qt::AlignTop);
    grid->addWidget(fittingList_, r++, 1, 1, 3);

    auto* fitRow = new QHBoxLayout;
    fittingChoice_ = new QComboBox(selBox);
    fittingChoice_->setMinimumWidth(200);
    fitRow->addWidget(fittingChoice_, 1);
    onlyFits_ = new QCheckBox(QStringLiteral("only what fits"), selBox);
    onlyFits_->setChecked(true);
    onlyFits_->setToolTip(QStringLiteral(
        "Hide the parts this bundle will not go into. What is left is what can actually be fitted "
        "here - which is a better question to answer than sorting it out afterwards."));
    connect(onlyFits_, &QCheckBox::toggled, this, [this](bool) { refreshFittingChoices(); });
    fitRow->addWidget(onlyFits_);
    addFittingBtn_ = new QPushButton(QStringLiteral("Fit it"), selBox);
    connect(addFittingBtn_, &QPushButton::clicked, this, [this] { addFitting(); });
    fitRow->addWidget(addFittingBtn_);
    auto* rmFitBtn = new QPushButton(QStringLiteral("Remove"), selBox);
    connect(rmFitBtn, &QPushButton::clicked, this, [this] { removeFitting(); });
    fitRow->addWidget(rmFitBtn);
    grid->addLayout(fitRow, r++, 1, 1, 3);
    rightLay->addWidget(selBox);
    split->addWidget(rightHost);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 4);
    root->addWidget(split, 3);

    // ---- Options ----
    auto* optBox = new QGroupBox(QStringLiteral("How it is drawn"), this);
    // Two rows, not one: the figures above, what is put on the drawing below. In one row the window
    // could not be opened narrower than the sum of them all, which on a laptop meant a window with its
    // right-hand end off the screen.
    auto* optCol = new QVBoxLayout(optBox);
    auto* opt = new QHBoxLayout;
    optCol->addLayout(opt);
    scaleSpin_ = new QDoubleSpinBox(optBox);
    scaleSpin_->setRange(0.01, 10.0);
    scaleSpin_->setDecimals(3);
    scaleSpin_->setSingleStep(0.1);
    scaleSpin_->setToolTip(QStringLiteral("Drawing units per millimeter of harness. 1 is full size."));
    opt->addWidget(caption(optBox, QStringLiteral("Scale")));
    opt->addWidget(scaleSpin_);
    factorSpin_ = new QDoubleSpinBox(optBox);
    factorSpin_->setRange(1.0, 2.0);
    factorSpin_->setDecimals(2);
    factorSpin_->setSingleStep(0.05);
    factorSpin_->setToolTip(QStringLiteral(
        "The random-lay factor for four conductors or more: Ø = factor × √(sum of the squares). 1.2 "
        "is the usual figure for a round bundle."));
    opt->addWidget(caption(optBox, QStringLiteral("Bundle factor")));
    opt->addWidget(factorSpin_);
    exagSpin_ = new QDoubleSpinBox(optBox);
    exagSpin_->setRange(1.0, 50.0);
    exagSpin_->setDecimals(1);
    exagSpin_->setSingleStep(1.0);
    exagSpin_->setPrefix(QStringLiteral("× "));
    exagSpin_->setToolTip(QStringLiteral(
        "Widths only: the bundle body, a fitting's diameter, a connector's height. At full size a "
        "6 mm bundle on a 500 mm run is a hairline - true, and unreadable. Lengths are never touched, "
        "and anything other than × 1 puts a note on the drawing saying so."));
    opt->addWidget(caption(optBox, QStringLiteral("Bundle shown")));
    opt->addWidget(exagSpin_);
    textSpin_ = new QDoubleSpinBox(optBox);
    textSpin_->setRange(0.5, 50.0);
    textSpin_->setDecimals(1);
    opt->addWidget(caption(optBox, QStringLiteral("Text")));
    opt->addWidget(textSpin_);
    dimsChk_ = new QCheckBox(QStringLiteral("Dimensions"), optBox);
    diaChk_ = new QCheckBox(QStringLiteral("Bundle Ø"), optBox);
    countChk_ = new QCheckBox(QStringLiteral("Wire count"), optBox);
    balloonChk_ = new QCheckBox(QStringLiteral("Balloons"), optBox);
    bodiesChk_ = new QCheckBox(QStringLiteral("Connector bodies"), optBox);
    spliceChk_ = new QCheckBox(QStringLiteral("Splice heatshrink"), optBox);
    spliceChk_->setToolTip(QStringLiteral(
        "A junction where one conductor ends and a different size carries on has a splice in it, and a "
        "splice is not left bare. Where the transition IS follows from the conductors either side, so "
        "re-gauging a wire moves the heatshrink with it rather than leaving it behind."));
    opt->addStretch(1);
    auto* optTicks = new QHBoxLayout;
    optCol->addLayout(optTicks);
    for (QCheckBox* c : {dimsChk_, diaChk_, countChk_, balloonChk_, bodiesChk_, spliceChk_})
        optTicks->addWidget(c);
    optTicks->addStretch(1);
    for (QDoubleSpinBox* s : {scaleSpin_, factorSpin_, exagSpin_, textSpin_})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this](double) {
            if (syncing_) return;
            readOptionsFromControls();
            refreshAll();
        });
    for (QCheckBox* c : {dimsChk_, diaChk_, countChk_, balloonChk_, bodiesChk_, spliceChk_})
        connect(c, &QCheckBox::toggled, this, [this](bool) {
            if (syncing_) return;
            readOptionsFromControls();
            refreshAll();
        });
    root->addWidget(optBox);
    writeOptionsToControls();

    // ---- What is wrong with it ----
    auto* issueBox = new QGroupBox(QStringLiteral("What is missing, and what will not fit"), this);
    auto* issueLay = new QVBoxLayout(issueBox);
    issues_ = new QListWidget(issueBox);
    issues_->setMaximumHeight(110);
    issueLay->addWidget(issues_);
    root->addWidget(issueBox, 1);

    auto* foot = new QHBoxLayout;
    summary_ = new QLabel(QString(), this);
    summary_->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
    foot->addWidget(summary_, 1);
    auto* genBtn = new QPushButton(QStringLiteral("Generate"), this);
    genBtn->setDefault(true);
    genBtn->setToolTip(QStringLiteral("Put the drawing on the sheet - then click where it goes"));
    connect(genBtn, &QPushButton::clicked, this, [this] { generate(); });
    foot->addWidget(genBtn);
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    foot->addWidget(closeBtn);
    root->addLayout(foot);
}

// ---- Reading and writing the definition --------------------------------------------------------

void MechanicalBuilderDialog::readFromDrawing() {
    if (!editor_ || !editor_->document()) return;
    const MechDrawOptions keep = def_.draw;
    def_ = mechFromDrawing(*editor_->document(), &catalog_);
    def_.draw = keep;   // how it is drawn is the user's setting, not the drawing's
    selected_ = def_.nodes.empty() ? QString() : def_.nodes.back().id;
    refreshAll();
    if (summary_) {
        summary_->setText(def_.segments.empty()
                              ? QStringLiteral("Nothing on the sheet to read: draw the schematic "
                                               "first, or add the runs by hand.")
                              : QStringLiteral("Read %1 run(s) and %2 conductor(s) off the drawing.")
                                    .arg(def_.segments.size())
                                    .arg(def_.wires.size()));
    }
}

void MechanicalBuilderDialog::setDefinition(const MechHarnessDef& def) {
    def_ = def;
    selected_ = def_.nodes.empty() ? QString() : def_.nodes.front().id;
    refreshAll();
}

MechNode* MechanicalBuilderDialog::currentNode() {
    for (MechNode& n : def_.nodes)
        if (n.id == selected_) return &n;
    return nullptr;
}

MechSegment* MechanicalBuilderDialog::currentSegment() {
    MechNode* n = currentNode();
    if (!n) return nullptr;
    for (MechSegment& s : def_.segments)
        if (s.id == n->parentSegment) return &s;
    return nullptr;
}

MechSegment* MechanicalBuilderDialog::currentRun(bool* atToEnd) {
    if (atToEnd) *atToEnd = true;
    const MechNode* n = def_.node(selected_);
    if (!n) return nullptr;
    const MechSegment* found = nodeRunSegment(def_, *n, atToEnd);
    if (!found) return nullptr;
    for (MechSegment& s : def_.segments)
        if (s.id == found->id) return &s;
    return nullptr;
}

bool MechanicalBuilderDialog::selectNode(const QString& nodeId) {
    if (!def_.node(nodeId)) return false;
    selected_ = nodeId;
    refreshTree();
    refreshDetails();
    return true;
}

void MechanicalBuilderDialog::addBranch(MechNodeKind kind) {
    // Nothing yet: the first thing added is the point everything else hangs off.
    if (def_.nodes.empty()) {
        MechNode root;
        root.id = def_.nextNodeId();
        root.name = QStringLiteral("BR1");
        root.kind = MechNodeKind::Breakout;
        def_.nodes.push_back(root);
        selected_ = root.id;
    }
    const QString parent = currentNode() ? currentNode()->id : def_.rootNode()->id;

    MechSegment s;
    s.id = def_.nextSegmentId();
    s.fromNode = parent;
    s.lengthMm = 200.0;
    // A new run starts carrying what its parent carries: a branch off a trunk takes some of the
    // trunk's conductors, and taking a few away is less work than ticking them all on.
    if (const MechSegment* up = def_.segment(def_.node(parent)->parentSegment)) s.wireIds = up->wireIds;
    else
        for (const MechWire& w : def_.wires) s.wireIds.push_back(w.id);

    MechNode n;
    n.id = def_.nextNodeId();
    n.kind = kind;
    n.parentSegment = s.id;
    // Spread the branches out rather than laying them on top of one another.
    const int siblings = static_cast<int>(def_.childSegments(parent).size());
    n.angleDeg = siblings == 0 ? 0.0 : (siblings % 2 ? 1 : -1) * 45.0 * ((siblings + 1) / 2);
    int no = 1;
    for (const MechNode& x : def_.nodes)
        if (x.kind == kind) ++no;
    n.name = kind == MechNodeKind::Connector ? QStringLiteral("P%1").arg(no)
             : kind == MechNodeKind::Breakout ? QStringLiteral("BR%1").arg(no)
                                              : QStringLiteral("E%1").arg(no);
    s.toNode = n.id;
    def_.segments.push_back(s);
    def_.nodes.push_back(n);
    selected_ = n.id;
    refreshAll();
}

void MechanicalBuilderDialog::removeSelected() {
    MechNode* n = currentNode();
    if (!n) return;
    if (n->parentSegment.isEmpty()) {
        if (summary_)
            summary_->setText(QStringLiteral("The root is what everything else hangs off; it cannot "
                                            "be removed on its own."));
        return;
    }
    // Everything below it goes too - a run cannot outlive the point it comes off.
    QStringList doomedNodes{n->id};
    QStringList doomedSegs{n->parentSegment};
    bool grew = true;
    while (grew) {
        grew = false;
        for (const MechSegment& s : def_.segments) {
            if (!doomedNodes.contains(s.fromNode) || doomedSegs.contains(s.id)) continue;
            doomedSegs.push_back(s.id);
            if (!doomedNodes.contains(s.toNode)) doomedNodes.push_back(s.toNode);
            grew = true;
        }
    }
    def_.nodes.erase(std::remove_if(def_.nodes.begin(), def_.nodes.end(),
                                    [&](const MechNode& x) { return doomedNodes.contains(x.id); }),
                     def_.nodes.end());
    def_.segments.erase(std::remove_if(def_.segments.begin(), def_.segments.end(),
                                       [&](const MechSegment& x) {
                                           return doomedSegs.contains(x.id);
                                       }),
                        def_.segments.end());
    selected_ = def_.rootNode() ? def_.rootNode()->id : QString();
    refreshAll();
}

// ---- Fittings ------------------------------------------------------------------------------------

void MechanicalBuilderDialog::refreshFittingChoices() {
    if (!fittingChoice_) return;
    syncing_ = true;
    fittingChoice_->clear();
    bool atToEnd = true;
    const MechSegment* seg = currentRun(&atToEnd);
    // What a part has to go over depends on WHERE it goes: a backshell at the end takes the cable, a
    // braid over the stripped conductors takes the bundle, a sleeve along the run takes the body. One
    // figure for all three is how a boot that will not pass the jacket comes to be on the list.
    double bundle = seg ? segmentBundleDiameterMm(def_, *seg) : def_.draw.minBundleMm;
    int hidden = 0;
    bool unknown = false;                 // a cable whose jacket diameter nobody has given
    for (PartKind kind : fittingKinds()) {
        bundle = fittingFitDiameterMm(def_, seg, kind, fittingGoesAtEnd(kind), atToEnd);
        // 0 means the figure is not known. Then NOTHING is hidden and nothing is marked as a misfit:
        // this is the case where every backshell and every boot used to disappear behind a check made
        // against the cores of a cable instead of against its jacket.
        const bool checkable = bundle > 0.0;
        if (!checkable) unknown = true;
        for (const LibraryPart* p : catalog_.ofKind(kind)) {
            QString why;
            bool known = false;
            const bool fits = !checkable || mechFittingFits(*p, bundle, why, &known);
            if (!fits && onlyFits_ && onlyFits_->isChecked()) { ++hidden; continue; }
            QString label = QStringLiteral("%1 — %2").arg(partKindName(kind), p->title());
            if (!fits) label += QStringLiteral("   ✗ %1").arg(why);
            else if (!checkable) label += QStringLiteral("   (jacket Ø not given: fit unchecked)");
            else if (!known) label += QStringLiteral("   (does not say what it fits)");
            fittingChoice_->addItem(label, p->id);
        }
    }
    if (fittingChoice_->count() == 0)
        fittingChoice_->addItem(
            hidden > 0 ? QStringLiteral("nothing in the library fits a %1 mm bundle")
                             .arg(bundle, 0, 'f', 1)
                       : QStringLiteral("no fittings in the connected libraries"),
            QString());
    else if (unknown)
        fittingChoice_->setToolTip(QStringLiteral(
            "This run is cable %1 and nothing says what it measures over the jacket, so no fit has "
            "been checked. Put the jacket Ø in above and the list will say which of these will "
            "actually go over it.").arg(seg ? seg->cableName : QString()));
    else if (hidden > 0)
        fittingChoice_->setToolTip(
            QStringLiteral("%1 part(s) hidden because this %2 mm body will not go into them")
                .arg(hidden)
                .arg(bundle, 0, 'f', 1));
    syncing_ = false;
}

void MechanicalBuilderDialog::addFitting() {
    MechNode* n = currentNode();
    if (!n || !fittingChoice_) return;
    const QString partId = fittingChoice_->currentData().toString();
    if (partId.isEmpty()) return;
    const LibraryPart* part = catalog_.byId(partId);
    if (!part) return;

    MechFitting f;
    f.id = QStringLiteral("f%1").arg(
        static_cast<int>(n->fittings.size() + (currentRun() ? currentRun()->coverings.size() : 0)
                         + 1));
    f.libPart = part->id;
    f.kind = part->kind;
    f.name = part->description.isEmpty() ? partKindName(part->kind) : part->description;
    if (fittingGoesAtEnd(part->kind)) n->fittings.push_back(f);
    else if (MechSegment* s = currentRun()) s->coverings.push_back(f);
    else n->fittings.push_back(f);
    refreshAll();
}

bool MechanicalBuilderDialog::fittingGoesAtEnd(PartKind kind) const {
    if (fittedAtEnd(kind)) return true;
    // A braid or heatshrink is pulled over the conductors that come out of the jacket - so at an end
    // that HAS bare wires, that is where it goes; anywhere else it runs along the bundle.
    if (!mechOverTheWires(kind)) return false;
    const MechNode* n = def_.node(selected_);
    return n && nodeBareLengthMm(def_, *n) > 0.0;
}

bool MechanicalBuilderDialog::hasEndFitting(PartKind kind) const {
    const MechNode* n = def_.node(selected_);
    if (!n) return false;
    for (const MechFitting& f : n->fittings)
        if (f.kind == kind) return true;
    return false;
}

void MechanicalBuilderDialog::setEndFitting(PartKind kind, bool on) {
    MechNode* n = currentNode();
    if (!n) return;
    if (on == hasEndFitting(kind)) return;
    if (!on) {
        n->fittings.erase(std::remove_if(n->fittings.begin(), n->fittings.end(),
                                         [kind](const MechFitting& f) { return f.kind == kind; }),
                          n->fittings.end());
    } else {
        MechFitting f;
        f.id = QStringLiteral("end-%1").arg(partKindKey(kind));
        f.kind = kind;
        f.name = partKindName(kind);
        // Nothing else is stated: it is drawn to what it goes over, and picking a real part for it
        // later is the same list of fittings either way.
        n->fittings.push_back(f);
    }
    // Keep them in the order they are built in, whatever order they were added in: the backshell on
    // the connector, the boot behind it, then whatever is pulled over the wires that come out.
    auto rank = [](PartKind k) {
        switch (k) {
            case PartKind::Backshell:      return 0;
            case PartKind::HeatshrinkBoot: return 1;
            case PartKind::CableGland:
            case PartKind::Grommet:
            case PartKind::Overmould:
            case PartKind::YSplit:         return 2;
            default:                       return mechOverTheWires(k) ? 3 : 2;
        }
    };
    std::stable_sort(n->fittings.begin(), n->fittings.end(),
                     [&rank](const MechFitting& a, const MechFitting& b) {
                         return rank(a.kind) < rank(b.kind);
                     });
    refreshAll();
}

void MechanicalBuilderDialog::removeFitting() {
    MechNode* n = currentNode();
    if (!n || !fittingList_) return;
    QListWidgetItem* item = fittingList_->currentItem();
    if (!item) return;
    const QString id = item->data(Qt::UserRole).toString();
    const bool atEnd = item->data(Qt::UserRole + 1).toBool();
    if (atEnd)
        n->fittings.erase(std::remove_if(n->fittings.begin(), n->fittings.end(),
                                        [&](const MechFitting& f) { return f.id == id; }),
                          n->fittings.end());
    else if (MechSegment* s = currentRun())
        s->coverings.erase(std::remove_if(s->coverings.begin(), s->coverings.end(),
                                         [&](const MechFitting& f) { return f.id == id; }),
                           s->coverings.end());
    refreshAll();
}

void MechanicalBuilderDialog::refreshFittingList() {
    if (!fittingList_) return;
    syncing_ = true;
    fittingList_->clear();
    const MechNode* n = def_.node(selected_);
    const MechSegment* seg = currentRun();
    if (n) {
        for (const MechFitting& f : n->fittings) {
            // Say which it is: a body at the end, or a layer over the conductors that come out of the
            // jacket - they are drawn differently, and reading it off the list saves guessing.
            const QString where = mechOverTheWires(f.kind)
                                      ? QStringLiteral("over the wires")
                                      : QStringLiteral("at the end");
            auto* item = new QListWidgetItem(QStringLiteral("%1  —  %2").arg(f.name, where),
                                             fittingList_);
            item->setData(Qt::UserRole, f.id);
            item->setData(Qt::UserRole + 1, true);
        }
    }
    if (seg) {
        for (const MechFitting& f : seg->coverings) {
            auto* item = new QListWidgetItem(
                QStringLiteral("%1  —  along the run").arg(f.name), fittingList_);
            item->setData(Qt::UserRole, f.id);
            item->setData(Qt::UserRole + 1, false);
        }
    }
    syncing_ = false;
}

// ---- Refresh -------------------------------------------------------------------------------------

void MechanicalBuilderDialog::readOptionsFromControls() {
    if (scaleSpin_) def_.draw.scale = scaleSpin_->value();
    if (factorSpin_) def_.draw.bundleFactor = factorSpin_->value();
    if (exagSpin_) def_.draw.diameterExaggeration = exagSpin_->value();
    if (textSpin_) def_.draw.textHeight = textSpin_->value();
    if (dimsChk_) def_.draw.dimensions = dimsChk_->isChecked();
    if (diaChk_) def_.draw.diameterLabels = diaChk_->isChecked();
    if (countChk_) def_.draw.wireCounts = countChk_->isChecked();
    if (balloonChk_) def_.draw.balloons = balloonChk_->isChecked();
    if (bodiesChk_) def_.draw.connectorBodies = bodiesChk_->isChecked();
    if (spliceChk_) def_.draw.spliceHeatshrink = spliceChk_->isChecked();
}

void MechanicalBuilderDialog::writeOptionsToControls() {
    syncing_ = true;
    if (scaleSpin_) scaleSpin_->setValue(def_.draw.scale);
    if (factorSpin_) factorSpin_->setValue(def_.draw.bundleFactor);
    if (exagSpin_) exagSpin_->setValue(def_.draw.diameterExaggeration);
    if (textSpin_) textSpin_->setValue(def_.draw.textHeight);
    if (dimsChk_) dimsChk_->setChecked(def_.draw.dimensions);
    if (diaChk_) diaChk_->setChecked(def_.draw.diameterLabels);
    if (countChk_) countChk_->setChecked(def_.draw.wireCounts);
    if (balloonChk_) balloonChk_->setChecked(def_.draw.balloons);
    if (bodiesChk_) bodiesChk_->setChecked(def_.draw.connectorBodies);
    if (spliceChk_) spliceChk_->setChecked(def_.draw.spliceHeatshrink);
    syncing_ = false;
}

void MechanicalBuilderDialog::refreshTree() {
    if (!tree_) return;
    syncing_ = true;
    tree_->clear();
    QHash<QString, QTreeWidgetItem*> byNode;
    // Parents before children: a node is added once the one it hangs off is there.
    std::vector<const MechNode*> pending;
    for (const MechNode& n : def_.nodes) pending.push_back(&n);
    bool grew = true;
    while (grew) {
        grew = false;
        for (auto it = pending.begin(); it != pending.end();) {
            const MechNode* n = *it;
            const MechSegment* seg = def_.segment(n->parentSegment);
            QTreeWidgetItem* parentItem = seg ? byNode.value(seg->fromNode, nullptr) : nullptr;
            if (seg && !parentItem) { ++it; continue; }
            auto* item = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(tree_);
            item->setText(0, n->name);
            item->setText(1, mechNodeKindName(n->kind));
            if (seg) {
                item->setText(2, QString::number(seg->lengthMm, 'f', 1));
                item->setText(3, QString::number(n->angleDeg, 'f', 1));
                // What the run IS: the jacket on a cable, the bundle on loose wires - and on a cable
                // whose jacket figure is missing, that it is a cable and the figure is missing, rather
                // than a bundle diameter that is not what anything on it has to go over.
                const double bundleMm = segmentBundleDiameterMm(def_, *seg);
                if (seg->isCable())
                    item->setText(4, QStringLiteral("Ø%1 cable").arg(seg->jacketOdMm, 0, 'f', 1));
                else if (seg->cableWithoutJacket())
                    item->setText(4, QStringLiteral("cable — jacket Ø?"));
                else
                    item->setText(4, QStringLiteral("Ø%1").arg(bundleMm, 0, 'f', 1));
                if (seg->cableWithoutJacket()) item->setForeground(4, QColor("#e6b45a"));
                item->setText(5, QString::number(seg->wireIds.size()));
            } else {
                item->setText(2, QStringLiteral("—"));
            }
            item->setData(0, Qt::UserRole, n->id);
            byNode.insert(n->id, item);
            if (n->id == selected_) tree_->setCurrentItem(item);
            it = pending.erase(it);
            grew = true;
        }
    }
    tree_->expandAll();
    for (int c = 0; c < tree_->columnCount(); ++c) tree_->resizeColumnToContents(c);
    syncing_ = false;
}

void MechanicalBuilderDialog::refreshDetails() {
    syncing_ = true;
    const MechNode* n = def_.node(selected_);
    const MechSegment* seg = currentSegment();
    // The jacket belongs to the run this point is an END of - which at the root is the run that leaves
    // it, so a connector at the start of a trunk has its cable and its stripped length here too.
    bool runAtTo = true;
    const MechSegment* run = currentRun(&runAtTo);
    const bool has = n != nullptr;
    for (QWidget* w : {static_cast<QWidget*>(nodeName_), static_cast<QWidget*>(nodeKind_)})
        if (w) w->setEnabled(has);
    if (lengthSpin_) lengthSpin_->setEnabled(seg != nullptr);
    if (angleSpin_) angleSpin_->setEnabled(seg != nullptr);
    if (nodeName_) nodeName_->setText(has ? n->name : QString());
    if (nodeKind_ && has) {
        const int at = nodeKind_->findData(static_cast<int>(n->kind));
        if (at >= 0) nodeKind_->setCurrentIndex(at);
    }
    if (lengthSpin_) lengthSpin_->setValue(seg ? seg->lengthMm : 0.0);
    if (angleSpin_) angleSpin_->setValue(has ? n->angleDeg : 0.0);

    // The connector, from the library.
    if (connPart_) {
        connPart_->clear();
        connPart_->addItem(QStringLiteral("— none —"), QString());
        for (const LibraryPart* p : catalog_.ofKind(PartKind::Connector))
                connPart_->addItem(p->title(), p->id);
        const int at = has ? connPart_->findData(n->libPart) : 0;
        connPart_->setCurrentIndex(at >= 0 ? at : 0);
        connPart_->setEnabled(has && n->kind == MechNodeKind::Connector);
    }

    if (bundleLabel_) {
        if (!run) bundleLabel_->setText(QStringLiteral("—"));
        else {
            const double d = segmentBundleDiameterMm(def_, *run);
            QString text = QStringLiteral("Ø%1 mm from %2 conductor(s)")
                               .arg(d, 0, 'f', 1)
                               .arg(run->wireIds.size());
            // On a cable, both figures: what the run is, and what it narrows to where the jacket is
            // cut back. That is the pair a boot has to be chosen against.
            if (run->isCable())
                text = QStringLiteral("Ø%1 mm cable, Ø%2 mm from %3 core(s)")
                           .arg(run->jacketOdMm, 0, 'f', 1)
                           .arg(d, 0, 'f', 1)
                           .arg(run->wireIds.size());
            else if (run->cableWithoutJacket())
                // The figure everything fitted to this run is chosen against, and it is missing. Say
                // that here rather than quietly offering the bundle in its place.
                text = QStringLiteral("cable %1 — jacket Ø not given; its %2 core(s) make Ø%3 mm")
                           .arg(run->cableName)
                           .arg(run->wireIds.size())
                           .arg(d, 0, 'f', 1);
            bundleLabel_->setText(text);
        }
    }

    // ---- The cable, and how far its jacket is cut back at this end ----
    if (cablePart_) {
        cablePart_->clear();
        cablePart_->addItem(QStringLiteral("— loose wires —"), QString());
        for (const LibraryPart* p : catalog_.ofKind(PartKind::Cable)) cablePart_->addItem(p->title(), p->id);
        const int at = run ? cablePart_->findData(run->cablePart) : 0;
        cablePart_->setCurrentIndex(at >= 0 ? at : 0);
        cablePart_->setEnabled(run != nullptr);
        if (run && !run->cablePart.isEmpty() && at < 0)
            cablePart_->setToolTip(QStringLiteral(
                "This run's cable is not in any connected library; the jacket diameter beside it is "
                "what the drawing uses."));
    }
    if (jacketSpin_) {
        jacketSpin_->setValue(run ? run->jacketOdMm : 0.0);
        jacketSpin_->setEnabled(run != nullptr);
    }
    if (stripSpin_ && stripFarSpin_) {
        // A run known to be a cable can be stripped whether or not its jacket diameter is in yet: where
        // the jacket STOPS is a different question from what it measures, and both get asked.
        const bool jacketed = run && (run->isCable() || run->cableWithoutJacket());
        stripSpin_->setValue(run ? (runAtTo ? run->stripToMm : run->stripFromMm) : 0.0);
        stripFarSpin_->setValue(run ? (runAtTo ? run->stripFromMm : run->stripToMm) : 0.0);
        stripSpin_->setEnabled(jacketed);
        stripFarSpin_->setEnabled(jacketed);
    }
    const struct { QCheckBox* box; PartKind kind; } ticks[] = {
        {backshellChk_, PartKind::Backshell},
        {bootChk_, PartKind::HeatshrinkBoot},
        {braidChk_, PartKind::ShieldBraid},
        {shrinkChk_, PartKind::Heatshrink},
    };
    for (const auto& t : ticks) {
        if (!t.box) continue;
        t.box->setChecked(hasEndFitting(t.kind));
        t.box->setEnabled(has);
    }

    // Which conductors run through this one.
    if (wireList_) {
        wireList_->clear();
        for (const MechWire& w : def_.wires) {
            const double od = w.odMm > 0.0 ? w.odMm : wireOuterDiameterMm(w.gauge);
            auto* item = new QListWidgetItem(
                od > 0.0 ? QStringLiteral("%1   %2 mm²   Ø%3 mm")
                               .arg(w.label, w.gauge)
                               .arg(od, 0, 'f', 2)
                         : QStringLiteral("%1   (no size stated)").arg(w.label),
                wireList_);
            item->setData(Qt::UserRole, w.id);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(run && run->wireIds.contains(w.id) ? Qt::Checked : Qt::Unchecked);
            if (od <= 0.0) item->setForeground(QColor("#e6b45a"));
        }
        wireList_->setEnabled(run != nullptr);
    }
    refreshFittingList();
    refreshFittingChoices();
    syncing_ = false;
}

void MechanicalBuilderDialog::refreshPreview() {
    if (!preview_) return;
    preview_->setEntities(
        buildMechanicalHarness(def_, &catalog_));
}

void MechanicalBuilderDialog::refreshIssues() {
    if (!issues_) return;
    issues_->clear();
    const std::vector<MechIssue> list =
        validateMechanicalHarness(def_, &catalog_);
    int gaps = 0, violations = 0;
    for (const MechIssue& i : list) {
        QString text;
        switch (i.kind) {
            case MechIssueKind::Gap:
                ++gaps;
                text = QStringLiteral("missing — %1: %2 (%3)")
                           .arg(i.objectName, i.property, i.message);
                break;
            case MechIssueKind::Violation:
                ++violations;
                text = QStringLiteral("will not fit — %1: %2").arg(i.objectName, i.message);
                break;
            case MechIssueKind::Warning:
                text = QStringLiteral("note — %1: %2").arg(i.objectName, i.message);
                break;
        }
        auto* item = new QListWidgetItem(text, issues_);
        item->setForeground(i.kind == MechIssueKind::Violation ? QColor("#e06c6c")
                            : i.kind == MechIssueKind::Gap     ? QColor("#e6b45a")
                                                               : QColor("#9fb4c8"));
    }
    if (summary_ && !list.empty())
        summary_->setText(QStringLiteral("%1 figure(s) missing, %2 that will not fit.")
                              .arg(gaps)
                              .arg(violations));
    else if (summary_ && def_.segments.size() > 0)
        summary_->setText(QStringLiteral("%1 run(s), nothing outstanding.").arg(def_.segments.size()));
}

void MechanicalBuilderDialog::refreshAll() {
    if (nameEdit_ && nameEdit_->text() != def_.name) {
        syncing_ = true;
        nameEdit_->setText(def_.name);
        syncing_ = false;
    }
    refreshTree();
    refreshDetails();
    refreshPreview();
    refreshIssues();
}

// ---- Generate ------------------------------------------------------------------------------------

void MechanicalBuilderDialog::generate() {
    if (!editor_) return;
    const std::vector<Entity> ents =
        buildMechanicalHarness(def_, &catalog_);
    if (ents.empty()) {
        if (summary_) summary_->setText(QStringLiteral("Nothing to generate yet."));
        return;
    }
    // Placed by a click, like anything else that is inserted, so it lands where it is wanted.
    editor_->placeEntities(ents);
    if (summary_)
        summary_->setText(QStringLiteral("%1 entity/ies ready — click on the sheet to place them.")
                              .arg(ents.size()));
}

} // namespace Edraw_Desktop
