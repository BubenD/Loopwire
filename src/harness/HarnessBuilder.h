#pragma once

#include "harness/Harness.h"
#include "model/Entity.h"

#include <QString>
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
    QString annotation = QStringLiteral("numbers"); // "numbers" | "letters"
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
};

struct HarnessWire {
    QString id;
    QString fromConn;
    int fromPin = 1;
    QString toConn;
    int toPin = 1;
    QString gauge = QStringLiteral("0.5");
    QString color = QStringLiteral("WH");
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
};

struct HarnessLayout {
    double leftX = 100.0;   // x of the left-hand connectors
    double leftY = 300.0;   // y of the first (topmost) connector on either side
    double rightX = 700.0;  // x of the right-hand connectors
    double connectorGap = 30.0; // clear space between connectors stacked on one side
    double legGap = 60.0;   // extra space between Y-split legs
    bool bridgeCrossings = true;    // draw a hop where two wires cross
    bool terminateUnconnected = false; // X-mark every pin no wire reaches
    int twistCount = 2;   // crossings drawn per twisted group
    int twistAmount = 2;  // twist symbols placed per group
    // Which twist symbol the generated twists use: "crossed" (a Z across the wires), "jog"
    // (a braid of crossings) or "type3".
    QString twistType = QStringLiteral("jog");
    int shieldAmount = 2; // OVERALL shield symbols placed per connector that asks for one
    int wireShieldAmount = 2; // symbols placed per INTERNAL (wire) shield group
    double shieldLegLength = 20.0; // how far a generated shield's drain leg reaches
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
    QString wireColorMode = QStringLiteral("actual");
};

struct HarnessDef {
    std::vector<HarnessConnector> connectors;
    std::vector<HarnessWire> wires;
    HarnessLayout layout;
};

const HarnessConnector* harnessConnById(const HarnessDef& def, const QString& id);

// The pins of `c` top-to-bottom: its pinOrder when set, otherwise 1..pinCount. Always
// exactly pinCount entries, so the layout can never fall out of step with the pins.
std::vector<int> harnessPinSequence(const HarnessConnector& c);
// The label shown for a pin number, following the connector's numbers/letters setting.
QString harnessPinLabel(const HarnessConnector& c, int pinNumber);

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
    int pinOrder = 20;
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
