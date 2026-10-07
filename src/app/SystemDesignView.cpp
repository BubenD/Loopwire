#include "app/SystemDesignView.h"

#include "app/DrawingFinderDialog.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace Edraw_Desktop {

namespace {

QLineEdit* field(QWidget* parent, const char* name, const QString& tip) {
    auto* e = new QLineEdit(parent);
    e->setObjectName(QLatin1String(name));
    e->setToolTip(tip);
    return e;
}

} // namespace

SystemDesignView::SystemDesignView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("systemDesign"));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    auto* outer = new QSplitter(Qt::Vertical, this);
    auto* split = new QSplitter(Qt::Horizontal, outer);
    canvas_ = new SystemCanvas(split);
    canvas_->setDesign(&design_);
    // ON THE RIGHT: WHAT IS SELECTED.
    panel_ = new QStackedWidget(split);
    panel_->setObjectName(QStringLiteral("systemPanel"));
    panel_->setMinimumWidth(270);
    nothing_ = new QLabel(QStringLiteral("<b>System design</b><br><br>Put units with <i>Unit</i>, ports on their sides with "
                                         "<i>Port</i>, and draw cables from port to port with <i>Cable</i> (or drag from a "
                                         "port). Click a unit or a cable to see and change what is its own here."),
                          panel_);
    nothing_->setWordWrap(true);
    nothing_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    nothing_->setMargin(8);
    panel_->addWidget(nothing_);
    // A UNIT.
    {
        auto* page = new QWidget(panel_);
        auto* v = new QVBoxLayout(page);
        auto* form = new QFormLayout;
        unitName_ = field(page, "systemUnitName", QStringLiteral("The unit's name on the diagram"));
        unitPn_ = field(page, "systemUnitPn", QStringLiteral("Its part number"));
        unitRev_ = field(page, "systemUnitRev", QStringLiteral("Its revision"));
        unitDesc_ = field(page, "systemUnitDesc", QStringLiteral("What it is"));
        form->addRow(QStringLiteral("Name"), unitName_);
        form->addRow(QStringLiteral("P/N"), unitPn_);
        form->addRow(QStringLiteral("Rev"), unitRev_);
        form->addRow(QStringLiteral("Description"), unitDesc_);
        v->addLayout(form);
        v->addWidget(new QLabel(QStringLiteral("<b>Ports</b> - name, side, the connector it takes, pins"), page));
        ports_ = new QTableWidget(0, 4, page);
        ports_->setObjectName(QStringLiteral("systemPorts"));
        ports_->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Side"), QStringLiteral("Connector"), QStringLiteral("Pins")});
        ports_->horizontalHeader()->setStretchLastSection(true);
        ports_->verticalHeader()->setVisible(false);
        v->addWidget(ports_, 1);
        auto* buttons = new QHBoxLayout;
        auto* add = new QPushButton(QStringLiteral("Add port"), page);
        auto* remove = new QPushButton(QStringLiteral("Remove port"), page);
        buttons->addWidget(add);
        buttons->addWidget(remove);
        v->addLayout(buttons);
        auto applyUnit = [this] {
            SysUnit* u = design_.unit(canvas_->selectedUnit());
            if (filling_ || !u) return;
            if (u->name == unitName_->text() && u->pn == unitPn_->text() && u->rev == unitRev_->text() && u->description == unitDesc_->text())
                return;
            aboutToChange();
            u->name = unitName_->text();
            u->pn = unitPn_->text();
            u->rev = unitRev_->text();
            u->description = unitDesc_->text();
            commit();
        };
        for (QLineEdit* e : {unitName_, unitPn_, unitRev_, unitDesc_}) connect(e, &QLineEdit::editingFinished, this, applyUnit);
        connect(ports_, &QTableWidget::cellChanged, this, [this](int row, int col) {
            SysUnit* u = design_.unit(canvas_->selectedUnit());
            if (filling_ || !u || row < 0 || row >= static_cast<int>(u->ports.size())) return;
            if (col == 1) return;   // the side is a box of its own
            aboutToChange();
            SysPort& p = u->ports[static_cast<size_t>(row)];
            const QString text = ports_->item(row, col) ? ports_->item(row, col)->text() : QString();
            if (col == 0) p.name = text;
            else if (col == 2) p.connector = text;
            else p.pins = text.toInt();
            commit();
        });
        connect(add, &QPushButton::clicked, this, [this] {
            if (!design_.unit(canvas_->selectedUnit())) return;
            aboutToChange();
            design_.addPort(canvas_->selectedUnit(), QStringLiteral("right"), 0.5);
            commit();
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            SysUnit* u = design_.unit(canvas_->selectedUnit());
            const int row = ports_->currentRow();
            if (!u || row < 0 || row >= static_cast<int>(u->ports.size())) return;
            aboutToChange();
            design_.removePort(u->id, u->ports[static_cast<size_t>(row)].id);
            commit();
        });
        panel_->addWidget(page);
    }
    // A CABLE.
    {
        auto* page = new QWidget(panel_);
        auto* v = new QVBoxLayout(page);
        auto* form = new QFormLayout;
        cableName_ = field(page, "systemCableName", QStringLiteral("The cable's name on the diagram (W1, W2...)"));
        cablePn_ = field(page, "systemCablePn", QStringLiteral("Its part number - the drawing linked should be this number"));
        cableRev_ = field(page, "systemCableRev", QStringLiteral("The revision the system calls for - the drawing linked should be it"));
        cableDesc_ = field(page, "systemCableDesc", QStringLiteral("What it is"));
        cableLength_ = new QDoubleSpinBox(page);
        cableLength_->setObjectName(QStringLiteral("systemCableLength"));
        cableLength_->setRange(0.0, 1000000.0);
        cableLength_->setDecimals(0);
        cableLength_->setSuffix(QStringLiteral(" mm"));
        cableLength_->setKeyboardTracking(false);
        cableWires_ = new QSpinBox(page);
        cableWires_->setObjectName(QStringLiteral("systemCableWires"));
        cableWires_->setRange(0, 9999);
        cableWires_->setKeyboardTracking(false);
        cableEnds_ = new QLabel(page);
        cableEnds_->setWordWrap(true);
        form->addRow(QStringLiteral("Name"), cableName_);
        form->addRow(QStringLiteral("P/N"), cablePn_);
        form->addRow(QStringLiteral("Rev"), cableRev_);
        form->addRow(QStringLiteral("Description"), cableDesc_);
        form->addRow(QStringLiteral("Length"), cableLength_);
        form->addRow(QStringLiteral("Wires"), cableWires_);
        form->addRow(QStringLiteral("From - to"), cableEnds_);
        v->addLayout(form);
        v->addWidget(new QLabel(QStringLiteral("<b>Its drawing</b>"), page));
        cableDrawing_ = field(page, "systemCableDrawing", QStringLiteral("The drawing that defines this cable"));
        cableDrawing_->setReadOnly(true);
        v->addWidget(cableDrawing_);
        auto* buttons = new QHBoxLayout;
        auto* link = new QPushButton(QStringLiteral("Link..."), page);
        link->setToolTip(QStringLiteral("Link a drawing file (DWX, DXF, DWG) to this cable"));
        auto* find = new QPushButton(QStringLiteral("Find..."), page);
        find->setToolTip(QStringLiteral("Find the drawing among the files - by its name or its drawing number"));
        auto* open = new QPushButton(QStringLiteral("Open"), page);
        auto* unlink = new QPushButton(QStringLiteral("Unlink"), page);
        for (QPushButton* b : {link, find, open, unlink}) buttons->addWidget(b);
        v->addLayout(buttons);
        connect(link, &QPushButton::clicked, this, [this] { linkDrawingFromFile(); });
        connect(find, &QPushButton::clicked, this, [this] { findDrawing(); });
        connect(open, &QPushButton::clicked, this, [this] { openDrawing(); });
        connect(unlink, &QPushButton::clicked, this, [this] { linkDrawing(canvas_->selectedCable(), QString()); });
        cableRead_ = new QLabel(page);
        cableRead_->setWordWrap(true);
        v->addWidget(cableRead_);
        cableStatus_ = new QLabel(page);
        cableStatus_->setObjectName(QStringLiteral("systemCableStatus"));
        cableStatus_->setWordWrap(true);
        v->addWidget(cableStatus_);
        v->addStretch(1);
        auto applyCable = [this] {
            SysCable* c = design_.cable(canvas_->selectedCable());
            if (filling_ || !c) return;
            if (c->name == cableName_->text() && c->pn == cablePn_->text() && c->rev == cableRev_->text() &&
                c->description == cableDesc_->text() && c->lengthMm == cableLength_->value() && c->wires == cableWires_->value())
                return;
            aboutToChange();
            c->name = cableName_->text();
            c->pn = cablePn_->text();
            c->rev = cableRev_->text();
            c->description = cableDesc_->text();
            c->lengthMm = cableLength_->value();
            c->wires = cableWires_->value();
            commit();
        };
        for (QLineEdit* e : {cableName_, cablePn_, cableRev_, cableDesc_}) connect(e, &QLineEdit::editingFinished, this, applyCable);
        connect(cableLength_, &QDoubleSpinBox::valueChanged, this, applyCable);
        connect(cableWires_, &QSpinBox::valueChanged, this, applyCable);
        panel_->addWidget(page);
    }
    split->addWidget(canvas_);
    split->addWidget(panel_);
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 1);
    // UNDER IT: THE CABLE LIST, AND WHAT THE CHECK FOUND.
    tabs_ = new QTabWidget(outer);
    tabs_->setObjectName(QStringLiteral("systemTabs"));
    cables_ = new QTableWidget(0, 9, tabs_);
    cables_->setObjectName(QStringLiteral("systemCables"));
    cables_->setHorizontalHeaderLabels({QStringLiteral("Cable"), QStringLiteral("P/N"), QStringLiteral("Rev"), QStringLiteral("From"),
                                        QStringLiteral("To"), QStringLiteral("Length"), QStringLiteral("Drawing"), QStringLiteral("Drawing rev"),
                                        QStringLiteral("Status")});
    cables_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cables_->setSelectionBehavior(QAbstractItemView::SelectRows);
    cables_->verticalHeader()->setVisible(false);
    cables_->horizontalHeader()->setStretchLastSection(true);
    connect(cables_, &QTableWidget::itemSelectionChanged, this, [this] {
        if (filling_) return;
        const QList<QTableWidgetItem*> sel = cables_->selectedItems();
        if (!sel.isEmpty()) canvas_->select(QString(), QString(), cables_->item(sel.first()->row(), 0)->data(Qt::UserRole).toString());
    });
    tabs_->addTab(cables_, QStringLiteral("Cables"));
    found_ = new QListWidget(tabs_);
    found_->setObjectName(QStringLiteral("systemIssues"));
    connect(found_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        canvas_->select(it->data(Qt::UserRole).toString(), QString(), it->data(Qt::UserRole + 1).toString());
    });
    tabs_->addTab(found_, QStringLiteral("Check"));
    outer->addWidget(split);
    outer->addWidget(tabs_);
    outer->setStretchFactor(0, 4);
    outer->setStretchFactor(1, 1);
    lay->addWidget(outer);

    connect(canvas_, &SystemCanvas::aboutToChange, this, [this] { aboutToChange(); });
    connect(canvas_, &SystemCanvas::changed, this, [this] { commit(); });
    connect(canvas_, &SystemCanvas::selectionChanged, this, [this] { syncPanel(); });
    refresh();
}

