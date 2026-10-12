#include "app/BomDialog.h"
#include <QEvent>
#include <QDropEvent>

#include "app/UiBits.h"
#include "app/Editor.h"
#include "model/Document.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>

namespace Loopwire {

BomDialog::BomDialog(Editor* editor, QWidget* parent) : QDialog(parent), editor_(editor) {
    setWindowTitle(QStringLiteral("Bill of materials"));
    resize(1120, 700);
    catalog_.openRemembered();
    lib_ = catalog_.merged(); // every connected library, as one shelf to add from
    buildUi();
    loadTable();
    reload();
}

void BomDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

    // ---- One button for the configuration: the settings live in Options, under BOM ----
    // What the list works in - the units, the spare, the decimals, which columns reach the drawing
    // - belongs with the drawing's other settings rather than in a strip above the table.
    auto* topRow = new QHBoxLayout;
    auto* cfgBtn = new QPushButton(QStringLiteral("Configuration…"), this);
    cfgBtn->setToolTip(QStringLiteral(
        "Options → BOM: the unit lengths are listed in, spare, decimals, whether the same part is "
        "totalled, and which columns the table placed on the drawing carries"));
    connect(cfgBtn, &QPushButton::clicked, this, [this] {
        if (opener_) opener_->openBomConfiguration();
    });
    topRow->addWidget(cfgBtn);
    cfgSummary_ = new QLabel(QString(), this);
    cfgSummary_->setStyleSheet(QStringLiteral("color:#8fa2b8;"));
    topRow->addWidget(cfgSummary_, 1);
    root->addLayout(topRow);

    auto* split = new QSplitter(Qt::Horizontal, this);
    root->addWidget(split, 1);

