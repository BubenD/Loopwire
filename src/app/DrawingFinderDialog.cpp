#include "app/DrawingFinderDialog.h"

#include "app/DrawingPreview.h"
#include "io/Dwx.h"
#include "io/Dxf.h"
#include "io/OdaConverter.h"
#include "model/Document.h"
#include "system/SystemDesign.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

namespace Edraw_Desktop {

DrawingFinderDialog::DrawingFinderDialog(QWidget* parent, bool choosing) : QDialog(parent), choosing_(choosing) {
    setObjectName(QStringLiteral("drawingFinder"));
    setWindowTitle(choosing ? QStringLiteral("Find the drawing to link") : QStringLiteral("Find drawings"));
    resize(1150, 680);
    auto* lay = new QVBoxLayout(this);
    // WHERE, AND WHAT.
    auto* top = new QGridLayout;
    top->addWidget(new QLabel(QStringLiteral("Look in"), this), 0, 0);
    folder_ = new QLineEdit(this);
    folder_->setObjectName(QStringLiteral("finderFolder"));
    folder_->setText(QSettings().value(QStringLiteral("finder/folder"), QDir::homePath()).toString());
    top->addWidget(folder_, 0, 1);
    auto* browse = new QPushButton(QStringLiteral("Browse..."), this);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Look in"), folder_->text());
        if (!d.isEmpty()) folder_->setText(d);
    });
    top->addWidget(browse, 0, 2);
    sub_ = new QCheckBox(QStringLiteral("and its folders"), this);
    sub_->setChecked(true);
    top->addWidget(sub_, 0, 3);
    top->addWidget(new QLabel(QStringLiteral("Find"), this), 1, 0);
    text_ = new QLineEdit(this);
    text_->setObjectName(QStringLiteral("finderText"));
    text_->setPlaceholderText(QStringLiteral("part of a file name, a drawing number, a title - empty: every drawing"));
    top->addWidget(text_, 1, 1);
    go_ = new QPushButton(QStringLiteral("Search"), this);
    go_->setObjectName(QStringLiteral("finderSearch"));
    go_->setDefault(true);
    connect(go_, &QPushButton::clicked, this, [this] {
        if (searching()) stop();
        else search();
    });
    connect(text_, &QLineEdit::returnPressed, this, [this] { search(); });
    top->addWidget(go_, 1, 2);
    auto* kinds = new QHBoxLayout;
    dwx_ = new QCheckBox(QStringLiteral("DWX"), this);
    dxf_ = new QCheckBox(QStringLiteral("DXF"), this);
    dwg_ = new QCheckBox(QStringLiteral("DWG"), this);
    for (QCheckBox* c : {dwx_, dxf_, dwg_}) {
        c->setChecked(true);
        kinds->addWidget(c);
    }
    inside_ = new QCheckBox(QStringLiteral("read inside them (drawing No., title, rev)"), this);
    inside_->setObjectName(QStringLiteral("finderInside"));
    inside_->setChecked(true);
    inside_->setToolTip(QStringLiteral("Each DWX and DXF read for its drawing number, title and revision - found by those too, and "
                                       "listed with them (slower over many files)"));
    kinds->addWidget(inside_);
    kinds->addStretch(1);
    top->addLayout(kinds, 1, 3);
    top->setColumnStretch(1, 1);
    lay->addLayout(top);
    // WHAT WAS FOUND, AND THE ONE CHOSEN.
    auto* split = new QSplitter(this);
    table_ = new QTableWidget(0, 6, split);
    table_->setObjectName(QStringLiteral("finderResults"));
    table_->setHorizontalHeaderLabels({QStringLiteral("File"), QStringLiteral("Drawing No."), QStringLiteral("Rev"), QStringLiteral("Title"),
                                       QStringLiteral("Modified"), QStringLiteral("Folder")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSortingEnabled(true);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        const QList<QTableWidgetItem*> sel = table_->selectedItems();
        if (!sel.isEmpty()) showResult(sel.first()->row());
    });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (choosing_) {
            chosen_ = resultPath(row);
            accept();
        } else {
            emit openRequested(resultPath(row));
        }
    });
    auto* right = new QWidget(split);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    preview_ = new DrawingPreview(QString(), right);
    preview_->setObjectName(QStringLiteral("finderPreview"));
    preview_->setEmptyText(QStringLiteral("Choose a drawing in the list to see it here"));
    rl->addWidget(preview_, 1);
    info_ = new QLabel(right);
    info_->setWordWrap(true);
    info_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rl->addWidget(info_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    lay->addWidget(split, 1);
    // WHAT TO DO WITH IT.
    auto* bottom = new QHBoxLayout;
    said_ = new QLabel(this);
    said_->setObjectName(QStringLiteral("finderSaid"));
    bottom->addWidget(said_, 1);
    auto* reveal = new QPushButton(QStringLiteral("Show in folder"), this);
    connect(reveal, &QPushButton::clicked, this, [this] {
        const int row = table_->currentRow();
        if (row >= 0) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(resultPath(row)).absolutePath()));
    });
    bottom->addWidget(reveal);
    auto* act = new QPushButton(choosing ? QStringLiteral("Link this drawing") : QStringLiteral("Open"), this);
    act->setObjectName(QStringLiteral("finderAct"));
    connect(act, &QPushButton::clicked, this, [this] {
        const int row = table_->currentRow();
        if (row < 0) return;
        if (choosing_) {
            chosen_ = resultPath(row);
            accept();
        } else {
            emit openRequested(resultPath(row));
        }
    });
    bottom->addWidget(act);
    auto* close = new QPushButton(QStringLiteral("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    bottom->addWidget(close);
    lay->addLayout(bottom);
}

DrawingFinderDialog::~DrawingFinderDialog() {
    stop();
    if (worker_) worker_->wait();
}

void DrawingFinderDialog::setFolder(const QString& folder) { folder_->setText(folder); }

void DrawingFinderDialog::setText(const QString& text) { text_->setText(text); }

bool DrawingFinderDialog::searching() const { return worker_ && worker_->isRunning(); }

int DrawingFinderDialog::resultCount() const { return table_->rowCount(); }

QString DrawingFinderDialog::resultPath(int row) const {
    const QTableWidgetItem* it = table_->item(row, 0);
    return it ? it->data(Qt::UserRole).toString() : QString();
}

void DrawingFinderDialog::stop() {
    if (halt_) halt_->store(true);
}

void DrawingFinderDialog::search(bool wait) {
    if (searching()) return;
    const QString root = folder_->text().trimmed();
    if (!QDir(root).exists()) {
        said_->setText(QStringLiteral("No such folder: %1").arg(root));
        return;
    }
    QSettings().setValue(QStringLiteral("finder/folder"), root);
    table_->setSortingEnabled(false);
    table_->setRowCount(0);
    scanned_ = 0;
    QStringList filters;
    if (dwx_->isChecked()) filters << QStringLiteral("*.dwx");
    if (dxf_->isChecked()) filters << QStringLiteral("*.dxf");
    if (dwg_->isChecked()) filters << QStringLiteral("*.dwg");
    const QString text = text_->text().trimmed();
    const bool inside = inside_->isChecked();
    const bool deep = sub_->isChecked();
    halt_ = std::make_shared<std::atomic<bool>>(false);
    auto halt = halt_;
    QPointer<DrawingFinderDialog> self(this);
    go_->setText(QStringLiteral("Stop"));
    said_->setText(QStringLiteral("Searching..."));
    // IN THE BACKGROUND: each file found - read inside, where asked - handed back to be listed.
    worker_ = QThread::create([self, halt, root, filters, text, inside, deep] {
        QDirIterator it(root, filters, QDir::Files, deep ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
        while (it.hasNext() && !halt->load()) {
            const QString path = it.next();
            const QFileInfo fi(path);
            Found f;
            f.path = path;
            f.modified = fi.lastModified().toMSecsSinceEpoch();
            if (inside && fi.suffix().compare(QLatin1String("dwg"), Qt::CaseInsensitive) != 0) {
                const DrawingInfo info = readDrawingInfo(path);
                f.drawingNo = info.drawingNo;
                f.revision = info.revision;
                f.title = info.title;
            }
            const bool match = text.isEmpty() || fi.fileName().contains(text, Qt::CaseInsensitive) ||
                               f.drawingNo.contains(text, Qt::CaseInsensitive) || f.title.contains(text, Qt::CaseInsensitive);
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [self, f, match] {
                    if (!self) return;
                    ++self->scanned_;
                    if (match) self->addFound(f);
                },
                Qt::QueuedConnection);
        }
    });
    connect(worker_, &QThread::finished, this, [this] { finished(); });
    worker_->start();
    if (wait) {
        worker_->wait();
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
    }
}

void DrawingFinderDialog::addFound(const Found& f) {
    const QFileInfo fi(f.path);
    const int row = table_->rowCount();
    table_->insertRow(row);
    auto* name = new QTableWidgetItem(fi.fileName());
    name->setData(Qt::UserRole, f.path);
    name->setToolTip(f.path);
    table_->setItem(row, 0, name);
    table_->setItem(row, 1, new QTableWidgetItem(f.drawingNo));
    table_->setItem(row, 2, new QTableWidgetItem(f.revision));
    table_->setItem(row, 3, new QTableWidgetItem(f.title));
    table_->setItem(row, 4, new QTableWidgetItem(QDateTime::fromMSecsSinceEpoch(f.modified).toString(QStringLiteral("yyyy-MM-dd hh:mm"))));
    table_->setItem(row, 5, new QTableWidgetItem(QDir::toNativeSeparators(fi.absolutePath())));
    said_->setText(QStringLiteral("Searching... %1 found of %2 looked at").arg(table_->rowCount()).arg(scanned_));
}

void DrawingFinderDialog::finished() {
    if (worker_) {
        worker_->deleteLater();
        worker_ = nullptr;
    }
    QCoreApplication::processEvents();
    table_->setSortingEnabled(true);
    go_->setText(QStringLiteral("Search"));
    said_->setText(QStringLiteral("%1 drawing(s) found of %2 looked at%3")
                       .arg(table_->rowCount())
                       .arg(scanned_)
                       .arg(halt_ && halt_->load() ? QStringLiteral(" - stopped") : QString()));
    table_->resizeColumnsToContents();
}

void DrawingFinderDialog::showResult(int row) {
    if (row < 0 || row >= table_->rowCount()) return;
    table_->selectRow(row);
    preview(resultPath(row));
}

void DrawingFinderDialog::preview(const QString& path) {
    const QFileInfo fi(path);
    const QString suffix = fi.suffix().toLower();
    QString said = QStringLiteral("<b>%1</b><br>%2").arg(fi.fileName().toHtmlEscaped(), QDir::toNativeSeparators(fi.absolutePath()).toHtmlEscaped());
    if (suffix != QLatin1String("dwg")) {
        const DrawingInfo info = readDrawingInfo(path);
        if (!info.drawingNo.isEmpty()) said += QStringLiteral("<br>Drawing No. <b>%1</b>").arg(info.drawingNo.toHtmlEscaped());
        if (!info.revision.isEmpty()) said += QStringLiteral("  rev <b>%1</b>").arg(info.revision.toHtmlEscaped());
        if (!info.title.isEmpty()) said += QStringLiteral("<br>%1").arg(info.title.toHtmlEscaped());
        Document doc(fi.completeBaseName());
        const bool ok = suffix == QLatin1String("dwx") ? dwx::load(doc, path) : dxf::importFile(doc, path);
        preview_->setCaption(fi.fileName());
        if (ok) {
            std::vector<Entity> all = doc.entities();
            if (all.empty())
                for (const DrawingLayout& l : doc.layouts()) all.insert(all.end(), l.paperEntities.begin(), l.paperEntities.end());
            preview_->setEntities(all);
        } else {
            preview_->setEntities({});
            preview_->setEmptyText(QStringLiteral("This file could not be read"));
        }
        info_->setText(said);
        return;
    }
    // A DWG: through the ODA File Converter, where it is installed - turned into a DXF to show.
    preview_->setEntities({});
    if (!oda_) oda_ = new OdaConverter(this);
    if (!oda_->available() || oda_->busy()) {
        preview_->setEmptyText(oda_->available() ? QStringLiteral("Converting another DWG...")
                                                 : QStringLiteral("A DWG is shown through the ODA File Converter - install it to see DWG drawings here"));
        info_->setText(said);
        return;
    }
    auto tmp = std::make_shared<QTemporaryDir>();
    const QString out = tmp->filePath(QStringLiteral("preview.dxf"));
    preview_->setEmptyText(QStringLiteral("Converting the DWG to show it..."));
    QString err;
    disconnect(oda_, nullptr, this, nullptr);
    connect(oda_, &OdaConverter::finished, this, [this, tmp, out, path](bool ok, const QString&) {
        if (!ok) {
            preview_->setEmptyText(QStringLiteral("The DWG could not be converted to show"));
            return;
        }
        Document doc(QFileInfo(path).completeBaseName());
        if (dxf::importFile(doc, out)) preview_->setEntities(doc.entities());
    });
    if (!oda_->importDwgAsDxf(path, out, &err)) preview_->setEmptyText(err);
    info_->setText(said);
}

QString DrawingFinderDialog::choose(QWidget* parent, const QString& text) {
    DrawingFinderDialog dlg(parent, true);
    dlg.setText(text);
    if (dlg.exec() != QDialog::Accepted) return QString();
    return dlg.chosen();
}

} // namespace Edraw_Desktop
