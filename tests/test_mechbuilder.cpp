// The mechanical builder: sizes worked out from the conductors, a tree laid out from lengths and
// angles, and a drawing generated from both. Every figure here can be checked by hand.
#include "harness/Harness.h"
#include "harness/MechanicalHarness.h"
#include "model/Document.h"

#include <cmath>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++failures;
    }
}

bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

// A wire of a given cross-section, added to a definition.
MechWire wireOf(const QString& id, const QString& gauge) {
    MechWire w;
    w.id = id;
    w.label = id;
    w.gauge = gauge;
    return w;
}

// Root --s1--> P1, with `count` wires of `gauge` through it.
MechHarnessDef straightDef(double lengthMm, int count, const QString& gauge) {
    MechHarnessDef def;
    def.name = QStringLiteral("test");
    MechNode root;
    root.id = QStringLiteral("root");
    root.name = QStringLiteral("BR1");
    root.kind = MechNodeKind::Breakout;
    def.nodes.push_back(root);

    MechNode p1;
    p1.id = QStringLiteral("p1");
    p1.name = QStringLiteral("P1");
    p1.kind = MechNodeKind::Connector;
    p1.pinCount = count;
    p1.parentSegment = QStringLiteral("s1");
    def.nodes.push_back(p1);

    MechSegment s1;
    s1.id = QStringLiteral("s1");
    s1.fromNode = QStringLiteral("root");
    s1.toNode = QStringLiteral("p1");
    s1.lengthMm = lengthMm;
    for (int i = 0; i < count; ++i) {
        const QString id = QStringLiteral("w%1").arg(i + 1);
        def.wires.push_back(wireOf(id, gauge));
        s1.wireIds.push_back(id);
    }
    def.segments.push_back(s1);
    return def;
}

int countRole(const std::vector<Entity>& ents, const char* role) {
    int n = 0;
    for (const Entity& e : ents)
        if (e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String(role)) ++n;
    return n;
}

} // namespace

