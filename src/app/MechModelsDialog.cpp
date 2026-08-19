#include <QFont>
#include <algorithm>
#include "app/MechModelsDialog.h"

#include "app/DrawingPreview.h"
#include "app/MechModelCanvasDialog.h"
#include "app/Editor.h"
#include "harness/PartsLibrary.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <utility>

namespace Edraw_Desktop {

namespace {

// What each model is for, in the words of the thing itself - so it is clear what to draw before it is
// drawn, and clear which end is which when the ends are typed in.
QString hintFor(MechModelKind kind) {
    switch (kind) {
    case MechModelKind::RingInsulated:
    case MechModelKind::RingUninsulated:
        return QStringLiteral(
            "Draw it along +x with the barrel at the left and the ring at the right, at the size it "
            "is. A terminal is not stretched: it is the size it is, and a bigger one is a different "
            "terminal.");
    case MechModelKind::FerruleInsulated:
    case MechModelKind::FerruleUninsulated:
        return QStringLiteral(
            "Along +x, the wire entering at the left and the pin at the right. Not stretched - a "
            "ferrule is the size it is.");
    case MechModelKind::TransitionHeatshrink:
        return QStringLiteral(
            "Over the step from the cable to the conductors coming out of it: the cable end at the "
            "left, the wires' end at the right. The two ends keep their shape at any length; put the "
            "length of each into 'the end that does not stretch'.");
    case MechModelKind::Cable:
        return QStringLiteral(
            "A length of cable: whatever the jacket is drawn as, with its two ends. The ends stay as "
            "drawn however long the cable is on the drawing.");
    case MechModelKind::Wire:
        return QStringLiteral(
            "A length of conductor, drawn along +x. The flush ends are what must not stretch - a wire "
            "drawn 60 long with 4 mm ends, asked for at 500, keeps those two ends and puts 492 of "
            "wire between them.");
    case MechModelKind::OpenWires:
        return QStringLiteral(
            "Conductors running bare, as a set. Draw ONE of them and say how many the drawing shows; "
            "the harness says how many there really are, and they are spaced across rather than the "
            "picture being squashed.");
    case MechModelKind::ConnectorToCableWires:
        return QStringLiteral(
            "The wires between a connector and the cable they come out of: the connector end at the "
            "left. Draw one conductor and say how many are shown.");
    case MechModelKind::YSplit:
        return QStringLiteral(
            "The split itself, drawn about the point the branches leave. Not stretched - it is a "
            "thing of its own size.");
    case MechModelKind::Heatshrink:
        return QStringLiteral(
            "A length of heatshrink over a bundle: its two ends keep their shape, the middle makes up "
            "the length.");
    case MechModelKind::Boot:
        return QStringLiteral(
            "A boot behind a connector: the neck at the left, facing the connector, and the body at "
            "the right over the cable. The taper is what must not stretch.");
    case MechModelKind::CableBody:
        return QStringLiteral(
            "The body of a cable between its ends - what is drawn along the length of it, however long "
            "that is.");
    }
    return QString();
}

bool hasInsulationProperty(MechModelKind kind) {
    return kind == MechModelKind::RingInsulated || kind == MechModelKind::RingUninsulated ||
           kind == MechModelKind::FerruleInsulated || kind == MechModelKind::FerruleUninsulated;
}

QDoubleSpinBox* mmSpin(double max = 2000.0) {
    auto* s = new QDoubleSpinBox;
    s->setRange(0.0, max);
    s->setDecimals(1);
    s->setSuffix(QStringLiteral(" mm"));
    s->setKeyboardTracking(false);
    return s;
}

} // namespace

MechModelsDialog::MechModelsDialog(Editor* editor, QWidget* parent,
                                   std::function<void(MechModelKind, QString)> onEdit)
    : QDialog(parent), editor_(editor), onEdit_(std::move(onEdit)) {
    setWindowTitle(QStringLiteral("Default mechanical models"));
    setModal(false);
    kinds_ = allMechModelKinds();
    set_.load();
    buildUi();
    refreshList();
    if (list_ && list_->count() > 0) list_->setCurrentRow(0);
}

void MechModelsDialog::buildUi() {
    auto* outer = new QHBoxLayout(this);

    // ---- The manager: every model there is, and what can be done to the list ----------------------
    //
    // The list IS the window. A kind holds as many models as an office draws - a straight backshell and a
    // right-angled one are both backshells - so adding, removing, renaming and choosing which one a drawing
    // uses are the commands that belong beside it and nowhere else.
    auto* left = new QVBoxLayout;
    list_ = new QListWidget;
    list_->setMinimumWidth(260);
    list_->setMaximumWidth(320);
    connect(list_, &QListWidget::currentRowChanged, this, [this](int) { refreshDetails(); });
    left->addWidget(list_, 1);

    auto* manage = new QGridLayout;
    manage->setContentsMargins(0, 0, 0, 0);
    auto* add = new QPushButton(QStringLiteral("Add"));
    add->setToolTip(QStringLiteral(
        "Another model of the kind showing - a straight backshell and a right-angled one are both "
        "backshells"));
    connect(add, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Add model"),
                                                   QStringLiteral("What is it called?"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (ok) addModel(name);
    });
    removeBtn_ = new QPushButton(QStringLiteral("Remove"));
    removeBtn_->setToolTip(QStringLiteral("Take this model off the list"));
    connect(removeBtn_, &QPushButton::clicked, this, [this] { removeCurrentModel(); });
    renameBtn_ = new QPushButton(QStringLiteral("Rename"));
    connect(renameBtn_, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Rename model"),
                                                   QStringLiteral("What is it called?"),
                                                   QLineEdit::Normal, currentName(), &ok);
        if (ok) renameCurrentModel(name);
    });
    defaultBtn_ = new QPushButton(QStringLiteral("Make default"));
    defaultBtn_->setToolTip(
        QStringLiteral("What a drawing uses for this kind when the wizard is not told otherwise"));
    connect(defaultBtn_, &QPushButton::clicked, this, [this] { makeCurrentDefault(); });
    manage->addWidget(add, 0, 0);
    manage->addWidget(removeBtn_, 0, 1);
    manage->addWidget(renameBtn_, 1, 0);
    manage->addWidget(defaultBtn_, 1, 1);
    left->addLayout(manage);
    outer->addLayout(left);

    // ---- What the model showing IS ---------------------------------------------------------------
    auto* right = new QVBoxLayout;
    heading_ = new QLabel;
    QFont hf = heading_->font();
    hf.setBold(true);
    hf.setPointSizeF(hf.pointSizeF() * 1.2);
    heading_->setFont(hf);
    right->addWidget(heading_);
    hint_ = new QLabel;
    hint_->setWordWrap(true);
    right->addWidget(hint_);

    // ONE drawing window, the width of the panel, and it shows this model and nothing else. Two of them
    // side by side halved the size of both, and for every kind that is not stretched - a ring, a ferrule, a
    // Y-split - the second one showed the same drawing as the first.
    view_ = new DrawingPreview(QStringLiteral("As drawn"));
    view_->setEmptyText(QStringLiteral("nothing drawn for this yet"));
    view_->setMinimumHeight(260);
    right->addWidget(view_, 1);

    // What the capture MEASURED, and what was marked on the drawing. The drawing knows how long and how
    // thick it is: asking for those in two fields with a magic zero in each ("0 = measure it", "0 = do not
    // scale across") was asking for the drawing to be described to the program a second time.
    auto* takenBox = new QGroupBox(QStringLiteral("Taken from the drawing"));
    auto* takenLay = new QVBoxLayout(takenBox);
    measured_ = new QLabel;
    measured_->setWordWrap(true);
    takenLay->addWidget(measured_);
    scaleAcross_ = new QCheckBox(QStringLiteral("Scale it across to the bundle it goes over"));
    scaleAcross_->setToolTip(QStringLiteral(
        "Drawn at one diameter and used at another: with this on the model is scaled across as well as "
        "along. Off, it is drawn at the size it was captured at."));
    connect(scaleAcross_, &QCheckBox::toggled, this, [this] { applyFigures(); });
    takenLay->addWidget(scaleAcross_);
    placement_ = new QLabel;
    placement_->setWordWrap(true);
    takenLay->addWidget(placement_);
    right->addWidget(takenBox);

    // The figures that cannot be measured - and only the ones this kind has. A row that does not apply is
    // taken away rather than greyed, so what is on the screen is what this model is made of.
    figures_ = new QGroupBox(QStringLiteral("What must not stretch"));
    figuresForm_ = new QFormLayout(figures_);
    head_ = mmSpin();
    connect(head_, &QDoubleSpinBox::valueChanged, this, [this] { applyFigures(); });
    figuresForm_->addRow(QStringLiteral("The end at the left"), head_);
    tail_ = mmSpin();
    connect(tail_, &QDoubleSpinBox::valueChanged, this, [this] { applyFigures(); });
    figuresForm_->addRow(QStringLiteral("The end at the right"), tail_);
    // A cable and a wire are taken AS DRAWN and given a length when the drawing is generated, so the only
    // figure of their own that a run needs is the gap: the piece at each end they do not cover.
    gap_ = mmSpin();
    gap_->setToolTip(QStringLiteral(
        "The piece at each end of the run this does not cover - where the conductors come out. The LENGTH "
        "is not asked for: it comes from the harness when the drawing is generated."));
    connect(gap_, &QDoubleSpinBox::valueChanged, this, [this] { applyFigures(); });
    figuresForm_->addRow(QStringLiteral("Gap left at each end"), gap_);
    // A label is placed ACROSS the runs, not along one, and it has to cover all of them. Flush with the
    // first and last wire it reads as a label that missed them, so it goes a little over - and how much is a
    // house style, the same on every sheet an office releases, not something to be worked out from the width
    // of whatever bundle it happens to land on.
    exceed_ = mmSpin();
    exceed_->setToolTip(QStringLiteral(
        "How far past the first and last line it crosses the label goes, at each end - vertical for a run "
        "drawn across the sheet, and square to the runs whichever way they go. On zero: a proportion of the "
        "label's own size, which gives a different answer on every bundle - set a number and every label on "
        "every sheet clears its bundle by the same amount."));
    connect(exceed_, &QDoubleSpinBox::valueChanged, this, [this] { applyFigures(); });
    figuresForm_->addRow(QStringLiteral("Vertical exceed from the lines"), exceed_);
    wires_ = new QSpinBox;
    wires_->setRange(0, 200);
    wires_->setKeyboardTracking(false);
    connect(wires_, &QSpinBox::valueChanged, this, [this] { applyFigures(); });
    figuresForm_->addRow(QStringLiteral("Conductors the drawing shows"), wires_);
    insulation_ = new QComboBox;
    insulation_->addItems({QStringLiteral("Insulated"), QStringLiteral("Uninsulated")});
    connect(insulation_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (syncing_) return;
        pendingInsulated_ = index == 0;
        if (const MechModel* had = set_.of(currentKind(), currentName())) {
            MechModel m = *had;
            m.insulated = pendingInsulated_;
            set_.set(m);
            set_.save();
            refreshPreview();
        }
    });
    figuresForm_->addRow(QStringLiteral("Insulation"), insulation_);
    right->addWidget(figures_);

    tryBox_ = new QGroupBox(QStringLiteral("Try it"));
    tryForm_ = new QFormLayout(tryBox_);
    tryLen_ = mmSpin(20000.0);
    tryLen_->setValue(250.0);
    connect(tryLen_, &QDoubleSpinBox::valueChanged, this, [this] { refreshPreview(); });
    tryForm_->addRow(QStringLiteral("At this length"), tryLen_);
    tryWires_ = new QSpinBox;
    tryWires_->setRange(0, 200);
    tryWires_->setValue(6);
    tryWires_->setKeyboardTracking(false);
    connect(tryWires_, &QSpinBox::valueChanged, this, [this] { refreshPreview(); });
    tryForm_->addRow(QStringLiteral("With this many conductors"), tryWires_);
    right->addWidget(tryBox_);

    summary_ = new QLabel;
    summary_->setWordWrap(true);
    summary_->setStyleSheet(QStringLiteral("color:#8fb0d6;"));
    right->addWidget(summary_);

    // Two things are done TO a model: it is drawn (on the canvas, with the ordinary tools) and it is taken
    // (captured off the sheet). "Put on the sheet" was a third way of saying Edit, and "Clear" emptied a
    // model that can now simply be removed: both were ways of not choosing.
    auto* buttons = new QHBoxLayout;
    captureBtn_ = new QPushButton(QStringLiteral("Capture"));
    captureBtn_->setToolTip(QStringLiteral(
        "Take the objects on the sheet as this model's drawing - drawn along +x, at the size it is in "
        "millimetres. This window steps aside so they can be picked; Enter takes them, Esc drops it."));
    connect(captureBtn_, &QPushButton::clicked, this, &MechModelsDialog::captureFromSheet);
    buttons->addWidget(captureBtn_);
    auto* edit = new QPushButton(QStringLiteral("Edit on the canvas"));
    edit->setToolTip(QStringLiteral(
        "Open it in the main canvas with every Place and Modify tool - and with Connection point, which is "
        "where the point it connects at and the direction it runs from there are marked"));
    connect(edit, &QPushButton::clicked, this, &MechModelsDialog::editOnCanvas);
    buttons->addWidget(edit);
    buttons->addStretch(1);
    auto* close = new QPushButton(QStringLiteral("Close"));
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    buttons->addWidget(close);
    right->addLayout(buttons);

    outer->addLayout(right, 1);
    resize(1020, 780);
}

