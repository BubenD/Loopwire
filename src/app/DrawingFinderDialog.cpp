#include "app/DrawingFinderDialog.h"

#include "app/DrawingPreview.h"
#include "io/Dwx.h"
#include "io/Dxf.h"
#include "io/OdaConverter.h"
#include "io/PdfImport.h"
#include "model/Document.h"
#include "system/FileText.h"
#include "system/PnMatch.h"
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

namespace {

enum Column { FileCol, HitsCol, FoundCol, NoCol, RevCol, TitleCol, ModifiedCol, FolderCol };

// The drawings beside a file, of the same name in the same folder.
QHash<QString, QString> besideOf(const QString& path) {
    QHash<QString, QString> out;
    const QFileInfo fi(path);
    for (const char* kind : {"dwg", "dxf", "dwx", "pdf"}) {
        if (fi.suffix().compare(QLatin1String(kind), Qt::CaseInsensitive) == 0) continue;
        const QDir dir = fi.absoluteDir();
        for (const QString& name : dir.entryList({fi.completeBaseName() + QLatin1Char('.') + QLatin1String(kind)}, QDir::Files))
            if (QFileInfo(name).completeBaseName().compare(fi.completeBaseName(), Qt::CaseInsensitive) == 0)
                out.insert(QString::fromLatin1(kind).toUpper(), dir.filePath(name));
    }
    return out;
}

} // namespace

