// Voltage drop: every case here has an answer that can be worked out by hand, so a wrong figure
// shows up as a wrong figure rather than as a plausible one.
#include "analysis/VoltageDrop.h"
#include "harness/Harness.h"
#include "model/Document.h"

#include <cmath>
#include <cstdio>

using namespace Loopwire;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++failures;
    }
}

bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) < tol; }

// A bare two-node network: one wire from a source pin to a load pin, no contacts, no joints.
VdGraph twoNodeGraph(double lengthM, double mm2, ConductorMaterial mat = ConductorMaterial::BareCopper,
                     bool stranded = false) {
    VdGraph g;
    VdNode a;
    a.id = "A";
    a.name = "A";
    a.kind = VdNodeKind::OpenEnd;
    VdNode b;
    b.id = "B";
    b.name = "B";
    b.kind = VdNodeKind::OpenEnd;
    g.nodes = {a, b};
    VdSegment s;
    s.id = "w1";
    s.wireId = "w1";
    s.label = "W1";
    s.fromNode = "A";
    s.toNode = "B";
    s.lengthM = lengthM;
    s.mm2 = mm2;
    s.material = mat;
    s.stranded = stranded;
    g.segments = {s};
    return g;
}

VdRequest requestTo(const QString& load, double amps, double volts = 24.0) {
    VdRequest r;
    r.sourceNode = "A";
    r.sourceV = volts;
    r.loads = {{load, amps, {}}};
    r.returnNode = load;      // the load node is the reference: the feed leg alone
    r.returnPath = VdReturn::NotModelled;
    r.resistanceCase = VdCase::WorstCase;
    return r;
}

} // namespace

