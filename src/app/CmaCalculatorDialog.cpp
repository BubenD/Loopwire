#include "app/CmaCalculatorDialog.h"

#include "harness/PartsLibrary.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace Loopwire {

namespace {
const CmaUnit kUnits[] = {CmaUnit::Awg, CmaUnit::Mm2, CmaUnit::Kcmil, CmaUnit::Cmil, CmaUnit::DiameterMm};

QString cmText(double cm) { return cm > 0.0 ? QString::number(cm, 'f', 0) : QStringLiteral("-"); }

// A LIBRARY PART AS CONDUCTORS: a wire, one of its gauge; a cable, its cores of their gauge (the smaller end of a range).
// The gauge in AWG unless the part says mm2. False for a part with no gauge to read.
bool conductorOf(const LibraryPart& part, CmaConductor& out) {
    const bool mm2 = part.value(QStringLiteral("gaugeMode")).compare(QLatin1String("mm2"), Qt::CaseInsensitive) == 0;
    double gauge = 0.0;
    bool ok = false;
    for (const char* key : {"gauge", "gaugeMin", "gaugeMax"}) {
        QString said = part.value(QLatin1String(key)).trimmed();
        said.remove(QStringLiteral("AWG"), Qt::CaseInsensitive);
        said.remove(QStringLiteral("mm2"), Qt::CaseInsensitive);
        gauge = said.trimmed().toDouble(&ok);
        if (ok) break;
    }
    if (!ok) return false;
    out.size = gauge;
    out.unit = mm2 ? CmaUnit::Mm2 : CmaUnit::Awg;
    out.count = 1;
    if (part.kind == PartKind::Cable) {
        bool coresOk = false;
        const int cores = part.value(QStringLiteral("cores")).toInt(&coresOk);
        if (coresOk && cores > 0) out.count = cores;
    }
    out.what = (part.mfrPn.isEmpty() ? QString() : part.mfrPn + QStringLiteral(" - ")) + part.description;
    return out.each() > 0.0;
}
} // namespace

