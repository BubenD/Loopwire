#include "app/PartFromPdfDialog.h"

#include "app/DrawingPreview.h"
#include "harness/PartsLibrary.h"
#include "io/PdfImport.h"
#include "model/Document.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace Edraw_Desktop {

PartFromPdfDialog::PartFromPdfDialog(const QString& pdfPath, const QString& partTitle, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Draw the part from a PDF — %1").arg(partTitle));
    resize(1200, 760);

    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        Document doc;
        loaded_ = pdf::importFile(doc, pdfPath, &error_, nullptr, &pages_);
        QApplication::restoreOverrideCursor();
        if (loaded_) all_ = doc.entities();
    }
    // A PDF with no page tree to follow comes in as one drawing: one "page" round all of it.
    if (loaded_ && pages_.empty()) {
        Bounds b;
        for (const Entity& e : all_) b.expand(e.bounds());
        pages_.push_back(b);
    }

    auto* lay = new QVBoxLayout(this);
    auto* intro = new QLabel(
        QStringLiteral("<b>%1</b> — drag a box round the view of the part to take it as the mechanical drawing, "
                       "and type the width the datasheet gives it. The pinout is read from the pin table; "
                       "put it right below, or draw a box round the table and read it from there.")
            .arg(QFileInfo(pdfPath).fileName().toHtmlEscaped()),
        this);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    auto* split = new QSplitter(Qt::Horizontal, this);
    lay->addWidget(split, 1);

    // THE PAGE, left: the page to show, and the page itself to draw the box on.
    auto* left = new QWidget(split);
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    auto* pageRow = new QHBoxLayout;
    pageRow->addWidget(new QLabel(QStringLiteral("Page"), left));
    page_ = new QComboBox(left);
    page_->setObjectName(QStringLiteral("pdfPartPage"));
    for (size_t i = 0; i < pages_.size(); ++i) page_->addItem(QStringLiteral("%1 of %2").arg(i + 1).arg(pages_.size()));
    pageRow->addWidget(page_);
    pageRow->addStretch(1);
    ll->addLayout(pageRow);
    view_ = new DrawingPreview(QStringLiteral("The PDF — drag a box round the part"), left);
    view_->setObjectName(QStringLiteral("pdfPartView"));
    view_->setBoxSelect(true);
    view_->setFitButtonShown(true);
    view_->setMinimumSize(560, 480);
    ll->addWidget(view_, 1);
    split->addWidget(left);

    // WHAT IS TAKEN, right: the drawing at its size, and the pinout.
    auto* right = new QWidget(split);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    takeDrawing_ = new QCheckBox(QStringLiteral("Take the boxed view as the mechanical drawing"), right);
    takeDrawing_->setObjectName(QStringLiteral("pdfPartTakeDrawing"));
    rl->addWidget(takeDrawing_);
    auto* widthRow = new QHBoxLayout;
    widthRow->addWidget(new QLabel(QStringLiteral("Its width"), right));
    width_ = new QDoubleSpinBox(right);
    width_->setObjectName(QStringLiteral("pdfPartWidth"));
    width_->setRange(0.01, 100000.0);
    width_->setDecimals(2);
    width_->setSuffix(QStringLiteral(" mm"));
    width_->setToolTip(QStringLiteral(
        "How wide the boxed view is on the real part - the overall width the datasheet's dimensions give it. "
        "It starts at the size the page prints it, which is only right for a drawing at 1:1."));
    widthRow->addWidget(width_, 1);
    rl->addLayout(widthRow);
    withText_ = new QCheckBox(QStringLiteral("Keep the text in the box (dimensions, notes)"), right);
    withText_->setObjectName(QStringLiteral("pdfPartWithText"));
    rl->addWidget(withText_);
    boxSaid_ = new QLabel(QStringLiteral("No box drawn yet."), right);
    boxSaid_->setWordWrap(true);
    rl->addWidget(boxSaid_);
    result_ = new DrawingPreview(QStringLiteral("The mechanical drawing"), right);
    result_->setObjectName(QStringLiteral("pdfPartResult"));
    result_->setEmptyText(QStringLiteral("draw a box round the part"));
    result_->setMinimumSize(300, 200);
    rl->addWidget(result_, 1);

    takePinout_ = new QCheckBox(QStringLiteral("Take the pinout (the pin count, and the symbol's pin labels)"), right);
    takePinout_->setObjectName(QStringLiteral("pdfPartTakePinout"));
    rl->addWidget(takePinout_);
    pinsSaid_ = new QLabel(right);
    pinsSaid_->setWordWrap(true);
    rl->addWidget(pinsSaid_);
    pins_ = new QTableWidget(0, 2, right);
    pins_->setObjectName(QStringLiteral("pdfPartPins"));
    pins_->setHorizontalHeaderLabels({QStringLiteral("Pin"), QStringLiteral("Signal")});
    pins_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    pins_->verticalHeader()->setVisible(false);
    pins_->setMinimumHeight(160);
    rl->addWidget(pins_, 1);
    auto* pinBtns = new QHBoxLayout;
    auto* fromBox = new QPushButton(QStringLiteral("Read from the box"), right);
    fromBox->setObjectName(QStringLiteral("pdfPartReadPins"));
    fromBox->setToolTip(QStringLiteral("Read the pin table inside the box drawn on the page"));
    pinBtns->addWidget(fromBox);
    auto* addRow = new QPushButton(QStringLiteral("Add pin"), right);
    pinBtns->addWidget(addRow);
    auto* delRow = new QPushButton(QStringLiteral("Remove pin"), right);
    pinBtns->addWidget(delRow);
    pinBtns->addStretch(1);
    rl->addLayout(pinBtns);
    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Take into the part"));
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto okState = [this, bb] {
        bb->button(QDialogButtonBox::Ok)->setEnabled(takeDrawing() || takePinout());
    };

    connect(page_, &QComboBox::currentIndexChanged, this, [this](int i) { showPage(i); });
    connect(view_, &DrawingPreview::boxChanged, this, [this] { boxChanged(); });
    connect(width_, &QDoubleSpinBox::valueChanged, this, [this] {
        widthTyped_ = true;
        refreshResult();
    });
    connect(withText_, &QCheckBox::toggled, this, [this] { refreshResult(); });
    connect(fromBox, &QPushButton::clicked, this, [this] { readPinsFromBox(); });
    connect(addRow, &QPushButton::clicked, this, [this] {
        const int r = pins_->rowCount();
        pins_->insertRow(r);
        pins_->setItem(r, 0, new QTableWidgetItem(QString::number(r + 1)));
        pins_->setItem(r, 1, new QTableWidgetItem);
        takePinout_->setChecked(true);
    });
    connect(delRow, &QPushButton::clicked, this, [this] {
        const int r = pins_->currentRow() >= 0 ? pins_->currentRow() : pins_->rowCount() - 1;
        if (r >= 0) pins_->removeRow(r);
    });
    connect(takeDrawing_, &QCheckBox::toggled, this, okState);
    connect(takePinout_, &QCheckBox::toggled, this, okState);

    // THE PINOUT FOUND BY ITSELF, from every page: a datasheet's pin table is usually the only column of
    // numbers counting up from one with a word beside each.
    std::vector<PdfPin> found = pinoutFromText(all_);
    fillPins(found);
    pinsSaid_->setText(found.empty() ? QStringLiteral("No pin table found by itself - draw a box round it and "
                                                      "read it from the box, or type the pins in.")
                                     : QStringLiteral("%1 pins found in the PDF's pin table.").arg(found.size()));
    showPage(0);
    okState();
}

