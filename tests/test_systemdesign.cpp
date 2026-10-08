// The system design: units, ports and the cables between them, each cable's part number and revision checked against
// the drawing it is linked to; Find drawings; the System design tab.
#include "app/DrawingFinderDialog.h"
#include "app/MainWindow.h"
#include "app/SystemCanvas.h"
#include "app/SystemDesignView.h"
#include "io/Dwx.h"
#include "model/Document.h"
#include "system/PnMatch.h"
#include "system/SystemDesign.h"

#include <QFileInfo>
#include <QComboBox>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QToolButton>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QLineEdit>
#include <QPushButton>
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
    finder.setKinds({QStringLiteral("dwx"), QStringLiteral("dxf"), QStringLiteral("dwg")});
    finder.search(true);
    check(finder.resultCount() == 2, "finder: every drawing - and nothing that is not one");
    finder.setText(QStringLiteral("harness"));
    finder.search(true);
    check(finder.resultCount() == 1, "finder: by its file name");
    finder.showResult(0);
}

// PART NUMBERS FOUND as a person means them - the same rules, case by case, as the Partseek app.
void testMatcher() {
    auto texts = [](const PnMatcher& m, const QString& t) {
        QStringList out;
        for (const PnMatch& x : m.find(t)) out << QStringLiteral("%1%2").arg(x.text, x.exact ? QString() : QStringLiteral("~"));
        return out.join(QLatin1Char('|'));
    };
    check(texts(PnMatcher({QStringLiteral("LM317")}, {}), QStringLiteral("Use LM317, not LM317T or XLM317")) == QLatin1String("LM317"),
          "match: exact - the P/N on its own only");
    PnOptions partial;
    partial.partial = true;
    check(texts(PnMatcher({QStringLiteral("LM317")}, partial), QStringLiteral("LM317 LM317T XLM317")) == QLatin1String("LM317|LM317~|LM317~"),
          "match: partial - inside longer ones too, each said to be partial");
    PnOptions cased;
    cased.caseSensitive = true;
    check(texts(PnMatcher({QStringLiteral("abc-1")}, {}), QStringLiteral("ABC-1")) == QLatin1String("ABC-1") &&
              texts(PnMatcher({QStringLiteral("abc-1")}, cased), QStringLiteral("ABC-1")).isEmpty(),
          "match: case alike unless asked");
    PnOptions seps;
    seps.ignoreSeparators = true;
    check(texts(PnMatcher({QStringLiteral("ABC-123/4")}, seps), QStringLiteral("ABC1234 ; ABC 123.4 ; ABC-\n123-4")) ==
              QStringLiteral("ABC1234|ABC 123.4|ABC-\n123-4"),
          "match: separators ignored where asked");
    check(texts(PnMatcher({QStringLiteral("R1.5(K)")}, {}), QStringLiteral("R1.5(K) R135(K)")) == QLatin1String("R1.5(K)"),
          "match: punctuation in a P/N taken as it is");
    check(texts(PnMatcher(PnMatcher::splitTerms(QStringLiteral("ABC-12; ABC-123")), partial), QStringLiteral("ABC-123")) == QLatin1String("ABC-123"),
          "match: several at once, the longest first");
    check(PnMatcher::splitTerms(QStringLiteral(" a ; b;a;; ")) == QStringList({QStringLiteral("a"), QStringLiteral("b")}) &&
              !PnMatcher({}, {}).valid(),
          "match: terms split, each once; none - nothing to find");
    check(texts(PnMatcher({QStringLiteral("LM317")}, {}), QStringLiteral("LM317-ADJ, X-LM317, LM317. (LM317) LM317/")) ==
              QLatin1String("LM317|LM317|LM317"),
          "match: a joined suffix or prefix is another P/N");
    QString before, mid, after;
    PnMatcher::snippet(QString(100, QLatin1Char('x')) + QStringLiteral(" PN-1\n here"), 101, 105, before, mid, after, 5);
    check(before == QLatin1String("...xxxx ") && mid == QLatin1String("PN-1") && after == QLatin1String(" her..."), "match: the words round it");
}