int run_mechbuilder_tests() {
    std::printf("mechanical builder tests:\n");

    // ---- A conductor's outside diameter, from its cross-section ----
    {
        // The table's own entries come back exactly.
        check(near(wireOuterDiameterMm(QStringLiteral("0.5")), 1.5, 1e-9),
              "wire OD: 0.5 mm2 is 1.5 mm over thin wall");
        check(near(wireOuterDiameterMm(QStringLiteral("2.5")), 3.1, 1e-9),
              "wire OD: 2.5 mm2 is 3.1 mm");
        // Between two entries it interpolates, so a non-standard size still has a diameter.
        const double mid = wireOuterDiameterMm(QStringLiteral("0.625"));
        std::printf("    (0.625 mm2 interpolates to %.3f mm)\n", mid);
        check(mid > 1.5 && mid < 1.8, "wire OD: a size between two entries lands between them");
        // AWG is accepted and converted: 20 AWG is 0.519 mm2, so a shade over the 0.5 entry.
        const double awg20 = wireOuterDiameterMm(QStringLiteral("20 AWG"));
        std::printf("    (20 AWG -> %.3f mm)\n", awg20);
        check(awg20 > 1.5 && awg20 < 1.6, "wire OD: an AWG gauge is converted, not read as mm2");
        // Nothing stated is nothing returned - the caller has to report a gap, not guess.
        check(near(wireOuterDiameterMm(QString()), 0.0, 1e-12),
              "wire OD: a blank gauge gives nothing back");
    }

    // ---- The bundle those conductors make ----
    {
        const double d = 1.5;
        check(near(bundleDiameterMm({d}, 1.2, 0.0), d, 1e-9),
              "bundle: one wire is its own bundle");
        check(near(bundleDiameterMm({d, d}, 1.2, 0.0), 2 * d, 1e-9),
              "bundle: two lie side by side");
        check(near(bundleDiameterMm({d, d, d}, 1.2, 0.0), 2 * d * 1.077, 1e-9),
              "bundle: three make a triangle, not a line");
        // Four or more: the random-lay figure, 1.2 * sqrt(sum of squares).
        const double four = bundleDiameterMm({d, d, d, d}, 1.2, 0.0);
        std::printf("    (four 1.5 mm wires bundle to %.3f mm)\n", four);
        check(near(four, 1.2 * std::sqrt(4 * d * d), 1e-9),
              "bundle: four or more is the random-lay figure");
        // Doubling the count multiplies the diameter by root two, which is the whole point of the
        // formula: area adds, diameter does not.
        const double eight = bundleDiameterMm({d, d, d, d, d, d, d, d}, 1.2, 0.0);
        check(near(eight, four * std::sqrt(2.0), 1e-9),
              "bundle: twice the wires is root-two the diameter");
        // An empty run still has a body to draw.
        check(near(bundleDiameterMm({}, 1.2, 2.0), 2.0, 1e-9),
              "bundle: nothing in it still draws at the minimum");
        // A wire with no diameter is left out rather than counted as zero.
        check(near(bundleDiameterMm({d, 0.0}, 1.2, 0.0), d, 1e-9),
              "bundle: a wire of unknown size is not counted as nothing");
    }

    // ---- A segment is sized by what runs through it ----
    {
        const MechHarnessDef def = straightDef(500.0, 6, QStringLiteral("0.5"));
        const double d = segmentBundleDiameterMm(def, def.segments.front());
        const double expect = 1.2 * std::sqrt(6.0 * 1.5 * 1.5);
        std::printf("    (six 0.5 mm2 wires: bundle %.2f mm)\n", d);
        check(near(d, expect, 1e-6), "segment: the bundle follows the conductors in it");

        // Change one wire's gauge and the bundle changes with it - the reason to generate the
        // drawing rather than draw it.
        MechHarnessDef fatter = def;
        fatter.wires.front().gauge = QStringLiteral("4");
        const double d2 = segmentBundleDiameterMm(fatter, fatter.segments.front());
        std::printf("    (one of them at 4 mm2: bundle %.2f mm)\n", d2);
        check(d2 > d, "segment: a bigger conductor makes a bigger bundle");
    }

    // ---- The tree: lengths and relative angles ----
    {
        MechHarnessDef def = straightDef(300.0, 4, QStringLiteral("0.5"));
        def.draw.origin = Vec2{0.0, 0.0};
        // A branch off P1's run: another connector at +90 degrees, 100 mm out.
        MechNode p2;
        p2.id = QStringLiteral("p2");
        p2.name = QStringLiteral("P2");
        p2.kind = MechNodeKind::Connector;
        p2.parentSegment = QStringLiteral("s2");
        p2.angleDeg = 90.0;
        def.nodes.push_back(p2);
        MechSegment s2;
        s2.id = QStringLiteral("s2");
        s2.fromNode = QStringLiteral("p1");   // hangs off the far end of s1
        s2.toNode = QStringLiteral("p2");
        s2.lengthMm = 100.0;
        s2.wireIds = {QStringLiteral("w1"), QStringLiteral("w2")};
        def.segments.push_back(s2);

        const MechLayout lay = layoutMechanicalHarness(def);
        const MechPlacedNode* root = lay.node(QStringLiteral("root"));
        const MechPlacedNode* n1 = lay.node(QStringLiteral("p1"));
        const MechPlacedNode* n2 = lay.node(QStringLiteral("p2"));
        check(root && near(root->at.x, 0.0) && near(root->at.y, 0.0),
              "layout: the root sits at the origin");
        check(n1 && near(n1->at.x, 300.0, 1e-9) && near(n1->at.y, 0.0, 1e-9),
              "layout: the trunk runs its own length along x");
        check(n2 && near(n2->at.x, 300.0, 1e-6) && near(n2->at.y, 100.0, 1e-6),
              "layout: a branch at +90 turns up from where its parent ended");
        std::printf("    (layout: P1 at %.0f,%.0f  P2 at %.0f,%.0f)\n", n1 ? n1->at.x : 0,
                    n1 ? n1->at.y : 0, n2 ? n2->at.x : 0, n2 ? n2->at.y : 0);

        // The angle is RELATIVE, so turning the parent takes the child with it.
        MechHarnessDef turned = def;
        for (MechNode& n : turned.nodes)
            if (n.id == QLatin1String("p1")) n.angleDeg = 90.0;
        const MechLayout lay2 = layoutMechanicalHarness(turned);
        const MechPlacedNode* t2 = lay2.node(QStringLiteral("p2"));
        check(t2 && near(t2->at.x, -100.0, 1e-6) && near(t2->at.y, 300.0, 1e-6),
              "layout: a branch angle is measured from its parent, not from the sheet");

        // Scale shrinks the geometry and nothing else.
        MechHarnessDef half = def;
        half.draw.scale = 0.5;
        const MechPlacedNode* h1 = layoutMechanicalHarness(half).node(QStringLiteral("p1"));
        check(h1 && near(h1->at.x, 150.0, 1e-9), "layout: the scale is applied to the lengths");

        // A node hanging off a run that leads nowhere is not placed at all.
        MechHarnessDef orphan = def;
        MechNode lost;
        lost.id = QStringLiteral("lost");
        lost.name = QStringLiteral("X");
        lost.parentSegment = QStringLiteral("nosuch");
        orphan.nodes.push_back(lost);
        check(layoutMechanicalHarness(orphan).node(QStringLiteral("lost")) == nullptr,
              "layout: an orphan is left out rather than put somewhere arbitrary");
    }

    // ---- The drawing ----
    {
        MechHarnessDef def = straightDef(400.0, 6, QStringLiteral("0.5"));
        def.draw.origin = Vec2{0.0, 0.0};
        // A second branch, so there is a junction to draw.
        MechNode p2;
        p2.id = QStringLiteral("p2");
        p2.name = QStringLiteral("P2");
        p2.kind = MechNodeKind::Connector;
        p2.parentSegment = QStringLiteral("s2");
        p2.angleDeg = -90.0;
        def.nodes.push_back(p2);
        MechSegment s2;
        s2.id = QStringLiteral("s2");
        s2.fromNode = QStringLiteral("root");
        s2.toNode = QStringLiteral("p2");
        s2.lengthMm = 150.0;
        s2.wireIds = {QStringLiteral("w1"), QStringLiteral("w2")};
        def.segments.push_back(s2);
        MechNode tail;
        tail.id = QStringLiteral("tail");
        tail.name = QStringLiteral("cut");
        tail.kind = MechNodeKind::FreeEnd;
        tail.parentSegment = QStringLiteral("s3");
        tail.angleDeg = 180.0;
        def.nodes.push_back(tail);
        MechSegment s3;
        s3.id = QStringLiteral("s3");
        s3.fromNode = QStringLiteral("root");
        s3.toNode = QStringLiteral("tail");
        s3.lengthMm = 80.0;
        s3.wireIds = {QStringLiteral("w3")};
        def.segments.push_back(s3);

        const std::vector<Entity> ents = buildMechanicalHarness(def, nullptr);
        std::printf("    (drawing: %zu entity/ies - %d bundle, %d breakout, %d connector, %d dim)\n",
                    ents.size(), countRole(ents, "bundle"), countRole(ents, "breakout"),
                    countRole(ents, "connector"), countRole(ents, "dim"));
        // Two edges per run: a body is its outline, so a junction can be mitred into it.
        check(countRole(ents, "bundle") == 6, "drawing: two edges per run make up its body");
        check(countRole(ents, "connector") == 2, "drawing: a body at each connector");
        check(countRole(ents, "dim") == 3, "drawing: a length dimension on every run");
        check(countRole(ents, "dia") == 3, "drawing: the bundle diameter written on every run");
        check(countRole(ents, "breakout") == 1,
              "drawing: the point three runs meet at is an object of its own");
        check(countRole(ents, "end") == 1, "drawing: a cut end is capped");

        // The trunk's edges sit half the bundle either side of its centreline, and carry that
        // diameter as a figure.
        const Entity* plus = nullptr;
        const Entity* minus = nullptr;
        for (const Entity& e : ents)
            if (e.meta.value(QStringLiteral("mechSeg")).toString() == QLatin1String("s1") &&
                e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("bundle")) {
                if (e.meta.value(QStringLiteral("side")).toInt() > 0) plus = &e;
                else minus = &e;
            }
        const double expect = 1.2 * std::sqrt(6.0 * 1.5 * 1.5);
        check(plus && minus, "drawing: the trunk has an edge on each side");
        check(plus && near(plus->meta.value(QStringLiteral("bundleDia")).toDouble(), expect, 1e-6),
              "drawing: and each carries the bundle it is drawn from");
        check(plus && minus && near(plus->b.y - minus->b.y, expect, 1e-6),
              "drawing: they stand the bundle's diameter apart");
        check(plus && near(plus->b.x, 400.0, 1e-9),
              "drawing: and run the length of the run");

        // At the breakout - s1's near end, where three runs meet - the edges are mitred into the
        // branches instead of stopping square, so the two edges no longer start abreast. Its far end
        // is a connector with nothing to mitre into, and keeps its square cap.
        check(plus && minus && std::abs(plus->a.x - minus->a.x) > 1e-6,
              "drawing: a junction mitres the edges into the branches");
        check(plus && minus && near(plus->b.x, minus->b.x, 1e-9),
              "drawing: and a run with nothing at its end keeps a square cap");

        // Turning the options off takes the annotation off and leaves the harness.
        MechHarnessDef bare = def;
        bare.draw.dimensions = false;
        bare.draw.diameterLabels = false;
        bare.draw.connectorBodies = false;
        const std::vector<Entity> plain = buildMechanicalHarness(bare, nullptr);
        check(countRole(plain, "dim") == 0 && countRole(plain, "dia") == 0 &&
                  countRole(plain, "connector") == 0,
              "drawing: what is drawn on it is optional");
        check(countRole(plain, "bundle") == 6, "drawing: the harness itself is not");

        // Exaggerating the bundle widens the body and leaves the lengths where they are - and says
        // so on the drawing, because a width scaled off the print would otherwise be wrong.
        MechHarnessDef big = def;
        big.draw.diameterExaggeration = 4.0;
        const std::vector<Entity> wide = buildMechanicalHarness(big, nullptr);
        auto edgeGap = [](const std::vector<Entity>& list) {
            const Entity* p = nullptr;
            const Entity* m = nullptr;
            for (const Entity& e : list)
                if (e.meta.value(QStringLiteral("mechSeg")).toString() == QLatin1String("s1") &&
                    e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("bundle")) {
                    if (e.meta.value(QStringLiteral("side")).toInt() > 0) p = &e;
                    else m = &e;
                }
            return (p && m) ? p->a.y - m->a.y : 0.0;
        };
        std::printf("    (bundle drawn %.2f at full size, %.2f exaggerated x4)\n",
                    edgeGap(ents), edgeGap(wide));
        check(near(edgeGap(wide), edgeGap(ents) * 4.0, 1e-6),
              "drawing: exaggeration multiplies the body's width");
        // The centreline is what the length is: exaggerating the body moves the mitres, and must not
        // move where the run ends.
        const MechPlacedSegment* asDrawn = layoutMechanicalHarness(def).segment(QStringLiteral("s1"));
        const MechPlacedSegment* exaggerated =
            layoutMechanicalHarness(big).segment(QStringLiteral("s1"));
        check(asDrawn && exaggerated && near(asDrawn->b.x, exaggerated->b.x, 1e-9) &&
                  near(asDrawn->b.y, exaggerated->b.y, 1e-9),
              "drawing: and leaves the length exactly as it was");
        check(countRole(wide, "note") == 1,
              "drawing: an exaggerated width is noted on the face of the drawing");
        check(countRole(ents, "note") == 0, "drawing: at full size there is nothing to note");

        // An empty definition draws nothing rather than something at the origin.
        MechHarnessDef empty;
        check(buildMechanicalHarness(empty, nullptr).empty(),
              "drawing: nothing defined draws nothing");
    }

    // ---- What is wrong with it ----
    {
        MechHarnessDef def = straightDef(0.0, 2, QStringLiteral("0.5"));   // no length
        def.wires.push_back(wireOf(QStringLiteral("wx"), QString()));      // no gauge
        def.segments.front().wireIds.push_back(QStringLiteral("wx"));
        const std::vector<MechIssue> issues = validateMechanicalHarness(def, nullptr);
        bool sawLength = false, sawGauge = false;
        for (const MechIssue& i : issues) {
            if (i.kind == MechIssueKind::Gap && i.property == QLatin1String("Length"))
                sawLength = true;
            if (i.kind == MechIssueKind::Gap && i.property == QLatin1String("Diameter"))
                sawGauge = true;
        }
        std::printf("    (issues: %zu on an unfinished definition)\n", issues.size());
        check(sawLength, "issues: a run with no length is a gap");
        check(sawGauge, "issues: a conductor with no size is a gap");

        // A run that routes a conductor nothing knows about is a fault, not a gap.
        MechHarnessDef bad = straightDef(100.0, 1, QStringLiteral("0.5"));
        bad.segments.front().wireIds.push_back(QStringLiteral("ghost"));
        bool sawGhost = false;
        for (const MechIssue& i : validateMechanicalHarness(bad, nullptr))
            if (i.kind == MechIssueKind::Violation && i.message.contains(QLatin1String("ghost")))
                sawGhost = true;
        check(sawGhost, "issues: routing a conductor that does not exist is a fault");

        // A finished definition is quiet.
        const MechHarnessDef ok = straightDef(250.0, 4, QStringLiteral("0.5"));
        int hard = 0;
        for (const MechIssue& i : validateMechanicalHarness(ok, nullptr))
            if (i.kind != MechIssueKind::Warning) ++hard;
        check(hard == 0, "issues: a finished definition has nothing to report");
    }

    // ---- Fit: a fitting against the bundle it goes over ----
    {
        // A boot that takes 4.8 to 12.7 mm on the cable side.
        LibraryPart boot;
        boot.kind = PartKind::HeatshrinkBoot;
        boot.mfrPn = QStringLiteral("202A132-25-0");
        boot.params[QStringLiteral("cableEntryMin")] = QStringLiteral("4.8");
        boot.params[QStringLiteral("cableEntryMax")] = QStringLiteral("12.7");

        QString why;
        bool known = false;
        check(mechFittingFits(boot, 9.0, why, &known) && known && why.isEmpty(),
              "fit: a 9 mm bundle goes into a 4.8-12.7 mm boot");
        check(!mechFittingFits(boot, 14.0, why, &known),
              "fit: a 14 mm bundle does not");
        std::printf("    (fit: %s)\n", qPrintable(why));
        check(why.contains(QLatin1String("14.0")) && why.contains(QLatin1String("12.7")),
              "fit: and it says both figures, so the fix is obvious");
        check(!mechFittingFits(boot, 3.0, why, &known) &&
                  why.contains(QLatin1String("recovers")),
              "fit: too small a bundle means it will not recover onto it");

        // A backshell is rigid: too small a bundle is a different sentence.
        LibraryPart shell;
        shell.kind = PartKind::Backshell;
        shell.params[QStringLiteral("cableEntryMin")] = QStringLiteral("8.0");
        shell.params[QStringLiteral("cableEntryMax")] = QStringLiteral("12.0");
        check(!mechFittingFits(shell, 5.0, why, &known) &&
                  why.contains(QLatin1String("at least")),
              "fit: a rigid entry that is too big for the bundle is put differently");

        // A part that does not say what it fits is a gap, not a pass.
        LibraryPart silent;
        silent.kind = PartKind::HeatshrinkBoot;
        check(mechFittingFits(silent, 9.0, why, &known) && !known,
              "fit: a part that states no window is reported as unknown, not as fitting");

        // And the validator turns that into the right kind of issue.
        PartsCatalog cat;
        cat.setRemembering(false);
        LibraryPart& saved = cat.active().add(PartKind::HeatshrinkBoot);
        saved.params = boot.params;
        saved.mfrPn = boot.mfrPn;
        MechFitting fit;
        fit.libPart = saved.id;
        fit.kind = PartKind::HeatshrinkBoot;
        fit.name = QStringLiteral("Boot");
        check(cat.byId(saved.id) != nullptr,
              "fit: a part added to the working library can be read back");

        // Eight 0.5 mm2 wires make 5.1 mm, which is inside the boot's 4.8-12.7 mm window.
        MechHarnessDef def = straightDef(200.0, 8, QStringLiteral("0.5"));
        for (MechNode& n : def.nodes)
            if (n.id == QLatin1String("p1")) n.fittings.push_back(fit);
        std::printf("    (eight 0.5 mm2 into a 4.8-12.7 mm boot: bundle %.2f mm)\n",
                    segmentBundleDiameterMm(def, def.segments.front()));
        int violations = 0;
        for (const MechIssue& i : validateMechanicalHarness(def, &cat))
            if (i.kind == MechIssueKind::Violation) ++violations;
        check(violations == 0, "fit: a boot that takes the bundle passes through the validator");

        // Twelve 2.5 mm2 wires make 12.9 mm, which will not go in - and the validator has to say
        // which fitting and why, not merely that something is wrong.
        MechHarnessDef fat = straightDef(200.0, 12, QStringLiteral("2.5"));
        for (MechNode& n : fat.nodes)
            if (n.id == QLatin1String("p1")) n.fittings.push_back(fit);
        bool named = false;
        for (const MechIssue& i : validateMechanicalHarness(fat, &cat))
            if (i.kind == MechIssueKind::Violation && i.message.contains(QLatin1String("Boot")) &&
                i.message.contains(QLatin1String("takes up to")))
                named = true;
        std::printf("    (twelve 2.5 mm2: bundle %.2f mm)\n",
                    segmentBundleDiameterMm(fat, fat.segments.front()));
        check(named, "fit: a bundle too big for the boot is a violation naming the boot");

        // A fitting whose part is not in any connected library is its own fault, said plainly.
        MechHarnessDef missing = straightDef(200.0, 8, QStringLiteral("0.5"));
        MechFitting ghost = fit;
        ghost.libPart = QStringLiteral("no-such-part");
        for (MechNode& n : missing.nodes)
            if (n.id == QLatin1String("p1")) n.fittings.push_back(ghost);
        bool saidMissing = false;
        for (const MechIssue& i : validateMechanicalHarness(missing, &cat))
            if (i.message.contains(QLatin1String("not in any connected library")))
                saidMissing = true;
        check(saidMissing, "fit: a fitting pointing at nothing is reported as that, not as a misfit");
    }

    // ---- Read off the schematic ----
    {
        Document doc;
        doc.entities().clear();

        auto addConn = [&doc](const QString& des, double x, bool mirrorX) {
            ConnectorSpec spec = defaultConnectorSpec(2);
            spec.pitch = 20.0;
            spec.pinLength = 30.0;
            spec.designator = des;
            spec.mirrorX = mirrorX;
            Entity e;
            e.type = EntityType::Connector;
            e.id = QStringLiteral("c_%1").arg(des);
            e.a = Vec2{x, 0.0};
            writeConnectorSpec(e.meta, spec);
            doc.entities().push_back(e);
            return connectorGeometry(e.a, spec);
        };
        const ConnectorGeometry g1 = addConn(QStringLiteral("P1"), 0.0, false);
        const ConnectorGeometry g2 = addConn(QStringLiteral("P2"), 400.0, true);

        auto addWire = [&doc](const QString& id, const Vec2& a, const Vec2& b, const char* mm2) {
            Entity e;
            e.type = EntityType::Wire;
            e.id = id;
            e.points = {a, b};
            e.meta[QStringLiteral("wireLabel")] = id;
            e.meta[QStringLiteral("wireGauge")] = QString::fromLatin1(mm2);
            doc.entities().push_back(e);
        };
        addWire(QStringLiteral("W1"), g1.pinEnd[0], g2.pinEnd[0], "0.5");
        addWire(QStringLiteral("W2"), g1.pinEnd[1], g2.pinEnd[1], "0.5");

        const MechHarnessDef def = mechFromDrawing(doc);
        std::printf("    (from the schematic: %zu wire(s), %zu node(s), %zu run(s))\n",
                    def.wires.size(), def.nodes.size(), def.segments.size());
        check(def.wires.size() == 2, "from schematic: both conductors are picked up");
        check(def.segments.size() == 2, "from schematic: a run out to each connector");
        check(def.nodes.size() == 3, "from schematic: a breakout and the two connectors");
        const MechNode* root = def.rootNode();
        check(root && root->kind == MechNodeKind::Breakout,
              "from schematic: the middle is a breakout");

        // Each run carries the conductors that land on its own connector.
        for (const MechSegment& s : def.segments)
            check(s.wireIds.size() == 2, "from schematic: the wires are routed into the branches");
        // The gauge came across, so the bundle can be worked out at once.
        const double d = segmentBundleDiameterMm(def, def.segments.front());
        check(near(d, 2 * 1.5, 1e-9), "from schematic: and the bundle follows from their gauges");

        // A designator becomes the node's name, so the drawing is labelled the way the schematic is.
        bool sawP1 = false, sawP2 = false;
        for (const MechNode& n : def.nodes) {
            if (n.name == QLatin1String("P1")) sawP1 = true;
            if (n.name == QLatin1String("P2")) sawP2 = true;
        }
        check(sawP1 && sawP2, "from schematic: the connectors keep their designators");

        // And it generates: a body per run, a connector at each end.
        const std::vector<Entity> ents = buildMechanicalHarness(def, nullptr);
        check(countRole(ents, "bundle") == 4 && countRole(ents, "connector") == 2,
              "from schematic: the definition it produces draws");

        // Conductors that say which cable they are cores of make the run they share a CABLE, without
        // anything in the library having to be involved: the schematic already knows they run together
        // inside one jacket, and what that jacket measures is then the one thing left to ask for.
        {
            Document cabled;
            cabled.entities().clear();
            for (const Entity& e : doc.entities()) {
                Entity copy = e;
                if (copy.type == EntityType::Wire)
                    copy.meta[QStringLiteral("bundle")] = QStringLiteral("W1");
                cabled.entities().push_back(copy);
            }
            const MechHarnessDef cdef = mechFromDrawing(cabled);
            bool named = false;
            for (const MechSegment& s : cdef.segments)
                if (s.cableName == QLatin1String("W1") && s.cableWithoutJacket()) named = true;
            check(named, "from schematic: cores of one cable make the run a cable");
            bool asked = false;
            for (const MechIssue& i : validateMechanicalHarness(cdef, nullptr))
                if (i.property == QLatin1String("Jacket diameter")) asked = true;
            check(asked, "from schematic: and the jacket diameter is what it then asks for");
        }

        // Nothing drawn gives an empty definition rather than a broken one.
        Document blank;
        blank.entities().clear();
        const MechHarnessDef none = mechFromDrawing(blank);
        check(none.nodes.empty() && none.segments.empty(),
              "from schematic: an empty sheet gives an empty definition");
    }

    // ---- A cable is its JACKET, not the bundle its cores would make loose ----
    {
        // Five conductors that add up to a small bundle, inside a 5 mm cable. The run is a 5 mm run:
        // that is what it measures, what it is bought as, and what a boot on the end has to pass.
        MechHarnessDef def = straightDef(300.0, 5, QStringLiteral("0.08"));
        def.draw.origin = Vec2{0.0, 0.0};
        MechSegment& s = def.segments.front();
        const double bundle = segmentBundleDiameterMm(def, s);
        check(bundle > 0.0 && bundle < 5.0, "cable: the cores make a bundle smaller than the jacket");
        s.jacketOdMm = 5.0;
        s.cableName = QStringLiteral("W1");
        check(near(segmentBodyDiameterMm(def, s), 5.0),
              "cable: the run is drawn at the jacket, whatever the cores add up to");
        check(near(segmentBundleDiameterMm(def, s), bundle),
              "cable: and the bundle inside it is still the bundle");
        // Whole jacket, both ends: one diameter from end to end.
        check(segmentProfile(def, s).size() == 2, "cable: an unstripped cable is one diameter");
        check(near(segmentEndDiameterMm(def, s, true), 5.0),
              "cable: with the jacket at the end too");

        // Cut back 30 mm at the connector end: bare wires, a cone, then the jacket.
        s.stripToMm = 30.0;
        check(near(segmentEndDiameterMm(def, s, /*atToEnd=*/true), bundle),
              "cable: where the jacket is cut back the run is the bundle");
        check(near(segmentEndDiameterMm(def, s, /*atToEnd=*/false), 5.0),
              "cable: and it is still the jacket at the other end");
        const std::vector<MechStation> prof = segmentProfile(def, s);
        check(prof.size() == 4, "cable: jacket, the end of the jacket, the cone, the bundle");
        if (prof.size() == 4) {
            check(near(prof[0].atMm, 0.0) && near(prof[0].diameterMm, 5.0),
                  "cable: it starts as a cable");
            check(near(prof[1].atMm, 270.0) && near(prof[1].diameterMm, 5.0),
                  "cable: and is a cable up to where it is cut");
            check(prof[2].atMm > 270.0 && prof[2].atMm <= 300.0 &&
                      near(prof[2].diameterMm, bundle),
                  "cable: the wires spread out of it over a cone, not a step");
            check(near(prof[3].atMm, 300.0) && near(prof[3].diameterMm, bundle),
                  "cable: and run bare to the end");
        }
        // Stripped further than it is long: bare wire from end to end rather than a crossed cone.
        MechSegment allBare = s;
        allBare.stripFromMm = 200.0;
        allBare.stripToMm = 200.0;
        check(segmentProfile(def, allBare).size() == 2,
              "cable: strip it all away and it is bare wire, not a knot");

        // The drawing: an edge with knees in it, the jacket at the whole end and the bundle at the
        // stripped one.
        const std::vector<Entity> ents = buildMechanicalHarness(def, nullptr);
        const Entity* plus = nullptr;
        for (const Entity& e : ents)
            if (e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("bundle") &&
                e.meta.value(QStringLiteral("side")).toInt() > 0)
                plus = &e;
        check(plus && plus->points.size() == 4,
              "cable: the outline carries a knee at every change of diameter");
        if (plus && plus->points.size() == 4) {
            check(near(plus->points.front().y, 2.5),
                  "cable: half the jacket off the centreline where it is whole");
            check(near(plus->points.back().y, bundle / 2.0),
                  "cable: and half the bundle where the jacket is off");
        }
        // What is written on it says both figures, because a boot is chosen against the pair.
        bool bothFigures = false;
        for (const Entity& e : ents) {
            if (e.meta.value(QStringLiteral("mechRole")).toString() != QLatin1String("dia")) continue;
            const QString label = e.meta.value(QStringLiteral("text")).toString();
            if (label.contains(QLatin1String("cable")) && label.contains(QLatin1String("bundle")))
                bothFigures = true;
        }
        check(bothFigures, "cable: the label gives the cable and the bundle inside it");

        // What each fitting has to go over: the cable enters a boot, so a boot takes 5 mm; a braid is
        // combed over the conductors, so it takes the bundle.
        check(near(fittingFitDiameterMm(def, &s, PartKind::HeatshrinkBoot, true, true), 5.0),
              "cable: a boot at the end has to pass the jacket behind it");
        check(near(fittingFitDiameterMm(def, &s, PartKind::Backshell, true, true), 5.0),
              "cable: so does a backshell");
        check(near(fittingFitDiameterMm(def, &s, PartKind::ShieldBraid, true, true), bundle),
              "cable: braid over the bare wires goes on the bundle");
        check(near(fittingFitDiameterMm(def, &s, PartKind::Sleeve, false, true), 5.0),
              "cable: and a sleeve along the run goes over the cable");

        // A jacket that will not hold its own cores is one of the two figures being wrong.
        MechHarnessDef silly = def;
        silly.segments.front().jacketOdMm = 0.5;
        bool sawTooSmall = false, sawNoStrip = false;
        for (const MechIssue& i : validateMechanicalHarness(silly, nullptr))
            if (i.message.contains(QLatin1String("one of the two figures is wrong")))
                sawTooSmall = true;
        check(sawTooSmall, "cable: a jacket narrower than its cores is reported");
        // And a jacket that runs into the connector is a figure nobody has given yet.
        MechHarnessDef unstripped = def;
        unstripped.segments.front().stripToMm = 0.0;
        for (const MechIssue& i : validateMechanicalHarness(unstripped, nullptr))
            if (i.property == QLatin1String("Wires without jacket")) sawNoStrip = true;
        check(sawNoStrip, "cable: and so is a jacket that runs into the contacts");
    }

    // ---- A cable whose jacket diameter nobody has given ----
    {
        // The case that matters: the harness KNOWS these five conductors are cores of one cable, and
        // nothing says what that cable measures. It must not be sized as a Ø2.4 bundle of loose wires -
        // that is not what a boot on the end of it has to pass - and it must not silently hide every
        // boot in the library behind a check made against the wrong figure.
        MechHarnessDef def = straightDef(300.0, 5, QStringLiteral("0.08"));
        MechSegment& s = def.segments.front();
        s.cableName = QStringLiteral("Cable, test 2");
        const double bundle = segmentBundleDiameterMm(def, s);
        check(s.cableWithoutJacket(), "unknown jacket: the run is a cable with no jacket figure");
        check(!s.isCable(), "unknown jacket: and so is not drawn as one");
        check(near(fittingFitDiameterMm(def, &s, PartKind::HeatshrinkBoot, true, true), 0.0),
              "unknown jacket: what a boot has to pass is NOT KNOWN, not the bundle");
        check(near(fittingFitDiameterMm(def, &s, PartKind::Sleeve, false, true), 0.0),
              "unknown jacket: nor what a sleeve along it goes over");
        check(near(fittingFitDiameterMm(def, &s, PartKind::ShieldBraid, true, true), bundle),
              "unknown jacket: braid over the bare conductors is still the bundle");

        bool asked = false;
        for (const MechIssue& i : validateMechanicalHarness(def, nullptr))
            if (i.property == QLatin1String("Jacket diameter") &&
                i.message.contains(QLatin1String("Cable, test 2")))
                asked = true;
        check(asked, "unknown jacket: the figure is asked for by the cable's name");

        // A boot fitted there is not reported as a misfit against a figure nobody has: the missing
        // figure is the fault, and it is reported once.
        MechFitting boot;
        boot.kind = PartKind::HeatshrinkBoot;
        boot.name = QStringLiteral("boot");
        boot.idMm = 3.0;             // smaller than the cores would need, let alone the jacket
        for (MechNode& n : def.nodes)
            if (n.id == QLatin1String("p1")) n.fittings.push_back(boot);
        int violations = 0;
        for (const MechIssue& i : validateMechanicalHarness(def, nullptr))
            if (i.kind == MechIssueKind::Violation) ++violations;
        check(violations == 0,
              "unknown jacket: nothing is judged against a diameter the drawing does not have");

        // And once the figure is in, everything is checked against it - 5 mm, not 2.4.
        s.jacketOdMm = 5.0;
        check(!s.cableWithoutJacket() && s.isCable(), "unknown jacket: filling it in settles it");
        check(near(fittingFitDiameterMm(def, &s, PartKind::HeatshrinkBoot, true, true), 5.0),
              "unknown jacket: and then a boot is checked at the cable's 5 mm");
        bool tooSmall = false;
        for (const MechIssue& i : validateMechanicalHarness(def, nullptr))
            if (i.kind == MechIssueKind::Violation && i.message.contains(QLatin1String("bore")))
                tooSmall = true;
        check(tooSmall, "unknown jacket: a boot that will not pass the cable is then a violation");
    }

    // ---- A cable trunk, a spread point, and bare wires after it ----
    {
        // The shape a cable harness actually is: a Ø5 cable in, the jacket cut at the spread point, and
        // only the conductors carrying on into the branches. The trunk is a 5 mm run, the branches are
        // bundles, and each is sized on its own body.
        MechHarnessDef def;
        def.draw.origin = Vec2{0.0, 0.0};
        MechNode in;
        in.id = QStringLiteral("p1");
        in.name = QStringLiteral("P1");
        in.kind = MechNodeKind::Connector;
        def.nodes.push_back(in);
        MechNode br;
        br.id = QStringLiteral("br");
        br.name = QStringLiteral("BR1");
        br.kind = MechNodeKind::Breakout;
        br.parentSegment = QStringLiteral("trunk");
        def.nodes.push_back(br);

        MechSegment trunk;
        trunk.id = QStringLiteral("trunk");
        trunk.fromNode = QStringLiteral("p1");
        trunk.toNode = QStringLiteral("br");
        trunk.lengthMm = 200.0;
        trunk.jacketOdMm = 5.0;
        trunk.cableName = QStringLiteral("W1");
        trunk.stripFromMm = 40.0;    // into the connector
        trunk.stripToMm = 30.0;      // and cut at the spread point
        for (int i = 0; i < 4; ++i) {
            const QString id = QStringLiteral("c%1").arg(i + 1);
            def.wires.push_back(wireOf(id, QStringLiteral("0.5")));
            trunk.wireIds.push_back(id);
        }
        def.segments.push_back(trunk);

        // Two branches of loose wires out of the spread point, two conductors each.
        for (int b = 0; b < 2; ++b) {
            MechNode n;
            n.id = QStringLiteral("e%1").arg(b + 1);
            n.name = QStringLiteral("P%1").arg(b + 2);
            n.kind = MechNodeKind::Connector;
            n.parentSegment = QStringLiteral("br%1").arg(b + 1);
            n.angleDeg = b == 0 ? 30.0 : -30.0;
            def.nodes.push_back(n);
            MechSegment s;
            s.id = n.parentSegment;
            s.fromNode = QStringLiteral("br");
            s.toNode = n.id;
            s.lengthMm = 150.0;
            s.wireIds = {QStringLiteral("c%1").arg(b * 2 + 1), QStringLiteral("c%1").arg(b * 2 + 2)};
            def.segments.push_back(s);
        }

        // A connector at the START of the trunk is a connector like any other: no run arrives at it, so
        // its own run is the one that leaves, and the jacket cut back there is that run's.
        {
            const MechNode* p1 = def.node(QStringLiteral("p1"));
            bool atTo = true;
            const MechSegment* atP1 = p1 ? nodeRunSegment(def, *p1, &atTo) : nullptr;
            check(atP1 && atP1->id == QLatin1String("trunk") && !atTo,
                  "spread point: the run at the trunk's first connector is the trunk itself");
            check(p1 && near(nodeBareLengthMm(def, *p1), 40.0),
                  "spread point: and the wires without jacket there are its own 40 mm");
            const MechNode* spread = def.node(QStringLiteral("br"));
            check(spread && near(nodeBareLengthMm(def, *spread), 30.0),
                  "spread point: at the spread point itself, the 30 mm cut there");
            check(spread && nodeRunSegment(def, *spread) != nullptr,
                  "spread point: a breakout with a run arriving at it has one");
        }

        const MechSegment& tr = def.segments.front();
        const MechSegment& branch = def.segments[1];
        const double branchBundle = segmentBundleDiameterMm(def, branch);
        check(near(segmentBodyDiameterMm(def, tr), 5.0),
              "spread point: the trunk is the 5 mm cable");
        std::printf("    (spread point: trunk checked at %.1f mm, a branch at %.1f mm)\n",
                    fittingFitDiameterMm(def, &tr, PartKind::Heatshrink, false, true),
                    fittingFitDiameterMm(def, &branch, PartKind::Heatshrink, false, true));
        check(near(fittingFitDiameterMm(def, &tr, PartKind::Heatshrink, false, true), 5.0),
              "spread point: heatshrink over the trunk is checked at 5 mm");
        check(!branch.isCable() && near(segmentBodyDiameterMm(def, branch), branchBundle),
              "spread point: past it the jacket is gone and a branch is its bundle");
        check(near(fittingFitDiameterMm(def, &branch, PartKind::Heatshrink, false, true),
                   branchBundle),
              "spread point: so heatshrink on a branch is checked at the bundle diameter");
        // The trunk narrows at both ends and the branches do not.
        check(segmentProfile(def, tr).size() == 6,
              "spread point: the trunk is bundle, cone, jacket, cone, bundle");
        check(segmentProfile(def, branch).size() == 2, "spread point: a branch is one diameter");
        // It draws: the trunk's outline carries its knees, and the junction is mitred on the stripped
        // bundle rather than on the jacket that stopped 30 mm short of it.
        const std::vector<Entity> ents = buildMechanicalHarness(def, nullptr);
        const Entity* trunkEdge = nullptr;
        for (const Entity& e : ents)
            if (e.meta.value(QStringLiteral("mechSeg")).toString() == QLatin1String("trunk") &&
                e.meta.value(QStringLiteral("side")).toInt() > 0)
                trunkEdge = &e;
        check(trunkEdge && trunkEdge->points.size() == 6,
              "spread point: and the trunk is drawn with every change of diameter in it");
    }

    // ---- What every connector end can carry ----
    {
        MechHarnessDef def = straightDef(300.0, 5, QStringLiteral("0.08"));
        def.draw.origin = Vec2{0.0, 0.0};
        def.draw.dimensions = false;
        def.draw.diameterLabels = false;
        MechSegment& s = def.segments.front();
        s.jacketOdMm = 5.0;
        s.stripToMm = 60.0;               // 60 mm of wire out of the jacket
        const double bundle = segmentBundleDiameterMm(def, s);

        auto fitting = [](PartKind kind, double lengthMm) {
            MechFitting f;
            f.id = QStringLiteral("end-%1").arg(partKindKey(kind));
            f.kind = kind;
            f.name = partKindName(kind);
            f.lengthMm = lengthMm;
            return f;
        };
        for (MechNode& n : def.nodes) {
            if (n.id != QLatin1String("p1")) continue;
            n.fittings.push_back(fitting(PartKind::Backshell, 15.0));
            n.fittings.push_back(fitting(PartKind::HeatshrinkBoot, 12.0));
            n.fittings.push_back(fitting(PartKind::ShieldBraid, 0.0));
            n.fittings.push_back(fitting(PartKind::Heatshrink, 0.0));
        }
        check(near(nodeBareLengthMm(def, *def.node(QStringLiteral("p1"))), 60.0),
              "ends: the bare length is the strip at that end of the run");

        // The run goes out along +x and the connector is at (300,0), so a fitting sits back from there.
        std::vector<Bounds> boxes;
        for (const Entity& e : buildMechanicalHarness(def, nullptr))
            if (e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("fitting"))
                boxes.push_back(e.bounds());
        check(boxes.size() == 4, "ends: a backshell, a boot, braid and heatshrink, all four drawn");
        if (boxes.size() == 4) {
            // The bodies stack back from the connector, in the order they are built in.
            check(near(boxes[0].max.x, 300.0) && near(boxes[0].min.x, 285.0),
                  "ends: the backshell is on the connector");
            check(near(boxes[1].max.x, 285.0) && near(boxes[1].min.x, 273.0),
                  "ends: the boot is behind the backshell, not on top of it");
            // The braid covers the bare stretch that is left - as far back as the jacket, no further.
            check(near(boxes[2].max.x, 273.0) && near(boxes[2].min.x, 240.0),
                  "ends: braid covers the wires from the boot back to the jacket");
            check(near(boxes[3].max.x, 273.0) && near(boxes[3].min.x, 240.0),
                  "ends: heatshrink goes OVER the braid rather than after it");
            const double braidOd = boxes[2].max.y - boxes[2].min.y;
            const double shrinkOd = boxes[3].max.y - boxes[3].min.y;
            check(braidOd > bundle && shrinkOd > braidOd,
                  "ends: each layer is a little bigger than the one under it");
        }
    }

    // ---- Heatshrink where a conductor changes gauge ----
    {
        // 0.5 mm² out to a junction, 1.0 mm² on from it: the wires that arrive are not the wires that
        // leave, and they are not the same size - so there is a splice inside.
        MechHarnessDef def = straightDef(200.0, 2, QStringLiteral("0.5"));
        def.draw.origin = Vec2{0.0, 0.0};
        for (MechNode& n : def.nodes)
            if (n.id == QLatin1String("p1")) n.kind = MechNodeKind::Breakout;
        def.wires.push_back(wireOf(QStringLiteral("w3"), QStringLiteral("1.0")));
        def.wires.push_back(wireOf(QStringLiteral("w4"), QStringLiteral("1.0")));
        MechNode p2;
        p2.id = QStringLiteral("p2");
        p2.name = QStringLiteral("P2");
        p2.kind = MechNodeKind::Connector;
        p2.parentSegment = QStringLiteral("s2");
        def.nodes.push_back(p2);
        MechSegment s2;
        s2.id = QStringLiteral("s2");
        s2.fromNode = QStringLiteral("p1");
        s2.toNode = QStringLiteral("p2");
        s2.lengthMm = 150.0;
        s2.wireIds = {QStringLiteral("w3"), QStringLiteral("w4")};
        def.segments.push_back(s2);

        const std::vector<MechGaugeChange> changes = mechGaugeChanges(def);
        check(changes.size() == 1, "gauge change: the junction the size changes at is found");
        if (changes.size() == 1) {
            check(changes.front().nodeId == QLatin1String("p1"),
                  "gauge change: and it is the right junction");
            check(changes.front().fromGauges == QLatin1String("0.5, 0.5") &&
                      changes.front().toGauges == QLatin1String("1, 1"),
                  "gauge change: it says what changes into what");
        }
        // It is drawn without being asked for, because it follows from the conductors.
        const std::vector<Entity> ents = buildMechanicalHarness(def, nullptr);
        check(countRole(ents, "splice") == 1, "gauge change: heatshrink is drawn over the splice");
        // And it is listed, so it reaches the bill of materials rather than only the picture.
        bool said = false;
        for (const MechIssue& i : validateMechanicalHarness(def, nullptr))
            if (i.message.contains(QLatin1String("there is a splice"))) said = true;
        check(said, "gauge change: and it is reported");

        // Turned off, the splice is bare - which is a fault, not a preference.
        MechHarnessDef bare = def;
        bare.draw.spliceHeatshrink = false;
        check(countRole(buildMechanicalHarness(bare, nullptr), "splice") == 0,
              "gauge change: it can be turned off");
        bool complained = false;
        for (const MechIssue& i : validateMechanicalHarness(bare, nullptr))
            if (i.kind == MechIssueKind::Violation &&
                i.message.contains(QLatin1String("nothing covers it")))
                complained = true;
        check(complained, "gauge change: and then nothing covering it is a fault");

        // A plain breakout carries the SAME conductors on into its branches: nothing is spliced, so
        // nothing is covered.
        MechHarnessDef plain = def;
        for (MechSegment& seg : plain.segments)
            if (seg.id == QLatin1String("s2"))
                seg.wireIds = {QStringLiteral("w1"), QStringLiteral("w2")};
        check(mechGaugeChanges(plain).empty(),
              "gauge change: a breakout that divides a bundle is not a transition");
        check(countRole(buildMechanicalHarness(plain, nullptr), "splice") == 0,
              "gauge change: and gets no heatshrink");
    }

    // ---- The bill of materials, and the balloons that point at it ----
    {
        // The rule the released drawings keep without exception: every balloon on the assembly is a row
        // in the table, and every row in the table is ballooned somewhere on the assembly. One row per
        // PART, not per place it is used.
        MechHarnessDef def = straightDef(300.0, 4, QStringLiteral("0.5"));
        def.draw.origin = Vec2{0.0, 0.0};
        def.draw.balloons = true;
        auto fitting = [](PartKind kind, double lengthMm) {
            MechFitting f;
            f.id = QStringLiteral("f-%1").arg(partKindKey(kind));
            f.kind = kind;
            f.name = partKindName(kind);
            f.lengthMm = lengthMm;
            return f;
        };
        // A backshell and a boot at the connector, and heatshrink at BOTH ends of the run - the same
        // part in two places, which must come out as one row of 2 x its length.
        for (MechNode& n : def.nodes) {
            if (n.id == QLatin1String("p1")) {
                n.fittings.push_back(fitting(PartKind::Backshell, 15.0));
                n.fittings.push_back(fitting(PartKind::HeatshrinkBoot, 12.0));
                n.openLengthMm = 20.0;
            }
            if (n.id == QLatin1String("root")) n.fittings.push_back(fitting(PartKind::Heatshrink, 30.0));
        }
        def.segments.front().coverings.push_back(fitting(PartKind::Heatshrink, 30.0));

        const std::vector<MechBomLine> bom = mechBom(def, nullptr);
        std::printf("    (bom: %zu line(s))\n", bom.size());
        for (const MechBomLine& l : bom)
            std::printf("      %2d  %-22s %8.1f %s\n", l.item, qPrintable(l.description), l.qty,
                        qPrintable(l.unit));
        // Backshell, boot, wire, heatshrink: four lines, numbered from 1 with no gaps.
        check(bom.size() == 4, "bom: one line per part, whatever order they were fitted in");
        for (int i = 0; i < static_cast<int>(bom.size()); ++i)
            check(bom[static_cast<size_t>(i)].item == i + 1, "bom: numbered down the table from 1");
        const MechBomLine* shrink = nullptr;
        const MechBomLine* wire = nullptr;
        const MechBomLine* boot = nullptr;
        for (const MechBomLine& l : bom) {
            if (l.kind == PartKind::Heatshrink) shrink = &l;
            if (l.kind == PartKind::Wire) wire = &l;
            if (l.kind == PartKind::HeatshrinkBoot) boot = &l;
        }
        // The order is by what the thing IS, which is the order the released drawings list them in:
        // what dresses the connector, then the wire, then what goes over the wire.
        check(bom.front().kind == PartKind::Backshell,
              "bom: what dresses the connector is listed first");
        check(wire && shrink && wire->item < shrink->item,
              "bom: the wire before the heatshrink that goes over it");
        check(wire && boot && wire->item < boot->item, "bom: and before the boot");
        check(shrink && near(shrink->qty, 60.0),
              "bom: heatshrink used in two places is ONE row of 60 mm, the way it is bought");
        check(shrink && shrink->unit == QLatin1String("mm"),
              "bom: anything cut off a reel is counted by length");
        check(shrink && shrink->where.size() == 2, "bom: and the row says where it is used");
        check(wire && near(wire->qty, 4 * 300.0) && wire->unit == QLatin1String("mm"),
              "bom: four conductors 300 mm long are 1200 mm of wire");
        const MechBomLine* backshell = nullptr;
        for (const MechBomLine& l : bom)
            if (l.kind == PartKind::Backshell) backshell = &l;
        check(backshell && near(backshell->qty, 1.0) &&
                  backshell->unit == QLatin1String("each"),
              "bom: a thing is counted each, not by its length");

        // The balloons on the drawing carry those item numbers - not a running count.
        const std::vector<Entity> ents = buildMechanicalHarness(def, nullptr);
        QStringList balloonText;
        for (const Entity& e : ents)
            if (e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("balloon"))
                balloonText << e.meta.value(QStringLiteral("text")).toString();
        std::printf("    (balloons: %s)\n", qPrintable(balloonText.join(QStringLiteral(" "))));
        check(balloonText.size() == 4, "balloons: one on every part fitted");
        check(!balloonText.contains(QStringLiteral("?")),
              "balloons: every one of them found its row");
        // The two heatshrinks - one at the end, one along the run - are the same part, so they carry the
        // SAME number. A running count would have called them 3 and 4.
        int shrinkItem = shrink ? shrink->item : -1;
        check(balloonText.count(QString::number(shrinkItem)) == 2,
              "balloons: the same part in two places is the same item number twice");
        for (const QString& s : balloonText) {
            bool found = false;
            for (const MechBomLine& l : bom)
                if (QString::number(l.item) == s) found = true;
            check(found, "balloons: nothing is ballooned that is not in the table");
        }
        // A cable's cores are inside the cable and are not bought twice.
        MechHarnessDef cabled = def;
        cabled.segments.front().jacketOdMm = 5.0;
        cabled.segments.front().cableName = QStringLiteral("W1");
        bool hasWireRow = false, hasCableRow = false;
        for (const MechBomLine& l : mechBom(cabled, nullptr)) {
            if (l.kind == PartKind::Wire) hasWireRow = true;
            if (l.kind == PartKind::Cable) hasCableRow = true;
        }
        check(hasCableRow && !hasWireRow,
              "bom: a cable is bought as a cable, and its cores are not bought again");
    }

    // ---- Where the wires are open, and the process notes ----
    {
        MechHarnessDef def = straightDef(300.0, 4, QStringLiteral("0.5"));
        def.draw.origin = Vec2{0.0, 0.0};
        def.draw.diameterLabels = false;
        MechFitting shrink;
        shrink.id = QStringLiteral("s");
        shrink.kind = PartKind::Heatshrink;
        shrink.name = QStringLiteral("heatshrink");
        shrink.lengthMm = 40.0;
        for (MechNode& n : def.nodes) {
            if (n.id != QLatin1String("p1")) continue;
            n.openLengthMm = 20.0;      // 20 mm of open conductors behind the connector
            n.fittings.push_back(shrink);
        }
        // The run goes out along +x to (300,0), so the heatshrink must start 20 mm back from there and
        // the open conductors are the 20 mm between.
        std::vector<Bounds> boxes;
        for (const Entity& e : buildMechanicalHarness(def, nullptr))
            if (e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("fitting"))
                boxes.push_back(e.bounds());
        check(boxes.size() == 1, "open wires: the heatshrink is drawn");
        if (boxes.size() == 1)
            check(near(boxes[0].max.x, 280.0) && near(boxes[0].min.x, 240.0),
                  "open wires: it stops 20 mm short of the connector face, not at it");
        // And that 20 mm is a dimension of its own, because it is a figure the harness is built to.
        bool dimensioned = false;
        for (const Entity& e : buildMechanicalHarness(def, nullptr))
            if (e.meta.value(QStringLiteral("openWires")).toBool()) dimensioned = true;
        check(dimensioned, "open wires: and the distance is dimensioned");

        // And a covering that runs right up to the connector face is asked about, because the distance
        // it should stop short by is a figure on the drawing rather than the builder's to choose.
        MechHarnessDef flush0 = def;
        for (MechNode& n : flush0.nodes) n.openLengthMm = 0.0;
        bool asked = false;
        for (const MechIssue& i : validateMechanicalHarness(flush0, nullptr))
            if (i.property == QLatin1String("Wires open")) asked = true;
        check(asked, "open wires: heatshrink up to the connector face is reported");
        bool quiet = true;
        for (const MechIssue& i : validateMechanicalHarness(def, nullptr))
            if (i.property == QLatin1String("Wires open")) quiet = false;
        check(quiet, "open wires: and once the figure is there, nothing is said");

        // Nothing said means nothing open: the covering starts where the bodies end, as before.
        MechHarnessDef flush = def;
        for (MechNode& n : flush.nodes) n.openLengthMm = 0.0;
        std::vector<Bounds> flushBoxes;
        for (const Entity& e : buildMechanicalHarness(flush, nullptr))
            if (e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("fitting"))
                flushBoxes.push_back(e.bounds());
        check(flushBoxes.size() == 1 && near(flushBoxes[0].max.x, 300.0),
              "open wires: with none stated, nothing is invented");
    }

    // ---- A balloon says what to buy; a triangle says what to do ----
    {
        // A gauge transition: 0.5 out to a junction, 1.0 on from it. The note is one the definition can
        // work out for itself, and the released drawings word it exactly this way.
        MechHarnessDef def = straightDef(200.0, 2, QStringLiteral("0.5"));
        def.draw.origin = Vec2{0.0, 0.0};
        for (MechNode& n : def.nodes)
            if (n.id == QLatin1String("p1")) n.kind = MechNodeKind::Breakout;
        def.wires.push_back(wireOf(QStringLiteral("w3"), QStringLiteral("1.0")));
        MechNode p2;
        p2.id = QStringLiteral("p2");
        p2.name = QStringLiteral("P2");
        p2.kind = MechNodeKind::Connector;
        p2.parentSegment = QStringLiteral("s2");
        def.nodes.push_back(p2);
        MechSegment s2;
        s2.id = QStringLiteral("s2");
        s2.fromNode = QStringLiteral("p1");
        s2.toNode = QStringLiteral("p2");
        s2.lengthMm = 150.0;
        s2.wireIds = {QStringLiteral("w3")};
        def.segments.push_back(s2);

        const std::vector<MechDeltaNote> notes = mechDeltaNotes(def);
        check(notes.size() == 1 && notes.front().number == 1,
              "notes: the gauge transition is note 1");
        check(!notes.empty() && notes.front().text.contains(QLatin1String("GAUGE TRANSITION")),
              "notes: worded the way a released drawing words it");
        check(!notes.empty() && notes.front().atNodes == QStringList{QStringLiteral("p1")},
              "notes: and it points at the junction it happens at");

        const std::vector<Entity> ents = buildMechanicalHarness(def, nullptr);
        int triangles = 0;
        bool listed = false;
        for (const Entity& e : ents) {
            if (e.meta.contains(QStringLiteral("deltaNote"))) ++triangles;
            if (e.meta.value(QStringLiteral("text")).toString().contains(
                    QLatin1String("GAUGE TRANSITION")))
                listed = true;
        }
        check(triangles == 1, "notes: a triangle is drawn where it applies");
        check(listed, "notes: and the note itself is written out beside the harness");

        // A Y-splitter boot brings its own note, because filling it with epoxy is a thing done rather
        // than a thing drawn.
        MechHarnessDef withY = def;
        MechFitting y;
        y.id = QStringLiteral("y");
        y.kind = PartKind::YSplit;
        y.name = QStringLiteral("Y-splitter boot");
        y.lengthMm = 30.0;
        for (MechNode& n : withY.nodes)
            if (n.id == QLatin1String("p1")) n.fittings.push_back(y);
        const std::vector<MechDeltaNote> both = mechDeltaNotes(withY);
        check(both.size() == 2 && both[1].number == 2 &&
                  both[1].text.contains(QLatin1String("Y-SPLITTER")),
              "notes: a Y-splitter adds the epoxy note as note 2");
        // And the boot is drawn as a Y - a body on the run that arrives and a leg on each that leaves -
        // rather than as one box across the junction.
        int legs = 0;
        for (const Entity& e : buildMechanicalHarness(withY, nullptr))
            if (e.meta.contains(QStringLiteral("ysplitLeg"))) ++legs;
        check(legs == 1, "notes: the Y-splitter is drawn with a leg on the run leaving the junction");

        // Turned off, neither the triangles nor the list are drawn.
        MechHarnessDef quiet = def;
        quiet.draw.deltaNotes = false;
        int quietTriangles = 0;
        for (const Entity& e : buildMechanicalHarness(quiet, nullptr))
            if (e.meta.contains(QStringLiteral("deltaNote"))) ++quietTriangles;
        check(quietTriangles == 0, "notes: they can be turned off");
    }

    if (failures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", failures);
    return failures;
}