    // ---- The BOM itself ----
    auto* left = new QWidget(split);
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    view_ = new QTableWidget(left);
    view_->horizontalHeader()->setHighlightSections(false);   // rows lit do not bold the headings
    view_->setColumnCount(bomColumnCount());
    // Dragging a line moves the part up or down the list. The "#" column is the row's PLACE, so it
    // simply follows - a part is never carried about by its number.
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    // A LINE IS DRAGGED BY THE LIST, not only by the little numbers down the side. Dropping BETWEEN two
    // lines is what moving one means - a bill is read in order, and an order that can only be changed by
    // dragging an 18-pixel handle is an order nobody changes.
    view_->setDragEnabled(true);
    view_->setAcceptDrops(true);
    view_->viewport()->setAcceptDrops(true);
    view_->setDropIndicatorShown(true);
    view_->setDragDropMode(QAbstractItemView::InternalMove);
    view_->setDragDropOverwriteMode(false);
    view_->setDefaultDropAction(Qt::MoveAction);
    // AND THE DROP MOVES THE LINE. A table left to itself moves the CELLS that were dragged, which on a bill
    // would carry a description into the row above and leave a hole behind it. The drop is taken here, turned
    // into "this line goes there", and the table is redrawn from the rows - so what is on the screen and what
    // is in the bill cannot disagree.
    view_->viewport()->installEventFilter(this);
    // Right-click a line: take it off. The button is still there, but a list is worked on where the cursor
    // already is, and "remove" is looked for on the thing being removed.
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        if (!view_) return;
        QTableWidgetItem* cell = view_->itemAt(at);
        if (cell) view_->setCurrentCell(cell->row(), cell->column());
        const std::vector<BomRow> shown = shownRows();
        const int r = view_->currentRow();
        QMenu menu(this);
        if (r >= 0 && r < static_cast<int>(shown.size())) {
            const BomRow& row = shown[static_cast<size_t>(r)];
            const QString what = row.description.isEmpty() ? row.ref : row.description;
            if (row.fromDrawing) {
                // Saying why is more use than a greyed command: the line is the drawing's, and the way to
                // take it off is to take the thing off the drawing - or to stop counting the drawing.
                QAction* why = menu.addAction(tr("\"%1\" is what the drawing calls for").arg(what));
                why->setEnabled(false);
            } else {
                menu.addAction(tr("Remove \"%1\"").arg(what), this, [this] { removeCurrentRow(); });
            }
            menu.addSeparator();
        }
        menu.addAction(tr("Add a row to fill in by hand"), this, [this] { addManualRow(); });
        menu.exec(view_->viewport()->mapToGlobal(at));
    });
    view_->verticalHeader()->setSectionsMovable(true);
    view_->verticalHeader()->setVisible(true);
    view_->verticalHeader()->setFixedWidth(18);
    view_->verticalHeader()->setToolTip(QStringLiteral("Drag a line to move it up or down"));
    connect(view_->verticalHeader(), &QHeaderView::sectionMoved, this,
            [this](int, int from, int to) {
                if (syncing_) return;
                // The header has already moved visually; the rows themselves are re-ordered and the
                // table redrawn from them, so the two can never disagree.
                const QSignalBlocker block(view_->verticalHeader());
                view_->verticalHeader()->moveSection(to, from); // undo the visual move
                moveRow(from, to);
            });
    view_->horizontalHeader()->setStretchLastSection(false);
    view_->horizontalHeader()->setSectionsMovable(false);
    // Any heading can be renamed: double-click it, type, Enter. What a company calls a column is
    // what belongs on its sheet.
    view_->horizontalHeader()->setToolTip(
        QStringLiteral("Double-click a heading to rename it"));
    connect(view_->horizontalHeader(), &QHeaderView::sectionDoubleClicked, this, [this](int col) {
        if (col < 0 || col >= bomColumnCount()) return;
        bool ok = false;
        const QString was = table_.header(col);
        const QString now = QInputDialog::getText(this, QStringLiteral("Column heading"),
                                                  QStringLiteral("Heading:"), QLineEdit::Normal,
                                                  was, &ok);
        if (!ok) return;
        table_.setHeader(col, now.trimmed().isEmpty() ? bomDefaultHeaders().at(col)
                                                      : now.trimmed());
        storeTable();
        refreshTable();
    });
    connect(view_, &QTableWidget::cellChanged, this, [this](int row, int col) {
        if (syncing_) return;
        // Only the typed-in rows are editable; a row the drawing put there is read from the
        // drawing, so editing it here would be undone on the next reload.
        std::vector<BomRow> shown = shownRows();
        if (row < 0 || row >= static_cast<int>(shown.size())) return;
        const BomRow& shownRow = shown[static_cast<size_t>(row)];
        if (shownRow.fromDrawing) { refreshTable(); return; }
        // The row the line came from, by its own id - two blank rows are told apart by nothing
        // else.
        for (BomRow& r : table_.rows) {
            if (r.fromDrawing || r.rowId != shownRow.rowId) continue;
            const QString text = view_->item(row, col) ? view_->item(row, col)->text() : QString();
            switch (col) {
                case 1: r.description = text; break;
                case 2: r.internalPn = text; break;
                case 3: r.mfrPn = text; break;
                case 4: r.mfrName = text; break;
                case 5: r.qty = text.toDouble(); break;
                default: break; // line # is the row's place; the unit has its own editor
            }
            break;
        }
        storeTable();
        refreshTable();
    });
    lv->addWidget(view_, 1);

    auto* rowBtns = new QHBoxLayout;
    auto* addBtn = makeAddButton(left, QStringLiteral("Row"));
    addBtn->setToolTip(QStringLiteral("A blank row to fill in by hand"));
    connect(addBtn, &QPushButton::clicked, this, [this] { addManualRow(); });
    rowBtns->addWidget(addBtn);
    auto* delBtn = new QPushButton(QStringLiteral("Remove row"), left);
    connect(delBtn, &QPushButton::clicked, this, [this] { removeCurrentRow(); });
    rowBtns->addWidget(delBtn);
    rowBtns->addStretch(1);
    auto* csvBtn = new QPushButton(QStringLiteral("Export CSV…"), left);
    csvBtn->setToolTip(QStringLiteral(
        "The list as it reads here - the same columns, units and totals - as a CSV file"));
    connect(csvBtn, &QPushButton::clicked, this, [this] {
        QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export bill of materials"),
                                                    QString(), QStringLiteral("CSV (*.csv)"));
        if (path.isEmpty()) return;
        if (!path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive))
            path += QStringLiteral(".csv");
        if (summary_)
            summary_->setText(exportCsv(path) ? QStringLiteral("Exported to %1").arg(path)
                                              : QStringLiteral("Could not write %1").arg(path));
    });
    rowBtns->addWidget(csvBtn);
    auto* placeBtn = new QPushButton(QStringLiteral("Place table on drawing"), left);
    placeBtn->setToolTip(QStringLiteral(
        "The bill of materials as a table on the sheet - then click where it goes"));
    connect(placeBtn, &QPushButton::clicked, this, [this] { placeOnDrawing(); });
    rowBtns->addWidget(placeBtn);
    lv->addLayout(rowBtns);
    split->addWidget(left);

    // ---- The library, to add from ----
    auto* right = new QGroupBox(QStringLiteral("Add from the parts library"), split);
    auto* rv = new QVBoxLayout(right);
    libFilter_ = new QLineEdit(right);
    libFilter_->setPlaceholderText(QStringLiteral("Search description, P/N or manufacturer"));
    connect(libFilter_, &QLineEdit::textChanged, this, [this] { refreshLibraryList(); });
    rv->addWidget(libFilter_);
    libView_ = new QTableWidget(right);
    libView_->setColumnCount(4);
    libView_->setHorizontalHeaderLabels({QStringLiteral("Type"), QStringLiteral("Description"),
                                         QStringLiteral("Mfr P/N"), QStringLiteral("Unit")});
    libView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    libView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    libView_->horizontalHeader()->setStretchLastSection(true);
    rv->addWidget(libView_, 1);
    auto* addRow = new QHBoxLayout;
    addRow->addWidget(new QLabel(QStringLiteral("Qty"), right));
    auto* qty = new QDoubleSpinBox(right);
    qty->setRange(0.0, 1e6);
    qty->setDecimals(2);
    qty->setValue(1.0);
    addRow->addWidget(qty);
    auto* addLib = new QPushButton(QStringLiteral("Add to BOM"), right);
    connect(addLib, &QPushButton::clicked, this, [this, qty] {
        const int r = libView_->currentRow();
        if (r < 0) return;
        const QString id = libView_->item(r, 0)->data(Qt::UserRole).toString();
        addFromLibrary(id, qty->value());
    });
    addRow->addWidget(addLib);
    addRow->addStretch(1);
    rv->addLayout(addRow);
    split->addWidget(right);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 1);

    auto* foot = new QHBoxLayout;
    summary_ = new QLabel(QString(), this);
    summary_->setStyleSheet(QStringLiteral("color:#8fb0d6;"));
    foot->addWidget(summary_, 1);
    // SAVE, CANCEL, CLOSE - and each of them one thing. Every edit here writes into the drawing as it is
    // made (which is what keeps the side panel in step), so what was missing was the way back: Cancel puts
    // the bill back as it was when this window opened. Save keeps what is written from here on, so a Cancel
    // after it undoes only what came after.
    auto* saveBtn = new QPushButton(QStringLiteral("Save"), this);
    saveBtn->setToolTip(QStringLiteral("Write the bill into the drawing and keep it - Cancel after this "
                                       "undoes only what comes next"));
    connect(saveBtn, &QPushButton::clicked, this, [this] { saveNow(); });
    foot->addWidget(saveBtn);
    auto* cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    cancelBtn->setToolTip(QStringLiteral("Put the bill back as it was when this window opened, and close"));
    connect(cancelBtn, &QPushButton::clicked, this, [this] { cancelChanges(); });
    foot->addWidget(cancelBtn);
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    closeBtn->setToolTip(QStringLiteral("Leave what is written as it is"));
    connect(closeBtn, &QPushButton::clicked, this, [this] {
        storeTable();
        accept();
    });
    foot->addWidget(closeBtn);
    root->addLayout(foot);

    refreshLibraryList();
}

