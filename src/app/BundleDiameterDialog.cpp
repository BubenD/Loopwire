#include "app/BundleDiameterDialog.h"

#include "app/UiBits.h"
#include "app/Editor.h"
#include "harness/PartsLibrary.h"
#include "model/Document.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QGuiApplication>
#include <QInputDialog>
#include <QJsonObject>
#include <QListWidget>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {
namespace {

// The columns, named. A bundle is described by more than a diameter now - what each conductor is
// called, how many of it there are, its cross-section in whichever unit it is quoted in - so the
// numbers are given names rather than being counted along the header.
enum Col {
    kColNo = 0,      // the line number: what a conductor is referred to by in a note
    kColName,
    kColAmount,      // how many identical conductors this row stands for
    kColGauge,       // the cross-section, in the unit beside it
    kColGaugeUnit,   // AWG or mm2
    kColOdMm,
    kColOdIn,
    kColCount
};

// THE SIZES A HARNESS IS BUILT FROM, thickest first - 8 AWG down to 32, which is the range the
// conversion table covers and therefore the range the two units can be swapped between without
// anything falling off the end. Only the sizes that HAVE a cross-section on that table are offered:
// an entry the arithmetic cannot turn into mm2 is an entry that reads as an empty diameter.
const int kGaugeAwgSizes[] = {8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32};

// How a mm2 figure is written in this window. The same rule the unit switch uses, so a size picked
// from the list and the same size arrived at by changing the unit are the same text: three places at
// the thin end, two at the thick, because 0.081 and 0.08 are not the same wire and 8.37 needs no
// third place.
QString gaugeMm2Text(double mm2) {
    return QString::number(mm2, 'f', mm2 < 1.0 ? 3 : 2);
}

// The list a gauge cell drops down, in the unit that row is written in.
QStringList gaugeChoices(bool awg) {
    QStringList out;
    for (int size : kGaugeAwgSizes) {
        if (awg) {
            out << QString::number(size);
        } else {
            const double mm2 = mechAwgMm2(size);
            if (mm2 > 0.0) out << gaugeMm2Text(mm2);
        }
    }
    return out;
}

// A GAUGE IS CHOSEN FROM THE SIZES THAT EXIST, OR TYPED.
//
// It was a plain text cell, so every cross-section in the bundle was spelled out by hand - and a
// cross-section is not free text: there are fifteen of them a harness is ever built from, and typing
// one is a chance to type "0.35" where the table says "0.34" and get a diameter for a wire that is
// not the wire. The list removes the chance for the common case.
//
// It stays EDITABLE, because the common case is not the only one. A conductor that is not a standard
// size - a bus bar, something off a data sheet, a figure somebody measured - still has to go in the
// bundle, and a picker that will not accept it is a picker that sends the person back to a
// calculator. So the drop-down offers the sizes and the box takes anything.
//
// The list follows the ROW's unit, not the window's: AWG and mm2 sit side by side in the same table
// because a bundle is regularly made of some of each.
class GaugeDelegate : public QStyledItemDelegate {
public:
    GaugeDelegate(QTableWidget* table, int unitColumn, QObject* parent)
        : QStyledItemDelegate(parent), table_(table), unitColumn_(unitColumn) {}

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&,
                          const QModelIndex& index) const override {
        auto* box = new QComboBox(parent);
        box->setEditable(true);
        box->setInsertPolicy(QComboBox::NoInsert);   // typing a size does not grow the list
        box->addItems(gaugeChoices(rowIsAwg(index.row())));
        return box;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        auto* box = qobject_cast<QComboBox*>(editor);
        if (!box) return QStyledItemDelegate::setEditorData(editor, index);
        const QString had = index.data(Qt::EditRole).toString().trimmed();
        // What is already in the cell wins over the list: a hand-typed 0.35 opens as 0.35 and not as
        // whichever standard size happens to be nearest it.
        const int at = box->findText(had);
        if (at >= 0) box->setCurrentIndex(at);
        else box->setEditText(had);
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override {
        auto* box = qobject_cast<QComboBox*>(editor);
        if (!box) return QStyledItemDelegate::setModelData(editor, model, index);
        model->setData(index, box->currentText().trimmed(), Qt::EditRole);
    }

private:
    bool rowIsAwg(int row) const {
        auto* unit = qobject_cast<QComboBox*>(table_->cellWidget(row, unitColumn_));
        return !unit || unit->currentIndex() == 0;
    }
    QTableWidget* table_ = nullptr;
    int unitColumn_ = 0;
};

constexpr double kMmPerInch = 25.4;

// 4 places for inches because a thin wire is 0.03 of one: two places would round a 22 AWG and a
// 26 AWG to the same number, which is worse than not showing inches at all.
QString mmText(double mm) { return mm > 0.0 ? QString::number(mm, 'f', 2) : QString(); }
QString inText(double mm) { return mm > 0.0 ? QString::number(mm / kMmPerInch, 'f', 4) : QString(); }

// A cell nobody types into: the line number, which the table keeps for itself.
QTableWidgetItem* readOnlyCell(const QString& text) {
    auto* it = new QTableWidgetItem(text);
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    it->setTextAlignment(Qt::AlignCenter);
    return it;
}

// What a library part is called on a list of them: the house description if there is one, else the
// manufacturer's part number, else its id - so a part always has a name to be picked by.
QString partTitle(const LibraryPart& p) {
    if (!p.description.trimmed().isEmpty()) return p.description.trimmed();
    if (!p.mfrPn.trimmed().isEmpty()) return p.mfrPn.trimmed();
    return p.id;
}

} // namespace

BundleDiameterDialog::BundleDiameterDialog(Editor* editor, QWidget* parent)
    : QDialog(parent), editor_(editor) {
    setWindowTitle(QStringLiteral("Bundle diameter"));
    resize(1000, 760);
    buildUi();
    readFromDrawing();
}

void BundleDiameterDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

    // ---- The formula itself, stated before anything is put into it ----
    auto* formula = new QLabel(this);
    formula->setTextFormat(Qt::RichText);
    formula->setText(QStringLiteral(
        "<div style='font-size:15px;'><b>D<sub>bundle</sub></b> &nbsp;=&nbsp; "
        "&radic;<span style='text-decoration:overline;'>&nbsp;&Sigma; D<sup>2</sup><sub>wire</sub>"
        "&nbsp;</span> &nbsp;&times;&nbsp; <b>k<sub>packing</sub></b></div>"));
    formula->setAlignment(Qt::AlignCenter);
    root->addWidget(formula);

    // ---- The conductors ----
    auto* wireBox = new QGroupBox(QStringLiteral("The conductors in this bundle"), this);
    auto* wireLay = new QVBoxLayout(wireBox);
    wires_ = new QTableWidget(wireBox);
    wires_->setColumnCount(kColCount);
    wires_->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("Name"),
                                       QStringLiteral("Amount"), QStringLiteral("Cross-section"),
                                       QStringLiteral("Unit"), QStringLiteral("\xC3\x98 insul. (mm)"),
                                       QStringLiteral("\xC3\x98 insul. (in)")});
    wires_->verticalHeader()->setVisible(false);
    wires_->horizontalHeader()->setStretchLastSection(false);
    wires_->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    wires_->setColumnWidth(kColNo, 34);
    wires_->setColumnWidth(kColAmount, 62);
    wires_->setColumnWidth(kColGauge, 96);
    // The cross-section is picked from the sizes that exist, or typed - see GaugeDelegate.
    wires_->setItemDelegateForColumn(kColGauge, new GaugeDelegate(wires_, kColGaugeUnit, this));
    wires_->setColumnWidth(kColGaugeUnit, 66);
    wires_->setColumnWidth(kColOdMm, 116);
    wires_->setColumnWidth(kColOdIn, 116);
    wires_->setToolTip(QStringLiteral(
        "The diameter is what the calculation uses. A cross-section fills it in from the nominal "
        "thin-wall figures for the unit beside it; type over it when the data sheet says otherwise.\n"
        "Amount is how many identical conductors the row stands for.\n"
        "Millimetres and inches follow each other - type into whichever the drawing is in."));

    connect(wires_, &QTableWidget::cellChanged, this, [this](int row, int col) {
        if (syncing_ || row < 0 || row >= wires_->rowCount()) return;
        if (col == kColGauge) {
            // A CROSS-SECTION WRITES THE DIAMETER. The wire is quoted by its cross-section and the
            // sum needs its outside diameter, so saying the one says the other - and it stays
            // sayable by hand afterwards, because a data sheet beats a nominal table every time.
            diameterFromGauge(row);
        } else if (col == kColOdMm) {
            syncing_ = true;
            const double mm = wires_->item(row, kColOdMm)->text().trimmed().toDouble();
            wires_->item(row, kColOdIn)->setText(inText(mm));
            syncing_ = false;
        } else if (col == kColOdIn) {
            syncing_ = true;
            const double in = wires_->item(row, kColOdIn)->text().trimmed().toDouble();
            wires_->item(row, kColOdMm)->setText(mmText(in * kMmPerInch));
            syncing_ = false;
        } else if (col == kColAmount) {
            // An amount is a count of conductors: at least one, and whole.
            syncing_ = true;
            const int n = std::max(1, wires_->item(row, kColAmount)->text().trimmed().toInt());
            wires_->item(row, kColAmount)->setText(QString::number(n));
            syncing_ = false;
        }
        recalculate();
    });
    // A click while Match is armed is the SOURCE of the copy, not a change of selection.
    connect(wires_, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (matchRow_ >= 0) applyMatch(row, col);
    });
    wireLay->addWidget(wires_);

    auto* btns = new QHBoxLayout;
    auto* readBtn = new QPushButton(QStringLiteral("Read from the drawing"), wireBox);
    readBtn->setToolTip(QStringLiteral("Every wire on the sheet, with the gauge it is drawn with"));
    connect(readBtn, &QPushButton::clicked, this, [this] { readFromDrawing(); });
    btns->addWidget(readBtn);

    auto* libBtn = new QPushButton(QStringLiteral("From the library\xE2\x80\xA6"), wireBox);
    libBtn->setToolTip(QStringLiteral(
        "Pick a wire or a cable from the parts library. Its cross-section and its outside diameter "
        "come with it, so the bundle is worked out from the part that will actually be bought."));
    connect(libBtn, &QPushButton::clicked, this, [this] { takeFromLibrary(); });
    btns->addWidget(libBtn);

    auto* addBtn = makeAddButton(wireBox, QStringLiteral("Conductor"));
    connect(addBtn, &QPushButton::clicked, this, [this] {
        // AWG, and a size in the middle of the range: what most hook-up wire is quoted in, so the
        // common case is typing over a number rather than changing the unit as well.
        addRow(nextWireName(), QStringLiteral("22"), true, 0.0, 1);
        recalculate();
    });
    btns->addWidget(addBtn);

    auto* dupBtn = new QPushButton(QStringLiteral("Duplicate"), wireBox);
    dupBtn->setToolTip(QStringLiteral(
        "Another conductor the same as the selected one, under the next name. For many of them, "
        "put the number in the Amount column instead."));
    connect(dupBtn, &QPushButton::clicked, this, [this] { duplicateSelected(); });
    btns->addWidget(dupBtn);

    matchBtn_ = new QPushButton(QStringLiteral("Match"), wireBox);
    matchBtn_->setCheckable(true);
    matchBtn_->setToolTip(QStringLiteral(
        "Put another cell's value into the cell you are on: click the cell to change, press Match, "
        "then click the cell to copy from."));
    connect(matchBtn_, &QPushButton::clicked, this, [this] { armMatch(); });
    btns->addWidget(matchBtn_);

    auto* rmBtn = makeRemoveButton(wireBox, QStringLiteral("Remove"), QString(), true);
    connect(rmBtn, &QPushButton::clicked, this, [this] { removeSelected(); });
    btns->addWidget(rmBtn);

    auto* clearBtn = new QPushButton(QStringLiteral("Clear all"), wireBox);
    clearBtn->setToolTip(QStringLiteral("Take every conductor out and start the bundle again"));
    connect(clearBtn, &QPushButton::clicked, this, [this] { clearAll(); });
    btns->addWidget(clearBtn);

    btns->addStretch(1);

    // The make-up saved with the drawing, and the whole answer on one page.
    auto* saveBtn = new QPushButton(QStringLiteral("Save preset\xE2\x80\xA6"), wireBox);
    saveBtn->setToolTip(QStringLiteral(
        "Keep this make-up with the drawing under a name. Saved INTO the drawing, so the figure "
        "quoted on this sheet can be got back and checked on any machine that opens it."));
    connect(saveBtn, &QPushButton::clicked, this, [this] { savePreset(); });
    btns->addWidget(saveBtn);
    auto* loadBtn = new QPushButton(QStringLiteral("Load preset\xE2\x80\xA6"), wireBox);
    connect(loadBtn, &QPushButton::clicked, this, [this] { loadPreset(); });
    btns->addWidget(loadBtn);
    auto* sumBtn = new QPushButton(QStringLiteral("Results\xE2\x80\xA6"), wireBox);
    sumBtn->setToolTip(QStringLiteral("Every conductor, how many of each, and what it all comes to"));
    connect(sumBtn, &QPushButton::clicked, this, [this] { showSummary(); });
    btns->addWidget(sumBtn);

    wireLay->addLayout(btns);
    root->addWidget(wireBox);

    // ---- k_packing ----
    auto* kBox = new QGroupBox(QStringLiteral("k_packing — the allowance for the air between them"),
                              this);
    auto* kLay = new QHBoxLayout(kBox);
    autoK_ = new QCheckBox(QStringLiteral("Work it out from the count"), kBox);
    autoK_->setChecked(true);
    autoK_->setToolTip(QStringLiteral(
        "A handful of conductors cannot help leaving air between them, so they pack loosely; a big "
        "bundle settles down. 1.25 at the small end, 1.15 at the large one."));
    connect(autoK_, &QCheckBox::toggled, this, [this](bool on) {
        if (kSpin_) kSpin_->setEnabled(!on);
        recalculate();
    });
    kLay->addWidget(autoK_);
    kSpin_ = new QDoubleSpinBox(kBox);
    kSpin_->setRange(1.0, 1.6);
    kSpin_->setDecimals(3);
    kSpin_->setSingleStep(0.01);
    kSpin_->setValue(1.20);
    kSpin_->setEnabled(false);
    kSpin_->setToolTip(QStringLiteral("1.15 to 1.25 covers ordinary practice; more than that means "
                                      "the bundle is not round or not settled."));
    connect(kSpin_, &QDoubleSpinBox::valueChanged, this, [this](double) { recalculate(); });
    kLay->addWidget(kSpin_);
    kNote_ = new QLabel(QString(), kBox);
    kNote_->setStyleSheet(QStringLiteral("color:%1;").arg(capInk().name()));
    kLay->addWidget(kNote_, 1);
    root->addWidget(kBox);

    result_ = new QLabel(QString(), this);
    // THE ANSWER, IN A BOX OF ITS OWN. It is the one line anybody opens this window for, and as a
    // bare line of coloured text it sat between two panels looking like a caption. A light ground
    // with an edge round it makes it the thing on the page.
    //
    // The same light grey in BOTH schemes, with dark lettering on it - not a themed panel. It is a
    // result being held up to be read and copied down, the way a highlighted field is on a form,
    // and the point of it is that it does not look like the rest of the window.
    result_->setStyleSheet(QStringLiteral(
        "background:#e9eaec;color:#16181c;border:1px solid #b9bcc2;border-radius:3px;"
        "padding:8px 10px;font-size:15px;font-weight:600;"));
    result_->setAlignment(Qt::AlignCenter);
    root->addWidget(result_);

    working_ = new QTextBrowser(this);
    working_->setOpenExternalLinks(false);
    root->addWidget(working_, 1);

    auto* foot = new QHBoxLayout;
    foot->addStretch(1);
    auto* copyBtn = new QPushButton(QStringLiteral("Copy the working"), this);
    copyBtn->setToolTip(QStringLiteral("The calculation as text, to paste into a report"));
    connect(copyBtn, &QPushButton::clicked, this, [this] {
        if (working_) {
            working_->selectAll();
            working_->copy();
            working_->moveCursor(QTextCursor::Start);
        }
    });
    foot->addWidget(copyBtn);
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    foot->addWidget(closeBtn);
    root->addLayout(foot);
}

