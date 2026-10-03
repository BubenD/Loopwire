#pragma once

#include "harness/Harness.h"
#include "harness/PartsLibrary.h"
#include "model/Entity.h"

#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

namespace Edraw_Desktop {

// ==================================================================================================
// A HARNESS DESCRIBED AS BRICKS.
//
// Two kinds of brick. An END is a part the harness lands on - a connector, a ferrule, a crimp, a
// terminal - or a JOINT the harness passes through: a splice, a resistor, a heatshrink joint, a
// Y-split. It is a library part, or a placeholder of its kind until one is chosen. A SPECIAL is what
// runs between ends: the wires fanned out of one and into another, a cable or heatshrink over that
// run, a twisted pair, a twisted bundle. Two ends are never linked to each other directly, because
// what is between them is exactly what the harness is.
//
// AN END HAS A PORT FOR EVERY CONNECTION FACE ITS PART HAS - a connector one, a splice two, a Y-split
// three - where that face is on the part: the output faces marked on the library part, or the usual
// faces of its kind until one is chosen. A SPECIAL has two, the left where its run STARTS and the
// right where it FINISHES. Every brick turns a quarter at a time, and the part turns with it. A link
// joins a port of one brick to a port of another. A fanout may finish on several ends, each taking so
// many of its wires.
//
// Each brick has a place on a coarse grid - a column along the harness and a row across it - which is
// the low-detail picture the user arranges. The real drawing is made from it: ends in one column
// share an x, rows are a row pitch apart, and the columns are as far apart as the runs between them
// are long.
// ==================================================================================================

// A JOINT is tied to a special - a splice made in that run - and is not on the harness's way itself.
enum class BrickType { End, Fanout, Run, TwistedPair, TwistedBundle, Joint };
// A special's two ports.
enum BrickPort { kPortLeft = 0, kPortRight = 1 };

QString brickTypeName(BrickType type);
QString brickTypeKey(BrickType type);
BrickType brickTypeFromKey(const QString& key);
inline bool brickIsSpecial(BrickType t) { return t != BrickType::End && t != BrickType::Joint; }

// The kinds an end brick can be: the parts a harness finishes on, then the joints it passes through.
std::vector<PartKind> brickEndKinds();
std::vector<PartKind> brickJointKinds();
// A resistor, a capacitor, a coil or a ferrite bead: one Electronic component on the palette.
bool brickKindIsElectronic(PartKind kind);
// A JOINT has an entry face each side (a Y-split three); an end part has one.
bool brickKindIsJoint(PartKind kind);
// How many connection faces a part of this kind has when no library part says.
int brickKindFaceCount(PartKind kind);

// A LABEL on a run: on the whole bundle, or on one wire of a fan, so far from one end.
struct BrickLabel {
    int wire = -1;              // -1 = the whole harness; 0.. = that wire of the fan at `fromFinish`
    bool fromFinish = false;    // measured from the finish end rather than the start
    double distanceMm = 40.0;   // from that end's entry face, along the run
    QString text;               // empty: the wire's net name, if it has one
    // How long the label is along the run; 0 = as long as suits it. Either way it is kept off a
    // bend - shortened to the straight it sits on.
    double lengthMm = 0.0;
    // Its text called out with a leader beside the run, on the drawing.
    bool leader = true;
    // Covered over with clear heatshrink - the label model that has it.
    bool clearShrink = false;
};

// WHAT A SPECIAL IS DRAWN WITH. Below nought is "the drawing decides"; nought is a real figure.
struct BrickRunSpec {
    int wires = 4;
    double lengthMm = 150.0;       // along the harness, from the one face to the other
    double wireWidthMm = 2.0;      // one wire across, as drawn
    double fanStartMm = -1.0;      // how long the fan is at each end; <0 = as long as it needs
    double fanFinishMm = -1.0;
    double kneeRadiusMm = -1.0;    // the bends of the body
    double fanKneeMm = -1.0;       // the corners of the fans
    double fanScalePct = 95.0;     // the fans of a face spread over this share of it
    double bodyScalePct = 60.0;    // a cable or heatshrink this share of its face across
    double bodyWidthMm = 0.0;      // or this wide, when set
    // THE STACK, from the middle out: "wires", then any of "cable", "braid", "heatshrink" in the order
    // they go on. Only the outermost is seen - a braid under a heatshrink is not drawn.
    QStringList stack{QStringLiteral("wires")};
    RunEnds transitions = RunEnds::None;   // a transition heatshrink at the start, finish or both
    // HOW WIDE THE TRANSITION HEATSHRINK IS; nought = as the drawing sizes it. Never narrower than the
    // cable it goes over - a figure below that is drawn at the cable's width.
    double transitionWidthMm = 0.0;
    // THE PART NUMBER OF ITS TRANSITION HEATSHRINKS - at the ends of the cable's body, where it divides, at a
    // branch's end.
    QString transitionPart;
    // MORE TRANSITION HEATSHRINKS, one over another over the first (transitionPart): their part numbers, inside out.
    QStringList transitionLayers;
    // A CABLE TO SEVERAL PARTS AT ONE END: what its wires are after its body ends - 0 bare wires fanning out to each
    // part, 1 each branch of wires under heatshrink (with a transition where the body ends) and then its fan.
    int branchCover = 0;
    QString branchShrinkPart;   // the heatshrink over a branch's wires
    // HEATSHRINK OVER EACH WIRE'S END where the wires fan out: on or off, how long from the end, and its part number.
    bool fanEndShrink = false;
    double fanEndShrinkMm = 10.0;
    // HOW LONG EACH TRANSITION HEATSHRINK IS, at the run's start and at its finish: 0, as long as the drawing makes
    // it (three times the bundle across, never past the straight it sits on).
    double transitionLenStartMm = 0.0;
    double transitionLenFinishMm = 0.0;
    QString fanEndShrinkPart;
    // Dimensions: the whole run, each fan, and what is between the fans.
    bool dimTotal = false;
    bool dimFans = false;
    bool dimBetween = false;
    // What those dimensions SAY, when it is not the measured figure - the measurement is kept too.
    QString dimTextTotal;
    QString dimTextFans;
    QString dimTextBetween;
    // LINEAR: along the way the harness leaves its start. Not linear: at the angle from one point to
    // the other - or, TRACING the run, along it round every bend, knee radius and all.
    bool dimLinear = true;
    bool dimTrace = false;
    // The whole length said by a single-ended dimension: from its start, a short line with the figure.
    bool dimSingle = false;
    // WHICH SIDE each dimension goes: below the run, or above it. The whole length below, the fans and between them
    // above, unless told.
    bool dimTotalBelow = true;
    bool dimFansBelow = false;
    bool dimBetweenBelow = false;
    // BRIDGE WIRES: which wires of the special this one is joined to it takes ("1-3,5"); empty, as many as
    // it says of its own. Set, they decide how many wires it has and what each one's net is.
    QString bridgeWires;
    // A BRIDGE WIRE'S BRANCH: which of its cable's parts at that end it comes out beside (of a cable going to several
    // there) - empty, the first.
    QString bridgeAt;
    // A BRANCH: a run joined to a cable's body that takes some of its wires (bridgeWires says which) on to its own
    // part - with its own stack, fans, transitions - out of where the cable divides.
    bool isBranch = false;
    // ON THE BILL, NOT DRAWN: the heatshrink over the wire ends, the transition heatshrink - inside a connector's
    // backshell, where nobody sees them.
    bool fanEndShrinkInside = false;
    bool transitionInside = false;
    // STRETCHED OVER THE END PART: a transition at an end with no fan carried on over the part it goes into, so far
    // (0 - a tenth of the part's length).
    bool transitionStretch = false;
    double transitionStretchMm = 0.0;
    // WHICH TRANSITION IS INSIDE THE BACKSHELL (transitionInside): the start's, the finish's or both - not drawn, on the
    // bill with its length all the same.
    RunEnds transitionInsideEnds = RunEnds::Both;
    // THE SAME AT BOTH ENDS, or each its own: where they are not, these are the finish's - the fields above the start's.
    bool transitionSame = true;
    double transitionWidthFinishMm = 0.0;   // 0: Auto
    QString transitionPartFinish;
    bool transitionStretchFinish = false;
    double transitionStretchFinishMm = 0.0;
    // WHICH END A FANOUT SPREADS AT: -1 the end on a part (the start when both are), 0 the start,
    // 1 the finish.
    int fanSide = -1;
    // HOW MANY LINES THE WIRES CONVERGE TO where they are gathered; 0 = as the drawing draws it.
    int trunkWires = 0;
    // A TWISTED PAIR's number of pairs - two wires each.
    int pairs = 1;
    std::vector<BrickLabel> labels;
    // THE NET OF EACH WIRE, numbered from the top down like the labels: taken from the schematic
    // where the part is on it, or given here.
    QStringList nets;
    // THE PART NUMBER OF EACH LAYER OF THE STACK - "wires", "cable", "braid", "heatshrink" to a library part.
    QMap<QString, QString> stackParts;

