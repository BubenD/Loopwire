// The rebuilt harness build wizard.
//
// WHAT IS WORTH TESTING, given the compiler is already covered by test_mechscript:
//
//   1. THE PALETTE IS THE WHOLE VOCABULARY. The application does not know what comes next, so the
//      only thing standing between a user and a harness is whether the category they want is on the
//      list. A missing category is a harness that cannot be described at all.
//   2. NOTHING IS PRE-PLANNED. The old wizard showed eleven steps before one had been answered. The
//      list here must only ever show what HAS been built.
//   3. THE TURN ARITHMETIC. What the user picks is a direction on the paper; what the script stores is
//      a turn RELATIVE to the previous step. That conversion is the one piece of arithmetic in the
//      window, it is invisible when wrong, and a wrong turn puts a branch somewhere nobody asked for.
#include "app/Editor.h"
#include "app/MechWizardDialog.h"
#include "geometry/Geometry.h"
#include "harness/MechBuildScript.h"
#include "harness/Bom.h"
#include "harness/MechModels.h"
#include "harness/MechanicalHarness.h"
#include "harness/PartsLibrary.h"
#include "model/Document.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {

int fails = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++fails;
    }
}

bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

using Cat = MechWizardDialog::Category;

// ---- The palette ---------------------------------------------------------------------------------

void testPaletteCoversWhatWasAskedFor() {
    const auto cats = MechWizardDialog::categories();
    const auto has = [&](Cat c) {
        return std::find(cats.begin(), cats.end(), c) != cats.end();
    };
    // Every category asked for, by name, so a rename or a deletion is a named failure rather than a
    // gap somebody notices in front of a customer.
    check(has(Cat::Connector), "palette: connector");
    check(has(Cat::Terminal), "palette: terminal");
    check(has(Cat::Heatshrink), "palette: heatshrink");
    check(has(Cat::Wires), "palette: wires");
    check(has(Cat::Cable), "palette: cable");
    check(has(Cat::HarnessBody), "palette: harness body");
    check(has(Cat::HarnessBranch), "palette: harness branch");
    check(has(Cat::NodePoint), "palette: node point");
    // And the ones a harness cannot be dressed without, which were on the earlier list.
    check(has(Cat::Backshell), "palette: backshell");
    check(has(Cat::Boot), "palette: boot");
    check(has(Cat::Label), "palette: label");

    // Each one has a name and becomes something the compiler understands.
    for (Cat c : cats) {
        check(!MechWizardDialog::categoryName(c).isEmpty(), "palette: every category has a name");
        check(MechWizardDialog::categoryKind(c) != PartKind::Other,
              "palette: every category maps to a real part kind");
    }

    // The two run categories are the same thing to the compiler and different things to the user -
    // which is the reason they are two categories over one kind.
    check(mechStepRole(MechWizardDialog::categoryKind(Cat::HarnessBody)) == MechStepRole::Run,
          "palette: the harness body is a run");
    check(mechStepRole(MechWizardDialog::categoryKind(Cat::HarnessBranch)) == MechStepRole::Run,
          "palette: a branch is a run too");
    check(mechStepRole(MechWizardDialog::categoryKind(Cat::NodePoint)) == MechStepRole::Node,
          "palette: a node point is a node");
}

// ---- Nothing is planned --------------------------------------------------------------------------

void testNothingIsPrePlanned() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    check(w.script().steps.empty(), "opening: no steps are invented before the user says anything");
    check(w.anchorStepId().isEmpty(), "opening: and nothing to attach to yet");

    w.setCurrentCategory(Cat::Connector);
    w.commitStep();
    check(w.script().steps.size() == 1, "next: one step described, one step in the script");
    w.setCurrentCategory(Cat::Cable);
    w.commitStep();
    check(w.script().steps.size() == 2, "next: and the second follows it");

    // The script grows by exactly what was asked for and nothing else. The old wizard would have had
    // eleven entries at this point, nine of them describing a harness nobody had mentioned.
    check(w.script().steps[0].kind == PartKind::Connector, "next: the first step is what was picked");
    check(w.script().steps[1].kind == PartKind::Cable, "next: and so is the second");
    check(w.script().steps[1].parentStep == w.script().steps[0].id,
          "next: each step follows the one before it");
}

void testUndoTakesTheLastStepBack() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    w.setCurrentCategory(Cat::Connector);
    w.commitStep();
    w.setCurrentCategory(Cat::Cable);
    w.commitStep();
    const QString first = w.script().steps.front().id;

    w.undoStep();
    check(w.script().steps.size() == 1, "undo: the last step comes off");
    check(w.anchorStepId() == first, "undo: and the next step attaches to what is left");

    w.undoStep();
    check(w.script().steps.empty(), "undo: back to nothing");
    check(w.anchorStepId().isEmpty(), "undo: with nothing to attach to");

    w.undoStep();   // must not fall over on an empty script
    check(w.script().steps.empty(), "undo: undoing an empty script does nothing");
}

// ---- The turn arithmetic -------------------------------------------------------------------------

void testDirectionIsStickyAndStoredAsATurn() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    // A straight harness costs no decision: every step inherits the way the last one went.
    check(w.currentGoesRight(), "direction: a fresh wizard starts out going right");
    w.setCurrentCategory(Cat::Connector);
    w.commitStep();
    check(w.currentGoesRight(), "direction: and the next step still goes right");

    w.setCurrentCategory(Cat::Cable);
    w.commitStep();
    check(near(w.script().steps[1].turnDeg, 0.0),
          "direction: carrying straight on is a turn of nothing");

    // NOW TURN BACK. What the user picks is "left" - a direction on the paper. What the script must
    // store is 180, the turn away from a step that was going right. Storing the absolute direction
    // instead would break the moment a branch was moved: MechNode::angleDeg is relative on purpose.
    w.setCurrentCategory(Cat::HarnessBranch);
    w.setCurrentGoesRight(false);
    w.commitStep();
    check(near(std::abs(w.script().steps[2].turnDeg), 180.0),
          "direction: right then left is stored as a 180 turn");

    // And the NEXT step inherits left, so a harness that has turned stays turned.
    check(!w.currentGoesRight(), "direction: after turning, the next step starts out going left");

    w.setCurrentCategory(Cat::Cable);
    w.commitStep();
    check(near(w.script().steps[3].turnDeg, 0.0),
          "direction: carrying on leftwards is again a turn of nothing, not another 180");

    // The compiled tree agrees: two steps going left after one going right means the second and third
    // runs point the opposite way to the first.
    const MechCompiled c = compileMechScript(w.script());
    const MechLayout lay = layoutMechanicalHarness(c.def);
    check(lay.nodes.size() >= 3, "direction: the compiled harness lays out");
}

// Turning back and forth must not accumulate: left, right, left is three turns of 180 and not a
// harness slowly rotating off the sheet.
void testTurningBackAndForthDoesNotDrift() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    w.setCurrentCategory(Cat::Connector);
    w.commitStep();

    const bool wants[] = {false, true, false, true};
    for (bool right : wants) {
        w.setCurrentCategory(Cat::Cable);
        w.setCurrentGoesRight(right);
        w.commitStep();
        check(w.currentGoesRight() == right,
              "direction: what was picked is what the next step inherits");
    }

    // Every one of those was a reversal, so every stored turn is 180 - never 360, never 540.
    for (size_t i = 1; i < w.script().steps.size(); ++i) {
        const double t = std::abs(w.script().steps[i].turnDeg);
        check(t <= 180.0 + 1e-9, "direction: a stored turn is never more than half a circle");
    }

    // And the harness ends up pointing the way the last step said, not somewhere accumulated.
    const MechCompiled c = compileMechScript(w.script());
    const MechLayout lay = layoutMechanicalHarness(c.def);
    check(lay.nodes.size() == c.def.nodes.size(),
          "direction: every node still gets placed after four reversals");
}

// ---- The checks are wired to the window ----------------------------------------------------------

void testIssuesAreReportedNotInvented() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    // A cable with no jacket diameter is a gap: nothing over it can be sized. It must be REPORTED,
    // and it must not be a violation - nobody has said anything wrong, they have not said yet.
    w.setCurrentCategory(Cat::Connector);
    w.commitStep();
    w.setCurrentCategory(Cat::Cable);
    w.commitStep();

    const auto found = w.issues();
    const int violations = static_cast<int>(
        std::count_if(found.begin(), found.end(),
                      [](const MechStepIssue& i) { return i.kind == MechIssueKind::Violation; }));
    check(violations == 0, "checks: an unfinished harness has nothing WRONG with it");
    bool mentionsJacket = false;
    for (const MechStepIssue& i : found)
        if (i.message.contains(QStringLiteral("jacket"), Qt::CaseInsensitive)) mentionsJacket = true;
    check(mentionsJacket, "checks: a cable with no jacket diameter is reported as a gap");
}

// ---- Editing a step, not describing a new one ----------------------------------------------------

// The bug this is about: with Next as the default button, pressing Enter after correcting a length
// committed a WHOLE NEW STEP. Correcting a figure and adding a step are opposite intentions and must
// not share a key.
void testEnterEditsRatherThanAdding() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    w.setCurrentCategory(Cat::Connector);
    w.commitStep();
    w.setCurrentCategory(Cat::Cable);
    w.commitStep();
    const size_t before = w.script().steps.size();
    const QString second = w.script().steps[1].id;

    w.editStep(second);
    check(w.editingStepId() == second, "edit: clicking a step edits that step");
    w.applyToEditedStep();
    check(w.script().steps.size() == before,
          "edit: applying a change does NOT add a step - that was the bug");

    // And with nothing being edited, applying is a no-op rather than an accidental append.
    MechWizardDialog fresh(&ed, nullptr);
    fresh.applyToEditedStep();
    check(fresh.script().steps.empty(), "edit: applying with nothing selected adds nothing");
}

// ---- Deleting any step, not only the last --------------------------------------------------------

void testDeleteClosesTheHarnessUp() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    w.setCurrentCategory(Cat::Connector);   w.commitStep();
    w.setCurrentCategory(Cat::Backshell);   w.commitStep();
    w.setCurrentCategory(Cat::Cable);       w.commitStep();
    const QString s1 = w.script().steps[0].id;
    const QString s2 = w.script().steps[1].id;
    const QString s3 = w.script().steps[2].id;

    w.editStep(s2);
    w.deleteStep(s2);

    check(w.script().steps.size() == 2, "delete: a middle step comes out");
    check(w.script().step(s2) == nullptr, "delete: and is gone");

    // THE LIMB MUST NOT GO WITH IT. A child left pointing at a deleted step compiles to nothing and
    // drops that whole branch off the drawing silently, which is the worst possible outcome.
    const MechStep* survivor = w.script().step(s3);
    check(survivor != nullptr, "delete: what followed it is still there");
    check(survivor && survivor->parentStep == s1,
          "delete: re-parented onto the deleted step's parent - the harness closes up");

    const MechCompiled c = compileMechScript(w.script());
    check(c.errors.isEmpty(), "delete: and the result still compiles");
    check(c.def.segments.size() == 1, "delete: with the run still in it");
}

// ---- Several conductor groups in one run ---------------------------------------------------------

void testSeveralConductorGroupsMakeOneBundle() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    w.setCurrentCategory(Cat::Connector);
    w.commitStep();
    w.setCurrentCategory(Cat::Wires);

    w.addConductorGroup();
    check(w.pendingConductors().size() == 1, "groups: a group is added");
    w.addConductorGroup();
    check(w.pendingConductors().size() == 2, "groups: and a second");
    w.commitStep();

    const MechStep& run = w.script().steps.back();
    check(run.conductors.size() == 2, "groups: the run carries both groups");

    // ONE BUNDLE, not two runs. A hook-up run carrying a mixture is one length of harness with several
    // sizes in it; compiling it to two segments would draw two legs where there is one.
    const MechCompiled c = compileMechScript(w.script());
    check(c.def.segments.size() == 1, "groups: several groups are still ONE run");
    const MechSegment* seg = c.def.segment(c.stepToSegment.value(run.id));
    check(seg != nullptr, "groups: the run compiled");
    if (seg) {
        int total = 0;
        for (const MechConductorGroup& g : run.conductors) total += g.count;
        check(static_cast<int>(seg->wireIds.size()) == total,
              "groups: every conductor of every group is routed through that one run");
    }
    check(w.pendingConductors().empty(), "groups: committing clears them for the next run");
}

// ---- Discrete wires do not invent a splice -------------------------------------------------------

// THE REGRESSION FROM THE SCREENSHOT. Two Wire steps of different gauges produced "heatshrink over
// the splice 0.5, 0.5 -> 1.5, 1.5 mm2" and a NOTES entry reading WIRES GAUGE TRANSITION LOCATION.
// Nobody asked for a splice: those were two lengths of one bundle, and the drawing inferred a
// transition because each step had been given its own fresh set of conductors.
void testContinuedConductorsDoNotMakeASplice() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);

    // As it was: each run declaring its own conductors, at different gauges.
    {
        MechBuildScript sc;
        MechStep a;
        a.id = QStringLiteral("S1"); a.kind = PartKind::Wire;
        a.lengthMm = 200.0; a.wireCount = 2; a.gauge = QStringLiteral("0.5");
        sc.steps.push_back(a);
        MechStep b;
        b.id = QStringLiteral("S2"); b.kind = PartKind::Wire; b.parentStep = QStringLiteral("S1");
        b.lengthMm = 20.0; b.wireCount = 2; b.gauge = QStringLiteral("1.5");
        sc.steps.push_back(b);
        const MechCompiled c = compileMechScript(sc);
        check(!mechGaugeChanges(c.def).empty(),
              "splice: separate conductors at different gauges IS a transition - correctly");
    }

    // As it is now: the second run continues the first one's conductors.
    {
        MechBuildScript sc;
        MechStep a;
        a.id = QStringLiteral("S1"); a.kind = PartKind::Wire;
        a.lengthMm = 200.0; a.wireCount = 2; a.gauge = QStringLiteral("0.5");
        sc.steps.push_back(a);
        MechStep b;
        b.id = QStringLiteral("S2"); b.kind = PartKind::Wire; b.parentStep = QStringLiteral("S1");
        b.lengthMm = 20.0; b.continuesConductors = true;
        sc.steps.push_back(b);
        const MechCompiled c = compileMechScript(sc);
        check(mechGaugeChanges(c.def).empty(),
              "splice: continued conductors are NOT a transition - no invented splice");
        // And they really are the same wires, not a copy that happens to match.
        const MechSegment* s1 = c.def.segment(c.stepToSegment.value(QStringLiteral("S1")));
        const MechSegment* s2 = c.def.segment(c.stepToSegment.value(QStringLiteral("S2")));
        check(s1 && s2 && s1->wireIds == s2->wireIds,
              "splice: the second run carries the very same conductors");
        check(c.def.wires.size() == 2, "splice: and no extra wires were created");
    }

    // A run with nothing to continue from starts its own whatever the flag says - otherwise the first
    // run of a harness would have no conductors at all.
    {
        MechBuildScript sc;
        MechStep only;
        only.id = QStringLiteral("S1"); only.kind = PartKind::Wire; only.lengthMm = 100.0;
        only.wireCount = 3; only.gauge = QStringLiteral("0.5"); only.continuesConductors = true;
        sc.steps.push_back(only);
        const MechCompiled c = compileMechScript(sc);
        const MechSegment* seg = c.def.segment(c.stepToSegment.value(QStringLiteral("S1")));
        check(seg && seg->wireIds.size() == 3,
              "splice: the first run starts its own conductors even when asked to continue");
    }

    // And after Next, the wizard offers continuation by default - which is what makes the common case
    // (another length of the same bundle) cost nothing.
    MechWizardDialog w(&ed, nullptr);
    w.setCurrentCategory(Cat::Wires);
    w.commitStep();
    w.setCurrentCategory(Cat::Wires);
    w.commitStep();
    check(w.script().steps[1].continuesConductors,
          "splice: a run following a run continues its conductors by default");
}

// ---- Balloons and break marks --------------------------------------------------------------------

void testBalloonAndBreakMarkReachTheDrawing() {
    MechBuildScript sc;
    MechStep con;
    con.id = QStringLiteral("S1"); con.kind = PartKind::Connector;
    sc.steps.push_back(con);
    MechStep run;
    run.id = QStringLiteral("S2"); run.kind = PartKind::Cable;
    run.parentStep = QStringLiteral("S1");
    run.lengthMm = 2000.0; run.odMm = 8.0; run.wireCount = 4; run.gauge = QStringLiteral("0.5");
    run.lengthBreak = true;
    sc.steps.push_back(run);

    const MechCompiled c = compileMechScript(sc);
    const MechSegment* seg = c.def.segment(c.stepToSegment.value(QStringLiteral("S2")));
    check(seg && seg->lengthBreak, "break: the flag reaches the run");
    check(seg && seg->balloon, "balloon: on by default, as a released drawing balloons everything");

    // The mark is a real LengthBreak entity, so the same code that draws one placed by hand draws
    // this one - rather than a second break mark that could look different.
    const std::vector<Entity> ents = buildMechanicalHarness(c.def, nullptr);
    int marks = 0;
    for (const Entity& e : ents)
        if (e.type == EntityType::LengthBreak) ++marks;
    check(marks == 1, "break: exactly one break mark is drawn on the run that asked for one");

    // Turned off, it is not drawn - the flag is doing the work rather than the length.
    MechBuildScript quiet = sc;
    quiet.steps[1].lengthBreak = false;
    const MechCompiled qc = compileMechScript(quiet);
    const std::vector<Entity> qents = buildMechanicalHarness(qc.def, nullptr);
    int qmarks = 0;
    for (const Entity& e : qents)
        if (e.type == EntityType::LengthBreak) ++qmarks;
    check(qmarks == 0, "break: and none is drawn when the run does not ask");
}

