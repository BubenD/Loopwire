// The system design: units, ports and the cables between them, each cable's part number and revision checked against
// the drawing it is linked to; Find drawings; the System design tab.
#include "app/DrawingFinderDialog.h"
#include "app/MainWindow.h"
#include "app/SystemCanvas.h"
#include "app/SystemDesignView.h"
#include "io/Dwx.h"
#include "model/Document.h"
#include "system/SystemDesign.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QLineEdit>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {
int g_fail = 0;
void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  [FAIL] %s\n", what);
        ++g_fail;
    }
}

// A DRAWING ON DISK with a drawing number in its title block and revisions A and B in its revision table.
bool writeDrawing(const QString& path, const QString& number, const QStringList& revs) {
    Document doc(QStringLiteral("cable"));
    Entity no;
    no.type = EntityType::Text;
    no.meta.insert(QStringLiteral("text"), number);
    no.meta.insert(QStringLiteral("field"), QStringLiteral("drawingNo"));
    doc.entities().push_back(no);
    Entity title;
    title.type = EntityType::Text;
    title.meta.insert(QStringLiteral("text"), QStringLiteral("Cable from the controller to the sensor"));
    title.meta.insert(QStringLiteral("field"), QStringLiteral("title"));
    doc.entities().push_back(title);
    Entity table;
    table.type = EntityType::Rect;
    const int rows = 4, cols = 2;
    QJsonArray cells{QStringLiteral("REV"), QStringLiteral("NOTE")};
    for (int r = 1; r < rows; ++r) {
        cells.append(r - 1 < revs.size() ? revs.at(r - 1) : QString());
        cells.append(QString());
    }
    table.meta.insert(QStringLiteral("table"), true);
    table.meta.insert(QStringLiteral("rows"), rows);
    table.meta.insert(QStringLiteral("cols"), cols);
    table.meta.insert(QStringLiteral("cells"), cells);
    table.meta.insert(QStringLiteral("revisionTable"), true);
    doc.entities().push_back(table);
    return dwx::save(doc, path);
}

