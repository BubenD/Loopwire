#pragma once

#include "harness/Harness.h"
#include "model/Entity.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <vector>

namespace Edraw_Desktop {

// ---- Harness definition ----------------------------------------------------
// A complete, self-contained description of a wire harness: which connectors sit on
// which side and in what order, which pin joins which pin, how the wires are bundled
// (shields and twisted groups), and where it all goes on the sheet. Handing one of these
// to buildHarness() produces the whole schematic in one go — connectors, routed wires,
// shields, twist symbols and unconnected-pin markers — so a harness is described once and
// drawn automatically.
//
// Everything here is pure data + pure functions: no document, no UI. The dialog edits a
// HarnessDef, validateHarness() reports what is wrong with it, minimizeHarnessCrossings()
// re-orders pins to untangle it, and buildHarness() turns it into entities.

enum class HarnessSide { Left, Right };

struct HarnessConnector {
    QString id;         // stable key the wire list refers to
    HarnessSide side = HarnessSide::Left;
    QString designator; // refdes, e.g. "P1"
    QString partNumber;
    // The library part this connector IS, when it comes from the library (its id). What lets the
    // bill of materials order it by the manufacturer's number.
    QString libPart;
    QString annotation = QStringLiteral("numbers"); // "numbers" | "letters" | "mixed"
    int pinCount = 4;
    double pitch = 15.0;
    double pinLength = 40.0;
    // Placement order down its own side (0 = topmost). Several connectors on one side are
    // ordered by this, which is what the builder's drag-to-reorder block map sets.
    int order = 0;
    // Y-split leg this connector belongs to. Connectors sharing a leg are stacked
    // together and their wires run down that leg's own trunk; "" is the main trunk.
    QString leg;
    // An OVERALL shield for this connector: a screen around everything arriving at it, drawn
    // just inboard of its pins. Only the flag for its own side is meaningful — a left-hand
    // connector lives at the left end of the harness — so the other one is never offered.
    bool shieldLeft = false;
    bool shieldRight = false;
    // Bond that end's shield drain to the connector BODY: the drain leg is carried back and
    // joined to the body's corner joint, the way a screen is terminated onto a shell. Each
    // end has its own bond, so a group screened at both ends can be bonded at one or both.
    bool bodyBondLeft = false;
    bool bodyBondRight = false;
    // The pin numbers in the order they appear top-to-bottom. Empty = natural 1..pinCount.
    // Re-ordering this is how crossings are minimised without moving any connector.
    std::vector<int> pinOrder;
    // ITS CONTACTS: in the connector (as most are bought), or a part of their own - a library contact, or a P/N typed.
    bool contactsIncluded = true;
    QString contactPn;
    QString contactLibPart;
    // ITS OVERALL SHIELD'S DRAIN: where it goes - "" nowhere, "body" the connector's body, "pin:<connector id>:<pin>" a
    // pin - and how: "direct" (the screen's own drain) or "wire" (a hook-up wire spliced on: a library wire, or the
    // gauge and colour given here).
    QString drainTo;
    QString drainVia = QStringLiteral("direct");
    QString drainLibPart;
    QString drainGauge = QStringLiteral("0.5");
    QString drainColor = QStringLiteral("DRAIN");
};

struct HarnessWire {
    QString id;
    // Each end names either a CONNECTOR (its id, with the pin below) or a SHIELD - a drain wire
    // runs from a pin to the screen it drains. A shield end is written as a key rather than an
    // id, because a shield is not an object in the definition: an overall one belongs to a
    // connector and an end of the harness, an internal one is just a group name on the wires.
    // See harnessOverallShieldKey() / harnessInternalShieldKey(). The pin is ignored on a
    // shield end, and the wire is joined to the screen's drain leg with a splice.
    QString fromConn;
    int fromPin = 1;
    QString toConn;
    int toPin = 1;
    QString gauge = QStringLiteral("0.5");
    QString color = QStringLiteral("WH");
    // What this row is: a plain wire, or one conductor of a COAX. A coax is a cable of two
    // conductors given as TWO rows of one bundle - a Signal row and a Shield row - so a row on its
    // own is half a cable and the validator says so. The signal runs pin to pin; the shield row is
    // drawn as the braid: a screen symbol on the signal near each end with the shield's own
    // conductor brought from its pin into it.
    // "wire" | "coax" | "coax-splice" | "cable". A coax-splice is drawn exactly like a coax,
    // except that the cable reaches the harness through a splice at each end: past that splice
    // the conductors are separate wires, not the cable.
    QString kind = QStringLiteral("wire");
    // Rows sharing a non-empty bundle name are ONE cable (or one coax with several cores):
    // drawn together and shown on a background of their own in the builder, so a bundle can be
    // seen at a glance and re-formed by picking rows.
    QString bundle;
    // A coax's braid is terminated by the SHIELD row's own pins - that is what the row is for -
    // so there is nothing else to say about where the screen goes. (It used to be bonded to the
    // connector shell, and bonding it at both ends joined the two connector bodies through it;
    // with the braid landing on a pin, that shape cannot be built at all.)
    QString label;
    // What is printed on the wire in the drawing: its colour code, its cross-section, and its
    // label (above the run). Each is independent, so a wire can be marked with as much or as
    // little as the drawing needs.
    bool showColor = false;
    bool showGauge = false;
    bool showLabel = true;
    // Bundling. Wires carrying the same non-empty key are shielded together / twisted
    // together; a wire can be in both (a shielded twisted pair).
    //
    // shieldGroup is the INTERNAL shield: a screen around a named subset of the wires, drawn
    // inside the overall screen. The OVERALL shield — the one around everything arriving at a
    // connector — is asked for on the connector itself (shieldLeft / shieldRight), not here.
    QString shieldGroup;
    QString twistGroup;
    // The library part this conductor is made of - a discrete wire, a cable or a coax. What lets a
    // bill of materials order it by the manufacturer's number, in the unit it is bought in.
    QString libPart;
};

struct HarnessLayout {
    double leftX = 100.0;   // x of the left-hand connectors
    double leftY = 300.0;   // y of the first (topmost) connector on either side
    double rightX = 700.0;  // x of the right-hand connectors
    double connectorGap = 30.0; // clear space between connectors stacked on one side
    double legGap = 60.0;   // extra space between Y-split legs
    bool bridgeCrossings = true;    // draw a hop where two wires cross
    bool terminateUnconnected = false; // X-mark every pin no wire reaches
    // X-mark a WIRE end that lands on nothing - a wire drawn to a pin that does not exist, or
    // left dangling by an edit. A pin with no wire and a wire with no pin are different faults,
    // so they are marked separately.
    bool terminateUnconnectedWires = false;
    // Put a splice where two wires meet end to end, so a join always reads as a joint rather
    // than as one line crossing another.
    bool spliceAtJoins = true;
    // How big the joint dot is drawn, in world units.
    double spliceSize = 3.0;
    int twistCount = 2;   // crossings drawn per twisted group
    int twistAmount = 2;  // twist symbols placed per group
    // Which twist symbol the generated twists use: "crossed" (a Z across the wires), "jog"
    // (a braid of crossings) or "type3".
    QString twistType = QStringLiteral("jog");
    int shieldAmount = 1; // OVERALL shield symbols placed per connector that asks for one
    int wireShieldAmount = 2; // symbols placed per INTERNAL (wire) shield group
    double shieldLegLength = 20.0; // how far a generated shield's drain leg reaches
    // How far a FLYING LEAD (a wire with one open end) reaches out of its pin. 0 = worked out, far
    // enough to lie inside the screens of the cable it belongs to. How long a flying lead is left is
    // the loom maker's decision, so it can be said outright.
    double leadLength = 0.0;
    // Outline of a generated screen: "ellipse" or "capsule" (a vertical stadium).
    QString shieldType = QStringLiteral("ellipse");
    // The pin pitch every connector is drawn at. Editing it in the builder writes through to
    // all of them, so a whole harness can be re-scaled from one place; a connector that needs
    // a different pitch can still be given one in its own row afterwards.
    double pinPitch = 15.0;
    // How the generated wires are coloured: "actual" gives each wire its own colour, "bylayer"
    // leaves them all on the Wires layer colour (a single-colour drawing). Either way the
    // colour is written as the wire's ordinary colour property, which is what is drawn - the
    // harness colour CODE stays on the wire as information.
    QString wireColorMode = QStringLiteral("bylayer");
    // How far the colour/gauge marking and the wire label sit above the run, and how tall that
    // text is drawn.
    double textGap = 0.0;
    double textHeight = 3.5;
    // Whether a colour reads as its two-letter code ("short", RD) or spelled out ("long", Red).
    // A colour reads by its full name by default, matching what a wire's own properties default
    // to: "White" reads on a sheet where "WH" has to be looked up.
    QString colorNameMode = QStringLiteral("long");
    // A generated drain - the conductor that carries a screen to the connector shell - is a
    // wire like any other: drawn as one, counted as one, editable as one. It is given the
    // "DRAIN" colour rather than a setting of its own, which is what that entry in the wire
    // colour list is for.
};

struct HarnessDef {
    std::vector<HarnessConnector> connectors;
    std::vector<HarnessWire> wires;
    HarnessLayout layout;
};

// THE DEFINITION AS A FILE HAS IT, and back: every field of every connector, wire and the layout - what a builder
// file, a preset and the builder's own undo keep. A field a file does not have takes its default.
QJsonObject harnessDefToJson(const HarnessDef& def);
HarnessDef harnessDefFromJson(const QJsonObject& root);

// THE DRAINS AS THE CONNECTORS SAY: each connector's overall-shield drain made what its drainTo asks - bonded to its body,
// or a wire (id "drain-<connector id>") from its screen to the pin named - and none where it asks for nothing. A
// connector without an overall shield has no drain. Run after a connector's shield or drain is changed.
void harnessSyncDrains(HarnessDef& def);
// Whether a wire row is a connector's drain, made by harnessSyncDrains rather than by hand.
bool harnessIsDrainRow(const HarnessWire& w);

const HarnessConnector* harnessConnById(const HarnessDef& def, const QString& id);

// Which coax of its bundle this wire is, 1-based, in the order the wires are listed. One cable
// can hold several coaxes and their conductors have to be told apart on the drawing, so the
// signal and screen of the second coax in CABLE1 are marked SIGNAL2 / SHIELD2. Returns 0 when
// the wire is not a coax, or is the only one in its bundle (then a plain SIGNAL / SHIELD says
// all there is to say).
// Both coax kinds are coax everywhere except at the transition itself.
bool harnessIsCoaxKind(const QString& kind);
// True when the rows of `bundle` describe a coax that reaches the harness through a splice.
bool harnessCoaxSplices(const HarnessDef& def, const QString& bundle);

int harnessCoaxIndex(const HarnessDef& def, const HarnessWire& wire);

// ---- Shield endpoints ------------------------------------------------------
// A wire end can land on a screen instead of a pin: that is what a drain wire is. These build
// and read the keys that name one. They are deliberately prefixed so they can never collide
// with a connector id.
// An OPEN end: the wire runs out and stops, connected to nothing. A harness has flying leads,
// and one has to be drawable without inventing a connector for it.
QString harnessOpenEndpointKey();
bool harnessIsOpenEndpoint(const QString& key);

// Whether an END of a wire lands on nothing. Either it names no connector (#OPEN), or it names one but
// has no pin: pin 0 is "not landed", which is what a row shows while it is being filled in and what a
// conductor gets when the connector it was heading for has no free pin left. Neither is a fault to
// report, and neither is drawn as a pin-to-pin run.
bool harnessEndIsOpen(const QString& conn, int pin);

QString harnessOverallShieldKey(const QString& connId, HarnessSide side);
QString harnessInternalShieldKey(const QString& group);
bool harnessIsShieldEndpoint(const QString& key);
// The internal group a key names, or "" when it names an overall shield.
QString harnessShieldEndpointGroup(const QString& key);
// The connector and side an overall-shield key names. False when the key is an internal one.
bool harnessShieldEndpointConn(const QString& key, QString& connId, HarnessSide& side);
// How the endpoint reads on screen: "P1 shield (left)", "Shield S1 (internal)", or the key
// itself if it names something that is no longer in the harness.
QString harnessShieldEndpointLabel(const HarnessDef& def, const QString& key);
// Every shield endpoint the harness actually has: the overall screens its connectors ask for,
// and the internal groups its wires name. In a stable order, for a drop-down.
QStringList harnessShieldEndpoints(const HarnessDef& def);

// The pins of `c` top-to-bottom: its pinOrder when set, otherwise 1..pinCount. Always
// exactly pinCount entries, so the layout can never fall out of step with the pins.
std::vector<int> harnessPinSequence(const HarnessConnector& c);
// The label shown for a pin number, following the connector's numbers/letters setting.
QString harnessPinLabel(const HarnessConnector& c, int pinNumber);

// ---- Connection table -------------------------------------------------------
// A wiring list built straight from the description: one row per wire with both ends on a
// real pin (an open end or a drain wire has no "from"/"to" pin to print, so it is left out),
// and the shield/twist membership as SPANS over those rows rather than a per-row column - a
// shield can nest (an overall screen around everything at a connector, and inside it a named
// subset with a screen - and a twist - of its own), which a single column cannot say.
struct ConnectionTableRow {
    QString wireId, from, to, color, gauge;
};
// One bracket (a shield) or one merged run (a twist) over a CONTIGUOUS range of rows.
struct ConnectionTableSpan {
    int firstRow = 0, lastRow = 0;  // inclusive, 0-based into ConnectionTable::rows
    int level = 0;                  // shield spans only: 0 = overall, 1 = internal (nested inside it)
    QString label;                  // "SHIELD" for an overall span; the group's own name otherwise
    int memberCount = 0;            // twist spans only: how many wires share this run
};
struct ConnectionTable {
    std::vector<ConnectionTableRow> rows;
    std::vector<ConnectionTableSpan> shieldSpans;
    std::vector<ConnectionTableSpan> twistSpans;
};
// Rows ordered so a shield/twist group's members land next to each other (a connector pair's
// wires are kept together, then grouped by shieldGroup, then by twistGroup), and the spans
// that grouping finds - one per MAXIMAL RUN of rows sharing a membership, so a connector that
// talks to more than one other connector correctly gets more than one overall-shield bracket
// rather than one bracket wrongly spanning rows that are not really its own.
ConnectionTable harnessConnectionTable(const HarnessDef& def);
// The table entity (columns: #, From, To, Color, Gauge, Twist - shield is drawn as brackets
// beside the table, not a column) plus a Line+Text pair per shieldSpans bracket, all built in
// one local frame so placing the result keeps the brackets together with the table.
std::vector<Entity> harnessConnectionTableEntities(const HarnessDef& def);

// ---- Validation ------------------------------------------------------------
// One problem found in a definition. `key` is stable so the UI can track/ignore it.
struct HarnessIssue {
    QString key;
    QString message;
    QString connId; // the connector it is about ("" if not connector-specific)
    QString wireId; // the wire it is about ("" if not wire-specific)
};

// Check a definition before it is drawn:
//   - no pin NAME repeated within one connector
//   - every shielded group's wires are neighbours on each connector (nothing unshielded
//     in between), since a shield can only enclose a contiguous band of wires
//   - every twisted group's wires are neighbours too, for the same reason
//   - plus the basics: wires pointing at missing connectors or out-of-range pins, two
//     wires on one pin, and connectors sharing a designator
std::vector<HarnessIssue> validateHarness(const HarnessDef& def);

// ---- Pinout optimisation ---------------------------------------------------
// How many wire pairs cross, judged from the pin rows each wire leaves and lands on.
int harnessCrossings(const HarnessDef& def);
// How badly the bundles are split: for every shielded / twisted group, the number of
// FOREIGN wires sitting between its wires on a connector. Zero means every group occupies
// a solid band of neighbouring pins, which is the only way a shield can enclose it or a
// twist can braid it. `shield` picks which kind of group is counted.
int harnessBundleBreaks(const HarnessDef& def, bool shield);

// How spread out the wired pins are: for each connector, the number of SPARE pins sitting
// between its first and last wired pin. Zero means every wire is on a neighbouring pin, so the
// bundle leaves the connector as one tight group with the least distance between its wires -
// which is what "minimum wire pitch" asks for. It says nothing about WHERE the group sits;
// harnessPinOrderPenalty() is what pulls it to the top.
int harnessWireSpanPenalty(const HarnessDef& def);

// How far a connector's pinout is from "everything that is wired first, the spare pins
// after": the number of times an UNWIRED pin sits above a wired one. Zero means the
// populated pins occupy a solid block at the top and the spares follow, which is how a
// pinout is read off a drawing.
int harnessPinOrderPenalty(const HarnessDef& def);

// What the optimiser is trying to make small. Bundling matters far more than a crossing —
// a split shield cannot be drawn at all, whereas a crossing merely looks busy — so the
// bundle terms carry much heavier weights by default. Keeping the populated pins together
// at the top sits between the two: worth more than a crossing, not worth breaking a shield.
struct HarnessOptimizeWeights {
    int crossing = 1;
    int shieldBreak = 200;
    int twistBreak = 200;
    int pinOrder = 20;  // populated pins first, i.e. the group starts at the TOP
    int wireSpan = 0;   // the wires sit on neighbouring pins, wherever the group starts
};
int harnessLayoutCost(const HarnessDef& def, const HarnessOptimizeWeights& w);

// Re-order each connector's PINS (never the connectors, never the wire list) to make that
// cost as small as it can. Returns the cost it settled on. Every button below is this one
// engine with different weights, so they all keep the wiring intact and only move pins.
int optimizeHarnessPinout(HarnessDef& def, const HarnessOptimizeWeights& w);
// Fewest crossings, while still pulling twisted pairs and shielded wires together — a
// crossing-free layout that splits a shield is not actually usable.
int minimizeHarnessCrossings(HarnessDef& def);
// Group the shielded wires onto neighbouring pins above all else.
int optimizeHarnessForShields(HarnessDef& def);
// Group the twisted wires onto neighbouring pins above all else.
int optimizeHarnessForTwists(HarnessDef& def);

// ---- Knee lanes ------------------------------------------------------------
// The order a set of wires should turn in, so their vertical runs cut across as few of the
// other wires' horizontal runs as possible. Each wire is given as the row it LEAVES and the
// row it LANDS ON (any consistent vertical axis); the result is one lane index per wire,
// 0 turning first. Lanes are all distinct, so no two wires share a turn line, and the drawing
// places them a fixed pitch apart from one common start.
//
// Turning wire i before wire j costs a crossing for each of:
//   - j's outbound run (at j's source row) passing i's turn line, and
//   - i's inbound run (at i's destination row) passing j's turn line.
// Some pairs cost one whichever way round they go — a wire spanning both of another's rows
// separates the rails and cannot be avoided — so this minimises the total rather than
// promising zero. Both the generator and the builder's preview use it, so the preview shows
// the routing that will actually be drawn.
std::vector<int> harnessKneeLanes(const std::vector<std::pair<double, double>>& legs);

// ---- Generation ------------------------------------------------------------
struct HarnessBuild {
    std::vector<Entity> entities; // connectors, wires, shields, twists, X markers
    int crossings = 0;            // wire crossings in the drawn result
    std::vector<HarnessIssue> issues; // whatever validateHarness found
};

// Draw the harness: every connector placed on its side in its order (grouped by leg),
// every wire routed pin-to-pin, a shield per shielded group beside its chosen connector,
// a twist symbol per twisted group, and X markers on unconnected pins when asked for.
// Ids are left empty — the caller stamps them as it adds the entities.
HarnessBuild buildHarness(const HarnessDef& def);

} // namespace Edraw_Desktop