QString BundleDiameterDialog::nextWireName() {
    return QStringLiteral("Wire %1").arg(nextWireNo_++);
}

void BundleDiameterDialog::renumber() {
    const bool was = syncing_;
    syncing_ = true;
    for (int r = 0; r < wires_->rowCount(); ++r)
        if (QTableWidgetItem* it = wires_->item(r, kColNo)) it->setText(QString::number(r + 1));
    syncing_ = was;
}

void BundleDiameterDialog::writeDiameter(int row, double mm) {
    const bool was = syncing_;
    syncing_ = true;
    if (QTableWidgetItem* it = wires_->item(row, kColOdMm)) it->setText(mmText(mm));
    if (QTableWidgetItem* it = wires_->item(row, kColOdIn)) it->setText(inText(mm));
    syncing_ = was;
}

QString BundleDiameterDialog::gaugeForLookup(int row) const {
    const QTableWidgetItem* g = wires_->item(row, kColGauge);
    if (!g) return QString();
    const QString text = g->text().trimmed();
    if (text.isEmpty()) return QString();
    // The unit is the row's own, so a bare "22" in an AWG row is 22 AWG and in a mm2 row is a very
    // fat conductor indeed - which is the whole reason the unit is a column and not a guess.
    auto* box = qobject_cast<QComboBox*>(wires_->cellWidget(row, kColGaugeUnit));
    const bool awg = box && box->currentIndex() == 0;
    if (!awg) return text;
    return text.contains(QStringLiteral("AWG"), Qt::CaseInsensitive)
               ? text
               : text + QStringLiteral(" AWG");
}

