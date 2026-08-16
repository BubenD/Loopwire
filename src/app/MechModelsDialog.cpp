#include "app/MechModelsDialog.h"

#include "app/DrawingPreview.h"
#include "app/MechModelCanvasDialog.h"
#include "app/Editor.h"
#include "harness/PartsLibrary.h"

#include <QCheckBox>
#include <QComboBox>
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
                                   std::function<void(MechModelKind)> onEdit)
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

    list_ = new QListWidget;
    list_->setMinimumWidth(250);
    list_->setMaximumWidth(300);
    connect(list_, &QListWidget::currentRowChanged, this, [this](int) { refreshDetails(); });
    outer->addWidget(list_);

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

    propertiesGroup_ = new QGroupBox(QStringLiteral("Properties"));
    auto* pf = new QFormLayout(propertiesGroup_);
    insulation_ = new QComboBox;
    insulation_->addItems({QStringLiteral("Insulated"), QStringLiteral("Uninsulated")});
    connect(insulation_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (syncing_) return;
        pendingInsulated_ = index == 0;
        const MechModelKind kind = currentKind();
        if (const MechModel* had = set_.of(kind)) {
            MechModel m = *had;
            m.insulated = pendingInsulated_;
            set_.set(m);
            set_.save();
            refreshPreview();
        }
    });
    pf->addRow(QStringLiteral("Insulation"), insulation_);
    right->addWidget(propertiesGroup_);

    // The two views: what was drawn, and the same thing at the length it is asked for.
    auto* views = new QHBoxLayout;
    asDrawn_ = new DrawingPreview(QStringLiteral("As drawn"));
    asDrawn_->setEmptyText(QStringLiteral("nothing drawn for this yet"));
    views->addWidget(asDrawn_, 1);
    stretched_ = new DrawingPreview(QStringLiteral("At the length asked for"));
    stretched_->setEmptyText(QStringLiteral("—"));
    views->addWidget(stretched_, 1);
    right->addLayout(views, 1);

    auto* figures = new QGroupBox(QStringLiteral("What must not stretch"));
    auto* ff = new QFormLayout(figures);
    drawnLen_ = mmSpin();
    connect(drawnLen_, &QDoubleSpinBox::valueChanged, this, [this] { applyFigures(); });
    ff->addRow(QStringLiteral("Drawn at this length (0 = measure it)"), drawnLen_);
    head_ = mmSpin();
    connect(head_, &QDoubleSpinBox::valueChanged, this, [this] { applyFigures(); });
    ff->addRow(QStringLiteral("The end at the left"), head_);
    tail_ = mmSpin();
    connect(tail_, &QDoubleSpinBox::valueChanged, this, [this] { applyFigures(); });
    ff->addRow(QStringLiteral("The end at the right"), tail_);
    across_ = mmSpin(500.0);
    connect(across_, &QDoubleSpinBox::valueChanged, this, [this] { applyFigures(); });
    ff->addRow(QStringLiteral("Drawn at this diameter (0 = do not scale across)"), across_);
    wires_ = new QSpinBox;
    wires_->setRange(0, 200);
    wires_->setKeyboardTracking(false);
    connect(wires_, &QSpinBox::valueChanged, this, [this] { applyFigures(); });
    ff->addRow(QStringLiteral("Conductors the drawing shows"), wires_);
    // A cable and a wire are taken AS DRAWN and given a length when the harness drawing is generated, so the
    // only figure of their own that a run needs is the gap: the piece at each end they do not cover, where
    // the conductors come out and are worked on.
    gap_ = mmSpin();
    gap_->setToolTip(QStringLiteral(
        "The piece at each end of the run this does not cover — where the conductors come out. The LENGTH is "
        "not asked for here: it comes from the harness when the drawing is generated."));
    connect(gap_, &QDoubleSpinBox::valueChanged, this, [this] { applyFigures(); });
    ff->addRow(QStringLiteral("Gap left at each end"), gap_);
    right->addWidget(figures);

    auto* tryBox = new QGroupBox(QStringLiteral("Try it"));
    auto* tf = new QFormLayout(tryBox);
    tryLen_ = mmSpin(20000.0);
    tryLen_->setValue(250.0);
    connect(tryLen_, &QDoubleSpinBox::valueChanged, this, [this] { refreshPreview(); });
    tf->addRow(QStringLiteral("At this length"), tryLen_);
    tryWires_ = new QSpinBox;
    tryWires_->setRange(0, 200);
    tryWires_->setValue(6);
    tryWires_->setKeyboardTracking(false);
    connect(tryWires_, &QSpinBox::valueChanged, this, [this] { refreshPreview(); });
    tf->addRow(QStringLiteral("With this many conductors"), tryWires_);
    right->addWidget(tryBox);

    summary_ = new QLabel;
    summary_->setWordWrap(true);
    summary_->setStyleSheet(QStringLiteral("color:#8fb0d6;"));
    right->addWidget(summary_);

    auto* buttons = new QHBoxLayout;
    captureBtn_ = new QPushButton(QStringLiteral("Capture"));
    captureBtn_->setToolTip(QStringLiteral(
        "Take the objects on the sheet as this model's drawing — drawn along +x, at the size it is in "
        "millimetres. This window steps aside so they can be picked; Enter takes them, Esc drops it."));
    connect(captureBtn_, &QPushButton::clicked, this, &MechModelsDialog::captureFromSheet);
    buttons->addWidget(captureBtn_);
    auto* edit = new QPushButton(QStringLiteral("Edit"));
    edit->setToolTip(QStringLiteral(
        "Open it in the main canvas with all Place and Modify tools available"));
    connect(edit, &QPushButton::clicked, this, &MechModelsDialog::editOnCanvas);
    buttons->addWidget(edit);
    auto* ins = new QPushButton(QStringLiteral("Put on the sheet"));
    ins->setToolTip(QStringLiteral("Draw it on the sheet to change it, then capture it again"));
    connect(ins, &QPushButton::clicked, this, &MechModelsDialog::insertIntoDrawing);
    buttons->addWidget(ins);
    // Managing the models themselves: another of this kind, this one gone, renamed, or made the one a
    // drawing uses. A window that could only fill in a fixed list of kinds could not hold a second backshell.
    auto* add = new QPushButton(QStringLiteral("Add model"));
    add->setToolTip(QStringLiteral(
        "Another model of this kind - a straight backshell and a right-angled one are both backshells"));
    connect(add, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Add model"),
                                                   QStringLiteral("What is it called?"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (ok) addModel(name);
    });
    buttons->addWidget(add);
    auto* rm = new QPushButton(QStringLiteral("Remove model"));
    rm->setToolTip(QStringLiteral("Take this model off the list"));
    connect(rm, &QPushButton::clicked, this, [this] { removeCurrentModel(); });
    buttons->addWidget(rm);
    auto* ren = new QPushButton(QStringLiteral("Rename"));
    connect(ren, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Rename model"),
                                                   QStringLiteral("What is it called?"),
                                                   QLineEdit::Normal, currentName(), &ok);
        if (ok) renameCurrentModel(name);
    });
    buttons->addWidget(ren);
    auto* def = new QPushButton(QStringLiteral("Make default"));
    def->setToolTip(QStringLiteral("What a drawing uses for this kind when nothing else is said"));
    connect(def, &QPushButton::clicked, this, [this] { makeCurrentDefault(); });
    buttons->addWidget(def);
    auto* clr = new QPushButton(QStringLiteral("Clear"));
    connect(clr, &QPushButton::clicked, this, &MechModelsDialog::clearModel);
    buttons->addWidget(clr);
    buttons->addStretch(1);
    auto* close = new QPushButton(QStringLiteral("Close"));
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    buttons->addWidget(close);
    right->addLayout(buttons);

    outer->addLayout(right, 1);
    resize(1020, 760);
}

