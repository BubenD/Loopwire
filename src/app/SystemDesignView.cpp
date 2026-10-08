#include "app/SystemDesignView.h"

#include "app/DrawingFinderDialog.h"
#include "system/SystemExport.h"

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
#include <QInputDialog>
#include <QPlainTextEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QSettings>
#include <QTextStream>
#include <QVBoxLayout>
#include <QColorDialog>
#include <QPainter>
#include <QPainterPath>
#include <QSlider>
#include <QToolButton>

#include <algorithm>

namespace Edraw_Desktop {

namespace {

// AN EYE: open - shown on the diagram; shut, a stroke across it - not.
QIcon eyeIcon() {
    QIcon icon;
    for (const bool open : {true, false}) {
        QPixmap pm(36, 36);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QColor ink = open ? QColor(40, 140, 230) : QColor(140, 146, 156);
        p.setPen(QPen(ink, 2.6));
        QPainterPath lid;
        lid.moveTo(4, 18);
        lid.quadTo(18, 4, 32, 18);
        lid.quadTo(18, 32, 4, 18);
        p.drawPath(lid);
        if (open) {
            p.setBrush(ink);
            p.drawEllipse(QPointF(18, 18), 5, 5);
        } else {
            p.drawLine(QPointF(7, 29), QPointF(29, 7));
        }
        p.end();
        icon.addPixmap(pm, QIcon::Normal, open ? QIcon::On : QIcon::Off);
    }
    return icon;
}

const char* const kShown[4] = {"name", "pn", "rev", "desc"};

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
        unitKind_ = new QComboBox(page);
        unitKind_->setObjectName(QStringLiteral("systemUnitKind"));
        unitKind_->addItem(QStringLiteral("Mechanical part"), QStringLiteral("mech"));
        unitKind_->addItem(QStringLiteral("PCBA"), QStringLiteral("pcba"));
        unitKind_->setToolTip(QStringLiteral("What the unit is - a mechanical part or a printed circuit board assembly; its mark "
                                             "is in its corner on the diagram"));
        connect(unitKind_, &QComboBox::currentIndexChanged, this, [this](int i) {
            SysUnit* u = design_.unit(canvas_->selectedUnit());
            if (filling_ || !u || i < 0 || u->kind == unitKind_->itemData(i).toString()) return;
            aboutToChange();
            u->kind = unitKind_->itemData(i).toString();
            commit();
        });
        form->addRow(QStringLiteral("Kind"), unitKind_);
        // EACH FIELD WITH ITS EYE: shown on the unit on the diagram, or not.
        auto withEye = [this, page](QLineEdit* edit, int which, bool cable) {
            auto* row = new QWidget(page);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            h->setSpacing(2);
            h->addWidget(edit, 1);
            auto* eye = new QToolButton(row);
            eye->setObjectName(edit->objectName() + QStringLiteral("Eye"));
            eye->setCheckable(true);
            eye->setChecked(true);
            eye->setAutoRaise(true);
            eye->setIcon(eyeIcon());
            eye->setIconSize(QSize(18, 18));
            eye->setToolTip(QStringLiteral("Shown on the diagram - click to hide it there (it is kept all the same)"));
            h->addWidget(eye);
            (cable ? cableEyes_ : unitEyes_)[which] = eye;
            connect(eye, &QToolButton::toggled, this, [this, which, cable](bool on) {
                if (filling_) return;
                QStringList* hidden = nullptr;
                if (cable) {
                    if (SysCable* c = design_.cable(canvas_->selectedCable())) hidden = &c->hidden;
                } else if (SysUnit* u = design_.unit(canvas_->selectedUnit())) {
                    hidden = &u->hidden;
                }
                const QString key = QString::fromLatin1(kShown[which]);
                if (!hidden || hidden->contains(key) == !on) return;
                aboutToChange();
                if (on) hidden->removeAll(key);
                else hidden->append(key);
                commit();
            });
            return row;
        };
        form->addRow(QStringLiteral("Name"), withEye(unitName_, 0, false));
        form->addRow(QStringLiteral("P/N"), withEye(unitPn_, 1, false));
        form->addRow(QStringLiteral("Rev"), withEye(unitRev_, 2, false));
        form->addRow(QStringLiteral("Description"), withEye(unitDesc_, 3, false));
        // ITS COLOUR, and how solid it is drawn.
        auto* look = new QWidget(page);
        auto* lh = new QHBoxLayout(look);
        lh->setContentsMargins(0, 0, 0, 0);
        unitColour_ = new QToolButton(look);
        unitColour_->setObjectName(QStringLiteral("systemUnitColour"));
        unitColour_->setText(QStringLiteral("Colour..."));
        unitColour_->setToolTip(QStringLiteral("The unit's colour on the diagram"));
        connect(unitColour_, &QToolButton::clicked, this, [this] {
            const SysUnit* u = design_.unit(canvas_->selectedUnit());
            if (!u) return;
            const QColor c = QColorDialog::getColor(u->color.isValid() ? u->color : QColor(232, 238, 246), this, QStringLiteral("Unit colour"));
            if (c.isValid()) setUnitColour(c);
        });
        auto* plain = new QToolButton(look);
        plain->setObjectName(QStringLiteral("systemUnitColourDefault"));
        plain->setText(QStringLiteral("As its kind"));
        plain->setToolTip(QStringLiteral("Back to the colour of its kind - grey-blue for a mechanical part, green for a PCBA"));
        connect(plain, &QToolButton::clicked, this, [this] { setUnitColour(QColor()); });
        unitOpacity_ = new QSlider(Qt::Horizontal, look);
        unitOpacity_->setObjectName(QStringLiteral("systemUnitOpacity"));
        unitOpacity_->setRange(5, 100);
        unitOpacity_->setToolTip(QStringLiteral("How solid the unit is filled - less, and what is behind it shows through"));
        connect(unitOpacity_, &QSlider::valueChanged, this, [this](int v) {
            if (!filling_) setUnitOpacity(v / 100.0);
        });
        lh->addWidget(unitColour_);
        lh->addWidget(plain);
        lh->addWidget(unitOpacity_, 1);
        form->addRow(QStringLiteral("Look"), look);
        v->addLayout(form);
        v->addWidget(new QLabel(QStringLiteral("<b>Ports</b> - name, side, the connector P/N outside and inside, pins"), page));
        ports_ = new QTableWidget(0, 5, page);
        ports_->setObjectName(QStringLiteral("systemPorts"));
        ports_->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Side"), QStringLiteral("Outside P/N"),
                                           QStringLiteral("Inside P/N"), QStringLiteral("Pins")});
        ports_->setToolTip(QStringLiteral("Each port is a connector through the unit's wall: the connector P/N its outside takes (the "
                                          "mating one, as the system calls for it) and its inside's"));
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
            else if (col == 3) p.insidePn = text;
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
        // EACH FIELD WITH ITS EYE: shown on the cable's label, or not.
        auto withEye = [this, page](QLineEdit* edit, int which) {
            auto* row = new QWidget(page);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            h->setSpacing(2);
            h->addWidget(edit, 1);
            auto* eye = new QToolButton(row);
            eye->setObjectName(edit->objectName() + QStringLiteral("Eye"));
            eye->setCheckable(true);
            eye->setAutoRaise(true);
            eye->setIcon(eyeIcon());
            eye->setIconSize(QSize(18, 18));
            eye->setToolTip(QStringLiteral("Shown on the cable's label - click to hide it there, or show it"));
            h->addWidget(eye);
            cableEyes_[which] = eye;
            connect(eye, &QToolButton::toggled, this, [this, which](bool on) {
                SysCable* c = design_.cable(canvas_->selectedCable());
                const QString key = QString::fromLatin1(kShown[which]);
                if (filling_ || !c || c->hidden.contains(key) == !on) return;
                aboutToChange();
                if (on) c->hidden.removeAll(key);
                else c->hidden.append(key);
                commit();
            });
            return row;
        };
        form->addRow(QStringLiteral("Name"), withEye(cableName_, 0));
        form->addRow(QStringLiteral("P/N"), withEye(cablePn_, 1));
        form->addRow(QStringLiteral("Rev"), withEye(cableRev_, 2));
        form->addRow(QStringLiteral("Description"), withEye(cableDesc_, 3));
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
    // A NOTE.
    {
        auto* page = new QWidget(panel_);
        auto* v = new QVBoxLayout(page);
        v->addWidget(new QLabel(QStringLiteral("<b>Note</b> - its text"), page));
        noteText_ = new QPlainTextEdit(page);
        noteText_->setObjectName(QStringLiteral("systemNoteText"));
        v->addWidget(noteText_, 1);
        connect(noteText_, &QPlainTextEdit::textChanged, this, [this] {
            SysNote* n = design_.note(canvas_->selectedNote());
            if (filling_ || !n || n->text == noteText_->toPlainText()) return;
            // ONE STEP TO UNDO for what is typed into one note at a time.
            if (noteTyping_ != n->id) {
                aboutToChange();
                noteTyping_ = n->id;
            }
            n->text = noteText_->toPlainText();
            if (save_) save_(design_.toJson());
            canvas_->update();
            emit changed();
        });
        panel_->addWidget(page);
    }
    connect(canvas_, &SystemCanvas::deleteRequested, this, [this] { deleteSelected(); });
    connect(canvas_, &SystemCanvas::noteEditRequested, this, [this] {
        noteText_->setFocus();
        noteText_->selectAll();
    });
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
    future_.clear();
    issues_.clear();
    canvas_->setDesign(&design_);
    refresh();
    canvas_->fit();
}

