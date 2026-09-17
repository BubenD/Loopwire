#include "app/PartsLibraryDialog.h"
#include <QLayout>
#include <QWidgetAction>
#include <QAction>
#include <QToolButton>
#include "harness/PartDataFromWeb.h"
#include <QTreeWidgetItem>
#include "harness/MechModels.h"
#include "app/PartWebDataDialog.h"
#include "harness/MechanicalHarness.h"

#include "app/DrawingPreview.h"
#include "app/UiBits.h"
#include "app/Chrome.h"
#include <QStyle>
#include "harness/CableSpec.h"
#include "app/CableBuilderDialog.h"
#include "harness/Datasheet.h"
#include "io/AppPaths.h"
#include "app/Editor.h"
#include "model/Block.h"
#include "model/Document.h"
#include "tools/EntityTransforms.h"

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
#include <QObject>
#include <QProcess>
#include <QCloseEvent>
#include <QSettings>
#include <QStandardPaths>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QGuiApplication>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace Edraw_Desktop {

namespace {

// The lettering roles, coloured for the scheme in force.
//
// Marked as a PROPERTY on the label and coloured from one stylesheet, rather than a stylesheet per
// label: the window is kept alive between openings, so a scheme change has to reach labels that
// already exist - and a list of pointers to them would miss every one built later.
QString roleStyle() {
    return QStringLiteral("QLabel[role=\"caption\"]{color:%1;}"
                          "QLabel[role=\"faint\"]{color:%2;}"
                          "QLabel[role=\"info\"]{color:%3;}"
                          "QLabel[role=\"warn\"]{color:%4;}")
        .arg(capInk().name(), faintInk().name(), infoInk().name(), warnInk().name());
}

// The tree's own chrome. The hover was a near-black navy, which is a dark blob with dark text on it the
// moment the window is light, so it follows the scheme too. The selected row stays the library green in
// both: it is white-on-green either way, and it is the one row that has to be found at a glance.
QString treeStyle() {
    return QStringLiteral("QTreeWidget::item:selected{background:#2f6b46;color:#ffffff;}"
                          "QTreeWidget::item:selected:!active{background:#2f6b46;color:#ffffff;}"
                          "QTreeWidget::item:hover{background:%1;}"
                          "QTreeWidget::item{padding:2px 0px;}")
        .arg(Chrome::dark() ? QStringLiteral("#33414f") : QStringLiteral("#dce6f2"));
}

// The same ink, thinned - for the wash inside a sign, which has to stay the colour of the outline
// round it in either scheme.
QColor withAlpha(QColor c, int a) {
    c.setAlpha(a);
    return c;
}

// A role set after the stylesheet is up needs the widget re-polished, or Qt keeps the colour it first
// resolved - which is how one label that switches between "all accounted for" and "say which" would
// have kept whichever of the two it was born with.
void setRole(QWidget* w, const char* role) {
    w->setProperty("role", role);
    w->style()->unpolish(w);
    w->style()->polish(w);
}


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
    setRole(cap, "caption");
    lay->addWidget(cap);
    // EVERY FIELD THE SAME WIDTH, and the space left over after it. A maximum with a stretch shared with
    // the gap behind it is not a width at all: each field ended up somewhere between its own idea of how
    // wide it wanted to be and that maximum, so a combo box sat at one width, the box under it at another,
    // and the column of fields came out ragged down the form. A form is read down its columns - a figure
    // against a field - and a ragged column makes the eye do the aligning.
    //
    // A sentence-length field still takes what there is: there is nothing to line it up with.
    constexpr int kFieldWidth = 260;
    // A field that is a box is set to the width; a field that is a box WITH SOMETHING BESIDE IT (a clear
    // cross, the two halves of a range) sets its own inner box to that width and is left alone here, so the
    // boxes line up and the extra hangs off the end rather than eating into them.
    const bool carriesMore = field->layout() && field->layout()->count() > 1;
    if (!wide && !carriesMore) {
        field->setMinimumWidth(kFieldWidth);
        field->setMaximumWidth(kFieldWidth);
    }
    lay->addWidget(field, wide ? 1 : 0);
    auto* u = new QLabel(unit, host);
    u->setFixedWidth(42);
    setRole(u, "faint");
    lay->addWidget(u);
    if (!wide) lay->addStretch(1);
    return host;
}

} // namespace