// FOUND INSIDE: in a drawing's note, in a list's cell, in a PDF's text - and a DWG beside a PDF of the same name.
void testFinderInside(const QString& base) {
    const QString dir = QDir(base).filePath(QStringLiteral("inside"));
    QDir().mkpath(dir);
    {
        Document doc(QStringLiteral("bracket"));
        Entity note;
        note.type = EntityType::Text;
        note.meta.insert(QStringLiteral("text"), QStringLiteral("NOTES:\n1. Use screw XK-4417-02 only."));
        doc.entities().push_back(note);
        dwx::save(doc, QDir(dir).filePath(QStringLiteral("bracket.dwx")));
    }
    {
        QFile f(QDir(dir).filePath(QStringLiteral("bom.csv")));
        f.open(QIODevice::WriteOnly);
        f.write("Item;P/N;Description\n1;XK-4417;Screw M4\n2;XK-4417-02;Screw M4 long\n");
    }
    {
        // A PDF WITH TEXT IN IT, written as one is: a page, a font, the words shown in its content stream.
        const QByteArray content = "BT /F1 12 Tf 20 100 Td (Fasten with XK-4417 \(4x\)) Tj ET";
        QByteArray pdf = "%PDF-1.4\n1 0 obj <</Type/Catalog/Pages 2 0 R>> endobj\n2 0 obj <</Type/Pages/Kids[3 0 R]/Count 1>> endobj\n"
                         "3 0 obj <</Type/Page/Parent 2 0 R/MediaBox[0 0 300 200]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>>> endobj\n";
        pdf += "4 0 obj <</Length " + QByteArray::number(content.size()) + ">> stream\n" + content + "\nendstream endobj\n";
        pdf += "5 0 obj <</Type/Font/Subtype/Type1/BaseFont/Helvetica>> endobj\ntrailer <</Root 1 0 R>>\n%%EOF\n";
        QFile out(QDir(dir).filePath(QStringLiteral("assembly.pdf")));
        out.open(QIODevice::WriteOnly);
        out.write(pdf);
        out.close();
        QFile dwg(QDir(dir).filePath(QStringLiteral("assembly.dwg")));
        dwg.open(QIODevice::WriteOnly);
        dwg.write("AC1027 not really a drawing");
    }
    DrawingFinderDialog finder;
    finder.setFolder(dir);
    finder.setMatching(false, false, false);
    finder.setText(QStringLiteral("XK-4417"));
    finder.search(true);
    QStringList names;
    int pdfRow = -1;
    for (int r = 0; r < finder.resultCount(); ++r) {
        names << QFileInfo(finder.resultPath(r)).fileName();
        if (finder.resultPath(r).endsWith(QLatin1String(".pdf"))) pdfRow = r;
    }
    names.sort();
    std::printf("  finder inside: %s\n", qPrintable(names.join(QStringLiteral(", "))));
    check(names.contains(QStringLiteral("bom.csv")) && !names.contains(QStringLiteral("bracket.dwx")),
          "finder: exact - XK-4417 in the list's cell; not in XK-4417-02 in the drawing's note");
    check(pdfRow >= 0 && finder.resultDrawings(pdfRow).contains(QStringLiteral("DWG")),
          "finder: in the PDF's text - and the DWG of the same name beside it");
    // The PDF shown, read in the background; Open PDF and Open DWG for it.
    finder.showResult(pdfRow);
    auto* openPdf = finder.findChild<QPushButton*>(QStringLiteral("finderOpenPdf"));
    auto* openDwg = finder.findChild<QPushButton*>(QStringLiteral("finderOpenDwg"));
    auto* openDwx = finder.findChild<QPushButton*>(QStringLiteral("finderOpenDwx"));
    check(finder.previewCount(true) > 0, "finder: the PDF shown in the preview");
    check(openPdf && openPdf->isEnabled() && openDwg && openDwg->isEnabled() && openDwx && !openDwx->isEnabled(),
          "finder: Open PDF, and Open DWG for the one beside it - no DWX of that name");
    finder.setText(QStringLiteral("XK-4417-02"));
    finder.search(true);
    names.clear();
    for (int r = 0; r < finder.resultCount(); ++r) names << QFileInfo(finder.resultPath(r)).fileName();
    check(names.contains(QStringLiteral("bracket.dwx")) && names.contains(QStringLiteral("bom.csv")) && names.size() == 2,
          "finder: in the drawing's note and the list's cell");
    finder.setMatching(true, false, false);
    finder.setText(QStringLiteral("XK-4417"));
    finder.search(true);
    check(finder.resultCount() == 3, "finder: partial - every file it is in, inside the longer number too");
    finder.setMatching(false, false, true);
    finder.setText(QStringLiteral("XK4417 02"));
    finder.search(true);
    check(finder.resultCount() == 2, "finder: separators ignored - XK4417 02 finds XK-4417-02");
}

