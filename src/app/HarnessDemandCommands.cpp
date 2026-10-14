// The harness demand commands on the Harness schematic tab: a workbook for a customer without Loopwire to fill in,
// the filled one read back into the schematic harness builder, and the demand put on one sheet of paper to confirm.
// The workbook and the reading of it are in harness/HarnessDemand; this is the files and the windows around them.

#include "app/MainWindow.h"

#include "app/HarnessBuilderDialog.h"
#include "harness/HarnessDemand.h"

#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>

namespace Loopwire {

namespace {
const char* kDemandDirKey = "harnessDemand/lastDir";

QString demandDir() {
    const QString dir = QSettings().value(QLatin1String(kDemandDirKey)).toString();
    return dir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : dir;
}

void rememberDir(const QString& path) {
    QSettings().setValue(QLatin1String(kDemandDirKey), QFileInfo(path).absolutePath());
}

bool readDemand(QWidget* parent, const QString& path, HarnessDemand& demand) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(parent, QStringLiteral("Harness demand"), QStringLiteral("Could not read %1").arg(path));
        return false;
    }
    QString error;
    if (!harnessDemandFromXlsx(f.readAll(), demand, &error)) {
        QMessageBox::warning(parent, QStringLiteral("Harness demand"),
                             QStringLiteral("%1\n\n%2").arg(QFileInfo(path).fileName(), error));
        return false;
    }
    return true;
}

QString issueLine(const DemandIssue& i) {
    if (i.sheet.isEmpty()) return i.text;
    if (i.row > 0) return QStringLiteral("%1, row %2: %3").arg(i.sheet).arg(i.row).arg(i.text);
    return QStringLiteral("%1: %2").arg(i.sheet, i.text);
}
} // namespace

void MainWindow::exportHarnessDemand(bool fromBuilder) {
    HarnessDemand demand;
    if (fromBuilder) {
        if (!harnessDlg_ || harnessDlg_->definition().connectors.empty()) {
            QMessageBox::information(this, QStringLiteral("Harness demand"),
                                     QStringLiteral("The schematic harness builder has no connectors yet - describe the "
                                                    "harness there first, or save a blank template."));
            return;
        }
        demand = harnessDemandFromDef(harnessDlg_->definition());
    }
    const QString name = fromBuilder ? QStringLiteral("Harness demand.xlsx") : QStringLiteral("Harness demand template.xlsx");
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save harness demand"),
                                                QDir(demandDir()).filePath(name),
                                                QStringLiteral("Excel workbook (*.xlsx)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QLatin1String(".xlsx"), Qt::CaseInsensitive)) path += QStringLiteral(".xlsx");
    QFile f(path);
    const QByteArray bytes = harnessDemandToXlsx(demand);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) {
        QMessageBox::warning(this, QStringLiteral("Harness demand"), QStringLiteral("Could not write %1").arg(path));
        return;
    }
    rememberDir(path);
    statusBar()->showMessage(QStringLiteral("Harness demand saved: %1").arg(QDir::toNativeSeparators(path)), 6000);
}

void MainWindow::importHarnessDemand() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import harness demand"), demandDir(),
                                                      QStringLiteral("Excel workbook (*.xlsx)"));
    if (path.isEmpty()) return;
    HarnessDemand demand;
    if (!readDemand(this, path, demand)) return;
    rememberDir(path);
    std::vector<DemandIssue> issues;
    const HarnessDef def = harnessDefFromDemand(demand, &issues);
    // WHAT WAS READ, and what could not be taken as written - shown before the builder is filled, so a demand with
    // something wrong in it can be sent back rather than half used.
    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("harnessDemandImport"));
    dlg.setWindowTitle(QStringLiteral("Import harness demand"));
    auto* lay = new QVBoxLayout(&dlg);
    QString who = demand.info.harnessName;
    if (!demand.info.customer.isEmpty()) who += (who.isEmpty() ? QString() : QStringLiteral(" - ")) + demand.info.customer;
    auto* summary = new QLabel(QStringLiteral("<b>%1</b><br>%2 connector(s), %3 wire row(s)")
                                   .arg((who.isEmpty() ? QFileInfo(path).fileName() : who).toHtmlEscaped())
                                   .arg(def.connectors.size())
                                   .arg(def.wires.size()),
                               &dlg);
    lay->addWidget(summary);
    if (!issues.empty()) {
        lay->addWidget(new QLabel(QStringLiteral("To check:"), &dlg));
        auto* list = new QListWidget(&dlg);
        list->setObjectName(QStringLiteral("harnessDemandIssues"));
        for (const DemandIssue& i : issues) list->addItem(issueLine(i));
        list->setMinimumSize(560, 220);
        lay->addWidget(list);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dlg);
    buttons->addButton(QStringLiteral("Open in the builder"), QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) return;
    showHarnessBuilder();
    if (harnessDlg_) harnessDlg_->loadDemand(def, QFileInfo(path).fileName());
}

void MainWindow::harnessDemandOverview() {
    const QString in = QFileDialog::getOpenFileName(this, QStringLiteral("Harness demand to put on paper"), demandDir(),
                                                    QStringLiteral("Excel workbook (*.xlsx)"));
    if (in.isEmpty()) return;
    HarnessDemand demand;
    if (!readDemand(this, in, demand)) return;
    rememberDir(in);
    QString out = QFileDialog::getSaveFileName(this, QStringLiteral("Save demand overview"),
                                               QFileInfo(in).absoluteDir().filePath(QFileInfo(in).completeBaseName() +
                                                                                    QStringLiteral(" overview.pdf")),
                                               QStringLiteral("PDF (*.pdf)"));
    if (out.isEmpty()) return;
    if (!out.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) out += QStringLiteral(".pdf");
    QString error;
    if (!writeHarnessDemandPdf(demand, out, &error)) {
        QMessageBox::warning(this, QStringLiteral("Harness demand"), error);
        return;
    }
    statusBar()->showMessage(QStringLiteral("Demand overview saved: %1").arg(QDir::toNativeSeparators(out)), 6000);
    QDesktopServices::openUrl(QUrl::fromLocalFile(out));
}

} // namespace Loopwire
