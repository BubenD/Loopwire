#pragma once

#include "harness/PartsLibrary.h"
#include "model/Entity.h"

#include <QString>
#include <QStringList>
#include <vector>

namespace Edraw_Desktop {

class Document;

// ---- The harness as a physical object ----------------------------------------------------------
// The mechanical drawing is a TREE: a trunk that branches at breakouts, with a connector or a free
// end at every tip. What it needs beyond the schematic is size and length - how thick each run is,
// how long it is, and what is fitted over it.
//
// The thickness is never drawn by hand. It is worked out from the conductors that run through the
// segment, so changing a wire's gauge changes the bundle it is drawn in, and a boot that no longer
// fits says so. That is the reason to generate this drawing rather than sketch it.

// One conductor running through the harness.
struct MechWire {
    QString id;
    QString label;
    // The cross-section as the schematic writes it: mm2 ("0.5"). An AWG figure ("22 AWG") is
    // accepted and converted.
    QString gauge;
    // Over the insulation. 0 means work it out from the gauge - and if the gauge says nothing
    // either, that is a gap the drawing has to answer, not something to guess at.
    double odMm = 0.0;
    QString libPart;
};

enum class MechNodeKind { Root, Breakout, Connector, FreeEnd };

QString mechNodeKindName(MechNodeKind kind);

// A part fitted at a node (a backshell, a boot) or along a segment (a sleeve, a conduit, a tie).
struct MechFitting {
    QString id;
    QString libPart;                        // the library part, when it comes from the library
    PartKind kind = PartKind::HeatshrinkBoot;
    QString name;                           // what the drawing calls it
    // What a part typed on the spot says about itself. A library part's own figures are used when
    // these are left at 0 - the same hybrid the mechanical module engine describes: library or
    // manual, one set of geometry either way.
    double lengthMm = 0.0;
    double idMm = 0.0;
    double odMm = 0.0;
    // Along a segment: how far from its start. At a node: how far back from the node.
    double atMm = 0.0;
};

struct MechNode {
    QString id;
    QString name;                           // "P1", "BR1"
    MechNodeKind kind = MechNodeKind::Breakout;
    QString parentSegment;                  // "" on the root
    // Which way this node's own segment leaves its parent, in degrees, measured from the parent's
    // direction. 0 carries straight on, +90 turns up. Relative rather than absolute, so moving a
    // branch takes its own sub-branches with it.
    double angleDeg = 0.0;
    QString libPart;                        // the connector, when it is one
    int pinCount = 0;
    double bodyWidthMm = 0.0;               // 0 -> from the library part, or a default
    double bodyHeightMm = 0.0;
    std::vector<MechFitting> fittings;
};

struct MechSegment {
    QString id;
    QString fromNode;
    QString toNode;
    double lengthMm = 0.0;
    QStringList wireIds;                    // the conductors running through this run
    std::vector<MechFitting> coverings;     // fitted along it
};

// How the drawing is laid out, and what is drawn on it.
struct MechDrawOptions {
    Vec2 origin{100.0, 300.0};
    double scale = 1.0;                     // drawing units per millimetre of harness
    // Random-lay bundle factor for four conductors or more:
    //   D = factor * sqrt(sum of the conductors' squared diameters).
    // 1.2 is the usual figure for a round bundle. Two and three wires are worked out from their own
    // geometry instead (see bundleDiameterMm), because the factor is wrong at that size.
    double bundleFactor = 1.2;
    double minBundleMm = 2.0;
    // Sizes ACROSS the run - the bundle body, a fitting's outside diameter, a connector's height -
    // multiplied by this, while lengths along the run are left alone. At full size a 6 mm bundle on
    // a 500 mm run is a hairline, which is true and useless; a formboard drawing exaggerates the
    // body so it can be read. Anything other than 1 puts a note on the drawing saying so, because a
    // width scaled off the print would otherwise be wrong.
    double diameterExaggeration = 1.0;
    bool dimensions = true;                 // a length dimension on every segment
    bool diameterLabels = true;             // the bundle diameter written along each run
    bool balloons = false;                  // a callout on every fitting
    bool connectorBodies = true;
    bool wireCounts = false;                // "(6)" beside each run
    double textHeight = 5.0;
    QString layer = QStringLiteral("0");
};

struct MechHarnessDef {
    QString name;
    std::vector<MechWire> wires;
    std::vector<MechNode> nodes;
    std::vector<MechSegment> segments;
    MechDrawOptions draw;