void BomDialog::loadTable() {
    if (!editor_ || !editor_->document()) return;

    table_.fromJson(editor_->document()->bom());
    unify_ = table_.config.unify;
    loadControlsFromConfig();
}

// The controls show what the drawing was set up with.
void BomDialog::loadControlsFromConfig() {
    syncing_ = true;
    if (lengthUnitBox_) {
        const int at = lengthUnitBox_->findData(table_.config.lengthUnit);
        if (at >= 0) lengthUnitBox_->setCurrentIndex(at);
    }
    if (spareSpin_) spareSpin_->setValue(table_.config.sparePercent);
    if (decimalsSpin_) decimalsSpin_->setValue(table_.config.qtyDecimals);
    if (roundUpBox_) roundUpBox_->setChecked(table_.config.roundUpLengths);
    if (unifyBox_) unifyBox_->setChecked(table_.config.unify);
    if (fromDrawingBox_) fromDrawingBox_->setChecked(table_.config.includeDrawing);
    for (int i = 0; i < columnBoxes_.size(); ++i) {
        columnBoxes_[i]->setText(table_.header(i));
        columnBoxes_[i]->setChecked(table_.config.columnShown(i));
    }
    syncing_ = false;
}

int BomDialog::shownRowCount() const { return static_cast<int>(shownRows().size()); }