void PartFromPdfDialog::showPage(int index) {
    if (index < 0 || index >= static_cast<int>(pages_.size())) {
        view_->setEntities({});
        return;
    }
    if (page_->currentIndex() != index) {
        const QSignalBlocker hold(page_);
        page_->setCurrentIndex(index);
    }
    const Bounds& page = pages_[static_cast<size_t>(index)];
    pageEnts_.clear();
    for (const Entity& e : all_) {
        const Bounds b = e.type == EntityType::Text ? Bounds{e.a, e.a, true} : e.bounds();
        if (!b.valid) continue;
        const Vec2 c = b.center();
        if (!page.valid || (c.x >= page.min.x && c.x <= page.max.x && c.y >= page.min.y && c.y <= page.max.y))
            pageEnts_.push_back(e);
    }
    view_->setSelectedBox(Bounds{});
    view_->setEntities(pageEnts_);
    view_->fitToView();
    boxChanged();
}

std::vector<Entity> PartFromPdfDialog::picked() const {
    return entitiesInBox(pageEnts_, view_->selectedBox(), withText_->isChecked());
}

void PartFromPdfDialog::boxChanged() {
    const std::vector<Entity> got = entitiesInBox(pageEnts_, view_->selectedBox(), false);
    Bounds b;
    for (const Entity& e : got) b.expand(e.bounds());
    // THE WIDTH FOLLOWS THE BOX until one is typed: the page's own size, which is right for a 1:1 drawing
    // and a starting figure to correct for any other.
    if (!widthTyped_ && b.valid && b.width() > 0.0) {
        const QSignalBlocker hold(width_);
        width_->setValue(pdfPointsToMm(b.width()));
    }
    {
        const QSignalBlocker hold(takeDrawing_);
        takeDrawing_->setChecked(!got.empty());
    }
    takeDrawing_->setEnabled(!got.empty());
    emit takeDrawing_->toggled(takeDrawing_->isChecked());
    refreshResult();
}