    // What is outermost, and so what the body is drawn as.
    QString outermost() const;
};

// A JOINT IN A RUN: wires of the harness soldered or crimped together - one to one, several to one, so many to so
// many - with a splice part (or several), the wires that go out of it, and where along the run it is made.
struct BrickJoint {
    QString method = QStringLiteral("Crimp");   // or "Solder"
    QStringList parts;                          // the splice parts, from the library's Splice group
    QString inWires;                            // the harness's wires that go in, "1,2"
    int outCount = 1;                           // how many wires go on out of it
    QString outWirePart;                        // the wire they are, from the library
    QStringList outNames;                       // their names - an input's net carries on when not said
    double atMm = 50.0;                         // where, from the end said
    bool fromFinish = false;                    // measured from the run's finish (its start otherwise)
    bool stepped = false;                       // a STEP JOINT: the splices staggered along the run
    double stepMm = 15.0;                       // and how far apart
};

struct Brick {
    QString id;
    BrickType type = BrickType::End;
    PartKind kind = PartKind::Connector;   // for an end
    QString libPart;                       // the library part, when one has been chosen
    QString name;                          // designator ("X1") or a name for a special
    double col = 0.0;                      // along the harness, in grid steps
    double row = 0.0;                      // across it; down the pane is +
    int rot = 0;                           // quarter turns clockwise on the pane
    // TURNED AND MIRRORED ON THE DRAWING ONLY (R 45 degrees, Space 90, M mirror on the wizard's pane): the part as
    // drawn, on top of its quarter turns - the brick on the pane is not turned by it.
    double drawTurnDeg = 0.0;              // clockwise
    bool drawMirror = false;               // flipped left for right on the sheet
    bool balloon = false;
    // THE DESIGNATOR ON THE DRAWING, part by part: written in front of the part or not (off unless asked
    // for), and how tall its letters are.
    bool showDesignator = false;
    // MOVED UP OR DOWN ON THE DRAWING itself (dragged in the preview), off the row the pane gives it.
    double drawDyMm = 0.0;
    // DRAWING ORDER: a brick with a higher figure is drawn over one with a lower.
    int z = 0;
    // WHICH SIDE ITS DIMENSIONS GO: above the parts (the default) or below them.
    bool dimBelow = false;
    double designatorHeightMm = 3.5;
    BrickRunSpec run;                      // for a special
    // HOW BIG AN END'S PART IS DRAWN, 1 = as drawn in the library. Its faces scale with it, so the wires
    // and cables meet it where its faces now are. On Auto the drawing works it out: the part is made as
    // big as the bundle coming into it wants, so the wires run in with as few bends as there can be.
    double scale = 1.0;
    bool autoScale = false;
    // SIZED BY MATCHING it to another part (the brick it was matched to) - until its size is set some other way.
    QString matchedTo;
    BrickJoint joint;                      // for a joint
};

struct BrickLink {
    QString a;
    QString b;
    int portA = -1;    // the port of `a` it is on; -1 = whichever faces the other brick
    int portB = -1;
    int wires = 0;     // wires a fanout gives the end at this link; 0 = an even share
    // ITS OWN LENGTH, where a cable goes to several parts at one end: 0 = locked to the run's length with the others;
    // set (Shift and the part dragged on the drawing), this branch is that long whatever the others are.
    double lengthMm = 0.0;
    // WHICH OF THE CABLE'S WIRES go to this part, "1,3" (its count with them); empty, an even share.
    QString wireList;
    // A TRANSITION HEATSHRINK AT THE END OF THIS BRANCH, before it fans out into the part.
    bool endBoot = false;
    double endBootLenMm = 0.0;   // its length; 0 as the drawing makes it
    // NO HEATSHRINK OVER THE WIRE ENDS AT THIS PART, where the run has them: they go on at every part it is linked to
    // unless the part is left out here.
    bool noEndShrink = false;
};

// WHAT A BRICK LOOKS LIKE UNTURNED: its connection faces - where, which way they look, how long -
// with the middle of the part at 0,0 on the drawing, and for an end the drawing itself. The brick's
// turn is applied on top.
struct BrickShape {
    std::vector<Vec2> faceMid;
    std::vector<Vec2> faceOut;      // square to each face, away from the part
    std::vector<double> faceLen;
    Vec2 half{12.0, 10.0};           // half the part's size along x and y
    std::vector<Entity> drawing;    // an end's drawing, unturned
    bool haveEnds = false;          // the library part's own near and far points
    Vec2 nearAt{0.0, 0.0};
    Vec2 farAt{0.0, 0.0};
    int ports() const { return static_cast<int>(faceOut.size()); }
};

// A DIMENSION PUT ON BY HAND: between two bricks, above or below them.
struct BrickDim {
    QString a;
    QString b;
    bool below = false;
};

struct BrickDesign {
    std::vector<Brick> bricks;
    std::vector<BrickLink> links;
    std::vector<BrickDim> dims;
    // Where library parts are read from - for how many faces an end has, and where. Not saved.
    const PartsCatalog* catalog = nullptr;
    // How much of a cable's width the wires of a fan joined to it enter across (Options, Drawing:
    // Auto scale fan). Not saved: it is the drawing's setting.
    double faceFanPct = 95.0;
    // Shapes already worked out, by what decides them. Not saved.
    mutable QHash<QString, BrickShape> shapeCache;
    // WHERE DIMENSIONS ARE TAKEN FROM at each end of a run: the end part's near point (its face) or
    // its far point (the back of it).
    bool dimFromFar = false;
    bool dimToFar = false;
    // Or from the MIDDLE of each part - halfway between its near point and its far point.
    bool dimFromMid = false;
    bool dimToMid = false;
    // THE BRICKS TURN WITH THE DRAWING: a part turned there (R 45, Space 90) has its brick's connections turned round
    // it on the pane as well. Off, the turn is the drawing's alone. (Mirroring is on the brick either way.)
    bool tieBrickTurn = true;
    // A DIMENSION MOVED IN OR OUT on the drawing (its line dragged in the preview): by its key - "manual:<n>" for one
    // put on between two bricks, "<special>:<kind>:<part>" for a run's - how far off its own place, in mm.
    QHash<QString, double> dimShift;
    // HELD WHERE IT IS on the drawing while a part is dragged there: this part stays at this x, so only what the drag
    // changes moves. Not saved.
    QString holdPart;
    double holdX = 0.0;
    // The part as its brick shows it: turned on the pane with the drawing only while they are tied.
    Brick asOnPane(const Brick& b) const {
        Brick t = b;
        if (!tieBrickTurn) t.drawTurnDeg = 0.0;
        return t;
    }

