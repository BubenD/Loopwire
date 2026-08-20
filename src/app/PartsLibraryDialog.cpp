#include "app/PartsLibraryDialog.h"
#include <QToolButton>
#include "harness/PartDataFromWeb.h"
#include "app/PartWebDataDialog.h"
#include "harness/MechanicalHarness.h"

#include "app/DrawingPreview.h"
#include "app/UiBits.h"
#include "harness/CableSpec.h"
#include "app/CableBuilderDialog.h"
#include "harness/Datasheet.h"
#include "io/AppPaths.h"
#include "app/Editor.h"
#include "model/Block.h"
#include "model/Document.h"

#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDesktopServices>
#include <QDir>
#include <QUrl>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QHeaderView>
#include <QListWidget>
#include <QMenu>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace Edraw_Desktop {

namespace {

// A small labelled row in the parameter form. `wide` lets a field that holds a SENTENCE - a cable's
// structure, say - have the width of the form instead of the 260 px a number needs: a line of
// "3x24AWG + FTP 2x2x28AWG + coax, foil, braid, black" in a 260 px box shows about a third of itself.
QWidget* rowHost(QWidget* parent, const QString& label, QWidget* field, const QString& unit,
                 bool wide = false) {
    auto* host = new QWidget(parent);
    auto* lay = new QHBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    auto* cap = new QLabel(label, host);
    cap->setFixedWidth(150);
    cap->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
    lay->addWidget(cap);
    // The field takes a fixed share, so the unit after it stays on screen and the rows line up
    // however long a label is - unless it holds a sentence, in which case it takes what there is.
    if (!wide) field->setMaximumWidth(260);
    lay->addWidget(field, 1);
    auto* u = new QLabel(unit, host);
    u->setFixedWidth(42);
    u->setStyleSheet(QStringLiteral("color:#7f8896;"));
    lay->addWidget(u);
    if (!wide) lay->addStretch(1);
    return host;
}

} // namespace

PartsLibraryDialog::PartsLibraryDialog(Editor* editor, QWidget* parent)
    : QDialog(parent), editor_(editor) {
    setWindowTitle(QStringLiteral("Harness parts library"));
    resize(1180, 760);
    // Everything connected last time (and, on a first run, the default library seeded with real
    // parts). The one being worked on is the first of them until the chooser says otherwise.
    catalog_.openRemembered();
    path_ = catalog_.activePath();
    buildUi();
    // The chooser and the library list are FILLED here, not by buildUi - which only makes the widgets.
    // Without this the window opened with both of them empty while the status line said which library
    // was being worked in: everything was connected, nothing was listed. It went unnoticed because a
    // window opened through setLibraryPath (a driver, a test) fills them on the way past, and the
    // window a person opens does not.
    refreshLibraryChooser();
    reloadTree();
    const PartsLibrary merged = catalog_.merged();
    if (!merged.parts.empty()) selectPart(merged.parts.front().id);
    else showPart(QString());
}

void PartsLibraryDialog::setLibraryPath(const QString& path) {
    // Work on this one only - what a driver (or a test) asks for. The set of connected libraries is
    // NOT remembered from here: only the user connecting or disconnecting one changes that, so a
    // test run can never leave the application pointed at a file of its own.
    catalog_.setRemembering(false);
    const QStringList was = catalog_.paths();
    for (const QString& p : was)
        if (p != path) catalog_.disconnectLibrary(p);
    catalog_.connectLibrary(path);
    path_ = catalog_.activePath();
    refreshLibraryChooser();
    reloadTree();
    const PartsLibrary merged = catalog_.merged();
    showPart(merged.parts.empty() ? QString() : merged.parts.front().id);
}

namespace {

// The sign in the S / M column. A symbol drawn for this part is bright and filled; a kind's standard
// symbol is the same shape outlined, because the part can be placed but nobody has drawn it; nothing at
// all is a faint dash, which is different from a blank cell that might just be a rendering slip.
//
// mechanical: a body with a gland on it. Otherwise: the zigzag every schematic uses.
QIcon symbolSignIcon(bool mechanical, bool own, bool standard) {
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (!own && !standard) {
        p.setPen(QPen(QColor("#5c636d"), 1.6));
        p.drawLine(4, 8, 12, 8);
        return QIcon(pm);
    }
    const QColor ink(own ? "#7ee0a0" : "#8fb0d6");
    p.setPen(QPen(ink, 1.6));
    if (mechanical) {
        const QRectF body(2.5, 4.5, 8.0, 7.0);
        if (own) p.setBrush(QColor(126, 224, 160, 90));
        p.drawRoundedRect(body, 1.5, 1.5);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(10.5, 8.0), QPointF(14.0, 8.0));   // the tail out of it
    } else {
        // A resistor zigzag between two leads: the universal "schematic symbol" shape.
        QPolygonF zig({QPointF(1.5, 8), QPointF(4, 8), QPointF(5.2, 4.5), QPointF(7.4, 11.5),
                       QPointF(9.6, 4.5), QPointF(10.8, 8), QPointF(14.5, 8)});
        p.drawPolyline(zig);
        if (own) {
            p.setPen(Qt::NoPen);
            p.setBrush(ink);
            p.drawEllipse(QPointF(13.4, 12.4), 2.2, 2.2);   // a filled dot: drawn for this part
        }
    }
    return QIcon(pm);
}

// The DS sign: a sheet of paper with its corner turned, filled when the datasheet is there. Struck
// through in amber when the part points at a file that is not in the folder any more - which is a
// different thing from having none, and worth seeing without opening the part.
QIcon datasheetSignIcon(int attached, int onFile) {
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (attached == 0) {
        p.setPen(QPen(QColor("#5c636d"), 1.6));
        p.drawLine(4, 8, 12, 8);
        return QIcon(pm);
    }
    const bool missing = onFile < attached;
    const QColor ink(missing ? "#e6b45a" : "#7ee0a0");
    QPainterPath sheet;
    sheet.moveTo(3.5, 1.5);
    sheet.lineTo(9.5, 1.5);
    sheet.lineTo(12.5, 4.5);
    sheet.lineTo(12.5, 14.5);
    sheet.lineTo(3.5, 14.5);
    sheet.closeSubpath();
    p.setPen(QPen(ink, 1.4));
    p.setBrush(missing ? QBrush(Qt::NoBrush) : QBrush(QColor(126, 224, 160, 70)));
    p.drawPath(sheet);
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(9.5, 1.5), QPointF(9.5, 4.5));   // the folded corner
    p.drawLine(QPointF(9.5, 4.5), QPointF(12.5, 4.5));
    if (missing) {
        p.setPen(QPen(QColor("#e06c6c"), 1.6));
        p.drawLine(QPointF(2.0, 14.0), QPointF(14.0, 2.0));
    } else if (attached > 1) {
        p.setPen(QPen(ink, 1.0));
        p.drawLine(QPointF(2.0, 3.5), QPointF(2.0, 12.5));   // a second sheet behind the first
    }
    return QIcon(pm);
}

} // namespace

// The S / M / DS signs of one row: a symbol drawn for this part, the standard one its kind comes with,
// or nothing yet - the same for the mechanical drawing - and whether it has a datasheet on file.
void PartsLibraryDialog::setPartSigns(QTreeWidgetItem* item, const LibraryPart& part) {
    if (!item) return;
    const bool ownSchem = part.hasSchematic();
    const bool stdSchem = !ownSchem && !builtinSchematicSymbol(part.kind).isEmpty();
    item->setIcon(2, symbolSignIcon(false, ownSchem, stdSchem));
    item->setIcon(3, symbolSignIcon(true, part.hasMechanical(), false));
    // The datasheet: whether one is attached, and whether the file is actually still there - a part
    // pointing at a datasheet that has been deleted is not the same as a part with one, and the list
    // is where that is noticed.
    int onFile = 0;
    for (const QString& sheet : part.datasheets)
        if (DatasheetStore::exists(sheet)) ++onFile;
    const int attached = static_cast<int>(part.datasheets.size());
    item->setIcon(4, datasheetSignIcon(attached, onFile));
    item->setToolTip(4, attached == 0
                            ? QStringLiteral("No datasheet attached")
                            : onFile == attached
                                  ? (attached == 1
                                         ? QStringLiteral("A datasheet is attached")
                                         : QStringLiteral("%1 files attached").arg(attached))
                                  : QStringLiteral("%1 of %2 attached file(s) are missing from the "
                                                   "datasheet folder")
                                        .arg(attached - onFile)
                                        .arg(attached));
    item->setToolTip(2, ownSchem ? QStringLiteral("Has a schematic symbol of its own")
                    : stdSchem   ? QStringLiteral("Uses the standard symbol for its kind — it can be "
                                                  "placed on a schematic as it is")
                                 : QStringLiteral("No schematic symbol yet"));
    item->setToolTip(3, part.hasMechanical() ? QStringLiteral("Has a mechanical drawing")
                                             : QStringLiteral("No mechanical drawing yet"));
}

// Every library, connected or merely present. The connected ones keep the order they are searched in,
// because that is the order a part is found in; the rest follow, so what could be connected is as
// plain as what already is.
void PartsLibraryDialog::refreshLibraryList() {
    if (!libraryList_) return;
    const bool wasSyncing = syncing_;
    syncing_ = true;
    libraryList_->clear();
    const QStringList connected = catalog_.paths();
    const QString active = catalog_.activePath();
    int connectedParts = 0;

    auto row = [this](const QString& file, int parts, bool isConnected, bool isActive) {
        auto* it = new QTreeWidgetItem(libraryList_);
        // The file NAME, extension and all. A library is a file on disk and that is how it is found
        // again, renamed, backed up or sent to somebody - "parts-library" is not a thing you can look
        // for in a folder, "parts-library.plb" is. The whole path is on the tooltip.
        it->setText(0, QFileInfo(file).fileName());
        it->setText(1, parts >= 0 ? QString::number(parts) : QStringLiteral("—"));
        it->setText(2, !isConnected ? QStringLiteral("not connected")
                                    : isActive ? QStringLiteral("connected — working here")
                                               : QStringLiteral("connected"));
        it->setData(0, Qt::UserRole, file);
        it->setData(1, Qt::UserRole, isConnected);
        it->setToolTip(0, file);
        if (isActive) {
            QFont f = it->font(0);
            f.setBold(true);
            for (int c = 0; c < 3; ++c) it->setFont(c, f);
        }
        it->setForeground(2, QColor(isConnected ? (isActive ? "#7ee0a0" : "#8fb0d6") : "#8b929c"));
        return it;
    };

    for (const QString& file : connected) {
        int parts = -1;
        if (const PartsLibrary* lib = const_cast<PartsCatalog&>(catalog_).byPath(file)) {
            parts = static_cast<int>(lib->parts.size());
            connectedParts += parts;
        }
        row(file, parts, true, QFileInfo(file) == QFileInfo(active));
    }
    // What else is in the library folder. A file that will not load is still listed - being told it is
    // there and broken beats it silently not existing.
    int others = 0;
    QDir dir(AppPaths::libraryDir());
    for (const QString& name : dir.entryList({QStringLiteral("*.plb")}, QDir::Files, QDir::Name)) {
        const QString file = dir.absoluteFilePath(name);
        bool already = false;
        for (const QString& c : connected)
            if (QFileInfo(c) == QFileInfo(file)) already = true;
        if (already) continue;
        PartsLibrary probe;
        const bool ok = probe.load(file);
        row(file, ok ? static_cast<int>(probe.parts.size()) : -1, false, false);
        ++others;
    }
    if (libraryTotals_)
        libraryTotals_->setText(
            QStringLiteral("%1 connected (%2 part(s)), %3 more in the folder")
                .arg(connected.size())
                .arg(connectedParts)
                .arg(others));
    syncing_ = wasSyncing;
}

