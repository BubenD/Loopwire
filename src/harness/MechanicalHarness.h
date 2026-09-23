#pragma once

#include "harness/MechModels.h"
#include "harness/PartsLibrary.h"
#include "model/Entity.h"

#include <QString>
#include <QStringList>
#include <functional>
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

// How far the conductors run open behind a connector, until somebody says otherwise. A starting figure
// in the same sense as a run's starting length: the released drawings use 10 to 40 mm, and 20 is the
// one they use most. Zero would mean heatshrink hard up against the contacts, which is never right, so
// the default is a workable figure rather than nothing.
inline constexpr double kMechOpenLengthMm = 20.0;

enum class MechNodeKind { Root, Breakout, Connector, FreeEnd };

QString mechNodeKindName(MechNodeKind kind);

// A part fitted at a node (a backshell, a boot) or along a segment (a sleeve, a conduit, a tie).
// The office's own drawings of the standard pieces, loaded once. A part number's shape comes out of the
// library; a KIND of thing - heatshrink, a boot, a ferrule - comes out of these.
const MechModelSet& mechModels();
// Which model kind draws a fitting of this part kind. False for the ones there is no standard drawing of.
bool mechModelForFitting(PartKind kind, MechModelKind& out);

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
    // WHICH default model draws it, when there is more than one of that kind - a straight backshell and a
    // right-angled one are both backshells. Empty takes the one the kind is set to use.
    QString modelName;
    // WHICH SIDE of the run it goes on. A part is drawn one way round and used both ways: the same boot on
    // the other side of the harness is the same drawing the other way up. False is the way it was drawn.
    bool flipped = false;
    // DIMENSION THIS STEP, and what the dimension is to READ.
    //
    // A build drawing is a list of steps and the sizes that go with them, and which sizes appear on it is a
    // decision per step rather than a setting for the whole sheet: where a boot goes matters and gets a
    // dimension, and the twenty ties along a trunk do not and would bury the drawing.
    //
    // `dimText` is the size it is to SAY, as against the size it measures. That is not a lie - it is how a
    // drawing is dimensioned: the nominal figure is what the shop works to, and what the model happens to
    // measure after the tolerances have been applied is not what belongs on the sheet.
    bool dimension = false;
    QString dimText;
    // A BALLOON POINTING AT THIS PART'S BOM LINE. On by default - a released drawing balloons
    // everything - but per part, because the exceptions are real: twenty ties along a trunk would bury
    // the sheet in numbers.
    bool balloon = true;
};

// ANOTHER CONNECTOR AT THE SAME END OF THE HARNESS.
//
// Two connectors feeding one cable is an ordinary harness and not a special case: a twin-lead adaptor,
// a pair of headers going into one loom, a power and a signal housing sharing a bundle. What that is,
// physically, is ONE PLACE with two housings on it - the wires leave both and gather into the same run.
//
// It is deliberately not modelled as two nodes. Two nodes would need a run between them to be a tree at
// all, and there is no run: the housings sit side by side. Nor is it a breakout, because a breakout is
// where the harness parts company and this is where it comes together. So the node keeps a list of the
// FURTHER housings on it, each with its own designator and way count, and the first connector stays
// where it was - which means every harness that has only one is untouched by this.
struct MechExtraEnd {
    // WHICH HOUSING THIS IS, so a run can say it leaves from this one and not from the station.
    //
    // The step id it came from. Without a name of its own an extra housing could only be referred to
    // by its place in the list, and a run pointing at "the second one" stops meaning that the moment
    // one is deleted.
    QString id;
    QString libPart;
    QString name;                           // its own designator: the second connector is not "P1"
    int pinCount = 0;
    bool balloon = true;
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
    double openLengthMm = kMechOpenLengthMm;
    // A BALLOON POINTING AT THIS CONNECTOR'S BOM LINE, as MechFitting::balloon. It was the only one of
    // the three that had no flag, so unticking "balloon it" on a connector step changed nothing.
    bool balloon = true;
    // DIMENSION THE CONNECTOR ITSELF: its own length, far face to the face the harness leaves by.
    // Separate from the open-conductor dimension, which measures the wires behind it and not the part.
    bool dimension = false;
    QString dimText;
    // The FURTHER housings on this same node. Empty for the ordinary one-connector end. See MechExtraEnd.
    std::vector<MechExtraEnd> extraEnds;
    // WHETHER ANYTHING HANGS OFF THIS STATION AS A PART OF IT.
    //
    // A station that has parts on it is a PLACE rather than a part: what stands there is what was hung
    // on it, and drawing the station's own body as well puts a housing on the sheet that nobody
    // described. So its body, its designator and its balloon stand down and the parts speak.
    //
    // This is a change of rule, and it costs the case where two housings feed one cable and BOTH were
    // meant: the second one is a part of the first, so the first now stands down and one box is drawn
    // where two were. Two housings that must both appear are two stations, or the second is described
    // with Next rather than Add subpart.
    bool hasSubparts = false;
};