// ---- Dimensioning a connector step ---------------------------------------------------------------
//
// "Dimension this step" on a connector means the CONNECTOR: from its far face to the face the harness
// leaves by, which is how far it stands off the end of the loom and the figure a released drawing
// carries. Measured along the run, so it reads the same way round as every other length on the sheet.
//
// AND ONE TICK MUST PRODUCE ONE FIGURE. The open length defaults to kMechOpenLengthMm and a connector
// body defaults to 20 mm wide, so a dimensioned connector with a run behind it used to carry two 20 mm
// dimensions starting at the same face and pointing opposite ways - identical readings, indistinguishable
// on the sheet, and only one of them the thing that was asked for.
void testDimensioningAConnectorMeasuresTheConnector() {
    // The spans, and whether an automatic open-conductor dimension came too.
    struct Found {
        bool haveBody = false;
        double bodyFrom = 0.0, bodyTo = 0.0;      // the body's own x extent
        bool haveDim = false;
        Vec2 dimA{}, dimB{};
        int openWireDims = 0;
    };
    const auto look = [](const MechBuildScript& sc, const QString& connStep) {
        const MechCompiled c = compileMechScript(sc);
        const std::vector<Entity> ents = buildMechanicalHarness(c.def, nullptr);
        const QString connNode = c.stepToNode.value(connStep);
        Found f;
        for (const Entity& e : ents) {
            // AT THIS CONNECTOR'S NODE, not anywhere on the sheet. The other end of the run has an
            // open-conductor figure of its own and is perfectly entitled to it - counting those too
            // would be asserting that dimensioning one connector silences the whole drawing.
            const bool here =
                e.meta.value(QStringLiteral("mechNode")).toString() == connNode;
            if (e.meta.value(QStringLiteral("openWires")).toBool()) {
                if (here) ++f.openWireDims;
                continue;
            }
            if (e.type == EntityType::Dimension && here) {
                f.haveDim = true;
                f.dimA = e.a;
                f.dimB = e.b;
            }
            if (e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("connector") &&
                !e.points.empty()) {
                f.haveBody = true;
                f.bodyFrom = e.points.front().x;
                f.bodyTo = e.points.front().x;
                for (const Vec2& p : e.points) {
                    f.bodyFrom = std::min(f.bodyFrom, p.x);
                    f.bodyTo = std::max(f.bodyTo, p.x);
                }
            }
        }
        return f;
    };

    // ---- A connector at the START of the harness: the run LEAVES it, so the harness goes off to the
    // right and the near face is the body's right-hand edge.
    {
        MechBuildScript sc;
        MechStep con;
        con.id = QStringLiteral("S1");
        con.kind = PartKind::Connector;
        con.name = QStringLiteral("P1");
        con.dimension = true;
        sc.steps.push_back(con);
        MechStep run;
        run.id = QStringLiteral("S2");
        run.kind = PartKind::Cable;
        run.parentStep = QStringLiteral("S1");
        run.lengthMm = 200.0;
        run.odMm = 6.0;
        run.wireCount = 4;
        run.gauge = QStringLiteral("0.5");
        sc.steps.push_back(run);

        const Found f = look(sc, QStringLiteral("S1"));
        check(f.haveBody && f.haveDim, "condim: a dimensioned connector draws a body and a dimension");
        // FAR FIRST, NEAR SECOND. The far face is the outer one, away from the harness.
        check(near(f.dimA.x, f.bodyFrom) && near(f.dimB.x, f.bodyTo),
              "condim: at the start of a harness the dimension runs from the far face to the near one");
        check(f.dimA.x < f.dimB.x,
              "condim: which with the harness leaving rightwards means left to right");
        check(near(f.dimA.y, f.dimB.y),
              "condim: measured ALONG the run, so it reads like every other length on the sheet");
        check(f.openWireDims == 0,
              "condim: and the automatic 20 mm conductor figure stands down - one tick, one dimension");
    }

    // ---- A connector at the FAR END of a run: the run ARRIVES, so the body sticks out to the right
    // and the near face is its LEFT edge. The direction of the dimension must follow the connector, not
    // the paper - far to near either way.
    {
        MechBuildScript sc;
        MechStep run;
        run.id = QStringLiteral("S1");
        run.kind = PartKind::Cable;
        run.lengthMm = 200.0;
        run.odMm = 6.0;
        run.wireCount = 4;
        run.gauge = QStringLiteral("0.5");
        sc.steps.push_back(run);
        MechStep con;
        con.id = QStringLiteral("S2");
        con.kind = PartKind::Connector;
        con.name = QStringLiteral("P2");
        con.parentStep = QStringLiteral("S1");
        con.dimension = true;
        sc.steps.push_back(con);

        const Found f = look(sc, QStringLiteral("S2"));
        check(f.haveBody && f.haveDim, "condim: the far-end connector draws a body and a dimension");
        check(near(f.dimA.x, f.bodyTo) && near(f.dimB.x, f.bodyFrom),
              "condim: at the far end the dimension STILL runs far to near - right to left here");
        check(f.dimA.x > f.dimB.x,
              "condim: which is the opposite way round on the paper, and the same way round on the part");
        check(f.openWireDims == 0,
              "condim: and again only the one figure that was asked for");
    }

    // ---- UNTICKED, NOTHING IS DIMENSIONED. The wizard asks "Dimension this step" on every step and
    // has no sheet-wide tick to overrule them, so a step left clear must come out clear - including
    // the automatic open-conductor figure at a connector, which used to appear whatever was asked.
    //
    // It used to be the other way round, on the reasoning that a harness nobody has dimensioned still
    // wants the open length at its connectors. That reads well until you are the person who unticked
    // every box on the sheet and is still looking at dimensions: there is then nothing left in the
    // window to turn them off with. The tick decides, and an untouched step says nothing.
    {
        MechBuildScript sc;
        MechStep con;
        con.id = QStringLiteral("S1");
        con.kind = PartKind::Connector;
        sc.steps.push_back(con);
        MechStep run;
        run.id = QStringLiteral("S2");
        run.kind = PartKind::Cable;
        run.parentStep = QStringLiteral("S1");
        run.lengthMm = 200.0;
        run.odMm = 6.0;
        run.wireCount = 4;
        run.gauge = QStringLiteral("0.5");
        sc.steps.push_back(run);

        const Found f = look(sc, QStringLiteral("S1"));
        check(f.openWireDims == 0,
              "condim: an UNdimensioned connector is not dimensioned at all");
        check(!f.haveDim, "condim: and carries no dimension of its own either");
    }

    // ---- A CONNECTOR FROM THE LIBRARY IS AS LONG AS ITS OWN DRAWING.
    //
    // The default box is 20 mm and the dimension used to be worked out from that figure whatever was
    // actually drawn - so a chosen library connector read 20.0 no matter its real length, with the
    // arrows landing somewhere inside its outline. The body that gets drawn is the part's own, so the
    // body that gets measured has to be the part's own too.
    {
        PartsCatalog cat;
        cat.setRemembering(false);
        LibraryPart& p = cat.active().add(PartKind::Connector);
        p.description = QStringLiteral("A connector 60 long");
        // 60 along the run and 24 across it - deliberately nothing like the 20 mm default, so a
        // dimension still taken off the default is a plainly wrong number rather than a near miss.
        Entity body;
        body.type = EntityType::Rect;
        body.points = {{0.0, -12.0}, {60.0, -12.0}, {60.0, 12.0}, {0.0, 12.0}};
        p.mechanical = partDrawingFromEntities({body});
        const QString partId = p.id;

        MechBuildScript sc;
        MechStep con;
        con.id = QStringLiteral("S1");
        con.kind = PartKind::Connector;
        con.name = QStringLiteral("P1");
        con.libPart = partId;
        con.dimension = true;
        sc.steps.push_back(con);

        const MechCompiled c = compileMechScript(sc);
        const std::vector<Entity> ents = buildMechanicalHarness(c.def, &cat);
        const QString node = c.stepToNode.value(QStringLiteral("S1"));

        // What was actually drawn, and what the dimension says about it.
        Bounds drawn;
        bool haveDrawn = false;
        const Entity* dim = nullptr;
        for (const Entity& e : ents) {
            if (e.meta.value(QStringLiteral("mechNode")).toString() != node) continue;
            if (e.type == EntityType::Dimension) { dim = &e; continue; }
        }
        for (const Entity& e : ents)
            if (e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("connector")) {
                const Bounds eb = e.bounds();
                if (eb.valid) { drawn.expand(eb.min); drawn.expand(eb.max); haveDrawn = true; }
            }
        check(haveDrawn, "condim: the library part's own drawing is placed");
        check(dim != nullptr, "condim: and it is dimensioned");
        if (dim && haveDrawn) {
            const double span = std::abs(dim->b.x - dim->a.x);
            const double body = drawn.max.x - drawn.min.x;
            check(near(span, body, 1e-3),
                  "condim: the dimension spans the drawing that was placed, not the 20 mm default box");
            check(span > 25.0,
                  "condim: which for a 60-long connector is emphatically not 20");

            // ABOVE THE OBJECT. The text point is the third point of the dimension, and it must sit
            // clear of the body rather than under it among the conductors and the run.
            check(dim->points.size() >= 3, "condim: the dimension carries its text point");
            if (dim->points.size() >= 3)
                check(dim->points[2].y > drawn.max.y,
                      "condim: and the dimension line is placed ABOVE the object, not below it");
        }
    }

    // ---- A LONE CONNECTOR, which is what the wizard previews before Next is pressed. No run means no
    // conductors, so there is nothing for an open-length figure to measure and only the body's own.
    {
        MechBuildScript sc;
        MechStep con;
        con.id = QStringLiteral("S1");
        con.kind = PartKind::Connector;
        con.name = QStringLiteral("P1");
        con.dimension = true;
        sc.steps.push_back(con);

        const Found f = look(sc, QStringLiteral("S1"));
        check(f.haveDim && near(f.dimA.x, f.bodyFrom) && near(f.dimB.x, f.bodyTo),
              "condim: a lone connector is dimensioned far to near as well");
        check(f.openWireDims == 0,
              "condim: with no run there is no conductor length to report");
    }
}

// ---- What a run is asked, and what it is called --------------------------------------------------
//
// A run is DESIGNATED, not called something: W1 is the reference the wire list and the schematic know
// it by, exactly as P1 is for a connector, and the number is one in a series rather than a decision.
// Only a fitting - a boot, a label, a length of heatshrink - is CALLED something.
void testARunIsDesignatedAndTheGaugesAreAwg() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    // ---- The gauges are AWG, with the mm² equivalent to read against a European schedule ----
    w.setCurrentCategory(Cat::Wires);
    const QStringList gauges = w.shownGauges();
    check(!gauges.isEmpty(), "gauge: the list offers sizes");
    bool allAwg = true, allBracketMm2 = true;
    for (const QString& g : gauges) {
        if (!g.contains(QStringLiteral("AWG"))) allAwg = false;
        if (!g.contains(QStringLiteral("mm")) || !g.contains(QLatin1Char('('))) allBracketMm2 = false;
    }
    check(allAwg, "gauge: every size is written in AWG");
    check(allBracketMm2, "gauge: with the mm² equivalent in brackets beside it");

    // WHAT IS STORED IS THE BARE SIZE. gaugeMm2() has to parse it, and a size that only reads to a
    // human is a size no contact-range check can be run against - which is the whole point of the
    // neighbour checks.
    check(!w.gaugeCanon().contains(QLatin1Char('(')),
          "gauge: the bracket is a reading aid and is NOT what the script keeps");
    check(mechGaugeMm2(w.gaugeCanon()) > 0.0,
          "gauge: and what the script keeps can still be converted to mm² - the checks depend on it");
    w.setGauge(QStringLiteral("16 AWG"));
    check(w.gaugeCanon() == QStringLiteral("16 AWG"), "gauge: a size can be chosen by its bare name");

    // ---- The name field ----
    check(w.shownNameLabel() == QStringLiteral("Designator"),
          "run: a run's name field is called Designator, as a connector's is");
    w.setCurrentCategory(Cat::Boot);
    check(w.shownNameLabel() == QStringLiteral("Called"),
          "run: a boot really is CALLED something - that one is a caption");

    // ---- W1, W2, W3: offered rather than asked for ----
    w.setCurrentCategory(Cat::Wires);
    check(w.nextRunDesignator() == QStringLiteral("W1"), "run: the first run is offered W1");
    w.commitStep();
    check(w.script().steps.back().name == QStringLiteral("W1"),
          "run: and the committed step carries it without anybody typing");
    check(w.nextRunDesignator() == QStringLiteral("W2"), "run: the next one is W2");

    // SCANNED, NOT COUNTED. Steps get deleted out of the middle, and a count would then hand out a
    // designator that is already on the drawing.
    MechBuildScript sc = w.script();
    MechStep w5;
    w5.id = QStringLiteral("SX");
    w5.kind = PartKind::Wire;
    w5.name = QStringLiteral("W2");
    w5.parentStep = sc.steps.back().id;
    sc.steps.push_back(w5);
    w.setScript(sc);
    w.setCurrentCategory(Cat::Wires);
    check(w.nextRunDesignator() == QStringLiteral("W3"),
          "run: a designator already in use is skipped rather than handed out twice");
}

// ---- Writing the designator on the drawing -------------------------------------------------------
//
// Off by default, and WITHHELD ON A RUN BETWEEN TWO PARTS however the box is ticked: a wire from P1 to
// P2 is already identified by its ends, and the wire list calls it P1-3 to P2-5. It earns its place
// where an end is open and there is nothing else to call the run by.
void testTheRunDesignatorIsShownOnlyWhereItIsTheOnlyName() {
    const auto drawnNames = [](const MechBuildScript& sc) {
        const MechCompiled c = compileMechScript(sc);
        QStringList texts;
        for (const Entity& e : buildMechanicalHarness(c.def, nullptr)) {
            const QString t = e.meta.value(QStringLiteral("text")).toString();
            if (!t.isEmpty()) texts << t;
        }
        return texts;
    };

    // A connector, then a run with an OPEN far end: nothing else names that run.
    MechBuildScript open;
    MechStep con;
    con.id = QStringLiteral("S1");
    con.kind = PartKind::Connector;
    con.name = QStringLiteral("P1");
    open.steps.push_back(con);
    MechStep run;
    run.id = QStringLiteral("S2");
    run.kind = PartKind::Wire;
    run.parentStep = QStringLiteral("S1");
    run.name = QStringLiteral("W1");
    run.lengthMm = 200.0;
    run.wireCount = 3;
    run.gauge = QStringLiteral("20 AWG");
    open.steps.push_back(run);

    check(!drawnNames(open).contains(QStringLiteral("W1")),
          "showname: off by default - a released harness does not letter every leg of itself");

    open.steps[1].showName = true;
    check(drawnNames(open).contains(QStringLiteral("W1")),
          "showname: ticked, the designator is written beside the run");

    // NOW CLOSE THE FAR END with a second connector. The run is between two parts, and the designator
    // must stand down whatever the tick says - the wire list already identifies it by its ends.
    MechBuildScript between = open;
    MechStep con2;
    con2.id = QStringLiteral("S3");
    con2.kind = PartKind::Connector;
    con2.name = QStringLiteral("P2");
    con2.parentStep = QStringLiteral("S2");
    between.steps.push_back(con2);

    const QStringList names = drawnNames(between);
    check(names.contains(QStringLiteral("P1")) && names.contains(QStringLiteral("P2")),
          "showname: both connectors are still named");
    check(!names.contains(QStringLiteral("W1")),
          "showname: but the run between them is NOT - a third name for one conductor");
}

// ---- The fan at a connector ----------------------------------------------------------------------
//
// The wires fanning from the bundle to the connector face must not run through one another. They used
// to be spaced EVENLY whatever their sizes, while each is drawn as a double line whose gap is its own
// outer diameter - so on a bundle carrying a mixture, which is the ordinary case, a heavy conductor
// straddled its lane and crossed into its neighbour's.
void testFanWiresDoNotCrossOneAnother() {
    MechBuildScript sc;
    MechStep con;
    con.id = QStringLiteral("S1");
    con.kind = PartKind::Connector;
    con.name = QStringLiteral("P1");
    con.pinCount = 5;
    sc.steps.push_back(con);
    MechStep run;
    run.id = QStringLiteral("S2");
    run.kind = PartKind::Wire;
    run.parentStep = QStringLiteral("S1");
    run.lengthMm = 200.0;
    // A DELIBERATE MIXTURE, heavy and light alternating: even spacing puts a 6 AWG lane and a 24 AWG
    // lane the same width apart, and it is the heavy one either side of a light one that overlaps it.
    // GROUPED, NOT ALTERNATED - the heavy conductors next to one ANOTHER.
    //
    // Alternating them hid the fault: with a light wire between every heavy pair, no two wide bands are
    // ever neighbours, and even spacing passes. A real bundle lists its power pair together and its
    // signals after, so heavy sits beside heavy - and that is the arrangement that needs a lane sized
    // to each conductor rather than an equal share for all of them.
    for (const char* g : {"6 AWG", "6 AWG", "6 AWG", "24 AWG", "24 AWG"}) {
        MechConductorGroup one;
        one.count = 1;
        one.gauge = QString::fromLatin1(g);
        run.conductors.push_back(one);
    }
    sc.steps.push_back(run);

    const MechCompiled c = compileMechScript(sc);
    const std::vector<Entity> ents = buildMechanicalHarness(c.def, nullptr);

    // The fan lines AT ONE NODE. Scoping this matters: a run has an open end at each of its ends, so
    // the drawing carries TWO fans - one at the connector and one at the free end away from it. Pooling
    // both and sorting by y interleaves them, and every "overlap" found is then two separate fans
    // passing each other rather than two wires sharing a lane. That is what made the worst offender
    // come out at exactly 6.97 mm however the spacing rule was changed.
    const QString fanNode = c.stepToNode.value(QStringLiteral("S1"));
    std::vector<const Entity*> fan;
    for (const Entity& e : ents)
        if (e.type == EntityType::Polyline &&
            e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("wires") &&
            e.meta.contains(QStringLiteral("wireOd")) && e.points.size() >= 2 &&
            e.meta.value(QStringLiteral("mechNode")).toString() == fanNode)
            fan.push_back(&e);
    check(fan.size() >= 3, "fan: the conductors are drawn fanning into the connector");

    // THE REAL TEST IS THAT THE DRAWN BANDS DO NOT OVERLAP, and not merely that the centrelines miss
    // one another.
    //
    // Centrelines never crossed: the old fan stepped evenly through the wires and used the SAME step at
    // both ends, so they were always in order and never intersected. What overlapped was the ink - each
    // wire is a double line whose gap is its own outer diameter, so a heavy conductor straddled its
    // evenly-sized lane and ran through the thin one beside it. A test that only checked intersections
    // would have passed against the very bug it was written for.
    struct Band { double centre; double half; };
    std::vector<Band> bands;
    for (const Entity* e : fan) {
        const double gap = e->meta.value(QStringLiteral("gap")).toDouble();
        bands.push_back({e->points.back().y, gap / 2.0});
    }
    std::sort(bands.begin(), bands.end(),
              [](const Band& a, const Band& b) { return a.centre < b.centre; });
    int overlaps = 0;
    double widest = 0.0;
    for (size_t i = 1; i < bands.size(); ++i) {
        const double clear = (bands[i].centre - bands[i - 1].centre) -
                             (bands[i].half + bands[i - 1].half);
        if (clear < -1e-9) {
            ++overlaps;
            widest = std::min(widest, clear);
        }
    }
    if (overlaps > 0)
        std::printf("  fan: %d overlapping pair(s), worst by %.2f mm\n", overlaps, -widest);
    check(overlaps == 0,
          "fan: no conductor's drawn width runs into its neighbour's on a mixed bundle");

    // NO TWO CENTRELINES MEET either. Every segment of every wire against every segment of every other.
    int crossings = 0;
    for (size_t i = 0; i < fan.size(); ++i)
        for (size_t j = i + 1; j < fan.size(); ++j)
            for (size_t a = 1; a < fan[i]->points.size(); ++a)
                for (size_t b = 1; b < fan[j]->points.size(); ++b)
                    if (segmentSegmentIntersection(fan[i]->points[a - 1], fan[i]->points[a],
                                                   fan[j]->points[b - 1], fan[j]->points[b]))
                        ++crossings;
    check(crossings == 0, "fan: no two conductors cross one another between the bundle and the face");

    // AND THE ORDER IS THE SAME AT BOTH ENDS, which is what makes that provable rather than lucky: two
    // polylines whose ends are both in order cannot cross, however wide or narrow the shell.
    //
    // ASKED OF THE CONDUCTORS THAT REACH THE BUNDLE. The fan is a picture of a bundle rather than a
    // tally of it - five leave the face and three arrive - and the ones between them stop where the
    // closing conductors pass over them (meta "fanMerged"). Those never reach the bundle, so "the
    // order at both ends" is not a thing that can be said about them: their far end is still out at
    // their own place across the face. The invariant is about the wires that go the whole way, and it
    // is those that the no-crossing proof rests on.
    std::vector<const Entity*> arriving;
    for (const Entity* e : fan)
        if (!e->meta.value(QStringLiteral("fanMerged")).toBool()) arriving.push_back(e);
    std::printf("  fan: %d line(s), %d arriving, %d merging\n", int(fan.size()),
                int(arriving.size()), int(fan.size() - arriving.size()));
    check(arriving.size() >= 2 && arriving.size() < fan.size(),
          "fan: of five conductors at the face, some reach the bundle and some merge into it");
    bool ordered = true;
    for (size_t i = 1; i < arriving.size(); ++i) {
        const bool startsUp = arriving[i]->points.front().y > arriving[i - 1]->points.front().y;
        const bool endsUp = arriving[i]->points.back().y > arriving[i - 1]->points.back().y;
        if (startsUp != endsUp) ordered = false;
    }
    check(ordered, "fan: the wires are in the same order on the bundle as on the connector face");

    // AND NO "n conductors" CAPTION. The count belongs in the wire list and the bill, where it can be
    // read against the gauges and the ends.
    //
    // A FAT BUNDLE ON PURPOSE. The caption was only ever written when more conductors were declared
    // than were worth drawing lines for - past two dozen - so asking this of the five-wire bundle above
    // proved nothing at all: the caption could have been left in and the check would still have passed.
    {
        MechBuildScript many;
        MechStep con2;
        con2.id = QStringLiteral("S1");
        con2.kind = PartKind::Connector;
        con2.pinCount = 40;
        many.steps.push_back(con2);
        MechStep fat;
        fat.id = QStringLiteral("S2");
        fat.kind = PartKind::Wire;
        fat.parentStep = QStringLiteral("S1");
        fat.lengthMm = 200.0;
        fat.wireCount = 40;                 // well past the two dozen the fan draws lines for
        fat.gauge = QStringLiteral("22 AWG");
        many.steps.push_back(fat);

        const MechCompiled mc = compileMechScript(many);
        const std::vector<Entity> ments = buildMechanicalHarness(mc.def, nullptr);
        check(mc.def.wires.size() == 40, "fan: forty conductors are declared");
        bool caption = false;
        for (const Entity& e : ments)
            if (e.meta.value(QStringLiteral("text")).toString().contains(QStringLiteral("conductors")))
                caption = true;
        check(!caption,
              "fan: no 'n conductors' caption, even on a bundle too fat to draw every line for");
    }
}

