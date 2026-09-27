// The PARTS panel on the side of the window: the shelf, while drawing.
//
// A shelf that shows some of the stock is worse than no shelf. The panel listed only parts that already had
// a drawing - a mechanical view or a schematic symbol - which in a real library is a handful out of hundreds:
// a heat-shrink size has no drawing, nor has a contact, nor a length of wire, and none of them were there. So
// the shelf showed three parts of a hundred and looked broken rather than filtered.
//
// And a shop connects more than one library: the house library, a customer's, a project's. Which of them the
// panel is drawn from is the first thing to be able to say about it, so it is said with a tick per library.
#include <QLayout>
#include <QAbstractButton>
#include <QGroupBox>
#include <QMouseEvent>
#include <QTableWidget>
#include <QCheckBox>
#include "app/MainWindow.h"
#include <QWidgetAction>
#include "harness/Bom.h"
#include "app/BomDialog.h"
#include <functional>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include "tools/ToolId.h"
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QDialog>
#include "harness/MechModels.h"
#include <QSplitter>
#include "model/Entity.h"
#include <QImage>
#include "app/DrawingPreview.h"
#include "app/PartsLibraryDialog.h"
#include "app/Editor.h"
#include "model/Block.h"
#include "render/CanvasWidget.h"
#include "harness/PartsLibrary.h"

#include <QAction>
#include <QSettings>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTableWidget>
#include <QCheckBox>
#include <QTreeWidget>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QToolButton>

#include <cstdio>

using namespace Edraw_Desktop;

// The side panel's columns. It gained a leading "#" line number - which counts POSITIONS, so a line moved
// is renumbered rather than carrying its old number - and everything after it shifted along by one. Named
// rather than written as digits so the next column added moves one line of code instead of thirty.
constexpr int kNum = 0;
constexpr int kDesc = 1;
constexpr int kQty = 4;

namespace {

int panelFailures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++panelFailures;
    }
}

// A part drawn as two lines, the second of them marked as the line the harness attaches to - the thing the
// eye shows and hides. Written through the same call the application writes a captured drawing with, because
// a hand-built JSON object with the wrong key names in it is a drawing of nothing at all.
QJsonArray twoLinesOneMarked() {
    Entity body;
    body.id = QStringLiteral("b");
    body.type = EntityType::Line;
    body.a = {0.0, 0.0};
    body.b = {20.0, 0.0};
    Entity face;
    face.id = QStringLiteral("f");
    face.type = EntityType::Line;
    face.a = {20.0, -6.0};
    face.b = {20.0, 6.0};
    face.meta.insert(QLatin1String(kConnectionLineKey), true);
    return partDrawingFromEntities({body, face});
}

// A drawing of something: enough of an entity for the part to count as having one.
QJsonArray oneLine() {
    QJsonObject line{{"type", "line"}, {"id", "e1"}, {"x1", 0.0}, {"y1", 0.0},
                     {"x2", 10.0},     {"y2", 0.0},  {"layer", "0"}};
    QJsonArray a;
    a.append(line);
    return a;
}

// A connector with a body, a symbol AND a fanout: the part the mark on the row and the menu have most to say
// about.
QString writeDrawnLibrary(QString& partId) {
    PartsLibrary lib;
    {
        LibraryPart& part = lib.add(PartKind::Connector);
        part.mfrPn = QStringLiteral("PANELTEST-CONN-1");
        part.description = QStringLiteral("housing, 4 way");
        part.mfrName = QStringLiteral("Molex");
        part.mechanical = oneLine();
        part.schematic = oneLine();
        part.fanout = oneLine();
        part.setFanoutSide(QStringLiteral("front"));
        partId = part.id;
    }
    {
        // A connector with a way count and NO fanout drawn: what a real library is mostly made of, and the
        // case the menu has to answer with a fanout rather than with a trip to the library.
        LibraryPart& plain = lib.add(PartKind::Connector);
        plain.mfrPn = QStringLiteral("PANELTEST-CONN-NOFAN");
        plain.description = QStringLiteral("housing, 6 way");
        plain.mfrName = QStringLiteral("Molex");
        plain.mechanical = oneLine();
        plain.params.insert(QStringLiteral("pins"), QStringLiteral("6"));
    }
    const QString path = QDir::tempPath() + QStringLiteral("/edraw-panel-drawn.plb");
    QFile::remove(path);
    lib.save(path);
    return path;
}

// A library file of its own, with parts that have no drawing at all - which is what most of a library is.
QString writeLibrary(const QString& name, const QStringList& partNames) {
    PartsLibrary lib;
    for (const QString& pn : partNames) {
        LibraryPart& part = lib.add(PartKind::Heatshrink);
        part.mfrPn = pn;
        part.description = QStringLiteral("sleeve, %1").arg(pn);
        part.mfrName = QStringLiteral("Raychem");
    }
    const QString path = QDir::tempPath() + QStringLiteral("/edraw-panel-%1.plb").arg(name);
    QFile::remove(path);
    lib.save(path);
    return path;
}

QTreeWidget* partsList(MainWindow& win) {
    for (QDockWidget* d : win.findChildren<QDockWidget*>())
        if (d->objectName() == QStringLiteral("panel_PARTS")) return d->findChild<QTreeWidget*>();
    return nullptr;
}

// Every row of the shelf, headings and parts alike, however deep it goes. It used to be one level - a
// heading per library - and is now two, because the parts are grouped by KIND under it: a library of a
// hundred and twenty rows of which two are connectors is a shelf nobody can use flat.
void collectRows(QTreeWidgetItem* it, QList<QTreeWidgetItem*>& out) {
    out << it;
    for (int i = 0; i < it->childCount(); ++i) collectRows(it->child(i), out);
}

QList<QTreeWidgetItem*> everyRow(QTreeWidget* tree) {
    QList<QTreeWidgetItem*> out;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) collectRows(tree->topLevelItem(i), out);
    return out;
}

// The rows that stand for a part (the library headings carry no part id).
QStringList partRows(QTreeWidget* tree) {
    QStringList out;
    for (QTreeWidgetItem* it : everyRow(tree))
        if (!it->data(0, Qt::UserRole).toString().isEmpty()) out << it->text(0);
    return out;
}

QTreeWidgetItem* rowFor(QTreeWidget* tree, const QString& partId) {
    for (QTreeWidgetItem* it : everyRow(tree))
        if (it->data(0, Qt::UserRole).toString() == partId) return it;
    return nullptr;
}

QTreeWidgetItem* rowNamed(QTreeWidget* tree, const QString& text) {
    for (QTreeWidgetItem* it : everyRow(tree))
        if (it->text(0).contains(text)) return it;
    return nullptr;
}

} // namespace