void BundleDiameterDialog::diameterFromGauge(int row) {
    const double od = wireOuterDiameterMm(gaugeForLookup(row));
    if (od > 0.0) writeDiameter(row, od);
}

void BundleDiameterDialog::addRow(const QString& name, const QString& gauge, bool awg, double odMm,
                                  int amount) {
    const bool was = syncing_;
    syncing_ = true;
    const int row = wires_->rowCount();
    wires_->insertRow(row);
    wires_->setItem(row, kColNo, readOnlyCell(QString::number(row + 1)));
    wires_->setItem(row, kColName, new QTableWidgetItem(name));
    wires_->setItem(row, kColAmount, new QTableWidgetItem(QString::number(std::max(1, amount))));
    wires_->item(row, kColAmount)->setTextAlignment(Qt::AlignCenter);
    wires_->setItem(row, kColGauge, new QTableWidgetItem(gauge));

    // The unit the cross-section is written in, as the row's own choice. AWG and mm2 are both on
    // real drawings and a bundle is regularly made of some of each, so it cannot be one setting for
    // the window: it belongs to the conductor.
    auto* unit = new QComboBox(wires_);
    unit->addItems({QStringLiteral("AWG"), QStringLiteral("mm\xC2\xB2")});
    unit->setCurrentIndex(awg ? 0 : 1);
    wires_->setCellWidget(row, kColGaugeUnit, unit);
    connect(unit, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, unit](int idx) {
        if (syncing_) return;
        // Which row it is is asked for now rather than captured: rows above may have been taken
        // away since, and a captured index would then name somebody else's conductor.
        int row = -1;
        for (int r = 0; r < wires_->rowCount(); ++r)
            if (wires_->cellWidget(r, kColGaugeUnit) == unit) { row = r; break; }
        if (row < 0) return;
        // THE UNIT CONVERTS THE CROSS-SECTION; IT DOES NOT REINTERPRET IT.
        //
        // Changing the unit means "say this conductor's size the other way", not "the number stays
        // and now means something else": 22 AWG asked for in mm2 is 0.33, not a 22 mm2 welding
        // cable. Reinterpreting silently turned a signal wire into a battery lead.
        QTableWidgetItem* g = wires_->item(row, kColGauge);
        const QString had = g ? g->text().trimmed() : QString();
        if (g && !had.isEmpty()) {
            const bool nowAwg = idx == 0;
            syncing_ = true;
            if (nowAwg) {
                // It was mm2 until a moment ago, so that is what the number is.
                const int awg = mechNearestAwg(had.toDouble());
                if (awg > 0) g->setText(QString::number(awg));
            } else {
                const double mm2 = mechAwgMm2(had.split(QLatin1Char(' ')).value(0).toInt());
                // Three places at the thin end, two at the thick: 0.081 mm2 and 0.08 mm2 are not
                // the same wire, and 33.63 does not need a third.
                if (mm2 > 0.0) g->setText(QString::number(mm2, 'f', mm2 < 1.0 ? 3 : 2));
            }
            syncing_ = false;
        }
        diameterFromGauge(row);
        recalculate();
    });

    wires_->setItem(row, kColOdMm, new QTableWidgetItem());
    wires_->setItem(row, kColOdIn, new QTableWidgetItem());
    const double use = odMm > 0.0 ? odMm : wireOuterDiameterMm(gaugeForLookup(row));
    writeDiameter(row, use);
    syncing_ = was;
}

void BundleDiameterDialog::duplicateSelected() {
    const int row = wires_->currentRow();
    if (row < 0 || row >= wires_->rowCount()) return;
    auto* box = qobject_cast<QComboBox*>(wires_->cellWidget(row, kColGaugeUnit));
    // Under the NEXT name, not the same one: two conductors sharing a name cannot be told apart on
    // the sheet, on a bill of materials, or by whoever builds it.
    addRow(nextWireName(), wires_->item(row, kColGauge)->text(), box && box->currentIndex() == 0,
           rowDiameterMm(row), rowAmount(row));
    recalculate();
}

void BundleDiameterDialog::removeSelected() {
    const int row = wires_->currentRow();
    if (row < 0 || row >= wires_->rowCount()) return;
    wires_->removeRow(row);
    renumber();
    recalculate();
}

void BundleDiameterDialog::fillRowFromPart(int row, const LibraryPart& part) {
    const bool was = syncing_;
    syncing_ = true;
    wires_->item(row, kColName)->setText(partTitle(part));
    // A WIRE IS QUOTED BY ITS GAUGE, A CABLE BY ITS JACKET. A cable has no single cross-section - it
    // is a bundle already - so only its outside diameter is taken, and the cross-section is left
    // blank rather than filled with something untrue.
    const QString gauge = part.params.value(QStringLiteral("gauge")).trimmed();
    const bool awg = part.params.value(QStringLiteral("gaugeMode")).compare(
                         QStringLiteral("mm2"), Qt::CaseInsensitive) != 0;
    wires_->item(row, kColGauge)->setText(part.kind == PartKind::Cable ? QString() : gauge);
    if (auto* box = qobject_cast<QComboBox*>(wires_->cellWidget(row, kColGaugeUnit)))
        box->setCurrentIndex(awg ? 0 : 1);
    syncing_ = was;
    // The part's OWN diameter first - that is the number on the data sheet and the reason for
    // picking a part at all. Only when it says nothing does the nominal table get a turn.
    const double od = libraryWireOdMm(part);
    if (od > 0.0) writeDiameter(row, od);
    else diameterFromGauge(row);
}