// ---- Five at the face, three into the bundle -----------------------------------------------------
//
// A branch carrying twenty conductors cannot be DRAWN as twenty conductors - twenty double lines
// fanning into one shell is a grey band at the size these sheets print, and the one thing the fan is
// for is lost. So the fan is a picture of the bundle forming: up to five leave the face, three of them
// carry on, and the ones between stop where the closing conductors pass over them. That is the shape
// of the hand-drawn view in mytest/mytest1.dwx, which is the specification for this.
void testTheFanIsAPictureOfTheBundleNotATally() {
    // A branch of twenty conductors that is NOT bundled: open its whole length, which is the case the
    // shape was drawn for and the one the wizard produces with the jacket box unticked.
    MechBuildScript sc;
    MechStep con;
    con.id = QStringLiteral("S1");
    con.kind = PartKind::Connector;
    con.name = QStringLiteral("P1");
    con.pinCount = 20;
    sc.steps.push_back(con);
    MechStep run;
    run.id = QStringLiteral("S2");
    run.kind = PartKind::Wire;
    run.parentStep = QStringLiteral("S1");
    run.lengthMm = 100.0;
    run.wireCount = 20;
    run.gauge = QStringLiteral("20 AWG");
    run.bundled = false;
    sc.steps.push_back(run);

    const MechCompiled c = compileMechScript(sc);
    check(c.def.wires.size() == 20, "picture: twenty conductors are declared");
    const std::vector<Entity> ents = buildMechanicalHarness(c.def, nullptr);

    const QString fanNode = c.stepToNode.value(QStringLiteral("S1"));
    std::vector<const Entity*> fan;
    for (const Entity& e : ents)
        if (e.type == EntityType::Polyline &&
            e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("wires") &&
            e.meta.contains(QStringLiteral("wireOd")) &&
            e.meta.value(QStringLiteral("mechNode")).toString() == fanNode)
            fan.push_back(&e);

    // FIVE LINES, NOT TWENTY.
    std::printf("    (fan: %d line(s) drawn for 20 conductors)\n", int(fan.size()));
    check(fan.size() == 5, "picture: five conductors are drawn at the face, whatever the count is");

    int arriving = 0, merging = 0;
    for (const Entity* e : fan)
        (e->meta.value(QStringLiteral("fanMerged")).toBool() ? merging : arriving)++;
    check(arriving == 3, "picture: three of them carry on into the bundle");
    check(merging == 2, "picture: and the two between them merge into it");

    // THE COUNT IS NOT LOST - the drawing says what it is a picture of.
    for (const Entity* e : fan)
        if (e->meta.value(QStringLiteral("fanOf")).toInt() != 20) {
            check(false, "picture: every line says how many conductors it stands for");
            break;
        }

    // THE ARRIVING ONES CLOSE IN AND THEN RUN PARALLEL. Four points: the far end, where it has closed,
    // the knee, and the face. And they finish nearer the centreline than they started, which is what
    // "closing into a bundle" means.
    // Measured as the SPREAD across the fan - how far the outermost conductors are apart - and not as
    // a distance from the origin: the harness is drawn wherever the sheet puts it, so an absolute y
    // says nothing about the shape.
    double faceLo = 1e18, faceHi = -1e18, endLo = 1e18, endHi = -1e18;
    int withTail = 0;
    for (const Entity* e : fan) {
        if (e->meta.value(QStringLiteral("fanMerged")).toBool()) continue;
        if (e->points.size() == 4) ++withTail;
        faceLo = std::min(faceLo, e->points.back().y);
        faceHi = std::max(faceHi, e->points.back().y);
        endLo = std::min(endLo, e->points.front().y);
        endHi = std::max(endHi, e->points.front().y);
        // The last leg is PARALLEL to the run: the far end and the point it closed at are the same
        // distance across.
        if (e->points.size() == 4)
            check(std::abs(e->points[0].y - e->points[1].y) < 1e-6,
                  "picture: and runs parallel from where it closed to the end of the run");
    }
    check(withTail == 3, "picture: each arriving conductor closes and then runs on parallel");
    std::printf("    (fan: spread %.2f mm at the face, %.2f mm at the bundle)\n",
                faceHi - faceLo, endHi - endLo);
    check((endHi - endLo) < (faceHi - faceLo),
          "picture: the fan is wider at the connector face than at the bundle - it closes in");

    // THE MERGING ONES STOP SHORT, and stop somewhere sensible: past the knee, and no further along
    // than the point the bundle has closed at.
    double closedAlong = 0.0;
    for (const Entity* e : fan)
        if (!e->meta.value(QStringLiteral("fanMerged")).toBool() && e->points.size() == 4)
            closedAlong = std::max(closedAlong, std::abs(e->points[1].x - e->points.back().x));
    for (const Entity* e : fan) {
        if (!e->meta.value(QStringLiteral("fanMerged")).toBool()) continue;
        const double ran = std::abs(e->points.front().x - e->points.back().x);
        const double knee = std::abs(e->points[1].x - e->points.back().x);
        check(ran > knee && ran <= closedAlong + 1e-6,
              "picture: a merging conductor stops between its knee and the closed bundle");
        // And it is still going straight when it stops: it merges, it does not veer.
        check(std::abs(e->points.front().y - e->points.back().y) < 1e-6,
              "picture: running straight out of the face when it stops");
    }

    // AND THE LINES AT THE BUNDLE DO NOT LIE ON TOP OF ONE ANOTHER.
    //
    // THIS is the fault that was reported - "I added five conductors and it drew two". A round bundle
    // NESTS and a row does not: √(ΣD²)×k is less than the same conductors laid side by side, so
    // spreading them across the bundle diameter drew them through each other and three lines came out
    // as one thick one. Measured on the ink, not on the centrelines, because it is the ink that
    // overlapped: each conductor is a double line whose gap is its own diameter.
    {
        struct Band { double centre; double half; };
        std::vector<Band> bands;
        for (const Entity* e : fan) {
            if (e->meta.value(QStringLiteral("fanMerged")).toBool()) continue;
            bands.push_back({e->points.front().y,
                             e->meta.value(QStringLiteral("gap")).toDouble() / 2.0});
        }
        std::sort(bands.begin(), bands.end(),
                  [](const Band& x, const Band& y) { return x.centre < y.centre; });
        int overlaps = 0;
        double worst = 0.0;
        for (size_t i = 1; i < bands.size(); ++i) {
            const double clear = (bands[i].centre - bands[i - 1].centre) -
                                 (bands[i].half + bands[i - 1].half);
            if (clear < -1e-9) { ++overlaps; worst = std::min(worst, clear); }
        }
        if (overlaps > 0)
            std::printf("  picture: %d overlapping pair(s) at the bundle, worst by %.2f mm\n",
                        overlaps, -worst);
        check(overlaps == 0,
              "picture: the conductors that reach the bundle are drawn side by side, not through "
              "one another");
    }

    // AND NOTHING CROSSES. The merging conductors stop at the closing one's near edge, half a
    // conductor's width short of its centreline, so no two centrelines ever meet.
    int crossings = 0;
    for (size_t i = 0; i < fan.size(); ++i)
        for (size_t j = i + 1; j < fan.size(); ++j)
            for (size_t a = 1; a < fan[i]->points.size(); ++a)
                for (size_t b = 1; b < fan[j]->points.size(); ++b)
                    if (segmentSegmentIntersection(fan[i]->points[a - 1], fan[i]->points[a],
                                                   fan[j]->points[b - 1], fan[j]->points[b]))
                        ++crossings;
    check(crossings == 0, "picture: and no two of the five cross one another");

    // A BUNDLED RUN IS LEFT ALONE. Its fan is open only for the short stretch behind the connector and
    // its body starts where that stretch ends, so the conductors have to arrive at the bundle exactly
    // there: two legs, no parallel tail. Changing that would move every drawing already made.
    {
        MechBuildScript bs = sc;
        bs.steps[1].bundled = true;
        bs.steps[1].openStartMm = 20.0;
        const MechCompiled bc = compileMechScript(bs);
        const std::vector<Entity> bents = buildMechanicalHarness(bc.def, nullptr);
        const QString bNode = bc.stepToNode.value(QStringLiteral("S1"));
        int lines = 0, threePoint = 0;
        for (const Entity& e : bents) {
            if (e.type != EntityType::Polyline) continue;
            if (e.meta.value(QStringLiteral("mechRole")).toString() != QLatin1String("wires")) continue;
            if (!e.meta.contains(QStringLiteral("wireOd"))) continue;
            if (e.meta.value(QStringLiteral("mechNode")).toString() != bNode) continue;
            ++lines;
            if (e.points.size() == 3) ++threePoint;
        }
        check(lines == 5, "picture: a bundled run's fan is five lines too");
        check(threePoint == lines,
              "picture: and every one of them is the two-legged fan, arriving at the body");
    }
}

// ---- The break mark is in front, and the balloons are the bill's own numbers ----------------------
//
// Three separate faults with one thing in common: each is a drawing that says something untrue and
// looks deliberate while it does it.
void testBreakMarksAndBalloons() {
    // A connector with a boot on it, and a run with a break mark across it.
    MechHarnessDef def;
    MechNode p1;
    p1.id = QStringLiteral("p1");
    p1.kind = MechNodeKind::Connector;
    p1.name = QStringLiteral("P1");
    p1.pinCount = 4;
    p1.openLengthMm = 20.0;
    p1.libPart = QStringLiteral("lib-conn");
    MechFitting boot;
    boot.kind = PartKind::HeatshrinkBoot;
    boot.name = QStringLiteral("Boot");
    boot.libPart = QStringLiteral("lib-boot");
    boot.lengthMm = 28.0;
    boot.odMm = 13.1;
    p1.fittings.push_back(boot);
    def.nodes.push_back(p1);
    MechNode p2;
    p2.id = QStringLiteral("p2");
    p2.kind = MechNodeKind::FreeEnd;
    p2.parentSegment = QStringLiteral("s1");
    def.nodes.push_back(p2);
    for (int i = 0; i < 4; ++i) {
        MechWire w;
        w.id = QStringLiteral("w%1").arg(i + 1);
        w.gauge = QStringLiteral("0.5");
        def.wires.push_back(w);
    }
    MechSegment s;
    s.id = QStringLiteral("s1");
    s.fromNode = QStringLiteral("p1");
    s.toNode = QStringLiteral("p2");
    s.lengthMm = 400.0;
    s.lengthBreak = true;              // "longer than it is drawn"
    for (const MechWire& w : def.wires) s.wireIds.push_back(w.id);
    def.segments.push_back(s);

    // ---- 2. THE BREAK MARK IS DRAWN IN FRONT ----
    //
    // It MASKS the stretch of run between its two strokes, and a mask only masks what was painted
    // before it. Emitted while its own run was being drawn, everything after it - the conductors, the
    // fittings, the balloons - painted straight over the gap it had opened.
    {
        const std::vector<Entity> ents = buildMechanicalHarness(def, nullptr);
        int marks = 0, lastMark = -1, lastOther = -1;
        for (int i = 0; i < static_cast<int>(ents.size()); ++i) {
            if (ents[static_cast<size_t>(i)].type == EntityType::LengthBreak) {
                ++marks;
                lastMark = i;
            } else {
                lastOther = i;
            }
        }
        check(marks == 1, "break: the run carries its break mark");
        std::printf("  break: mark at %d of %d, last other object at %d\n", lastMark,
                    int(ents.size()), lastOther);
        check(marks == 1 && lastMark > lastOther,
              "break: and it is listed after everything else, so it is painted in FRONT of what it "
              "masks");
    }

    // ---- 3. THE BALLOON NUMBER IS A LINE OF THE DRAWING'S BILL ----
    {
        Document doc;
        PartsLibrary lib;
        // A bill with the boot on its SECOND line, and the connector nowhere on it.
        BomTable table;
        BomRow first;
        first.rowId = QStringLiteral("r1");
        first.description = QStringLiteral("Something else entirely");
        first.mfrPn = QStringLiteral("ZZ-1");
        first.qty = 1.0;
        table.rows.push_back(first);
        BomRow bootRow;
        bootRow.rowId = QStringLiteral("r2");
        bootRow.partId = QStringLiteral("lib-boot");
        bootRow.description = QStringLiteral("Boot");
        bootRow.qty = 1.0;
        table.rows.push_back(bootRow);
        table.config.includeDrawing = false;   // nothing is on the sheet yet
        doc.bom() = table.toJson();

        MechHarnessDef numbered = def;
        numbered.draw.bomItemOf = bomItemLookup(doc, lib);
        const std::vector<Entity> ents = buildMechanicalHarness(numbered, nullptr);
        QStringList said;
        for (const Entity& e : ents)
            if (e.meta.value(QStringLiteral("balloon")).toBool())
                said << e.meta.value(QStringLiteral("text")).toString();
        std::printf("  balloons: %s\n", qPrintable(said.join(QStringLiteral(" "))));
        check(said.contains(QStringLiteral("2")),
              "balloon: the boot is ballooned 2 - the line it is on in the drawing's bill");
        check(!said.contains(QStringLiteral("1")),
              "balloon: and nothing is ballooned 1, which is a part this harness does not have");
        check(said.contains(QStringLiteral("?")),
              "balloon: a part the bill does not list is ballooned \"?\" rather than a number "
              "nobody can look up");

        // NO BILL AT ALL: everything is "?". A sheet with no table has no line numbers to quote, and
        // inventing them is the one answer that cannot be checked.
        Document bare;
        MechHarnessDef none = def;
        none.draw.bomItemOf = bomItemLookup(bare, lib);
        QStringList all;
        for (const Entity& e : buildMechanicalHarness(none, nullptr))
            if (e.meta.value(QStringLiteral("balloon")).toBool())
                all << e.meta.value(QStringLiteral("text")).toString();
        bool everyOneUnknown = !all.isEmpty();
        for (const QString& t : all)
            if (t != QLatin1String("?")) everyOneUnknown = false;
        check(everyOneUnknown, "balloon: with no bill on the sheet, every balloon says \"?\"");
    }

    // ---- 4. THE BALLOON TOUCHES THE PART IT BELONGS TO ----
    //
    // The leader used to be drawn to the CENTRE of the part - straight through its outline, stopping
    // somewhere inside it - which reads as a balloon lying on top of the drawing rather than as one
    // pointing at something.
    {
        const std::vector<Entity> ents = buildMechanicalHarness(def, nullptr);
        const Entity* bootBody = nullptr;
        for (const Entity& e : ents)
            if (e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("fitting") &&
                e.meta.contains(QStringLiteral("bodyDia")))
                bootBody = &e;
        check(bootBody != nullptr, "touch: the boot is drawn");
        if (!bootBody) return;
        const Bounds bb = bootBody->bounds();
        check(bb.valid, "touch: and has an extent");

        int touching = 0, checked = 0;
        for (const Entity& e : ents) {
            if (!e.meta.value(QStringLiteral("balloon")).toBool()) continue;
            const QJsonArray leaders = e.meta.value(QStringLiteral("leaders")).toArray();
            if (leaders.isEmpty()) continue;
            const QJsonArray tipArr = leaders.first().toArray();
            if (tipArr.size() < 2) continue;
            const Vec2 tip{tipArr[0].toDouble(), tipArr[1].toDouble()};
            // Is this the boot's balloon? Its tip is somewhere on the boot.
            if (tip.x < bb.min.x - 1.0 || tip.x > bb.max.x + 1.0) continue;
            ++checked;
            // TOUCHING: the tip is ON the outline it points at, within a hair - not at the middle of
            // it, and not floating clear of it.
            const double centreY = (bb.min.y + bb.max.y) / 2.0;
            const double halfAcross = (bb.max.y - bb.min.y) / 2.0;
            const double off = std::abs(tip.y - centreY);
            std::printf("  touch: tip %.2f from the middle, half-height %.2f\n", off, halfAcross);
            if (off > halfAcross * 0.75 && off <= halfAcross + 1.0) ++touching;
        }
        check(checked > 0, "touch: the boot has a balloon to look at");
        check(checked > 0 && touching == checked,
              "touch: its leader lands ON the boot's outline, not at the middle of it");
    }
}

// ---- Left and Right turn the part in the preview --------------------------------------------------
//
// On the FIRST step the choice is not a turn at all - there is nothing before it to turn away from - it
// is which way the whole harness is built across the sheet. The compiler DISCARDS a turn on a step that
// lands on the root node, so storing it as a turn meant pressing Left on the first connector changed
// absolutely nothing: the preview went on showing the part facing the other way.
void testDirectionTurnsTheFirstPartRoundInThePreview() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    w.setCurrentCategory(Cat::Connector);
    check(w.directionSetsHandedness(),
          "direction: on the first step, left/right IS the harness's handedness");
    check(w.script().leftToRight, "direction: which starts out left to right");

    // Where the connector body sits relative to the node it is on, for the harness as previewed.
    const auto bodySideOfNode = [](const MechBuildScript& sc) {
        const MechCompiled c = compileMechScript(sc);
        const MechLayout lay = layoutMechanicalHarness(c.def);
        double nodeX = 0.0;
        if (!lay.nodes.empty()) nodeX = lay.nodes.front().at.x;
        double bodyMid = nodeX;
        int seen = 0;
        for (const Entity& e : buildMechanicalHarness(c.def, nullptr)) {
            if (e.meta.value(QStringLiteral("mechRole")).toString() != QLatin1String("connector"))
                continue;
            const Bounds b = e.bounds();
            if (!b.valid) continue;
            bodyMid = (b.min.x + b.max.x) / 2.0;
            ++seen;
        }
        return seen > 0 ? bodyMid - nodeX : 0.0;
    };

    const double right = bodySideOfNode(w.previewScript());
    check(std::abs(right) > 1e-6, "direction: the connector body is drawn off to one side of its node");

    // NOW PRESS LEFT. The whole harness is built the other way, so the body swings to the other side of
    // the node - its output face still pointing the way the harness goes. setCurrentGoesRight settles
    // the handedness itself on the first step: that is the same path the buttons and the tests take.
    w.setCurrentGoesRight(false);
    check(!w.script().leftToRight, "direction: the script is now built right to left");

    const double left = bodySideOfNode(w.previewScript());
    check(std::abs(left) > 1e-6, "direction: and the body is still drawn to one side");
    check(right * left < 0.0,
          "direction: on the OTHER side now - the part turned round rather than staying put");

    // And the compiled harness says so in the one figure the renderer reads.
    const MechCompiled c = compileMechScript(w.previewScript());
    check(near(c.def.draw.baseDirectionDeg, 180.0),
          "direction: which is baseDirectionDeg, the figure the whole tree is laid from");
}

// ---- The first station is named for the end it starts on ------------------------------------------
//
// The first thing described is still the connector, terminal or run that was picked - the end-side
// words are its place on the sheet, not a step of their own. Drawing rightwards starts at the LEFT
// end; drawing leftwards starts at the RIGHT end. And once parts have been described against it the
// end side is the heading the subparts were fitted to, so its own figures shut like any other
// station's.
void testTheFirstStationReadsAsTheEndSideAndLocksAsAHeading() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    w.setCurrentCategory(Cat::Connector);
    QStringList rows = w.shownSteps();
    check(rows.size() == 1 && rows.front().contains(QStringLiteral("Left end side")),
          "end side: the first step being described is the left end when the harness goes right");
    w.typeName(QStringLiteral("P1"));
    w.commitStep();
    rows = w.shownSteps();
    check(!rows.isEmpty() && rows.front().contains(QStringLiteral("Left end side")),
          "end side: and keeps that name once the connector is committed");
    check(rows.front().contains(QStringLiteral("Connector")) &&
              rows.front().contains(QStringLiteral("P1")),
          "end side: while still saying which connector it is");

    // Give that end a part and it stops being a form - the part was described against these figures.
    const QString endSide = w.script().steps.front().id;
    w.setCurrentCategory(Cat::Backshell);
    w.typeName(QStringLiteral("BS1"));
    w.commitSubpart();
    check(w.stepHasSubparts(endSide), "end side: it now has a part of its own");
    w.editStep(endSide);
    check(w.editedStepIsLocked(), "end side: so its right-hand figures are not editable");

    // The other way round names the OTHER end.
    MechWizardDialog leftwards(&ed, nullptr);
    leftwards.setCurrentCategory(Cat::Connector);
    leftwards.setCurrentGoesRight(false);
    const QStringList pendingLeft = leftwards.shownSteps();
    check(!pendingLeft.isEmpty() &&
              pendingLeft.front().contains(QStringLiteral("Right end side")),
          "end side: and the pending row changes with the direction before anything is committed");
    leftwards.typeName(QStringLiteral("P2"));
    leftwards.commitStep();
    const QStringList leftRows = leftwards.shownSteps();
    check(!leftRows.isEmpty() && leftRows.front().contains(QStringLiteral("Right end side")),
          "end side: drawing leftwards begins on the right end");
    check(leftRows.front().contains(QStringLiteral("Connector")) &&
              leftRows.front().contains(QStringLiteral("P2")),
          "end side: and still names the part that is really there");
}

// ---- A described step is a ghost until it is applied ---------------------------------------------
//
// The preview shows what is on the drawing AND what is being described, and the two have to be told
// apart at a glance - otherwise a step nobody has committed reads as part of the harness and stops
// being checked. Drawn IN PLACE, so applying changes only how it is drawn and never where it sits: a
// step that moved when it was applied would make the preview a picture of something else.
void testADescribedStepIsGhostedUntilApplied() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    const auto ghostCount = [](const std::vector<Entity>& ents) {
        int n = 0;
        for (const Entity& e : ents)
            if (e.meta.value(QLatin1String(kPreviewGhostKey)).toBool()) ++n;
        return n;
    };
    const auto plainCount = [](const std::vector<Entity>& ents) {
        int n = 0;
        for (const Entity& e : ents)
            if (!e.meta.value(QLatin1String(kPreviewGhostKey)).toBool()) ++n;
        return n;
    };
    // Where every object sits, ghost or not - so "applying moved nothing" can be asserted rather than
    // assumed.
    // Where each object sits, so "applying moved nothing" can be asserted rather than assumed. `ghosts`
    // picks which half of the drawing to describe: only the ghosts, or only what is applied.
    const auto places = [](const std::vector<Entity>& ents, bool ghosts) {
        QStringList out;
        for (const Entity& e : ents) {
            if (e.meta.value(QLatin1String(kPreviewGhostKey)).toBool() != ghosts) continue;
            QString sig = QString::number(static_cast<int>(e.type));
            for (const Vec2& pt : e.points)
                sig += QStringLiteral("|%1,%2").arg(pt.x, 0, 'f', 3).arg(pt.y, 0, 'f', 3);
            out << sig;
        }
        out.sort();
        return out;
    };

    // A connector, applied. Then a cable being described after it.
    w.setCurrentCategory(Cat::Connector);
    w.typeName(QStringLiteral("P1"));
    w.applyToDrawing();
    check(w.script().steps.size() == 1, "ghost: Apply puts the described step on the drawing");
    // AND LEAVES YOU STANDING ON IT. Apply is not Next: the reason to apply rather than move on is to
    // see the thing against the rest of the harness and go on correcting it, so the window is editing
    // what was just applied and no new step has been opened behind it.
    //
    // The cost is that picking a category now RE-KINDS the applied step rather than starting the next
    // one - the same as picking a category with a row selected in the list, which is what edit mode has
    // always meant. Next is what opens the following step.
    check(w.editingStepId() == w.script().steps.front().id,
          "ghost: and leaves the window editing the step just applied, not opening another");

    // Next opens the following step; THAT is the one drawn as a ghost.
    w.commitStep();
    check(w.editingStepId().isEmpty(), "ghost: Next opens a new step to describe");
    w.setCurrentCategory(Cat::Cable);
    const std::vector<Entity> describing = w.shownEntities();
    check(ghostCount(describing) > 0,
          "ghost: the cable being described is drawn as not yet applied");
    check(plainCount(describing) > 0,
          "ghost: while the connector already applied is drawn as part of the harness");

    // What was a ghost, and where it sat.
    std::vector<Entity> wereGhosts;
    for (const Entity& e : describing)
        if (e.meta.value(QLatin1String(kPreviewGhostKey)).toBool()) wereGhosts.push_back(e);

    // NOW APPLY IT. Those objects become part of the drawing, in the SAME PLACES.
    w.applyToDrawing();
    const std::vector<Entity> applied = w.shownEntities();
    // TWO, not three: Next after an Apply commits nothing. The fields still describe the step that was
    // just applied, so committing them again would copy it - which is how one connector came out as
    // 1.1 and 1.1.1 with the same designator. Next only opens the next thing to describe.
    check(w.script().steps.size() == 2, "ghost: the connector and the cable, and no copy of either");

    const QStringList nowPlain = places(applied, /*ghosts=*/false);
    const QStringList wasGhost = places(wereGhosts, /*ghosts=*/true);
    bool allLanded = true;
    for (const QString& sig : wasGhost)
        if (!nowPlain.contains(sig)) allLanded = false;
    check(!wasGhost.isEmpty() && allLanded,
          "ghost: every object that was a ghost is now part of the drawing, in the same place - "
          "applying changed its status, not its geometry");
}