// A row per MODEL, not per kind: an office that draws a straight backshell and a right-angled one keeps two,
// and both have to be visible to be worked on. A kind with nothing drawn still gets its row - the point of the
// window is to fill those in.
void MechModelsDialog::refreshList() {
    if (!list_) return;
    const bool was = syncing_;
    syncing_ = true;
    const int row = list_->currentRow();
    list_->clear();
    rows_.clear();
    for (MechModelKind k : kinds_) {
        const std::vector<const MechModel*> models = set_.all(k);
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
    return rows_[static_cast<size_t>(row)].name;
}

bool MechModelsDialog::selectKind(MechModelKind kind) {
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].kind == kind) {
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
    const MechModel* m = set_.of(kind);
    syncing_ = true;
    if (heading_) heading_->setText(mechModelName(kind));
    if (hint_) hint_->setText(hintFor(kind));
    if (propertiesGroup_) propertiesGroup_->setVisible(hasInsulationProperty(kind));
    if (m && hasInsulationProperty(kind)) pendingInsulated_ = m->insulated;
    if (insulation_) {
        QSignalBlocker block(insulation_);
        insulation_->setCurrentIndex(pendingInsulated_ ? 0 : 1);
    }
    if (asDrawn_) asDrawn_->setDrawing(m ? m->drawing : QJsonArray());
    if (drawnLen_) drawnLen_->setValue(m ? m->drawnLengthMm : 0.0);
    if (gap_) gap_->setValue(m ? m->gapMm : 0.0);
    if (head_) head_->setValue(m ? m->headMm : 0.0);
    if (tail_) tail_->setValue(m ? m->tailMm : 0.0);
    if (across_) across_->setValue(m ? m->drawnAcrossMm : 0.0);
    if (wires_) wires_->setValue(m ? m->drawnWireCount : 0);
    // A thing that is not stretched has no ends to keep and no length to try.
    const bool stretches = mechModelStretches(kind);
    for (QWidget* w : {static_cast<QWidget*>(head_), static_cast<QWidget*>(tail_),
                       static_cast<QWidget*>(tryLen_)})
        if (w) w->setEnabled(stretches);
    const bool perWire = mechModelPerWire(kind);
    if (wires_) wires_->setEnabled(perWire);
    if (tryWires_) tryWires_->setEnabled(perWire);
    // The gap belongs to the two kinds that are drawn to a length the harness decides and leave one.
    if (gap_) gap_->setEnabled(MechModel::kindHasGap(kind));
    syncing_ = false;
    refreshPreview();
}

