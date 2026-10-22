#include "app/HarnessBuilderDialog.h"
#include "harness/HarnessBuilderBom.h"
#include "harness/PartsLibrary.h"
#include "app/WizardFlowLayout.h"
#include "app/WizardFlowSplitter.h"
#include <QScrollArea>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QStyle>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace Loopwire {
namespace {
const QString uiKey = QStringLiteral("harnessBuilder/ui/v2/");
void clearForm(QWidget* host) {
    if (!host->layout()) new QVBoxLayout(host);
    while (auto* item = host->layout()->takeAt(0)) {
        if (auto* widget = item->widget()) {
            for (auto* child : widget->findChildren<QObject*>())
                if (child->objectName().startsWith(QLatin1String("wizard"))) child->setObjectName(QString());
            if (widget->objectName().startsWith(QLatin1String("wizard"))) widget->setObjectName(QString());
            widget->hide(); widget->deleteLater();
        }
        delete item;
    }
}
}

void HarnessBuilderDialog::initializeWizardUi() {
    QSettings settings;
    if (!wizardUiBuilt_) {
        wizardUiBuilt_ = true;
        wireTable_->setObjectName(QStringLiteral("wizardWireTable"));
        connTable_->setObjectName(QStringLiteral("wizardConnectorTable"));
        auto* tools = new QWidget(this);
        tools->setObjectName(QStringLiteral("wizardTools"));
        auto* bar = new WizardFlowLayout;
        tools->setLayout(bar);
        auto* fields = new QCheckBox(QStringLiteral("More fields"), tools);
        fields->setObjectName(QStringLiteral("wizardMoreFields"));
        fields->setToolTip(QStringLiteral("Show every original connector and wire column"));
        fields->setChecked(!settings.value(uiKey + "compact", true).toBool());
        compactColumns_ = !fields->isChecked();
        grouping_ = WireGrouping::Assembly;
        { QSignalBlocker block(groupByBox_); groupByBox_->setCurrentIndex(groupByBox_->findData(int(grouping_))); }
        reloadWires();
        connect(fields, &QCheckBox::toggled, this, [this](bool on) {
            setCompactColumns(!on); applyStepLayout(currentStep()); fitWizardColumns(); saveWizardUi();
        });
        bar->addWidget(fields);
        auto button = [&](const QString& text, const QString& name, auto callback) {
            auto* b = new QPushButton(text, tools); b->setObjectName(name); b->setAutoDefault(false);
            connect(b, &QPushButton::clicked, this, callback); bar->addWidget(b); return b;
        };
        button(QStringLiteral("Live BOM"), QStringLiteral("wizardLiveBom"), [this] { openLiveBom(); });
        button(QStringLiteral("Fit columns"), QStringLiteral("wizardFitColumns"), [this] { fitWizardColumns(); saveWizardUi(); });
        button(QStringLiteral("Reset layout"), QStringLiteral("wizardResetLayout"), [this, fields] {
            static_cast<WizardFlowSplitter*>(mainSplit_)->releasePanes();
            static_cast<WizardFlowSplitter*>(wireSplit_)->releasePanes();
            QSettings s; s.remove(uiKey); fields->setChecked(false);
            for (auto* section : findChildren<QWidget*>()) {
                if (section->property("wizardSectionFrame").toBool()) { section->setMinimumHeight(0); section->setMaximumHeight(QWIDGETSIZE_MAX); }
                const auto defaultMin = section->property("wizardSectionDefaultMin");
                if (!defaultMin.isValid()) continue;
                section->setMaximumHeight(QWIDGETSIZE_MAX); section->setMinimumHeight(defaultMin.toInt());
            }
            setCompactColumns(true);
            if (auto* display = screen()) {
                const auto area = display->availableGeometry();
                resize(std::min(1240, area.width() - 40), std::min(840, area.height() - 40));
            }
            previewBottom_ = width() < 1120; applyStepLayout(currentStep());
            if (auto* position = findChild<QComboBox*>(QStringLiteral("harnessBuilderPreviewWhere")))
                position->setCurrentIndex(previewBottom_ ? 1 : 0);
            mainSplit_->setSizes({width() * 2 / 3, width() / 3});
            applyStepLayout(currentStep());
            fitWizardColumns(); saveWizardUi();
        });
        static_cast<QVBoxLayout*>(layout())->insertWidget(1, tools);
        if (settings.contains(uiKey + "geometry")) restoreGeometry(settings.value(uiKey + "geometry").toByteArray());
        keepOnScreen();
        for (auto* table : {connTable_, wireTable_}) {
            table->horizontalHeader()->setMinimumSectionSize(24);
            table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
            table->horizontalHeader()->setStretchLastSection(false);
            connect(table->horizontalHeader(), &QHeaderView::sectionResized, this, [this] { if (wizardUiReady_) saveWizardUi(); });
        }
        setCompactColumns(compactColumns_);
        // THE BOM BESIDE THE WORK: the step pages and the picture go into a splitter of their own, so the live BOM can
        // be attached to the left, the right or the bottom of this window as well as kept in a window of its own.
        bomSplit_ = new QSplitter(Qt::Horizontal, this);
        bomSplit_->setObjectName(QStringLiteral("wizardBomSplit"));
        bomSplit_->setChildrenCollapsible(false);
        layout()->replaceWidget(mainSplit_, bomSplit_);
        auto* workScroll = new QScrollArea(bomSplit_);
        workScroll->setObjectName(QStringLiteral("wizardWorkScroll"));
        workScroll->setFrameShape(QFrame::NoFrame); workScroll->setWidgetResizable(true);
        workScroll->setLayoutDirection(Qt::RightToLeft); mainSplit_->setLayoutDirection(Qt::LeftToRight);
        workScroll->setWidget(mainSplit_); bomSplit_->addWidget(workScroll);
        bomDock_ = settings.value(uiKey + "bomDock", QStringLiteral("window")).toString();
        connect(mainSplit_, &QSplitter::splitterMoved, this, [this] { saveWizardUi(); });
        connect(wireSplit_, &QSplitter::splitterMoved, this, [this] { saveWizardUi(); });
        connect(wireTable_, &QTableWidget::cellClicked, this, [this](int row, int) {
            // A click shows that wire in the panel - again, where it is the one already there.
            auto* cell = wireTable_->cellWidget(row, 0);
            const QString id = cell ? cell->property("wireId").toString() : QString();
            if (id.isEmpty()) return;
            if (id == selectedWireId_) refreshWireDetails();
            else selectWire(id);
        });
        refreshWireDetails();
    }
    if (!isVisible() || wizardUiReady_) return;
    QTimer::singleShot(0, this, [this] {
        if (wizardUiReady_) return;
        QSettings s;
        auto* display = screen() ? screen() : QGuiApplication::primaryScreen();
        if (display) {
            const QRect available = display->availableGeometry();
            resize(std::min(width(), available.width()), std::min(height(), available.height()));
            move(std::clamp(x(), available.left(), std::max(available.left(), available.right() - width() + 1)),
                 std::clamp(y(), available.top(), std::max(available.top(), available.bottom() - height() + 1)));
        }
        applyStepLayout(currentStep());
        if (s.contains(uiKey + "split")) mainSplit_->restoreState(s.value(uiKey + "split").toByteArray());
        else mainSplit_->setSizes({width() * 2 / 3, width() / 3});
        if (s.contains(uiKey + "wireSplit")) wireSplit_->restoreState(s.value(uiKey + "wireSplit").toByteArray());
        wireSplit_->setChildrenCollapsible(false);
        wireSplit_->setCollapsible(0, false);
        wireTable_->show();
        if (wireSplit_->sizes().value(0) < 100) wireSplit_->setSizes({180, 220});
        applyStepLayout(currentStep());
        fitWizardColumns();
        if (mainSplit_->orientation() == Qt::Vertical) {
            const auto saved = s.value(uiKey + "flowSplit").value<QList<int>>();
            if (saved.size()==2 && saved[0]+saved[1]<=mainSplit_->height())
                static_cast<WizardFlowSplitter*>(mainSplit_)->restoreFlowHeights(saved);
            else {
                static_cast<WizardFlowSplitter*>(mainSplit_)->releasePanes();
                mainSplit_->setSizes({mainSplit_->height()*6/10,mainSplit_->height()*4/10});
            }
        }
        const auto wireHeights = s.value(uiKey + "flowWires").value<QList<int>>();
        if (!wireHeights.isEmpty()) {
            static_cast<WizardFlowSplitter*>(wireSplit_)->restoreFlowHeights(wireHeights);
            wireSplit_->setProperty("flowUserResized", true);
        }
        if (s.contains(uiKey + "connectors")) connTable_->horizontalHeader()->restoreState(s.value(uiKey + "connectors").toByteArray());
        if (s.contains(uiKey + "wires")) wireTable_->horizontalHeader()->restoreState(s.value(uiKey + "wires").toByteArray());
        keepDeleteColumnLast();
        // Open wide enough for the widest step, not just the initially visible
        // connectors page. Preserve a larger saved window, within the monitor.
        int widest=0;
        for(auto* table:{connTable_,wireTable_}) {
            int needed=56;
            for(int column=0;column<table->columnCount();++column)if(!table->isColumnHidden(column))
                needed+=std::max(80,std::min(220,table->horizontalHeader()->sectionSizeHint(column)));
            widest=std::max(widest,needed);
        }
        for(int step=0;step<pages_->count();++step)
            if(auto* scroll=pages_->widget(step)->findChild<QScrollArea*>();scroll && scroll->widget())
                widest=std::max(widest,scroll->widget()->minimumSizeHint().width()+56);
        if(auto* display=screen()) {
            const int room=display->availableGeometry().width()-32;
            resize(std::min(room,std::max(width(),widest+(previewBottom_?0:340))),height());
            keepOnScreen();
        }
        fitWizardColumns();
        wizardUiReady_ = true;
    });
}

