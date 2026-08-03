#include "app/VoltageDropDialog.h"

#include "app/Editor.h"
#include "model/Document.h"

#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace Edraw_Desktop {

VoltageDropDialog::VoltageDropDialog(Editor* editor, QWidget* parent)
    : QDialog(parent), editor_(editor) {
    setWindowTitle(QStringLiteral("Voltage drop"));
    resize(1080, 680);
    buildUi();
    reloadFromDocument();
}

void VoltageDropDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

    // ---- The question being asked ----
    auto* setup = new QGroupBox(QStringLiteral("Path"), this);
    auto* grid = new QGridLayout(setup);
    grid->setContentsMargins(8, 8, 8, 8);
    int r = 0;
    auto cap = [&](const QString& text) {
        auto* l = new QLabel(text, setup);
        l->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
        return l;
    };

    sourceBox_ = new QComboBox(setup);
    sourceBox_->setToolTip(QStringLiteral("Where the supply enters: a pin, or a stripped wire end"));
    loadBox_ = new QComboBox(setup);
    loadBox_->setToolTip(QStringLiteral("Where the load is, and what it draws"));
    grid->addWidget(cap(QStringLiteral("From")), r, 0);
    grid->addWidget(sourceBox_, r, 1);
    grid->addWidget(cap(QStringLiteral("To")), r, 2);
    grid->addWidget(loadBox_, r++, 3);

    voltsSpin_ = new QDoubleSpinBox(setup);
    voltsSpin_->setRange(0.1, 1000.0);
    voltsSpin_->setDecimals(2);
    voltsSpin_->setValue(24.0);
    voltsSpin_->setSuffix(QStringLiteral(" V"));
    ampsSpin_ = new QDoubleSpinBox(setup);
    ampsSpin_->setRange(0.0, 1000.0);
    ampsSpin_->setDecimals(2);
    ampsSpin_->setValue(5.0);
    ampsSpin_->setSuffix(QStringLiteral(" A"));
    grid->addWidget(cap(QStringLiteral("Supply")), r, 0);
    grid->addWidget(voltsSpin_, r, 1);
    grid->addWidget(cap(QStringLiteral("Load current")), r, 2);
    grid->addWidget(ampsSpin_, r++, 3);

    // The return leg is part of the question, not a detail: a feed-only figure is half the drop a
    // harness with a wired return actually has.
    returnKindBox_ = new QComboBox(setup);
    returnKindBox_->addItem(QStringLiteral("Back through the harness"),
                            static_cast<int>(VdReturn::ThroughHarness));
    returnKindBox_->addItem(QStringLiteral("Chassis ground"), static_cast<int>(VdReturn::Chassis));
    returnKindBox_->addItem(QStringLiteral("Feed leg only (not modelled)"),
                            static_cast<int>(VdReturn::NotModelled));
    returnKindBox_->setToolTip(QStringLiteral(
        "The drop is around the LOOP. A harness that carries its own return has twice the copper in "
        "circuit; a feed-only figure is marked as such in the report."));
    // It opens on the feed leg, because the return pins cannot be guessed: defaulting them to the
    // source would quietly send the return back down the feed wire and count that copper twice. The
    // feed-only answer is half the loop and the report says so, which is the honest place to start.
    returnKindBox_->setCurrentIndex(
        returnKindBox_->findData(static_cast<int>(VdReturn::NotModelled)));
    // Two ends, because a load bridges two pins: the one its current leaves by, and the supply-side
    // pin it finally comes back to. Guessing either of those is how a loop figure ends up half right.
    loadReturnBox_ = new QComboBox(setup);
    loadReturnBox_->setToolTip(QStringLiteral(
        "The pin at the load end that the current leaves by. On a two-pin load that is its second "
        "pin; leave it on the load's own pin when the load sits at one point."));
    returnBox_ = new QComboBox(setup);
    returnBox_->setToolTip(QStringLiteral("The supply-side pin the current finally comes back to"));
    auto* retRow = new QWidget(setup);
    auto* retLay = new QHBoxLayout(retRow);
    retLay->setContentsMargins(0, 0, 0, 0);
    retLay->setSpacing(6);
    retLay->addWidget(returnKindBox_, 1);
    retLay->addWidget(cap(QStringLiteral("from")));
    retLay->addWidget(loadReturnBox_, 1);
    retLay->addWidget(cap(QStringLiteral("to")));
    retLay->addWidget(returnBox_, 1);
    grid->addWidget(cap(QStringLiteral("Return")), r, 0);
    grid->addWidget(retRow, r++, 1, 1, 3);
    auto syncReturnRow = [this] {
        const auto kind = static_cast<VdReturn>(returnKindBox_->currentData().toInt());
        loadReturnBox_->setEnabled(kind != VdReturn::NotModelled);
        returnBox_->setEnabled(kind == VdReturn::ThroughHarness);
    };
    connect(returnKindBox_, &QComboBox::currentIndexChanged, this, syncReturnRow);
    syncReturnRow();

    caseBox_ = new QComboBox(setup);
    caseBox_->addItem(QStringLiteral("Worst case (for sign-off)"), static_cast<int>(VdCase::WorstCase));
    caseBox_->addItem(QStringLiteral("Nominal (typical)"), static_cast<int>(VdCase::Nominal));
    caseBox_->setToolTip(QStringLiteral(
        "Contact resistance spans about 2x within a size band, and rises in service. Worst case is "
        "what a design should be signed off on."));
    limitSpin_ = new QDoubleSpinBox(setup);
    limitSpin_->setRange(0.1, 50.0);
    limitSpin_->setDecimals(1);
    limitSpin_->setValue(3.0);
    limitSpin_->setSuffix(QStringLiteral(" %"));
    grid->addWidget(cap(QStringLiteral("Resistance")), r, 0);
    grid->addWidget(caseBox_, r, 1);
    grid->addWidget(cap(QStringLiteral("Limit")), r, 2);
    grid->addWidget(limitSpin_, r++, 3);

    ambientSpin_ = new QDoubleSpinBox(setup);
    ambientSpin_->setRange(-60.0, 250.0);
    ambientSpin_->setValue(20.0);
    ambientSpin_->setSuffix(QStringLiteral(" °C"));
    riseSpin_ = new QDoubleSpinBox(setup);
    riseSpin_->setRange(0.0, 200.0);
    riseSpin_->setValue(0.0);
    riseSpin_->setSuffix(QStringLiteral(" K"));
    riseSpin_->setToolTip(QStringLiteral(
        "Rise over ambient for the conductors. Copper gains about 0.4 % resistance per kelvin, so a "
        "hot bundle drops more than a cold one."));
    grid->addWidget(cap(QStringLiteral("Ambient")), r, 0);
    grid->addWidget(ambientSpin_, r, 1);
    grid->addWidget(cap(QStringLiteral("Conductor rise")), r, 2);
    grid->addWidget(riseSpin_, r++, 3);
    root->addWidget(setup);

    // ---- The breakdown ----
    rows_ = new QTableWidget(this);
    rows_->setColumnCount(11);
    rows_->setHorizontalHeaderLabels({QStringLiteral("Leg"), QStringLiteral("Segment"),
                                      QStringLiteral("Wire"), QStringLiteral("Gauge"),
                                      QStringLiteral("Material"), QStringLiteral("Length (m)"),
                                      QStringLiteral("I (A)"), QStringLiteral("Wire (mΩ)"),
                                      QStringLiteral("Pin (mΩ)"), QStringLiteral("Joint (mΩ)"),
                                      QStringLiteral("Drop")});
    rows_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rows_->setSelectionBehavior(QAbstractItemView::SelectRows);
    rows_->verticalHeader()->setVisible(false);
    root->addWidget(rows_, 2);

    // ---- Interfaces on the path: measured figures, and the ones the drawing does not state ----
    auto* ovBox = new QGroupBox(QStringLiteral("Contacts and joints on this path"), this);
    auto* ovLay = new QVBoxLayout(ovBox);
    ovLay->setContentsMargins(8, 8, 8, 8);
    overrides_ = new QTableWidget(ovBox);
    overrides_->setColumnCount(4);
    overrides_->setHorizontalHeaderLabels({QStringLiteral("Interface"), QStringLiteral("What it is"),
                                           QStringLiteral("From"), QStringLiteral("mΩ")});
    overrides_->horizontalHeader()->setStretchLastSection(false);
    overrides_->verticalHeader()->setVisible(false);
    overrides_->setToolTip(QStringLiteral(
        "Type a figure to use a measured value, or 0 for a stripped end with no mating interface. "
        "Anything the drawing does not state is listed here to be filled in."));
    connect(overrides_, &QTableWidget::cellChanged, this, [this](int row, int col) {
        if (syncing_ || col != 3) return;
        const QString nodeId = overrides_->item(row, 0)->data(Qt::UserRole).toString();
        const bool isJoint = overrides_->item(row, 0)->data(Qt::UserRole + 1).toBool();
        const QString text = overrides_->item(row, col)->text().trimmed();
        auto& map = isJoint ? jointOverrides_ : contactOverrides_;
        if (text.isEmpty()) map.remove(nodeId);
        else map.insert(nodeId, text.toDouble() / 1000.0);   // the column is in milliohms
        run();   // a figure typed here re-runs the calculation at once
    });
    ovLay->addWidget(overrides_);
    root->addWidget(ovBox, 1);

    // ---- What it came to ----
    summary_ = new QLabel(QString(), this);
    summary_->setWordWrap(true);
    summary_->setStyleSheet(QStringLiteral("color:#8fb0d6;font-weight:600;"));
    root->addWidget(summary_);
    totals_ = new QLabel(QString(), this);
    totals_->setWordWrap(true);
    totals_->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
    root->addWidget(totals_);

    auto* foot = new QHBoxLayout;
    auto* runBtn = new QPushButton(QStringLiteral("Calculate"), this);
    runBtn->setDefault(true);
    connect(runBtn, &QPushButton::clicked, this, [this] { reloadFromDocument(); run(); });
    foot->addWidget(runBtn);
    highlightBtn_ = new QPushButton(QStringLiteral("Highlight the path"), this);
    highlightBtn_->setToolTip(QStringLiteral("Select the wires the current runs through"));
    connect(highlightBtn_, &QPushButton::clicked, this, [this] {
        if (!editor_ || report_.rows.empty()) return;
        editor_->clearSelection();
        bool first = true;
        for (const VdSegmentRow& row : report_.rows) {
            // Segment rows only: the load's own contact row carries a node id, not an entity.
            if (row.wireId.isEmpty() || !row.name.contains(QStringLiteral(" → "))) continue;
            editor_->selectById(row.wireId, /*additive=*/!first);
            first = false;
        }
    });
    foot->addWidget(highlightBtn_);
    auto* csvBtn = new QPushButton(QStringLiteral("Export CSV…"), this);
    connect(csvBtn, &QPushButton::clicked, this, [this] {
        QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export voltage drop"),
                                                    QString(), QStringLiteral("CSV (*.csv)"));
        if (path.isEmpty()) return;
        if (!path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive))
            path += QStringLiteral(".csv");
        exportCsv(path);
    });
    foot->addWidget(csvBtn);
    foot->addStretch(1);
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    foot->addWidget(closeBtn);
    root->addLayout(foot);
}