void SystemDesignView::setStore(std::function<QJsonObject()> load, std::function<void(const QJsonObject&)> save) {
    load_ = std::move(load);
    save_ = std::move(save);
    reload();
}

void SystemDesignView::reload() {
    design_ = SystemDesign::fromJson(load_ ? load_() : QJsonObject());
    history_.clear();
    issues_.clear();
    canvas_->setDesign(&design_);
    refresh();
    canvas_->fit();
}

void SystemDesignView::setTool(SysTool tool) { canvas_->setTool(tool); }

void SystemDesignView::aboutToChange() {
    history_.push_back(design_.toJson());
    if (history_.size() > 100) history_.erase(history_.begin());
}

void SystemDesignView::commit() {
    if (save_) save_(design_.empty() ? QJsonObject() : design_.toJson());
    refresh();
    emit changed();
}

void SystemDesignView::undo() {
    if (history_.empty()) return;
    const QJsonObject back = history_.back();
    history_.pop_back();
    const QString unit = canvas_->selectedUnit(), cable = canvas_->selectedCable();
    design_ = SystemDesign::fromJson(back);
    canvas_->setDesign(&design_);
    canvas_->select(design_.unit(unit) ? unit : QString(), QString(), design_.cable(cable) ? cable : QString());
    if (save_) save_(design_.empty() ? QJsonObject() : design_.toJson());
    refresh();
    emit changed();
}