CmaCalculatorDialog::CmaCalculatorDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("cmaCalculator"));
    setWindowTitle(QStringLiteral("CMA calculator"));
    setModal(false);
    resize(760, 560);
    auto* root = new QVBoxLayout(this);

    // ---- THE CONDUCTORS ----
    auto* list = new QGroupBox(QStringLiteral("Conductors"), this);
    auto* lv = new QVBoxLayout(list);
    // Its diameter each, in inches and millimetres, as the solid round conductor its area is.
    table_ = new QTableWidget(0, 8, list);
    table_->setObjectName(QStringLiteral("cmaTable"));
    table_->setHorizontalHeaderLabels({QStringLiteral("Conductor"), QStringLiteral("Size"), QStringLiteral("Unit"),
                                       QStringLiteral("Qty"), QStringLiteral("D in / mm"),
                                       QStringLiteral("CMA each"), QStringLiteral("mm2 each"), QStringLiteral("CMA total")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    lv->addWidget(table_, 1);
    auto* buttons = new QHBoxLayout;
    auto* fromParts = new QPushButton(QStringLiteral("Add from parts list..."), list);
    fromParts->setObjectName(QStringLiteral("cmaAddPart"));
    fromParts->setToolTip(QStringLiteral("A wire by its gauge, a cable by its cores and their gauge"));
    connect(fromParts, &QPushButton::clicked, this, [this] { addFromLibrary(); });
    auto* manual = new QPushButton(QStringLiteral("Add size"), list);
    manual->setObjectName(QStringLiteral("cmaAddManual"));
    manual->setToolTip(QStringLiteral("A conductor typed in: its size and what it is given in"));
    connect(manual, &QPushButton::clicked, this, [this] { addConductor({QStringLiteral("manual"), 20.0, CmaUnit::Awg, 1}); });
    auto* remove = new QPushButton(QStringLiteral("Remove"), list);
    remove->setObjectName(QStringLiteral("cmaRemove"));
    connect(remove, &QPushButton::clicked, this, [this] {
        const int row = table_->currentRow();
        if (row < 0 || row >= static_cast<int>(rows_.size())) return;
        rows_.erase(rows_.begin() + row);
        rebuildTable();
    });
    auto* clear = new QPushButton(QStringLiteral("Clear"), list);
    connect(clear, &QPushButton::clicked, this, [this] { rows_.clear(); rebuildTable(); });
    buttons->addWidget(fromParts);
    buttons->addWidget(manual);
    buttons->addWidget(remove);
    buttons->addWidget(clear);
    buttons->addStretch(1);
    lv->addLayout(buttons);
    // THE ANSWERS STAND OUT: bold, larger, on a panel of their own.
    const QString answer = QStringLiteral("QLabel{font-weight:700;font-size:15px;color:#f4f7fb;background:#24476e;"
                                          "border:1px solid #3d6a9c;border-radius:4px;padding:8px 10px;}");
    totals_ = new QLabel(list);
    totals_->setObjectName(QStringLiteral("cmaTotals"));
    totals_->setStyleSheet(answer);
    totals_->setWordWrap(true);
    totals_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lv->addWidget(totals_);
    root->addWidget(list, 1);

    // ---- SIZING A RUN ----
    auto* size = new QGroupBox(QStringLiteral("Size for a run"), this);
    auto* form = new QFormLayout(size);
    auto spin = [&](const char* name, double max, double value, const QString& suffix, int decimals) {
        auto* s = new QDoubleSpinBox(size);
        s->setObjectName(QLatin1String(name));
        s->setRange(0.0, max);
        s->setDecimals(decimals);
        s->setValue(value);
        s->setSuffix(suffix);
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] { refresh(); });
        return s;
    };
    amps_ = spin("cmaAmps", 10000.0, 10.0, QStringLiteral(" A"), 2);
    length_ = spin("cmaLength", 100000.0, 5.0, QStringLiteral(" m"), 2);
    drop_ = spin("cmaDrop", 1000.0, 0.5, QStringLiteral(" V"), 3);
    material_ = new QComboBox(size);
    material_->setObjectName(QStringLiteral("cmaMaterial"));
    material_->addItem(QStringLiteral("Copper (K 10.75)"), kCmaCopperK);
    material_->addItem(QStringLiteral("Aluminium (K 17.0)"), kCmaAluminiumK);
    connect(material_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    form->addRow(QStringLiteral("Current"), amps_);
    form->addRow(QStringLiteral("Length, one way"), length_);
    form->addRow(QStringLiteral("Voltage drop allowed"), drop_);
    form->addRow(QStringLiteral("Conductor"), material_);
    sizing_ = new QLabel(size);
    sizing_->setObjectName(QStringLiteral("cmaSizing"));
    sizing_->setStyleSheet(answer);
    sizing_->setWordWrap(true);
    sizing_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(sizing_);
    root->addWidget(size);

    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(close);
    rebuildTable();
}

void CmaCalculatorDialog::addConductor(const CmaConductor& c) {
    rows_.push_back(c);
    rebuildTable();
}

bool CmaCalculatorDialog::addPart(const LibraryPart& part, int count) {
    CmaConductor c;
    if (!conductorOf(part, c)) return false;
    c.count *= std::max(1, count);
    addConductor(c);
    return true;
}

void CmaCalculatorDialog::setRun(double amps, double lengthM, double dropVolts, bool aluminium) {
    syncing_ = true;
    amps_->setValue(amps);
    length_->setValue(lengthM);
    drop_->setValue(dropVolts);
    material_->setCurrentIndex(aluminium ? 1 : 0);
    syncing_ = false;
    refresh();
}

