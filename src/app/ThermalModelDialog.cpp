#include "app/ThermalModelDialog.h"

#include "app/UiBits.h"
#include "app/Editor.h"
#include "harness/MechanicalHarness.h"
#include "model/Document.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace Edraw_Desktop {
namespace {
constexpr int kColLabel = 0, kColMm2 = 1, kColAmps = 2, kColLen = 3, kColRating = 4;

QLabel* caption(QWidget* parent, const QString& text) {
    auto* l = new QLabel(text, parent);
    l->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
    return l;
}
} // namespace

ThermalModelDialog::ThermalModelDialog(Editor* editor, QWidget* parent)
    : QDialog(parent), editor_(editor) {
    setWindowTitle(QStringLiteral("Lumped thermal model"));
    resize(1120, 800);
    buildUi();
    readFromDrawing();
}

void ThermalModelDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

    // ---- The model, stated before anything is put into it ----
    auto* formula = new QLabel(this);
    formula->setTextFormat(Qt::RichText);
    formula->setText(QStringLiteral(
        "<div style='font-size:13px;line-height:150%;'>"
        "<b>P</b> = &Sigma; I<sup>2</sup> &middot; R(T) &nbsp;&nbsp;&nbsp; "
        "<b>R(T)</b> = R<sub>20</sub> &middot; [1 + &alpha;<sub>20</sub>(T &minus; 20)] &middot; L "
        "&middot; k<sub>strand</sub><br>"
        "<b>&Delta;T</b> = P &middot; R<sub>th,total</sub> &nbsp;&nbsp;&nbsp; "
        "<b>T</b> = T<sub>ambient</sub> + &Delta;T &nbsp;&nbsp;&nbsp;&rarr; and round again until it "
        "stops moving<br>"
        "<b>R<sub>th,total</sub></b> = R<sub>th,bundle</sub> + R<sub>th,enclosure</sub> + "
        "R<sub>th,ambient</sub>"
        "</div>"));
    formula->setStyleSheet(QStringLiteral(
        "color:#d7dade;background:#2b2e33;border:1px solid #4a4e55;padding:10px;"));
    root->addWidget(formula);

    auto* split = new QSplitter(Qt::Horizontal, this);

    // ---- The conductors and the conditions ----
    auto* left = new QWidget(split);
    auto* leftLay = new QVBoxLayout(left);
    leftLay->setContentsMargins(0, 0, 0, 0);

    auto* wireBox = new QGroupBox(QStringLiteral("The conductors sharing this bundle"), left);
    auto* wireLay = new QVBoxLayout(wireBox);
    wires_ = new QTableWidget(wireBox);
    wires_->setColumnCount(5);
    wires_->setHorizontalHeaderLabels({QStringLiteral("Wire"), QStringLiteral("mm²"),
                                       QStringLiteral("Current (A)"), QStringLiteral("Length (m)"),
                                       QStringLiteral("Insulation (°C)")});
    wires_->horizontalHeader()->setStretchLastSection(true);
    wires_->verticalHeader()->setVisible(false);
    wires_->setToolTip(QStringLiteral(
        "The current is what each one actually carries at the same time - not its rating. Leave the "
        "insulation blank and the target below is used instead, and reported as a gap."));
    connect(wires_, &QTableWidget::cellChanged, this, [this](int, int) {
        if (!syncing_) recalculate();
    });
    wireLay->addWidget(wires_);
    auto* btns = new QHBoxLayout;
    auto* readBtn = new QPushButton(QStringLiteral("Read from the drawing"), wireBox);
    connect(readBtn, &QPushButton::clicked, this, [this] { readFromDrawing(); });
    btns->addWidget(readBtn);
    auto* addBtn = makeAddButton(wireBox, QStringLiteral("Conductor"));
    connect(addBtn, &QPushButton::clicked, this, [this] {
        ThWire w;
        w.id = QStringLiteral("w%1").arg(wires_->rowCount() + 1);
        w.label = w.id;
        w.mm2 = 0.5;
        w.currentA = 1.0;
        w.lengthM = lengthSpin_ ? lengthSpin_->value() : 1.0;
        addRow(w);
        recalculate();
    });
    btns->addWidget(addBtn);
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
    leftLay->addWidget(wireBox, 1);

    auto* condBox = new QGroupBox(QStringLiteral("Where it sits, and how the heat gets out"), left);
    auto* grid = new QGridLayout(condBox);
    int r = 0;
    auto spin = [&](double lo, double hi, double val, const QString& suffix, int dp) {
        auto* s = new QDoubleSpinBox(condBox);
        s->setRange(lo, hi);
        s->setDecimals(dp);
        s->setValue(val);
        s->setSuffix(suffix);
        connect(s, &QDoubleSpinBox::valueChanged, this, [this](double) {
            if (!syncing_) recalculate();
        });
        return s;
    };
    ambientSpin_ = spin(-60.0, 250.0, 25.0, QStringLiteral(" °C"), 1);
    targetSpin_ = spin(20.0, 400.0, 105.0, QStringLiteral(" °C"), 0);
    targetSpin_->setToolTip(QStringLiteral(
        "What it is measured against when no wire states an insulation rating of its own."));
    grid->addWidget(caption(condBox, QStringLiteral("Ambient")), r, 0);
    grid->addWidget(ambientSpin_, r, 1);
    grid->addWidget(caption(condBox, QStringLiteral("Target limit")), r, 2);
    grid->addWidget(targetSpin_, r++, 3);

    mountingBox_ = new QComboBox(condBox);
    for (ThMounting m : {ThMounting::FreeAir, ThMounting::Bundled, ThMounting::InConduit,
                         ThMounting::InEnclosure})
        mountingBox_->addItem(thMountingName(m), static_cast<int>(m));
    mountingBox_->setCurrentIndex(1);
    mountingBox_->setToolTip(QStringLiteral(
        "Free air loses heat off the whole surface; a bundle only off its outside; conduit and an "
        "enclosure put another resistance in series."));
    connect(mountingBox_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!syncing_) recalculate();
    });
    lengthSpin_ = spin(0.01, 1000.0, 1.0, QStringLiteral(" m"), 3);
    lengthSpin_->setToolTip(QStringLiteral(
        "The length considered. 1 m makes this a per-meter answer, which is the usual way to ask."));
    grid->addWidget(caption(condBox, QStringLiteral("Mounting")), r, 0);
    grid->addWidget(mountingBox_, r, 1);
    grid->addWidget(caption(condBox, QStringLiteral("Length")), r, 2);
    grid->addWidget(lengthSpin_, r++, 3);

    hSpin_ = spin(1.0, 200.0, 10.0, QStringLiteral(" W/m²K"), 1);
    hSpin_->setToolTip(QStringLiteral(
        "Convection and radiation off the surface together. Still air round a small cylinder is about "
        "8 to 12; moving air is far more."));
    kSpin_ = spin(0.02, 5.0, 0.25, QStringLiteral(" W/mK"), 2);
    kSpin_->setToolTip(QStringLiteral(
        "Through the bundle itself - copper, polymer and air together, which is about 0.25."));
    grid->addWidget(caption(condBox, QStringLiteral("Surface h")), r, 0);
    grid->addWidget(hSpin_, r, 1);
    grid->addWidget(caption(condBox, QStringLiteral("Bundle k")), r, 2);
    grid->addWidget(kSpin_, r++, 3);

    bundleSpin_ = spin(0.0, 200.0, 0.0, QStringLiteral(" mm"), 2);
    bundleSpin_->setToolTip(QStringLiteral(
        "The bundle's outside diameter. Leave it at 0 and it is worked out from the conductors, the "
        "same way the bundle-diameter tool does it."));
    grid->addWidget(caption(condBox, QStringLiteral("Bundle Ø")), r, 0);
    grid->addWidget(bundleSpin_, r++, 1);
    leftLay->addWidget(condBox);
    split->addWidget(left);

    // ---- The answer ----
    auto* right = new QWidget(split);
    auto* rightLay = new QVBoxLayout(right);
    rightLay->setContentsMargins(0, 0, 0, 0);
    summary_ = new QLabel(QString(), right);
    summary_->setWordWrap(true);
    summary_->setStyleSheet(QStringLiteral("color:#8fb0d6;font-weight:600;font-size:14px;"));
    rightLay->addWidget(summary_);
    advice_ = new QLabel(QString(), right);
    advice_->setWordWrap(true);
    advice_->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
    rightLay->addWidget(advice_);

    auto* stepBox = new QGroupBox(QStringLiteral("The loop settling"), right);
    auto* stepLay = new QVBoxLayout(stepBox);
    steps_ = new QTableWidget(stepBox);
    steps_->setColumnCount(4);
    steps_->setHorizontalHeaderLabels({QStringLiteral("Round"), QStringLiteral("T (°C)"),
                                       QStringLiteral("P (W)"), QStringLiteral("moved (K)")});
    steps_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    steps_->verticalHeader()->setVisible(false);
    steps_->setMaximumHeight(150);
    stepLay->addWidget(steps_);
    rightLay->addWidget(stepBox);

    working_ = new QTextBrowser(right);
    rightLay->addWidget(working_, 1);
    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 4);
    root->addWidget(split, 1);

    auto* foot = new QHBoxLayout;
    foot->addStretch(1);
    auto* copyBtn = new QPushButton(QStringLiteral("Copy the working"), this);
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

