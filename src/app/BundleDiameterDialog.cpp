#include "app/BundleDiameterDialog.h"

#include "app/UiBits.h"
#include "app/Editor.h"
#include "model/Document.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace Edraw_Desktop {
namespace {
constexpr int kColLabel = 0, kColGauge = 1, kColOd = 2;
}

BundleDiameterDialog::BundleDiameterDialog(Editor* editor, QWidget* parent)
    : QDialog(parent), editor_(editor) {
    setWindowTitle(QStringLiteral("Bundle diameter"));
    resize(880, 720);
    buildUi();
    readFromDrawing();
}

void BundleDiameterDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

    // ---- The formula itself, stated before anything is put into it ----
    auto* formula = new QLabel(this);
    formula->setTextFormat(Qt::RichText);
    formula->setText(QStringLiteral(
        "<div style='font-size:15px;'><b>D<sub>bundle</sub></b> &nbsp;=&nbsp; "
        "&radic;<span style='text-decoration:overline;'>&nbsp;&Sigma; D<sup>2</sup><sub>wire</sub>"
        "&nbsp;</span> &nbsp;&times;&nbsp; <b>k<sub>packing</sub></b></div>"));
    formula->setStyleSheet(QStringLiteral(
        "color:#d7dade;background:#2b2e33;border:1px solid #4a4e55;padding:10px;"));
    formula->setAlignment(Qt::AlignCenter);
    root->addWidget(formula);

    // ---- The conductors ----
    auto* wireBox = new QGroupBox(QStringLiteral("The conductors in this bundle"), this);
    auto* wireLay = new QVBoxLayout(wireBox);
    wires_ = new QTableWidget(wireBox);
    wires_->setColumnCount(3);
    wires_->setHorizontalHeaderLabels({QStringLiteral("Wire"), QStringLiteral("Cross-section"),
                                       QStringLiteral("Ø over insulation (mm)")});
    wires_->horizontalHeader()->setStretchLastSection(true);
    wires_->verticalHeader()->setVisible(false);
    wires_->setToolTip(QStringLiteral(
        "The diameter is what the calculation uses. Leave it and a cross-section fills it in from the "
        "nominal thin-wall figures; type over it when the data sheet says something else."));
    connect(wires_, &QTableWidget::cellChanged, this, [this](int row, int col) {
        if (syncing_) return;
        if (col == kColGauge) {
            // A cross-section typed in gives the diameter, unless one has been typed over it.
            const QString gauge = wires_->item(row, kColGauge)->text();
            const double od = wireOuterDiameterMm(gauge);
            QTableWidgetItem* odItem = wires_->item(row, kColOd);
            if (od > 0.0 && odItem && odItem->text().trimmed().isEmpty()) {
                syncing_ = true;
                odItem->setText(QString::number(od, 'f', 2));
                syncing_ = false;
            }
        }
        recalculate();
    });
    wireLay->addWidget(wires_);

    auto* btns = new QHBoxLayout;
    auto* readBtn = new QPushButton(QStringLiteral("Read from the drawing"), wireBox);
    readBtn->setToolTip(QStringLiteral("Every wire on the sheet, with the gauge it is drawn with"));
    connect(readBtn, &QPushButton::clicked, this, [this] { readFromDrawing(); });
    btns->addWidget(readBtn);
    auto* addBtn = makeAddButton(wireBox, QStringLiteral("Conductor"));
    connect(addBtn, &QPushButton::clicked, this, [this] {
        addRow(QStringLiteral("wire %1").arg(wires_->rowCount() + 1), QStringLiteral("0.5"), 0.0);
        recalculate();
    });
    btns->addWidget(addBtn);
    auto* dupBtn = new QPushButton(QStringLiteral("Duplicate ×5"), wireBox);
    dupBtn->setToolTip(QStringLiteral("Five more of the selected conductor - a quick way to try a "
                                      "bundle of a given make-up"));
    connect(dupBtn, &QPushButton::clicked, this, [this] {
        const int row = wires_->currentRow();
        if (row < 0) return;
        const QString label = wires_->item(row, kColLabel)->text();
        const QString gauge = wires_->item(row, kColGauge)->text();
        const double od = wires_->item(row, kColOd)->text().toDouble();
        for (int i = 0; i < 5; ++i) addRow(label, gauge, od);
        recalculate();
    });
    btns->addWidget(dupBtn);
    auto* rmBtn = new QPushButton(QStringLiteral("Remove"), wireBox);
    connect(rmBtn, &QPushButton::clicked, this, [this] {
        const int row = wires_->currentRow();
        if (row < 0) return;
        wires_->removeRow(row);
        recalculate();
    });
    btns->addWidget(rmBtn);
    btns->addStretch(1);
    wireLay->addLayout(btns);
    root->addWidget(wireBox);

    // ---- k_packing ----
    auto* kBox = new QGroupBox(QStringLiteral("k_packing — the allowance for the air between them"),
                              this);
    auto* kLay = new QHBoxLayout(kBox);
    autoK_ = new QCheckBox(QStringLiteral("Work it out from the count"), kBox);
    autoK_->setChecked(true);
    autoK_->setToolTip(QStringLiteral(
        "A handful of conductors cannot help leaving air between them, so they pack loosely; a big "
        "bundle settles down. 1.25 at the small end, 1.15 at the large one."));
    connect(autoK_, &QCheckBox::toggled, this, [this](bool on) {
        if (kSpin_) kSpin_->setEnabled(!on);
        recalculate();
    });
    kLay->addWidget(autoK_);
    kSpin_ = new QDoubleSpinBox(kBox);
    kSpin_->setRange(1.0, 1.6);
    kSpin_->setDecimals(3);
    kSpin_->setSingleStep(0.01);
    kSpin_->setValue(1.20);
    kSpin_->setEnabled(false);
    kSpin_->setToolTip(QStringLiteral("1.15 to 1.25 covers ordinary practice; more than that means "
                                      "the bundle is not round or not settled."));
    connect(kSpin_, &QDoubleSpinBox::valueChanged, this, [this](double) { recalculate(); });
    kLay->addWidget(kSpin_);
    kNote_ = new QLabel(QString(), kBox);
    kNote_->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
    kLay->addWidget(kNote_, 1);
    root->addWidget(kBox);

    result_ = new QLabel(QString(), this);
    result_->setStyleSheet(QStringLiteral("color:#8fb0d6;font-size:15px;font-weight:600;"));
    root->addWidget(result_);

    working_ = new QTextBrowser(this);
    working_->setOpenExternalLinks(false);
    root->addWidget(working_, 1);

    auto* foot = new QHBoxLayout;
    foot->addStretch(1);
    auto* copyBtn = new QPushButton(QStringLiteral("Copy the working"), this);
    copyBtn->setToolTip(QStringLiteral("The calculation as text, to paste into a report"));
    connect(copyBtn, &QPushButton::clicked, this, [this] {
        if (working_) {
            working_->selectAll();
            working_->copy();
            working_->moveCursor(QTextCursor::Start);
        }
    });
    foot->addWidget(copyBtn);
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    foot->addWidget(closeBtn);
    root->addLayout(foot);
}

