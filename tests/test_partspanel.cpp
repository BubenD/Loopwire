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
#include "harness/PartsLibrary.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
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

QListWidget* partsList(MainWindow& win) {
    for (QDockWidget* d : win.findChildren<QDockWidget*>())
        if (d->objectName() == QStringLiteral("panel_PARTS")) return d->findChild<QListWidget*>();
    return nullptr;
}

// The items that stand for a part (the library headings are not parts).
QStringList partRows(QListWidget* list) {
    QStringList out;
    for (int i = 0; i < list->count(); ++i)
        if (!list->item(i)->data(Qt::UserRole).toString().isEmpty()) out << list->item(i)->text();
    return out;
}

} // namespace

int run_partspanel_tests() {
    std::printf("parts panel tests:\n");

    const QString houseFile = writeLibrary(QStringLiteral("house"),
                                           {QStringLiteral("PANELTEST-HOUSE-1"), QStringLiteral("PANELTEST-HOUSE-2"),
                                            QStringLiteral("PANELTEST-HOUSE-3")});
    const QString jobFile =
        writeLibrary(QStringLiteral("job"), {QStringLiteral("PANELTEST-JOB-1"), QStringLiteral("PANELTEST-JOB-2")});

    // Both connected, and remembered - which is what the panel reads when it builds itself.
    {
        PartsCatalog cat;
        cat.connectLibrary(houseFile);
        cat.connectLibrary(jobFile);
        cat.remember();
        check(cat.paths().size() >= 2, "libraries: two libraries are connected");
    }

    MainWindow win;
    QListWidget* list = partsList(win);
    check(list != nullptr, "panel: the parts panel is a list");
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
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem* it = list->item(i);
        if (it->data(Qt::UserRole).toString().isEmpty()) continue;
        if (!it->data(Qt::UserRole + 2).toBool()) anyWithoutADrawing = true;
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
        cat.remember();
    }
    QFile::remove(houseFile);
    QFile::remove(jobFile);
    if (panelFailures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", panelFailures);
    return panelFailures;
}