struct MechSegment {
    // WHICH HOUSING AT `fromNode` THIS RUN LEAVES, when the station carries more than one.
    //
    // Empty means the station itself, which is every run on every harness that has one connector at a
    // place. Where two housings are stacked at one end they sit at different heights, and their runs
    // have to leave from THEM: both starting at the station's middle drew two bundles crossing out of
    // one point that belongs to neither housing.
    QString fromEnd;

    // WHERE ITS FAR END LANDS, when that is a station rather than wherever the run happened to reach.
    //
    // A station described in parts has limbs hanging off it - a connector, its wires - and those wires
    // end nowhere until the harness carries on. Add the next mother part and they should MEET it: that
    // is what a loom is, several tails gathering into the next thing along. Left alone they stopped in
    // mid air at whatever length each was given, and the new station sat past them unconnected.
    //
    // `toStation` is the node they gather on, and `toLane` which way in they take once there - one per
    // limb, so two wires entering one part do not land on the same spot. Empty means what it has always
    // meant: the run ends where it ends, which is every run that nothing follows.
    QString toStation;
    int toLane = 0;
    QString id;
    QString fromNode;
    QString toNode;
    double lengthMm = 0.0;
    QStringList wireIds;                    // the conductors running through this run

    // HOW MUCH BARE WIRE SHOWS AT EACH END OF THIS RUN, where the run says so itself.
    //
    // NEGATIVE means the run says nothing and the end's own figure stands - see MechNode::openLengthMm.
    // ZERO MEANS NONE, because it is a number somebody typed: a branch that goes straight into a boot
    // shows no bare wire there, and reading that 0 as "unsaid" drew the default 20mm and a fan of
    // conductors nobody asked for.
    double openStartMm = -1.0;
    double openEndMm = -1.0;

    // IS THERE A BUNDLE HERE AT ALL, or is this run so many conductors side by side the whole way? A
    // run of loose hook-up wire has no body drawn round it and nothing laid over it. See
    // MechStep::bundled - it is said on the step, never guessed from the lengths.
    bool bundled = true;
    std::vector<MechFitting> coverings;     // fitted along it
    // ---- A jacket, when the run is a CABLE rather than loose wires --------------------------------
    // A cable is not the bundle its cores would make loose: a cable whose five conductors add up to
    // Ø2.4 is still a Ø5.0 cable, because there is a jacket round them. So a run that is a cable is
    // drawn - and its boots and its heatshrink are sized - at the JACKET, and only where the jacket
    // has been cut back does the bundle inside it become the diameter that matters.
    double jacketOdMm = 0.0;                // over the jacket; 0 = loose wires, bundle from the cores
    QString cablePart;                      // the library cable, when it comes from one
    QString cableName;                      // what the schematic calls it ("W1", "cable")