PartsLibraryDialog::PartsLibraryDialog(Editor* editor, QWidget* parent,
                                       std::function<void(QString, LibraryPartDrawing)> onEdit)
    : QDialog(parent), editor_(editor), onEdit_(std::move(onEdit)) {
    setWindowTitle(QStringLiteral("Harness parts library"));
    // WIDE ENOUGH THAT EVERY BUTTON CAN BE READ. Three drawings side by side, each with its own row of
    // buttons under it, and a window opened at a width somebody picked once means captions cut in half -
    // "r from opposite fa" was a real button. So the width is asked of the window itself once it is built
    // (see the end of buildUi), and it never exceeds the screen it is opening on.
    resize(1180, 760);
    // Everything connected last time (and, on a first run, the default library seeded with real
    // parts). The one being worked on is the first of them until the chooser says otherwise.
    catalog_.openRemembered();
    path_ = catalog_.activePath();
    buildUi();
    // The roles the labels were tagged with, coloured for the scheme the window is opening in.
    setStyleSheet(roleStyle());
    // The size the window actually needs, now that everything in it exists: what the layouts ask for,
    // never more than the screen can show. Asked for here rather than guessed at in resize() above,
    // because the thing that decides it is the widest row of buttons under a drawing - and that changes
    // whenever a button is added.
    {
        const QSize want = sizeHint();
        QSize room(1600, 900);
        if (QScreen* screen = QGuiApplication::primaryScreen())
            room = screen->availableGeometry().size() - QSize(80, 120);

        // NOTHING MAY BE SQUEEZED SMALLER THAN ITS CAPTIONS. The layout knows what the widest row of
        // buttons under a drawing needs; saying so as a MINIMUM means neither a remembered size from a
        // bigger screen nor a drag of the corner can cut "Capture selection" in half. Clamped to the
        // screen, because a minimum larger than the display is a window that cannot be used at all.
        const QSize least = minimumSizeHint().expandedTo(layout() ? layout()->minimumSize() : QSize());
        setMinimumSize(QSize(std::min(least.width(), room.width()),
                             std::min(least.height(), room.height())));

        // THE SIZE IT WAS LEFT AT, when there is one. Somebody who has widened this window once so that
        // every caption reads has already answered the question; asking again on the next opening is
        // making them answer it repeatedly. Still clamped to the screen in front of them, which may not
        // be the screen it was last sized on.
        const QSize remembered =
            QSettings().value(QStringLiteral("ui/partsLibrarySize")).toSize();
        const QSize open = remembered.isValid() && remembered.width() > 0 && remembered.height() > 0
                               ? remembered
                               : QSize(want.width(), std::max(want.height(), 760));
        resize(std::min(open.width(), room.width()), std::min(open.height(), room.height()));
    }
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

void PartsLibraryDialog::closeEvent(QCloseEvent* e) {
    // The size, not the position. Where a window sat is tied to the screen layout of the moment - a
    // second monitor that is not there next time puts it somewhere nobody can reach - while how big it
    // needs to be to show its captions is a lasting answer about this window's contents.
    QSettings().setValue(QStringLiteral("ui/partsLibrarySize"), size());
    QDialog::closeEvent(e);
}

void PartsLibraryDialog::restyle() {
    setStyleSheet(roleStyle());
    if (tree_) tree_->setStyleSheet(treeStyle());
    // THE SIGNS HAVE TO BE PAINTED AGAIN. They are little pixmaps with the colour baked into them, so
    // unlike lettering they cannot follow a stylesheet - the rows have to be laid again for the green
    // and the amber to come out in the new scheme. Cheap, and only ever on a change of scheme.
    reloadTree();
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
        p.setPen(QPen(idleInk(), 1.6));
        p.drawLine(4, 8, 12, 8);
        return QIcon(pm);
    }
    const QColor ink(own ? ownInk() : infoInk());
    p.setPen(QPen(ink, 1.6));
    if (mechanical) {
        const QRectF body(2.5, 4.5, 8.0, 7.0);
        if (own) p.setBrush(withAlpha(ownInk(), 90));
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
        p.setPen(QPen(idleInk(), 1.6));
        p.drawLine(4, 8, 12, 8);
        return QIcon(pm);
    }
    const bool missing = onFile < attached;
    const QColor ink(missing ? missInk() : ownInk());
    QPainterPath sheet;
    sheet.moveTo(3.5, 1.5);
    sheet.lineTo(9.5, 1.5);
    sheet.lineTo(12.5, 4.5);
    sheet.lineTo(12.5, 14.5);
    sheet.lineTo(3.5, 14.5);
    sheet.closeSubpath();
    p.setPen(QPen(ink, 1.4));
    p.setBrush(missing ? QBrush(Qt::NoBrush) : QBrush(withAlpha(ownInk(), 70)));
    p.drawPath(sheet);
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(9.5, 1.5), QPointF(9.5, 4.5));   // the folded corner
    p.drawLine(QPointF(9.5, 4.5), QPointF(12.5, 4.5));
    if (missing) {
        p.setPen(QPen(warnInk(), 1.6));
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
        it->setForeground(2, isConnected ? (isActive ? ownInk() : infoInk()) : idleInk());
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
        setRole(cap, "caption");
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
        setRole(libraryTotals_, "caption");
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
    tree_->setStyleSheet(treeStyle());
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
        setRole(cap, "caption");
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
        setRole(datasheetLabel_, "caption");
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
    // Named, so a test can tell a field ON THE FORM from the filter box or a pin row.
    paramHost_->setObjectName(QStringLiteral("paramHost"));
    paramLay_ = new QVBoxLayout(paramHost_);
    paramLay_->setContentsMargins(0, 0, 0, 0);
    paramLay_->setSpacing(4);
    scroll->setWidget(paramHost_);
    pbLay->addWidget(scroll);
    rv->addWidget(paramBox, 1);   // and the drawings below get twice this, growing with the window

    // The drawings of a part: what it is on a circuit, what it is in the metal, and what the harness does at
    // each side of it.
    //
    // FOUR PANES OF EQUAL SIZE, AND DRAGGABLE. They were four columns of a grid, and a grid column is as wide
    // as the widest thing in it - which was not the drawing but the row of BUTTONS under it. The fanout has
    // five ("Insert mirrored", "Mirror from opposite fanout") and the schematic has four, so the fanout's
    // drawing was given half again the width of the schematic's for a reason that has nothing to do with
    // either drawing. Four panes of a splitter start equal and stay equal, and any one of them can be pulled
    // wider to look at a drawing closely - which is the whole reason for having the drawing on screen.
    auto* draw = new QGroupBox(QStringLiteral("Drawings"), right);
    auto* dgOuter = new QVBoxLayout(draw);
    dgOuter->setContentsMargins(8, 8, 8, 8);
    auto* drawSplit = new QSplitter(Qt::Horizontal, draw);
    drawSplit->setChildrenCollapsible(false);
    drawSplit->setHandleWidth(6);
    dgOuter->addWidget(drawSplit, 1);
    // One pane per drawing: the drawing itself, then its buttons, then any setting of its own.
    QWidget* paneHost[3] = {nullptr, nullptr, nullptr};
    QVBoxLayout* paneLay[3] = {nullptr, nullptr, nullptr};
    for (int i = 0; i < 3; ++i) {
        paneHost[i] = new QWidget(drawSplit);
        paneLay[i] = new QVBoxLayout(paneHost[i]);
        paneLay[i]->setContentsMargins(0, 0, 0, 0);
        paneLay[i]->setSpacing(4);
        drawSplit->addWidget(paneHost[i]);
        drawSplit->setStretchFactor(i, 1);
    }
    // A ROW OF BUTTONS THAT CAN GET OUT OF THE WAY. Two rows rather than one, so a pane needs half the width
    // to show them, and each button allowed to shrink below its caption - a pane pulled narrow elides the
    // words (the tooltip still says what it does) instead of refusing to be narrow and holding the whole
    // splitter open. The same two-equal-rows shape the ribbon's icon groups use.
    auto reflowButtons = [](QWidget* host) {
        const QList<QPushButton*> bs =
            host->findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly);
        // PREFERRED, not ignored: the window opens at a width where every caption is READ, because a button
        // whose words are cut in half ("r from opposite fa") is a button nobody can be sure of. The small
        // minimum is what still lets a pane be dragged narrow afterwards - then, and only then, the words
        // give way and the tooltip answers for them.
        for (QPushButton* b : bs) {
            b->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
            b->setMinimumWidth(28);
        }
        if (bs.size() < 3) return;
        delete host->layout();
        auto* g = new QGridLayout(host);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(2);
        const int cols = (bs.size() + 1) / 2;
        for (int i = 0; i < bs.size(); ++i)
            g->addWidget(bs[i], i / cols, i % cols);
    };
    struct Side {
        const char* caption;
        bool mechanical;
    };
    // Three drawings of a part, side by side: what it is on a circuit, what it is in the metal, and what
    // the harness does at it. Each pane is given room enough to read - a drawing squeezed into a thumbnail
    // is a drawing nobody can check.
    // THE MECHANICAL DRAWING FIRST. This is a harness library: what a part looks like in the metal is what
    // is drawn, checked and placed all day, and the schematic symbol is the occasional one. The first pane is
    // the one the eye lands on.
    const Side sides[] = {{"Mechanical drawing", true}, {"Schematic symbol", false},
                          {"Fanout front", false}};
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
    // ONE PLACE BUTTON, and a tick against each drawing it puts down.
    //
    // There was a Place on every pane - four of them, and a fifth for the fanout mirrored - which is five
    // buttons for one verb, and no way at all to put down two drawings of the same part together. A part
    // usually goes on a sheet as more than one of its drawings: the connector and the fanout that belongs to
    // it, the symbol and the fanout beside it.
    //
    // So: one button, and the menu says WHICH. Ticks rather than a list of one-shot commands, because
    // "which drawings of this part" is a standing answer - tick the two you always place together and press
    // Place from then on.
    {
        auto* row = new QWidget(draw);
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(6);
        rl->addWidget(showConnBtn_);
        placeBtn_ = new QToolButton(row);
        placeBtn_->setText(QStringLiteral("Place"));
        placeBtn_->setToolButtonStyle(Qt::ToolButtonTextOnly);
        placeBtn_->setPopupMode(QToolButton::MenuButtonPopup);
        placeBtn_->setToolTip(QStringLiteral(
            "Put this part on the sheet - then click where it goes. The arrow beside it says WHICH of its "
            "drawings: tick more than one and they go down together, in the order they are listed."));
        // THE MENU STAYS OPEN WHILE THE TICKS ARE MADE. A menu closes on the first thing clicked in it, so
        // choosing "the connector AND its fanout" meant opening the menu twice and hoping the first tick had
        // stuck - which is not a choice of two things, it is two choices of one. The ticks live in a panel
        // inside the menu (a widget in a menu takes its own clicks), with an Apply on the end of them: tick
        // what is wanted, in any order, and press Apply once.
        auto* pm = new QMenu(placeBtn_);
        auto* panel = new QWidget(pm);
        auto* pl = new QVBoxLayout(panel);
        pl->setContentsMargins(10, 6, 10, 6);
        pl->setSpacing(2);
        struct Which { const char* text; LibraryPartDrawing kind; };
        const Which whichOnes[] = {{"Mechanical drawing", LibraryPartDrawing::Mechanical},
                                   {"Schematic symbol", LibraryPartDrawing::Schematic},
                                   {"Fanout front", LibraryPartDrawing::Fanout},
                                   {"Fanout back", LibraryPartDrawing::FanoutOpposite}};
        for (const Which& w : whichOnes) {
            // Kept as ACTIONS, because that is what everything else already asks (see placeTicked), with a
            // tick box in the panel driving each one. One state, shown once.
            auto* act = new QAction(QString::fromLatin1(w.text), this);
            act->setCheckable(true);
            act->setData(static_cast<int>(w.kind));
            act->setChecked(w.kind == LibraryPartDrawing::Mechanical);
            auto* box = new QCheckBox(QString::fromLatin1(w.text), panel);
            box->setChecked(act->isChecked());
            connect(box, &QCheckBox::toggled, act, &QAction::setChecked);
            connect(act, &QAction::toggled, box, &QCheckBox::setChecked);
            pl->addWidget(box);
            placeWhat_.push_back(act);
        }
        auto* buttons = new QWidget(panel);
        auto* rl2 = new QHBoxLayout(buttons);
        rl2->setContentsMargins(0, 4, 0, 0);
        rl2->setSpacing(6);
        auto* all = new QPushButton(QStringLiteral("All of them it has"), buttons);
        connect(all, &QPushButton::clicked, this, [this, pm] {
            for (QAction* a : placeWhat_) a->setChecked(true);
            pm->close();
            placeTicked();
        });
        rl2->addWidget(all);
        rl2->addStretch(1);
        auto* apply = new QPushButton(QStringLiteral("Apply"), buttons);
        apply->setDefault(true);
        apply->setToolTip(QStringLiteral("Place what is ticked - the menu closes on this and nothing else"));
        connect(apply, &QPushButton::clicked, this, [this, pm] {
            pm->close();
            placeTicked();
        });
        rl2->addWidget(apply);
        pl->addWidget(buttons);
        auto* holder = new QWidgetAction(pm);
        holder->setDefaultWidget(panel);
        pm->addAction(holder);
        placeBtn_->setMenu(pm);
        connect(placeBtn_, &QToolButton::clicked, this, [this] { placeTicked(); });
        rl->addWidget(placeBtn_);
        rl->addStretch(1);
        dgOuter->addWidget(row);
    }
    for (int i = 0; i < 3; ++i) {
        const bool isFanout = i == 2;
        QWidget* fanoutSideRowPending = nullptr;
        auto* prev = new DrawingPreview(QString::fromLatin1(sides[i].caption), paneHost[i]);
        // Every pane the same, and every pane free to grow: what a drawing needs is room, and the four of
        // them have no reason to differ. Small enough that four fit a modest window, big enough to read.
        prev->setMinimumSize(150, 150);
        prev->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        if (isFanout) {
            fanoutPreview_ = prev;
            prev->setObjectName(QStringLiteral("fanoutPane"));
        } else if (sides[i].mechanical) mechPreview_ = prev;
        else schemPreview_ = prev;
        paneLay[i]->addWidget(prev, 1);
        if (isFanout) {
            // WHICH SIDE OF THE CONNECTOR THIS PANE IS WORKING ON. A connector has two fanouts and they are
            // two pictures, not one and its reflection: the wires are numbered the other way about, and
            // keying, a polarising tab or a shielded pair breaking out on one side only mean the back is not
            // the front held up to a mirror. So each side has its own drawing, captured and edited on its
            // own, and this box says which of the two the pane below shows and the buttons act on.
            //
            // It is also the part's ANSWER: the side showing is the side a plain "place the fanout" gives,
            // because the one somebody chose to leave selected is the one they meant.
            auto* sideRow = new QWidget(paneHost[i]);
            fanoutSideRow_ = sideRow;
            auto* sl = new QHBoxLayout(sideRow);
            sl->setContentsMargins(0, 0, 0, 0);
            // The words in front of it may be narrow too. This row sits under one of four drawings that
            // are meant to be the same size, and a splitter hands each pane what its contents ASK for -
            // so anything in a pane that asks for more than the drawing does makes that pane the odd one
            // out. Nothing here needs to hold the row open.
            auto* sideWords = new QLabel(QStringLiteral("Side"), sideRow);
            sideWords->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            sl->addWidget(sideWords);
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
                showFanoutSide();   // the drawing, the caption and the buttons follow the side
            });
            // AND IT MAY BE NARROW. "front - looking at the face" is a long thing to write in a box, and a
            // splitter gives each pane what its contents ask for: this one asked for half the row and got it,
            // which is why the fanout's drawing was three times the size of the others. The words are still
            // there when there is room for them.
            fanoutSideBox_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            fanoutSideBox_->setMinimumContentsLength(6);
            fanoutSideBox_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            sl->addWidget(fanoutSideBox_, 1);
            sideRow->setMinimumWidth(0);
            fanoutSideRowPending = sideRow;
        }
        auto* btns = new QWidget(paneHost[i]);
        auto* bl = new QHBoxLayout(btns);
        bl->setContentsMargins(0, 0, 0, 0);
        const bool mech = sides[i].mechanical;
        if (isFanout) {
            // All of them on THE SIDE SHOWING: take it off the sheet, edit it in its own session, start it
            // from a mirror of the other side, or throw it away. Each button asks the box which side that is.
            auto* cap = new QPushButton(QStringLiteral("Capture selection"), btns);
            cap->setToolTip(QStringLiteral(
                "Take what is selected on the sheet as this part's fanout on the side showing"));
            connect(cap, &QPushButton::clicked, this, [this] { beginCaptureFanout(showingBackFanout()); });
            bl->addWidget(cap);
            auto* edt = new QPushButton(QStringLiteral("Edit"), btns);
            edt->setToolTip(QStringLiteral(
                "Open the side showing in its own canvas session to add or move lines"));
            connect(edt, &QPushButton::clicked, this, [this] {
                editDrawing(showingBackFanout() ? LibraryPartDrawing::FanoutOpposite
                                                : LibraryPartDrawing::Fanout);
            });
            bl->addWidget(edt);
            // A STARTING POINT FROM THE OTHER SIDE, for the common case where the two sides really are
            // mirror images bar a detail: mirrored once into a real drawing that can then be corrected by
            // hand, not a transform re-applied every time one is placed.
            fanoutMirrorBtn_ = new QPushButton(QStringLiteral("Mirror the other side"), btns);
            connect(fanoutMirrorBtn_, &QPushButton::clicked, this,
                    [this] { mirrorFanoutFromOpposite(showingBackFanout()); });
            bl->addWidget(fanoutMirrorBtn_);
            auto* clr = new QPushButton(QStringLiteral("Clear"), btns);
            clr->setToolTip(QStringLiteral("Throw away the drawing on the side showing"));
            connect(clr, &QPushButton::clicked, this, [this] {
                if (LibraryPart* p = current()) {
                    if (showingBackFanout()) p->fanoutOpposite = QJsonArray();
                    else p->fanout = QJsonArray();
                    showFanoutSide();
                    touched();
                    reloadTree();
                }
            });
            bl->addWidget(clr);
            reflowButtons(btns);
            paneLay[i]->addWidget(btns);
            if (fanoutSideRowPending) paneLay[i]->addWidget(fanoutSideRowPending);
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
        auto* edt = new QPushButton(QStringLiteral("Edit"), btns);
        edt->setToolTip(mech ? QStringLiteral(
                                  "Open it in its own canvas session (a grey one, like a default "
                                  "model's) to add or move lines and mark where it connects, which "
                                  "way it runs from there, and its two ends.")
                             : QStringLiteral("Open it in its own canvas session to add or move lines"));
        connect(edt, &QPushButton::clicked, this, [this, mech] {
            editDrawing(mech ? LibraryPartDrawing::Mechanical : LibraryPartDrawing::Schematic);
        });
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
        // FROM A DEFAULT MODEL. The house style already holds a drawn ferrule, ring, backshell, connector -
        // the pieces every harness is made of - and a library part is very often one of exactly those with a
        // part number on it. Drawing it a second time, by hand, to say the same thing is work nobody should
        // do: this takes the house drawing as the part's own, and it can be edited from there like any other.
        if (mech) {
            // THE FACES THE WIRES LEAVE BY, applied from here. They are marked on the drawing itself - two
            // clicks across the face - so this opens the part's mechanical drawing with that tool in hand,
            // which is the one thing that cannot be done by typing in this window. Only for the kinds wires
            // actually go into: a length of tape has no face to mark, and offering it one would be offering
            // a setting with no meaning.
            outputFacesBtn_ = new QPushButton(QStringLiteral("Output faces"), btns);
            outputFacesBtn_->setToolTip(QStringLiteral(
                "Mark the faces the conductors leave by - click across each one, in the order the branches "
                "come out. A part with a face marked is SET DOWN by it: the face across the wires, its "
                "centre on the joint, the body away from the harness."));
            connect(outputFacesBtn_, &QPushButton::clicked, this, [this] {
                if (currentId_.isEmpty()) return;
                emit markOutputFacesRequested(currentId_);
                hide();
            });
            bl->addWidget(outputFacesBtn_);
        }
        auto* fromModel = new QPushButton(QStringLiteral("From default"), btns);
        fromModel->setToolTip(QStringLiteral(
            "Take this drawing from a default mechanical model - the house style for a ferrule, a ring, a "
            "backshell, a connector. The list opens with each model's drawings beside it, so what is being "
            "taken is seen before it is taken."));
        connect(fromModel, &QPushButton::clicked, this, [this, mech] { importFromDefaultModel(mech); });
        bl->addWidget(fromModel);
        auto* clr = new QPushButton(QStringLiteral("Clear"), btns);
        connect(clr, &QPushButton::clicked, this, [this, mech] { clearDrawing(mech); });
        bl->addWidget(clr);
        reflowButtons(btns);
        paneLay[i]->addWidget(btns);
    }
    // NO FOURTH PANE. The fanout already says which side of the connector it was drawn from - front,
    // looking at the face, or back, looking at the wires - and that is the whole of the difference between
    // the two sides. A second drawing window for "the other one" asks the user to draw twice what the
    // combo box already says once, and then to keep the two in step by hand.
    //
    // A part that already HAS an opposite drawing keeps it (LibraryPart::fanoutOpposite is still read when
    // one is placed); there is simply no longer a window inviting anybody to make another.
    fanoutOppositePreview_ = nullptr;
    // EQUAL TO BEGIN WITH, and said once the row has a width to divide. A splitter left to itself gives
    // each pane whatever its contents ask for, and asking for equal shares before the window has been laid
    // out only gets them clamped to their minimums with the leftover handed to whichever pane asked
    // loudest. So it is done on the first turn of the event loop, when there is a real width - and once
    // only, so a pane the user drags afterwards stays where they put it.
    QTimer::singleShot(0, drawSplit, [drawSplit] {
        const int n = drawSplit->count();
        if (n <= 0 || drawSplit->width() <= 0) return;
        drawSplit->setSizes(QList<int>(n, std::max(1, drawSplit->width() / n)));
    });
    // The remembered answer, given to the panes now they exist: a preference that is read back but not
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
        auto* cl = new QVBoxLayout(group);
        // COMES WITH ITS PINS. Most connectors are ordered as a housing and its contacts separately, and
        // the contact is a part of its own tied to the housing. Some come with the pins ALREADY IN THEM -
        // a moulded header, an overmoulded lead, a power connector with its terminals fitted - and for
        // those there is no contact to order, so nothing to tie: the menu below is not a question that has
        // an answer, and it goes. What matters instead is what those pins ARE.
        includePinsBox_ = new QCheckBox(QStringLiteral("Comes with its pins fitted"), group);
        includePinsBox_->setToolTip(QStringLiteral(
            "Ticked, this connector is bought with its pins in it: there is no contact to order, so the "
            "contact menu goes and the pins are described here instead. A connector can carry more than "
            "one size of pin - two heavy ways and six signal ways in one housing - so there is a row per "
            "type."));
        connect(includePinsBox_, &QCheckBox::toggled, this, [this](bool on) {
            if (syncing_) return;
            if (LibraryPart* p = current()) {
                setPartIncludesPins(*p, on);
                if (on) p->setContactIds(QStringList());   // nothing to order, so nothing tied
                touched();
                refreshContactChoice();
                rebuildPinTypes();
            }
        });
        cl->addWidget(includePinsBox_);
        auto* contactRow = new QWidget(group);
        contactRowHost_ = contactRow;
        auto* crl = new QHBoxLayout(contactRow);
        crl->setContentsMargins(0, 0, 0, 0);
        contactBox_ = new QComboBox(contactRow);
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
        crl->addWidget(contactBox_, 1);
        cl->addWidget(contactRow);
        // THE PIN TYPES: a size, the conductor it takes and in which unit, its rating, and WHICH PINS are
        // that type. The last of those is what lets a schematic label each pin with its own size instead of
        // one figure for the whole connector - which on a connector with two sizes in it is a figure
        // somebody wires the wrong way from.
        pinTypesHost_ = new QWidget(group);
        // Named, so its rows can be told from the parameter form's - both have a "Conductor".
        pinTypesHost_->setObjectName(QStringLiteral("pinTypes"));
        pinTypesLay_ = new QVBoxLayout(pinTypesHost_);
        pinTypesLay_->setContentsMargins(0, 2, 0, 0);
        pinTypesLay_->setSpacing(2);
        cl->addWidget(pinTypesHost_);
        auto* addPinType = makeAddButton(group, QStringLiteral("Another type of pin"),
                                         QStringLiteral("A second size of pin in the same housing"));
        addPinTypeBtn_ = addPinType;
        connect(addPinType, &QPushButton::clicked, this, [this] {
            LibraryPart* p = current();
            if (!p) return;
            std::vector<PartPinType> types = partPinTypes(*p);
            PartPinType fresh;
            fresh.unit = QStringLiteral("AWG");
            types.push_back(fresh);
            // Written straight away with a placeholder size, so the row has something to be keyed by and
            // survives the rebuild below.
            if (types.back().size.isEmpty())
                types.back().size = QStringLiteral("size %1").arg(types.size());
            setPartPinTypes(*p, types);
            touched();
            rebuildPinTypes();
        });
        cl->addWidget(addPinType, 0, Qt::AlignLeft);
        rv->addWidget(group);
    }

    rv->addWidget(draw, 2);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);

    auto* foot = new QHBoxLayout;
    summary_ = new QLabel(QString(), this);
    setRole(summary_, "info");
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