void MechModelsDialog::refreshList() {
    if (!list_) return;
    const bool was = syncing_;
    syncing_ = true;
    const int row = list_->currentRow();
    list_->clear();
    rows_.clear();
    for (MechModelKind k : kinds_) {
        std::vector<const MechModel*> models = set_.all(k);
        // THE SUB-DRAWINGS THIS KIND COMES IN, whether or not any of them has been drawn yet. A boot leaves
        // straight, at 45 or at 90; the wires between a connector and its cable are two run straight or two to
        // eight spread. Each is a drawing of its own, and a kind listed as one bare line told you none of that -
        // "draw the eight variants this needs" is not something anybody should have to know.
        //
        // So the standard names are always listed, and one an office has actually drawn simply lands on its own
        // row instead of the placeholder. Anything the office added itself is listed after them.
        const std::vector<QString> standard = defaultSubModelNames(k);
        if (!standard.empty()) {
            auto* head = new QListWidgetItem(mechModelName(k));
            head->setToolTip(hintFor(k));
            QFont hf = head->font();
            hf.setBold(true);
            head->setFont(hf);
            head->setFlags(Qt::ItemIsEnabled);   // a heading, not a thing to select
            list_->addItem(head);
            rows_.push_back({k, QStringLiteral("«heading»")});
            std::vector<QString> shown = standard;
            for (const MechModel* m : models)
                if (!m->name.isEmpty() &&
                    std::find(shown.begin(), shown.end(), m->name) == shown.end())
                    shown.push_back(m->name);
            for (const QString& name : shown) {
                const MechModel* m = set_.of(k, name);
                QString label = QStringLiteral("    ") + name;
                label += (m && !m->isEmpty()) ? QStringLiteral("   ✓") : QStringLiteral("   —");
                if (m && m->isDefault) label += QStringLiteral("   (default)");
                auto* item = new QListWidgetItem(label);
                item->setToolTip(hintFor(k));
                list_->addItem(item);
                rows_.push_back({k, name});
            }
            continue;
        }
        if (models.empty()) {
            auto* item = new QListWidgetItem(mechModelName(k) + QStringLiteral("   —"));
            item->setToolTip(hintFor(k));
            list_->addItem(item);
            rows_.push_back({k, QString()});
            continue;
        }
        for (const MechModel* m : models) {
            QString label = mechModelName(k);
            if (!m->name.isEmpty()) label += QStringLiteral(" · ") + m->name;
            label += m->isEmpty() ? QStringLiteral("   —") : QStringLiteral("   ✓");
            // The one the kind uses when nothing else is said, said plainly rather than by a shade of grey.
            if (m->isDefault && models.size() > 1) label += QStringLiteral("   (default)");
            auto* item = new QListWidgetItem(label);
            item->setToolTip(hintFor(k));
            list_->addItem(item);
            rows_.push_back({k, m->name});
        }
    }
    if (row >= 0 && row < list_->count()) list_->setCurrentRow(row);
    syncing_ = was;
}

