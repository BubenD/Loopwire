#include "app/Recovery.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace recovery {

QString folder() {
    QString dir = qEnvironmentVariable("EDRAW_RECOVERY_DIR");
    if (dir.isEmpty()) dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/Recovery");
    QDir().mkpath(dir);
    return dir;
}

namespace {

QString fileFor(const QString& documentPath, int tab, const char* extension) {
    QString base = documentPath.isEmpty() ? QStringLiteral("Untitled %1").arg(tab + 1) : QFileInfo(documentPath).completeBaseName();
    // Nothing a file name cannot hold.
    for (QChar& c : base)
        if (QStringLiteral("\\/:*?\"<>|").contains(c)) c = QLatin1Char('_');
    const QString key = documentPath.isEmpty() ? QString::number(tab) : QDir::cleanPath(QFileInfo(documentPath).absoluteFilePath()).toLower();
    const QString tag = QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Md5).toHex().left(6));
    return folder() + QLatin1Char('/') + base + QStringLiteral(" [") + tag + QStringLiteral("].") + QLatin1String(extension);
}

}  // namespace

QString drawingFile(const QString& documentPath, int tab) { return fileFor(documentPath, tab, "dbak"); }
QString systemFile(const QString& documentPath, int tab) { return fileFor(documentPath, tab, "sbak"); }

std::vector<Entry> list() {
    std::vector<Entry> out;
    const QDateTime stale = QDateTime::currentDateTime().addDays(-14);
    for (const QFileInfo& f : QDir(folder()).entryInfoList({QStringLiteral("*.dbak"), QStringLiteral("*.sbak")}, QDir::Files)) {
        if (f.lastModified() < stale) {
            QFile::remove(f.absoluteFilePath());
            continue;
        }
        out.push_back({f.absoluteFilePath(), f.suffix().compare(QLatin1String("sbak"), Qt::CaseInsensitive) == 0, f.lastModified(),
                       f.size()});
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.when > b.when; });
    return out;
}

}  // namespace recovery

RecoveryDialog::RecoveryDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("recoveryDialog"));
    setWindowTitle(QStringLiteral("Recover"));
    resize(640, 360);
    auto* v = new QVBoxLayout(this);
    auto* said = new QLabel(QStringLiteral("Copies written while drawings (.dbak) and system designs (.sbak) had changes not "
                                           "saved. A drawing recovered opens in a tab of its own, to be saved where you choose; "
                                           "a system design takes the place of the one open (Undo takes it back)."),
                            this);
    said->setWordWrap(true);
    v->addWidget(said);
    table_ = new QTableWidget(0, 4, this);
    table_->setObjectName(QStringLiteral("recoveryTable"));
    table_->setHorizontalHeaderLabels({QStringLiteral("Copy of"), QStringLiteral("Kind"), QStringLiteral("Written"), QStringLiteral("Size")});
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c) table_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    v->addWidget(table_, 1);
    auto* row = new QHBoxLayout;
    auto* recover = new QPushButton(QStringLiteral("Recover"), this);
    recover->setObjectName(QStringLiteral("recoveryRecover"));
    recover->setDefault(true);
    auto* drop = new QPushButton(QStringLiteral("Delete"), this);
    drop->setObjectName(QStringLiteral("recoveryDelete"));
    auto* where = new QPushButton(QStringLiteral("Open folder"), this);
    auto* close = new QPushButton(QStringLiteral("Close"), this);
    row->addWidget(recover);
    row->addWidget(drop);
    row->addWidget(where);
    row->addStretch(1);
    row->addWidget(close);
    v->addLayout(row);
    auto current = [this] {
        const int r = table_->currentRow();
        return r < 0 ? QString() : table_->item(r, 0)->data(Qt::UserRole).toString();
    };
    connect(recover, &QPushButton::clicked, this, [this, current] {
        chosen_ = current();
        if (!chosen_.isEmpty()) accept();
    });
    connect(table_, &QTableWidget::cellDoubleClicked, recover, &QPushButton::click);
    connect(drop, &QPushButton::clicked, this, [this, current] {
        const QString path = current();
        if (path.isEmpty()) return;
        if (QMessageBox::question(this, QStringLiteral("Recover"),
                                  QStringLiteral("Delete the copy %1?").arg(QFileInfo(path).fileName())) != QMessageBox::Yes)
            return;
        QFile::remove(path);
        fill();
    });
    connect(where, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(recovery::folder())); });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    fill();
}

void RecoveryDialog::fill() {
    const auto entries = recovery::list();
    table_->setRowCount(static_cast<int>(entries.size()));
    for (int r = 0; r < static_cast<int>(entries.size()); ++r) {
        const recovery::Entry& e = entries[static_cast<size_t>(r)];
        auto* name = new QTableWidgetItem(QFileInfo(e.path).completeBaseName());
        name->setData(Qt::UserRole, e.path);
        name->setToolTip(e.path);
        table_->setItem(r, 0, name);
        table_->setItem(r, 1, new QTableWidgetItem(e.system ? QStringLiteral("System design") : QStringLiteral("Drawing")));
        table_->setItem(r, 2, new QTableWidgetItem(QLocale().toString(e.when, QLocale::ShortFormat)));
        table_->setItem(r, 3, new QTableWidgetItem(QLocale().formattedDataSize(e.size)));
    }
    if (!entries.empty()) table_->selectRow(0);
}
