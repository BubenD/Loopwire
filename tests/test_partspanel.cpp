// The PARTS panel on the side of the window: the shelf, while drawing.
//
// A shelf that shows some of the stock is worse than no shelf. The panel listed only parts that already had
// a drawing - a mechanical view or a schematic symbol - which in a real library is a handful out of hundreds:
// a heat-shrink size has no drawing, nor has a contact, nor a length of wire, and none of them were there. So
// the shelf showed three parts of a hundred and looked broken rather than filtered.
//
// And a shop connects more than one library: the house library, a customer's, a project's. Which of them the
// panel is drawn from is the first thing to be able to say about it, so it is said with a tick per library.
#include "app/MainWindow.h"
#include "app/Editor.h"
#include "model/Block.h"
#include "harness/PartsLibrary.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QTableWidget>
#include <QCheckBox>
#include <QTreeWidget>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QToolButton>

#include <cstdio>

using namespace Edraw_Desktop;

namespace {

int panelFailures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++panelFailures;
    }
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

// Every row of the shelf, headings and parts alike - the tree is one level deep, a heading per library.
QList<QTreeWidgetItem*> everyRow(QTreeWidget* tree) {
    QList<QTreeWidgetItem*> out;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = tree->topLevelItem(i);
        out << top;
        for (int k = 0; k < top->childCount(); ++k) out << top->child(k);
    }
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
            bool place = false, addBom = false, edit = false, fanout = false, mirrored = false;
            for (const QString& s : items) {
                if (s.contains(QStringLiteral("mechanical"))) place = true;
                if (s.contains(QStringLiteral("bill of materials"))) addBom = true;
                if (s.contains(QStringLiteral("Edit in the library"))) edit = true;
                if (s.contains(QStringLiteral("fanout")) && !s.contains(QStringLiteral("mirrored")))
                    fanout = true;
                if (s.contains(QStringLiteral("mirrored"))) mirrored = true;
            }
            check(place, "menu: place the drawing");
            check(addBom, "menu: add it to the bill of materials");
            check(edit, "menu: edit it in the library");
            check(fanout && mirrored,
                  "menu: and the fanout for either side of the connector, since this part has one");
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
                if (bom->item(i, 0) && bom->item(i, 0)->text().contains(QStringLiteral("housing"))) row = i;
            check(row >= 0, "bom: and the line is the part that was added");
            if (row >= 0) {
                check(bom->item(row, 0)->data(Qt::UserRole + 1).toBool(),
                      "bom: marked as put there by hand, which is what makes it removable");
                win.removeBomPanelRow(row);
                QApplication::processEvents();
                check(bom->rowCount() == before, "bom: and it can be taken off again");
                bool stillThere = false;
                for (int i = 0; i < bom->rowCount(); ++i)
                    if (bom->item(i, 0) && bom->item(i, 0)->text().contains(QStringLiteral("housing")))
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
                if (bom->item(i, 0) && bom->item(i, 0)->text().contains(QStringLiteral("housing")))
                    twice = i;
            check(twice >= 0, "bom: a part added twice is one line, totalled");
            if (twice >= 0) {
                win.removeBomPanelRow(twice);
                QApplication::processEvents();
                bool leftOver = false;
                for (int i = 0; i < bom->rowCount(); ++i)
                    if (bom->item(i, 0) && bom->item(i, 0)->text().contains(QStringLiteral("housing")))
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
            if (bom->item(i, 0) && bom->item(i, 0)->text().contains(QStringLiteral("housing"))) line = i;
        std::printf("  rules: placed one - %d lines, the part on line %d\n", bom->rowCount(), line);
        check(bom->rowCount() == before + 1 && line >= 0,
              "rules: a part put on the sheet is on the bill of materials, without being asked twice");
        const QString qtyOne = line >= 0 && bom->item(line, 3) ? bom->item(line, 3)->text() : QString();

        // The same part again: one line, counted twice - not a second line.
        putOneDown();
        int lines = 0, only = -1;
        for (int i = 0; i < bom->rowCount(); ++i)
            if (bom->item(i, 0) && bom->item(i, 0)->text().contains(QStringLiteral("housing"))) {
                ++lines;
                only = i;
            }
        const QString qtyTwo = only >= 0 && bom->item(only, 3) ? bom->item(only, 3)->text() : QString();
        std::printf("  rules: placed a second - %d line(s), quantity %s -> %s\n", lines,
                    qPrintable(qtyOne), qPrintable(qtyTwo));
        check(lines == 1, "rules: the same part number twice is ONE line");
        check(qtyTwo != qtyOne, "rules: and its quantity went up by one");

        // Switched off, each is a line of its own again.
        unify->setChecked(false);
        QApplication::processEvents();
        int apart = 0;
        for (int i = 0; i < bom->rowCount(); ++i)
            if (bom->item(i, 0) && bom->item(i, 0)->text().contains(QStringLiteral("housing"))) ++apart;
        std::printf("  rules: not adding up - %d lines\n", apart);
        check(apart == 2, "rules: switched off, two of the same part are two lines");

        // And with placing switched off, the sheet is not counted at all.
        onPlace->setChecked(false);
        QApplication::processEvents();
        int counted = 0;
        for (int i = 0; i < bom->rowCount(); ++i)
            if (bom->item(i, 0) && bom->item(i, 0)->text().contains(QStringLiteral("housing"))) ++counted;
        check(counted == 0,
              "rules: with the switch off the bill lists only what was added to it by hand");
        // Left as they were found, so the rest of the run sees the ordinary rules.
        onPlace->setChecked(true);
        unify->setChecked(true);
        QApplication::processEvents();
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
    if (panelFailures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", panelFailures);
    return panelFailures;
}