    // THE RUN'S DESIGNATOR, WRITTEN ON THE DRAWING. Off by default: a released harness does not letter
    // every leg of itself, and a designator against each of thirty branches buries the sheet.
    //
    // AND WITHHELD ON A RUN BETWEEN TWO PARTS, whatever this says. A wire from P1 to P2 is already
    // identified by its ends - the wire list calls it P1-3 to P2-5 - so a designator beside it is a
    // third name for the same conductor and a fourth thing to keep in step. It earns its place where an
    // end is OPEN and there is nothing else to call the run by, which is exactly the harness that gets
    // built from bare wires. See MechNodeKind::FreeEnd.
    bool showName = false;
    // How much jacket is stripped off at each end, so the conductors come out and fan into the
    // connector. Over that length the run is the bundle, with a cone between the two diameters.
    double stripFromMm = 0.0;
    double stripToMm = 0.0;
    // DIMENSION THIS RUN, and what the dimension is to READ. A run's length is the figure a harness is
    // actually built to, so this is the one most likely to be wanted - and `dimText` is what it is to SAY, which
    // on a build drawing is the nominal figure rather than whatever the model measures.
    bool dimension = false;
    QString dimText;
    bool balloon = true;                    // as MechFitting::balloon
    // "LONGER THAN IT IS DRAWN": a break mark across the run. A two-metre trunk on an A3 sheet is
    // drawn short with a break in it and dimensioned at its real length, which is a property of the
    // run rather than of the paper it lands on.
    bool lengthBreak = false;

    bool isCable() const { return jacketOdMm > 0.0; }
    // A run KNOWN to be a cable - every conductor in it a core of the same one - whose jacket
    // diameter nobody has given yet. It is not a Ø2.4 bundle of loose wires and must not be sized as
    // one: until the figure is there, what a boot has to pass is unknown rather than small.
    bool cableWithoutJacket() const { return jacketOdMm <= 0.0 && !cableName.trimmed().isEmpty(); }
    double stripAt(bool atToEnd) const { return atToEnd ? stripToMm : stripFromMm; }
};