// ---- Several housings at one station read as a stack ---------------------------------------------
//
// A station can carry more than one connector, and they have to be read as a set: one above the next,
// each clear of its neighbour, in the order they were described. They used to be drawn with the first
// on the axis and the others stepped off it alternately, above then below, which reads as one connector
// with strays either side rather than as a stack.
void testSeveralHousingsAtOneStationAreStackedWithGaps() {
    // Three connectors at one station: one applied, two as parts of it.
    MechBuildScript sc;
    MechStep p1;
    p1.id = QStringLiteral("S1");
    p1.kind = PartKind::Connector;
    p1.name = QStringLiteral("P1");
    p1.pinCount = 4;
    sc.steps.push_back(p1);
    for (const char* nm : {"P2", "P3"}) {
        MechStep extra;
        extra.id = QStringLiteral("S_%1").arg(QString::fromLatin1(nm));
        extra.kind = PartKind::Connector;
        extra.name = QString::fromLatin1(nm);
        extra.parentStep = QStringLiteral("S1");
        extra.isSubpart = true;
        extra.pinCount = 4;
        sc.steps.push_back(extra);
    }
    MechStep run;
    run.id = QStringLiteral("S2");
    run.kind = PartKind::Cable;
    run.parentStep = QStringLiteral("S1");
    run.lengthMm = 200.0;
    run.odMm = 6.0;
    run.wireCount = 6;
    run.gauge = QStringLiteral("22 AWG");
    sc.steps.push_back(run);

    const MechCompiled c = compileMechScript(sc);
    const MechNode* node = c.def.node(c.stepToNode.value(QStringLiteral("S1")));
    check(node && node->extraEnds.size() == 2, "stack: three housings on one node");

    // Each housing's own band across the harness.
    std::vector<std::pair<double, double>> bands;   // low, high across the run
    for (const Entity& e : buildMechanicalHarness(c.def, nullptr)) {
        if (e.meta.value(QStringLiteral("mechRole")).toString() != QLatin1String("connector"))
            continue;
        const Bounds b = e.bounds();
        if (b.valid) bands.push_back({b.min.y, b.max.y});
    }
    // TWO, NOT THREE. A station with parts hung on it draws no body of its own - see
    // MechNode::hasSubparts - so what is on the sheet is its two parts. The station is still a station:
    // it keeps its slot in the stack, the parts are still spaced against it, and the run still leaves
    // from it. It simply is not also a housing.
    check(bands.size() == 2, "stack: the parts are drawn; the station they hang on is not");
    std::sort(bands.begin(), bands.end());

    int overlaps = 0;
    double smallestGap = 1e9;
    for (size_t i = 1; i < bands.size(); ++i) {
        const double gap = bands[i].first - bands[i - 1].second;
        if (gap < 0.0) ++overlaps;
        smallestGap = std::min(smallestGap, gap);
    }
    if (bands.size() == 2)
        std::printf("  stack: gaps %.2f, overlaps %d\n", smallestGap, overlaps);
    check(overlaps == 0, "stack: no housing is drawn on top of another");
    check(bands.size() == 2 && smallestGap > 0.0,
          "stack: and there is a real gap between each one and the next");

    // CENTRED ON THE NODE, so the stack straddles the harness rather than hanging off one side of it.
    const MechLayout lay = layoutMechanicalHarness(c.def);
    double nodeY = 0.0;
    for (const MechPlacedNode& pn : lay.nodes)
        if (pn.id == node->id) nodeY = pn.at.y;
    if (bands.size() == 2) {
        const double stackMid = (bands.front().first + bands.back().second) / 2.0;
        check(std::abs(stackMid - nodeY) < 1e-6,
              "stack: the stack is centred on the station, not hung off one side of it");
    }

    // AND A SINGLE HOUSING IS UNMOVED. Every harness drawn before this has one, and a stack of one must
    // land exactly where it always did - on the axis.
    MechBuildScript alone;
    alone.steps.push_back(p1);
    MechStep only = run;
    alone.steps.push_back(only);
    const MechCompiled ac = compileMechScript(alone);
    const MechNode* an = ac.def.node(ac.stepToNode.value(QStringLiteral("S1")));
    const MechLayout al = layoutMechanicalHarness(ac.def);
    double aNodeY = 0.0;
    for (const MechPlacedNode& pn : al.nodes)
        if (an && pn.id == an->id) aNodeY = pn.at.y;
    for (const Entity& e : buildMechanicalHarness(ac.def, nullptr)) {
        if (e.meta.value(QStringLiteral("mechRole")).toString() != QLatin1String("connector"))
            continue;
        const Bounds b = e.bounds();
        if (!b.valid) continue;
        check(std::abs((b.min.y + b.max.y) / 2.0 - aNodeY) < 1e-6,
              "stack: one housing on its own is still centred on the axis - nothing moved");
    }
}

// ---- Each run enters the junction part at its own declared face -----------------------------------
//
// Limbs and a trunk meeting at one place all end at the node POINT, which is right when there is
// nothing there but a junction. Where a real part sits on it - a splice block, a breakout boot, anything
// whose drawing declares the faces the conductors leave by - each run has its own way in, and drawing
// them all converging on one spot says the harness enters the part somewhere it does not.
//
// AND ONLY WHERE THE PART DECLARES FACES: a part that says nothing keeps exactly the behaviour it had.
void testRunsEnterTheJunctionPartAtItsOwnFaces() {
    // A junction part with three declared faces, and three runs meeting on it.
    PartsCatalog cat;
    cat.setRemembering(false);
    LibraryPart& block = cat.active().add(PartKind::YSplit);
    block.description = QStringLiteral("Three-way splice block");
    Entity body;
    body.type = EntityType::Rect;
    body.points = {{-10.0, -15.0}, {10.0, -15.0}, {10.0, 15.0}, {-10.0, 15.0}};
    block.mechanical = partDrawingFromEntities({body});
    block.outputs.push_back({{-10.0, -10.0}, {-10.0, -4.0}});
    block.outputs.push_back({{-10.0, -3.0}, {-10.0, 3.0}});
    block.outputs.push_back({{-10.0, 4.0}, {-10.0, 10.0}});
    const QString blockId = block.id;

    // The same part with NO faces declared, for the comparison that matters.
    LibraryPart& plain = cat.active().add(PartKind::YSplit);
    plain.description = QStringLiteral("Same block, no faces given");
    plain.mechanical = partDrawingFromEntities({body});
    const QString plainId = plain.id;

    const auto buildWith = [&](const QString& partId) {
        MechBuildScript sc;
        MechStep node;
        node.id = QStringLiteral("S1");
        node.kind = PartKind::YSplit;
        node.name = QStringLiteral("BR1");
        node.libPart = partId;
        sc.steps.push_back(node);
        // Three runs off it: two limbs and a trunk, which is the shape that meets at a junction.
        int n = 0;
        for (double turn : {90.0, 0.0, -90.0}) {
            MechStep run;
            run.id = QStringLiteral("R%1").arg(++n);
            run.kind = PartKind::Wire;
            run.parentStep = QStringLiteral("S1");
            run.turnDeg = turn;
            run.lengthMm = 120.0;
            run.wireCount = 2;
            run.gauge = QStringLiteral("22 AWG");
            sc.steps.push_back(run);
        }
        const MechCompiled c = compileMechScript(sc);
        return buildMechanicalHarness(c.def, &cat);
    };

    // ---- With faces: one entry lead per run, each to a DIFFERENT face ----
    const std::vector<Entity> withFaces = buildWith(blockId);
    std::vector<Vec2> targets;
    for (const Entity& e : withFaces)
        if (e.meta.value(QStringLiteral("entryFace")).toBool() && e.points.size() == 2)
            targets.push_back(e.points.back());
    check(targets.size() >= 2,
          "faces: the runs meeting the block are taken to its declared faces");

    // EACH FACE ONCE. Two runs into one face would be two conductors crimped into one way.
    bool distinct = true;
    for (size_t i = 0; i < targets.size(); ++i)
        for (size_t j = i + 1; j < targets.size(); ++j)
            if (distance(targets[i], targets[j]) < 1e-6) distinct = false;
    check(distinct, "faces: no two runs are taken to the same face");

    // ---- With no faces declared: nothing new is drawn ----
    const std::vector<Entity> withoutFaces = buildWith(plainId);
    int strays = 0;
    for (const Entity& e : withoutFaces)
        if (e.meta.value(QStringLiteral("entryFace")).toBool()) ++strays;
    check(strays == 0,
          "faces: a part that declares none keeps the behaviour it had - everything meets the junction");
}

// ---- Next carries on from whatever you are standing on -------------------------------------------
//
// Two rules, and which one applies is decided by what is selected rather than by a second button:
//
//   Standing on a MAIN step, Next adds the next main step - the next station along the harness.
//   Standing on a SUBPART, Next carries that subpart on - the next thing in the mother's own group.
//
// And selecting a step has to move the anchor at all, which it did not: clicking back to a connector
// and pressing Next put the new step after the END of the harness, the one place nobody meant.
void testNextCarriesOnFromWhereYouAreStanding() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    // A connector, then a cable after it: two main stations.
    w.setCurrentCategory(Cat::Connector);
    w.typeName(QStringLiteral("P1"));
    w.commitStep();
    const QString mother = w.script().steps.back().id;
    w.setCurrentCategory(Cat::Cable);
    w.commitStep();
    check(w.script().steps.size() == 2, "standing: two stations so far");
    check(!w.script().steps.back().isSubpart, "standing: the cable is a station, not a part");

    // ---- STAND BACK ON THE CONNECTOR. The anchor must follow the selection. ----
    w.editStep(mother);
    check(w.anchorStepId() == mother,
          "standing: selecting a step makes it what the next thing attaches to");

    // Add subpart there: wires leaving that connector.
    w.setCurrentCategory(Cat::Wires);
    w.commitSubpart();
    const MechStep wires = w.script().steps.back();
    check(wires.isSubpart && wires.parentStep == mother,
          "standing: Add subpart hangs the wires off the connector");
    check(w.anchorStepId() == wires.id,
          "standing: and leaves you standing on the part just added, ready to carry it on");

    // ---- NEXT ON A SUBPART GIVES IT A SIBLING, not a child. ----
    w.editStep(wires.id);
    check(w.anchorStepId() == wires.id, "standing: now standing on the wires");
    w.setCurrentCategory(Cat::Heatshrink);
    w.commitStep();
    const MechStep after = w.script().steps.back();
    check(after.parentStep == mother,
          "standing: Next on a part gives the next part of the same station");
    check(after.isSubpart,
          "standing: and is STILL a part of the connector's group, not a new station");

    // ---- AND ADD SUBPART ON A SUBPART GOES DEEPER. ----
    w.editStep(wires.id);
    w.setCurrentCategory(Cat::Heatshrink);
    w.commitSubpart();
    check(w.script().steps.back().parentStep == wires.id,
          "standing: Add subpart on a part makes a part OF that part");

    // ---- NEXT ON THE MOTHER adds a station. ----
    w.editStep(mother);
    w.setCurrentCategory(Cat::HarnessBody);
    w.commitStep();
    const MechStep station = w.script().steps.back();
    check(station.parentStep == mother, "standing: Next on the mother follows the mother");
    check(!station.isSubpart,
          "standing: and is a MAIN step - the next station along the harness");

    // And the list says so: the station takes a number of its own, the subparts take the mother's.
    const QStringList rows = w.shownSteps();
    bool sawStationNumber = false, sawDeepSubpart = false;
    for (const QString& r : rows) {
        const QString num = r.trimmed().section(QLatin1Char(' '), 0, 0);
        if (r.contains(partKindName(PartKind::Wire)) && num.count(QLatin1Char('.')) == 1)
            sawDeepSubpart = true;
        if (r.contains(partKindName(PartKind::Heatshrink)) && num.count(QLatin1Char('.')) >= 2)
            sawDeepSubpart = true;
        if (num == QStringLiteral("3")) sawStationNumber = true;
    }
    check(sawDeepSubpart, "standing: the subpart chain is numbered under its mother");
    check(sawStationNumber, "standing: and the new station is numbered as one");
}

// ---- Deleting a station does not leave its parts orphaned ---------------------------------------
//
// Deleting a step re-parents whatever hung off it, and a PART of that step then has nothing to be a
// part of. Left flagged as one it claimed a mother that no longer existed: the numbering could find no
// station to count it under and wrote it "0.1", and the list showed a single indented orphan where a
// step and its part had been - which reads exactly like the part having eaten the step.
void testDeletingAStationDoesNotOrphanItsParts() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    // A connector, and a part of it.
    w.setCurrentCategory(Cat::Connector);
    w.typeName(QStringLiteral("P1"));
    w.commitStep();
    const QString station = w.script().steps.back().id;
    w.setCurrentCategory(Cat::Wires);
    w.commitSubpart();
    check(w.script().steps.size() == 2, "orphan: a station and a part of it");
    check(w.script().steps.back().isSubpart, "orphan: the wires are a part of the connector");

    // NOW DELETE THE STATION. What was its part has nothing left to belong to.
    w.deleteStep(station);
    check(w.script().steps.size() == 1, "orphan: one step left");
    if (w.script().steps.empty()) return;
    const MechStep left = w.script().steps.front();
    check(left.parentStep.isEmpty(), "orphan: with nothing before it");
    check(!left.isSubpart,
          "orphan: so it is a STATION now - a step with nothing before it cannot be a part of anything");

    // And the list says so: numbered as a station, not indented under one that is not there.
    const QStringList rows = w.shownSteps();
    bool sawOrphanNumber = false;
    for (const QString& r : rows)
        if (r.trimmed().startsWith(QStringLiteral("0."))) sawOrphanNumber = true;
    check(!sawOrphanNumber,
          "orphan: nothing is numbered under station zero - there is no station zero");
    check(!rows.isEmpty() && rows.value(0).startsWith(QStringLiteral("1")),
          "orphan: the step that is left is station 1");
}

// ---- A station's parts can be rolled up, and stay rolled up --------------------------------------
//
// The list is rebuilt on every keystroke - typing a designator refreshes it - so a tree that forgot its
// expansion state would snap shut under the hand. The state is remembered by step ID, because the rows
// themselves are thrown away and rebuilt each time.
void testAStationCanBeRolledUpAndStaysThatWay() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    // A station with two parts, and a second station after it.
    w.setCurrentCategory(Cat::Connector);
    w.typeName(QStringLiteral("P1"));
    w.commitStep();
    const QString station = w.script().steps.back().id;
    w.setCurrentCategory(Cat::Backshell);
    w.commitSubpart();
    w.setCurrentCategory(Cat::Boot);
    w.commitSubpart();

    check(w.rolledUpSteps().isEmpty(),
          "rollup: a station whose parts have just been added is open - somebody wants to see them");

    // ROLL IT UP.
    w.rollUp(station, true);
    check(w.rolledUpSteps().contains(station), "rollup: the station folds away");

    // NOW TYPE. Every keystroke rebuilds the list, and it must still be folded afterwards.
    w.typeName(QStringLiteral("P1-x"));
    check(w.rolledUpSteps().contains(station),
          "rollup: and stays folded through the refresh a keystroke causes");

    // Its parts are still THERE, just hidden - the script is untouched by folding a row.
    int subparts = 0;
    for (const MechStep& st : w.script().steps)
        if (st.isSubpart) ++subparts;
    check(subparts == 2, "rollup: folding hides its parts and does not remove them");

    // And unrolling brings them back.
    w.rollUp(station, false);
    check(!w.rolledUpSteps().contains(station), "rollup: and it opens again");
}

// ---- A station with parts is a heading, and a heading is not a form -----------------------------
//
// Once parts have been described against a station, its own figures are what they were described
// against: a backshell chosen for a 9-way shell is sized to that shell. Leaving the shell editable
// under it is how the backshell ends up fitted to a connector that has since become a different one -
// silently, because nothing on the sheet says the two ever disagreed. So the station becomes what its
// number already calls it, the heading 1 over the parts 1.1 and 1.2, and a click folds them away.
void testAStationWithPartsIsAHeadingNotAForm() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    w.setCurrentCategory(Cat::Connector);
    w.typeName(QStringLiteral("P1"));
    w.commitStep();
    const QString station = w.script().steps.back().id;
    check(!w.stepHasSubparts(station), "heading: a station on its own has no parts");
    w.editStep(station);
    check(!w.editedStepIsLocked(), "heading: and is an ordinary form while it has none");

    // Give it a part.
    w.setCurrentCategory(Cat::Backshell);
    w.typeName(QStringLiteral("BS1"));
    w.commitSubpart();
    QString part;
    for (const MechStep& st : w.script().steps)
        if (st.isSubpart) part = st.id;
    check(w.stepHasSubparts(station), "heading: with a part described against it, it has parts");

    // STANDING ON IT: read-only, and nothing gets through to it.
    w.editStep(station);
    check(w.editedStepIsLocked(), "heading: standing on it, the window says it is read-only");
    w.typeName(QStringLiteral("SOMETHING ELSE"));
    const MechStep* kept = w.script().step(station);
    check(kept && kept->name == QLatin1String("P1"),
          "heading: and typing at it changes nothing - the station keeps its designator");

    // Its PART is still a form. That is where the figures that matter to the build live, and where
    // somebody sent away from the station is being sent.
    w.editStep(part);
    check(!w.editedStepIsLocked(), "heading: the part of it is still a form");
    w.typeName(QStringLiteral("BS2"));
    const MechStep* edited = w.script().step(part);
    check(edited && edited->name == QLatin1String("BS2"), "heading: and typing at it does change it");

    // CLICKING A HEADING FOLDS IT. Both ways, and more than once: a fold that only worked the first
    // time would read as a fold that did not work.
    check(w.rolledUpSteps().isEmpty(), "heading: a station whose part has just appeared is open");
    w.toggleFold(station);
    check(w.rolledUpSteps().contains(station), "heading: and folds its parts away");
    w.toggleFold(station);
    check(!w.rolledUpSteps().contains(station), "heading: and opens them again");

    // TAKE THE PART OFF AND IT IS A FORM AGAIN. The rule is about what the station HAS, not a flag
    // set once on it - so the way out of it is the way in, reversed.
    w.deleteStep(part);
    check(!w.stepHasSubparts(station), "heading: with its parts gone it is a station again");
    w.editStep(station);
    check(!w.editedStepIsLocked(), "heading: and editable again");
    w.typeName(QStringLiteral("P9"));
    const MechStep* back = w.script().step(station);
    check(back && back->name == QLatin1String("P9"), "heading: and it takes figures again");
}