int run_voltagedrop_tests() {
    std::printf("voltage drop tests:\n");

    // ---- One wire, against R = rho * L / A ----
    {
        // 10 m of 1 mm2 bare solid copper: R = 0.017241 * 10 / 1 = 0.17241 ohm. At 5 A that is
        // 0.862 V, and the load sees 24 - 0.862 V.
        const VdGraph g = twoNodeGraph(10.0, 1.0);
        const VdSegment& s = g.segments.front();
        const double r = wireResistanceOhms(s, 20.0);
        std::printf("    (1 mm2 x 10 m bare solid copper at 20 C: %.5f ohm)\n", r);
        check(near(r, 0.17241, 1e-5), "wire: R = rho * L / A at 20 C");

        const VdReport rep = analyzeVoltageDrop(g, requestTo("B", 5.0));
        check(rep.solved, "one wire: it solves");
        check(near(rep.totalDropV, 5.0 * 0.17241, 1e-4), "one wire: drop is I * R");
        check(near(rep.endVoltageV, 24.0 - 5.0 * 0.17241, 1e-4), "one wire: the load's own voltage");
        check(near(rep.dropPercent(), 100.0 * 5.0 * 0.17241 / 24.0, 1e-3),
              "one wire: the percentage of the supply");
        check(rep.rows.size() == 1 && near(rep.rows.front().wireOhms, 0.17241, 1e-5),
              "one wire: the breakdown carries the wire's own resistance");
        check(near(rep.pinOhms, 0.0) && near(rep.spliceOhms, 0.0),
              "one wire: nothing is charged for interfaces that are not there");
        check(rep.dominant() == QLatin1String("the wire"), "one wire: the copper is what limits it");
    }

    // ---- Temperature, stranding and material ----
    {
        // Each segment copied out: the graph it comes from is a temporary, gone at the end of the line.
        const VdSegment cold = twoNodeGraph(10.0, 1.0).segments.front();
        const double r20 = wireResistanceOhms(cold, 20.0);
        const double r85 = wireResistanceOhms(cold, 85.0);
        // R(T) = R20 * [1 + 0.00393 * (85 - 20)] = R20 * 1.25545
        std::printf("    (same wire at 85 C: %.5f ohm, x%.4f)\n", r85, r85 / r20);
        check(near(r85 / r20, 1.0 + 0.00393 * 65.0, 1e-9), "R(T): the copper coefficient is applied");

        const VdSegment strandedWire =
            twoNodeGraph(10.0, 1.0, ConductorMaterial::BareCopper, true).segments.front();
        check(near(wireResistanceOhms(strandedWire, 20.0) / r20, 1.03, 1e-9),
              "R(T): stranded costs 3 % over solid");

        const VdSegment tinned =
            twoNodeGraph(10.0, 1.0, ConductorMaterial::TinnedCopper, false).segments.front();
        check(near(wireResistanceOhms(tinned, 20.0) / r20, 1.03, 1e-9),
              "R(T): the tin coating costs 3 % over bare");

        const VdSegment alu =
            twoNodeGraph(10.0, 1.0, ConductorMaterial::Aluminium, false).segments.front();
        check(near(wireResistanceOhms(alu, 20.0), 0.28264, 1e-5),
              "R(T): aluminium is 1.64 times the resistance of copper");
    }

    // ---- Two equal wires in parallel: half the resistance, half the current each ----
    {
        // A splice at each end and two identical wires between: the pair must read half of one.
        VdGraph g;
        VdNode a, mid1, mid2, b;
        a.id = "A"; a.name = "A"; a.kind = VdNodeKind::OpenEnd;
        b.id = "B"; b.name = "B"; b.kind = VdNodeKind::OpenEnd;
        g.nodes = {a, b};
        for (int i = 0; i < 2; ++i) {
            VdSegment s;
            s.id = QStringLiteral("w%1").arg(i + 1);
            s.wireId = s.id;
            s.label = s.id;
            s.fromNode = "A";
            s.toNode = "B";
            s.lengthM = 10.0;
            s.mm2 = 1.0;
            s.material = ConductorMaterial::BareCopper;
            s.stranded = false;
            g.segments.push_back(s);
        }
        const VdReport rep = analyzeVoltageDrop(g, requestTo("B", 10.0));
        const double expected = 10.0 * (0.17241 / 2.0);
        std::printf("    (two 1 mm2 wires paralleled, 10 A: %.4f V, one alone would be %.4f V)\n",
                    rep.totalDropV, 10.0 * 0.17241);
        check(rep.solved, "parallel: it solves");
        check(near(rep.totalDropV, expected, 1e-3), "parallel: two equal wires halve the drop");
        check(rep.rows.size() == 1, "parallel: the path shown is one of the two, as traced");
        check(near(rep.rows.front().currentA, 5.0, 1e-3),
              "parallel: each wire carries half the load");
    }

    // ---- Contacts and joints, kept apart from the copper ----
    {
        // Source pin -> splice -> load pin, size 20 contacts at each end and a crimp joint between.
        // Worst case: 5.0 mOhm per contact, 2.0 mOhm for the joint.
        VdGraph g;
        VdNode p1;
        p1.id = "pin:c1:1"; p1.name = "P1:1"; p1.kind = VdNodeKind::Pin; p1.contactSize = "20";
        VdNode sp;
        sp.id = "splice:s1"; sp.name = "SP1"; sp.kind = VdNodeKind::Splice; sp.jointType = "crimp";
        VdNode p2;
        p2.id = "pin:c2:1"; p2.name = "P2:1"; p2.kind = VdNodeKind::Pin; p2.contactSize = "20";
        g.nodes = {p1, sp, p2};
        auto addWire = [&g](const char* id, const char* from, const char* to, double lengthM) {
            VdSegment s;
            s.id = id; s.wireId = id; s.label = id;
            s.fromNode = from; s.toNode = to;
            s.lengthM = lengthM; s.mm2 = 1.0;
            s.material = ConductorMaterial::BareCopper; s.stranded = false;
            g.segments.push_back(s);
        };
        addWire("w1", "pin:c1:1", "splice:s1", 5.0);
        addWire("w2", "splice:s1", "pin:c2:1", 5.0);

        VdRequest req;
        req.sourceNode = "pin:c1:1";
        req.sourceV = 24.0;
        req.loads = {{"pin:c2:1", 10.0, {}}};
        req.returnNode = "pin:c2:1";
        req.returnPath = VdReturn::NotModelled;
        req.resistanceCase = VdCase::WorstCase;

        const VdReport rep = analyzeVoltageDrop(g, req);
        const double wire = 2 * (0.017241 * 5.0 / 1.0);   // 0.17241 ohm of copper in total
        const double pinsR = 0.0050 * 2;                  // the source contact and the load contact
        const double joint = 0.0020;
        std::printf("    (with contacts: wire %.4f, pins %.4f, joints %.4f ohm)\n",
                    rep.wireOhms, rep.pinOhms, rep.spliceOhms);
        check(rep.solved, "contacts: it solves");
        check(near(rep.wireOhms, wire, 1e-4), "contacts: the copper is reported on its own");
        check(near(rep.pinOhms, pinsR, 1e-6), "contacts: both mating pairs are counted, once each");
        check(near(rep.spliceOhms, joint, 1e-6), "contacts: the joint is counted once");
        check(near(rep.totalDropV, 10.0 * (wire + pinsR + joint), 1e-3),
              "contacts: the drop is the current through everything in the path");
        check(near(rep.totalDropV,
                   rep.rows[0].dropV + rep.rows[1].dropV + rep.rows[2].dropV, 1e-9),
              "contacts: the breakdown adds up to the total");

        // Nominal figures are lower, and the report says which it used.
        req.resistanceCase = VdCase::Nominal;
        const VdReport nom = analyzeVoltageDrop(g, req);
        check(nom.pinOhms < rep.pinOhms, "contacts: nominal is lower than worst case");
        check(near(nom.pinOhms, 0.0025 * 2, 1e-6), "contacts: the nominal figure for size 20");

        // A stripped end has no mating interface: entering 0 for a contact must remove it entirely.
        req.resistanceCase = VdCase::WorstCase;
        req.contactOverrides.insert(QStringLiteral("pin:c1:1"), 0.0);
        const VdReport open = analyzeVoltageDrop(g, req);
        check(near(open.pinOhms, 0.0050, 1e-6), "contacts: an override of 0 removes that interface");

        // Power contacts are an order of magnitude better, and it shows.
        req.contactOverrides.clear();
        for (VdNode& n : g.nodes)
            if (n.kind == VdNodeKind::Pin) n.contactSize = QStringLiteral("12");
        const VdReport power = analyzeVoltageDrop(g, req);
        check(near(power.pinOhms, 0.0015 * 2, 1e-6), "contacts: size 12 power contacts");
        check(power.dominant() == QLatin1String("the wire"),
              "contacts: with power contacts the copper is what limits it again");
    }

    // ---- The contacts can beat the copper, and the report has to say so ----
    {
        // Half a metre of 2.5 mm2 through four size-20 contacts: the copper is nothing and the
        // interfaces are everything. A bigger gauge would fix nothing here, which is the point.
        VdGraph g;
        VdNode p1, p2, p3, p4;
        p1.id = "pin:a:1"; p1.name = "A:1"; p1.kind = VdNodeKind::Pin; p1.contactSize = "20";
        p2.id = "pin:b:1"; p2.name = "B:1"; p2.kind = VdNodeKind::Pin; p2.contactSize = "20";
        p3.id = "pin:c:1"; p3.name = "C:1"; p3.kind = VdNodeKind::Pin; p3.contactSize = "20";
        p4.id = "pin:d:1"; p4.name = "D:1"; p4.kind = VdNodeKind::Pin; p4.contactSize = "20";
        g.nodes = {p1, p2, p3, p4};
        auto addWire = [&g](const char* id, const char* from, const char* to) {
            VdSegment s;
            s.id = id; s.wireId = id; s.label = id;
            s.fromNode = from; s.toNode = to;
            s.lengthM = 0.15; s.mm2 = 2.5;
            s.material = ConductorMaterial::TinnedCopper; s.stranded = true;
            g.segments.push_back(s);
        };
        addWire("w1", "pin:a:1", "pin:b:1");
        addWire("w2", "pin:b:1", "pin:c:1");
        addWire("w3", "pin:c:1", "pin:d:1");

        VdRequest req;
        req.sourceNode = "pin:a:1";
        req.sourceV = 24.0;
        req.loads = {{"pin:d:1", 15.0, {}}};
        req.returnNode = "pin:d:1";
        req.returnPath = VdReturn::NotModelled;
        const VdReport rep = analyzeVoltageDrop(g, req);
        std::printf("    (four size-20 contacts, 15 A: wire %.4f ohm vs contacts %.4f ohm — %s)\n",
                    rep.wireOhms, rep.pinOhms, qPrintable(rep.dominant()));
        check(rep.pinOhms > rep.wireOhms, "dominance: the interfaces beat the copper here");
        check(rep.dominant() == QLatin1String("the contacts"),
              "dominance: and the report says the contacts are what limits it");
    }

    // ---- The loop, not the leg: a wired return doubles what the copper costs ----
    {
        VdGraph g;
        VdNode a, b, c;
        a.id = "A"; a.name = "A"; a.kind = VdNodeKind::OpenEnd;   // supply
        b.id = "B"; b.name = "B"; b.kind = VdNodeKind::OpenEnd;   // load
        c.id = "C"; c.name = "C"; c.kind = VdNodeKind::OpenEnd;   // supply return
        g.nodes = {a, b, c};
        auto addWire = [&g](const char* id, const char* from, const char* to) {
            VdSegment s;
            s.id = id; s.wireId = id; s.label = id;
            s.fromNode = from; s.toNode = to;
            s.lengthM = 10.0; s.mm2 = 1.0;
            s.material = ConductorMaterial::BareCopper; s.stranded = false;
            g.segments.push_back(s);
        };
        addWire("feed", "A", "B");
        addWire("return", "B", "C");

        VdRequest req;
        req.sourceNode = "A";
        req.sourceV = 24.0;
        req.loads = {{"B", 5.0, {}}};
        req.returnNode = "C";
        req.returnPath = VdReturn::ThroughHarness;
        const VdReport loop = analyzeVoltageDrop(g, req);

        req.returnPath = VdReturn::NotModelled;
        req.returnNode = "B";
        const VdReport legOnly = analyzeVoltageDrop(g, req);

        std::printf("    (loop %.4f V against feed only %.4f V)\n", loop.totalDropV,
                    legOnly.totalDropV);
        check(loop.solved && legOnly.solved, "loop: both cases solve");
        check(near(loop.totalDropV, 2.0 * legOnly.totalDropV, 1e-3),
              "loop: a wired return doubles the drop");
        check(loop.rows.size() == 2, "loop: the breakdown shows the feed and the return");
        check(loop.rows[0].leg == VdLeg::Feed && loop.rows[1].leg == VdLeg::Return,
              "loop: and says which leg each row is");
        check(legOnly.assumptions.join(QStringLiteral(" ")).contains(QLatin1String("FEED leg only")),
              "loop: a feed-only figure says so in the report");
    }

    // ---- The ordinary harness: two connectors, and the load bridges two pins ----
    // The feed wires and the return wires touch nowhere in the drawing - the load joins them - so
    // this is the case a single network solve cannot do, and the case every real harness is.
    {
        VdGraph g;
        VdNode s1, s2, l1, l2;
        s1.id = "pin:p1:1"; s1.name = "P1:1"; s1.kind = VdNodeKind::Pin; s1.contactSize = "16";
        s2.id = "pin:p1:2"; s2.name = "P1:2"; s2.kind = VdNodeKind::Pin; s2.contactSize = "16";
        l1.id = "pin:p2:1"; l1.name = "P2:1"; l1.kind = VdNodeKind::Pin; l1.contactSize = "16";
        l2.id = "pin:p2:2"; l2.name = "P2:2"; l2.kind = VdNodeKind::Pin; l2.contactSize = "16";
        g.nodes = {s1, s2, l1, l2};
        auto addWire = [&g](const char* id, const char* from, const char* to) {
            VdSegment s;
            s.id = id; s.wireId = id; s.label = id;
            s.fromNode = from; s.toNode = to;
            s.lengthM = 4.0; s.mm2 = 1.0;
            s.material = ConductorMaterial::BareCopper; s.stranded = false;
            g.segments.push_back(s);
        };
        addWire("feed", "pin:p1:1", "pin:p2:1");
        addWire("return", "pin:p2:2", "pin:p1:2");

        VdRequest req;
        req.sourceNode = "pin:p1:1";
        req.sourceV = 28.0;
        VdLoad load;
        load.nodeId = "pin:p2:1";
        load.currentA = 8.0;
        load.returnNodeId = "pin:p2:2";   // the load bridges P2:1 and P2:2
        req.loads = {load};
        req.returnNode = "pin:p1:2";
        req.returnPath = VdReturn::ThroughHarness;
        const VdReport rep = analyzeVoltageDrop(g, req);

        const double copper = 2 * (0.017241 * 4.0 / 1.0);   // both legs
        const double contacts = 4 * 0.0050;                 // four size-16 mating pairs
        std::printf("    (two-connector loop: %s)\n", qPrintable(rep.summary()));
        check(rep.solved, "two-connector: the loop solves even though the legs are separate");
        check(near(rep.wireOhms, copper, 1e-4), "two-connector: both legs of copper are counted");
        check(near(rep.pinOhms, contacts, 1e-6),
              "two-connector: all four mating pairs are counted, once each");
        check(near(rep.totalDropV, 8.0 * (copper + contacts), 1e-3),
              "two-connector: the drop is the current through the whole loop");
        check(near(rep.endVoltageV, 28.0 - rep.totalDropV, 1e-9),
              "two-connector: and what is left is what arrives");
        int feedRows = 0, returnRows = 0;
        for (const VdSegmentRow& r : rep.rows) (r.leg == VdLeg::Feed ? feedRows : returnRows)++;
        check(feedRows > 0 && returnRows > 0,
              "two-connector: the breakdown covers both legs");
        // Both wires carry the load current: the load's current source is what ties the legs together.
        for (const VdSegmentRow& r : rep.rows)
            if (r.lengthM > 0.0)
                check(near(r.currentA, 8.0, 1e-6), "two-connector: every wire carries the load");
    }

    // ---- A return that goes nowhere is said so, not silently dropped ----
    {
        VdGraph g;
        VdNode a, b, c;
        a.id = "A"; a.name = "A"; a.kind = VdNodeKind::OpenEnd;
        b.id = "B"; b.name = "B"; b.kind = VdNodeKind::OpenEnd;
        c.id = "C"; c.name = "C"; c.kind = VdNodeKind::OpenEnd;   // wired to nothing
        g.nodes = {a, b, c};
        VdSegment s;
        s.id = "w1"; s.wireId = "w1"; s.label = "W1";
        s.fromNode = "A"; s.toNode = "B";
        s.lengthM = 2.0; s.mm2 = 1.0;
        s.material = ConductorMaterial::BareCopper; s.stranded = false;
        g.segments = {s};

        VdRequest req;
        req.sourceNode = "A";
        req.sourceV = 24.0;
        req.loads = {{"B", 5.0, {}}};
        req.returnNode = "C";
        req.returnPath = VdReturn::ThroughHarness;
        const VdReport rep = analyzeVoltageDrop(g, req);
        check(!rep.solved, "unreachable return: it does not pretend to have an answer");
        check(rep.problem.contains(QLatin1String("return")),
              "unreachable return: and it says the return is what is missing");
    }

    // ---- A chassis return: the body of the machine is in the loop, and named ----
    {
        VdGraph g = twoNodeGraph(6.0, 1.5);
        VdRequest req;
        req.sourceNode = "A";
        req.sourceV = 12.0;
        req.loads = {{"B", 10.0, {}}};
        req.returnPath = VdReturn::Chassis;
        req.chassisOhms = 0.002;
        const VdReport rep = analyzeVoltageDrop(g, req);
        const double copper = 0.017241 * 6.0 / 1.5;
        std::printf("    (chassis return: %s)\n", qPrintable(rep.summary()));
        check(rep.solved, "chassis: it solves without a return pin");
        check(near(rep.totalDropV, 10.0 * (copper + 0.002), 1e-3),
              "chassis: the ground path's own resistance is in the drop");
        bool named = false;
        for (const VdSegmentRow& r : rep.rows)
            named = named || r.label == QLatin1String("chassis return");
        check(named, "chassis: and it is a row of its own, not hidden in the copper");
    }

    // ---- What the drawing does not say is reported, not assumed ----
    {
        VdGraph g = twoNodeGraph(10.0, 0.0);   // no gauge
        const VdReport rep = analyzeVoltageDrop(g, requestTo("B", 5.0));
        check(rep.blocked(), "gaps: a wire with no gauge blocks the calculation");
        check(!rep.gaps.empty() &&
                  rep.gaps.front().property == QLatin1String("Wire cross-section"),
              "gaps: and the gap names the parameter that is missing");

        // A pin with no contact size: the calculation still runs on the copper, and the missing size
        // is reported so it can be filled in.
        VdGraph h = twoNodeGraph(10.0, 1.0);
        h.nodes[0].kind = VdNodeKind::Pin;
        h.nodes[0].name = QStringLiteral("P1:1");
        h.nodes[0].connectorId = QStringLiteral("c1");
        const VdReport pinRep = analyzeVoltageDrop(h, requestTo("B", 5.0));
        check(pinRep.solved, "gaps: an unstated contact size does not stop the copper being added up");
        bool sawContactGap = false;
        for (const VdGap& gap : pinRep.gaps)
            if (gap.property == QLatin1String("Contact size")) sawContactGap = true;
        check(sawContactGap, "gaps: the unstated contact size is reported");
    }

    // ---- A circuit that does not close is a wiring fault, reported as one ----
    {
        VdGraph g = twoNodeGraph(10.0, 1.0);
        VdNode lonely;
        lonely.id = "Z";
        lonely.name = "Z";
        lonely.kind = VdNodeKind::OpenEnd;
        g.nodes.push_back(lonely);
        VdRequest req = requestTo("Z", 5.0);   // a load nothing is wired to
        const VdReport rep = analyzeVoltageDrop(g, req);
        check(!rep.solved, "open circuit: it does not pretend to have an answer");
        check(!rep.problem.isEmpty(), "open circuit: and says what is wrong");
    }

    // ---- The path is the least-resistance one, and knows when it is not series ----
    {
        // Two ways from A to C: 1 mm2 straight through, or 0.5 mm2 the long way round. The straight
        // one is picked, and the network is not series because the middle node has three wires.
        VdGraph g;
        VdNode a, b, c;
        a.id = "A"; a.name = "A"; a.kind = VdNodeKind::OpenEnd;
        b.id = "B"; b.name = "B"; b.kind = VdNodeKind::OpenEnd;
        c.id = "C"; c.name = "C"; c.kind = VdNodeKind::OpenEnd;
        g.nodes = {a, b, c};
        auto addWire = [&g](const char* id, const char* from, const char* to, double mm2,
                            double lengthM) {
            VdSegment s;
            s.id = id; s.wireId = id; s.label = id;
            s.fromNode = from; s.toNode = to;
            s.lengthM = lengthM; s.mm2 = mm2;
            s.material = ConductorMaterial::BareCopper; s.stranded = false;
            g.segments.push_back(s);
        };
        addWire("direct", "A", "C", 1.0, 10.0);
        addWire("viaB1", "A", "B", 0.5, 10.0);
        addWire("viaB2", "B", "C", 0.5, 10.0);

        const std::vector<QString> path = voltageDropPath(g, "A", "C", 20.0);
        std::printf("    (path A to C: %s)\n", qPrintable(path.empty() ? QString() : path.front()));
        check(path.size() == 1 && path.front() == QLatin1String("direct"),
              "path: the least-resistance route is the one traced");
        check(!voltageDropPathIsSeries(g, path, "A", "C"),
              "path: a route past a junction is not a series path");

        const std::vector<QString> onlyWay = voltageDropPath(g, "A", "B", 20.0);
        check(onlyWay.size() == 1 && onlyWay.front() == QLatin1String("viaB1"),
              "path: and it still finds a route that is the only one");
    }

    // ---- Built from a drawing: pins, splices and open ends found by geometry ----
    {
        Document doc;
        doc.entities().clear();

        ConnectorSpec spec;
        spec.pinCount = 4;
        spec.pitch = 15.0;
        spec.pinLength = 40.0;
        spec.designator = QStringLiteral("P1");
        regenPinLabels(spec);
        Entity conn;
        conn.id = QStringLiteral("c1");
        conn.type = EntityType::Connector;
        conn.layer = QStringLiteral("Connectors");
        conn.a = {0, 0};
        writeConnectorSpec(conn.meta, spec);
        conn.meta.insert(QStringLiteral("contactSize"), QStringLiteral("16"));
        doc.entities().push_back(conn);

        const ConnectorGeometry geom = connectorGeometry(conn.a, spec);
        Entity wire;
        wire.id = QStringLiteral("w1");
        wire.type = EntityType::Wire;
        wire.layer = QStringLiteral("Wires");
        wire.points = {geom.pinEnd[0], {geom.pinEnd[0].x + 1000.0, geom.pinEnd[0].y}}; // 1 m
        wire.meta.insert(QStringLiteral("wireGauge"), QStringLiteral("0.5"));
        wire.meta.insert(QStringLiteral("wireLabel"), QStringLiteral("PWR"));
        doc.entities().push_back(wire);

        const VdGraph g = buildVoltageDropGraph(doc);
        std::printf("    (from the drawing: %d node(s), %d segment(s))\n", int(g.nodes.size()),
                    int(g.segments.size()));
        check(g.segments.size() == 1, "from drawing: the wire became a segment");
        check(near(g.segments.front().lengthM, 1.0, 1e-6),
              "from drawing: its length came from the geometry, in metres");
        check(near(g.segments.front().mm2, 0.5, 1e-9),
              "from drawing: and its gauge from the wire");

        const VdNode* pinNode = nullptr;
        const VdNode* openNode = nullptr;
        for (const VdNode& n : g.nodes) {
            if (n.kind == VdNodeKind::Pin) pinNode = &n;
            if (n.kind == VdNodeKind::OpenEnd) openNode = &n;
        }
        check(pinNode != nullptr, "from drawing: the end on a pin became a pin node");
        check(pinNode && pinNode->contactSize == QLatin1String("16"),
              "from drawing: carrying the connector's contact size");
        check(pinNode && pinNode->name == QLatin1String("P1:1"),
              "from drawing: named the way the drawing names it");
        check(openNode != nullptr, "from drawing: the end on nothing became an open end");

        // And it calculates: 1 m of 0.5 mm2 tinned stranded at 3 A, through one size-16 contact.
        VdRequest req;
        req.sourceNode = pinNode->id;
        req.sourceV = 12.0;
        req.loads = {{openNode->id, 3.0, {}}};
        req.returnNode = openNode->id;
        req.returnPath = VdReturn::NotModelled;
        const VdReport rep = analyzeVoltageDrop(g, req);
        const double expectedWire = 0.017241 / 0.5 * 1.03 * 1.03 * 1.0;  // tinned + stranded
        std::printf("    (drawing case: %s)\n", qPrintable(rep.summary()));
        check(rep.solved, "from drawing: the analysis runs on it");
        check(near(rep.wireOhms, expectedWire, 1e-5),
              "from drawing: tinned and stranded are both applied");
        check(near(rep.pinOhms, 0.0050, 1e-9), "from drawing: the size-16 contact is counted");
        check(!rep.summary().isEmpty(), "from drawing: it can say what it found");
    }

    // ---- The limit, and the profile the UI charts ----
    {
        const VdGraph g = twoNodeGraph(10.0, 1.0);
        VdRequest req = requestTo("B", 5.0);
        req.limitPercent = 3.0;
        const VdReport over = analyzeVoltageDrop(g, req);
        check(over.dropPercent() > 3.0 && over.exceedsLimit(),
              "limit: 3.6 % against a 3 % limit is over it");
        req.limitPercent = 5.0;
        check(!analyzeVoltageDrop(g, req).exceedsLimit(), "limit: and under a 5 % one it is not");

        check(over.profile.size() == over.rows.size() + 1,
              "profile: a point at the source and one after each element");
        check(near(over.profile.front().voltageV, 24.0, 1e-9), "profile: it starts at the supply");
        check(near(over.profile.back().voltageV, over.endVoltageV, 1e-6),
              "profile: and ends at the load's own voltage");
    }

    std::printf(failures == 0 ? "  -> ok\n" : "  -> %d failed\n", failures);
    return failures;
}
