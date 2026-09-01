// The build script: an authored sequence of steps, and the tree it compiles to.
//
// WHAT IS WORTH TESTING HERE
//
// The compiler is the whole correctness surface of the new wizard. Once the script is what the user
// authors and what gets saved, every mechanical drawing comes through compileMechScript() - so a
// wrong step-to-tree rule is a wrong drawing, silently, for every harness. It is also the easy part
// to test properly, being data in and data out with no window anywhere near it.
//
// The compliance checks get the same attention for a different reason: their whole value is that they
// only speak when they KNOW. A check that reports a pass on absent data is worse than no check, because
// it teaches people the absence of a warning means something.
#include "harness/MechBuildScript.h"
#include "harness/MechanicalHarness.h"
#include "harness/PartsLibrary.h"

#include <QJsonDocument>
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

// A step, filled in as briefly as each test needs.
MechStep mk(const QString& id, PartKind kind, const QString& parent = QString()) {
    MechStep s;
    s.id = id;
    s.kind = kind;
    s.parentStep = parent;
    return s;
}

int countIssues(const std::vector<MechStepIssue>& v, MechIssueKind kind) {
    return static_cast<int>(std::count_if(v.begin(), v.end(),
                                          [&](const MechStepIssue& i) { return i.kind == kind; }));
}

bool anyMentions(const std::vector<MechStepIssue>& v, const QString& text) {
    for (const MechStepIssue& i : v)
        if (i.message.contains(text, Qt::CaseInsensitive)) return true;
    return false;
}

// ---- Roles ---------------------------------------------------------------------------------------

void testRoles() {
    check(mechStepRole(PartKind::Cable) == MechStepRole::Run, "role: a cable is a run");
    check(mechStepRole(PartKind::Wire) == MechStepRole::Run, "role: loose wires are a run");
    check(mechStepRole(PartKind::Connector) == MechStepRole::End, "role: a connector ends a run");
    check(mechStepRole(PartKind::YSplit) == MechStepRole::Node, "role: a Y-split is a node");
    // The split that matters, and the one MechFitting's own comment describes: a backshell sits at a
    // point, a braid runs along the harness. Putting either on the wrong one draws it in the wrong
    // place and sizes it against the wrong diameter.
    check(mechStepRole(PartKind::Backshell) == MechStepRole::AtNode, "role: a backshell is at a node");
    check(mechStepRole(PartKind::HeatshrinkBoot) == MechStepRole::AtNode, "role: a boot is at a node");
    check(mechStepRole(PartKind::Marker) == MechStepRole::AtNode, "role: a label is at a node");
    check(mechStepRole(PartKind::ShieldBraid) == MechStepRole::AlongRun, "role: braid runs along");
    check(mechStepRole(PartKind::Heatshrink) == MechStepRole::AlongRun, "role: heatshrink runs along");
}

// ---- A plain lead, end to end --------------------------------------------------------------------