void BundleDiameterDialog::addRow(const QString& label, const QString& gauge, double od) {
    const bool was = syncing_;
    syncing_ = true;
    const int row = wires_->rowCount();
    wires_->insertRow(row);
    wires_->setItem(row, kColLabel, new QTableWidgetItem(label));
    wires_->setItem(row, kColGauge, new QTableWidgetItem(gauge));
    const double use = od > 0.0 ? od : wireOuterDiameterMm(gauge);
    wires_->setItem(row, kColOd,
                    new QTableWidgetItem(use > 0.0 ? QString::number(use, 'f', 2) : QString()));
    syncing_ = was;
}

void BundleDiameterDialog::readFromDrawing() {
    if (!editor_ || !editor_->document()) return;
    syncing_ = true;
    wires_->setRowCount(0);
    for (const Entity& e : editor_->document()->entities()) {
        if (e.type != EntityType::Wire) continue;
        QString label = e.meta.value(QStringLiteral("wireLabel")).toString();
        if (label.isEmpty()) label = e.id;
        QString gauge = e.meta.value(QStringLiteral("wireGauge")).toString();
        if (gauge.isEmpty()) gauge = e.meta.value(QStringLiteral("awg")).toString();
        addRow(label, gauge, 0.0);
    }
    syncing_ = false;
    recalculate();
}