void ThermalModelDialog::addRow(const ThWire& w) {
    const bool was = syncing_;
    syncing_ = true;
    const int row = wires_->rowCount();
    wires_->insertRow(row);
    wires_->setItem(row, kColLabel, new QTableWidgetItem(w.label.isEmpty() ? w.id : w.label));
    wires_->setItem(row, kColMm2,
                    new QTableWidgetItem(w.mm2 > 0.0 ? QString::number(w.mm2, 'f', 2) : QString()));
    wires_->setItem(row, kColAmps, new QTableWidgetItem(QString::number(w.currentA, 'f', 2)));
    wires_->setItem(row, kColLen, new QTableWidgetItem(QString::number(w.lengthM, 'f', 3)));
    wires_->setItem(row, kColRating,
                    new QTableWidgetItem(w.insulationRatingC > 0.0
                                             ? QString::number(w.insulationRatingC, 'f', 0)
                                             : QString()));
    syncing_ = was;
}

void ThermalModelDialog::readFromDrawing() {
    if (!editor_ || !editor_->document()) return;
    syncing_ = true;
    wires_->setRowCount(0);
    for (const Entity& e : editor_->document()->entities()) {
        if (e.type != EntityType::Wire) continue;
        ThWire w;
        w.id = e.id;
        w.label = e.meta.value(QStringLiteral("wireLabel")).toString();
        if (w.label.isEmpty()) w.label = e.id;
        w.mm2 = e.meta.value(QStringLiteral("wireGauge")).toString().toDouble();
        w.lengthM = lengthSpin_ ? lengthSpin_->value() : 1.0;
        // What it carries is not on the drawing, and must not be invented: it starts at nothing and
        // the currents are put in here.
        w.currentA = 0.0;
        addRow(w);
    }
    syncing_ = false;
    recalculate();
}