void CmaCalculatorDialog::rebuildTable() {
    syncing_ = true;
    table_->setRowCount(static_cast<int>(rows_.size()));
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r) {
        const CmaConductor& c = rows_[static_cast<size_t>(r)];
        auto* what = new QLineEdit(c.what, table_);
        connect(what, &QLineEdit::textEdited, this, [this, r](const QString& t) { rows_[static_cast<size_t>(r)].what = t; });
        table_->setCellWidget(r, 0, what);
        auto* sizeBox = new QDoubleSpinBox(table_);
        sizeBox->setRange(-3.0, 1.0e7);
        sizeBox->setDecimals(c.unit == CmaUnit::Awg ? 0 : 3);
        sizeBox->setValue(c.size);
        connect(sizeBox, &QDoubleSpinBox::valueChanged, this, [this, r](double v) {
            if (syncing_) return;
            rows_[static_cast<size_t>(r)].size = v;
            refresh();
        });
        table_->setCellWidget(r, 1, sizeBox);
        auto* unitBox = new QComboBox(table_);
        for (CmaUnit u : kUnits) unitBox->addItem(cmaUnitName(u), static_cast<int>(u));
        unitBox->setCurrentIndex(unitBox->findData(static_cast<int>(c.unit)));
        connect(unitBox, &QComboBox::currentIndexChanged, this, [this, r, unitBox, sizeBox](int) {
            if (syncing_) return;
            rows_[static_cast<size_t>(r)].unit = static_cast<CmaUnit>(unitBox->currentData().toInt());
            sizeBox->setDecimals(rows_[static_cast<size_t>(r)].unit == CmaUnit::Awg ? 0 : 3);
            refresh();
        });
        table_->setCellWidget(r, 2, unitBox);
        auto* qty = new QSpinBox(table_);
        qty->setRange(1, 100000);
        qty->setValue(c.count);
        connect(qty, &QSpinBox::valueChanged, this, [this, r](int v) {
            if (syncing_) return;
            rows_[static_cast<size_t>(r)].count = v;
            refresh();
        });
        table_->setCellWidget(r, 3, qty);
        for (int col = 4; col < 8; ++col) {
            auto* item = new QTableWidgetItem;
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table_->setItem(r, col, item);
        }
    }
    syncing_ = false;
    refresh();
}

void CmaCalculatorDialog::refresh() {
    if (syncing_) return;
    for (int r = 0; r < static_cast<int>(rows_.size()) && r < table_->rowCount(); ++r) {
        const CmaConductor& c = rows_[static_cast<size_t>(r)];
        const double each = c.each();
        // The diameter of a solid round conductor of that area: its mils are the root of its circular mils.
        const double inches = each > 0.0 ? std::sqrt(each) / 1000.0 : 0.0;
        if (auto* item = table_->item(r, 4))
            item->setText(each > 0.0 ? QStringLiteral("%1 / %2").arg(inches, 0, 'f', 4).arg(inches * 25.4, 0, 'f', 3) : QStringLiteral("-"));
        if (auto* item = table_->item(r, 5)) item->setText(each > 0.0 ? cmText(each) : QStringLiteral("not a size"));
        if (auto* item = table_->item(r, 6)) item->setText(each > 0.0 ? QString::number(mm2FromCma(each), 'f', 3) : QStringLiteral("-"));
        if (auto* item = table_->item(r, 7)) item->setText(cmText(c.total()));
    }
    totals_->setText(summary());
    sizing_->setText(sizing());
}

QString CmaCalculatorDialog::summary() const {
    const double total = cmaTotal(rows_);
    if (total <= 0.0) return QStringLiteral("Add conductors - from the parts list, or by their size.");
    int count = 0;
    for (const CmaConductor& c : rows_) count += c.each() > 0.0 ? c.count : 0;
    const double metric = mm2ForCma(total);
    return QStringLiteral("%1 conductor(s): CMA %2 in all (%3 mm2). As one conductor: %4 AWG, or %5.")
        .arg(count)
        .arg(cmText(total))
        .arg(mm2FromCma(total), 0, 'f', 3)
        .arg(awgName(awgForCma(total)))
        .arg(metric > 0.0 ? QStringLiteral("%1 mm2").arg(metric) : QStringLiteral("more than 240 mm2"));
}