MechModelKind MechModelsDialog::currentKind() const {
    const int row = list_ ? list_->currentRow() : -1;
    if (row < 0 || row >= static_cast<int>(rows_.size())) return MechModelKind::Wire;
    return rows_[static_cast<size_t>(row)].kind;
}

QString MechModelsDialog::currentName() const {
    const int row = list_ ? list_->currentRow() : -1;
    if (row < 0 || row >= static_cast<int>(rows_.size())) return QString();
    // A heading is the kind's own name over its sub-drawings, not a model. It cannot be selected - the item is
    // not selectable - but a stale current row could still land on one, and a heading answering as a model name
    // would have the office capturing a drawing into a model called "«heading»".
    const QString name = rows_[static_cast<size_t>(row)].name;
    return name == QStringLiteral("«heading»") ? QString() : name;
}

bool MechModelsDialog::selectKind(MechModelKind kind) {
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].kind == kind && rows_[i].name != QStringLiteral("«heading»")) {
            if (list_) list_->setCurrentRow(static_cast<int>(i));
            return true;
        }
    return false;
}

// Another model of this kind. It starts empty and is drawn or captured like any other - what it inherits is
// the kind, and nothing else, because a second model exists precisely to be different.
void MechModelsDialog::addModel(const QString& name) {
    const MechModelKind kind = currentKind();
    if (name.trimmed().isEmpty()) return;
    if (set_.of(kind, name.trimmed())) {
        if (summary_)
            summary_->setText(QStringLiteral("There is already a %1 called \"%2\"")
                                  .arg(mechModelName(kind), name.trimmed()));
        return;
    }
    MechModel m;
    m.kind = kind;
    m.name = name.trimmed();
    m.insulated = pendingInsulated_;
    // An empty drawing would not be kept by the file, so it is held in the set until something is captured
    // into it: the row is there to capture INTO, which is the whole point of adding one.
    set_.set(m);
    set_.save();
    refreshList();
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].kind == kind && rows_[i].name == m.name && list_)
            list_->setCurrentRow(static_cast<int>(i));
    refreshDetails();
    if (summary_)
        summary_->setText(QStringLiteral("Added %1 \"%2\" — draw it, then Capture")
                              .arg(mechModelName(kind), m.name));
}