std::vector<ThWire> ThermalModelDialog::wiresFromTable() const {
    std::vector<ThWire> out;
    for (int r = 0; r < wires_->rowCount(); ++r) {
        ThWire w;
        w.id = QStringLiteral("r%1").arg(r);
        auto text = [this, r](int col) {
            const QTableWidgetItem* it = wires_->item(r, col);
            return it ? it->text().trimmed() : QString();
        };
        w.label = text(kColLabel);
        w.mm2 = text(kColMm2).toDouble();
        w.currentA = text(kColAmps).toDouble();
        w.lengthM = text(kColLen).toDouble();
        if (w.lengthM <= 0.0) w.lengthM = lengthSpin_ ? lengthSpin_->value() : 1.0;
        w.insulationRatingC = text(kColRating).toDouble();
        out.push_back(w);
    }
    return out;
}

ThRequest ThermalModelDialog::buildRequest() const {
    ThRequest req;
    req.wires = wiresFromTable();
    req.ambientC = ambientSpin_ ? ambientSpin_->value() : 25.0;
    req.targetMaxC = targetSpin_ ? targetSpin_->value() : 105.0;
    req.mounting = static_cast<ThMounting>(
        mountingBox_ ? mountingBox_->currentData().toInt() : static_cast<int>(ThMounting::Bundled));
    req.surfaceCoefficient = hSpin_ ? hSpin_->value() : 10.0;
    req.bundleConductivity = kSpin_ ? kSpin_->value() : 0.25;
    req.bundleDiameterMm = bundleSpin_ ? bundleSpin_->value() : 0.0;
    return req;
}

