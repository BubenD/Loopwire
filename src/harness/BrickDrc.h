#pragma once

#include "harness/BrickHarness.h"
#include "harness/PartsLibrary.h"
#include "model/Entity.h"

#include <QString>

#include <functional>
#include <vector>

namespace Loopwire {

// THE MARKS ON A BRICK: whether its part is on the bill (-1 no part to put on it, 0 not on it, 1 on it), and how
// well it suits what is linked to it (-1 not checked, 0 suits, 1 something unknown or not linked yet, 2 does not
// suit) - with why.
struct BrickMark {
    int bom = -1;
    int compat = -1;
    QString why;
};

// One thing a compatibility check found: its rule (stack, entry, gauge, ways, solid, contacts, joint, data, logic),
// how bad (1 unknown or unfinished, 2 does not suit), and what it says.
struct BrickFinding {
    QString rule;
    int level = 1;
    QString why;
};

// HOW WELL A BRICK SUITS WHAT IT IS LINKED TO: its run's layers over one another, each run's bundle into the entry it
// goes into, the wires against the contacts, ferrule or terminal they go into, a joint's bundle under its cover. The
// worst finding is the mark; with `all`, every finding too. `bomItemOf` says a part's item on the bill (0: not on it).
BrickMark brickCompatibility(const BrickDesign& d, const Brick& b, const PartsLibrary& merged,
                             const std::function<int(const QString&)>& bomItemOf,
                             std::vector<BrickFinding>* all = nullptr);

// THE HARNESS DRC: the logical rules (what is linked, named, chosen, long enough, on the bill) and the compatibility
// rules (what fits and suits) - each a rule that can be turned off, each finding a key that can be bypassed.
struct BrickDrcRule {
    QString id;
    QString label;
    QString group;   // "Logical" or "Compatibility"
    QString description;   // what it checks, and what to do about it
    int severity = 2;      // as it comes: 2 fail, 1 warning (the user may change it, or set it to pass)
};
const std::vector<BrickDrcRule>& brickDrcRules();

struct BrickIssue {
    QString key;       // stable across checks: harness, rule, brick, message
    QString rule;
    QString harness;   // the harness's name in the drawing
    QString brickId;
    QString message;
    int level = 1;     // 0 a note, 1 unfinished or unknown, 2 wrong
    // Where it is on the drawing, when that is known (a part found off its wires there).
    bool located = false;
    double atX = 0.0, atY = 0.0;
};
std::vector<BrickIssue> brickDrc(const BrickDesign& d, const QString& harness, const PartsLibrary& merged,
                                 const std::function<int(const QString&)>& bomItemOf);

// THE HARNESS AS PLACED ON THE DRAWING: every part the wizard put down touches a run's end, or a part that does - one
// moved off its wires on the sheet is found here, whatever its design says.
std::vector<BrickIssue> drawingConnectivity(const std::vector<Entity>& ents);

// ---- A SLEEVE DRAWN ON THE SHEET and the part it is ------------------------------------------------------------------
// Heatshrink, a cable, a braided sleeve or shield over the run, a transition: a double polyline told apart by its flags
// (shrunkEnds, shrinkBraid, shrinkShield, shrinkTransition, shrinkEndsOnly). Its name as a person says it; the kinds of
// part its body can be; and how far a part's size is from its width - 0 fits, more is how far out (mm), below 0 not known.
bool isDrawnSleeve(const Entity& e);
QString drawnSleeveName(const Entity& e);
std::vector<PartKind> drawnSleeveBodyKinds(const Entity& e);
double drawnSleeveFit(const Entity& e, const LibraryPart& p);
// EVERY DRAWN SLEEVE CHECKED against the parts linked to it - a balloon to its line on the bill, or its own part: one
// is linked, of a kind it can be, of a size its width is; heatshrink on its ends linked too; a bill line that names no
// library part (typed in) cannot be checked, and says so.
class Document;
std::vector<BrickIssue> drawingSleeveParts(const Document& doc, const PartsLibrary& lib);

} // namespace Loopwire