void SystemDesignView::setTool(SysTool tool) { canvas_->setTool(tool); }

void SystemDesignView::aboutToChange() {
    history_.push_back(design_.toJson());
    if (history_.size() > 100) history_.erase(history_.begin());
    // Something new done: what Undo took back cannot be put back over it.
    future_.clear();
}

void SystemDesignView::commit() {
    if (save_) save_(design_.empty() ? QJsonObject() : design_.toJson());
    refresh();
    emit changed();
}

void SystemDesignView::redo() {
    if (future_.empty()) return;
    const QJsonObject again = future_.back();
    future_.pop_back();
    history_.push_back(design_.toJson());
    const QString unit = canvas_->selectedUnit(), cable = canvas_->selectedCable();
    noteTyping_.clear();
    design_ = SystemDesign::fromJson(again);
    canvas_->setDesign(&design_);
    canvas_->select(design_.unit(unit) ? unit : QString(), QString(), design_.cable(cable) ? cable : QString());
    if (save_) save_(design_.empty() ? QJsonObject() : design_.toJson());
    refresh();
    emit changed();
}

bool SystemDesignView::deleteSelected() {
    const QString unit = canvas_->selectedUnit(), port = canvas_->selectedPort();
    const bool any = !unit.isEmpty() || !canvas_->selectedCable().isEmpty() || !canvas_->selectedNote().isEmpty();
    if (!any) return false;
    // CONNECTED: a unit with cables (its own, or those of units inside it) or units in it, or a port with cables - asked
    // first.
    QString question;
    if (const SysUnit* u = design_.unit(unit)) {
        if (!port.isEmpty()) {
            const size_t n = design_.cablesAt(unit, port).size();
            const SysPort* p = u->port(port);
            if (n > 0)
                question = QStringLiteral("%1.%2 has %3 cable(s) plugged in - delete the port and its cable(s)?")
                               .arg(u->name, p ? p->name : port)
                               .arg(n);
        } else {
            size_t cables = 0, inside = 0;
            for (const SysCable& c : design_.cables)
                if (c.aUnit == unit || c.bUnit == unit || design_.isWithin(c.aUnit, unit) || design_.isWithin(c.bUnit, unit)) ++cables;
            for (const SysUnit& o : design_.units) inside += design_.isWithin(o.id, unit) ? 1 : 0;
            if (cables > 0 || inside > 0) {
                QStringList what;
                if (cables > 0) what << QStringLiteral("%1 cable(s)").arg(cables);
                if (inside > 0) what << QStringLiteral("%1 unit(s) inside it").arg(inside);
                question = QStringLiteral("%1 is connected - %2. Delete it with them?").arg(u->name, what.join(QStringLiteral(" and ")));
            }
        }
    }
    if (!question.isEmpty()) {
        const bool yes = confirm_ ? confirm_(question)
                                  : QMessageBox::question(this, QStringLiteral("Delete"), question, QMessageBox::Yes | QMessageBox::No,
                                                          QMessageBox::No) == QMessageBox::Yes;
        if (!yes) return false;
    }
    canvas_->deleteSelected();
    canvas_->noteDeleteDone();
    return true;
}

