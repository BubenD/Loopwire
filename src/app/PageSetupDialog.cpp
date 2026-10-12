#include "app/PageSetupDialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPrinterInfo>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include <cmath>

namespace Loopwire {

// The page, drawn small: the paper, the margin the printer will not reach, and the part of the sheet that
// will land on it. The point of it is that "full bleed", "fit to paper" and an offset stop being words and
// become a picture - a sheet hanging over the margin visibly hangs over the margin, before it is printed
// rather than after.
class PageSetupDialog::Preview : public QWidget {
public:
    // No Q_OBJECT: nothing here needs the meta-object, and the dialog keeps this one by its own type
    // rather than casting for it.
    explicit Preview(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(240, 200);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void showSetup(const PageSetup& setup, double sheetWmm, double sheetHmm) {
        setup_ = setup;
        sheetW_ = sheetWmm;
        sheetH_ = sheetHmm;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), palette().window());

        const double pageW = std::max(1.0, setup_.pageWidthMm());
        const double pageH = std::max(1.0, setup_.pageHeightMm());
        const QRectF room = QRectF(rect()).adjusted(26, 18, -18, -28);
        if (room.width() < 20.0 || room.height() < 20.0) return;
        const double s = std::min(room.width() / pageW, room.height() / pageH);
        const QRectF page(room.center().x() - pageW * s / 2.0, room.center().y() - pageH * s / 2.0,
                          pageW * s, pageH * s);

        p.setPen(QPen(QColor("#7a7f87"), 1.0));
        p.setBrush(Qt::white);
        p.drawRect(page);

        // The margin: what the printer will not put ink in. A full-bleed size has none, and then this line
        // sits on the paper's own edge - which is the whole of the difference, shown rather than described.
        const double margin = setup_.marginMm() * s;
        const QRectF printable = page.adjusted(margin, margin, -margin, -margin);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor("#aeb4bd"), 1.0, Qt::DashLine));
        p.drawRect(printable);

        // What will be printed: the sheet, or the window out of it, at the scale and offset set.
        if (sheetW_ > 0.0 && sheetH_ > 0.0) {
            double partW = sheetW_, partH = sheetH_;
            if (setup_.area == PlotArea::Window && setup_.windowValid()) {
                partW = std::max(1.0, std::abs(setup_.windowX2Mm - setup_.windowX1Mm));
                partH = std::max(1.0, std::abs(setup_.windowY2Mm - setup_.windowY1Mm));
            }
            const double plotScale =
                setup_.fitToPaper
                    ? std::min(setup_.printableWidthMm() / partW, setup_.printableHeightMm() / partH)
                    : setup_.scale;
            const double w = partW * plotScale * s, h = partH * plotScale * s;
            const QPointF centre =
                setup_.centred ? printable.center()
                               : QPointF(printable.left() + setup_.offsetXMm * s + w / 2.0,
                                         printable.bottom() - setup_.offsetYMm * s - h / 2.0);
            const QRectF plot(centre.x() - w / 2.0, centre.y() - h / 2.0, w, h);
            p.setBrush(QColor(61, 139, 253, 55));
            p.setPen(QPen(QColor("#3d8bfd"), 1.4));
            p.drawRect(plot);
            const bool over = plot.left() < printable.left() - 0.6 ||
                              plot.right() > printable.right() + 0.6 ||
                              plot.top() < printable.top() - 0.6 ||
                              plot.bottom() > printable.bottom() + 0.6;
            if (over) {
                // Bigger than what can be printed: it will be cut off there. Said here, where it can be
                // put right, rather than found out on paper.
                QFont f = p.font();
                f.setPixelSize(10);
                p.setFont(f);
                p.setPen(QColor("#d9534f"));
                p.drawText(QRectF(0, page.bottom() + 3, width(), 22), Qt::AlignHCenter | Qt::AlignTop,
                           QStringLiteral("exceeds the printable area — the plot will be clipped"));
            }
        }

        // How big the page is, along its edges: the preview answers "how big" as well as "where".
        QFont f = p.font();
        f.setPixelSize(10);
        p.setFont(f);
        p.setPen(QColor("#6f7681"));
        p.drawText(QRectF(0, page.top() - 15, width(), 14), Qt::AlignHCenter | Qt::AlignBottom,
                   QStringLiteral("%1 mm").arg(pageW, 0, 'f', 0));
        p.save();
        p.translate(page.left() - 7, page.center().y());
        p.rotate(-90);
        p.drawText(QRectF(-45, -12, 90, 12), Qt::AlignCenter,
                   QStringLiteral("%1 mm").arg(pageH, 0, 'f', 0));
        p.restore();
    }