void MechModelsDialog::refreshPreview() {
    const MechModelKind kind = currentKind();
    const MechModel* m = set_.of(kind, currentName());
    if (!m) {
        if (stretched_) stretched_->setEntities({});
        if (summary_)
            summary_->setText(QStringLiteral("Nothing drawn for this yet — draw it on the sheet, "
                                             "then select them and press Capture"));
        return;
    }
    const double want = mechModelStretches(kind) && tryLen_ ? tryLen_->value()
                                                            : mechModelDrawnLength(*m);
    const int wires = mechModelPerWire(kind) && tryWires_ ? tryWires_->value() : 0;
    if (stretched_)
        stretched_->setEntities(mechModelEntities(*m, Vec2{0.0, 0.0}, 0.0, want, 0.0, wires));
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
    m.drawnLengthMm = drawnLen_ ? drawnLen_->value() : 0.0;
    m.gapMm = gap_ && MechModel::kindHasGap(m.kind) ? gap_->value() : 0.0;
    m.headMm = head_ ? head_->value() : 0.0;
    m.tailMm = tail_ ? tail_->value() : 0.0;
    m.drawnAcrossMm = across_ ? across_->value() : 0.0;
    m.drawnWireCount = wires_ ? wires_->value() : 0;
    set_.set(m);
    set_.save();
    refreshPreview();
}

// Capture what is SELECTED. Nothing selected yet, so the window steps aside and the sheet is left clear to
// pick on - the same way the parts library captures a part's drawing. Taking the whole sheet was the only way
// before, which meant a model could not be drawn beside anything else, not even beside the thing it goes on.
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
    set_.set(m);
    set_.save();
    refreshList();
    refreshDetails();
    if (summary_)
        summary_->setText(QStringLiteral("Captured %1 selected object(s) as %2")
                              .arg(drawn.size())
                              .arg(mechModelName(m.kind)));
}

void MechModelsDialog::insertIntoDrawing() {
    if (!editor_) return;
    const MechModel* m = set_.of(currentKind());
    if (!m) {
        if (summary_) summary_->setText(QStringLiteral("Nothing drawn for this yet"));
        return;
    }
    editor_->placeEntities(partDrawingEntities(m->drawing));
    if (summary_)
        summary_->setText(QStringLiteral("Click the sheet to put it down — change it there and press "
                                         "select it and press Capture to take it back"));
    hide();
}

void MechModelsDialog::editOnCanvas() {
    const MechModelKind kind = currentKind();
    if (onEdit_) {
        onEdit_(kind);
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

void MechModelsDialog::clearModel() {
    const MechModelKind kind = currentKind();
    set_.remove(kind);
    set_.save();
    refreshList();
    refreshDetails();
    if (summary_)
        summary_->setText(QStringLiteral("%1 cleared — the generator draws its own until one is "
                                         "captured again")
                              .arg(mechModelName(kind)));
}

void MechModelsDialog::setModels(const MechModelSet& set) {
    set_ = set;
    refreshList();
    refreshDetails();
}

} // namespace Edraw_Desktop
