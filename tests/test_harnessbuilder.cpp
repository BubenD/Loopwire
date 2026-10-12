// THE SCHEMATIC HARNESS BUILDER, as it is made simpler step by step (see the roadmap in the round's commit): each phase's
// tests here, one function a phase.
#include "app/DrawingPreview.h"
#include "app/Editor.h"
#include "app/HarnessBuilderDialog.h"
#include "harness/HarnessBuilder.h"
#include "model/Document.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>

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

// A small harness with something of every sort a row can be: a loose wire, a cable's cores in a twisted, screened
// pair, and a coax (its core and its screen).
HarnessDef sampleHarness() {
    HarnessDef d;
    HarnessConnector a;
    a.id = QStringLiteral("c1");
    a.designator = QStringLiteral("P1");
    a.pinCount = 6;
    a.shieldLeft = true;
    a.annotation = QStringLiteral("mixed");
    HarnessConnector b = a;
    b.id = QStringLiteral("c2");
    b.designator = QStringLiteral("P2");
    b.side = HarnessSide::Right;
    b.shieldLeft = false;
    b.shieldRight = true;
    d.connectors = {a, b};
    auto wire = [&](const char* id, int from, int to, const char* colour) {
        HarnessWire w;
        w.id = QString::fromLatin1(id);
        w.fromConn = QStringLiteral("c1");
        w.fromPin = from;
        w.toConn = QStringLiteral("c2");
        w.toPin = to;
        w.color = QString::fromLatin1(colour);
        return w;
    };
    HarnessWire loose = wire("w1", 1, 1, "RD");
    HarnessWire coreA = wire("w2", 2, 2, "WH");
    HarnessWire coreB = wire("w3", 3, 3, "BU");
    for (HarnessWire* w : {&coreA, &coreB}) {
        w->bundle = QStringLiteral("CABLE1");
        w->twistGroup = QStringLiteral("T1");
        w->shieldGroup = QStringLiteral("S1");
        w->libPart = QStringLiteral("lib-cable-2p");
    }
    HarnessWire coaxCore = wire("w4", 4, 4, "Signal");
    HarnessWire coaxScreen = wire("w5", 5, 5, "Shield");
    coaxCore.kind = coaxScreen.kind = QStringLiteral("coax");
    coaxCore.bundle = coaxScreen.bundle = QStringLiteral("COAX1");
    d.wires = {loose, coreA, coreB, coaxCore, coaxScreen};
    d.layout.colorNameMode = QStringLiteral("long");
    return d;
}

bool sameWires(const HarnessDef& a, const HarnessDef& b) {
    if (a.wires.size() != b.wires.size()) return false;
    for (size_t i = 0; i < a.wires.size(); ++i) {
        const HarnessWire& x = a.wires[i];
        const HarnessWire& y = b.wires[i];
        if (x.id != y.id || x.kind != y.kind || x.bundle != y.bundle || x.twistGroup != y.twistGroup || x.shieldGroup != y.shieldGroup ||
            x.color != y.color || x.libPart != y.libPart || x.fromPin != y.fromPin || x.toPin != y.toPin)
            return false;
    }
    return true;
}