void HarnessBuilderDialog::setCompactColumns(bool compact) {
    compactColumns_ = compact;
    auto hide = [](QTableWidget* table, int column, bool hidden) {
        // Header resize-mode changes can leave a previously hidden section
        // with a nonzero extent. Re-hide it after configuring the header.
        if (hidden && table->isColumnHidden(column)) table->setColumnHidden(column, false);
        table->setColumnHidden(column, hidden);
    };
    // Column 12 of both tables is the row's delete button: always there, whatever else is hidden.
    // Bundle (5) and Internal shield (10) are never shown: what a row is part of is said in the Group column (11), and
    // made with Group as.
    for (int c = 0; c < wireTable_->columnCount(); ++c)
        hide(wireTable_, c, c == 5 || c == 10 ||
                                (compact && c != 0 && c != 1 && c != 2 && c != 3 && c != 7 && c != 11 && c != kDeleteColumn && c != 13));
    for (int c = 0; c < connTable_->columnCount(); ++c)
        hide(connTable_, c, (c >= 8 && c <= 11) ||
            (compact && c != 0 && c != 1 && c != 2 && c != 4 && c != kDeleteColumn && c != 13 && c != 14));
    keepDeleteColumnLast();
    // The rows again, so a merged editor is made for a column only while it is in sight (see mergeGroupCells).
    const bool wasSyncing = syncing_;
    syncing_ = true;
    reloadWires();
    syncing_ = wasSyncing;
}

