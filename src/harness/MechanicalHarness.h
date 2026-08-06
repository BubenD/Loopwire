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
    // The cable this conductor is a core OF, when it is one. Every core of one cable carries the same
    // name, which is what lets a run be recognised as a cable rather than as loose wires.
    QString bundle;
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
    // The conductors run OPEN for this distance behind the connector - behind its backshell and boot
    // where it has them - before anything covers them. It is a figure on the drawing in its own right:
    // the wires have to fan out to the contacts and be inspected, so heatshrink never runs up to the
    // connector face, and how far it stops short is not left to whoever builds it.
    double openLengthMm = 0.0;
};

struct MechSegment {
    QString id;
    QString fromNode;
    QString toNode;
    double lengthMm = 0.0;
    QStringList wireIds;                    // the conductors running through this run
    std::vector<MechFitting> coverings;     // fitted along it
    // ---- A jacket, when the run is a CABLE rather than loose wires --------------------------------
    // A cable is not the bundle its cores would make loose: a cable whose five conductors add up to
    // Ø2.4 is still a Ø5.0 cable, because there is a jacket round them. So a run that is a cable is
    // drawn - and its boots and its heatshrink are sized - at the JACKET, and only where the jacket
    // has been cut back does the bundle inside it become the diameter that matters.
    double jacketOdMm = 0.0;                // over the jacket; 0 = loose wires, bundle from the cores
    QString cablePart;                      // the library cable, when it comes from one
    QString cableName;                      // what the schematic calls it ("W1", "cable")
    // How much jacket is stripped off at each end, so the conductors come out and fan into the
    // connector. Over that length the run is the bundle, with a cone between the two diameters.
    double stripFromMm = 0.0;
    double stripToMm = 0.0;