// How the drawing is laid out, and what is drawn on it.
struct MechDrawOptions {
    // DRAW EACH RUN AS A HARNESS RUN - the object the Special tab's Harness run draws - rather than as
    // an outline and a fan of separate lines. The run is then the same kind of thing whichever way it
    // was put on the sheet: picked, edited and carried on the same way, with its wires, its body and
    // its fans as settings on it. Where three runs or more meet, a breakout part is put at the
    // junction and each run ends on it, the way a harness is actually made up. The wizard turns this
    // on for its preview and for what it places; everything else draws as it always has.
    bool runsAsHarness = false;
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
    // WHICH WAY THE HARNESS RUNS ACROSS THE SHEET.
    //
    // The direction the ROOT is laid in; every other node is placed at its parent's direction plus its
    // own relative turn. So this one figure decides left-to-right (0) or right-to-left (180) for the
    // whole tree, and every relative turn on every branch is left alone by the change.
    //
    // It used to be a literal 0.0 inside layoutMechanicalHarness, which meant a harness could only
    // ever be built one way across the paper.
    double baseDirectionDeg = 0.0;
    bool dimensions = true;                 // a length dimension on every segment
    bool diameterLabels = true;             // the bundle diameter written along each run
    // A callout on every part, carrying its BOM item number. On by default: a released harness drawing
    // balloons everything, and a drawing whose parts cannot be looked up in the table is not one.
    bool balloons = true;
    // WHICH LINE OF THE DRAWING'S BILL OF MATERIALS A PART IS ON - 1-based, 0 for "not on it".
    //
    // A balloon that says 11 is a reference to line 11 of the bill, so the number has to be that
    // line's position in the bill the sheet actually carries. Left unset, the balloons are numbered
    // against the harness's OWN generated bill (mechBom), which is the right answer inside the
    // builder - the table is on the screen beside the drawing - and the wrong one on a sheet, where
    // the bill is the drawing's and its rows are in the drawing's order.
    //
    // A part the bill does not list balloons as "?" rather than as a number: a callout pointing at a
    // line that is not there is worse than one admitting it has nothing to point at.
    std::function<int(const QString& libPart, PartKind kind, const QString& description)> bomItemOf;
    // The numbered triangles and the NOTES block beside the harness.
    bool deltaNotes = true;
    // A BRANCH LEAVES AND ARRIVES HORIZONTALLY, dropping across in between - see MechPlacedSegment
    // ::path. This is how a harness is drawn: what sits at either end of a run is a connector, and a
    // connector is drawn square. Off, a branch is the single slanted line it always was.
    bool squareBranches = true;
    // How much straight run to leave at each end before the diagonal starts, in millimetres. Enough
    // to get clear of the breakout and to give the backshell and boot something straight to sit on.
    double branchLeadMm = 60.0;
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

// The outside diameter a LIBRARY conductor states over its insulation, or 0 where it states none.
//
// The part's own figure is the data-sheet one and the reason for picking a part out of the library at
// all, so it comes first wherever both it and a gauge exist. Zero is returned rather than a guess: a
// part that says nothing about its diameter leaves a gap for the nominal table (wireOuterDiameterMm)
// to answer from the gauge, and answering it here would put a made-up figure where a real one is
// missing.
double libraryWireOdMm(const LibraryPart& part);

// A gauge as a cross-section in mm2, from either "0.5" or "22 AWG" - both are written on real
// drawings and both arrive here. 0 when it said nothing usable.
//
// Exposed because the build script's compliance checks compare a contact's stated gauge range against
// the conductor it is crimped onto, and that comparison has to be done in ONE unit. A second AWG
// table elsewhere in the tree is a second table to be wrong.
double mechGaugeMm2(const QString& gauge);
// The cross-section of an AWG size. 0 for a size not in the table.
double mechAwgMm2(int awg);
// The AWG size nearest a cross-section - the other way through the same table, so a gauge quoted in
// mm2 can be restated in AWG without a second table being written down somewhere to disagree with this
// one. Nearest by RATIO rather than by difference: the sizes are a geometric series, so 0.4 mm2 is
// nearer 22 than 20 in the way a wire is chosen, though it is closer to 20 in plain subtraction.
// 0 when the cross-section says nothing.
int mechNearestAwg(double mm2);

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

// The contact chosen when a connector was placed, stamped on its block so the bill of materials can put it
// on the line below the connector. Empty means it was placed on its own.
inline const char* kContactPartKey = "contactPart";

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
    // THE DIRECTION IT IS DRAWN ALONG, which is not always the one it arrived on.
    //
    // A branch that turns is routed as a dogleg (see MechPlacedSegment::path) so that it comes IN
    // horizontally, and what sits here - a connector, its backshell, its boot, its designator - is
    // squared to that last leg rather than to the slant the branch took to get here.
    //
    // dirDeg is left alone because it is also the base every child of this node is placed from: a
    // branch of a branch is measured against the way its parent was HEADING, and squaring that would
    // quietly move every sub-branch on the sheet.
    double faceDeg = 0.0;
};

struct MechPlacedSegment {
    QString id;
    Vec2 a{0.0, 0.0};
    Vec2 b{0.0, 0.0};
    double dirDeg = 0.0;
    // THE WAY THE RUN ACTUALLY GOES, when it is not a straight line from a to b.
    //
    // A harness drawing does not send a branch off diagonally and leave it there. It leaves its
    // breakout horizontally, drops across to the height the other end is at, and arrives horizontally
    // - because what is at either end is a connector, and a connector is drawn square. A run that
    // arrives at 30 degrees puts its backshell, its boot and its designator on the slant.
    //
    // Empty means the run IS the straight line a..b, which is what a run in line with its parent is
    // and what every harness drawn before this was. Where it is set, the first and last legs are
    // horizontal (or rather: along the node directions at either end), so everything placed AT an end
    // - the mitre into the breakout, the fittings, the conductors - is square to it as before.
    std::vector<Vec2> path;
    double diameterMm = 0.0;        // the body: the jacket when it is a cable, else the bundle
    // What it is at each end, which is the body again unless the jacket is stripped back there. The
    // outline is mitred at a junction on THESE, not on the body - a cable that comes out of its jacket
    // before a breakout meets the other runs at the bundle it has narrowed to.
    double fromDiameterMm = 0.0;
    double toDiameterMm = 0.0;
};