void PartsLibraryDialog::disconnectSelectedLibrary() {
    QTreeWidgetItem* it = libraryList_ ? libraryList_->currentItem() : nullptr;
    const QString file = it ? it->data(0, Qt::UserRole).toString() : QString();
    if (file.isEmpty()) {
        if (summary_) summary_->setText(QStringLiteral("Pick a library in the list first."));
        return;
    }
    if (!it->data(1, Qt::UserRole).toBool()) {
        if (summary_)
            summary_->setText(QStringLiteral("%1 is not connected.")
                                  .arg(QFileInfo(file).completeBaseName()));
        return;
    }
    if (catalog_.paths().size() < 2) {
        if (summary_) summary_->setText(QStringLiteral("The last library stays connected."));
        return;
    }
    catalog_.saveAll();   // its own edits are written before it is let go
    catalog_.disconnectLibrary(file);
    path_ = catalog_.activePath();
    catalog_.remember();
    refreshLibraryChooser();
    reloadTree();
    if (summary_)
        summary_->setText(QStringLiteral("%1 disconnected — the file is still in the folder.")
                              .arg(QFileInfo(file).completeBaseName()));
}

// The chooser lists every connected library by its file name, with the one being worked on picked.
// Every library there is to work in: the connected ones first, then the rest of what is in the library
// folder. Offering only the connected ones meant a library had to be connected before it could be
// chosen, which is a step nobody should have to know about - picking an unconnected one here connects
// it and works in it.
void PartsLibraryDialog::refreshLibraryChooser() {
    refreshLibraryList();
    if (!libraryBox_) return;
    syncing_ = true;
    libraryBox_->clear();
    for (const QString& p : catalog_.paths()) {
        const PartsLibrary* lib = const_cast<PartsCatalog&>(catalog_).byPath(p);
        libraryBox_->addItem(QStringLiteral("%1  (%2 parts)")
                                 .arg(QFileInfo(p).fileName())
                                 .arg(lib ? lib->parts.size() : 0),
                             p);
    }
    {
        // The others in the folder, marked as what they are. A file that will not load is still
        // offered - being told it is there and broken beats it silently not existing.
        QDir dir(AppPaths::libraryDir());
        bool separated = false;
        for (const QString& name :
             dir.entryList({QStringLiteral("*.plb")}, QDir::Files, QDir::Name)) {
            const QString file = dir.absoluteFilePath(name);
            bool already = false;
            for (const QString& c : catalog_.paths())
                if (QFileInfo(c) == QFileInfo(file)) already = true;
            if (already) continue;
            if (!separated) {
                libraryBox_->insertSeparator(libraryBox_->count());
                separated = true;
            }
            PartsLibrary probe;
            const bool ok = probe.load(file);
            libraryBox_->addItem(
                ok ? QStringLiteral("%1  (%2 parts — not connected)")
                         .arg(name)
                         .arg(probe.parts.size())
                   : QStringLiteral("%1  (not a library)").arg(name),
                file);
        }
    }
    const int at = libraryBox_->findData(catalog_.activePath());
    libraryBox_->setCurrentIndex(at >= 0 ? at : 0);
    syncing_ = false;
}

void PartsLibraryDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    auto* split = new QSplitter(Qt::Horizontal, this);
    root->addWidget(split, 1);
    // The parts list opens wide enough for its columns - the two sign columns included. Left to
    // itself the splitter halves the window and the signs end up beyond the right-hand edge.
    split->setSizes({560, 620});
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);

    // ---- Left: the parts, by kind ----
    auto* left = new QWidget(split);
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    // Which library is being worked on, and which others are connected. Several can be open at
    // once - a project's own beside a company one - and everything that only READS parts (the bill
    // of materials, the harness builder) sees all of them together.
    {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        auto* cap = new QLabel(QStringLiteral("Working in"), left);
        cap->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
        row->addWidget(cap);
        libraryBox_ = new QComboBox(left);
        libraryBox_->setToolTip(QStringLiteral(
            "The library a new part is added to. Every connected library is searched and listed; "
            "a part is always edited in the library it came from."));
        connect(libraryBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
            if (syncing_ || i < 0) return;
            const QString file = libraryBox_->itemData(i).toString();
            if (file.isEmpty()) return;   // the separator
            // One that is not connected yet is connected by being chosen: "work in this one" is the
            // whole of what a person means by picking it, and asking them to connect it first is a
            // step that explains nothing.
            catalog_.saveAll();
            bool connected = false;
            for (const QString& p : catalog_.paths())
                if (QFileInfo(p) == QFileInfo(file)) connected = true;
            if (!connected) catalog_.connectLibrary(file);
            catalog_.setActivePath(file);
            path_ = catalog_.activePath();
            catalog_.remember();
            refreshLibraryChooser();
            reloadTree();
        });
        row->addWidget(libraryBox_, 1);
        auto* connectBtn = new QPushButton(QStringLiteral("Connect…"), left);
        connectBtn->setToolTip(QStringLiteral("Open another library alongside this one (.plb)"));
        connect(connectBtn, &QPushButton::clicked, this, [this] {
            const QString file =
                QFileDialog::getOpenFileName(this, QStringLiteral("Connect a part library"),
                                             QFileInfo(path_).absolutePath(),
                                             PartsLibrary::fileFilter());
            if (file.isEmpty()) return;
            catalog_.saveAll();
            catalog_.connectLibrary(file);
            path_ = catalog_.activePath();
            refreshLibraryChooser();
            reloadTree();
        });
        row->addWidget(connectBtn);
        auto* dropBtn = new QPushButton(QStringLiteral("Disconnect"), left);
        dropBtn->setToolTip(QStringLiteral("Stop working with the library shown here"));
        connect(dropBtn, &QPushButton::clicked, this, [this] {
            if (catalog_.paths().size() < 2) {
                if (summary_)
                    summary_->setText(QStringLiteral("The last library stays connected"));
                return;
            }
            catalog_.saveAll();
            catalog_.disconnectLibrary(catalog_.activePath());
            path_ = catalog_.activePath();
            refreshLibraryChooser();
            reloadTree();
        });
        row->addWidget(dropBtn);
        lv->addLayout(row);
    }
    // ---- Every library: which are connected, and what else there is ----
    // "Connected" is not obvious from a chooser that shows one name at a time, and a library
    // connected by accident could not be found again to disconnect it. This lists the lot - the
    // connected ones first, then whatever else is in the library folder - and any row can be
    // connected, disconnected or made the one being worked in.
    {
        libraryList_ = new QTreeWidget(left);
        libraryList_->setHeaderLabels({QStringLiteral("Library"), QStringLiteral("Parts"),
                                       QStringLiteral("State")});
        libraryList_->setRootIsDecorated(false);
        libraryList_->setColumnWidth(0, 190);
        libraryList_->setColumnWidth(1, 52);
        libraryList_->setMinimumHeight(96);
        libraryList_->setMaximumHeight(150);
        libraryList_->setToolTip(QStringLiteral(
            "Every part library: the connected ones, and the others in the library folder. "
            "Double-click one to connect it, or to work in it when it is already connected."));
        connect(libraryList_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it) {
            if (!it) return;
            const QString file = it->data(0, Qt::UserRole).toString();
            if (file.isEmpty()) return;
            catalog_.saveAll();
            if (it->data(1, Qt::UserRole).toBool()) {   // connected: work in it
                catalog_.setActivePath(file);
            } else {
                catalog_.connectLibrary(file);
            }
            path_ = catalog_.activePath();
            catalog_.remember();
            refreshLibraryChooser();
            reloadTree();
        });
        lv->addWidget(libraryList_);

        auto* libRow = new QHBoxLayout;
        libRow->setContentsMargins(0, 0, 0, 0);
        libraryTotals_ = new QLabel(left);
        libraryTotals_->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
        libRow->addWidget(libraryTotals_, 1);
        auto* dropSel = new QPushButton(QStringLiteral("Disconnect selected"), left);
        dropSel->setToolTip(QStringLiteral(
            "Stop working with the library selected above. The file is left exactly where it is - "
            "disconnecting is not deleting."));
        connect(dropSel, &QPushButton::clicked, this, [this] { disconnectSelectedLibrary(); });
        libRow->addWidget(dropSel);
        auto* folderBtn = new QPushButton(QStringLiteral("Folder"), left);
        folderBtn->setToolTip(QStringLiteral("Open the library folder"));
        connect(folderBtn, &QPushButton::clicked, this, [this] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::libraryDir()));
        });
        libRow->addWidget(folderBtn);
        lv->addLayout(libRow);
    }
    filter_ = new QLineEdit(left);
    filter_->setPlaceholderText(QStringLiteral("Filter by description, P/N or manufacturer"));
    connect(filter_, &QLineEdit::textChanged, this, [this] { reloadTree(); });
    lv->addWidget(filter_);
    tree_ = new QTreeWidget(left);
    // Two narrow columns of signs: has a schematic symbol, has a mechanical drawing. Which parts are
    // finished and which are still just a row of numbers is the thing one most wants to see down a
    // library list, and a tooltip only says it one part at a time.
    tree_->setHeaderLabels({QStringLiteral("Part"), QStringLiteral("Manufacturer"),
                            QStringLiteral("S"), QStringLiteral("M"), QStringLiteral("DS")});
    tree_->headerItem()->setToolTip(2, QStringLiteral("Schematic symbol"));
    tree_->headerItem()->setToolTip(3, QStringLiteral("Mechanical drawing"));
    tree_->headerItem()->setToolTip(4, QStringLiteral("Datasheet attached"));
    // ---- L3: the columns as they should open ----
    // The name column takes what it needs and no more, the manufacturer stretches, and the two sign
    // columns are fixed and narrow - so S and M are on screen the moment the window opens instead of
    // being pushed off the right-hand edge by a column that grew.
    tree_->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    tree_->header()->setSectionResizeMode(3, QHeaderView::Fixed);
    tree_->header()->setSectionResizeMode(4, QHeaderView::Fixed);
    tree_->header()->setStretchLastSection(false);
    tree_->setColumnWidth(0, 250);
    tree_->setColumnWidth(2, 30);
    tree_->setColumnWidth(3, 30);
    tree_->setColumnWidth(4, 34);
    // The selected part has to be obvious: a row highlighted in the theme's faintest grey is not,
    // especially in a list of fifty where the eye has to come back to it after reading the form.
    tree_->setStyleSheet(QStringLiteral(
        "QTreeWidget::item:selected{background:#2f6b46;color:#ffffff;}"
        "QTreeWidget::item:selected:!active{background:#2f6b46;color:#ffffff;}"
        "QTreeWidget::item:hover{background:#33414f;}"
        "QTreeWidget::item{padding:2px 0px;}"));
    tree_->setRootIsDecorated(true);
    connect(tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* it, QTreeWidgetItem*) {
                if (syncing_ || !it) return;
                const QString id = it->data(0, Qt::UserRole).toString();
                if (!id.isEmpty()) showPart(id);
            });
    lv->addWidget(tree_, 1);

    auto* addRow = new QWidget(left);
    auto* ar = new QHBoxLayout(addRow);
    ar->setContentsMargins(0, 0, 0, 0);
    auto* newKind = new QComboBox(addRow);
    for (PartKind k : allPartKinds()) newKind->addItem(partKindName(k), partKindKey(k));
    ar->addWidget(newKind, 1);
    auto* addBtn = makeAddButton(addRow, QStringLiteral("New part"),
                                QStringLiteral("Add a part of the kind chosen beside"));
    connect(addBtn, &QPushButton::clicked, this, [this, newKind] {
        LibraryPart* p = addPart(partKindFromKey(newKind->currentData().toString()));
        if (p) selectPart(p->id);
    });
    ar->addWidget(addBtn);
    auto* dupBtn = new QPushButton(QStringLiteral("Duplicate"), addRow);
    connect(dupBtn, &QPushButton::clicked, this, [this] { duplicateCurrentPart(); });
    ar->addWidget(dupBtn);
    auto* delBtn = makeRemoveButton(addRow, QStringLiteral("Delete"),
                                   QStringLiteral("Take this part out of the library"), true);
    connect(delBtn, &QPushButton::clicked, this, [this] { deleteCurrentPart(); });
    ar->addWidget(delBtn);
    lv->addWidget(addRow);
    split->addWidget(left);

    // ---- Right: the part ----
    auto* right = new QWidget(split);
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);

    auto* head = new QGroupBox(QStringLiteral("Part"), right);
    auto* hg = new QGridLayout(head);
    hg->setContentsMargins(8, 8, 8, 8);
    hg->setHorizontalSpacing(8);
    int hr = 0;
    auto headRow = [&](const QString& label, QWidget* field) {
        auto* cap = new QLabel(label, head);
        cap->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
        hg->addWidget(cap, hr, 0);
        hg->addWidget(field, hr, 1);
        ++hr;
    };
    kindBox_ = new QComboBox(head);
    for (PartKind k : allPartKinds()) kindBox_->addItem(partKindName(k), partKindKey(k));
    connect(kindBox_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (syncing_) return;
        if (LibraryPart* p = current()) {
            p->kind = partKindFromKey(kindBox_->currentData().toString());
            // A different kind is issued differently: the unit follows unless it was set by hand.
            if (p->unit.isEmpty()) p->unit = defaultUnitForKind(p->kind);
            rebuildParamForm(); // a different kind asks for different parameters
            touched();
        }
    });
    headRow(QStringLiteral("Type"), kindBox_);
    descEdit_ = new QLineEdit(head);
    descEdit_->setPlaceholderText(QStringLiteral("what the part is"));
    connect(descEdit_, &QLineEdit::textChanged, this, [this](const QString& t) {
        if (syncing_) return;
        if (LibraryPart* p = current()) { p->description = t; touched(); }
    });
    {
        // Writing the description out by hand for every part is work the technical data already
        // did: this reads the parameters that are filled in and writes the sentence a catalogue
        // would - and it only ever names what is actually there.
        auto* host = new QWidget(head);
        auto* hl = new QHBoxLayout(host);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(4);
        hl->addWidget(descEdit_, 1);
        auto* fromData = new QPushButton(QStringLiteral("From data"), host);
        fromData->setToolTip(QStringLiteral(
            "Write the description from this part's technical parameters"));
        connect(fromData, &QPushButton::clicked, this, [this] { describeFromData(); });
        hl->addWidget(fromData);
        headRow(QStringLiteral("Description"), host);
    }
    mfrEdit_ = new QLineEdit(head);
    connect(mfrEdit_, &QLineEdit::textChanged, this, [this](const QString& t) {
        if (syncing_) return;
        if (LibraryPart* p = current()) { p->mfrName = t; touched(); }
    });
    headRow(QStringLiteral("Mfr name"), mfrEdit_);
    pnEdit_ = new QLineEdit(head);
    connect(pnEdit_, &QLineEdit::textChanged, this, [this](const QString& t) {
        if (syncing_) return;
        if (LibraryPart* p = current()) { p->mfrPn = t; touched(); }
    });
    headRow(QStringLiteral("Mfr P/N"), pnEdit_);
    internalEdit_ = new QLineEdit(head);
    internalEdit_->setPlaceholderText(QStringLiteral("the house part number, if there is one"));
    connect(internalEdit_, &QLineEdit::textChanged, this, [this](const QString& t) {
        if (syncing_) return;
        if (LibraryPart* p = current()) { p->internalPn = t; touched(); }
    });
    headRow(QStringLiteral("Internal P/N"), internalEdit_);
    // The part's own page - the manufacturer's, or the distributor's listing. Every part has one, and it is
    // asked for in the same breath as the part number, so it is a field of the part and not a parameter of
    // some kinds of part. Beside it: open it, and read what can be read off it.
    {
        auto* host = new QWidget(head);
        auto* hl = new QHBoxLayout(host);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(4);
        linkEdit_ = new QLineEdit(host);
        linkEdit_->setPlaceholderText(QStringLiteral("https://…  the manufacturer's page for this part"));
        connect(linkEdit_, &QLineEdit::textChanged, this, [this](const QString& t) {
            if (syncing_) return;
            if (LibraryPart* p = current()) { p->link = t.trimmed(); touched(); }
        });
        hl->addWidget(linkEdit_, 1);
        auto* open = new QPushButton(QStringLiteral("Open"), host);
        open->setToolTip(QStringLiteral("Open this part's page in the browser"));
        connect(open, &QPushButton::clicked, this, [this] { openPartLink(); });
        hl->addWidget(open);
        auto* fetch = new QPushButton(QStringLiteral("Read data…"), host);
        fetch->setToolTip(QStringLiteral(
            "Read the page and offer what can be found on it — the gauge, the ratio, the way count, the "
            "temperature range, whatever that kind of part is described by.\n"
            "Nothing is written until it is ticked: a reading off a page is a suggestion, and it is shown "
            "with the words it came from."));
        connect(fetch, &QPushButton::clicked, this, [this] { fetchDataFromLink(); });
        hl->addWidget(fetch);
        headRow(QStringLiteral("Web link"), host);
    }
    // What the part is issued in. A bill of materials totals quantities by it, so it decides what
    // can be added to what: wire by the metre, a connector each, heatshrink by the millimetre.
    unitBox_ = new QComboBox(head);
    unitBox_->setEditable(true);
    for (const PartUnit& u : partUnits())
        unitBox_->addItem(QStringLiteral("%1 — %2").arg(u.code, u.label), u.code);
    unitBox_->setToolTip(QStringLiteral(
        "The unit this part is bought and counted in. The bill of materials adds up quantities in "
        "it, and can only add units of the same kind (mm to m, never m to each)."));
    connect(unitBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (syncing_ || i < 0) return;
        if (LibraryPart* p = current()) { p->unit = unitBox_->itemData(i).toString(); touched(); }
    });
    headRow(QStringLiteral("Unit"), unitBox_);

    // The datasheet. Attached rather than linked: the file is copied into the datasheet folder, so a
    // library still has its datasheets when the folder it was downloaded to is long gone.
    {
        auto* dsRow = new QWidget(head);
        auto* dsLay = new QHBoxLayout(dsRow);
        dsLay->setContentsMargins(0, 0, 0, 0);
        dsLay->setSpacing(4);
        // The files themselves, one per row: a part may have a datasheet, an outline drawing and a
        // certificate, and they are all worth keeping with it.
        datasheetList_ = new QListWidget(dsRow);
        datasheetList_->setMaximumHeight(74);
        datasheetList_->setToolTip(QStringLiteral(
            "The files this part points at. One datasheet can serve a whole family of parts: removing "
            "it from one part keeps the file for the others, and says which they are.\n"
            "Double-click to open one; right-click for its folder."));
        datasheetList_->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(datasheetList_, &QListWidget::itemDoubleClicked, this,
                [this](QListWidgetItem*) { openDatasheet(); });
        connect(datasheetList_, &QListWidget::customContextMenuRequested, this,
                [this](const QPoint& at) {
                    if (selectedDatasheet().isEmpty()) return;
                    QMenu menu(this);
                    menu.addAction(QStringLiteral("Open"), this, [this] { openDatasheet(); });
                    menu.addAction(QStringLiteral("Show in folder"), this,
                                   [this] { showDatasheetInFolder(); });
                    menu.addSeparator();
                    menu.addAction(QStringLiteral("Read the technical data from it"), this,
                                   [this] { pullFromDatasheet(); });
                    menu.addSeparator();
                    menu.addAction(QStringLiteral("Take off this part"), this,
                                   [this] { removeDatasheet(); });
                    menu.exec(datasheetList_->viewport()->mapToGlobal(at));
                });
        dsLay->addWidget(datasheetList_, 1);

        auto* dsBtns = new QWidget(dsRow);
        auto* dsb = new QVBoxLayout(dsBtns);
        dsb->setContentsMargins(0, 0, 0, 0);
        dsb->setSpacing(2);
        attachBtn_ = new QPushButton(QStringLiteral("Attach…"), dsBtns);
        attachBtn_->setToolTip(QStringLiteral("Add a file — as many as the part needs"));
        connect(attachBtn_, &QPushButton::clicked, this, [this] { attachDatasheet(); });
        dsb->addWidget(attachBtn_);
        openDsBtn_ = new QPushButton(QStringLiteral("Open"), dsBtns);
        connect(openDsBtn_, &QPushButton::clicked, this, [this] { openDatasheet(); });
        dsb->addWidget(openDsBtn_);
        auto* pullBtn = new QPushButton(QStringLiteral("Read data"), dsBtns);
        pullBtn->setToolTip(QStringLiteral(
            "Read the technical fields out of the attached file again. Worth doing on a datasheet "
            "attached before — nothing is read from a file until it is asked for."));
        connect(pullBtn, &QPushButton::clicked, this, [this] { pullFromDatasheet(); });
        dsb->addWidget(pullBtn);
        removeDsBtn_ = new QPushButton(QStringLiteral("Remove"), dsBtns);
        connect(removeDsBtn_, &QPushButton::clicked, this, [this] { removeDatasheet(); });
        dsb->addWidget(removeDsBtn_);
        dsLay->addWidget(dsBtns);
        datasheetLabel_ = new QLabel(QString(), dsRow);
        datasheetLabel_->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
        datasheetLabel_->hide();   // what it used to say is in the rows themselves now
        headRow(QStringLiteral("Datasheets"), dsRow);
    }
    rv->addWidget(head);

    // The technical parameters of this kind, in a scroller: some kinds have many.
    auto* paramBox = new QGroupBox(QStringLiteral("Technical parameters"), right);
    auto* pbLay = new QVBoxLayout(paramBox);
    pbLay->setContentsMargins(8, 8, 8, 8);
    auto* scroll = new QScrollArea(paramBox);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    paramHost_ = new QWidget(scroll);
    paramLay_ = new QVBoxLayout(paramHost_);
    paramLay_->setContentsMargins(0, 0, 0, 0);
    paramLay_->setSpacing(4);
    scroll->setWidget(paramHost_);
    pbLay->addWidget(scroll);
    rv->addWidget(paramBox, 1);

    // The two drawings.
    auto* draw = new QGroupBox(QStringLiteral("Drawings"), right);
    auto* dg = new QGridLayout(draw);
    dg->setContentsMargins(8, 8, 8, 8);
    struct Side {
        const char* caption;
        bool mechanical;
    };
    // Three drawings of a part, side by side: what it is on a circuit, what it is in the metal, and what
    // the harness does at it. Each pane is given room enough to read - a drawing squeezed into a thumbnail
    // is a drawing nobody can check.
    const Side sides[] = {{"Schematic symbol", false}, {"Mechanical drawing", true},
                          {"Fanout", false}};
    // THE EYE, over all three drawings at once. The connection point is drawn ORANGE over the part - the line
    // the harness attaches to - and while the drawing itself is being checked that mark is in the way of seeing
    // it: an orange line reads as part of the part. So it goes, and comes back. One button for the three panes
    // because it is one question about one part, and the same button the model manager has, remembering the
    // same answer.
    showConnBtn_ = makeEyeToggle(
        draw, QStringLiteral("Connection points shown"), QStringLiteral("Connection points hidden"),
        QStringLiteral("Show or hide the connection point on these drawings - the line the harness attaches "
                       "to, drawn orange. It is a mark ON the part, not part of it, and while the drawing is "
                       "being checked it is in the way of seeing what the part actually looks like."));
    connect(showConnBtn_, &QToolButton::toggled, this, [this] { applyShowConnection(); });
    dg->addWidget(showConnBtn_, 3, 0, 1, 3, Qt::AlignLeft);
    for (int i = 0; i < 3; ++i) {
        const bool isFanout = i == 2;
        auto* prev = new DrawingPreview(QString::fromLatin1(sides[i].caption), draw);
        // Room enough to see what the drawing IS, and no more: three of these side by side, and the window
        // has the part's data to show as well.
        prev->setMinimumSize(210, 170);
        if (isFanout) fanoutPreview_ = prev;
        else if (sides[i].mechanical) mechPreview_ = prev;
        else schemPreview_ = prev;
        dg->addWidget(prev, 0, i);
        if (isFanout) {
            // Which side of the connector it was drawn from. A fanout is only meaningful with that said:
            // the same drawing is the front view or the back view depending on it, and the other side is
            // the same drawing mirrored.
            auto* sideRow = new QWidget(draw);
            fanoutSideRow_ = sideRow;
            auto* sl = new QHBoxLayout(sideRow);
            sl->setContentsMargins(0, 0, 0, 0);
            sl->addWidget(new QLabel(QStringLiteral("Drawn from the"), sideRow));
            fanoutSideBox_ = new QComboBox(sideRow);
            fanoutSideBox_->addItem(QStringLiteral("front — looking at the face"),
                                    QStringLiteral("front"));
            fanoutSideBox_->addItem(QStringLiteral("back — looking at the wires"),
                                    QStringLiteral("back"));
            connect(fanoutSideBox_, &QComboBox::currentIndexChanged, this, [this](int) {
                if (syncing_) return;
                if (LibraryPart* p = current()) {
                    p->setFanoutSide(fanoutSideBox_->currentData().toString());
                    touched();
                }
            });
            sl->addWidget(fanoutSideBox_, 1);
            dg->addWidget(sideRow, 2, i);
        }
        auto* btns = new QWidget(draw);
        auto* bl = new QHBoxLayout(btns);
        bl->setContentsMargins(0, 0, 0, 0);
        const bool mech = sides[i].mechanical;
        if (isFanout) {
            // Its own four: take it off the sheet, put it down as drawn, put it down mirrored for the other
            // side of the connector, and throw it away.
            auto* cap = new QPushButton(QStringLiteral("Capture selection"), btns);
            cap->setToolTip(QStringLiteral("Take what is selected on the sheet as this part's fanout"));
            connect(cap, &QPushButton::clicked, this, [this] { beginCaptureFanout(); });
            bl->addWidget(cap);
            auto* ins = new QPushButton(QStringLiteral("Insert"), btns);
            ins->setToolTip(QStringLiteral("Put the fanout on the sheet, as it was drawn"));
            connect(ins, &QPushButton::clicked, this, [this] { insertFanout(false); });
            bl->addWidget(ins);
            auto* mir = new QPushButton(QStringLiteral("Insert mirrored"), btns);
            mir->setToolTip(QStringLiteral(
                "Put it on the sheet mirrored about the X axis — the same fanout seen from the other side of "
                "the connector, front for back and back for front."));
            connect(mir, &QPushButton::clicked, this, [this] { insertFanout(true); });
            bl->addWidget(mir);
            auto* clr = new QPushButton(QStringLiteral("Clear"), btns);
            connect(clr, &QPushButton::clicked, this, [this] {
                if (LibraryPart* p = current()) {
                    p->fanout = QJsonArray();
                    if (fanoutPreview_) fanoutPreview_->setDrawing(p->fanout);
                    touched();
                    reloadTree();
                }
            });
            bl->addWidget(clr);
            dg->addWidget(btns, 1, i);
            continue;
        }
        auto* cap = new QPushButton(QStringLiteral("Capture selection"), btns);
        cap->setToolTip(QStringLiteral(
            "Take what is selected on the sheet as this part's %1 drawing")
                            .arg(mech ? QStringLiteral("mechanical") : QStringLiteral("schematic")));
        cap->setToolTip(cap->toolTip() +
                        QStringLiteral("\nWith nothing selected, this window steps aside so the "
                                       "objects can be picked; Enter then takes them."));
        connect(cap, &QPushButton::clicked, this, [this, mech] { beginCaptureFromSheet(mech); });
        bl->addWidget(cap);
        auto* ins = new QPushButton(QStringLiteral("Insert"), btns);
        ins->setToolTip(QStringLiteral("Put this drawing on the sheet — then click where it goes"));
        connect(ins, &QPushButton::clicked, this, [this, mech] { insertIntoDrawing(mech); });
        bl->addWidget(ins);
        auto* edt = new QPushButton(QStringLiteral("Edit"), btns);
        edt->setToolTip(QStringLiteral(
            "Put this drawing on the sheet and select it, to be worked on — add or move lines, and "
            "mark the line the harness attaches to (Harness ▸ connection line). Capture selection "
            "puts it back on the part."));
        connect(edt, &QPushButton::clicked, this, [this, mech] { editDrawing(mech); });
        bl->addWidget(edt);
        if (!mech) {
            // Only on the schematic side, and only for a connector: this is the one drawing that can be
            // had from the data rather than drawn - a body, a stub per way, the numbers - and typing it out
            // by hand for a 61-way connector is work nobody should do twice.
            auto* gen = new QPushButton(QStringLiteral("From data"), btns);
            gen->setToolTip(QStringLiteral(
                "Build the schematic symbol from the part's own data: a connector of its way count, at its "
                "pitch, numbered — as a real connector object, so it can be wired and edited after it is "
                "placed."));
            connect(gen, &QPushButton::clicked, this, [this] { generateConnectorSymbol(); });
            genSymbolBtn_ = gen;
            bl->addWidget(gen);
        }
        auto* clr = new QPushButton(QStringLiteral("Clear"), btns);
        connect(clr, &QPushButton::clicked, this, [this, mech] { clearDrawing(mech); });
        bl->addWidget(clr);
        dg->addWidget(btns, 1, i);
    }
    // The remembered answer, given to the three panes now they exist: a preference that is read back but not
    // applied until the button is next pressed is a preference that looks forgotten.
    applyShowConnection();
    // ---- The pin contact this connector is used with ----
    //
    // ONE, and for both drawings: a connector is built with one kind of contact in it, and which one is a
    // property of the connector rather than of the moment it is placed. So it is set here, once, and every
    // copy put on a sheet carries it - which is what puts it on the line below the connector on the bill of
    // materials, in the quantity the connector needs.
    {
        auto* group = new QGroupBox(QStringLiteral("Use with pin contact"), right);
        contactGroupBox_ = group;
        contactGroup_ = group;
        auto* cl = new QHBoxLayout(group);
        contactBox_ = new QComboBox(group);
        contactBox_->setToolTip(QStringLiteral(
            "The contact ordered and crimped into this connector. It goes on the bill of materials under "
            "the connector, one per way, wherever the connector is placed."));
        connect(contactBox_, &QComboBox::currentIndexChanged, this, [this](int) {
            if (syncing_) return;
            LibraryPart* p = current();
            if (!p) return;
            const QString other = contactBox_->currentData().toString();
            const bool isContact = p->kind == PartKind::ContactPin || p->kind == PartKind::Crimp;
            // Both ends of the tie, in the library being worked in. Tied from the contact's side or the
            // connector's, it is the same tie and it is written on both parts.
            if (isContact) {
                p->setConnectorId(other);
                tieContactToConnector(catalog_.active(), other, p->id);
            } else {
                tieContactToConnector(catalog_.active(), p->id, other);
            }
            touched();
            reloadTree();
        });
        cl->addWidget(contactBox_, 1);
        rv->addWidget(group);
    }

    rv->addWidget(draw);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);

    auto* foot = new QHBoxLayout;
    summary_ = new QLabel(QString(), this);
    summary_->setStyleSheet(QStringLiteral("color:#8fb0d6;"));
    foot->addWidget(summary_, 1);
    auto* openBtn = new QPushButton(QStringLiteral("Open…"), this);
    openBtn->setToolTip(QStringLiteral("Open another part library (.plb)"));
    connect(openBtn, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Open part library"),
                                                          QFileInfo(path_).absolutePath(),
                                                          PartsLibrary::fileFilter());
        if (file.isEmpty()) return;
        saveLibrary(); // the one open now is kept before the other is loaded
        setLibraryPath(file);
    });
    foot->addWidget(openBtn);
    auto* saveAsBtn = new QPushButton(QStringLiteral("Save as…"), this);
    connect(saveAsBtn, &QPushButton::clicked, this, [this] {
        QString file = QFileDialog::getSaveFileName(this, QStringLiteral("Save part library"),
                                                    path_, PartsLibrary::fileFilter());
        if (file.isEmpty()) return;
        if (!file.endsWith(QStringLiteral(".plb"), Qt::CaseInsensitive))
            file += QStringLiteral(".plb");
        path_ = file;
        if (saveLibrary()) reloadTree(); // the footer names the file it is kept in
    });
    foot->addWidget(saveAsBtn);
    auto* saveBtn = new QPushButton(QStringLiteral("Save library"), this);
    connect(saveBtn, &QPushButton::clicked, this, [this] {
        if (saveLibrary())
            summary_->setText(QStringLiteral("Saved %1 part(s) across %2 library/ies")
                                  .arg(catalog_.partCount())
                                  .arg(catalog_.paths().size()));
        else
            summary_->setText(QStringLiteral("Could not write %1").arg(path_));
    });
    foot->addWidget(saveBtn);
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, [this] {
        saveLibrary(); // the library is a file, not a document: closing keeps it
        accept();
    });
    foot->addWidget(closeBtn);
    root->addLayout(foot);
}

