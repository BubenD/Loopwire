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
        dlg.resize(1600, 900);
        dlg.show();
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

    if (panelFailures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", panelFailures);
    return panelFailures;
}