void VoltageDropDialog::reloadFromDocument() {
    if (!editor_ || !editor_->document()) return;
    graph_ = buildVoltageDropGraph(*editor_->document());

    // Keep what was picked if it is still there: re-running after an edit should not reset the
    // question being asked.
    const QString keepSource = sourceBox_ ? sourceBox_->currentData().toString() : QString();
    const QString keepLoad = loadBox_ ? loadBox_->currentData().toString() : QString();
    const QString keepReturn = returnBox_ ? returnBox_->currentData().toString() : QString();
    const QString keepLoadReturn =
        loadReturnBox_ ? loadReturnBox_->currentData().toString() : QString();

    syncing_ = true;
    for (QComboBox* box : {sourceBox_, loadBox_, returnBox_, loadReturnBox_}) {
        if (!box) continue;
        box->clear();
        for (const QString& id : graph_.terminals())
            if (const VdNode* n = graph_.node(id)) box->addItem(n->name, id);
    }
    auto restore = [](QComboBox* box, const QString& id) {
        if (!box || id.isEmpty()) return;
        const int at = box->findData(id);
        if (at >= 0) box->setCurrentIndex(at);
    };
    restore(sourceBox_, keepSource);
    restore(loadBox_, keepLoad);
    restore(returnBox_, keepReturn);
    restore(loadReturnBox_, keepLoadReturn);

    // A first open with nothing picked: the first pin as the source, the last as the load, which is
    // the common case for a two-connector harness.
    if (keepSource.isEmpty() && loadBox_ && loadBox_->count() > 1)
        loadBox_->setCurrentIndex(loadBox_->count() - 1);
    // The load's return starts on the load's own pin, which is the honest default: a wired return
    // has to be pointed at a pin, not guessed at.
    if (keepLoadReturn.isEmpty() && loadReturnBox_ && loadBox_)
        restore(loadReturnBox_, loadBox_->currentData().toString());
    syncing_ = false;
}