    Brick* brick(const QString& id);
    const Brick* brick(const QString& id) const;
    QString addBrick(BrickType type, PartKind kind, double col, double row);
    // The brick's shape, and its number of ports.
    BrickShape shapeOf(const Brick& b) const;
    int portCount(const Brick& b) const { return shapeOf(b).ports(); }
    // The way a port faces - on the pane (y down) and on the drawing (y up) - with the turn in it.
    Vec2 portPaneDir(const Brick& b, int port) const;
    Vec2 portWorldDir(const Brick& b, int port) const;
    // The port of `self` that best faces `toward` (pane units), of those `allowed` says may be used.
    int facingPort(const Brick& self, const Vec2& toward,
                   const std::function<bool(int)>& allowed = nullptr) const;
    void removeBrick(const QString& id);
    // LINK a port of an end to a port of a special. Refused, with the reason, when it would not
    // describe a harness: two ends or two specials, a port of a special already taken (a fanout may
    // finish on several ends, from one port), the same pair twice.
    bool connect(const QString& a, const QString& b, QString* why = nullptr, int portA = -1,
                 int portB = -1);
    void disconnect(const QString& a, const QString& b);
    bool linked(const QString& a, const QString& b) const;
    BrickLink* link(const QString& a, const QString& b);
    const BrickLink* link(const QString& a, const QString& b) const;
    QStringList linksOf(const QString& id) const;
    // Which port of `id` the link to `other` is on - the one stated, or the one facing it.
    int portOn(const QString& id, const QString& other) const;
    // The ends a special starts from (its left port) and finishes on (its right port).
    QStringList startsOf(const QString& specialId) const;
    QStringList finishesOf(const QString& specialId) const;
    // Everything on one port of a special: its parts, and a special it is joined to.
    QStringList sideOf(const QString& specialId, int port) const;
    // The end a fanout spreads at, 0 the start or 1 the finish, as fanSide decides it.
    int fanoutSpreadSide(const Brick& s) const;
    // Every end of a special, the starts first.
    QStringList endsOf(const QString& specialId) const;