void SystemDesignView::setUnitColour(const QColor& colour) {
    SysUnit* u = design_.unit(canvas_->selectedUnit());
    if (!u || u->color == colour) return;
    aboutToChange();
    u->color = colour;
    commit();
}

void SystemDesignView::setUnitOpacity(double opacity) {
    SysUnit* u = design_.unit(canvas_->selectedUnit());
    opacity = std::clamp(opacity, 0.05, 1.0);
    if (!u || std::abs(u->opacity - opacity) < 1e-9) return;
    // ONE STEP TO UNDO for a slide of it.
    if (opacityDragging_ != u->id) {
        aboutToChange();
        opacityDragging_ = u->id;
    }
    u->opacity = opacity;
    if (save_) save_(design_.toJson());
    canvas_->update();
    emit changed();
}

void SystemDesignView::setNumbering(const QString& prefix, int digits) {
    if (prefix == design_.pnPrefix && digits == design_.pnDigits) return;
    aboutToChange();
    design_.pnPrefix = prefix;
    design_.pnDigits = std::clamp(digits, 1, 9);
    commit();
}

void SystemDesignView::askNumbering() {
    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("systemNumberingDialog"));
    dlg.setWindowTitle(QStringLiteral("Number P/Ns"));
    auto* form = new QFormLayout(&dlg);
    auto* prefix = new QLineEdit(design_.pnPrefix, &dlg);
    prefix->setObjectName(QStringLiteral("systemPnPrefix"));
    auto* digits = new QSpinBox(&dlg);
    digits->setObjectName(QStringLiteral("systemPnDigits"));
    digits->setRange(1, 9);
    digits->setValue(design_.pnDigits);
    auto* example = new QLabel(&dlg);
    auto show = [prefix, digits, example] {
        example->setText(QStringLiteral("The first: %1%2").arg(prefix->text()).arg(1, digits->value(), 10, QLatin1Char('0')));
    };
    connect(prefix, &QLineEdit::textChanged, &dlg, show);
    connect(digits, &QSpinBox::valueChanged, &dlg, show);
    show();
    form->addRow(QStringLiteral("Prefix"), prefix);
    form->addRow(QStringLiteral("Digits"), digits);
    form->addRow(example);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() == QDialog::Accepted) setNumbering(prefix->text(), digits->value());
}

