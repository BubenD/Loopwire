// The system design: units, ports and the cables between them, each cable's part number and revision checked against
// the drawing it is linked to; Find drawings; the System design tab.
#include "app/DrawingFinderDialog.h"
#include "app/MainWindow.h"
#include "app/SystemCanvas.h"
#include "app/SystemDesignView.h"
#include "app/RuleExplainer.h"
#include "app/Editor.h"
#include "app/PdfViewer.h"
#include "app/MechanicalValidationDialog.h"
#include "model/DrawingDetails.h"
#include "system/SystemRouting.h"
#include "io/NativePdf.h"
#include <QPdfWriter>
#include <QPainter>
#include <QProgressBar>
#include <QTabBar>
#include <future>
#include <QDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QLabel>
#include "io/Dwx.h"
#include "harness/Harness.h"
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QTimer>
#include <QThread>
#include "io/PdfImport.h"
#include "model/Document.h"
#include "system/PnMatch.h"
#include "system/SystemDesign.h"
#include <QUrl>
#include "app/UiBits.h"
#include "system/SystemImport.h"
#include "system/SystemExport.h"

#include <QAction>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <QComboBox>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QToolButton>
#include <QImage>
#include <QDockWidget>
#include <QMenu>
#include <QVBoxLayout>
#include <QSplitter>
#include <QCheckBox>

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
    view.setTool(SysTool::Unit);   // a unit put, back to Select
    click(c, c->toScreen({500, 100}));
    SystemDesign& d = view.design();
    check(d.units.size() == 2 && stored.contains(QStringLiteral("units")), "view: units put with Unit - and kept in the drawing");
    if (d.units.size() != 2) return;
    const SysUnit& u1 = d.units[0];
    const SysUnit& u2 = d.units[1];
    view.setTool(SysTool::Port);
    click(c, c->toScreen({u1.x + u1.w - 3, u1.y + u1.h / 2}));
    view.setTool(SysTool::Port);
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
    // The drawing has no DRC-passed stamps (DRAWING-FIELDS asks for them): its revision is what is checked here.
    view.checkRevisions();
    check(std::none_of(view.issues().begin(), view.issues().end(), [](const SysIssue& i) { return i.rule == QLatin1String("DRAWING-LINK"); }),
          "view: the revisions checked - none differs");
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
        {
            QPdfWriter writer(QDir(dir).filePath(QStringLiteral("assembly.pdf")));writer.setResolution(72);
            QPainter painter(&writer);painter.setFont(QFont(QStringLiteral("Arial"),12));painter.drawText(30,100,QStringLiteral("Fasten with XK-4417 (4x)"));
        }
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
    finder.setText(QStringLiteral("assembly"));
    finder.search(true);
    int dwgRow = -1;
    for (int r = 0; r < finder.resultCount(); ++r)
        if (finder.resultPath(r).endsWith(QLatin1String(".dwg"))) dwgRow = r;
    if (dwgRow >= 0) {
        finder.showResult(dwgRow);
        std::printf("  finder: the DWG beside its PDF - shown %d\n", finder.previewCount(true));
        check(finder.previewCount(true) > 0, "finder: a DWG with its PDF beside it - shown from the PDF");
    }
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
    view.setTool(SysTool::Unit);
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
    view.setTool(SysTool::Cable);
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
    view.setTool(SysTool::Unit);
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
    view.setTool(SysTool::Cable);
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
    // One connector P/N and its contact P/N in the port list.
    c->select(housing, QString(), QString());
    auto* ports = view.findChild<QTableWidget*>(QStringLiteral("systemPorts"));
    if (ports && ports->rowCount() > 0) {
        ports->setItem(0, 2, new QTableWidgetItem(QStringLiteral("M12-8F")));
        ports->setItem(0, 3, new QTableWidgetItem(QStringLiteral("HDR-8")));
    }
    check(ports && d.unit(housing)->port(through)->connector == QLatin1String("M12-8F") &&
              d.unit(housing)->port(through)->contactPn == QLatin1String("HDR-8") && ports->columnCount() == 5,
          "ports: one connector P/N and a contact P/N");

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

// SAVED AS A FILE OF ITS OWN, AND OUT FOR DRAW.IO AND VISIO: the design read back the same; the draw.io file a diagram
// of its units, ports, cables (from port to port, through their corners) and notes; the Visio file a package with a
// page of shapes, each part well formed.
void testFilesOut(const QString& base) {
    const QString dir = QDir(base).filePath(QStringLiteral("out"));
    QDir().mkpath(dir);
    SystemDesign d;
    d.name = QStringLiteral("Test rig");
    const QString u1 = d.addUnit(0, 0), u2 = d.addUnit(400, 40);
    d.unit(u2)->kind = QStringLiteral("pcba");
    d.unit(u1)->pn = QStringLiteral("ENC-100");
    const QString p1 = d.addPort(u1, QStringLiteral("right"), 0.5), p2 = d.addPort(u2, QStringLiteral("left"), 0.5);
    d.unit(u1)->port(p1)->connector = QStringLiteral("M12 & co <8F>");
    const QString w = d.addCable(u1, p1, u2, p2);
    d.cable(w)->pn = QStringLiteral("CBL-001");
    d.addNote(0, 200, QStringLiteral("Keep W1 clear\nof the fan"));

    // ---- A FILE OF ITS OWN ----
    const QString own = QDir(dir).filePath(QStringLiteral("rig.edsys"));
    SystemDesign back;
    check(saveSystemFile(d, own) && loadSystemFile(own, back) && back.toJson() == d.toJson(), "file: saved as its own and read back the same");
    SystemDesign none;
    check(!loadSystemFile(QDir(dir).filePath(QStringLiteral("nothing.edsys")), none), "file: one not there - not read");

    // ---- DRAW.IO ----
    const QString drawio = QDir(dir).filePath(QStringLiteral("rig.drawio"));
    check(exportSystemDrawio(d, drawio), "draw.io: written");
    QFile f(drawio);
    f.open(QIODevice::ReadOnly);
    QXmlStreamReader xml(&f);
    int vertices = 0, edges = 0;
    bool joined = false, labelled = false;
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement || xml.name() != QLatin1String("mxCell")) continue;
        const auto a = xml.attributes();
        if (a.value(QLatin1String("vertex")) == QLatin1String("1")) ++vertices;
        if (a.value(QLatin1String("edge")) == QLatin1String("1")) {
            ++edges;
            joined = a.value(QLatin1String("source")) == QStringLiteral("%1.%2").arg(u1, p1) &&
                     a.value(QLatin1String("target")) == QStringLiteral("%1.%2").arg(u2, p2);
            labelled = a.value(QLatin1String("value")).contains(QLatin1String("CBL-001"));
        }
    }
    std::printf("  draw.io: %d shape(s), %d cable(s), well formed %d\n", vertices, edges, xml.hasError() ? 0 : 1);
    check(!xml.hasError() && vertices == 5 && edges == 1 && joined && labelled,
          "draw.io: well formed - two units, two ports, a note, and the cable from port to port with its P/N");

    // ---- VISIO ----
    const QString vsdx = QDir(dir).filePath(QStringLiteral("rig.vsdx"));
    check(exportSystemVsdx(d, vsdx), "visio: written");
    QFile v(vsdx);
    v.open(QIODevice::ReadOnly);
    const QByteArray bytes = v.readAll();
    bool parts = bytes.startsWith("PK\x03\x04");
    for (const char* part : {"[Content_Types].xml", "_rels/.rels", "visio/document.xml", "visio/pages/pages.xml", "visio/pages/page1.xml",
                             "visio/pages/_rels/pages.xml.rels", "visio/_rels/document.xml.rels"})
        parts = parts && bytes.contains(part);
    // Each part stored as it is: its XML read where it lies.
    auto wellFormed = [&](const QByteArray& open, const QByteArray& close) {
        const int at = bytes.indexOf(open), end = bytes.indexOf(close, at);
        if (at < 0 || end < 0) return false;
        QXmlStreamReader r(bytes.mid(at, end - at + close.size()));
        while (!r.atEnd()) r.readNext();
        return !r.hasError();
    };
    const int shapes = static_cast<int>(bytes.count("<Shape ID="));
    std::printf("  visio: %lld bytes, %d shape(s)\n", static_cast<long long>(bytes.size()), shapes);
    check(parts && wellFormed("<PageContents", "</PageContents>") && wellFormed("<VisioDocument", "</VisioDocument>") &&
              wellFormed("<Pages ", "</Pages>") && wellFormed("<Types ", "</Types>"),
          "visio: a package with its document, its pages and a page - each part well formed");
    check(shapes == 2 + 2 * 2 + 1 + 1 && bytes.contains("Keep W1 clear") && bytes.contains("M12 &amp; co &lt;8F&gt;"),
          "visio: a shape for each unit, port and its name, cable and note - their text kept, written safely");

    // ---- FROM THE TAB ----
    SystemDesignView view;
    QJsonObject stored;
    view.setStore([&] { return stored; }, [&](const QJsonObject& o) { stored = o; });
    check(view.openDesign(own) && view.design().units.size() == 2 && view.design().notes.size() == 1, "file: opened on the tab");
    view.undo();
    check(view.design().units.empty(), "file: opened - Undo takes it back");
    check(view.exportDrawio(QDir(dir).filePath(QStringLiteral("tab.drawio"))) && view.exportVisio(QDir(dir).filePath(QStringLiteral("tab.vsdx"))) &&
              view.saveDesignAs(QDir(dir).filePath(QStringLiteral("tab.edsys"))),
          "file: saved, and out for draw.io and Visio, from the tab");
    // Kept for a look from outside: the Visio file the tests wrote.
    QFile::remove(QDir::temp().filePath(QStringLiteral("edraw_system_test.vsdx")));
    QFile::copy(vsdx, QDir::temp().filePath(QStringLiteral("edraw_system_test.vsdx")));
}