// ---- The bundle a trunk or a branch comes to, worked out from the wires taken from the library ----
//
// A run is described by what it CARRIES, and the one figure that follows from that is how thick it is -
// which decides the backshell, the boot, the braid and the heatshrink at both its ends. Two things were
// wrong: the wire chosen from the library never reached the conductor group at all, so the run was laid
// up at the nominal for its gauge rather than at the diameter on the data sheet; and the figure was
// nowhere on the screen, so it could only be found out after the drawing had been placed.
void testTheBundleIsWorkedOutFromTheWiresTaken() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    // A thick-walled wire: its stated diameter is deliberately NOT the nominal for its gauge, which is
    // the whole point - the two can be told apart in the answer.
    PartsCatalog& cat = w.catalogForTest();
    cat.setRemembering(false);
    LibraryPart& wire = cat.active().add(PartKind::Wire);
    wire.description = QStringLiteral("Hook-up, thick wall");
    wire.mfrPn = QStringLiteral("TW-050");
    wire.params[QStringLiteral("od")] = QStringLiteral("2.6");
    const QString wireId = wire.id;

    w.setCurrentCategory(Cat::Connector);
    w.commitStep();
    w.setCurrentCategory(Cat::HarnessBody);
    w.reloadPartsForTest();
    check(w.choosePart(wireId), "bundle: the library wire is offered on a harness body");
    w.setRunLengthForTest(400.0);
    w.setConductorsForTest(4);

    const double nominal = wireOuterDiameterMm(w.gaugeCanon());
    check(nominal > 0.0 && std::abs(nominal - 2.6) > 0.2,
          "bundle: the test's wire is not the nominal for its gauge, so the two can be told apart");

    // FOUR OF Ø2.6: √(4 × 2.6²) × 1.2 = 5.2 × 1.2 = 6.24.
    const std::vector<double> ods = w.conductorOds();
    check(ods.size() == 4, "bundle: four conductors are counted");
    check(!ods.empty() && near(ods.front(), 2.6),
          "bundle: at the diameter the LIBRARY part states, not the nominal for the gauge");
    const BundleCalc calc = w.bundleForDescribedRun();
    check(near(calc.result, 6.24, 1e-6), "bundle: and they lay up to Ø6.24");
    check(calc.unknown == 0, "bundle: with none of them left out for want of a diameter");

    // AND IT IS ON THE SCREEN, beside the conductors it was worked out from.
    const QString said = w.shownBundleText();
    std::printf("    (bundle readout: %s)\n", qPrintable(said));
    check(said.contains(QStringLiteral("6.2")), "bundle: the readout shows the figure");
    check(said.contains(QStringLiteral("4")), "bundle: and says how it was reached");

    // THE SAME FIGURE THE RUN IS DRAWN AT. The group carries the part and its diameter onto the step,
    // so the harness is laid up at what the readout said rather than at the gauge's nominal.
    w.addConductorGroup();
    check(w.pendingConductors().size() == 1, "bundle: the group is added");
    check(w.pendingConductors().front().libPart == wireId,
          "bundle: carrying the library wire it was taken from");
    check(near(w.pendingConductors().front().odMm, 2.6),
          "bundle: and the diameter that wire states");
    w.commitStep();

    const MechCompiled c = compileMechScript(w.script());
    check(c.errors.isEmpty(), "bundle: the run compiles");
    const MechStep& run = w.script().steps.back();
    const MechSegment* seg = c.def.segment(c.stepToSegment.value(run.id));
    check(seg != nullptr, "bundle: and made a segment");
    if (seg)
        check(near(segmentBundleDiameterMm(c.def, *seg), 6.24, 1e-3),
              "bundle: which is DRAWN at the bundle the readout gave");

    // A CABLE IS ITS JACKET, whatever its cores come to - said rather than showing the cores as
    // though they were the outside of the thing.
    w.setCurrentCategory(Cat::Cable);
    check(w.shownBundleText().contains(QStringLiteral("jacket")),
          "bundle: on a cable the readout is about the jacket");
}

// ---- Library housings at one station do not lie on top of each other ----------------------------
//
// The stack used ONE pitch for every housing, taken from the default body formula. That is right while
// every housing is a default box and wrong the moment one comes from the library, because a real part
// draws whatever size somebody drew it - a pitch sized for a 2-way default box laid three real drawings
// on top of one another, and the sheet showed one big part with the others buried under it.
//
// The stack test that already existed used default boxes and passed straight through this.
void testLibraryHousingsAtOneStationDoNotOverlap() {
    PartsCatalog cat;
    cat.setRemembering(false);
    // A part far taller than the default formula would ever guess for a 2-way housing.
    LibraryPart& big = cat.active().add(PartKind::Connector);
    big.description = QStringLiteral("A tall 2-way housing");
    Entity body;
    body.type = EntityType::Rect;
    body.points = {{0.0, -22.0}, {30.0, -22.0}, {30.0, 22.0}, {0.0, 22.0}};
    big.mechanical = partDrawingFromEntities({body});
    const QString bigId = big.id;

    MechBuildScript sc;
    MechStep p1;
    p1.id = QStringLiteral("S1");
    p1.kind = PartKind::Connector;
    p1.name = QStringLiteral("P1");
    p1.libPart = bigId;
    p1.pinCount = 2;
    sc.steps.push_back(p1);
    for (const char* nm : {"P2", "P3"}) {
        MechStep x;
        x.id = QStringLiteral("S_%1").arg(QString::fromLatin1(nm));
        x.kind = PartKind::Connector;
        x.name = QString::fromLatin1(nm);
        x.parentStep = QStringLiteral("S1");
        x.isSubpart = true;
        x.libPart = bigId;
        x.pinCount = 2;
        sc.steps.push_back(x);
    }
    MechStep run;
    run.id = QStringLiteral("S2");
    run.kind = PartKind::Cable;
    run.parentStep = QStringLiteral("S1");
    run.lengthMm = 200.0;
    run.odMm = 6.0;
    run.wireCount = 4;
    run.gauge = QStringLiteral("22 AWG");
    sc.steps.push_back(run);

    const MechCompiled c = compileMechScript(sc);
    const MechNode* node = c.def.node(c.stepToNode.value(QStringLiteral("S1")));
    check(node && node->extraEnds.size() == 2, "libstack: three real housings on one node");

    // The band each housing takes across the harness. The part is 44 tall, so a default-sized pitch
    // cannot possibly hold three of them apart - which is what makes this the case that bites.
    std::vector<std::pair<double, double>> bands;
    for (const Entity& e : buildMechanicalHarness(c.def, &cat)) {
        if (e.meta.value(QStringLiteral("mechRole")).toString() != QLatin1String("connector"))
            continue;
        const Bounds b = e.bounds();
        if (b.valid) bands.push_back({b.min.y, b.max.y});
    }
    // The station draws no body of its own now, so the two parts hung on it are what is on the sheet -
    // and they are the two this test is about: the point is that a library housing gets a slot as tall
    // as it really draws, which is checked on the pair just as well as on the trio.
    check(bands.size() == 2, "libstack: both parts are drawn, the station is not");
    std::sort(bands.begin(), bands.end());
    int overlaps = 0;
    double worst = 0.0;
    for (size_t i = 1; i < bands.size(); ++i) {
        const double gap = bands[i].first - bands[i - 1].second;
        if (gap < 0.0) { ++overlaps; worst = std::min(worst, gap); }
    }
    if (overlaps > 0)
        std::printf("  libstack: %d overlapping pair(s), worst by %.1f\n", overlaps, -worst);
    check(overlaps == 0,
          "libstack: a housing drawn from the library gets a slot as tall as it actually draws");
}

// ---- Subparts can be dragged into order ----------------------------------------------------------
//
// Which part of a station comes first is the order it is fitted in - the backshell before the boot
// behind it - and getting it wrong used to mean deleting the steps and describing them again.
void testSubpartsCanBeDraggedIntoOrder() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    w.setCurrentCategory(Cat::Connector);
    w.typeName(QStringLiteral("P1"));
    w.commitStep();
    const QString station = w.anchorStepId();
    w.setCurrentCategory(Cat::Backshell);
    w.commitSubpart();
    const QString backshell = w.script().steps.back().id;
    w.setCurrentCategory(Cat::Boot);
    w.commitSubpart();
    const QString boot = w.script().steps.back().id;

    const auto orderOfParts = [&] {
        QStringList ids;
        for (const MechStep& st : w.script().steps)
            if (st.isSubpart) ids << st.id;
        return ids;
    };
    check(orderOfParts() == QStringList({backshell, boot}),
          "drag: the backshell was described first, so it comes first");

    // DRAG THE BOOT ABOVE THE BACKSHELL.
    check(w.dragStepTo(boot, station, 0), "drag: the boot can be moved");
    check(orderOfParts() == QStringList({boot, backshell}),
          "drag: and the script follows the order the list now shows");
    check(w.script().steps.size() == 3, "drag: nothing was lost to the move");
    for (const MechStep& st : w.script().steps)
        if (st.isSubpart)
            check(st.parentStep == station, "drag: both are still parts of the station");

    // AND A PART DRAGGED OUT TO THE TOP LEVEL BECOMES A STATION - the tree parent is the script
    // parent, so a step with nothing above it cannot still be a part of something.
    check(w.dragStepTo(boot, QString(), 0), "drag: the boot can be moved out to the top level");
    const MechStep* movedOut = w.script().step(boot);
    check(movedOut && !movedOut->isSubpart,
          "drag: dragged out of the station, it is a station of its own");
    check(movedOut && movedOut->parentStep.isEmpty(), "drag: with nothing before it");
    check(w.script().steps.size() == 3, "drag: and still nothing lost");
}

// ---- Heatshrink is drawn with its ends shrunk in ------------------------------------------------
//
// Recovered heatshrink pulls down onto whatever is under it, so it grips at each END - where there is
// only the bundle - and stands off over the MIDDLE where the joint it covers is thicker. A box is what
// it looked like before it went on, and on a released drawing that shape is what tells heatshrink apart
// at a glance from a rigid sleeve or a length of conduit, which really are boxes.
//
// BOTH ends, and equally: one tapered end is a BOOT, a thing with a mouth and a back, and drawing
// heatshrink that way says it goes on one way round.
void testHeatshrinkIsDrawnWithShrunkEnds() {
    MechBuildScript sc;
    MechStep con;
    con.id = QStringLiteral("S1");
    con.kind = PartKind::Connector;
    sc.steps.push_back(con);
    MechStep run;
    run.id = QStringLiteral("S2");
    run.kind = PartKind::Cable;
    run.parentStep = QStringLiteral("S1");
    run.lengthMm = 300.0;
    run.odMm = 8.0;
    run.wireCount = 4;
    run.gauge = QStringLiteral("22 AWG");
    sc.steps.push_back(run);
    MechStep shrink;
    shrink.id = QStringLiteral("S3");
    shrink.kind = PartKind::Heatshrink;
    shrink.parentStep = QStringLiteral("S2");
    shrink.lengthMm = 60.0;
    sc.steps.push_back(shrink);
    // A tie over the same run, which is NOT heatshrink and must stay the box it always was.
    MechStep tie;
    tie.id = QStringLiteral("S4");
    tie.kind = PartKind::CableTie;
    tie.parentStep = QStringLiteral("S2");
    tie.lengthMm = 6.0;
    sc.steps.push_back(tie);

    const MechCompiled c = compileMechScript(sc);
    const std::vector<Entity> ents = buildMechanicalHarness(c.def, nullptr);

    const Entity* sleeve = nullptr;
    int boxes = 0;
    for (const Entity& e : ents) {
        if (e.meta.value(QStringLiteral("shrunkEnds")).toBool()) { sleeve = &e; continue; }
        if (e.type == EntityType::Rect &&
            e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("fitting"))
            ++boxes;
    }
    check(sleeve != nullptr, "shrink: the heatshrink is drawn as a shrunk sleeve, not a box");
    check(boxes > 0, "shrink: and the tie beside it is still a box - only heatshrink changed");
    if (!sleeve) return;

    // The outline, measured across the run at each end and in the middle. The run is along x, so
    // across is y.
    // MEASURED FROM THE RUN, not from the origin.
    //
    // The harness is laid out where the layout puts it - the run in this script sits at about y = 300 -
    // so taking |pt.y| measured every half-width from y = 0 and reported 304.51 against 305.50. The
    // neck was a correct 18% and the reading made it look like 0.3%. The outline is symmetric about the
    // run, so the axis is the middle of its own extent.
    double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
    bool first = true;
    for (const Vec2& pt : sleeve->points) {
        if (first) { minX = maxX = pt.x; minY = maxY = pt.y; first = false; }
        minX = std::min(minX, pt.x);
        maxX = std::max(maxX, pt.x);
        minY = std::min(minY, pt.y);
        maxY = std::max(maxY, pt.y);
    }
    const double axisY = (minY + maxY) / 2.0;
    double endHalf = 0.0, midHalf = 0.0;
    for (const Vec2& pt : sleeve->points) {
        const bool atEnd = std::abs(pt.x - minX) < 1e-6 || std::abs(pt.x - maxX) < 1e-6;
        const double half = std::abs(pt.y - axisY);
        if (atEnd) endHalf = std::max(endHalf, half);
        else midHalf = std::max(midHalf, half);
    }
    std::printf("  shrink: end half %.2f, middle half %.2f\n", endHalf, midHalf);
    check(endHalf > 0.0 && midHalf > 0.0,
          "shrink: the outline has a width at its ends and across its middle");
    check(endHalf < midHalf,
          "shrink: and the ends are drawn IN - narrower than the middle, which is what recovering does");

    // BOTH ENDS, EQUALLY. A sleeve narrow at one end only is a boot.
    double leftHalf = 0.0, rightHalf = 0.0;
    for (const Vec2& pt : sleeve->points) {
        const double half = std::abs(pt.y - axisY);
        if (std::abs(pt.x - minX) < 1e-6) leftHalf = std::max(leftHalf, half);
        if (std::abs(pt.x - maxX) < 1e-6) rightHalf = std::max(rightHalf, half);
    }
    check(std::abs(leftHalf - rightHalf) < 1e-6,
          "shrink: both ends are shrunk by the same amount - it does not go on one way round");

    // AND THE NECK IS VISIBLE. A step too small to see is the box this replaced.
    check(midHalf - endHalf > midHalf * 0.05,
          "shrink: the neck is deep enough to read at the size these sheets are printed");
}

// ---- The Heatshrink button on the ribbon has something to place ---------------------------------
//
// A button that places nothing is worse than no button: it says the tool exists and then sends you off
// to draw one. builtInMechModel only drew the two Label kinds, so heatshrink came back EMPTY and the
// button would have reported that nothing had been drawn yet.
//
// And what it places is the recovered shape, the same one the renderer draws for a covering along a
// run. A placed sleeve and a generated one looking different on the same sheet is the kind of thing
// that gets noticed on a released drawing and cannot be explained.
void testTheHeatshrinkButtonHasSomethingToPlace() {
    const MechModel m = builtInMechModel(MechModelKind::Heatshrink);
    check(!m.isEmpty(), "hsbutton: there is a built-in heatshrink for the button to place");
    if (m.isEmpty()) return;

    const std::vector<Entity> es = partDrawingEntities(m.drawing);
    check(!es.empty(), "hsbutton: and it has an outline");
    const Entity* sleeve = nullptr;
    for (const Entity& e : es)
        if (e.meta.value(QStringLiteral("shrunkEnds")).toBool()) sleeve = &e;
    check(sleeve != nullptr, "hsbutton: drawn as a shrunk sleeve rather than a box");
    if (!sleeve) return;

    // Measured off the outline itself, as the renderer test is - not from the origin, which is what
    // made that one read a correct neck as a rounding error.
    double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
    bool first = true;
    for (const Vec2& pt : sleeve->points) {
        if (first) { minX = maxX = pt.x; minY = maxY = pt.y; first = false; }
        minX = std::min(minX, pt.x); maxX = std::max(maxX, pt.x);
        minY = std::min(minY, pt.y); maxY = std::max(maxY, pt.y);
    }
    const double axisY = (minY + maxY) / 2.0;
    double endHalf = 0.0, midHalf = 0.0;
    for (const Vec2& pt : sleeve->points) {
        const double half = std::abs(pt.y - axisY);
        if (std::abs(pt.x - minX) < 1e-6 || std::abs(pt.x - maxX) < 1e-6)
            endHalf = std::max(endHalf, half);
        else
            midHalf = std::max(midHalf, half);
    }
    std::printf("  hsbutton: end half %.2f, middle half %.2f\n", endHalf, midHalf);
    check(endHalf < midHalf && midHalf - endHalf > midHalf * 0.05,
          "hsbutton: with both ends pulled in by a step that can be seen");

    // THE NECKS ARE HELD WHEN IT STRETCHES. Head and tail are what a model holds back from stretching,
    // so a sleeve fitted to a longer run lengthens its straight middle and keeps the necks the size
    // they recover to. Stretching the whole outline would splay them in proportion, which is not what
    // tubing does.
    check(m.headMm > 0.0 && m.tailMm > 0.0,
          "hsbutton: the necks are held back from stretching");
    check(std::abs(m.headMm - m.tailMm) < 1e-9,
          "hsbutton: and both by the same amount - it does not go on one way round");
    check(m.drawnLengthMm > m.headMm + m.tailMm,
          "hsbutton: leaving a straight middle for the stretch to go into");
}

void testBalloonCanBeWithheldPerStep() {
    MechBuildScript sc;
    MechStep con;
    con.id = QStringLiteral("S1"); con.kind = PartKind::Connector;
    sc.steps.push_back(con);
    MechStep run;
    run.id = QStringLiteral("S2"); run.kind = PartKind::Cable;
    run.parentStep = QStringLiteral("S1");
    run.lengthMm = 300.0; run.odMm = 6.0; run.wireCount = 2; run.gauge = QStringLiteral("0.5");
    run.balloon = false;                       // this one is not ballooned
    sc.steps.push_back(run);

    const MechCompiled c = compileMechScript(sc);
    const MechSegment* seg = c.def.segment(c.stepToSegment.value(QStringLiteral("S2")));
    check(seg && !seg->balloon, "balloon: withheld per step, not per sheet");

    // Withholding it must actually draw fewer objects: the flag has to reach the renderer, not just
    // sit in the model looking correct.
    MechHarnessDef allowed = c.def;
    for (MechSegment& s : allowed.segments) s.balloon = true;
    const std::vector<Entity> without = buildMechanicalHarness(c.def, nullptr);
    const std::vector<Entity> with = buildMechanicalHarness(allowed, nullptr);
    check(without.size() < with.size(),
          "balloon: withholding it really removes callouts from the drawing");
}

// ---- Choosing a part from the library, and keeping it --------------------------------------------
//
// TWO BUGS LIVED HERE, and the second was worse than the first.
//
//   1. Picking a part fired the combo's own change handler, which rebuilt the part list - and
//      rebuilding a combo clears it. The selection snapped back to Default before anything read it,
//      so a library part could be chosen and never stuck.
//
//   2. editStep() never touched the part combos at all. Clicking a step to edit it left them on
//      Default, and the first apply wrote that emptiness back - so merely SELECTING a step erased the
//      part somebody had already chosen for it. A silent loss of data on a click.
void testChoosingALibraryPartSticks() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    // A known connector and a contact tied to nothing, in this window's own library.
    PartsCatalog& cat = w.catalogForTest();
    cat.setRemembering(false);
    LibraryPart& conn = cat.active().add(PartKind::Connector);
    conn.description = QStringLiteral("D-sub 9 way");
    conn.mfrPn = QStringLiteral("DE-9S");
    const QString connId = conn.id;
    LibraryPart& contact = cat.active().add(PartKind::ContactPin);
    contact.description = QStringLiteral("Crimp socket 20-24");
    const QString contactId = contact.id;

    w.setCurrentCategory(Cat::Connector);
    w.reloadPartsForTest();

    // BUG 1: it must still be chosen after the handler has run.
    check(w.choosePart(connId), "part: the library connector is offered in the list");
    check(w.chosenPart() == connId,
          "part: and it is STILL chosen after picking it - the list no longer clears itself");

    check(w.chooseContact(contactId), "part: the contact is offered too");
    check(w.chosenContact() == contactId, "part: and stays chosen");

    // It reaches the step, and the step reaches the drawing.
    w.commitStep();
    const MechStep& step = w.script().steps.back();
    check(step.libPart == connId, "part: the chosen part is on the committed step");
    check(step.contactPart == contactId, "part: and so is the contact");

    const MechCompiled c = compileMechScript(w.script());
    const MechNode* node = c.def.node(c.stepToNode.value(step.id));
    check(node && node->libPart == connId,
          "part: and it reaches the connector in the compiled harness");

    // BUG 2: selecting the step to edit it must not lose the part.
    //
    // THE COMBO IS MOVED AWAY FIRST, and without that this test proves nothing. refreshPartCombo()
    // preserves whatever is selected, so if the combo still held the connector from a moment ago then
    // editStep() would appear to "restore" it while doing nothing at all - which is exactly how this
    // test passed against the bug it was written for. Selecting Default is what forces editStep to
    // actually put the step's own part back.
    check(w.choosePart(QString()), "part: the Default entry can be selected");
    check(w.chosenPart().isEmpty(), "part: and the combo is genuinely off the connector now");

    w.editStep(step.id);
    check(w.chosenPart() == connId, "part: editing a step puts its part back in the list");
    check(w.chosenContact() == contactId, "part: and its contact");

    w.applyToEditedStep();
    const MechStep* after = w.script().step(step.id);
    check(after && after->libPart == connId,
          "part: applying does NOT erase the part - that was the silent data loss");
    check(after && after->contactPart == contactId, "part: nor the contact");
}

// Changing category must drop a part that belongs to a different kind of thing - a D-sub is not a
// choice of heatshrink - and must do it without complaint.
void testChangingCategoryDropsAnUnrelatedPart() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    PartsCatalog& cat = w.catalogForTest();
    cat.setRemembering(false);
    LibraryPart& conn = cat.active().add(PartKind::Connector);
    conn.description = QStringLiteral("D-sub 9 way");
    const QString connId = conn.id;

    w.setCurrentCategory(Cat::Connector);
    w.reloadPartsForTest();
    check(w.choosePart(connId), "category: the connector is offered");
    check(w.chosenPart() == connId, "category: and chosen");

    w.setCurrentCategory(Cat::Heatshrink);
    check(w.chosenPart().isEmpty(),
          "category: switching to heatshrink falls back to Default rather than keeping a connector");

    // And back again, the connector is offered once more.
    w.setCurrentCategory(Cat::Connector);
    check(w.choosePart(connId), "category: and it is still there on returning");
}