void BomDialog::storeTable() {
    if (!editor_ || !editor_->document()) return;
    editor_->document()->bom() = table_.toJson();
    editor_->document()->setDirty(true);
    // AND WHOEVER ELSE IS SHOWING THIS BILL. The side panel shows the same list: a line added here appeared
    // there only when something else happened to refresh it, so the two disagreed until they were both
    // reopened - and which of them was right was anybody's guess.
    emit bomChanged();
}

// The bill as it stands, kept: from here on Cancel undoes what comes next and not what has been saved.
void BomDialog::saveNow() {
    storeTable();
    openedWith_ = table_.toJson();
    if (summary_) summary_->setText(QStringLiteral("Saved to the drawing"));
}

// EVERYTHING BACK AS IT WAS. Every edit in this window writes straight into the drawing, which is what keeps
// the side panel in step - and it left no way out of a change made by accident: a unit picked from a menu on
// the wrong row was the drawing's answer from then on. The bill as it was when this window opened is kept, and
// this puts it back.
void BomDialog::cancelChanges() {
    if (editor_ && editor_->document()) {
        editor_->document()->bom() = openedWith_;
        editor_->document()->setDirty(true);
        table_.fromJson(openedWith_);
        emit bomChanged();
    }
    reject();
}

int BomDialog::highlightParts(const QStringList& partIds) {
    if (!view_) return 0;
    view_->clearSelection();
    if (partIds.isEmpty()) return 0;
    const std::vector<BomRow> rows = shownRows();
    int picked = 0;
    for (int row = 0; row < view_->rowCount() && row < static_cast<int>(rows.size()); ++row) {
        if (!partIds.contains(rows[static_cast<size_t>(row)].partId)) continue;
        view_->selectionModel()->select(view_->model()->index(row, 0),
                                        QItemSelectionModel::Select | QItemSelectionModel::Rows);
        if (picked++ == 0) view_->scrollTo(view_->model()->index(row, 0));
    }
    return picked;
}

std::vector<BomRow> BomDialog::shownRows() const {
    std::vector<BomRow> rows;
    if (table_.config.includeDrawing && editor_ && editor_->document())
        rows = bomFromDrawing(*editor_->document(), lib_);
    for (const BomRow& r : table_.rows)
        if (!r.fromDrawing) rows.push_back(r);
    // The working unit, the spare and the rounding first, so the lines total in the unit they are
    // shown in rather than being converted after the sums.
    rows = bomApplyConfig(rows, table_.config);
    return unify_ ? bomUnify(rows) : rows;
}

void BomDialog::reload() {
    // The libraries may have changed since this opened - a part added, another connected.
    catalog_.openRemembered();
    lib_ = catalog_.merged();
    // AND THE BILL ITSELF, out of the drawing. It re-read the LIBRARIES and redrew the table from its own
    // copy of the bill - so a line added from the side panel, which writes into the drawing, never reached
    // this window at all: the same list, open in two places, showing two different things.
    loadTable();
    refreshLibraryList();
    refreshTable();
}

// The bill as the drawing has it now, kept as the thing Cancel puts back. Said when the window is OPENED
// rather than on every reload: a reload happens whenever anything writes, and snapshotting then would mean
// Cancel had nothing left to undo.
void BomDialog::markOpened() {
    if (editor_ && editor_->document()) openedWith_ = editor_->document()->bom();
}