VdRequest VoltageDropDialog::buildRequest() const {
    VdRequest req;
    req.sourceNode = sourceBox_ ? sourceBox_->currentData().toString() : QString();
    req.sourceV = voltsSpin_ ? voltsSpin_->value() : 24.0;
    const QString load = loadBox_ ? loadBox_->currentData().toString() : QString();
    req.returnPath = static_cast<VdReturn>(
        returnKindBox_ ? returnKindBox_->currentData().toInt()
                       : static_cast<int>(VdReturn::ThroughHarness));
    if (!load.isEmpty()) {
        VdLoad l;
        l.nodeId = load;
        l.currentA = ampsSpin_ ? ampsSpin_->value() : 0.0;
        if (req.returnPath != VdReturn::NotModelled && loadReturnBox_) {
            const QString from = loadReturnBox_->currentData().toString();
            if (from != load) l.returnNodeId = from;
        }
        req.loads.push_back(l);
    }
    req.returnNode = req.returnPath == VdReturn::NotModelled
                         ? load   // the load itself is the reference: only the feed is counted
                         : (returnBox_ ? returnBox_->currentData().toString() : QString());
    req.resistanceCase = static_cast<VdCase>(
        caseBox_ ? caseBox_->currentData().toInt() : static_cast<int>(VdCase::WorstCase));
    req.limitPercent = limitSpin_ ? limitSpin_->value() : 3.0;
    req.ambientC = ambientSpin_ ? ambientSpin_->value() : 20.0;
    req.conductorRiseC = riseSpin_ ? riseSpin_->value() : 0.0;
    req.contactOverrides = contactOverrides_;
    req.jointOverrides = jointOverrides_;
    return req;
}