void MechModelsDialog::removeCurrentModel() {
    const MechModelKind kind = currentKind();
    const QString name = currentName();
    set_.remove(kind, name);
    set_.save();
    refreshList();
    refreshDetails();
    if (summary_)
        summary_->setText(name.isEmpty()
                              ? QStringLiteral("Removed the %1 model").arg(mechModelName(kind))
                              : QStringLiteral("Removed %1 \"%2\"").arg(mechModelName(kind), name));
}

void MechModelsDialog::renameCurrentModel(const QString& to) {
    const MechModelKind kind = currentKind();
    const QString from = currentName();
    if (to.trimmed().isEmpty() || to.trimmed() == from) return;
    if (!set_.rename(kind, from, to.trimmed())) {
        if (summary_)
            summary_->setText(QStringLiteral("There is already a %1 called \"%2\"")
                                  .arg(mechModelName(kind), to.trimmed()));
        return;
    }
    set_.save();
    refreshList();
    refreshDetails();
}

void MechModelsDialog::makeCurrentDefault() {
    const MechModelKind kind = currentKind();
    if (!set_.setDefault(kind, currentName())) return;
    set_.save();
    refreshList();
    refreshDetails();
    if (summary_)
        summary_->setText(QStringLiteral("%1 is what a drawing uses for %2 now")
                              .arg(currentName().isEmpty() ? QStringLiteral("This one") : currentName(),
                                   mechModelName(kind)));
}