std::vector<double> BundleDiameterDialog::wireDiameters() const {
    std::vector<double> ods;
    for (int r = 0; r < wires_->rowCount(); ++r) {
        const QTableWidgetItem* it = wires_->item(r, kColOd);
        ods.push_back(it ? it->text().toDouble() : 0.0);
    }
    return ods;
}

void BundleDiameterDialog::setWireDiameters(const std::vector<double>& ods) {
    syncing_ = true;
    wires_->setRowCount(0);
    int n = 0;
    for (double od : ods) addRow(QStringLiteral("wire %1").arg(++n), QString(), od);
    syncing_ = false;
    recalculate();
}

void BundleDiameterDialog::setPackingFactor(double k) {
    syncing_ = true;
    if (autoK_) autoK_->setChecked(k <= 0.0);
    if (kSpin_) {
        kSpin_->setEnabled(k > 0.0);
        if (k > 0.0) kSpin_->setValue(k);
    }
    syncing_ = false;
    recalculate();
}

void BundleDiameterDialog::recalculate() {
    const bool autoK = !autoK_ || autoK_->isChecked();
    calc_ = bundleCalculation(wireDiameters(), autoK ? -1.0 : kSpin_->value(), 0.0);
    if (kNote_) {
        kNote_->setText(
            autoK ? QStringLiteral("%1 for %2 conductor(s) — the range is 1.15 (big bundle) to "
                                   "1.25 (a handful)")
                        .arg(calc_.packingFactor, 0, 'f', 2)
                        .arg(calc_.counted)
                  : QStringLiteral("entered by hand%1")
                        .arg(calc_.packingFactor < 1.15 || calc_.packingFactor > 1.25
                                 ? QStringLiteral(" — outside the usual 1.15 to 1.25")
                                 : QString()));
    }
    if (autoK && kSpin_) {
        syncing_ = true;
        kSpin_->setValue(calc_.packingFactor);
        syncing_ = false;
    }
    refreshWorking();
}

void BundleDiameterDialog::refreshWorking() {
    if (result_) {
        result_->setText(
            calc_.counted == 0
                ? QStringLiteral("Nothing to work out yet — add the conductors.")
                : QStringLiteral("D_bundle = %1 mm   (%2 conductor(s), %3)")
                      .arg(calc_.result, 0, 'f', 2)
                      .arg(calc_.counted)
                      .arg(calc_.rule == QLatin1String("random-lay")
                               ? QStringLiteral("random lay")
                               : QStringLiteral("exact geometry for %1").arg(calc_.rule)));
    }
    if (!working_) return;
    QString html = QStringLiteral(
        "<style>body{color:#d7dade;} h4{color:#8fb0d6;margin-bottom:4px;} "
        "pre{background:#2b2e33;border:1px solid #4a4e55;padding:8px;color:#d7dade;} "
        "p{color:#c3c9d1;}</style>");
    html += QStringLiteral("<h4>The working</h4><pre>");
    for (const QString& line : calc_.workings) html += line.toHtmlEscaped() + QStringLiteral("\n");
    html += QStringLiteral("</pre>");
    html += QStringLiteral("<h4>What it means, and where it stops</h4>");
    for (const QString& para : calc_.explanation().split(QStringLiteral("\n\n")))
        html += QStringLiteral("<p>%1</p>").arg(para.toHtmlEscaped());
    working_->setHtml(html);
}

} // namespace Edraw_Desktop
