// THE SCHEMATIC HARNESS BUILDER, as it is made simpler step by step (see the roadmap in the round's commit): each phase's
// tests here, one function a phase.
#include "app/DrawingPreview.h"
#include "app/Editor.h"
#include "app/HarnessBuilderDialog.h"
#include "app/WiringDialog.h"
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
#include <QElapsedTimer>
#include "harness/CableSpec.h"
#include "harness/PartsLibrary.h"
#include <QAction>
#include <QGroupBox>
#include <QLabel>
#include <QSplitter>
#include <QSettings>
#include <QToolButton>
#include <QScrollArea>
#include <QWheelEvent>
#include <QKeyEvent>
#include "app/UiBits.h"
#include "harness/Harness.h"
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
    // Add wire is a button with a menu on its arrow now - found by its name.
    auto* addWire = dialog.findChild<QAbstractButton*>(QStringLiteral("harnessBuilderAddWire"));
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
    // One button for the two views: it shows the pin map, then the drawing again.
    auto* showMap = dialog.findChild<QPushButton*>(QStringLiteral("harnessBuilderViewSwitch"));
    auto* showDrawing = showMap;
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
        std::sort(captions.begin(), captions.end(), [](QLabel* a, QLabel* b) {
            const QPoint at = a->mapToGlobal(QPoint()), bt = b->mapToGlobal(QPoint());
            return at.y() == bt.y() ? at.x() < bt.x() : at.y() < bt.y();
        });
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
    auto* previewChoice = dialog.findChild<QComboBox*>(QStringLiteral("harnessBuilderPreviewWhere"));
    const Qt::Orientation chosen = previewChoice && previewChoice->currentIndex() == 1 ? Qt::Vertical : Qt::Horizontal;
    const bool under = split && split->orientation() == chosen;
    dialog.setStep(0);
    QApplication::processEvents();
    check(under && split->orientation() == chosen,
          "compact steps: preview position follows its menu across every step");
    // ---- DRAINS: to the body or a pin, direct or through a hook-up wire ----
    dialog.setStep(1);   // Wires
    // Add wire is a button with a menu on its arrow now - found by its name.
    auto* addWire = dialog.findChild<QAbstractButton*>(QStringLiteral("harnessBuilderAddWire"));
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
    // Add wire is a button with a menu on its arrow now - found by its name.
    auto* addWire = dialog.findChild<QAbstractButton*>(QStringLiteral("harnessBuilderAddWire"));
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
        if (farList->item(r)->data(Qt::UserRole).toInt() == w.toPin) farShown = farList->item(r)->background().color() == QColor(255, 209, 102, 70);   // lit lightly
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