void mouse(QWidget* w, QEvent::Type type, const QPointF& at, Qt::MouseButtons buttons, Qt::MouseButton button) {
    QMouseEvent e(type, at, w->mapToGlobal(at), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
}

void click(QWidget* w, const QPointF& at) {
    mouse(w, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
    mouse(w, QEvent::MouseButtonRelease, at, Qt::NoButton, Qt::LeftButton);
}

void testModel() {
    SystemDesign d;
    const QString u1 = d.addUnit(0, 0), u2 = d.addUnit(300, 0);
    check(d.unit(u1)->name == QLatin1String("U1") && d.unit(u2)->name == QLatin1String("U2"), "model: units named U1, U2");
    const QString p1 = d.addPort(u1, QStringLiteral("right"), 0.5), p2 = d.addPort(u2, QStringLiteral("left"), 0.5);
    const QString p3 = d.addPort(u2, QStringLiteral("left"), 0.8);
    check(d.unit(u1)->port(p1)->name == QLatin1String("P1") && d.unit(u2)->port(p3)->name == QLatin1String("P2"),
          "model: ports named P1, P2 on each unit");
    const QString w1 = d.addCable(u1, p1, u2, p2);
    check(d.cable(w1) && d.cable(w1)->name == QLatin1String("W1"), "model: a cable named W1");
    check(d.addCable(u1, p1, u1, p1).isEmpty() && d.addCable(u1, QStringLiteral("nope"), u2, p2).isEmpty(),
          "model: no cable from a port to itself, or to a port that is not there");
    // Saved and read back; a cable to a port no longer there dropped.
    const SystemDesign back = SystemDesign::fromJson(d.toJson());
    check(back.units.size() == 2 && back.cables.size() == 1 && back.cable(w1)->aPort == p1, "model: saved and read back");
    QJsonObject broken = d.toJson();
    QJsonArray cs = broken.value(QStringLiteral("cables")).toArray();
    QJsonObject c0 = cs.at(0).toObject();
    c0[QStringLiteral("bPort")] = QStringLiteral("gone");
    cs[0] = c0;
    broken[QStringLiteral("cables")] = cs;
    check(SystemDesign::fromJson(broken).cables.empty(), "model: a cable to a port that is not there dropped on reading");
    // Numbered.
    const QString w2 = d.addCable(u1, p1, u2, p3);
    d.cable(w1)->pn = QStringLiteral("CBL-005");
    check(d.numberCables() == 1 && d.cable(w2)->pn == QLatin1String("CBL-006"), "numbering: the next free part number");
    // The check: P1 has two cables; W2 has no drawing.
    const std::vector<SysIssue> found = d.check();
    bool two = false, unlinked = false, noUnitPn = false;
    for (const SysIssue& i : found) {
        two = two || (i.level == 2 && i.what.contains(QLatin1String("2 cables")));
        unlinked = unlinked || i.what.contains(QLatin1String("no drawing linked"));
        noUnitPn = noUnitPn || i.what.contains(QLatin1String("U1 has no part number"));
    }
    check(two && unlinked && noUnitPn, "check: two cables in one port, a cable with no drawing, a unit with no part number");
    d.cable(w2)->pn = QStringLiteral("CBL-005");
    d.cable(w2)->rev = QStringLiteral("B");
    bool same = false;
    for (const SysIssue& i : d.check()) same = same || i.what.contains(QLatin1String("same part number"));
    check(same, "check: the same part number on two cables that differ");
    // Removed with what is plugged into it.
    d.removePort(u2, p3);
    check(d.cables.size() == 1, "model: a port taken away with its cable");
    d.removeUnit(u1);
    check(d.units.size() == 1 && d.cables.empty(), "model: a unit taken away with its cables");
    // The cable list.
    SystemDesign e;
    const QString a = e.addUnit(0, 0), b = e.addUnit(200, 0);
    e.addCable(a, e.addPort(a, QStringLiteral("right"), 0.5), b, e.addPort(b, QStringLiteral("left"), 0.5));
    e.cables[0].pn = QStringLiteral("CBL-001, special");
    const QString csv = e.cableListCsv();
    check(csv.startsWith(QLatin1String("Cable,P/N,Rev")) && csv.contains(QLatin1String("\"CBL-001, special\"")) && csv.contains(QLatin1String("U1,P1,U2,P1")),
          "cable list: a heading line, a line a cable, a comma in a cell quoted");
}

void testDrawings(const QString& dir) {
    const QString path = QDir(dir).filePath(QStringLiteral("CBL-001.dwx"));
    check(writeDrawing(path, QStringLiteral("CBL-001"), {QStringLiteral("A"), QStringLiteral("B")}), "drawing: written");
    const DrawingInfo info = readDrawingInfo(path);
    std::printf("  drawing: No. %s rev %s - %s\n", qPrintable(info.drawingNo), qPrintable(info.revision), qPrintable(info.title));
    check(info.found && info.drawingNo == QLatin1String("CBL-001") && info.revision == QLatin1String("B") &&
              info.title.startsWith(QLatin1String("Cable from")),
          "drawing: its number, title and latest revision read off it");
    check(!readDrawingInfo(QDir(dir).filePath(QStringLiteral("none.dwx"))).found, "drawing: one that is not there - not found");
    SystemDesign d;
    const QString a = d.addUnit(0, 0), b = d.addUnit(200, 0);
    const QString w = d.addCable(a, d.addPort(a, QStringLiteral("right"), 0.5), b, d.addPort(b, QStringLiteral("left"), 0.5));
    SysCable& c = *d.cable(w);
    check(SystemDesign::linkState(c) == SysLinkState::None, "link: none yet");
    c.pn = QStringLiteral("CBL-001");
    c.rev = QStringLiteral("B");
    c.drawing = path;
    d.refreshDrawings();
    check(SystemDesign::linkState(*d.cable(w)) == SysLinkState::Match, "link: the drawing is the revision called for");
    d.cable(w)->rev = QStringLiteral("C");
    check(SystemDesign::linkState(*d.cable(w)) == SysLinkState::Mismatch, "link: another revision called for - it differs");
    bool said = false;
    for (const SysIssue& i : d.check()) said = said || (i.level == 2 && i.what.contains(QLatin1String("rev C called for, the drawing is rev B")));
    check(said, "check: says the revision called for and the drawing's");
    d.cable(w)->drawing = QDir(dir).filePath(QStringLiteral("moved.dwx"));
    check(SystemDesign::linkState(*d.cable(w)) == SysLinkState::Missing, "link: the drawing not there");
}

void testView(const QString& dir) {
    QJsonObject stored;
    SystemDesignView view;
    view.setStore([&] { return stored; }, [&](const QJsonObject& o) { stored = o; });
    view.resize(1100, 700);
    view.show();
    QApplication::processEvents();
    SystemCanvas* c = view.canvas();
    // Two units, a port on each, a cable drawn from one to the other.
    view.setTool(SysTool::Unit);
    click(c, c->toScreen({100, 100}));
    click(c, c->toScreen({500, 100}));
    SystemDesign& d = view.design();
    check(d.units.size() == 2 && stored.contains(QStringLiteral("units")), "view: units put with Unit - and kept in the drawing");
    if (d.units.size() != 2) return;
    const SysUnit& u1 = d.units[0];
    const SysUnit& u2 = d.units[1];
    view.setTool(SysTool::Port);
    click(c, c->toScreen({u1.x + u1.w - 3, u1.y + u1.h / 2}));
    click(c, c->toScreen({u2.x + 3, u2.y + u2.h / 2}));
    check(d.units[0].ports.size() == 1 && d.units[0].ports[0].side == QLatin1String("right") && d.units[1].ports.size() == 1 &&
              d.units[1].ports[0].side == QLatin1String("left"),
          "view: a port put on the side of the unit clicked");
    view.setTool(SysTool::Cable);
    const QPointF from = c->portOnScreen(d.units[0].id, d.units[0].ports[0].id), to = c->portOnScreen(d.units[1].id, d.units[1].ports[0].id);
    mouse(c, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 6; ++i) mouse(c, QEvent::MouseMove, from + (to - from) * (i / 6.0), Qt::LeftButton, Qt::NoButton);
    mouse(c, QEvent::MouseButtonRelease, to, Qt::NoButton, Qt::LeftButton);
    check(d.cables.size() == 1 && c->selectedCable() == d.cables[0].id, "view: a cable drawn from one port to the other - selected");
    // Its own in the panel: a part number typed.
    auto* pn = view.findChild<QLineEdit*>(QStringLiteral("systemCablePn"));
    check(pn && pn->isVisibleTo(&view), "view: the cable's own on the right");
    if (pn) {
        pn->setText(QStringLiteral("CBL-001"));
        emit pn->editingFinished();
    }
    check(!d.cables.empty() && d.cables[0].pn == QLatin1String("CBL-001"), "view: its part number typed");
    // Linked to its drawing: number and revision read; the cable's revision taken from it.
    const QString path = QDir(dir).filePath(QStringLiteral("CBL-001.dwx"));
    view.linkDrawing(d.cables[0].id, path);
    check(d.cables[0].drawingRev == QLatin1String("B") && d.cables[0].rev == QLatin1String("B") &&
              SystemDesign::linkState(d.cables[0]) == SysLinkState::Match,
          "view: linked - its drawing's number and revision read, the revision taken where it had none");
    check(view.checkRevisions() == 0, "view: the revisions checked - none differs");
    view.undo();
    check(d.cables[0].drawing == path, "undo: the check is one step - the link still there");
    view.undo();
    check(d.cables[0].drawing.isEmpty(), "undo: and the link taken back");
    // The cable list out.
    const QString csv = QDir(dir).filePath(QStringLiteral("cables.csv"));
    check(view.exportCableList(csv) && QFile(csv).size() > 20, "view: the cable list written out");
    // Read again from the drawing.
    SystemDesignView again;
    again.setStore([&] { return stored; }, [&](const QJsonObject&) {});
    check(again.design().units.size() == 2 && again.design().cables.size() == 1, "view: read again from the drawing");
}

void testFinder(const QString& dir) {
    QDir(dir).mkpath(QStringLiteral("sub"));
    writeDrawing(QDir(dir).filePath(QStringLiteral("sub/harness-17.dwx")), QStringLiteral("CBL-002"), {QStringLiteral("A")});
    QFile txt(QDir(dir).filePath(QStringLiteral("notes.txt")));
    txt.open(QIODevice::WriteOnly);
    txt.write("not a drawing");
    txt.close();
    DrawingFinderDialog finder;
    finder.setFolder(dir);
    finder.setText(QStringLiteral("CBL-002"));
    finder.search(true);
    std::printf("  finder: %d found by drawing number\n", finder.resultCount());
    check(finder.resultCount() == 1 && finder.resultPath(0).endsWith(QLatin1String("harness-17.dwx")),
          "finder: found by the drawing number written in it, in a folder below");
    finder.setText(QString());
    finder.search(true);
    check(finder.resultCount() == 2, "finder: every drawing - and nothing that is not one");
    finder.setText(QStringLiteral("harness"));
    finder.search(true);
    check(finder.resultCount() == 1, "finder: by its file name");
    finder.showResult(0);
}

void testTab() {
    MainWindow w;
    w.show();
    QApplication::processEvents();
    QTabWidget* ribbon = nullptr;
    int tab = -1;
    for (QTabWidget* t : w.findChildren<QTabWidget*>())
        for (int i = 0; i < t->count(); ++i)
            if (t->tabText(i) == QLatin1String("System design")) {
                ribbon = t;
                tab = i;
            }
    check(ribbon && tab >= 0, "tab: System design on the ribbon");
    if (!ribbon) return;
    auto* view = w.findChild<SystemDesignView*>(QStringLiteral("systemDesign"));
    ribbon->setCurrentIndex(tab);
    QApplication::processEvents();
    check(view && view->isVisible(), "tab: open - the system diagram where the drawing was");
    ribbon->setCurrentIndex(0);
    QApplication::processEvents();
    check(view && !view->isVisible(), "tab: another tab - the drawing back");
}

} // namespace

int run_systemdesign_tests() {
    std::printf("system design\n");
    QTemporaryDir dir;
    testModel();
    testDrawings(dir.path());
    testView(dir.path());
    testFinder(dir.path());
    testTab();
    std::printf("  system design: %s\n", g_fail == 0 ? "OK" : "FAILED");
    return g_fail;
}