void SystemDesignView::refresh() {
    syncPanel();
    fillCables();
    fillIssues();
    canvas_->update();
}

QString SystemDesignView::status(const SysCable& c) const {
    switch (SystemDesign::linkState(c)) {
    case SysLinkState::None: return QStringLiteral("no drawing linked");
    case SysLinkState::Missing: return QStringLiteral("its drawing is not there");
    case SysLinkState::Mismatch:
        return QStringLiteral("rev %1 called for - the drawing is rev %2").arg(c.rev, c.drawingRev.isEmpty() ? QStringLiteral("(none)") : c.drawingRev);
    case SysLinkState::Match: break;
    }
    return QStringLiteral("ok - the drawing is rev %1").arg(c.drawingRev.isEmpty() ? QStringLiteral("(none)") : c.drawingRev);
}

void SystemDesignView::syncPanel() {
    filling_ = true;
    const SysUnit* u = design_.unit(canvas_->selectedUnit());
    const SysCable* c = design_.cable(canvas_->selectedCable());
    if (c) {
        panel_->setCurrentIndex(2);
        cableName_->setText(c->name);
        cablePn_->setText(c->pn);
        cableRev_->setText(c->rev);
        cableDesc_->setText(c->description);
        cableLength_->setValue(c->lengthMm);
        cableWires_->setValue(c->wires);
        const SysUnit* a = design_.unit(c->aUnit);
        const SysUnit* b = design_.unit(c->bUnit);
        const SysPort* pa = a ? a->port(c->aPort) : nullptr;
        const SysPort* pb = b ? b->port(c->bPort) : nullptr;
        cableEnds_->setText(QStringLiteral("%1.%2 - %3.%4").arg(a ? a->name : QString(), pa ? pa->name : QString(), b ? b->name : QString(),
                                                                pb ? pb->name : QString()));
        cableDrawing_->setText(c->drawing.isEmpty() ? QString() : QDir::toNativeSeparators(c->drawing));
        cableRead_->setText(c->drawing.isEmpty() ? QString()
                                                 : QStringLiteral("Read off it: drawing No. %1, rev %2")
                                                       .arg(c->drawingNo.isEmpty() ? QStringLiteral("(none)") : c->drawingNo,
                                                            c->drawingRev.isEmpty() ? QStringLiteral("(none)") : c->drawingRev));
        const SysLinkState st = SystemDesign::linkState(*c);
        cableStatus_->setText(QStringLiteral("<b>%1</b>").arg(status(*c).toHtmlEscaped()));
        cableStatus_->setStyleSheet(st == SysLinkState::Match ? QStringLiteral("color:#2f8a4a;")
                                    : st == SysLinkState::None ? QStringLiteral("color:#b07a10;")
                                                               : QStringLiteral("color:#c0392b;"));
    } else if (u) {
        panel_->setCurrentIndex(1);
        unitName_->setText(u->name);
        unitPn_->setText(u->pn);
        unitRev_->setText(u->rev);
        unitDesc_->setText(u->description);
        ports_->setRowCount(static_cast<int>(u->ports.size()));
        for (int i = 0; i < static_cast<int>(u->ports.size()); ++i) {
            const SysPort& p = u->ports[static_cast<size_t>(i)];
            ports_->setItem(i, 0, new QTableWidgetItem(p.name));
            auto* side = new QComboBox(ports_);
            side->addItems({QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("top"), QStringLiteral("bottom")});
            side->setCurrentText(p.side);
            const QString unitId = u->id, portId = p.id;
            connect(side, &QComboBox::currentTextChanged, this, [this, unitId, portId](const QString& s) {
                SysUnit* uu = design_.unit(unitId);
                SysPort* pp = uu ? uu->port(portId) : nullptr;
                if (filling_ || !pp) return;
                aboutToChange();
                pp->side = s;
                commit();
            });
            ports_->setCellWidget(i, 1, side);
            ports_->setItem(i, 2, new QTableWidgetItem(p.connector));
            ports_->setItem(i, 3, new QTableWidgetItem(p.pins > 0 ? QString::number(p.pins) : QString()));
        }
    } else {
        panel_->setCurrentIndex(0);
    }
    filling_ = false;
}