void testLinearLeadCompiles() {
    MechBuildScript sc;
    sc.name = QStringLiteral("W-100");

    MechStep p1 = mk("S1", PartKind::Connector);
    p1.name = QStringLiteral("P1");
    p1.pinCount = 9;
    sc.steps.push_back(p1);

    MechStep bs = mk("S2", PartKind::Backshell, "S1");
    bs.name = QStringLiteral("BS1");
    sc.steps.push_back(bs);

    MechStep run = mk("S3", PartKind::Cable, "S2");
    run.name = QStringLiteral("W1");
    run.lengthMm = 300.0;
    run.odMm = 6.4;
    run.wireCount = 4;
    run.gauge = QStringLiteral("0.5");
    run.dimension = true;
    run.dimText = QStringLiteral("300");
    sc.steps.push_back(run);

    MechStep p2 = mk("S4", PartKind::Connector, "S3");
    p2.name = QStringLiteral("P2");
    p2.pinCount = 9;
    sc.steps.push_back(p2);

    const MechCompiled c = compileMechScript(sc);
    check(c.errors.isEmpty(), "lead: compiles with no errors");
    check(c.def.nodes.size() == 2, "lead: two nodes - one at each end");
    check(c.def.segments.size() == 1, "lead: one run between them");
    check(c.def.wires.size() == 4, "lead: the run's four conductors are in the definition");

    // The two connectors are the two nodes, and the backshell is ON the first one rather than being a
    // node of its own: a backshell is not a place the harness goes, it is a thing fitted at one.
    const MechNode* root = c.def.rootNode();
    check(root && root->kind == MechNodeKind::Connector, "lead: the first step made the root a connector");
    check(root && root->name == QLatin1String("P1"), "lead: and named it");
    check(root && root->pinCount == 9, "lead: and gave it its positions");
    check(root && root->fittings.size() == 1, "lead: the backshell is a fitting at that node");

    const MechSegment* seg = c.def.segment(c.stepToSegment.value("S3"));
    check(seg != nullptr, "lead: the run step maps to a segment");
    if (seg) {
        check(near(seg->lengthMm, 300.0), "lead: the run's length carried through");
        check(near(seg->jacketOdMm, 6.4), "lead: a cable step became a jacketed run");
        check(seg->isCable(), "lead: and the definition agrees it is a cable");
        check(seg->dimension && seg->dimText == QLatin1String("300"),
              "lead: the step's dimension tick and its nominal text carried through");
        check(seg->wireIds.size() == 4, "lead: the conductors are routed through it");
    }

    // The far node is where the second connector landed - not a fifth node.
    const MechNode* far = c.def.node(c.stepToNode.value("S4"));
    check(far && far->kind == MechNodeKind::Connector, "lead: the far end is the second connector");
    check(far && far->name == QLatin1String("P2"), "lead: named from its own step");

    // And the trail back: a finding about the segment has to be showable against the step that made it.
    check(c.objectToStep.value(seg ? seg->id : QString()) == QLatin1String("S3"),
          "lead: the segment knows which step created it");
}

// ---- The direction arrow, and which way the harness runs -----------------------------------------

void testDirectionAndHandedness() {
    MechBuildScript sc;
    sc.steps.push_back(mk("S1", PartKind::Connector));
    MechStep run = mk("S2", PartKind::Cable, "S1");
    run.lengthMm = 100.0;
    run.turnDeg = 90.0;      // the step turns up
    sc.steps.push_back(run);

    {
        sc.leftToRight = true;
        const MechCompiled c = compileMechScript(sc);
        check(near(c.def.draw.baseDirectionDeg, 0.0), "direction: left-to-right lays the root at 0");
        const MechNode* far = c.def.node(c.stepToNode.value("S2"));
        check(far && near(far->angleDeg, 90.0), "direction: the step's turn became the node's angle");
    }
    {
        // RIGHT TO LEFT IS THE ROOT'S DIRECTION, NOT A CHANGE TO EVERY TURN. The turn stays +90 and
        // the whole tree is laid the other way - which is the point of MechNode::angleDeg being
        // relative. If flipping the build direction had to rewrite every branch's angle, moving a
        // branch afterwards would stop working.
        sc.leftToRight = false;
        const MechCompiled c = compileMechScript(sc);
        check(near(c.def.draw.baseDirectionDeg, 180.0), "direction: right-to-left lays the root at 180");
        const MechNode* far = c.def.node(c.stepToNode.value("S2"));
        check(far && near(far->angleDeg, 90.0),
              "direction: and leaves the relative turn exactly as it was");
    }

    // The layout honours it, which is the whole reason the field exists: it replaced a literal 0.0.
    sc.leftToRight = false;
    const MechCompiled c = compileMechScript(sc);
    const MechLayout lay = layoutMechanicalHarness(c.def);
    const MechPlacedNode* placedRoot = lay.node(c.def.rootNode()->id);
    check(placedRoot && near(placedRoot->dirDeg, 180.0),
          "direction: the layout lays the root in the script's direction");
}

// ---- A node that goes several ways ---------------------------------------------------------------

