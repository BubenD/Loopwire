#pragma once

#include "harness/BrickHarness.h"
#include "harness/PartsLibrary.h"

#include <QString>

#include <functional>
#include <vector>

namespace Edraw_Desktop {

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
};
const std::vector<BrickDrcRule>& brickDrcRules();

struct BrickIssue {
    QString key;       // stable across checks: harness, rule, brick, message
    QString rule;
    QString harness;   // the harness's name in the drawing
    QString brickId;
    QString message;
    int level = 1;     // 0 a note, 1 unfinished or unknown, 2 wrong
};
std::vector<BrickIssue> brickDrc(const BrickDesign& d, const QString& harness, const PartsLibrary& merged,
                                 const std::function<int(const QString&)>& bomItemOf);

} // namespace Edraw_Desktop