void BomDialog::setConfig(const BomConfig& cfg) {
    table_.config = cfg;
    unify_ = cfg.unify;
    storeTable();
    loadControlsFromConfig();
    refreshTable();
}

void BomDialog::setUnifyTotals(bool on) {
    unify_ = on;
    table_.config.unify = on;
    storeTable();
    refreshTable();
}

void BomDialog::refreshTable() {
    if (!view_) return;
    // THE COLUMNS THE CONFIGURATION ASKS FOR, in this window as well as on the sheet. A column switched off
    // in Options was still shown here, so the list being worked on and the list that would be printed were
    // two different lists, and the only way to see what the drawing would say was to place the table and
    // look at it. A column nobody wants is a column nobody should have to read past.
    for (int i = 0; i < view_->columnCount(); ++i)
        view_->setColumnHidden(i, !table_.config.columnShown(i));
    const std::vector<BomRow> rows = shownRows();
    syncing_ = true;
    QStringList heads;
    for (int i = 0; i < bomColumnCount(); ++i) heads << table_.header(i);
    view_->setHorizontalHeaderLabels(heads);
    view_->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
        const BomRow& row = rows[static_cast<size_t>(r)];
        auto cell = [&](int col, const QString& text, bool editable) {
            auto* it = new QTableWidgetItem(text);
            if (!editable) it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            // A row the drawing put there is shown greyed, so it is clear which rows are typed.
            if (row.fromDrawing) it->setForeground(QColor("#9aa6b4"));
            view_->setItem(r, col, it);
        };
        const bool typed = !row.fromDrawing;
        cell(0, QString::number(r + 1), false);
        cell(1, row.description, typed);
        cell(2, row.internalPn, typed);
        cell(3, row.mfrPn, typed);
        cell(4, row.mfrName, typed);
        cell(5, row.qtyText(table_.config.qtyDecimals), typed);
        // The unit is a choice, not free text: what can be totalled with what depends on it.
        if (typed) {
            auto* box = new QComboBox(view_);
            box->addItems(partUnitCodes());
            if (box->findText(row.unit) < 0) box->addItem(row.unit);
            box->setCurrentText(row.unit);
            const QString rowId = row.rowId;
            connect(box, &QComboBox::currentTextChanged, this,
                    [this, rowId](const QString& u) {
                        if (syncing_) return;
                        for (BomRow& tr : table_.rows)
                            if (!tr.fromDrawing && tr.rowId == rowId) {
                                // THE SAME QUANTITY, SAID ANOTHER WAY. 2 m IS 2000 mm: changing the unit on
                                // a line used to relabel it and leave the figure alone, so a line that said
                                // "2 m" became "2 mm" - the program saying something untrue about how much to
                                // buy, in the one place somebody orders from.
                                //
                                // Between a length and a count there is nothing to convert (a hundred
                                // grommets are not any number of metres), so the figure stands.
                                const double was = tr.qty;
                                tr.qty = convertQuantity(tr.qty, tr.unit, u);
                                if (std::abs(tr.qty - was) > 1e-12 && summary_)
                                    summary_->setText(QStringLiteral("%1 %2 is %3 %4")
                                                          .arg(was, 0, 'g', 6)
                                                          .arg(tr.unit)
                                                          .arg(tr.qty, 0, 'g', 6)
                                                          .arg(u));
                                tr.unit = u;
                                break;
                            }
                        storeTable();
                        refreshTable();
                    });
            view_->setCellWidget(r, 6, box);
        } else {
            view_->removeCellWidget(r, 6);
            cell(6, row.unit, false);
        }
    }
    view_->resizeColumnsToContents();
    syncing_ = false;
    if (cfgSummary_)
        cfgSummary_->setText(
            QStringLiteral("lengths in %1%2%3 · %4 · %5 of %6 columns shown")
                .arg(table_.config.lengthUnit,
                     table_.config.sparePercent > 0.0
                         ? QStringLiteral(" +%1%% spare").arg(table_.config.sparePercent, 0, 'f', 0)
                         : QString(),
                     table_.config.roundUpLengths ? QStringLiteral(", rounded up") : QString(),
                     table_.config.unify ? QStringLiteral("totalled by part")
                                         : QStringLiteral("one line per entry"))
                .arg(shownColumnCount())
                .arg(bomColumnCount()));
    if (summary_) {
        int drawn = 0;
        for (const BomRow& row : rows)
            if (row.fromDrawing) ++drawn;
        summary_->setText(QStringLiteral("%1 line(s) — %2 from the drawing, %3 entered")
                              .arg(rows.size())
                              .arg(drawn)
                              .arg(rows.size() - static_cast<size_t>(drawn)));
    }
}