void PartFromPdfDialog::refreshResult() {
    const std::vector<Entity> got = picked();
    const QJsonArray made = partDrawingFromPdf(got, width_->value());
    result_->setDrawing(made);
    result_->fitToView();
    if (made.isEmpty()) {
        boxSaid_->setText(view_->selectedBox().valid ? QStringLiteral("Nothing lies wholly inside the box.")
                                                     : QStringLiteral("No box drawn yet."));
        return;
    }
    Bounds b;
    for (const Entity& e : partDrawingEntities(made))
        if (e.type != EntityType::Text) b.expand(e.bounds());
    boxSaid_->setText(QStringLiteral("%1 object(s), %2 × %3 mm.")
                          .arg(made.size())
                          .arg(b.width(), 0, 'f', 2)
                          .arg(b.height(), 0, 'f', 2));
}

void PartFromPdfDialog::fillPins(const std::vector<PdfPin>& pins) {
    pins_->setRowCount(0);
    for (const PdfPin& p : pins) {
        const int r = pins_->rowCount();
        pins_->insertRow(r);
        pins_->setItem(r, 0, new QTableWidgetItem(p.pin));
        pins_->setItem(r, 1, new QTableWidgetItem(p.name));
    }
    takePinout_->setChecked(!pins.empty());
}

void PartFromPdfDialog::readPinsFromBox() {
    const Bounds box = view_->selectedBox();
    if (!box.valid) {
        pinsSaid_->setText(QStringLiteral("Draw a box round the pin table first."));
        return;
    }
    const std::vector<PdfPin> got = pinoutFromText(pageEnts_, &box);
    if (got.empty()) {
        pinsSaid_->setText(QStringLiteral("No pin table inside the box - pins counting up from 1 (or A), "
                                          "each with its signal to the right of it."));
        return;
    }
    fillPins(got);
    pinsSaid_->setText(QStringLiteral("%1 pins read from the box.").arg(got.size()));
}

void PartFromPdfDialog::setBox(const Bounds& box) {
    view_->setSelectedBox(box);
    boxChanged();
}

void PartFromPdfDialog::setWidthMm(double mm) {
    width_->setValue(mm);
}

bool PartFromPdfDialog::takeDrawing() const {
    return takeDrawing_->isEnabled() && takeDrawing_->isChecked();
}

bool PartFromPdfDialog::takePinout() const {
    return takePinout_->isChecked() && !pinout().empty();
}

QJsonArray PartFromPdfDialog::drawing() const {
    return partDrawingFromPdf(picked(), width_->value());
}

std::vector<PdfPin> PartFromPdfDialog::pinout() const {
    std::vector<PdfPin> out;
    for (int r = 0; r < pins_->rowCount(); ++r) {
        const QString pin = pins_->item(r, 0) ? pins_->item(r, 0)->text().trimmed() : QString();
        const QString name = pins_->item(r, 1) ? pins_->item(r, 1)->text().trimmed() : QString();
        if (!pin.isEmpty()) out.push_back({pin, name});
    }
    return out;
}

} // namespace Edraw_Desktop
