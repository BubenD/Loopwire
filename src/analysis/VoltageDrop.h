#pragma once

#include "model/Entity.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <vector>

namespace Edraw_Desktop {

class Document;

// ---- Voltage drop across a harness ------------------------------------------------------------
// What a supply loses on its way to a load, and where it loses it: the copper, the contacts it
// passes through, and the joints along the way.
//
// Two things decide the shape of this engine. The drop is around a LOOP - a feed-only figure is
// half an answer, so the return leg (or a chassis reference) is part of the request rather than an
// afterthought. And a splice puts wires in PARALLEL, so adding resistances along one path is only
// the truth when the path is strictly series; anything else is solved as a network, and the
// breakdown is then a report over that solution.

enum class ConductorMaterial { BareCopper, TinnedCopper, SilverPlatedCopper, Aluminium };

// rho20 in ohm*mm2/m, alpha20 in 1/K. A coating raises the resistance of a stranded conductor a
// little (less copper per strand); it is kept apart from the metal so the metal's own figures stay
// recognisable.
struct ConductorSpec {
    ConductorMaterial material = ConductorMaterial::TinnedCopper;
    double rho20 = 0.017241;
    double alpha20 = 0.00393;
    double coating = 1.03;
    static ConductorSpec forMaterial(ConductorMaterial m);
    static QString name(ConductorMaterial m);
};

// The lay of the strands adds length to the conductor for the same run of harness.
inline double strandFactor(bool stranded) { return stranded ? 1.03 : 1.0; }

enum class VdNodeKind { Pin, Splice, OpenEnd, Chassis, Source };

struct VdNode {
    QString id;
    QString name;                 // "P1:4", "SP2", "W7 open end"
    VdNodeKind kind = VdNodeKind::OpenEnd;
    QString connectorId;
    int pin = 0;
    // Mating-pair size ("20", "16", "12", "8") when the drawing says; empty means it does not, and
    // the analysis reports that as a gap rather than assuming a size.
    QString contactSize;
    QString jointType;            // "crimp" | "weld" | "solder" | "terminal"
    Vec2 at{0.0, 0.0};
};

struct VdSegment {
    QString id;                   // one per wire entity (a wire through a splice is two entities)
    QString wireId;
    QString label;
    QString fromNode;
    QString toNode;
    double lengthM = 0.0;
    double mm2 = 0.0;
    int awg = 0;                  // 0 when the drawing gives mm2 only
    ConductorMaterial material = ConductorMaterial::TinnedCopper;
    bool stranded = true;
};

// The harness as a resistive network: pins, splices and open ends as nodes, wires as branches.
struct VdGraph {
    std::vector<VdNode> nodes;
    std::vector<VdSegment> segments;

    const VdNode* node(const QString& id) const;
    std::vector<const VdSegment*> incidentTo(const QString& nodeId) const;
    static QString other(const VdSegment& s, const QString& nodeId);
    // The nodes a user can pick as a source or a load, in reading order.
    QStringList terminals() const;
};

// Build the network from what is drawn: every wire's ends are resolved to the pin, splice or open
// end they land on, and ends that meet at a point become one node.
VdGraph buildVoltageDropGraph(const Document& doc);

enum class VdCase { Nominal, WorstCase };
enum class VdReturn { ThroughHarness, Chassis, NotModelled };
enum class VdLeg { Feed, Return };

struct VdLoad {
    QString nodeId;
    double currentA = 0.0;
    QString description;
    // The pin the load's own current leaves by, when that is a different pin from the one it arrives
    // at - the ordinary case for a two-connector harness, where the load bridges pin 1 and pin 2 and
    // the return runs back down its own wire. Empty means the load sits at one node (a ring main, a
    // chassis-grounded lamp), which is what the feed-only and chassis questions assume.
    QString returnNodeId;
};

struct VdRequest {
    QString sourceNode;
    double sourceV = 24.0;
    std::vector<VdLoad> loads;
    QString returnNode;                       // where the current comes back to
    VdReturn returnPath = VdReturn::ThroughHarness;
    double chassisOhms = 0.001;
    // Worst case for signing a design off, nominal for information: a contact's resistance spans
    // 2x within its size band, so which one was used belongs in the report.
    VdCase resistanceCase = VdCase::WorstCase;
    double limitPercent = 3.0;
    double ambientC = 20.0;
    // A rise over ambient for the conductors. The thermal engine can fill this in per wire; until it
    // does, one figure for the branch is honest and adjustable.
    double conductorRiseC = 0.0;
    QHash<QString, double> contactOverrides;   // by node id, ohms (0 for a stripped end)
    QHash<QString, double> jointOverrides;     // by node id, ohms
};

// One row of the breakdown: what it is, and its three resistances kept apart, because the fix for a
// contact-limited feed is not the fix for a copper-limited one.
struct VdSegmentRow {
    QString name;                 // "P1:4 -> SP2"
    QString wireId;
    QString label;
    QString gauge;
    QString material;
    VdLeg leg = VdLeg::Feed;
    double lengthM = 0.0;
    double conductorTempC = 20.0;
    double currentA = 0.0;
    double wireOhms = 0.0;
    double pinOhms = 0.0;
    double spliceOhms = 0.0;
    double dropV = 0.0;
    double percentOfSource = 0.0;
    QString wireBasis, pinBasis, spliceBasis;