void BundleDiameterDialog::takeFromLibrary() {
    PartsCatalog catalog;
    catalog.openRemembered();
    std::vector<const LibraryPart*> parts = catalog.ofKind(PartKind::Wire);
    for (const LibraryPart* c : catalog.ofKind(PartKind::Cable)) parts.push_back(c);

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Take a conductor from the library"));
    dlg.resize(720, 460);
    auto* outer = new QVBoxLayout(&dlg);

    auto* head = new QHBoxLayout;
    auto* filter = new QLineEdit(&dlg);
    filter->setPlaceholderText(QStringLiteral("Filter by name, part number or gauge"));
    head->addWidget(filter, 1);
    // WHICH KIND, SAID OUT LOUD AND COUNTED.
    //
    // Wires and cables were both offered from the first, but a library with no cable in it looked
    // exactly like a picker that could not do cables - there was nothing to tell the two apart. The
    // counts say which it is without anybody having to find out by scrolling.
    int nWire = 0, nCable = 0;
    for (const LibraryPart* p : parts) (p->kind == PartKind::Cable ? nCable : nWire)++;
    auto* kind = new QComboBox(&dlg);
    kind->addItem(QStringLiteral("Wires and cables (%1)").arg(nWire + nCable), -1);
    kind->addItem(QStringLiteral("Wires only (%1)").arg(nWire), int(PartKind::Wire));
    kind->addItem(QStringLiteral("Cables only (%1)").arg(nCable), int(PartKind::Cable));
    head->addWidget(kind);
    outer->addLayout(head);

    auto* list = new QTableWidget(&dlg);
    list->setColumnCount(5);
    list->setHorizontalHeaderLabels({QStringLiteral("Kind"), QStringLiteral("Part"),
                                     QStringLiteral("Part number"), QStringLiteral("Gauge"),
                                     QStringLiteral("\xC3\x98 (mm)")});
    list->verticalHeader()->setVisible(false);
    list->setSelectionBehavior(QAbstractItemView::SelectRows);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    outer->addWidget(list, 1);

    const auto fill = [&](const QString& needle) {
        const int want = kind->currentData().toInt();
        list->setRowCount(0);
        for (const LibraryPart* p : parts) {
            if (want >= 0 && int(p->kind) != want) continue;
            const QString gauge = p->params.value(QStringLiteral("gauge")).trimmed();
            const QString od = p->params.value(QStringLiteral("od")).trimmed();
            const QString hay = partTitle(*p) + QLatin1Char(' ') + p->mfrPn + QLatin1Char(' ') + gauge;
            if (!needle.isEmpty() && !hay.contains(needle, Qt::CaseInsensitive)) continue;
            const int r = list->rowCount();
            list->insertRow(r);
            list->setItem(r, 0, new QTableWidgetItem(p->kind == PartKind::Cable
                                                         ? QStringLiteral("Cable")
                                                         : QStringLiteral("Wire")));
            auto* title = new QTableWidgetItem(partTitle(*p));
            title->setData(Qt::UserRole, p->id);
            list->setItem(r, 1, title);
            list->setItem(r, 2, new QTableWidgetItem(p->mfrPn));
            list->setItem(r, 3, new QTableWidgetItem(gauge));
            list->setItem(r, 4, new QTableWidgetItem(od));
        }
        if (list->rowCount() > 0) list->selectRow(0);
    };
    fill(QString());
    connect(filter, &QLineEdit::textChanged, &dlg, [&](const QString& t) { fill(t); });
    connect(kind, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg,
            [&](int) { fill(filter->text()); });

    auto* note = new QLabel(&dlg);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:%1;").arg(capInk().name()));
    note->setText(
        parts.empty()
            ? QStringLiteral("There are no wires or cables in the connected libraries yet — add one "
                             "in the parts library window first.")
            : QStringLiteral("%1 wire(s) and %2 cable(s) in the connected libraries. The part's own "
                             "outside diameter is used when it has one; otherwise the nominal figure "
                             "for its gauge.%3")
                  .arg(nWire)
                  .arg(nCable)
                  .arg(nCable == 0 ? QStringLiteral(" No cables are filed yet — add them in the parts "
                                                    "library window and they will appear here.")
                                   : QString()));
    outer->addWidget(note);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    outer->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(list, &QTableWidget::cellDoubleClicked, &dlg, [&dlg](int, int) { dlg.accept(); });

    if (dlg.exec() != QDialog::Accepted) return;
    const int picked = list->currentRow();
    if (picked < 0) return;
    const QString id = list->item(picked, 1)->data(Qt::UserRole).toString();
    const LibraryPart* part = catalog.byId(id);
    if (!part) return;

    // Into the selected row when one is selected, so a conductor already in the table can be given
    // a real part; otherwise a new row, which is what the button reads as with nothing selected.
    int row = wires_->currentRow();
    if (row < 0 || row >= wires_->rowCount()) {
        addRow(nextWireName(), QString(), true, 0.0, 1);
        row = wires_->rowCount() - 1;
    }
    fillRowFromPart(row, *part);
    recalculate();
}

void BundleDiameterDialog::readFromDrawing() {
    if (!editor_ || !editor_->document()) return;
    syncing_ = true;
    wires_->setRowCount(0);
    nextWireNo_ = 1;
    syncing_ = false;
    for (const Entity& e : editor_->document()->entities()) {
        if (e.type != EntityType::Wire) continue;
        QString label = e.meta.value(QStringLiteral("wireLabel")).toString();
        if (label.isEmpty()) label = e.id;
        QString gauge = e.meta.value(QStringLiteral("wireGauge")).toString();
        if (gauge.isEmpty()) gauge = e.meta.value(QStringLiteral("awg")).toString();
        // A gauge off the drawing says which unit it is in by how it is written: "22 AWG" is one,
        // "0.5" is the other. Read here rather than assumed, because both are drawn.
        const bool awg = gauge.contains(QStringLiteral("AWG"), Qt::CaseInsensitive);
        addRow(label, gauge, awg, 0.0, 1);
        ++nextWireNo_;   // names from the drawing still move the counter on
    }
    renumber();
    recalculate();
}