int BomDialog::shownColumnCount() const {
    int n = 0;
    for (int i = 0; i < bomColumnCount(); ++i)
        if (table_.config.columnShown(i)) ++n;
    return n;
}

void BomDialog::refreshLibraryList() {
    if (!libView_) return;
    const QString needle = libFilter_ ? libFilter_->text().trimmed() : QString();
    std::vector<const LibraryPart*> shown;
    for (const LibraryPart& p : lib_.parts) {
        if (!needle.isEmpty() && !p.description.contains(needle, Qt::CaseInsensitive) &&
            !p.mfrPn.contains(needle, Qt::CaseInsensitive) &&
            !p.internalPn.contains(needle, Qt::CaseInsensitive) &&
            !p.mfrName.contains(needle, Qt::CaseInsensitive) &&
            !partKindName(p.kind).contains(needle, Qt::CaseInsensitive))
            continue;
        shown.push_back(&p);
    }
    libView_->setRowCount(static_cast<int>(shown.size()));
    for (int r = 0; r < static_cast<int>(shown.size()); ++r) {
        const LibraryPart* p = shown[static_cast<size_t>(r)];
        auto* kind = new QTableWidgetItem(partKindName(p->kind));
        kind->setData(Qt::UserRole, p->id);
        libView_->setItem(r, 0, kind);
        libView_->setItem(r, 1, new QTableWidgetItem(p->description));
        libView_->setItem(r, 2, new QTableWidgetItem(p->mfrPn));
        libView_->setItem(r, 3, new QTableWidgetItem(p->unit));
    }
    libView_->resizeColumnsToContents();
}

bool BomDialog::exportCsv(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    // A field is quoted when it holds a comma, a quote or a line break; a quote inside is doubled -
    // the ordinary CSV rules, so a spreadsheet reads it back as written.
    auto field = [](const QString& v) {
        if (!v.contains(QLatin1Char(',')) && !v.contains(QLatin1Char('"')) &&
            !v.contains(QLatin1Char('\n')))
            return v;
        QString q = v;
        q.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QStringLiteral("\"%1\"").arg(q);
    };
    // The same columns the drawing gets, so the file and the sheet say the same thing.
    std::vector<int> use;
    for (int i = 0; i < bomColumnCount(); ++i)
        if (table_.config.columnShown(i)) use.push_back(i);
    if (use.empty())
        for (int i = 0; i < bomColumnCount(); ++i) use.push_back(i);
    QStringList head;
    for (int i : use) head << field(table_.header(i));
    out << head.join(QLatin1Char(',')) << "\r\n";
    int line = 0;
    for (const BomRow& r : shownRows()) {
        ++line;
        const QString byColumn[7] = {QString::number(line), r.description, r.internalPn, r.mfrPn,
                                    r.mfrName, r.qtyText(table_.config.qtyDecimals), r.unit};
        QStringList cells;
        for (int i : use) cells << field(byColumn[i]);
        out << cells.join(QLatin1Char(',')) << "\r\n";
    }
    return true;
}

bool BomDialog::eventFilter(QObject* watched, QEvent* e) {
    if (view_ && watched == view_->viewport() && e->type() == QEvent::Drop) {
        auto* de = static_cast<QDropEvent*>(e);
        const int from = view_->currentRow();
        // WHERE IT LANDS, including between two lines: the row under the cursor if the cursor is in its top
        // half, the one after it if in its bottom half. Below the last line means the end of the list.
        const QPoint at = de->position().toPoint();
        int to = view_->rowAt(at.y());
        if (to < 0) {
            to = view_->rowCount() - 1;
        } else {
            const QRect r = view_->visualItemRect(view_->item(to, 0));
            if (r.isValid() && at.y() > r.center().y() && to < view_->rowCount() - 1) ++to;
        }
        de->setDropAction(Qt::IgnoreAction);
        de->accept();
        if (from >= 0 && to >= 0 && from != to) moveRow(from, to);
        return true;
    }
    return QDialog::eventFilter(watched, e);
}