// THE DELETE BUTTON IS THE LAST COLUMN of each table, narrow and with no heading - moved there on the screen, so every
// other column keeps its number - and is never hidden or stretched.
void HarnessBuilderDialog::keepDeleteColumnLast() {
    for (auto* table : {connTable_, wireTable_}) {
        if (!table || table->columnCount() <= kDeleteColumn) continue;
        QHeaderView* header = table->horizontalHeader();
        const int at = header->visualIndex(kDeleteColumn);
        if (at != table->columnCount() - 1) header->moveSection(at, table->columnCount() - 1);
        table->setColumnHidden(kDeleteColumn, false);
        header->setSectionResizeMode(kDeleteColumn, QHeaderView::Fixed);
        table->setColumnWidth(kDeleteColumn, kDeleteWidth);
    }
}

void HarnessBuilderDialog::fitWizardColumns() {
    const bool wasReady = wizardUiReady_; wizardUiReady_ = false;
    for (auto* table : {connTable_, wireTable_}) {
        table->horizontalHeader()->setStretchLastSection(false);
        const int reserve = table->verticalScrollBar()->isVisible() ? 0 : table->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
        // A hidden step has not been laid out yet; its viewport still has the
        // constructor's tiny size. Fit it to the actual page instead.
        const int available = (table->isVisible() ? table->viewport()->width() - reserve - 4 : pages_->width() - reserve - 32) -
                              kDeleteWidth;
        auto isPin = [table, this](int column) {
            return (table == wireTable_ && (column == 1 || column == 3)) || (table == connTable_ && column == 4);
        };
        int textColumns = 0, minimum = 0;
        for (int c = 0; c < table->columnCount(); ++c) if (!table->isColumnHidden(c) && c != kDeleteColumn) {
            minimum += isPin(c) ? 40 : 80;
            if (!isPin(c)) ++textColumns;
        }
        int extra = std::max(0, available - minimum);
        for (int c = 0; c < table->columnCount(); ++c) if (!table->isColumnHidden(c) && c != kDeleteColumn) {
            int width = 40;
            if (!isPin(c)) {
                const int share = textColumns > 0 ? extra / textColumns : 0;
                width = 80 + share; extra -= share; --textColumns;
            }
            table->setColumnWidth(c, width);
        }
    }
    keepDeleteColumnLast();
    wizardUiReady_ = wasReady;
}

void HarnessBuilderDialog::saveWizardUi() {
    if (!wizardUiReady_) return;
    QSettings s; s.setValue(uiKey + "geometry", saveGeometry());
    s.setValue(uiKey + "windowWidth", width());
    s.setValue(uiKey + "split", mainSplit_->saveState()); s.setValue(uiKey + "wireSplit", wireSplit_->saveState());
    for (auto entry : {std::make_pair(mainSplit_, QStringLiteral("flowSplit")), std::make_pair(wireSplit_, QStringLiteral("flowWires"))}) {
        const auto heights = static_cast<WizardFlowSplitter*>(entry.first)->flowHeights();
        if (!heights.isEmpty()) s.setValue(uiKey + entry.second, QVariant::fromValue(heights));
    }
    s.setValue(uiKey + "connectors", connTable_->horizontalHeader()->saveState());
    s.setValue(uiKey + "wires", wireTable_->horizontalHeader()->saveState());
    s.setValue(uiKey + "compact", compactColumns_);
}
void HarnessBuilderDialog::hideEvent(QHideEvent* event) { saveWizardUi(); QDialog::hideEvent(event); }
void HarnessBuilderDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    if (wizardUiReady_) applyStepLayout(currentStep());
}