MechModel* MechModelsDialog::current() {
    static MechModel scratch;
    const MechModelKind kind = currentKind();
    if (const MechModel* m = set_.of(kind, currentName())) {
        scratch = *m;
        return &scratch;
    }
    scratch = MechModel();
    scratch.kind = kind;
    scratch.insulated = pendingInsulated_;
    return &scratch;
}

void MechModelsDialog::refreshDetails() {
    const MechModelKind kind = currentKind();
    const MechModel* m = set_.of(kind, currentName());
    syncing_ = true;
    if (heading_)
        heading_->setText(currentName().isEmpty()
                              ? mechModelName(kind)
                              : QStringLiteral("%1 - %2").arg(mechModelName(kind), currentName()));
    if (hint_) hint_->setText(hintFor(kind));

    // Which rows this kind even has. A model of a thing that is only placed has no ends that must not
    // stretch; one that is not drawn per conductor has no conductor count; only a ring or a ferrule is
    // insulated or not. Taken away rather than greyed: a row that can never be filled in is furniture.
    const bool stretches = mechModelStretches(kind);
    const bool perWire = mechModelPerWire(kind);
    const bool hasGap = MechModel::kindHasGap(kind);
    const bool hasExceed = MechModel::kindHasExceed(kind);
    if (figuresForm_) {
        figuresForm_->setRowVisible(head_, stretches);
        figuresForm_->setRowVisible(tail_, stretches);
        figuresForm_->setRowVisible(gap_, hasGap);
        figuresForm_->setRowVisible(exceed_, hasExceed);
        figuresForm_->setRowVisible(wires_, perWire);
        figuresForm_->setRowVisible(insulation_, hasInsulationProperty(kind));
    }
    if (figures_)
        figures_->setVisible(stretches || hasGap || hasExceed || perWire ||
                             hasInsulationProperty(kind));
    if (tryForm_) {
        tryForm_->setRowVisible(tryLen_, stretches);
        tryForm_->setRowVisible(tryWires_, perWire);
    }
    if (tryBox_) tryBox_->setVisible(stretches || perWire);

    if (m && hasInsulationProperty(kind)) pendingInsulated_ = m->insulated;
    if (insulation_) insulation_->setCurrentIndex(pendingInsulated_ ? 0 : 1);
    if (head_) head_->setValue(m ? m->headMm : 0.0);
    if (tail_) tail_->setValue(m ? m->tailMm : 0.0);
    if (gap_) gap_->setValue(m ? m->gapMm : 0.0);
    if (exceed_) exceed_->setValue(m ? m->exceedMm : 0.0);
    if (wires_) wires_->setValue(m ? m->drawnWireCount : 0);
    if (scaleAcross_) scaleAcross_->setChecked(m && m->drawnAcrossMm > 0.0);

    // What the drawing itself says. Measured on capture, said here - not asked for.
    if (measured_) {
        if (!m || m->isEmpty()) {
            measured_->setText(QStringLiteral("Nothing drawn for this yet."));
        } else {
            const double len = mechModelDrawnLength(*m);
            QString said = QStringLiteral("%1 object(s), drawn %2 mm long")
                               .arg(m->drawing.size())
                               .arg(len, 0, 'f', 1);
            if (m->drawnAcrossMm > 0.0)
                said += QStringLiteral(" and %1 mm across").arg(m->drawnAcrossMm, 0, 'f', 1);
            measured_->setText(said);
        }
    }
    // And what was marked on it: where it connects, and which way it runs from there. Both are what the
    // wizard places the part by, so a model that has neither says so plainly rather than leaving it to be
    // found out when a drawing comes out wrong.
    if (placement_) {
        if (m && m->hasConnect)
            placement_->setText(
                QStringLiteral("Connects at %1, %2 and runs %3° from there")
                    .arg(m->connectPoint.x, 0, 'f', 1)
                    .arg(m->connectPoint.y, 0, 'f', 1)
                    .arg(m->connectAngleRad * 180.0 / 3.14159265358979323846, 0, 'f', 1));
        else
            placement_->setText(QStringLiteral(
                "No connection point marked — it is placed from its left-hand end, along the run. Edit it on "
                "the canvas and press Connection point to mark one."));
    }
    if (removeBtn_) removeBtn_->setEnabled(m != nullptr);
    if (renameBtn_) renameBtn_->setEnabled(m != nullptr);
    if (defaultBtn_)
        defaultBtn_->setEnabled(m != nullptr && set_.all(kind).size() > 1 && !m->isDefault);
    syncing_ = false;
    refreshPreview();
}