void BomDialog::moveRow(int from, int to) {
    // The lines shown are the drawing's rows followed by the typed ones; only the typed ones have
    // an order of their own to change.
    const std::vector<BomRow> shown = shownRows();
    if (from < 0 || to < 0 || from >= static_cast<int>(shown.size()) ||
        to >= static_cast<int>(shown.size()) || from == to)
        return;
    const QString moveId = shown[static_cast<size_t>(from)].rowId;
    const QString beforeId = shown[static_cast<size_t>(to)].rowId;
    if (moveId.isEmpty() || shown[static_cast<size_t>(from)].fromDrawing) {
        if (summary_)
            summary_->setText(QStringLiteral(
                "That line comes from the drawing — its place follows the drawing"));
        return;
    }
    int fromIdx = -1, toIdx = -1;
    for (size_t i = 0; i < table_.rows.size(); ++i) {
        if (table_.rows[i].rowId == moveId) fromIdx = static_cast<int>(i);
        if (table_.rows[i].rowId == beforeId) toIdx = static_cast<int>(i);
    }
    if (fromIdx < 0) return;
    BomRow moved = table_.rows[static_cast<size_t>(fromIdx)];
    table_.rows.erase(table_.rows.begin() + fromIdx);
    // The line lands AT the place it was dropped on: the header reports where the section now sits,
    // so dragging to the bottom means the bottom, not "before the last one".
    if (toIdx < 0 || toIdx > static_cast<int>(table_.rows.size()))
        toIdx = static_cast<int>(table_.rows.size());
    table_.rows.insert(table_.rows.begin() + toIdx, moved);
    storeTable();
    refreshTable();
    if (summary_) summary_->setText(QStringLiteral("Line moved"));
}

void BomDialog::addManualRow() {
    BomRow r;
    r.rowId = bomNextRowId(table_.rows); // its own row, however many blank ones there are
    r.qty = 1.0;
    r.unit = QStringLiteral("each");
    r.fromDrawing = false;
    table_.rows.push_back(r);
    storeTable();
    refreshTable();
    if (summary_)
        summary_->setText(QStringLiteral("Row %1 added — fill in what it is")
                              .arg(table_.rows.size()));
}

void BomDialog::addFromLibrary(const QString& partId, double qty) {
    const LibraryPart* p = lib_.byId(partId);
    if (!p) return;
    BomRow r = bomRowFromPart(*p, qty);
    r.rowId = bomNextRowId(table_.rows);
    r.fromDrawing = false;
    table_.rows.push_back(r);
    storeTable();
    refreshTable();
    if (summary_)
        summary_->setText(QStringLiteral("Added %1 %2 of %3")
                              .arg(r.qtyText(), r.unit, r.description));
}

void BomDialog::removeCurrentRow() {
    if (!view_) return;
    const int r = view_->currentRow();
    const std::vector<BomRow> shown = shownRows();
    if (r < 0 || r >= static_cast<int>(shown.size())) return;
    const BomRow& row = shown[static_cast<size_t>(r)];
    if (row.fromDrawing) {
        if (summary_)
            summary_->setText(QStringLiteral(
                "That line comes from the drawing — remove the object, or switch the drawing rows "
                "off"));
        return;
    }
    for (size_t i = 0; i < table_.rows.size(); ++i)
        if (!table_.rows[i].fromDrawing && table_.rows[i].rowId == row.rowId) {
            table_.rows.erase(table_.rows.begin() + static_cast<long>(i));
            break;
        }
    storeTable();
    refreshTable();
}