void PartsLibraryDialog::reloadTree() {
    if (!tree_) return;
    const QString keep = currentId_;
    syncing_ = true;
    tree_->clear();
    const QString needle = filter_ ? filter_->text().trimmed() : QString();
    for (PartKind k : allPartKinds()) {
        const std::vector<const LibraryPart*> parts = catalog_.ofKind(k);
        std::vector<const LibraryPart*> shown;
        for (const LibraryPart* p : parts) {
            if (needle.isEmpty() ||
                p->description.contains(needle, Qt::CaseInsensitive) ||
                p->mfrPn.contains(needle, Qt::CaseInsensitive) ||
                p->mfrName.contains(needle, Qt::CaseInsensitive))
                shown.push_back(p);
        }
        if (shown.empty()) continue; // a kind with nothing in it is not listed
        auto* head = new QTreeWidgetItem(tree_);
        head->setText(0, QStringLiteral("%1  (%2)").arg(partKindName(k)).arg(shown.size()));
        head->setFirstColumnSpanned(false);
        QFont f = head->font(0);
        f.setBold(true);
        head->setFont(0, f);
        for (const LibraryPart* p : shown) {
            auto* it = new QTreeWidgetItem(head);
            it->setText(0, p->title());
            it->setText(1, p->mfrName);
            it->setData(0, Qt::UserRole, p->id);
            setPartSigns(it, *p);
            // The columns are narrow and a part number is long, so a row shows what it can and says
            // the rest on hover - the whole title, who makes it, and what it is.
            QStringList told;
            told << p->title();
            if (!p->mfrName.isEmpty()) told << p->mfrName;
            if (!p->mfrPn.isEmpty()) told << QStringLiteral("P/N %1").arg(p->mfrPn);
            if (!p->internalPn.isEmpty()) told << QStringLiteral("internal %1").arg(p->internalPn);
            if (!p->description.isEmpty() && p->description != p->title()) told << p->description;
            const QString whole = told.join(QStringLiteral("\n"));
            it->setToolTip(0, whole);
            it->setToolTip(1, whole);
        }
        head->setExpanded(true);
    }
    syncing_ = false;
    if (!keep.isEmpty()) selectPart(keep);
    if (summary_)
        summary_->setText(QStringLiteral("%1 part(s) in %2 library/ies — working in %3")
                              .arg(catalog_.partCount())
                              .arg(catalog_.paths().size())
                              .arg(QFileInfo(path_).fileName()));
}

