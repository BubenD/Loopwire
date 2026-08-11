#include "app/PageSetupManagerDialog.h"

#include "app/PageSetupDialog.h"
#include "io/Dwx.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

namespace Edraw_Desktop {

namespace {

// A setup's row on the list. The layout's own is written in stars, as a plot dialog writes it.
QString rowLabel(const PageSetup& setup, bool layoutOwn, const QString& layoutName) {
    return layoutOwn ? QStringLiteral("*%1*").arg(layoutName) : setup.name;
}

} // namespace

PageSetupManagerDialog::PageSetupManagerDialog(Document& doc, int layoutIndex, QWidget* parent)
    : QDialog(parent), doc_(doc), layoutIndex_(layoutIndex) {
    setWindowTitle(QStringLiteral("Page setup manager"));
    setModal(true);

    auto* outer = new QVBoxLayout(this);
    current_ = new QLabel(this);
    outer->addWidget(current_);

    auto* box = new QGroupBox(QStringLiteral("Page setups in this drawing"), this);
    auto* boxRow = new QHBoxLayout(box);
    list_ = new QListWidget(box);
    list_->setMinimumWidth(280);
    list_->setMinimumHeight(150);
    boxRow->addWidget(list_, 1);

    auto* side = new QVBoxLayout();
    setCurrentBtn_ = new QPushButton(QStringLiteral("Set current"), box);
    auto* newBtn = new QPushButton(QStringLiteral("New..."), box);
    auto* modifyBtn = new QPushButton(QStringLiteral("Edit..."), box);
    auto* importBtn = new QPushButton(QStringLiteral("Import from drawing..."), box);
    side->addWidget(setCurrentBtn_);
    side->addWidget(newBtn);
    side->addWidget(modifyBtn);
    side->addWidget(importBtn);
    side->addStretch(1);
    boxRow->addLayout(side);
    outer->addWidget(box);

    auto* detailBox = new QGroupBox(QStringLiteral("Page setup details"), this);
    auto* detailCol = new QVBoxLayout(detailBox);
    details_ = new QLabel(detailBox);
    details_->setWordWrap(true);
    details_->setTextFormat(Qt::PlainText);
    detailCol->addWidget(details_);
    outer->addWidget(detailBox);

    showOnNew_ = new QCheckBox(QStringLiteral("Prompt for a page setup on a new layout"), this);
    showOnNew_->setToolTip(QStringLiteral(
        "Not yet: a new layout takes the drawing's default paper. Left here because it is where the "
        "setting belongs when it arrives."));
    showOnNew_->setEnabled(false);
    outer->addWidget(showOnNew_);

    auto* closeRow = new QHBoxLayout();
    closeRow->addStretch(1);
    auto* close = new QPushButton(QStringLiteral("Close"), this);
    close->setDefault(true);
    closeRow->addWidget(close);
    outer->addLayout(closeRow);

    connect(list_, &QListWidget::currentRowChanged, this, [this] { updateDetails(); });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] { modify(); });
    connect(setCurrentBtn_, &QPushButton::clicked, this, [this] { setCurrent(); });
    connect(newBtn, &QPushButton::clicked, this, [this] { makeNew(); });
    connect(modifyBtn, &QPushButton::clicked, this, [this] { modify(); });
    connect(importBtn, &QPushButton::clicked, this, [this] { importFrom(); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    rebuild();
}

void PageSetupManagerDialog::rebuild() {
    if (!list_) return;
    const int keep = list_->currentRow();
    list_->clear();
    const auto& layouts = doc_.layouts();
    const bool haveLayout = layoutIndex_ >= 0 && layoutIndex_ < static_cast<int>(layouts.size());
    const QString layoutName =
        haveLayout ? layouts[static_cast<size_t>(layoutIndex_)].name : QStringLiteral("Layout");
    if (haveLayout) {
        auto* own = new QListWidgetItem(
            rowLabel(layouts[static_cast<size_t>(layoutIndex_)].pageSetup, true, layoutName), list_);
        own->setData(Qt::UserRole, -1);   // -1: the layout's own
    }
    for (int i = 0; i < static_cast<int>(doc_.pageSetups().size()); ++i) {
        auto* item = new QListWidgetItem(doc_.pageSetups()[static_cast<size_t>(i)].name, list_);
        item->setData(Qt::UserRole, i);
    }
    list_->setCurrentRow(keep >= 0 && keep < list_->count() ? keep : 0);
    if (current_)
        current_->setText(QStringLiteral("Layout:  %1\nPage setup in use:  %2")
                              .arg(layoutName)
                              .arg(haveLayout && !layouts[static_cast<size_t>(layoutIndex_)]
                                                      .pageSetup.name.isEmpty()
                                       ? layouts[static_cast<size_t>(layoutIndex_)].pageSetup.name
                                       : QStringLiteral("the layout's own")));
    updateDetails();
}

PageSetup* PageSetupManagerDialog::setupAt(int row) {
    if (!list_ || row < 0 || row >= list_->count()) return nullptr;
    const int which = list_->item(row)->data(Qt::UserRole).toInt();
    if (which < 0) {
        auto& layouts = doc_.layouts();
        if (layoutIndex_ < 0 || layoutIndex_ >= static_cast<int>(layouts.size())) return nullptr;
        return &layouts[static_cast<size_t>(layoutIndex_)].pageSetup;
    }
    if (which >= static_cast<int>(doc_.pageSetups().size())) return nullptr;
    return &doc_.pageSetups()[static_cast<size_t>(which)];
}

void PageSetupManagerDialog::updateDetails() {
    if (!details_) return;
    const PageSetup* setup = setupAt(list_ ? list_->currentRow() : -1);
    if (!setup) {
        details_->setText(QStringLiteral("Nothing is selected."));
        if (setCurrentBtn_) setCurrentBtn_->setEnabled(false);
        return;
    }
    const PaperSize& size = paperSizeByName(setup->paper);
    const QString what = setup->area == PlotArea::Extents  ? QStringLiteral("extents")
                         : setup->area == PlotArea::Display ? QStringLiteral("display")
                         : setup->area == PlotArea::Window
                             ? QStringLiteral("window, %1 × %2 mm")
                                   .arg(std::abs(setup->windowX2Mm - setup->windowX1Mm), 0, 'f', 1)
                                   .arg(std::abs(setup->windowY2Mm - setup->windowY1Mm), 0, 'f', 1)
                             : QStringLiteral("layout");
    details_->setText(
        QStringLiteral("Device:   %1\nPaper size:   %2, %3 mm, %4\nPrintable area:   %5\n"
                       "Plot area:   %6\nPlot scale:   %7\nInk:   %8, %9")
            .arg(setup->device.isEmpty() ? QStringLiteral("none — plots to a PDF file")
                                         : setup->device)
            .arg(setup->paper)
            .arg(QStringLiteral("%1 × %2")
                     .arg(setup->pageWidthMm(), 0, 'f', 0)
                     .arg(setup->pageHeightMm(), 0, 'f', 0))
            .arg(setup->landscape ? QStringLiteral("landscape") : QStringLiteral("portrait"))
            .arg(size.marginMm <= 0.0
                     ? QStringLiteral("borderless — the full page")
                     : QStringLiteral("%1 × %2 mm, inside a %3 mm unprintable margin")
                           .arg(setup->printableWidthMm(), 0, 'f', 0)
                           .arg(setup->printableHeightMm(), 0, 'f', 0)
                           .arg(size.marginMm, 0, 'f', 1))
            .arg(what)
            .arg(setup->scaleLabel())
            .arg(setup->monochrome ? QStringLiteral("monochrome")
                                   : QStringLiteral("the drawing's colours"))
            .arg(setup->plotLineweights ? QStringLiteral("lineweights plotted")
                                        : QStringLiteral("lineweights off")));
    // The layout's own setup is already current: there is nothing to set.
    const bool isOwn = list_ && list_->currentRow() >= 0 &&
                       list_->item(list_->currentRow())->data(Qt::UserRole).toInt() < 0;
    if (setCurrentBtn_) setCurrentBtn_->setEnabled(!isOwn);
}

void PageSetupManagerDialog::setCurrent() {
    const PageSetup* named = setupAt(list_ ? list_->currentRow() : -1);
    auto& layouts = doc_.layouts();
    if (!named || layoutIndex_ < 0 || layoutIndex_ >= static_cast<int>(layouts.size())) return;
    // The named setup's values are COPIED onto the layout, and the name is kept with them so the manager
    // can say which one the sheet is following. A layout plots by its own setup, always: a named setup
    // that was renamed or deleted must not be able to change how a sheet plots.
    PageSetup applied = *named;
    layouts[static_cast<size_t>(layoutIndex_)].pageSetup = applied;
    doc_.setDirty(true);
    changed_ = true;
    rebuild();
}

void PageSetupManagerDialog::makeNew() {
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("New page setup"), QStringLiteral("Name:"), QLineEdit::Normal,
        QStringLiteral("Setup%1").arg(doc_.pageSetups().size() + 1), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    for (const PageSetup& existing : doc_.pageSetups())
        if (existing.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
            QMessageBox::information(this, QStringLiteral("New page setup"),
                                     QStringLiteral("There is already a setup called %1.").arg(name));
            return;
        }
    // Started from whatever is selected, so a new setup is usually a small change to one that works.
    PageSetup start;
    if (const PageSetup* from = setupAt(list_ ? list_->currentRow() : -1)) start = *from;
    start.name = name.trimmed();
    double sheetW = 0.0, sheetH = 0.0;
    if (layoutIndex_ >= 0 && layoutIndex_ < static_cast<int>(doc_.layouts().size())) {
        sheetW = doc_.layouts()[static_cast<size_t>(layoutIndex_)].paperWidth;
        sheetH = doc_.layouts()[static_cast<size_t>(layoutIndex_)].paperHeight;
    }
    PageSetupDialog dlg(start, start.name, sheetW, sheetH, this);
    if (dlg.exec() != QDialog::Accepted) return;
    PageSetup made = dlg.setup();
    made.name = start.name;
    doc_.pageSetups().push_back(made);
    doc_.setDirty(true);
    changed_ = true;
    rebuild();
    if (list_) list_->setCurrentRow(list_->count() - 1);
}

void PageSetupManagerDialog::modify() {
    PageSetup* setup = setupAt(list_ ? list_->currentRow() : -1);
    if (!setup) return;
    const auto& layouts = doc_.layouts();
    const bool isOwn = list_->item(list_->currentRow())->data(Qt::UserRole).toInt() < 0;
    const QString title =
        isOwn && layoutIndex_ < static_cast<int>(layouts.size())
            ? layouts[static_cast<size_t>(layoutIndex_)].name
            : setup->name;
    double sheetW = 0.0, sheetH = 0.0;
    if (layoutIndex_ >= 0 && layoutIndex_ < static_cast<int>(layouts.size())) {
        sheetW = layouts[static_cast<size_t>(layoutIndex_)].paperWidth;
        sheetH = layouts[static_cast<size_t>(layoutIndex_)].paperHeight;
    }
    PageSetupDialog dlg(*setup, title, sheetW, sheetH, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString keepName = setup->name;
    *setup = dlg.setup();
    setup->name = keepName;
    doc_.setDirty(true);
    changed_ = true;
    rebuild();
}

void PageSetupManagerDialog::importFrom() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import page setups from"),
                                                      QString(), QStringLiteral("DWX (*.dwx)"));
    if (path.isEmpty()) return;
    Document other;
    QString err;
    if (!dwx::load(other, path, &err)) {
        QMessageBox::warning(this, QStringLiteral("Import page setups"),
                             err.isEmpty() ? QStringLiteral("Could not read that drawing.") : err);
        return;
    }
    // Everything that drawing has: its named setups, and each of its layouts' own setups under the
    // layout's name - which is what makes importing worth doing at all, since most offices keep their
    // paper and scale on the sheets rather than in named setups.
    int added = 0;
    auto addOne = [&](PageSetup setup, const QString& name) {
        setup.name = name;
        for (const PageSetup& existing : doc_.pageSetups())
            if (existing.name.compare(name, Qt::CaseInsensitive) == 0) return;
        doc_.pageSetups().push_back(setup);
        ++added;
    };
    for (const PageSetup& setup : other.pageSetups())
        if (!setup.name.isEmpty()) addOne(setup, setup.name);
    for (const DrawingLayout& layout : other.layouts())
        addOne(layout.pageSetup, QStringLiteral("%1 (%2)").arg(layout.name, QFileInfo(path).baseName()));
    if (added == 0) {
        QMessageBox::information(this, QStringLiteral("Import page setups"),
                                 QStringLiteral("Nothing new to import from that drawing."));
        return;
    }
    doc_.setDirty(true);
    changed_ = true;
    rebuild();
}

} // namespace Edraw_Desktop