void BomDialog::placeOnDrawing() {
    if (!editor_) return;
    const std::vector<BomRow> rows = shownRows();
    if (rows.empty()) {
        if (summary_) summary_->setText(QStringLiteral("Nothing to place"));
        return;
    }
    // A table entity: a heading row plus one row per line, filled with what is shown here. It is
    // placed by a click, like anything else that is inserted.
    // Only the columns the configuration asks for go onto the sheet: a shop-floor list may want
    // nothing but the line, what it is and how many.
    std::vector<int> use;
    for (int i = 0; i < bomColumnCount(); ++i)
        if (table_.config.columnShown(i)) use.push_back(i);
    if (use.empty()) {
        if (summary_) summary_->setText(QStringLiteral("No columns are switched on for the drawing"));
        return;
    }
    const int cols = static_cast<int>(use.size());
    const int nRows = static_cast<int>(rows.size()) + 1;
    const double rowH = 8.0, textH = rowH * 0.5;
    QJsonArray cells;
    for (int i : use) cells.append(table_.header(i));
    int line = 0;
    for (const BomRow& r : rows) {
        ++line;
        const QString byColumn[7] = {QString::number(line), r.description, r.internalPn, r.mfrPn,
                                    r.mfrName, r.qtyText(table_.config.qtyDecimals), r.unit};
        for (int i : use) cells.append(byColumn[i]);
    }
    // Each column is as wide as the widest thing in it - a description needs room, a line number
    // does not - down to a floor so a short column is still a column. Anything that fits in the
    // ordinary width keeps it; only a column whose text exceeds it grows.
    const double pad = textH * 1.6, minW = 14.0, plainW = 30.0;
    std::vector<double> colW(static_cast<size_t>(cols), minW);
    for (int i = 0; i < cells.size(); ++i) {
        const int c = i % cols;
        // The same width estimate the wire markings use: a proportional font, judged by the
        // character count, which is close enough to keep text off the cell edges.
        const double need = pad + 0.62 * textH * cells[i].toString().size();
        colW[static_cast<size_t>(c)] = std::max(colW[static_cast<size_t>(c)], need);
    }
    double W = 0.0;
    for (int c = 0; c < cols; ++c) {
        colW[static_cast<size_t>(c)] =
            std::max(colW[static_cast<size_t>(c)], use[static_cast<size_t>(c)] == 1 ? plainW : minW);
        W += colW[static_cast<size_t>(c)];
    }
    const double H = nRows * rowH;
    Entity t;
    t.type = EntityType::Rect;
    t.layer = editor_->document() ? editor_->document()->activeLayer() : QStringLiteral("0");
    t.points = {{0, -H}, {W, -H}, {W, 0}, {0, 0}};
    t.a = {0, 0};
    QJsonArray colFrac, rowFrac;
    for (int c = 0; c < cols; ++c) colFrac.append(colW[static_cast<size_t>(c)] / W);
    for (int i = 0; i < nRows; ++i) rowFrac.append(1.0 / nRows);
    // Every cell reads middle-centre - a number, a part number and a unit all sit best that way -
    // except the description, which is prose and so reads middle-left.
    QJsonArray cellAlign;
    for (int i = 0; i < cells.size(); ++i) {
        // The description reads middle-left wherever it ends up; everything else middle-centre.
        const bool isDescription = use[static_cast<size_t>(i % cols)] == 1;
        cellAlign.append(isDescription ? QStringLiteral("ML") : QStringLiteral("MC"));
    }
    t.meta.insert(QStringLiteral("table"), true);
    t.meta.insert(QStringLiteral("rows"), nRows);
    t.meta.insert(QStringLiteral("cols"), cols);
    t.meta.insert(QStringLiteral("colFrac"), colFrac);
    t.meta.insert(QStringLiteral("rowFrac"), rowFrac);
    t.meta.insert(QStringLiteral("cells"), cells);
    t.meta.insert(QStringLiteral("cellAlign"), cellAlign);
    t.meta.insert(QStringLiteral("headerRow"), true);
    t.meta.insert(QStringLiteral("textSize"), textH);
    t.meta.insert(QStringLiteral("bomTable"), true); // so it can be found and refreshed later
    editor_->placeEntities({t});
    if (summary_) summary_->setText(QStringLiteral("Click the sheet to place the table"));
    hide();
}

} // namespace Loopwire