void MechModelsDialog::refreshPreview() {
    const MechModelKind kind = currentKind();
    const MechModel* m = set_.of(kind, currentName());
    if (!m) {
        if (view_) {
            view_->setEntities({});
            view_->setCaption(QStringLiteral("As drawn"));
        }
        if (summary_)
            summary_->setText(QStringLiteral("Nothing drawn for this yet — draw it on the sheet, "
                                             "then select them and press Capture"));
        return;
    }
    // The one window shows the model AS IT WILL BE DRAWN: at the length being tried when the kind is run to
    // a length, and as it was captured when it is only placed. The caption says which, so what is on the
    // screen is never in doubt.
    const bool stretches = mechModelStretches(kind);
    const double want = stretches && tryLen_ ? tryLen_->value() : mechModelDrawnLength(*m);
    const int wires = mechModelPerWire(kind) && tryWires_ ? tryWires_->value() : 0;
    if (view_) {
        view_->setEntities(mechModelEntities(*m, Vec2{0.0, 0.0}, 0.0, want, 0.0, wires));
        view_->setCaption(stretches ? tr("At %1 mm").arg(want, 0, 'f', 0) : tr("As drawn"));
    }
    if (summary_) {
        const double drawn = mechModelDrawnLength(*m);
        QString s = QStringLiteral("Drawn %1 mm long").arg(drawn, 0, 'f', 1);
        if (mechModelStretches(kind))
            s += QStringLiteral(", ends of %1 and %2 mm kept, %3 mm of it stretches")
                     .arg(m->headMm, 0, 'f', 1)
                     .arg(m->tailMm, 0, 'f', 1)
                     .arg(std::max(0.0, drawn - m->headMm - m->tailMm), 0, 'f', 1);
        else
            s += QStringLiteral(" — placed at that size, not stretched");
        summary_->setText(s);
    }
}