// PHASE 0: what is saved comes back whole; undo and redo inside the builder; the preview IS the drawing.
void testPhaseZero() {
    // ---- SAVED AND READ BACK: every row's kind and cable with it ----
    const HarnessDef sample = sampleHarness();
    const QJsonObject saved = harnessDefToJson(sample);
    const HarnessDef back = harnessDefFromJson(QJsonDocument::fromJson(QJsonDocument(saved).toJson()).object());
    std::printf("  builder file: version %d, %d row(s) back, cable %s, coax %s\n", saved.value(QStringLiteral("version")).toInt(),
                static_cast<int>(back.wires.size()), qPrintable(back.wires.size() > 1 ? back.wires[1].bundle : QString()),
                qPrintable(back.wires.size() > 3 ? back.wires[3].kind : QString()));
    check(sameWires(sample, back), "builder file: a cable's cores and a coax come back as they were - not as loose wires");
    check(back.connectors.size() == 2 && back.connectors[0].annotation == QLatin1String("mixed") && back.connectors[1].shieldRight &&
              back.layout.colorNameMode == QLatin1String("long"),
          "builder file: the connectors and the layout come back too");
    // An older file - no kind, no cable - reads as loose wires, as it was written.
    QJsonObject old = saved;
    old[QStringLiteral("version")] = 2;
    QJsonArray rows = old.value(QStringLiteral("wires")).toArray();
    for (int i = 0; i < rows.size(); ++i) {
        QJsonObject r = rows[i].toObject();
        r.remove(QStringLiteral("kind"));
        r.remove(QStringLiteral("bundle"));
        rows[i] = r;
    }
    old[QStringLiteral("wires")] = rows;
    const HarnessDef older = harnessDefFromJson(old);
    bool plain = !older.wires.empty();
    for (const HarnessWire& w : older.wires) plain = plain && w.kind == QLatin1String("wire") && w.bundle.isEmpty();
    check(plain, "builder file: an older file without them still opens - its rows plain wires, as they were written");

    // ---- UNDO AND REDO IN THE BUILDER ----
    Document doc(QStringLiteral("builder"));
    Editor editor;
    editor.setDocument(&doc);
    HarnessBuilderDialog dialog(&editor);
    dialog.show();
    QApplication::processEvents();
    const size_t start = dialog.definition().connectors.size();
    QPushButton* addLeft = nullptr;
    for (QPushButton* b : dialog.findChildren<QPushButton*>())
        if (b->text() == QLatin1String("Left")) addLeft = b;
    if (addLeft) addLeft->click();
    QApplication::processEvents();
    const size_t added = dialog.definition().connectors.size();
    dialog.undo();
    const size_t undone = dialog.definition().connectors.size();
    dialog.redo();
    const size_t redone = dialog.definition().connectors.size();
    std::printf("  builder undo: %d connector(s), %d after +Left, %d undone, %d done again\n", static_cast<int>(start), static_cast<int>(added),
                static_cast<int>(undone), static_cast<int>(redone));
    check(addLeft && added == start + 1 && undone == start && redone == start + 1,
          "builder undo: a connector added - Undo takes it back, Redo puts it back");
    auto* undoButton = dialog.findChild<QPushButton*>(QStringLiteral("harnessBuilderUndo"));
    auto* redoButton = dialog.findChild<QPushButton*>(QStringLiteral("harnessBuilderRedo"));
    check(undoButton && redoButton && undoButton->isEnabled() && !redoButton->isEnabled(), "builder undo: its own buttons, each enabled when it can act");

    // ---- THE PREVIEW IS WHAT GENERATE DRAWS - wires and all ----
    QPushButton* addWire = nullptr;
    for (QPushButton* b : dialog.findChildren<QPushButton*>())
        if (b->text() == QLatin1String("Add wire")) addWire = b;
    for (int i = 0; i < 3 && addWire; ++i) {
        addWire->click();
        QApplication::processEvents();
    }
    auto* picture = dialog.findChild<DrawingPreview*>(QStringLiteral("harnessBuilderPreviewPicture"));
    const HarnessBuild built = buildHarness(dialog.definition());
    std::printf("  builder preview: %d entities shown, Generate draws %d\n", picture ? static_cast<int>(picture->entities().size()) : -1,
                static_cast<int>(built.entities.size()));
    check(picture && dialog.definition().wires.size() >= 3 && picture->entities().size() == built.entities.size() &&
              picture->entities().size() > dialog.definition().connectors.size(),
          "builder preview: the real drawing Generate makes - the same entities, not a sketch of them");
}
} // namespace

int run_harnessbuilder_tests() {
    g_fail = 0;
    std::printf("harness builder tests:\n");
    testPhaseZero();
    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