int BundleDiameterDialog::rowCount() const { return wires_ ? wires_->rowCount() : 0; }

int BundleDiameterDialog::rowAmount(int row) const {
    if (!wires_ || row < 0 || row >= wires_->rowCount()) return 0;
    const QTableWidgetItem* it = wires_->item(row, kColAmount);
    return it ? std::max(1, it->text().trimmed().toInt()) : 1;
}

double BundleDiameterDialog::rowDiameterMm(int row) const {
    if (!wires_ || row < 0 || row >= wires_->rowCount()) return 0.0;
    const QTableWidgetItem* it = wires_->item(row, kColOdMm);
    return it ? it->text().trimmed().toDouble() : 0.0;
}

QString BundleDiameterDialog::rowName(int row) const {
    if (!wires_ || row < 0 || row >= wires_->rowCount()) return QString();
    const QTableWidgetItem* it = wires_->item(row, kColName);
    return it ? it->text() : QString();
}

std::vector<double> BundleDiameterDialog::wireDiameters() const {
    // EACH ROW AS MANY TIMES AS IT SAYS. The sum counts conductors, not kinds of conductor, so a row
    // of eight goes in eight times - which is the whole point of there being an amount.
    std::vector<double> ods;
    for (int r = 0; r < rowCount(); ++r) {
        const double mm = rowDiameterMm(r);
        for (int n = rowAmount(r); n > 0; --n) ods.push_back(mm);
    }
    return ods;
}

void BundleDiameterDialog::setWireDiameters(const std::vector<double>& ods) {
    syncing_ = true;
    wires_->setRowCount(0);
    nextWireNo_ = 1;
    syncing_ = false;
    for (double od : ods) addRow(nextWireName(), QString(), true, od, 1);
    renumber();
    recalculate();
}

void BundleDiameterDialog::setPackingFactor(double k) {
    syncing_ = true;
    if (autoK_) autoK_->setChecked(k <= 0.0);
    if (kSpin_) {
        kSpin_->setEnabled(k > 0.0);
        if (k > 0.0) kSpin_->setValue(k);
    }
    syncing_ = false;
    recalculate();
}

void BundleDiameterDialog::recalculate() {
    const bool autoK = !autoK_ || autoK_->isChecked();
    calc_ = bundleCalculation(wireDiameters(), autoK ? -1.0 : kSpin_->value(), 0.0);
    if (kNote_) {
        kNote_->setText(
            autoK ? QStringLiteral("%1 for %2 conductor(s) — the range is 1.15 (big bundle) to "
                                   "1.25 (a handful)")
                        .arg(calc_.packingFactor, 0, 'f', 2)
                        .arg(calc_.counted)
                  : QStringLiteral("entered by hand%1")
                        .arg(calc_.packingFactor < 1.15 || calc_.packingFactor > 1.25
                                 ? QStringLiteral(" — outside the usual 1.15 to 1.25")
                                 : QString()));
    }
    if (autoK && kSpin_) {
        syncing_ = true;
        kSpin_->setValue(calc_.packingFactor);
        syncing_ = false;
    }
    refreshWorking();
}

void BundleDiameterDialog::refreshWorking() {
    if (result_) {
        // IN BOTH UNITS, THE INCHES IN BRACKETS. The calculation is in millimetres and stays in
        // millimetres; the inches are the same figure said again for whoever is reading an American
        // drawing, which is why they are beside it rather than instead of it.
        result_->setText(
            calc_.counted == 0
                ? QStringLiteral("Nothing to work out yet — add the conductors.")
                : QStringLiteral("D_bundle = %1 mm  (%2 in)   —   %3 conductor(s), %4")
                      .arg(calc_.result, 0, 'f', 2)
                      .arg(calc_.result / kMmPerInch, 0, 'f', 4)
                      .arg(calc_.counted)
                      .arg(calc_.rule == QLatin1String("random-lay")
                               ? QStringLiteral("random lay")
                               : QStringLiteral("exact geometry for %1").arg(calc_.rule)));
    }
    if (!working_) return;
    QString html = QStringLiteral("<style>h4{color:%1;margin-bottom:4px;} pre{padding:8px;}</style>")
                       .arg(infoInk().name());
    html += QStringLiteral("<h4>The working</h4><pre>");
    for (const QString& line : calc_.workings) html += line.toHtmlEscaped() + QStringLiteral("\n");
    if (calc_.result > 0.0)
        html += QStringLiteral("D_bundle = %1 mm = %2 in\n")
                    .arg(calc_.result, 0, 'f', 2)
                    .arg(calc_.result / kMmPerInch, 0, 'f', 4);
    html += QStringLiteral("</pre>");
    html += QStringLiteral("<h4>What it means, and where it stops</h4>");
    for (const QString& para : calc_.explanation().split(QStringLiteral("\n\n")))
        html += QStringLiteral("<p>%1</p>").arg(para.toHtmlEscaped());
    working_->setHtml(html);
}