void testBranchingTree() {
    MechBuildScript sc;
    sc.steps.push_back(mk("S1", PartKind::Connector));

    MechStep trunk = mk("S2", PartKind::Cable, "S1");
    trunk.lengthMm = 200.0;
    sc.steps.push_back(trunk);

    MechStep node = mk("S3", PartKind::YSplit, "S2");
    node.name = QStringLiteral("BR1");
    sc.steps.push_back(node);

    // TWO STEPS NAMING THE SAME PARENT ARE TWO BRANCHES. There is no branch table to keep in step
    // with anything - the tree is in the script, which is what parentStep is for.
    MechStep b1 = mk("S4", PartKind::Wire, "S3");
    b1.lengthMm = 120.0;
    b1.turnDeg = 30.0;
    b1.wireCount = 2;
    b1.gauge = QStringLiteral("0.5");
    sc.steps.push_back(b1);

    MechStep b2 = mk("S5", PartKind::Wire, "S3");
    b2.lengthMm = 90.0;
    b2.turnDeg = -30.0;
    b2.wireCount = 2;
    b2.gauge = QStringLiteral("0.5");
    sc.steps.push_back(b2);

    sc.steps.push_back(mk("S6", PartKind::Connector, "S4"));
    sc.steps.push_back(mk("S7", PartKind::Connector, "S5"));

    const MechCompiled c = compileMechScript(sc);
    check(c.errors.isEmpty(), "tree: compiles with no errors");
    check(c.def.segments.size() == 3, "tree: trunk plus two branches");
    check(c.def.nodes.size() == 4, "tree: root, the breakout, and an end on each branch");

    const MechNode* br = c.def.node(c.stepToNode.value("S3"));
    check(br && br->kind == MechNodeKind::Breakout, "tree: the node step made a breakout");
    check(br && br->name == QLatin1String("BR1"), "tree: named from the step");
    if (br) {
        check(c.def.childSegments(br->id).size() == 2, "tree: two runs leave the breakout");
    }

    // Both branches leave from the SAME node, and each keeps its own turn.
    const MechNode* e1 = c.def.node(c.stepToNode.value("S4"));
    const MechNode* e2 = c.def.node(c.stepToNode.value("S5"));
    check(e1 && near(e1->angleDeg, 30.0), "tree: the first branch keeps its own turn");
    check(e2 && near(e2->angleDeg, -30.0), "tree: and the second keeps its own");

    // Every node placed: a branch whose parent never got placed would be dropped silently by the
    // layout, so this is the check that the tree is actually connected.
    const MechLayout lay = layoutMechanicalHarness(c.def);
    check(lay.nodes.size() == c.def.nodes.size(), "tree: the layout places every node");
}

// ---- Where a fitting lands ----------------------------------------------------------------------

void testFittingPlacement() {
    MechBuildScript sc;
    sc.steps.push_back(mk("S1", PartKind::Connector));
    MechStep run = mk("S2", PartKind::Cable, "S1");
    run.lengthMm = 250.0;
    run.odMm = 5.0;
    sc.steps.push_back(run);

    MechStep braid = mk("S3", PartKind::ShieldBraid, "S2");   // along the run that arrives
    sc.steps.push_back(braid);
    MechStep boot = mk("S4", PartKind::HeatshrinkBoot, "S2"); // at the node the run arrives at
    sc.steps.push_back(boot);

    const MechCompiled c = compileMechScript(sc);
    check(c.errors.isEmpty(), "fittings: compiles with no errors");

    const MechSegment* seg = c.def.segment(c.stepToSegment.value("S2"));
    check(seg && seg->coverings.size() == 1, "fittings: the braid went onto the run");
    check(c.stepToSegment.value("S3") == (seg ? seg->id : QString()),
          "fittings: and the step maps to that run");

    const MechNode* far = c.def.node(c.stepToNode.value("S2"));
    check(far && far->fittings.size() == 1, "fittings: the boot went onto the node");
    check(far && far->fittings.front().kind == PartKind::HeatshrinkBoot,
          "fittings: as a boot rather than as anything else");

    // A covering with no run before it has nowhere to go, and says so rather than sliding onto the
    // run that LEAVES - which is a different piece of harness.
    MechBuildScript bad;
    MechStep lone = mk("S1", PartKind::Heatshrink);
    lone.name = QStringLiteral("HS1");
    bad.steps.push_back(lone);
    const MechCompiled bc = compileMechScript(bad);
    check(!bc.errors.isEmpty(), "fittings: a covering with no run before it is reported");
    check(bc.errors.join(QLatin1Char(' ')).contains(QLatin1String("no run")),
          "fittings: and the message says why");
}

// ---- Sub-parts: counted, never drawn -------------------------------------------------------------