// THE DRAWING'S DESIGNATORS: linked, the cable and the connectors it names otherwise - asked, and taken on yes.
void testDesignators(const QString& base) {
    const QString path = QDir(base).filePath(QStringLiteral("named.dwx"));
    {
        Document doc(QStringLiteral("named"));
        for (const char* d : {"W3", "X1", "X2"}) {
            Entity t;
            t.type = EntityType::Text;
            t.meta.insert(QStringLiteral("text"), QString::fromLatin1(d));
            t.meta.insert(QStringLiteral("mechRole"), QStringLiteral("designator"));
            doc.entities().push_back(t);
        }
        dwx::save(doc, path);
    }
    const DrawingInfo info = readDrawingInfo(path);
    std::printf("  designators: cables [%s], connectors [%s]\n", qPrintable(info.cables.join(QLatin1Char(','))),
                qPrintable(info.connectors.join(QLatin1Char(','))));
    check(info.cables == QStringList{QStringLiteral("W3")} && info.connectors == QStringList({QStringLiteral("X1"), QStringLiteral("X2")}),
          "designators: the drawing's cable and connectors read");
    QJsonObject stored;
    SystemDesignView view;
    view.setStore([&] { return stored; }, [&](const QJsonObject& o) { stored = o; });
    SystemDesign& d = view.design();
    const QString a = d.addUnit(0, 0), b = d.addUnit(300, 0);
    const QString pa = d.addPort(a, QStringLiteral("right"), 0.5, QStringLiteral("J2"));
    const QString pb = d.addPort(b, QStringLiteral("left"), 0.5, QStringLiteral("X1"));
    const QString w = d.addCable(a, pa, b, pb);
    const std::vector<DesignatorChange> changes = d.designatorChanges(w, info);
    check(changes.size() == 2 && changes[0].cable && changes[0].to == QLatin1String("W3") && !changes[1].cable &&
              changes[1].from == QLatin1String("J2") && changes[1].to == QLatin1String("X2"),
          "designators: the cable W1 is W3, J2 is X2 - X1 alike, kept");
    QString asked;
    view.setConfirm([&](const QString& q) {
        asked = q;
        return false;
    });
    view.linkDrawing(w, path, true);
    check(asked.contains(QLatin1String("W1 -> W3")) && asked.contains(QLatin1String("J2 -> X2")) && d.cable(w)->name == QLatin1String("W1"),
          "designators: asked - and no, the diagram's kept");
    view.setConfirm([](const QString&) { return true; });
    view.linkDrawing(w, path, true);
    check(d.cable(w)->name == QLatin1String("W3") && d.unit(a)->port(pa)->name == QLatin1String("X2") && d.unit(b)->port(pb)->name == QLatin1String("X1"),
          "designators: yes - the drawing's taken on the diagram");
    check(d.designatorChanges(w, info).empty(), "designators: alike now - nothing to ask");
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

// MECHANICAL PARTS AND PCBAS, SIZED, THEIR PORTS THROUGH THE WALL: each unit marked for what it is, sized by its corner;
// each port an inside and an outside, its outside there once a cable is plugged in from outside, each with its
// connector P/N; ports slid along the sides; ports put where a cable starts or ends on a unit without one; notes; the
// numbering's prefix and digits; the tools by their keys; Undo and Redo - the window's, on every tab.
void testUnitsPortsNotes() {
    // ---- SAVED ----
    {
        SystemDesign d;
        const QString u = d.addUnit(0, 0);
        d.unit(u)->kind = QStringLiteral("pcba");
        const QString p = d.addPort(u, QStringLiteral("right"), 0.5);
        d.unit(u)->port(p)->insidePn = QStringLiteral("HDR-10");
        const QString v = d.addUnit(300, 0);
        const QString q = d.addPort(v, QStringLiteral("left"), 0.5);
        const QString c = d.addCable(u, p, v, q);
        d.cable(c)->aInside = true;
        d.addNote(10, 200, QStringLiteral("Keep W1 away from the fan"));
        d.pnPrefix = QStringLiteral("HRN-");
        d.pnDigits = 4;
        const SystemDesign back = SystemDesign::fromJson(d.toJson());
        check(back.unit(u)->kind == QLatin1String("pcba") && back.unit(u)->port(p)->insidePn == QLatin1String("HDR-10") &&
                  back.cable(c)->aInside && !back.cable(c)->bInside && back.notes.size() == 1 &&
                  back.notes[0].text == QLatin1String("Keep W1 away from the fan") && back.pnDigits == 4,
              "saved: what a unit is, a port's inside P/N, a cable's inside end, notes, the numbering's digits");
        check(SystemDesign::fromJson(QJsonObject{{QStringLiteral("units"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("u1")}}}}})
                      .units[0]
                      .kind == QLatin1String("mech"),
              "saved: a unit saved before - a mechanical part");
        SystemDesign numbered = back;
        numbered.cables[0].pn.clear();
        numbered.numberCables();
        check(numbered.cables[0].pn == QLatin1String("HRN-0001"), "numbering: its prefix and its digits - HRN-0001");
    }

    QJsonObject stored;
    SystemDesignView view;
    view.setStore([&] { return stored; }, [&](const QJsonObject& o) { stored = o; });
    view.resize(1200, 760);
    view.show();
    QApplication::processEvents();
    SystemCanvas* c = view.canvas();
    SystemDesign& d = view.design();
    auto drag = [&](const QPointF& from, const QPointF& to) {
        mouse(c, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 6; ++i) mouse(c, QEvent::MouseMove, from + (to - from) * (i / 6.0), Qt::LeftButton, Qt::NoButton);
        mouse(c, QEvent::MouseButtonRelease, to, Qt::NoButton, Qt::LeftButton);
        QApplication::processEvents();
    };
    auto key = [&](Qt::Key k) {
        QKeyEvent e(QEvent::KeyPress, k, Qt::NoModifier);
        QApplication::sendEvent(c, &e);
    };
    // ---- THE TOOLS BY THEIR KEYS ----
    key(Qt::Key_U);
    const bool unitKey = c->tool() == SysTool::Unit;
    key(Qt::Key_P);
    const bool portKey = c->tool() == SysTool::Port;
    key(Qt::Key_H);
    const bool cableKey = c->tool() == SysTool::Cable;
    key(Qt::Key_N);
    const bool noteKey = c->tool() == SysTool::Note;
    key(Qt::Key_Escape);
    check(unitKey && portKey && cableKey && noteKey && c->tool() == SysTool::Select, "keys: U a unit, P a port, H a harness, N a note, Esc select");

    // ---- A MECHANICAL PART AND A PCBA ----
    view.setTool(SysTool::Unit);
    click(c, c->toScreen({100, 100}));
    c->setNewUnitKind(QStringLiteral("pcba"));
    click(c, c->toScreen({500, 100}));
    check(d.units.size() == 2 && d.units[0].kind == QLatin1String("mech") && d.units[1].kind == QLatin1String("pcba"),
          "kind: units put as a mechanical part, then - chosen - as a PCBA");
    c->select(d.units[0].id, QString(), QString());
    auto* kind = view.findChild<QComboBox*>(QStringLiteral("systemUnitKind"));
    if (kind) kind->setCurrentIndex(1);
    check(kind && d.units[0].kind == QLatin1String("pcba"), "kind: set on the right - a PCBA");
    if (kind) kind->setCurrentIndex(0);

    // ---- SIZED BY ITS CORNER ----
    view.setTool(SysTool::Select);
    const QString u1 = d.units[0].id, u2 = d.units[1].id;
    const double w0 = d.unit(u1)->w, h0 = d.unit(u1)->h;
    const QPointF corner = c->unitCornerOnScreen(u1);
    drag(corner, corner + (c->toScreen({100, 60}) - c->toScreen({0, 0})));
    std::printf("  size: %.0f x %.0f, was %.0f x %.0f\n", d.unit(u1)->w, d.unit(u1)->h, w0, h0);
    check(std::abs(d.unit(u1)->w - (w0 + 100)) < 10.5 && std::abs(d.unit(u1)->h - (h0 + 60)) < 10.5, "size: a unit made bigger by dragging its corner");
    view.undo();
    check(d.unit(u1)->w == w0 && d.unit(u1)->h == h0, "size: undone in one step");
    view.redo();
    check(d.unit(u1)->w > w0, "size: and done again with Redo");

    // ---- PORTS PUT WHERE A CABLE STARTS AND ENDS ON A UNIT ----
    view.setTool(SysTool::Cable);
    drag(c->toScreen({d.unit(u1)->x + d.unit(u1)->w - 8, d.unit(u1)->y + d.unit(u1)->h / 2}),
         c->toScreen({d.unit(u2)->x + 8, d.unit(u2)->y + d.unit(u2)->h / 2}));
    check(d.cables.size() == 1 && d.unit(u1)->ports.size() == 1 && d.unit(u2)->ports.size() == 1 &&
              d.unit(u1)->ports[0].side == QLatin1String("right") && d.unit(u2)->ports[0].side == QLatin1String("left") &&
              !d.cables[0].aInside && !d.cables[0].bInside,
          "ports: a cable from a unit to a unit with no ports - a port put on each, on the sides facing, the cable on their outsides");
    view.undo();
    check(d.cables.empty() && d.unit(u1)->ports.empty() && d.unit(u2)->ports.empty(), "ports: the ports and the cable - one step to undo");
    view.redo();
    // A cable let go on nothing: none, and no port.
    drag(c->unitOnScreen(u1), c->toScreen({300, 600}));
    check(d.cables.size() == 1 && d.unit(u1)->ports.size() == 1, "ports: a cable let go on nothing - given up, no port put");

    // ---- A PORT SLID ALONG ITS UNIT'S SIDES ----
    view.setTool(SysTool::Select);
    const QString p1 = d.unit(u1)->ports[0].id;
    const SysUnit& unit1 = *d.unit(u1);
    drag(c->portOnScreen(u1, p1), c->toScreen({unit1.x + unit1.w * 0.4, unit1.y - 2}));
    check(d.unit(u1)->port(p1)->side == QLatin1String("top") && std::abs(d.unit(u1)->port(p1)->at - 0.4) < 0.08 && d.cables.size() == 1,
          "ports: slid round onto the top - its cable with it");

    // ---- A PCBA INSIDE ITS HOUSING: its cable on the housing's port's inside ----
    view.setTool(SysTool::Unit);
    c->setNewUnitKind(QStringLiteral("mech"));
    click(c, c->toScreen({180, 500}));
    const QString housing = d.units.back().id;
    d.unit(housing)->w = 400;
    d.unit(housing)->h = 220;
    c->setNewUnitKind(QStringLiteral("pcba"));
    click(c, c->toScreen({d.unit(housing)->x + 230, d.unit(housing)->y + 120}));
    const QString board = d.units.back().id;
    const bool within = QRectF(d.unit(housing)->x, d.unit(housing)->y, 400, 220)
                            .contains(QRectF(d.unit(board)->x, d.unit(board)->y, d.unit(board)->w, d.unit(board)->h));
    // The housing's port, on its left; a cable to it from outside (U2), and one from the board within.
    view.setTool(SysTool::Port);
    click(c, c->toScreen({d.unit(housing)->x + 2, d.unit(housing)->y + 110}));
    const QString through = d.unit(housing)->ports.back().id;
    view.setTool(SysTool::Cable);
    drag(c->unitOnScreen(u2), c->portOnScreen(housing, through));
    drag(c->unitOnScreen(board), c->portOnScreen(housing, through));
    const SysCable& outsideCable = d.cables[d.cables.size() - 2];
    const SysCable& insideCable = d.cables.back();
    std::printf("  through the wall: board within its housing %d; the outside cable's ends %d/%d, the inside one's %d/%d\n", within ? 1 : 0,
                outsideCable.aInside ? 1 : 0, outsideCable.bInside ? 1 : 0, insideCable.aInside ? 1 : 0, insideCable.bInside ? 1 : 0);
    check(within && !outsideCable.bInside && insideCable.bInside && !insideCable.aInside,
          "through the wall: a cable from outside on the port's outside, one from the board within on its inside");
    bool doubled = false;
    for (const SysIssue& i : d.check()) doubled = doubled || i.what.contains(QLatin1String("cables plugged"));
    check(!doubled, "through the wall: a cable each side of a port is not two cables in one");
    // The connector P/Ns, outside and inside, in the port list.
    c->select(housing, QString(), QString());
    auto* ports = view.findChild<QTableWidget*>(QStringLiteral("systemPorts"));
    if (ports && ports->rowCount() > 0) {
        ports->setItem(0, 2, new QTableWidgetItem(QStringLiteral("M12-8F")));
        ports->setItem(0, 3, new QTableWidgetItem(QStringLiteral("HDR-8")));
    }
    check(ports && d.unit(housing)->port(through)->connector == QLatin1String("M12-8F") &&
              d.unit(housing)->port(through)->insidePn == QLatin1String("HDR-8"),
          "ports: a connector P/N each for its outside and its inside");

    // ---- A NOTE ----
    view.setTool(SysTool::Note);
    click(c, c->toScreen({700, 400}));
    check(d.notes.size() == 1 && c->selectedNote() == d.notes[0].id, "note: put where clicked, and selected");
    auto* text = view.findChild<QPlainTextEdit*>(QStringLiteral("systemNoteText"));
    if (text) text->setPlainText(QStringLiteral("Fan side - keep cables 20 mm clear"));
    check(text && d.notes[0].text == QLatin1String("Fan side - keep cables 20 mm clear") && stored.contains(QStringLiteral("notes")),
          "note: its text written on the right - and kept in the drawing");
    view.undo();
    check(d.notes.size() == 1 && d.notes[0].text == QLatin1String("Note"), "note: its text one step to undo");
    view.setTool(SysTool::Select);
    const QPointF noteAt = c->noteOnScreen(d.notes[0].id);
    const double nx = d.notes[0].x;
    drag(noteAt, noteAt + QPointF(60, 0));
    check(d.notes[0].x > nx + 20, "note: moved by dragging it");
    c->selectNote(d.notes[0].id);
    c->deleteSelected();
    check(d.notes.empty(), "note: deleted");

    // ---- NUMBERED WITH ITS PREFIX AND DIGITS ----
    view.setNumbering(QStringLiteral("HRN-"), 4);
    view.numberCables();
    check(!d.cables.empty() && d.cables[0].pn == QLatin1String("HRN-0001"), "numbering: set - the cables numbered HRN-0001...");

    // ---- UNDO AND REDO: THE WINDOW'S, ON EVERY TAB ----
    MainWindow w;
    w.show();
    QApplication::processEvents();
    check(!w.findChild<QToolButton*>(QStringLiteral("systemUndo")) && !w.findChild<QToolButton*>(QStringLiteral("preview3dRedo")) &&
              !w.findChild<QToolButton*>(QStringLiteral("preview3dUndo")) && !w.findChild<QToolButton*>(QStringLiteral("view3dRouteRedo")),
          "undo: no Undo of its own on the System design and 3D Preview tabs, no Redo in the 3D Preview");
    QToolButton* undo = nullptr;
    QToolButton* redo = nullptr;
    for (QToolButton* b : w.findChildren<QToolButton*>())
        if (b->property("redo").isValid() && b->property("compact").toBool()) (b->property("redo").toBool() ? redo : undo) = b;
    QTabWidget* ribbon = nullptr;
    int tab = -1;
    for (QTabWidget* t : w.findChildren<QTabWidget*>())
        for (int i = 0; i < t->count(); ++i)
            if (t->tabText(i) == QLatin1String("System design")) {
                ribbon = t;
                tab = i;
            }
    auto* sys = w.findChild<SystemDesignView*>(QStringLiteral("systemDesign"));
    check(undo && redo && ribbon && sys, "undo: the window's Undo and Redo in its corner");
    if (!undo || !redo || !ribbon || !sys) return;
    ribbon->setCurrentIndex(tab);
    QApplication::processEvents();
    sys->setTool(SysTool::Unit);
    click(sys->canvas(), sys->canvas()->toScreen({100, 100}));
    QApplication::processEvents();
    check(sys->design().units.size() == 1 && undo->isEnabled(), "undo: a unit put on the System design tab - the corner's Undo lit");
    undo->click();
    QApplication::processEvents();
    check(sys->design().units.empty() && redo->isEnabled(), "undo: the corner's Undo takes it back - the diagram's, not the drawing's");
    redo->click();
    QApplication::processEvents();
    check(sys->design().units.size() == 1, "undo: and its Redo puts it back");
    ribbon->setCurrentIndex(0);
    QApplication::processEvents();
}

} // namespace

int run_systemdesign_tests() {
    std::printf("system design\n");
    QTemporaryDir dir;
    testModel();
    testDrawings(dir.path());
    testView(dir.path());
    testFinder(dir.path());
    testMatcher();
    testFinderInside(dir.path());
    testDesignators(dir.path());
    testTab();
    testUnitsPortsNotes();
    std::printf("  system design: %s\n", g_fail == 0 ? "OK" : "FAILED");
    return g_fail;
}