void SystemDesignView::undo() {
    if (history_.empty()) return;
    const QJsonObject back = history_.back();
    history_.pop_back();
    future_.push_back(design_.toJson());
    noteTyping_.clear();
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
    const SysNote* n = design_.note(canvas_->selectedNote());
    if (!n || n->id != noteTyping_) noteTyping_.clear();
    if (n) {
        panel_->setCurrentIndex(3);
        if (noteText_->toPlainText() != n->text) noteText_->setPlainText(n->text);
    } else if (c) {
        panel_->setCurrentIndex(2);
        cableName_->setText(c->name);
        cablePn_->setText(c->pn);
        cableRev_->setText(c->rev);
        cableDesc_->setText(c->description);
        cableLength_->setValue(c->lengthMm);
        cableWires_->setValue(c->wires);
        for (int i = 0; i < 4; ++i)
            if (cableEyes_[i]) cableEyes_[i]->setChecked(c->shows(QString::fromLatin1(kShown[i])));
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
        unitKind_->setCurrentIndex(u->kind == QLatin1String("pcba") ? 1 : 0);
        for (int i = 0; i < 4; ++i)
            if (unitEyes_[i]) unitEyes_[i]->setChecked(u->shows(QString::fromLatin1(kShown[i])));
        unitOpacity_->setValue(static_cast<int>(std::lround(u->opacity * 100.0)));
        if (opacityDragging_ != u->id) opacityDragging_.clear();
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
            ports_->setItem(i, 3, new QTableWidgetItem(p.insidePn));
            ports_->setItem(i, 4, new QTableWidgetItem(p.pins > 0 ? QString::number(p.pins) : QString()));
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

namespace {

// WHERE TO, asked when not given: the place last used for these files, kept.
QString askPath(QWidget* parent, const QString& given, bool save, const QString& title, const QString& name, const QString& filter) {
    if (!given.isEmpty()) return given;
    QSettings settings;
    const QString dir = settings.value(QStringLiteral("system/fileDir")).toString();
    const QString start = dir.isEmpty() ? name : QDir(dir).filePath(name);
    const QString path = save ? QFileDialog::getSaveFileName(parent, title, start, filter) : QFileDialog::getOpenFileName(parent, title, dir, filter);
    if (!path.isEmpty()) settings.setValue(QStringLiteral("system/fileDir"), QFileInfo(path).absolutePath());
    return path;
}

QString baseName(const SystemDesign& d) { return d.name.isEmpty() ? QStringLiteral("system") : d.name; }

} // namespace

bool SystemDesignView::saveDesignAs(const QString& given) {
    const QString path = askPath(this, given, true, QStringLiteral("Save the system design"), baseName(design_) + QStringLiteral(".wetsys"),
                                 QStringLiteral("Edraw system design (*.wetsys)"));
    if (path.isEmpty()) return false;
    QString error;
    if (!saveSystemFile(design_, path, &error)) {
        if (given.isEmpty()) QMessageBox::warning(this, QStringLiteral("System design"), error);
        return false;
    }
    return true;
}

bool SystemDesignView::openDesign(const QString& given) {
    const QString path = askPath(this, given, false, QStringLiteral("Open a system design"), QString(),
                                 QStringLiteral("Edraw system design (*.wetsys *.edsys)"));
    if (path.isEmpty()) return false;
    SystemDesign read;
    QString error;
    if (!loadSystemFile(path, read, &error)) {
        if (given.isEmpty()) QMessageBox::warning(this, QStringLiteral("System design"), error);
        return false;
    }
    aboutToChange();
    design_ = read;
    canvas_->setDesign(&design_);
    commit();
    canvas_->fit();
    return true;
}

bool SystemDesignView::exportDrawio(const QString& given) {
    const QString path = askPath(this, given, true, QStringLiteral("Export the diagram for draw.io"), baseName(design_) + QStringLiteral(".drawio"),
                                 QStringLiteral("draw.io diagram (*.drawio)"));
    if (path.isEmpty()) return false;
    QString error;
    const bool ok = exportSystemDrawio(design_, path, &error);
    if (!ok && given.isEmpty()) QMessageBox::warning(this, QStringLiteral("System design"), error);
    return ok;
}

bool SystemDesignView::exportVisio(const QString& given) {
    const QString path = askPath(this, given, true, QStringLiteral("Export the diagram for Visio"), baseName(design_) + QStringLiteral(".vsdx"),
                                 QStringLiteral("Visio drawing (*.vsdx)"));
    if (path.isEmpty()) return false;
    QString error;
    const bool ok = exportSystemVsdx(design_, path, &error);
    if (!ok && given.isEmpty()) QMessageBox::warning(this, QStringLiteral("System design"), error);
    return ok;
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