void MechModelsDialog::applyFigures() {
    if (syncing_) return;
    const MechModelKind kind = currentKind();
    const MechModel* had = set_.of(kind, currentName());
    if (!had) return;   // nothing drawn: the figures have nothing to be about yet
    MechModel m = *had;
    m.headMm = head_ && mechModelStretches(kind) ? head_->value() : 0.0;
    m.tailMm = tail_ && mechModelStretches(kind) ? tail_->value() : 0.0;
    m.gapMm = gap_ && MechModel::kindHasGap(kind) ? gap_->value() : 0.0;
    m.exceedMm = exceed_ && MechModel::kindHasExceed(kind) ? exceed_->value() : 0.0;
    m.drawnWireCount = wires_ && mechModelPerWire(kind) ? wires_->value() : 0;
    // Scaling across is a yes or no, and the size it was drawn at is measured: the two used to be one field
    // where zero meant no, which is a number standing in for a decision.
    m.drawnAcrossMm = scaleAcross_ && scaleAcross_->isChecked() ? mechModelDrawnAcross(m) : 0.0;
    set_.set(m);
    set_.save();
    refreshDetails();
}

void MechModelsDialog::captureFromSheet() {
    if (!editor_ || !editor_->document()) return;
    if (editor_->copyOfSelection().empty()) {
        capturing_ = true;
        hide();
        emit captureArmed(mechModelName(currentKind()));
        return;
    }
    captureSelection();
}