void SystemDesignView::fillCables() {
    filling_ = true;
    cables_->setRowCount(0);
    for (const SysCable& c : design_.cables) {
        const int row = cables_->rowCount();
        cables_->insertRow(row);
        const SysUnit* a = design_.unit(c.aUnit);
        const SysUnit* b = design_.unit(c.bUnit);
        const SysPort* pa = a ? a->port(c.aPort) : nullptr;
        const SysPort* pb = b ? b->port(c.bPort) : nullptr;
        auto* name = new QTableWidgetItem(c.name);
        name->setData(Qt::UserRole, c.id);
        cables_->setItem(row, 0, name);
        cables_->setItem(row, 1, new QTableWidgetItem(c.pn));
        cables_->setItem(row, 2, new QTableWidgetItem(c.rev));
        cables_->setItem(row, 3, new QTableWidgetItem(QStringLiteral("%1.%2").arg(a ? a->name : QString(), pa ? pa->name : QString())));
        cables_->setItem(row, 4, new QTableWidgetItem(QStringLiteral("%1.%2").arg(b ? b->name : QString(), pb ? pb->name : QString())));
        cables_->setItem(row, 5, new QTableWidgetItem(c.lengthMm > 0.0 ? QStringLiteral("%1 mm").arg(c.lengthMm, 0, 'f', 0) : QString()));
        cables_->setItem(row, 6, new QTableWidgetItem(c.drawing.isEmpty() ? QString() : QFileInfo(c.drawing).fileName()));
        cables_->setItem(row, 7, new QTableWidgetItem(c.drawingRev));
        auto* st = new QTableWidgetItem(status(c));
        const SysLinkState s = SystemDesign::linkState(c);
        st->setForeground(s == SysLinkState::Match ? QColor(47, 138, 74) : s == SysLinkState::None ? QColor(176, 122, 16) : QColor(192, 57, 43));
        cables_->setItem(row, 8, st);
        if (c.id == canvas_->selectedCable()) cables_->selectRow(row);
    }
    filling_ = false;
}