    bool isCable() const { return jacketOdMm > 0.0; }
    // A run KNOWN to be a cable - every conductor in it a core of the same one - whose jacket
    // diameter nobody has given yet. It is not a Ø2.4 bundle of loose wires and must not be sized as
    // one: until the figure is there, what a boot has to pass is unknown rather than small.
    bool cableWithoutJacket() const { return jacketOdMm <= 0.0 && !cableName.trimmed().isEmpty(); }
    double stripAt(bool atToEnd) const { return atToEnd ? stripToMm : stripFromMm; }
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
    // A callout on every part, carrying its BOM item number. The released drawings balloon everything;
    // it is off by default only because a drawing being built up is easier to read without them.
    bool balloons = false;
    // The numbered triangles and the NOTES block beside the harness.
    bool deltaNotes = true;
    bool connectorBodies = true;
    bool wireCounts = false;                // "(6)" beside each run
    // A junction where a conductor changes gauge has a splice in it, and a splice is never left bare:
    // heatshrink goes over it. It is drawn without being asked for, because the transition is worked
    // out from the conductors either side rather than typed in - the same reason the bundle is.
    bool spliceHeatshrink = true;
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
// The same for a segment of a definition, over the conductors routed through it. This is the bundle
// the bare conductors make - what the run is where its jacket has been cut back, and what it is all
// the way along when there is no jacket at all.
double segmentBundleDiameterMm(const MechHarnessDef& def, const MechSegment& seg);

// What the run is drawn AT along its body: the jacket when it is a cable, the bundle when it is loose
// wires. A Ø5.0 cable is a Ø5.0 run whatever its cores add up to.
double segmentBodyDiameterMm(const MechHarnessDef& def, const MechSegment& seg);

// And at one END of it: the bundle where the jacket is stripped back, the body where it is not. This
// is the figure a backshell or a boot at that end has to pass - which is why it is asked for by end
// and not once for the whole run.
double segmentEndDiameterMm(const MechHarnessDef& def, const MechSegment& seg, bool atToEnd);

// The run at a node, and which of its two ends the node sits at: the run that ARRIVES, or - at the
// root, where none does - the single run that leaves, taken by its start. Null at a breakout, where no
// one run is "the" run at it. A connector at the beginning of a trunk is a connector like any other,
// and this is what makes it one.
const MechSegment* nodeRunSegment(const MechHarnessDef& def, const MechNode& node,
                                  bool* atToEnd = nullptr);

// How much of the jacket is stripped at the end a node sits at. 0 when the run is not a cable.
double nodeBareLengthMm(const MechHarnessDef& def, const MechNode& node);

// The diameter a fitting at an END has to accept. Anything that goes over the wires themselves - a
// braid, heatshrink over the braid - is sized on the bare bundle; anything the CABLE passes into - a
// backshell, a boot, a gland - has to take the jacket, because the jacket is behind it.
// Returns 0 when the figure is NOT KNOWN - a cable whose jacket diameter nobody has given. Nothing
// can be checked against 0, and that is the honest answer: sizing a boot on the cores of a cable
// instead of on its jacket is how a boot that will not go on gets onto a drawing.
double fittingFitDiameterMm(const MechHarnessDef& def, const MechSegment* seg, PartKind kind,
                            bool atEnd, bool atToEnd);

// Whether this kind is pulled over the bare conductors at an end rather than stacked back from the
// node as a body of its own.
bool mechOverTheWires(PartKind kind);

// ---- The shape of one run -----------------------------------------------------------------------

// A run is not one diameter from end to end. A cable stripped back at both ends is bundle, cone,
// jacket, cone, bundle - and that shape is the drawing, not a decoration on it. One station per
// change of diameter, from the run's start to its end.
struct MechStation {
    double atMm = 0.0;
    double diameterMm = 0.0;
};

std::vector<MechStation> segmentProfile(const MechHarnessDef& def, const MechSegment& seg);

// ---- Where a conductor changes gauge ------------------------------------------------------------

// A junction where the conductors arriving are not the conductors leaving AND their sizes differ:
// there is a splice inside it. Worked out from the definition rather than declared, so re-gauging a
// wire moves the transition with it.
struct MechGaugeChange {
    QString nodeId;
    QString nodeName;
    QString fromSegment;
    QString toSegment;
    QString fromGauges;      // "0.5, 0.5" - what arrives
    QString toGauges;        // "1.0, 1.0" - what leaves
    double diameterMm = 0.0; // the larger of the two runs, which is what has to be covered
};

std::vector<MechGaugeChange> mechGaugeChanges(const MechHarnessDef& def);

// ---- The bill of materials, and the balloons that point at it -----------------------------------

// One line of the harness's bill of materials. `item` IS the balloon number on the drawing: a balloon
// says "item 11" and item 11 in the table says what to buy. Nothing is ballooned that is not in the
// table and nothing is in the table that is not ballooned - which is the whole point of generating
// both from one definition instead of keeping a table beside a drawing and hoping.
//
// One line per PART NUMBER rather than per place it is used: heatshrink used at four ends is one line
// whose quantity is the total, the way it is bought.
struct MechBomLine {
    int item = 0;
    QString libPart;                 // the library part, when it has one
    PartKind kind = PartKind::Other;
    QString description;
    QString mfrPn;
    QString mfrName;
    double qty = 0.0;
    QString unit;                    // "each" for a thing, "mm" for anything cut off a reel
    QStringList where;               // "P1", "BR1", "the run to P2" - so a line can be found
};

// The harness as a list of things to buy, ordered by what they are: connectors and contacts first,
// then backshells and boots, then cable and wire, then heatshrink, braid and sleeving, then labels,
// then adhesive - the order the released sample drawings use.
std::vector<MechBomLine> mechBom(const MechHarnessDef& def, const PartsCatalog* catalog);

// The item number for one thing on the drawing, so the balloon and the table agree. 0 when the thing
// is not in the table at all, which is a fault in the caller rather than a licence to draw a balloon
// with no number in it.
int mechBomItem(const std::vector<MechBomLine>& bom, const QString& libPart, PartKind kind,
                const QString& description);

// ---- Process notes ------------------------------------------------------------------------------

// A PROCESS note: a numbered triangle on the drawing and a line of text in the notes block. It says
// what to DO at that place - this is where the gauge changes, fill this transition with epoxy - as
// against a balloon, which says what to BUY. The sample drawings keep the two apart and so does this.
struct MechDeltaNote {
    int number = 0;
    QString text;
    QStringList atNodes;             // where its triangles go
};

// The notes the definition can work out for itself, numbered from 1. A gauge transition is one: it is
// wherever a conductor ends and a different cross-section carries on, which is already known.
std::vector<MechDeltaNote> mechDeltaNotes(const MechHarnessDef& def);

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
    double diameterMm = 0.0;        // the body: the jacket when it is a cable, else the bundle
    // What it is at each end, which is the body again unless the jacket is stripped back there. The
    // outline is mitred at a junction on THESE, not on the body - a cable that comes out of its jacket
    // before a breakout meets the other runs at the bundle it has narrowed to.
    double fromDiameterMm = 0.0;
    double toDiameterMm = 0.0;
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
// With a catalog, a run whose conductors are all cores of one cable is recognised as that cable and
// takes the jacket diameter off the library part, so a Ø5 cable arrives as a Ø5 run.
MechHarnessDef mechFromDrawing(const Document& doc, const PartsCatalog* catalog = nullptr);

} // namespace Edraw_Desktop