    const MechNode* node(const QString& id) const;
    const MechSegment* segment(const QString& id) const;
    const MechWire* wire(const QString& id) const;
    const MechNode* rootNode() const;
    // The segments leaving a node, in definition order.
    std::vector<const MechSegment*> childSegments(const QString& nodeId) const;
    // A name nothing else has, for a new node or segment.
    QString nextNodeId() const;
    QString nextSegmentId() const;
};

// ---- Sizes --------------------------------------------------------------------------------------

// The outside diameter of one conductor of this cross-section, over thin-wall insulation. Nominal
// figures in the region of ISO 6722 class B; a wire whose own diameter matters is given it
// explicitly on the wire. Returns 0 when the gauge says nothing at all.
double wireOuterDiameterMm(const QString& gauge);

// The bundle a set of conductors makes. Three cases, because one formula does not cover them:
//   one wire   -> the wire itself
//   two wires  -> side by side
//   three or more -> factor * sqrt(sum of squares), the random-lay figure
// Never less than `minMm`, so an empty run still has a body to draw.
double bundleDiameterMm(const std::vector<double>& wireOds, double factor, double minMm);

// The packing factor to use when nothing else is said. A few conductors lie badly and leave more air
// between them, so they pack loosely; a big bundle settles down. Hence 1.25 at the small end of the
// range and 1.15 at the large one - the range every harness standard quotes.
double autoPackingFactor(int wireCount);

// The working behind one bundle diameter, so the figure can be shown as a calculation rather than
// asserted. What the bundle-diameter tool displays, and what makes the result checkable by hand.
struct BundleCalc {
    std::vector<double> wireOds;      // the conductors counted, largest first
    int counted = 0;                  // how many had a diameter at all
    int unknown = 0;                  // how many did not, and so were left out
    double sumOfSquares = 0.0;        // sum of D_wire^2, mm^2
    double root = 0.0;                // sqrt of that
    double packingFactor = 1.0;       // k_packing actually applied
    bool packingAuto = true;          // whether that k came from the count or was given
    double result = 0.0;              // D_bundle, mm
    bool clampedToMinimum = false;    // the answer was the floor rather than the formula
    // Which rule produced it: "single", "pair", "triple" or "random-lay". The formula only claims to
    // describe a random lay of four or more; below that the geometry is exact and is used instead.
    QString rule;
    // The formula, with this calculation's own numbers in it, line by line.
    QStringList workings;
    // Where the figure comes from and where it stops being trustworthy.
    QString explanation() const;
};

// `packingFactor` <= 0 means: work it out from the count (autoPackingFactor).
BundleCalc bundleCalculation(const std::vector<double>& wireOds, double packingFactor,
                             double minMm);
// The same for a segment of a definition, over the conductors routed through it.
double segmentBundleDiameterMm(const MechHarnessDef& def, const MechSegment& seg);

// ---- Layout -------------------------------------------------------------------------------------

struct MechPlacedNode {
    QString id;
    Vec2 at{0.0, 0.0};
    double dirDeg = 0.0;   // the direction its incoming segment arrives on
};

struct MechPlacedSegment {
    QString id;
    Vec2 a{0.0, 0.0};
    Vec2 b{0.0, 0.0};
    double dirDeg = 0.0;
    double diameterMm = 0.0;
};

struct MechLayout {
    std::vector<MechPlacedNode> nodes;
    std::vector<MechPlacedSegment> segments;
    const MechPlacedNode* node(const QString& id) const;
    const MechPlacedSegment* segment(const QString& id) const;
};

// Walk the tree from the root, placing every node. A node whose parent cannot be reached is left
// out - and reported by validateMechanicalHarness rather than drawn somewhere arbitrary.
MechLayout layoutMechanicalHarness(const MechHarnessDef& def);

// ---- What is wrong with it ----------------------------------------------------------------------

enum class MechIssueKind { Gap, Violation, Warning };

struct MechIssue {
    MechIssueKind kind = MechIssueKind::Gap;
    QString objectId;
    QString objectName;
    QString property;      // Gap: which figure is missing
    QString message;
};

// Gaps (a wire with no diameter, a segment with no length) are kept apart from violations (a boot
// that will not go over the bundle): one means the drawing is unfinished, the other that it is
// wrong. Neither is guessed at.
std::vector<MechIssue> validateMechanicalHarness(const MechHarnessDef& def,
                                                 const PartsCatalog* catalog);

// Does this part take a bundle of that diameter? Reads the library's own Range fields - a
// backshell's "cableEntry", a boot's, a sleeve's "diameter" - as the fit window. False with `why`
// filled in when it does not; true with `why` empty when it does. `known` is false when the part
// does not state a window at all, which is a gap rather than a pass.
bool mechFittingFits(const LibraryPart& part, double bundleMm, QString& why, bool* known = nullptr);

// ---- The drawing --------------------------------------------------------------------------------

// The mechanical drawing, as entities ready to be placed: the bundle bodies to scale, the breakout
// junctions, the connectors, the fittings, and the dimensions and labels the options ask for.
std::vector<Entity> buildMechanicalHarness(const MechHarnessDef& def, const PartsCatalog* catalog);

// ---- From what is already drawn -----------------------------------------------------------------

// A first definition read off the schematic: every connector becomes a branch, every wire is routed
// into the branches of the two connectors it joins, and what runs between them becomes the trunk.
// Lengths are what the schematic happens to be drawn at, which is a starting point and no more -
// the whole reason the mechanical drawing exists is that those are not the real lengths.
MechHarnessDef mechFromDrawing(const Document& doc);

} // namespace Edraw_Desktop