void MechModelsDialog::finishCaptureFromSheet() {
    if (!capturing_) return;
    capturing_ = false;
    show();
    raise();
    captureSelection();
    emit captureFinished();
}

void MechModelsDialog::cancelCaptureFromSheet() {
    if (!capturing_) return;
    capturing_ = false;
    show();
    raise();
    if (summary_) summary_->setText(QStringLiteral("Capture cancelled — the model is unchanged."));
    emit captureFinished();
}

void MechModelsDialog::captureSelection() {
    if (!editor_ || !editor_->document()) return;
    std::vector<Entity> drawn;
    for (const Entity& e : editor_->copyOfSelection())
        if (e.layer != QLatin1String("«guide»")) drawn.push_back(e);
    if (drawn.empty()) {
        if (summary_)
            summary_->setText(QStringLiteral(
                "Nothing was selected — pick the objects that make up the model, then capture them"));
        return;
    }
    MechModel m;
    m.kind = currentKind();
    m.insulated = pendingInsulated_;
    if (const MechModel* had = set_.of(m.kind, currentName())) m = *had;
    m.name = currentName();   // into the model showing, not over the kind's first one
    m.drawing = partDrawingFromEntities(drawn);
    // TAKEN FROM THE CAPTURE: how long it is, and how thick. The drawing knows both, so neither is asked
    // for - and the length in particular was a field with a magic zero in it that nobody could check
    // against the drawing.
    m.drawnLengthMm = 0.0;                       // 0 = measured off the drawing, which is what it is
    if (m.drawnAcrossMm > 0.0 || (scaleAcross_ && scaleAcross_->isChecked()))
        m.drawnAcrossMm = mechModelDrawnAcross(m);
    set_.set(m);
    set_.save();
    refreshList();
    refreshDetails();
    if (summary_)
        summary_->setText(QStringLiteral("Captured %1 selected object(s) as %2")
                              .arg(drawn.size())
                              .arg(mechModelName(m.kind)));
}

void MechModelsDialog::editOnCanvas() {
    const MechModelKind kind = currentKind();
    if (onEdit_) {
        onEdit_(kind, currentName());   // the model showing, not the kind's default
        hide();
        return;
    }
    MechModel m;
    m.kind = kind;
    m.insulated = pendingInsulated_;
    if (const MechModel* had = set_.of(kind)) m = *had;   // what is there, to be changed
    MechModelCanvasDialog dlg(m, this);
    if (dlg.exec() != QDialog::Accepted) {
        if (summary_) summary_->setText(QStringLiteral("Left as it was"));
        return;
    }
    const QJsonArray drawn = dlg.drawing();
    if (drawn.isEmpty()) {
        if (summary_) summary_->setText(QStringLiteral("Nothing was drawn — left as it was"));
        return;
    }
    m.drawing = drawn;
    // Measured off what was drawn, so the figures are the drawing's own rather than something typed
    // that has to be kept in step with it by hand. The ends that must not stretch stay as they were:
    // they are a decision about the model, not a measurement of it.
    m.drawnLengthMm = dlg.drawnLengthMm();
    m.drawnAcrossMm = dlg.drawnAcrossMm();
    set_.set(m);
    set_.save();
    refreshList();
    refreshDetails();
    if (summary_)
        summary_->setText(QStringLiteral("Drawn %1 mm long and %2 mm across — now say which ends must "
                                         "not stretch")
                              .arg(m.drawnLengthMm, 0, 'f', 1)
                              .arg(m.drawnAcrossMm, 0, 'f', 1));
}

void MechModelsDialog::setModels(const MechModelSet& set) {
    set_ = set;
    refreshList();
    refreshDetails();
}

} // namespace Edraw_Desktop