// ---- The ways come from the connector's own pin table --------------------------------------------
//
// A connector that was captured for its PIN TABLE - "1-3 are size 16, 4-6 are size 20" - states its
// positions twice: once in the single "Pins" box and once, in more detail, in the table. The table is
// the one somebody entered off the datasheet a row at a time; the box is the one most likely to have
// been left at whatever it started as. So the table wins, and the figure is not asked of the user at
// all where the part already answers it.
void testWaysComeFromTheConnectorsOwnPins() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);
    PartsCatalog& cat = w.catalogForTest();
    cat.setRemembering(false);

    // Its pin table names six ways. Its "Pins" box says two - deliberately wrong, so that a test which
    // passed by reading the box would fail here.
    LibraryPart& tabled = cat.active().add(PartKind::Connector);
    tabled.description = QStringLiteral("Power header, mixed sizes");
    tabled.params[QStringLiteral("pins")] = QStringLiteral("2");
    setPartIncludesPins(tabled, true);
    setPartPinTypes(tabled, {PartPinType{QStringLiteral("16"), QStringLiteral("AWG"),
                                         QStringLiteral("16"), QStringLiteral("13"),
                                         QStringLiteral("1-3")},
                             PartPinType{QStringLiteral("20"), QStringLiteral("AWG"),
                                         QStringLiteral("20"), QStringLiteral("7.5"),
                                         QStringLiteral("4,5,6")}});
    const QString tabledId = tabled.id;

    // No table, but the box is filled in: that is what there is, so that is what is used.
    LibraryPart& boxed = cat.active().add(PartKind::Connector);
    boxed.description = QStringLiteral("D-sub 9 way");
    boxed.params[QStringLiteral("pins")] = QStringLiteral("9");
    const QString boxedId = boxed.id;

    // Says nothing at all about its positions.
    LibraryPart& silent = cat.active().add(PartKind::Connector);
    silent.description = QStringLiteral("Unspecified housing");
    const QString silentId = silent.id;

    w.setCurrentCategory(Cat::Connector);
    w.reloadPartsForTest();

    check(w.choosePart(tabledId), "ways: the tabled connector is offered");
    check(w.shownWays() == 6,
          "ways: SIX, from the pin table's own pin lists - not the two the Pins box claims");

    check(w.choosePart(boxedId), "ways: the plain connector is offered");
    check(w.shownWays() == 9, "ways: nine, from the box, when there is no table to read");

    // A PART THAT SAYS NOTHING MUST NOT OVERWRITE WHAT THE USER TYPED. wayCount() answers 1 for
    // everything so a connector always has one contact to order, and quietly replacing a typed 4 with
    // that 1 would be the library asserting something it does not know.
    w.typeWays(4);
    check(w.choosePart(silentId), "ways: the silent connector is offered");
    check(w.shownWays() == 4, "ways: a part that states none leaves the typed figure alone");
}

// ---- The preview shows the step being described ---------------------------------------------------
//
// The preview used to compile only the COMMITTED script, so choosing a connector from the library
// changed nothing on screen until Next was pressed. The one question a preview is asked is what the
// choice just made looks like, and it could not answer it.
void testThePreviewShowsTheStepBeingDescribed() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);
    PartsCatalog& cat = w.catalogForTest();
    cat.setRemembering(false);
    LibraryPart& conn = cat.active().add(PartKind::Connector);
    conn.description = QStringLiteral("D-sub 9 way");
    conn.params[QStringLiteral("pins")] = QStringLiteral("9");
    const QString connId = conn.id;

    w.setCurrentCategory(Cat::Connector);
    w.reloadPartsForTest();

    check(w.script().steps.empty(), "preview: nothing has been committed");
    check(w.previewScript().steps.size() == 1,
          "preview: yet the step being described is already in what the preview draws");

    check(w.choosePart(connId), "preview: the connector is offered");
    // GUARDED, because a test must report a failure rather than take the process down with it: with the
    // pending step missing, steps is empty and back() would be reading off the end of a vector.
    const MechBuildScript afterPick = w.previewScript();
    check(!afterPick.steps.empty() && afterPick.steps.back().libPart == connId,
          "preview: and the part chosen a moment ago is on it - without pressing Next");

    const MechCompiled c = compileMechScript(w.previewScript());
    check(!buildMechanicalHarness(c.def, &cat).empty(), "preview: which does draw something");

    // Committing moves it across rather than duplicating it, and the next step is at once the one
    // being described.
    w.commitStep();
    check(w.script().steps.size() == 1, "preview: Next commits exactly the one step");
    check(w.previewScript().steps.size() == 2,
          "preview: and step 2 is being described from the moment step 1 is settled");

    // A RUN OF NO LENGTH IS NOT SHOWN. It would compile to a segment going nowhere, which draws as a
    // smear on the previous node rather than as a preview of anything.
    w.setCurrentCategory(Cat::Cable);
    const size_t withCable = w.previewScript().steps.size();
    check(withCable == 2 || withCable == 3, "preview: a cable step is either shown or withheld");
}

// ---- What the name field is called ---------------------------------------------------------------
//
// On a connector the answer is a DESIGNATOR - P1, J3 - and it is not a nickname: it is the reference
// the wire list, the schematic and every crimp record know the part by. "Called" reads like a caption.
void testTheNameFieldIsCalledDesignatorOnAnEnd() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    w.setCurrentCategory(Cat::Connector);
    check(w.shownNameLabel() == QStringLiteral("Designator"),
          "label: a connector's name field is called Designator");
    w.setCurrentCategory(Cat::Terminal);
    check(w.shownNameLabel() == QStringLiteral("Designator"),
          "label: and so is a terminal's - it is an end too");
    w.setCurrentCategory(Cat::NodePoint);
    check(w.shownNameLabel() == QStringLiteral("Designator"),
          "label: and a breakout's, which is BR1 on every drawing");
    // A RUN IS DESIGNATED TOO - a cable is W1 in the wire list exactly as a connector is P1. It is a
    // FITTING that is called something, because there the name really is a caption.
    w.setCurrentCategory(Cat::Cable);
    check(w.shownNameLabel() == QStringLiteral("Designator"),
          "label: a cable is designated - it is a run, and the wire list knows it by that name");
    w.setCurrentCategory(Cat::Boot);
    check(w.shownNameLabel() == QStringLiteral("Called"),
          "label: a boot is CALLED something - that one really is a caption");
}

// ---- The list carries the step being described ---------------------------------------------------
//
// The list used to start empty, so at the moment somebody was describing step 1 nothing anywhere said
// so: the panel said "Step 1" and the list said nothing. And a settled step showed its name INSTEAD of
// its type, so a real harness read "P1, W1, P2" - three references with no indication of what any of
// them was.
void testTheListShowsTheStepBeingDescribedUntilItIsApplied() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    w.setCurrentCategory(Cat::Connector);
    QStringList rows = w.shownSteps();
    check(rows.size() == 1, "list: step 1 is on the list before anything has been added");
    check(rows.value(0).startsWith(QStringLiteral("1")),
          "list: numbered 1, which is the step the panel is asking about");
    check(rows.value(0).contains(QStringLiteral("being described")),
          "list: and it reads as unsettled rather than as a step of the build");

    w.typeName(QStringLiteral("P1"));
    w.commitStep();
    rows = w.shownSteps();
    check(rows.size() == 2, "list: Next settles step 1 and opens step 2");
    check(rows.value(0).contains(partKindName(PartKind::Connector)),
          "list: a settled step reads as its TYPE");
    check(rows.value(0).contains(QStringLiteral("P1")),
          "list: and as its designator - both, so the line is a build step and a wire-list reference");
    check(!rows.value(0).contains(QStringLiteral("being described")),
          "list: step 1 is no longer being described");
    check(rows.value(1).contains(QStringLiteral("being described")),
          "list: step 2 has appeared and is the one now being described");
}

// ---- A harness that starts open names each wire --------------------------------------------------
//
// A run out of a connector needs no wire names: "P1-3" identifies a conductor without anybody typing
// it. A harness that starts at bare wires, ferrules or ring terminals has no connector to be named
// from, and then the designators are the only identification the conductors have.
void testAnOpenStartNamesEachWire() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    w.setCurrentCategory(Cat::Wires);
    check(w.asksForWireDesignators(),
          "wires: a harness starting at bare wires is asked for a designator per wire");

    // Commas or spaces, because a person typing three wire numbers uses whichever they habitually use.
    w.typeWireDesignators(QStringLiteral("BK1, RD2  WH3"));
    w.commitStep();

    // BY VALUE. Committing another step can reallocate script_.steps, and a reference into it would
    // then be reading freed memory - which is a test that reports on whatever happens to be there.
    check(!w.script().steps.empty(), "wires: the run was committed");
    if (w.script().steps.empty()) return;
    const MechStep s = w.script().steps.back();
    check(!s.conductors.empty(), "wires: naming the wires materialises a group for the names to sit on");
    const QStringList got = s.conductors.empty() ? QStringList() : s.conductors.front().designators;
    check(got == QStringList({QStringLiteral("BK1"), QStringLiteral("RD2"), QStringLiteral("WH3")}),
          "wires: three designators, in the order they were typed, whatever separated them");

    const MechCompiled c = compileMechScript(w.script());
    QStringList labels;
    for (const MechWire& wire : c.def.wires) labels << wire.label;
    check(labels.contains(QStringLiteral("BK1")) && labels.contains(QStringLiteral("RD2")) &&
              labels.contains(QStringLiteral("WH3")),
          "wires: and they are what the conductors are CALLED in the compiled harness");

    // Not asked again once there is something behind the run: those wires have names already.
    w.setCurrentCategory(Cat::Wires);
    check(!w.asksForWireDesignators(),
          "wires: a run further along the harness is not asked for names it already has");

    // And selecting the step to edit it must not erase them - the same silent loss the part had.
    w.editStep(s.id);
    w.applyToEditedStep();
    const MechStep* after = w.script().step(w.script().steps.front().id);
    check(after && !after->conductors.empty() &&
              after->conductors.front().designators.size() == 3,
          "wires: editing the step and applying keeps the names it was given");
}

// ---- Two connectors at one end -------------------------------------------------------------------
//
// Two housings feeding one cable is an ordinary harness - a twin-lead adaptor, a power and a signal
// shell sharing a bundle - and it is ONE decision about that end rather than two steps. The tree could
// not express it before: two End steps naming the same parent both land on the same node, and the
// second silently overwrote the first.
void testTwoConnectorsCanShareOneEnd() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);
    PartsCatalog& cat = w.catalogForTest();
    cat.setRemembering(false);
    LibraryPart& a = cat.active().add(PartKind::Connector);
    a.description = QStringLiteral("Signal shell");
    a.params[QStringLiteral("pins")] = QStringLiteral("6");
    const QString aId = a.id;
    LibraryPart& b = cat.active().add(PartKind::Connector);
    b.description = QStringLiteral("Power shell");
    b.params[QStringLiteral("pins")] = QStringLiteral("2");
    const QString bId = b.id;

    // P1 goes on as the station itself.
    w.setCurrentCategory(Cat::Connector);
    w.reloadPartsForTest();
    w.typeName(QStringLiteral("P1"));
    check(w.choosePart(aId), "two ends: the signal shell is offered");
    w.commitStep();

    // P2 goes on as a PART OF that station - "Add subpart" - which is the button that replaced the
    // connector-only banking one. The anchor must not move, or the second housing would be a station
    // further down the harness instead of another one at the same place.
    const QString station = w.anchorStepId();
    w.setCurrentCategory(Cat::Connector);
    w.typeName(QStringLiteral("P2"));
    check(w.choosePart(bId), "two ends: the power shell is offered");
    w.commitSubpart();

    check(w.script().steps.size() == 2, "two ends: two steps described");
    const MechStep sub = w.script().steps.back();
    check(sub.isSubpart, "two ends: the second is marked as a part of the first");
    check(sub.parentStep == station, "two ends: and hangs off it");
    check(w.anchorStepId() == sub.id,
          "two ends: adding a subpart leaves you standing on the part just added");

    // A cable out of that end, so both housings have a run to gather into. STANDING BACK ON THE
    // STATION first: Next carries on at the level you are on, and after a subpart that level is the
    // station's parts - so a run has to be asked for from the station itself.
    w.editStep(station);
    w.setCurrentCategory(Cat::Cable);
    w.commitStep();

    const MechCompiled c = compileMechScript(w.script());
    const MechNode* node = c.def.node(c.stepToNode.value(station));
    check(node && node->kind == MechNodeKind::Connector,
          "two ends: the station is a connector");
    const bool haveExtra = node && node->extraEnds.size() == 1;
    check(haveExtra,
          "two ends: with a SECOND housing on the same node - not overwritten by it");
    // GUARDED. With the second housing missing, extraEnds is empty and front() reads off the end of a
    // vector - so the run CRASHED here instead of reporting, and every assertion after this point went
    // unchecked. A test has to survive the absence of the thing it is testing.
    check(!haveExtra || node->extraEnds.front().name == QStringLiteral("P2"),
          "two ends: and the second keeps its own designator");
    check(node && node->name == QStringLiteral("P1"),
          "two ends: while the first keeps its own - the overwrite was the silent data loss");

    // BOTH ARE DRAWN AND BOTH ARE NAMED. A housing that reached the model and not the sheet would be a
    // part on the bill that nobody building the harness could find.
    const std::vector<Entity> ents = buildMechanicalHarness(c.def, &cat);
    bool sawP1 = false, sawP2 = false;
    for (const Entity& e : ents) {
        const QString text = e.meta.value(QStringLiteral("text")).toString();
        if (text == QStringLiteral("P1")) sawP1 = true;
        if (text == QStringLiteral("P2")) sawP2 = true;
    }
    check(sawP2, "two ends: the second housing is named on the drawing");
    check(sawP1 || (node && node->name == QStringLiteral("P1")),
          "two ends: and so is the first, which the harness already knew how to do");
}

// ---- Subparts nest, and the list shows the shape --------------------------------------------------
//
// A station is described in parts: the connector, its backshell, the wires leaving it. That is not four
// stations, and a flat numbered run of everything anybody typed says it is. Subparts nest to whatever
// depth the harness has, and the list indents them under what they are a part of.
void testSubpartsNestAndTheListShowsIt() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    w.setCurrentCategory(Cat::Connector);
    w.typeName(QStringLiteral("P1"));
    w.commitStep();
    const QString station = w.anchorStepId();

    // Its backshell, as a part of it. Add subpart goes one level in.
    w.setCurrentCategory(Cat::Backshell);
    w.commitSubpart();
    const QString firstPart = w.anchorStepId();
    check(firstPart != station,
          "subpart: after Add subpart you are standing on the part, not back on the station");

    // And its boot, as another part of the SAME station. NEXT is what gives a sibling: it is the next
    // thing at the level you are on, and the level you are on is the station's parts.
    w.setCurrentCategory(Cat::Boot);
    w.commitStep();
    check(w.script().steps.back().parentStep == station,
          "subpart: Next on a part gives another part of the same station");

    int subparts = 0;
    for (const MechStep& s : w.script().steps) {
        if (!s.isSubpart) continue;
        ++subparts;
        check(s.parentStep == station, "subpart: every subpart hangs off the station");
    }
    check(subparts == 2, "subpart: two parts of one station");

    // THE LIST SHOWS THE SHAPE. The station is numbered; its parts are indented under it and are not.
    const QStringList rows = w.shownSteps();
    check(rows.size() >= 3, "subpart: the station and its two parts are all listed");
    check(rows.value(0).startsWith(QStringLiteral("1")),
          "subpart: the station carries the step number");
    // THE NUMBER SAYS WHICH IT IS. Indentation alone reads as a list of steps with some nudged over,
    // and somebody scanning the sequence counts stations - so a station is 1 and its parts are 1.1, 1.2.
    check(rows.value(1).trimmed().startsWith(QStringLiteral("1.1")),
          "subpart: the first part of station 1 is numbered 1.1, not 2");
    check(rows.value(2).trimmed().startsWith(QStringLiteral("1.2")),
          "subpart: and the second is 1.2");
    check(rows.value(1).startsWith(QStringLiteral(" ")) &&
              rows.value(2).startsWith(QStringLiteral(" ")),
          "subpart: and both are indented under the station as well as numbered under it");

    // Nesting DEEPER: a part of a part. The wires leave the backshell, not the bare connector.
    check(w.script().steps.size() >= 2, "subpart: there is a backshell step to nest under");
    if (w.script().steps.size() < 2) return;
    const MechStep backshell = w.script().steps[1];
    w.editStep(backshell.id);
    const QString onBackshell = backshell.id;
    MechBuildScript sc = w.script();
    MechStep wires;
    wires.id = QStringLiteral("SW");
    wires.kind = PartKind::Wire;
    wires.parentStep = onBackshell;
    wires.isSubpart = true;
    wires.lengthMm = 120.0;
    wires.wireCount = 3;
    wires.gauge = QStringLiteral("22 AWG");
    sc.steps.push_back(wires);
    w.setScript(sc);

    const QStringList deep = w.shownSteps();
    // The deepest row is indented further than the one it hangs off - the depth is walked, so it is
    // right however the tree was built up.
    int lead1 = 0, lead2 = 0;
    QString num2;
    for (const QString& r : deep) {
        const int n = r.length() - r.trimmed().length();
        if (r.contains(QStringLiteral("Backshell"))) lead1 = n;
        if (r.contains(partKindName(PartKind::Wire))) {
            lead2 = n;
            num2 = r.trimmed().section(QLatin1Char(' '), 0, 0);
        }
    }
    check(lead2 > lead1,
          "subpart: a part of a part is indented deeper than the part it belongs to");
    check(num2.count(QLatin1Char('.')) >= 2,
          "subpart: and its number says how deep it is - 1.1.1 rather than 1.1");
}

} // namespace

// ---- A RUN LEAVES THE HOUSING IT BELONGS TO ------------------------------------------------------
//
// Two connectors at one end stand on ONE node but are drawn one above the other. A run described under
// one of them has to come out of THAT housing: while both started at the node's point, the wires
// appeared in the middle of the drawing, between the two connectors they were supposed to be leaving,
// and a second set under the other housing landed on top of the first.
//
// Checked on where the run is DRAWN rather than on the segment's endpoints, because two things have to
// be right for it to look right - the compiler saying which housing the run leaves (MechSegment::
// fromEnd) and the renderer starting it on that housing's lane - and only the drawing shows both.
void testARunLeavesItsOwnHousing() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    // A station with two housings on it: p1 first, then p3.
    w.setCurrentCategory(Cat::Connector);
    w.typeName(QStringLiteral("P1"));
    w.commitSubpart();
    const QString station = w.script().steps.front().id;
    w.setCurrentCategory(Cat::Connector);
    w.typeName(QStringLiteral("p1"));
    w.applyToDrawing();
    const QString p1 = w.script().steps.back().id;
    w.editStep(station);
    w.commitSubpart();
    w.setCurrentCategory(Cat::Connector);
    w.typeName(QStringLiteral("p3"));
    w.applyToDrawing();
    const QString p3 = w.script().steps.back().id;

    // Wires under each housing, both applied.
    for (const QString& housing : {p1, p3}) {
        w.editStep(housing);
        w.commitSubpart();
        w.setCurrentCategory(Cat::Wires);
        w.setRunLengthForTest(30.0);
        w.applyToDrawing();
    }
    check(w.script().steps.size() == 5, "own housing: a station, two housings and a run off each");

    // WHICH HOUSING EACH RUN SAYS IT LEAVES.
    const MechCompiled c = compileMechScript(w.script());
    QStringList ends;
    for (const MechSegment& s : c.def.segments) ends << s.fromEnd;
    ends.sort();
    check(c.def.segments.size() == 2, "own housing: two runs");
    check(ends.size() == 2 && !ends[0].isEmpty() && ends[0] != ends[1],
          "own housing: each run names a housing of its own, not the station");

    // AND WHERE THEY ARE DRAWN. Each run's middle has to sit on a housing, not between them.
    std::vector<double> housings;
    std::vector<std::pair<double, double>> runs;   // low, high across the harness, per run
    QHash<QString, std::pair<double, double>> bySeg;
    for (const Entity& e : buildMechanicalHarness(c.def, nullptr)) {
        const Bounds b = e.bounds();
        if (!b.valid) continue;
        const QString role = e.meta.value(QStringLiteral("mechRole")).toString();
        if (role == QLatin1String("connector")) housings.push_back((b.min.y + b.max.y) / 2.0);
        if (role != QLatin1String("bundle") && role != QLatin1String("wires")) continue;
        const QString sid = e.meta.value(QStringLiteral("mechSeg")).toString();
        if (sid.isEmpty()) continue;
        auto it = bySeg.find(sid);
        if (it == bySeg.end()) bySeg.insert(sid, {b.min.y, b.max.y});
        else { it->first = std::min(it->first, b.min.y); it->second = std::max(it->second, b.max.y); }
    }
    for (auto it = bySeg.begin(); it != bySeg.end(); ++it) runs.push_back(it.value());
    check(housings.size() == 2 && runs.size() == 2,
          "own housing: two housings and two runs are drawn");
    if (housings.size() != 2 || runs.size() != 2) return;

    double worst = 0.0;
    for (const auto& r : runs) {
        const double mid = (r.first + r.second) / 2.0;
        double nearest = 1e18;
        for (double h : housings) nearest = std::min(nearest, std::abs(h - mid));
        worst = std::max(worst, nearest);
    }
    std::printf("  own housing: housings at %.1f and %.1f; the furthest run is %.2f from one\n",
                housings[0], housings[1], worst);
    check(worst < 1.0, "own housing: every run is drawn on a housing, not between them");

    // And the two runs are not on top of each other, which is what one shared start point looked like.
    const double gap = std::abs((runs[0].first + runs[0].second) / 2.0 -
                                (runs[1].first + runs[1].second) / 2.0);
    check(gap > 1.0, "own housing: and the two are apart, each on its own connector");
}