void HarnessBuilderDialog::selectWire(const QString& id) {
    if (id.isEmpty() || selectedWireId_ == id) return;
    selectedWireId_ = id;
    for (const auto& w : def_.wires) if (w.id == id) {
        lightPins(w.fromConn, w.fromPin); pinnedPins_ = litPins_; break;
    }
    refreshWireDetails();
}
void HarnessBuilderDialog::highlightWires(const QStringList& ids) {
    QStringList all;
    for (const auto& w : def_.wires) if (ids.contains(w.id)) {
        lightPins(w.fromConn, w.fromPin);
        for (const auto& pin : litPins_) if (!all.contains(pin)) all << pin;
    }
    applyPinHighlights(all); pinnedPins_ = all;
}

void HarnessBuilderDialog::refreshWireDetails() {
    if (!wireDetails_) return;
    clearForm(wireDetails_);
    auto it = std::find_if(def_.wires.begin(), def_.wires.end(), [this](const auto& w) { return w.id == selectedWireId_; });
    if (it == def_.wires.end()) {
        wireDetails_->layout()->addWidget(new QLabel(QStringLiteral("Select a connection to edit its parts and terminations."), wireDetails_));
        return;
    }
    const HarnessWire w = *it; const QString id = w.id;
    // EVERY WIRE PICKED, where this one is among them: a change here goes to all of them - one colour, one gauge, one part
    // for the lot. Otherwise this one alone.
    QStringList targets = pickedWireIds();
    if (!targets.contains(id) || targets.size() < 2) targets = {id};
    auto commit = [this, targets](std::function<void(HarnessWire&)> edit) {
        for (auto& current : def_.wires) if (targets.contains(current.id)) edit(current);
        reloadAll();
    };
    auto* basic = new QGroupBox(targets.size() > 1
                                    ? QStringLiteral("%1 connections picked - a change here goes to all of them").arg(targets.size())
                                    : QStringLiteral("Connection %1%2").arg(id, w.bundle.isEmpty() ? QString() : QStringLiteral(" · ") + w.bundle),
                                wireDetails_);
    basic->setObjectName(QStringLiteral("wizardConnectionBox"));
    // SHORT FIELDS, the same whatever the window's width: a field as wide as what goes in it, not as the window.
    auto* form = new QFormLayout(basic); form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    wireDetails_->layout()->addWidget(basic);
    QHash<QString, QWidget*> createdFields;
    auto text = [&](QFormLayout* f, const QString& caption, const QString& name, const QString& value, auto setter) {
        auto* edit = new QLineEdit(value, wireDetails_); edit->setObjectName(name);
        edit->setProperty("wizardValueField", true); edit->installEventFilter(this);
        createdFields.insert(name, edit);
        connect(edit, &QLineEdit::editingFinished, this, [commit, edit, setter, original = value.trimmed()] {
            const QString value = edit->text().trimmed();
            if (value == original) return;
            commit([setter, value](auto& current) { setter(current, value); });
        });
        f->addRow(caption, edit);
    };
    auto length = [&](QFormLayout* f, const QString& caption, const QString& name, double value, auto setter) {
        auto* spin = new QDoubleSpinBox(wireDetails_); spin->setObjectName(name); spin->setRange(0, 1e9);
        spin->setProperty("wizardValueField", true); spin->installEventFilter(this);
        spin->setDecimals(2); spin->setSuffix(QStringLiteral(" mm")); spin->setSpecialValueText(QStringLiteral("Not specified"));
        spin->setKeyboardTracking(false); spin->setValue(value);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [commit, setter](double value) { commit([setter, value](auto& current) { setter(current, value); }); });
        f->addRow(caption, spin);
    };
    // A GAUGE IN THE APP'S NOTATION - "20 AWG" when the options say AWG, "0.5 mm2" when they say mm2 - kept as mm2.
    // A size not on the list can still be typed.
    auto gauge = [&](QFormLayout* f, const QString& caption, const QString& name, const QString& value, auto setter) {
        auto* box = new QComboBox(wireDetails_);
        box->setObjectName(name);
        box->setEditable(true);
        box->setInsertPolicy(QComboBox::NoInsert);
        for (const QString& label : gaugeChoices()) box->addItem(label, gaugeValueOf(label));
        const int at = box->findData(value);
        if (at >= 0) box->setCurrentIndex(at);
        else box->setEditText(value.isEmpty() ? QString() : gaugeLabelOf(value));
        box->lineEdit()->setProperty("wizardValueField", true);
        box->lineEdit()->installEventFilter(this);
        createdFields.insert(name, box);
        auto apply = [commit, box, setter] {
            const QString text = box->currentText().trimmed();
            const QString mm2 = box->currentIndex() >= 0 && box->itemText(box->currentIndex()) == text
                                    ? box->currentData().toString()
                                    : gaugeValueOf(text);
            commit([setter, mm2](auto& current) { setter(current, mm2); });
        };
        connect(box, &QComboBox::activated, this, apply);
        connect(box->lineEdit(), &QLineEdit::editingFinished, this, apply);
        f->addRow(caption, box);
    };
    auto part = [&](QFormLayout* f, const QString& caption, const QString& name, const QString& current, const std::vector<PartKind>& kinds, auto setter) {
        auto* box = wizardPartChoice(wireDetails_, kinds, current); box->setObjectName(name);
        connect(box, &QComboBox::currentIndexChanged, this, [commit, box, setter](int index) {
            if (index < 0) return; const QString value = box->itemData(index).toString();
            commit([setter, value](auto& current) { setter(current, value); });
        });
        f->addRow(caption, box);
    };
    if (!w.bundle.isEmpty()) {
        auto* conductor = new QComboBox(wireDetails_); conductor->setObjectName(QStringLiteral("wizardConductor"));
        for (const auto& member : def_.wires) if (member.bundle == w.bundle)
            conductor->addItem(QStringLiteral("%1 · %2").arg(member.id, harnessIsCoaxKind(member.kind) ?
                (harnessCoaxShield(member) ? QStringLiteral("Shield") : QStringLiteral("Signal")) : wireColorName(member.color)), member.id);
        conductor->setCurrentIndex(conductor->findData(id));
        connect(conductor, &QComboBox::currentIndexChanged, this, [this, conductor](int index) { selectWire(conductor->itemData(index).toString()); });
        form->addRow(QStringLiteral("Conductor"), conductor);
    }
    auto* kind = new QComboBox(wireDetails_); kind->setObjectName(QStringLiteral("wizardConstruction"));
    kind->addItem(QStringLiteral("Wire"), QStringLiteral("wire"));
    kind->addItem(QStringLiteral("Cable conductor"), QStringLiteral("cable"));
    kind->addItem(QStringLiteral("Coax"), QStringLiteral("coax"));
    kind->addItem(QStringLiteral("Coax with spliced leads"), QStringLiteral("coax-splice"));
    kind->setCurrentIndex(kind->findData(w.kind));
    connect(kind, &QComboBox::currentIndexChanged, this, [commit, kind](int index) { const QString value = kind->itemData(index).toString(); commit([value](auto& current) { current.kind = value; }); });
    form->addRow(QStringLiteral("Construction"), kind);
    part(form, QStringLiteral("Part description from part list"), QStringLiteral("wizardMaterial"), w.libPart,
         harnessIsCoaxKind(w.kind) ? std::vector<PartKind>{PartKind::Cable} : std::vector<PartKind>{PartKind::Wire, PartKind::Cable},
         [this](auto& current, const QString& value) {
             const auto* selected = harnessBuilderLibrary().byId(value);
             int conductor = 0;
             const QString bundle = current.bundle;
             for (auto& member : def_.wires) if (member.id == current.id || (!bundle.isEmpty() && member.bundle == bundle)) {
                 member.libPart = value; member.partNumber.clear();
                 if (selected) applyHarnessWirePart(member, *selected, conductor++);
             }
         });
    const auto* selectedPart = harnessBuilderLibrary().byId(w.libPart);
    text(form, QStringLiteral("Part P/N"), QStringLiteral("wizardMaterialPn"),
         selectedPart ? (selectedPart->mfrPn.isEmpty() ? selectedPart->internalPn : selectedPart->mfrPn) : w.partNumber,
         [this](auto& current, const QString& value) { current.partNumber = value; current.libPart.clear(); for (auto& member : def_.wires) if (!current.bundle.isEmpty() && member.bundle == current.bundle) { member.partNumber = value; member.libPart.clear(); } });
    length(form, QStringLiteral("Cut length (cable / wire)"), QStringLiteral("wizardCutLength"), w.cutLengthMm,
         [this](auto& current, double value) { current.cutLengthMm = value; for (auto& member : def_.wires) if (!current.bundle.isEmpty() && member.bundle == current.bundle) member.cutLengthMm = value; });
    gauge(form, QStringLiteral("Cross section"), QStringLiteral("wizardGauge"), w.gauge, [](auto& current, const QString& value) { current.gauge = value; });
    text(form, QStringLiteral("Bundle"), QStringLiteral("wizardBundle"), w.bundle, [](auto& current, const QString& value) { current.bundle = value; });
    text(form, QStringLiteral("Label"), QStringLiteral("wizardLabel"), w.label, [](auto& current, const QString& value) { current.label = value; });
    auto* colors = new QWidget(wireDetails_); auto* colorRow = new QHBoxLayout(colors); colorRow->setContentsMargins(0, 0, 0, 0);
    auto* base = new QComboBox(colors); auto* stripe = new QComboBox(colors);
    base->setObjectName(QStringLiteral("wizardBaseColor")); stripe->setObjectName(QStringLiteral("wizardStripeColor"));
    stripe->addItem(QStringLiteral("No stripe"), QString());
    for (const QString& code : wireColorCodes()) if (!code.contains(QLatin1Char('-'))) {
        base->addItem(QStringLiteral("%1 · %2").arg(code, wireColorName(code)), code);
        if (code != QLatin1String("SIGNAL") && code != QLatin1String("SHIELD") && code != QLatin1String("DRAIN"))
            stripe->addItem(QStringLiteral("%1 · %2").arg(code, wireColorName(code)), code);
    }
    // BL is accepted as Blue while preserving the established BU library code.
    base->addItem(QStringLiteral("BL · Blue"), QStringLiteral("BL")); stripe->addItem(QStringLiteral("BL · Blue"), QStringLiteral("BL"));
    QString first = w.color.section(QLatin1Char('-'), 0, 0), second = w.color.section(QLatin1Char('-'), 1, 1);
    if (customWireColorCodes().contains(w.color)) { first = w.color; second.clear(); }
    if (base->findData(first) < 0) base->addItem(first, first);
    base->setCurrentIndex(base->findData(first)); stripe->setCurrentIndex(std::max(0, stripe->findData(second)));
    auto changeColor = [commit, base, stripe] {
        const QString value = base->currentData().toString() + (stripe->currentData().toString().isEmpty() ? QString() : QLatin1Char('-') + stripe->currentData().toString());
        commit([value](auto& current) { if (harnessIsCoaxKind(current.kind)) current.coaxRole = harnessCoaxShield(current) ? QStringLiteral("shield") : QStringLiteral("signal"); current.color = value; });
    };
    connect(base, &QComboBox::currentIndexChanged, this, changeColor); connect(stripe, &QComboBox::currentIndexChanged, this, changeColor);
    colorRow->addWidget(base); colorRow->addWidget(stripe); form->addRow(QStringLiteral("Color"), colors);
    text(form, QStringLiteral("Internal shield group"), QStringLiteral("wizardShieldGroup"), w.shieldGroup, [](auto& current, const QString& value) { current.shieldGroup = value; });
    text(form, QStringLiteral("Twisted group"), QStringLiteral("wizardTwistGroup"), w.twistGroup, [](auto& current, const QString& value) { current.twistGroup = value; });
    auto* markings = new QWidget(wireDetails_); auto* markRow = new QHBoxLayout(markings); markRow->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < 3; ++i) {
        auto* tick = new QCheckBox(QStringList{QStringLiteral("Color"), QStringLiteral("Cross section"), QStringLiteral("Label")}[i], markings);
        tick->setChecked(i == 0 ? w.showColor : i == 1 ? w.showGauge : w.showLabel);
        connect(tick, &QCheckBox::toggled, this, [commit, i](bool on) { commit([i, on](auto& current) { if (i == 0) current.showColor = on; else if (i == 1) current.showGauge = on; else current.showLabel = on; }); }); markRow->addWidget(tick);
    }
    form->addRow(QStringLiteral("Print on wire"), markings);
    auto* advanced = new QPushButton(QStringLiteral("Advanced wire settings"), basic);
    advanced->setCheckable(true); advanced->setAutoDefault(false);
    advanced->setObjectName(QStringLiteral("wizardAdvancedSettings"));
    form->addRow(advanced);
    auto setAdvanced = [form, createdFields, markings](bool on) {
        for (const auto& name : {"wizardBundle", "wizardShieldGroup", "wizardTwistGroup"})
            if (auto* field = createdFields.value(QLatin1String(name))) form->setRowVisible(field, on);
        markings->setVisible(true);
    };
    advanced->setChecked(wireDetails_->property("showAdvanced").toBool());
    setAdvanced(advanced->isChecked());
    connect(advanced, &QPushButton::toggled, this, [this, setAdvanced](bool on) {
        wireDetails_->setProperty("showAdvanced", on); setAdvanced(on);
    });
    if (harnessIsCoaxKind(w.kind)) for (int end = 0; end < 2; ++end) {
        const auto t = end == 0 ? w.fromTermination : w.toTermination;
        const QString prefix = end == 0 ? QStringLiteral("wizardFrom") : QStringLiteral("wizardTo");
        auto* group = new QGroupBox(end == 0 ? QStringLiteral("From end") : QStringLiteral("To end"), wireDetails_);
        auto* f = new QFormLayout(group); wireDetails_->layout()->addWidget(group);
        auto* via = new QComboBox(group); via->setObjectName(prefix + "Via");
        via->addItem(QStringLiteral("Direct to pin"), QStringLiteral("direct")); via->addItem(QStringLiteral("Through splice + hook-up wire"), QStringLiteral("wire"));
        via->setCurrentIndex(harnessTailAt(w, end) ? 1 : 0);
        connect(via, &QComboBox::currentIndexChanged, this, [commit, via, end](int index) { const QString value = via->itemData(index).toString(); commit([end, value](auto& current) { (end == 0 ? current.fromTermination : current.toTermination).via = value; }); });
        f->addRow(QStringLiteral("Termination"), via);
        if (!harnessTailAt(w, end)) continue;
        part(f, QStringLiteral("Hook-up wire"), prefix + "WirePart", t.wireLibPart, {PartKind::Wire}, [end](auto& current, const QString& value) {
            auto& termination = end == 0 ? current.fromTermination : current.toTermination;
            termination.wireLibPart = value; termination.wirePn.clear();
            if (const auto* selected = harnessBuilderLibrary().byId(value)) {
                HarnessWire imported; applyHarnessWirePart(imported, *selected);
                termination.wireGauge = imported.gauge; termination.wireColor = imported.color;
            }
        });
        text(f, QStringLiteral("Or wire P/N"), prefix + "WirePn", t.wirePn, [end](auto& current, const QString& value) { auto& t = end == 0 ? current.fromTermination : current.toTermination; t.wirePn = value; t.wireLibPart.clear(); });
        gauge(f, QStringLiteral("Tail gauge"), prefix + "Gauge", t.wireGauge, [end](auto& current, const QString& value) { (end == 0 ? current.fromTermination : current.toTermination).wireGauge = value; });
        text(f, QStringLiteral("Tail colour (e.g. WH-BL)"), prefix + "Color", t.wireColor, [end](auto& current, const QString& value) { (end == 0 ? current.fromTermination : current.toTermination).wireColor = value.toUpper(); });
        length(f, QStringLiteral("Tail cut length"), prefix + "Length", t.lengthMm, [end](auto& current, double value) { (end == 0 ? current.fromTermination : current.toTermination).lengthMm = value; });
        part(f, QStringLiteral("Splice"), prefix + "SplicePart", t.spliceLibPart, {PartKind::Splice}, [end](auto& current, const QString& value) { auto& t = end == 0 ? current.fromTermination : current.toTermination; t.spliceLibPart = value; t.splicePn.clear(); });
        text(f, QStringLiteral("Or splice P/N"), prefix + "SplicePn", t.splicePn, [end](auto& current, const QString& value) { auto& t = end == 0 ? current.fromTermination : current.toTermination; t.splicePn = value; t.spliceLibPart.clear(); });
    }
    for (auto* f : wireDetails_->findChildren<QFormLayout*>()) {
        f->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
        f->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    }
    for (auto* field : wireDetails_->findChildren<QWidget*>()) {
        if (qobject_cast<QLineEdit*>(field) && qobject_cast<QComboBox*>(field->parentWidget())) continue;   // a combo's own editor
        if (qobject_cast<QLineEdit*>(field) || qobject_cast<QDoubleSpinBox*>(field)) field->setFixedWidth(kDetailFieldWidth);
        else if (auto* box = qobject_cast<QComboBox*>(field)) {
            const bool pair = box->objectName() == QLatin1String("wizardBaseColor") || box->objectName() == QLatin1String("wizardStripeColor");
            box->setFixedWidth(pair ? (kDetailFieldWidth - 4) / 2 : kDetailFieldWidth);
        }
    }
    for (auto* group : wireDetails_->findChildren<QGroupBox*>()) group->setMaximumWidth(kDetailFieldWidth + 200);
    wireDetails_->layout()->activate();
    auto* flow = static_cast<WizardFlowSplitter*>(wireSplit_);
    if (!flow->property("flowUserResized").toBool()) {
        const int height = std::max(180, std::max(wireDetails_->sizeHint().height(), wireDetails_->minimumSizeHint().height()));
        flow->restoreFlowHeights({std::max(100, wireTable_->height()), height});
    }
    calmSpinBoxes();
}