void PartsLibraryDialog::selectPart(const QString& id) {
    if (!tree_) return;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* head = tree_->topLevelItem(i);
        for (int j = 0; j < head->childCount(); ++j)
            if (head->child(j)->data(0, Qt::UserRole).toString() == id) {
                syncing_ = true;
                tree_->setCurrentItem(head->child(j));
                syncing_ = false;
                showPart(id);
                return;
            }
    }
    showPart(id); // not in the list (filtered out): still show it
}

LibraryPart* PartsLibraryDialog::current() {
    // The part is edited in the library it came from, whichever that is.
    if (PartsLibrary* own = catalog_.byPath(catalog_.pathOf(currentId_)))
        return own->byId(currentId_);
    return catalog_.active().byId(currentId_);
}

void PartsLibraryDialog::showPart(const QString& id) {
    currentId_ = id;
    const LibraryPart* p = catalog_.byId(id);
    syncing_ = true;
    const bool have = p != nullptr;
    if (kindBox_) {
        kindBox_->setEnabled(have);
        if (have) kindBox_->setCurrentIndex(std::max(0, kindBox_->findData(partKindKey(p->kind))));
    }
    if (descEdit_) {
        descEdit_->setEnabled(have);
        descEdit_->setText(have ? p->description : QString());
    }
    if (mfrEdit_) {
        mfrEdit_->setEnabled(have);
        mfrEdit_->setText(have ? p->mfrName : QString());
    }
    if (pnEdit_) {
        pnEdit_->setEnabled(have);
        pnEdit_->setText(have ? p->mfrPn : QString());
    }
    if (linkEdit_) {
        linkEdit_->setEnabled(have);
        linkEdit_->setText(have ? p->link : QString());
    }
    if (internalEdit_) {
        internalEdit_->setEnabled(have);
        internalEdit_->setText(have ? p->internalPn : QString());
    }
    if (unitBox_) {
        unitBox_->setEnabled(have);
        const QString unit = have ? (p->unit.isEmpty() ? defaultUnitForKind(p->kind) : p->unit)
                                  : QString();
        const int at = unitBox_->findData(unit);
        if (at >= 0) unitBox_->setCurrentIndex(at);
        else if (have) unitBox_->setCurrentText(unit);
    }
    // A component of a standard kind shows its symbol straight away; capturing one for the part
    // replaces it.
    if (schemPreview_) schemPreview_->setDrawing(have ? partSchematicDrawing(*p) : QJsonArray());
    if (fanoutPreview_) fanoutPreview_->setDrawing(p ? p->fanout : QJsonArray());
    if (fanoutSideBox_ && p) {
        const QSignalBlocker block(fanoutSideBox_);
        const int at = fanoutSideBox_->findData(p->fanoutSide());
        fanoutSideBox_->setCurrentIndex(at >= 0 ? at : 0);
    }
    refreshDatasheetRow();
    if (mechPreview_) mechPreview_->setDrawing(have ? p->mechanical : QJsonArray());
    syncing_ = false;
    rebuildParamForm();
}

void PartsLibraryDialog::rebuildParamForm() {
    refreshContactRow();   // a different part, different contacts - or none, if it is not a connector
    if (!paramLay_) return;
    paramWidgets_.clear();
    // Clear the form: the parameters belong to the KIND, so they are rebuilt whenever it changes.
    while (QLayoutItem* item = paramLay_->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    LibraryPart* p = current();
    if (!p) return;
    syncing_ = true;
    for (const PartField& f : partFields(p->kind)) {
        QWidget* field = nullptr;
        // A cable's make-up is a LIST - so many pairs, so many singles, a coax, the screens, the
        // colours - and a list does not go in a line edit. The row shows what it comes to and opens
        // the builder, which is where a structure is edited.
        if (p->kind == PartKind::Cable && f.key == QLatin1String("structure")) {
            auto* host = new QWidget(paramHost_);
            auto* hl = new QHBoxLayout(host);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->setSpacing(4);
            // The FIELD holds the shorthand - "3x24AWG + FTP 2x2x28AWG + coax, foil, braid, black" -
            // and is typed in like any other field; the button opens the window where the structure
            // itself is built, and writes the line back from it.
            const CableSpec spec = cableSpecOf(*p);
            auto* line = new QLineEdit(host);
            line->setText(p->value(QLatin1String(kCableStructureKey)));
            line->setPlaceholderText(
                QStringLiteral("3x24AWG + FTP 2x2x28AWG + coax, foil, braid, black"));
            // Empty, the tooltip is a worked example - which is what someone looking at a blank field
            // actually needs. Filled in, it says what the button beside it will do with it.
            auto structureTip = [](bool empty) {
                return empty
                           ? QStringLiteral(
                                 "Say what the cable is, then press \"Made cable structure\".\n\n"
                                 "For example:\n"
                                 "  3x24AWG + FTP 2x2x28AWG + coax, foil, braid, black\n\n"
                                 "  3x24AWG                three singles of 24 AWG\n"
                                 "  2x2x28AWG              two pairs of 28 AWG\n"
                                 "  FTP / STP / SFTP       screened per pair: foil, braid, or both\n"
                                 "  coax 0.22              a coax\n"
                                 "  4x0.5mm2               in mm² instead\n"
                                 "  foil 100%, braid 85%   the overall screens and their coverage\n"
                                 "  drain 24AWG            a drain wire\n"
                                 "  black                  the jacket colour\n\n"
                                 "Terms are separated by + or by a comma, in any order.")
                           : QStringLiteral(
                                 "What the cable is, in one line. \"Made cable structure\" builds the "
                                 "make-up from it; building it there writes this line back.");
            };
            line->setToolTip(structureTip(line->text().trimmed().isEmpty()));
            connect(line, &QLineEdit::textChanged, this, [line, structureTip](const QString& t) {
                line->setToolTip(structureTip(t.trimmed().isEmpty()));
            });
            connect(line, &QLineEdit::editingFinished, this, [this, line] {
                if (syncing_) return;
                if (LibraryPart* cp = current()) {
                    cp->params[QLatin1String(kCableStructureKey)] = line->text();
                    touched();
                }
            });
            hl->addWidget(line, 1);
            (void)spec;
            auto* edit = new QPushButton(QStringLiteral("Made cable structure"), host);
            edit->setToolTip(QStringLiteral(
                "What is inside the jacket: the pairs, the singles, the coaxes, their cross-sections "
                "and colours, and the screens. This is what the harness builder expands the cable "
                "into when it is placed."));
            connect(edit, &QPushButton::clicked, this, [this] { editCableStructure(); });
            hl->addWidget(edit);
            paramWidgets_.insert(f.key, host);
            field = host;
            paramLay_->addWidget(rowHost(paramHost_, f.label, field, f.unit, true));
            continue;
        }
        switch (f.type) {
            case PartField::Type::Text:
            case PartField::Type::Number: {
                auto* le = new QLineEdit(paramHost_);
                le->setText(p->value(f.key));
                const QString key = f.key;
                connect(le, &QLineEdit::textChanged, this, [this, key](const QString& t) {
                    if (syncing_) return;
                    if (LibraryPart* cp = current()) { cp->params[key] = t; touched(); }
                });
                paramWidgets_.insert(f.key, le);
                field = le;
                break;
            }
            case PartField::Type::Bool: {
                auto* cb = new QCheckBox(QStringLiteral("yes"), paramHost_);
                cb->setChecked(p->value(f.key) == QLatin1String("1"));
                const QString key = f.key;
                connect(cb, &QCheckBox::toggled, this, [this, key](bool on) {
                    if (syncing_) return;
                    if (LibraryPart* cp = current()) {
                        cp->params[key] = on ? QStringLiteral("1") : QStringLiteral("0");
                        touched();
                    }
                });
                paramWidgets_.insert(f.key, cb);
                field = cb;
                break;
            }
            case PartField::Type::Choice: {
                auto* box = new QComboBox(paramHost_);
                // Editable, and what is typed is KEPT: a material that is not on the built-in list is
                // added to it, so the next part offers it too. A list that cannot be added to is a
                // list that gets worked around.
                box->setEditable(true);
                box->addItems(f.choices);
                const QStringList mine = addedChoices(f.key);
                if (!mine.isEmpty()) {
                    box->insertSeparator(box->count());
                    box->addItems(mine);
                }
                box->setCurrentText(p->value(f.key));
                box->setToolTip(QStringLiteral(
                    "Pick one, or type a new one — a new %1 is added to the list for every part.")
                                    .arg(f.label.toLower()));
                const QString key = f.key;
                const QStringList builtIn = f.choices;
                connect(box, &QComboBox::currentTextChanged, this,
                        [this, key, builtIn](const QString& t) {
                            if (syncing_) return;
                            if (LibraryPart* cp = current()) { cp->params[key] = t; touched(); }
                        });
                // Remembered when the field is left, not on every keystroke, or every half-typed
                // word would end up on the list.
                connect(box->lineEdit(), &QLineEdit::editingFinished, this,
                        [this, box, key, builtIn] {
                            if (syncing_) return;
                            const QString t = box->currentText().trimmed();
                            rememberChoice(key, t, builtIn);
                            if (!t.isEmpty() && box->findText(t) < 0) box->addItem(t);
                        });
                paramWidgets_.insert(f.key, box);
                field = box;
                break;
            }
            case PartField::Type::Range: {
                // Two halves of one parameter: a crimp fits a RANGE of studs, a boot a range of
                // diameters. Stored as "<key>Min" / "<key>Max".
                auto* host = new QWidget(paramHost_);
                auto* hl = new QHBoxLayout(host);
                hl->setContentsMargins(0, 0, 0, 0);
                hl->setSpacing(4);
                for (int half = 0; half < 2; ++half) {
                    const QString key = f.key + (half == 0 ? QStringLiteral("Min")
                                                           : QStringLiteral("Max"));
                    auto* le = new QLineEdit(host);
                    le->setPlaceholderText(half == 0 ? QStringLiteral("min")
                                                     : QStringLiteral("max"));
                    le->setText(p->value(key));
                    connect(le, &QLineEdit::textChanged, this, [this, key](const QString& t) {
                        if (syncing_) return;
                        if (LibraryPart* cp = current()) { cp->params[key] = t; touched(); }
                    });
                    paramWidgets_.insert(key, le);
                    if (half == 1) {
                        auto* dash = new QLabel(QStringLiteral("to"), host);
                        dash->setStyleSheet(QStringLiteral("color:#7f8896;"));
                        hl->addWidget(dash);
                    }
                    hl->addWidget(le, 1);
                }
                field = host;
                break;
            }
        }
        if (field) paramLay_->addWidget(rowHost(paramHost_, f.label, field, f.unit));
    }
    paramLay_->addStretch(1);
    syncing_ = false;
}

// The description, written from the parameters that are filled in.
// ---- Datasheets ---------------------------------------------------------------------------------

QString PartsLibraryDialog::selectedDatasheet() const {
    if (datasheetList_) {
        if (QListWidgetItem* it = datasheetList_->currentItem())
            return it->data(Qt::UserRole).toString();
    }
    // Nothing picked: "the" datasheet is the first one, which is what a single-file part means.
    const LibraryPart* p = const_cast<PartsLibraryDialog*>(this)->current();
    return p ? p->datasheet() : QString();
}

void PartsLibraryDialog::refreshDatasheetRow() {
    LibraryPart* p = current();
    const bool have = p != nullptr;
    if (attachBtn_) attachBtn_->setEnabled(have);
    if (!datasheetList_) return;
    const QString keep = selectedDatasheet();
    datasheetList_->clear();
    if (have) {
        for (const QString& sheet : p->datasheets) {
            // How many parts share it, because that is what decides whether removing it deletes the
            // file, and whether it is even there at all.
            const int tied = static_cast<int>(DatasheetStore::tiedParts(catalog_, sheet).size());
            QString text = DatasheetStore::originalName(sheet);
            if (!DatasheetStore::exists(sheet))
                text += QStringLiteral("   (missing from the datasheet folder)");
            else if (tied > 1)
                text += QStringLiteral("   — shared with %1 other part(s)").arg(tied - 1);
            auto* it = new QListWidgetItem(text, datasheetList_);
            it->setData(Qt::UserRole, sheet);
            if (!DatasheetStore::exists(sheet)) it->setForeground(QColor("#e6b45a"));
            if (sheet == keep) datasheetList_->setCurrentItem(it);
        }
    }
    if (datasheetList_->count() > 0 && !datasheetList_->currentItem())
        datasheetList_->setCurrentRow(0);
    const bool attached = datasheetList_->count() > 0;
    const QString picked = selectedDatasheet();
    if (openDsBtn_) openDsBtn_->setEnabled(attached && DatasheetStore::exists(picked));
    if (removeDsBtn_) removeDsBtn_->setEnabled(attached);
    if (datasheetLabel_)
        datasheetLabel_->setText(attached ? QString()
                                          : (have ? QStringLiteral("none attached") : QString()));
}

void PartsLibraryDialog::attachDatasheet() {
    LibraryPart* p = current();
    if (!p) return;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Attach a datasheet"), QString(),
        QStringLiteral("Datasheets (*.pdf *.txt *.htm *.html *.csv);;All files (*)"));
    if (path.isEmpty()) return;
    QString error;
    const QString stored = DatasheetStore::attach(path, &error);
    if (stored.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Datasheet"), error);
        return;
    }
    if (!p->datasheets.contains(stored)) p->datasheets << stored;
    touched();
    refreshDatasheetRow();
    if (summary_)
        summary_->setText(QStringLiteral("Attached %1").arg(QFileInfo(path).fileName()));

    // ---- And then: shall the figures be read out of it? ----
    QString how;
    const QString text = datasheetText(DatasheetStore::pathOf(stored), &how);
    if (text.trimmed().isEmpty()) {
        if (summary_)
            summary_->setText(QStringLiteral("Attached %1 — the technical data could not be read "
                                            "out of it (%2).")
                                  .arg(QFileInfo(path).fileName(), how));
        return;
    }
    const std::vector<PulledField> found = pullTechnicalFields(text, p->kind, sizeTokenOf(*p));
    if (found.empty()) {
        if (summary_)
            summary_->setText(QStringLiteral("Attached %1 (%2) — nothing in it matched the fields a "
                                            "%3 has, so nothing was filled in.")
                                  .arg(QFileInfo(path).fileName(), how,
                                       partKindName(p->kind).toLower()));
        return;
    }

    // What was found, what it would overwrite, and the line each figure came from - so it can be
    // judged before it is taken. Nothing is filled in without being asked.
    QStringList lines;
    int wouldOverwrite = 0;
    for (const PulledField& f : found) {
        const QString had = p->value(f.key);
        if (!had.isEmpty() && had != f.value) ++wouldOverwrite;
        lines << QStringLiteral("%1: %2 %3%4\n      from “%5”")
                     .arg(f.label, f.value, f.unit,
                          had.isEmpty() ? QString()
                                        : QStringLiteral("   (replacing %1)").arg(had),
                          f.evidence.left(90));
    }
    const QString question =
        QStringLiteral("%1 was read (%2). Fill the technical fields in from it?\n\n%3\n\n%4")
            .arg(QFileInfo(path).fileName(), how, lines.join(QStringLiteral("\n")),
                 wouldOverwrite > 0
                     ? QStringLiteral("%1 field(s) already have a different value and would be "
                                      "replaced. Every figure above is read out of the document by "
                                      "pattern — check them against the datasheet.")
                           .arg(wouldOverwrite)
                     : QStringLiteral("Every figure above is read out of the document by pattern — "
                                      "check them against the datasheet."));
    if (QMessageBox::question(this, QStringLiteral("Pull the technical data in?"), question,
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) !=
        QMessageBox::Yes) {
        if (summary_)
            summary_->setText(QStringLiteral("Attached %1 — the fields were left as they were.")
                                  .arg(QFileInfo(path).fileName()));
        return;
    }
    for (const PulledField& f : found) p->params[f.key] = f.value;
    touched();
    rebuildParamForm();
    refreshDatasheetRow();
    if (summary_)
        summary_->setText(QStringLiteral("Attached %1 and filled in %2 field(s) from it.")
                              .arg(QFileInfo(path).fileName())
                              .arg(found.size()));
}