// ---- Walking a run that is not a straight line ---------------------------------------------------
//
// Everything placed along a run - a fitting, a sleeve, a break mark, the figure that dimensions it -
// asks the same two questions: where is `x` millimetres along, and which way is the run going there.
// While every run was a straight line both had one-line answers (a + dir * x, and dir). A run that
// leaves its breakout horizontally, drops across and arrives horizontally has three legs, and the
// answers have to walk them.
//
// The path is in DRAWING units already, so the millimetre figure is scaled by the caller exactly as it
// was before - these take the distance in drawing units and nothing changes about who scales what.

// The legs of a run, whether it has a path or is still the straight line a..b.
inline std::vector<Vec2> segmentPath(const MechPlacedSegment& ps) {
    if (ps.path.size() >= 2) return ps.path;
    return {ps.a, ps.b};
}

// How long the drawn path is. A dogleg is LONGER than the straight line between its ends, which
// matters: a fitting 30 mm along is 30 mm along the wire, not 30 mm along the line of sight.
inline double segmentPathLength(const MechPlacedSegment& ps) {
    const std::vector<Vec2> p = segmentPath(ps);
    double total = 0.0;
    for (size_t i = 1; i < p.size(); ++i) total += distance(p[i - 1], p[i]);
    return total;
}

// Where `at` (in drawing units, from the start) falls on the run, and which way the run is heading
// there. Before the start and past the end it carries on along the first and last legs, so a fitting
// that overhangs an end lands where it would have if the run had been longer, rather than piling up
// on the end point.
inline void segmentPointAt(const MechPlacedSegment& ps, double at, Vec2* point, Vec2* dir) {
    const std::vector<Vec2> p = segmentPath(ps);
    double left = at;
    for (size_t i = 1; i < p.size(); ++i) {
        const Vec2 d = p[i] - p[i - 1];
        const double len = d.length();
        if (len < 1e-12) continue;
        const Vec2 u = d / len;
        // The last leg takes everything that is left, so a point past the end runs on along it.
        if (left <= len || i + 1 == p.size()) {
            if (point) *point = p[i - 1] + u * left;
            if (dir) *dir = u;
            return;
        }
        left -= len;
    }
    if (point) *point = ps.a;
    if (dir) *dir = Vec2{1.0, 0.0};
}

// HOW MUCH LONGER THE DRAWN RUN IS than the straight line between its ends.
//
// A dogleg goes further to get to the same place. Everything placed along a run is measured in the
// run's own millimetres - 0 at one end, its length at the other - and those have to be spread over
// the path that is actually drawn, or the body is laid out for a shorter run than the one on the
// sheet: it stops before the last leg and the connector at the end is left with nothing arriving at
// it. 1 for a straight run, so nothing that was drawn before this moves.
//
// It does mean a dogleg is not to scale ALONG itself. That is how a harness drawing works - the
// length is what the dimension says, not what a ruler on the paper says - and it is the whole reason
// a run can be drawn with a bend in it at all.
inline double segmentStretch(const MechPlacedSegment& ps) {
    if (ps.path.size() < 2) return 1.0;
    const double straight = distance(ps.a, ps.b);
    if (straight < 1e-12) return 1.0;
    return segmentPathLength(ps) / straight;
}

inline Vec2 segmentPointAlong(const MechPlacedSegment& ps, double at) {
    Vec2 p;
    segmentPointAt(ps, at, &p, nullptr);
    return p;
}

// Which way the run LEAVES its start, and which way it ARRIVES at its end. These are what a junction
// mitres against and what a connector is squared to - on a dogleg they are the two horizontal legs,
// which is the whole point of drawing one.
inline Vec2 segmentStartDir(const MechPlacedSegment& ps) {
    const std::vector<Vec2> p = segmentPath(ps);
    for (size_t i = 1; i < p.size(); ++i)
        if (distance(p[i - 1], p[i]) > 1e-12) return (p[i] - p[i - 1]).normalized();
    return Vec2{1.0, 0.0};
}
inline Vec2 segmentEndDir(const MechPlacedSegment& ps) {
    const std::vector<Vec2> p = segmentPath(ps);
    for (size_t i = p.size(); i >= 2; --i)
        if (distance(p[i - 2], p[i - 1]) > 1e-12) return (p[i - 1] - p[i - 2]).normalized();
    return Vec2{1.0, 0.0};
}