DrawingFinderDialog::DrawingFinderDialog(QWidget* parent, bool choosing) : QDialog(parent), choosing_(choosing) {
    setObjectName(QStringLiteral("drawingFinder"));
    setWindowTitle(choosing ? QStringLiteral("Find the drawing to link") : QStringLiteral("Find drawings"));
    resize(1250, 720);
    QSettings settings;
    auto* lay = new QVBoxLayout(this);
    // WHERE, AND WHAT.
    auto* top = new QGridLayout;
    top->addWidget(new QLabel(QStringLiteral("Look in"), this), 0, 0);
    folder_ = new QLineEdit(this);
    folder_->setObjectName(QStringLiteral("finderFolder"));
    folder_->setText(settings.value(QStringLiteral("finder/folder"), QDir::homePath()).toString());
    top->addWidget(folder_, 0, 1);
    auto* browse = new QPushButton(QStringLiteral("Browse..."), this);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Look in"), folder_->text());
        if (!d.isEmpty()) folder_->setText(d);
    });
    top->addWidget(browse, 0, 2);
    sub_ = new QCheckBox(QStringLiteral("and every folder in it"), this);
    sub_->setChecked(true);
    top->addWidget(sub_, 0, 3);
    top->addWidget(new QLabel(QStringLiteral("P/N"), this), 1, 0);
    text_ = new QLineEdit(this);
    text_->setObjectName(QStringLiteral("finderText"));
    text_->setPlaceholderText(QStringLiteral("a part number - several separated by ; - or part of a file name; empty: every file"));
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
    // HOW A PART NUMBER IS MATCHED.
    auto* how = new QHBoxLayout;
    partial_ = new QCheckBox(QStringLiteral("Partial match"), this);
    partial_->setObjectName(QStringLiteral("finderPartial"));
    partial_->setToolTip(QStringLiteral("Found inside a longer number too - LM317 in LM317T - those shown in orange"));
    case_ = new QCheckBox(QStringLiteral("Match case"), this);
    case_->setObjectName(QStringLiteral("finderCase"));
    seps_ = new QCheckBox(QStringLiteral("Ignore separators"), this);
    seps_->setObjectName(QStringLiteral("finderSeparators"));
    seps_->setToolTip(QStringLiteral("- . / _ and spaces optional: ABC-123 finds ABC123 and ABC 123 too"));
    partial_->setChecked(settings.value(QStringLiteral("finder/partial"), false).toBool());
    case_->setChecked(settings.value(QStringLiteral("finder/case"), false).toBool());
    seps_->setChecked(settings.value(QStringLiteral("finder/separators"), false).toBool());
    for (QCheckBox* c : {partial_, case_, seps_}) how->addWidget(c);
    how->addStretch(1);
    top->addLayout(how, 1, 3);
    // WHICH FILES.
    auto* kinds = new QHBoxLayout;
    kinds->addWidget(new QLabel(QStringLiteral("Files:"), this));
    dwx_ = new QCheckBox(QStringLiteral("DWX"), this);
    dxf_ = new QCheckBox(QStringLiteral("DXF"), this);
    dwg_ = new QCheckBox(QStringLiteral("DWG"), this);
    pdf_ = new QCheckBox(QStringLiteral("PDF"), this);
    csv_ = new QCheckBox(QStringLiteral("CSV"), this);
    dwg_->setToolTip(QStringLiteral("A DWG holds its text compressed: found by its name - and beside a PDF or DXF of the same name "
                                    "that has the part number"));
    for (QCheckBox* c : {dwx_, dxf_, dwg_, pdf_, csv_}) {
        c->setChecked(true);
        kinds->addWidget(c);
    }
    inside_ = new QCheckBox(QStringLiteral("Search inside the files"), this);
    inside_->setObjectName(QStringLiteral("finderInside"));
    inside_->setChecked(true);
    inside_->setToolTip(QStringLiteral("Every text, note, table cell and attribute of a drawing, a PDF's text, a CSV's cells - off: "
                                       "file names only (quicker)"));
    kinds->addWidget(inside_);
    kinds->addStretch(1);
    top->addLayout(kinds, 2, 1, 1, 3);
    top->setColumnStretch(1, 1);
    lay->addLayout(top);
    // WHAT WAS FOUND, AND THE ONE CHOSEN.
    auto* split = new QSplitter(this);
    table_ = new QTableWidget(0, 8, split);
    table_->setObjectName(QStringLiteral("finderResults"));
    table_->setHorizontalHeaderLabels({QStringLiteral("File"), QStringLiteral("Hits"), QStringLiteral("Found"), QStringLiteral("Drawing No."),
                                       QStringLiteral("Rev"), QStringLiteral("Title"), QStringLiteral("Modified"), QStringLiteral("Folder")});
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
            open(resultPath(row));
        }
    });
    auto* right = new QWidget(split);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    preview_ = new DrawingPreview(QString(), right);
    preview_->setObjectName(QStringLiteral("finderPreview"));
    preview_->setEmptyText(QStringLiteral("Choose a file in the list to see it here"));
    rl->addWidget(preview_, 3);
    info_ = new QLabel(right);
    info_->setObjectName(QStringLiteral("finderInfo"));
    info_->setWordWrap(true);
    info_->setTextFormat(Qt::RichText);
    info_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    info_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rl->addWidget(info_, 2);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    lay->addWidget(split, 1);
    // WHAT TO DO WITH IT.
    auto* bottom = new QHBoxLayout;
    said_ = new QLabel(this);
    said_->setObjectName(QStringLiteral("finderSaid"));
    bottom->addWidget(said_, 1);
    auto* reveal = new QPushButton(QStringLiteral("Open folder"), this);
    connect(reveal, &QPushButton::clicked, this, [this] {
        const int row = table_->currentRow();
        if (row >= 0) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(resultPath(row)).absolutePath()));
    });
    bottom->addWidget(reveal);
    // OPEN EACH KIND: the file chosen, or the one of that kind of the same name beside it.
    auto opener = [this, bottom](const char* name, const QString& kind) {
        auto* b = new QPushButton(QStringLiteral("Open %1").arg(kind), this);
        b->setObjectName(QLatin1String(name));
        b->setToolTip(QStringLiteral("Open the %1 - the file chosen, or the one of the same name beside it").arg(kind));
        b->setEnabled(false);
        connect(b, &QPushButton::clicked, this, [this, kind] {
            const QString path = ofKind(resultPath(table_->currentRow()), kind);
            if (!path.isEmpty()) open(path);
        });
        bottom->addWidget(b);
        return b;
    };
    openPdf_ = opener("finderOpenPdf", QStringLiteral("PDF"));
    openDxf_ = opener("finderOpenDxf", QStringLiteral("DXF"));
    openDwx_ = opener("finderOpenDwx", QStringLiteral("DWX"));
    openDwg_ = opener("finderOpenDwg", QStringLiteral("DWG"));
    auto* act = new QPushButton(choosing ? QStringLiteral("Link this drawing") : QStringLiteral("Open"), this);
    act->setObjectName(QStringLiteral("finderAct"));
    connect(act, &QPushButton::clicked, this, [this] {
        const int row = table_->currentRow();
        if (row < 0) return;
        if (choosing_) {
            chosen_ = resultPath(row);
            accept();
        } else {
            open(resultPath(row));
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

void DrawingFinderDialog::setMatching(bool partial, bool caseSensitive, bool ignoreSeparators) {
    partial_->setChecked(partial);
    case_->setChecked(caseSensitive);
    seps_->setChecked(ignoreSeparators);
}

void DrawingFinderDialog::setKinds(const QStringList& kinds) {
    dwx_->setChecked(kinds.contains(QStringLiteral("dwx")));
    dxf_->setChecked(kinds.contains(QStringLiteral("dxf")));
    dwg_->setChecked(kinds.contains(QStringLiteral("dwg")));
    pdf_->setChecked(kinds.contains(QStringLiteral("pdf")));
    csv_->setChecked(kinds.contains(QStringLiteral("csv")));
}

bool DrawingFinderDialog::searching() const { return worker_ && worker_->isRunning(); }

int DrawingFinderDialog::resultCount() const { return table_->rowCount(); }

QString DrawingFinderDialog::resultPath(int row) const {
    const QTableWidgetItem* it = table_->item(row, FileCol);
    return it ? it->data(Qt::UserRole).toString() : QString();
}

int DrawingFinderDialog::resultHits(int row) const { return static_cast<int>(found_.value(resultPath(row)).hits.size()); }

QStringList DrawingFinderDialog::resultDrawings(int row) const {
    QStringList out = found_.value(resultPath(row)).beside.keys();
    out.sort();
    return out;
}

QString DrawingFinderDialog::ofKind(const QString& path, const QString& kind) const {
    if (path.isEmpty()) return QString();
    if (QFileInfo(path).suffix().compare(kind, Qt::CaseInsensitive) == 0) return path;
    return found_.value(path).beside.value(kind.toUpper());
}

int DrawingFinderDialog::previewCount(bool wait) {
    QElapsedTimer clock;
    clock.start();
    while (wait && preview_->isEmpty() && clock.elapsed() < 20000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return preview_->isEmpty() ? 0 : 1;
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
    QSettings settings;
    settings.setValue(QStringLiteral("finder/folder"), root);
    settings.setValue(QStringLiteral("finder/partial"), partial_->isChecked());
    settings.setValue(QStringLiteral("finder/case"), case_->isChecked());
    settings.setValue(QStringLiteral("finder/separators"), seps_->isChecked());
    table_->setSortingEnabled(false);
    table_->setRowCount(0);
    found_.clear();
    scanned_ = 0;
    unreadable_ = 0;
    QStringList filters;
    if (dwx_->isChecked()) filters << QStringLiteral("*.dwx");
    if (dxf_->isChecked()) filters << QStringLiteral("*.dxf");
    if (dwg_->isChecked()) filters << QStringLiteral("*.dwg");
    if (pdf_->isChecked()) filters << QStringLiteral("*.pdf");
    if (csv_->isChecked()) filters << QStringLiteral("*.csv");
    const QStringList terms = PnMatcher::splitTerms(text_->text());
    PnOptions options;
    options.partial = partial_->isChecked();
    options.caseSensitive = case_->isChecked();
    options.ignoreSeparators = seps_->isChecked();
    const bool inside = inside_->isChecked();
    const bool deep = sub_->isChecked();
    halt_ = std::make_shared<std::atomic<bool>>(false);
    auto halt = halt_;
    QPointer<DrawingFinderDialog> self(this);
    go_->setText(QStringLiteral("Stop"));
    said_->setText(QStringLiteral("Searching..."));
    // IN THE BACKGROUND: each file looked at - its name, and inside it where asked - and what is found handed back.
    worker_ = QThread::create([self, halt, root, filters, terms, options, inside, deep] {
        const PnMatcher matcher(terms, options);
        // The name too is matched as a part of it, wherever in the name it is.
        PnOptions loose = options;
        loose.partial = true;
        const PnMatcher byName(terms, loose);
        QDirIterator it(root, filters, QDir::Files, deep ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
        while (it.hasNext() && !halt->load()) {
            const QString path = it.next();
            const QFileInfo fi(path);
            if (fi.fileName().startsWith(QLatin1String("~$"))) continue;
            Found f;
            f.path = path;
            f.modified = fi.lastModified().toMSecsSinceEpoch();
            const QString suffix = fi.suffix().toLower();
            bool keep = terms.isEmpty();
            if (!terms.isEmpty() && byName.contains(fi.fileName())) {
                f.byName = true;
                keep = true;
            }
            if (inside && !terms.isEmpty() && suffix != QLatin1String("dwg")) {
                QString why;
                for (const TextPiece& piece : fileText(path, &why)) {
                    for (const PnMatch& m : matcher.find(piece.text)) {
                        Hit h;
                        h.where = piece.where;
                        h.exact = m.exact;
                        PnMatcher::snippet(piece.text, m.start, m.end, h.before, h.match, h.after);
                        f.hits << h;
                        if (f.hits.size() >= 500) break;
                    }
                    if (f.hits.size() >= 500 || halt->load()) break;
                }
                f.why = why;
                keep = keep || !f.hits.isEmpty();
            }
            if (keep && (suffix == QLatin1String("dwx") || suffix == QLatin1String("dxf"))) {
                const DrawingInfo info = readDrawingInfo(path);
                f.drawingNo = info.drawingNo;
                f.revision = info.revision;
                f.title = info.title;
            }
            if (keep) f.beside = besideOf(path);
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [self, f, keep] {
                    if (!self) return;
                    ++self->scanned_;
                    if (!f.why.isEmpty() && !f.path.endsWith(QLatin1String(".dwg"), Qt::CaseInsensitive)) ++self->unreadable_;
                    if (keep) self->addFound(f);
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
    found_.insert(f.path, f);
    const QFileInfo fi(f.path);
    const int row = table_->rowCount();
    table_->insertRow(row);
    QString name = fi.fileName();
    QStringList beside = f.beside.keys();
    beside.sort();
    for (const QString& k : beside) name += QStringLiteral("  [+%1]").arg(k);
    auto* file = new QTableWidgetItem(name);
    file->setData(Qt::UserRole, f.path);
    file->setToolTip(f.path);
    if (!beside.isEmpty()) file->setForeground(QColor(40, 110, 200));
    table_->setItem(row, FileCol, file);
    auto* hits = new QTableWidgetItem;
    hits->setData(Qt::DisplayRole, static_cast<int>(f.hits.size()));
    table_->setItem(row, HitsCol, hits);
    // THE FIRST PLACE IT IS FOUND, with the words round it - or its name. Only partly matched: orange.
    QString found;
    bool partialOnly = !f.hits.isEmpty();
    for (const Hit& h : f.hits) partialOnly = partialOnly && !h.exact;
    if (!f.hits.isEmpty()) {
        const Hit& h = f.hits.first();
        found = QStringLiteral("%1: %2[%3]%4%5").arg(h.where, h.before, h.match, h.after, partialOnly ? QStringLiteral("  (partial)") : QString());
    } else if (f.byName) {
        found = QStringLiteral("in its name");
    }
    auto* foundItem = new QTableWidgetItem(found);
    if (partialOnly) foundItem->setForeground(QColor(220, 120, 20));
    table_->setItem(row, FoundCol, foundItem);
    table_->setItem(row, NoCol, new QTableWidgetItem(f.drawingNo));
    table_->setItem(row, RevCol, new QTableWidgetItem(f.revision));
    table_->setItem(row, TitleCol, new QTableWidgetItem(f.title));
    table_->setItem(row, ModifiedCol, new QTableWidgetItem(QDateTime::fromMSecsSinceEpoch(f.modified).toString(QStringLiteral("yyyy-MM-dd hh:mm"))));
    table_->setItem(row, FolderCol, new QTableWidgetItem(QDir::toNativeSeparators(fi.absolutePath())));
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
    said_->setText(QStringLiteral("%1 file(s) found of %2 looked at%3%4")
                       .arg(table_->rowCount())
                       .arg(scanned_)
                       .arg(unreadable_ > 0 ? QStringLiteral(" - %1 could not be read inside (a scan, a locked file)").arg(unreadable_) : QString())
                       .arg(halt_ && halt_->load() ? QStringLiteral(" - stopped") : QString()));
    table_->resizeColumnToContents(FileCol);
    table_->resizeColumnToContents(HitsCol);
    table_->setColumnWidth(FoundCol, 360);
}

void DrawingFinderDialog::showResult(int row) {
    if (row < 0 || row >= table_->rowCount()) return;
    table_->selectRow(row);
    preview(resultPath(row));
}

void DrawingFinderDialog::open(const QString& path) {
    // A drawing in Edraw; a PDF or a list in what opens it.
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("pdf") || suffix == QLatin1String("csv")) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    else emit openRequested(path);
}

void DrawingFinderDialog::preview(const QString& path) {
    const QFileInfo fi(path);
    const QString suffix = fi.suffix().toLower();
    const Found f = found_.value(path);
    openPdf_->setEnabled(!ofKind(path, QStringLiteral("PDF")).isEmpty());
    openDxf_->setEnabled(!ofKind(path, QStringLiteral("DXF")).isEmpty());
    openDwx_->setEnabled(!ofKind(path, QStringLiteral("DWX")).isEmpty());
    openDwg_->setEnabled(!ofKind(path, QStringLiteral("DWG")).isEmpty());
    const int token = ++previewToken_;
    // WHERE IT IS FOUND, every place: what it is in, and the words round it.
    QString said = QStringLiteral("<b>%1</b><br><span style='color:#7a8696'>%2</span>")
                       .arg(fi.fileName().toHtmlEscaped(), QDir::toNativeSeparators(fi.absolutePath()).toHtmlEscaped());
    if (!f.drawingNo.isEmpty()) said += QStringLiteral("<br>Drawing No. <b>%1</b>").arg(f.drawingNo.toHtmlEscaped());
    if (!f.revision.isEmpty()) said += QStringLiteral("  rev <b>%1</b>").arg(f.revision.toHtmlEscaped());
    if (!f.title.isEmpty()) said += QStringLiteral("<br>%1").arg(f.title.toHtmlEscaped());
    for (auto it = f.beside.begin(); it != f.beside.end(); ++it)
        said += QStringLiteral("<br><span style='color:#2870c8'>Beside it: %1</span>").arg(QFileInfo(it.value()).fileName().toHtmlEscaped());
    if (!f.hits.isEmpty()) {
        said += QStringLiteral("<br><br><b>%1 place(s):</b>").arg(f.hits.size());
        int shown = 0;
        for (const Hit& h : f.hits) {
            if (++shown > 60) {
                said += QStringLiteral("<br>... and %1 more").arg(f.hits.size() - 60);
                break;
            }
            said += QStringLiteral("<br><span style='color:#7a8696'>%1:</span> %2<b style='background:%5'>%3</b>%4")
                        .arg(h.where.toHtmlEscaped(), h.before.toHtmlEscaped(), h.match.toHtmlEscaped(), h.after.toHtmlEscaped(),
                             h.exact ? QStringLiteral("#fff176") : QStringLiteral("#ffcc80"));
        }
    } else if (f.byName) {
        said += QStringLiteral("<br><br>Found in its name.");
    }
    if (!f.why.isEmpty()) said += QStringLiteral("<br><span style='color:#b07a10'>Not searched inside: %1</span>").arg(f.why.toHtmlEscaped());
    info_->setText(said);
    // THE DRAWING ITSELF: a DWX or DXF drawn; a PDF or a list - its places listed under; a DWG through the ODA converter.
    preview_->setCaption(fi.fileName());
    if (suffix == QLatin1String("dwx") || suffix == QLatin1String("dxf")) {
        Document doc(fi.completeBaseName());
        const bool ok = suffix == QLatin1String("dwx") ? dwx::load(doc, path) : dxf::importFile(doc, path);
        if (ok) {
            std::vector<Entity> all = doc.entities();
            if (all.empty())
                for (const DrawingLayout& l : doc.layouts()) all.insert(all.end(), l.paperEntities.begin(), l.paperEntities.end());
            preview_->setEntities(all);
        } else {
            preview_->setEntities({});
            preview_->setEmptyText(QStringLiteral("This file could not be read"));
        }
        return;
    }
    preview_->setEntities({});
    if (suffix == QLatin1String("csv")) {
        preview_->setEmptyText(QStringLiteral("A list - every place it is found is listed below; double-click to open it"));
        return;
    }
    if (suffix == QLatin1String("pdf")) {
        // A PDF DRAWN: its pages read as lines and text in the background - a big one takes a moment - shown when read,
        // unless another file has been chosen meanwhile.
        preview_->setEmptyText(QStringLiteral("Reading the PDF to show it..."));
        QPointer<DrawingFinderDialog> self(this);
        QThread* reader = QThread::create([self, path, token] {
            auto doc = std::make_shared<Document>(QFileInfo(path).completeBaseName());
            QString error;
            const bool ok = pdf::importFile(*doc, path, &error);
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [self, doc, ok, error, token] {
                    if (!self || token != self->previewToken_) return;
                    if (ok && !doc->entities().empty()) self->preview_->setEntities(doc->entities());
                    else self->preview_->setEmptyText(error.isEmpty() ? QStringLiteral("Nothing in the PDF to show") : error);
                },
                Qt::QueuedConnection);
        });
        connect(reader, &QThread::finished, reader, &QObject::deleteLater);
        reader->start();
        return;
    }
    if (!oda_) oda_ = new OdaConverter(this);
    if (!oda_->available() || oda_->busy()) {
        preview_->setEmptyText(oda_->available() ? QStringLiteral("Converting another DWG...")
                                                 : QStringLiteral("A DWG is shown through the ODA File Converter - install it to see DWG drawings here"));
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
}

QString DrawingFinderDialog::choose(QWidget* parent, const QString& text) {
    DrawingFinderDialog dlg(parent, true);
    dlg.setText(text);
    if (dlg.exec() != QDialog::Accepted) return QString();
    return dlg.chosen();
}

} // namespace Edraw_Desktop