// Read the technical fields out of a file that is ALREADY attached.
//
// Nothing is read from a datasheet until it is asked for, so a part whose datasheet was attached before
// this application could read PDFs - or before its kind had any patterns - has a datasheet full of
// figures and empty fields. This is how those get filled in without detaching and attaching again.
void PartsLibraryDialog::pullFromDatasheet() {
    LibraryPart* p = current();
    if (!p) return;
    if (p->datasheets.isEmpty()) {
        if (summary_)
            summary_->setText(QStringLiteral("This part has no file attached to read from."));
        return;
    }
    // Whichever is picked first, then the rest: a part may keep its datasheet beside a drawing that
    // has no figures in it at all.
    QStringList order;
    const QString picked = selectedDatasheet();
    if (!picked.isEmpty()) order << picked;
    for (const QString& s : p->datasheets)
        if (!order.contains(s)) order << s;

    const QString sizeHint = sizeTokenOf(*p);
    QString readFrom, how, text;
    std::vector<PulledField> found;
    QStringList tried;
    for (const QString& sheet : order) {
        QString why;
        const QString body = datasheetText(DatasheetStore::pathOf(sheet), &why);
        tried << QStringLiteral("%1 — %2").arg(DatasheetStore::originalName(sheet), why);
        if (body.trimmed().isEmpty()) continue;
        const std::vector<PulledField> got = pullTechnicalFields(body, p->kind, sizeHint);
        if (got.empty()) {
            if (text.isEmpty()) { text = body; how = why; readFrom = sheet; }
            continue;
        }
        found = got;
        text = body;
        how = why;
        readFrom = sheet;
        break;
    }

    if (found.empty()) {
        // Say exactly why nothing came of it, and show what WAS read - the difference between "the
        // file could not be read" and "it was read and nothing in it matched" is the whole diagnosis,
        // and guessing which one it is wastes the person's afternoon.
        const QString sample = text.trimmed().left(600);
        QString message =
            text.trimmed().isEmpty()
                ? QStringLiteral("No text could be read out of the file(s):\n\n%1")
                      .arg(tried.join(QStringLiteral("\n")))
                : QStringLiteral(
                      "%1 was read (%2), but nothing in it matched the fields a %3 has%4.\n\nThis is "
                      "what was read from it:\n\n%5")
                      .arg(DatasheetStore::originalName(readFrom), how,
                           partKindName(p->kind).toLower(),
                           sizeHint.isEmpty()
                               ? QStringLiteral(" (and no size could be read out of this part's "
                                                "number, so a table of sizes could not be looked up)")
                               : QStringLiteral(" for size %1").arg(sizeHint),
                           sample);
        QMessageBox::information(this, QStringLiteral("Read the technical data"), message);
        if (summary_)
            summary_->setText(QStringLiteral("Nothing in the datasheet matched the fields a %1 has.")
                                  .arg(partKindName(p->kind).toLower()));
        return;
    }

    QStringList lines;
    int wouldOverwrite = 0;
    for (const PulledField& f : found) {
        const QString had = p->value(f.key);
        if (!had.isEmpty() && had != f.value) ++wouldOverwrite;
        lines << QStringLiteral("%1: %2 %3%4\n      from “%5”")
                     .arg(f.label, f.value, f.unit,
                          had.isEmpty() ? QString()
                                        : QStringLiteral("   (replacing %1)").arg(had),
                          f.evidence.left(90));
    }
    const QString question =
        QStringLiteral("%1 was read (%2)%3. Fill the technical fields in from it?\n\n%4\n\n%5")
            .arg(DatasheetStore::originalName(readFrom), how,
                 sizeHint.isEmpty() ? QString()
                                    : QStringLiteral(", at size %1").arg(sizeHint),
                 lines.join(QStringLiteral("\n")),
                 wouldOverwrite > 0
                     ? QStringLiteral("%1 field(s) already have a different value and would be "
                                      "replaced. Every figure above is read out of the document by "
                                      "pattern — check them against the datasheet.")
                           .arg(wouldOverwrite)
                     : QStringLiteral("Every figure above is read out of the document by pattern — "
                                      "check them against the datasheet."));
    if (QMessageBox::question(this, QStringLiteral("Read the technical data"), question,
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) !=
        QMessageBox::Yes)
        return;
    for (const PulledField& f : found) p->params[f.key] = f.value;
    touched();
    rebuildParamForm();
    if (summary_)
        summary_->setText(QStringLiteral("%1 field(s) filled in from %2.")
                              .arg(found.size())
                              .arg(DatasheetStore::originalName(readFrom)));
}