void VoltageDropDialog::run() {
    report_ = analyzeVoltageDrop(graph_, buildRequest());
    refreshResults();
    refreshOverrideTable();
}

void VoltageDropDialog::refreshResults() {
    syncing_ = true;
    rows_->setRowCount(static_cast<int>(report_.rows.size()));
    for (int i = 0; i < static_cast<int>(report_.rows.size()); ++i) {
        const VdSegmentRow& row = report_.rows[static_cast<size_t>(i)];
        auto set = [&](int col, const QString& text, const QString& tip = QString()) {
            auto* it = new QTableWidgetItem(text);
            if (!tip.isEmpty()) it->setToolTip(tip);
            // The return leg is dimmed, so the two halves of the loop read apart at a glance.
            if (row.leg == VdLeg::Return) it->setForeground(QColor("#9aa6b4"));
            rows_->setItem(i, col, it);
        };
        set(0, row.leg == VdLeg::Feed ? QStringLiteral("feed") : QStringLiteral("return"));
        set(1, row.name);
        set(2, row.label);
        set(3, row.gauge);
        set(4, row.material);
        set(5, QString::number(row.lengthM, 'f', 3));
        set(6, QString::number(row.currentA, 'f', 2));
        // Milliohms and millivolts: a segment is a few of each, and reading that off 0.0025 is how
        // mistakes get made.
        set(7, QString::number(row.wireOhms * 1000.0, 'f', 2), row.wireBasis);
        set(8, QString::number(row.pinOhms * 1000.0, 'f', 2), row.pinBasis);
        set(9, QString::number(row.spliceOhms * 1000.0, 'f', 2), row.spliceBasis);
        set(10, QStringLiteral("%1 mV (%2 %)")
                    .arg(row.dropV * 1000.0, 0, 'f', 1)
                    .arg(row.percentOfSource, 0, 'f', 2));
    }
    rows_->resizeColumnsToContents();
    syncing_ = false;

    if (report_.blocked()) {
        summary_->setText(QStringLiteral("%1 value(s) below have to be filled in first.")
                              .arg(report_.gaps.size()));
        totals_->setText(QString());
        return;
    }
    if (!report_.solved) {
        summary_->setText(report_.problem);
        totals_->setText(QString());
        return;
    }

    summary_->setText(report_.summary() +
                      (report_.exceedsLimit()
                           ? QStringLiteral("  ⚠ over the %1 % limit.").arg(report_.limitPercent)
                           : QString()));
    totals_->setText(
        QStringLiteral("Path resistance %1 mΩ  =  wire %2 + contacts %3 + joints %4.   %5")
            .arg(report_.totalOhms() * 1000.0, 0, 'f', 1)
            .arg(report_.wireOhms * 1000.0, 0, 'f', 1)
            .arg(report_.pinOhms * 1000.0, 0, 'f', 1)
            .arg(report_.spliceOhms * 1000.0, 0, 'f', 1)
            .arg(report_.assumptions.join(QStringLiteral("  "))));
}

