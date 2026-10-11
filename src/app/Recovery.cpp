#include "app/Recovery.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace recovery {

namespace {

QString besideFile(const QString& path, const char* extension) {
    if (path.isEmpty()) return QString();
    const QFileInfo f(path);
    return QDir(f.absolutePath()).filePath(f.completeBaseName() + QLatin1Char('.') + QLatin1String(extension));
}

QStringList remembered() { return QSettings().value(QStringLiteral("recovery/files")).toStringList(); }

}  // namespace

QString drawingFile(const QString& documentPath) { return besideFile(documentPath, "dbak"); }
QString systemFile(const QString& designPath) { return besideFile(designPath, "sbak"); }

void remember(const QString& copy) {
    if (copy.isEmpty()) return;
    const QString full = QDir::cleanPath(QFileInfo(copy).absoluteFilePath());
    QStringList list = remembered();
    list.removeAll(full);
    list.prepend(full);
    while (list.size() > 50) list.removeLast();
    QSettings().setValue(QStringLiteral("recovery/files"), list);
}

void forget(const QString& copy) {
    if (copy.isEmpty()) return;
    QStringList list = remembered();
    if (list.removeAll(QDir::cleanPath(QFileInfo(copy).absoluteFilePath())) > 0) QSettings().setValue(QStringLiteral("recovery/files"), list);
}

std::vector<Entry> list() {
    std::vector<Entry> out;
    QStringList kept;
    for (const QString& path : remembered()) {
        const QFileInfo f(path);
        if (!f.exists()) continue;
        kept << path;
        out.push_back({f.absoluteFilePath(), f.suffix().compare(QLatin1String("sbak"), Qt::CaseInsensitive) == 0, f.lastModified(),
                       f.size()});
    }
    // Those gone since - saved, or deleted - forgotten.
    if (kept != remembered()) QSettings().setValue(QStringLiteral("recovery/files"), kept);
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.when > b.when; });
    return out;
}

}  // namespace recovery

RecoveryDialog::RecoveryDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("recoveryDialog"));
    setWindowTitle(QStringLiteral("Recover"));
    resize(640, 360);
    auto* v = new QVBoxLayout(this);
    auto* said = new QLabel(QStringLiteral("Copies written beside drawings (.dbak) and system designs (.sbak) saved to a file, "
                                           "while they had changes not saved. A drawing recovered opens in a tab of its own, to be "
                                           "saved where you choose; a system design takes the place of the one open (Undo takes it "
                                           "back). Browse finds a copy that is not listed."),
                            this);
    said->setWordWrap(true);
    v->addWidget(said);
    table_ = new QTableWidget(0, 5, this);
    table_->setObjectName(QStringLiteral("recoveryTable"));
    table_->setHorizontalHeaderLabels({QStringLiteral("Copy of"), QStringLiteral("Folder"), QStringLiteral("Kind"), QStringLiteral("Written"),
                                       QStringLiteral("Size")});
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int c = 2; c < 5; ++c) table_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    v->addWidget(table_, 1);
    auto* row = new QHBoxLayout;
    auto* recover = new QPushButton(QStringLiteral("Recover"), this);
    recover->setObjectName(QStringLiteral("recoveryRecover"));
    recover->setDefault(true);
    auto* drop = new QPushButton(QStringLiteral("Delete"), this);
    drop->setObjectName(QStringLiteral("recoveryDelete"));
    auto* where = new QPushButton(QStringLiteral("Browse..."), this);
    where->setObjectName(QStringLiteral("recoveryBrowse"));
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
        recovery::forget(path);
        fill();
    });
    connect(where, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("A recovery copy"), QString(),
                                                          QStringLiteral("Recovery copies (*.dbak *.sbak)"));
        if (path.isEmpty()) return;
        chosen_ = path;
        accept();
    });
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
        table_->setItem(r, 1, new QTableWidgetItem(QDir::toNativeSeparators(QFileInfo(e.path).absolutePath())));
        table_->setItem(r, 2, new QTableWidgetItem(e.system ? QStringLiteral("System design") : QStringLiteral("Drawing")));
        table_->setItem(r, 3, new QTableWidgetItem(QLocale().toString(e.when, QLocale::ShortFormat)));
        table_->setItem(r, 4, new QTableWidgetItem(QLocale().formattedDataSize(e.size)));
    }
    if (!entries.empty()) table_->selectRow(0);
}
