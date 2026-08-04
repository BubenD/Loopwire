#include "analysis/VoltageDrop.h"

#include "harness/Harness.h"
#include "model/Document.h"

#include <QQueue>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace Edraw_Desktop {
namespace {

// Two points are the same node when they are this close: wires are routed to pin ends exactly, and
// a fraction of a millimetre of rounding must not split a joint into two.
constexpr double kNodeTol = 1e-6;

// Contact resistance per MATING PAIR, by size band: (nominal, worst case) in ohms. Signal contacts
// are an order of magnitude worse than power contacts, which is why a 15 A load through size-20
// pins is worth checking at all.
struct ContactRow { const char* size; double nominal; double worst; };
const ContactRow kContacts[] = {
    {"22", 0.0030, 0.0060}, {"20", 0.0025, 0.0050}, {"16", 0.0025, 0.0050},
    {"12", 0.0008, 0.0015}, {"8", 0.0005, 0.0012},  {"4", 0.0004, 0.0010},
};

struct JointRow { const char* type; double nominal; double worst; };
const JointRow kJoints[] = {
    {"crimp", 0.0010, 0.0020}, {"weld", 0.0005, 0.0010},
    {"solder", 0.0008, 0.0015}, {"terminal", 0.0006, 0.0012},
};

bool samePoint(const Vec2& a, const Vec2& b) {
    return std::abs(a.x - b.x) < kNodeTol && std::abs(a.y - b.y) < kNodeTol;
}

double polylineLengthMm(const std::vector<Vec2>& pts) {
    double len = 0.0;
    for (size_t i = 1; i < pts.size(); ++i) len += distance(pts[i - 1], pts[i]);
    return len;
}

ConductorMaterial materialFromMeta(const Entity& wire) {
    const QString m = wire.meta.value(QStringLiteral("wireMaterial")).toString();
    if (m.contains(QLatin1String("Alumin"), Qt::CaseInsensitive)) return ConductorMaterial::Aluminium;
    if (m.contains(QLatin1String("Silver"), Qt::CaseInsensitive))
        return ConductorMaterial::SilverPlatedCopper;
    if (m.contains(QLatin1String("Tinned"), Qt::CaseInsensitive))
        return ConductorMaterial::TinnedCopper;
    if (m.contains(QLatin1String("Copper"), Qt::CaseInsensitive)) return ConductorMaterial::BareCopper;
    // Nothing said: tinned copper is what a harness is nearly always built from, and the report says
    // it assumed that.
    return ConductorMaterial::TinnedCopper;
}

// A gauge as the drawing carries it: mm2 in "wireGauge", or an AWG figure in "awg".
void gaugeFromMeta(const Entity& wire, double& mm2, int& awg) {
    mm2 = wire.meta.value(QStringLiteral("wireGauge")).toString().toDouble();
    const QString awgText = wire.meta.value(QStringLiteral("awg")).toString();
    awg = awgText.split(QLatin1Char(' ')).value(0).toInt();
    if (mm2 <= 0.0 && awg > 0) {
        // The standard areas, so an AWG-only drawing still calculates.
        static const QHash<int, double> kAwgMm2 = {
            {30, 0.0507}, {28, 0.0810}, {26, 0.1288}, {24, 0.2047}, {22, 0.3247}, {20, 0.5188},
            {18, 0.8230}, {16, 1.3090}, {14, 2.0810}, {12, 3.3090}, {10, 5.2610}, {8, 8.3670},
            {6, 13.300},  {4, 21.150},  {2, 33.630},  {1, 42.410}};
        mm2 = kAwgMm2.value(awg, 0.0);
    }
}

} // namespace

ConductorSpec ConductorSpec::forMaterial(ConductorMaterial m) {
    switch (m) {
        case ConductorMaterial::BareCopper:         return {m, 0.017241, 0.00393, 1.000};
        case ConductorMaterial::TinnedCopper:       return {m, 0.017241, 0.00393, 1.030};
        case ConductorMaterial::SilverPlatedCopper: return {m, 0.017241, 0.00393, 1.005};
        case ConductorMaterial::Aluminium:          return {m, 0.028264, 0.00403, 1.000};
    }
    return {};
}