// THE HARNESS DEMAND: a pinout table a customer fills in - a block per cable, a row per connector, a column per
// signal - read back into a builder definition.
void testHarnessDemand() {
    auto connector = [](const char* name, const char* type, int pins, std::initializer_list<const char*> cells) {
        DemandConnector c;
        c.name = QString::fromUtf8(name);
        c.type = QLatin1String(type);
        c.maxPins = pins;
        for (const char* cell : cells) c.cells << QString::fromUtf8(cell);
        return c;
    };
    auto signal = [](const char* name, const char* group = "", int id = -1) {
        DemandSignal s;
        s.name = QLatin1String(name);
        s.group = QLatin1String(group);
        s.groupId = id;
        return s;
    };
    // A MOTOR WITH LEADS, its phases a shielded twisted three, its halls under one shield, its thermistor twisted -
    // the second cable of the drawing office's sheet.
    DemandCable motor;
    motor.name = QStringLiteral("HGC700151");
    motor.function = QStringLiteral("Tilt - GR43 to Motor subassembly");
    motor.signalColumns = {signal("THERM-", "Twist", 1), signal("THERM+", "Twist", 1), signal("HALL_5V", "Shield", 2),
                     signal("HALL_GND", "Shield", 2), signal("HALL_A", "Shield", 2), signal("PH_A", "Shield+Twist", 3),
                     signal("PH_B", "Shield+Twist", 3), signal("PH_C", "Shield+Twist", 3)};
    motor.connectors = {connector("Motor", "TBM2G-06813C", 0, {"White", "White", "Blue", "Green", "Brown", "RED\n\"U\"", "White", "Black"}),
                        connector("43-J5\nPH PW OUT", "M300-2250396F2", 3, {"", "", "", "", "", "1", "2", "3"}),
                        connector("43-J3\nHALLS", "GHR-07V-S", 7, {"1", "2", "3", "4", "5", "", "", ""})};
    motor.connectors[0].length = QStringLiteral("N/A");
    // A CONTROLLER CABLE, ground on two pins at each end, a lettered connector, a coax.
    DemandCable ebox;
    ebox.name = QStringLiteral("HGC700150");
    ebox.function = QStringLiteral("Tilt - GR43 to Ebox");
    ebox.signalColumns = {signal("A-", "Twist", 1), signal("A+", "Twist", 1), signal("GND"), signal("RF", "Coax", 2),
                    signal("SHIELD", "Coax", 2)};
    ebox.connectors = {connector("E.Box", "A19507-801", 27, {"5", "4", "21,25", "10", "11"}),
                       connector("43-J2", "505432-2201", 22, {"1", "3", "22,26", "7", "8"}),
                       connector("43-J7", "LTR", 0, {"A", "B", "", "", ""})};
    HarnessDemand demand;
    demand.info.customer = QStringLiteral("Acme");
    demand.info.project = QStringLiteral("Tilt");
    demand.cables = {ebox, motor};

    // OUT AND BACK IN: each cable's block as written.
    const QByteArray book = harnessDemandToXlsx(demand);
    HarnessDemand read;
    QString error;
    const bool ok = harnessDemandFromXlsx(book, read, &error);
    std::printf("  demand: workbook %d bytes, read %s, %d cable(s)\n", int(book.size()), ok ? "ok" : qPrintable(error),
                int(read.cables.size()));
    check(ok && read.info.customer == QLatin1String("Acme") && read.cables.size() == 2,
          "demand: the workbook reads back, both cables and the details");
    if (read.cables.size() == 2) {
        const DemandCable& m = read.cables[1];
        std::printf("  demand: second cable %s \"%s\", %d connector(s), %d signal(s), first row %s\n", qPrintable(m.name),
                    qPrintable(m.function), int(m.connectors.size()), int(m.signalColumns.size()),
                    m.connectors.empty() ? "-" : qPrintable(m.connectors[0].cells.join(QLatin1Char('|'))));
        check(m.name == QLatin1String("HGC700151") && m.function == motor.function && m.connectors.size() == 3 &&
                  m.signalColumns.size() == 8,
              "demand: a cable's name, function, connectors and signals read back");
        check(m.signalColumns[0].group == QLatin1String("Twist") && m.signalColumns[1].groupId == m.signalColumns[0].groupId &&
                  m.signalColumns[4].group == QLatin1String("Shield") && m.signalColumns[2].groupId == m.signalColumns[4].groupId &&
                  m.signalColumns[7].group == QLatin1String("Shield+Twist") && m.signalColumns[5].groupId == m.signalColumns[7].groupId &&
                  m.signalColumns[5].groupId != m.signalColumns[4].groupId,
              "demand: the group row reads back - each group over its own columns");
        check(m.connectors[1].name == QLatin1String("43-J5\nPH PW OUT") && m.connectors[1].cells[6] == QLatin1String("2") &&
                  m.connectors[0].cells[5] == QLatin1String("RED\n\"U\""),
              "demand: the cells read back as written, lines and all");
        check(read.cables[0].connectors[0].cells[2] == QLatin1String("21,25"), "demand: several pins in a cell read back");
    }
    // THE DROP-DOWNS on the group row, on the cells they belong to: Excel refuses a whole workbook over one range it
    // cannot read.
    {
        QByteArray sheet;
        for (const ZipItem& z : readZip(book))
            if (z.name == QLatin1String("xl/worksheets/sheet3.xml")) sheet = z.data;
        QList<QByteArray> ranges;
        for (qsizetype at = sheet.indexOf("sqref=\""); at >= 0; at = sheet.indexOf("sqref=\"", at + 1))
            ranges << sheet.mid(at + 7, sheet.indexOf('"', at + 7) - at - 7);
        bool wellFormed = !ranges.isEmpty();
        for (const QByteArray& r : ranges)
            wellFormed = wellFormed && QRegularExpression(QStringLiteral("^[A-Z]+[0-9]+:[A-Z]+[0-9]+$")).match(QString::fromLatin1(r)).hasMatch();
        std::printf("  demand: group row drop-downs on %s\n", ranges.join(' ').constData());
        check(wellFormed && ranges.contains("J1:Q1"), "demand: each group row's drop-down is on its signal columns, every block as wide as the widest (J1:Q1)");
    }

    // THE MOTOR CABLE AS A DEFINITION.
    std::vector<DemandIssue> issues;
    const HarnessDef def = harnessDefFromDemand(motor, &issues);
    for (const DemandIssue& i : issues) std::printf("    issue: %s %d %s\n", qPrintable(i.sheet), i.row, qPrintable(i.text));
    QStringList names;
    for (const HarnessConnector& c : def.connectors)
        names << QStringLiteral("%1/%2/%3").arg(c.designator, c.side == HarnessSide::Left ? "L" : "R").arg(c.pinCount);
    std::printf("  demand: connectors %s, %d wire(s)\n", qPrintable(names.join(QStringLiteral(" "))), int(def.wires.size()));
    check(names == QStringList({QStringLiteral("43-J5/L/3"), QStringLiteral("43-J3/R/7")}),
          "demand: a part with only leads is no connector; the first with pins is on the left, the next on the right");
    int leads = 0, twisted = 0, shielded = 0, both = 0;
    bool phaseRed = false;
    for (const HarnessWire& w : def.wires) {
        if (harnessIsOpenEndpoint(w.fromConn) || harnessIsOpenEndpoint(w.toConn)) ++leads;
        if (!w.twistGroup.isEmpty() && w.shieldGroup.isEmpty()) ++twisted;
        if (w.twistGroup.isEmpty() && !w.shieldGroup.isEmpty()) ++shielded;
        if (!w.twistGroup.isEmpty() && !w.shieldGroup.isEmpty()) ++both;
        if (w.label == QLatin1String("PH_A")) phaseRed = w.color == QLatin1String("RD");
    }
    check(def.wires.size() == 8 && leads == 8, "demand: every motor signal is a wire from its lead to its pin (or a lead alone)");
    check(twisted == 2 && shielded == 3 && both == 3, "demand: the twist, the shield and the shielded twisted three");
    check(phaseRed, "demand: a lead's colour is its wire's - RED (\"U\") is RD");
    bool thermFlying = false;
    for (const DemandIssue& i : issues)
        if (i.text.contains(QLatin1String("THERM"))) thermFlying = true;
    check(!thermFlying, "demand: a signal from a lead to nowhere else is a flying lead, not a fault");

    // THE CONTROLLER CABLE: two pins to two pins in order, a lettered connector, a coax's signal and braid.
    issues.clear();
    const HarnessDef ctl = harnessDefFromDemand(ebox, &issues);
    for (const DemandIssue& i : issues) std::printf("    issue: %s %d %s\n", qPrintable(i.sheet), i.row, qPrintable(i.text));
    QStringList grounds;
    int coaxRows = 0;
    bool braid = false, letteredJ7 = false;
    for (const HarnessWire& w : ctl.wires) {
        if (w.label == QLatin1String("GND")) grounds << QStringLiteral("%1-%2").arg(w.fromPin).arg(w.toPin);
        if (harnessIsCoaxKind(w.kind)) ++coaxRows;
        if (harnessIsCoaxKind(w.kind) && w.color == QLatin1String("SHIELD")) braid = w.fromPin == 11 && w.toPin == 8;
    }
    for (const HarnessConnector& c : ctl.connectors)
        if (c.designator == QLatin1String("43-J7")) letteredJ7 = c.annotation == QLatin1String("letters") && c.pinCount == 2;
    std::printf("  demand: GND wires %s\n", qPrintable(grounds.join(QStringLiteral(" "))));
    check(grounds == QStringList({QStringLiteral("21-22"), QStringLiteral("25-26")}), "demand: 21,25 to 22,26 is two wires, pin by pin");
    check(coaxRows == 2 && braid, "demand: a coax is its signal and its braid, the braid E.Box-11 to 43-J2-8");
    check(letteredJ7, "demand: a connector written with letters is lettered, as many pins as it uses");
    int aMinus = 0;
    for (const HarnessWire& w : ctl.wires)
        if (w.label == QLatin1String("A-")) ++aMinus;
    check(aMinus == 2, "demand: a signal on three connectors is a wire from the first to each of the others");

    // WHAT CANNOT BE TAKEN AS WRITTEN is reported on its row.
    {
        DemandCable bad = ebox;
        bad.connectors[1].cells[0] = QStringLiteral("x9");
        bad.connectors[1].sheetRow = 7;
        std::vector<DemandIssue> badIssues;
        harnessDefFromDemand(bad, &badIssues);
        bool reported = false;
        for (const DemandIssue& i : badIssues)
            if (i.row == 7 && i.text.contains(QLatin1String("x9"))) reported = true;
        check(reported, "demand: a pin that cannot be read is reported on its row");
    }

    // AS EXCEL SAVES IT: shared strings, numbers as numbers, merged cells - the cable's name merged down its rows,
    // a shield merged over its columns, and two twisted pairs written cell by cell.
    {
        auto entry = [](const char* name, const QString& xml) { return ZipItem{QLatin1String(name), xml.toUtf8()}; };
        const QString shared = QStringLiteral(
            "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
            "<si><t>Cable Name</t></si><si><t>Connector\nName</t></si><si><t>HGC1</t></si><si><t>J1</t></si>"
            "<si><t>P1</t></si><si><t>Twist</t></si><si><t>Shiled</t></si><si><t>A-</t></si><si><t>A+</t></si>"
            "<si><t>B-</t></si><si><t>B+</t></si><si><r><t>S</t></r><r><t>1</t></r></si><si><t>S2</t></si>"
            "<si><t>Mating Connector</t></si></sst>");
        // Columns: A Cable Name, B Connector Name, C..F two twisted pairs, G..H a shield; I mating.
        const QString pinout = QStringLiteral(
            "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>"
            "<row r=\"2\"><c r=\"C2\" t=\"s\"><v>5</v></c><c r=\"D2\" t=\"s\"><v>5</v></c><c r=\"E2\" t=\"s\"><v>5</v></c>"
            "<c r=\"F2\" t=\"s\"><v>5</v></c><c r=\"G2\" t=\"s\"><v>6</v></c></row>"
            "<row r=\"3\"><c r=\"A3\" t=\"s\"><v>0</v></c><c r=\"B3\" t=\"s\"><v>1</v></c><c r=\"C3\" t=\"s\"><v>7</v></c>"
            "<c r=\"D3\" t=\"s\"><v>8</v></c><c r=\"E3\" t=\"s\"><v>9</v></c><c r=\"F3\" t=\"s\"><v>10</v></c>"
            "<c r=\"G3\" t=\"s\"><v>11</v></c><c r=\"H3\" t=\"s\"><v>12</v></c><c r=\"I3\" t=\"s\"><v>13</v></c></row>"
            "<row r=\"4\"><c r=\"A4\" t=\"s\"><v>2</v></c><c r=\"B4\" t=\"s\"><v>3</v></c><c r=\"C4\"><v>1</v></c>"
            "<c r=\"D4\"><v>2</v></c><c r=\"E4\"><v>3</v></c><c r=\"F4\"><v>4.0000000000001</v></c><c r=\"G4\"><v>5</v></c>"
            "<c r=\"H4\"><v>6</v></c></row>"
            "<row r=\"5\"><c r=\"B5\" t=\"s\"><v>4</v></c><c r=\"C5\"><v>1</v></c><c r=\"D5\"><v>2</v></c>"
            "<c r=\"E5\"><v>3</v></c><c r=\"F5\"><v>4</v></c><c r=\"G5\"><v>5</v></c><c r=\"H5\"><v>6</v></c></row>"
            "</sheetData><mergeCells count=\"2\"><mergeCell ref=\"A4:A5\"/><mergeCell ref=\"G2:H2\"/></mergeCells></worksheet>");
        const QString workbook = QStringLiteral(
            "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>"
            "<sheet name=\"Harnesses\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>");
        const QString rels = QStringLiteral(
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rId1\" Type=\"x\" Target=\"/xl/worksheets/sheet1.xml\"/></Relationships>");
        const QByteArray excel = writeZip({entry("xl/workbook.xml", workbook), entry("xl/_rels/workbook.xml.rels", rels),
                                           entry("xl/sharedStrings.xml", shared), entry("xl/worksheets/sheet1.xml", pinout)});
        HarnessDemand fromExcel;
        const bool readOk = harnessDemandFromXlsx(excel, fromExcel, &error);
        const DemandCable* c = fromExcel.cables.empty() ? nullptr : &fromExcel.cables.front();
        QStringList groups;
        if (c)
            for (const DemandSignal& s : c->signalColumns) groups << QStringLiteral("%1:%2#%3").arg(s.name, s.group).arg(s.groupId);
        std::printf("  demand from Excel: %s, %s, %s\n", readOk ? "read" : qPrintable(error), c ? qPrintable(c->name) : "-",
                    qPrintable(groups.join(QStringLiteral(" "))));
        check(readOk && c && c->name == QLatin1String("HGC1") && c->connectors.size() == 2 &&
                  c->connectors[1].name == QLatin1String("P1") && c->connectors[0].cells[3] == QLatin1String("4"),
              "demand: a table as Excel saves it reads - shared strings, numbers, the name merged down its rows");
        check(c && c->signalColumns.size() == 6 && c->signalColumns[0].groupId == c->signalColumns[1].groupId &&
                  c->signalColumns[2].groupId == c->signalColumns[3].groupId && c->signalColumns[1].groupId != c->signalColumns[2].groupId &&
                  c->signalColumns[4].group == QLatin1String("Shield") && c->signalColumns[5].groupId == c->signalColumns[4].groupId &&
                  c->signalColumns[4].name == QLatin1String("S1"),
              "demand: two pairs written Twist cell by cell are two pairs; a merged Shiled is one shield");
        HarnessDemand none;
        check(!harnessDemandFromXlsx(QByteArray("not a workbook"), none, &error) && !error.isEmpty(),
              "demand: something that is not a workbook is refused with a reason");
    }

    // A BLANK TEMPLATE reads back as nothing filled in.
    {
        HarnessDemand blank;
        const bool blankOk = harnessDemandFromXlsx(harnessDemandToXlsx(HarnessDemand{}), blank, &error);
        check(blankOk && blank.cables.empty(), "demand: a blank template reads back as nothing filled in");
    }

    // FROM THE BUILDER AND BACK: the same wires, groups and coax.
    {
        const DemandCable out = harnessDemandFromDef(ctl, QStringLiteral("HGC700150"));
        const HarnessDef again = harnessDefFromDemand(out);
        auto count = [](const HarnessDef& d, auto pred) { return int(std::count_if(d.wires.begin(), d.wires.end(), pred)); };
        auto twist = [](const HarnessWire& w) { return !w.twistGroup.isEmpty(); };
        auto coax = [](const HarnessWire& w) { return harnessIsCoaxKind(w.kind); };
        std::printf("  demand from the builder: %d wire(s) -> %d signal column(s) -> %d wire(s)\n", int(ctl.wires.size()),
                    int(out.signalColumns.size()), int(again.wires.size()));
        check(again.wires.size() == ctl.wires.size() && count(again, twist) == count(ctl, twist) &&
                  count(again, coax) == count(ctl, coax) && again.connectors.size() == ctl.connectors.size(),
              "demand: the builder's harness sent out as a demand comes back the same");
    }

    // ON PAPER: a page a cable.
    {
        const QString pdf = QDir::temp().filePath(QStringLiteral("loopwire-demand-test.pdf"));
        QFile::remove(pdf);
        QString pdfError;
        const bool wrote = writeHarnessDemandPdf(read, pdf, &pdfError);
        QFile f(pdf);
        const bool opened = f.open(QIODevice::ReadOnly);
        const QByteArray all = opened ? f.readAll() : QByteArray();
        f.close();
        const int pages = int(all.count("/Type /Page\n") + all.count("/Type /Page\r") + all.count("/Type /Page "));
        std::printf("  demand overview: %s, %d bytes\n", wrote ? "written" : qPrintable(pdfError), int(all.size()));
        check(wrote && all.startsWith("%PDF") && all.size() > 2000, "demand: the overview is written as a PDF");
        (void)pages;
        QFile::remove(pdf);
    }
}