// UNITS IN UNITS, SIZED BY ANY WALL, THEIR LOOK, WHAT IS SHOWN, DELETE ASKED, THE TOOLS: a unit put in a unit kept
// inside it and moved with it; any wall or corner sizes a unit; its colour and how solid it is; each field's eye;
// Delete asks where a unit is connected; back to Select after putting one (Shift: another), Space the last again.
void testNestingAndLook() {
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
    auto shiftClick = [&](const QPointF& at) {
        QMouseEvent down(QEvent::MouseButtonPress, at, c->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
        QMouseEvent up(QEvent::MouseButtonRelease, at, c->mapToGlobal(at), Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
        QApplication::sendEvent(c, &down);
        QApplication::sendEvent(c, &up);
    };
    auto key = [&](Qt::Key k) {
        QKeyEvent e(QEvent::KeyPress, k, Qt::NoModifier);
        QApplication::sendEvent(c, &e);
    };

    // ---- PUT, THEN BACK TO SELECT - SHIFT FOR ANOTHER - SPACE THE TOOL AGAIN ----
    view.setTool(SysTool::Unit);
    click(c, c->toScreen({100, 100}));
    check(d.units.size() == 1 && c->tool() == SysTool::Select, "tools: a unit put - back to Select");
    key(Qt::Key_Space);
    check(c->tool() == SysTool::Unit, "tools: Space - the Unit tool again");
    shiftClick(c->toScreen({900, 100}));
    check(d.units.size() == 2 && c->tool() == SysTool::Unit, "tools: Shift held - a unit put, and Unit kept for another");
    key(Qt::Key_Escape);

    // ---- A UNIT IN A UNIT ----
    const QString mother = d.units[0].id;
    d.unit(mother)->w = 400;
    d.unit(mother)->h = 300;
    view.setTool(SysTool::Unit);
    click(c, c->toScreen({d.unit(mother)->x + 200, d.unit(mother)->y + 150}));
    const QString child = d.units.back().id;
    check(d.unit(child)->parent == mother && d.unit(mother)->rect().contains(d.unit(child)->rect()),
          "inside: a unit put in a unit - in it, and inside its walls");
    // Dragged against her wall: kept inside.
    view.setTool(SysTool::Select);
    const QPointF mid = c->unitOnScreen(child);
    drag(mid, mid + (c->toScreen({140, 0}) - c->toScreen({0, 0})));
    check(d.unit(child)->parent == mother && d.unit(mother)->rect().contains(d.unit(child)->rect()), "inside: dragged to her wall - kept inside her");
    // The mother moved: the child with her.
    const QPointF childWas(d.unit(child)->x, d.unit(child)->y);
    const QPointF momMid = c->toScreen({d.unit(mother)->x + 30, d.unit(mother)->y + 270});
    drag(momMid, momMid + (c->toScreen({100, 50}) - c->toScreen({0, 0})));
    std::printf("  inside: the child moved %.0f, %.0f with its mother\n", d.unit(child)->x - childWas.x(), d.unit(child)->y - childWas.y());
    check(std::abs(d.unit(child)->x - childWas.x() - 100) < 10.5 && std::abs(d.unit(child)->y - childWas.y() - 50) < 10.5,
          "inside: the mother moved - what is in her moves with her");
    // Made smaller than what is in her: no smaller than it.
    const QRectF in = d.contentsOf(mother);
    drag(c->unitCornerOnScreen(mother), c->toScreen({d.unit(mother)->x + 30, d.unit(mother)->y + 30}));
    check(d.unit(mother)->rect().contains(in), "inside: sized - never smaller than what is in her");
    // Dragged out past her wall: out of her.
    const QPointF cm = c->unitOnScreen(child);
    drag(cm, c->toScreen({d.unit(mother)->x + d.unit(mother)->w + 300, d.unit(child)->y + 20}));
    check(d.unit(child)->parent.isEmpty(), "inside: dragged out past her wall - in her no more");
    view.undo();
    check(d.unit(child)->parent == mother, "inside: one step to undo");

    // ---- ANY WALL, ANY CORNER ----
    const QString other = d.units[1].id;
    const QRectF before = d.unit(other)->rect();
    drag(c->toScreen({before.left(), before.center().y()}), c->toScreen({before.left() - 60, before.center().y()}));
    std::printf("  wall: left %.0f -> %.0f, width %.0f -> %.0f\n", before.left(), d.unit(other)->x, before.width(), d.unit(other)->w);
    check(std::abs(d.unit(other)->x - (before.left() - 60)) < 10.5 && std::abs(d.unit(other)->rect().right() - before.right()) < 1e-6,
          "size: by its left wall - wider to the left, its right where it was");
    const QRectF b2 = d.unit(other)->rect();
    drag(c->toScreen(b2.topLeft()), c->toScreen(b2.topLeft() - QPointF(0, 40)));
    check(std::abs(d.unit(other)->y - (b2.top() - 40)) < 10.5 && std::abs(d.unit(other)->rect().bottom() - b2.bottom()) < 1e-6,
          "size: by its top-left corner - taller upward");

    // ---- ITS LOOK, AND WHAT IS SHOWN ----
    c->select(other, QString(), QString());
    view.setUnitColour(QColor(200, 60, 60));
    view.setUnitOpacity(0.4);
    auto* eye = view.findChild<QToolButton*>(QStringLiteral("systemUnitNameEye"));
    check(eye && eye->isChecked(), "eye: by the unit's name, open - shown");
    if (eye) eye->click();
    check(d.unit(other)->color == QColor(200, 60, 60) && std::abs(d.unit(other)->opacity - 0.4) < 1e-9 &&
              d.unit(other)->hidden.contains(QStringLiteral("name")) && d.unit(other)->shows(QStringLiteral("desc")),
          "look: its colour, how solid it is, and its name hidden on the diagram (its description shown, as it starts)");
    const SystemDesign back = SystemDesign::fromJson(d.toJson());
    check(back.unit(other)->opacity == d.unit(other)->opacity && back.unit(other)->hidden == d.unit(other)->hidden &&
              back.unit(child)->parent == mother,
          "saved: how solid, what is hidden, which unit it is in");
    // An old design - no unit said to be in another: found by where they lie.
    QJsonObject old = d.toJson();
    QJsonArray us = old.value(QStringLiteral("units")).toArray();
    for (int i = 0; i < us.size(); ++i) {
        QJsonObject u = us[i].toObject();
        u.remove(QStringLiteral("parent"));
        us[i] = u;
    }
    old.insert(QStringLiteral("units"), us);
    check(SystemDesign::fromJson(old).unit(child)->parent == mother, "saved: an old design - a unit inside another found in it");
    c->selectNote(QString());
    auto* cableEye = view.findChild<QToolButton*>(QStringLiteral("systemCableDescEye"));
    check(cableEye != nullptr, "eye: by each of a cable's fields too");

    // ---- DELETE, ASKED WHERE IT IS CONNECTED ----
    QStringList asked;
    bool answer = false;
    view.setConfirm([&](const QString& q) {
        asked << q;
        return answer;
    });
    // A cable from the mother's child to the other unit.
    view.setTool(SysTool::Cable);
    drag(c->unitOnScreen(child), c->unitOnScreen(other));
    check(d.cables.size() == 1, "delete: a cable to delete with");
    c->select(mother, QString(), QString());
    check(!view.deleteSelected() && d.unit(mother) && asked.size() == 1 && asked[0].contains(QStringLiteral("unit(s) inside it")),
          "delete: a unit with a unit and a cable in it - asked, and kept when the answer is no");
    answer = true;
    check(view.deleteSelected() && !d.unit(mother) && !d.unit(child) && d.cables.empty(), "delete: yes - it, what is in it and its cables gone");
    view.setTool(SysTool::Unit);
    click(c, c->toScreen({100, 600}));
    const QString lone = d.units.back().id;
    asked.clear();
    c->select(lone, QString(), QString());
    key(Qt::Key_Delete);
    check(!d.unit(lone) && asked.isEmpty(), "delete: a unit with nothing connected - deleted, nothing asked");
    // SPACE: Delete again.
    c->select(other, QString(), QString());
    key(Qt::Key_Space);
    check(!d.unit(other), "delete: Space - deletes again");
    // Saved as Edraw's own: .wetsys read back.
    const QString own = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_test.wetsys"));
    SystemDesign got;
    check(saveSystemFile(back, own) && loadSystemFile(own, got) && got.units.size() == back.units.size(), "file: Edraw's own .wetsys");
    QFile::remove(own);
}

// THE KEYBOARD'S DELETE ON THE SYSTEM DESIGN TAB - the window's shortcut - deletes the unit selected; A CABLE BY TWO
// CLICKS: one on a port starts it, one on another ends it.
void testDeleteKeyAndClickCable() {
    // ---- TWO CLICKS ----
    QJsonObject stored;
    SystemDesignView view;
    view.setStore([&] { return stored; }, [&](const QJsonObject& o) { stored = o; });
    view.resize(1100, 700);
    view.show();
    QApplication::processEvents();
    SystemCanvas* c = view.canvas();
    SystemDesign& d = view.design();
    const QString a = d.addUnit(0, 0), b = d.addUnit(400, 0);
    const QString pa = d.addPort(a, QStringLiteral("right"), 0.5), pb = d.addPort(b, QStringLiteral("left"), 0.5);
    c->fit();
    view.setTool(SysTool::Cable);
    const QPointF from = c->portOnScreen(a, pa), to = c->portOnScreen(b, pb);
    click(c, from);
    for (int i = 1; i <= 4; ++i) mouse(c, QEvent::MouseMove, from + (to - from) * (i / 4.0), Qt::NoButton, Qt::NoButton);
    check(d.cables.empty(), "two clicks: the first - the cable started, following the pointer, none put yet");
    click(c, to);
    check(d.cables.size() == 1 && d.cables[0].aPort == pa && d.cables[0].bPort == pb && c->tool() == SysTool::Select,
          "two clicks: the second, on the other port - the cable put, back to Select");
    // Started by a click on a unit, ended on nothing: given up.
    view.setTool(SysTool::Cable);
    click(c, c->unitOnScreen(a));
    click(c, c->toScreen({200, 600}));
    check(d.cables.size() == 1 && d.unit(a)->ports.size() == 1, "two clicks: ended on nothing - given up, no port put");

    // ---- THE KEYBOARD'S DELETE ----
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
    auto* sys = w.findChild<SystemDesignView*>(QStringLiteral("systemDesign"));
    QAction* del = nullptr;
    for (QAction* act : w.findChildren<QAction*>())
        if (act->shortcut() == QKeySequence(QKeySequence::Delete)) del = act;
    check(ribbon && sys && del, "delete key: the window's Delete");
    if (!ribbon || !sys || !del) return;
    ribbon->setCurrentIndex(tab);
    QApplication::processEvents();
    sys->setTool(SysTool::Unit);
    click(sys->canvas(), sys->canvas()->toScreen({100, 100}));
    const QString u = sys->design().units.empty() ? QString() : sys->design().units[0].id;
    sys->canvas()->select(u, QString(), QString());
    del->trigger();
    QApplication::processEvents();
    check(!u.isEmpty() && sys->design().units.empty(), "delete key: on the System design tab - the unit selected deleted");
    ribbon->setCurrentIndex(0);
    QApplication::processEvents();
}

void testSystemEditingAndChecks(const QString& dir) {
    auto drag = [](QWidget* widget, const QPointF& from, const QPointF& to) {
        mouse(widget, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        mouse(widget, QEvent::MouseMove, to, Qt::LeftButton, Qt::NoButton);
        mouse(widget, QEvent::MouseButtonRelease, to, Qt::NoButton, Qt::LeftButton);
        QApplication::processEvents();
    };
    SystemDesignView view;
    view.resize(1150, 780); view.show(); QApplication::processEvents();
    SystemDesign& d = view.design();
    const QString a = d.addUnit(60, 70), b = d.addUnit(330, 70);
    const QString pa = d.addPort(a, QStringLiteral("right"), 0.5, QStringLiteral("X1"));
    const QString pb = d.addPort(b, QStringLiteral("left"), 0.5, QStringLiteral("X2"));
    const QString c = d.addCable(a, pa, b, pb);
    const QString n = d.addNote(80, 300, QStringLiteral("A longwordwithoutspaces must wrap inside this container."));
    d.note(n)->color = QColor(100, 150, 220); d.note(n)->unit = a; d.note(n)->port = pa;
    d.unit(a)->port(pa)->contactPn = QStringLiteral("CONTACT-20");
    auto* canvas = view.canvas(); canvas->update(); QApplication::processEvents();
    const QPointF label = canvas->portLabelOnScreen(a, pa);
    drag(canvas, label, label + QPointF(45, -30));
    check(d.unit(a)->port(pa)->labelDx > 40 && d.unit(a)->port(pa)->labelDy < -25, "port designator: dragged independently above the cable");
    view.undo(); check(d.unit(a)->port(pa)->labelDx == 0, "port designator: drag undone in one step");
    canvas->selectNote(n);
    const QPointF corner = canvas->toScreen({d.note(n)->x + d.note(n)->w, d.note(n)->y + d.note(n)->h});
    drag(canvas, corner - QPointF(1, 1), corner + QPointF(70, 40));
    check(d.note(n)->w > 240 && d.note(n)->h > 100, "note: text container resized by its corner");
    QPointF target;
    check(sysNoteTarget(d, *d.note(n), target) && target == sysPortPoint(*d.unit(a), *d.unit(a)->port(pa)), "note: leader resolves the exact port");
    const QJsonObject saved = d.toJson();
    const SystemDesign back = SystemDesign::fromJson(saved);
    check(back.note(n)->color == d.note(n)->color && back.note(n)->w == d.note(n)->w && back.note(n)->port == pa &&
          back.unit(a)->port(pa)->contactPn == QLatin1String("CONTACT-20"), "saved: note appearance/target and contact P/N round trip");
    const QString xml = systemDrawioXml(d);
    check(xml.contains(n + QStringLiteral("-leader")) && xml.contains(QStringLiteral("#6496dc"), Qt::CaseInsensitive), "export: note leader and custom fill retained");
    canvas->select({}, {}, {});
    drag(canvas, canvas->toScreen({40, 40}), canvas->toScreen({510, 210}));
    check(canvas->selectedUnits().size() == 2 && canvas->selectedCables().contains(c), "blue window: enclosed units and cable selected");
    const double ax = d.unit(a)->x, bx = d.unit(b)->x;
    drag(canvas, canvas->unitOnScreen(a), canvas->unitOnScreen(a) + QPointF(30, 20));
    check(d.unit(a)->x == ax + 30 && d.unit(b)->x == bx + 30, "zone: selected units moved together");
    view.undo();
    check(d.unit(a)->x == ax && d.unit(b)->x == bx, "zone: group move undone in one step");
    drag(canvas, canvas->toScreen({155, 45}), canvas->toScreen({235, 180}));
    check(canvas->selectedUnits().isEmpty(), "blue window: partly enclosed unit excluded");
    drag(canvas, canvas->toScreen({235, 180}), canvas->toScreen({155, 45}));
    check(canvas->selectedUnits().contains(a), "green crossing: partly crossed unit selected");
    canvas->setSelectionMode(SysSelectionMode::Crossing);
    drag(canvas, canvas->toScreen({155, 45}), canvas->toScreen({235, 180}));
    check(canvas->selectedUnits().contains(a), "green mode: crossing also works left to right when explicitly chosen");
    canvas->setSelectionMode(SysSelectionMode::Direction);
    canvas->selectNote(n);
    auto* noteLink = view.findChild<QComboBox*>(QStringLiteral("systemNoteLink"));
    const int cableTarget = noteLink ? noteLink->findData(QStringList{{}, {}, c}) : -1;
    if (noteLink && cableTarget >= 0) { noteLink->setCurrentIndex(cableTarget); noteLink->activated(cableTarget); }
    check(d.note(n)->cable == c && d.note(n)->unit.isEmpty(), "note link: cable chosen from the target control");
    view.undo(); check(d.note(n)->port == pa && d.note(n)->cable.isEmpty(), "note link: target change undone in one step");
    d.unit(b)->name = d.unit(a)->name;
    bool duplicate = false;
    for (const SysIssue& issue : d.check()) duplicate |= issue.rule == QLatin1String("DESIGNATOR") && issue.level == 2;
    check(duplicate, "rules: duplicate unit names identified by stable rule id");
    d.disabledRules << QStringLiteral("DESIGNATOR");
    view.checkDesign();
    bool disabled = false;
    for (const SysIssue& issue : view.issues()) disabled |= issue.rule == QLatin1String("DESIGNATOR");
    check(!disabled && view.findChild<QTableWidget*>(QStringLiteral("systemConnections"))->rowCount() == 1,
          "check: disabled rules excluded, unlinked cable listed in connections");
    const QImage firstFrame = canvas->grab().toImage();
    QElapsedTimer reviewWait; reviewWait.start();
    while (reviewWait.elapsed() < 180) { QApplication::processEvents(); QThread::msleep(10); }
    auto* animation = canvas->findChild<QTimer*>(QStringLiteral("systemCheckAnimation"));
    check(animation && animation->isActive() && firstFrame != canvas->grab().toImage(),
          "rules animation: visible frames change and the review does not finish after a brief flash");
    canvas->grab().save(QDir(dir).filePath(QStringLiteral("system-editing.png")));
    const QString preview = qEnvironmentVariable("EDRAW_SYSTEM_PREVIEW");
    if (!preview.isEmpty()) canvas->grab().save(preview);
}

void testSystemSchematicConnections(const QString& dir) {
    Document doc(QStringLiteral("Named paths"));
    ConnectorSpec sa = defaultConnectorSpec(1), sb = defaultConnectorSpec(1);
    sa.designator = QStringLiteral("X1"); sb.designator = QStringLiteral("X2");
    sa.pins = {QStringLiteral("A")}; sb.pins = {QStringLiteral("B")};
    Entity a; a.id = QStringLiteral("conn-a"); a.type = EntityType::Connector; a.a = {0, 0}; writeConnectorSpec(a.meta, sa);
    Entity b; b.id = QStringLiteral("conn-b"); b.type = EntityType::Connector; b.a = {250, 0}; writeConnectorSpec(b.meta, sb);
    doc.entities().push_back(a); doc.entities().push_back(b);
    const Vec2 ae = connectorGeometry(a.a, sa).pinEnd.front(), be = connectorGeometry(b.a, sb).pinEnd.front();
    Entity w; w.id = QStringLiteral("wire-a"); w.type = EntityType::Wire; w.points = {ae, {150, 50}};
    w.meta[QStringLiteral("wireLabel")] = QStringLiteral("SUPPLY"); doc.entities().push_back(w);
    w.id = QStringLiteral("wire-b"); w.points = {{150, 50}, be}; doc.entities().push_back(w);
    const QString path = QDir(dir).filePath(QStringLiteral("signal-path.dwx"));
    check(dwx::save(doc, path), "schematic: named path fixture written");
    DrawingInfo info = readDrawingInfo(path);
    check(info.schematic && info.connections.size() == 1 && info.connections[0].level == 0 && info.connections[0].name == QLatin1String("SUPPLY"),
          "schematic: joined wires form one named connector-to-connector path");
    check(info.connections[0].fromContact == QLatin1String("A") || info.connections[0].toContact == QLatin1String("A"),
          "schematic: actual contact labels used instead of numeric indices");
    for (Entity& e : doc.entities()) if (e.type == EntityType::Wire) e.meta[QStringLiteral("wireLabel")] = QStringLiteral("SUPPLY / RETURN");
    dwx::save(doc, path); info = readDrawingInfo(path);
    check(info.connections[0].level == 0, "schematic: a slash in a legitimate line name is not a label conflict");
    for (Entity& e : doc.entities()) if (e.type == EntityType::Wire) e.meta[QStringLiteral("wireLabel")] = QStringLiteral("SUPPLY");
    DrawingLayout extra; extra.name = QStringLiteral("Second schematic"); extra.paperEntities = doc.entities();
    for (Entity& e : extra.paperEntities) if (e.type == EntityType::Wire) e.meta[QStringLiteral("wireLabel")] = QStringLiteral("SECOND-SHEET");
    doc.layouts().push_back(extra); dwx::save(doc, path); info = readDrawingInfo(path);
    check(info.connections.size() == 2 && info.connections[0].level == 0 && info.connections[1].level == 0,
          "schematic: independent sheets at identical coordinates never become one electrical path");
    doc.layouts().clear();
    doc.entities().back().meta[QStringLiteral("wireLabel")] = QStringLiteral("RETURN");
    dwx::save(doc, path); info = readDrawingInfo(path);
    check(info.connections.size() == 1 && info.connections[0].level == 2, "schematic: names changing across joined wires are errors");
    doc.entities().back().points.back() = {500, 500}; dwx::save(doc, path);
    info = readDrawingInfo(path);
    check(info.connections[0].issue.contains(QStringLiteral("Open")), "schematic: dangling wire reported as unresolved connection");

    SystemDesign d;
    const QString ua = d.addUnit(0, 0), ub = d.addUnit(300, 0), uc = d.addUnit(600, 0);
    const QString pa = d.addPort(ua, QStringLiteral("right"), .5, QStringLiteral("X1"));
    const QString pb = d.addPort(ub, QStringLiteral("left"), .5, QStringLiteral("X2"));
    const QString pc = d.addPort(uc, QStringLiteral("left"), .5, QStringLiteral("X3"));
    const QString ca = d.addCable(ua, pa, ub, pb), cb = d.addCable(ub, pb, uc, pc);
    d.cable(ca)->drawing = QStringLiteral("one"); d.cable(cb)->drawing = QStringLiteral("two");
    auto reader = [](const QString& file) {
        DrawingInfo result; result.found = result.schematic = true;
        if (file == QLatin1String("one")) result.connections.push_back({QStringLiteral("SUPPLY"), QStringLiteral("X2"), QStringLiteral("1"), QStringLiteral("X1"), QStringLiteral("1"), {}, 0});
        else result.connections.push_back({QStringLiteral("RETURN"), QStringLiteral("X2"), QStringLiteral("1"), QStringLiteral("X3"), QStringLiteral("2"), {}, 0});
        return result;
    };
    const auto rows = d.checkConnections(reader);
    check(rows.size() == 2 && rows[0].level == 2 && rows[1].level == 2 && rows[0].status.contains(QStringLiteral("shared contact")),
          "connections: line names inconsistent across drawings at a shared unit contact flagged on both rows");
    check(rows[0].aUnit == ua && rows[0].bUnit == ub, "connections: reversed schematic endpoints map to the correct units");
    const auto mismatched = d.checkConnections([](const QString&) {
        DrawingInfo result; result.found = result.schematic = true;
        result.connections.push_back({QStringLiteral("SUPPLY"), QStringLiteral("UNKNOWN"), QStringLiteral("1"), QStringLiteral("X2"), QStringLiteral("1"), {}, 0});
        return result;
    });
    check(mismatched[0].level == 2 && mismatched[0].status.contains(QStringLiteral("do not match")),
          "connections: unmatched connector designators reported, never guessed");
}

void testSystemDrcWorkflow() {
    SystemDesignView view; view.resize(1200, 850); view.show(); QApplication::processEvents();
    SystemDesign& d = view.design();
    const QString a = d.addUnit(60, 100, QStringLiteral("U1"));
    const QString b = d.addUnit(4000, 2000, QStringLiteral("U1"));
    const QString pa = d.addPort(a, QStringLiteral("right"), .5);
    const QString pb = d.addPort(b, QStringLiteral("left"), .5);
    const QString c = d.addCable(a, pa, b, pb);
    const QString n = d.addNote(8000, 4000, QStringLiteral("Relink this installation note"));
    d.note(n)->unit = QStringLiteral("removed-unit");
    const QJsonObject before = d.toJson();
    view.checkDesign();
    auto* table = view.findChild<QTableWidget*>(QStringLiteral("systemIssues"));
    check(table && table->rowCount() == static_cast<int>(view.issues().size()) && table->rowCount() > 0,
          "system DRC: every finding listed after checking");
    bool sorted = true;
    for (size_t i=1; i<view.issues().size(); ++i) sorted &= view.issues()[i-1].level >= view.issues()[i].level;
    check(sorted, "system DRC: errors precede warnings and notes");
    int portRow=-1, unitRow=-1, cableRow=-1, noteRow=-1;
    for (int i=0;i<static_cast<int>(view.issues().size());++i) {
        const SysIssue& issue=view.issues()[static_cast<size_t>(i)];
        if (issue.unit==b && issue.port==pb) portRow=i;
        if (issue.unit==b && issue.port.isEmpty()) unitRow=i;
        if (issue.cable==c) cableRow=i;
        if (issue.note==n) noteRow=i;
    }
    const QPointF centre(view.canvas()->width()/2.0, view.canvas()->height()/2.0);
    if (table && portRow>=0) table->cellClicked(portRow,2);
    check(portRow>=0 && view.canvas()->selectedPort()==pb && (view.canvas()->portOnScreen(b,pb)-centre).manhattanLength()<2,
          "system DRC: single click jumps to an offscreen port and centres it");
    auto* flash=view.canvas()->findChild<QTimer*>(QStringLiteral("systemIssueFlash"));
    check(flash && flash->isActive(), "system DRC: the located part flashes like harness DRC");
    if (unitRow>=0) view.locateIssue(unitRow);
    check(unitRow>=0 && view.canvas()->selectedUnit()==b && (view.canvas()->unitOnScreen(b)-centre).manhattanLength()<2,
          "system DRC: unit finding jumps to its unit");
    if (cableRow>=0) view.locateIssue(cableRow);
    check(cableRow>=0 && view.canvas()->selectedCable()==c && (view.canvas()->cableOnScreen(c)-centre).manhattanLength()<2,
          "system DRC: cable finding jumps to its cable");
    if (noteRow>=0) view.locateIssue(noteRow);
    check(noteRow>=0 && view.canvas()->selectedNote()==n && (view.canvas()->noteOnScreen(n)-centre).manhattanLength()<2,
          "system DRC: removed note target still locates the note");
    check(d.toJson()==before && !view.canUndo(), "system DRC: locating findings never changes or dirties the design");
    bool hoverAttached=false;
    QTimer::singleShot(0,&view,[&] {
        auto* dialog=view.findChild<QDialog*>(QStringLiteral("systemDrcRulesDialog"));
        if (!dialog) return;
        auto* title=dialog->findChild<QLabel*>(QStringLiteral("systemDrcRuleName_SCHEMATIC-NAME"));
        if (title) {
            QEvent enter(QEvent::Enter); QApplication::sendEvent(title,&enter);
            hoverAttached=RuleExplainer::instance()->isVisible() && RuleExplainer::instance()->rule()==QLatin1String("SCHEMATIC-NAME");
            QEvent leave(QEvent::Leave); QApplication::sendEvent(title,&leave);
        }
        dialog->reject();
    });
    view.askRules(); check(hoverAttached,"system DRC: hovering a rule opens the shared harness rule explainer");
    for (const char* rule : {"DESIGNATOR","PORT-CONNECTION","PART-NUMBER","DRAWING-LINK","NOTE-LINK","SCHEMATIC-NAME"}) {
        const QString id=QString::fromLatin1(rule);
        check(RuleExplainer::hasScene(id),"system DRC: every system rule has a fault/fix illustration");
    }
    auto* anchor=new QLabel(QStringLiteral("Rule preview"),&view);
    RuleExplainer::attach(anchor,QStringLiteral("SCHEMATIC-NAME"),QStringLiteral("Schematic line names"),QStringLiteral("Same contact, same line name."));
    QEvent enter(QEvent::Enter); QApplication::sendEvent(anchor,&enter);
    auto* explainer=RuleExplainer::instance(); explainer->setPhase(.2); const QImage fault=explainer->grab().toImage();
    explainer->setPhase(.75); const QImage fixed=explainer->grab().toImage();
    check(fault!=fixed,"system DRC: hover animation shows distinct fault and corrected scenes");
    const QString preview=qEnvironmentVariable("EDRAW_SYSTEM_DRC_PREVIEW");
    if (!preview.isEmpty()) { fault.save(preview+QStringLiteral("-fault.png")); fixed.save(preview+QStringLiteral("-fix.png")); view.grab().save(preview+QStringLiteral("-locate.png")); }
    QEvent leave(QEvent::Leave); QApplication::sendEvent(anchor,&leave);
}

void testProjectTrafficBypassAndAppearance() {
    SystemDesignView view; view.resize(1300,900); view.show(); QApplication::processEvents();
    QJsonObject saved; view.setStore([&]{return saved;},[&](const QJsonObject& o){saved=o;});
    check(view.design().background==QColor(48,52,58) && view.canvas()->grab().toImage().pixelColor(3,3)==QColor(48,52,58),
          "canvas: project starts with the dark grey background");
    view.setCanvasColour(QColor(250,251,252));
    check(!saved.isEmpty() && SystemDesign::fromJson(saved).background==QColor(250,251,252),"canvas: colour saved even in an empty project");
    view.undo(); check(view.design().background==QColor(48,52,58),"canvas: colour change undone");
    view.setCanvasColour(QColor(70,80,90));
    SystemDesign& d=view.design(); const QString a=d.addUnit(50,100,QStringLiteral("U1")), b=d.addUnit(450,100,QStringLiteral("U1"));
    d.addPort(a,QStringLiteral("right"),.5);
    view.checkDesign();
    bool yellow=false, green=false, red=false, legend=false;
    QTimer::singleShot(0,&view,[&]{
        auto* dialog=view.findChild<QDialog*>(QStringLiteral("systemDrcRulesDialog")); if(!dialog)return;
        legend=dialog->findChild<QWidget*>(QStringLiteral("drcTrafficLegend"));
        auto* square=dialog->findChild<QToolButton*>(QStringLiteral("systemDrcRule_DESIGNATOR"));
        if(square){ square->click(); yellow=square->property("severity").toInt()==1 && square->styleSheet().contains(QStringLiteral("#e6b84a"));
                    square->click(); green=square->property("severity").toInt()==0;
                    bool active=false;for(const SysIssue& i:view.issues())active|=i.rule==QLatin1String("DESIGNATOR");green=green&&!active;
                    square->click();red=square->property("severity").toInt()==2; }
        const QString image=qEnvironmentVariable("EDRAW_PROJECT_CONTROLS_PREVIEW");if(!image.isEmpty())dialog->grab().save(image+QStringLiteral("-project-rules.png"));
        dialog->reject();
    });
    view.askRules(); check(legend&&yellow&&green&&red,"project rules: traffic lights cycle warning / not checked / error with a legend");
    int row=-1;for(int i=0;i<static_cast<int>(view.issues().size());++i)if(view.issues()[static_cast<size_t>(i)].rule==QLatin1String("DESIGNATOR"))row=i;
    const QString key=row>=0?view.issues()[static_cast<size_t>(row)].key():QString();
    check(row>=0&&view.bypassIssue(row)&&d.bypassed.size()==1,"project bypass: finding accepted into its own bypass table");
    bool hidden=true;for(const SysIssue& i:view.issues())hidden&=i.key()!=key;check(hidden,"project bypass: accepted finding removed from active issues");
    SystemDesignView reopened; reopened.setStore([&]{return saved;},[](const QJsonObject&){}); reopened.checkDesign();
    bool stillHidden=true;for(const SysIssue& i:reopened.issues())stillHidden&=i.key()!=key;
    check(stillHidden&&reopened.design().bypassed.size()==1,"project bypass: acceptance survives reopening the project");
    const SystemDesign copy=SystemDesign::fromJson(saved);
    check(copy.bypassed.size()==1&&copy.bypassed[0].key()==key&&copy.ruleLevels.value(QStringLiteral("DESIGNATOR")).toInt()==2&&copy.background==QColor(70,80,90),
          "project settings: bypass, traffic-light level and custom canvas colour round trip");
    view.showBypass();auto* table=view.findChild<QTableWidget*>(QStringLiteral("systemDrcBypassTable"));
    check(table&&table->rowCount()==1,"project bypass: accepted finding listed in the bypass window");
    auto* restore=table? qobject_cast<QPushButton*>(table->cellWidget(0,3)):nullptr;if(restore)restore->click();
    bool returned=false;for(const SysIssue& i:view.issues())returned|=i.key()==key;
    check(d.bypassed.empty()&&returned,"project bypass: Restore re-flags the original finding");
    view.undo();view.checkDesign();check(d.bypassed.size()==1,"project bypass: restoring can be undone");
    d.unit(b)->name=QStringLiteral("U2");d.addUnit(800,100,QStringLiteral("U2"));view.checkDesign();
    bool newer=true, fresh=false;for(const SysIssue& i:view.issues()){newer&=i.key()!=key;fresh|=i.rule==QLatin1String("DESIGNATOR");}
    check(newer&&fresh,"project bypass: a changed design generates new findings instead of reusing an old acceptance");
    SystemDesign recycled;const QString oldUnit=recycled.addUnit(0,0);
    recycled.bypassed.push_back({2,QStringLiteral("Accepted unit finding"),oldUnit,{},QStringLiteral("DESIGNATOR"),{}, {}});
    recycled.removeUnit(oldUnit);check(recycled.addUnit(0,0)!=oldUnit,"project bypass: a removed accepted part's id is not recycled onto a new part");
    const QString preview=qEnvironmentVariable("EDRAW_PROJECT_CONTROLS_PREVIEW");
    const QString leader=d.addNote(110,330,QStringLiteral("Unit-linked note: leader meets the unit edge"));d.note(leader)->unit=a;
    view.canvas()->clearCheck();view.canvas()->selectNote(leader);
    if(!preview.isEmpty()){view.grab().save(preview+QStringLiteral("-canvas.png"));if(auto* dialog=view.findChild<QDialog*>(QStringLiteral("systemDrcBypassDialog")))dialog->grab().save(preview+QStringLiteral("-bypass.png"));}
    if(auto* dialog=view.findChild<QDialog*>(QStringLiteral("systemDrcBypassDialog")))dialog->close();
    // UNIT LEADERS: intersect the unit boundary along the ray towards the note, in every direction.
    SystemDesign links;const QString uid=links.addUnit(100,100);const QString nid=links.addNote(400,130);links.note(nid)->unit=uid;
    for(const QPointF& pos:{QPointF(400,130),QPointF(-300,130),QPointF(100,-300),QPointF(100,400),QPointF(400,400)}){
        links.note(nid)->x=pos.x();links.note(nid)->y=pos.y();QPointF target;sysNoteTarget(links,*links.note(nid),target);
        const QRectF box=links.unit(uid)->rect();const QPointF ray=QRectF(pos,QSizeF(180,70)).center()-box.center();const QPointF delta=target-box.center();
        const bool edge=std::abs(target.x()-box.left())<1e-6||std::abs(target.x()-box.right())<1e-6||std::abs(target.y()-box.top())<1e-6||std::abs(target.y()-box.bottom())<1e-6;
        check(edge&&box.adjusted(-.001,-.001,.001,.001).contains(target)&&std::abs(delta.x()*ray.y()-delta.y()*ray.x())<1e-5,"note leader: stops at the unit's facing edge");
    }
    const QString port=links.addPort(uid,QStringLiteral("left"),.3);links.note(nid)->port=port;QPointF target;sysNoteTarget(links,*links.note(nid),target);
    check(target==sysPortPoint(*links.unit(uid),*links.unit(uid)->port(port)),"note leader: a port target still meets the exact port");
}

void testSchematicTrafficLights() {
    Document doc; Editor ed; ed.setDocument(&doc);
    ConnectorSpec spec=defaultConnectorSpec(1);spec.designator=QStringLiteral("X1");spec.partNumber.clear();
    Entity connector;connector.id=QStringLiteral("traffic-connector");connector.type=EntityType::Connector;writeConnectorSpec(connector.meta,spec);doc.entities().push_back(connector);
    ed.setSchematicRuleSeverity(QStringLiteral("nopn"),1);
    bool warning=false;for(const ValidationIssue& i:ed.validateSchematic())if(i.key.startsWith(QLatin1String("nopn:")))warning=i.level==1;
    check(warning,"schematic traffic: warning level applied to validation findings");
    ed.setSchematicRuleSeverity(QStringLiteral("nopn"),0);bool absent=true;for(const ValidationIssue& i:ed.validateSchematic())absent&=!i.key.startsWith(QLatin1String("nopn:"));
    check(absent,"schematic traffic: green skips the selected rule");
    ed.setSchematicRuleSeverity(QStringLiteral("nopn"),2);bool error=false;for(const ValidationIssue& i:ed.validateSchematic())if(i.key.startsWith(QLatin1String("nopn:")))error=i.level==2;
    check(error&&ed.drcRuleEnabled(QStringLiteral("nopart")),"schematic traffic: error restored without changing mechanical rules");
    MainWindow w;w.show();QApplication::processEvents();auto* rules=w.findChild<QToolButton*>(QStringLiteral("schematicDrcRules"));
    if(rules)rules->click();auto* dialog=w.findChild<QDialog*>(QStringLiteral("schematicDrcRulesDialog"));
    auto* square=dialog?dialog->findChild<QToolButton*>(QStringLiteral("schematicDrcRule_nopn")):nullptr;
    check(dialog&&square&&dialog->findChild<QWidget*>(QStringLiteral("drcTrafficLegend")),"schematic traffic: the rules window uses the shared light controls and legend");
    if(square){square->click();check(square->property("severity").toInt()==1,"schematic traffic: yellow selected from rules window");square->click();check(square->property("severity").toInt()==0,"schematic traffic: green selected from rules window");square->click();}
    const QString preview=qEnvironmentVariable("EDRAW_PROJECT_CONTROLS_PREVIEW");if(dialog&&!preview.isEmpty())dialog->grab().save(preview+QStringLiteral("-schematic-rules.png"));
    if(dialog)dialog->close();
}

void testDrawingDetailsFields(const QString& directory) {
    Document doc;Editor ed;ed.setDocument(&doc);
    const QJsonObject details{{QStringLiteral("drawingNo"),QStringLiteral("CBL-123")},{QStringLiteral("revision"),QStringLiteral("A")},{QStringLiteral("title"),QStringLiteral("Controller harness")}};
    ed.setDrawingDetails(details);
    for(const QString& key:{QStringLiteral("drawingNo"),QStringLiteral("revision"),QStringLiteral("title")}){
        check(ed.placeDrawingField(key),"drawing details: a declared field can be placed");ed.onPointerPress({20,40+20.0*doc.entities().size()},Qt::LeftButton,Qt::NoModifier);
    }
    check(doc.entities().size()==3&&checkDrawingDetails(doc).empty(),"drawing details: P/N, revision and title placed as matching fields");
    Entity repeated=doc.entities()[0];repeated.id=QStringLiteral("paper-number");doc.layouts()[0].paperEntities.push_back(repeated);
    repeated.id=QStringLiteral("bad-number");repeated.meta[QStringLiteral("text")]=QStringLiteral("CBL-999");doc.layouts()[1].paperEntities.push_back(repeated);
    const auto problems=checkDrawingDetails(doc);check(problems.size()==1&&problems[0].layout==doc.layouts()[1].name,"drawing details: every sheet checked and a conflicting copy located");
    bool schematic=false;for(const ValidationIssue& i:ed.validateSchematic())schematic|=i.key.startsWith(QLatin1String("drawing-fields:"));
    check(schematic,"drawing details: schematic DRC uses the same field consistency check");
    MechanicalValidationDialog mechanical(&ed);mechanical.revalidate();bool physical=false;for(const BrickIssue& i:mechanical.shownHarnessIssues())physical|=i.rule==QLatin1String("drawing-fields");
    check(physical,"drawing details: mechanical DRC checks repeated fields too");
    const QString file=QDir(directory).filePath(QStringLiteral("metadata.dwx"));dwx::save(doc,file);
    DrawingInfo info=readDrawingInfo(file);check(info.drawingNo==QLatin1String("CBL-123")&&info.revision==QLatin1String("A")&&info.title==QLatin1String("Controller harness")&&!info.fieldIssues.isEmpty(),"drawing details: project reads authoritative data and all conflicting copies");
    SystemDesignView project;auto& system=project.design();const QString a=system.addUnit(0,0),b=system.addUnit(400,0);const QString pa=system.addPort(a,QStringLiteral("right"),.5),pb=system.addPort(b,QStringLiteral("left"),.5);const QString cable=system.addCable(a,pa,b,pb);
    project.linkDrawing(cable,file);project.checkDesign();bool flagged=false;for(const SysIssue& i:project.issues())flagged|=i.rule==QLatin1String("DRAWING-FIELDS");check(flagged,"drawing details: linked drawing conflicts appear in project DRC");
    const QJsonObject revised{{QStringLiteral("drawingNo"),QStringLiteral("CBL-124")},{QStringLiteral("revision"),QStringLiteral("B")},{QStringLiteral("title"),QStringLiteral("Controller harness revised")}};
    ed.setDrawingDetails(revised);check(checkDrawingDetails(doc).empty()&&doc.layouts()[1].paperEntities[0].meta.value(QStringLiteral("text")).toString()==QLatin1String("CBL-124"),"drawing details: updates reach every copy including inactive sheets");
    ed.undo();check(doc.drawingDetails()==details&&!checkDrawingDetails(doc).empty()&&doc.layouts()[1].paperEntities[0].meta.value(QStringLiteral("text")).toString()==QLatin1String("CBL-999"),"drawing details: undo restores metadata and previous individual field values atomically");
    ed.redo();check(doc.drawingDetails()==revised&&checkDrawingDetails(doc).empty(),"drawing details: redo restores all matching field copies");
    doc.layouts()[0].paperEntities[0].meta[QStringLiteral("text")]=QStringLiteral("wrong");
    Document paper;paper.entities()=doc.layouts()[0].paperEntities;Editor paperEditor;paperEditor.setDocument(&paper);paperEditor.setDrawingOwner(&doc);
    paperEditor.setDrawingDetails(details);check(doc.drawingDetails()==details&&paper.entities()[0].meta.value(QStringLiteral("text")).toString()==QLatin1String("CBL-123"),"drawing details: paper editing updates the owning drawing");
    paperEditor.undo();check(doc.drawingDetails()==revised&&paper.entities()[0].meta.value(QStringLiteral("text")).toString()==QLatin1String("wrong"),"drawing details: paper undo preserves the owner's metadata and paper field state");
    MainWindow window;window.show();QApplication::processEvents();auto* editor=window.findChild<Editor*>();
    bool annotation=false, harness=false;for(QToolButton* button:window.findChildren<QToolButton*>(QStringLiteral("drawingDetailsButton"))){annotation|=button->toolButtonStyle()==Qt::ToolButtonTextUnderIcon;harness|=button->toolButtonStyle()==Qt::ToolButtonIconOnly;}
    check(annotation&&harness&&editor,"drawing details: commands present on Annotation and harness annotation");
}

void testPdfViewerAndProgress(const QString& directory) {
    const QString file=QDir(directory).filePath(QStringLiteral("viewer-pages.pdf"));
    {
        QPdfWriter writer(file);writer.setResolution(72);QPainter painter(&writer);painter.setFont(QFont(QStringLiteral("Arial"),14));painter.drawText(30,70,QStringLiteral("First page: no requested part"));
        writer.newPage();painter.drawText(30,70,QStringLiteral("Second page: ABC-123"));painter.fillRect(QRect(40,100,120,60),QColor(30,170,70));
        writer.newPage();painter.drawText(30,70,QStringLiteral("Third page: ABC 123"));
    }
    QFile raw(file);raw.open(QIODevice::ReadOnly);const auto pages=pdf::readTextPages(raw.readAll());
    check(pages.size()==3&&pages[1].text.contains(QStringLiteral("ABC-123")),"PDF search: text assigned to the actual page with scoped font mappings");
    const auto rendered=std::async(std::launch::async,[&]{return renderNativePdf(file,1,700);}).get();
    check(rendered.count==3&&!rendered.image.isNull(),"PDF viewer: Windows renders real pages including images and fonts");
    DrawingFinderDialog finder;finder.resize(1400,900);finder.show();finder.setFolder(directory);finder.setKinds({QStringLiteral("pdf")});finder.setText(QStringLiteral("ABC-123"));finder.setMatching(false,false,true);finder.search(true);
    auto* progress=finder.findChild<QProgressBar*>(QStringLiteral("finderProgress"));
    check(progress&&progress->minimum()==0&&progress->maximum()==100&&progress->value()==100,"finder: completed search reaches a determinate 100 percent");
    int row=-1;for(int i=0;i<finder.resultCount();++i)if(finder.resultPath(i)==file)row=i;
    check(row>=0&&finder.resultHits(row)==2,"PDF search: finds inside multiple pages with separator-aware matching like Partseek");
    if(row>=0)finder.showResult(row);check(finder.previewCount(true)==3,"PDF viewer: finder previews the actual multi-page PDF");
    auto* viewer=finder.findChild<PdfViewer*>(QStringLiteral("finderPdfViewer"));
    check(viewer&&viewer->currentPage()==2,"PDF viewer: opens the first page with a match");
    if(viewer){viewer->jumpHit(1);QElapsedTimer wait;wait.start();while(viewer->loading()&&wait.elapsed()<20000){QApplication::processEvents();QThread::msleep(10);}check(viewer->currentPage()==3&&viewer->ready(),"PDF viewer: next-hit navigation moves to the other matching page");}
    const QString preview=qEnvironmentVariable("EDRAW_UPGRADE_PREVIEW");if(!preview.isEmpty())finder.grab().save(preview+QStringLiteral("-finder.png"));
}

void testSystemModuleLinksAndRoutes() {
    SystemDesignView view;view.resize(1350,1000);view.show();QApplication::processEvents();SystemDesign& d=view.design();auto* canvas=view.canvas();
    const QString a=d.addUnit(100,100),b=d.addUnit(700,100),obstacle=d.addUnit(370,50);d.unit(obstacle)->w=170;d.unit(obstacle)->h=220;
    const QString pa=d.addPort(a,QStringLiteral("right"),.5),pb=d.addPort(b,QStringLiteral("left"),.5);const QString c=d.addCable(a,pa,b,pb);
    const auto routed=sysCablePath(d,*d.cable(c));check(!systemPathCrossesBox(routed,d.unit(obstacle)->rect()),"system routing: connected route avoids an intervening unit body");
    bool square=true;for(size_t i=1;i<routed.size();++i)square&=std::abs(routed[i].x()-routed[i-1].x())<.001||std::abs(routed[i].y()-routed[i-1].y())<.001;
    check(square,"system routing: all cable legs remain orthogonal");
    const QString note=d.addNote(120,390,QStringLiteral("This note follows its linked target"));canvas->selectNote(note);
    QKeyEvent link(QEvent::KeyPress,Qt::Key_L,Qt::NoModifier);QApplication::sendEvent(canvas,&link);click(canvas,canvas->portOnScreen(a,pa));
    check(d.note(note)->unit==a&&d.note(note)->port==pa&&canvas->tool()==SysTool::Select,"system link: L links the selected note to the clicked port");
    view.undo();check(d.note(note)->unit.isEmpty(),"system link: linked target assignment undone in one step");
    canvas->selectNote(note);canvas->startNoteLink();click(canvas,canvas->cableOnScreen(c));check(d.note(note)->cable==c,"system link: L can link to a particular cable");
    canvas->selectNote(note);canvas->startNoteLink();click(canvas,canvas->unitOnScreen(b));check(d.note(note)->unit==b&&d.note(note)->port.isEmpty(),"system link: L can link to a particular unit");
    auto drag=[&](const QPointF& from,const QPointF& to){mouse(canvas,QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);mouse(canvas,QEvent::MouseMove,to,Qt::LeftButton,Qt::NoButton);mouse(canvas,QEvent::MouseButtonRelease,to,Qt::NoButton,Qt::LeftButton);QApplication::processEvents();};
    const QPointF cable=canvas->cableOnScreen(c);drag(cable,cable+QPointF(0,80));
    check(!d.cable(c)->waypoints.empty()&&d.cable(c)->aPort==pa&&d.cable(c)->bPort==pb,"system route: a connected cable can be dragged while port identities stay attached");
    view.undo();check(d.cable(c)->waypoints.empty(),"system route: dragging its bends undone atomically");
    d.moveUnit(b,-1000,0);arrangeSystemPorts(d,{b});check(d.unit(a)->port(pa)->side==QLatin1String("left")&&d.unit(b)->port(pb)->side==QLatin1String("right"),"system routing: ports move to the nearest clear facing sides when a unit changes sides");
    check(!systemPathCrossesBox(sysCablePath(d,*d.cable(c)),d.unit(a)->rect()),"system routing: the rerouted cable does not cross its own unit");
    canvas->setNewUnitKind(QStringLiteral("module"));view.setTool(SysTool::Unit);click(canvas,canvas->toScreen({550,470}));
    const QString module=canvas->selectedUnit();check(d.unit(module)&&d.unit(module)->kind==QLatin1String("module")&&d.unit(module)->opacity<.2,"system module: dashed grouping unit starts with transparent fill");
    view.setUnitOpacity(0);const SystemDesign copy=SystemDesign::fromJson(d.toJson());check(copy.unit(module)&&copy.unit(module)->opacity==0,"system module: fully transparent fill persists through save/load");
    const QString xml=systemDrawioXml(d);check(xml.contains(QStringLiteral("dashed=1"))&&xml.contains(QStringLiteral("[System module]")),"system module: dashed style and module identity preserved in export");
    const QString preview=qEnvironmentVariable("EDRAW_UPGRADE_PREVIEW");if(!preview.isEmpty()){canvas->fit();view.grab().save(preview+QStringLiteral("-system.png"));}
}

// Deleting a container must preserve unselected descendants when the user chooses to keep them.
void testParentDeleteChoices() {
    SystemDesign original;
    const QString outer=original.addUnit(10,20,QStringLiteral("Outer"));
    const QString parent=original.addUnit(40,60,QStringLiteral("Parent"));original.unit(parent)->parent=outer;
    const QString child=original.addUnit(75,95,QStringLiteral("Child"));original.unit(child)->parent=parent;
    const QString grandchild=original.addUnit(90,115,QStringLiteral("Grandchild"));original.unit(grandchild)->parent=child;
    const QString peer=original.addUnit(420,60,QStringLiteral("Peer"));
    const QString cp=original.addPort(child,QStringLiteral("right"),.5),pp=original.addPort(peer,QStringLiteral("left"),.5);
    const QString retained=original.addCable(child,cp,peer,pp);
    original.cable(retained)->waypoints={{250,95},{250,170}};
    const QString own=original.addPort(parent,QStringLiteral("right"),.5);
    const QString removed=original.addCable(parent,own,peer,pp);
    const QString note=original.addNote(200,200,QStringLiteral("Keep this note"));original.note(note)->unit=child;
    const QRectF childRect=original.unit(child)->rect();
    SystemDesign model=original;model.removeUnit(parent,false);
    check(!model.unit(parent)&&model.unit(child)&&model.unit(child)->parent==outer&&model.unit(child)->rect()==childRect,
          "parent delete: keep contents promotes the child without moving it");
    check(model.unit(grandchild)&&model.unit(grandchild)->parent==child&&model.cable(retained)&&!model.cable(removed)&&model.note(note)->unit==child,
          "parent delete: descendants, retained cables and linked notes survive; deleted-unit cables do not");
    check(model.cable(retained)&&model.cable(retained)->waypoints==original.cable(retained)->waypoints,
          "parent delete: preserved harness route stays unchanged");
    model.removeUnit(outer,false);
    check(model.unit(child)&&model.unit(child)->parent.isEmpty(),"parent delete: removing the outer container promotes children to the canvas");
    model=original;model.removeUnit(child,false);model.removeUnit(parent,false);
    check(model.unit(grandchild)&&model.unit(grandchild)->parent==outer,"parent delete: separately selected nested parents promote to the surviving ancestor");
    const SystemDesign saved=SystemDesign::fromJson(model.toJson());
    check(saved.unit(grandchild)&&saved.unit(grandchild)->parent==outer,"parent delete: promoted hierarchy survives serialization");

    SystemDesignView view;QJsonObject stored=original.toJson();
    view.setStore([&]{return stored;},[&](const QJsonObject& next){stored=next;});
    auto answer=[&](const QString& buttonName){
        QTimer::singleShot(0,&view,[&,buttonName]{
            auto* dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            check(dialog&&dialog->objectName()==QLatin1String("systemDeleteUnitDialog"),"parent delete: real choice dialog opens");
            if(!dialog)return;
            auto* button=dialog->findChild<QPushButton*>(buttonName);
            check(button!=nullptr,"parent delete: requested action is present");
            if(button)button->click();else dialog->reject();
        });
    };
    view.canvas()->select(parent,{},{});answer(QStringLiteral("cancelDeleteUnit"));
    check(!view.deleteSelected()&&view.design().toJson()==original.toJson()&&!view.canUndo(),"parent delete: Cancel changes neither drawing nor undo history");
    answer(QStringLiteral("deleteUnitKeepContents"));
    check(view.deleteSelected()&&!view.design().unit(parent)&&view.design().unit(child)&&view.design().unit(child)->parent==outer,
          "parent delete: selected-only button keeps and reparents the inner units");
    view.undo();check(view.design().toJson()==original.toJson(),"parent delete: one Undo restores parent, descendants and cables exactly");
    view.redo();check(!view.design().unit(parent)&&view.design().unit(child),"parent delete: Redo repeats selected-only deletion");
    view.undo();view.canvas()->select(parent,{},{});answer(QStringLiteral("deleteUnitWithContents"));
    check(view.deleteSelected()&&!view.design().unit(parent)&&!view.design().unit(child)&&!view.design().unit(grandchild)&&view.design().cables.empty(),
          "parent delete: all-inside choice removes the complete subtree and its cables");
    view.undo();check(view.design().toJson()==original.toJson(),"parent delete: recursive deletion also restores in one Undo");
}

// IMPORTED FROM DRAW.IO AND VISIO, CHECKS OF THEIR OWN PART, CTRL+CLICK TO TAKE THE TOOL, THE WIZARD'S EYES.
void testImportScopesCtrl(const QString& base) {
    const QString dir = QDir(base).filePath(QStringLiteral("import"));
    QDir().mkpath(dir);
    SystemDesign d;
    const QString box = d.addUnit(0, 0), board = d.addUnit(40, 30), other = d.addUnit(400, 0);
    d.unit(box)->w = 260;
    d.unit(box)->h = 200;
    d.unit(board)->kind = QStringLiteral("pcba");
    d.unit(board)->parent = box;
    d.unit(box)->pn = QStringLiteral("ENC-1");
    d.unit(box)->rev = QStringLiteral("B");
    const QString p1 = d.addPort(box, QStringLiteral("right"), 0.5, QStringLiteral("J1"));
    const QString p2 = d.addPort(other, QStringLiteral("left"), 0.5, QStringLiteral("J2"));
    d.unit(box)->port(p1)->connector = QStringLiteral("M12-8");
    const QString w = d.addCable(box, p1, other, p2);
    d.cable(w)->pn = QStringLiteral("CBL-007");
    d.addNote(0, 260, QStringLiteral("Keep clear"));
    auto same = [&](const SystemDesign& got, const char* how) {
        int pcba = 0, inside = 0;
        for (const SysUnit& u : got.units) {
            pcba += u.kind == QLatin1String("pcba") ? 1 : 0;
            inside += u.parent.isEmpty() ? 0 : 1;
        }
        const SysUnit* enc = nullptr;
        for (const SysUnit& u : got.units)
            if (u.name == d.unit(box)->name) enc = &u;
        size_t ports = 0;
        for (const SysUnit& u : got.units) ports += u.ports.size();
        std::printf("  import %s: %d unit(s) (%d PCBA, %d inside another), %d port(s), %d cable(s), %d note(s)\n", how,
                    static_cast<int>(got.units.size()), pcba, inside, static_cast<int>(ports), static_cast<int>(got.cables.size()),
                    static_cast<int>(got.notes.size()));
        return got.units.size() == 3 && pcba == 1 && inside == 1 && ports == 2 && got.cables.size() == 1 && got.notes.size() == 1 && enc &&
               enc->pn == QLatin1String("ENC-1") && enc->rev == QLatin1String("B") && got.cables[0].pn == QLatin1String("CBL-007");
    };
    // ---- DRAW.IO: plain, and compressed as draw.io keeps a page ----
    const QString drawio = QDir(dir).filePath(QStringLiteral("rig.drawio"));
    exportSystemDrawio(d, drawio);
    SystemDesign got;
    check(importSystemDiagram(drawio, got) && same(got, "draw.io"),
          "import: draw.io - units (one a PCBA in its housing), ports, the cable with its P/N, the note");
    {
        const QString xml = systemDrawioXml(d);
        const int a = xml.indexOf(QLatin1String("<mxGraphModel")), b = xml.indexOf(QLatin1String("</mxGraphModel>")) + 15;
        const QByteArray model = QUrl::toPercentEncoding(xml.mid(a, b - a));
        const QByteArray zlib = qCompress(model, 9);
        const QByteArray raw = zlib.mid(6, zlib.size() - 10);   // Qt's length, zlib's header and checksum off: raw DEFLATE
        const QByteArray packed = QByteArrayLiteral("<mxfile><diagram id=\"a\" name=\"Page-1\">") + raw.toBase64() + QByteArrayLiteral("</diagram></mxfile>");
        SystemDesign zipped;
        check(importSystemDrawioXml(packed, zipped) && same(zipped, "draw.io compressed"), "import: a compressed draw.io page");
    }
    // A diagram drawn in draw.io itself: two boxes, a line between them (no ports) - a port put on each.
    {
        const QByteArray own = R"(<mxfile><diagram name="P"><mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/>
<mxCell id="a" value="PSU&lt;br&gt;PS-24 rev C" style="rounded=1;whiteSpace=wrap;html=1;" vertex="1" parent="1"><mxGeometry x="40" y="40" width="120" height="60" as="geometry"/></mxCell>
<mxCell id="b" value="Controller" style="rounded=0;" vertex="1" parent="1"><mxGeometry x="300" y="40" width="120" height="80" as="geometry"/></mxCell>
<mxCell id="e" value="W5 CBL-100" style="edgeStyle=orthogonalEdgeStyle;" edge="1" parent="1" source="a" target="b"><mxGeometry relative="1" as="geometry"/></mxCell>
</root></mxGraphModel></diagram></mxfile>)";
        SystemDesign mine;
        const bool ok = importSystemDrawioXml(own, mine);
        const SysUnit* psu = ok && !mine.units.empty() ? &mine.units[0] : nullptr;
        for (const SysUnit& u : mine.units)
            std::printf("  own draw.io: unit %s pn %s rev %s, %d port(s)%s\n", qPrintable(u.name), qPrintable(u.pn), qPrintable(u.rev),
                        static_cast<int>(u.ports.size()), u.ports.empty() ? "" : qPrintable(QStringLiteral(" on ") + u.ports[0].side));
        for (const SysCable& c : mine.cables) std::printf("  own draw.io: cable %s pn %s\n", qPrintable(c.name), qPrintable(c.pn));
        check(ok && mine.units.size() == 2 && psu && psu->name == QLatin1String("PSU") && psu->pn == QLatin1String("PS-24") &&
                  psu->rev == QLatin1String("C") && mine.cables.size() == 1 && mine.cables[0].name == QLatin1String("W5") &&
                  mine.cables[0].pn == QLatin1String("CBL-100") && mine.units[0].ports.size() == 1 && mine.units[1].ports.size() == 1 &&
                  mine.units[0].ports[0].side == QLatin1String("right") && mine.units[1].ports[0].side == QLatin1String("left"),
              "import: a draw.io diagram of its own - boxes as units, the line a cable, a port put where it meets each box");
    }
    // ---- VISIO: as Edraw writes it, and a drawing with a connector glued to two shapes ----
    const QString vsdx = QDir(dir).filePath(QStringLiteral("rig.vsdx"));
    exportSystemVsdx(d, vsdx);
    SystemDesign fromVisio;
    check(importSystemDiagram(vsdx, fromVisio) && same(fromVisio, "Visio"), "import: Visio - the same units, ports, cable and note");
    {
        // A page with two boxes and a 1-D connector glued to them, stored in a package as Visio writes one.
        const QByteArray page = R"(<?xml version="1.0"?><PageContents xmlns="http://schemas.microsoft.com/office/visio/2012/main"><Shapes>
<Shape ID="1" Type="Shape"><Cell N="PinX" V="2"/><Cell N="PinY" V="6"/><Cell N="Width" V="1.5"/><Cell N="Height" V="1"/><Text>Sensor</Text></Shape>
<Shape ID="2" Type="Shape"><Cell N="PinX" V="6"/><Cell N="PinY" V="6"/><Cell N="Width" V="1.5"/><Cell N="Height" V="1"/><Text>ECU</Text></Shape>
<Shape ID="3" Type="Shape"><Cell N="BeginX" V="2.75"/><Cell N="BeginY" V="6"/><Cell N="EndX" V="5.25"/><Cell N="EndY" V="6"/><Text>W9</Text></Shape>
</Shapes><Connects><Connect FromSheet="3" FromCell="BeginX" ToSheet="1"/><Connect FromSheet="3" FromCell="EndX" ToSheet="2"/></Connects></PageContents>)";
        // The package: the page stored in a zip of its own.
        auto zipOne = [](const QString& name, const QByteArray& data) {
            QByteArray out, central;
            auto put16 = [](QByteArray& b, int v) { b.append(char(v & 0xFF)); b.append(char((v >> 8) & 0xFF)); };
            auto put32 = [&](QByteArray& b, quint32 v) { put16(b, int(v & 0xFFFF)); put16(b, int(v >> 16)); };
            quint32 crc = 0xFFFFFFFFu;
            for (const char ch : data) {
                crc ^= static_cast<quint8>(ch);
                for (int k = 0; k < 8; ++k) crc = crc & 1 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
            }
            crc ^= 0xFFFFFFFFu;
            const QByteArray n = name.toUtf8();
            put32(out, 0x04034b50); put16(out, 20); put16(out, 0); put16(out, 0); put16(out, 0); put16(out, 0);
            put32(out, crc); put32(out, quint32(data.size())); put32(out, quint32(data.size())); put16(out, int(n.size())); put16(out, 0);
            out += n; out += data;
            put32(central, 0x02014b50); put16(central, 20); put16(central, 20); put16(central, 0); put16(central, 0); put16(central, 0);
            put16(central, 0); put32(central, crc); put32(central, quint32(data.size())); put32(central, quint32(data.size()));
            put16(central, int(n.size())); put16(central, 0); put16(central, 0); put16(central, 0); put16(central, 0); put32(central, 0);
            put32(central, 0); central += n;
            const quint32 at = quint32(out.size());
            out += central;
            put32(out, 0x06054b50); put16(out, 0); put16(out, 0); put16(out, 1); put16(out, 1); put32(out, quint32(central.size()));
            put32(out, at); put16(out, 0);
            return out;
        };
        const QString path = QDir(dir).filePath(QStringLiteral("glued.vsdx"));
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(zipOne(QStringLiteral("visio/pages/page1.xml"), page));
        f.close();
        SystemDesign glued;
        check(importSystemDiagram(path, glued) && glued.units.size() == 2 && glued.cables.size() == 1 &&
                  glued.cables[0].name == QLatin1String("W9") && glued.units[0].name == QLatin1String("Sensor") &&
                  glued.units[0].ports.size() == 1 && glued.units[0].ports[0].side == QLatin1String("right"),
              "import: a Visio connector glued to two shapes - a cable between them, a port put on each");
    }
    SystemDesign none;
    check(!importSystemDiagram(QDir(dir).filePath(QStringLiteral("rig.txt")), none), "import: not a diagram - said so");

    // ---- FROM THE TAB: in place of the design, one step to undo ----
    QJsonObject stored;
    SystemDesignView view;
    view.setStore([&] { return stored; }, [&](const QJsonObject& o) { stored = o; });
    view.resize(1100, 700);
    view.show();
    QApplication::processEvents();
    check(view.importDesign(drawio) && view.design().units.size() == 3, "import: from the tab");
    view.undo();
    check(view.design().units.empty(), "import: Undo takes it back");
    view.redo();

    // ---- CHECKS OF THEIR OWN PART ----
    auto rules = [&](const char* label) {
        QStringList seen;
        for (const SysIssue& i : view.issues())
            if (!seen.contains(i.rule)) seen << i.rule;
        std::printf("  %s: %s\n", label, qPrintable(seen.join(QStringLiteral(", "))));
        return seen;
    };
    view.checkDesign();
    const QStringList all = rules("check design");
    view.checkRevisions();
    const QStringList revs = rules("check revisions");
    view.checkSchematics();
    const QStringList conns = rules("connections");
    bool revOnly = true, connOnly = true;
    for (const QString& r : revs) revOnly = revOnly && (r == QLatin1String("DRAWING-LINK") || r == QLatin1String("DRAWING-FIELDS"));
    for (const QString& r : conns) connOnly = connOnly && (r == QLatin1String("PORT-CONNECTION") || r == QLatin1String("SCHEMATIC-NAME"));
    check(all.size() > revs.size() && all.size() > conns.size() && revOnly && connOnly,
          "checks: Check revisions only the revisions, Connections only the connections, Check design all of it");

    // ---- CTRL+CLICK: THE TOOL OF WHAT WAS CLICKED ----
    SystemCanvas* c = view.canvas();
    c->fit();
    QApplication::processEvents();
    auto ctrlClick = [&](const QPointF& at) {
        QMouseEvent down(QEvent::MouseButtonPress, at, c->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
        QMouseEvent up(QEvent::MouseButtonRelease, at, c->mapToGlobal(at), Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
        QApplication::sendEvent(c, &down);
        QApplication::sendEvent(c, &up);
        QApplication::processEvents();
    };
    const SystemDesign& vd = view.design();
    QString pcbaId;
    for (const SysUnit& u : vd.units)
        if (u.kind == QLatin1String("pcba")) pcbaId = u.id;
    view.setTool(SysTool::Select);
    ctrlClick(c->unitOnScreen(pcbaId));
    const bool unitTool = c->tool() == SysTool::Unit && c->newUnitKind() == QLatin1String("pcba");
    view.setTool(SysTool::Select);
    ctrlClick(c->cableOnScreen(vd.cables[0].id));
    const bool cableTool = c->tool() == SysTool::Cable;
    view.setTool(SysTool::Select);
    const SysUnit* withPort = nullptr;
    for (const SysUnit& u : vd.units)
        if (!u.ports.empty()) withPort = &u;
    ctrlClick(c->portOnScreen(withPort->id, withPort->ports[0].id));
    const bool portTool = c->tool() == SysTool::Port;
    check(unitTool && cableTool && portTool, "ctrl+click: a unit - a unit of its kind to put; a cable - Cable; a port - Port");
    view.setTool(SysTool::Select);

    // ---- THE EYES, AS THE MECHANICAL WIZARD'S ----
    c->select(vd.units[0].id, QString(), QString());
    auto* eye = view.findChild<QToolButton*>(QStringLiteral("systemUnitNameEye"));
    check(eye && !eye->icon().isNull() && eye->icon().pixmap(20, 20, QIcon::Normal, QIcon::On).toImage() == eyeIcon(true).pixmap(20, 20).toImage(),
          "eyes: drawn as the mechanical wizard's");
}

// THE CHECK CARD OF EACH CHECK, THE ANIMATION HELP SWITCH, PORT DESCRIPTIONS, THE NOTE'S LINK EYE AND SPLIT, DESIGNATOR COLOUR.
void testCheckCardsHelpNotesDesignators() {
    QJsonObject stored;
    SystemDesignView view;
    view.setStore([&] { return stored; }, [&](const QJsonObject& o) { stored = o; });
    view.resize(1100, 700);
    view.show();
    QApplication::processEvents();
    SystemDesign& d = view.design();
    const QString a = d.addUnit(0, 0, QStringLiteral("U1")), b = d.addUnit(400, 0, QStringLiteral("U2"));
    const QString pa = d.addPort(a, QStringLiteral("right"), .5, QStringLiteral("J1")), pb = d.addPort(b, QStringLiteral("left"), .5, QStringLiteral("J2"));
    d.addCable(a, pa, b, pb);
    stored = view.design().toJson();
    view.reload();

    // ---- EACH CHECK'S CARD LISTS WHAT IT CHECKED, AND NOTHING ELSE ----
    view.checkRevisions();
    const QStringList revisionCard = view.canvas()->checkedRules();
    view.checkSchematics();
    const QStringList connectionCard = view.canvas()->checkedRules();
    view.checkDesign();
    const QStringList designCard = view.canvas()->checkedRules();
    std::printf("  check cards: revisions [%s], connections [%s], design %d rule(s)\n", qPrintable(revisionCard.join(QStringLiteral(", "))),
                qPrintable(connectionCard.join(QStringLiteral(", "))), static_cast<int>(designCard.size()));
    check(revisionCard == QStringList{QStringLiteral("DRAWING-LINK"), QStringLiteral("DRAWING-FIELDS")},
          "check card: Check revisions reviews the revision rules only");
    check(connectionCard == QStringList{QStringLiteral("PORT-CONNECTION"), QStringLiteral("SCHEMATIC-NAME")},
          "check card: Connections reviews the connection rules only");
    check(designCard.size() == 8, "check card: Check design reviews every rule");

    // ---- ANIMATION HELP: OFF, HOVERING SHOWS NOTHING; ON, IT SHOWS - OUTSIDE THE WINDOW WHERE ASKED ----
    {
        QDialog window;
        window.resize(400, 300);
        window.move(100, 100);
        auto* lay = new QVBoxLayout(&window);
        QCheckBox* toggle = RuleExplainer::addHelpToggle(lay, &window);
        auto* hovered = new QLabel(QStringLiteral("Port connections"), &window);
        lay->addWidget(hovered);
        RuleExplainer::attachOutside(hovered, &window, QStringLiteral("PORT-CONNECTION"), QStringLiteral("Port connections"), QStringLiteral("One cable a side."));
        window.show();
        QApplication::processEvents();
        toggle->setChecked(false);
        QEvent enter(QEvent::Enter);
        QApplication::sendEvent(hovered, &enter);
        const bool hiddenWhenOff = !RuleExplainer::instance()->isVisible();
        toggle->setChecked(true);
        QApplication::sendEvent(hovered, &enter);
        QApplication::processEvents();
        const bool shownWhenOn = RuleExplainer::instance()->isVisible();
        const int x = RuleExplainer::instance()->geometry().left();
        const QRect frame = window.frameGeometry();
        const bool outside = x >= frame.right() || RuleExplainer::instance()->geometry().right() <= frame.left();
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(hovered, &leave);
        check(toggle->objectName() == QLatin1String("drcShowAnimationHelp") && hiddenWhenOff && shownWhenOn,
              "animation help: its tick box - off, hovering a rule shows nothing; on, the animation");
        check(outside, "animation help: shown outside the rules window, not over it");
        check(RuleExplainer::helpShown(), "animation help: the choice remembered");
    }
    // In each rules window: the schematic's, the harness DRC's and the system's.
    {
        MainWindow window;
        window.show();
        QApplication::processEvents();
        auto* editor = window.findChild<Editor*>();
        MechanicalValidationDialog mechanical(editor);
        mechanical.show();
        QApplication::processEvents();
        bool harness = false, system = false;
        for (QPushButton* b : mechanical.findChildren<QPushButton*>())
            if (b->text().contains(QLatin1String("Rules"), Qt::CaseInsensitive)) { b->click(); break; }
        QApplication::processEvents();
        for (QWidget* w : QApplication::topLevelWidgets())
            if (w->objectName() == QLatin1String("harnessDrcRulesDialog")) harness = w->findChild<QCheckBox*>(QStringLiteral("drcShowAnimationHelp")) != nullptr;
        QTimer::singleShot(0, [&] {
            if (QWidget* modal = QApplication::activeModalWidget()) {
                system = modal->findChild<QCheckBox*>(QStringLiteral("drcShowAnimationHelp")) != nullptr;
                modal->close();
            }
        });
        view.askRules();
        std::printf("  animation help tick box: harness DRC rules %d, system rules %d\n", harness ? 1 : 0, system ? 1 : 0);
        check(harness && system, "animation help: in the harness DRC and system rules windows");
    }

    // ---- A PORT'S DESCRIPTION: typed in the ports table, kept in the file ----
    view.canvas()->select(a, QString(), QString());
    QApplication::processEvents();
    auto* ports = view.findChild<QTableWidget*>(QStringLiteral("systemPorts"));
    if (ports && ports->rowCount() > 0) ports->setItem(0, 4, new QTableWidgetItem(QStringLiteral("CAN bus")));
    const SystemDesign back = SystemDesign::fromJson(view.design().toJson());
    check(ports && ports->columnCount() == 5 && view.design().unit(a)->ports[0].description == QLatin1String("CAN bus") &&
              back.unit(a)->ports[0].description == QLatin1String("CAN bus"),
          "port: a description of its own, typed in the table and saved");

    // ---- THE NOTE: AN EYE FOR ITS LINK LINE, A LINE TO DRAG BETWEEN ITS TEXT AND ITS PROPERTIES ----
    const QString n = view.design().addNote(0, 200, QStringLiteral("Keep clear"));
    view.design().note(n)->unit = a;
    stored = view.design().toJson();
    view.reload();
    view.canvas()->select(QString(), QString(), QString());
    view.canvas()->selectNote(n);
    QApplication::processEvents();
    auto* eye = view.findChild<QToolButton*>(QStringLiteral("systemNoteLinkEye"));
    if (eye && eye->isEnabled()) eye->click();
    const bool hidden = view.design().note(n) && !view.design().note(n)->showLink && view.design().note(n)->unit == a;
    const SystemDesign saved = SystemDesign::fromJson(view.design().toJson());
    view.undo();
    check(eye && hidden && saved.note(n) && !saved.note(n)->showLink && view.design().note(n)->showLink,
          "note: its link line's eye - hidden with the link kept, saved, one step to undo");
    auto* split = view.findChild<QSplitter*>(QStringLiteral("systemNoteSplit"));
    bool resized = false;
    if (split && split->count() == 2) {
        const QList<int> before = split->sizes();
        // The line dragged up: the properties given more room, the text less (it cannot go under the form's own height).
        split->setSizes({before[0] - 60, before[1] + 60});
        QApplication::processEvents();
        resized = split->sizes()[1] > before[1] && split->sizes()[0] < before[0];
    }
    check(split && split->count() == 2 && split->widget(0) == view.findChild<QPlainTextEdit*>(QStringLiteral("systemNoteText")) && resized,
          "note: the text above, its properties below, the line between them dragged");

    // ---- DESIGNATORS: AUTO, OR ONE COLOUR SET FOR ALL ----
    check(!view.design().designatorColor.isValid(), "designators: Auto to start with");
    view.setDesignatorColour(QColor(200, 30, 30));
    const SystemDesign coloured = SystemDesign::fromJson(view.design().toJson());
    check(coloured.designatorColor == QColor(200, 30, 30), "designators: a colour of their own, saved with the design");
    view.setDesignatorColour(QColor());
    check(!view.design().designatorColor.isValid() && !view.design().toJson().contains(QStringLiteral("designatorColor")),
          "designators: back to Auto - nothing written");
    // AUTO AGAINST WHAT IS BEHIND: a port's name dragged off a pale unit onto the dark canvas turns light.
    {
        view.canvas()->select(QString(), QString(), QString());
        view.canvas()->fit();
        QApplication::processEvents();
        SysPort* port = view.design().unit(a)->port(pa);
        auto inkAt = [&]() {
            const QImage shot = view.canvas()->grab().toImage();
            const QPointF at = view.canvas()->portLabelOnScreen(a, pa);
            const QRect box(static_cast<int>(at.x()) - 12, static_cast<int>(at.y()) - 6, 24, 12);
            int lightest = 0, darkest = 255;   // the text's own ink, against the ground
            const int dpr = static_cast<int>(shot.devicePixelRatio());
            for (int y = box.top(); y <= box.bottom(); ++y)
                for (int x = box.left(); x <= box.right(); ++x) {
                    const QColor c = shot.pixelColor(x * dpr, y * dpr);
                    lightest = std::max(lightest, c.lightness());
                    darkest = std::min(darkest, c.lightness());
                }
            return std::make_pair(lightest, darkest);
        };
        port->labelDx = -60;   // well inside the pale unit
        view.canvas()->update();
        const auto onUnit = inkAt();
        port->labelDx = 80;    // out on the dark canvas
        view.canvas()->update();
        const auto onCanvas = inkAt();
        std::printf("  designator ink: on the unit lightness %d..%d, on the canvas %d..%d\n", onUnit.second, onUnit.first,
                    onCanvas.second, onCanvas.first);
        // The unit's fill is near white and the canvas near black: dark ink shows as something well under the fill, light
        // ink as something well over the canvas.
        check(onUnit.second < 150 && onCanvas.first > 150, "designators: Auto - dark on a pale unit, light on the dark canvas");
        port->labelDx = 0;
    }
}

// FINDINGS OF EVERY CHECK LISTED, A UNIT'S PICTURE, HARNESS JUMPS AND BRANCHES, COPY AND PASTE, FEWER CROSSINGS, BENDS THAT
// DO NOT FOLD BACK; THE VIEW RIBBON'S 3D SPLIT AND OPEN DRAWINGS TOGETHER.
void testRoundSix(const QString& base) {
    QJsonObject stored;
    SystemDesignView view;
    view.setStore([&] { return stored; }, [&](const QJsonObject& o) { stored = o; });
    view.resize(1200, 760);
    view.show();
    QApplication::processEvents();
    {
        SystemDesign d;
        const QString a = d.addUnit(0, 0, QStringLiteral("U1")), b = d.addUnit(400, 0, QStringLiteral("U2"));
        const QString p1 = d.addPort(a, QStringLiteral("right"), .3, QStringLiteral("J1")), p2 = d.addPort(a, QStringLiteral("right"), .7, QStringLiteral("J2"));
        const QString q1 = d.addPort(b, QStringLiteral("left"), .3, QStringLiteral("J3")), q2 = d.addPort(b, QStringLiteral("left"), .7, QStringLiteral("J4"));
        d.addCable(a, p1, b, q2);   // the two cross
        d.addCable(a, p2, b, q1);
        stored = d.toJson();
        view.reload();
    }
    SystemDesign& d = view.design();
    const QString a = d.units[0].id, b = d.units[1].id;

    // ---- 1. EVERY CHECK LISTS ITS FINDINGS ----
    auto* table = view.findChild<QTableWidget*>(QStringLiteral("systemIssues"));
    auto* summary = view.findChild<QLabel*>(QStringLiteral("systemDrcSummary"));
    auto* tabs = view.findChild<QTabWidget*>(QStringLiteral("systemTabs"));
    view.checkRevisions();
    const bool revisionsListed = table && table->rowCount() == static_cast<int>(view.issues().size()) && table->rowCount() > 0 &&
                                 summary->text().startsWith(QLatin1String("Revision check")) && tabs->currentIndex() == 1;
    view.checkSchematics();
    const bool connectionsListed = table && table->rowCount() == static_cast<int>(view.issues().size()) &&
                                   summary->text().startsWith(QLatin1String("Connection check")) && tabs->currentIndex() == 1;
    // Check again: the same check once more.
    if (auto* again = view.findChild<QPushButton*>(QStringLiteral("systemCheckAgain"))) again->click();
    bool againSame = summary->text().startsWith(QLatin1String("Connection check"));
    for (const SysIssue& i : view.issues()) againSame = againSame && (i.rule == QLatin1String("PORT-CONNECTION") || i.rule == QLatin1String("SCHEMATIC-NAME"));
    std::printf("  findings listed: revisions %d, connections %d, check again the same %d\n", revisionsListed ? 1 : 0, connectionsListed ? 1 : 0, againSame ? 1 : 0);
    check(revisionsListed && connectionsListed, "findings: Check revisions and Connections list their findings, named by the check");
    check(againSame, "findings: Check again runs the check last run");

    // ---- 6. FEWER CROSSINGS: the ports reordered so the two harnesses no longer cross ----
    // The router keeps them from crossing by sending one the long way round; reordered, both run straight across.
    const int crossedBefore = systemCrossings(d);
    const double longBefore = systemHarnessLength(d);
    view.minimizeCrossings();
    const int crossedAfter = systemCrossings(view.design());
    const double longAfter = systemHarnessLength(view.design());
    const SysUnit* left = view.design().unit(a);
    const bool swapped = left && left->ports.size() == 2 && left->ports[0].at > left->ports[1].at;
    std::printf("  minimize crossings: %d -> %d crossing(s), %.0f -> %.0f long, ports swapped %d\n", crossedBefore, crossedAfter, longBefore,
                longAfter, swapped ? 1 : 0);
    check(crossedAfter <= crossedBefore && crossedAfter == 0 && longAfter < longBefore - 1 && swapped,
          "minimize crossings: the ports reordered - the harnesses straight across, crossing none");
    view.undo();
    check(std::abs(systemHarnessLength(view.design()) - longBefore) < 0.5, "minimize crossings: one step to undo");

    // ---- 2. A PICTURE IN A UNIT: kept in the design, drawn in it ----
    const QString png = QDir(base).filePath(QStringLiteral("unit-picture.png"));
    {
        QImage red(120, 80, QImage::Format_RGB32);
        red.fill(QColor(220, 30, 30));
        red.save(png);
    }
    check(view.setUnitPicture(a, png) && !view.design().unit(a)->picture.isEmpty() &&
              !SystemDesign::fromJson(view.design().toJson()).unit(a)->picture.isEmpty(),
          "picture: put in a unit and saved with the design");
    view.canvas()->select(QString(), QString(), QString());
    view.canvas()->fit();
    QApplication::processEvents();
    {
        const QImage shot = view.canvas()->grab().toImage();
        const SysUnit* u = view.design().unit(a);
        const QPointF centre = view.canvas()->unitOnScreen(a);
        int reddish = 0;
        const int dpr = static_cast<int>(shot.devicePixelRatio());
        for (int dy = -20; dy <= 10; ++dy)
            for (int dx = -20; dx <= 20; ++dx) {
                const QColor c = shot.pixelColor((static_cast<int>(centre.x()) + dx) * dpr, (static_cast<int>(centre.y()) + dy) * dpr);
                if (c.red() > 180 && c.green() < 90 && c.blue() < 90) ++reddish;
            }
        std::printf("  picture: %d red px round the middle of %s\n", reddish, qPrintable(u->name));
        check(reddish > 100, "picture: drawn in the unit");
    }
    view.removeUnitPicture(a);
    check(view.design().unit(a)->picture.isEmpty(), "picture: taken out");

    // ---- 5. THE RIGHT-CLICK MENU; COPY AND PASTE ----
    view.canvas()->select(a, QString(), QString());
    QMenu* menu = view.contextMenu(QPointF(100, 300));
    QStringList names;
    for (QAction* act : menu->actions()) if (!act->objectName().isEmpty()) names << act->objectName();
    std::printf("  right-click menu: %s\n", qPrintable(names.join(QStringLiteral(", "))));
    check(names.contains(QLatin1String("systemMenuCopy")) && names.contains(QLatin1String("systemMenuCut")) &&
              names.contains(QLatin1String("systemMenuPaste")) && names.contains(QLatin1String("systemMenuDelete")) &&
              names.contains(QLatin1String("systemMenuMinimize")) && names.contains(QLatin1String("systemMenuPicture")),
          "right-click menu: copy, cut, paste, delete, minimize crossings, a unit's picture");
    delete menu;
    // Both units copied (a zone round them) with the harnesses between them, pasted where clicked.
    view.canvas()->selectZone(QRectF(-50, -50, 700, 300), false);
    const size_t unitsBefore = view.design().units.size(), cablesBefore = view.design().cables.size();
    const bool copied = view.copySelection();
    const bool pasted = view.paste(QPointF(0, 400), true);
    QSet<QString> unitNames, ids;
    bool unique = true;
    for (const SysUnit& u : view.design().units) { unique = unique && !unitNames.contains(u.name) && !ids.contains(u.id); unitNames.insert(u.name); ids.insert(u.id); }
    bool pastedBelow = false;
    for (const SysUnit& u : view.design().units) pastedBelow = pastedBelow || (u.y >= 390 && u.y <= 410);
    std::printf("  copy and paste: %d -> %d unit(s), %d -> %d harness(es)\n", static_cast<int>(unitsBefore), static_cast<int>(view.design().units.size()),
                static_cast<int>(cablesBefore), static_cast<int>(view.design().cables.size()));
    check(copied && pasted && view.design().units.size() == unitsBefore * 2 && view.design().cables.size() == cablesBefore * 2 && unique && pastedBelow,
          "copy and paste: the units and the harnesses between them, new names, where right-clicked");
    view.undo();
    check(view.design().units.size() == unitsBefore, "paste: one step to undo");

    // ---- 9. A HARNESS SPREAD TO SEVERAL PORTS ----
    {
        SystemDesign& dd = view.design();
        const QString c3 = dd.addUnit(200, 300, QStringLiteral("U9"));
        const QString p9 = dd.addPort(c3, QStringLiteral("top"), .5, QStringLiteral("J9"));
        stored = dd.toJson();
        view.reload();
        SysCable& trunk = view.design().cables[0];
        const QString trunkId = trunk.id;
        // Drawn with the Harness tool: pressed on the harness, let go on the port.
        SystemCanvas* c = view.canvas();
        c->fit();
        QApplication::processEvents();
        view.setTool(SysTool::Cable);
        const QPointF from = c->cableOnScreen(trunkId), to = c->portOnScreen(c3, p9);
        QMouseEvent down(QEvent::MouseButtonPress, from, c->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(c, &down);
        for (int i = 1; i <= 5; ++i) {
            const QPointF at = from + (to - from) * (i / 5.0);
            QMouseEvent move(QEvent::MouseMove, at, c->mapToGlobal(at), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(c, &move);
        }
        QMouseEvent up(QEvent::MouseButtonRelease, to, c->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(c, &up);
        QApplication::processEvents();
        const SysCable* spread = view.design().cable(trunkId);
        const bool branched = spread && spread->branches.size() == 1 && spread->branches[0].unit == c3 && spread->branches[0].port == p9;
        const bool plugged = branched && view.design().cablesAt(c3, p9).size() == 1;
        const auto limb = branched ? sysBranchPath(view.design(), *spread, spread->branches[0]) : std::vector<QPointF>{};
        const SystemDesign back = SystemDesign::fromJson(view.design().toJson());
        std::printf("  branch: %d branch(es), its line %d point(s)\n", spread ? static_cast<int>(spread->branches.size()) : -1, static_cast<int>(limb.size()));
        check(branched && plugged && limb.size() >= 3 && back.cable(trunkId) && back.cable(trunkId)->branches.size() == 1,
              "branch: a harness spread to one more port - drawn from the harness to the port, saved, the port plugged");
        view.setTool(SysTool::Select);
        view.design().removePort(c3, p9);
        check(view.design().cable(trunkId) && view.design().cable(trunkId)->branches.empty(), "branch: its port taken away - the branch only, the harness kept");
    }

    // ---- 8. BENDS THAT DO NOT FOLD BACK: a unit moved past a harness's dragged leg ----
    {
        SystemDesign t;
        const QString lo = t.addUnit(0, 200), hi = t.addUnit(300, -200);
        const QString up = t.addPort(lo, QStringLiteral("top"), .5), side = t.addPort(hi, QStringLiteral("left"), .5);
        const QString cable = t.addCable(lo, up, hi, side);
        t.cable(cable)->waypoints = {QPointF(80, 100), QPointF(250, 100)};   // its leg dragged to y 100
        t.moveUnit(lo, 0, -250);                                            // the unit moved up past it (top now -50)
        const std::vector<QPointF> path = sysCablePath(t, *t.cable(cable));
        // From the port up: never down again before the leg.
        bool foldsBack = false;
        for (size_t i = 1; i < path.size() && i < 4; ++i) foldsBack = foldsBack || path[i].y() > path[i - 1].y() + 0.01;
        const std::vector<QPointF> bends = sysCableWaypoints(t, *t.cable(cable));
        std::printf("  bends: the leg now at y %.1f (dragged to 100, the port at %.1f)\n", bends.empty() ? 0.0 : bends.front().y(), t.unit(lo)->y);
        check(!foldsBack && !bends.empty() && bends.front().y() < t.unit(lo)->y && std::abs(bends.front().y() - bends.back().y()) < 0.01,
              "bends: a unit moved past the dragged leg - the line out of its port shortens, not out and back");
    }
}

// THE VIEW RIBBON: THE 3D VIEW SPLIT BELOW OR BESIDE AND DRAGGED TO FOUR FIFTHS; THE OPEN DRAWINGS SHOWN TOGETHER.
void testViewSplitAndTiles() {
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QApplication::processEvents();
    auto* dock = window.findChild<QDockWidget*>(QStringLiteral("view3dDock"));
    auto* splitBelow = window.findChild<QToolButton*>(QStringLiteral("viewSplit3DHorizontal"));
    auto* splitBeside = window.findChild<QToolButton*>(QStringLiteral("viewSplit3DVertical"));
    check(dock && splitBelow && splitBeside, "view: the 3D split buttons");
    if (!dock || !splitBelow || !splitBeside) return;
    splitBeside->click();
    QApplication::processEvents();
    const bool beside = window.dockWidgetArea(dock) == Qt::RightDockWidgetArea && dock->isVisible();
    splitBelow->click();
    QApplication::processEvents();
    QApplication::processEvents();
    const bool below = window.dockWidgetArea(dock) == Qt::BottomDockWidgetArea && dock->isVisible();
    check(beside && below, "view: Split vertical puts the 3D view beside the drawing, Split horizontal under it");
    // DRAGGED UP: as far as four fifths of the room, not past it.
    window.keepCanvasShare();
    const int room = window.centralWidget()->height() + dock->height();
    window.resizeDocks({dock}, {room * 9 / 10}, Qt::Vertical);
    QApplication::processEvents();
    const double share = dock->height() / static_cast<double>(window.centralWidget()->height() + dock->height());
    std::printf("  3D view dragged up: %.0f%% of the room (the drawing keeps %d px)\n", share * 100.0, window.centralWidget()->height());
    check(share > 0.7 && share < 0.83, "view: the 3D view dragged up to four fifths, the drawing keeping a fifth");

    // ---- THE OPEN DRAWINGS TOGETHER ----
    window.newDrawingForTest();
    window.newDrawingForTest();
    QApplication::processEvents();
    auto* sideBySide = window.findChild<QToolButton*>(QStringLiteral("viewCascadeVertical"));
    auto* rows = window.findChild<QToolButton*>(QStringLiteral("viewCascadeHorizontal"));
    auto* single = window.findChild<QToolButton*>(QStringLiteral("viewCascadeSingle"));
    check(sideBySide && rows && single, "view: the open drawings buttons");
    if (!sideBySide || !rows || !single) return;
    sideBySide->click();
    QApplication::processEvents();
    QSplitter* tiles = window.drawingTiles();
    const int shown = tiles ? tiles->count() : 0;
    const bool columns = tiles && tiles->orientation() == Qt::Horizontal;
    rows->click();
    QApplication::processEvents();
    const bool stacked = tiles && tiles->orientation() == Qt::Vertical && tiles->count() == shown;
    std::printf("  open drawings together: %d shown, columns %d, rows %d\n", shown, columns ? 1 : 0, stacked ? 1 : 0);
    check(shown >= 3 && columns && stacked, "view: every open drawing shown together - in columns, or in rows");
    // A double-click on another: it is worked on, the one before becomes a tile.
    int before = -1, after = -1;
    for (int i = 0; i < tiles->count(); ++i)
        if (tiles->widget(i)->objectName() == QLatin1String("drawingTile")) { before = tiles->widget(i)->property("drawingIndex").toInt(); break; }
    if (before >= 0) {
        for (int i = 0; i < tiles->count(); ++i) {
            QWidget* tile = tiles->widget(i);
            if (tile->objectName() != QLatin1String("drawingTile") || tile->property("drawingIndex").toInt() != before) continue;
            const QPointF mid(tile->width() / 2.0, tile->height() / 2.0);
            QMouseEvent dbl(QEvent::MouseButtonDblClick, mid, tile->mapToGlobal(mid), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(tile, &dbl);
            break;
        }
        for (int k = 0; k < 4; ++k) QApplication::processEvents();
        bool stillTile = false;
        for (int i = 0; i < tiles->count(); ++i)
            if (tiles->widget(i)->objectName() == QLatin1String("drawingTile") && tiles->widget(i)->property("drawingIndex").toInt() == before) stillTile = true;
        after = stillTile ? before : -2;
    }
    check(before >= 0 && after == -2, "view: a drawing double-clicked is the one worked on");
    single->click();
    QApplication::processEvents();
    check(tiles->count() == 1, "view: back to one drawing at a time");
}

} // namespace

int run_systemdesign_tests() {
    std::printf("system design\n");
    QTemporaryDir dir;
    if(qEnvironmentVariable("EDRAW_SYSTEM_CASE")==QLatin1String("parent-delete")){testParentDeleteChoices();return g_fail;}
    testModel();
    testDrawings(dir.path());
    testView(dir.path());
    testFinder(dir.path());
    testMatcher();
    testFinderInside(dir.path());
    testDesignators(dir.path());
    testTab();
    testUnitsPortsNotes();
    testFilesOut(dir.path());
    testNestingAndLook();
    testParentDeleteChoices();
    testDeleteKeyAndClickCable();
    testSystemEditingAndChecks(dir.path());
    testSystemSchematicConnections(dir.path());
    testSystemDrcWorkflow();
    testProjectTrafficBypassAndAppearance();
    testSchematicTrafficLights();
    testDrawingDetailsFields(dir.path());
    testPdfViewerAndProgress(dir.path());
    testSystemModuleLinksAndRoutes();
    testImportScopesCtrl(dir.path());
    testCheckCardsHelpNotesDesignators();
    testRoundSix(dir.path());
    testViewSplitAndTiles();
    std::printf("  system design: %s\n", g_fail == 0 ? "OK" : "FAILED");
    return g_fail;
}