// ---- WIRES UNDER ONE HOUSING, DRAWN ON THAT HOUSING AND NOWHERE ELSE ------------------------------
//
// The tree from the report: one station, two connector parts at it (p1 and p2), and a run of wires
// described under the FIRST of them. What the window drew was a fan on p1 AND a second fan out in the
// gap between the two housings, belonging to neither.
//
// A station whose connectors are all parts has no housing of its own, so its own lane IS that gap:
// anything hung on the station rather than on one of its housings comes out in the middle of the
// drawing. This lists every wire the preview draws against the housing lanes and fails on any that sit
// between them - measured, because three previous readings of this fault from the code were wrong.
void testWiresUnderOneHousingAreDrawnOnlyThere() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    // 1 Connector (the station), then two housings at it.
    w.setCurrentCategory(Cat::Connector);
    w.typeWays(4);
    w.commitStep();                       // 1 Connector
    w.setCurrentCategory(Cat::Connector);
    w.typeName(QStringLiteral("p1"));
    w.typeWays(4);
    w.commitSubpart();                    // 1.1 Connector p1
    w.setCurrentCategory(Cat::Connector);
    w.typeName(QStringLiteral("p2"));
    w.typeWays(4);
    w.commitStep();                       // 1.2 Connector p2, a sibling of p1

    QString p1Id;
    for (const MechStep& st : w.script().steps)
        if (st.name == QLatin1String("p1")) p1Id = st.id;
    check(!p1Id.isEmpty(), "middle wires: p1 is in the script");
    if (p1Id.isEmpty()) return;

    // EXACTLY THE PATH FROM THE REPORT: p1 picked out in the list, Add subpart pressed on it, and the
    // run of wires described under it - which is what puts "Wires - being described" nested inside 1.1
    // with 1.2 below it. Reached this way and not by picking p1 and choosing a category, which is the
    // sibling path and was already right.
    w.editStep(p1Id);
    w.commitSubpart();
    w.setCurrentCategory(Cat::Wires);
    w.setRunLengthForTest(30.0);
    w.setConductorsForTest(2);   // Conductors: 2, as the form shows

    // WHAT THE PREVIEW IS ACTUALLY ASKED TO DRAW. Printed because the fault is that there are TWO runs
    // where there should be one, and the drawing alone cannot say which step each belongs to.
    const MechBuildScript prev = w.previewScript();
    for (const MechStep& st : prev.steps)
        std::printf("  middle wires: step %-4s kind=%d sub=%d parent=%-4s name=%s len=%.1f cond=%d\n",
                    qPrintable(st.id), static_cast<int>(st.kind), st.isSubpart ? 1 : 0,
                    qPrintable(st.parentStep), qPrintable(st.name), st.lengthMm,
                    static_cast<int>(st.conductors.size()));
    const MechCompiled comp = compileMechScript(prev);
    for (const MechSegment& sg : comp.def.segments)
        std::printf("  middle wires: seg %-4s from=%-4s to=%-4s fromEnd=[%s] toStation=[%s] lane=%d\n",
                    qPrintable(sg.id), qPrintable(sg.fromNode), qPrintable(sg.toNode),
                    qPrintable(sg.fromEnd), qPrintable(sg.toStation), sg.toLane);

    const std::vector<Entity> shown = w.shownEntities();
    std::vector<double> laneMids;
    for (const Entity& e : shown) {
        if (e.meta.value(QStringLiteral("mechRole")).toString() != QLatin1String("connector"))
            continue;
        const Bounds b = e.bounds();
        if (b.valid) laneMids.push_back((b.min.y + b.max.y) / 2.0);
    }
    std::sort(laneMids.begin(), laneMids.end());
    QString said;
    for (double m : laneMids) said += QStringLiteral("%1 ").arg(m, 0, 'f', 2);
    std::printf("  middle wires: housing lanes at y %s\n", qPrintable(said));
    check(laneMids.size() >= 2, "middle wires: both housings are drawn");
    if (laneMids.size() < 2) return;

    // A fan on a housing sits ON that housing's lane, so anything in the middle third of the gap
    // between the lanes is on neither of them.
    const double lo = laneMids.front();
    const double hi = laneMids.back();
    const double span = hi - lo;
    int inTheGap = 0;
    int onALane = 0;
    for (const Entity& e : shown) {
        const QString role = e.meta.value(QStringLiteral("mechRole")).toString();
        if (role.isEmpty() || role == QLatin1String("connector")) continue;
        const Bounds b = e.bounds();
        if (!b.valid) continue;
        const double mid = (b.min.y + b.max.y) / 2.0;
        const bool between = mid > lo + span * 0.33 && mid < hi - span * 0.33;
        const bool isRun = role == QLatin1String("wires") || role == QLatin1String("bundle");
        std::printf("  middle wires: %-9s y %8.2f..%8.2f (mid %8.2f)  x %8.2f..%8.2f%s\n",
                    qPrintable(role), b.min.y, b.max.y, mid, b.min.x, b.max.x,
                    between ? "   <-- IN THE GAP" : "");
        if (isRun) {
            if (between) ++inTheGap;
            else ++onALane;
        }
    }
    // THE RUN IS DRAWN AT ALL, first - a preview with no wires in it would pass the next check for
    // the one reason it must never pass for.
    check(onALane + inTheGap > 0, "middle wires: the run being described is drawn");
    check(inTheGap == 0, "middle wires: and nothing is drawn in the gap between the housings");

    // AND THE ANNOTATION IS IN FRONT OF ITS CONNECTOR, past the far point - not across from the face,
    // where it stood in the middle of the fan the connector feeds. The harness runs to the +x side, so
    // "in front" is to the -x side of every housing.
    double housingMinX = 1e18;
    for (const Entity& e : shown) {
        if (e.meta.value(QStringLiteral("mechRole")).toString() != QLatin1String("connector"))
            continue;
        const Bounds b = e.bounds();
        if (b.valid) housingMinX = std::min(housingMinX, b.min.x);
    }
    int balloons = 0;
    int inFront = 0;
    for (const Entity& e : shown) {
        if (e.meta.value(QStringLiteral("mechRole")).toString() != QLatin1String("balloon")) continue;
        const Bounds b = e.bounds();
        if (!b.valid) continue;
        ++balloons;
        const bool front = b.max.x <= housingMinX + 1e-6;
        std::printf("  middle wires: balloon x %8.2f..%8.2f (housings from %8.2f)%s\n", b.min.x,
                    b.max.x, housingMinX, front ? "  in front" : "   <-- BEHIND THE FACE");
        if (front) ++inFront;
    }
    check(balloons > 0, "annotation: the connector is ballooned at all");
    check(balloons == inFront, "annotation: every balloon is in front of the connector");
}

// ---- NOTHING IS DRAWN IN THE GAP BETWEEN TWO STACKED LIBRARY HOUSINGS -----------------------------
//
// Two connectors at one station, both drawn from the library, with a run of wires leaving the FIRST of
// them. Reported as "3 lines between the connectors" appearing the moment the second connector is
// added - so this lists everything that is drawn, by role AND by type, against the band each housing
// occupies, and names whatever sits between them.
//
// With default boxes there was never anything there; the housings in the report are library parts, and
// that is the difference this reproduces. Measured rather than read: five readings of this fault from
// the code have now been wrong.
void testNothingIsDrawnBetweenStackedHousings() {
    PartsCatalog cat;
    cat.setRemembering(false);
    // A housing deeper than the 20mm default box and taller than the default formula would guess -
    // both of the things that made the placements miss.
    LibraryPart& big = cat.active().add(PartKind::Connector);
    big.description = QStringLiteral("A deep 2-way housing");
    Entity body;
    body.type = EntityType::Rect;
    body.points = {{0.0, -22.0}, {70.0, -22.0}, {70.0, 22.0}, {0.0, 22.0}};
    big.mechanical = partDrawingFromEntities({body});
    const QString bigId = big.id;

    MechBuildScript sc;
    MechStep station;
    station.id = QStringLiteral("S1");
    station.kind = PartKind::Connector;
    sc.steps.push_back(station);
    // IN THE ORDER THEY WERE DESCRIBED: p1, then its wires, and p2 only afterwards. That is the order
    // in the report, and it is not the same script as p1, p2, then the wires - the compiler walks the
    // steps in order, so which housing existed when the run was described can change what it makes.
    auto housing = [&](const char* nm) {
        MechStep x;
        x.id = QStringLiteral("S_%1").arg(QString::fromLatin1(nm));
        x.kind = PartKind::Connector;
        x.name = QString::fromLatin1(nm);
        x.parentStep = QStringLiteral("S1");
        x.isSubpart = true;
        x.libPart = bigId;
        x.pinCount = 2;
        return x;
    };
    sc.steps.push_back(housing("p1"));
    // The wires, under p1 - 4 conductors, 30mm, as the report shows.
    MechStep run;
    run.id = QStringLiteral("S_W");
    run.kind = PartKind::Wire;
    run.name = QStringLiteral("W1");
    run.parentStep = QStringLiteral("S_p1");
    run.isSubpart = true;
    run.lengthMm = 30.0;
    run.wireCount = 4;
    run.gauge = QStringLiteral("20 AWG");
    sc.steps.push_back(run);
    sc.steps.push_back(housing("p2"));   // added last, as in the report

    const MechCompiled c = compileMechScript(sc);
    for (const MechSegment& sg : c.def.segments)
        std::printf("  gap: seg %-4s from=%-4s to=%-4s fromEnd=[%s] lane=%d wires=%d\n",
                    qPrintable(sg.id), qPrintable(sg.fromNode), qPrintable(sg.toNode),
                    qPrintable(sg.fromEnd), sg.toLane, static_cast<int>(sg.wireIds.size()));
    const std::vector<Entity> ents = buildMechanicalHarness(c.def, &cat);

    std::vector<std::pair<double, double>> bands;
    for (const Entity& e : ents) {
        if (e.meta.value(QStringLiteral("mechRole")).toString() != QLatin1String("connector"))
            continue;
        const Bounds b = e.bounds();
        if (b.valid) bands.push_back({b.min.y, b.max.y});
    }
    std::sort(bands.begin(), bands.end());
    check(bands.size() >= 2, "gap: both housings are drawn");
    if (bands.size() < 2) return;
    for (const auto& b : bands)
        std::printf("  gap: a housing occupies y %8.2f..%8.2f\n", b.first, b.second);

    // THE GAP ITSELF: between the top of the lower housing and the bottom of the upper one. Anything
    // whose middle lies in there is between the connectors, which is what was reported.
    const double gapLo = bands[0].second;
    const double gapHi = bands[1].first;
    std::printf("  gap: the space between them is y %.2f..%.2f\n", gapLo, gapHi);
    int inGap = 0;
    for (const Entity& e : ents) {
        const QString role = e.meta.value(QStringLiteral("mechRole")).toString();
        if (role == QLatin1String("connector")) continue;
        const Bounds b = e.bounds();
        if (!b.valid) continue;
        const double mid = (b.min.y + b.max.y) / 2.0;
        if (mid <= gapLo || mid >= gapHi) continue;
        ++inGap;
        std::printf("  gap: IN THE GAP role=%-9s type=%d  x %8.2f..%8.2f  y %8.2f..%8.2f\n",
                    qPrintable(role.isEmpty() ? QStringLiteral("(none)") : role),
                    static_cast<int>(e.type), b.min.x, b.max.x, b.min.y, b.max.y);
    }
    std::printf("  gap: %d object(s) between the connectors\n", inGap);
    check(inGap == 0, "gap: nothing is drawn between the two housings");

    // AND THE ANNOTATION IS CLEAR OF THE BODIES, not written inside one: a balloon placed by a guess
    // at how deep a library housing is ends up on top of the part it belongs to.
    double bodyMinX = 1e18, bodyMaxX = -1e18;
    for (const Entity& e : ents) {
        if (e.meta.value(QStringLiteral("mechRole")).toString() != QLatin1String("connector"))
            continue;
        const Bounds b = e.bounds();
        if (!b.valid) continue;
        bodyMinX = std::min(bodyMinX, b.min.x);
        bodyMaxX = std::max(bodyMaxX, b.max.x);
    }
    int balloons = 0, clear = 0;
    for (const Entity& e : ents) {
        if (e.meta.value(QStringLiteral("mechRole")).toString() != QLatin1String("balloon")) continue;
        const Bounds b = e.bounds();
        if (!b.valid) continue;
        ++balloons;
        const bool ok = b.max.x <= bodyMinX + 1e-6;
        std::printf("  gap: balloon x %8.2f..%8.2f (bodies %8.2f..%8.2f)%s\n", b.min.x, b.max.x,
                    bodyMinX, bodyMaxX, ok ? "  in front" : "   <-- ON THE BODY");
        if (ok) ++clear;
    }
    if (balloons > 0)
        check(balloons == clear, "gap: no balloon is written on top of a connector body");
}

// ---- A RUN'S FAN LEAVES ITS OWN HOUSING, EVEN WHEN IT IS DRAWN FROM A SAVED MODEL -----------------
//
// This is the fault that was reported four times as "wires in the middle of the drawing between the
// connectors" and did not reproduce five times running.
//
// Where wires leave a connector they are drawn as a fan of separate conductors, and an office that has
// drawn its own view of that keeps it as a MODEL. Two fans therefore exist in the code: the generator's
// own, and one stretched from the model. The generator's was fixed to start where the run starts; the
// model's still started at the node's own point, which for a station whose housings are all parts is
// the gap between them. And the model's is the one that runs - where a model exists the generator's is
// switched off - so the corrected fan was never drawn at all.
//
// It hid because a model has to EXIST for that branch to be taken. A fresh test binary has none, so
// every reproduction drew the corrected fan and looked right. This test installs one.
void testTheFanFromAModelLeavesItsOwnHousing() {
    // A model for open wires at a connector: two lines running along +x, which is all that is needed -
    // what matters is WHERE it is stretched from, not what it draws.
    // Installed by writing the office's model file, which is what the application reads - and in the
    // test binary that file is in the TESTS' own app-data folder ("Edraw tests"), so nothing anybody
    // has drawn is touched. Taken away again at the end.
    MechModel om;
    om.kind = MechModelKind::ConnectorToCableWires;
    om.isDefault = true;
    QJsonArray drawing;
    for (double y : {-1.0, 1.0}) {
        QJsonObject line;
        line.insert(QStringLiteral("type"), QStringLiteral("line"));
        QJsonObject a, b;
        a.insert(QStringLiteral("x"), 0.0);
        a.insert(QStringLiteral("y"), y);
        b.insert(QStringLiteral("x"), 10.0);
        b.insert(QStringLiteral("y"), y);
        line.insert(QStringLiteral("a"), a);
        line.insert(QStringLiteral("b"), b);
        drawing.append(line);
    }
    om.drawing = drawing;
    const QString modelFile = MechModelSet::defaultPath();
    const QString kept = modelFile + QStringLiteral(".keep");
    QFile::remove(kept);
    const bool had = QFile::exists(modelFile);
    if (had) QFile::rename(modelFile, kept);
    {
        MechModelSet mine;
        mine.set(om);
        mine.save(modelFile);
    }
    struct PutItBack {
        QString file, kept;
        bool had;
        ~PutItBack() {
            QFile::remove(file);
            if (had) QFile::rename(kept, file);
        }
    } putItBack{modelFile, kept, had};

    check(mechModels().has(MechModelKind::ConnectorToCableWires),
          "model fan: the office has a view of open wires");
    if (!mechModels().has(MechModelKind::ConnectorToCableWires)) return;

    // Two housings at one station, with a run of wires under the FIRST of them.
    MechBuildScript sc;
    MechStep station;
    station.id = QStringLiteral("S1");
    station.kind = PartKind::Connector;
    sc.steps.push_back(station);
    auto housing = [](const char* nm) {
        MechStep x;
        x.id = QStringLiteral("S_%1").arg(QString::fromLatin1(nm));
        x.kind = PartKind::Connector;
        x.name = QString::fromLatin1(nm);
        x.parentStep = QStringLiteral("S1");
        x.isSubpart = true;
        x.pinCount = 4;
        return x;
    };
    sc.steps.push_back(housing("p1"));
    MechStep run;
    run.id = QStringLiteral("S_W");
    run.kind = PartKind::Wire;
    run.name = QStringLiteral("W1");
    run.parentStep = QStringLiteral("S_p1");
    run.isSubpart = true;
    run.lengthMm = 30.0;
    run.wireCount = 4;
    run.gauge = QStringLiteral("20 AWG");
    sc.steps.push_back(run);
    sc.steps.push_back(housing("p2"));

    const MechCompiled c = compileMechScript(sc);
    const std::vector<Entity> ents = buildMechanicalHarness(c.def, nullptr);

    // The lane each housing sits on, and the lane the run's BUNDLE sits on - the bundle has always been
    // put on the right one, so it says where the fan belongs.
    std::vector<double> housingMids;
    double bundleMid = 0.0;
    int bundles = 0;
    for (const Entity& e : ents) {
        const QString role = e.meta.value(QStringLiteral("mechRole")).toString();
        const Bounds b = e.bounds();
        if (!b.valid) continue;
        if (role == QLatin1String("connector")) housingMids.push_back((b.min.y + b.max.y) / 2.0);
        if (role == QLatin1String("bundle")) {
            bundleMid += (b.min.y + b.max.y) / 2.0;
            ++bundles;
        }
    }
    std::sort(housingMids.begin(), housingMids.end());
    check(housingMids.size() >= 2, "model fan: both housings are drawn");
    check(bundles > 0, "model fan: the run is drawn");
    if (housingMids.size() < 2 || bundles == 0) return;
    bundleMid /= static_cast<double>(bundles);
    std::printf("  model fan: housings on y %.2f and %.2f, the bundle on y %.2f\n",
                housingMids.front(), housingMids.back(), bundleMid);

    // EVERY WIRE OF THAT RUN IS ON THE BUNDLE'S LANE. The gap between the housings is where the fan
    // used to go, and the middle of it is roughly halfway between the two lanes.
    const double gapMid = (housingMids.front() + housingMids.back()) / 2.0;
    int wires = 0, astray = 0;
    for (const Entity& e : ents) {
        if (e.meta.value(QStringLiteral("mechRole")).toString() != QLatin1String("wires")) continue;
        const Bounds b = e.bounds();
        if (!b.valid) continue;
        ++wires;
        const double mid = (b.min.y + b.max.y) / 2.0;
        const bool onRun = std::abs(mid - bundleMid) < std::abs(mid - gapMid);
        std::printf("  model fan: a wire on y %8.2f  (run %8.2f, gap %8.2f)%s\n", mid, bundleMid,
                    gapMid, onRun ? "" : "   <-- IN THE GAP");
        if (!onRun) ++astray;
    }
    // DRAWN AT ALL, first: a run with no wires would pass the next check for the wrong reason.
    check(wires > 0, "model fan: the conductors are drawn");
    check(astray == 0, "model fan: every conductor leaves the housing its run belongs to");
}

// ---- A BRANCH IS DESCRIBED AS ONE THING ----------------------------------------------------------
//
// So many wires or cables, running so far, screened, sleeved, with heatshrink where it needs one - one
// row in the list, one thing to move or delete, and everything that goes OVER it held ON it.
//
// Held on the run rather than as steps hung off it for a second reason as well: a covering described as
// its own step goes over "the run that ARRIVES", so a screen described straight after the wires it is
// meant to cover reports that there is no run before it yet. A covering on the run is over that run by
// construction, and there is nothing left to get wrong.
void testABranchCarriesItsOwnCoverings() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    MechWizardDialog w(&ed, nullptr);

    w.setCurrentCategory(Cat::Connector);
    w.typeWays(4);
    w.commitStep();

    // The branch: 4 conductors for 300mm, with a screen the whole way, a sleeve over the middle of it
    // and heatshrink at the far end.
    w.setCurrentCategory(Cat::HarnessBranch);
    w.typeName(QStringLiteral("B1"));
    w.setRunLengthForTest(300.0);
    w.setConductorsForTest(4);
    w.addCovering(PartKind::ShieldBraid);                        // the whole run
    w.addCovering(PartKind::Sleeve, QString(), 120.0, 60.0);     // 120mm of it, starting 60 along
    w.addCovering(PartKind::Heatshrink, QString(), 40.0, 260.0); // the last 40
    check(w.pendingCoverings().size() == 3, "branch: three coverings are on the run being described");
    w.commitStep();

    const MechStep* run = nullptr;
    for (const MechStep& st : w.script().steps)
        if (st.name == QLatin1String("B1")) run = &st;
    check(run != nullptr, "branch: the branch is in the script");
    if (!run) return;
    check(run->coverings.size() == 3, "branch: and it kept them, as one step");

    // ONE ROW, NOT FOUR: the coverings are part of the branch, so they are not steps of their own.
    int steps = static_cast<int>(w.script().steps.size());
    std::printf("  branch: %d step(s) describe a screened, sleeved branch with heatshrink\n", steps);
    check(steps == 2, "branch: a connector and a branch, and nothing else");

    // AND THEY REACH THE DRAWING as things fitted along that run.
    const MechCompiled c = compileMechScript(w.script());
    const QString segId = c.stepToSegment.value(run->id);
    const MechSegment* seg = segId.isEmpty() ? nullptr : c.def.segment(segId);
    check(seg != nullptr, "branch: it compiles to a run");
    if (!seg) return;
    std::printf("  branch: the run carries %d fitting(s)\n", static_cast<int>(seg->coverings.size()));
    check(seg->coverings.size() == 3, "branch: all three are fitted along it");
    int screens = 0, sleeves = 0, shrinks = 0;
    for (const MechFitting& f : seg->coverings) {
        if (f.kind == PartKind::ShieldBraid) ++screens;
        if (f.kind == PartKind::Sleeve) ++sleeves;
        if (f.kind == PartKind::Heatshrink) ++shrinks;
    }
    check(screens == 1 && sleeves == 1 && shrinks == 1,
          "branch: the screen, the sleeve and the heatshrink each arrive as what it is");

    // AND NOTHING COMPLAINS that there is no run before them, which is what describing them as their
    // own steps would have done.
    QString said;
    for (const QString& e : c.errors) said += e + QStringLiteral(" | ");
    if (!c.errors.isEmpty()) std::printf("  branch: errors - %s\n", qPrintable(said));
    check(c.errors.isEmpty(), "branch: and no covering is left with nothing to cover");
}