QString ConductorSpec::name(ConductorMaterial m) {
    switch (m) {
        case ConductorMaterial::BareCopper:         return QStringLiteral("Bare copper");
        case ConductorMaterial::TinnedCopper:       return QStringLiteral("Tinned copper");
        case ConductorMaterial::SilverPlatedCopper: return QStringLiteral("Silver-plated copper");
        case ConductorMaterial::Aluminium:          return QStringLiteral("Aluminium");
    }
    return QStringLiteral("Copper");
}

const VdNode* VdGraph::node(const QString& id) const {
    for (const VdNode& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

std::vector<const VdSegment*> VdGraph::incidentTo(const QString& nodeId) const {
    std::vector<const VdSegment*> out;
    for (const VdSegment& s : segments)
        if (s.fromNode == nodeId || s.toNode == nodeId) out.push_back(&s);
    return out;
}

QString VdGraph::other(const VdSegment& s, const QString& nodeId) {
    return s.fromNode == nodeId ? s.toNode : s.fromNode;
}

QStringList VdGraph::terminals() const {
    QStringList out;
    for (const VdNode& n : nodes)
        if (n.kind != VdNodeKind::Splice) out << n.id;
    return out;
}

double contactResistanceOhms(const QString& contactSize, VdCase which, bool* known) {
    for (const ContactRow& r : kContacts)
        if (contactSize == QLatin1String(r.size)) {
            if (known) *known = true;
            return which == VdCase::Nominal ? r.nominal : r.worst;
        }
    if (known) *known = false;
    return 0.0;
}

double jointResistanceOhms(const QString& jointType, VdCase which, bool* known) {
    for (const JointRow& r : kJoints)
        if (jointType.compare(QLatin1String(r.type), Qt::CaseInsensitive) == 0) {
            if (known) *known = true;
            return which == VdCase::Nominal ? r.nominal : r.worst;
        }
    if (known) *known = false;
    return 0.0;
}

double wireResistanceOhms(const VdSegment& seg, double conductorTempC) {
    if (seg.mm2 <= 0.0) return std::numeric_limits<double>::quiet_NaN();
    const ConductorSpec spec = ConductorSpec::forMaterial(seg.material);
    const double r20PerM = spec.rho20 / seg.mm2 * spec.coating * strandFactor(seg.stranded);
    return r20PerM * (1.0 + spec.alpha20 * (conductorTempC - 20.0)) * seg.lengthM;
}

// ---- Building the network from the drawing -----------------------------------------------------

VdGraph buildVoltageDropGraph(const Document& doc) {
    VdGraph g;

    // Every connector pin end, so a wire that lands on one becomes a pin node rather than an open
    // end - and carries that contact's own resistance.
    struct PinPoint { Vec2 at; QString connId; QString designator; int pin; QString contactSize; };
    std::vector<PinPoint> pins;
    for (const Entity& e : doc.entities()) {
        if (e.type != EntityType::Connector) continue;
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
        const ConnectorGeometry geom = connectorGeometry(e.a, spec);
        const QString des = spec.designator.isEmpty() ? e.id : spec.designator;
        const QString size = e.meta.value(QStringLiteral("contactSize")).toString();
        for (int i = 0; i < static_cast<int>(geom.pinEnd.size()); ++i)
            pins.push_back({geom.pinEnd[i], e.id, des, i + 1, size});
    }

    // Splices: a joint is a node in its own right, and it is where the current can divide.
    struct SplicePoint { Vec2 at; QString id; QString jointType; };
    std::vector<SplicePoint> splices;
    for (const Entity& e : doc.entities()) {
        if (e.type != EntityType::Splice) continue;
        splices.push_back({e.a, e.id,
                           e.meta.value(QStringLiteral("jointType")).toString(QStringLiteral("crimp"))});
        // A two-conductor cable transition carries a second joint at its other conductor.
        if (e.meta.value(QStringLiteral("pair")).toBool(false))
            splices.push_back({e.b, e.id + QStringLiteral(":b"),
                               e.meta.value(QStringLiteral("jointType"))
                                   .toString(QStringLiteral("crimp"))});
    }

    auto findOrAddNode = [&g](const VdNode& proto) {
        for (const VdNode& n : g.nodes)
            if (n.id == proto.id) return n.id;
        g.nodes.push_back(proto);
        return proto.id;
    };

    // Which node a wire end lands on: a pin, a joint, or - failing both - an end of its own. Ends
    // that meet at the same point share the open node, so two wires joined without a splice symbol
    // are still one electrical node.
    auto nodeAt = [&](const Vec2& at, const QString& wireId, int endIndex) {
        for (const PinPoint& p : pins)
            if (samePoint(p.at, at)) {
                VdNode n;
                n.id = QStringLiteral("pin:%1:%2").arg(p.connId).arg(p.pin);
                n.name = QStringLiteral("%1:%2").arg(p.designator).arg(p.pin);
                n.kind = VdNodeKind::Pin;
                n.connectorId = p.connId;
                n.pin = p.pin;
                n.contactSize = p.contactSize;
                n.at = at;
                return findOrAddNode(n);
            }
        for (const SplicePoint& s : splices)
            if (samePoint(s.at, at)) {
                VdNode n;
                n.id = QStringLiteral("splice:%1").arg(s.id);
                n.name = QStringLiteral("joint %1").arg(s.id);
                n.kind = VdNodeKind::Splice;
                n.jointType = s.jointType;
                n.at = at;
                return findOrAddNode(n);
            }
        // An unmarked meeting point is still a node: look for one already made there.
        for (const VdNode& n : g.nodes)
            if (n.kind == VdNodeKind::OpenEnd && samePoint(n.at, at)) return n.id;
        VdNode n;
        n.id = QStringLiteral("open:%1:%2").arg(wireId).arg(endIndex);
        n.name = QStringLiteral("open end (%1)").arg(wireId);
        n.kind = VdNodeKind::OpenEnd;
        n.at = at;
        return findOrAddNode(n);
    };

    for (const Entity& e : doc.entities()) {
        if (e.type != EntityType::Wire || e.points.size() < 2) continue;
        VdSegment s;
        s.id = e.id;
        s.wireId = e.id;
        s.label = e.meta.value(QStringLiteral("wireLabel")).toString();
        if (s.label.isEmpty()) s.label = e.id;
        // The drawing is in millimetres; resistance is per metre.
        s.lengthM = polylineLengthMm(e.points) / 1000.0;
        gaugeFromMeta(e, s.mm2, s.awg);
        s.material = materialFromMeta(e);
        s.stranded = e.meta.value(QStringLiteral("conductorType")).toString() !=
                     QLatin1String("Solid");
        s.fromNode = nodeAt(e.points.front(), e.id, 0);
        s.toNode = nodeAt(e.points.back(), e.id, 1);
        if (s.fromNode == s.toNode) continue; // a loop back to itself carries no load current
        g.segments.push_back(s);
    }
    return g;
}

// ---- Path finding -----------------------------------------------------------------------------

std::vector<QString> voltageDropPath(const VdGraph& graph, const QString& fromNode,
                                     const QString& toNode, double conductorTempC) {
    // Dijkstra weighted by resistance: the path found is the one the current mostly takes, which is
    // also the one worth lighting up on the drawing - not the one with the fewest hops.
    QHash<QString, double> dist;
    QHash<QString, QPair<QString, QString>> prev; // node -> (previous node, segment id)
    using Item = std::pair<double, QString>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
    dist[fromNode] = 0.0;
    queue.push({0.0, fromNode});
    QSet<QString> done;

    while (!queue.empty()) {
        const auto [d, node] = queue.top();
        queue.pop();
        if (done.contains(node)) continue;
        done.insert(node);
        if (node == toNode) break;
        for (const VdSegment* s : graph.incidentTo(node)) {
            const QString next = VdGraph::other(*s, node);
            const double r = wireResistanceOhms(*s, conductorTempC);
            if (std::isnan(r)) continue; // a segment with no gauge cannot be weighed
            const double nd = d + r;
            if (nd < dist.value(next, std::numeric_limits<double>::max())) {
                dist[next] = nd;
                prev[next] = {node, s->id};
                queue.push({nd, next});
            }
        }
    }
    if (fromNode == toNode) return {};
    if (!prev.contains(toNode)) return {};

    std::vector<QString> out;
    for (QString at = toNode; prev.contains(at);) {
        const auto step = prev.value(at);
        out.push_back(step.second);
        at = step.first;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

bool voltageDropPathIsSeries(const VdGraph& graph, const std::vector<QString>& segmentIds,
                             const QString& fromNode, const QString& toNode) {
    if (segmentIds.empty()) return false;
    const QSet<QString> ends{fromNode, toNode};
    const QSet<QString> onPath(segmentIds.begin(), segmentIds.end());

    // No junction inside the path: at a node with a third wire on it the current divides, and what
    // goes down the path is no longer all of it.
    for (const QString& id : segmentIds) {
        for (const VdSegment& s : graph.segments) {
            if (s.id != id) continue;
            for (const QString& n : {s.fromNode, s.toNode})
                if (!ends.contains(n) && graph.incidentTo(n).size() != 2) return false;
        }
    }

    // And no way round it: if the two ends are still joined once the path's own wires are taken out,
    // there is a second route in parallel with this one, and adding up along this one is short of the
    // answer however few junctions it has. (A dead-end stub off either end is harmless - it carries
    // nothing - and this test lets it be, because it leads nowhere.)
    QSet<QString> reached{fromNode};
    bool grew = true;
    while (grew) {
        grew = false;
        for (const VdSegment& s : graph.segments) {
            if (onPath.contains(s.id)) continue;
            const bool hasFrom = reached.contains(s.fromNode), hasTo = reached.contains(s.toNode);
            if (hasFrom != hasTo) {
                reached.insert(hasFrom ? s.toNode : s.fromNode);
                grew = true;
            }
        }
    }
    return !reached.contains(toNode);
}

// ---- The solve --------------------------------------------------------------------------------

namespace {

// Gaussian elimination with partial pivoting. A harness branch is tens of nodes, so a dense solve
// beats assembling anything sparse. A singular matrix means the circuit does not close - which is a
// wiring fault to report, not a numerical one to hide.
bool solveLinear(std::vector<std::vector<double>>& a, std::vector<double>& b,
                 std::vector<double>& x) {
    const int n = static_cast<int>(b.size());
    for (int col = 0; col < n; ++col) {
        int pivot = col;
        for (int r = col + 1; r < n; ++r)
            if (std::abs(a[r][col]) > std::abs(a[pivot][col])) pivot = r;
        if (std::abs(a[pivot][col]) < 1e-14) return false;
        if (pivot != col) {
            std::swap(a[col], a[pivot]);
            std::swap(b[col], b[pivot]);
        }
        for (int r = col + 1; r < n; ++r) {
            const double f = a[r][col] / a[col][col];
            if (f == 0.0) continue;
            for (int c = col; c < n; ++c) a[r][c] -= f * a[col][c];
            b[r] -= f * b[col];
        }
    }
    x.assign(n, 0.0);
    for (int r = n - 1; r >= 0; --r) {
        double s = b[r];
        for (int c = r + 1; c < n; ++c) s -= a[r][c] * x[c];
        x[r] = s / a[r][r];
    }
    return true;
}

// A node's own contact or joint resistance is in series with the branches on it, so half of it is
// folded into each branch that touches it. That keeps one unknown per node.
double nodeSeriesOhms(const VdNode& n, const VdRequest& req) {
    if (req.contactOverrides.contains(n.id)) return req.contactOverrides.value(n.id);
    if (req.jointOverrides.contains(n.id)) return req.jointOverrides.value(n.id);
    switch (n.kind) {
        case VdNodeKind::Pin:    return contactResistanceOhms(n.contactSize, req.resistanceCase);
        case VdNodeKind::Splice: return jointResistanceOhms(n.jointType, req.resistanceCase);
        default:                 return 0.0; // a stripped end has no mating interface
    }
}

double branchOhms(const VdGraph& g, const VdSegment& s, const VdRequest& req, double tempC) {
    const VdNode* a = g.node(s.fromNode);
    const VdNode* b = g.node(s.toNode);
    return wireResistanceOhms(s, tempC) + 0.5 * (a ? nodeSeriesOhms(*a, req) : 0.0) +
           0.5 * (b ? nodeSeriesOhms(*b, req) : 0.0);
}

} // namespace

QString VdReport::dominant() const {
    if (pinOhms > wireOhms && pinOhms >= spliceOhms) return QStringLiteral("the contacts");
    if (spliceOhms > wireOhms) return QStringLiteral("the joints");
    return QStringLiteral("the wire");
}

QString VdReport::summary() const {
    if (blocked())
        return QStringLiteral("%1 value(s) missing before this can be worked out").arg(gaps.size());
    if (!solved) return problem;
    return QStringLiteral("%1 V (%2 %) of %3 V — %4 V at the load. Most of it is in %5.")
        .arg(totalDropV, 0, 'f', 3)
        .arg(dropPercent(), 0, 'f', 2)
        .arg(sourceV, 0, 'f', 2)
        .arg(endVoltageV, 0, 'f', 2)
        .arg(dominant());
}

VdReport analyzeVoltageDrop(const VdGraph& g, const VdRequest& req) {
    VdReport out;
    out.sourceV = req.sourceV;
    out.returnPath = req.returnPath;
    out.resistanceCase = req.resistanceCase;
    out.limitPercent = req.limitPercent;
    for (const VdLoad& l : req.loads) out.totalCurrentA += l.currentA;

    const double tempC = req.ambientC + req.conductorRiseC;

    // ---- What the drawing does not say ----
    // Reported as gaps rather than assumed: a drop computed on a guessed contact size or a wire with
    // no gauge is a number nobody can sign.
    for (const VdSegment& s : g.segments) {
        if (s.mm2 <= 0.0)
            out.gaps.push_back({s.wireId, s.label, QStringLiteral("Wire cross-section"),
                                QStringLiteral("mm2"), QStringLiteral("Voltage drop"),
                                QStringLiteral("the conductor's area, or its AWG size")});
        if (s.lengthM <= 0.0)
            out.gaps.push_back({s.wireId, s.label, QStringLiteral("Wire length"),
                                QStringLiteral("m"), QStringLiteral("Voltage drop"),
                                QStringLiteral("drawn length, or a length override")});
    }
    // Only the contacts on the path being asked about matter, so pins are checked once the path is
    // known - below.

    if (req.loads.empty()) {
        out.problem = QStringLiteral("No load: set a current at the destination pin.");
        return out;
    }
    if (!g.node(req.sourceNode)) {
        out.problem = QStringLiteral("The source pin is not part of this harness.");
        return out;
    }

    // Where the loop closes. A chassis return closes outside the harness, so it gets a reference of
    // its own that no wire lands on; the body's own resistance is then one named row of the report
    // rather than something hidden in the copper.
    const QString kChassis = QStringLiteral("chassis");
    QString reference = req.returnPath == VdReturn::Chassis ? kChassis : req.returnNode;
    if (reference.isEmpty()) {
        for (const VdNode& n : g.nodes)
            if (n.kind == VdNodeKind::Chassis) { reference = n.id; break; }
    }
    if (reference.isEmpty()) {
        // The loop has to close somewhere. Saying so is the useful answer: a feed-only figure is
        // half of the drop a harness with a wired return actually has.
        out.problem = QStringLiteral(
            "No return: pick the pin the current comes back to, or set the return to chassis.");
        return out;
    }
    if (req.returnPath == VdReturn::NotModelled)
        out.assumptions << QStringLiteral(
            "Return path not modelled: this is the FEED leg only, so the loop drop is higher.");

    // Every wire has to be weighable before anything is solved.
    QHash<QString, double> branchR;
    for (const VdSegment& s : g.segments) {
        const double r = branchOhms(g, s, req, tempC);
        if (std::isnan(r)) {
            out.problem = QStringLiteral("A wire on the path has no gauge, so it cannot be weighed.");
            return out;
        }
        branchR[s.id] = r;
    }

    // ---- Nodal analysis, one leg at a time ----------------------------------------------------
    // What drives the network is the LOAD, not the supply. An ECU or a lamp draws what it draws
    // almost regardless of the volt or two the harness costs it, so the load is a current source -
    // and a current source in series SEPARATES the feed from the return. That is why each leg is
    // solved on its own: in the ordinary two-connector harness the feed wires and the return wires
    // are not joined anywhere in the drawing (the load joins them), so one solve over the lot would
    // have a floating island in it and no answer at all.
    //
    // Each leg is solved over the part of the harness its current can actually reach, so wires that
    // belong to the other leg - or to nothing at this moment - neither float nor take current.
    QHash<QString, double> current;   // by segment id, merged over the legs

    // Returns false and sets out.problem when the current has nowhere to go.
    auto solveLeg = [&](const QString& inject, const QString& extract, double amps,
                        const QString& whatFor) -> bool {
        if (inject == extract || amps == 0.0) return true;   // nothing to solve, nothing to report

        // The reachable part, from the injection point.
        QSet<QString> comp{inject};
        bool grew = true;
        while (grew) {
            grew = false;
            for (const VdSegment& s : g.segments) {
                const bool hasFrom = comp.contains(s.fromNode), hasTo = comp.contains(s.toNode);
                if (hasFrom != hasTo) {
                    comp.insert(hasFrom ? s.toNode : s.fromNode);
                    grew = true;
                }
            }
            if (req.returnPath == VdReturn::Chassis) {
                for (const VdLoad& l : req.loads) {
                    if (comp.contains(l.nodeId) && !comp.contains(reference)) {
                        comp.insert(reference);
                        grew = true;
                    }
                }
            }
        }
        if (!comp.contains(extract)) {
            auto shown = [&](const QString& id) {
                const VdNode* n = g.node(id);
                return n ? n->name : id;
            };
            out.problem = QStringLiteral("No %1 path: nothing carries the current from %2 to %3.")
                              .arg(whatFor, shown(inject), shown(extract));
            return false;
        }

        std::vector<QString> unknowns;
        for (const QString& id : comp)
            if (id != extract) unknowns.push_back(id);
        std::sort(unknowns.begin(), unknowns.end());   // a stable order, so the solve is repeatable
        QHash<QString, int> idx;
        for (int i = 0; i < static_cast<int>(unknowns.size()); ++i) idx[unknowns[i]] = i;
        const int n0 = static_cast<int>(unknowns.size());
        std::vector<std::vector<double>> a(static_cast<size_t>(n0),
                                          std::vector<double>(static_cast<size_t>(n0), 0.0));
        std::vector<double> b(static_cast<size_t>(n0), 0.0);

        auto stamp = [&](const QString& p, const QString& q, double ohms) {
            const double cond = 1.0 / std::max(ohms, 1e-9);
            const bool pRef = p == extract, qRef = q == extract;
            if (!pRef) a[idx[p]][idx[p]] += cond;
            if (!qRef) a[idx[q]][idx[q]] += cond;
            if (!pRef && !qRef) {
                a[idx[p]][idx[q]] -= cond;
                a[idx[q]][idx[p]] -= cond;
            }
        };
        for (const VdSegment& s : g.segments)
            if (comp.contains(s.fromNode) && comp.contains(s.toNode))
                stamp(s.fromNode, s.toNode, branchR.value(s.id));
        if (req.returnPath == VdReturn::Chassis)
            for (const VdLoad& l : req.loads)
                if (comp.contains(l.nodeId)) stamp(l.nodeId, reference, req.chassisOhms);

        b[idx[inject]] += amps;   // and out at `extract`, which grounding that node accounts for

        std::vector<double> x;
        if (!solveLinear(a, b, x)) {
            out.problem = QStringLiteral(
                "The circuit does not close: check the return, and that every load is wired.");
            return false;
        }
        QHash<QString, double> potential;
        potential[extract] = 0.0;
        for (int i = 0; i < n0; ++i) potential[unknowns[i]] = x[static_cast<size_t>(i)];
        for (const VdSegment& s : g.segments) {
            if (!comp.contains(s.fromNode) || !comp.contains(s.toNode)) continue;
            const double branchI = (potential.value(s.fromNode) - potential.value(s.toNode)) /
                                   std::max(branchR.value(s.id), 1e-9);
            // A wire the other leg already accounted for keeps the larger of the two: a wire cannot
            // be in both legs of one loop, and near-zero readings here are the solve saying so.
            if (std::abs(branchI) > std::abs(current.value(s.id, 0.0))) current[s.id] = branchI;
        }
        return true;
    };

    const VdLoad& load0 = req.loads.front();
    if (!solveLeg(req.sourceNode, load0.nodeId, out.totalCurrentA, QStringLiteral("feed"))) return out;
    if (req.returnPath != VdReturn::NotModelled) {
        // Where the load's own return leaves by: the load pin itself unless the drawing says another.
        const QString from = load0.returnNodeId.isEmpty() ? load0.nodeId : load0.returnNodeId;
        if (!solveLeg(from, reference, out.totalCurrentA, QStringLiteral("return"))) return out;
    }

    // ---- The breakdown, along the path the current favours ----
    auto rowsAlong = [&](const std::vector<QString>& segIds, const QString& startNode, VdLeg leg) {
        QString at = startNode;
        for (const QString& id : segIds) {
            const VdSegment* seg = nullptr;
            for (const VdSegment& s : g.segments)
                if (s.id == id) { seg = &s; break; }
            if (!seg) continue;
            const VdNode* from = g.node(at);
            const VdNode* to = g.node(VdGraph::other(*seg, at));
            if (!from || !to) break;

            VdSegmentRow row;
            row.name = QStringLiteral("%1 → %2").arg(from->name, to->name);
            row.wireId = seg->wireId;
            row.label = seg->label;
            row.leg = leg;
            row.gauge = seg->awg > 0 ? QStringLiteral("%1 AWG").arg(seg->awg)
                                     : QStringLiteral("%1 mm²").arg(seg->mm2, 0, 'f', 2);
            row.material = ConductorSpec::name(seg->material);
            row.lengthM = seg->lengthM;
            row.conductorTempC = tempC;
            row.currentA = std::abs(current.value(seg->id));
            row.wireOhms = wireResistanceOhms(*seg, tempC);
            row.wireBasis = QStringLiteral("%1 mm² %2 at %3 °C")
                                .arg(seg->mm2, 0, 'f', 3)
                                .arg(row.material)
                                .arg(tempC, 0, 'f', 1);

            // The interface the current ENTERS this segment through belongs to this row; the far end
            // belongs to the next one, so nothing is counted twice down the path.
            if (req.contactOverrides.contains(from->id)) {
                row.pinOhms = req.contactOverrides.value(from->id);
                row.pinBasis = QStringLiteral("entered for this contact");
            } else if (from->kind == VdNodeKind::Pin) {
                bool known = false;
                row.pinOhms = contactResistanceOhms(from->contactSize, req.resistanceCase, &known);
                row.pinBasis = known
                    ? QStringLiteral("size %1 contact, %2").arg(
                          from->contactSize,
                          req.resistanceCase == VdCase::Nominal ? QStringLiteral("nominal")
                                                                : QStringLiteral("worst case"))
                    : QStringLiteral("contact size not stated");
                if (!known)
                    out.gaps.push_back({from->connectorId, from->name,
                                        QStringLiteral("Contact size"), QStringLiteral("size code"),
                                        QStringLiteral("Voltage drop"),
                                        QStringLiteral("20, 16, 12, 8 - the mating pair's size")});
            } else {
                row.pinBasis = QStringLiteral("no mating interface");
            }

            if (req.jointOverrides.contains(from->id)) {
                row.spliceOhms = req.jointOverrides.value(from->id);
                row.spliceBasis = QStringLiteral("entered for this joint");
            } else if (from->kind == VdNodeKind::Splice) {
                bool known = false;
                row.spliceOhms = jointResistanceOhms(from->jointType, req.resistanceCase, &known);
                row.spliceBasis = known ? QStringLiteral("%1 joint").arg(from->jointType)
                                        : QStringLiteral("joint type not stated");
            } else {
                row.spliceBasis = QStringLiteral("not a joint");
            }

            row.dropV = row.currentA * row.totalOhms();
            row.percentOfSource = req.sourceV > 0.0 ? 100.0 * row.dropV / req.sourceV : 0.0;
            out.rows.push_back(row);
            out.wireOhms += row.wireOhms;
            out.pinOhms += row.pinOhms;
            out.spliceOhms += row.spliceOhms;
            out.totalDropV += row.dropV;
            at = to->id;
        }
    };

    const std::vector<QString> feed = voltageDropPath(g, req.sourceNode, load0.nodeId, tempC);
    if (feed.empty()) {
        out.problem = QStringLiteral("No path from the source to the load.");
        return out;
    }
    rowsAlong(feed, req.sourceNode, VdLeg::Feed);

    // The load's own contact is the last interface on the way in, and no segment starts there.
    if (const VdNode* loadNode = g.node(load0.nodeId);
        loadNode && loadNode->kind == VdNodeKind::Pin) {
        bool known = false;
        const double r = req.contactOverrides.contains(loadNode->id)
                             ? req.contactOverrides.value(loadNode->id)
                             : contactResistanceOhms(loadNode->contactSize, req.resistanceCase, &known);
        if (r > 0.0) {
            VdSegmentRow row;
            row.name = QStringLiteral("%1 contact").arg(loadNode->name);
            row.wireId = loadNode->id;
            row.label = loadNode->name;
            row.gauge = QStringLiteral("—");
            row.material = QStringLiteral("contact");
            row.currentA = out.totalCurrentA;
            row.pinOhms = r;
            row.pinBasis = known ? QStringLiteral("size %1 contact").arg(loadNode->contactSize)
                                 : QStringLiteral("entered for this contact");
            row.conductorTempC = tempC;
            row.dropV = row.currentA * r;
            row.percentOfSource = req.sourceV > 0.0 ? 100.0 * row.dropV / req.sourceV : 0.0;
            out.rows.push_back(row);
            out.pinOhms += r;
            out.totalDropV += row.dropV;
        }
    }

    const QString backFrom = load0.returnNodeId.isEmpty() ? load0.nodeId : load0.returnNodeId;
    if (req.returnPath == VdReturn::ThroughHarness) {
        const std::vector<QString> back = voltageDropPath(g, backFrom, reference, tempC);
        // A return that comes back along a wire the feed already used is the same copper counted
        // twice - usually a return pin picked on the wrong connector. Say so rather than let a
        // doubled figure pass for a loop.
        for (const QString& id : back)
            if (std::find(feed.begin(), feed.end(), id) != feed.end()) {
                out.assumptions << QStringLiteral(
                    "The return runs back along a wire the feed already uses, so that wire is "
                    "counted twice: check which pin the return really comes back to.");
                break;
            }
        rowsAlong(back, backFrom, VdLeg::Return);
        // And the supply-side return pin, for the same reason the load's own contact is a row: it is
        // the last interface of the loop and no segment starts there. A two-connector harness crosses
        // four mating pairs, and all four belong in the figure.
        if (const VdNode* endNode = g.node(reference);
            endNode && endNode->kind == VdNodeKind::Pin) {
            bool known = false;
            const double r =
                req.contactOverrides.contains(endNode->id)
                    ? req.contactOverrides.value(endNode->id)
                    : contactResistanceOhms(endNode->contactSize, req.resistanceCase, &known);
            if (r > 0.0) {
                VdSegmentRow row;
                row.name = QStringLiteral("%1 contact").arg(endNode->name);
                row.wireId = endNode->id;
                row.label = endNode->name;
                row.gauge = QStringLiteral("—");
                row.material = QStringLiteral("contact");
                row.leg = VdLeg::Return;
                row.currentA = out.totalCurrentA;
                row.pinOhms = r;
                row.pinBasis = known ? QStringLiteral("size %1 contact").arg(endNode->contactSize)
                                     : QStringLiteral("entered for this contact");
                row.conductorTempC = tempC;
                row.dropV = row.currentA * r;
                row.percentOfSource = req.sourceV > 0.0 ? 100.0 * row.dropV / req.sourceV : 0.0;
                out.rows.push_back(row);
                out.pinOhms += r;
                out.totalDropV += row.dropV;
            }
        }
    } else if (req.returnPath == VdReturn::Chassis && req.chassisOhms > 0.0) {
        // The body of the machine is part of this loop, and a poor ground strap has cost more than
        // the whole of the copper before now - so it is a row, with its figure in the open.
        VdSegmentRow row;
        const VdNode* at = g.node(backFrom);
        row.name = QStringLiteral("%1 → chassis").arg(at ? at->name : QStringLiteral("load"));
        row.label = QStringLiteral("chassis return");
        row.gauge = QStringLiteral("—");
        row.material = QStringLiteral("chassis");
        row.leg = VdLeg::Return;
        row.conductorTempC = tempC;
        row.currentA = out.totalCurrentA;
        row.wireOhms = req.chassisOhms;
        row.wireBasis = QStringLiteral("ground path allowance, %1 mΩ")
                            .arg(req.chassisOhms * 1000.0, 0, 'f', 2);
        row.dropV = row.currentA * row.totalOhms();
        row.percentOfSource = req.sourceV > 0.0 ? 100.0 * row.dropV / req.sourceV : 0.0;
        out.rows.push_back(row);
        out.wireOhms += row.wireOhms;
        out.totalDropV += row.dropV;
    }

    // The breakdown is arrived at by walking the path; that walk is what the total has to agree with,
    // and what the report shows.
    out.endVoltageV = req.sourceV - out.totalDropV;
    out.solved = true;

    out.profile.push_back({QStringLiteral("Source"), req.sourceV, 0.0});
    for (const VdSegmentRow& r : out.rows) {
        const VdProfilePoint& last = out.profile.back();
        out.profile.push_back({r.name, last.voltageV - r.dropV, last.cumulativeDropV + r.dropV});
    }

    out.assumptions << QStringLiteral("Contact and joint resistance: %1 figures.")
                           .arg(req.resistanceCase == VdCase::Nominal ? QStringLiteral("nominal")
                                                                     : QStringLiteral("worst case"));
    out.assumptions << QStringLiteral("Conductors at %1 °C.").arg(tempC, 0, 'f', 1);
    return out;
}

} // namespace Edraw_Desktop