void VoltageDropDialog::refreshOverrideTable() {
    syncing_ = true;
    // `fixable` is what separates a figure that can be typed here (a contact, a joint) from one that
    // belongs on the object itself (a wire's gauge or its length) - an editable box that does nothing
    // is worse than no box.
    struct Row { QString nodeId, name, what, from; double ohms; bool joint; bool gap; bool fixable; };
    std::vector<Row> list;

    // The interfaces the calculation actually crossed, so the table is about this path rather than
    // the whole harness.
    for (const VdSegmentRow& r : report_.rows) {
        // A segment row is named "<from> → <to>" and owns the interface it enters through; the
        // load's own contact row is named after that node and carries it in the label.
        const bool isSegment = r.name.contains(QStringLiteral(" → "));
        const QString name = isSegment ? r.name.section(QStringLiteral(" → "), 0, 0) : r.label;
        for (const VdNode& n : graph_.nodes) {
            if (n.name != name) continue;
            if (n.kind != VdNodeKind::Pin && n.kind != VdNodeKind::Splice) break;
            const bool joint = n.kind == VdNodeKind::Splice;
            const double ohms = joint ? r.spliceOhms : r.pinOhms;
            bool exists = false;
            for (const Row& had : list) exists = exists || had.nodeId == n.id;
            if (!exists)
                list.push_back({n.id, n.name,
                                joint ? QStringLiteral("%1 joint").arg(n.jointType)
                                      : QStringLiteral("size %1 contact").arg(
                                            n.contactSize.isEmpty() ? QStringLiteral("?")
                                                                    : n.contactSize),
                                joint ? r.spliceBasis : r.pinBasis, ohms, joint,
                                !joint && n.contactSize.isEmpty(), true});
            break;
        }
    }
    // Then anything the drawing does not state, whether or not it is on the path: those are what
    // stop the calculation, so they belong at the top of the list.
    for (const VdGap& g : report_.gaps) {
        bool exists = false;
        for (const Row& had : list) exists = exists || had.name == g.objectName;
        if (exists) continue;
        // A gap on an interface can be answered here; one on a wire has to be answered on the wire.
        const VdNode* node = nullptr;
        for (const VdNode& n : graph_.nodes)
            if (n.name == g.objectName) { node = &n; break; }
        const bool joint = node && node->kind == VdNodeKind::Splice;
        list.insert(list.begin(), {node ? node->id : g.objectId, g.objectName, g.property, g.hint,
                                   0.0, joint, true, node != nullptr});
    }

    overrides_->setRowCount(static_cast<int>(list.size()));
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        const Row& row = list[static_cast<size_t>(i)];
        auto* first = new QTableWidgetItem(row.name);
        first->setData(Qt::UserRole, row.nodeId);
        first->setData(Qt::UserRole + 1, row.joint);
        first->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (row.gap) first->setForeground(QColor("#e6b45a"));
        overrides_->setItem(i, 0, first);
        for (int c = 1; c <= 2; ++c) {
            auto* it = new QTableWidgetItem(c == 1 ? row.what : row.from);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            if (row.gap) it->setForeground(QColor("#e6b45a"));
            overrides_->setItem(i, c, it);
        }
        const QHash<QString, double>& map = row.joint ? jointOverrides_ : contactOverrides_;
        QTableWidgetItem* last = nullptr;
        if (row.fixable) {
            last = new QTableWidgetItem(
                map.contains(row.nodeId)
                    ? QString::number(map.value(row.nodeId) * 1000.0, 'f', 2)
                    : (row.gap ? QString() : QString::number(row.ohms * 1000.0, 'f', 2)));
        } else {
            // Not a resistance to type: it is a property of the wire, and that is where it is set.
            last = new QTableWidgetItem(QStringLiteral("set it on the wire"));
            last->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            last->setForeground(QColor("#e6b45a"));
        }
        overrides_->setItem(i, 3, last);
    }
    overrides_->resizeColumnsToContents();
    syncing_ = false;
}