QString CmaCalculatorDialog::sizing() const {
    const double need = cmaRequired(amps_->value(), length_->value(), drop_->value(), material_->currentData().toDouble());
    if (need <= 0.0) return QStringLiteral("Give the current, the length and the drop allowed.");
    const double metric = mm2ForCma(need);
    QString said = QStringLiteral("Needs CMA %1 (%2 mm2): at least %3 AWG, or %4.")
                       .arg(cmText(need))
                       .arg(mm2FromCma(need), 0, 'f', 3)
                       .arg(awgName(awgForCma(need)))
                       .arg(metric > 0.0 ? QStringLiteral("%1 mm2").arg(metric) : QStringLiteral("more than 240 mm2"));
    const double have = cmaTotal(rows_);
    if (have > 0.0) {
        said += have >= need ? QStringLiteral(" The conductors listed (CMA %1) carry it, CMA %2 per amp.").arg(cmText(have)).arg(have / amps_->value(), 0, 'f', 0)
                             : QStringLiteral(" The conductors listed (CMA %1) fall short by CMA %2.").arg(cmText(have)).arg(cmText(need - have));
    }
    return said;
}

void CmaCalculatorDialog::addFromLibrary() {
    PartsCatalog catalog;
    catalog.openRemembered();
    const PartsLibrary lib = catalog.merged();
    QDialog pick(this);
    pick.setWindowTitle(QStringLiteral("Add from the parts list"));
    pick.resize(520, 420);
    auto* v = new QVBoxLayout(&pick);
    auto* filter = new QLineEdit(&pick);
    filter->setPlaceholderText(QStringLiteral("Filter by P/N or description"));
    v->addWidget(filter);
    auto* list = new QListWidget(&pick);
    list->setObjectName(QStringLiteral("cmaParts"));
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    v->addWidget(list, 1);
    std::vector<const LibraryPart*> offered;
    for (const LibraryPart& p : lib.parts) {
        if (p.kind != PartKind::Wire && p.kind != PartKind::Cable) continue;
        CmaConductor c;
        if (!conductorOf(p, c)) continue;
        offered.push_back(&p);
        auto* item = new QListWidgetItem(QStringLiteral("%1   (%2 x %3 %4)").arg(c.what).arg(c.count).arg(c.size).arg(cmaUnitName(c.unit)), list);
        item->setData(Qt::UserRole, static_cast<int>(offered.size() - 1));
    }
    connect(filter, &QLineEdit::textChanged, list, [list](const QString& t) {
        for (int i = 0; i < list->count(); ++i) list->item(i)->setHidden(!list->item(i)->text().contains(t, Qt::CaseInsensitive));
    });
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(QStringLiteral("How many of each"), &pick));
    auto* many = new QSpinBox(&pick);
    many->setRange(1, 10000);
    row->addWidget(many);
    row->addStretch(1);
    v->addLayout(row);
    auto* ok = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &pick);
    connect(ok, &QDialogButtonBox::accepted, &pick, &QDialog::accept);
    connect(ok, &QDialogButtonBox::rejected, &pick, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &pick, &QDialog::accept);
    v->addWidget(ok);
    if (offered.empty()) list->addItem(QStringLiteral("No wire or cable in the parts list has a gauge to read"));
    if (pick.exec() != QDialog::Accepted) return;
    for (QListWidgetItem* item : list->selectedItems()) {
        bool isPart = false;
        const int at = item->data(Qt::UserRole).toInt(&isPart);
        if (isPart && at >= 0 && at < static_cast<int>(offered.size())) addPart(*offered[static_cast<size_t>(at)], many->value());
    }
}

} // namespace Loopwire