    QJsonObject toJson() const;
    static BrickDesign fromJson(const QJsonObject& o);
};

// A special's shape: a port each side.
BrickShape specialShape();
// An end's shape: its library part's output faces, or the usual faces of its kind.
BrickShape endShape(const Brick& b, const PartsCatalog* catalog);
// A drawing direction turned `rot` quarters clockwise.
Vec2 turnQuarters(const Vec2& v, int rot);
// A point or direction of an end's own drawing as it is on the harness drawing: mirrored, turned its quarters, then
// by its drawing turn - and by `extraRad` more (counter-clockwise), for a part lined up with a face at a slant.
Vec2 turnOnDrawing(const Brick& b, const Vec2& v, double extraRad = 0.0);

// SYMMETRIC. The ends are laid out from the one most connected: it is column 0 on the axis, what its
// runs reach is the next column out, and so on; the ends of a column are spread evenly either side
// of the axis, keeping the order they were in. Each special sits halfway between its ends. What is
// linked to nothing is left where it was.
void arrangeBricks(BrickDesign& design);

// THE ROUTE OF A RUN from one entry face to another: it leaves `a` along `aOut` and arrives at `b`
// travelling against `bOut`, both square to their faces, with as few bends as that allows.
// `bendAt` is where along the way a level shift makes its slant, 0 at `a` to 1 at `b` - where the special's
// brick sits between its two parts on the pane.
// `kneeAt` says which bend `bendAt` places: 0 the first (the special in line with its start part - its brick IS
// the knee where the run leaves that line), 1 the second (in line with its finish part), -1 the middle of the
// slant (the special between the two lines).
std::vector<Vec2> routeBetweenFaces(const Vec2& a, const Vec2& aOut, const Vec2& b, const Vec2& bOut,
                                    double leadMm, double shiftDeg = 45.0, double bendAt = 0.5, int kneeAt = -1);

// ---- LENGTHS ON THE PANE ----------------------------------------------------------------------------
// A special's length and how far apart its two parts sit on the pane are one figure: 75 mm to a column. A
// part linked to a special whose other end is on a part is put at the special's length from it; moving the
// part changes the length; changing the length pulls or pushes the part - and everything beyond it - along.
constexpr double kBrickMmPerCol = 75.0;
// The least room the drawing keeps between two parts facing each other along a run - for the fans between them.
constexpr double kBrickRoomBetween = 0.0;   // none: a part may come right up to the other, the run down to nought
// The one part each side of a special, when it runs from one part to one part.
bool specialOneToOne(const BrickDesign& d, const QString& special, QString* start, QString* finish);
// `end` (linked to `special`) put as far along from the special's other part as the special is long, on the
// side it is already on; the special halfway between. False when there is no other part to measure from.
bool fitEndToLength(BrickDesign& d, const QString& special, const QString& end);
// The special's length from how far apart its two parts are on the pane, to the millimetre; the special kept
// at the same share of the way between them (`fraction`, below nought for halfway).
bool lengthFromPositions(BrickDesign& d, const QString& special, double fraction = -1.0);
// The special's length changed from `oldLen`: its finish part, and everything beyond it, moved along to suit.
bool pushForLength(BrickDesign& d, const QString& special, double oldLen);
// Every special put at its length from the part it starts on, out from the most connected part - after the
// bricks are arranged by columns, so the pane says the lengths again.
void fitPositionsToLengths(BrickDesign& d);

// ---- BRIDGE WIRES --------------------------------------------------------------------------------------
// Which wires of the special it is joined to a special takes: "1-3,5" is wires 1, 2, 3 and 5, in that order,
// none past `most` (nought: no limit) and none twice.
QList<int> parseWireList(const QString& text, int most = 0);
// The special joined to this one at a junction - the cable or fanout it takes its wires from - or empty.
QString joinedSpecial(const BrickDesign& d, const QString& special);
// Every special with bridge wires made up from them: as many wires as it takes, each with the net of the wire
// it takes. Returns how many specials were changed.
int syncBridgeWires(BrickDesign& d);
// The bridge wire taken is the cable's braided SHIELD rather than one of its wires.
constexpr char kBridgeShield[] = "shield";
// A BRIDGE WIRE: a special that takes some of the wires of the cable it is joined to (it has bridge wires set).
// Unlike any other special it may join a cable's end that already has its parts - the rest of the wires go on
// to them.
bool brickIsBridge(const Brick& b);
// AN END PART TIED TO ITS SPECIALS - put at their length, moving in line with them. A JOINT stands alone: it is
// placed where it is put and ties nothing.
bool brickIsTied(const Brick& b);
// Made into a bridge wire: one wire, no cover, taking wire 1 until told otherwise.
void makeBridgeWire(Brick& b);
// A BRANCH brick: joined to a cable's body, it takes wires from it and carries them on to its part - a run of its own.
void makeBranch(Brick& b);
// THE JOINTS TIED TO A RUN, and the run a joint is tied to.
QStringList jointsOf(const BrickDesign& d, const QString& specialId);
QString jointRun(const BrickDesign& d, const QString& jointId);
// How many splices a joint is (each of its parts; a step joint of one part, one per pair of wires), and how long
// each is - the part's length, 8 mm when it does not say.
int jointSpliceCount(const BrickDesign& d, const Brick& joint);
double jointSpliceLengthMm(const LibraryPart* part);
bool brickIsBranch(const Brick& b);

// ---- WHAT MOVES WITH WHAT ------------------------------------------------------------------------------
// A part and the special in line with it (on its row) are one line: moved up or down, the other goes too. A
// special not strung between two parts goes along with its part as well. `moving` are the bricks being moved.
struct BrickFollow {
    bool x = false;
    bool y = false;
};
QHash<QString, BrickFollow> bricksFollowing(const BrickDesign& d, const QStringList& moving);

struct BrickBuild;
// ---- MOVING A PART ON THE DRAWING ------------------------------------------------------------------------
// An end part dragged on the drawing by (dx, dy) mm, `built` being the drawing it was dragged on: the run that
// brings it from its other part is made as much longer or shorter as the move takes it away from that part or
// toward it - along, and across (a part moved down, away from the part its cable comes from, takes more cable) -
// and it is set that much up or down on the drawing. The bricks on the pane do not move. Returns the special
// whose length changed, or empty when the part hangs on no run from a part.
QString dragPartOnDrawing(BrickDesign& d, const BrickBuild& built, const QString& part, double dx, double dy,
                          bool ownLength = false);
// HOW LONG A RUN IS TO EACH OF ITS PARTS, and the longest: a branch with a length of its own, or the run's.
double runLengthTo(const BrickDesign& d, const Brick& special, const QString& part);
double runLongestMm(const BrickDesign& d, const Brick& special);
// Whether a part's branch shares its length with the others on that end of its run (and with which of them).
QStringList lengthLockedWith(const BrickDesign& d, const QString& part);
// How far apart the pane's rows are on the drawing - so a brick moved a row on the pane can be kept where it was
// on the drawing.
double brickRowPitchMm(const BrickDesign& d);
// SYMMETRIC ON THE DRAWING ONLY: every part set up or down on the drawing to where a symmetric layout puts it, the
// bricks on the pane left exactly where they are. `rowPitch` is how far apart the drawing's rows are.
void symmetricOnDrawing(BrickDesign& d, double rowPitch);
// EVERY DIMENSION TO ONE SIDE: each part's, each run's (whole length, fans, between) and each put on by hand - above
// the objects, or below them.
void setAllDimensionsBelow(BrickDesign& d, bool below);
// Whether every dimension there is goes below (none of them above).
bool allDimensionsBelow(const BrickDesign& d);

// One entry face of an end, once the design is laid out.
struct BrickFace {
    Vec2 centre{0.0, 0.0};
    Vec2 out{1.0, 0.0};        // square to the face, toward the harness
    double lengthMm = 20.0;
    Vec2 nearAt{0.0, 0.0};     // the part's near and far points, for dimensions
    Vec2 farAt{0.0, 0.0};
};

// WHAT A SPECIAL CAME TO where it was left on Auto: the figures the drawing worked out, so the
// window can show them.
struct BrickActual {
    double fanStart = 0.0;
    double fanFinish = 0.0;
    double knee = 0.0;
    double fanKnee = 0.0;
    double bodyWidth = 0.0;
    double trunk = 0.0;
    double transitionWidth = 0.0;   // the transition heatshrink, as drawn
    double transitionStretch = 0.0; // how far it is stretched over the end part, as drawn (or would be, asked)
    double transitionWidthFinish = 0.0;     // the finish's, where the two ends are not the same
    double transitionStretchFinish = 0.0;
    double transitionLenStart = 0.0;   // each transition's length, as the drawing makes it
    double transitionLenFinish = 0.0;
};

struct BrickBuild {
    std::vector<Entity> entities;           // every entity carries meta "brickId"
    QHash<QString, BrickActual> actual;     // by special id
    QHash<QString, BrickFace> faces;        // by "<end id>:<port>"
    QHash<QString, double> scales;          // what each end was drawn at, by id - Auto's figure included
    QStringList problems;                   // what could not be drawn, and why
    double rowPitch = 0.0;                  // how far apart the pane's rows were drawn
    // How long each branch after a cable divides is drawn, by "<special>|<part>" - for its heatshrink on the bill.
    QHash<QString, double> branchLengths;
    // How many transition heatshrinks each special has on it, and how many wire ends are covered at its fans.
    QHash<QString, int> transitionCount;
    QHash<QString, int> fanEndCount;
    // How long the transition heatshrinks on each special come to, all told - inside a backshell too.
    QHash<QString, double> transitionMm;
    // The layers over them, by "<special>|<part>": how many, and how long all told.
    QHash<QString, int> layerCount;
    QHash<QString, double> layerMm;
    // The transitions themselves by the part each end has, by "<special>|<part>": how many, how long.
    QHash<QString, int> transitionPartCount;
    QHash<QString, double> transitionPartMm;
    // Where each end part was laid out along the harness, by id.
    QHash<QString, double> partX;
};

// THE DRAWING: every end as its library part (or a placeholder of its kind) set down by its entry
// faces, every special as harness runs routed square from face to face, the fans of each face shared
// across it by wire count so that together they never exceed it, and the dimensions, labels, braid
// and transitions each run asks for.
// A balloon on an end is numbered by `itemOf` - the line of the bill its part is on, 0 for none ("?").
BrickBuild buildBrickHarness(const BrickDesign& design, const PartsCatalog* catalog,
                             const std::function<int(const QString& libPart)>& itemOf = nullptr);

} // namespace Edraw_Desktop
