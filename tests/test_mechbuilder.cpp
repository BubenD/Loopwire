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

        // Nothing drawn gives an empty definition rather than a broken one.
        Document blank;
        blank.entities().clear();
        const MechHarnessDef none = mechFromDrawing(blank);
        check(none.nodes.empty() && none.segments.empty(),
              "from schematic: an empty sheet gives an empty definition");
    }

    if (failures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", failures);
    return failures;
}
