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

int run_harnessbuilder_tests() {
    g_fail = 0;
    std::printf("harness builder tests:\n");
    testPhaseZero();
    testPhaseOne();
    testStepsRefined();
    testShieldConnectAndPinLight();
    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
