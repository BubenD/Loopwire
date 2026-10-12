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
#include <QHeaderView>
#include <QTextBrowser>
#include <QTableWidget>
#include <QStackedWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QCheckBox>

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

// PHASE 1: ONE STEP AT A TIME - the steps down the left with their marks, Back and Next, the drawing always on the right.
void testPhaseOne() {
    Document doc(QStringLiteral("builder steps"));
    Editor editor;
    editor.setDocument(&doc);
    HarnessBuilderDialog dialog(&editor);
    dialog.resize(1400, 860);
    dialog.show();
    QApplication::processEvents();
    auto* steps = dialog.findChild<QListWidget*>(QStringLiteral("harnessBuilderSteps"));
    auto* pages = dialog.findChild<QStackedWidget*>(QStringLiteral("harnessBuilderPages"));
    QStringList titles;
    if (steps)
        for (int i = 0; i < steps->count(); ++i) titles << steps->item(i)->data(Qt::UserRole).toString();
    std::printf("  builder steps: %s; open at %d\n", qPrintable(titles.join(QStringLiteral(" > "))), dialog.currentStep());
    check(steps && pages && pages->count() == 6 &&
              titles == QStringList({QStringLiteral("Connectors"), QStringLiteral("Placement"), QStringLiteral("Pin map"),
                                     QStringLiteral("Shields"), QStringLiteral("Wires"), QStringLiteral("Layout")}) &&
              dialog.currentStep() == 0,
          "builder steps: six steps in order - Connectors, Placement, Pin map, Shields, Wires, Layout - opened at the first");
    // ---- BACK AND NEXT, AND A STEP CLICKED ----
    auto* next = dialog.findChild<QPushButton*>(QStringLiteral("harnessBuilderNext"));
    auto* back = dialog.findChild<QPushButton*>(QStringLiteral("harnessBuilderBack"));
    const bool backOffAtFirst = back && !back->isEnabled();
    if (next) next->click();
    const int afterNext = dialog.currentStep();
    if (back) back->click();
    const int afterBack = dialog.currentStep();
    steps->setCurrentRow(5);
    QApplication::processEvents();
    const bool lastNextOff = next && !next->isEnabled() && pages->currentIndex() == 5;
    check(backOffAtFirst && afterNext == 1 && afterBack == 0 && lastNextOff,
          "builder steps: Next and Back walk the steps, a step clicked opens it; no Back at the first, no Next at the last");
    // ---- THE DRAWING ON THE RIGHT AT EVERY STEP ----
    auto* picture = dialog.findChild<DrawingPreview*>(QStringLiteral("harnessBuilderPreviewPicture"));
    bool alwaysShown = picture != nullptr;
    for (int i = 0; i < 6; ++i) {
        dialog.setStep(i);
        QApplication::processEvents();
        alwaysShown = alwaysShown && picture->isVisible();
    }
    check(alwaysShown, "builder steps: the drawing, as Generate will draw it, beside every step");
    // ---- THE OPTIMISERS ON THE PIN MAP STEP ----
    auto* minimise = dialog.findChild<QPushButton*>(QStringLiteral("harnessBuilderMinimise"));
    check(minimise && pages->widget(2)->isAncestorOf(minimise), "builder steps: the pin optimisers on the Pin map step");
    // ---- THE SHIELDS STEP: an overall shield ticked there is the harness's ----
    const QString left = dialog.definition().connectors.front().id;
    auto* shield = dialog.findChild<QCheckBox*>(QStringLiteral("harnessBuilderShield_") + left);
    dialog.setStep(3);
    QApplication::processEvents();
    if (shield) shield->setChecked(true);
    for (int k = 0; k < 3; ++k) QApplication::processEvents();
    check(shield && dialog.definition().connectors.front().shieldLeft, "builder steps: the Shields step - an overall shield ticked on a connector's end");
    QTableWidget* connectorTable = nullptr;
    for (QTableWidget* t : dialog.findChildren<QTableWidget*>())
        if (t->columnCount() == 14 && t->horizontalHeaderItem(0) && t->horizontalHeaderItem(0)->text() == QLatin1String("Side")) connectorTable = t;
    bool hidden = connectorTable != nullptr;
    for (int col = 8; col <= 11 && connectorTable; ++col) hidden = hidden && connectorTable->isColumnHidden(col);
    check(hidden, "builder steps: the shield columns gone from the Connectors table - they are on the Shields step");
    // ---- THE MARKS: a fault on a step is marked there, and said beside the drawing ----
    dialog.setStep(0);
    QApplication::processEvents();
    const int cleanAtStart = dialog.stepIssueCount(0);
    QLineEdit* designator = connectorTable ? qobject_cast<QLineEdit*>(connectorTable->cellWidget(1, 1)) : nullptr;
    if (designator) {
        designator->setText(dialog.definition().connectors.front().designator);
        emit designator->editingFinished();
    }
    for (int k = 0; k < 3; ++k) QApplication::processEvents();
    auto* said = dialog.findChild<QTextBrowser*>(QStringLiteral("harnessBuilderChecks"));
    std::printf("  builder marks: step 1 %d issue(s) after two connectors given one name\n", dialog.stepIssueCount(0));
    check(designator && cleanAtStart == 0 && dialog.stepIssueCount(0) >= 1 && steps->item(0)->data(Qt::UserRole + 1).toInt() >= 1 &&
              said && said->toPlainText().contains(QLatin1String("this step")),
          "builder marks: two connectors given one name - the Connectors step marked, and what is wrong said beside the drawing");
}
} // namespace

int run_harnessbuilder_tests() {
    g_fail = 0;
    std::printf("harness builder tests:\n");
    testPhaseZero();
    testPhaseOne();
    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
