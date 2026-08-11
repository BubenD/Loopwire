#include "app/OutputDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace Edraw_Desktop {

namespace {

// What a space is called on the list, and what it says about itself: a sheet says its size, because
// choosing between Layout1 and Layout2 is usually choosing between an A3 and an A4.
QString spaceLabel(const Document& doc, int space) {
    if (space == 0) return QStringLiteral("Model — the harness, at size");
    const auto& layouts = doc.layouts();
    const int at = space - 1;
    if (at < 0 || at >= static_cast<int>(layouts.size())) return QStringLiteral("Layout %1").arg(space);
    const DrawingLayout& layout = layouts[static_cast<size_t>(at)];
    const int windows = static_cast<int>(layout.viewports.size());
    return QStringLiteral("%1 — %2 × %3 mm, %4 window%5")
        .arg(layout.name)
        .arg(layout.paperWidth, 0, 'f', 0)
        .arg(layout.paperHeight, 0, 'f', 0)
        .arg(windows)
        .arg(windows == 1 ? QString() : QStringLiteral("s"));
}

} // namespace

OutputDialog::OutputDialog(const Document& doc, int currentSpace, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Output"));
    setModal(true);

    auto* outer = new QVBoxLayout(this);
    outer->addWidget(new QLabel(
        QStringLiteral("What comes out, and in what order. Drag to arrange, or use the arrows."), this));

    auto* row = new QHBoxLayout();
    list_ = new QListWidget(this);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setDragDropMode(QAbstractItemView::InternalMove);   // arranged by dragging
    list_->setMinimumWidth(340);
    list_->setMinimumHeight(190);

    // Model first, then the sheets in the drawing's own order. Everything is ticked except that, when a
    // sheet is being worked on, the model is left out: laying out sheets and then putting out the raw
    // model with them is almost never what was meant.
    const int spaces = 1 + static_cast<int>(doc.layouts().size());
    for (int space = 0; space < spaces; ++space) {
        auto* item = new QListWidgetItem(spaceLabel(doc, space), list_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const bool sheetsInHand = currentSpace > 0;
        item->setCheckState((space == 0 && sheetsInHand) ? Qt::Unchecked : Qt::Checked);
        item->setData(Qt::UserRole, space);
        if (space == currentSpace) {
            QFont f = item->font();
            f.setBold(true);   // the one being worked on, so it is found at a glance
            item->setFont(f);
        }
    }
    if (currentSpace >= 0 && currentSpace < list_->count()) list_->setCurrentRow(currentSpace);

    auto* side = new QVBoxLayout();
    auto* up = new QToolButton(this);
    up->setText(QStringLiteral("▲"));
    up->setToolTip(QStringLiteral("Earlier in the pack"));
    auto* down = new QToolButton(this);
    down->setText(QStringLiteral("▼"));
    down->setToolTip(QStringLiteral("Later in the pack"));
    auto* all = new QToolButton(this);
    all->setText(QStringLiteral("All"));
    all->setToolTip(QStringLiteral("Include everything"));
    auto* none = new QToolButton(this);
    none->setText(QStringLiteral("None"));
    none->setToolTip(QStringLiteral("Include nothing, and start again"));
    for (QToolButton* b : {up, down, all, none}) b->setAutoRaise(true);
    side->addWidget(up);
    side->addWidget(down);
    side->addSpacing(12);
    side->addWidget(all);
    side->addWidget(none);
    side->addStretch(1);
    row->addWidget(list_, 1);
    row->addLayout(side);
    outer->addLayout(row);

    auto* where = new QGroupBox(QStringLiteral("Where it goes"), this);
    auto* whereBox = new QVBoxLayout(where);
    toPdf_ = new QRadioButton(QStringLiteral("A PDF file"), where);
    toPrint_ = new QRadioButton(QStringLiteral("A printer"), where);
    toPdf_->setChecked(true);
    whereBox->addWidget(toPdf_);
    whereBox->addWidget(toPrint_);
    perSheet_ = new QCheckBox(QStringLiteral("A separate PDF for each sheet"), where);
    perSheet_->setToolTip(QStringLiteral(
        "One file per sheet, named after it, rather than one file of many pages."));
    whereBox->addWidget(perSheet_);
    outer->addWidget(where);

    auto* how = new QGroupBox(QStringLiteral("How it is drawn"), this);
    auto* howBox = new QVBoxLayout(how);
    black_ = new QRadioButton(QStringLiteral("Black lines on white"), how);
    colour_ = new QRadioButton(QStringLiteral("The drawing's own colours, on white"), how);
    black_->setChecked(true);
    howBox->addWidget(black_);
    howBox->addWidget(colour_);
    fit_ = new QCheckBox(QStringLiteral("Fit each page to the paper"), how);
    fit_->setToolTip(QStringLiteral(
        "Off: a sheet goes out at its own size, so a scale read off it with a rule is right. On: it is "
        "sized to whatever paper the printer has."));
    howBox->addWidget(fit_);
    outer->addWidget(how);

    summary_ = new QLabel(this);
    summary_->setStyleSheet(QStringLiteral("color:#8fa2b8;"));
    outer->addWidget(summary_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Output"));
    outer->addWidget(buttons);

    connect(up, &QToolButton::clicked, this, [this] { moveSelected(-1); });
    connect(down, &QToolButton::clicked, this, [this] { moveSelected(1); });
    connect(all, &QToolButton::clicked, this, [this] { tickAll(true); });
    connect(none, &QToolButton::clicked, this, [this] { tickAll(false); });
    connect(list_, &QListWidget::itemChanged, this, [this] { updateSummary(); });
    connect(toPdf_, &QRadioButton::toggled, this, [this] { updateSummary(); });
    connect(perSheet_, &QCheckBox::toggled, this, [this] { updateSummary(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateSummary();
}

void OutputDialog::moveSelected(int delta) {
    if (!list_ || delta == 0) return;
    const int row = list_->currentRow();
    const int to = row + delta;
    if (row < 0 || to < 0 || to >= list_->count()) return;
    QListWidgetItem* item = list_->takeItem(row);
    list_->insertItem(to, item);
    list_->setCurrentRow(to);
    updateSummary();
}

void OutputDialog::tickAll(bool on) {
    if (!list_) return;
    for (int i = 0; i < list_->count(); ++i)
        list_->item(i)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    updateSummary();
}

void OutputDialog::updateSummary() {
    if (!summary_) return;
    const std::vector<int> chosen = spaces();
    if (chosen.empty()) {
        summary_->setText(QStringLiteral("Nothing is ticked — nothing would come out."));
        return;
    }
    const bool separate = toPdf_ && toPdf_->isChecked() && perSheet_ && perSheet_->isChecked();
    summary_->setText(
        separate ? QStringLiteral("%1 file%2, one per sheet, in the order above.")
                       .arg(chosen.size())
                       .arg(chosen.size() == 1 ? QString() : QStringLiteral("s"))
                 : QStringLiteral("%1 page%2, in the order above.")
                       .arg(chosen.size())
                       .arg(chosen.size() == 1 ? QString() : QStringLiteral("s")));
}

std::vector<int> OutputDialog::spaces() const {
    std::vector<int> out;
    if (!list_) return out;
    for (int i = 0; i < list_->count(); ++i) {
        const QListWidgetItem* item = list_->item(i);
        if (item->checkState() == Qt::Checked) out.push_back(item->data(Qt::UserRole).toInt());
    }
    return out;
}

bool OutputDialog::toPrinter() const { return toPrint_ && toPrint_->isChecked(); }
bool OutputDialog::monochrome() const { return black_ && black_->isChecked(); }
bool OutputDialog::fitToPage() const { return fit_ && fit_->isChecked(); }
bool OutputDialog::oneFilePerSheet() const {
    return perSheet_ && perSheet_->isChecked() && !toPrinter();
}

} // namespace Edraw_Desktop