void testBomOnlySubParts() {
    MechBuildScript sc;
    sc.steps.push_back(mk("S1", PartKind::Connector));
    MechStep run = mk("S2", PartKind::Cable, "S1");
    run.lengthMm = 400.0;
    run.odMm = 7.2;
    MechSubPart braid;
    braid.kind = PartKind::ShieldBraid;
    braid.name = QStringLiteral("inner screen");
    braid.qty = 400.0;
    braid.unit = QStringLiteral("mm");
    run.subParts.push_back(braid);
    sc.steps.push_back(run);

    const MechCompiled c = compileMechScript(sc);
    const MechSegment* seg = c.def.segment(c.stepToSegment.value("S2"));

    // THE POINT OF bomOnly: it must not become a covering. A covering is drawn, and drawing a shield
    // that lives inside the jacket would say something false about the harness.
    check(seg && seg->coverings.empty(),
          "sub-parts: a BOM-only sub-part is NOT put on the run as a drawn covering");
    check(braid.bomOnly, "sub-parts: bomOnly is the default, so nothing has to remember to set it");
}

// ---- Compliance: only speak when you know --------------------------------------------------------

void testComplianceStaysSilentWithoutFigures() {
    MechBuildScript sc;
    sc.steps.push_back(mk("S1", PartKind::Connector));
    MechStep run = mk("S2", PartKind::Cable, "S1");
    run.lengthMm = 300.0;
    run.odMm = 6.0;
    sc.steps.push_back(run);

    const MechCompiled c = compileMechScript(sc);
    // No library open at all. Every library-derived figure is unknown, so the answer must be gaps and
    // never violations: nothing here is known to be WRONG.
    const auto issues = mechScriptIssues(sc, c, nullptr);
    check(countIssues(issues, MechIssueKind::Violation) == 0,
          "compliance: with no library open, nothing is reported as wrong");

    // A run with nothing said about it is a gap, named.
    MechBuildScript bare;
    bare.steps.push_back(mk("S1", PartKind::Connector));
    MechStep vague = mk("S2", PartKind::Cable, "S1");
    vague.name = QStringLiteral("W9");
    bare.steps.push_back(vague);          // no length, no jacket
    const MechCompiled bc = compileMechScript(bare);
    const auto gaps = mechScriptIssues(bare, bc, nullptr);
    check(anyMentions(gaps, QStringLiteral("no length")), "compliance: a run with no length is a gap");
    check(anyMentions(gaps, QStringLiteral("jacket diameter")),
          "compliance: a cable with no jacket diameter is a gap, because nothing over it can be sized");
    check(countIssues(gaps, MechIssueKind::Violation) == 0,
          "compliance: and neither is called a violation");
}