bool VoltageDropDialog::setSource(const QString& nodeId) {
    const int at = sourceBox_ ? sourceBox_->findData(nodeId) : -1;
    if (at < 0) return false;
    sourceBox_->setCurrentIndex(at);
    return true;
}

bool VoltageDropDialog::setLoad(const QString& nodeId) {
    const int at = loadBox_ ? loadBox_->findData(nodeId) : -1;
    if (at < 0) return false;
    loadBox_->setCurrentIndex(at);
    return true;
}

void VoltageDropDialog::setSourceVoltage(double v) {
    if (voltsSpin_) voltsSpin_->setValue(v);
}

void VoltageDropDialog::setLoadCurrent(double a) {
    if (ampsSpin_) ampsSpin_->setValue(a);
}

bool VoltageDropDialog::exportCsv(const QString& path) const {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    auto field = [](const QString& v) {
        if (!v.contains(QLatin1Char(',')) && !v.contains(QLatin1Char('"'))) return v;
        QString q = v;
        q.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QStringLiteral("\"%1\"").arg(q);
    };
    out << "Leg,Segment,Wire,Gauge,Material,Length_m,Current_A,Wire_mOhm,Pin_mOhm,Joint_mOhm,"
           "Drop_mV,Drop_percent\r\n";
    for (const VdSegmentRow& r : report_.rows)
        out << (r.leg == VdLeg::Feed ? "feed" : "return") << ',' << field(r.name) << ','
            << field(r.label) << ',' << field(r.gauge) << ',' << field(r.material) << ','
            << QString::number(r.lengthM, 'f', 3) << ',' << QString::number(r.currentA, 'f', 2) << ','
            << QString::number(r.wireOhms * 1000.0, 'f', 3) << ','
            << QString::number(r.pinOhms * 1000.0, 'f', 3) << ','
            << QString::number(r.spliceOhms * 1000.0, 'f', 3) << ','
            << QString::number(r.dropV * 1000.0, 'f', 2) << ','
            << QString::number(r.percentOfSource, 'f', 3) << "\r\n";
    out << "\r\n";
    out << "Supply," << QString::number(report_.sourceV, 'f', 2) << " V\r\n";
    out << "At the load," << QString::number(report_.endVoltageV, 'f', 3) << " V\r\n";
    out << "Total drop," << QString::number(report_.totalDropV, 'f', 3) << " V,"
        << QString::number(report_.dropPercent(), 'f', 2) << " %\r\n";
    out << "Path resistance," << QString::number(report_.totalOhms() * 1000.0, 'f', 2) << " mOhm\r\n";
    for (const QString& a : report_.assumptions) out << "Assumption," << field(a) << "\r\n";
    return true;
}

} // namespace Edraw_Desktop