// THE WIZARD'S TABLES, BOM AND STEPS: the delete button last, loose wires grouped by trunk, short detail fields, a BOM
// that can be attached to the window, no tick on a step with nothing in it, a lit pin washed lightly.
void testWizardTablesAndBom() {
    QSettings settings;
    const QString uiKey = QStringLiteral("harnessBuilder/ui/v2/");
    const QVariant priorDock = settings.value(uiKey + QStringLiteral("bomDock"));
    settings.remove(uiKey + QStringLiteral("bomDock"));
    Document doc;
    Editor editor;
    editor.setDocument(&doc);
    HarnessBuilderDialog wizard(&editor);
    wizard.resize(1200, 800);
    wizard.show();
    QApplication::processEvents();
    auto* steps = wizard.findChild<QListWidget*>(QStringLiteral("harnessBuilderSteps"));
    auto blankIcon = [&](int step) {
        if (!steps || step >= steps->count()) return false;
        const QImage img = steps->item(step)->icon().pixmap(36, 36).toImage();
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qAlpha(img.pixel(x, y)) > 0) return false;
        return true;
    };
    check(blankIcon(0) && blankIcon(1) && blankIcon(2) && blankIcon(3),
          "wizard steps: nothing filled in, no step is ticked");

    HarnessDef def;
    HarnessConnector j1;
    j1.id = QStringLiteral("c1");
    j1.designator = QStringLiteral("J1");
    j1.pinCount = 6;
    HarnessConnector p1 = j1;
    p1.id = QStringLiteral("c2");
    p1.designator = QStringLiteral("P1");
    p1.side = HarnessSide::Right;
    def.connectors = {j1, p1};
    for (int i = 1; i <= 3; ++i) {
        HarnessWire w;
        w.id = QStringLiteral("w%1").arg(i);
        w.fromConn = j1.id;
        w.toConn = p1.id;
        w.fromPin = w.toPin = i;
        def.wires.push_back(w);
    }
    wizard.loadDemand(def, QStringLiteral("test"));
    wizard.setStep(1);
    QApplication::processEvents();
    check(blankIcon(0) && blankIcon(1) && blankIcon(2),
          "wizard steps: populated steps wait for explicit checks");
    auto* checks = wizard.findChild<QPushButton*>(QStringLiteral("wizardChecks"));
    if (checks) checks->click();
    check(blankIcon(0) && !blankIcon(1) && blankIcon(2),
          "wizard steps: checking wires approves only wires");

    auto* wires = wizard.findChild<QTableWidget*>(QStringLiteral("wizardWireTable"));
    auto* conns = wizard.findChild<QTableWidget*>(QStringLiteral("wizardConnectorTable"));
    QStringList headers;
    int wireRows = 0;
    for (int r = 0; wires && r < wires->rowCount(); ++r) {
        if (wires->columnSpan(r, 0) > 1) {
            if (auto* b = qobject_cast<QToolButton*>(wires->cellWidget(r, 0))) headers << b->text().simplified();
        } else {
            ++wireRows;
        }
    }
    std::printf("  wizard: wire groups %s\n", qPrintable(headers.join(QStringLiteral(" | "))));
    check(headers.size() == 1 && headers.front().contains(QLatin1String("J1")) && headers.front().contains(QLatin1String("P1")) &&
              wireRows == 3,
          "wizard wires: three loose wires between J1 and P1 are one group, by their trunk - not a group a wire");
    auto deleteLast = [](QTableWidget* t) {
        if (!t) return false;
        const QTableWidgetItem* heading = t->horizontalHeaderItem(12);
        return !t->isColumnHidden(12) && t->horizontalHeader()->visualIndex(12) == t->columnCount() - 1 &&
               (!heading || heading->text().isEmpty());
    };
    check(deleteLast(wires) && deleteLast(conns), "wizard tables: the delete button is the last column, with no heading");
    bool everyRowDeletes = wires != nullptr;
    for (int r = 0; wires && r < wires->rowCount(); ++r)
        if (wires->columnSpan(r, 0) == 1) everyRowDeletes = everyRowDeletes && qobject_cast<QToolButton*>(wires->cellWidget(r, 12));
    check(everyRowDeletes, "wizard wires: every wire row has its delete button");

    // SHORT DETAIL FIELDS, whatever the window's width.
    int row = -1;
    for (int r = 0; wires && r < wires->rowCount(); ++r)
        if (wires->columnSpan(r, 0) == 1) { row = r; break; }
    if (wires && row >= 0) emit wires->cellClicked(row, 0);
    QApplication::processEvents();
    auto fieldWidth = [&] {
        auto* gauge = wizard.findChild<QWidget*>(QStringLiteral("wizardGauge"));
        return gauge ? gauge->width() : -1;
    };
    const int narrow = fieldWidth();
    wizard.resize(1600, 800);
    QApplication::processEvents();
    const int wide = fieldWidth();
    std::printf("  wizard: gauge field %d px at 1200 wide, %d px at 1600\n", narrow, wide);
    check(narrow > 0 && narrow == wide && narrow <= 220, "wizard details: a field is short and the same width whatever the window's");

    // THE BOM, attached to the window and back.
    if (auto* bom = wizard.findChild<QPushButton*>(QStringLiteral("wizardLiveBom"))) bom->click();
    QApplication::processEvents();
    auto* dock = wizard.findChild<QComboBox*>(QStringLiteral("wizardBomDock"));
    auto* panel = wizard.findChild<QWidget*>(QStringLiteral("wizardBomPanel"));
    auto* split = wizard.findChild<QSplitter*>(QStringLiteral("wizardBomSplit"));
    if (dock) dock->setCurrentIndex(dock->findData(QStringLiteral("right")));
    QApplication::processEvents();
    check(panel && split && panel->parentWidget() == split && split->indexOf(panel) == 1 && split->orientation() == Qt::Horizontal &&
              panel->isVisible(),
          "wizard BOM: attached on the right of the wizard");
    if (dock) dock->setCurrentIndex(dock->findData(QStringLiteral("bottom")));
    QApplication::processEvents();
    check(panel && split && panel->parentWidget() == split && split->orientation() == Qt::Vertical,
          "wizard BOM: attached at the bottom");
    if (dock) dock->setCurrentIndex(dock->findData(QStringLiteral("window")));
    QApplication::processEvents();
    check(panel && split && panel->parentWidget() != split && panel->window() != &wizard,
          "wizard BOM: back in a window of its own");

    // THE PIN MAP: a dragged pin shows where it will land.
    bool dropShown = false;
    for (auto* list : wizard.findChildren<QListWidget*>())
        if (list->objectName().startsWith(QLatin1String("harnessBuilderPins_")))
            dropShown = list->showDropIndicator() && list->style() != wizard.style();
    check(dropShown, "wizard pin map: a dragged pin's place is drawn by the list's own drop line");
    if (priorDock.isValid()) settings.setValue(uiKey + QStringLiteral("bomDock"), priorDock);
    else settings.remove(uiKey + QStringLiteral("bomDock"));
}

// THE WIZARD, SECOND ROUND: one view switch, optimisers on together, a see-through dragged pin, Fit on the picture, a
// new wire where the last went, Enter applies a field only, gauges in the app's notation, part lists shown whole, and
// a drain into its screen's leg from its connector's side.
void testWizardSecondRound() {
    Document doc;
    Editor editor;
    editor.setDocument(&doc);
    HarnessBuilderDialog wizard(&editor);
    wizard.resize(1200, 800);
    wizard.show();
    QApplication::processEvents();
    HarnessDef def;
    auto connector = [](const char* id, const char* name, HarnessSide side, int order) {
        HarnessConnector c;
        c.id = QLatin1String(id);
        c.designator = QLatin1String(name);
        c.side = side;
        c.order = order;
        c.pinCount = 6;
        return c;
    };
    def.connectors = {connector("c1", "J1", HarnessSide::Left, 0), connector("c2", "P1", HarnessSide::Right, 0),
                      connector("c3", "P2", HarnessSide::Right, 1)};
    auto wire = [](const char* id, const char* from, int fp, const char* to, int tp) {
        HarnessWire w;
        w.id = QLatin1String(id);
        w.fromConn = QLatin1String(from);
        w.fromPin = fp;
        w.toConn = QLatin1String(to);
        w.toPin = tp;
        w.gauge = QStringLiteral("0.5");
        return w;
    };
    // A pair twisted together but split apart on P2, so the twist optimiser has something to do.
    def.wires = {wire("w1", "c1", 1, "c2", 1), wire("w2", "c1", 2, "c3", 1), wire("w3", "c1", 3, "c3", 4),
                 wire("w4", "c1", 4, "c3", 2)};   // w4 between the pair on P2: the pair is split
    def.wires[1].twistGroup = def.wires[2].twistGroup = QStringLiteral("TP1");
    wizard.loadDemand(def, QStringLiteral("round two"));
    wizard.setStep(1);
    QApplication::processEvents();

    // ONE VIEW SWITCH.
    auto* viewSwitch = wizard.findChild<QPushButton*>(QStringLiteral("harnessBuilderViewSwitch"));
    const QString before = viewSwitch ? viewSwitch->text() : QString();
    if (viewSwitch) viewSwitch->click();
    QApplication::processEvents();
    const bool onMap = wizard.showingPinMap();
    const QString during = viewSwitch ? viewSwitch->text() : QString();
    if (viewSwitch) viewSwitch->click();
    QApplication::processEvents();
    check(viewSwitch && before == QLatin1String("Pin map") && onMap && during == QLatin1String("Drawing") && !wizard.showingPinMap() &&
              !wizard.findChild<QPushButton*>(QStringLiteral("harnessBuilderShowDrawing")),
          "wizard view: one button switches the drawing and the pin map, saying which it will show");

    // THE OPTIMISERS, ON AND OFF, TOGETHER.
    auto* cross = wizard.findChild<QPushButton*>(QStringLiteral("harnessBuilderMinimise"));
    auto* twist = wizard.findChild<QPushButton*>(QStringLiteral("harnessBuilderOptTwist"));
    auto* shieldOpt = wizard.findChild<QPushButton*>(QStringLiteral("harnessBuilderOptShield"));
    const int splitBefore = harnessBundleBreaks(wizard.definition(), false);
    if (twist) twist->click();
    if (cross) cross->click();
    check(harnessBundleBreaks(wizard.definition(), false) == splitBefore,
          "wizard optimisers: toggling criteria leaves the pin order unchanged");
    auto* applyOptimisation = wizard.findChild<QPushButton*>(QStringLiteral("harnessBuilderApplyOptimisation"));
    check(applyOptimisation, "wizard optimisers: a separate apply button is available");
    if (applyOptimisation) applyOptimisation->click();
    QApplication::processEvents();
    std::printf("  wizard optimisers: split twists %d -> %d, crossings %d\n", splitBefore,
                harnessBundleBreaks(wizard.definition(), false), harnessCrossings(wizard.definition()));
    check(cross && twist && shieldOpt && cross->isCheckable() && twist->isChecked() && cross->isChecked() && !shieldOpt->isChecked(),
          "wizard optimisers: on/off buttons, each keeping its state");
    check(splitBefore > 0 && harnessBundleBreaks(wizard.definition(), false) == 0,
          "wizard optimisers: with twist and crossings on, the pair is put on neighbouring pins");
    {
        HarnessDef untouched = def;
        const HarnessDef copy = untouched;
        optimizeHarnessCombined(untouched, false, false, false);
        bool same = true;
        for (size_t i = 0; i < copy.connectors.size(); ++i)
            same = same && harnessPinSequence(copy.connectors[i]) == harnessPinSequence(untouched.connectors[i]);
        check(same, "wizard optimisers: with nothing on, the pinout is left alone");
    }
    if (twist) twist->click();
    if (cross) cross->click();

    // FIT ON THE PICTURE, see-through.
    auto* fit = wizard.findChild<QToolButton*>(QStringLiteral("harnessBuilderPreviewFit"));
    check(fit && fit->text() == QLatin1String("Fit") && fit->styleSheet().contains(QLatin1String("rgba")) &&
              fit->parentWidget() && fit->parentWidget()->objectName() == QLatin1String("harnessBuilderPreviewPicture"),
          "wizard preview: a see-through Fit button over the picture");

    // A NEW WIRE WHERE THE LAST WENT: J1 to P2, not the first right-hand connector.
    if (auto* add = wizard.findChild<QAbstractButton*>(QStringLiteral("harnessBuilderAddWire"))) add->click();
    QApplication::processEvents();
    const HarnessWire& added = wizard.definition().wires.back();
    std::printf("  wizard add wire: %s-%d to %s-%d\n", qPrintable(added.fromConn), added.fromPin, qPrintable(added.toConn), added.toPin);
    check(wizard.definition().wires.size() == 5 && added.fromConn == QLatin1String("c1") && added.toConn == QLatin1String("c3") &&
              added.toPin > 0,
          "wizard add wire: the new wire goes to the connectors the last one went to, on a free pin");

    // ENTER IN A FIELD APPLIES IT, AND DRAWS NOTHING.
    const size_t drawnBefore = doc.entities().size();
    auto* field = wizard.findChild<QLineEdit*>();
    for (auto* edit : wizard.findChildren<QLineEdit*>())
        if (edit->isVisible() && edit->isEnabled()) { field = edit; break; }
    if (field) {
        field->setFocus();
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(field, &enter);
    }
    QApplication::processEvents();
    check(field && doc.entities().size() == drawnBefore && wizard.isVisible(),
          "wizard fields: Enter applies the field and does not generate the drawing");

    // GAUGES IN AWG, when the options say AWG.
    const bool wasAwg = wireGaugeNotationAwg();
    setWireGaugeNotationAwg(true);
    if (auto* wires = wizard.findChild<QTableWidget*>(QStringLiteral("wizardWireTable")))
        for (int r = 0; r < wires->rowCount(); ++r)
            if (wires->columnSpan(r, 0) == 1) { emit wires->cellClicked(r, 0); break; }
    QApplication::processEvents();
    auto* gaugeBox = wizard.findChild<QComboBox*>(QStringLiteral("wizardGauge"));
    std::printf("  wizard gauge: %s\n", gaugeBox ? qPrintable(gaugeBox->currentText()) : "-");
    check(gaugeBox && gaugeBox->currentText().contains(QLatin1String("AWG")), "wizard details: the gauge reads in AWG when the options say AWG");
    setWireGaugeNotationAwg(wasAwg);

    // A PART LIST SHOWN WHOLE: a drop-down opens as wide as its longest line.
    {
        installWideComboPopups(QCoreApplication::instance());
        QWidget host;
        host.resize(300, 100);
        auto* box = new QComboBox(&host);
        box->setFixedWidth(90);
        box->addItem(QStringLiteral("Connector · A19507-801 (MMCP-27-WD-10.0-C-PP-OR-B) Omnetics, 27 way"));
        box->addItem(QStringLiteral("Wire"));
        host.show();
        QApplication::processEvents();
        box->showPopup();
        QApplication::processEvents();
        QWidget* popup = box->view()->window();
        const int want = box->view()->sizeHintForColumn(0);
        std::printf("  part list popup: %d px for a %d px line\n", popup ? popup->width() : -1, want);
        check(popup && popup->width() >= want, "part lists: a drop-down opens wide enough for its longest line");
        box->hidePopup();
    }

    // A DRAIN INTO ITS SCREEN'S LEG FROM ITS CONNECTOR'S SIDE.
    {
        HarnessDef shielded;
        shielded.connectors = {connector("a", "J1", HarnessSide::Left, 0), connector("b", "P1", HarnessSide::Right, 0)};
        shielded.wires = {wire("s1", "a", 1, "b", 1), wire("s2", "a", 2, "b", 2), wire("s3", "a", 4, "b", 4)};
        shielded.wires[0].shieldGroup = shielded.wires[1].shieldGroup = QStringLiteral("S1");
        HarnessWire drainL = wire("dl", "a", 6, "", 0);
        drainL.toConn = harnessInternalShieldKey(QStringLiteral("S1"));
        drainL.color = QStringLiteral("DRAIN");
        shielded.wires.push_back(drainL);
        HarnessDef rightHand = shielded;
        rightHand.wires.back().fromConn = QStringLiteral("b");
        for (const HarnessDef* d : {&shielded, &rightHand}) {
            const bool fromLeft = d == &shielded;
            const Entity* drain = nullptr;
            const HarnessBuild built = buildHarness(*d);   // kept: the drain is read after the loop
            for (const Entity& e : built.entities)
                if (e.type == EntityType::Wire && e.meta.value(QStringLiteral("shieldLeg")).toString().size()) drain = &e;
            bool level = false, fromItsSide = false;
            if (drain && drain->points.size() >= 2) {
                const Vec2 end = drain->points.back(), before2 = drain->points[drain->points.size() - 2];
                level = std::abs(end.y - before2.y) < 1e-6;
                fromItsSide = fromLeft ? before2.x < end.x : before2.x > end.x;
            }
            std::printf("  drain from the %s connector: %d point(s), into the leg %s\n", fromLeft ? "left" : "right",
                        drain ? int(drain->points.size()) : 0, level ? (fromItsSide ? "level, from its side" : "level, other side") : "upright");
            check(drain && level && fromItsSide, fromLeft ? "shield drain: from a left connector it comes into the leg from the left"
                                                           : "shield drain: from a right connector it comes into the leg from the right");
        }
    }
}