void BundleDiameterDialog::clearAll() {
    if (wires_->rowCount() == 0) return;
    if (QMessageBox::question(this, QStringLiteral("Clear all"),
                              QStringLiteral("Take all %1 conductor(s) out of this bundle?")
                                  .arg(wires_->rowCount()),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;
    syncing_ = true;
    wires_->setRowCount(0);
    // The names start again, because the conductors they named are gone. Counting on from where an
    // emptied table left off would open the next bundle at "Wire 27".
    nextWireNo_ = 1;
    syncing_ = false;
    recalculate();
}

void BundleDiameterDialog::armMatch() {
    if (!matchBtn_) return;
    if (!matchBtn_->isChecked()) { matchRow_ = matchCol_ = -1; return; }
    const int row = wires_->currentRow();
    const int col = wires_->currentColumn();
    // The line number is the table's own and nothing may be copied into it; nor is there anything to
    // match if no cell is current.
    if (row < 0 || col <= kColNo || col >= kColCount) {
        matchBtn_->setChecked(false);
        matchRow_ = matchCol_ = -1;
        QMessageBox::information(this, QStringLiteral("Match"),
                                 QStringLiteral("Click the cell you want to change first, then "
                                                "press Match and click the cell to copy from."));
        return;
    }
    matchRow_ = row;
    matchCol_ = col;
    wires_->setCursor(Qt::CrossCursor);
}

void BundleDiameterDialog::applyMatch(int sourceRow, int sourceCol) {
    const int row = matchRow_, col = matchCol_;
    matchRow_ = matchCol_ = -1;
    if (matchBtn_) matchBtn_->setChecked(false);
    wires_->unsetCursor();
    if (row < 0 || row >= wires_->rowCount() || sourceRow < 0 || sourceRow >= wires_->rowCount())
        return;
    if (sourceRow == row && sourceCol == col) return;   // itself: nothing to do

    // THE UNIT IS A WIDGET, NOT A CELL, so it is matched by asking the widget rather than by reading
    // an item that does not exist. Matching a unit converts the cross-section with it, exactly as
    // choosing that unit by hand would - the row is left saying one consistent thing.
    if (col == kColGaugeUnit) {
        auto* src = qobject_cast<QComboBox*>(wires_->cellWidget(sourceRow, kColGaugeUnit));
        auto* dst = qobject_cast<QComboBox*>(wires_->cellWidget(row, kColGaugeUnit));
        if (src && dst) dst->setCurrentIndex(src->currentIndex());
        return;
    }
    // Anywhere else, the value is the text of the cell that was clicked - taken from the same column
    // when the click was in one, and otherwise from the column being filled, so clicking anywhere
    // along the source row means "this row's one of these".
    const int readCol = (sourceCol > kColNo && sourceCol < kColCount) ? sourceCol : col;
    const QTableWidgetItem* src = wires_->item(sourceRow, readCol == kColGaugeUnit ? col : readCol);
    QTableWidgetItem* dst = wires_->item(row, col);
    if (!src || !dst) return;
    dst->setText(src->text());   // fires cellChanged, so the paired unit and the sum both follow
}

QJsonArray BundleDiameterDialog::tableToJson() const {
    QJsonArray rows;
    for (int r = 0; r < rowCount(); ++r) {
        auto* box = qobject_cast<QComboBox*>(wires_->cellWidget(r, kColGaugeUnit));
        QJsonObject o;
        o.insert(QStringLiteral("name"), rowName(r));
        o.insert(QStringLiteral("amount"), rowAmount(r));
        o.insert(QStringLiteral("gauge"), wires_->item(r, kColGauge)->text());
        o.insert(QStringLiteral("awg"), box && box->currentIndex() == 0);
        // The diameter in MILLIMETRES only. It is the unit the calculation is done in, and writing
        // both would let a file be saved whose two halves disagree.
        o.insert(QStringLiteral("odMm"), rowDiameterMm(r));
        rows.append(o);
    }
    return rows;
}

void BundleDiameterDialog::tableFromJson(const QJsonArray& rows) {
    syncing_ = true;
    wires_->setRowCount(0);
    nextWireNo_ = 1;
    syncing_ = false;
    for (const QJsonValue& v : rows) {
        const QJsonObject o = v.toObject();
        QString name = o.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) name = nextWireName();
        addRow(name, o.value(QStringLiteral("gauge")).toString(),
               o.value(QStringLiteral("awg")).toBool(true),
               o.value(QStringLiteral("odMm")).toDouble(), o.value(QStringLiteral("amount")).toInt(1));
        ++nextWireNo_;   // a name that came back still moves the counter past it
    }
    renumber();
    recalculate();
}

void BundleDiameterDialog::savePreset() {
    Document* doc = editor_ ? editor_->document() : nullptr;
    if (!doc) {
        QMessageBox::information(this, QStringLiteral("Save preset"),
                                 QStringLiteral("There is no drawing open to save it into."));
        return;
    }
    if (rowCount() == 0) {
        QMessageBox::information(this, QStringLiteral("Save preset"),
                                 QStringLiteral("There are no conductors to save yet."));
        return;
    }
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, QStringLiteral("Save preset"),
                              QStringLiteral("A name for this bundle make-up:"), QLineEdit::Normal,
                              QStringLiteral("Bundle %1").arg(doc->bundlePresets().size() + 1), &ok)
            .trimmed();
    if (!ok || name.isEmpty()) return;
    if (doc->bundlePresets().contains(name) &&
        QMessageBox::question(this, QStringLiteral("Save preset"),
                              QStringLiteral("\"%1\" already exists on this drawing. Replace it?")
                                  .arg(name),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;
    QJsonObject entry;
    entry.insert(QStringLiteral("wires"), tableToJson());
    // The k that was in force is part of the make-up: a bundle saved with a hand-set factor and
    // brought back with an automatic one is not the same calculation.
    entry.insert(QStringLiteral("kAuto"), !autoK_ || autoK_->isChecked());
    entry.insert(QStringLiteral("k"), kSpin_ ? kSpin_->value() : 1.2);
    doc->bundlePresets().insert(name, entry);
    doc->setDirty(true);
}

void BundleDiameterDialog::loadPreset() {
    Document* doc = editor_ ? editor_->document() : nullptr;
    if (!doc || doc->bundlePresets().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Load preset"),
                                 QStringLiteral("This drawing has no saved bundle make-ups yet."));
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Load a bundle make-up"));
    dlg.resize(420, 320);
    auto* outer = new QVBoxLayout(&dlg);
    auto* list = new QListWidget(&dlg);
    const QJsonObject presets = doc->bundlePresets();
    for (auto it = presets.begin(); it != presets.end(); ++it) {
        const int n = it.value().toObject().value(QStringLiteral("wires")).toArray().size();
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  —  %2 kind(s) of conductor").arg(it.key()).arg(n), list);
        item->setData(Qt::UserRole, it.key());
    }
    if (list->count() > 0) list->setCurrentRow(0);
    outer->addWidget(list, 1);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, &dlg);
    auto* del = bb->addButton(QStringLiteral("Delete"), QDialogButtonBox::DestructiveRole);
    outer->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dlg, [&dlg](QListWidgetItem*) { dlg.accept(); });
    connect(del, &QPushButton::clicked, &dlg, [&] {
        QListWidgetItem* item = list->currentItem();
        if (!item) return;
        doc->bundlePresets().remove(item->data(Qt::UserRole).toString());
        doc->setDirty(true);
        delete list->takeItem(list->row(item));
        if (list->count() == 0) dlg.reject();
    });
    if (dlg.exec() != QDialog::Accepted || !list->currentItem()) return;

    const QJsonObject entry =
        doc->bundlePresets().value(list->currentItem()->data(Qt::UserRole).toString()).toObject();
    tableFromJson(entry.value(QStringLiteral("wires")).toArray());
    if (autoK_) autoK_->setChecked(entry.value(QStringLiteral("kAuto")).toBool(true));
    if (kSpin_ && !entry.value(QStringLiteral("kAuto")).toBool(true))
        kSpin_->setValue(entry.value(QStringLiteral("k")).toDouble(1.2));
    recalculate();
}