void SystemDesignView::fillIssues() {
    found_->clear();
    for (const SysIssue& i : issues_) {
        auto* it = new QListWidgetItem((i.level == 2 ? QStringLiteral("Error: ") : i.level == 1 ? QStringLiteral("Warning: ") : QStringLiteral("Note: ")) + i.what);
        it->setForeground(i.level == 2 ? QColor(192, 57, 43) : i.level == 1 ? QColor(176, 122, 16) : QColor(90, 98, 110));
        it->setData(Qt::UserRole, i.unit);
        it->setData(Qt::UserRole + 1, i.cable);
        found_->addItem(it);
    }
    tabs_->setTabText(1, issues_.empty() ? QStringLiteral("Check") : QStringLiteral("Check (%1)").arg(issues_.size()));
}

int SystemDesignView::numberCables() {
    aboutToChange();
    const int n = design_.numberCables();
    if (n == 0) history_.pop_back();
    else commit();
    return n;
}

bool SystemDesignView::linkDrawing(const QString& cable, const QString& path, bool ask) {
    SysCable* c = design_.cable(cable);
    if (!c) return false;
    aboutToChange();
    c->drawing = path;
    c->drawingNo.clear();
    c->drawingRev.clear();
    if (!path.isEmpty()) {
        const DrawingInfo info = readDrawingInfo(path);
        c->drawingNo = info.drawingNo;
        c->drawingRev = info.revision;
        // A cable with no part number or revision of its own takes the drawing's.
        if (c->pn.isEmpty()) c->pn = info.drawingNo;
        if (c->rev.isEmpty()) c->rev = info.revision;
        // THE DRAWING NAMES THEM OTHERWISE: the cable, or the connectors at its ends - asked whether to take its names.
        const std::vector<DesignatorChange> changes = design_.designatorChanges(cable, info);
        if (ask && !changes.empty()) {
            QStringList lines;
            for (const DesignatorChange& ch : changes)
                lines << QStringLiteral("  %1 %2 -> %3").arg(ch.cable ? QStringLiteral("cable") : QStringLiteral("connector"), ch.from, ch.to);
            const QString question = QStringLiteral("The drawing %1 names them otherwise:\n\n%2\n\nTake the drawing's designators on the diagram?")
                                         .arg(QFileInfo(path).fileName(), lines.join(QLatin1Char('\n')));
            const bool yes = confirm_ ? confirm_(question)
                                      : QMessageBox::question(this, QStringLiteral("Designators"), question, QMessageBox::Yes | QMessageBox::No,
                                                              QMessageBox::Yes) == QMessageBox::Yes;
            if (yes) design_.applyDesignatorChanges(cable, changes);
        }
    }
    commit();
    return true;
}