// WIRES MADE INTO ONE THING, AND BACK: twisted, screened with a drain, one cable, one coax - each named afresh - then taken
// out again, a screen's drain with it; a wire moved in the list; what each is part of, said.
void testWireGrouping() {
    HarnessDef d;
    HarnessConnector p1, p2;
    p1.id = QStringLiteral("P1");
    p1.designator = QStringLiteral("P1");
    p1.pinCount = 8;
    p2 = p1;
    p2.id = QStringLiteral("P2");
    p2.designator = QStringLiteral("P2");
    p2.side = HarnessSide::Right;
    d.connectors = {p1, p2};
    for (int i = 1; i <= 6; ++i) {
        HarnessWire w;
        w.id = harnessFreshWireId(d);
        w.fromConn = QStringLiteral("P1");
        w.fromPin = i;
        w.toConn = QStringLiteral("P2");
        w.toPin = i;
        d.wires.push_back(w);
    }
    check(d.wires[5].id == QLatin1String("w6") && harnessFreshWireId(d) == QLatin1String("w7"), "grouping: wire ids given in turn");
    auto wire = [&](const QString& id) -> const HarnessWire& {
        static const HarnessWire none;
        for (const HarnessWire& w : d.wires)
            if (w.id == id) return w;
        return none;
    };
    // TWISTED: two pairs, two names.
    const QString t1 = harnessTwistWires(d, {QStringLiteral("w1"), QStringLiteral("w2")});
    const QString t2 = harnessTwistWires(d, {QStringLiteral("w3"), QStringLiteral("w4")});
    check(t1 == QLatin1String("T1") && t2 == QLatin1String("T2") && wire(QStringLiteral("w2")).twistGroup == t1 &&
              wire(QStringLiteral("w4")).twistGroup == t2,
          "grouping: two pairs twisted - two twisted groups, not one of four");
    check(harnessTwistWires(d, {QStringLiteral("w5")}).isEmpty(), "grouping: one wire alone is not twisted");
    check(harnessGroupCaption(d, wire(QStringLiteral("w1"))) == QLatin1String("twisted pair T1"), "grouping: said as a twisted pair");
    // SCREENED, with its drain to a pin.
    const size_t before = d.wires.size();
    const QString s1 = harnessScreenWires(d, {QStringLiteral("w1"), QStringLiteral("w2")}, QStringLiteral("P1"), 8);
    const HarnessWire& drain = d.wires.back();
    check(s1 == QLatin1String("S1") && d.wires.size() == before + 1 && drain.fromConn == harnessInternalShieldKey(s1) &&
              drain.toConn == QLatin1String("P1") && drain.toPin == 8 && drain.color == QLatin1String("DRAIN") &&
              !harnessIsDrainRow(drain),
          "grouping: a pair screened - its drain a wire from the screen to the pin given");
    check(harnessGroupCaption(d, wire(QStringLiteral("w1"))).startsWith(QLatin1String("screened twisted pair")),
          "grouping: said as a screened twisted pair");
    // ONE CABLE, and a coax.
    const QString cable = harnessCableWires(d, {QStringLiteral("w1"), QStringLiteral("w2"), QStringLiteral("w3"), QStringLiteral("w4")});
    check(cable == QLatin1String("CABLE1") && wire(QStringLiteral("w3")).bundle == cable && wire(QStringLiteral("w1")).twistGroup == t1,
          "grouping: four wires one cable - the pairs inside it still twisted");
    const QString coax = harnessCoaxWires(d, {QStringLiteral("w5"), QStringLiteral("w6")});
    check(coax == QLatin1String("COAX1") && wire(QStringLiteral("w5")).coaxRole == QLatin1String("signal") &&
              wire(QStringLiteral("w6")).coaxRole == QLatin1String("shield") && wire(QStringLiteral("w6")).kind == QLatin1String("coax"),
          "grouping: two wires a coax - the first its signal, the second its screen");
    check(harnessCoaxWires(d, {QStringLiteral("w1")}).isEmpty(), "grouping: a coax is two wires, no fewer");
    const std::vector<HarnessIssue> issues = validateHarness(d);
    bool twistIssue = false;
    for (const HarnessIssue& i : issues) twistIssue = twistIssue || i.key.startsWith(QLatin1String("twist"));
    check(!twistIssue, "grouping: the pairs made are neighbours - nothing for the checks to say about them");
    // TAKEN OUT AGAIN: the screen's drain goes with the last wire out of it.
    check(harnessUngroupWires(d, {QStringLiteral("w1")}, HarnessGrouping::Screen) == 1 &&
              std::any_of(d.wires.begin(), d.wires.end(), [&](const HarnessWire& w) { return w.fromConn == harnessInternalShieldKey(s1); }),
          "ungroup: one wire out of the screen - the drain stays for the other");
    harnessUngroupWires(d, {QStringLiteral("w2")}, HarnessGrouping::Screen);
    check(d.wires.size() == before && std::none_of(d.wires.begin(), d.wires.end(), [&](const HarnessWire& w) { return w.fromConn == harnessInternalShieldKey(s1); }),
          "ungroup: the last wire out - the drain goes with the screen");
    check(harnessUngroupWires(d, {QStringLiteral("w5"), QStringLiteral("w6")}, HarnessGrouping::Cable) == 2 &&
              wire(QStringLiteral("w6")).kind == QLatin1String("wire") && wire(QStringLiteral("w6")).bundle.isEmpty(),
          "ungroup: the coax taken apart - two plain wires again");
    harnessUngroupWires(d, {QStringLiteral("w1"), QStringLiteral("w2"), QStringLiteral("w3"), QStringLiteral("w4")}, HarnessGrouping::All);
    check(std::all_of(d.wires.begin(), d.wires.end(), [](const HarnessWire& w) { return w.twistGroup.isEmpty() && w.bundle.isEmpty(); }),
          "ungroup: all of it - every wire on its own");
    // MOVED in the list.
    check(harnessMoveWire(d, QStringLiteral("w5"), 0) && d.wires.front().id == QLatin1String("w5") && d.wires[1].id == QLatin1String("w1"),
          "move: a wire to the top of the list");
    check(harnessMoveWire(d, QStringLiteral("w5"), 3) && d.wires[2].id == QLatin1String("w5") && d.wires[3].id == QLatin1String("w3"),
          "move: down, to before the wire that was third");
    check(harnessMoveWire(d, QStringLiteral("w5"), 99) && d.wires.back().id == QLatin1String("w5") && !harnessMoveWire(d, QStringLiteral("nope"), 0),
          "move: past the end - the last; no such wire, nothing moved");
}