// The check the whole feature exists for, and the one with a trap in it.
void testContactAgainstConductor() {
    PartsCatalog cat;
    cat.setRemembering(false);

    // A contact that takes 20 to 24 AWG - which in mm2 is 0.52 down to 0.20. Written in AWG on
    // purpose: this is where the numbers run backwards.
    LibraryPart& contact = cat.active().add(PartKind::ContactPin);
    contact.description = QStringLiteral("Contact 20-24 AWG");
    contact.params.insert(QStringLiteral("gaugeMode"), QStringLiteral("AWG"));
    contact.params.insert(QStringLiteral("gaugeMin"), QStringLiteral("20"));
    contact.params.insert(QStringLiteral("gaugeMax"), QStringLiteral("24"));
    const QString contactId = contact.id;

    const auto issuesFor = [&](const QString& gauge) {
        MechBuildScript sc;
        sc.steps.push_back(mk("S1", PartKind::Connector));
        MechStep run = mk("S2", PartKind::Wire, "S1");
        run.lengthMm = 200.0;
        run.wireCount = 2;
        run.gauge = gauge;
        sc.steps.push_back(run);
        MechStep con = mk("S3", PartKind::Connector, "S2");
        con.name = QStringLiteral("P2");
        con.contactPart = contactId;
        sc.steps.push_back(con);
        const MechCompiled c = compileMechScript(sc);
        return mechScriptIssues(sc, c, &cat);
    };

    // 22 AWG (0.32 mm2) is inside 0.20 - 0.52: nothing to say.
    check(countIssues(issuesFor(QStringLiteral("22 AWG")), MechIssueKind::Violation) == 0,
          "contact: a 22 AWG conductor in a 20-24 AWG contact passes");

    // AWG RUNS BACKWARDS, and this is the pair that catches it. 14 AWG is 2.08 mm2 - far THICKER than
    // the 0.52 the contact takes - and 28 AWG is 0.08 mm2, far thinner. If the range were compared as
    // raw AWG numbers rather than as cross-sections, both of these would come out the wrong way
    // round: 14 would look "below the minimum of 20" and pass, and 28 would look "inside 20 to 24"
    // and pass as well. Both must fail.
    check(countIssues(issuesFor(QStringLiteral("14 AWG")), MechIssueKind::Violation) == 1,
          "contact: a conductor too THICK for the contact is a violation");
    check(countIssues(issuesFor(QStringLiteral("28 AWG")), MechIssueKind::Violation) == 1,
          "contact: a conductor too THIN for the contact is a violation");

    // The same conductor written in mm2 must give the same answer as written in AWG: 2.08 mm2 is
    // 14 AWG, and the check cannot care which way the drawing spells it.
    check(countIssues(issuesFor(QStringLiteral("2.08")), MechIssueKind::Violation) == 1,
          "contact: the same conductor in mm2 gives the same answer as in AWG");

    // And with no gauge at all it is a gap, not a pass and not a failure.
    const auto silent = issuesFor(QString());
    check(countIssues(silent, MechIssueKind::Violation) == 0,
          "contact: a conductor with no gauge is not reported as wrong");
    check(anyMentions(silent, QStringLiteral("no gauge")), "contact: it is reported as a gap");
}

// A contact whose library entry states no range is a gap. This is the "if it exists" rule: absent
// data must never read as a pass.
void testContactWithNoStatedRange() {
    PartsCatalog cat;
    cat.setRemembering(false);
    LibraryPart& contact = cat.active().add(PartKind::ContactPin);
    contact.description = QStringLiteral("Contact, no range given");
    const QString contactId = contact.id;

    MechBuildScript sc;
    sc.steps.push_back(mk("S1", PartKind::Connector));
    MechStep run = mk("S2", PartKind::Wire, "S1");
    run.lengthMm = 100.0;
    run.wireCount = 1;
    run.gauge = QStringLiteral("0.5");
    sc.steps.push_back(run);
    MechStep con = mk("S3", PartKind::Connector, "S2");
    con.contactPart = contactId;
    sc.steps.push_back(con);

    const MechCompiled c = compileMechScript(sc);
    const auto issues = mechScriptIssues(sc, c, &cat);
    check(countIssues(issues, MechIssueKind::Violation) == 0,
          "contact: a part that states no range is never a violation");
    check(anyMentions(issues, QStringLiteral("states no gauge range")),
          "contact: and the gap says the part is silent, not that the wire is wrong");
}

// ---- JSON round trip ----------------------------------------------------------------------------