    double totalOhms() const { return wireOhms + pinOhms + spliceOhms; }
};

// A parameter the analysis needed and the drawing does not state. Reported instead of assumed: a
// drop computed on a guessed contact size is a number nobody can sign.
struct VdGap {
    QString objectId;
    QString objectName;
    QString property;
    QString unit;
    QString blockedRule;
    QString hint;
};

struct VdProfilePoint {
    QString at;
    double voltageV = 0.0;
    double cumulativeDropV = 0.0;
};

struct VdReport {
    double sourceV = 0.0;
    double totalCurrentA = 0.0;
    double wireOhms = 0.0;
    double pinOhms = 0.0;
    double spliceOhms = 0.0;
    double totalDropV = 0.0;
    double endVoltageV = 0.0;
    std::vector<VdSegmentRow> rows;
    std::vector<VdProfilePoint> profile;
    std::vector<VdGap> gaps;
    QStringList assumptions;
    VdReturn returnPath = VdReturn::ThroughHarness;
    VdCase resistanceCase = VdCase::WorstCase;
    double limitPercent = 3.0;
    bool solved = false;
    QString problem;              // why it could not be solved, when it could not

    double totalOhms() const { return wireOhms + pinOhms + spliceOhms; }
    double dropPercent() const { return sourceV > 0.0 ? 100.0 * totalDropV / sourceV : 0.0; }
    bool exceedsLimit() const { return solved && dropPercent() > limitPercent; }
    bool blocked() const { return !gaps.empty(); }
    // Where the drop actually is. Copper is usually the answer, but a chain of size-20 contacts on a
    // 15 A feed can beat the wire - and then a bigger gauge fixes nothing.
    QString dominant() const;
    QString summary() const;
};

VdReport analyzeVoltageDrop(const VdGraph& graph, const VdRequest& request);

// The resistance of one mating pair / one joint, for the report and for the UI's own use.
double contactResistanceOhms(const QString& contactSize, VdCase which, bool* known = nullptr);
double jointResistanceOhms(const QString& jointType, VdCase which, bool* known = nullptr);
// R(T) = R20 * [1 + alpha20 * (T - 20)] * L * k_strand, over the whole segment.
double wireResistanceOhms(const VdSegment& seg, double conductorTempC);

// The least-resistance path between two nodes (what the current mostly takes, and what the canvas
// should light up). Empty when they are not connected.
std::vector<QString> voltageDropPath(const VdGraph& graph, const QString& fromNode,
                                     const QString& toNode, double conductorTempC);
// True when adding resistances down that path is the whole truth. Two things have to hold: no node
// inside the path carries a third wire (a splice feeding two legs does not add up in a line), and
// there is no second route around the path from one end to the other (two wires in parallel do not
// either, however straight each one is).
bool voltageDropPathIsSeries(const VdGraph& graph, const std::vector<QString>& segmentIds,
                             const QString& fromNode, const QString& toNode);

} // namespace Edraw_Desktop