// ---- A covering that goes round a bend ------------------------------------------------------------
//
// Everything pulled over a run - a sleeve, a braid, a length of heatshrink, a conduit - used to be
// drawn as one rigid shape: a box, or the part's own drawing, put at the middle of the covered stretch
// and turned to the run's direction. That is right for a run that is a straight line, and wrong the
// moment the run bends: the box is laid straight across the corner, so the braid and the DR-25 over a
// branch were drawn cutting the bend while the conductors inside them went round it.
//
// So where a covering spans a knee it is drawn as what it is: the run's own path over that stretch,
// offset either side by half the covering's diameter and closed at both ends. One outline, following
// the wire.

// Does the stretch [fromAt, toAt] along the drawn path cross a bend?
inline bool spansBend(const MechPlacedSegment& ps, double fromAt, double toAt) {
    if (ps.path.size() < 3) return false;
    double acc = 0.0;
    for (size_t i = 1; i + 1 < ps.path.size(); ++i) {
        acc += distance(ps.path[i - 1], ps.path[i]);
        if (acc > fromAt + 1e-9 && acc < toAt - 1e-9) return true;
    }
    return false;
}

// The points of the run's path between two distances along it, ends included.
inline std::vector<Vec2> pathBetween(const MechPlacedSegment& ps, double fromAt, double toAt) {
    std::vector<Vec2> out;
    out.push_back(segmentPointAlong(ps, fromAt));
    double acc = 0.0;
    for (size_t i = 1; i + 1 < ps.path.size(); ++i) {
        acc += distance(ps.path[i - 1], ps.path[i]);
        if (acc > fromAt + 1e-9 && acc < toAt - 1e-9) out.push_back(ps.path[i]);
    }
    out.push_back(segmentPointAlong(ps, toAt));
    return out;
}

// Two rails either side of a run of points, mitred at the corners: the outline of something laid over
// it. Returned as one closed loop - up the plus side and back down the minus side.
inline std::vector<Vec2> sleeveOutlineAlong(const std::vector<Vec2>& pts, double halfWidth) {
    std::vector<Vec2> plus, minus;
    for (size_t i = 0; i < pts.size(); ++i) {
        Vec2 u;
        if (i + 1 < pts.size() && i > 0) {
            // At a corner, square to the average of the two legs - the way the corner turns.
            const Vec2 in = (pts[i] - pts[i - 1]).normalized();
            const Vec2 outv = (pts[i + 1] - pts[i]).normalized();
            u = Vec2{in.x + outv.x, in.y + outv.y};
        } else if (i + 1 < pts.size()) {
            u = pts[i + 1] - pts[i];
        } else {
            u = pts[i] - pts[i - 1];
        }
        if (u.length() < 1e-12) continue;
        const Vec2 v = u.normalized().perp();
        plus.push_back(pts[i] + v * halfWidth);
        minus.push_back(pts[i] - v * halfWidth);
    }
    std::vector<Vec2> loop = plus;
    for (auto it = minus.rbegin(); it != minus.rend(); ++it) loop.push_back(*it);
    return loop;
}

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

// Where a library part lands when the generator puts one down - by its marked face, by its connection
// point, by a flagged connection line, or centred on the joint if it has been told none of it. Exposed
// because "a marked face is what the part is set down by" is a claim about the generator, and checking it
// by copying the arithmetic into a test would only prove the copy right.
std::vector<Entity> placedPartDrawingForTest(const LibraryPart& part, const Vec2& at, double dirRad,
                                             const MechDrawOptions& opt);

} // namespace Edraw_Desktop