void BundleDiameterDialog::showSummary() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Bundle results"));
    dlg.resize(700, 560);
    auto* outer = new QVBoxLayout(&dlg);

    // THE ANSWER FIRST. This window is opened to be read out or copied into a note, so the figure it
    // is about is at the top in the same box it wears on the main window.
    auto* head = new QLabel(&dlg);
    head->setAlignment(Qt::AlignCenter);
    head->setStyleSheet(QStringLiteral(
        "background:#e9eaec;color:#16181c;border:1px solid #b9bcc2;border-radius:3px;"
        "padding:10px;font-size:16px;font-weight:600;"));
    head->setText(calc_.counted == 0
                      ? QStringLiteral("No conductors yet")
                      : QStringLiteral("D_bundle = %1 mm   (%2 in)")
                            .arg(calc_.result, 0, 'f', 2)
                            .arg(calc_.result / kMmPerInch, 0, 'f', 4));
    outer->addWidget(head);

    auto* table = new QTableWidget(&dlg);
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("Conductor"),
                                      QStringLiteral("Amount"), QStringLiteral("Cross-section"),
                                      QStringLiteral("\xC3\x98 (mm)"), QStringLiteral("\xC3\x98 (in)")});
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    int totalWires = 0;
    for (int r = 0; r < rowCount(); ++r) {
        auto* box = qobject_cast<QComboBox*>(wires_->cellWidget(r, kColGaugeUnit));
        const int amount = rowAmount(r);
        totalWires += amount;
        const double mm = rowDiameterMm(r);
        const int t = table->rowCount();
        table->insertRow(t);
        table->setItem(t, 0, new QTableWidgetItem(QString::number(t + 1)));
        table->setItem(t, 1, new QTableWidgetItem(rowName(r)));
        table->setItem(t, 2, new QTableWidgetItem(QString::number(amount)));
        table->setItem(t, 3, new QTableWidgetItem(
                                 QStringLiteral("%1 %2")
                                     .arg(wires_->item(r, kColGauge)->text(),
                                          box && box->currentIndex() == 0
                                              ? QStringLiteral("AWG")
                                              : QStringLiteral("mm\xC2\xB2"))
                                     .trimmed()));
        table->setItem(t, 4, new QTableWidgetItem(mmText(mm)));
        table->setItem(t, 5, new QTableWidgetItem(inText(mm)));
    }
    outer->addWidget(table, 1);

    // HOW MANY CONDUCTORS, not how many rows. A row of eight is eight wires in the bundle and eight
    // wires to cut, and that is the number a shop wants off this page.
    auto* totals = new QLabel(&dlg);
    totals->setWordWrap(true);
    totals->setText(
        QStringLiteral("%1 conductor(s) in %2 kind(s).   k_packing = %3 (%4).   Rule: %5.%6")
            .arg(totalWires)
            .arg(rowCount())
            .arg(calc_.packingFactor, 0, 'f', 3)
            .arg(calc_.packingAuto ? QStringLiteral("from the count") : QStringLiteral("by hand"))
            .arg(calc_.rule.isEmpty() ? QStringLiteral("—") : calc_.rule)
            .arg(calc_.unknown > 0
                     ? QStringLiteral("   %1 conductor(s) had no diameter and were left out.")
                           .arg(calc_.unknown)
                     : QString()));
    outer->addWidget(totals);

    auto* bb = new QDialogButtonBox(&dlg);
    auto* copy = bb->addButton(QStringLiteral("Copy as text"), QDialogButtonBox::ActionRole);
    bb->addButton(QDialogButtonBox::Close);
    outer->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(copy, &QPushButton::clicked, &dlg, [&] {
        // Tab separated, so it lands in a spreadsheet or a report as columns and not as a paragraph.
        QString out = head->text() + QStringLiteral("\n\n#\tConductor\tAmount\tCross-section\t"
                                                    "D (mm)\tD (in)\n");
        for (int r = 0; r < table->rowCount(); ++r) {
            QStringList cells;
            for (int c = 0; c < table->columnCount(); ++c) cells << table->item(r, c)->text();
            out += cells.join(QLatin1Char('\t')) + QLatin1Char('\n');
        }
        out += QLatin1Char('\n') + totals->text() + QLatin1Char('\n');
        QGuiApplication::clipboard()->setText(out);
    });
    dlg.exec();
}

} // namespace Edraw_Desktop