void HarnessBuilderDialog::openLiveBom() {
    if (!bomDialog_) {
        bomDialog_ = new QDialog(this); bomDialog_->setWindowTitle(QStringLiteral("Live BOM")); bomDialog_->setModal(false);
        const auto area = screen() ? screen()->availableGeometry() : QRect(0, 0, 1024, 768);
        bomDialog_->resize(std::min(880, area.width() - 40), std::min(440, area.height() - 40));
        new QVBoxLayout(bomDialog_);
        const auto geometry = QSettings().value(uiKey + "bomGeometry").toByteArray();
        if (!geometry.isEmpty()) bomDialog_->restoreGeometry(geometry);
        bomDialog_->installEventFilter(this);
        // THE BOM ITSELF is one panel, put in its own window or attached to a side of this one.
        bomPanel_ = new QWidget(bomDialog_);
        bomPanel_->setObjectName(QStringLiteral("wizardBomPanel"));
        auto* layout = new QVBoxLayout(bomPanel_);
        layout->setContentsMargins(4, 4, 4, 4);
        auto* top = new QHBoxLayout;
        auto* note = new QLabel(QStringLiteral("Physical materials. Enter cut lengths explicitly; drawing distances are not purchasing lengths."), bomPanel_); note->setWordWrap(true);
        top->addStretch(1);
        auto* where = new QComboBox(bomPanel_);
        where->setObjectName(QStringLiteral("wizardBomDock"));
        where->addItem(QStringLiteral("Own window"), QStringLiteral("window"));
        where->addItem(QStringLiteral("Attach left"), QStringLiteral("left"));
        where->addItem(QStringLiteral("Attach right"), QStringLiteral("right"));
        where->addItem(QStringLiteral("Attach bottom"), QStringLiteral("bottom"));
        where->setCurrentIndex(std::max(0, where->findData(bomDock_)));
        where->setToolTip(QStringLiteral("Keep the BOM in a window of its own, or attach it to a side of the wizard"));
        connect(where, &QComboBox::currentIndexChanged, this, [this, where](int i) { placeLiveBom(where->itemData(i).toString()); });
        top->addWidget(where);
        auto* close = new QToolButton(bomPanel_);
        close->setObjectName(QStringLiteral("wizardBomClose"));
        close->setText(QStringLiteral("×"));
        close->setToolTip(QStringLiteral("Close the BOM"));
        connect(close, &QToolButton::clicked, this, [this] {
            if (bomDialog_) bomDialog_->hide();
            if (bomPanel_) bomPanel_->hide();
        });
        top->addWidget(close);
        layout->addLayout(top);
        layout->addWidget(note);
        bomTable_ = new QTableWidget(bomPanel_); bomTable_->setObjectName(QStringLiteral("wizardBomTable"));
        bomTable_->setColumnCount(6); bomTable_->setHorizontalHeaderLabels({QStringLiteral("P/N"), QStringLiteral("Description"), QStringLiteral("Quantity"), QStringLiteral("Unit"), QStringLiteral("Used by"), QStringLiteral("Status")});
        bomTable_->setEditTriggers(QAbstractItemView::NoEditTriggers); bomTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
        bomTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); layout->addWidget(bomTable_);
        connect(bomTable_, &QTableWidget::cellClicked, this, [this](int row, int) {
            if (auto* item = bomTable_->item(row, 0)) {
                selectWire(item->data(Qt::UserRole).toString());
                highlightWires(item->data(Qt::UserRole + 1).toStringList());
                if (!item->data(Qt::UserRole).toString().isEmpty()) setStep(1);
            }
        });
    }
    refreshLiveBom();
    placeLiveBom(bomDock_);
}