int run_partspanel_tests() {
    std::printf("parts panel tests:\n");

    const QString houseFile = writeLibrary(QStringLiteral("house"),
                                           {QStringLiteral("PANELTEST-HOUSE-1"), QStringLiteral("PANELTEST-HOUSE-2"),
                                            QStringLiteral("PANELTEST-HOUSE-3")});
    const QString jobFile =
        writeLibrary(QStringLiteral("job"), {QStringLiteral("PANELTEST-JOB-1"), QStringLiteral("PANELTEST-JOB-2")});

    QString drawnId;
    const QString drawnFile = writeDrawnLibrary(drawnId);

    // Connected, and remembered - which is what the panel reads when it builds itself.
    {
        PartsCatalog cat;
        cat.connectLibrary(houseFile);
        cat.connectLibrary(jobFile);
        cat.connectLibrary(drawnFile);
        cat.remember();
        check(cat.paths().size() >= 3, "libraries: the libraries are connected");
    }

    MainWindow win;
    QTreeWidget* list = partsList(win);
    check(list != nullptr, "panel: the parts panel is a table of parts");
    if (!list) {
        std::printf("  -> %d failed\n", ++panelFailures);
        return panelFailures;
    }

    QStringList rows = partRows(list);
    std::printf("  panel: %lld parts listed from two libraries\n", static_cast<long long>(rows.size()));
    check(rows.size() >= 5,
          "panel: every part of every library in view is listed - a part with no drawing is still a part");
    bool sleeveThere = false;
    for (const QString& r : rows)
        if (r.contains(QStringLiteral("PANELTEST-HOUSE-1"))) sleeveThere = true;
    check(sleeveThere, "panel: a heat-shrink size, which has no drawing at all, is on the shelf");

    // A SECTION DRAGGED BY ITS HEADING goes where it was let go: the last one put before the first.
    {
        auto kindsShown = [&] {
            QList<int> out;
            for (QTreeWidgetItem* it : everyRow(list)) {
                const QVariant k = it->data(0, Qt::UserRole + 4);
                if (k.isValid() && !out.contains(k.toInt())) out << k.toInt();
            }
            return out;
        };
        const QList<int> before = kindsShown();
        if (before.size() >= 2) {
            win.movePartsPanelKind(before.last(), before.first(), false);
            const QList<int> after = kindsShown();
            std::printf("  panel: sections %lld, first now %d (was %d)\n",
                        static_cast<long long>(after.size()), after.value(0), before.first());
            check(after.value(0) == before.last() && after.value(1) == before.first(),
                  "panel: a section dropped above the first is first, and the rest follow in order");
            win.movePartsPanelKind(before.last(), before.value(before.size() - 2), true);
            check(kindsShown() == before, "panel: and dropped below where it was, it is back");
        } else {
            check(false, "panel: the shelf has at least two sections to reorder");
        }
        QSettings().remove(QStringLiteral("ui/partsPanelKindOrder"));
    }

    // A HEADING ROLLED UP STAYS ROLLED UP: remembered, and the shelf comes back that way - the next
    // session builds it the same way this refresh does.
    {
        QTreeWidgetItem* head = nullptr;
        for (QTreeWidgetItem* it : everyRow(list))
            if (it->data(0, Qt::UserRole + 4).isValid()) { head = it; break; }
        if (head) {
            const QString key = MainWindow::partsPanelHeadingKey(head);
            head->setExpanded(false);   // the user rolls it up
            check(win.partsPanelHeadingCollapsed(key), "panel: a heading rolled up is remembered");
            // Rebuilt: a heading "moved" to where it already is redraws the shelf and changes nothing else.
            const int k = head->data(0, Qt::UserRole + 4).toInt();
            int next = -1;
            bool seen = false;
            for (QTreeWidgetItem* it : everyRow(list)) {
                const QVariant v = it->data(0, Qt::UserRole + 4);
                if (!v.isValid()) continue;
                if (seen && v.toInt() != k) { next = v.toInt(); break; }
                if (v.toInt() == k) seen = true;
            }
            win.movePartsPanelKind(k, next, false);
            QTreeWidgetItem* again = nullptr;
            for (QTreeWidgetItem* it : everyRow(list))
                if (MainWindow::partsPanelHeadingKey(it) == key) again = it;
            check(again && !again->isExpanded(), "panel: and the shelf is rebuilt with it rolled up");
            if (again) again->setExpanded(true);   // opened again
            check(!win.partsPanelHeadingCollapsed(key), "panel: opened again, it is remembered open");
        } else {
            check(false, "panel: there is a heading to roll up");
        }
        QSettings().remove(QStringLiteral("ui/partsPanelCollapsed"));
    }

    // A part with nothing to place says so, rather than being left out: the flag the double-click reads.
    bool anyWithoutADrawing = false;
    for (QTreeWidgetItem* it : everyRow(list)) {
        if (it->data(0, Qt::UserRole).toString().isEmpty()) continue;
        if (!it->data(0, Qt::UserRole + 2).toBool()) anyWithoutADrawing = true;
    }
    check(anyWithoutADrawing,
          "panel: and it is marked as having no drawing, so a double-click opens it in the library");

    // Which libraries are in view: a tick each, and taking one off takes its parts off the shelf.
    QToolButton* libsBtn = nullptr;
    for (QDockWidget* d : win.findChildren<QDockWidget*>())
        if (d->objectName() == QStringLiteral("panel_PARTS"))
            for (QToolButton* b : d->findChildren<QToolButton*>())
                if (b->text() == QStringLiteral("Libraries")) libsBtn = b;
    check(libsBtn && libsBtn->menu(), "libraries: the panel says which libraries it draws from");
    if (libsBtn && libsBtn->menu()) {
        QMenu* menu = libsBtn->menu();
        menu->popup(QPoint(0, 0));   // filled when it is opened, so a library connected since is on it
        QApplication::processEvents();
        menu->close();
        QAction* jobAction = nullptr;
        int ticked = 0;
        for (QAction* a : menu->actions()) {
            if (!a->isCheckable()) continue;
            if (a->isChecked()) ++ticked;
            if (a->text().contains(QStringLiteral("job"))) jobAction = a;
        }
        std::printf("  libraries: %d in view\n", ticked);
        check(ticked >= 2, "libraries: each connected library has a tick, and starts in view");
        check(jobAction != nullptr, "libraries: the job library is one of them");
        if (jobAction) {
            const int before = partRows(list).size();
            jobAction->setChecked(false);   // take it out of view
            QApplication::processEvents();
            const QStringList after = partRows(list);
            check(after.size() < before, "libraries: taking one off view takes its parts off the shelf");
            bool jobPartStillThere = false;
            for (const QString& r : after)
                if (r.contains(QStringLiteral("PANELTEST-JOB-1"))) jobPartStillThere = true;
            check(!jobPartStillThere, "libraries: and it is that library's parts that went");
            jobAction->setChecked(true);
            QApplication::processEvents();
            check(partRows(list).size() == before, "libraries: putting it back brings them back");
        }
    }

    // ---- what a part HAS, in columns of its own ---------------------------------------------------
    {
        // A part is chosen off the shelf by what can be done with it, and that took opening it. There is a
        // column each now - M, S, F - ticked for the drawings the part has, so a shelf is read down its
        // columns rather than part by part.
        check(list->columnCount() == 4, "columns: the part, and one column each for M, S and F");
        check(list->headerItem()->text(1) == QStringLiteral("M") &&
                  list->headerItem()->text(2) == QStringLiteral("S") &&
                  list->headerItem()->text(3) == QStringLiteral("F"),
              "columns: headed M, S and F");
        check(list->headerItem()->toolTip(1).contains(QStringLiteral("Mechanical")) &&
                  list->headerItem()->toolTip(3).contains(QStringLiteral("Fanout")),
              "columns: and each says in words what its letter means");

        QTreeWidgetItem* drawn = rowFor(list, drawnId);
        QTreeWidgetItem* bare = rowNamed(list, QStringLiteral("PANELTEST-HOUSE-1"));
        check(drawn != nullptr, "columns: the connector with all three drawings is on the shelf");
        if (drawn) {
            const QString tick(QChar(0x2713));
            std::printf("  columns: %s -> [%s][%s][%s]\n", qPrintable(drawn->text(0)),
                        qPrintable(drawn->text(1)), qPrintable(drawn->text(2)),
                        qPrintable(drawn->text(3)));
            check(drawn->text(1) == tick && drawn->text(2) == tick && drawn->text(3) == tick,
                  "columns: all three ticked for a part that has all three");
            check(drawn->toolTip(3).contains(QStringLiteral("fanout")),
                  "columns: and the cell says what it is about");
            check(drawn->data(0, Qt::UserRole + 3).toBool(), "columns: the fanout is flagged on the row");
        }
        if (bare) {
            check(bare->text(1).isEmpty() && bare->text(2).isEmpty() && bare->text(3).isEmpty(),
                  "columns: a part with no drawing has three empty cells - a gap the eye stops at");
            check(!bare->data(0, Qt::UserRole + 2).toBool(),
                  "columns: and is flagged as having nothing to place");
        }
    }

    // ---- the menu on a part ------------------------------------------------------------------------
    {
        // Right-click offers what CAN be done and nothing else: a part with no fanout is not offered one.
        QMenu* menu = win.buildPartsPanelMenu(drawnId);
        check(menu != nullptr, "menu: a part has a menu");
        if (menu) {
            QStringList items;
            for (QAction* a : menu->actions())
                if (!a->isSeparator()) items << a->text();
            std::printf("  menu: %s\n", qPrintable(items.join(QStringLiteral(" | "))));
            bool place = false, addBom = false, edit = false, front = false, back = false;
            for (const QString& s : items) {
                if (s.contains(QStringLiteral("mechanical"))) place = true;
                if (s.contains(QStringLiteral("bill of materials"))) addBom = true;
                if (s.contains(QStringLiteral("Edit in the library"))) edit = true;
                // BY SIDE, not by transform. Each side of a connector has its own drawing now, so the two
                // entries name what each one SHOWS - "mirrored" said how the second was made, which is not
                // what somebody placing one is choosing between.
                if (s.contains(QStringLiteral("fanout")) && s.contains(QStringLiteral("front")))
                    front = true;
                if (s.contains(QStringLiteral("fanout")) && s.contains(QStringLiteral("back")))
                    back = true;
            }
            check(place, "menu: place the drawing");
            check(addBom, "menu: add it to the bill of materials");
            check(edit, "menu: edit it in the library");
            check(front && back,
                  "menu: and the fanout for either side of the connector, each named by the side it shows");
            for (const QString& s : items)
                check(!s.contains(QStringLiteral("mirrored")),
                      "menu: nothing on it is named for what was done to it");
            delete menu;
        }
        // A part with nothing to place says so, rather than offering a command that cannot work.
        QString bareId;
        if (QTreeWidgetItem* bareRow = rowNamed(list, QStringLiteral("PANELTEST-HOUSE-1")))
            bareId = bareRow->data(0, Qt::UserRole).toString();
        if (QMenu* bareMenu = win.buildPartsPanelMenu(bareId)) {
            bool saysSo = false, canStillList = false;
            for (QAction* a : bareMenu->actions()) {
                if (a->text().contains(QStringLiteral("No drawing")) && !a->isEnabled()) saysSo = true;
                if (a->text().contains(QStringLiteral("bill of materials"))) canStillList = true;
            }
            check(saysSo, "menu: a part with no drawing says so, greyed, rather than offering to place it");
            check(canStillList, "menu: and can still be added to the bill of materials");
            delete bareMenu;
        }
    }

    // ---- a part onto the bill of materials, and off it again ---------------------------------------
    {
        QTableWidget* bom = nullptr;
        for (QDockWidget* d : win.findChildren<QDockWidget*>())
            if (d->objectName() == QStringLiteral("panel_BILL OF MATERIALS"))
                bom = d->findChild<QTableWidget*>();
        check(bom != nullptr, "bom: the bill of materials is a panel of its own");
        if (bom) {
            const int before = bom->rowCount();
            win.addPartToBom(drawnId);
            QApplication::processEvents();
            check(bom->rowCount() == before + 1, "bom: a part added from the library is a line on it");
            int row = -1;
            for (int i = 0; i < bom->rowCount(); ++i)
                if (bom->item(i, kDesc) && bom->item(i, kDesc)->text().contains(QStringLiteral("housing"))) row = i;
            check(row >= 0, "bom: and the line is the part that was added");

            // The line NUMBER down the left counts POSITIONS: 1, 2, 3 down the panel whatever order the
            // lines are in. That is the whole point of the column - a drawing's balloons read against a
            // settled order, and a list whose numbering shuffles when it is re-sorted cannot be read
            // against anything. So it never carries a line's old number along with it.
            {
                bool inOrder = true;
                for (int i = 0; i < bom->rowCount(); ++i) {
                    const QTableWidgetItem* n = bom->item(i, kNum);
                    if (!n || n->text() != QString::number(i + 1)) inOrder = false;
                }
                std::printf("  bom: %d line(s), numbered 1..%d down the panel\n", bom->rowCount(),
                            bom->rowCount());
                check(bom->rowCount() > 0 && inOrder,
                      "bom: the # column numbers the positions, 1 downwards, in constant order");
                // And it says nothing about the part - the description cell is what carries that, so a
                // number is only ever a place in the list.
                const QTableWidgetItem* n0 = bom->item(0, kNum);
                check(n0 && n0->data(Qt::UserRole + 2).toString().isEmpty(),
                      "bom: a number is a position and holds nothing about the part in it");
            }
            if (row >= 0) {
                check(bom->item(row, kDesc)->data(Qt::UserRole + 1).toBool(),
                      "bom: marked as put there by hand, which is what makes it removable");
                win.removeBomPanelRow(row);
                QApplication::processEvents();
                check(bom->rowCount() == before, "bom: and it can be taken off again");
                bool stillThere = false;
                for (int i = 0; i < bom->rowCount(); ++i)
                    if (bom->item(i, kDesc) && bom->item(i, kDesc)->text().contains(QStringLiteral("housing")))
                        stillThere = true;
                check(!stillThere, "bom: it is the line that was asked for that went");
            }
            // Added twice and taken off once: the panel shows the lines TOTALLED, so one line on screen can
            // be two rows in the drawing, and both have to go - or the first removal looks as though it did
            // nothing at all.
            win.addPartToBom(drawnId);
            win.addPartToBom(drawnId);
            QApplication::processEvents();
            int twice = -1;
            for (int i = 0; i < bom->rowCount(); ++i)
                if (bom->item(i, kDesc) && bom->item(i, kDesc)->text().contains(QStringLiteral("housing")))
                    twice = i;
            check(twice >= 0, "bom: a part added twice is one line, totalled");
            if (twice >= 0) {
                win.removeBomPanelRow(twice);
                QApplication::processEvents();
                bool leftOver = false;
                for (int i = 0; i < bom->rowCount(); ++i)
                    if (bom->item(i, kDesc) && bom->item(i, kDesc)->text().contains(QStringLiteral("housing")))
                        leftOver = true;
                check(!leftOver, "bom: and taking that line off takes both of the rows behind it");
            }
        }
    }

    // ---- placing a part, and counting it ----------------------------------------------------------
    {
        // Two rules a shop sets once and then forgets: a part put on the sheet is counted, and two of the
        // same part number are one line of two rather than two lines of one. Both are the DRAWING'S rules -
        // they are kept in its bill of materials - and both are within reach of the parts panel now, which
        // is where parts are put down.
        QCheckBox* onPlace = nullptr;
        QCheckBox* unify = nullptr;
        for (QDockWidget* d : win.findChildren<QDockWidget*>())
            if (d->objectName() == QStringLiteral("panel_PARTS"))
                for (QCheckBox* b : d->findChildren<QCheckBox*>()) {
                    if (b->text().contains(QStringLiteral("bill of materials"))) onPlace = b;
                    if (b->text().contains(QStringLiteral("one line"))) unify = b;
                }
        check(onPlace != nullptr, "rules: the panel carries the place-and-count switch");
        check(unify != nullptr, "rules: and the one that adds up two of the same part");
        if (!onPlace || !unify) return panelFailures;

        QTableWidget* bom = nullptr;
        for (QDockWidget* d : win.findChildren<QDockWidget*>())
            if (d->objectName() == QStringLiteral("panel_BILL OF MATERIALS"))
                bom = d->findChild<QTableWidget*>();
        check(bom != nullptr, "rules: the bill of materials is on screen to be counted on");
        if (!bom) return panelFailures;

        Editor* editor = win.findChild<Editor*>();
        check(editor != nullptr, "rules: there is an editor to place with");
        PartsCatalog cat;
        cat.openRemembered();
        const LibraryPart* part = cat.byId(drawnId);
        check(part != nullptr, "rules: and the part to place");
        if (!editor || !part) return panelFailures;

        // Put the part on the sheet the way placing it does - the same block, with the part stamped on
        // every entity of it - but committed rather than left following the cursor.
        auto putOneDown = [&] {
            editor->appendEntities(partBlockInstance(*part, true, Vec2{0.0, 0.0}));
            QApplication::processEvents();   // the panels follow the drawing on their own
        };

        onPlace->setChecked(true);
        unify->setChecked(true);
        QApplication::processEvents();
        const int before = bom->rowCount();
        putOneDown();
        int line = -1;
        for (int i = 0; i < bom->rowCount(); ++i)
            if (bom->item(i, kDesc) && bom->item(i, kDesc)->text().contains(QStringLiteral("housing"))) line = i;
        std::printf("  rules: placed one - %d lines, the part on line %d\n", bom->rowCount(), line);
        check(bom->rowCount() == before + 1 && line >= 0,
              "rules: a part put on the sheet is on the bill of materials, without being asked twice");
        const QString qtyOne = line >= 0 && bom->item(line, kQty) ? bom->item(line, kQty)->text() : QString();

        // The same part again: one line, counted twice - not a second line.
        putOneDown();
        int lines = 0, only = -1;
        for (int i = 0; i < bom->rowCount(); ++i)
            if (bom->item(i, kDesc) && bom->item(i, kDesc)->text().contains(QStringLiteral("housing"))) {
                ++lines;
                only = i;
            }
        const QString qtyTwo = only >= 0 && bom->item(only, kQty) ? bom->item(only, kQty)->text() : QString();
        std::printf("  rules: placed a second - %d line(s), quantity %s -> %s\n", lines,
                    qPrintable(qtyOne), qPrintable(qtyTwo));
        check(lines == 1, "rules: the same part number twice is ONE line");
        check(qtyTwo != qtyOne, "rules: and its quantity went up by one");

        // Switched off, each is a line of its own again.
        unify->setChecked(false);
        QApplication::processEvents();
        int apart = 0;
        for (int i = 0; i < bom->rowCount(); ++i)
            if (bom->item(i, kDesc) && bom->item(i, kDesc)->text().contains(QStringLiteral("housing"))) ++apart;
        std::printf("  rules: not adding up - %d lines\n", apart);
        check(apart == 2, "rules: switched off, two of the same part are two lines");

        // And with placing switched off, the sheet is not counted at all.
        onPlace->setChecked(false);
        QApplication::processEvents();
        int counted = 0;
        for (int i = 0; i < bom->rowCount(); ++i)
            if (bom->item(i, kDesc) && bom->item(i, kDesc)->text().contains(QStringLiteral("housing"))) ++counted;
        check(counted == 0,
              "rules: with the switch off the bill lists only what was added to it by hand");
        // Left as they were found, so the rest of the run sees the ordinary rules.
        onPlace->setChecked(true);
        unify->setChecked(true);
        QApplication::processEvents();
    }

    // ---- double-click a COLUMN and that is the drawing you get --------------------------------------
    {
        // The columns are how the row says what the part has, so they are also how it is asked for: a part
        // with three drawings and one gesture had no way of saying which one was wanted.
        Editor* editor = win.findChild<Editor*>();
        check(editor != nullptr, "columns: there is an editor to place into");
        if (editor) {
            editor->selectAll();
            editor->deleteSelected();
            QTreeWidgetItem* row = rowFor(list, drawnId);
            check(row != nullptr, "columns: the connector is on the shelf");
            if (row) {
                // The S column: the schematic symbol, not the mechanical drawing.
                emit list->itemDoubleClicked(row, 2);
                editor->onPointerPress(Vec2{0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
                QApplication::processEvents();
                QString view;
                for (const Entity& e : editor->document()->entities())
                    if (e.meta.value(QStringLiteral("libPart")).toString() == drawnId)
                        view = e.meta.value(QStringLiteral("libPartView")).toString();
                std::printf("  columns: double-clicked S, placed the %s drawing\n", qPrintable(view));
                check(view == QStringLiteral("schematic"),
                      "columns: double-clicking under S places the schematic symbol");

                editor->selectAll();
                editor->deleteSelected();
                emit list->itemDoubleClicked(row, 1);
                editor->onPointerPress(Vec2{0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
                QApplication::processEvents();
                view.clear();
                for (const Entity& e : editor->document()->entities())
                    if (e.meta.value(QStringLiteral("libPart")).toString() == drawnId)
                        view = e.meta.value(QStringLiteral("libPartView")).toString();
                check(view == QStringLiteral("mechanical"), "columns: and under M, the mechanical drawing");

                editor->selectAll();
                editor->deleteSelected();
                emit list->itemDoubleClicked(row, 3);
                editor->onPointerPress(Vec2{0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
                QApplication::processEvents();
                view.clear();
                for (const Entity& e : editor->document()->entities())
                    if (e.meta.value(QStringLiteral("libPart")).toString() == drawnId)
                        view = e.meta.value(QStringLiteral("libPartView")).toString();
                check(view == QStringLiteral("fanout"), "columns: and under F, the fanout");
                editor->selectAll();
                editor->deleteSelected();
                QApplication::processEvents();
            }
        }
    }

    // ---- a fanout can be PLACED whether one was drawn for the part or not --------------------------
    {
        // A connector's fanout is exactly the thing its own data can say - one wire a way, out of the back of
        // the housing and into the bundle - so a part with none drawn gets one worked out and placed. Being
        // sent to the library to draw one is not placing a fanout.
        QString noFanId;
        if (QTreeWidgetItem* row = rowNamed(list, QStringLiteral("PANELTEST-CONN-NOFAN")))
            noFanId = row->data(0, Qt::UserRole).toString();
        check(!noFanId.isEmpty(), "fanout: the connector with no fanout drawn is on the shelf");
        if (QMenu* menu = win.buildPartsPanelMenu(noFanId)) {
            QStringList items;
            bool canPlace = false, mirrored = false, sentToLibrary = false;
            for (QAction* a : menu->actions()) {
                if (a->isSeparator()) continue;
                items << a->text();
                if (a->text().contains(QStringLiteral("fanout")) && a->isEnabled() &&
                    !a->text().contains(QStringLiteral("mirrored")))
                    canPlace = true;
                if (a->text().contains(QStringLiteral("mirrored")) && a->isEnabled()) mirrored = true;
                if (a->text().contains(QStringLiteral("Draw the fanout"))) sentToLibrary = true;
            }
            std::printf("  fanout: %s\n", qPrintable(items.join(QStringLiteral(" | "))));
            check(canPlace && mirrored,
                  "fanout: the menu places one for a connector that has none drawn, either way round");
            check(!sentToLibrary, "fanout: and does not send anybody to the library to draw it first");
            delete menu;
        }

        // A heat-shrink sleeve has no fanout and never will: it is offered none. wayCount() answers 1 for
        // everything, so the question has to be asked of the part rather than of the number.
        QString sleeveId;
        if (QTreeWidgetItem* row = rowNamed(list, QStringLiteral("PANELTEST-HOUSE-2")))
            sleeveId = row->data(0, Qt::UserRole).toString();
        if (QMenu* menu = win.buildPartsPanelMenu(sleeveId)) {
            bool anyFanout = false;
            for (QAction* a : menu->actions())
                if (a->text().contains(QStringLiteral("fanout"))) anyFanout = true;
            check(!anyFanout, "fanout: a sleeve is not offered one - a sleeve has no fanout");
            delete menu;
        }

        // And placing it puts down a wire a way: what the generated fanout IS.
        Editor* editor = win.findChild<Editor*>();
        if (editor && !noFanId.isEmpty()) {
            editor->selectAll();
            editor->deleteSelected();
            win.placeLibraryFanout(noFanId, false);
            editor->onPointerPress(Vec2{0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            QApplication::processEvents();
            int wires = 0, bodies = 0;
            for (const Entity& e : editor->document()->entities()) {
                if (e.meta.value(QStringLiteral("libPartView")).toString() != QStringLiteral("fanout"))
                    continue;
                if (e.type == EntityType::Polyline) ++wires;
                if (e.type == EntityType::Rect) ++bodies;
            }
            std::printf("  fanout: placed - %d wire(s) and %d housing(s)\n", wires, bodies);
            check(wires == 6 && bodies == 1,
                  "fanout: six ways, six wires out of the back of one housing");
            editor->selectAll();
            editor->deleteSelected();
            QApplication::processEvents();
        }
    }

    // ---- the same part drawn two ways is one part to buy --------------------------------------------
    {
        // A connector is drawn twice: its body on the assembly and its symbol on the schematic. It is one
        // connector to buy. Counting the views separately and adding them up said two, which is a bill
        // nobody could order from - so the part is counted as the BIGGEST of its views, and a schematic that
        // shows more than the assembly is the count, because then the assembly sheet is short.
        Editor* editor = win.findChild<Editor*>();
        QTableWidget* bom = nullptr;
        for (QDockWidget* d : win.findChildren<QDockWidget*>())
            if (d->objectName() == QStringLiteral("panel_BILL OF MATERIALS"))
                bom = d->findChild<QTableWidget*>();
        PartsCatalog cat;
        cat.openRemembered();
        const LibraryPart* part = cat.byId(drawnId);
        check(editor && bom && part, "views: the sheet, the bill and the part are all here");
        if (editor && bom && part) {
            auto qty = [bom]() -> QString {
                for (int i = 0; i < bom->rowCount(); ++i)
                    if (bom->item(i, kDesc) && bom->item(i, kDesc)->text().contains(QStringLiteral("housing")))
                        return bom->item(i, kQty) ? bom->item(i, kQty)->text() : QString();
                return QStringLiteral("0");
            };
            editor->selectAll();
            editor->deleteSelected();
            QApplication::processEvents();

            editor->appendEntities(partBlockInstance(*part, true, Vec2{0.0, 0.0}));
            QApplication::processEvents();
            const QString one = qty();
            editor->appendEntities(partBlockInstance(*part, false, Vec2{100.0, 0.0}));
            QApplication::processEvents();
            const QString both = qty();
            std::printf("  views: mechanical %s, plus its schematic %s\n", qPrintable(one),
                        qPrintable(both));
            check(one == QStringLiteral("1") && both == QStringLiteral("1"),
                  "views: the body and the symbol of one connector are ONE connector to buy");

            // Two more symbols: the schematic now shows three where the assembly shows one, and three is
            // the truth - the assembly sheet is two short.
            editor->appendEntities(partBlockInstance(*part, false, Vec2{200.0, 0.0}));
            editor->appendEntities(partBlockInstance(*part, false, Vec2{300.0, 0.0}));
            QApplication::processEvents();
            std::printf("  views: three on the schematic, one on the assembly -> %s\n",
                        qPrintable(qty()));
            check(qty() == QStringLiteral("3"),
                  "views: and when one view shows more than the other, the bigger one is the count");
            editor->selectAll();
            editor->deleteSelected();
            QApplication::processEvents();
        }
    }

    // ---- a COPY of a placed part is another one of it ----------------------------------------------
    {
        // Copy a connector on the sheet and there are two connectors to buy. The copy kept the instance key
        // of the one it came from - every paste went through a path that never re-minted the copies'
        // references - so anything counting instances counted the two as one, and the bill of materials
        // showed one connector where the drawing had two.
        QTableWidget* bom = nullptr;
        for (QDockWidget* d : win.findChildren<QDockWidget*>())
            if (d->objectName() == QStringLiteral("panel_BILL OF MATERIALS"))
                bom = d->findChild<QTableWidget*>();
        Editor* editor = win.findChild<Editor*>();
        CanvasWidget* canvas = win.findChild<CanvasWidget*>();
        check(bom && editor && canvas, "copy: the sheet, the editor and the bill of materials are all here");
        if (!bom || !editor || !canvas) return panelFailures;

        PartsCatalog cat;
        cat.openRemembered();
        const LibraryPart* part = cat.byId(drawnId);
        check(part != nullptr, "copy: and the part to place");
        if (!part) return panelFailures;

        auto qtyOfTheConnector = [bom]() -> QString {
            for (int i = 0; i < bom->rowCount(); ++i)
                if (bom->item(i, kDesc) && bom->item(i, kDesc)->text().contains(QStringLiteral("housing")))
                    return bom->item(i, kQty) ? bom->item(i, kQty)->text() : QString();
            return QString();
        };
        auto linesOfTheConnector = [bom] {
            int n = 0;
            for (int i = 0; i < bom->rowCount(); ++i)
                if (bom->item(i, kDesc) && bom->item(i, kDesc)->text().contains(QStringLiteral("housing"))) ++n;
            return n;
        };

        // An empty sheet to count on: the checks above left parts on it, and a count is only a count when
        // it is known what is being counted.
        editor->selectAll();
        editor->deleteSelected();
        QApplication::processEvents();
        check(linesOfTheConnector() == 0, "copy: the sheet starts empty");

        // One on the sheet.
        editor->appendEntities(partBlockInstance(*part, true, Vec2{0.0, 0.0}));
        QApplication::processEvents();
        check(linesOfTheConnector() == 1 && qtyOfTheConnector() == QStringLiteral("1"),
              "copy: one placed connector is one line of one");

        // Copied and pasted, the way the keys do it: copy the selection, paste, then click to put it down.
        editor->selectAll();
        editor->copySelection();
        editor->paste();
        editor->onPointerPress(Vec2{200.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        QApplication::processEvents();
        std::printf("  copy: after pasting - %d line(s), quantity %s\n", linesOfTheConnector(),
                    qPrintable(qtyOfTheConnector()));
        check(linesOfTheConnector() == 1 && qtyOfTheConnector() == QStringLiteral("2"),
              "copy: the copy is another one of the part - one line, counted twice");

        // Every entity of the copy is its own instance, and its own entity: a copy that shares an id or an
        // instance key with the original is the whole cause of the miscount.
        {
            QSet<QString> ids, instances;
            int stamped = 0;
            for (const Entity& e : editor->document()->entities()) {
                if (e.meta.value(QStringLiteral("libPart")).toString() != part->id) continue;
                ++stamped;
                ids.insert(e.id);
                instances.insert(e.meta.value(QStringLiteral("blockInst")).toString());
            }
            check(ids.size() == stamped, "copy: no two lines of the two copies share an id");
            check(instances.size() == 2, "copy: and the two copies are two instances, not one");
        }

        // Delete one of them: back to one. The line is the drawing's own, so taking the thing off the
        // drawing is what takes it off the bill - there is nothing else to do and nothing else to undo.
        editor->clearSelection();
        {
            const QString oneInstance =
                [&]() -> QString {
                    for (const Entity& e : editor->document()->entities())
                        if (e.meta.value(QStringLiteral("libPart")).toString() == part->id)
                            return e.meta.value(QStringLiteral("blockInst")).toString();
                    return QString();
                }();
            bool first = true;
            QStringList toGo;
            for (const Entity& e : editor->document()->entities())
                if (e.meta.value(QStringLiteral("blockInst")).toString() == oneInstance) toGo << e.id;
            for (const QString& id : toGo) {
                editor->selectById(id, !first);
                first = false;
            }
            editor->deleteSelected();
            QApplication::processEvents();
        }
        std::printf("  copy: one deleted - %d line(s), quantity %s\n", linesOfTheConnector(),
                    qPrintable(qtyOfTheConnector()));
        check(linesOfTheConnector() == 1 && qtyOfTheConnector() == QStringLiteral("1"),
              "copy: deleting one of the two takes one off the bill of materials");

        // And the last of them takes the line with it.
        editor->selectAll();
        editor->deleteSelected();
        QApplication::processEvents();
        check(linesOfTheConnector() == 0,
              "copy: deleting the last one takes the line off altogether");
    }

    // The count under the list says what is in view out of what is connected, so a short list is never a
    // mystery: it is either the filter or a library that is not in view, and both are said.
    QLabel* count = nullptr;
    for (QDockWidget* d : win.findChildren<QDockWidget*>())
        if (d->objectName() == QStringLiteral("panel_PARTS"))
            for (QLabel* l : d->findChildren<QLabel*>())
                if (l->text().contains(QStringLiteral("parts"))) count = l;
    check(count != nullptr, "panel: it says how many parts are in view, of how many connected");
    if (count) std::printf("  panel: \"%s\"\n", qPrintable(count->text()));

    // Put the remembered set back as it was found: a test that leaves the program pointed at files it made
    // up is a test that breaks the next run.
    {
        PartsCatalog cat;
        cat.openRemembered();
        cat.disconnectLibrary(houseFile);
        cat.disconnectLibrary(jobFile);
        cat.disconnectLibrary(drawnFile);
        cat.remember();
    }
    QFile::remove(houseFile);
    QFile::remove(jobFile);
    QFile::remove(drawnFile);

    // ---- the library's eye: the connection point is shown and hidden on all three drawings ----------
    {
        // The connection point is drawn ORANGE over a part - the line the harness attaches to. While the
        // drawing itself is being checked, an orange line reads as part of the part, so it is in the way of
        // seeing what the part actually looks like. One eye over the three panes, because it is one question
        // about one part shown three ways.
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_eye_lib.json"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            LibraryPart part;
            part.id = QStringLiteral("P-EYE");
            part.kind = PartKind::Connector;
            part.description = QStringLiteral("Two-line connector");
            part.mechanical = twoLinesOneMarked();
            lib.parts.push_back(part);
            lib.save(libFile);
        }

        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        PartsLibraryDialog dlg(&ed);
        dlg.setLibraryPath(libFile);
        dlg.selectPart(QStringLiteral("P-EYE"));
        dlg.show();
        QApplication::processEvents();

        // EVERY CAPTION READS, however small the window is dragged: each button with words on it is held
        // at the width its words need, and the window cannot be made narrower than that. (The test screen
        // is smaller than the window, so the window is given what a big enough screen would give it.)
        {
            dlg.setMinimumSize(dlg.layout()->minimumSize());
            dlg.resize(300, 200);
            QApplication::processEvents();
            QStringList cut;
            for (QAbstractButton* b : dlg.findChildren<QAbstractButton*>())
                if (b->isVisibleTo(&dlg) && !b->text().isEmpty() && b->width() + 1 < b->sizeHint().width())
                    cut << b->text();
            if (!cut.isEmpty()) std::printf("  library: cut captions: %s\n", qPrintable(cut.join(QStringLiteral(", "))));
            check(cut.isEmpty(), "library: no button is narrower than its caption, even at the smallest size");
        }

        QToolButton* eye = nullptr;
        for (QToolButton* b : dlg.findChildren<QToolButton*>())
            if (b->isCheckable() && b->text().contains(QStringLiteral("Connection points"))) eye = b;
        check(eye != nullptr, "library: there is a toggle for the connection points");
        const QList<DrawingPreview*> panes = dlg.findChildren<DrawingPreview*>();
        std::printf("  library: the eye covers %lld drawing pane(s)\n", static_cast<long long>(panes.size()));
        check(panes.size() >= 3, "library: and it covers all three drawings of the part");
        if (eye) {
            eye->setChecked(false);
            QApplication::processEvents();
            bool allHidden = !panes.isEmpty();
            for (DrawingPreview* pane : panes)
                if (pane->showsConnection()) allHidden = false;
            check(allHidden, "library: pressed out, no pane marks the connection point");
            eye->setChecked(true);
            QApplication::processEvents();
            bool allShown = !panes.isEmpty();
            for (DrawingPreview* pane : panes)
                if (!pane->showsConnection()) allShown = false;
            check(allShown, "library: pressed in, they all mark it again");
        }

        // AND IT IS ACTUALLY NOT DRAWN. A flag that no painter reads is a setting that does nothing, so the
        // orange is counted on the pane itself: the part keeps its lines either way, and only the colour that
        // says "this is where the harness attaches" comes and goes.
        {
            DrawingPreview pane(QStringLiteral("test"));
            pane.resize(240, 180);
            pane.setDrawing(twoLinesOneMarked());
            auto orange = [](const QImage& img) {
                int n = 0;
                for (int y = 0; y < img.height(); ++y)
                    for (int x = 0; x < img.width(); ++x) {
                        const QRgb c = img.pixel(x, y);
                        if (qRed(c) > 150 && qGreen(c) > 90 && qGreen(c) < 190 && qBlue(c) < 110) ++n;
                    }
                return n;
            };
            auto ink = [](const QImage& img) {
                int n = 0;
                for (int y = 0; y < img.height(); ++y)
                    for (int x = 0; x < img.width(); ++x)
                        if (qGray(img.pixel(x, y)) > 90) ++n;
                return n;
            };
            // AN ORANGE LINE IS NOT A MARK, and there is no longer any such thing. Saying where a harness
            // attaches by colouring one of the part's OWN lines orange reads as a feature of the part - an
            // orange line across a connector face looks like something the connector has - says nothing
            // about which way the part runs from there, and cannot say where the wires leave at all. The
            // part's marks say all of it, drawn as marks (see the faces test below).
            //
            // So the flag colours nothing now, either way, and the line it was on is still drawn in the
            // same grey as the rest of the part: the mark is gone, not the geometry.
            pane.setShowConnection(true);
            const QImage withMark = pane.grab().toImage();
            pane.setShowConnection(false);
            const QImage without = pane.grab().toImage();
            std::printf("  library: %d orange pixel(s) shown, %d hidden; ink %d then %d\n", orange(withMark),
                        orange(without), ink(withMark), ink(without));
            check(orange(withMark) == 0 && orange(without) == 0,
                  "library: a flagged line is not painted orange any more - a mark is drawn as a mark");
            check(ink(without) > 20 && ink(without) == ink(withMark),
                  "library: and the line itself is still there, in the same grey as the rest of the part");
        }
        QFile::remove(libFile);
    }


    // ---- the library's drawing panes: four of a size, draggable, and fillable from the house style ---
    {
        // They were four columns of a grid, and a grid column is as wide as the widest thing in it - which was
        // not the drawing but the row of BUTTONS under it. The fanout has five and the schematic four, so the
        // fanout's drawing was given half again the width of the schematic's for a reason that has nothing to
        // do with either drawing.
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_panes_lib.json"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            LibraryPart part;
            part.id = QStringLiteral("P-PANES");
            part.kind = PartKind::Connector;
            part.description = QStringLiteral("A connector to draw");
            lib.parts.push_back(part);
            lib.save(libFile);
        }
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        PartsLibraryDialog dlg(&ed);
        dlg.setLibraryPath(libFile);
        dlg.selectPart(QStringLiteral("P-PANES"));
        // Wide enough that there IS slack to drag: four panes with a floor of 150 px each fill a 600-pixel
        // row exactly, and a row with nothing spare cannot be re-divided however good the splitter is.
        // Now that no pane goes narrower than its buttons' captions, "wide enough" is measured from what the
        // window needs rather than guessed: its minimum, and room beyond it.
        // Measured once it is on screen, styled and filled, which is when its needs are real.
        dlg.show();
        QApplication::processEvents();
        dlg.setMinimumSize(dlg.layout()->minimumSize());
        dlg.resize(std::max(1600, dlg.layout()->minimumSize().width() + 400), 900);
        QApplication::processEvents();

        // FOUR PANES OF A SPLITTER, equal to begin with and any one of them draggable wider.
        // THREE panes now, not four: the fanout says which side of the connector it was drawn from, and a
        // second window for "the other one" asked for the same drawing twice.
        QSplitter* paneSplit = nullptr;
        for (QSplitter* sp : dlg.findChildren<QSplitter*>())
            if (sp->count() == 3 && sp->findChildren<DrawingPreview*>().size() == 3) paneSplit = sp;
        check(paneSplit != nullptr, "panes: the three drawings are the three panes of a splitter");
        if (paneSplit) {
            QList<int> sizes = paneSplit->sizes();
            int widest = 0, narrowest = 1 << 30;
            for (int s : sizes) { widest = std::max(widest, s); narrowest = std::min(narrowest, s); }
            std::printf("  panes: widths %d/%d/%d\n", sizes.value(0), sizes.value(1), sizes.value(2));
            check(widest - narrowest <= 2, "panes: all four the same width, whatever is written on the buttons");
            check(!paneSplit->childrenCollapsible(), "panes: and none of them can be dragged shut by accident");
            // Draggable: a splitter that refuses to move is not a resizable pane.
            QList<int> pulled = sizes;
            pulled[0] = sizes[0] + 80;
            pulled[1] = sizes[1] - 80;
            paneSplit->setSizes(pulled);
            QApplication::processEvents();
            check(paneSplit->sizes().value(0) > sizes.value(0),
                  "panes: a pane can be pulled wider, to look at a drawing closely");
        }
        // Every pane the same minimum, so none of them is the reason the window cannot be made smaller.
        QSet<QString> mins;
        for (DrawingPreview* p : dlg.findChildren<DrawingPreview*>())
            mins.insert(QStringLiteral("%1x%2").arg(p->minimumWidth()).arg(p->minimumHeight()));
        check(mins.size() == 1, "panes: and one minimum size between them");

        // FROM THE HOUSE STYLE. A library part is very often one of the pieces the default models already
        // hold, with a part number on it; drawing it a second time by hand to say the same thing is work
        // nobody should do.
        MechModelSet models;
        MechModel m;
        m.kind = MechModelKind::FerruleInsulated;
        m.name = QStringLiteral("Insulated");
        Entity body;
        body.type = EntityType::Line;
        body.a = {0.0, 0.0};
        body.b = {12.0, 0.0};
        Entity crimp;
        crimp.type = EntityType::Line;
        crimp.a = {2.0, -1.0};
        crimp.b = {2.0, 1.0};
        m.drawing = partDrawingFromEntities({body, crimp});
        Entity glyph;
        glyph.type = EntityType::Circle;
        glyph.center = {0.0, 0.0};
        glyph.radius = 1.5;
        m.schematicDrawing = partDrawingFromEntities({glyph});
        models.set(m);

        check(dlg.takeDrawingFromDefaultModel(models, MechModelKind::FerruleInsulated,
                                              QStringLiteral("Insulated"), false, true),
              "from default: the model's mechanical drawing becomes the part's mechanical drawing");
        check(dlg.takeDrawingFromDefaultModel(models, MechModelKind::FerruleInsulated,
                                              QStringLiteral("Insulated"), true, false),
              "from default: and its schematic becomes the part's schematic");
        const LibraryPart* got = nullptr;
        const PartsLibrary after = dlg.library();
        for (const LibraryPart& p : after.parts)
            if (p.id == QStringLiteral("P-PANES")) got = &p;
        check(got != nullptr, "from default: the part is still there");
        if (got) {
            std::printf("  from default: %lld object(s) mechanical, %lld schematic\n",
                        static_cast<long long>(got->mechanical.size()),
                        static_cast<long long>(got->schematic.size()));
            check(got->mechanical.size() == 2,
                  "from default: two objects, which is what the model's mechanical drawing has");
            check(got->schematic.size() == 1,
                  "from default: one, which is what its schematic has - the two sides do not cross over");
        }
        // Nothing drawn is nothing to take - and taking it would CLEAR the part's own drawing, which is a
        // destructive answer to "give me the house one".
        MechModel empty;
        empty.kind = MechModelKind::Boot;
        empty.name = QStringLiteral("Straight");
        MechModelSet blank;
        blank.set(empty);
        check(!dlg.takeDrawingFromDefaultModel(blank, MechModelKind::Boot, QStringLiteral("Straight"),
                                               false, true),
              "from default: a model with nothing drawn is refused rather than emptying the part");
        QFile::remove(libFile);
    }


    // ---- a part carries the faces its wires leave by, and its marks are drawn as marks --------------
    {
        // ANYTHING WIRES GO INTO has one or more faces they come out of - a connector, a ferrule, a ring -
        // and the generator hands the branches out in the order they were marked. A default model has been
        // able to say so all along; a library part, which is the same part with a part number on it, could
        // only be told where it connects.
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_faces_lib.json"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            LibraryPart part;
            part.id = QStringLiteral("P-FACE");
            part.kind = PartKind::Connector;
            part.description = QStringLiteral("A connector with faces");
            Entity body;
            body.type = EntityType::Rect;
            body.points = {{0.0, -5.0}, {10.0, -5.0}, {10.0, 5.0}, {0.0, 5.0}};
            part.mechanical = partDrawingFromEntities({body});
            part.hasConnect = true;
            part.connectPoint = {0.0, 0.0};
            part.connectAngleRad = 0.0;
            part.outputs.push_back({{10.0, -4.0}, {10.0, 4.0}});
            lib.parts.push_back(part);
            check(lib.save(libFile), "faces: a part with a face on it is written");
        }
        // KEPT IN THE FILE. A mark that does not survive being saved is a mark somebody has to put on again
        // every time the library is opened.
        {
            PartsLibrary back;
            check(back.load(libFile), "faces: and read again");
            const LibraryPart* p = back.byId(QStringLiteral("P-FACE"));
            check(p != nullptr, "faces: the part came back");
            if (p) {
                std::printf("  faces: %lld face(s) kept, first from %.1f,%.1f to %.1f,%.1f\n",
                            static_cast<long long>(p->outputs.size()),
                            p->outputs.empty() ? 0.0 : p->outputs.front().a.x,
                            p->outputs.empty() ? 0.0 : p->outputs.front().a.y,
                            p->outputs.empty() ? 0.0 : p->outputs.front().b.x,
                            p->outputs.empty() ? 0.0 : p->outputs.front().b.y);
                check(p->outputs.size() == 1, "faces: the face is in the file");
                if (p->outputs.size() == 1)
                    check(std::abs(p->outputs.front().a.y + 4.0) < 1e-9 &&
                              std::abs(p->outputs.front().b.y - 4.0) < 1e-9,
                          "faces: with the two ends it was marked between, the right way round");
            }
        }

        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        PartsLibraryDialog dlg(&ed);
        dlg.setLibraryPath(libFile);
        dlg.selectPart(QStringLiteral("P-FACE"));
        dlg.resize(1600, 900);
        dlg.show();
        QApplication::processEvents();

        // THREE PANES, not four. The fanout already says which side of the connector it was drawn from, and
        // that is the whole of the difference between the two sides: a second window for "the other one"
        // asks for the same drawing twice and for the two to be kept in step by hand.
        const QList<DrawingPreview*> panes = dlg.findChildren<DrawingPreview*>();
        std::printf("  faces: %lld drawing pane(s)\n", static_cast<long long>(panes.size()));
        check(panes.size() == 3, "panes: three drawings - the fanout says its own side, so there is no fourth");

        // AND THE MARKS ARE DRAWN. Blue where it connects, with an arrow along the run, and a purple bar
        // across the face the wires leave by - the same marks the editing canvas draws, in the same colours.
        DrawingPreview* mech = nullptr;
        for (DrawingPreview* p : panes)
            for (const Entity& e : p->entities())
                if (e.meta.value(QLatin1String(kPreviewMarkKey)).toBool()) mech = p;
        check(mech != nullptr, "marks: the mechanical pane draws the part's marks over the drawing");
        if (mech) {
            QStringList colours;
            int arrows = 0, bars = 0;
            for (const Entity& e : mech->entities()) {
                if (!e.meta.value(QLatin1String(kPreviewMarkKey)).toBool()) continue;
                if (!colours.contains(e.color)) colours << e.color;
                if (e.id.startsWith(QStringLiteral("model-connect-barb"))) ++arrows;
                if (e.meta.contains(QStringLiteral("modelOutput"))) ++bars;
            }
            colours.sort();
            std::printf("  marks: %s; %d barb(s), %d face bar(s)\n",
                        qPrintable(colours.join(QStringLiteral(", "))), arrows, bars);
            check(colours.contains(QStringLiteral("#5aa0ff")) && arrows == 2,
                  "marks: blue where it connects, with an arrow saying which way it runs");
            check(colours.contains(QStringLiteral("#c678dd")) && bars == 1,
                  "marks: and a purple bar across the face the wires leave by");
            // AND NO ORANGE LINE. The old way was to colour one of the part's OWN lines orange, which reads
            // as a feature of the part, says nothing about direction and cannot say where the wires leave.
            const QImage shot = mech->grab().toImage();
            int oldOrange = 0;
            for (int y = 0; y < shot.height(); ++y)
                for (int x = 0; x < shot.width(); ++x) {
                    const QRgb c = shot.pixel(x, y);
                    if (qRed(c) > 200 && qGreen(c) > 130 && qGreen(c) < 190 && qBlue(c) < 110) ++oldOrange;
                }
            std::printf("  marks: %d pixel(s) of the old orange connection line\n", oldOrange);
            check(oldOrange == 0, "marks: the orange line is gone - a mark is drawn as a mark");
        }
        QFile::remove(libFile);
    }


    // ---- a schematic connector placed with its fanout, and a part told where its wires leave --------
    {
        // THE FANOUT BELONGS BESIDE THE SYMBOL IT BELONGS TO. Put down separately it has to be lined up by
        // hand every time, and a fanout lined up by eye against the wrong connector is a drawing that lies.
        // A toggle, because a sheet of forty connectors would otherwise be a sheet of forty fanouts.
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_fanout_place.json"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            LibraryPart part;
            part.id = QStringLiteral("P-FAN");
            part.kind = PartKind::Connector;
            part.description = QStringLiteral("Three-way with a fanout");
            Entity body;
            body.type = EntityType::Rect;
            body.points = {{0.0, 0.0}, {20.0, 0.0}, {20.0, 30.0}, {0.0, 30.0}};
            part.schematic = partDrawingFromEntities({body});
            Entity brk;
            brk.type = EntityType::Line;
            brk.a = {0.0, 0.0};
            brk.b = {10.0, 10.0};
            part.fanout = partDrawingFromEntities({brk});
            part.mechanical = partDrawingFromEntities({body});   // something to mark a face on
            lib.parts.push_back(part);
            lib.save(libFile);
        }
        PartsCatalog cat;
        cat.openRemembered();
        cat.connectLibrary(libFile);
        cat.remember();

        MainWindow win;
        win.resize(1280, 820);
        win.show();
        QApplication::processEvents();

        auto* ed = win.findChild<Editor*>();
        check(ed != nullptr && ed->document() != nullptr, "fanout place: there is a drawing to place on");
        QCheckBox* toggle = nullptr;
        for (QCheckBox* b : win.findChildren<QCheckBox*>())
            if (b->text().contains(QStringLiteral("placed with its fanout"))) toggle = b;
        check(toggle != nullptr, "fanout place: there is a switch for it in the parts panel");
        if (ed && ed->document() && toggle) {
            // OFF: the symbol alone, as before.
            toggle->setChecked(false);
            check(!win.placesFanoutWithSchematic(), "fanout place: off means off");
            win.placeLibraryPart(QStringLiteral("P-FAN"), /*mechanical=*/false);
            const int alone = static_cast<int>(ed->clipboardCount());
            ed->onKey(Qt::Key_Escape, Qt::NoModifier);
            // ON: the fanout comes with it, in front of the symbol, as ONE placement.
            toggle->setChecked(true);
            check(win.placesFanoutWithSchematic(), "fanout place: and the switch is remembered as on");
            win.placeLibraryPart(QStringLiteral("P-FAN"), /*mechanical=*/false);
            const int together = static_cast<int>(ed->clipboardCount());
            std::printf("  fanout place: %d object(s) alone, %d with the fanout\n", alone, together);
            check(together > alone,
                  "fanout place: the fanout goes down with the symbol, in the same placement");
            // IN FRONT OF IT: the fanout sits to the left of the symbol, not on top of it.
            Bounds all, sym;
            for (const Entity& e : ed->clipboardContents()) {
                all.expand(e.bounds());
                if (e.type == EntityType::Rect) sym.expand(e.bounds());
            }
            if (all.valid && sym.valid) {
                std::printf("  fanout place: symbol x %.1f..%.1f inside all x %.1f..%.1f\n", sym.min.x,
                            sym.max.x, all.min.x, all.max.x);
                check(all.min.x < sym.min.x - 1e-6,
                      "fanout place: and it is in FRONT of the symbol, not over it");
            }
            ed->onKey(Qt::Key_Escape, Qt::NoModifier);
        }

        // Placing FROM THE LIBRARY WINDOW hides it so the sheet can take the click. Esc lets the ghost go,
        // so the same library comes back instead of leaving the user with neither window nor placement.
        PartsLibraryDialog* library = win.partsLibraryDialogForTest();
        check(library != nullptr, "library place: the library window is there");
        if (library && ed) {
            library->selectPart(QStringLiteral("P-FAN"));
            library->show();
            QApplication::processEvents();
            library->insertIntoDrawing(false);
            QApplication::processEvents();
            check(ed->placingPaste() && library->isHidden(),
                  "library place: the part is on the cursor and the library is out of the way");
            check(QApplication::focusWidget() == win.findChild<CanvasWidget*>(),
                  "library place: and the sheet owns the keyboard while it is there");
            ed->onKey(Qt::Key_Escape, Qt::NoModifier);
            QApplication::processEvents();
            check(!ed->placingPaste() && !library->isHidden(),
                  "library place: Esc puts the ghost down and brings the library back");
        }

        // AND THE OUTPUT FACE BUTTON WORKS ON A LIBRARY PART. It was greyed for a part session on the
        // reasoning that a connector has its own pins to say where the wires go - true of one built from
        // its data, no help at all for one traced off a supplier's drawing, which is most of them. A part
        // is SET DOWN by that face, so it has to be possible to mark one.
        // The session is opened THROUGH the library window, which owns the part being worked on - without
        // it there is no session at all, and a greyed button would be proof of nothing.
        win.partsLibraryDialogForTest();
        win.openLibraryPartEditor(QStringLiteral("P-FAN"), LibraryPartDrawing::Mechanical);
        QApplication::processEvents();
        QToolButton* face = nullptr;
        QToolButton* scaleBy = nullptr;
        for (QToolButton* b : win.findChildren<QToolButton*>()) {
            if (b->toolTip().startsWith(QStringLiteral("Click across the face"))) face = b;
            if (b->toolTip().startsWith(QStringLiteral("Click the object the app scales"))) scaleBy = b;
        }
        check(face != nullptr, "output face: the button is there in a library part session");
        if (face) check(face->isEnabled(), "output face: and it can be pressed - it was greyed");
        if (scaleBy)
            check(!scaleBy->isEnabled(),
                  "output face: what it is scaled BY stays off - a catalogue part is the size it is");
        {
            PartsCatalog put;
            put.openRemembered();
            put.disconnectLibrary(libFile);
            put.remember();
        }
        QFile::remove(libFile);
    }


    // ---- a double-click on a placed part does not fetch the library --------------------------------
    {
        // A part placed from the library is a block copy, and double-clicking a block copy opens the block
        // for editing - which for a library drawing meant OPENING THE PARTS LIBRARY: a whole window arriving
        // over the drawing because of one stray double-click while selecting or moving something. A
        // double-click on a placed part is nearly always a mis-click, and the answer to a mis-click is
        // nothing at all. Getting to the library is still wanted, and it is on the part's own right-click
        // menu - asked for rather than sprung.
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_dblclick_lib.json"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            LibraryPart part;
            part.id = QStringLiteral("P-DBL");
            part.kind = PartKind::Connector;
            part.description = QStringLiteral("A placed connector");
            Entity body;
            body.type = EntityType::Rect;
            body.points = {{0.0, 0.0}, {20.0, 0.0}, {20.0, 12.0}, {0.0, 12.0}};
            part.mechanical = partDrawingFromEntities({body});
            lib.parts.push_back(part);
            lib.save(libFile);
        }
        PartsCatalog cat;
        cat.openRemembered();
        cat.connectLibrary(libFile);
        cat.remember();

        MainWindow win;
        win.resize(1200, 800);
        win.show();
        QApplication::processEvents();
        auto* ed = win.findChild<Editor*>();
        check(ed != nullptr && ed->document() != nullptr, "dbl: there is a drawing");
        if (ed && ed->document()) {
            // The part, on the sheet, as a block copy - placed the way the canvas menu places one.
            win.placeLibraryPart(QStringLiteral("P-DBL"), /*mechanical=*/true);
            ed->onPointerMove({50.0, 50.0});
            ed->onPointerPress({50.0, 50.0}, Qt::LeftButton, Qt::NoModifier);
            QApplication::processEvents();
            int placed = 0;
            Vec2 on{0.0, 0.0};
            for (const Entity& e : ed->document()->entities())
                if (!entityBlockName(e).isEmpty()) {
                    ++placed;
                    const Bounds b = e.bounds();
                    if (b.valid) on = {(b.min.x + b.max.x) / 2.0, (b.min.y + b.max.y) / 2.0};
                }
            std::printf("  dbl: %d placed object(s) carrying a block name\n", placed);
            check(placed > 0, "dbl: the part is on the sheet as a copy of its block");

            // DOUBLE-CLICKED: no library window, and no new document opened for it either.
            ed->onDoubleClick(on);
            QApplication::processEvents();
            bool libraryUp = false;
            for (QDialog* d : win.findChildren<QDialog*>())
                if (d->windowTitle().contains(QStringLiteral("parts library")) && d->isVisible())
                    libraryUp = true;
            check(!libraryUp, "dbl: the parts library does not arrive over the drawing");

            // AND THE RIGHT-CLICK STILL OFFERS IT, in words that read as a door.
            ed->clearSelection();
            for (const Entity& e : ed->document()->entities())
                if (!entityBlockName(e).isEmpty()) { ed->selectById(e.id); break; }
            QMenu menu;
            win.buildCanvasMenu(menu, on);
            QStringList said;
            for (QAction* a : menu.actions())
                if (!a->text().isEmpty()) said << a->text();
            std::printf("  dbl: menu - %s\n", qPrintable(said.join(QStringLiteral(" | "))));
            bool opens = false;
            for (const QString& t : said)
                if (t.contains(QStringLiteral("in the parts library"))) opens = true;
            check(opens, "dbl: right-clicking it offers to open it in the parts library");
        }
        {
            PartsCatalog put;
            put.openRemembered();
            put.disconnectLibrary(libFile);
            put.remember();
        }
        QFile::remove(libFile);
    }


    // ---- the right-click menu offers a draw order ---------------------------------------------------
    {
        // Two objects in the same place are drawn in the order the sheet holds them, and there was no way
        // to say which. The commands are on the right-click menu, where a person looks for them, under a
        // submenu of their own - and greyed out with nothing picked, because they act on a selection.
        MainWindow win;
        win.resize(1200, 800);
        win.show();
        QApplication::processEvents();
        auto* ed = win.findChild<Editor*>();
        check(ed != nullptr && ed->document() != nullptr, "draw order menu: there is a drawing");
        if (ed && ed->document()) {
            Entity line;
            line.id = QStringLiteral("ORD-1");
            line.type = EntityType::Line;
            line.a = {0.0, 0.0};
            line.b = {50.0, 0.0};
            ed->document()->entities().push_back(line);

            auto submenu = [&](QMenu& m) -> QMenu* {
                for (QAction* a : m.actions())
                    if (a->menu() && a->text().contains(QStringLiteral("Draw Order"))) return a->menu();
                return nullptr;
            };

            // WITH NOTHING PICKED it is there but dead - the commands need something to act on.
            ed->clearSelection();
            QMenu idle;
            win.buildCanvasMenu(idle, {25.0, 0.0});
            QMenu* off = submenu(idle);
            check(off != nullptr, "draw order menu: the submenu is on the canvas menu");
            bool offEnabled = true;
            for (QAction* a : idle.actions())
                if (a->menu() == off) offEnabled = a->isEnabled();
            check(!offEnabled, "draw order menu: with nothing picked it is greyed out");

            // WITH SOMETHING PICKED, all four commands, live.
            ed->selectById(QStringLiteral("ORD-1"));
            QMenu live;
            win.buildCanvasMenu(live, {25.0, 0.0});
            QMenu* on = submenu(live);
            check(on != nullptr, "draw order menu: still there with a selection");
            QStringList said;
            if (on)
                for (QAction* a : on->actions())
                    if (!a->text().isEmpty()) said << a->text();
            std::printf("  draw order menu: %s\n", qPrintable(said.join(QStringLiteral(" | "))));
            check(said.contains(QStringLiteral("Bring to Front")), "draw order menu: Bring to Front");
            check(said.contains(QStringLiteral("Send to Back")), "draw order menu: Send to Back");
            check(said.contains(QStringLiteral("Bring Above Objects")),
                  "draw order menu: Bring Above Objects");
            check(said.contains(QStringLiteral("Send Under Objects")),
                  "draw order menu: Send Under Objects");
            bool onEnabled = false;
            for (QAction* a : live.actions())
                if (a->menu() == on) onEnabled = a->isEnabled();
            check(onEnabled, "draw order menu: and with a selection it is live");

            // AND THE ENTRY DOES THE WORK - the menu is not a row of dead words.
            if (on) {
                Entity over;
                over.id = QStringLiteral("ORD-2");
                over.type = EntityType::Line;
                over.a = {0.0, 1.0};
                over.b = {50.0, 1.0};
                ed->document()->entities().push_back(over);   // ORD-2 now sits in front of ORD-1
                for (QAction* a : on->actions())
                    if (a->text() == QLatin1String("Bring to Front")) a->trigger();
                QApplication::processEvents();
                const auto& ents = ed->document()->entities();
                check(!ents.empty() && ents.back().id == QLatin1String("ORD-1"),
                      "draw order menu: Bring to Front put the picked line in front");
            }
        }
    }

    // ---- editing in the library: primitives, and a connector made from data --------------------------
    {
        // ON A SHEET a part is one thing: a click picks the whole connector, which is right, because there
        // it is a part being USED. In the library it is being DRAWN, and every line of it has to be movable
        // on its own - a session where the first click selects all seventy-nine lines of a connector body is
        // a session in which nothing can be corrected.
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_editprim_lib.json"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            LibraryPart drawn;
            drawn.id = QStringLiteral("P-PRIM");
            drawn.kind = PartKind::Connector;
            drawn.description = QStringLiteral("Captured connector");
            drawn.params[QStringLiteral("ways")] = QStringLiteral("3");
            Entity a;
            a.type = EntityType::Line;
            a.a = {0.0, 0.0};
            a.b = {10.0, 0.0};
            Entity b;
            b.type = EntityType::Line;
            b.a = {0.0, 5.0};
            b.b = {10.0, 5.0};
            drawn.mechanical = partDrawingFromEntities({a, b});
            lib.parts.push_back(drawn);
            // And one with NOTHING captured for its schematic: it has one all the same, made from its own
            // data, and that is what the library shows and what it puts on a sheet.
            LibraryPart fromData;
            fromData.id = QStringLiteral("P-DATA");
            fromData.kind = PartKind::Connector;
            fromData.description = QStringLiteral("Connector with no captured symbol");
            fromData.params[QStringLiteral("ways")] = QStringLiteral("4");
            lib.parts.push_back(fromData);
            lib.save(libFile);
        }
        PartsCatalog cat;
        cat.openRemembered();
        cat.connectLibrary(libFile);
        cat.remember();

        MainWindow win;
        win.resize(1280, 820);
        win.show();
        QApplication::processEvents();
        win.partsLibraryDialogForTest();   // the session is opened through the library window
        win.openLibraryPartEditor(QStringLiteral("P-PRIM"), LibraryPartDrawing::Mechanical);
        QApplication::processEvents();
        auto* ed = win.findChild<Editor*>();
        check(ed != nullptr && ed->document() != nullptr, "edit prim: the session opened");
        if (ed && ed->document()) {
            int grouped = 0, lines = 0;
            for (const Entity& e : ed->document()->entities()) {
                if (e.layer == QLatin1String(kMechModelGuideLayer)) continue;
                ++lines;
                if (!entityBlockInstance(e).isEmpty()) ++grouped;
            }
            std::printf("  edit prim: %d line(s), %d of them grouped\n", lines, grouped);
            check(lines >= 2, "edit prim: the part's lines are on the canvas");
            check(grouped == 0, "edit prim: and not one of them is stamped as part of a block");
            // Which shows in the one way that matters: a click takes ONE line, not all of them.
            ed->clearSelection();
            ed->onPointerPress({5.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            ed->onPointerRelease({5.0, 0.0}, Qt::LeftButton);
            std::printf("  edit prim: clicking one line selected %d\n",
                        static_cast<int>(ed->selection().size()));
            check(ed->selection().size() == 1, "edit prim: a click takes the line it is on, and no more");
        }

        // A CONNECTOR MADE FROM DATA opens with the drawing that is actually being shown, not empty.
        win.openLibraryPartEditor(QStringLiteral("P-DATA"), LibraryPartDrawing::Schematic);
        QApplication::processEvents();
        auto* ed2 = win.findChild<Editor*>();
        if (ed2 && ed2->document()) {
            int on = 0;
            for (const Entity& e : ed2->document()->entities())
                if (e.layer != QLatin1String(kMechModelGuideLayer)) ++on;
            std::printf("  edit prim: a from-data schematic opened with %d object(s)\n", on);
            check(on > 0,
                  "edit prim: a connector whose symbol comes from its data is there to edit, not an empty "
                  "canvas");
        }
        {
            PartsCatalog put;
            put.openRemembered();
            put.disconnectLibrary(libFile);
            put.remember();
        }
        QFile::remove(libFile);
    }


    // ---- Shift+Enter is a new row in the on-canvas text editor ---------------------------------------
    {
        // A leader's words are a single-value edit, so plain Enter finishes them - and Shift+Enter finished
        // them too, because nothing looked at the Shift. Which left no way at all to type a callout of two
        // lines: the one key everybody reaches for did the opposite of what it does everywhere else.
        MainWindow win;
        win.resize(1200, 800);
        win.show();
        QApplication::processEvents();
        auto* ed = win.findChild<Editor*>();
        check(ed != nullptr && ed->document() != nullptr, "shift-enter: there is a drawing");
        if (ed && ed->document()) {
            Entity ld;
            ld.id = QStringLiteral("ld");
            ld.type = EntityType::Leader;
            ld.points = {{0.0, 0.0}, {30.0, 20.0}};
            ld.meta.insert(QStringLiteral("text"), QStringLiteral("ONE"));
            ld.meta.insert(QStringLiteral("size"), 10.0);
            ld.meta.insert(QStringLiteral("landing"), 6.0);
            ed->document()->entities().push_back(ld);
            // Open its editor the way a double-click does - on the words themselves, found rather than
            // guessed at, because a click into the gap between the shelf and the words proves nothing.
            const Bounds words = ed->leaderWordsBox(ed->document()->entities().back());
            check(words.valid, "shift-enter: the leader has words to double-click");
            ed->onDoubleClick({(words.min.x + words.max.x) / 2.0, (words.min.y + words.max.y) / 2.0});
            QApplication::processEvents();
            QPlainTextEdit* box = nullptr;
            for (QPlainTextEdit* t : win.findChildren<QPlainTextEdit*>())
                if (t->isVisible()) box = t;
            check(box != nullptr, "shift-enter: the on-canvas editor is open on it");
            if (box) {
                box->setFocus();
                QKeyEvent shiftReturn(QEvent::KeyPress, Qt::Key_Return, Qt::ShiftModifier,
                                      QStringLiteral("\r"));
                QApplication::sendEvent(box, &shiftReturn);
                QApplication::processEvents();
                std::printf("  shift-enter: the editor is %s and holds %d line(s)\n",
                            box->isVisible() ? "still open" : "shut",
                            static_cast<int>(box->toPlainText().count(QLatin1Char('\n')) + 1));
                check(box->isVisible(), "shift-enter: it does not finish the edit");
                // Qt puts a break in as a line SEPARATOR inside the paragraph rather than as a second
                // paragraph - either way it is a second row of words, which is what was asked for.
                check(box->toPlainText().contains(QLatin1Char('\n')),
                      "shift-enter: it puts a second row in");
                // And plain Enter still finishes it, which is the other half of the bargain.
                QKeyEvent plainReturn(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
                                      QStringLiteral("\r"));
                QApplication::sendEvent(box, &plainReturn);
                QApplication::processEvents();
                check(!box->isVisible(), "shift-enter: plain Enter still finishes it");
            }
        }
    }


    // ---- a marked output face survives the session, and one Place puts down what is ticked ----------
    {
        // A FACE MARKED AND THEN DROPPED is worse than one that cannot be marked at all. The faces were
        // left behind twice over on the way out of a session: not shifted with the drawing as it was
        // squared up, and then not copied into the part - so the library still said "no output face
        // marked" over a part that had just been given one.
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_facesave_lib.json"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            LibraryPart part;
            part.id = QStringLiteral("P-SAVE");
            part.kind = PartKind::Connector;
            part.description = QStringLiteral("Connector to mark up");
            Entity body;
            body.type = EntityType::Rect;
            body.points = {{100.0, 50.0}, {120.0, 50.0}, {120.0, 62.0}, {100.0, 62.0}};
            part.mechanical = partDrawingFromEntities({body});
            part.schematic = partDrawingFromEntities({body});
            Entity brk;
            brk.type = EntityType::Line;
            brk.a = {0.0, 0.0};
            brk.b = {8.0, 4.0};
            part.fanout = partDrawingFromEntities({brk});
            lib.parts.push_back(part);
            lib.save(libFile);
        }
        PartsCatalog cat;
        cat.openRemembered();
        cat.connectLibrary(libFile);
        cat.remember();

        MainWindow win;
        win.resize(1400, 860);
        win.show();
        QApplication::processEvents();
        auto* dlg = win.partsLibraryDialogForTest();
        check(dlg != nullptr, "face save: the library window is there");
        win.openLibraryPartEditor(QStringLiteral("P-SAVE"), LibraryPartDrawing::Mechanical);
        QApplication::processEvents();
        auto* ed = win.findChild<Editor*>();
        check(ed != nullptr && ed->document() != nullptr, "face save: the session opened");
        if (ed && ed->document() && dlg) {
            // Mark a face across the right-hand end of the body, the way the tool does it: two clicks.
            ed->setTool(ToolId::ModelOutputLine);
            ed->onPointerPress({120.0, 51.0}, Qt::LeftButton, Qt::NoModifier);
            ed->onPointerPress({120.0, 61.0}, Qt::LeftButton, Qt::NoModifier);
            QApplication::processEvents();
            win.saveMechModelBeingEdited();   // Capture: the same button the ribbon corner has
            QApplication::processEvents();
            const PartsLibrary after = dlg->library();
            const LibraryPart* got = nullptr;
            for (const LibraryPart& p : after.parts)
                if (p.id == QStringLiteral("P-SAVE")) got = &p;
            check(got != nullptr, "face save: the part is still in the library");
            if (got) {
                std::printf("  face save: %lld face(s) kept\n", static_cast<long long>(got->outputs.size()));
                check(got->outputs.size() == 1, "face save: the face marked in the session is in the part");
                if (got->outputs.size() == 1) {
                    // The drawing is squared up on the way out - moved to the origin, centred across the
                    // run - and the face is moved with it. Left behind, it would point into space.
                    Bounds b;
                    for (const Entity& e : partDrawingEntities(got->mechanical)) b.expand(e.bounds());
                    const MechOutputFace& f = got->outputs.front();
                    std::printf("  face save: drawing x %.1f..%.1f, face at x %.1f\n", b.min.x, b.max.x,
                                f.a.x);
                    check(b.valid && f.a.x >= b.min.x - 1e-6 && f.a.x <= b.max.x + 1e-6,
                          "face save: and it moved with the drawing rather than being left behind");
                }
            }
        }

        // ONE PLACE BUTTON, and a tick against each drawing it puts down.
        dlg->show();
        dlg->selectPart(QStringLiteral("P-SAVE"));
        QApplication::processEvents();
        QToolButton* place = nullptr;
        for (QToolButton* b : dlg->findChildren<QToolButton*>())
            if (b->text() == QStringLiteral("Place")) place = b;
        check(place != nullptr, "place: there is one Place button");
        int places = 0;
        for (QPushButton* b : dlg->findChildren<QPushButton*>())
            if (b->text().startsWith(QStringLiteral("Place"))) ++places;
        std::printf("  place: %d old Place button(s) left on the panes\n", places);
        check(places == 0, "place: and not one on every pane as well - five buttons for one verb");
        if (place && place->menu()) {
            // The ticks are in a PANEL inside the menu now (a widget in a menu takes its own clicks, so the
            // menu stays open while several are made) - so they are counted there rather than as menu items.
            QWidget* panel = nullptr;
            for (QAction* a : place->menu()->actions())
                if (auto* wa = qobject_cast<QWidgetAction*>(a)) panel = wa->defaultWidget();
            check(panel != nullptr, "place: the menu holds a panel of ticks");
            if (panel) {
                QStringList said;
                for (QCheckBox* b : panel->findChildren<QCheckBox*>()) said << b->text();
                std::printf("  place: ticks - %s\n", qPrintable(said.join(QStringLiteral(" | "))));
                check(said.size() >= 4, "place: the panel lists the drawings a part can have");
            }
        }
        {
            PartsCatalog put;
            put.openRemembered();
            put.disconnectLibrary(libFile);
            put.remember();
        }
        QFile::remove(libFile);
    }


    // ---- the tick puts the contact menu away --------------------------------------------------------
    {
        // With the pins fitted there is no contact to order, so the menu is not a question with an answer:
        // it goes, rather than sitting there greyed, and the rows that describe the pins take its place.
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_pins_lib.json"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            LibraryPart part;
            part.id = QStringLiteral("P-WITHPINS");
            part.kind = PartKind::Connector;
            part.description = QStringLiteral("Header with its pins");
            part.params[QStringLiteral("ways")] = QStringLiteral("4");
            lib.parts.push_back(part);
            lib.save(libFile);
        }
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        PartsLibraryDialog dlg(&ed);
        dlg.setLibraryPath(libFile);
        dlg.selectPart(QStringLiteral("P-WITHPINS"));
        dlg.resize(1500, 900);
        dlg.show();
        QApplication::processEvents();

        QCheckBox* tick = nullptr;
        for (QCheckBox* b : dlg.findChildren<QCheckBox*>())
            if (b->text().contains(QStringLiteral("its pins fitted"))) tick = b;
        check(tick != nullptr, "with pins: a connector can say it comes with its pins");
        QComboBox* contact = nullptr;
        for (QComboBox* c : dlg.findChildren<QComboBox*>())
            if (c->toolTip().startsWith(QStringLiteral("The contact ordered"))) contact = c;
        check(contact != nullptr, "with pins: and there is a contact menu to put away");
        if (tick && contact) {
            check(contact->isVisible(), "with pins: unticked, the contact menu is there as before");
            tick->setChecked(true);
            QApplication::processEvents();
            std::printf("  with pins: ticked - contact menu %s\n",
                        contact->isVisible() ? "still showing" : "gone");
            check(!contact->isVisible(), "with pins: ticked, it goes - there is nothing to tie");
            // And a row appears to describe the pins, with a way to add a second type.
            int fields = 0;
            for (QLineEdit* le : dlg.findChildren<QLineEdit*>())
                if (le->isVisible() && le->placeholderText().contains(QStringLiteral("20-16"))) ++fields;
            // The pin's gender: male or female, from a menu.
            for (QComboBox* g : dlg.findChildren<QComboBox*>())
                if (g->isVisible() && g->findData(QStringLiteral("male")) >= 0 && g->findData(QStringLiteral("female")) >= 0)
                    ++fields;
            QPushButton* another = nullptr;
            for (QPushButton* b : dlg.findChildren<QPushButton*>())
                if (b->text().contains(QStringLiteral("Another type of pin"))) another = b;
            std::printf("  with pins: %d field(s) for the pins, add-another %s\n", fields,
                        another && another->isVisible() ? "offered" : "missing");
            check(fields >= 2, "with pins: a row to say what the pins are");
            check(another != nullptr && another->isVisible(),
                  "with pins: and a way to add a second size in the same housing");
            tick->setChecked(false);
            QApplication::processEvents();
            check(contact->isVisible(), "with pins: unticked again, the contact menu comes back");
        }
        QFile::remove(libFile);
    }


    // ---- the shelf, by kind, and what a double-click puts down ---------------------------------------
    {
        // A real library is mostly heat-shrink sizes and marker sleeves - a hundred rows of which two are
        // the connectors somebody wants - so the shelf is grouped by what a part IS, and one kind can be
        // asked for on its own. And what a double-click on a part's NAME puts down is said once, under the
        // shelf, rather than guessed at: "the mechanical drawing if it has one" is right on an assembly
        // sheet and wrong all day on a schematic.
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_shelf_lib.json"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            for (int i = 0; i < 3; ++i) {
                LibraryPart hs;
                hs.id = QStringLiteral("HS-%1").arg(i);
                hs.kind = PartKind::Heatshrink;
                hs.description = QStringLiteral("Sleeve %1").arg(i);
                lib.parts.push_back(hs);
            }
            LibraryPart conn;
            conn.id = QStringLiteral("CN-1");
            conn.kind = PartKind::Connector;
            conn.description = QStringLiteral("The connector");
            conn.params[QStringLiteral("ways")] = QStringLiteral("2");
            Entity body;
            body.type = EntityType::Rect;
            body.points = {{0.0, 0.0}, {10.0, 0.0}, {10.0, 6.0}, {0.0, 6.0}};
            conn.mechanical = partDrawingFromEntities({body});
            conn.schematic = partDrawingFromEntities({body});
            lib.parts.push_back(conn);
            lib.save(libFile);
        }
        PartsCatalog cat;
        cat.openRemembered();
        cat.connectLibrary(libFile);
        cat.remember();

        MainWindow win;
        win.resize(1280, 860);
        win.show();
        QApplication::processEvents();
        auto* tree = win.findChild<QTreeWidget*>();
        auto* ed = win.findChild<Editor*>();
        check(tree != nullptr && ed != nullptr, "shelf: there is a parts panel");
        QComboBox* kinds = nullptr;
        for (QComboBox* c : win.findChildren<QComboBox*>())
            if (c->count() > 0 && c->itemText(0) == QStringLiteral("All kinds")) kinds = c;
        check(kinds != nullptr, "shelf: and a way to ask for one kind of part");
        if (tree && kinds) {
            QStringList heads;
            // The kind headings sit UNDER the library headings when more than one shelf is in view, so
            // they are looked for wherever they are rather than only at the top.
            for (QTreeWidgetItem* it : everyRow(tree))
                if (it->data(0, Qt::UserRole).toString().isEmpty()) heads << it->text(0);
            std::printf("  shelf: headings - %s\n", qPrintable(heads.join(QStringLiteral(" | "))));
            bool grouped = false;
            for (const QString& h : heads)
                if (h.startsWith(QStringLiteral("Heatshrink")) || h.contains(QStringLiteral("(3)")))
                    grouped = true;
            check(grouped, "shelf: the parts are grouped under a heading per kind, with a count on it");
            std::printf("  shelf: %d kind(s) offered in the menu\n", kinds->count());
            check(kinds->count() >= 3, "shelf: every kind the library holds is offered, and All kinds");
            // Narrowed to connectors, the sleeves are not in view.
            const int at = kinds->findText(partKindName(PartKind::Connector));
            check(at > 0, "shelf: Connector is one of them");
            if (at > 0) {
                kinds->setCurrentIndex(at);
                QApplication::processEvents();
                // Counted by KIND rather than by row: the office's own library is connected too and holds
                // connectors of its own, so what matters is that the SLEEVES are not in the way.
                int connectors = 0, sleeves = 0;
                for (QTreeWidgetItem* it : everyRow(tree)) {
                    if (it->data(0, Qt::UserRole).toString().isEmpty()) continue;
                    if (it->text(0).startsWith(partKindName(PartKind::Connector))) ++connectors;
                    if (it->text(0).startsWith(partKindName(PartKind::Heatshrink))) ++sleeves;
                }
                std::printf("  shelf: asked for connectors - %d connector(s), %d sleeve(s)\n",
                            connectors, sleeves);
                check(connectors >= 1 && sleeves == 0,
                      "shelf: asked for one kind, the rest are not in the way");
            }
        }

        // WHAT A DOUBLE-CLICK PLACES: the ticks say, and they can say more than one.
        std::vector<QCheckBox*> ticks;
        for (QCheckBox* b : win.findChildren<QCheckBox*>())
            // A fanout is asked for by the SIDE it shows now - front or back - rather than by how the second
            // one was made.
            if (b->text() == QStringLiteral("mechanical") || b->text() == QStringLiteral("schematic") ||
                b->text() == QStringLiteral("fanout front") || b->text() == QStringLiteral("fanout back"))
                ticks.push_back(b);
        std::printf("  shelf: %lld tick(s) for what a double-click places\n",
                    static_cast<long long>(ticks.size()));
        check(ticks.size() == 4, "shelf: one tick per drawing a part can have");
        if (ed && ticks.size() == 4) {
            for (QCheckBox* b : ticks) b->setChecked(b->text() == QStringLiteral("mechanical") ||
                                                     b->text() == QStringLiteral("schematic"));
            QApplication::processEvents();
            win.placeFromPartsPanel(QStringLiteral("CN-1"), true);
            std::printf("  shelf: mechanical + schematic ticked put %d object(s) on the cursor\n",
                        ed->clipboardCount());
            check(ed->clipboardCount() >= 2,
                  "shelf: both ticked drawings go down together, in one placement");
            ed->onKey(Qt::Key_Escape, Qt::NoModifier);
        }
        {
            PartsCatalog put;
            put.openRemembered();
            put.disconnectLibrary(libFile);
            put.remember();
        }
        QFile::remove(libFile);
    }


    // ---- the technical parameters line up ------------------------------------------------------------
    {
        // A form is read down its columns - a figure against a field - and the fields were ragged: each had a
        // MAXIMUM width and a stretch shared with the gap behind it, which is not a width at all, so a combo
        // box sat at one size and the box under it at another and the eye did the aligning.
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_align_lib.json"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            LibraryPart wire;
            wire.id = QStringLiteral("W-1");
            wire.kind = PartKind::Wire;
            wire.description = QStringLiteral("A wire with every kind of field");
            lib.parts.push_back(wire);
            lib.save(libFile);
        }
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        PartsLibraryDialog dlg(&ed);
        dlg.setLibraryPath(libFile);
        dlg.selectPart(QStringLiteral("W-1"));
        dlg.resize(1500, 900);
        dlg.show();
        QApplication::processEvents();

        // THE FIELD OF EACH ROW, which is the thing that has to line up: a row is [label][field][unit], and
        // what the eye follows down the form is the left-hand edge of the middle one. Measured on the row
        // rather than on every box, because a field can BE two boxes - a range is a from and a to, a material
        // is a menu with a cross beside it - and those halves have every reason to differ inside their row.
        QSet<int> widths;
        int fields = 0;
        QWidget* form = dlg.findChild<QWidget*>(QStringLiteral("paramHost"));
        check(form != nullptr, "align: the parameter form is there");
        if (form && form->layout()) {
            for (int i = 0; i < form->layout()->count(); ++i) {
                QWidget* row = form->layout()->itemAt(i)->widget();
                if (!row || !row->layout() || row->layout()->count() < 3) continue;
                QWidget* field = row->layout()->itemAt(1)->widget();
                if (!field || !field->isVisible()) continue;
                // A field that carries something BESIDE it - a clear cross, the two halves of a range - is
                // wider than the box by that much on purpose: the box is what has to line up, so that the
                // menus and the plain boxes read as one column and the crosses hang off the end.
                if (field->layout() && field->layout()->count() > 1)
                    if (QWidget* inner = field->layout()->itemAt(0)->widget()) field = inner;
                ++fields;
                widths.insert(field->width());
            }
        }
        QStringList said;
        for (int w : widths) said << QString::number(w);
        said.sort();
        std::printf("  align: %d row(s) on the form, field width(s) %s\n", fields,
                    qPrintable(said.join(QStringLiteral(", "))));
        check(fields >= 3, "align: the form has rows on it");
        check(widths.size() == 1, "align: and every field is the same width");
    }


    // ---- the pin rows say what their fields are, and the pin table is seeded from them --------------
    {
        // FIVE BOXES IN A ROW WITH NOTHING OVER THEM is a row somebody works out by typing in it and seeing
        // what happens - and the two in the middle ("20", "AWG") read as one figure until you know the first
        // is the pin's own size and the second the conductor it takes.
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_pintbl_lib.json"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            LibraryPart part;
            part.id = QStringLiteral("P-TBL");
            part.kind = PartKind::Connector;
            part.description = QStringLiteral("Two-way with its pins");
            part.params[QStringLiteral("ways")] = QStringLiteral("2");
            setPartIncludesPins(part, true);
            std::vector<PartPinType> types;
            PartPinType t;
            t.size = QStringLiteral("16");
            t.unit = QStringLiteral("AWG");
            t.gauge = QStringLiteral("20");
            t.amps = QStringLiteral("7");
            types.push_back(t);
            setPartPinTypes(part, types);
            lib.parts.push_back(part);
            lib.save(libFile);
        }
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        PartsLibraryDialog dlg(&ed);
        dlg.setLibraryPath(libFile);
        dlg.selectPart(QStringLiteral("P-TBL"));
        dlg.resize(1500, 900);
        dlg.show();
        QApplication::processEvents();

        // Inside the pin rows' own box, and DISTINCT: the parameter form has a "Conductor" of its own, and a
        // widget waiting to be deleted after a rebuild is still a child for a moment - counting either would
        // report a heading twice and call it a fault.
        QSet<QString> heads;
        QWidget* pinBox = dlg.findChild<QWidget*>(QStringLiteral("pinTypes"));
        check(pinBox != nullptr, "pin heads: the pin rows have a box of their own");
        if (pinBox)
            for (QLabel* l : pinBox->findChildren<QLabel*>())
                if (l->isVisible() && !l->text().isEmpty()) heads.insert(l->text());
        QStringList said(heads.begin(), heads.end());
        said.sort();
        std::printf("  pin heads: %s\n", qPrintable(said.join(QStringLiteral(" | "))));
        // In the words a datasheet uses: what the column holds is the pin's CROSS SECTION, and the unit box
        // needs no heading because it says AWG or mm2 on its own face.
        check(heads.contains(QStringLiteral("Gender")) &&
                  heads.contains(QStringLiteral("Cross section")) &&
                  heads.contains(QStringLiteral("Current (A)")) &&
                  heads.contains(QStringLiteral("Pins")),
              "pin heads: every field of a pin row is named over it");

        // AND THE PIN TABLE IS SEEDED FROM THEM. A symbol BUILT from the data is labelled with the pin sizes;
        // a symbol CAPTURED off the sheet is not, and neither is one built before those rows were filled in -
        // so the table opened blank over a part whose datasheet the library was holding all along.
        const LibraryPart* p = nullptr;
        const PartsLibrary lib = dlg.library();
        for (const LibraryPart& one : lib.parts)
            if (one.id == QStringLiteral("P-TBL")) p = &one;
        check(p != nullptr, "pin table: the part is in the library");
        if (p) {
            const QStringList gauges = partPinGauges(*p);
            const QStringList amps = partPinAmps(*p);
            std::printf("  pin table: the library says %s / %s A for pin 1\n",
                        qPrintable(gauges.value(0)), qPrintable(amps.value(0)));
            check(gauges.value(0) == QStringLiteral("20 AWG") && gauges.value(1) == QStringLiteral("20 AWG"),
                  "pin table: both pins take the conductor the row names");
            check(amps.value(0) == QStringLiteral("7") && amps.value(1) == QStringLiteral("7"),
                  "pin table: and carry what it says they carry");
            // Which is the form the dialog's own gauge list uses ("20 AWG"), so the value lands ON a list
            // entry rather than as something typed that looks like one.
            check(gauges.value(0).endsWith(QStringLiteral(" AWG")),
                  "pin table: in the same words the table's own menu offers, so it matches an entry");
        }
        QFile::remove(libFile);
    }


    // ---- the Place menu stays open, the fields still line up, and the bill agrees with itself -------
    {
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_four_lib.json"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            LibraryPart part;
            part.id = QStringLiteral("P-FOUR");
            part.kind = PartKind::Connector;
            part.description = QStringLiteral("Four-item connector");
            part.mfrPn = QStringLiteral("FOUR-1");
            part.params[QStringLiteral("ways")] = QStringLiteral("2");
            Entity body;
            body.type = EntityType::Rect;
            body.points = {{0.0, 0.0}, {10.0, 0.0}, {10.0, 6.0}, {0.0, 6.0}};
            part.mechanical = partDrawingFromEntities({body});
            part.schematic = partDrawingFromEntities({body});
            lib.parts.push_back(part);
            lib.save(libFile);
        }

        // THE MENU STAYS OPEN WHILE THE TICKS ARE MADE. A menu closes on the first thing clicked in it, so
        // choosing "the connector AND its fanout" was two choices of one thing rather than one choice of two.
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        PartsLibraryDialog dlg(&ed);
        dlg.setLibraryPath(libFile);
        dlg.selectPart(QStringLiteral("P-FOUR"));
        dlg.resize(1500, 900);
        dlg.show();
        QApplication::processEvents();
        QToolButton* place = nullptr;
        for (QToolButton* b : dlg.findChildren<QToolButton*>())
            if (b->text() == QStringLiteral("Place")) place = b;
        check(place != nullptr && place->menu() != nullptr, "apply: the Place button has its menu");
        if (place && place->menu()) {
            QMenu* pm = place->menu();
            // One item, and it is a PANEL: a widget in a menu takes its own clicks, which is what lets the
            // ticks be made without the menu going.
            int widgetActions = 0, plainActions = 0;
            for (QAction* a : pm->actions()) {
                if (qobject_cast<QWidgetAction*>(a)) ++widgetActions;
                else if (!a->isSeparator()) ++plainActions;
            }
            std::printf("  apply: %d panel(s) in the menu, %d plain item(s)\n", widgetActions, plainActions);
            check(widgetActions == 1 && plainActions == 0,
                  "apply: the ticks are in a panel, so clicking one does not close the menu");
            QWidget* panel = qobject_cast<QWidgetAction*>(pm->actions().first())->defaultWidget();
            check(panel != nullptr, "apply: the panel is there");
            if (panel) {
                QStringList said;
                for (QCheckBox* b : panel->findChildren<QCheckBox*>()) said << b->text();
                QPushButton* apply = nullptr;
                for (QPushButton* b : panel->findChildren<QPushButton*>())
                    if (b->text() == QStringLiteral("Apply")) apply = b;
                std::printf("  apply: ticks %s; Apply %s\n", qPrintable(said.join(QStringLiteral(", "))),
                            apply ? "there" : "missing");
                check(said.size() == 4, "apply: one tick per drawing a part can have");
                check(apply != nullptr, "apply: and an Apply to finish on");
                // Two ticked without the menu closing, then Apply: both go down.
                for (QCheckBox* b : panel->findChildren<QCheckBox*>())
                    b->setChecked(b->text().startsWith(QStringLiteral("Mechanical")) ||
                                  b->text().startsWith(QStringLiteral("Schematic")));
                QApplication::processEvents();
                if (apply) apply->click();
                QApplication::processEvents();
                std::printf("  apply: %d object(s) placed by one Apply\n", ed.clipboardCount());
                check(ed.clipboardCount() >= 2, "apply: what was ticked goes down on Apply, all of it");
                ed.onKey(Qt::Key_Escape, Qt::NoModifier);
            }
        }


        QFile::remove(libFile);
    }

    // ---- the bill of materials: one list, two windows, and a way back --------------------------------
    {
        // Every edit in the bill's window writes into the drawing as it is made, which is what keeps the side
        // panel in step - and it left no way out of a change made by accident: a unit picked from a menu on
        // the wrong row was the drawing's answer from then on.
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_bomtwo_lib.json"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            LibraryPart part;
            part.id = QStringLiteral("P-BOM");
            part.kind = PartKind::Heatshrink;
            part.description = QStringLiteral("A sleeve to order");
            part.mfrPn = QStringLiteral("BOM-1");
            lib.parts.push_back(part);
            lib.save(libFile);
        }
        PartsCatalog cat;
        cat.openRemembered();
        cat.connectLibrary(libFile);
        cat.remember();

        MainWindow win;
        win.resize(1300, 860);
        win.show();
        QApplication::processEvents();
        Document* doc = win.findChild<Editor*>() ? win.findChild<Editor*>()->document() : nullptr;
        check(doc != nullptr, "bom two: there is a drawing");
        if (doc) {
            auto lines = [doc] {
                BomTable t;
                t.fromJson(doc->bom());
                return static_cast<int>(t.rows.size());
            };
            const int before = lines();
            // ADDED FROM THE PANEL, with the window open: it has to appear in both.
            win.openBomWindowForTest();
            QApplication::processEvents();
            auto* bom = win.findChild<BomDialog*>();
            check(bom != nullptr, "bom two: the bill's window is open");
            win.addPartToBom(QStringLiteral("P-BOM"));
            QApplication::processEvents();
            std::printf("  bom two: %d line(s) before, %d after adding from the panel\n", before, lines());
            check(lines() == before + 1, "bom two: the line is in the drawing's bill");
            if (bom) {
                check(bom->shownRowCount() >= 1,
                      "bom two: and in the window that was already open - one list, not two");
                // AND A WAY BACK. Cancel puts the bill back as it was when the window opened.
                bom->cancelChanges();
                QApplication::processEvents();
                std::printf("  bom two: after Cancel, %d line(s)\n", lines());
                check(lines() == before,
                      "bom two: Cancel puts the bill back as it was when the window opened");
            }
        }
        {
            PartsCatalog put;
            put.openRemembered();
            put.disconnectLibrary(libFile);
            put.remember();
        }
        QFile::remove(libFile);
    }


    // ---- front and back, and a pin table that writes back to the library ----------------------------
    {
        // FRONT AND BACK, not "mirrored". Which side of the connector a fanout is drawn from is what it IS -
        // the same drawing seen from the face or from the wires - and "mirrored" says how the second one was
        // made rather than what it shows.
        MainWindow win;
        win.resize(1300, 860);
        win.show();
        QApplication::processEvents();
        QStringList ticks;
        for (QCheckBox* b : win.findChildren<QCheckBox*>())
            if (b->text().contains(QStringLiteral("fanout"))) ticks << b->text();
        ticks.sort();
        std::printf("  sides: the shelf offers %s\n", qPrintable(ticks.join(QStringLiteral(" | "))));
        check(ticks.contains(QStringLiteral("fanout front")) &&
                  ticks.contains(QStringLiteral("fanout back")),
              "sides: a fanout is asked for by the side it shows, not by how it was made");
        check(!ticks.contains(QStringLiteral("fanout mirrored")),
              "sides: and \"mirrored\" is not one of the words");
    }

    // ---- a correction on the drawing reaches the library --------------------------------------------
    {
        // The figures went one way only: the library filled the pin table's empty cells, and a correction
        // made on the drawing stayed on that drawing - so the next connector placed from the same part came
        // down with the old ones, and the two disagreed with nothing to say which was right.
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_back_lib.json"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            LibraryPart part;
            part.id = QStringLiteral("P-BACK");
            part.kind = PartKind::Connector;
            part.description = QStringLiteral("Two-way, pins fitted");
            part.params[QStringLiteral("ways")] = QStringLiteral("2");
            setPartIncludesPins(part, true);
            std::vector<PartPinType> types;
            PartPinType t;
            t.size = QStringLiteral("size 1");
            t.unit = QStringLiteral("AWG");
            t.gauge = QStringLiteral("22");
            t.amps = QStringLiteral("5");
            types.push_back(t);
            setPartPinTypes(part, types);
            lib.parts.push_back(part);
            lib.save(libFile);
        }
        PartsCatalog cat;
        cat.openRemembered();
        cat.connectLibrary(libFile);
        cat.remember();

        MainWindow win;
        win.resize(1300, 860);
        win.show();
        QApplication::processEvents();
        auto* ed = win.findChild<Editor*>();
        check(ed != nullptr && ed->document() != nullptr, "back: there is a drawing");
        if (ed && ed->document()) {
            // A connector on the sheet, and the pin table's answer written back: pin 1 corrected to 20 AWG
            // at 7 A, pin 2 left as the library had it.
            Entity conn;
            conn.id = QStringLiteral("X1");
            conn.type = EntityType::Connector;
            conn.a = {0.0, 0.0};
            conn.meta.insert(QStringLiteral("pinCount"), 2);
            conn.meta.insert(QStringLiteral("libPart"), QStringLiteral("P-BACK"));
            QJsonArray gauges{QStringLiteral("20 AWG"), QStringLiteral("22 AWG")};
            QJsonArray amps{QStringLiteral("7"), QStringLiteral("5")};
            conn.meta.insert(QStringLiteral("pinGauge"), gauges);
            conn.meta.insert(QStringLiteral("pinAmp"), amps);
            ed->document()->entities().push_back(conn);
            win.writePinTableBackToLibrary(QStringLiteral("X1"));
            QApplication::processEvents();

            PartsLibrary back;
            check(back.load(libFile), "back: the library file is readable");
            const LibraryPart* p = back.byId(QStringLiteral("P-BACK"));
            check(p != nullptr, "back: the part is still there");
            if (p) {
                const std::vector<PartPinType> types = partPinTypes(*p);
                QStringList said;
                for (const PartPinType& t : types)
                    said << QStringLiteral("%1: %2 %3 @%4A [%5]")
                                .arg(t.size, t.gauge, t.unit, t.amps, t.pins);
                std::printf("  back: %s\n", qPrintable(said.join(QStringLiteral(" | "))));
                check(types.size() == 2,
                      "back: two sizes of pin now, because the table says the two pins differ");
                bool has20 = false, has22 = false;
                for (const PartPinType& t : types) {
                    if (t.gauge == QStringLiteral("20") && t.amps == QStringLiteral("7") &&
                        t.pins == QStringLiteral("1"))
                        has20 = true;
                    if (t.gauge == QStringLiteral("22") && t.amps == QStringLiteral("5") &&
                        t.pins == QStringLiteral("2"))
                        has22 = true;
                }
                check(has20, "back: the correction made on the drawing is in the library, against pin 1");
                check(has22, "back: and what was already right is still right, against pin 2");
                for (const PartPinType& t : types)
                    if (t.gauge == QStringLiteral("22"))
                        check(t.size == QStringLiteral("size 1"),
                              "back: a row that already described that pair keeps its own name for it");
            }
        }
        {
            PartsCatalog put;
            put.openRemembered();
            put.disconnectLibrary(libFile);
            put.remember();
        }
        QFile::remove(libFile);
    }


    // ---- the bill shows the columns the configuration asks for ---------------------------------------
    {
        // A column switched off in Options was still in this window and still in the side panel, so the list
        // being worked on and the list that would be printed were two different lists - and the only way to
        // find out what the drawing would say was to place the table and look at it.
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BomDialog dlg(&ed);
        dlg.addManualRow();
        dlg.addManualRow();
        // THE BILL's table, not the library list beside it - both are tables, and the list is found first.
        QTableWidget* view = nullptr;
        for (QTableWidget* t : dlg.findChildren<QTableWidget*>())
            if (t->columnCount() == bomColumnCount()) view = t;
        check(view != nullptr, "cols: the bill has a table");
        if (view) {
            BomConfig cfg = dlg.config();
            for (int i = 0; i < bomColumnCount(); ++i) cfg.setColumnShown(i, true);
            dlg.setConfig(cfg);
            QApplication::processEvents();
            int on = 0;
            for (int c = 0; c < view->columnCount(); ++c)
                if (!view->isColumnHidden(c)) ++on;
            check(on == bomColumnCount(), "cols: with every column asked for, every column is there");

            // The internal part number switched off - the one column a shop that orders by the maker's
            // number has no use for.
            cfg.setColumnShown(2, false);
            cfg.setColumnShown(4, false);
            dlg.setConfig(cfg);
            QApplication::processEvents();
            QStringList still;
            for (int c = 0; c < view->columnCount(); ++c)
                if (!view->isColumnHidden(c)) still << QString::number(c);
            std::printf("  cols: showing %s of %d\n", qPrintable(still.join(QStringLiteral(","))),
                        bomColumnCount());
            check(view->isColumnHidden(2), "cols: a column switched off is not in the window either");
            check(view->isColumnHidden(4), "cols: nor the second one");
            check(!view->isColumnHidden(1) && !view->isColumnHidden(5),
                  "cols: and the ones still asked for are still there");
        }
    }

    // ---- and so does the side panel -----------------------------------------------------------------
    {
        // Two views of one list. A column left off the sheet has no business in either of them.
        MainWindow win;
        win.resize(1300, 860);
        win.show();
        QApplication::processEvents();
        auto* ed = win.findChild<Editor*>();
        check(ed != nullptr && ed->document() != nullptr, "panelcols: there is a drawing");
        QTableWidget* panel = nullptr;
        for (QTableWidget* t : win.findChildren<QTableWidget*>())
            if (t->columnCount() == 6 && t->horizontalHeaderItem(0) &&
                t->horizontalHeaderItem(0)->text() == QStringLiteral("#"))
                panel = t;
        check(panel != nullptr, "panelcols: the side panel is there");
        if (panel && ed && ed->document()) {
            BomTable table;
            table.fromJson(ed->document()->bom());
            // Manufacturer (column 4) switched off: a shop with one supplier does not need it.
            table.config.setColumnShown(4, false);
            ed->document()->bom() = table.toJson();
            win.refreshBomPanelForTest();
            QApplication::processEvents();
            std::printf("  panelcols: manufacturer column hidden = %s\n",
                        panel->isColumnHidden(3) ? "yes" : "no");
            check(panel->isColumnHidden(3),
                  "panelcols: the column switched off is gone from the panel too");
            check(!panel->isColumnHidden(1),
                  "panelcols: and the description, which every line is read by, stays");
        }
    }

    // ---- a connector that comes with its wires: a tick, and a row per pin saying what each wire is ----
    {
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_wired_lib.json"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            LibraryPart part;
            part.id = QStringLiteral("P-WIRED");
            part.kind = PartKind::Connector;
            part.description = QStringLiteral("Four-way lead");
            part.params[QStringLiteral("pins")] = QStringLiteral("4");
            lib.parts.push_back(part);
            LibraryPart other;
            other.id = QStringLiteral("P-HOSE");
            other.kind = PartKind::Heatshrink;
            other.description = QStringLiteral("Not a connector");
            lib.parts.push_back(other);
            lib.save(libFile);
        }
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        PartsLibraryDialog dlg(&ed);
        dlg.setLibraryPath(libFile);
        dlg.selectPart(QStringLiteral("P-WIRED"));
        dlg.show();
        QApplication::processEvents();
        auto* group = dlg.findChild<QGroupBox*>(QStringLiteral("wiresGroup"));
        auto* wired = dlg.findChild<QCheckBox*>(QStringLiteral("wiredBox"));
        auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("wiresTable"));
        check(group && wired && table && group->isVisibleTo(&dlg) && !wired->isChecked() && !table->isVisibleTo(&dlg),
              "wired: a connector offers the tick, and the wires stay hidden until it is ticked");
        if (group && wired && table) {
            wired->setChecked(true);
            QApplication::processEvents();
            check(table->isVisibleTo(&dlg) && table->rowCount() == 4,
                  "wired: ticked, the wires open - a row for each of its four pins");
            // The "same for all" button sits on the tick's own line.
            auto* same = dlg.findChild<QPushButton*>(QStringLiteral("wiresSameAll"));
            check(same && same->isVisibleTo(&dlg) && std::abs(same->mapTo(&dlg, QPoint(0, 0)).y() -
                                                              wired->mapTo(&dlg, QPoint(0, 0)).y()) < 12,
                  "wired: \"Same as pin 1 for all\" is on the line with the tick");
            check(table->columnCount() == 6 && table->item(1, 1) && table->item(1, 5),
                  "wired: colour, size, unit, insulation, diameter");
            if (table->item(1, 1) && table->item(3, 1)) {
                // LIKE A SPREADSHEET: pins 2 to 4 picked, and a colour given to one of them goes to all three.
                table->clearSelection();
                table->setRangeSelected(QTableWidgetSelectionRange(1, 1, 3, 1), true);
                table->item(1, 1)->setText(QStringLiteral("Red"));
                QApplication::processEvents();
                check(table->item(2, 1)->text() == QStringLiteral("Red") && table->item(3, 1)->text() == QStringLiteral("Red") &&
                          table->item(0, 1)->text().isEmpty(),
                      "wired: a value given to one picked cell goes to every picked cell of its column");
                table->clearSelection();
                table->item(1, 2)->setText(QStringLiteral("0.5"));
                table->item(1, 3)->setData(Qt::UserRole, QStringLiteral("mm2"));
                table->item(1, 3)->setText(QStringLiteral("mm²"));
                table->item(1, 4)->setText(QStringLiteral("PTFE"));
                table->item(1, 5)->setText(QStringLiteral("1.3"));
                QApplication::processEvents();
            }
            check(dlg.saveLibrary(), "wired: the part saves");
            PartsLibrary again;
            check(again.load(libFile), "wired: and reads back");
            const LibraryPart* p = again.byId(QStringLiteral("P-WIRED"));
            if (p) {
                const std::vector<PartPinWire> wires = partPinWires(*p);
                check(partIsWired(*p) && wires.size() == 4, "wired: it is a wired connector with four pins");
                if (wires.size() == 4) {
                    const PartPinWire& w = wires[1];
                    std::printf("  wired: pin %d %s %s %s %s %s\n", w.pin, qPrintable(w.color), qPrintable(w.size),
                                qPrintable(w.unit), qPrintable(w.insulation), qPrintable(w.od));
                    check(w.pin == 2 && w.color == QStringLiteral("Red") && w.size == QStringLiteral("0.5") &&
                              w.unit == QStringLiteral("mm2") && w.insulation == QStringLiteral("PTFE") &&
                              w.od == QStringLiteral("1.3") && wires[0].empty() &&
                              wires[3].color == QStringLiteral("Red"),
                          "wired: pin 2's wire is what was entered, pin 4 got the shared colour, pin 1 is blank");
                }
            } else {
                check(false, "wired: the part is in the library");
            }
            // THE SECTIONS ARE PULLED TALLER BY THE GRIP UNDER EACH, and what comes after moves with it.
            {
                QWidget* grip = nullptr;
                for (QWidget* g : dlg.findChildren<QWidget*>())
                    if (g->objectName().startsWith(QStringLiteral("partSectionGrip")) && g->isVisibleTo(&dlg) &&
                        g->mapTo(&dlg, QPoint(0, 0)).y() > group->mapTo(&dlg, QPoint(0, 0)).y() &&
                        (!grip || g->mapTo(&dlg, QPoint(0, 0)).y() < grip->mapTo(&dlg, QPoint(0, 0)).y()))
                        grip = g;   // the one just under the Wires section
                QWidget* after = nullptr;
                for (QGroupBox* b : dlg.findChildren<QGroupBox*>())
                    if (b->title() == QStringLiteral("Drawings")) after = b;
                check(grip && after, "sections: a grip under the Wires section, and a section after it");
                if (grip && after) {
                    const int wasAt = after->mapTo(&dlg, QPoint(0, 0)).y();
                    const QPointF g(grip->width() / 2.0, 3.0);
                    QMouseEvent press(QEvent::MouseButtonPress, g, grip->mapToGlobal(g), Qt::LeftButton, Qt::LeftButton,
                                      Qt::NoModifier);
                    QApplication::sendEvent(grip, &press);
                    const QPointF to = g + QPointF(0, 60);
                    QMouseEvent move(QEvent::MouseMove, to, grip->mapToGlobal(to), Qt::NoButton, Qt::LeftButton,
                                     Qt::NoModifier);
                    QApplication::sendEvent(grip, &move);
                    QApplication::processEvents();
                    const int nowAt = after->mapTo(&dlg, QPoint(0, 0)).y();
                    std::printf("  sections: pulled 60 taller, the next moved %d\n", nowAt - wasAt);
                    check(std::abs(nowAt - wasAt - 60) <= 2, "sections: pulled taller, the rest are pushed down");
                }
            }

            dlg.selectPart(QStringLiteral("P-HOSE"));
            QApplication::processEvents();
            check(!group->isVisibleTo(&dlg), "wired: something other than a connector does not offer it");
        }
        QFile::remove(libFile);
    }

    // ---- front and back are two drawings, and the one showing is the part's -------------------------
    {
        // The pane worked on the front and nothing else: the side box only recorded a word, so the back of a
        // connector - which is not the front held up to a mirror, the wires being numbered the other way
        // about - could be neither drawn nor looked at from here.
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_sides_lib.json"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            LibraryPart part;
            part.id = QStringLiteral("P-SIDES");
            part.kind = PartKind::Connector;
            part.description = QStringLiteral("Two-way, drawn both sides");
            part.params[QStringLiteral("ways")] = QStringLiteral("2");
            // Two DIFFERENT drawings: one line for the front, three for the back.
            std::vector<Entity> front;
            Entity a;
            a.type = EntityType::Line;
            a.a = {0.0, 0.0};
            a.b = {10.0, 0.0};
            front.push_back(a);
            std::vector<Entity> back;
            for (int i = 0; i < 3; ++i) {
                Entity e;
                e.type = EntityType::Line;
                e.a = {0.0, static_cast<double>(i)};
                e.b = {10.0, static_cast<double>(i)};
                back.push_back(e);
            }
            part.fanout = partDrawingFromEntities(front);
            part.fanoutOpposite = partDrawingFromEntities(back);
            lib.parts.push_back(part);
            lib.save(libFile);
        }
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        PartsLibraryDialog dlg(&ed);
        dlg.setLibraryPath(libFile);
        dlg.selectPart(QStringLiteral("P-SIDES"));
        dlg.resize(1500, 900);
        dlg.show();
        QApplication::processEvents();

        auto* pane = dlg.findChild<DrawingPreview*>(QStringLiteral("fanoutPane"));
        check(pane != nullptr, "sides: the fanout pane is there");
        QComboBox* side = nullptr;
        for (QComboBox* b : dlg.findChildren<QComboBox*>())
            if (b->findData(QStringLiteral("back")) >= 0 && b->findData(QStringLiteral("front")) >= 0)
                side = b;
        check(side != nullptr, "sides: and a box to pick the side with");
        if (pane && side) {
            std::printf("  sides: opens on %s with %d object(s)\n", qPrintable(pane->caption()),
                        static_cast<int>(pane->entities().size()));
            check(pane->caption() == QStringLiteral("Fanout front"),
                  "sides: it opens on the part's own side, and says which that is");
            check(pane->entities().size() == 1, "sides: showing the front's own drawing");

            side->setCurrentIndex(side->findData(QStringLiteral("back")));
            QApplication::processEvents();
            std::printf("  sides: switched to %s with %d object(s)\n", qPrintable(pane->caption()),
                        static_cast<int>(pane->entities().size()));
            check(pane->caption() == QStringLiteral("Fanout back"),
                  "sides: picking the back shows the back, and says so");
            check(pane->entities().size() == 3,
                  "sides: which is its OWN drawing, not the front mirrored");
            check(dlg.showingBackFanout(), "sides: the window knows which side it is working on");

            // WHAT IS SELECTED IS THE PART'S: the side left showing is the one a plain fanout means.
            check(dlg.saveLibrary(), "sides: the part saves");
            PartsLibrary again;
            check(again.load(libFile), "sides: and reads back");
            const LibraryPart* p = again.byId(QStringLiteral("P-SIDES"));
            check(p != nullptr && p->fanoutSide() == QStringLiteral("back"),
                  "sides: the side chosen is the part's own from now on");
            if (p) {
                check(p->fanout.size() > 0 && p->fanoutOpposite.size() > 0,
                      "sides: and both drawings are still there - picking a side draws neither over");
                check(p->hasFanout(), "sides: a part with either side drawn has a fanout");
            }

            // Clearing acts on the side showing and leaves the other alone.
            for (QPushButton* b : dlg.findChildren<QPushButton*>()) {
                if (b->text() != QStringLiteral("Clear")) continue;
                if (!b->parentWidget() || !b->parentWidget()->parentWidget()) continue;
                if (b->parentWidget()->parentWidget() != pane->parentWidget()) continue;
                b->click();
                break;
            }
            QApplication::processEvents();
            check(pane->entities().empty(), "sides: Clear empties the side showing");
        }
        QFile::remove(libFile);
    }

    // ---- a part drawn only on the back still places one ---------------------------------------------
    {
        // "Has a fanout" asked about the front alone, so a connector drawn from the wires looked as though it
        // had none: no menu entry, no tick on the shelf, nothing to place.
        LibraryPart part;
        part.id = QStringLiteral("P-BACKONLY");
        part.kind = PartKind::Connector;
        std::vector<Entity> back;
        Entity e;
        e.type = EntityType::Line;
        e.a = {0.0, 0.0};
        e.b = {5.0, 0.0};
        back.push_back(e);
        part.fanoutOpposite = partDrawingFromEntities(back);
        part.setFanoutSide(QStringLiteral("back"));
        check(part.hasFanout(), "backonly: a fanout on either side is a fanout");
        check(!part.hasFanoutFront(), "backonly: and it is not on the front");
    }

    if (panelFailures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", panelFailures);
    return panelFailures;
}
