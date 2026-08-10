#include "app/MechModelsDialog.h"

#include "app/DrawingPreview.h"
#include "app/MechModelCanvasDialog.h"
#include "app/Editor.h"
#include "harness/PartsLibrary.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

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

QDoubleSpinBox* mmSpin(double max = 2000.0) {
    auto* s = new QDoubleSpinBox;
    s->setRange(0.0, max);
    s->setDecimals(1);
    s->setSuffix(QStringLiteral(" mm"));
    s->setKeyboardTracking(false);
    return s;
}

} // namespace

MechModelsDialog::MechModelsDialog(Editor* editor, QWidget* parent)
    : QDialog(parent), editor_(editor) {
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
    captureBtn_ = new QPushButton(QStringLiteral("Capture selection"));
    captureBtn_->setToolTip(QStringLiteral(
        "Take what is selected on the sheet as this model's drawing — drawn along +x, at the size it "
        "is in millimetres"));
    connect(captureBtn_, &QPushButton::clicked, this, &MechModelsDialog::captureFromSheet);
    buttons->addWidget(captureBtn_);
    auto* edit = new QPushButton(QStringLiteral("Edit"));
    edit->setToolTip(QStringLiteral(
        "Draw it on a canvas of its own — lines, polylines, rectangles, circles and arcs, with the ends "
        "that must not stretch marked while you work"));
    connect(edit, &QPushButton::clicked, this, &MechModelsDialog::editOnCanvas);
    buttons->addWidget(edit);
    auto* ins = new QPushButton(QStringLiteral("Put on the sheet"));
    ins->setToolTip(QStringLiteral("Draw it on the sheet to change it, then capture it again"));
    connect(ins, &QPushButton::clicked, this, &MechModelsDialog::insertIntoDrawing);
    buttons->addWidget(ins);
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

void MechModelsDialog::refreshList() {
    if (!list_) return;
    const bool was = syncing_;
    syncing_ = true;
    const int row = list_->currentRow();
    list_->clear();
    for (MechModelKind k : kinds_) {
        const MechModel* m = set_.of(k);
        QString label = mechModelName(k);
        // Which of them are drawn and which are still to do, at a glance: the point of the window is
        // to fill the list in.
        label += m ? QStringLiteral("   ✓") : QStringLiteral("   —");
        auto* item = new QListWidgetItem(label);
        item->setToolTip(hintFor(k));
        list_->addItem(item);
    }
    if (row >= 0 && row < list_->count()) list_->setCurrentRow(row);
    syncing_ = was;
}

MechModelKind MechModelsDialog::currentKind() const {
    const int row = list_ ? list_->currentRow() : -1;
    if (row < 0 || row >= static_cast<int>(kinds_.size())) return MechModelKind::Wire;
    return kinds_[static_cast<size_t>(row)];
}

bool MechModelsDialog::selectKind(MechModelKind kind) {
    for (size_t i = 0; i < kinds_.size(); ++i)
        if (kinds_[i] == kind) {
            if (list_) list_->setCurrentRow(static_cast<int>(i));
            return true;
        }
    return false;
}

MechModel* MechModelsDialog::current() {
    static MechModel scratch;
    const MechModelKind kind = currentKind();
    if (const MechModel* m = set_.of(kind)) {
        scratch = *m;
        return &scratch;
    }
    scratch = MechModel();
    scratch.kind = kind;
    return &scratch;
}

void MechModelsDialog::refreshDetails() {
    const MechModelKind kind = currentKind();
    const MechModel* m = set_.of(kind);
    syncing_ = true;
    if (heading_) heading_->setText(mechModelName(kind));
    if (hint_) hint_->setText(hintFor(kind));
    if (asDrawn_) asDrawn_->setDrawing(m ? m->drawing : QJsonArray());
    if (drawnLen_) drawnLen_->setValue(m ? m->drawnLengthMm : 0.0);
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
    syncing_ = false;
    refreshPreview();
}

void MechModelsDialog::refreshPreview() {
    const MechModelKind kind = currentKind();
    const MechModel* m = set_.of(kind);
    if (!m) {
        if (stretched_) stretched_->setEntities({});
        if (summary_)
            summary_->setText(QStringLiteral("Nothing drawn for this yet — draw it on the sheet, "
                                             "select it, and press Capture selection"));
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
    const MechModel* had = set_.of(kind);
    if (!had) return;   // nothing drawn: the figures have nothing to be about yet
    MechModel m = *had;
    m.drawnLengthMm = drawnLen_ ? drawnLen_->value() : 0.0;
    m.headMm = head_ ? head_->value() : 0.0;
    m.tailMm = tail_ ? tail_->value() : 0.0;
    m.drawnAcrossMm = across_ ? across_->value() : 0.0;
    m.drawnWireCount = wires_ ? wires_->value() : 0;
    set_.set(m);
    set_.save();
    refreshPreview();
}

void MechModelsDialog::captureFromSheet() {
    if (!editor_) return;
    const std::vector<Entity> sel = editor_->copyOfSelection();
    if (sel.empty()) {
        if (summary_)
            summary_->setText(QStringLiteral("Select the objects on the sheet first — then capture "
                                             "them as this model"));
        return;
    }
    MechModel m;
    m.kind = currentKind();
    if (const MechModel* had = set_.of(m.kind)) m = *had;   // keep the figures already given
    m.drawing = partDrawingFromEntities(sel);
    set_.set(m);
    set_.save();
    refreshList();
    refreshDetails();
    if (summary_)
        summary_->setText(QStringLiteral("Captured %1 object(s) as %2 — now say which ends must not "
                                         "stretch")
                              .arg(sel.size())
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
                                         "Capture selection to take it back"));
    hide();
}

void MechModelsDialog::editOnCanvas() {
    const MechModelKind kind = currentKind();
    MechModel m;
    m.kind = kind;
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
