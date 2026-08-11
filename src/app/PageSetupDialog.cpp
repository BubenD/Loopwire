#include "app/PageSetupDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QAbstractItemView>
#include <QPrinterInfo>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include <cmath>

namespace Edraw_Desktop {

PageSetupDialog::PageSetupDialog(const PageSetup& setup, const QString& title, QWidget* parent)
    : QDialog(parent), setup_(setup) {
    setWindowTitle(QStringLiteral("Page setup — %1").arg(title));
    setModal(true);

    auto* outer = new QVBoxLayout(this);
    auto* columns = new QHBoxLayout();
    auto* left = new QVBoxLayout();
    auto* right = new QVBoxLayout();
    columns->addLayout(left, 3);
    columns->addLayout(right, 2);
    outer->addLayout(columns);

    // ---- printer / plotter -----------------------------------------------------------------------
    auto* printerBox = new QGroupBox(QStringLiteral("Printer / plotter"), this);
    auto* printerForm = new QFormLayout(printerBox);
    device_ = new QComboBox(printerBox);
    device_->addItem(QStringLiteral("None — plot to a PDF file"), QString());
    for (const QPrinterInfo& info : QPrinterInfo::availablePrinters())
        device_->addItem(info.printerName(), info.printerName());
    const int atDevice = device_->findData(setup_.device);
    device_->setCurrentIndex(atDevice >= 0 ? atDevice : 0);
    device_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    device_->setMinimumContentsLength(26);
    if (device_->view()) device_->view()->setMinimumWidth(300);
    printerForm->addRow(QStringLiteral("Name:"), device_);
    auto* deviceNote = new QLabel(
        QStringLiteral("With no printer chosen the layout plots to a PDF, at the size set below."),
        printerBox);
    deviceNote->setWordWrap(true);
    deviceNote->setMaximumWidth(340);
    printerForm->addRow(deviceNote);
    left->addWidget(printerBox);

    // ---- paper size ------------------------------------------------------------------------------
    auto* paperBox = new QGroupBox(QStringLiteral("Paper size"), this);
    auto* paperCol = new QVBoxLayout(paperBox);
    paper_ = new QComboBox(paperBox);
    for (const PaperSize& size : paperSizes()) paper_->addItem(size.name, size.name);
    const int atPaper = paper_->findData(setup_.paper);
    paper_->setCurrentIndex(atPaper >= 0 ? atPaper : 0);
    paper_->setMaxVisibleItems(24);
    // The names are long - "ISO full bleed A3 (420.00 x 297.00 MM)" - and a combo sized to its longest
    // item drags the whole dialog out to a thousand pixels. The box is kept to a sensible width and the
    // POPUP is given the room instead, which is where the name has to be read.
    paper_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    paper_->setMinimumContentsLength(30);
    if (paper_->view()) paper_->view()->setMinimumWidth(360);
    paperCol->addWidget(paper_);
    paperDetail_ = new QLabel(paperBox);
    paperDetail_->setWordWrap(true);
    paperDetail_->setMaximumWidth(340);
    paperDetail_->setMinimumHeight(52);   // three lines: the figures do not shift as the paper changes
    paperCol->addWidget(paperDetail_);
    left->addWidget(paperBox);

    // ---- plot area and scale ---------------------------------------------------------------------
    auto* areaBox = new QGroupBox(QStringLiteral("Plot area"), this);
    auto* areaForm = new QFormLayout(areaBox);
    area_ = new QComboBox(areaBox);
    area_->addItem(QStringLiteral("Layout — the sheet, at its own size"),
                   static_cast<int>(PlotArea::Layout));
    area_->addItem(QStringLiteral("Extents — everything drawn, fitted"),
                   static_cast<int>(PlotArea::Extents));
    area_->setCurrentIndex(setup_.area == PlotArea::Extents ? 1 : 0);
    areaForm->addRow(QStringLiteral("What to plot:"), area_);
    left->addWidget(areaBox);

    auto* scaleBox = new QGroupBox(QStringLiteral("Plot scale"), this);
    auto* scaleCol = new QVBoxLayout(scaleBox);
    fit_ = new QCheckBox(QStringLiteral("Fit to paper"), scaleBox);
    fit_->setChecked(setup_.fitToPaper);
    scaleCol->addWidget(fit_);
    scale_ = new QComboBox(scaleBox);
    for (const auto& choice : PageSetup::scaleChoices()) scale_->addItem(choice.first, choice.second);
    int atScale = -1;
    for (int i = 0; i < scale_->count(); ++i)
        if (std::abs(scale_->itemData(i).toDouble() - setup_.scale) < 1e-9) atScale = i;
    scale_->setCurrentIndex(atScale >= 0 ? atScale : 0);
    scale_->setEnabled(!setup_.fitToPaper);
    auto* scaleRow = new QHBoxLayout();
    scaleRow->addWidget(new QLabel(QStringLiteral("Scale:"), scaleBox));
    scaleRow->addWidget(scale_, 1);
    scaleCol->addLayout(scaleRow);
    auto* scaleNote = new QLabel(
        QStringLiteral("1:1 puts one millimetre of the sheet on one millimetre of paper, so a dimension "
                       "read off the plot with a rule is right."),
        scaleBox);
    scaleNote->setWordWrap(true);
    scaleNote->setMaximumWidth(340);
    scaleCol->addWidget(scaleNote);
    left->addWidget(scaleBox);

    // ---- offset ----------------------------------------------------------------------------------
    auto* offsetBox = new QGroupBox(QStringLiteral("Plot offset (from the printable area)"), this);
    auto* offsetForm = new QFormLayout(offsetBox);
    offsetX_ = new QDoubleSpinBox(offsetBox);
    offsetY_ = new QDoubleSpinBox(offsetBox);
    for (QDoubleSpinBox* box : {offsetX_, offsetY_}) {
        box->setRange(-2000.0, 2000.0);
        box->setDecimals(2);
        box->setSuffix(QStringLiteral(" mm"));
    }
    offsetX_->setValue(setup_.offsetXMm);
    offsetY_->setValue(setup_.offsetYMm);
    centre_ = new QCheckBox(QStringLiteral("Centre the plot"), offsetBox);
    centre_->setChecked(setup_.centred);
    offsetX_->setEnabled(!setup_.centred);
    offsetY_->setEnabled(!setup_.centred);
    offsetForm->addRow(QStringLiteral("X:"), offsetX_);
    offsetForm->addRow(QStringLiteral("Y:"), offsetY_);
    offsetForm->addRow(centre_);
    right->addWidget(offsetBox);

    // ---- orientation -----------------------------------------------------------------------------
    auto* orientBox = new QGroupBox(QStringLiteral("Drawing orientation"), this);
    auto* orientCol = new QVBoxLayout(orientBox);
    portrait_ = new QRadioButton(QStringLiteral("Portrait"), orientBox);
    landscape_ = new QRadioButton(QStringLiteral("Landscape"), orientBox);
    (setup_.landscape ? landscape_ : portrait_)->setChecked(true);
    upsideDown_ = new QCheckBox(QStringLiteral("Plot upside-down"), orientBox);
    upsideDown_->setChecked(setup_.upsideDown);
    orientCol->addWidget(portrait_);
    orientCol->addWidget(landscape_);
    orientCol->addWidget(upsideDown_);
    right->addWidget(orientBox);

    // ---- plot options ----------------------------------------------------------------------------
    auto* optionBox = new QGroupBox(QStringLiteral("Plot options"), this);
    auto* optionCol = new QVBoxLayout(optionBox);
    mono_ = new QCheckBox(QStringLiteral("Black lines (else the drawing's own colours)"), optionBox);
    mono_->setChecked(setup_.monochrome);
    lineweights_ = new QCheckBox(QStringLiteral("Plot object lineweights"), optionBox);
    lineweights_->setChecked(setup_.plotLineweights);
    paperLast_ = new QCheckBox(QStringLiteral("Plot paperspace last"), optionBox);
    paperLast_->setChecked(setup_.plotPaperSpaceLast);
    paperLast_->setToolTip(QStringLiteral(
        "What is drawn on the sheet goes on over the windows, so a note reads across the view it points "
        "at."));
    optionCol->addWidget(mono_);
    optionCol->addWidget(lineweights_);
    optionCol->addWidget(paperLast_);
    right->addWidget(optionBox);
    right->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    outer->addWidget(buttons);

    connect(paper_, &QComboBox::currentIndexChanged, this, [this] { refreshPaperDetail(); });
    connect(portrait_, &QRadioButton::toggled, this, [this] { refreshPaperDetail(); });
    connect(fit_, &QCheckBox::toggled, this, [this](bool on) {
        if (scale_) scale_->setEnabled(!on);
        refreshPaperDetail();
    });
    connect(centre_, &QCheckBox::toggled, this, [this](bool on) {
        offsetX_->setEnabled(!on);
        offsetY_->setEnabled(!on);
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    refreshPaperDetail();
}

void PageSetupDialog::refreshPaperDetail() {
    if (!paperDetail_) return;
    const PageSetup live = setup();
    const PaperSize& size = paperSizeByName(live.paper);
    const QString bleed =
        size.marginMm <= 0.0
            ? QStringLiteral("Full bleed: the ink goes to the paper's edge, so the sheet comes out at "
                             "true size. A printer with a mechanical margin will clip it; a plotter and "
                             "a PDF will not.")
            : QStringLiteral("A %1 mm margin all round is left unprinted, as an office printer needs.")
                  .arg(size.marginMm, 0, 'f', 1);
    paperDetail_->setText(QStringLiteral("Page %1 × %2 mm, printable %3 × %4 mm.\n%5")
                              .arg(live.pageWidthMm(), 0, 'f', 1)
                              .arg(live.pageHeightMm(), 0, 'f', 1)
                              .arg(live.printableWidthMm(), 0, 'f', 1)
                              .arg(live.printableHeightMm(), 0, 'f', 1)
                              .arg(bleed));
}

PageSetup PageSetupDialog::setup() const {
    PageSetup out = setup_;
    if (device_) out.device = device_->currentData().toString();
    if (paper_) out.paper = paper_->currentData().toString();
    if (area_) out.area = static_cast<PlotArea>(area_->currentData().toInt());
    if (fit_) out.fitToPaper = fit_->isChecked();
    if (scale_) out.scale = scale_->currentData().toDouble();
    if (offsetX_) out.offsetXMm = offsetX_->value();
    if (offsetY_) out.offsetYMm = offsetY_->value();
    if (centre_) out.centred = centre_->isChecked();
    if (landscape_) out.landscape = landscape_->isChecked();
    if (upsideDown_) out.upsideDown = upsideDown_->isChecked();
    if (mono_) out.monochrome = mono_->isChecked();
    if (lineweights_) out.plotLineweights = lineweights_->isChecked();
    if (paperLast_) out.plotPaperSpaceLast = paperLast_->isChecked();
    if (out.scale <= 0.0) out.scale = 1.0;
    return out;
}

} // namespace Edraw_Desktop