// THE WIRES STEP, SIMPLER: one Add button (its menu a twisted pair, a screened pair, a cable, a coax), Group as on the rows
// picked, a Group column saying what each row is part of, the panel editing every row picked at once - and the old
// controls (Unify, All gauges, the column-wide ticks, the toolbar's coax, copy and swap) gone.
void testWizardSimplerWiring() {
    Document doc;
    Editor editor;
    editor.setDocument(&doc);
    HarnessBuilderDialog wizard(&editor);
    wizard.resize(1200, 800);
    wizard.show();
    QApplication::processEvents();
    HarnessDef def;
    for (const auto& [id, side] : {std::pair<const char*, HarnessSide>{"c1", HarnessSide::Left}, {"c2", HarnessSide::Right}}) {
        HarnessConnector c;
        c.id = QLatin1String(id);
        c.designator = QString::fromLatin1(id).toUpper();
        c.side = side;
        c.pinCount = 10;
        def.connectors.push_back(c);
    }
    wizard.loadDemand(def, QStringLiteral("simpler wiring"));
    wizard.setStep(1);
    QApplication::processEvents();
    auto* table = wizard.findChild<QTableWidget*>(QStringLiteral("wizardWireTable"));
    auto pick = [&](const QStringList& ids) {
        table->clearSelection();
        for (int row = 0; row < table->rowCount(); ++row)
            if (auto* cell = table->cellWidget(row, 0); cell && ids.contains(cell->property("wireId").toString()))
                table->selectionModel()->select(table->model()->index(row, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        QApplication::processEvents();
    };
    auto wireOf = [&](const QString& id) -> HarnessWire {
        for (const HarnessWire& w : wizard.definition().wires)
            if (w.id == id) return w;
        return {};
    };
    // THE OLD CONTROLS GONE.
    bool oldThere = false;
    for (QAbstractButton* b : wizard.findChildren<QAbstractButton*>())
        oldThere = oldThere || b->text().startsWith(QLatin1String("Unify")) || b->text() == QLatin1String("Un-bundle") ||
                   b->text() == QLatin1String("Finish unify") || b->text() == QLatin1String("Add cable");
    for (QLabel* l : wizard.findChildren<QLabel*>()) oldThere = oldThere || l->text() == QLatin1String("All gauges") || l->text() == QLatin1String("Show on wire");
    for (const char* name : {"wizardAddCoax", "wizardDuplicateConnection", "wizardSwapEnds", "wizardApplySelected"})
        oldThere = oldThere || wizard.findChild<QWidget*>(QLatin1String(name));
    check(table && !oldThere, "simpler wiring: Unify, All gauges, the column ticks and the toolbar's coax / copy / swap are gone");
    auto* add = wizard.findChild<QToolButton*>(QStringLiteral("harnessBuilderAddWire"));
    check(add && add->menu() && add->text() == QLatin1String("Add wire") && wizard.findChild<QAction*>(QStringLiteral("harnessBuilderAddTwisted")) &&
              wizard.findChild<QAction*>(QStringLiteral("harnessBuilderAddCable")) && wizard.findChild<QAction*>(QStringLiteral("harnessBuilderAddCoax")),
          "simpler wiring: one Add wire button - a twisted or screened pair, a cable, a coax on its menu");
    // A TWISTED PAIR IN ONE CLICK, picked, said in its Group column.
    const QStringList pair = wizard.addConnection(HarnessBuilderDialog::AddWhat::TwistedPair);
    QApplication::processEvents();
    check(pair.size() == 2 && !wireOf(pair[0]).twistGroup.isEmpty() && wireOf(pair[0]).twistGroup == wireOf(pair[1]).twistGroup &&
              wireOf(pair[0]).fromPin != wireOf(pair[1]).fromPin,
          "simpler wiring: Add twisted pair - two wires, twisted, on pins of their own");
    QString caption;
    for (int row = 0; row < table->rowCount(); ++row)
        if (auto* cell = qobject_cast<QToolButton*>(table->cellWidget(row, 11)); cell && cell->objectName() == QLatin1String("wizardGroupCell"))
            caption = cell->text();
    check(caption.startsWith(QLatin1String("twisted pair")) && table->horizontalHeaderItem(11)->text() == QLatin1String("Group") &&
              table->isColumnHidden(5) && table->isColumnHidden(10),
          "simpler wiring: the Group column says it - no Bundle or Internal shield columns to type into");
    // EDITED TOGETHER: both picked, one colour for both.
    pick(pair);
    auto* box = wizard.findChild<QGroupBox*>(QStringLiteral("wizardConnectionBox"));
    check(box && box->title().startsWith(QLatin1String("2 connections picked")), "simpler wiring: two rows picked - the panel edits both");
    if (auto* base = wizard.findChild<QComboBox*>(QStringLiteral("wizardBaseColor"))) base->setCurrentIndex(base->findData(QStringLiteral("RD")));
    QApplication::processEvents();
    check(wireOf(pair[0]).color == QLatin1String("RD") && wireOf(pair[1]).color == QLatin1String("RD"), "simpler wiring: a colour set once - both wires red");
    check(wizard.findChild<QGroupBox*>(QStringLiteral("wizardConnectionBox")) &&
              wizard.findChild<QGroupBox*>(QStringLiteral("wizardConnectionBox"))->title().startsWith(QLatin1String("2 connections")),
          "simpler wiring: still both picked after the change");
    // GROUP AS on what is picked: two more wires screened, their drain landed on P2; all four one cable.
    const QStringList two = {wizard.addConnection(HarnessBuilderDialog::AddWhat::Wire).value(0),
                             wizard.addConnection(HarnessBuilderDialog::AddWhat::Wire).value(0)};
    pick(two);
    const size_t before = wizard.definition().wires.size();
    const QString screen = wizard.groupSelectedWires(HarnessBuilderDialog::GroupAs::Screened, QStringLiteral("c2"));
    const HarnessWire drain = wizard.definition().wires.back();
    check(!screen.isEmpty() && wizard.definition().wires.size() == before + 1 && wireOf(two[1]).shieldGroup == screen &&
              drain.fromConn == harnessInternalShieldKey(screen) && drain.toConn == QLatin1String("c2") && drain.toPin > 0,
          "simpler wiring: Group as Screened - the two wires screened, the drain landed on P2's next free pin");
    pick(pair + two);
    const QString cable = wizard.groupSelectedWires(HarnessBuilderDialog::GroupAs::Cable);
    check(!cable.isEmpty() && wireOf(pair[0]).bundle == cable && wireOf(two[1]).bundle == cable && !wireOf(pair[0]).twistGroup.isEmpty(),
          "simpler wiring: Group as One cable - four wires one cable, the pair still twisted inside it");
    pick(two);
    wizard.groupSelectedWires(HarnessBuilderDialog::GroupAs::UngroupScreen);
    check(wireOf(two[0]).shieldGroup.isEmpty() &&
              std::none_of(wizard.definition().wires.begin(), wizard.definition().wires.end(), [&](const HarnessWire& w) { return w.fromConn == harnessInternalShieldKey(screen); }),
          "simpler wiring: Not screened - the screen and its drain gone");
    wizard.groupSelectedWires(HarnessBuilderDialog::GroupAs::Twisted);
    check(wireOf(two[0]).twistGroup == wireOf(two[1]).twistGroup && wireOf(two[0]).twistGroup != wireOf(pair[0]).twistGroup,
          "simpler wiring: Group as Twisted - a second pair, named apart from the first");
    // A COAX FROM THE MENU; copy and swap from the row menu.
    const QStringList coax = wizard.addConnection(HarnessBuilderDialog::AddWhat::Coax);
    check(coax.size() == 2 && wireOf(coax[0]).coaxRole == QLatin1String("signal") && harnessCoaxShield(wireOf(coax[1])),
          "simpler wiring: Add coax - a signal and its screen");
    pick({two[0]});
    const HarnessWire was = wireOf(two[0]);
    wizard.swapSelectedWireEnds();
    check(wireOf(two[0]).fromConn == was.toConn && wireOf(two[0]).toPin == was.fromPin, "simpler wiring: Swap ends, from the row's menu");
    const size_t count = wizard.definition().wires.size();
    wizard.duplicateSelectedWire();
    check(wizard.definition().wires.size() > count, "simpler wiring: Duplicate, from the row's menu");
    check(wizard.groupSelectedWires(HarnessBuilderDialog::GroupAs::Coax).isEmpty(), "simpler wiring: a coax is two wires - one picked, nothing made");
    if (std::getenv("LOOPWIRE_SHOTS")) {
        for (QToolButton* b : wizard.findChildren<QToolButton*>())
            if (b->text() == QLatin1String("Expand all")) b->click();
        pick(pair);
        QApplication::processEvents();
        wizard.grab().save(QStringLiteral("wires_step.png"));
    }
}

// THE WIRING WINDOW: two connectors as bricks of pin squares; a pin clicked and then another wires them, the next pair
// offered and taken with Enter; dragged pin to pin, a wire; a wired pin dragged to a free one, its end moved; a line
// dragged up the stack, moved in the list; a stroke across lines twists them, screens them (the drain asked), picks them;
// the lines picked given a colour in one go; Delete; Auto 1:1; several connectors; a pin with two wires said; undone.
void testWiringWindow() {
    Document doc;
    Editor editor;
    editor.setDocument(&doc);
    HarnessBuilderDialog wizard(&editor);
    wizard.resize(1200, 800);
    wizard.show();
    QApplication::processEvents();
    HarnessDef def;
    for (const auto& [id, side, pins] : {std::tuple<const char*, HarnessSide, int>{"c1", HarnessSide::Left, 8},
                                         {"c2", HarnessSide::Right, 8},
                                         {"c3", HarnessSide::Right, 4}}) {
        HarnessConnector c;
        c.id = QLatin1String(id);
        c.designator = QString::fromLatin1(id).toUpper();
        c.side = side;
        c.pinCount = pins;
        def.connectors.push_back(c);
    }
    wizard.loadDemand(def, QStringLiteral("wiring window"));
    wizard.setStep(1);
    QApplication::processEvents();
    check(wizard.findChild<QPushButton*>(QStringLiteral("harnessBuilderWireConnectors")), "wiring window: Wire connectors... on the Wires step");
    WiringDialog* win = wizard.openWiringWindow();
    QApplication::processEvents();
    check(win && win->isVisible() && win->twoMode() && win->canvas()->shown() == QStringList({QStringLiteral("c1"), QStringLiteral("c2")}),
          "wiring window: opened on two connectors, the first left and the first right");
    if (!win) return;
    check(!win->isWindow() && wizard.isAncestorOf(win), "wiring view: inside the wizard, in the table's place - no window of its own");
    win->setAskDrain([] { return QStringLiteral("c2"); });
    WiringCanvas* canvas = win->canvas();
    win->resize(1600, 800);
    QApplication::processEvents();
    // SIDE BY SIDE, in the middle: the lane between them as wide as its words, not the window.
    {
        const double gap = canvas->pinCentre(QStringLiteral("c2"), 1).x() - canvas->pinCentre(QStringLiteral("c1"), 1).x();
        const double middle = (canvas->pinCentre(QStringLiteral("c2"), 1).x() + canvas->pinCentre(QStringLiteral("c1"), 1).x()) / 2.0;
        std::printf("  wiring window: the two %.0f apart, their middle at %.0f of %d\n", gap, middle, canvas->width());
        check(gap > 200 && gap < 650 && std::abs(middle - canvas->width() / 2.0) < 40,
              "wiring window: the two connectors side by side, in the middle - not at the window's edges");
    }
    auto mouse = [&](QEvent::Type type, const QPointF& at, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        QMouseEvent e(type, at, canvas->mapToGlobal(at), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, buttons, mods);
        QApplication::sendEvent(canvas, &e);
    };
    auto click = [&](const QPointF& at) {
        mouse(QEvent::MouseButtonPress, at, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, at, Qt::NoButton);
        QApplication::processEvents();
    };
    auto drag = [&](const QPointF& from, const QPointF& to, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        mouse(QEvent::MouseButtonPress, from, Qt::LeftButton, mods);
        for (int i = 1; i <= 6; ++i) mouse(QEvent::MouseMove, from + (to - from) * (i / 6.0), Qt::LeftButton, mods);
        mouse(QEvent::MouseButtonRelease, to, Qt::NoButton, mods);
        QApplication::processEvents();
    };
    auto key = [&](int k) {
        QKeyEvent e(QEvent::KeyPress, k, Qt::NoModifier);
        QApplication::sendEvent(canvas, &e);
        QApplication::processEvents();
    };
    auto wireOf = [&](const QString& id) -> HarnessWire {
        for (const HarnessWire& w : wizard.definition().wires)
            if (w.id == id) return w;
        return {};
    };
    auto last = [&] { return wizard.definition().wires.empty() ? HarnessWire() : wizard.definition().wires.back(); };
    // A PIN, THEN ANOTHER: a wire; the next pair offered, taken with Enter.
    click(canvas->pinCentre(QStringLiteral("c1"), 1));
    check(canvas->pendingPin() == QLatin1String("c1:1"), "wiring window: a pin clicked waits for its other end");
    click(canvas->pinCentre(QStringLiteral("c2"), 3));
    const HarnessWire first = last();
    check(wizard.definition().wires.size() == 1 && first.fromConn == QLatin1String("c1") && first.fromPin == 1 && first.toConn == QLatin1String("c2") &&
              first.toPin == 3 && canvas->pendingPin().isEmpty(),
          "wiring window: clicked pin to pin - a wire, in the harness the wizard has");
    QString gf, gt;
    int gfp = 0, gtp = 0;
    check(canvas->ghost(gf, gfp, gt, gtp) && gfp == 2 && gtp == 4, "wiring window: the next pair offered - the next free pin on each");
    key(Qt::Key_Return);
    check(wizard.definition().wires.size() == 2 && last().fromPin == 2 && last().toPin == 4, "wiring window: Enter takes it");
    // DRAGGED PIN TO PIN: a wire. A wired pin dragged to a free one: its end moved there.
    drag(canvas->pinCentre(QStringLiteral("c1"), 5), canvas->pinCentre(QStringLiteral("c2"), 1));
    const QString third = last().id;
    check(wizard.definition().wires.size() == 3 && last().fromPin == 5 && last().toPin == 1, "wiring window: dragged from pin to pin - a wire");
    drag(canvas->pinCentre(QStringLiteral("c2"), 1), canvas->pinCentre(QStringLiteral("c2"), 6));
    check(wizard.definition().wires.size() == 3 && wireOf(third).toPin == 6, "wiring window: a wired pin dragged to a free one - its end moved there");
    // A NEW WIRE AT THE BOTTOM OF THE STACK; dragged to the top, first in the list - the table's and the drawing's order.
    check(canvas->barCentre(third).y() > canvas->barCentre(first.id).y(), "wiring window: a new wire's line under the ones before it");
    drag(canvas->barCentre(third), canvas->barCentre(first.id) - QPointF(0, 10));
    check(wizard.definition().wires.front().id == third && canvas->barCentre(third).y() < canvas->barCentre(first.id).y(),
          "wiring window: a line dragged up the stack - first in the list");
    // A STROKE ACROSS TWO LINES: twisted. Another, with Screen: screened, its drain to C2.
    const QString second = wizard.definition().wires[2].id;
    auto strokeAcross = [&](const QString& a, const QString& b) {
        const QPointF pa = canvas->barCentre(a), pb = canvas->barCentre(b);
        drag(QPointF(pa.x() - 20, std::min(pa.y(), pb.y()) - 9), QPointF(pa.x() + 20, std::max(pa.y(), pb.y()) + 9));
    };
    canvas->setTool(WiringCanvas::Tool::Twist);
    strokeAcross(first.id, second);
    check(!wireOf(first.id).twistGroup.isEmpty() && wireOf(first.id).twistGroup == wireOf(second).twistGroup && wireOf(third).twistGroup.isEmpty(),
          "wiring window: a stroke across two lines with Twist - those two twisted, no other");
    canvas->setTool(WiringCanvas::Tool::Screen);
    const size_t before = wizard.definition().wires.size();
    strokeAcross(first.id, second);
    const HarnessWire drain = last();
    check(!wireOf(first.id).shieldGroup.isEmpty() && wizard.definition().wires.size() == before + 1 &&
              drain.fromConn == harnessInternalShieldKey(wireOf(first.id).shieldGroup) && drain.toConn == QLatin1String("c2"),
          "wiring window: a stroke with Screen - screened, its drain to the connector said");
    // PICKED BY A STROKE, a colour for both.
    canvas->setTool(WiringCanvas::Tool::Select);
    strokeAcross(first.id, second);
    check(canvas->selection().size() == 2, "wiring window: a stroke with Select picks the lines it crosses");
    auto* title = win->findChild<QLabel*>(QStringLiteral("wiringPanelTitle"));
    check(title && title->text().contains(QLatin1String("2 lines picked")), "wiring window: the panel edits both");
    win->setSelectedColor(QStringLiteral("RD-BK"));
    win->setSelectedGauge(QStringLiteral("1.0"));
    check(wireOf(first.id).color == QLatin1String("RD-BK") && wireOf(second).color == QLatin1String("RD-BK") && wireOf(second).gauge == QLatin1String("1.0") &&
              wireOf(third).color != QLatin1String("RD-BK"),
          "wiring window: a colour and a gauge set once - on both lines picked, no other");
    // DELETE; AUTO 1:1.
    canvas->setSelection({third});
    key(Qt::Key_Delete);
    check(wireOf(third).id.isEmpty(), "wiring window: Delete takes the line picked away");
    const int made = win->autoOneToOne();
    int freeLeft = 0;
    for (int pin = 1; pin <= 8; ++pin)
        if (canvas->wiresOn(QStringLiteral("c1"), pin).isEmpty()) ++freeLeft;
    check(made > 0 && (freeLeft == 0 || made + 3 >= 7), "wiring window: Auto 1:1 - the free pins wired across, in turn");
    // SEVERAL CONNECTORS: all three, C3 wired from C1.
    win->showSeveral();
    check(!win->twoMode() && canvas->shown().size() == 3, "wiring window: several connectors - all three shown");
    const QString toC3 = win->connectPins(QStringLiteral("c1"), 8, QStringLiteral("c3"), 2);
    check(!toC3.isEmpty() && !canvas->barCentre(toC3).isNull() && canvas->pinCentre(QStringLiteral("c3"), 2).x() > canvas->barCentre(toC3).x(),
          "wiring window: several - a wire to the third connector, on the right with its side");
    // A PIN WITH TWO WIRES: said.
    win->connectPins(QStringLiteral("c1"), 8, QStringLiteral("c3"), 3);
    auto* problems = win->findChild<QLabel*>(QStringLiteral("wiringProblems"));
    check(problems && problems->text().contains(QLatin1String("two wires")), "wiring window: a pin with two wires - said under the picture");
    // UNDONE: the wizard's own undo, from the window.
    const size_t now = wizard.definition().wires.size();
    if (auto* undo = win->findChild<QPushButton*>(QStringLiteral("wiringUndo"))) undo->click();
    QApplication::processEvents();
    check(wizard.definition().wires.size() == now - 1, "wiring window: Undo - the last wire taken back, the wizard's undo");
    // A LIBRARY CABLE PLACED: two twisted pairs, from C1 to C3 - one cable, its pairs twisted apart.
    {
        PartsLibrary lib;
        LibraryPart& cable = lib.add(PartKind::Cable);
        cable.mfrPn = QStringLiteral("FTP-2x2");
        setCableSpecOn(cable, cableSpecFromText(QStringLiteral("2x2x24AWG")));
        useHarnessBuilderLibraryForTest(lib);
        win->showTwo(QStringLiteral("c1"), QStringLiteral("c3"));
        const size_t had = wizard.definition().wires.size();
        const int placed = win->placeCable(cable.id);
        std::vector<HarnessWire> cores(wizard.definition().wires.begin() + static_cast<long long>(had), wizard.definition().wires.end());
        QSet<QString> twists, bundles;
        for (const HarnessWire& w : cores) {
            twists.insert(w.twistGroup);
            bundles.insert(w.bundle);
        }
        std::printf("  wiring window: cable placed - %d conductor(s), %d twist group(s), %d cable(s)\n", placed, int(twists.size()), int(bundles.size()));
        check(placed == 4 && cores.size() == 4 && twists.size() == 2 && !twists.contains(QString()) && bundles.size() == 1 && !bundles.contains(QString()) &&
                  cores.front().libPart == cable.id,
              "wiring window: Place cable - its four conductors one cable, its two pairs twisted apart");
        useHarnessBuilderLibraryForTest(PartsLibrary{});
    }
    // OPTIMISED PIN STACKS: no line crossing another, twisted wires side by side - only the order the pins are drawn in
    // changes, every wire on its pins as it was; done at once, and to every new connection while a switch is on.
    {
        HarnessDef d;
        for (const auto& [id, side] : {std::pair<const char*, HarnessSide>{"a", HarnessSide::Left}, {"b", HarnessSide::Right}}) {
            HarnessConnector c;
            c.id = QLatin1String(id);
            c.designator = QString::fromLatin1(id).toUpper();
            c.side = side;
            c.pinCount = 6;
            d.connectors.push_back(c);
        }
        int changes = 0;
        WiringDialog opt(d, [&changes] { ++changes; });
        opt.resize(1400, 800);
        opt.show();
        opt.showTwo(QStringLiteral("a"), QStringLiteral("b"));
        QApplication::processEvents();
        auto at = [&](const QString& conn, int pin) {
            for (const HarnessConnector& c : d.connectors)
                if (c.id == conn) {
                    const std::vector<int> seq = harnessPinSequence(c);
                    return static_cast<int>(std::find(seq.begin(), seq.end(), pin) - seq.begin());
                }
            return -1;
        };
        const QString w1 = opt.connectPins(QStringLiteral("a"), 1, QStringLiteral("b"), 3);
        const QString w2 = opt.connectPins(QStringLiteral("a"), 2, QStringLiteral("b"), 1);
        const QString w3 = opt.connectPins(QStringLiteral("a"), 3, QStringLiteral("b"), 2);
        check(at(QStringLiteral("b"), 3) > at(QStringLiteral("b"), 1), "optimise: three wires that cross, before");
        auto* cross = opt.findChild<QToolButton*>(QStringLiteral("wiringOptCross"));
        auto* apply = opt.findChild<QPushButton*>(QStringLiteral("wiringOptApply"));
        check(cross && apply && opt.findChild<QToolButton*>(QStringLiteral("wiringOptShield")) &&
                  opt.findChild<QToolButton*>(QStringLiteral("wiringOptTwist")),
              "optimise: round switches - avoid crossing, by shield, by twist - and Apply");
        if (cross && apply) {
            cross->setChecked(true);
            apply->click();
        }
        QApplication::processEvents();
        std::printf("  optimise: B's pins 3, 1, 2 now at %d, %d, %d\n", at(QStringLiteral("b"), 3), at(QStringLiteral("b"), 1),
                    at(QStringLiteral("b"), 2));
        check(at(QStringLiteral("b"), 3) < at(QStringLiteral("b"), 1) && at(QStringLiteral("b"), 1) < at(QStringLiteral("b"), 2) &&
                  opt.canvas()->shownWires() == QStringList({w1, w2, w3}),
              "optimise: avoid crossing - the second connector's pins and the lines in the first's order");
        bool kept = true;
        for (const HarnessWire& w : d.wires)
            if (w.id == w1) kept = kept && w.fromPin == 1 && w.toPin == 3;
        check(kept, "optimise: every wire still on its pins - only the order they are drawn in changed");
        // A NEW CONNECTION, the switch on: optimised as it is made.
        const QString w4 = opt.connectPins(QStringLiteral("a"), 4, QStringLiteral("b"), 4);
        const QString w5 = opt.connectPins(QStringLiteral("a"), 5, QStringLiteral("b"), 6);
        const QString w6 = opt.connectPins(QStringLiteral("a"), 6, QStringLiteral("b"), 5);
        check(at(QStringLiteral("b"), 6) < at(QStringLiteral("b"), 5), "optimise: a new connection optimised as it is made, the switch on");
        // BY TWIST: two wires twisted together brought onto pins side by side.
        for (HarnessWire& w : d.wires)
            if (w.id == w1 || w.id == w4) w.twistGroup = QStringLiteral("T1");
        opt.setOptimize(false, false, true);
        opt.optimizePinStack();
        check(std::abs(at(QStringLiteral("a"), 1) - at(QStringLiteral("a"), 4)) == 1, "optimise: by twist - the pair on pins side by side");
        opt.setOptimize(false, false, false);
        (void)w5;
        (void)w6;
        // A PIN CAUGHT AND PUT ELSEWHERE IN THE STACK: held 0.2 s, let go on another pin.
        WiringCanvas* cv = opt.canvas();
        const int was3 = at(QStringLiteral("a"), 3);
        const QPointF from = cv->pinCentre(QStringLiteral("a"), 2), to = cv->pinCentre(QStringLiteral("a"), 3);
        auto send = [&](QEvent::Type type, const QPointF& p, Qt::MouseButtons buttons) {
            QMouseEvent e(type, p, cv->mapToGlobal(p), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, buttons, Qt::NoModifier);
            QApplication::sendEvent(cv, &e);
        };
        send(QEvent::MouseButtonPress, from, Qt::LeftButton);
        QElapsedTimer hold;
        hold.start();
        while (hold.elapsed() < 380) QApplication::processEvents();
        check(cv->carrying(), "pin stack: a pin held 0.3 s is caught");
        for (int i = 1; i <= 5; ++i) send(QEvent::MouseMove, from + (to - from) * (i / 5.0), Qt::LeftButton);
        send(QEvent::MouseButtonRelease, to, Qt::NoButton);
        QApplication::processEvents();
        std::printf("  pin stack: pin 2 now at %d (pin 3 was at %d)\n", at(QStringLiteral("a"), 2), was3);
        check(at(QStringLiteral("a"), 2) == was3, "pin stack: let go on another pin, it takes that one's place in the stack");
        bool same = true;
        for (const HarnessWire& w : d.wires)
            if (w.id == w2) same = same && w.fromConn == QLatin1String("a") && w.fromPin == 2;
        check(same, "pin stack: its wire still on it");
    }
    // THE WINDOW'S OTHER WAYS: the optimising under the picture, the groups on the left, two wires on one pin said, a wire
    // joined to another's run, a wire moved with its pins, the stacks put in order, a screen drained at each end and
    // given its part, a cable given to a group checked against it, the problems in the order of their wires.
    {
        HarnessDef d;
        for (const auto& [id, side] : {std::pair<const char*, HarnessSide>{"a", HarnessSide::Left}, {"b", HarnessSide::Right}}) {
            HarnessConnector c;
            c.id = QLatin1String(id);
            c.designator = QString::fromLatin1(id).toUpper();
            c.side = side;
            c.pinCount = 6;
            d.connectors.push_back(c);
        }
        WiringDialog dlg2(d, [] {});
        dlg2.resize(1400, 800);
        dlg2.show();
        dlg2.showTwo(QStringLiteral("a"), QStringLiteral("b"));
        QApplication::processEvents();
        auto* cross = dlg2.findChild<QToolButton*>(QStringLiteral("wiringOptCross"));
        auto* scrollArea = dlg2.findChild<QScrollArea*>();
        check(cross && scrollArea && cross->mapTo(&dlg2, QPoint(0, 0)).y() > scrollArea->mapTo(&dlg2, QPoint(0, 0)).y(),
              "optimise: its switches under the picture, not in the bar above it");
        auto at = [&](const QString& conn, int pin) {
            for (const HarnessConnector& c : d.connectors)
                if (c.id == conn) {
                    const std::vector<int> seq = harnessPinSequence(c);
                    return static_cast<int>(std::find(seq.begin(), seq.end(), pin) - seq.begin());
                }
            return -1;
        };
        const QString w1 = dlg2.connectPins(QStringLiteral("a"), 1, QStringLiteral("b"), 1);
        const QString w2 = dlg2.connectPins(QStringLiteral("a"), 2, QStringLiteral("b"), 2);
        const QString w3 = dlg2.connectPins(QStringLiteral("a"), 3, QStringLiteral("b"), 3);
        // TWO PINS ONTO ONE: made, and said.
        const QString w4 = dlg2.connectPins(QStringLiteral("a"), 4, QStringLiteral("b"), 3);
        check(!w4.isEmpty() && dlg2.problems().join(QLatin1Char('|')).contains(QLatin1String("B pin 3 has two wires")),
              "wiring: two pins wired to one - made, and a warning said");
        for (HarnessWire& w : d.wires)
            if (w.id == w4) w.toPin = 4;
        // THE GROUPS on the left.
        dlg2.canvas()->setSelection({w1, w2});
        dlg2.groupWires(WiringCanvas::Tool::Twist, {w1, w2});
        auto* groupList = dlg2.findChild<QListWidget*>(QStringLiteral("wiringGroups"));
        check(groupList && groupList->count() >= 1 && groupList->item(0)->text().contains(QLatin1String("twisted")),
              "groups: the twisted pair listed on the left");
        // A WIRE JOINED TO ANOTHER'S RUN.
        const QString j = dlg2.connectToWire(QStringLiteral("a"), 5, w3);
        HarnessWire joint;
        for (const HarnessWire& w : d.wires)
            if (w.id == j) joint = w;
        check(!j.isEmpty() && harnessIsJointEndpoint(joint.toConn) && harnessJointEndpointWire(joint.toConn) == w3,
              "joint: a wire from a pin to a joint on another wire's run");
        bool badJoint = false;
        for (const HarnessIssue& is : validateHarness(d))
            badJoint = badJoint || is.wireId == j;
        HarnessDef drawn = d;
        drawn.layout.terminateUnconnected = true;
        const HarnessBuild built = buildHarness(drawn);
        bool jointRun = false, spliced = false, xOnPin = false, stubless = false;
        for (const Entity& e : built.entities) {
            if (e.type == EntityType::Wire && e.meta.value(QStringLiteral("jointTo")).toString() == w3) jointRun = true;
            if (e.type == EntityType::Splice) spliced = true;
            if (e.type == EntityType::Connector && !e.meta.value(QStringLiteral("stublessRows")).toArray().isEmpty()) stubless = true;
        }
        for (const Entity& c : built.entities) {
            if (c.type != EntityType::Connector) continue;
            const ConnectorGeometry g = connectorGeometry(c.a, connectorSpecFromMeta(c.meta));
            for (const Entity& x : built.entities)
                if (x.type == EntityType::NotConnected)
                    for (const Vec2& end : g.pinEnd) xOnPin = xOnPin || distance(end, x.a) < 1e-6;
        }
        check(!badJoint && jointRun && spliced, "joint: drawn from its pin to the wire's run, a splice where it meets it");
        check(xOnPin && !stubless, "unconnected: an unwired pin terminated at its pin's end - off the connector's body, its stub drawn");
        // A WIRE MOVED WITH ITS PINS: to before w1, its pins where w1's are in both stacks.
        dlg2.moveWireWithPins(w3, 0);
        check(dlg2.canvas()->shownWires().front() == w3 && at(QStringLiteral("a"), 3) < at(QStringLiteral("a"), 1) &&
                  at(QStringLiteral("b"), 3) < at(QStringLiteral("b"), 1),
              "move with pins: the wire first in the list, its pins first in both stacks");
        // THE STACKS IN ORDER: the brick picked, its free pins to the top.
        dlg2.canvas()->setSelectedBricks({QStringLiteral("b")});
        dlg2.orderStacks(false);
        check(dlg2.canvas()->wiresOn(QStringLiteral("b"), harnessPinSequence(*harnessConnById(d, QStringLiteral("b"))).front()).isEmpty(),
              "pin stack: the brick picked has its free pins at the top");
        const std::vector<int> aBefore = harnessPinSequence(*harnessConnById(d, QStringLiteral("a")));
        check(aBefore == harnessPinSequence(*harnessConnById(d, QStringLiteral("a"))), "pin stack: a brick not picked left as it was");
        // A SCREEN: drained at its start by a wire to a pin, at its finish onto the connector's own screen; its part.
        const QString s = dlg2.groupWires(WiringCanvas::Tool::Screen, {w1, w2});
        check(dlg2.setScreenDrain(s, 0, QStringLiteral("drain"), QStringLiteral("pin:a:6")) && dlg2.screenDrain(s, 0) == QLatin1String("drain"),
              "screen: drained at its left by its own drain wire to a pin");
        {
            QString target;
            check(dlg2.setScreenDrain(s, 0, QStringLiteral("hook"), QStringLiteral("pin:b:6")) &&
                      dlg2.screenDrain(s, 0, &target) == QLatin1String("hook") && target == QLatin1String("pin:b:6"),
                  "screen: or by a hook-up wire spliced onto it - to any connector's pin, the other end's too");
        }
        check(dlg2.setScreenDrain(s, 1, QStringLiteral("drain"), QStringLiteral("shield:b")) && dlg2.screenDrain(s, 1) == QLatin1String("drain") &&
                  harnessConnById(d, QStringLiteral("b"))->shieldRight,
              "screen: drained at its right onto the connector's own screen - which it is given");
        dlg2.setScreenPart(s, QString(), QStringLiteral("SH-123"));
        check(harnessDefFromJson(harnessDefToJson(d)).shieldPns.value(s) == QLatin1String("SH-123"), "screen: its own P/N, kept");
        const HarnessBuild screened = buildHarness(d);
        bool toShield = false;
        for (const Entity& e : screened.entities)
            if (e.type == EntityType::Wire && e.meta.value(QStringLiteral("drainWire")).toBool() && e.points.size() >= 2) toShield = true;
        check(toShield, "screen: its drains drawn");
        // A CABLE GIVEN TO A GROUP: checked against it.
        {
            PartsLibrary lib;
            LibraryPart& cable = lib.add(PartKind::Cable);
            cable.mfrPn = QStringLiteral("CBL-2x1");
            setCableSpecOn(cable, cableSpecFromText(QStringLiteral("2x1.0mm2")));
            useHarnessBuilderLibraryForTest(lib);
            dlg2.canvas()->setSelection({w3, w4});
            dlg2.setSelectedPart(cable.id);
            for (HarnessWire& w : d.wires)
                if (w.id == w4) w.gauge = QStringLiteral("0.25");
            const QStringList said = dlg2.problems();
            std::printf("  wiring: problems [%s]\n", qPrintable(said.join(QStringLiteral(" | "))));
            check(said.join(QLatin1Char('|')).contains(QLatin1String("CBL-2x1's conductor")), "group P/N: a wire not as the cable's conductor is said");
            useHarnessBuilderLibraryForTest(PartsLibrary{});
        }
        // THE GROUPS PANEL: its title; the shields apart, under a heading of their own - no group.
        {
            auto* title = dlg2.findChild<QLabel*>(QStringLiteral("wiringGroupsTitle"));
            auto* list = dlg2.findChild<QListWidget*>(QStringLiteral("wiringGroups"));
            bool shieldsHead = false;
            for (int i = 0; list && i < list->count(); ++i) shieldsHead = shieldsHead || list->item(i)->text() == QLatin1String("Shields");
            check(title && title->text().contains(QLatin1String("Groups")) && shieldsHead,
                  "groups: the panel titled Groups, the shields under a heading of their own");
            bool split = false;
            for (const QString& p : dlg2.problems()) split = split || p.contains(QLatin1String("shield"), Qt::CaseInsensitive) && p.contains(QLatin1String("between"));
            check(!split, "groups: a shield is no group - its wires on pins apart not said");
        }
        // A SCREEN'S RING CLICKED: its wires picked.
        {
            dlg2.canvas()->setSelection({});
            const QPointF b1 = dlg2.canvas()->barCentre(w1), b2 = dlg2.canvas()->barCentre(w2);
            QStringList picked;
            for (double x = 0; x < dlg2.canvas()->width() && picked.isEmpty(); x += 3)
                picked = dlg2.canvas()->groupMarkAt(QPointF(x, (b1.y() + b2.y()) / 2.0));
            check(picked.contains(w1) && picked.contains(w2), "screen: its ring picks its wires");
        }
        // TERMINATE UNCONNECTED: the drawing's X on every pin nothing is wired to.
        if (auto* term = dlg2.findChild<QToolButton*>(QStringLiteral("wiringTerminate"))) {
            term->setChecked(true);
            check(d.layout.terminateUnconnected, "terminate: switched on, the pins nothing is wired to are marked");
        } else {
            check(false, "terminate: a Terminate unconnected switch");
        }
        // A SCREEN STROKE ACROSS NOTHING makes nothing.
        {
            const size_t had = d.wires.size();
            QStringList groupsBefore;
            for (const HarnessWire& w : d.wires) groupsBefore << w.shieldGroup;
            dlg2.groupWires(WiringCanvas::Tool::Screen, {});
            QStringList groupsAfter;
            for (const HarnessWire& w : d.wires) groupsAfter << w.shieldGroup;
            check(d.wires.size() == had && groupsBefore == groupsAfter, "screen: a stroke that crosses no line places no screen");
        }
        // A JOINT ADDED ON A WIRE (its right-click): the pin its new wire goes to given; the new wire picked by a click.
        {
            const QString added = dlg2.addJointOnWire(w2, QStringLiteral("b:5"));
            HarnessWire nw;
            for (const HarnessWire& w : d.wires)
                if (w.id == added) nw = w;
            check(!added.isEmpty() && nw.fromConn == QLatin1String("b") && nw.fromPin == 5 && harnessJointEndpointWire(nw.toConn) == w2,
                  "joint: added on a wire, its new wire to the pin picked");
            QApplication::processEvents();
            QString found;
            for (const HarnessWire& w : d.wires)
                if (w.id == added)
                    for (double x = 0; x < dlg2.canvas()->width() && found.isEmpty(); x += 2)
                        for (double y = 0; y < dlg2.canvas()->height() && found.isEmpty(); y += 2)
                            if (dlg2.canvas()->jointWireAt(QPointF(x, y)) == added) found = added;
            check(found == added, "joint: its new wire can be clicked - picked, for its part in the panel");
        }
        // ESC out of shielding: back to Select.
        {
            dlg2.canvas()->setTool(WiringCanvas::Tool::Screen);
            QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(dlg2.canvas(), &esc);
            auto* select = dlg2.findChild<QToolButton*>(QStringLiteral("wiringToolSelect"));
            check(dlg2.canvas()->tool() == WiringCanvas::Tool::Select && select && select->isChecked() && !select->autoRaise(),
                  "tools: Esc out of shielding goes back to Select - each tool a framed button");
        }
        // THE WHEEL ZOOMS.
        {
            const double z0 = dlg2.canvas()->zoom();
            QWheelEvent wheel(QPointF(50, 50), dlg2.canvas()->mapToGlobal(QPointF(50, 50)), QPoint(), QPoint(0, 120), Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(dlg2.canvas(), &wheel);
            check(dlg2.canvas()->zoom() > z0 * 1.1, "zoom: the wheel zooms the wiring in");
            dlg2.canvas()->setZoom(1.0);
        }
    }
    // SEVERAL CONNECTORS, AVOID CROSSING: P1 and P4 on the left, P2 and P3 on the right - every wire across in the order it
    // leaves the left column, each right-hand connector's pins in that order; wires between two on one side a short bar.
    {
        HarnessDef d;
        for (const auto& [id, side, pins] : {std::tuple<const char*, HarnessSide, int>{"p1", HarnessSide::Left, 3},
                                             {"p4", HarnessSide::Left, 4},
                                             {"p2", HarnessSide::Right, 8},
                                             {"p3", HarnessSide::Right, 6}}) {
            HarnessConnector c;
            c.id = QLatin1String(id);
            c.designator = QString::fromLatin1(id).toUpper();
            c.side = side;
            c.pinCount = pins;
            d.connectors.push_back(c);
        }
        WiringDialog dlg3(d, [] {});
        dlg3.resize(1400, 900);
        dlg3.show();
        dlg3.showSeveral();
        QApplication::processEvents();
        for (const auto& [a, ap, b, bp] : {std::tuple<const char*, int, const char*, int>{"p1", 1, "p2", 3},
                                           {"p1", 2, "p2", 4},
                                           {"p1", 3, "p2", 2},
                                           {"p4", 3, "p2", 1},
                                           {"p4", 2, "p2", 5},
                                           {"p4", 4, "p2", 6},
                                           {"p2", 8, "p3", 2},
                                           {"p2", 7, "p3", 1}})
            dlg3.connectPins(QLatin1String(a), ap, QLatin1String(b), bp);
        dlg3.setOptimize(true, false, false);
        dlg3.optimizePinStack();
        auto at = [&](const char* conn, int pin) {
            const std::vector<int> seq = harnessPinSequence(*harnessConnById(d, QLatin1String(conn)));
            return static_cast<int>(std::find(seq.begin(), seq.end(), pin) - seq.begin());
        };
        // Down the left column P1 1, 2, 3, then P4 3, 2, 4 as stacked: their P2 pins 3, 4, 2, 1, 5, 6 must go down P2 in that order.
        std::vector<std::pair<int, int>> leftRight;   // (left place, P2 place)
        for (const HarnessWire& w : d.wires) {
            if (w.toConn != QLatin1String("p2")) continue;
            const int lp = (w.fromConn == QLatin1String("p1") ? 0 : 100) + at(w.fromConn == QLatin1String("p1") ? "p1" : "p4", w.fromPin);
            leftRight.push_back({lp, at("p2", w.toPin)});
        }
        std::sort(leftRight.begin(), leftRight.end());
        bool rising = true;
        for (size_t k = 1; k < leftRight.size(); ++k) rising = rising && leftRight[k].second > leftRight[k - 1].second;
        check(leftRight.size() == 6 && rising, "several: avoid crossing - P2's pins in the order of the left-hand pins their wires come from");
        QStringList sameSide;
        for (const HarnessWire& w : d.wires)
            if (w.fromConn == QLatin1String("p2") && w.toConn == QLatin1String("p3")) sameSide << w.id;
        check(!sameSide.isEmpty(), "several: wires between two right-hand connectors");
        // AUTO 1:1 ON THE TWO PICKED: P4 and P3, every pin free on both wired to the next free on the other.
        {
            dlg3.canvas()->setSelectedBricks({QStringLiteral("p4"), QStringLiteral("p3")});
            QSet<QString> before;
            for (const HarnessWire& w : d.wires) before.insert(w.id);
            const int made = dlg3.autoOneToOne();
            int onPair = 0;
            for (const HarnessWire& w : d.wires)
                onPair += !before.contains(w.id) && w.fromConn == QLatin1String("p4") && w.toConn == QLatin1String("p3") ? 1 : 0;
            std::printf("  auto 1:1: %d wire(s) made between the two picked\n", made);
            check(made >= 1 && onPair == made, "auto 1:1: the two connectors picked wired, pin to free pin, as far as either has pins");
            dlg3.canvas()->setSelectedBricks({});
            check(dlg3.autoOneToOne() == 0, "auto 1:1: several shown and none picked - asked to pick two");
        }
        // A CABLE'S P/N given from one of its wires: every wire of the cable takes it, and the cable's colours where it has them.
        {
            PartsLibrary lib;
            LibraryPart& cable = lib.add(PartKind::Cable);
            cable.mfrPn = QStringLiteral("CBL-3");
            setCableSpecOn(cable, cableSpecFromText(QStringLiteral("3x0.5mm2")));
            useHarnessBuilderLibraryForTest(lib);
            QStringList three;
            for (const HarnessWire& w : d.wires)
                if (w.fromConn == QLatin1String("p1") && three.size() < 3) three << w.id;
            harnessCableWires(d, three);
            dlg3.refresh();
            dlg3.canvas()->setSelection({three.front()});
            dlg3.setSelectedPart(cable.id);
            int given = 0;
            for (const HarnessWire& w : d.wires) given += three.contains(w.id) && w.libPart == cable.id ? 1 : 0;
            check(given == 3, "cable P/N: picked from one of its wires, every wire of the cable takes it");
            useHarnessBuilderLibraryForTest(PartsLibrary{});
        }
    }
    if (std::getenv("LOOPWIRE_SHOTS")) {
        win->showTwo(QStringLiteral("c1"), QStringLiteral("c2"));
        canvas->setSelection({first.id, second});
        win->resize(1280, 780);
        QApplication::processEvents();
        win->grab().save(QStringLiteral("wiring_two.png"));
        win->showSeveral();
        QApplication::processEvents();
        win->grab().save(QStringLiteral("wiring_several.png"));
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
    testWizardTablesAndBom();
    testWizardSecondRound();
    testWireGrouping();
    testWizardSimplerWiring();
    testWiringWindow();
    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