void testJsonRoundTrip() {
    MechBuildScript sc;
    sc.name = QStringLiteral("H-42");
    sc.leftToRight = false;

    MechStep a = mk("S1", PartKind::Connector);
    a.name = QStringLiteral("P1");
    a.pinCount = 12;
    a.contactPart = QStringLiteral("C-1");
    a.contactQty = 9;
    a.dimension = true;
    a.dimText = QStringLiteral("REF");
    sc.steps.push_back(a);

    MechStep b = mk("S2", PartKind::Cable, "S1");
    b.name = QStringLiteral("W1");
    b.lengthMm = 275.5;
    b.odMm = 6.4;
    b.wireCount = 4;
    b.gauge = QStringLiteral("22 AWG");
    b.turnDeg = -45.0;
    MechSubPart sp;
    sp.kind = PartKind::ShieldBraid;
    sp.name = QStringLiteral("screen");
    sp.qty = 275.5;
    sp.unit = QStringLiteral("mm");
    b.subParts.push_back(sp);
    sc.steps.push_back(b);

    const QJsonObject json = sc.toJson();
    const MechBuildScript back = MechBuildScript::fromJson(json);

    check(back.name == sc.name, "json: the name survives");
    check(back.leftToRight == false, "json: the build direction survives");
    check(back.steps.size() == 2, "json: both steps survive");
    if (back.steps.size() == 2) {
        const MechStep& x = back.steps[0];
        check(x.id == QLatin1String("S1") && x.kind == PartKind::Connector, "json: kind survives");
        check(x.pinCount == 12 && x.contactPart == QLatin1String("C-1") && x.contactQty == 9,
              "json: a connector's positions and its contact survive");
        check(x.dimension && x.dimText == QLatin1String("REF"),
              "json: the dimension tick and its nominal text survive");

        const MechStep& y = back.steps[1];
        check(y.parentStep == QLatin1String("S1"), "json: the tree link survives");
        check(near(y.lengthMm, 275.5) && near(y.odMm, 6.4) && near(y.turnDeg, -45.0),
              "json: the figures survive, negatives included");
        check(y.gauge == QLatin1String("22 AWG"), "json: a gauge written in AWG survives as written");
        check(y.subParts.size() == 1 && y.subParts.front().bomOnly,
              "json: a BOM-only sub-part survives, still BOM-only");
    }

    // KIND IS STORED BY NAME, NOT BY NUMBER. PartKind has had things inserted into the middle of it
    // before, and a file written as integers would come back meaning a different kind of part. This is
    // the check that the file is readable by a future build.
    const QJsonObject first = json.value(QStringLiteral("steps")).toArray().at(0).toObject();
    check(first.value(QStringLiteral("kind")).isString(),
          "json: a step's kind is written as a name, so inserting a PartKind cannot reinterpret old files");

    // And it survives an actual serialise/parse, not just a struct copy.
    const QByteArray bytes = QJsonDocument(json).toJson(QJsonDocument::Compact);
    const MechBuildScript reparsed =
        MechBuildScript::fromJson(QJsonDocument::fromJson(bytes).object());
    check(reparsed.steps.size() == 2 && near(reparsed.steps[1].lengthMm, 275.5),
          "json: and through real bytes as well");
}

// The compiled tree must be the same whichever order equivalent steps were appended in, as long as
// the parent links say the same thing. Otherwise a script is not a description, it is a recording.
void testCompileIsDeterministic() {
    const auto build = [](bool reversedBranchOrder) {
        MechBuildScript sc;
        sc.steps.push_back(mk("S1", PartKind::Connector));
        MechStep trunk = mk("S2", PartKind::Cable, "S1");
        trunk.lengthMm = 100.0;
        sc.steps.push_back(trunk);
        sc.steps.push_back(mk("S3", PartKind::YSplit, "S2"));
        MechStep b1 = mk("S4", PartKind::Wire, "S3");
        b1.lengthMm = 50.0;
        MechStep b2 = mk("S5", PartKind::Wire, "S3");
        b2.lengthMm = 60.0;
        if (reversedBranchOrder) {
            sc.steps.push_back(b2);
            sc.steps.push_back(b1);
        } else {
            sc.steps.push_back(b1);
            sc.steps.push_back(b2);
        }
        return compileMechScript(sc);
    };

    const MechCompiled a = build(false);
    const MechCompiled b = build(true);
    check(a.def.nodes.size() == b.def.nodes.size() && a.def.segments.size() == b.def.segments.size(),
          "compile: the same script shape gives the same tree size either way round");
    // Each branch still gets its own length, whichever order they were added in.
    const MechSegment* a4 = a.def.segment(a.stepToSegment.value("S4"));
    const MechSegment* b4 = b.def.segment(b.stepToSegment.value("S4"));
    check(a4 && b4 && near(a4->lengthMm, b4->lengthMm),
          "compile: and a given step compiles to the same run either way round");
}

} // namespace

int run_mechscript_tests() {
    std::printf("Running mechanical build-script tests...\n");
    fails = 0;
    testRoles();
    testLinearLeadCompiles();
    testDirectionAndHandedness();
    testBranchingTree();
    testFittingPlacement();
    testBomOnlySubParts();
    testComplianceStaysSilentWithoutFigures();
    testContactAgainstConductor();
    testContactWithNoStatedRange();
    testJsonRoundTrip();
    testCompileIsDeterministic();
    if (fails == 0) std::printf("  mechanical build script: OK\n");
    return fails;
}
