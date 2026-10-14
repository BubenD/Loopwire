// THE SCHEMATIC HARNESS BUILDER, as it is made simpler step by step (see the roadmap in the round's commit): each phase's
// tests here, one function a phase.
#include "app/DrawingPreview.h"
#include "app/Editor.h"
#include "app/HarnessBuilderDialog.h"
#include "harness/HarnessBuilder.h"
#include "harness/HarnessDemand.h"
#include "system/SystemImport.h"
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include "model/Document.h"

#include <QApplication>
#include <QJsonArray>
#include <QSet>
#include <QJsonDocument>
#include <QPushButton>
#include <QListWidgetItem>
#include <QMouseEvent>
#include "harness/PartsLibrary.h"
#include <QLabel>
#include <QSplitter>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QHeaderView>
#include <QTextBrowser>
#include <QTableWidget>
#include <QStackedWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QCheckBox>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace Loopwire;

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

// A NEW BUILDER STARTS EMPTY. The tests that want the usual pair of ends add them, as somebody would: Left, then Right.
void addTwoEnds(HarnessBuilderDialog& dialog) {
    for (const char* side : {"Left", "Right"})
        for (QPushButton* b : dialog.findChildren<QPushButton*>())
            if (b->text() == QLatin1String(side)) {
                b->click();
                break;
            }
    QApplication::processEvents();
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
    addTwoEnds(dialog);
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
    addTwoEnds(dialog);
    auto* steps = dialog.findChild<QListWidget*>(QStringLiteral("harnessBuilderSteps"));
    auto* pages = dialog.findChild<QStackedWidget*>(QStringLiteral("harnessBuilderPages"));
    QStringList titles;
    if (steps)
        for (int i = 0; i < steps->count(); ++i) titles << steps->item(i)->data(Qt::UserRole).toString();
    std::printf("  builder steps: %s; open at %d\n", qPrintable(titles.join(QStringLiteral(" > "))), dialog.currentStep());
    check(steps && pages && pages->count() == 4 &&
              titles == QStringList({QStringLiteral("Connectors"), QStringLiteral("Wires"), QStringLiteral("Shields"), QStringLiteral("Layout")}) &&
              dialog.currentStep() == 0,
          "builder steps: four steps in order - Connectors (with placement), Wires, Shields, Layout - opened at the first");
    // ---- BACK AND NEXT, AND A STEP CLICKED ----
    auto* next = dialog.findChild<QPushButton*>(QStringLiteral("harnessBuilderNext"));
    auto* back = dialog.findChild<QPushButton*>(QStringLiteral("harnessBuilderBack"));
    const bool backOffAtFirst = back && !back->isEnabled();
    if (next) next->click();
    const int afterNext = dialog.currentStep();
    if (back) back->click();
    const int afterBack = dialog.currentStep();
    steps->setCurrentRow(3);
    QApplication::processEvents();
    const bool lastNextOff = next && !next->isEnabled() && pages->currentIndex() == 3;
    check(backOffAtFirst && afterNext == 1 && afterBack == 0 && lastNextOff,
          "builder steps: Next and Back walk the steps, a step clicked opens it; no Back at the first, no Next at the last");
    // ---- THE DRAWING ON THE RIGHT AT EVERY STEP ----
    auto* picture = dialog.findChild<DrawingPreview*>(QStringLiteral("harnessBuilderPreviewPicture"));
    bool alwaysShown = picture != nullptr;
    for (int i = 0; i < 4; ++i) {
        dialog.setStep(i);
        QApplication::processEvents();
        alwaysShown = alwaysShown && picture->isVisible();
    }
    check(alwaysShown, "builder steps: the drawing, as Generate will draw it, beside every step");
    // ---- THE OPTIMISERS WITH THE PIN MAP ----
    auto* minimise = dialog.findChild<QPushButton*>(QStringLiteral("harnessBuilderMinimise"));
    auto* view = dialog.findChild<QStackedWidget*>(QStringLiteral("harnessBuilderView"));
    check(minimise && view && view->widget(1)->isAncestorOf(minimise), "builder steps: the pin optimisers with the pin map");
    // ---- THE SHIELDS STEP: an overall shield ticked there is the harness's ----
    const QString left = dialog.definition().connectors.front().id;
    auto* shield = dialog.findChild<QCheckBox*>(QStringLiteral("harnessBuilderShield_") + left);
    dialog.setStep(2);   // Shields
    QApplication::processEvents();
    if (shield) shield->setChecked(true);
    for (int k = 0; k < 3; ++k) QApplication::processEvents();
    check(shield && dialog.definition().connectors.front().shieldLeft, "builder steps: the Shields step - an overall shield ticked on a connector's end");
    QTableWidget* connectorTable = nullptr;
    for (QTableWidget* t : dialog.findChildren<QTableWidget*>())
        if (t->columnCount() == 16 && t->horizontalHeaderItem(0) && t->horizontalHeaderItem(0)->text() == QLatin1String("Side")) connectorTable = t;
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

// THE STEPS REFINED: numbers taken when finished, Pitch in the drawing, connectors from the library with their contacts,
// the drawing or the pin map at any step (connectors in placement order), the Wires step full width, drains to the body or a
// pin - direct or through a hook-up wire - and connector cells that read.
void testStepsRefined() {
    Document doc(QStringLiteral("builder refined"));
    Editor editor;
    editor.setDocument(&doc);
    HarnessBuilderDialog dialog(&editor);
    dialog.resize(1400, 860);
    dialog.show();
    QApplication::processEvents();
    addTwoEnds(dialog);
    QTableWidget* connectorTable = nullptr;
    for (QTableWidget* t : dialog.findChildren<QTableWidget*>())
        if (t->columnCount() == 16 && t->horizontalHeaderItem(0) && t->horizontalHeaderItem(0)->text() == QLatin1String("Side")) connectorTable = t;
    // ---- A NUMBER TAKEN WHEN IT IS FINISHED ----
    auto* pins = connectorTable ? qobject_cast<QSpinBox*>(connectorTable->cellWidget(0, 4)) : nullptr;
    check(pins && !pins->keyboardTracking(), "connectors: Pins taken on Enter or leaving the field - not at each digit, so 10 can be typed");
    if (pins) pins->setValue(10);
    QApplication::processEvents();
    check(dialog.definition().connectors.front().pinCount == 10, "connectors: and taken when given");
    // ---- PITCH AND PIN LENGTH IN THE DRAWING ----
    auto* picture = dialog.findChild<DrawingPreview*>(QStringLiteral("harnessBuilderPreviewPicture"));
    auto pitchShown = [&] {
        for (const Entity& e : picture->entities())
            if (e.type == EntityType::Connector) return e.meta.value(QStringLiteral("pitch")).toDouble();
        return -1.0;
    };
    connectorTable = nullptr;
    for (QTableWidget* t : dialog.findChildren<QTableWidget*>())
        if (t->columnCount() == 16 && t->horizontalHeaderItem(0) && t->horizontalHeaderItem(0)->text() == QLatin1String("Side")) connectorTable = t;
    auto* pitch = connectorTable ? qobject_cast<QDoubleSpinBox*>(connectorTable->cellWidget(0, 5)) : nullptr;
    if (pitch) pitch->setValue(22.0);
    QApplication::processEvents();
    std::printf("  connectors: pitch 22 given, the drawing's connector at %.1f\n", picture ? pitchShown() : -1.0);
    check(picture && pitch && std::abs(pitchShown() - 22.0) < 1e-6, "connectors: Pitch given - the drawing shows it at once");
    // ---- A CONNECTOR FROM THE LIBRARY, WITH ITS CONTACT ----
    LibraryPart part;
    part.id = QStringLiteral("lib-conn-9");
    part.kind = PartKind::Connector;
    part.mfrPn = QStringLiteral("CONN-9");
    part.params.insert(QStringLiteral("pins"), QStringLiteral("9"));
    LibraryPart contact;
    contact.id = QStringLiteral("lib-contact-22");
    contact.kind = PartKind::ContactPin;
    contact.mfrPn = QStringLiteral("CT-22");
    const QString added = dialog.addConnectorFromLibrary(part, &contact);
    const HarnessConnector* lib = harnessConnById(dialog.definition(), added);
    const HarnessDef back = harnessDefFromJson(harnessDefToJson(dialog.definition()));
    const HarnessConnector* kept = harnessConnById(back, added);
    bool contactDrawn = false;
    for (const Entity& e : buildHarness(dialog.definition()).entities)
        contactDrawn = contactDrawn || (e.type == EntityType::Connector && e.meta.value(QStringLiteral("contactPn")).toString() == QLatin1String("CT-22"));
    check(lib && lib->pinCount == 9 && lib->partNumber == QLatin1String("CONN-9") && !lib->contactsIncluded &&
              lib->contactPn == QLatin1String("CT-22") && kept && kept->contactPn == QLatin1String("CT-22") && contactDrawn,
          "connectors: one from the library - its P/N, its 9 pins and its separate contact P/N, saved and on the drawing");
    // ---- THE DRAWING OR THE PIN MAP, AT ANY STEP; THE PIN MAP IN PLACEMENT ORDER ----
    auto* showMap = dialog.findChild<QPushButton*>(QStringLiteral("harnessBuilderShowPinMap"));
    auto* showDrawing = dialog.findChild<QPushButton*>(QStringLiteral("harnessBuilderShowDrawing"));
    dialog.setStep(1);   // Wires
    if (showMap) showMap->click();
    QApplication::processEvents();
    const bool mapShown = dialog.showingPinMap();
    auto* view = dialog.findChild<QStackedWidget*>(QStringLiteral("harnessBuilderView"));
    QStringList order;
    if (view) {
        QList<QLabel*> captions;
        for (QLabel* l : view->widget(1)->findChildren<QLabel*>())
            for (const HarnessConnector& c : dialog.definition().connectors)
                if (l->text() == c.designator && l->isVisible()) captions << l;
        std::sort(captions.begin(), captions.end(), [](QLabel* a, QLabel* b) { return a->mapToGlobal(QPoint()).x() < b->mapToGlobal(QPoint()).x(); });
        for (QLabel* l : captions) order << l->text();
    }
    QStringList want;
    {
        std::vector<const HarnessConnector*> sorted;
        for (const HarnessConnector& c : dialog.definition().connectors) sorted.push_back(&c);
        std::stable_sort(sorted.begin(), sorted.end(), [](const HarnessConnector* a, const HarnessConnector* b) {
            return a->side != b->side ? a->side == HarnessSide::Left : a->order < b->order;
        });
        for (const HarnessConnector* c : sorted) want << c->designator;
    }
    std::printf("  pin map: shown %d, columns %s (placement %s)\n", mapShown ? 1 : 0, qPrintable(order.join(QStringLiteral(", "))),
                qPrintable(want.join(QStringLiteral(", "))));
    if (showDrawing) showDrawing->click();
    QApplication::processEvents();
    check(showMap && mapShown && !dialog.showingPinMap(), "pin map: under the drawing at any step - a button to it, and one back");
    check(order == want, "pin map: the connectors in the order they are placed - each side top to bottom");
    // ---- THE WIRES STEP: THE TABLE FULL WIDTH, THE DRAWING UNDER IT ----
    auto* split = dialog.findChild<QSplitter*>(QStringLiteral("harnessBuilderSplit"));
    const bool under = split && split->orientation() == Qt::Vertical;
    dialog.setStep(0);
    QApplication::processEvents();
    check(under && split->orientation() == Qt::Horizontal, "wires step: the table the full width, the drawing under it; beside it on the other steps");
    // ---- DRAINS: to the body or a pin, direct or through a hook-up wire ----
    dialog.setStep(1);   // Wires
    QPushButton* addWire = nullptr;
    for (QPushButton* b : dialog.findChildren<QPushButton*>())
        if (b->text() == QLatin1String("Add wire")) addWire = b;
    for (int i = 0; i < 2 && addWire; ++i) {
        addWire->click();
        QApplication::processEvents();
    }
    const HarnessConnector first = dialog.definition().connectors.front();
    const QString id = first.id;
    dialog.setStep(2);   // Shields
    QApplication::processEvents();
    auto settle = [] { for (int k = 0; k < 4; ++k) QApplication::processEvents(); };
    auto* shield = dialog.findChild<QCheckBox*>(QStringLiteral("harnessBuilderShield_") + id);
    if (shield) shield->setChecked(true);
    settle();
    // The control on screen - a card rebuilt leaves the old one, hidden, until it is deleted.
    auto pick = [&](const char* name, const QString& data) {
        QComboBox* box = nullptr;
        for (QComboBox* b : dialog.findChildren<QComboBox*>(QString::fromLatin1(name) + id)) if (b->isVisible()) box = b;
        if (box) box->setCurrentIndex(box->findData(data));
        settle();
        return box != nullptr;
    };
    const bool toBody = pick("harnessBuilderDrainTo_", QStringLiteral("body"));
    const HarnessConnector* afterBody = harnessConnById(dialog.definition(), id);
    QString bondVia;
    for (const Entity& e : buildHarness(dialog.definition()).entities)
        if (e.meta.contains(QStringLiteral("shieldBond"))) bondVia = e.meta.value(QStringLiteral("drainVia")).toString();
    check(shield && toBody && afterBody && afterBody->drainTo == QLatin1String("body") && afterBody->bodyBondLeft && bondVia == QLatin1String("direct"),
          "shields: an overall shield whose drain goes to the connector body - direct, the shield's own drain");
    pick("harnessBuilderDrainVia_", QStringLiteral("wire"));
    pick("harnessBuilderDrainColour_", QStringLiteral("GN"));
    QString bondColour;
    for (const Entity& e : buildHarness(dialog.definition()).entities)
        if (e.meta.contains(QStringLiteral("shieldBond"))) bondColour = e.meta.value(QStringLiteral("wireColor")).toString();
    check(bondColour == QLatin1String("GN"), "shields: through a hook-up wire - drawn in that wire's colour");
    // To a pin: the drain a wire of its own; direct, no splice where it leaves the screen; through a hook-up wire, a splice.
    const HarnessConnector* other = harnessConnById(dialog.definition(), id);   // a shield connects to its own connector's pins
    const QString pinChoice = other ? QStringLiteral("pin:%1:%2").arg(other->id).arg(other->pinCount) : QString();
    pick("harnessBuilderDrainTo_", pinChoice);
    std::printf("  shields: drain asked to %s, set to %s\n", qPrintable(pinChoice), qPrintable(harnessConnById(dialog.definition(), id)->drainTo));
    auto splices = [&] {
        int n = 0;
        for (const Entity& e : buildHarness(dialog.definition()).entities) n += e.type == EntityType::Splice ? 1 : 0;
        return n;
    };
    bool drainRow = false;
    for (const HarnessWire& w : dialog.definition().wires) drainRow = drainRow || (harnessIsDrainRow(w) && w.toConn == (other ? other->id : QString()));
    const int splicedViaWire = splices();
    pick("harnessBuilderDrainVia_", QStringLiteral("direct"));
    const int splicedDirect = splices();
    std::printf("  shields: drain to a pin - its own wire %d; splices through a hook-up wire %d, direct %d\n", drainRow ? 1 : 0, splicedViaWire, splicedDirect);
    check(other && drainRow && splicedViaWire == splicedDirect + 1,
          "shields: a drain to a pin - a wire of its own; spliced to the screen through a hook-up wire, not when direct");
    pick("harnessBuilderDrainTo_", QString());
    bool gone = true;
    for (const HarnessWire& w : dialog.definition().wires) gone = gone && !harnessIsDrainRow(w);
    check(gone, "shields: not terminated - the drain's wire taken away");
    // ---- CELLS THAT READ: text set against every coloured connector cell ----
    dialog.setStep(1);   // Wires
    settle();
    int tinted = 0, readable = 0;
    for (QComboBox* box : dialog.findChildren<QComboBox*>())
        if (box->styleSheet().contains(QLatin1String("background:#"))) {
            ++tinted;
            readable += box->styleSheet().contains(QLatin1String("color:#")) ? 1 : 0;
        }
    std::printf("  wire cells: %d tinted, %d with a text colour of their own\n", tinted, readable);
    check(tinted > 0 && readable == tinted, "wires: each coloured connector cell has text set to read on it");
}

// SHIELDS AFTER WIRES; SHIELD CONNECT - this connector only; NOT CONNECTED an X; THE HOOK-UP WIRE GREYED WHEN DIRECT;
// CTRL+HOVER ON THE PIN MAP.
void testShieldConnectAndPinLight() {
    Document doc(QStringLiteral("builder shield connect"));
    Editor editor;
    editor.setDocument(&doc);
    HarnessBuilderDialog dialog(&editor);
    dialog.resize(1400, 860);
    dialog.show();
    QApplication::processEvents();
    addTwoEnds(dialog);
    auto settle = [] { for (int k = 0; k < 4; ++k) QApplication::processEvents(); };
    dialog.setStep(1);   // Wires
    QPushButton* addWire = nullptr;
    for (QPushButton* b : dialog.findChildren<QPushButton*>())
        if (b->text() == QLatin1String("Add wire")) addWire = b;
    for (int i = 0; i < 2 && addWire; ++i) {
        addWire->click();
        settle();
    }
    const HarnessConnector first = dialog.definition().connectors.front();
    const QString id = first.id;
    dialog.setStep(2);   // Shields
    settle();
    auto visible = [&](const QString& name) -> QWidget* {
        for (QWidget* w : dialog.findChildren<QWidget*>(name)) if (w->isVisible()) return w;
        return nullptr;
    };
    if (auto* shield = qobject_cast<QCheckBox*>(visible(QStringLiteral("harnessBuilderShield_") + id))) shield->setChecked(true);
    settle();
    // ---- SHIELD CONNECT: this connector's body and pins, and nothing of another's ----
    auto* to = qobject_cast<QComboBox*>(visible(QStringLiteral("harnessBuilderDrainTo_") + id));
    bool ownOnly = to != nullptr;
    QStringList offered;
    for (int i = 0; to && i < to->count(); ++i) {
        const QString data = to->itemData(i).toString();
        offered << to->itemText(i);
        ownOnly = ownOnly && (data.isEmpty() || data == QLatin1String("body") || data.startsWith(QStringLiteral("pin:%1:").arg(id)));
    }
    bool named = false;
    QWidget* card = visible(QStringLiteral("harnessBuilderShieldCard_") + id);
    for (QLabel* l : card ? card->findChildren<QLabel*>() : QList<QLabel*>()) named = named || l->text() == QLatin1String("Shield connect");
    std::printf("  shield connect: %d choice(s) - %s ... \n", to ? to->count() : -1, qPrintable(offered.mid(0, 3).join(QStringLiteral(", "))));
    check(to && named && ownOnly && to->count() == 2 + first.pinCount && to->itemText(0) == QLatin1String("Not connected"),
          "shields: Shield connect - not connected, the connector body, or one of this connector's own pins");
    // ---- NOT CONNECTED: an X on the shield's leg ----
    bool crossed = false;
    for (const Entity& e : buildHarness(dialog.definition()).entities)
        if (e.type == EntityType::Screen && !e.meta.value(QStringLiteral("overallShield")).toString().isEmpty())
            crossed = crossed || !e.meta.contains(QStringLiteral("notConnected")) || e.meta.value(QStringLiteral("notConnected")).toBool();
    check(crossed, "shields: not connected - the shield's leg ends in an X");
    // ---- DIRECT: the hook-up wire, its gauge and colour greyed; THROUGH A WIRE: open ----
    if (to) to->setCurrentIndex(to->findData(QStringLiteral("body")));
    settle();
    auto* wire = visible(QStringLiteral("harnessBuilderDrainWire_") + id);
    auto* colour = visible(QStringLiteral("harnessBuilderDrainColour_") + id);
    const bool greyedDirect = wire && colour && !wire->isEnabled() && !colour->isEnabled();
    if (auto* via = qobject_cast<QComboBox*>(visible(QStringLiteral("harnessBuilderDrainVia_") + id))) via->setCurrentIndex(1);
    settle();
    wire = visible(QStringLiteral("harnessBuilderDrainWire_") + id);
    colour = visible(QStringLiteral("harnessBuilderDrainColour_") + id);
    check(greyedDirect && wire && colour && wire->isEnabled() && colour->isEnabled(),
          "shields: direct - the hook-up wire, gauge and colour greyed; through a hook-up wire - open to choose");
    // ---- CTRL+HOVER ON THE PIN MAP: the pin and every pin wired to it lit ----
    dialog.showPinMap(true);
    settle();
    const HarnessWire w = dialog.definition().wires.front();
    auto* list = qobject_cast<QListWidget*>(visible(QStringLiteral("harnessBuilderPins_") + w.fromConn));
    QListWidgetItem* item = nullptr;
    for (int r = 0; list && r < list->count(); ++r)
        if (list->item(r)->data(Qt::UserRole).toInt() == w.fromPin) item = list->item(r);
    if (list && item) {
        const QPointF at = list->visualItemRect(item).center();
        QMouseEvent hover(QEvent::MouseMove, at, list->viewport()->mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::ControlModifier);
        QApplication::sendEvent(list->viewport(), &hover);
    }
    const QStringList lit = dialog.litPins();
    const QString far = QStringLiteral("%1:%2").arg(w.toConn).arg(w.toPin);
    auto* farList = qobject_cast<QListWidget*>(visible(QStringLiteral("harnessBuilderPins_") + w.toConn));
    bool farShown = false;
    for (int r = 0; farList && r < farList->count(); ++r)
        if (farList->item(r)->data(Qt::UserRole).toInt() == w.toPin) farShown = farList->item(r)->background().color() == QColor("#ffd166");
    if (list && item) {
        const QPointF at = list->visualItemRect(item).center();
        QMouseEvent plain(QEvent::MouseMove, at, list->viewport()->mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(list->viewport(), &plain);
    }
    std::printf("  pin map: Ctrl over %s:%d - lit %s\n", qPrintable(w.fromConn), w.fromPin, qPrintable(lit.join(QStringLiteral(", "))));
    check(item && lit.contains(QStringLiteral("%1:%2").arg(w.fromConn).arg(w.fromPin)) && lit.contains(far) && farShown,
          "pin map: Ctrl held over a pin - it, and the pin wired to it on the other connector, lit");
    check(dialog.litPins().isEmpty(), "pin map: Ctrl let go - nothing lit");
}
} // namespace

// THE PREVIEW'S TWISTS FIND THEIR WIRES. A twist names its wires; in the preview no wire carried the name, so each
// was drawn straight through its twist - a line along the middle of every cross that the placed drawing has not.
void testPreviewTwistsFindTheirWires() {
    const std::vector<Entity> shown = harnessPreviewEntities(sampleHarness(), true);
    QSet<QString> wireIds;
    for (const Entity& e : shown)
        if (e.type == EntityType::Wire) wireIds.insert(e.id);
    int twists = 0, named = 0, found = 0;
    for (const Entity& e : shown) {
        if (e.type != EntityType::Twist) continue;
        ++twists;
        for (const QJsonValue& v : e.meta.value(QStringLiteral("wires")).toArray()) {
            ++named;
            if (wireIds.contains(v.toString())) ++found;
        }
    }
    std::printf("  preview twists: %d twist(s) naming %d wire(s), %d of them in the preview\n", twists, named, found);
    check(twists > 0 && named > 0 && found == named,
          "preview twists: every wire a twist names is a wire in the preview, so it leaves the twist's gap");
    check(!wireIds.contains(QString()), "preview twists: no wire is left without a name");
}

// A LIBRARY CONNECTOR WITH ITS PINS FITTED has no contact to choose: both contact fields greyed. One whose contact is a
// library part shows that part's P/N, greyed - it is the library's, not typed.
void testFittedPinsGreyTheContacts() {
    Document doc(QStringLiteral("fitted pins"));
    Editor editor;
    editor.setDocument(&doc);
    HarnessBuilderDialog dialog(&editor);
    dialog.resize(1400, 860);
    dialog.show();
    QApplication::processEvents();
    PartsLibrary lib;
    const QString fittedId = lib.add(PartKind::Connector).id;
    lib.byId(fittedId)->mfrPn = QStringLiteral("HDR-6");
    setPartIncludesPins(*lib.byId(fittedId), true);
    const QString housingId = lib.add(PartKind::Connector).id;
    lib.byId(housingId)->mfrPn = QStringLiteral("HSG-6");
    const QString contactId = lib.add(PartKind::ContactPin).id;
    lib.byId(contactId)->mfrPn = QStringLiteral("CT-1");
    tieContactToConnector(lib, housingId, contactId);
    useHarnessBuilderLibraryForTest(lib);
    const QString fitted = dialog.addConnectorFromLibrary(*lib.byId(fittedId), lib.byId(contactId));
    const QString housing = dialog.addConnectorFromLibrary(*lib.byId(housingId), lib.byId(contactId));
    QApplication::processEvents();
    auto* fittedChoice = dialog.findChild<QComboBox*>(QStringLiteral("harnessBuilderContacts_") + fitted);
    auto* fittedPn = dialog.findChild<QLineEdit*>(QStringLiteral("harnessBuilderContactPn_") + fitted);
    auto* housingChoice = dialog.findChild<QComboBox*>(QStringLiteral("harnessBuilderContacts_") + housing);
    auto* housingPn = dialog.findChild<QLineEdit*>(QStringLiteral("harnessBuilderContactPn_") + housing);
    std::printf("  fitted pins: contacts %d/%d, P/N %d; housing contacts %d, P/N \"%s\" %d\n",
                fittedChoice ? int(fittedChoice->isEnabled()) : -1, fittedChoice ? fittedChoice->currentIndex() : -1,
                fittedPn ? int(fittedPn->isEnabled()) : -1, housingChoice ? int(housingChoice->isEnabled()) : -1,
                housingPn ? qPrintable(housingPn->text()) : "-", housingPn ? int(housingPn->isEnabled()) : -1);
    check(fittedChoice && !fittedChoice->isEnabled() && fittedChoice->currentIndex() == 0 && fittedPn && !fittedPn->isEnabled(),
          "fitted pins: a connector with its own pins has its contact fields greyed - its contacts are in it");
    check(housingChoice && housingChoice->isEnabled() && housingPn && !housingPn->isEnabled() &&
              housingPn->text() == QLatin1String("CT-1"),
          "fitted pins: a library contact's P/N is shown, greyed - the library's, not typed");
}

// THE HARNESS DEMAND: a workbook a customer fills in, read back into a builder definition.
void testHarnessDemand() {
    HarnessDemand demand;
    demand.info.customer = QStringLiteral("Acme");
    demand.info.harnessName = QStringLiteral("Sensor loom");
    demand.info.partNumber = QStringLiteral("AC-100");
    DemandConnector j1;
    j1.designator = QStringLiteral("J1");
    j1.end = QStringLiteral("A");
    j1.description = QStringLiteral("Controller");
    j1.pins = 8;
    j1.overallShield = true;
    j1.drainTo = QStringLiteral("Body");
    DemandConnector p1;
    p1.designator = QStringLiteral("P1");
    p1.end = QStringLiteral("B");
    p1.pinNames = QStringLiteral("Letters");   // no pin count: as many as are used
    demand.connectors = {j1, p1};
    auto row = [](const char* from, const char* fp, const char* to, const char* tp, const char* signal) {
        DemandConnection c;
        c.from = QLatin1String(from);
        c.fromPin = QLatin1String(fp);
        c.to = QLatin1String(to);
        c.toPin = QLatin1String(tp);
        c.signal = QLatin1String(signal);
        return c;
    };
    DemandConnection canH = row("J1", "1", "P1", "A", "CAN_H");
    canH.twistGroup = QStringLiteral("TP1");
    canH.shieldGroup = QStringLiteral("S1");
    DemandConnection canL = row("J1", "2", "P1", "B", "CAN_L");
    canL.twistGroup = QStringLiteral("TP1");
    canL.shieldGroup = QStringLiteral("S1");
    DemandConnection power = row("J1", "3", "P1", "C", "+24V");
    power.gauge = QStringLiteral("20 AWG");
    power.color = QStringLiteral("Red");
    DemandConnection rf = row("J1", "4", "P1", "D", "RF");
    rf.type = QStringLiteral("Coax");
    rf.coaxShieldFromPin = QStringLiteral("5");
    rf.coaxShieldToPin = QStringLiteral("E");
    DemandConnection lead = row("J1", "6", "", "", "SPARE");
    demand.connections = {canH, canL, power, rf, lead};
    DemandGroup s1;
    s1.name = QStringLiteral("S1");
    s1.kind = QStringLiteral("Shielded");
    s1.drainConnector = QStringLiteral("J1");
    s1.drainPin = QStringLiteral("7");
    demand.groups = {s1};

    // OUT AND BACK IN: every sheet and field as written.
    const QByteArray book = harnessDemandToXlsx(demand);
    HarnessDemand read;
    QString error;
    const bool ok = harnessDemandFromXlsx(book, read, &error);
    std::printf("  demand: workbook %d bytes, read %s, %d connector(s), %d connection(s), %d group(s)\n",
                int(book.size()), ok ? "ok" : qPrintable(error), int(read.connectors.size()),
                int(read.connections.size()), int(read.groups.size()));
    check(ok && read.info.customer == QLatin1String("Acme") && read.info.partNumber == QLatin1String("AC-100"),
          "demand: the workbook reads back, its details with it");
    check(read.connectors.size() == 2 && read.connectors[1].pinNames == QLatin1String("Letters") &&
              read.connectors[0].overallShield && read.connectors[0].pins == 8,
          "demand: the connectors read back as written");
    check(read.connections.size() == 5 && read.connections[3].type == QLatin1String("Coax") &&
              read.connections[3].coaxShieldToPin == QLatin1String("E") && read.connections[4].to.isEmpty(),
          "demand: the connections read back, the coax and the flying lead with them");
    check(read.groups.size() == 1 && read.groups[0].drainPin == QLatin1String("7"), "demand: the groups read back");
    // THE DROP-DOWNS ON THE CELLS THEY BELONG TO: Excel refuses a whole workbook over one range it cannot read.
    {
        QByteArray sheet;
        for (const ZipItem& z : readZip(book))
            if (z.name == QLatin1String("xl/worksheets/sheet4.xml")) sheet = z.data;
        const QList<QByteArray> ranges = [&] {
            QList<QByteArray> out;
            for (qsizetype at = sheet.indexOf("sqref=\""); at >= 0; at = sheet.indexOf("sqref=\"", at + 1))
                out << sheet.mid(at + 7, sheet.indexOf('"', at + 7) - at - 7);
            return out;
        }();
        bool wellFormed = !ranges.isEmpty();
        for (const QByteArray& r : ranges)
            wellFormed = wellFormed && QRegularExpression(QStringLiteral("^[A-Z]+2:[A-Z]+500$")).match(QString::fromLatin1(r)).hasMatch();
        std::printf("  demand: Connections drop-downs on %s\n", ranges.join(' ').constData());
        check(wellFormed && ranges.contains("F2:F500"), "demand: each drop-down is on its column from row 2 down (Type on F2:F500)");
    }

    // AS A DEFINITION.
    std::vector<DemandIssue> issues;
    const HarnessDef def = harnessDefFromDemand(read, &issues);
    for (const DemandIssue& i : issues) std::printf("    issue: %s %d %s\n", qPrintable(i.sheet), i.row, qPrintable(i.text));
    const HarnessConnector* cj1 = nullptr;
    const HarnessConnector* cp1 = nullptr;
    for (const HarnessConnector& c : def.connectors) (c.designator == QLatin1String("J1") ? cj1 : cp1) = &c;
    check(cj1 && cp1 && cj1->side == HarnessSide::Left && cp1->side == HarnessSide::Right,
          "demand: End A on the left, End B on the right");
    check(cp1 && cp1->annotation == QLatin1String("letters") && cp1->pinCount == 5,
          "demand: a lettered connector with no pin count has as many pins as its wires reach (A to E)");
    check(cj1 && cj1->shieldLeft && cj1->drainTo == QLatin1String("body"),
          "demand: an overall shield drained to the body");
    int twisted = 0, shielded = 0, coaxRows = 0, open = 0, drains = 0;
    bool braidOnPins = false, power20 = false;
    QSet<QString> coaxBundles;
    for (const HarnessWire& w : def.wires) {
        if (w.twistGroup == QLatin1String("TP1")) ++twisted;
        if (w.shieldGroup == QLatin1String("S1")) ++shielded;
        if (harnessIsCoaxKind(w.kind)) {
            ++coaxRows;
            coaxBundles.insert(w.bundle);
            if (w.color == QLatin1String("SHIELD")) braidOnPins = w.fromPin == 5 && w.toPin == 5;
        }
        if (harnessIsOpenEndpoint(w.toConn)) ++open;
        if (w.toConn == harnessInternalShieldKey(QStringLiteral("S1")) && w.fromPin == 7) ++drains;
        if (w.label == QLatin1String("+24V")) power20 = w.gauge == QLatin1String("0.5") && w.color == QLatin1String("RD");
    }
    check(twisted == 2 && shielded == 2, "demand: the twisted pair and its shield group");
    check(coaxRows == 2 && coaxBundles.size() == 1 && braidOnPins,
          "demand: a coax is two rows of one bundle, its braid on the shield pins (J1-5 to P1-E)");
    check(open == 1, "demand: an empty To is a flying lead");
    check(drains == 1, "demand: the shield group's drain is a wire from J1-7 to the screen");
    check(power20, "demand: 20 AWG is 0.5 mm2 and Red is RD");
    check(issues.empty(), "demand: a demand filled in properly reads with nothing to check");

    // WHAT CANNOT BE TAKEN AS WRITTEN is reported on its row.
    HarnessDemand bad = demand;
    bad.connections[0].to = QStringLiteral("X9");
    bad.connections[2].color = QStringLiteral("Mauve");
    bad.connections[3].coaxShieldToPin.clear();
    std::vector<DemandIssue> badIssues;
    harnessDefFromDemand(bad, &badIssues);
    auto reported = [&](int rowNo, const char* words) {
        for (const DemandIssue& i : badIssues)
            if (i.sheet == QLatin1String("Connections") && i.row == rowNo && i.text.contains(QLatin1String(words)))
                return true;
        return false;
    };
    check(reported(2, "X9") && reported(4, "Mauve") && reported(5, "braid"),
          "demand: an unknown connector, an unknown colour and a coax with no braid pin are each reported on their row");

    // AS EXCEL SAVES IT: shared strings, numbers as numbers, the columns in another order.
    {
        auto entry = [](const char* name, const QString& xml) { return ZipItem{QLatin1String(name), xml.toUtf8()}; };
        const QString shared = QStringLiteral(
            "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
            "<si><t>Designator</t></si><si><t>End</t></si><si><t>J1</t></si><si><t>A</t></si><si><t>P1</t></si>"
            "<si><t>B</t></si><si><t>To</t></si><si><t>To pin</t></si><si><t>From</t></si><si><t>From pin</t></si>"
            "<si><r><t>SIG</t></r><r><t>NAL</t></r></si><si><t>Signal</t></si></sst>");
        const QString connectors = QStringLiteral(
            "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>"
            "<row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>1</v></c></row>"
            "<row r=\"2\"><c r=\"A2\" t=\"s\"><v>2</v></c><c r=\"B2\" t=\"s\"><v>3</v></c></row>"
            "<row r=\"4\"><c r=\"A4\" t=\"s\"><v>4</v></c><c r=\"B4\" t=\"s\"><v>5</v></c></row>"
            "</sheetData></worksheet>");
        const QString connections = QStringLiteral(
            "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>"
            "<row r=\"1\"><c r=\"A1\" t=\"s\"><v>6</v></c><c r=\"B1\" t=\"s\"><v>7</v></c><c r=\"C1\" t=\"s\"><v>8</v></c>"
            "<c r=\"D1\" t=\"s\"><v>9</v></c><c r=\"F1\" t=\"s\"><v>11</v></c></row>"
            "<row r=\"2\"><c r=\"A2\" t=\"s\"><v>4</v></c><c r=\"B2\"><v>2</v></c><c r=\"C2\" t=\"s\"><v>2</v></c>"
            "<c r=\"D2\"><v>1.0000000000001</v></c><c r=\"F2\" t=\"s\"><v>10</v></c></row>"
            "</sheetData></worksheet>");
        const QString workbook = QStringLiteral(
            "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>"
            "<sheet name=\"Connectors\" sheetId=\"1\" r:id=\"rId1\"/><sheet name=\"Connections\" sheetId=\"2\" r:id=\"rId2\"/>"
            "</sheets></workbook>");
        const QString rels = QStringLiteral(
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rId1\" Type=\"x\" Target=\"worksheets/sheet1.xml\"/>"
            "<Relationship Id=\"rId2\" Type=\"x\" Target=\"/xl/worksheets/sheet2.xml\"/></Relationships>");
        const QByteArray excel = writeZip({entry("xl/workbook.xml", workbook), entry("xl/_rels/workbook.xml.rels", rels),
                                           entry("xl/sharedStrings.xml", shared),
                                           entry("xl/worksheets/sheet1.xml", connectors),
                                           entry("xl/worksheets/sheet2.xml", connections)});
        HarnessDemand fromExcel;
        const bool readOk = harnessDemandFromXlsx(excel, fromExcel, &error);
        const DemandConnection* c = fromExcel.connections.empty() ? nullptr : &fromExcel.connections.front();
        std::printf("  demand from Excel: %s, %d connector(s), first connection %s-%s to %s-%s \"%s\"\n",
                    readOk ? "read" : qPrintable(error), int(fromExcel.connectors.size()),
                    c ? qPrintable(c->from) : "-", c ? qPrintable(c->fromPin) : "-", c ? qPrintable(c->to) : "-",
                    c ? qPrintable(c->toPin) : "-", c ? qPrintable(c->signal) : "-");
        check(readOk && fromExcel.connectors.size() == 2 && c && c->from == QLatin1String("J1") &&
                  c->fromPin == QLatin1String("1") && c->to == QLatin1String("P1") && c->toPin == QLatin1String("2") &&
                  c->signal == QLatin1String("SIGNAL"),
              "demand: a workbook as Excel saves it reads - shared strings, numbers, columns found by their headings");
        HarnessDemand none;
        check(!harnessDemandFromXlsx(QByteArray("not a workbook"), none, &error) && !error.isEmpty(),
              "demand: something that is not a workbook is refused with a reason");
    }

    // A BLANK TEMPLATE reads back as an empty demand.
    {
        HarnessDemand blank;
        const bool blankOk = harnessDemandFromXlsx(harnessDemandToXlsx(HarnessDemand{}), blank, &error);
        check(blankOk && blank.connectors.empty() && blank.connections.empty() && blank.groups.empty(),
              "demand: a blank template reads back as nothing filled in");
    }

    // FROM THE BUILDER AND BACK: the same wires, groups and coax.
    {
        const HarnessDemand out = harnessDemandFromDef(def);
        const HarnessDef again = harnessDefFromDemand(out);
        auto count = [](const HarnessDef& d, auto pred) {
            return int(std::count_if(d.wires.begin(), d.wires.end(), pred));
        };
        auto twist = [](const HarnessWire& w) { return !w.twistGroup.isEmpty(); };
        auto coax = [](const HarnessWire& w) { return harnessIsCoaxKind(w.kind); };
        std::printf("  demand from the builder: %d wire row(s) -> %d connection(s) -> %d wire row(s)\n",
                    int(def.wires.size()), int(out.connections.size()), int(again.wires.size()));
        check(again.wires.size() == def.wires.size() && count(again, twist) == count(def, twist) &&
                  count(again, coax) == count(def, coax) && again.connectors.size() == def.connectors.size(),
              "demand: the builder's harness sent out as a demand comes back the same");
    }

    // ON PAPER.
    {
        const QString pdf = QDir::temp().filePath(QStringLiteral("loopwire-demand-test.pdf"));
        QFile::remove(pdf);
        QString pdfError;
        const bool wrote = writeHarnessDemandPdf(read, pdf, &pdfError);
        QFile f(pdf);
        const bool opened = f.open(QIODevice::ReadOnly);
        const QByteArray head = opened ? f.read(4) : QByteArray();
        const qint64 size = opened ? f.size() : 0;
        f.close();
        std::printf("  demand overview: %s, %lld bytes\n", wrote ? "written" : qPrintable(pdfError), size);
        check(wrote && head == "%PDF" && size > 2000, "demand: the overview is written as a PDF");
        QFile::remove(pdf);
    }
}

int run_harnessbuilder_tests() {
    g_fail = 0;
    std::printf("harness builder tests:\n");
    testPhaseZero();
    testPhaseOne();
    testStepsRefined();
    testShieldConnectAndPinLight();
    testPreviewTwistsFindTheirWires();
    testFittedPinsGreyTheContacts();
    testHarnessDemand();
    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