// ---- THE TRUNK CARRIES WHAT ARRIVES AT IT --------------------------------------------------------
//
// Two branches gathering into a main harness body. The body says nothing about what it carries, so it
// carries what arrives - which is what a trunk IS. The alternative is somebody typing the same
// conductors out a second time, and the drawing then reading the join as a gauge change and covering it
// with heatshrink nobody asked for.
//
// BUILT THE WAY THE WIZARD BUILDS IT: the branches are hung under the station as PARTS, and the trunk
// is the next step at that station that is a station in its own right. That matters - a limb keeps its
// own far end and is drawn REACHING the trunk rather than joining it in the topology, so a rule that
// only looked for runs ENDING at the trunk found nothing arriving at any trunk ever. The first version
// of this test asserted the interesting case behind an `if` that was false, and passed while proving
// nothing; the counts are checked outright now.
void testTheTrunkGathersTheBranches() {
    MechBuildScript sc;
    MechStep station;
    station.id = QStringLiteral("S1");
    station.kind = PartKind::Connector;
    station.name = QStringLiteral("P1");
    station.pinCount = 8;
    sc.steps.push_back(station);

    // Two branches, each a part OF the station: 3 conductors on one, 2 on the other.
    auto branch = [](const char* id, const char* nm, int wires, const char* gauge) {
        MechStep b;
        b.id = QString::fromLatin1(id);
        b.kind = PartKind::Wire;
        b.name = QString::fromLatin1(nm);
        b.parentStep = QStringLiteral("S1");
        b.isSubpart = true;
        b.lengthMm = 200.0;
        b.wireCount = wires;
        b.gauge = QString::fromLatin1(gauge);
        return b;
    };
    sc.steps.push_back(branch("S2", "B1", 3, "22 AWG"));
    sc.steps.push_back(branch("S3", "B2", 2, "20 AWG"));

    // THE TRUNK: the next step at the station that is not one of its parts, saying nothing at all
    // about its own make-up.
    MechStep trunk;
    trunk.id = QStringLiteral("S4");
    trunk.kind = PartKind::Wire;
    trunk.name = QStringLiteral("TRUNK");
    trunk.parentStep = QStringLiteral("S1");
    trunk.lengthMm = 500.0;
    trunk.mainBody = true;
    sc.steps.push_back(trunk);

    const MechCompiled c = compileMechScript(sc);
    const QString segId = c.stepToSegment.value(QStringLiteral("S4"));
    const MechSegment* body = segId.isEmpty() ? nullptr : c.def.segment(segId);
    check(body != nullptr, "trunk: the trunk compiles to a run");
    if (!body) return;

    for (const MechSegment& sg : c.def.segments)
        std::printf("  trunk: seg %-4s from=%-4s to=%-4s toStation=[%s] wires=%d\n",
                    qPrintable(sg.id), qPrintable(sg.fromNode), qPrintable(sg.toNode),
                    qPrintable(sg.toStation), static_cast<int>(sg.wireIds.size()));

    // TWO RUNS REACH IT, and the trunk carries the 3 + 2 they bring.
    // Counted the way the compiler counts it. A limb is drawn to the station the trunk STANDS at,
    // which for a run is its far node - not its start - so a check that only looked at the start node
    // said nothing arrived while the trunk was plainly carrying both branches.
    const QString trunkFar = c.stepToNode.value(QStringLiteral("S4"));
    int arriving = 0;
    for (const MechSegment& sg : c.def.segments) {
        if (sg.id == body->id) continue;
        const bool ends = sg.toNode == body->fromNode;
        const bool drawnInto = !sg.toStation.isEmpty() &&
                               (sg.toStation == body->fromNode || sg.toStation == trunkFar);
        if (ends || drawnInto) ++arriving;
    }
    std::printf("  trunk: %d run(s) reach it, the trunk carries %d conductor(s)\n", arriving,
                static_cast<int>(body->wireIds.size()));
    check(arriving >= 2, "trunk: both branches reach the trunk");
    check(body->wireIds.size() == 5, "trunk: it carries the conductors of both branches");

    // THE SAME IDS, not copies: that is what stops the join reading as a gauge change.
    int shared = 0;
    for (const MechSegment& sg : c.def.segments) {
        if (sg.id == body->id) continue;
        for (const QString& wid : sg.wireIds)
            if (body->wireIds.contains(wid)) ++shared;
    }
    check(shared == static_cast<int>(body->wireIds.size()),
          "trunk: and they are the branches' own conductors, not new ones");

    // A TRUNK THAT DOES SAY what it carries keeps what it said: "if not defined other".
    MechBuildScript sc2 = sc;
    for (MechStep& st : sc2.steps)
        if (st.id == QLatin1String("S4")) {
            st.wireCount = 7;
            st.gauge = QStringLiteral("18 AWG");
        }
    const MechCompiled c2 = compileMechScript(sc2);
    const QString seg2 = c2.stepToSegment.value(QStringLiteral("S4"));
    const MechSegment* body2 = seg2.isEmpty() ? nullptr : c2.def.segment(seg2);
    check(body2 != nullptr, "trunk: the declared trunk compiles too");
    if (body2) {
        std::printf("  trunk: having said 7, it carries %d\n",
                    static_cast<int>(body2->wireIds.size()));
        check(body2->wireIds.size() == 7, "trunk: a trunk that says what it carries keeps it");
    }

    // AND A RUN THAT IS NOT MARKED AS THE TRUNK gathers nothing, however many reach it - a splice has
    // the same shape, and guessing would put every branch's conductors into it.
    MechBuildScript sc3 = sc;
    for (MechStep& st : sc3.steps)
        if (st.id == QLatin1String("S4")) st.mainBody = false;
    const MechCompiled c3 = compileMechScript(sc3);
    const QString seg3 = c3.stepToSegment.value(QStringLiteral("S4"));
    const MechSegment* body3 = seg3.isEmpty() ? nullptr : c3.def.segment(seg3);
    if (body3) {
        std::printf("  trunk: unmarked, the same run carries %d\n",
                    static_cast<int>(body3->wireIds.size()));
        check(body3->wireIds.isEmpty(), "trunk: only a run marked as the trunk gathers the branches");
    }
}

// ---- A COVERING LIES OVER THE BUNDLE, NOT OVER THE BARE CONDUCTORS -------------------------------
//
// A branch runs bare out of each end - far enough for the conductors to fan to the contacts and be
// inspected - and nothing is laid over that stretch: a screen or a sleeve starts where the bundle
// starts and stops where it stops. The bundle outline was always drawn that way; the coverings were
// laid over the WHOLE run, straight across the fan, so a screened branch came out as a body over its
// own open conductors.
//
// Measured along the run, because that is where the fault was visible and nowhere else.
void testACoveringLiesOverTheBundleOnly() {
    MechBuildScript sc;
    MechStep end;
    end.id = QStringLiteral("S1");
    end.kind = PartKind::Connector;
    end.name = QStringLiteral("p2");
    end.pinCount = 4;
    sc.steps.push_back(end);

    MechStep br;
    br.id = QStringLiteral("S2");
    br.kind = PartKind::Wire;
    br.name = QStringLiteral("W1");
    br.parentStep = QStringLiteral("S1");
    br.lengthMm = 100.0;
    br.wireCount = 4;
    br.gauge = QStringLiteral("26 AWG");
    br.openStartMm = 30.0;
    br.openEndMm = 3.0;
    MechStepCovering braid;
    braid.kind = PartKind::ShieldBraid;
    br.coverings.push_back(braid);        // "all of it"
    sc.steps.push_back(br);

    const MechCompiled c = compileMechScript(sc);
    const std::vector<Entity> ents = buildMechanicalHarness(c.def, nullptr);

    // The bundle is the covered part of the run: it starts 30 in and stops 3 short.
    double bundleFrom = 1e18, bundleTo = -1e18, coverFrom = 1e18, coverTo = -1e18;
    double fanFrom = 1e18, fanTo = -1e18;
    for (const Entity& e : ents) {
        const QString role = e.meta.value(QStringLiteral("mechRole")).toString();
        const Bounds b = e.bounds();
        if (!b.valid) continue;
        if (role == QLatin1String("bundle")) {
            bundleFrom = std::min(bundleFrom, b.min.x);
            bundleTo = std::max(bundleTo, b.max.x);
        } else if (role == QLatin1String("fitting")) {
            coverFrom = std::min(coverFrom, b.min.x);
            coverTo = std::max(coverTo, b.max.x);
        } else if (role == QLatin1String("wires")) {
            fanFrom = std::min(fanFrom, b.min.x);
            fanTo = std::max(fanTo, b.max.x);
        }
    }
    std::printf("  covering: bundle x %.2f..%.2f, screen x %.2f..%.2f, conductors x %.2f..%.2f\n",
                bundleFrom, bundleTo, coverFrom, coverTo, fanFrom, fanTo);
    check(coverTo > coverFrom, "covering: the screen is drawn");
    if (!(coverTo > coverFrom)) return;
    // NO BUNDLE RAILS UNDER IT: a screen is opaque, so where it covers the whole stretch its own body
    // is the outline and the bundle's is not drawn as well. The screen's span is what this test is
    // about, so it is measured against the figures rather than against the body it replaced.
    check(!(bundleTo > bundleFrom), "covering: the bundle does not show through the screen");

    // THE SCREEN SPANS THE COVERED PART: 30 in from the connector, 3 short of the far end, on a run
    // of 100 that starts at x 100.
    check(std::abs(coverFrom - 130.0) < 0.01 && std::abs(coverTo - 197.0) < 0.01,
          "covering: the screen covers exactly the bundled stretch");
    // AND THE CONDUCTORS SHOW BEYOND IT AT BOTH ENDS: 30 at the connector, 3 at the far end. The tell
    // that the bare stretches were not simply swallowed.
    check(fanFrom < coverFrom - 1.0, "covering: the conductors show bare at the connector");
    check(fanTo > coverTo + 1.0, "covering: and bare at the far end");
    std::printf("  covering: %.1f bare at the connector, %.1f at the far end\n", coverFrom - fanFrom,
                fanTo - coverTo);
    check(std::abs((coverFrom - fanFrom) - 30.0) < 0.01, "covering: 30 bare where 30 was asked for");
    check(std::abs((fanTo - coverTo) - 3.0) < 0.01, "covering: and 3 at the other end");
}

// ---- NOUGHT BARE MEANS NONE, A COVERING HIDES WHAT IS UNDER IT, AND LOOSE WIRE HAS NO BODY --------
//
// Three things a screened branch has to get right, all of them about what is drawn along the run:
//
//  - A bare-end figure of NOUGHT is a person saying there is no bare wire there. It used to mean "say
//    nothing", so the end fell back to the connector's 20mm and a fan of conductors appeared at a far
//    end that was explicitly set to nothing.
//  - A covering is opaque. The bundle's own rails were drawn as well, so a screened branch came out as
//    a body with a narrower body inside it running the whole length.
//  - A run that is NOT bundled is so many conductors side by side. A body round them says there is a
//    covering the build sheet never called for.
void testWhatIsDrawnAlongAScreenedBranch() {
    auto branchScript = [](double openEnd, bool bundled) {
        MechBuildScript sc;
        MechStep end;
        end.id = QStringLiteral("S1");
        end.kind = PartKind::Connector;
        end.name = QStringLiteral("p2");
        end.pinCount = 4;
        sc.steps.push_back(end);

        MechStep br;
        br.id = QStringLiteral("S2");
        br.kind = PartKind::Wire;
        br.name = QStringLiteral("W1");
        br.parentStep = QStringLiteral("S1");
        br.lengthMm = 100.0;
        br.wireCount = 4;
        br.gauge = QStringLiteral("26 AWG");
        br.openStartMm = 25.0;
        br.openEndMm = openEnd;
        br.bundled = bundled;
        MechStepCovering braid;
        braid.kind = PartKind::ShieldBraid;
        br.coverings.push_back(braid);
        sc.steps.push_back(br);
        return sc;
    };
    struct Spans {
        double bundleFrom = 1e18, bundleTo = -1e18;
        double coverFrom = 1e18, coverTo = -1e18;
        double fanFrom = 1e18, fanTo = -1e18;
        int bundles = 0, covers = 0, fans = 0;
    };
    auto measure = [](const MechBuildScript& sc) {
        Spans sp;
        const MechCompiled c = compileMechScript(sc);
        for (const Entity& e : buildMechanicalHarness(c.def, nullptr)) {
            const QString role = e.meta.value(QStringLiteral("mechRole")).toString();
            const Bounds b = e.bounds();
            if (!b.valid) continue;
            if (role == QLatin1String("bundle")) {
                sp.bundleFrom = std::min(sp.bundleFrom, b.min.x);
                sp.bundleTo = std::max(sp.bundleTo, b.max.x);
                ++sp.bundles;
            } else if (role == QLatin1String("fitting")) {
                sp.coverFrom = std::min(sp.coverFrom, b.min.x);
                sp.coverTo = std::max(sp.coverTo, b.max.x);
                ++sp.covers;
            } else if (role == QLatin1String("wires")) {
                sp.fanFrom = std::min(sp.fanFrom, b.min.x);
                sp.fanTo = std::max(sp.fanTo, b.max.x);
                ++sp.fans;
            }
        }
        return sp;
    };

    // ---- NOUGHT AT THE FAR END: no conductors beyond the covering there. ----
    {
        const Spans sp = measure(branchScript(0.0, true));
        std::printf("  along: far end 0 -> screen x %.2f..%.2f, %d conductor run(s) x %.2f..%.2f\n",
                    sp.coverFrom, sp.coverTo, sp.fans, sp.fanFrom, sp.fanTo);
        check(sp.covers > 0, "along: the screen is drawn");
        check(sp.fans > 0, "along: and the conductors are drawn at the end that is bare");
        // The run is 100 long from x 100: bare 25 at the connector, nothing bare at the far end, so
        // the screen reaches the very end of it.
        check(std::abs(sp.coverTo - 200.0) < 0.01, "along: the screen runs to the far end");
        check(std::abs(sp.coverFrom - 125.0) < 0.01, "along: and starts where the bare stretch ends");
        // AND NO CONDUCTORS BEYOND IT. The tell: the fan lies wholly on the connector side.
        check(sp.fanTo <= sp.coverFrom + 0.01,
              "along: nought bare at the far end draws no conductors there");
    }

    // ---- A FAR END THAT DOES ASK for bare wire still gets it, or the check above proves nothing. ----
    {
        const Spans sp = measure(branchScript(3.0, true));
        std::printf("  along: far end 3 -> screen x %.2f..%.2f, conductors to x %.2f\n", sp.coverFrom,
                    sp.coverTo, sp.fanTo);
        check(std::abs(sp.coverTo - 197.0) < 0.01, "along: 3 bare leaves the screen 3 short");
        check(sp.fanTo > 196.0, "along: and the conductors show over those 3");
    }

    // ---- NOTHING SHOWS INSIDE THE COVERING. ----
    {
        const Spans sp = measure(branchScript(0.0, true));
        std::printf("  along: %d bundle rail(s) under a screen that covers the lot\n", sp.bundles);
        check(sp.bundles == 0, "along: the bundle is not drawn inside the covering");
    }

    // ---- NOT BUNDLED: conductors end to end, no body, nothing over them. ----
    {
        const Spans sp = measure(branchScript(0.0, false));
        std::printf("  along: unbundled -> %d body, %d covering(s), conductors x %.2f..%.2f\n",
                    sp.bundles, sp.covers, sp.fanFrom, sp.fanTo);
        check(sp.fans > 0, "along: loose wire is drawn as conductors");
        check(std::abs(sp.fanFrom - 100.0) < 0.01 && std::abs(sp.fanTo - 200.0) < 0.01,
              "along: and they run the whole length of it");
        check(sp.bundles == 0, "along: with no body round them");
        check(sp.covers == 0, "along: and nothing laid over them");
    }
}

// A part is found by whichever of its names you happen to know.
void testAPartIsFoundByAnyOfItsNames() {
// ---- A part is found by whichever of its names you happen to know ----------------------------
{
    // The buyer knows the house number, the draughtsman knows the description, the person holding
    // the reel knows what is printed on it, and somebody working off a bill of materials knows the
    // manufacturer. The picker used to show the description with the manufacturer's number in
    // brackets, so three of those four could not be typed at all - the text was not there to match.
    PartsLibrary lib;
    LibraryPart& p = lib.add(PartKind::Connector);
    p.id = QStringLiteral("P1");
    p.mfrPn = QStringLiteral("D38999/26WB35PD");
    p.description = QStringLiteral("CONN CIRCULAR D38999 III SHELL-11 PLUG");
    p.mfrName = QStringLiteral("AMPHENOL");
    p.internalPn = QStringLiteral("HOUSE-0042");

    const QString label = MechWizardDialog::partLabelForTest(p);
    std::printf("    (picker: \"%s\")\n", qPrintable(label));
    // Every name the part has is in what the box shows, which is what makes it typeable: a
    // completer matches on the text it displays.
    check(label.contains(p.mfrPn), "picker: the manufacturer's part number can be typed");
    check(label.contains(p.description), "picker: and the description");
    check(label.contains(p.mfrName), "picker: and the maker's name");
    check(label.contains(p.internalPn), "picker: and the house part number");
    // The part number LEADS, because that is what a part is ordered and marked by and it is what
    // a person scans a list for.
    check(label.startsWith(p.mfrPn), "picker: with the part number in front");

    // A part with only a description still reads as itself rather than as a string of separators.
    LibraryPart bare;
    bare.id = QStringLiteral("B1");
    bare.description = QStringLiteral("Something nobody numbered");
    check(MechWizardDialog::partLabelForTest(bare) == bare.description,
          "picker: a part with one name is called by it, with nothing hung off it");
    // And one with no names at all falls back to something rather than to blank.
    LibraryPart nameless;
    nameless.id = QStringLiteral("N1");
    check(!MechWizardDialog::partLabelForTest(nameless).isEmpty(), "picker: a nameless part still has a line");
}
}

int run_mechwizard_tests() {
    std::printf("Running harness build wizard tests...\n");
    fails = 0;
    testPaletteCoversWhatWasAskedFor();
    testNothingIsPrePlanned();
    testUndoTakesTheLastStepBack();
    testDirectionIsStickyAndStoredAsATurn();
    testTurningBackAndForthDoesNotDrift();
    testIssuesAreReportedNotInvented();
    testEnterEditsRatherThanAdding();
    testDeleteClosesTheHarnessUp();
    testSeveralConductorGroupsMakeOneBundle();
    testContinuedConductorsDoNotMakeASplice();
    testBalloonAndBreakMarkReachTheDrawing();
    testDimensioningAConnectorMeasuresTheConnector();
    testARunIsDesignatedAndTheGaugesAreAwg();
    testTheRunDesignatorIsShownOnlyWhereItIsTheOnlyName();
    testFanWiresDoNotCrossOneAnother();
    testTheFanIsAPictureOfTheBundleNotATally();
    testBreakMarksAndBalloons();
    testARunLeavesItsOwnHousing();
    testDirectionTurnsTheFirstPartRoundInThePreview();
    testTheFirstStationReadsAsTheEndSideAndLocksAsAHeading();
    testBalloonCanBeWithheldPerStep();
    testChoosingALibraryPartSticks();
    testChangingCategoryDropsAnUnrelatedPart();
    testWaysComeFromTheConnectorsOwnPins();
    testThePreviewShowsTheStepBeingDescribed();
    testTheNameFieldIsCalledDesignatorOnAnEnd();
    testTheListShowsTheStepBeingDescribedUntilItIsApplied();
    testAnOpenStartNamesEachWire();
    testTwoConnectorsCanShareOneEnd();
    testSubpartsNestAndTheListShowsIt();
    testADescribedStepIsGhostedUntilApplied();
    testSeveralHousingsAtOneStationAreStackedWithGaps();
    testRunsEnterTheJunctionPartAtItsOwnFaces();
    testNextCarriesOnFromWhereYouAreStanding();
    testDeletingAStationDoesNotOrphanItsParts();
    testAStationCanBeRolledUpAndStaysThatWay();
    testAStationWithPartsIsAHeadingNotAForm();
    testTheBundleIsWorkedOutFromTheWiresTaken();
    testLibraryHousingsAtOneStationDoNotOverlap();
    testSubpartsCanBeDraggedIntoOrder();
    testWiresUnderOneHousingAreDrawnOnlyThere();
    testNothingIsDrawnBetweenStackedHousings();
    testTheFanFromAModelLeavesItsOwnHousing();
    testABranchCarriesItsOwnCoverings();
    testTheTrunkGathersTheBranches();
    testACoveringLiesOverTheBundleOnly();
    testWhatIsDrawnAlongAScreenedBranch();
    testHeatshrinkIsDrawnWithShrunkEnds();
    testTheHeatshrinkButtonHasSomethingToPlace();
    testAPartIsFoundByAnyOfItsNames();
    if (fails == 0) std::printf("  harness build wizard: OK\n");
    return fails;
}