void PartsLibraryDialog::reloadTreeForTest() {
    catalog_.openRemembered();
    reloadTree();
    showPart(currentId_);
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
    if (fanoutOppositePreview_) fanoutOppositePreview_->setDrawing(p ? p->fanoutOpposite : QJsonArray());
    // The part's own side goes into the box, and the pane then shows THAT side's drawing - which is the
    // point of recording it: a part opens on the side it is normally placed from.
    if (fanoutSideBox_ && p) {
        const QSignalBlocker block(fanoutSideBox_);
        const int at = fanoutSideBox_->findData(p->fanoutSide());
        fanoutSideBox_->setCurrentIndex(at >= 0 ? at : 0);
    }
    showFanoutSide();
    refreshDatasheetRow();
    if (mechPreview_) {
        if (have && p) mechPreview_->setEntities(markedMechanicalDrawing(*p));
        else mechPreview_->setDrawing(QJsonArray());
        // HOW MANY FACES ARE MARKED, said on the pane itself. "Has this one been told where its wires
        // leave" is a question about the part, and it should be answerable by looking at the part rather
        // than by opening it for editing to find out.
        QString caption = QStringLiteral("Mechanical drawing");
        if (p && !p->outputs.empty())
            caption += QStringLiteral(" — %1 output face(s)").arg(p->outputs.size());
        else if (p && partKindTakesWires(p->kind))
            caption += QStringLiteral(" — no output face marked");
        mechPreview_->setCaption(caption);
    }
    if (outputFacesBtn_) {
        const bool wires = p && partKindTakesWires(p->kind);
        outputFacesBtn_->setVisible(wires);
        // Nothing drawn is nothing to mark ON: a face is two clicks across the part, and there has to be a
        // part there to click across.
        outputFacesBtn_->setEnabled(wires && p && p->hasMechanical());
    }
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
                // AND A WAY TO TAKE ONE OFF THE LIST. What is typed into one of these is remembered and
                // offered to every part from then on - which is right, and it means a misspelling typed
                // once is offered for ever. Right-click the box and the entry showing can be forgotten,
                // if it was ours to forget: the built-in materials are the program's and stay.
                {
                    const QString key = f.key;
                    const QStringList builtIn = f.choices;
                    box->setContextMenuPolicy(Qt::CustomContextMenu);
                    connect(box, &QComboBox::customContextMenuRequested, this,
                            [this, box, key, builtIn](const QPoint& at) {
                                const QString showing = box->currentText().trimmed();
                                QMenu menu(box);
                                const bool ours = !showing.isEmpty() &&
                                                  !builtIn.contains(showing, Qt::CaseInsensitive);
                                QAction* forget = menu.addAction(
                                    ours ? QStringLiteral("Take \"%1\" off the list").arg(showing)
                                         : QStringLiteral("Nothing here to take off the list"));
                                forget->setEnabled(ours);
                                if (menu.exec(box->mapToGlobal(at)) != forget || !ours) return;
                                forgetChoice(key, showing);
                                const int at2 = box->findText(showing);
                                if (at2 >= 0) box->removeItem(at2);
                                box->setCurrentIndex(-1);
                                box->setCurrentText(QString());
                                if (LibraryPart* cp = current()) {
                                    if (cp->value(key).compare(showing, Qt::CaseInsensitive) == 0) {
                                        cp->params.remove(key);
                                        touched();
                                    }
                                }
                                if (summary_)
                                    summary_->setText(
                                        QStringLiteral("\"%1\" is off the list - it is no longer offered "
                                                       "to other parts")
                                            .arg(showing));
                            });
                }
                // AND A WAY TO UNSAY IT. A parameter picked from a list could be changed but never taken
                // back off: the list has no blank entry, and clearing the text by hand left a part
                // remembering an empty string as though somebody had answered "". A part whose material
                // has not been decided yet is a real state and it needs a way to be said.
                {
                    // THE CROSS IS NOT PART OF THE FIELD. A row is [label][field][unit], and every field is
                    // one width so the column reads straight down - but a menu with a cross beside it inside
                    // that width made the MENU narrower than the plain boxes above and below it by exactly
                    // the width of the cross. The field keeps the width; the cross hangs off the end of it.
                    auto* host = new QWidget(paramHost_);
                    auto* hl = new QHBoxLayout(host);
                    hl->setContentsMargins(0, 0, 0, 0);
                    hl->setSpacing(2);
                    box->setMinimumWidth(260);
                    box->setMaximumWidth(260);
                    hl->addWidget(box, 0);
                    auto* clear = new QToolButton(host);
                    clear->setIcon(crossIcon(warnInk(), 12));
                    clear->setIconSize(QSize(12, 12));
                    clear->setAutoRaise(true);
                    clear->setFocusPolicy(Qt::NoFocus);
                    clear->setFixedSize(18, 18);
                    clear->setToolTip(QStringLiteral("Clear %1 - leave it unsaid").arg(f.label.toLower()));
                    const QString clearKey = f.key;
                    connect(clear, &QToolButton::clicked, this, [this, box, clearKey] {
                        box->setCurrentIndex(-1);
                        box->setCurrentText(QString());
                        if (LibraryPart* cp = current()) {
                            cp->params.remove(clearKey);
                            touched();
                        }
                    });
                    hl->addWidget(clear);
                    field = host;
                }
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
                        setRole(dash, "faint");
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
            if (!DatasheetStore::exists(sheet)) it->setForeground(missInk());
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

void PartsLibraryDialog::beginPlacement(std::vector<Entity> entities) {
    if (!editor_) return;
    editor_->placeEntities(std::move(entities));
    if (!editor_->placingPaste()) return;   // nothing was handed over after all

    // The library steps aside only while the ghost is being handled. A click leaves it hidden; Esc is
    // the way out, so it brings the same part back rather than leaving the library closed behind it.
    auto* placing = new QObject(this);
    connect(editor_, &Editor::pastePlacementFinished, placing,
            [this, placing](bool placed) {
        placing->deleteLater();
        if (placed) return;
        if (summary_)
            summary_->setText(QStringLiteral("Placement cancelled — nothing was put on the sheet."));
        show();
        raise();
        activateWindow();
    });

    hide();
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
    beginPlacement(std::move(block));
    const LibraryPart* contact = contactId.isEmpty() ? nullptr : catalog_.byId(contactId);
    if (summary_)
        summary_->setText(contact ? QStringLiteral("Click the sheet to place %1, with %2 — %3 of them on "
                                                   "the bill of materials")
                                        .arg(p->title(), contact->title())
                                        .arg(p->wayCount())
                                  : QStringLiteral("Click the sheet to place %1").arg(p->title()));
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

void PartsLibraryDialog::beginCaptureFanout(bool opposite) {
    LibraryPart* p = current();
    if (!p || !editor_) return;
    const std::vector<Entity> sel = editor_->copyOfSelection();
    if (sel.empty()) {
        if (summary_)
            summary_->setText(QStringLiteral(
                "Select the fanout on the sheet first — the wires as they leave the connector"));
        return;
    }
    const QJsonArray captured = partDrawingFromEntities(sel);
    if (opposite) {
        p->fanoutOpposite = captured;
        if (fanoutOppositePreview_) fanoutOppositePreview_->setDrawing(captured);
    } else {
        p->fanout = captured;
    }
    showFanoutSide();
    touched();
    reloadTree();
    if (summary_)
        summary_->setText(QStringLiteral("Captured %1 object(s) as the fanout %2 of this connector")
                              .arg(sel.size())
                              .arg(opposite ? QStringLiteral("back") : QStringLiteral("front")));
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
    // Mirrored, and the opposite side has its own captured drawing: place THAT, as it was actually
    // drawn - not a live mirror transform of the front, which is only ever a guess once a connector's
    // two faces are not identical (keying, an asymmetric rib, a polarising tab). A part that has
    // never used "Mirror from opposite fanout" still gets the old runtime mirror, unchanged.
    std::vector<Entity> block;
    if (mirrored && p->hasFanoutOpposite())
        block = partDrawingBlockInstance(*p, p->fanoutOpposite, QStringLiteral("fanout"),
                                         Vec2{0.0, 0.0}, false);
    else
        block = partDrawingBlockInstance(*p, p->fanout, QStringLiteral("fanout"), Vec2{0.0, 0.0},
                                         mirrored);
    const QString contactId = p->contactId();
    if (!contactId.isEmpty())
        for (Entity& e : block) e.meta.insert(QLatin1String(kContactPartKey), contactId);
    beginPlacement(std::move(block));
    // Mirroring turns the side it shows into the other one, and saying so is the whole point of the button.
    const QString shows = p->fanoutSide() == QStringLiteral("front")
                              ? (mirrored ? QStringLiteral("back") : QStringLiteral("front"))
                              : (mirrored ? QStringLiteral("front") : QStringLiteral("back"));
    if (summary_)
        summary_->setText(QStringLiteral("Click the sheet to place the fanout of %1 — the %2 of the "
                                         "connector")
                              .arg(p->title(), shows));
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
    // WITH THE PINS FITTED THERE IS NOTHING TO TIE. The menu is not a question with an answer for a
    // connector that comes with its pins in it, so it goes rather than sitting there greyed - and the tick
    // itself is only offered to a connector, since nothing else is bought either way.
    const bool withPins = isConnector && partIncludesPins(*p);
    if (includePinsBox_) {
        const QSignalBlocker hush(includePinsBox_);
        includePinsBox_->setVisible(isConnector);
        includePinsBox_->setChecked(withPins);
    }
    if (contactRowHost_) contactRowHost_->setVisible(!withPins);
    if (contactGroupBox_ && withPins)
        contactGroupBox_->setTitle(QStringLiteral("Its own pins"));
    const QSignalBlocker block(contactBox_);
    contactBox_->clear();
    if (!isConnector && !isContact) return;
    contactBox_->addItem(isContact ? QStringLiteral("(none — not tied to a connector)")
                                   : QStringLiteral("(none — the connector on its own)"),
                         QString());
    const PartsLibrary merged = catalog_.merged();
    for (const LibraryPart& part : merged.parts) {
        // A CONTACT PIN, and nothing else. A crimp terminal is crimped on a wire like a contact is, but it
        // goes on a stud or a post - it does not go INSIDE a connector, and offering it here as something a
        // connector is "used with" invites a bill of materials that orders ring terminals by the way count.
        const bool offer =
            isContact ? part.kind == PartKind::Connector : part.kind == PartKind::ContactPin;
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
    // And the pins it comes with, if it does: rebuilt from the part, because a form that remembers the last
    // part's rows is a form that quietly writes them onto this one.
    rebuildPinTypes();
}

void PartsLibraryDialog::editDrawing(LibraryPartDrawing kind) {
    // The host (MainWindow) owns the actual session, the way MechModelsDialog's onEdit_ works for a
    // default model - this window just steps aside for it, the same as it does for a capture.
    if (!onEdit_ || currentId_.isEmpty()) return;
    onEdit_(currentId_, kind);
    hide();
}

LibraryPart* PartsLibraryDialog::partForSession(const QString& id) {
    if (id.isEmpty()) return nullptr;
    if (PartsLibrary* own = catalog_.byPath(catalog_.pathOf(id))) return own->byId(id);
    return catalog_.active().byId(id);
}

// WHICH SIDE THE FANOUT PANE IS WORKING ON, and that pane brought into step with it. The front is the
// part's `fanout`, the back its `fanoutOpposite`: two drawings, because the back of a connector is not the
// front held up to a mirror (see the pane's own note). Whichever is showing is the side captured into,
// edited, cleared - and the side the part answers with when a plain fanout is asked for.
bool PartsLibraryDialog::showingBackFanout() const {
    return fanoutSideBox_ && fanoutSideBox_->currentData().toString() == QStringLiteral("back");
}

void PartsLibraryDialog::showFanoutSide() {
    const LibraryPart* p = current();
    const bool back = showingBackFanout();
    const QJsonArray mine = !p ? QJsonArray() : (back ? p->fanoutOpposite : p->fanout);
    const QJsonArray other = !p ? QJsonArray() : (back ? p->fanout : p->fanoutOpposite);
    if (fanoutPreview_) {
        // The caption says which side is on show, because two drawings in one pane with nothing to tell
        // them apart is worse than one drawing.
        fanoutPreview_->setCaption(back ? QStringLiteral("Fanout back")
                                        : QStringLiteral("Fanout front"));
        fanoutPreview_->setEmptyText(back ? QStringLiteral("nothing drawn for the back yet")
                                          : QStringLiteral("nothing drawn for the front yet"));
        fanoutPreview_->setDrawing(mine);
    }
    if (fanoutMirrorBtn_) {
        fanoutMirrorBtn_->setText(back ? QStringLiteral("Mirror the front")
                                       : QStringLiteral("Mirror the back"));
        fanoutMirrorBtn_->setToolTip(
            QStringLiteral("Start this side from a mirror of the %1, then correct it by hand")
                .arg(back ? QStringLiteral("front") : QStringLiteral("back")));
        fanoutMirrorBtn_->setEnabled(!other.isEmpty());
    }
}

void PartsLibraryDialog::mirrorFanoutFromOpposite(bool intoOpposite) {
    LibraryPart* p = current();
    if (!p) return;
    const QJsonArray& from = intoOpposite ? p->fanout : p->fanoutOpposite;
    if (from.isEmpty()) {
        if (summary_)
            summary_->setText(QStringLiteral(
                "Nothing on that side yet to mirror — capture or draw it first"));
        return;
    }
    // The exact mirror math placement already uses (partDrawingBlockInstance's mirrored branch,
    // mirrorEntity about a horizontal line through the origin) - run once, into a real drawing this
    // side can then be hand-corrected and captured over, not a transform re-applied every place.
    std::vector<Entity> ents = partDrawingEntities(from);
    for (Entity& e : ents) mirrorEntity(e, Vec2{0.0, 0.0}, Vec2{1.0, 0.0});
    const QJsonArray mirrored = partDrawingFromEntities(ents);
    if (intoOpposite) {
        p->fanoutOpposite = mirrored;
        if (fanoutOppositePreview_) fanoutOppositePreview_->setDrawing(mirrored);
    } else {
        p->fanout = mirrored;
    }
    showFanoutSide();
    touched();
    saveLibrary();
    reloadTree();
    if (summary_)
        summary_->setText(QStringLiteral(
            "Mirrored as a starting point — edit it on the canvas if the two sides are not identical, "
            "then Capture selection to keep the correction"));
}

// The eye's answer, given to all three drawings. In one place because the three panes are shown one part
// three ways and there is no sense in which one of them could be showing its marks and another not.
// THE PART'S OWN MARKS, over its mechanical drawing: where it connects and which way it runs from there,
// the two ends, and the faces the conductors leave by - the same marks, in the same colours, that the
// editing canvas draws while they are being put on. They belong in this window because this is where a part
// is chosen and checked: "has this one been marked up, and where" should be answerable by looking.
//
// The mechanical drawing only. A schematic symbol is a glyph on a circuit and a fanout is a picture of how
// the wires break out; neither is placed by running a direction along a run.
std::vector<Entity> PartsLibraryDialog::markedMechanicalDrawing(const LibraryPart& part) {
    std::vector<Entity> shown = partDrawingEntities(part.mechanical);
    if (!showConnBtn_ || !showConnBtn_->isChecked()) return shown;
    Bounds ext;
    for (const Entity& e : shown) ext.expand(e.bounds());
    // Sized to the part, because a mark is read against the thing it is on: three millimetres of ring on a
    // part two metres long is not a mark, it is a speck.
    const double r = ext.valid ? std::max(0.4, std::max(ext.width(), ext.height()) * 0.06) : 2.0;
    const double arm = std::max(6.0 * r, ext.valid ? ext.width() * 0.35 : 10.0);
    for (Entity& m : placementMarkEntities(part.hasConnect, part.connectPoint, part.connectAngleRad,
                                           part.hasNearPoint, part.nearPoint, part.hasFarPoint,
                                           part.farPoint, r, arm))
        shown.push_back(std::move(m));
    for (Entity& m : outputFaceMarkEntities(part.outputs, r)) shown.push_back(std::move(m));
    return shown;
}

void PartsLibraryDialog::applyShowConnection() {
    const bool on = !showConnBtn_ || showConnBtn_->isChecked();
    for (DrawingPreview* prev : {schemPreview_, mechPreview_, fanoutPreview_, fanoutOppositePreview_})
        if (prev) prev->setShowConnection(on);
    // The marks are DRAWN INTO the mechanical pane rather than switched off in the renderer, so that pane
    // has to be filled again - with them or without them.
    if (mechPreview_) {
        if (const LibraryPart* p = current()) mechPreview_->setEntities(markedMechanicalDrawing(*p));
        else mechPreview_->setDrawing(QJsonArray());
    }
}

// THE TAKING ITSELF, apart from the window that chooses what to take. The window's business is picking a
// model and a side of it; this copies the drawing over, writes the library and shows the result. Apart,
// because "a part can be drawn from the house style" is a thing to be able to check without opening a modal
// window and clicking in it - and because the four arguments are then said out loud rather than implied by
// which button was pressed.
//
// `fromSchematic` is which of the MODEL's two drawings is being taken; `intoMechanical` is which of the
// PART's it lands in. They are separate on purpose: a part's schematic symbol can be worth taking from a
// model's mechanical view and the other way about, and the pane the button sits on has already settled the
// second of them.
bool PartsLibraryDialog::takeDrawingFromDefaultModel(const MechModelSet& models, MechModelKind kind,
                                                     const QString& modelName, bool fromSchematic,
                                                     bool intoMechanical) {
    LibraryPart* part = current();
    const MechModel* m = models.of(kind, modelName);
    if (!part || !m) return false;
    const QJsonArray d = fromSchematic ? m->schematicDrawing : m->drawing;
    // Nothing drawn is nothing to take, and taking it would CLEAR the part's own drawing - which is a
    // destructive answer to "give me the house one".
    if (d.isEmpty()) {
        if (summary_)
            summary_->setText(QStringLiteral("That model has no %1 drawing — nothing to take")
                                  .arg(fromSchematic ? QStringLiteral("schematic")
                                                     : QStringLiteral("mechanical")));
        return false;
    }
    if (intoMechanical) part->mechanical = d;
    else part->schematic = d;
    touched();
    saveLibrary();
    if (intoMechanical && mechPreview_) mechPreview_->setDrawing(part->mechanical);
    if (!intoMechanical && schemPreview_) schemPreview_->setDrawing(part->schematic);
    reloadTree();
    if (summary_)
        summary_->setText(
            QStringLiteral("Taken from the %1 model's %2 drawing — edit it from here like any other")
                .arg(mechModelName(kind), fromSchematic ? QStringLiteral("schematic")
                                                        : QStringLiteral("mechanical")));
    return true;
}

// IMPORT A DRAWING FROM THE HOUSE STYLE.
//
// The default models already hold a drawn ferrule, ring, backshell, connector - the pieces every harness is
// made of - and a library part is very often one of exactly those with a part number on it. Drawing it again
// by hand to say the same thing is work nobody should do.
//
// The window shows the models on the left and, on the right, the drawings the chosen one HAS: its mechanical
// drawing and, for the kinds that keep one, its schematic. Both are shown because a model can hold both and
// which of them is wanted is the user's business - a part's schematic symbol may well be worth taking from a
// model's schematic rather than from its mechanical view. Each is taken with its own button, under the drawing
// it takes, so what is being taken is the thing being looked at.
void PartsLibraryDialog::importFromDefaultModel(bool mechanical) {
    LibraryPart* p = current();
    if (!p) {
        if (summary_) summary_->setText(QStringLiteral("Pick a part first"));
        return;
    }
    MechModelSet models;
    models.load();

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("%1 — take the %2 drawing from a default model")
                           .arg(p->title(),
                                mechanical ? QStringLiteral("mechanical") : QStringLiteral("schematic")));
    dlg.setModal(true);
    dlg.resize(940, 560);
    auto* outer = new QVBoxLayout(&dlg);
    auto* what = new QLabel(
        QStringLiteral("The house style for the pieces a harness is made of. Pick one on the left; what it "
                       "holds is drawn on the right. Take either drawing - a part's %1 drawing can be worth "
                       "taking from either side of a model.")
            .arg(mechanical ? QStringLiteral("mechanical") : QStringLiteral("schematic")),
        &dlg);
    what->setWordWrap(true);
    outer->addWidget(what);

    auto* split = new QSplitter(Qt::Horizontal, &dlg);
    auto* list = new QTreeWidget(split);
    list->setHeaderHidden(true);
    list->setMinimumWidth(240);
    split->addWidget(list);

    auto* panes = new QWidget(split);
    auto* pl = new QHBoxLayout(panes);
    pl->setContentsMargins(0, 0, 0, 0);
    struct Side {
        DrawingPreview* view = nullptr;
        QPushButton* take = nullptr;
        bool schematic = false;
    };
    Side sides[2];
    for (int i = 0; i < 2; ++i) {
        auto* host = new QWidget(panes);
        auto* hl = new QVBoxLayout(host);
        hl->setContentsMargins(0, 0, 0, 0);
        sides[i].schematic = i == 1;
        sides[i].view = new DrawingPreview(sides[i].schematic ? QStringLiteral("Its schematic drawing")
                                                             : QStringLiteral("Its mechanical drawing"),
                                          host);
        sides[i].view->setEmptyText(QStringLiteral("this model has none"));
        sides[i].view->setMinimumSize(200, 220);
        hl->addWidget(sides[i].view, 1);
        sides[i].take = new QPushButton(QStringLiteral("Take this one"), host);
        hl->addWidget(sides[i].take);
        pl->addWidget(host, 1);
    }
    split->addWidget(panes);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    outer->addWidget(split, 1);

    auto* said = new QLabel(&dlg);
    setRole(said, "info");
    said->setWordWrap(true);
    outer->addWidget(said);
    auto* row = new QHBoxLayout;
    row->addStretch(1);
    auto* close = new QPushButton(QStringLiteral("Cancel"), &dlg);
    connect(close, &QPushButton::clicked, &dlg, &QDialog::reject);
    row->addWidget(close);
    outer->addLayout(row);

    // The models, by kind, with the named sub-models under their kind - the same shape the manager lists
    // them in, because it is the same list and learning it twice is learning it wrong. Only what has been
    // DRAWN is offered: a model with nothing in it is nothing to import.
    for (MechModelKind kind : allMechModelKinds()) {
        std::vector<const MechModel*> of = models.all(kind);
        std::vector<const MechModel*> drawn;
        for (const MechModel* m : of)
            if (m && (!m->drawing.isEmpty() || !m->schematicDrawing.isEmpty())) drawn.push_back(m);
        if (drawn.empty()) continue;
        auto* head = new QTreeWidgetItem(list, QStringList{mechModelName(kind)});
        QFont hf = head->font(0);
        hf.setBold(true);
        head->setFont(0, hf);
        head->setFlags(Qt::ItemIsEnabled);   // a heading, not a thing to pick
        for (const MechModel* m : drawn) {
            const QString label = m->name.isEmpty() ? mechModelName(kind) : m->name;
            auto* item = new QTreeWidgetItem(head, QStringList{label});
            item->setData(0, Qt::UserRole, static_cast<int>(kind));
            item->setData(0, Qt::UserRole + 1, m->name);
        }
    }
    list->expandAll();

    // What is showing, and whether either half of it can be taken.
    auto chosen = [&list, &models]() -> const MechModel* {
        QTreeWidgetItem* item = list->currentItem();
        if (!item || item->data(0, Qt::UserRole).isNull()) return nullptr;
        const auto kind = static_cast<MechModelKind>(item->data(0, Qt::UserRole).toInt());
        return models.of(kind, item->data(0, Qt::UserRole + 1).toString());
    };
    auto show = [&] {
        const MechModel* m = chosen();
        for (int i = 0; i < 2; ++i) {
            const QJsonArray d = !m ? QJsonArray()
                                    : (sides[i].schematic ? m->schematicDrawing : m->drawing);
            sides[i].view->setDrawing(d);
            sides[i].take->setEnabled(!d.isEmpty());
        }
        said->setText(m ? QStringLiteral("%1 — %2 object(s) in the mechanical drawing, %3 in the schematic")
                              .arg(mechModelName(m->kind))
                              .arg(m->drawing.size())
                              .arg(m->schematicDrawing.size())
                        : QStringLiteral("Pick a model on the left"));
    };
    connect(list, &QTreeWidget::currentItemChanged, &dlg, [&show] { show(); });
    show();

    for (int i = 0; i < 2; ++i) {
        connect(sides[i].take, &QPushButton::clicked, &dlg, [&, i] {
            const MechModel* m = chosen();
            if (!m) return;
            if (takeDrawingFromDefaultModel(models, m->kind, m->name, sides[i].schematic, mechanical))
                dlg.accept();
        });
    }
    dlg.exec();
}

// PUT DOWN WHAT IS TICKED, as one placement.
//
// A part usually goes on a sheet as more than one of its drawings - the connector and the fanout that belongs
// to it, the symbol with the fanout beside it - and there was no way to do that at all: five separate Place
// buttons, each one its own trip to the sheet and its own lining-up by hand.
//
// Side by side in the order the menu lists them, each a gap clear of the last, and handed over as ONE set: so
// they land together, keep their spacing, and are moved together afterwards.
// THE PIN TYPES OF A CONNECTOR THAT COMES WITH ITS PINS: one row each, and a row is a size, the conductor
// it takes and in which unit, a rating if the datasheet gives one, and which pins are that type.
//
// Rebuilt from the part rather than kept in the widgets: a part is switched to in one click, and a form that
// remembers the last part's rows is a form that quietly writes them onto this one.
void PartsLibraryDialog::rebuildPinTypes() {
    if (!pinTypesLay_) return;
    while (QLayoutItem* item = pinTypesLay_->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    LibraryPart* p = current();
    const bool on = p && p->kind == PartKind::Connector && partIncludesPins(*p);
    if (pinTypesHost_) pinTypesHost_->setVisible(on);
    if (addPinTypeBtn_) addPinTypeBtn_->setVisible(on);
    if (!on || !p) return;
    std::vector<PartPinType> types = partPinTypes(*p);
    if (types.empty()) {
        PartPinType first;
        first.unit = QStringLiteral("AWG");
        first.size = QStringLiteral("size 1");
        types.push_back(first);
        setPartPinTypes(*p, types);
    }
    const int ways = p->wayCount();
    // WHAT EACH FIELD IS. Five boxes in a row with nothing over them is a row somebody has to work out by
    // typing in it and seeing what happens - and the two in the middle ("20", "AWG") read as one figure
    // until you know that the first is the pin's own size and the second the conductor it takes.
    {
        auto* head = new QWidget(pinTypesHost_);
        auto* hl = new QHBoxLayout(head);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(4);
        struct Col { const char* text; int width; int stretch; };
        // WHAT EACH COLUMN IS, in the words a datasheet uses. "Conductor" is what the column holds, but what
        // a datasheet calls it is its CROSS SECTION - and the unit box needs no heading at all, because it
        // says AWG or mm2 on its own face. "Current (A" was a heading cut in half by a column too narrow for
        // it, which is worse than no heading.
        const Col cols[] = {{"Pin size", 70, 0},
                            {"Cross section", 90, 0},
                            {"", 62, 0},
                            {"Current (A)", 70, 0},
                            {"Pins", 0, 1}};
        for (const Col& c : cols) {
            auto* cap = new QLabel(QString::fromLatin1(c.text), head);
            setRole(cap, "caption");
            if (c.width > 0) cap->setFixedWidth(c.width);
            hl->addWidget(cap, c.stretch);
        }
        // The column of crosses at the end has no name: a cross says what it does.
        auto* pad = new QLabel(QString(), head);
        pad->setFixedWidth(18);
        hl->addWidget(pad);
        pinTypesLay_->addWidget(head);
    }
    for (size_t row = 0; row < types.size(); ++row) {
        auto* host = new QWidget(pinTypesHost_);
        auto* hl = new QHBoxLayout(host);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(4);
        const size_t at = row;
        // Everything on ONE ROW, in the order it is read off a datasheet: what the pin is called, what it
        // takes, in what, what it carries, and which of the ways are it.
        auto write = [this, at](const std::function<void(PartPinType&)>& change) {
            LibraryPart* cp = current();
            if (!cp) return;
            std::vector<PartPinType> now = partPinTypes(*cp);
            if (at >= now.size()) return;
            change(now[at]);
            setPartPinTypes(*cp, now);
            touched();
            // The schematic is drawn from these when it is built from data, so what is showing is stale.
            if (schemPreview_ && !cp->hasSchematic())
                schemPreview_->setDrawing(partSchematicDrawing(*cp));
        };
        auto* size = new QLineEdit(types[row].size, host);
        size->setPlaceholderText(QStringLiteral("size"));
        size->setFixedWidth(70);
        size->setToolTip(QStringLiteral("What the maker calls this pin - \"16\", \"20\", \"6.3 mm blade\""));
        connect(size, &QLineEdit::textEdited, this,
                [write](const QString& t) { write([t](PartPinType& x) { x.size = t; }); });
        hl->addWidget(size);
        auto* gauge = new QLineEdit(types[row].gauge, host);
        // A RANGE IS A REAL ANSWER. A pin takes a range of conductors, not one: a size 20 contact is
        // crimped onto anything from 24 to 20 AWG, and a datasheet states it that way. Written as "20-16"
        // it stays that way on the form, in the description and on the schematic's pin labels.
        gauge->setPlaceholderText(QStringLiteral("22 or 20-16"));
        gauge->setFixedWidth(90);
        gauge->setToolTip(QStringLiteral(
            "The conductor this pin takes, in the unit beside it - one size (\"22\") or a range "
            "(\"20-16\"), as the datasheet states it."));
        connect(gauge, &QLineEdit::textEdited, this,
                [write](const QString& t) { write([t](PartPinType& x) { x.gauge = t; }); });
        hl->addWidget(gauge);
        auto* unit = new QComboBox(host);
        unit->addItem(QStringLiteral("AWG"));
        unit->addItem(QStringLiteral("mm2"));
        unit->setCurrentText(types[row].unit.isEmpty() ? QStringLiteral("AWG") : types[row].unit);
        unit->setToolTip(QStringLiteral("Which of the two the conductor is stated in"));
        unit->setFixedWidth(62);
        connect(unit, &QComboBox::currentTextChanged, this,
                [write](const QString& t) { write([t](PartPinType& x) { x.unit = t; }); });
        hl->addWidget(unit);
        auto* amps = new QLineEdit(types[row].amps, host);
        amps->setPlaceholderText(QStringLiteral("A"));
        amps->setFixedWidth(70);   // as wide as its own heading, which was being cut in half
        amps->setToolTip(QStringLiteral("What it is rated to carry, if the datasheet says"));
        connect(amps, &QLineEdit::textEdited, this,
                [write](const QString& t) { write([t](PartPinType& x) { x.amps = t; }); });
        hl->addWidget(amps);
        auto* pins = new QLineEdit(types[row].pins, host);
        pins->setPlaceholderText(ways > 0 ? QStringLiteral("pins (1-%1)").arg(ways)
                                         : QStringLiteral("pins"));
        pins->setToolTip(QStringLiteral(
            "Which pins are this type - \"1-3\", \"1,2,5\". Left empty it means all of them, which is right "
            "for a connector with one size in it and wrong for one with two."));
        connect(pins, &QLineEdit::textEdited, this,
                [write](const QString& t) { write([t](PartPinType& x) { x.pins = t; }); });
        hl->addWidget(pins, 1);
        auto* drop = makeRemoveButton(host, QString(), QStringLiteral("Take this type of pin off"), true);
        connect(drop, &QPushButton::clicked, this, [this, at] {
            LibraryPart* cp = current();
            if (!cp) return;
            std::vector<PartPinType> now = partPinTypes(*cp);
            if (at < now.size()) now.erase(now.begin() + static_cast<long>(at));
            setPartPinTypes(*cp, now);
            touched();
            rebuildPinTypes();
        });
        hl->addWidget(drop);
        pinTypesLay_->addWidget(host);
    }
    // WHICH PINS ARE WHICH, once there is more than one size in the housing. With one type the pins field
    // may be left empty and mean "all of them"; with two, an empty one is a question nobody has answered,
    // and the schematic would label those pins with nothing.
    if (types.size() > 1) {
        QStringList unsaid;
        for (const PartPinType& t : types)
            if (t.pins.trimmed().isEmpty()) unsaid << (t.size.isEmpty() ? QStringLiteral("(unnamed)")
                                                                        : t.size);
        auto* note = new QLabel(pinTypesHost_);
        note->setWordWrap(true);
        if (unsaid.isEmpty()) {
            const QStringList gauges = partPinGauges(*p);
            int blank = 0;
            for (const QString& g : gauges)
                if (g.isEmpty()) ++blank;
            setRole(note, "caption");
            note->setText(blank == 0
                              ? QStringLiteral("Every pin is accounted for - the schematic labels each one "
                                               "with its own size.")
                              : QStringLiteral("%1 pin(s) belong to no type yet - the schematic leaves "
                                               "those blank.")
                                    .arg(blank));
        } else {
            setRole(note, "warn");
            note->setText(QStringLiteral(
                                "More than one size of pin, so say which pins are which: %1 has none said, "
                                "and \"all of them\" cannot be true of two types at once.")
                              .arg(unsaid.join(QStringLiteral(", "))));
        }
        pinTypesLay_->addWidget(note);
    }
}

void PartsLibraryDialog::placeTicked() {
    LibraryPart* p = current();
    if (!p) {
        if (summary_) summary_->setText(QStringLiteral("Pick a part first"));
        return;
    }
    std::vector<Entity> all;
    QStringList placed, missing;
    double at = 0.0;   // where the next drawing starts, along the sheet
    for (QAction* act : placeWhat_) {
        if (!act || !act->isChecked()) continue;
        const auto kind = static_cast<LibraryPartDrawing>(act->data().toInt());
        std::vector<Entity> one;
        switch (kind) {
        case LibraryPartDrawing::Mechanical:
            one = partBlockInstance(*p, /*mechanical=*/true, Vec2{0.0, 0.0});
            break;
        case LibraryPartDrawing::Schematic:
            one = partBlockInstance(*p, /*mechanical=*/false, Vec2{0.0, 0.0});
            break;
        case LibraryPartDrawing::Fanout:
            if (!p->fanout.isEmpty())
                one = partDrawingBlockInstance(*p, p->fanout, QStringLiteral("fanout"), Vec2{0.0, 0.0});
            break;
        case LibraryPartDrawing::FanoutOpposite: {
            // Its own drawing if it has one, otherwise this one mirrored as a guess at the other side.
            const QJsonArray other = p->fanoutOpposite.isEmpty() ? p->fanout : p->fanoutOpposite;
            if (!other.isEmpty())
                one = partDrawingBlockInstance(*p, other, QStringLiteral("fanout (opposite)"),
                                               Vec2{0.0, 0.0}, p->fanoutOpposite.isEmpty());
            break;
        }
        }
        if (one.empty()) {
            missing << act->text();
            continue;
        }
        Bounds b;
        for (const Entity& e : one) b.expand(e.bounds());
        if (b.valid) {
            // Along from the last one, a gap of a tenth of its width (and at least 2 mm) between them.
            const Vec2 shift{at - b.min.x, -(b.min.y + b.max.y) / 2.0};
            for (Entity& e : one) translateEntity(e, shift);
            at += b.width() + std::max(2.0, b.width() * 0.1);
        }
        for (Entity& e : one) all.push_back(std::move(e));
        placed << act->text();
    }
    if (all.empty()) {
        if (summary_)
            summary_->setText(missing.isEmpty()
                                  ? QStringLiteral("Tick which drawing to place first")
                                  : QStringLiteral("Nothing drawn yet for: %1")
                                        .arg(missing.join(QStringLiteral(", "))));
        return;
    }
    // The contact it is used with travels with it, as it does however the part is placed.
    const QString contactId = p->contactId();
    if (!contactId.isEmpty())
        for (Entity& e : all) e.meta.insert(QLatin1String(kContactPartKey), contactId);
    if (editor_) beginPlacement(std::move(all));
    else hide();
    if (summary_)
        summary_->setText(
            QStringLiteral("Click the sheet to place %1%2")
                .arg(placed.join(QStringLiteral(" + ")))
                .arg(missing.isEmpty() ? QString()
                                       : QStringLiteral("  (nothing drawn for %1)")
                                             .arg(missing.join(QStringLiteral(", ")))));
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