void SystemDesignView::linkDrawingFromFile() {
    if (!design_.cable(canvas_->selectedCable())) return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Link a drawing"), QString(),
                                                      QStringLiteral("Drawings (*.dwx *.dxf *.dwg);;All files (*)"));
    if (!path.isEmpty()) linkDrawing(canvas_->selectedCable(), path, true);
}

void SystemDesignView::findDrawing() {
    const SysCable* c = design_.cable(canvas_->selectedCable());
    if (!c) {
        // Nothing to link: the finder opens to look, and to open.
        DrawingFinderDialog dlg(this);
        connect(&dlg, &DrawingFinderDialog::openRequested, this, [this](const QString& path) { emit openRequested(path); });
        dlg.exec();
        return;
    }
    const QString path = DrawingFinderDialog::choose(this, c->pn);
    if (!path.isEmpty()) linkDrawing(c->id, path, true);
}

void SystemDesignView::openDrawing() {
    const SysCable* c = design_.cable(canvas_->selectedCable());
    if (c && !c->drawing.isEmpty()) emit openRequested(c->drawing);
}

int SystemDesignView::checkRevisions() {
    aboutToChange();
    design_.refreshDrawings();
    commit();
    return checkDesign();
}

int SystemDesignView::checkDesign() {
    issues_ = design_.check();
    fillIssues();
    fillCables();
    syncPanel();
    tabs_->setCurrentIndex(1);
    int errors = 0;
    for (const SysIssue& i : issues_) errors += i.level == 2 ? 1 : 0;
    return errors;
}

bool SystemDesignView::exportCableList(const QString& given) {
    QString path = given;
    if (path.isEmpty())
        path = QFileDialog::getSaveFileName(this, QStringLiteral("The cable list"), QStringLiteral("cables.csv"), QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream(&f) << design_.cableListCsv();
    return true;
}

} // namespace Edraw_Desktop