private:
    PageSetup setup_;
    double sheetW_ = 0.0;
    double sheetH_ = 0.0;
};

PageSetupDialog::PageSetupDialog(const PageSetup& setup, const QString& title, double sheetWidthMm,
                                 double sheetHeightMm, QWidget* parent)
    : QDialog(parent), setup_(setup), sheetW_(sheetWidthMm), sheetH_(sheetHeightMm) {
    setWindowTitle(QStringLiteral("Page setup — %1").arg(title));
    setModal(true);

    auto* outer = new QVBoxLayout(this);

    // ---- the paper, with the page beside it ------------------------------------------------------
    auto* top = new QHBoxLayout();
    auto* preview = new Preview(this);
    preview_ = preview;
    top->addWidget(preview, 2);

    auto* paperSide = new QVBoxLayout();
    paperSide->addWidget(new QLabel(QStringLiteral("<b>Paper</b>"), this));
    paper_ = new QComboBox(this);
    // The plain sizes only, with a tick for the full-bleed twin. Every size listed twice over is where the
    // two get confused; this way the choice reads as what it is - a size, and whether the printer may go
    // right to its edges.
    for (const PaperSize& size : paperSizes()) {
        if (size.marginMm <= 0.0) continue;
        paper_->addItem(size.name, size.name);
    }
    paper_->setMaxVisibleItems(20);
    paper_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    paper_->setMinimumContentsLength(24);
    if (paper_->view()) paper_->view()->setMinimumWidth(320);
    bleed_ = new QCheckBox(QStringLiteral("Borderless — full bleed to the paper edge"), this);
    bleed_->setToolTip(QStringLiteral(
        "On: no margin, so the sheet comes out at true size — what a plotter or a PDF can do, and what a "
        "drawing that will be measured with a rule needs.\n"
        "Off: a margin is left unprinted, as an office printer needs."));
    {
        const PaperSize& current = paperSizeByName(setup_.paper);
        bleed_->setChecked(current.marginMm <= 0.0);
        QString plainName = setup_.paper;
        plainName.remove(QStringLiteral(" full bleed"));
        const int at = paper_->findData(plainName);
        paper_->setCurrentIndex(at >= 0 ? at : 0);
    }
    paperSide->addWidget(paper_);
    paperSide->addWidget(bleed_);

    auto* wayRound = new QHBoxLayout();
    landscape_ = new QRadioButton(QStringLiteral("Landscape"), this);
    portrait_ = new QRadioButton(QStringLiteral("Portrait"), this);
    // The dot has to be visible. On a dark theme the stock indicator is a dark ring on a dark ground and
    // the filled middle is barely there, so which way round the page goes cannot be read at a glance -
    // which is the one thing this pair exists to say. Drawn as a ring with a solid dot in it.
    const QString radioStyle = QStringLiteral(
        "QRadioButton{spacing:6px;}"
        "QRadioButton::indicator{width:14px;height:14px;border-radius:7px;"
        "border:1px solid #8a929c;background:#20262e;}"
        "QRadioButton::indicator:hover{border-color:#c8d0da;}"
        "QRadioButton::indicator:checked{border:1px solid #4a9eff;"
        "background:qradialgradient(cx:0.5,cy:0.5,radius:0.5,fx:0.5,fy:0.5,"
        "stop:0 #4a9eff,stop:0.55 #4a9eff,stop:0.6 #20262e,stop:1 #20262e);}");
    landscape_->setStyleSheet(radioStyle);
    portrait_->setStyleSheet(radioStyle);
    (setup_.landscape ? landscape_ : portrait_)->setChecked(true);
    wayRound->addWidget(landscape_);
    wayRound->addWidget(portrait_);
    wayRound->addStretch(1);
    paperSide->addLayout(wayRound);
    upsideDown_ = new QCheckBox(QStringLiteral("Rotate 180°"), this);
    upsideDown_->setChecked(setup_.upsideDown);
    paperSide->addWidget(upsideDown_);

    figures_ = new QLabel(this);
    figures_->setWordWrap(true);
    figures_->setMaximumWidth(330);
    figures_->setMinimumHeight(48);
    paperSide->addWidget(figures_);
    paperSide->addStretch(1);
    top->addLayout(paperSide, 3);
    outer->addLayout(top);

    // ---- what goes on the page --------------------------------------------------------------------
    auto* what = new QGroupBox(QStringLiteral("Plot area"), this);
    auto* whatGrid = new QGridLayout(what);
    area_ = new QComboBox(what);
    area_->addItem(QStringLiteral("Layout — the sheet at its own size"),
               static_cast<int>(PlotArea::Layout));
    area_->addItem(QStringLiteral("Extents — everything drawn on the sheet"),
               static_cast<int>(PlotArea::Extents));
    area_->addItem(QStringLiteral("Display — the current view"),
               static_cast<int>(PlotArea::Display));
    area_->addItem(QStringLiteral("Window — a defined rectangle"),
               static_cast<int>(PlotArea::Window));
    for (int i = 0; i < area_->count(); ++i)
        if (area_->itemData(i).toInt() == static_cast<int>(setup_.area)) area_->setCurrentIndex(i);
    windowBtn_ = new QPushButton(QStringLiteral("Pick window..."), what);
    windowBtn_->setToolTip(QStringLiteral(
        "This closes, you pick the two corners on the sheet with the snaps running, and it opens again "
        "with the window in it."));
    typeWindowBtn_ = new QPushButton(QStringLiteral("Window boundaries..."), what);
    typeWindowBtn_->setToolTip(
        QStringLiteral("Give the boundaries as figures, for a window that has to be exact."));
    windowLabel_ = new QLabel(what);
    windowLabel_->setWordWrap(true);
    whatGrid->addWidget(area_, 0, 0);
    whatGrid->addWidget(windowBtn_, 0, 1);
    whatGrid->addWidget(typeWindowBtn_, 0, 2);
    whatGrid->addWidget(windowLabel_, 1, 0, 1, 3);
    whatGrid->setColumnStretch(0, 1);
    outer->addWidget(what);

    // ---- size and position ------------------------------------------------------------------------
    auto* size = new QGroupBox(QStringLiteral("Plot scale and offset"), this);
    auto* sizeGrid = new QGridLayout(size);
    fit_ = new QCheckBox(QStringLiteral("Fit to paper"), size);
    fit_->setChecked(setup_.fitToPaper);
    scale_ = new QComboBox(size);
    for (const auto& choice : PageSetup::scaleChoices()) scale_->addItem(choice.first, choice.second);
    int atScale = -1;
    for (int i = 0; i < scale_->count(); ++i)
        if (std::abs(scale_->itemData(i).toDouble() - setup_.scale) < 1e-9) atScale = i;
    scale_->setCurrentIndex(atScale >= 0 ? atScale : 0);
    scale_->setEnabled(!setup_.fitToPaper);
    centre_ = new QCheckBox(QStringLiteral("Centre on the printable area"), size);
    centre_->setChecked(setup_.centred);
    offsetX_ = new QDoubleSpinBox(size);
    offsetY_ = new QDoubleSpinBox(size);
    for (QDoubleSpinBox* box : {offsetX_, offsetY_}) {
        box->setRange(-2000.0, 2000.0);
        box->setDecimals(2);
        box->setSuffix(QStringLiteral(" mm"));
        box->setEnabled(!setup_.centred);
    }
    offsetX_->setValue(setup_.offsetXMm);
    offsetY_->setValue(setup_.offsetYMm);
    sizeGrid->addWidget(new QLabel(QStringLiteral("Scale:"), size), 0, 0);
    sizeGrid->addWidget(scale_, 0, 1);
    sizeGrid->addWidget(fit_, 0, 2);
    sizeGrid->addWidget(centre_, 1, 0, 1, 3);
    sizeGrid->addWidget(new QLabel(QStringLiteral("Offset X:"), size), 2, 0);
    sizeGrid->addWidget(offsetX_, 2, 1);
    sizeGrid->addWidget(new QLabel(QStringLiteral("Offset Y:"), size), 3, 0);
    sizeGrid->addWidget(offsetY_, 3, 1);
    sizeGrid->setColumnStretch(2, 1);
    outer->addWidget(size);

    // ---- the machine and the ink ------------------------------------------------------------------
    auto* how = new QGroupBox(QStringLiteral("Output device and plot options"), this);
    auto* howGrid = new QGridLayout(how);
    device_ = new QComboBox(how);
    device_->addItem(QStringLiteral("PDF file"), QString());
    for (const QPrinterInfo& info : QPrinterInfo::availablePrinters())
        device_->addItem(info.printerName(), info.printerName());
    const int atDevice = device_->findData(setup_.device);
    device_->setCurrentIndex(atDevice >= 0 ? atDevice : 0);
    device_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    device_->setMinimumContentsLength(22);
    if (device_->view()) device_->view()->setMinimumWidth(280);
    mono_ = new QCheckBox(QStringLiteral("Monochrome"), how);
    mono_->setChecked(setup_.monochrome);
    mono_->setToolTip(QStringLiteral("Off: the drawing's own colours, on white paper."));
    lineweights_ = new QCheckBox(QStringLiteral("Plot lineweights"), how);
    lineweights_->setChecked(setup_.plotLineweights);
    lineweights_->setToolTip(QStringLiteral(
        "Off: every line the same thin one — a check print, where the geometry is what is wanted."));
    paperLast_ = new QCheckBox(QStringLiteral("Plot paper space last"), how);
    paperLast_->setChecked(setup_.plotPaperSpaceLast);
    paperLast_->setToolTip(QStringLiteral(
        "What is drawn on the sheet goes on last, so a note reads across the view it points at."));
    howGrid->addWidget(new QLabel(QStringLiteral("Device:"), how), 0, 0);
    howGrid->addWidget(device_, 0, 1, 1, 2);
    howGrid->addWidget(mono_, 1, 0);
    howGrid->addWidget(lineweights_, 1, 1);
    howGrid->addWidget(paperLast_, 1, 2);
    howGrid->setColumnStretch(2, 1);
    outer->addWidget(how);

    auto* buttons = new QDialogButtonBox(this);
    buttons->addButton(QStringLiteral("OK"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QStringLiteral("Cancel"), QDialogButtonBox::RejectRole);
    outer->addWidget(buttons);

    connect(paper_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(bleed_, &QCheckBox::toggled, this, [this] { refresh(); });
    connect(portrait_, &QRadioButton::toggled, this, [this] { refresh(); });
    connect(upsideDown_, &QCheckBox::toggled, this, [this] { refresh(); });
    connect(area_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(windowBtn_, &QPushButton::clicked, this, [this] { pickWindow(); });
    connect(typeWindowBtn_, &QPushButton::clicked, this, [this] { typeWindow(); });
    connect(scale_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(fit_, &QCheckBox::toggled, this, [this](bool on) {
        if (scale_) scale_->setEnabled(!on);
        refresh();
    });
    connect(centre_, &QCheckBox::toggled, this, [this](bool on) {
        offsetX_->setEnabled(!on);
        offsetY_->setEnabled(!on);
        refresh();
    });
    connect(offsetX_, &QDoubleSpinBox::valueChanged, this, [this] { refresh(); });
    connect(offsetY_, &QDoubleSpinBox::valueChanged, this, [this] { refresh(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    refresh();
}

void PageSetupDialog::applyPickedWindow(double x1, double y1, double x2, double y2) {
    setup_.windowX1Mm = std::min(x1, x2);
    setup_.windowY1Mm = std::min(y1, y2);
    setup_.windowX2Mm = std::max(x1, x2);
    setup_.windowY2Mm = std::max(y1, y2);
    setup_.area = PlotArea::Window;
    if (area_)
        for (int i = 0; i < area_->count(); ++i)
            if (area_->itemData(i).toInt() == static_cast<int>(PlotArea::Window))
                area_->setCurrentIndex(i);
    refresh();
}

void PageSetupDialog::pickWindow() {
    // Out of the way, and pick it off the sheet. A plot window is a rectangle ON the drawing, so it is
    // pointed at rather than typed: the dialog closes, the corners are picked with every snap running, and
    // the dialog comes back with the window in it. Typed corners are still there, under Type it instead,
    // for a window that has to be an exact figure.
    setup_ = setup();               // keep everything else the user has set
    done(PickWindowResult);
}

void PageSetupDialog::typeWindow() {
    // The corners, in the sheet's own millimetres. Typed rather than dragged: this dialog stands in front
    // of the drawing, and a window that has to be exact - one view of three, a title block on its own - is
    // a figure anyway. The whole sheet is one button away, for when the window was a mistake.
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Plot window"));
    auto* form = new QFormLayout(&dlg);
    auto* x1 = new QDoubleSpinBox(&dlg);
    auto* y1 = new QDoubleSpinBox(&dlg);
    auto* x2 = new QDoubleSpinBox(&dlg);
    auto* y2 = new QDoubleSpinBox(&dlg);
    const double maxW = sheetW_ > 0.0 ? sheetW_ : 4000.0;
    const double maxH = sheetH_ > 0.0 ? sheetH_ : 4000.0;
    for (QDoubleSpinBox* box : {x1, y1, x2, y2}) {
        box->setRange(-4000.0, 4000.0);
        box->setDecimals(2);
        box->setSuffix(QStringLiteral(" mm"));
    }
    const PageSetup live = setup();
    x1->setValue(live.windowValid() ? live.windowX1Mm : 0.0);
    y1->setValue(live.windowValid() ? live.windowY1Mm : 0.0);
    x2->setValue(live.windowValid() ? live.windowX2Mm : maxW);
    y2->setValue(live.windowValid() ? live.windowY2Mm : maxH);
    form->addRow(QStringLiteral("Left:"), x1);
    form->addRow(QStringLiteral("Bottom:"), y1);
    form->addRow(QStringLiteral("Right:"), x2);
    form->addRow(QStringLiteral("Top:"), y2);
    auto* whole = new QPushButton(QStringLiteral("Entire sheet"), &dlg);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->addButton(whole, QDialogButtonBox::ResetRole);
    form->addRow(buttons);
    connect(whole, &QPushButton::clicked, &dlg, [&] {
        x1->setValue(0.0);
        y1->setValue(0.0);
        x2->setValue(maxW);
        y2->setValue(maxH);
    });
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    setup_.windowX1Mm = std::min(x1->value(), x2->value());
    setup_.windowY1Mm = std::min(y1->value(), y2->value());
    setup_.windowX2Mm = std::max(x1->value(), x2->value());
    setup_.windowY2Mm = std::max(y1->value(), y2->value());
    // Setting a window is asking for the window to be what prints.
    if (area_)
        for (int i = 0; i < area_->count(); ++i)
            if (area_->itemData(i).toInt() == static_cast<int>(PlotArea::Window))
                area_->setCurrentIndex(i);
    refresh();
}

void PageSetupDialog::refresh() {
    const PageSetup live = setup();
    if (preview_)
        preview_->showSetup(live, sheetW_ > 0.0 ? sheetW_ : live.pageWidthMm(),
                           sheetH_ > 0.0 ? sheetH_ : live.pageHeightMm());
    const bool windowArea = live.area == PlotArea::Window;
    // Only when a window is what is being plotted. Offering "pick a window" while the whole sheet is being
    // plotted is offering something that would have no effect, which is worse than not offering it.
    if (windowBtn_) windowBtn_->setVisible(windowArea);
    if (typeWindowBtn_) typeWindowBtn_->setVisible(windowArea);
    if (windowLabel_) {
        const bool window = windowArea;
        if (!window)
            windowLabel_->setText(
                live.area == PlotArea::Display
                    ? QStringLiteral("The view on screen at the moment of plotting.")
                    : live.area == PlotArea::Extents
                          ? QStringLiteral("Everything on the sheet, and no more of the paper "
                                           "than that needs.")
                          : QStringLiteral("The sheet as laid out, edge to edge."));
        else if (live.windowValid())
            windowLabel_->setText(QStringLiteral("%1 × %2 mm of the sheet: (%3, %4) to (%5, %6).")
                                      .arg(live.windowX2Mm - live.windowX1Mm, 0, 'f', 1)
                                      .arg(live.windowY2Mm - live.windowY1Mm, 0, 'f', 1)
                                      .arg(live.windowX1Mm, 0, 'f', 1)
                                      .arg(live.windowY1Mm, 0, 'f', 1)
                                      .arg(live.windowX2Mm, 0, 'f', 1)
                                      .arg(live.windowY2Mm, 0, 'f', 1));
        else
            windowLabel_->setText(
                QStringLiteral("No window yet — the whole sheet will plot. Pick one on the sheet."));
    }
    if (figures_) {
        const PaperSize& size = paperSizeByName(live.paper);
        const double sheetW = sheetW_ > 0.0 ? sheetW_ : live.pageWidthMm();
        const double sheetH = sheetH_ > 0.0 ? sheetH_ : live.pageHeightMm();
        const double takenW = live.fitToPaper ? std::min(sheetW, live.printableWidthMm())
                                              : sheetW * live.scale;
        const double takenH = live.fitToPaper ? std::min(sheetH, live.printableHeightMm())
                                              : sheetH * live.scale;
        figures_->setText(
            QStringLiteral("Sheet size %1 × %2 mm; %3.\nAt %4 the plot measures %5 × %6 mm.")
                .arg(live.pageWidthMm(), 0, 'f', 1)
                .arg(live.pageHeightMm(), 0, 'f', 1)
                .arg(size.marginMm <= 0.0
                         ? QStringLiteral("borderless, printable to the paper edge")
                         : QStringLiteral("printable area %1 × %2 mm, %3 mm unprintable margin")
                               .arg(live.printableWidthMm(), 0, 'f', 1)
                               .arg(live.printableHeightMm(), 0, 'f', 1)
                               .arg(size.marginMm, 0, 'f', 1))
                .arg(live.scaleLabel())
                .arg(takenW, 0, 'f', 1)
                .arg(takenH, 0, 'f', 1));
    }
}

PageSetup PageSetupDialog::setup() const {
    PageSetup out = setup_;
    if (device_) out.device = device_->currentData().toString();
    if (paper_) {
        // The plain name off the list, turned into its full-bleed twin when that is what was ticked.
        QString name = paper_->currentData().toString();
        if (bleed_ && bleed_->isChecked()) {
            QString bleedName = name;
            for (const QString& series :
                 {QStringLiteral("ISO "), QStringLiteral("ANSI "), QStringLiteral("ARCH ")})
                if (bleedName.startsWith(series)) {
                    bleedName.insert(series.size(), QStringLiteral("full bleed "));
                    break;
                }
            if (bleedName == name) {   // Letter / Legal, which have no series word
                const int bracket = bleedName.indexOf(QStringLiteral(" ("));
                if (bracket > 0) bleedName.insert(bracket, QStringLiteral(" full bleed"));
            }
            if (paperSizeByName(bleedName).name == bleedName) name = bleedName;
        }
        out.paper = name;
    }
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

} // namespace Loopwire
