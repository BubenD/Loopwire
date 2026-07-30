#include "app/BomDialog.h"

#include "app/Editor.h"
#include "model/Document.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace Edraw_Desktop {

BomDialog::BomDialog(Editor* editor, QWidget* parent) : QDialog(parent), editor_(editor) {
    setWindowTitle(QStringLiteral("Bill of materials"));
    resize(1120, 700);
    lib_.load(PartsLibrary::defaultPath());
    buildUi();
    loadTable();
    reload();
}

void BomDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

    auto* opts = new QHBoxLayout;
    fromDrawingBox_ = new QCheckBox(QStringLiteral("Include what the drawing calls for"), this);
    fromDrawingBox_->setChecked(true);
    fromDrawingBox_->setToolTip(QStringLiteral(
        "Every connector and wire in the drawing, as rows. They are read off the drawing each "
        "time, so they cannot go stale."));
    connect(fromDrawingBox_, &QCheckBox::toggled, this, [this](bool) { reload(); });
    opts->addWidget(fromDrawingBox_);
    unifyBox_ = new QCheckBox(QStringLiteral("Total the same part into one line"), this);
    unifyBox_->setChecked(true);
    unifyBox_->setToolTip(QStringLiteral(
        "Rows naming the same part are added up in the unit of the first of them - 300 mm of wire "
        "and 1.2 m of wire come out as one line. Quantities in units that cannot be added (metres "
        "and pieces) are left as their own lines."));
    connect(unifyBox_, &QCheckBox::toggled, this, [this](bool on) { setUnifyTotals(on); });
    opts->addWidget(unifyBox_);
    opts->addStretch(1);
    root->addLayout(opts);

    auto* split = new QSplitter(Qt::Horizontal, this);
    root->addWidget(split, 1);

    // ---- The BOM itself ----
    auto* left = new QWidget(split);
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    view_ = new QTableWidget(left);
    view_->setColumnCount(bomColumnCount());
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
        // Find that row in the table's own rows and write the cell into it.
        for (BomRow& r : table_.rows) {
            if (r.fromDrawing) continue;
            if (r.description != shownRow.description || r.mfrPn != shownRow.mfrPn ||
                r.internalPn != shownRow.internalPn)
                continue;
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
    auto* addBtn = new QPushButton(QStringLiteral("+ Row"), left);
    addBtn->setToolTip(QStringLiteral("A blank row to fill in by hand"));
    connect(addBtn, &QPushButton::clicked, this, [this] { addManualRow(); });
    rowBtns->addWidget(addBtn);
    auto* delBtn = new QPushButton(QStringLiteral("Remove row"), left);
    connect(delBtn, &QPushButton::clicked, this, [this] { removeCurrentRow(); });
    rowBtns->addWidget(delBtn);
    rowBtns->addStretch(1);
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
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
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
}

void BomDialog::storeTable() {
    if (!editor_ || !editor_->document()) return;
    editor_->document()->bom() = table_.toJson();
    editor_->document()->setDirty(true);
}

std::vector<BomRow> BomDialog::shownRows() const {
    std::vector<BomRow> rows;
    if (fromDrawingBox_ && fromDrawingBox_->isChecked() && editor_ && editor_->document())
        rows = bomFromDrawing(*editor_->document(), lib_);
    for (const BomRow& r : table_.rows)
        if (!r.fromDrawing) rows.push_back(r);
    return unify_ ? bomUnify(rows) : rows;
}

void BomDialog::reload() {
    lib_.load(PartsLibrary::defaultPath()); // the library may have changed since this opened
    refreshLibraryList();
    refreshTable();
}

void BomDialog::setUnifyTotals(bool on) {
    unify_ = on;
    refreshTable();
}

void BomDialog::refreshTable() {
    if (!view_) return;
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
        cell(5, row.qtyText(), typed);
        // The unit is a choice, not free text: what can be totalled with what depends on it.
        if (typed) {
            auto* box = new QComboBox(view_);
            box->addItems(partUnitCodes());
            if (box->findText(row.unit) < 0) box->addItem(row.unit);
            box->setCurrentText(row.unit);
            const QString desc = row.description, pn = row.mfrPn;
            connect(box, &QComboBox::currentTextChanged, this,
                    [this, desc, pn](const QString& u) {
                        if (syncing_) return;
                        for (BomRow& tr : table_.rows)
                            if (!tr.fromDrawing && tr.description == desc && tr.mfrPn == pn) {
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

void BomDialog::addManualRow() {
    BomRow r;
    r.description = QStringLiteral("(new item)");
    r.qty = 1.0;
    r.unit = QStringLiteral("each");
    r.fromDrawing = false;
    table_.rows.push_back(r);
    storeTable();
    refreshTable();
    if (summary_) summary_->setText(QStringLiteral("Row added — fill it in"));
}

void BomDialog::addFromLibrary(const QString& partId, double qty) {
    const LibraryPart* p = lib_.byId(partId);
    if (!p) return;
    BomRow r = bomRowFromPart(*p, qty);
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
        if (!table_.rows[i].fromDrawing && table_.rows[i].description == row.description &&
            table_.rows[i].mfrPn == row.mfrPn) {
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
    const int cols = bomColumnCount();
    const int nRows = static_cast<int>(rows.size()) + 1;
    const double rowH = 8.0, textH = rowH * 0.5;
    QJsonArray cells;
    for (int i = 0; i < cols; ++i) cells.append(table_.header(i));
    for (const BomRow& r : rows) {
        cells.append(QString::number(static_cast<int>(cells.size()) / cols)); // line #
        cells.append(r.description);
        cells.append(r.internalPn);
        cells.append(r.mfrPn);
        cells.append(r.mfrName);
        cells.append(r.qtyText());
        cells.append(r.unit);
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
        colW[static_cast<size_t>(c)] = std::max(colW[static_cast<size_t>(c)],
                                               c == 1 ? plainW : minW);
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
    for (int i = 0; i < cells.size(); ++i)
        cellAlign.append((i % cols) == 1 ? QStringLiteral("ML") : QStringLiteral("MC"));
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

} // namespace Edraw_Desktop