void ThermalModelDialog::setWires(const std::vector<ThWire>& wires) {
    syncing_ = true;
    wires_->setRowCount(0);
    for (const ThWire& w : wires) addRow(w);
    syncing_ = false;
    recalculate();
}

void ThermalModelDialog::setAmbient(double c) {
    if (ambientSpin_) ambientSpin_->setValue(c);
}

void ThermalModelDialog::setMounting(ThMounting m) {
    if (!mountingBox_) return;
    const int at = mountingBox_->findData(static_cast<int>(m));
    if (at >= 0) mountingBox_->setCurrentIndex(at);
}

void ThermalModelDialog::recalculate() {
    const ThRequest req = buildRequest();
    report_ = analyzeThermal(req);
    refreshResults();
}

void ThermalModelDialog::refreshResults() {
    if (summary_) {
        summary_->setText(report_.summary());
        summary_->setStyleSheet(report_.exceedsLimit()
                                    ? QStringLiteral("color:#e06c6c;font-weight:600;font-size:14px;")
                                    : QStringLiteral("color:#8fb0d6;font-weight:600;font-size:14px;"));
    }

    // What to do about it: how much it could carry, and what size would take the currents asked for.
    if (advice_) {
        if (!report_.solved) {
            advice_->setText(QString());
        } else {
            const ThRequest req = buildRequest();
            const double maxA = thermalMaxCurrentA(req, report_.limitC);
            const double mm2 = thermalSuggestedMm2(req, report_.limitC);
            QStringList bits;
            bits << QStringLiteral("At this make-up the largest conductor could carry up to %1 A "
                                   "before reaching %2 °C.")
                        .arg(maxA, 0, 'f', 1)
                        .arg(report_.limitC, 0, 'f', 0);
            if (report_.exceedsLimit()) {
                bits << (mm2 > 0.0 ? QStringLiteral("At the currents asked for, %1 mm² would be "
                                                    "needed to stay under it.")
                                         .arg(mm2, 0, 'f', 2)
                                   : QStringLiteral("No standard size up to 50 mm² keeps this under "
                                                    "the limit: the heat has to get out better, or "
                                                    "the current has to come down."));
            }
            if (!report_.hottestContributor().isEmpty())
                bits << report_.hottestContributor() + QStringLiteral(".");
            advice_->setText(bits.join(QStringLiteral("  ")));
        }
    }

    if (steps_) {
        steps_->setRowCount(static_cast<int>(report_.steps.size()));
        for (int i = 0; i < static_cast<int>(report_.steps.size()); ++i) {
            const ThStep& s = report_.steps[static_cast<size_t>(i)];
            steps_->setItem(i, 0, new QTableWidgetItem(QString::number(s.iteration)));
            steps_->setItem(i, 1, new QTableWidgetItem(QString::number(s.conductorTempC, 'f', 2)));
            steps_->setItem(i, 2, new QTableWidgetItem(QString::number(s.totalPowerW, 'f', 3)));
            steps_->setItem(i, 3, new QTableWidgetItem(QString::number(s.movedC, 'f', 3)));
        }
        steps_->resizeColumnsToContents();
    }

    if (!working_) return;
    QString html = QStringLiteral(
        "<style>body{color:#d7dade;} h4{color:#8fb0d6;margin-bottom:4px;} "
        "pre{background:#2b2e33;border:1px solid #4a4e55;padding:8px;color:#d7dade;} "
        "p{color:#c3c9d1;} li{color:#c3c9d1;} .gap{color:#e6b45a;}</style>");

    if (!report_.gaps.empty()) {
        html += QStringLiteral("<h4>%1</h4><ul>")
                    .arg(report_.blocked() ? QStringLiteral("Missing — these stop the calculation")
                                           : QStringLiteral("Missing — worth filling in"));
        for (const VdGap& g : report_.gaps)
            html += QStringLiteral("<li class='gap'>%1 — %2 (%3)</li>")
                        .arg(g.objectName.toHtmlEscaped(), g.property.toHtmlEscaped(),
                             g.hint.toHtmlEscaped());
        html += QStringLiteral("</ul>");
    }
    if (!report_.workings.isEmpty()) {
        html += QStringLiteral("<h4>The working</h4><pre>");
        for (const QString& line : report_.workings)
            html += line.toHtmlEscaped() + QStringLiteral("\n");
        html += QStringLiteral("</pre>");
    }
    if (!report_.wires.empty()) {
        html += QStringLiteral(
            "<h4>Where the heat comes from</h4>"
            "<table cellpadding='4' style='color:#c3c9d1;'><tr><th align='left'>Wire</th>"
            "<th align='right'>mm²</th><th align='right'>A</th><th align='right'>A/mm²</th>"
            "<th align='right'>R at T</th><th align='right'>W</th><th align='right'>share</th></tr>");
        for (const ThWireResult& w : report_.wires)
            html += QStringLiteral("<tr><td>%1</td><td align='right'>%2</td><td align='right'>%3</td>"
                                   "<td align='right'>%4</td><td align='right'>%5 Ω</td>"
                                   "<td align='right'>%6</td><td align='right'>%7 %</td></tr>")
                        .arg(w.label.toHtmlEscaped())
                        .arg(w.mm2, 0, 'f', 2)
                        .arg(w.currentA, 0, 'f', 2)
                        .arg(w.currentDensity, 0, 'f', 1)
                        .arg(w.resistanceOhms, 0, 'f', 5)
                        .arg(w.powerW, 0, 'f', 3)
                        .arg(w.shareOfHeat * 100.0, 0, 'f', 0);
        html += QStringLiteral("</table>");
    }
    html += QStringLiteral("<h4>How to read it</h4>");
    html += QStringLiteral(
        "<p>The three thermal resistances are in series, from the middle of the bundle out to the "
        "air. Whichever is largest is what governs the temperature — and it is usually "
        "R<sub>th,ambient</sub>, which is why a bundle in still air runs so much hotter than the "
        "same wires spread out.</p>");
    html += QStringLiteral(
        "<p>The limit here is %1 °C, from %2.</p>")
                .arg(report_.limitC, 0, 'f', 0)
                .arg(report_.limitFrom.toHtmlEscaped());
    if (!report_.assumptions.isEmpty()) {
        html += QStringLiteral("<h4>What has been assumed</h4><ul>");
        for (const QString& a : report_.assumptions)
            html += QStringLiteral("<li>%1</li>").arg(a.toHtmlEscaped());
        html += QStringLiteral("</ul>");
    }
    html += QStringLiteral(
        "<p><b>What this is not.</b> It is a lumped estimate with one temperature for the whole "
        "bundle, on coefficients that are set above rather than measured. It is for comparing "
        "choices — this gauge against that one, bundled against spread out — and for finding the "
        "cases worth measuring. It does not replace the wire's own ampacity table or a test.</p>");
    working_->setHtml(html);
}

} // namespace Edraw_Desktop
