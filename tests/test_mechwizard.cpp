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
#include "harness/MechModels.h"
#include "harness/MechanicalHarness.h"
#include "harness/PartsLibrary.h"
#include "model/Document.h"

#include <QApplication>

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
    bool ordered = true;
    for (size_t i = 1; i < fan.size(); ++i) {
        const bool startsUp = fan[i]->points.front().y > fan[i - 1]->points.front().y;
        const bool endsUp = fan[i]->points.back().y > fan[i - 1]->points.back().y;
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
    // the node - its output face still pointing the way the harness goes.
    w.setCurrentGoesRight(false);
    // setCurrentGoesRight alone is the widget state; the button ALSO settles the handedness, which is
    // what the drawing reads. Done here the way the button does it.
    MechBuildScript flipped = w.script();
    flipped.leftToRight = false;
    w.setScript(flipped);
    w.setCurrentCategory(Cat::Connector);
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
    testDirectionTurnsTheFirstPartRoundInThePreview();
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
    testLibraryHousingsAtOneStationDoNotOverlap();
    testSubpartsCanBeDraggedIntoOrder();
    testHeatshrinkIsDrawnWithShrunkEnds();
    testTheHeatshrinkButtonHasSomethingToPlace();
    if (fails == 0) std::printf("  harness build wizard: OK\n");
    return fails;
}