// WHERE THE BOM IS: in its own window, or attached to the left, right or bottom of this one - remembered for next time.
void HarnessBuilderDialog::placeLiveBom(const QString& where) {
    if (!bomPanel_ || !bomDialog_ || !bomSplit_) return;
    bomDock_ = where;
    QSettings().setValue(uiKey + "bomDock", where);
    if (where == QLatin1String("window")) {
        bomPanel_->setParent(bomDialog_);
        bomDialog_->layout()->addWidget(bomPanel_);
        bomPanel_->show();
        bomDialog_->show();
        bomDialog_->raise();
        return;
    }
    bomDialog_->hide();
    bomSplit_->setOrientation(where == QLatin1String("bottom") ? Qt::Vertical : Qt::Horizontal);
    bomSplit_->insertWidget(where == QLatin1String("left") ? 0 : 1, bomPanel_);
    bomPanel_->show();
    const int room = where == QLatin1String("bottom") ? bomSplit_->height() : bomSplit_->width();
    const int bom = std::max(160, room / 4);
    bomSplit_->setSizes(where == QLatin1String("left") ? QList<int>{bom, room - bom} : QList<int>{room - bom, bom});
}
void HarnessBuilderDialog::refreshLiveBom() {
    if (!bomTable_) return;
    const auto materials = harnessMaterials(def_, harnessBuilderLibrary());
    bomTable_->setRowCount(int(materials.size()));
    for (int i = 0; i < int(materials.size()); ++i) {
        const auto& line = materials[size_t(i)]; const auto& row = line.row;
        QStringList cells{row.mfrPn.isEmpty() ? row.internalPn : row.mfrPn, row.description,
            line.missingLength ? QStringLiteral("Not set") : row.qtyText(), row.unit, row.ref,
            line.missingPart ? QStringLiteral("Assign part / P/N") : line.missingLength ? QStringLiteral("Set cut length") : QStringLiteral("Ready")};
        for (int c = 0; c < cells.size(); ++c) {
            auto* item = new QTableWidgetItem(cells[c]); item->setToolTip(cells[c]);
            if (line.missingPart || line.missingLength) item->setForeground(QColor("#d98b23"));
            item->setData(Qt::UserRole, line.wireIds.value(0)); bomTable_->setItem(i, c, item);
            item->setData(Qt::UserRole + 1, line.wireIds);
        }
    }
}
}