void PartsLibraryDialog::openDatasheet() {
    const QString sheet = selectedDatasheet();
    const QString path = DatasheetStore::pathOf(sheet);
    if (path.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

// Explorer, with the file itself selected - not just the folder opened, which leaves you hunting for
// it among a hundred others named after their own content.
void PartsLibraryDialog::showDatasheetInFolder() {
    const QString path = DatasheetStore::pathOf(selectedDatasheet());
    if (path.isEmpty() || !QFile::exists(path)) {
        if (summary_) summary_->setText(QStringLiteral("That file is not in the datasheet folder."));
        return;
    }
#ifdef Q_OS_WIN
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("explorer"));
    if (!tool.isEmpty()) {
        // /select, wants backslashes and the path as one argument.
        QProcess::startDetached(tool, {QStringLiteral("/select,") + QDir::toNativeSeparators(path)});
        return;
    }
#endif
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

void PartsLibraryDialog::removeDatasheet() {
    LibraryPart* p = current();
    if (!p || p->datasheets.isEmpty()) return;
    const QString stored = selectedDatasheet();
    if (stored.isEmpty()) return;
    const QString shown = DatasheetStore::originalName(stored);
    // Who else points at it, BEFORE anything is taken off - that is what the question has to say.
    QStringList others;
    for (const LibraryPart* tied : DatasheetStore::tiedParts(catalog_, stored))
        if (tied->id != p->id) others << tied->title();

    const QString question =
        others.isEmpty()
            ? QStringLiteral("Take %1 off this part?\n\nNo other part points at it, so the file "
                             "will be deleted from the datasheet folder.")
                  .arg(shown)
            : QStringLiteral("Take %1 off this part?\n\nThe file is kept: %2 other part(s) point at "
                             "it —\n\n%3")
                  .arg(shown)
                  .arg(others.size())
                  .arg(others.join(QStringLiteral("\n")));
    if (QMessageBox::question(this, QStringLiteral("Remove the datasheet"), question,
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
        QMessageBox::Yes)
        return;

    const DatasheetStore::DetachResult r = DatasheetStore::detach(catalog_, p->id, stored);
    touched();
    refreshDatasheetRow();
    if (summary_)
        summary_->setText(
            r.fileDeleted
                ? QStringLiteral("%1 removed, and the file deleted — nothing else pointed at it.")
                      .arg(shown)
                : QStringLiteral("%1 taken off this part. The file is kept for %2 other part(s).")
                      .arg(shown)
                      .arg(r.stillTiedTo.size()));
}

// The cable builder, on this part's make-up.
//
// What comes back is the make-up itself plus the things it DECIDES: how many conductors the cable has
// and whether it is screened are not separate facts to be typed in and kept in step - they follow from
// what is inside the jacket, so they are written from it.
void PartsLibraryDialog::editCableStructure() {
    LibraryPart* p = current();
    if (!p || p->kind != PartKind::Cable) return;
    CableBuilderDialog dlg(this);
    CableSpec spec = cableSpecOf(*p);
    // The parameters the part already carries are the same facts: start the builder from them rather
    // than from nothing, so a cable half described in the form is not described twice.
    if (spec.odMm <= 0.0) spec.odMm = p->value(QStringLiteral("od")).toDouble();
    if (spec.jacket.isEmpty()) spec.jacket = p->value(QStringLiteral("jacket"));
    if (spec.conductor.isEmpty()) spec.conductor = p->value(QStringLiteral("material"));
    dlg.setSpec(spec);
    dlg.setCableName(p->title());
    // Whatever the field says goes into the window's line, so a cable described there and never built
    // can be turned into a make-up with one press.
    const QString typed = p->value(QLatin1String(kCableStructureKey));
    if (spec.isEmpty() && !typed.trimmed().isEmpty()) {
        dlg.setStructureText(typed);
        dlg.generateFromStructureText();
    }
    if (dlg.exec() != QDialog::Accepted) return;

    const CableSpec made = dlg.spec();
    setCableSpecOn(*p, made);
    // And the plain parameters that follow from it, so the form, the bill of materials and the
    // bundle-diameter calculation all see the same cable.
    if (made.odMm > 0.0)
        p->params[QStringLiteral("od")] = QString::number(made.odMm, 'f', 2);
    if (!made.jacket.isEmpty()) p->params[QStringLiteral("jacket")] = made.jacket;
    if (!made.jacketColour.isEmpty())
        p->params[QStringLiteral("jacketColour")] = made.jacketColour;
    if (!made.conductor.isEmpty()) p->params[QStringLiteral("material")] = made.conductor;
    p->params[QStringLiteral("cores")] = QString::number(made.conductorCount());
    p->params[QStringLiteral("screened")] = made.screened() ? QStringLiteral("1")
                                                            : QStringLiteral("0");
    // The cross-section IS the mechanical drawing of a cable: it says what the make-up is in the one
    // way a drawing can. Drawn now, so the part shows it and can drop it on a sheet.
    if (!made.isEmpty()) {
        // Two drawings, because a cable is two different things to two different drawings: a
        // cross-section is what a mechanical drawing wants (what it looks like, how big it is), and a
        // schematic is what a wiring diagram wants (which conductor is which, what is twisted, what is
        // screened). Neither says what the other says.
        p->mechanical = partDrawingFromEntities(cableCrossSection(made, made.odMm));
        p->schematic = partDrawingFromEntities(cableSchematic(made));
        if (mechPreview_) mechPreview_->setDrawing(p->mechanical);
        if (schemPreview_) schemPreview_->setDrawing(p->schematic);
    }
    touched();
    rebuildParamForm();
    if (summary_)
        summary_->setText(QStringLiteral("%1 — %2 conductor(s)%3")
                              .arg(made.summary())
                              .arg(made.conductorCount())
                              .arg(made.screened() ? QStringLiteral(", screened") : QString()));
}

void PartsLibraryDialog::describeFromData() {
    LibraryPart* p = current();
    if (!p) return;
    const QString made = describePart(*p);
    p->description = made;
    syncing_ = true;
    if (descEdit_) descEdit_->setText(made);
    syncing_ = false;
    touched();
    if (summary_) summary_->setText(QStringLiteral("Description written from the data: %1").arg(made));
}

void PartsLibraryDialog::touched() {
    // The list shows the description and P/N, so it follows an edit; the file is written on Save
    // and on Close, not on every keystroke.
    if (syncing_) return;
    if (!tree_) return;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* head = tree_->topLevelItem(i);
        for (int j = 0; j < head->childCount(); ++j)
            if (head->child(j)->data(0, Qt::UserRole).toString() == currentId_) {
                if (const LibraryPart* p = catalog_.byId(currentId_)) {
                    head->child(j)->setText(0, p->title());
                    head->child(j)->setText(1, p->mfrName);
                    // And the signs: a drawing captured or cleared has to show in the list at once,
                    // or the column says "nothing yet" about a part that now has one.
                    setPartSigns(head->child(j), *p);
                }
                return;
            }
    }
}

LibraryPart* PartsLibraryDialog::addPart(PartKind kind) {
    // Added to the library being worked on.
    LibraryPart& p = catalog_.active().add(kind);
    const QString id = p.id;
    reloadTree();
    selectPart(id);
    if (summary_)
        summary_->setText(QStringLiteral("Added a %1 — fill in its description and P/N")
                              .arg(partKindName(kind).toLower()));
    return catalog_.byId(id) ? current() : nullptr;
}

void PartsLibraryDialog::deleteCurrentPart() {
    const LibraryPart* p = catalog_.byId(currentId_);
    if (!p) return;
    const QString title = p->title();
    if (PartsLibrary* own = catalog_.byPath(catalog_.pathOf(currentId_))) own->remove(currentId_);
    currentId_.clear();
    reloadTree();
    {
        const PartsLibrary merged = catalog_.merged();
        showPart(merged.parts.empty() ? QString() : merged.parts.front().id);
    }
    if (summary_) summary_->setText(QStringLiteral("Deleted %1").arg(title));
}

void PartsLibraryDialog::duplicateCurrentPart() {
    PartsLibrary* own = catalog_.byPath(catalog_.pathOf(currentId_));
    if (!own) own = &catalog_.active();
    if (LibraryPart* copy = own->duplicate(currentId_)) {
        const QString id = copy->id;
        reloadTree();
        selectPart(id);
        if (summary_) summary_->setText(QStringLiteral("Duplicated"));
    }
}

// Capture, when there is nothing selected to capture yet.
//
// Asking someone to select the objects first and only then open the library is the wrong way round:
// the library window sits over the sheet, and half the time what wants capturing is behind it. So the
// window steps aside, the ribbon says what to do, and Enter (or the ribbon button) finishes.
void PartsLibraryDialog::beginCaptureFromSheet(bool mechanical) {
    LibraryPart* p = current();
    if (!p) return;
    // Something already selected: nothing to wait for.
    if (editor_ && !editor_->copyOfSelection().empty()) {
        captureFromSelection(mechanical);
        return;
    }
    capturingPart_ = p->id;
    capturingMechanical_ = mechanical;
    const QString what = p->title();
    hide();
    emit captureArmed(mechanical, what);
}

void PartsLibraryDialog::finishCaptureFromSheet() {
    if (capturingPart_.isEmpty()) return;
    const QString id = capturingPart_;
    const bool mech = capturingMechanical_;
    capturingPart_.clear();
    show();
    raise();
    selectPart(id);
    captureFromSelection(mech);   // says what it took, or that there was nothing selected
    emit captureFinished();
}

void PartsLibraryDialog::cancelCaptureFromSheet() {
    if (capturingPart_.isEmpty()) return;
    const QString id = capturingPart_;
    capturingPart_.clear();
    show();
    raise();
    selectPart(id);
    if (summary_) summary_->setText(QStringLiteral("Capture cancelled — the drawing is unchanged."));
    emit captureFinished();
}

void PartsLibraryDialog::captureFromSelection(bool mechanical) {
    LibraryPart* p = current();
    if (!p) return;
    if (!editor_) return;
    const std::vector<Entity> sel = editor_->copyOfSelection();
    if (sel.empty()) {
        if (summary_)
            summary_->setText(QStringLiteral(
                "Select the objects on the sheet first — then capture them as the drawing"));
        return;
    }
    const QJsonArray drawing = partDrawingFromEntities(sel);
    if (mechanical) {
        p->mechanical = drawing;
        if (mechPreview_) mechPreview_->setDrawing(drawing);
    } else {
        p->schematic = drawing;
        if (schemPreview_) schemPreview_->setDrawing(drawing);
    }
    // The list has to keep up: its S / M signs are what say which parts are drawn and which are not.
    touched();
    if (summary_)
        summary_->setText(QStringLiteral("Captured %1 object(s) as the %2 drawing")
                              .arg(sel.size())
                              .arg(mechanical ? QStringLiteral("mechanical")
                                              : QStringLiteral("schematic")));
}

void PartsLibraryDialog::insertIntoDrawing(bool mechanical) {
    LibraryPart* p = current();
    // The contact the part is used with, whichever drawing is being placed: it is a property of the
    // connector, so there is nothing to ask at the moment of placing.
    const QString contactId = p ? p->contactId() : QString();
    if (!p || !editor_) return;
    const QJsonArray drawing = mechanical ? p->mechanical : partSchematicDrawing(*p);
    if (drawing.isEmpty()) {
        if (summary_)
            summary_->setText(QStringLiteral("Nothing captured for that side yet"));
        return;
    }
    // As a BLOCK, not as loose lines. A part is a block by definition - drawn once, dropped in wherever
    // it is wanted - and put down as one it stays one thing: picked in a click, moved in a piece, and
    // rebuilt everywhere on the sheet when the part's drawing is changed. Loose lines were none of that
    // the moment they landed.
    std::vector<Entity> block = partBlockInstance(*p, mechanical, Vec2{0.0, 0.0});
    // The contact fitted, stamped on every entity of the block: whichever of them the bill of materials
    // reads, it finds the same answer, and a block copied on the sheet carries its contact with it.
    if (!contactId.isEmpty())
        for (Entity& e : block) e.meta.insert(QLatin1String(kContactPartKey), contactId);
    editor_->placeEntities(std::move(block));
    const LibraryPart* contact = contactId.isEmpty() ? nullptr : catalog_.byId(contactId);
    if (summary_)
        summary_->setText(contact ? QStringLiteral("Click the sheet to place %1, with %2 — %3 of them on "
                                                   "the bill of materials")
                                        .arg(p->title(), contact->title())
                                        .arg(p->wayCount())
                                  : QStringLiteral("Click the sheet to place %1").arg(p->title()));
    // The click that places it happens on the canvas, so the library gets out of the way.
    hide();
    saveLibrary();
}

void PartsLibraryDialog::openPartLink() {
    const LibraryPart* p = current();
    if (!p) return;
    QString link = p->link.trimmed();
    if (link.isEmpty()) {
        if (summary_) summary_->setText(QStringLiteral("No web link on this part yet"));
        return;
    }
    // A link written without its scheme - copied out of a spreadsheet, usually - is still a link.
    if (!link.contains(QStringLiteral("://"))) link.prepend(QStringLiteral("https://"));
    if (!QDesktopServices::openUrl(QUrl(link)) && summary_)
        summary_->setText(QStringLiteral("That link could not be opened: %1").arg(link));
}

void PartsLibraryDialog::fetchDataFromLink() {
    LibraryPart* p = current();
    if (!p) return;
    QString link = p->link.trimmed();
    if (link.isEmpty()) {
        QMessageBox::information(
            this, QStringLiteral("Read data"),
            QStringLiteral("Put the part's page in the Web link field first — the manufacturer's page for "
                           "this part, or the distributor's listing of it."));
        return;
    }
    if (!link.contains(QStringLiteral("://"))) link.prepend(QStringLiteral("https://"));
    PartWebDataDialog dlg(*p, link, this);
    if (dlg.exec() != QDialog::Accepted) return;
    // Only what was ticked, and it is written into the part that is showing - which may have been changed
    // while the page was being read, so it is fetched again rather than remembered.
    LibraryPart* still = current();
    if (!still) return;
    const int changed = applyWebFinds(*still, dlg.chosen());
    const QString sheet = dlg.datasheetLink();
    if (!sheet.isEmpty()) still->params.insert(QStringLiteral("datasheetLink"), sheet);
    if (changed > 0 || !sheet.isEmpty()) {
        touched();
        rebuildParamForm();   // the fields it filled in are on the form
        reloadTree();
        if (summary_)
            summary_->setText(QStringLiteral("%1 field%2 filled in from the page%3")
                                  .arg(changed)
                                  .arg(changed == 1 ? QString() : QStringLiteral("s"))
                                  .arg(sheet.isEmpty() ? QString()
                                                       : QStringLiteral(", and the datasheet link kept")));
    } else if (summary_) {
        summary_->setText(QStringLiteral("Nothing was taken from the page"));
    }
}

void PartsLibraryDialog::generateConnectorSymbol() {
    LibraryPart* p = current();
    if (!p) return;
    if (p->kind != PartKind::Connector) {
        if (summary_)
            summary_->setText(QStringLiteral("Only a connector's symbol can be built from data"));
        return;
    }
    const QJsonArray made = connectorSymbolFromData(*p);
    if (made.isEmpty()) {
        QMessageBox::information(
            this, QStringLiteral("From data"),
            QStringLiteral("Fill in the number of pins first — that is what the symbol is built from."));
        return;
    }
    if (p->hasSchematic() &&
        QMessageBox::question(this, QStringLiteral("From data"),
                              QStringLiteral("Replace the schematic symbol this part already has?")) !=
            QMessageBox::Yes)
        return;
    p->schematic = made;
    if (schemPreview_) schemPreview_->setDrawing(p->schematic);
    touched();
    reloadTree();
    if (summary_)
        summary_->setText(QStringLiteral("Schematic symbol built: %1 way%2, numbered")
                              .arg(p->wayCount())
                              .arg(p->wayCount() == 1 ? QString() : QStringLiteral("s")));
}

void PartsLibraryDialog::beginCaptureFanout() {
    LibraryPart* p = current();
    if (!p || !editor_) return;
    const std::vector<Entity> sel = editor_->copyOfSelection();
    if (sel.empty()) {
        if (summary_)
            summary_->setText(QStringLiteral(
                "Select the fanout on the sheet first — the wires as they leave the connector"));
        return;
    }
    p->fanout = partDrawingFromEntities(sel);
    if (fanoutPreview_) fanoutPreview_->setDrawing(p->fanout);
    touched();
    reloadTree();
    if (summary_)
        summary_->setText(QStringLiteral("Captured %1 object(s) as the fanout, drawn from the %2")
                              .arg(sel.size())
                              .arg(p->fanoutSide()));
}

void PartsLibraryDialog::insertFanout(bool mirrored) {
    LibraryPart* p = current();
    if (!p || !editor_) return;
    if (!p->hasFanout()) {
        if (summary_)
            summary_->setText(QStringLiteral("No fanout captured for this part yet — draw it on the sheet "
                                             "and press Capture selection under Fanout"));
        return;
    }
    std::vector<Entity> block = partDrawingBlockInstance(*p, p->fanout, QStringLiteral("fanout"),
                                                         Vec2{0.0, 0.0}, mirrored);
    const QString contactId = p->contactId();
    if (!contactId.isEmpty())
        for (Entity& e : block) e.meta.insert(QLatin1String(kContactPartKey), contactId);
    editor_->placeEntities(std::move(block));
    // Mirroring turns the side it shows into the other one, and saying so is the whole point of the button.
    const QString shows = p->fanoutSide() == QStringLiteral("front")
                              ? (mirrored ? QStringLiteral("back") : QStringLiteral("front"))
                              : (mirrored ? QStringLiteral("front") : QStringLiteral("back"));
    if (summary_)
        summary_->setText(QStringLiteral("Click the sheet to place the fanout of %1 — the %2 of the "
                                         "connector")
                              .arg(p->title(), shows));
    hide();
    saveLibrary();
}

void PartsLibraryDialog::refreshContactChoice() {
    if (!contactGroup_ || !contactBox_) return;
    const LibraryPart* p = current();
    // The SAME tie, from whichever end is being looked at. On a connector the box chooses the contact that
    // goes in it; on a contact it chooses the connector it goes in. Tied from either end it shows on both,
    // which is the point: a contact opened on its own says what it is for.
    const bool isConnector = p && p->kind == PartKind::Connector;
    const bool isContact = p && (p->kind == PartKind::ContactPin || p->kind == PartKind::Crimp);
    if (contactGroupBox_)
        contactGroupBox_->setTitle(isContact ? QStringLiteral("Use with connector")
                                             : QStringLiteral("Use with pin contact"));
    contactGroup_->setVisible(isConnector || isContact);
    const QSignalBlocker block(contactBox_);
    contactBox_->clear();
    if (!isConnector && !isContact) return;
    contactBox_->addItem(isContact ? QStringLiteral("(none — not tied to a connector)")
                                   : QStringLiteral("(none — the connector on its own)"),
                         QString());
    const PartsLibrary merged = catalog_.merged();
    for (const LibraryPart& part : merged.parts) {
        const bool offer = isContact ? part.kind == PartKind::Connector
                                     : (part.kind == PartKind::ContactPin || part.kind == PartKind::Crimp);
        if (offer) contactBox_->addItem(part.title(), part.id);
    }
    // What it is tied to, worked out from BOTH ends: a tie written on the other part only still shows here.
    const QStringList tied = isContact ? tiedConnectorsOf(merged, p->id) : tiedContactsOf(merged, p->id);
    const QString chosen = tied.isEmpty() ? QString() : tied.front();
    const int at = contactBox_->findData(chosen);
    // Something no longer in any library that is open is still what the part says: shown as itself rather
    // than quietly cleared, because clearing it would lose what somebody chose.
    if (at >= 0) {
        contactBox_->setCurrentIndex(at);
    } else if (!chosen.isEmpty()) {
        contactBox_->addItem(QStringLiteral("%1 — not in any library that is open").arg(chosen), chosen);
        contactBox_->setCurrentIndex(contactBox_->count() - 1);
    } else {
        contactBox_->setCurrentIndex(0);
    }
}

void PartsLibraryDialog::refreshContactRow() {
    if (genSymbolBtn_) {
        const LibraryPart* part = current();
        genSymbolBtn_->setEnabled(part && part->kind == PartKind::Connector);
    }
    refreshContactChoice();
}

void PartsLibraryDialog::editDrawing(bool mechanical) {
    LibraryPart* p = current();
    if (!p || !editor_) return;
    const QJsonArray drawing = mechanical ? p->mechanical : partSchematicDrawing(*p);
    if (drawing.isEmpty()) {
        if (summary_)
            summary_->setText(QStringLiteral("Nothing captured for that side yet — draw it on the "
                                             "sheet and press Capture selection"));
        return;
    }

    // Its own canvas, not the sheet: this is not "insert a copy to use", it is "look at the part and
    // say where the harness attaches to it". One line, picked by clicking it, marked orange - and
    // clicking another moves the mark rather than adding a second, because a part has one place the
    // harness meets it.
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("%1 — connection source").arg(p->title()));
    dlg.setModal(true);
    auto* v = new QVBoxLayout(&dlg);
    auto* what = new QLabel(QStringLiteral(
        "Click the line the harness attaches to — the face of a connector, the barrel of a terminal. "
        "It is marked orange, and a part placed by the builder is set down by it: that line across the "
        "wires, its centre on the joint, the body facing away from the harness."), &dlg);
    what->setWordWrap(true);
    v->addWidget(what);

    auto* canvas = new DrawingPreview(
        mechanical ? QStringLiteral("Mechanical drawing — connection source")
                   : QStringLiteral("Schematic symbol — connection source"), &dlg);
    canvas->setMinimumSize(560, 380);
    canvas->setDrawing(drawing);
    canvas->setPickable(true);
    canvas->setPickedIndex(partDrawingConnectionLine(drawing));
    v->addWidget(canvas, 1);

    auto* said = new QLabel(&dlg);
    said->setStyleSheet(QStringLiteral("color:#8fb0d6;"));
    v->addWidget(said);
    auto sayPicked = [said, canvas] {
        said->setText(canvas->pickedIndex() >= 0
                          ? QStringLiteral("Object %1 is the connection source")
                                .arg(canvas->pickedIndex() + 1)
                          : QStringLiteral("Nothing marked — the part will be centred on the joint"));
    };
    connect(canvas, &DrawingPreview::pickedChanged, &dlg, sayPicked);
    sayPicked();

    auto* row = new QHBoxLayout;
    auto* none = new QPushButton(QStringLiteral("Nothing marked"), &dlg);
    connect(none, &QPushButton::clicked, canvas, [canvas] { canvas->setPickedIndex(-1); });
    row->addWidget(none);
    row->addStretch(1);
    auto* ok = new QPushButton(QStringLiteral("Keep"), &dlg);
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    row->addWidget(ok);
    auto* cancel = new QPushButton(QStringLiteral("Cancel"), &dlg);
    connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    row->addWidget(cancel);
    v->addLayout(row);

    if (dlg.exec() != QDialog::Accepted) {
        if (summary_) summary_->setText(QStringLiteral("Left as it was"));
        return;
    }
    const QJsonArray marked = partDrawingWithConnectionLine(drawing, canvas->pickedIndex());
    if (mechanical) {
        p->mechanical = marked;
        if (mechPreview_) mechPreview_->setDrawing(marked);
    } else {
        p->schematic = marked;
        if (schemPreview_) schemPreview_->setDrawing(marked);
    }
    touched();
    saveLibrary();
    if (summary_)
        summary_->setText(canvas->pickedIndex() >= 0
                              ? QStringLiteral("The connection source is marked — shown orange here, "
                                               "and on the sheet whenever the part is selected")
                              : QStringLiteral("No connection source: the part will be centred on the "
                                               "joint"));
}

// The eye's answer, given to all three drawings. In one place because the three panes are shown one part
// three ways and there is no sense in which one of them could be showing its marks and another not.
void PartsLibraryDialog::applyShowConnection() {
    const bool on = !showConnBtn_ || showConnBtn_->isChecked();
    for (DrawingPreview* prev : {schemPreview_, mechPreview_, fanoutPreview_})
        if (prev) prev->setShowConnection(on);
}

void PartsLibraryDialog::clearDrawing(bool mechanical) {
    LibraryPart* p = current();
    if (!p) return;
    if (mechanical) {
        p->mechanical = QJsonArray();
        if (mechPreview_) mechPreview_->setDrawing(QJsonArray());
    } else {
        p->schematic = QJsonArray();
        // Clearing a captured symbol on a standard component uncovers the standard one again rather
        // than leaving nothing - that is what will be shown and inserted from now on.
        if (schemPreview_) schemPreview_->setDrawing(partSchematicDrawing(*p));
    }
    touched();   // and the list's sign goes back to "nothing yet"
    if (summary_) summary_->setText(QStringLiteral("Drawing cleared"));
}

bool PartsLibraryDialog::saveLibrary() {
    // Every connected library, each to its own file.
    return catalog_.saveAll();
}

} // namespace Edraw_Desktop
