#include "harness/HarnessBuilder.h"

#include "geometry/Geometry.h"

#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace Edraw_Desktop {

namespace {

// Where a connector ends up, and where each of its pins ends up, once the sides and legs
// have been laid out. Everything downstream (wire routing, shields, twists, X markers)
// works off this, so the drawing and the geometry can never disagree.
struct Placed {
    const HarnessConnector* conn = nullptr;
    Vec2 origin;                  // the connector entity's anchor
    ConnectorSpec spec;
    ConnectorGeometry geom;
    std::vector<int> pinSeq;      // pins top-to-bottom
    double trunkX = 0.0;          // x of the vertical run this connector's wires use
};

ConnectorSpec specOf(const HarnessConnector& c) {
    ConnectorSpec s = defaultConnectorSpec(std::max(1, c.pinCount));
    s.designator = c.designator;
    s.partNumber = c.partNumber;
    s.pinCount = std::max(1, c.pinCount);
    s.pitch = std::max(1.0, c.pitch);
    s.pinLength = std::max(1.0, c.pinLength);
    s.annotation = c.annotation;
    // Right-hand connectors face the other way, so their pins exit toward the harness.
    s.mirrorX = (c.side == HarnessSide::Right);
    s.pins.clear();
    // The label at each ROW is the label of the pin that sits at that row — which is what
    // the pin order decides. Writing 1..n straight down would show a natural pinout no
    // matter how the pins were re-ordered, so a re-order would move the wires while the
    // connector still read 1,2,3… and the optimiser would look like it had done nothing.
    for (int pin : harnessPinSequence(c)) s.pins << harnessPinLabel(c, pin);
    return s;
}

// The row a pin occupies, counted from the top of its connector (0-based). This is what
// decides whether two wires cross, and re-ordering it is what untangles them.
int pinRow(const HarnessConnector& c, int pinNumber) {
    const std::vector<int> seq = harnessPinSequence(c);
    for (int i = 0; i < static_cast<int>(seq.size()); ++i)
        if (seq[i] == pinNumber) return i;
    return 0;
}

// Connectors on one side, in placement order (their `order`, then their designator so the
// result is stable when two share an order).
std::vector<const HarnessConnector*> sideConnectors(const HarnessDef& def, HarnessSide side) {
    std::vector<const HarnessConnector*> out;
    for (const auto& c : def.connectors)
        if (c.side == side) out.push_back(&c);
    std::sort(out.begin(), out.end(),
              [](const HarnessConnector* a, const HarnessConnector* b) {
                  if (a->order != b->order) return a->order < b->order;
                  return a->designator < b->designator;
              });
    return out;
}

// The legs present on a side, in the order their connectors first appear. A harness with a
// Y-split names a leg per branch; everything unnamed shares the main trunk.
std::vector<QString> legOrder(const std::vector<const HarnessConnector*>& conns) {
    std::vector<QString> legs;
    for (const HarnessConnector* c : conns)
        if (std::find(legs.begin(), legs.end(), c->leg) == legs.end()) legs.push_back(c->leg);
    return legs;
}

double bodyHeight(const ConnectorSpec& s) {
    return connectorGeometry({0.0, 0.0}, s).effectiveHeight;
}

// Lay every connector out: each side stacked top-down in placement order, grouped by leg
// (legs separated by legGap), and each leg given its own vertical trunk so the branches of
// a Y-split run separately instead of sharing one bundle.
std::vector<Placed> placeConnectors(const HarnessDef& def) {
    std::vector<Placed> out;
    const HarnessLayout& L = def.layout;
    for (const HarnessSide side : {HarnessSide::Left, HarnessSide::Right}) {
        const auto conns = sideConnectors(def, side);
        if (conns.empty()) continue;
        const std::vector<QString> legs = legOrder(conns);
        const double x = (side == HarnessSide::Left) ? L.leftX : L.rightX;
        double y = L.leftY;
        int legIndex = 0;
        for (const QString& leg : legs) {
            // Each leg's wires travel down their own trunk, stepped in from the connectors
            // so several legs can run side by side without sharing a lane.
            const double inward = (side == HarnessSide::Left) ? 1.0 : -1.0;
            const double trunk = x + inward * (60.0 + 40.0 * legIndex);
            for (const HarnessConnector* c : conns) {
                if (c->leg != leg) continue;
                Placed p;
                p.conn = c;
                p.spec = specOf(*c);
                p.origin = {x, y};
                p.geom = connectorGeometry(p.origin, p.spec);
                p.pinSeq = harnessPinSequence(*c);
                p.trunkX = trunk;
                out.push_back(p);
                y -= bodyHeight(p.spec) + std::max(0.0, L.connectorGap);
            }
            y -= std::max(0.0, L.legGap); // clear space before the next leg
            ++legIndex;
        }
    }
    return out;
}

const Placed* placedById(const std::vector<Placed>& placed, const QString& id) {
    for (const Placed& p : placed)
        if (p.conn->id == id) return &p;
    return nullptr;
}

// The world point of a pin's stub end — where a wire attaches.
bool pinEndOf(const Placed& p, int pinNumber, Vec2& out) {
    // pinEnd is indexed by ROW (top-down), while the wire names a pin NUMBER; the
    // connector's pin order maps between them.
    const int row = pinRow(*p.conn, pinNumber);
    if (row < 0 || row >= static_cast<int>(p.geom.pinEnd.size())) return false;
    out = p.geom.pinEnd[row];
    return true;
}

// Wires in the order they will be drawn, which is also the order they are bundled in:
// sorted by the row they leave their FROM connector, so a shield or twist covers a
// contiguous band of neighbours.
std::vector<const HarnessWire*> orderedWires(const HarnessDef& def) {
    std::vector<const HarnessWire*> out;
    for (const auto& w : def.wires) out.push_back(&w);
    std::sort(out.begin(), out.end(), [&def](const HarnessWire* a, const HarnessWire* b) {
        const HarnessConnector* ca = harnessConnById(def, a->fromConn);
        const HarnessConnector* cb = harnessConnById(def, b->fromConn);
        if (ca && cb && ca->id != cb->id) return ca->order < cb->order;
        if (ca && cb) return pinRow(*ca, a->fromPin) < pinRow(*cb, b->fromPin);
        return a->id < b->id;
    });
    return out;
}

// The rows a wire spans, used both for crossing counts and for the "are these wires
// neighbours" checks that shields and twists depend on.
struct WireRows {
    int fromRow = 0;
    int toRow = 0;
    bool valid = false;
};
WireRows rowsOf(const HarnessDef& def, const HarnessWire& w) {
    WireRows r;
    const HarnessConnector* a = harnessConnById(def, w.fromConn);
    const HarnessConnector* b = harnessConnById(def, w.toConn);
    if (!a || !b) return r;
    r.fromRow = pinRow(*a, w.fromPin);
    r.toRow = pinRow(*b, w.toPin);
    r.valid = true;
    return r;
}

// Are the wires of `group` (by the given key) neighbours on every connector they touch?
// A shield encloses a band and a twist braids adjacent wires, so a foreign wire sitting
// between them cannot be drawn — which is what checks (b) and (c) are about.
bool groupIsContiguous(const HarnessDef& def, const QString& key, bool shield,
                       QString& whereOut) {
    // Collect, per connector, the rows this group occupies and the rows anything else does.
    struct Rows { std::vector<int> mine, others; };
    QHash<QString, Rows> byConn;
    for (const auto& w : def.wires) {
        const QString wk = shield ? w.shieldGroup : w.twistGroup;
        const bool mine = (wk == key);
        const HarnessConnector* a = harnessConnById(def, w.fromConn);
        const HarnessConnector* b = harnessConnById(def, w.toConn);
        if (a) (mine ? byConn[a->id].mine : byConn[a->id].others).push_back(pinRow(*a, w.fromPin));
        if (b) (mine ? byConn[b->id].mine : byConn[b->id].others).push_back(pinRow(*b, w.toPin));
    }
    for (auto it = byConn.begin(); it != byConn.end(); ++it) {
        std::vector<int> mine = it.value().mine;
        if (mine.size() < 2) continue; // one wire on this connector is trivially contiguous
        std::sort(mine.begin(), mine.end());
        mine.erase(std::unique(mine.begin(), mine.end()), mine.end());
        const int lo = mine.front(), hi = mine.back();
        // Any row strictly inside the band that is NOT ours breaks the run.
        for (int row : it.value().others)
            if (row > lo && row < hi) {
                whereOut = it.key();
                return false;
            }
        // Gaps with nothing in them are fine (a free pin between two shielded wires still
        // lets the shield close), so only foreign wires count as a break.
    }
    return true;
}

} // namespace

const HarnessConnector* harnessConnById(const HarnessDef& def, const QString& id) {
    for (const auto& c : def.connectors)
        if (c.id == id) return &c;
    return nullptr;
}

std::vector<int> harnessPinSequence(const HarnessConnector& c) {
    const int n = std::max(1, c.pinCount);
    std::vector<int> out;
    QSet<int> seen;
    // Take the explicit order first, ignoring anything out of range or repeated...
    for (int p : c.pinOrder) {
        if (p < 1 || p > n || seen.contains(p)) continue;
        out.push_back(p);
        seen.insert(p);
    }
    // ...then fill in whatever it left out, so the sequence always covers every pin exactly
    // once however partial (or stale) the stored order is.
    for (int p = 1; p <= n; ++p)
        if (!seen.contains(p)) out.push_back(p);
    return out;
}

QString harnessPinLabel(const HarnessConnector& c, int pinNumber) {
    ConnectorSpec s;
    s.annotation = c.annotation;
    s.pinOrder = QStringLiteral("top");
    s.pinCount = std::max(1, c.pinCount);
    return connectorPinLabel(pinNumber - 1, s);
}

std::vector<HarnessIssue> validateHarness(const HarnessDef& def) {
    std::vector<HarnessIssue> out;
    auto add = [&out](const QString& key, const QString& msg, const QString& conn = QString(),
                      const QString& wire = QString()) {
        out.push_back({key, msg, conn, wire});
    };

    // (a) No pin NAME repeated inside one connector. Duplicates come from a hand-edited
    // label set or a pin order that names the same pin twice.
    for (const auto& c : def.connectors) {
        QSet<QString> names;
        for (int p = 1; p <= std::max(1, c.pinCount); ++p) {
            const QString label = harnessPinLabel(c, p);
            if (names.contains(label))
                add(QStringLiteral("duppin:%1:%2").arg(c.id, label),
                    QStringLiteral("%1: pin name \"%2\" is used more than once")
                        .arg(c.designator.isEmpty() ? c.id : c.designator, label),
                    c.id);
            names.insert(label);
        }
        QSet<int> ordered;
        for (int p : c.pinOrder) {
            if (ordered.contains(p))
                add(QStringLiteral("duporder:%1:%2").arg(c.id).arg(p),
                    QStringLiteral("%1: pin %2 appears twice in the pin order")
                        .arg(c.designator.isEmpty() ? c.id : c.designator).arg(p),
                    c.id);
            ordered.insert(p);
        }
    }
    // Designators identify a connector on the drawing, so two the same is a real fault.
    {
        QSet<QString> des;
        for (const auto& c : def.connectors) {
            if (c.designator.isEmpty()) continue;
            if (des.contains(c.designator))
                add(QStringLiteral("dupdes:%1").arg(c.designator),
                    QStringLiteral("Designator \"%1\" is used by more than one connector")
                        .arg(c.designator),
                    c.id);
            des.insert(c.designator);
        }
    }

    // Wires must point at connectors that exist, at pins that exist, and no pin may carry
    // two wires — otherwise the generated drawing cannot be built at all.
    QHash<QString, QString> pinTaken; // "conn/pin" -> wire id
    for (const auto& w : def.wires) {
        const HarnessConnector* a = harnessConnById(def, w.fromConn);
        const HarnessConnector* b = harnessConnById(def, w.toConn);
        if (!a || !b) {
            add(QStringLiteral("badconn:%1").arg(w.id),
                QStringLiteral("A wire refers to a connector that is not in the harness"),
                QString(), w.id);
            continue;
        }
        auto checkPin = [&](const HarnessConnector& c, int pin) {
            if (pin < 1 || pin > std::max(1, c.pinCount))
                add(QStringLiteral("badpin:%1:%2").arg(w.id).arg(pin),
                    QStringLiteral("%1 has no pin %2")
                        .arg(c.designator.isEmpty() ? c.id : c.designator).arg(pin),
                    c.id, w.id);
        };
        checkPin(*a, w.fromPin);
        checkPin(*b, w.toPin);
        for (const auto& side : {std::make_pair(a, w.fromPin), std::make_pair(b, w.toPin)}) {
            const QString key = QStringLiteral("%1/%2").arg(side.first->id).arg(side.second);
            if (pinTaken.contains(key))
                add(QStringLiteral("pinbusy:%1").arg(key),
                    QStringLiteral("%1 pin %2 has more than one wire on it")
                        .arg(side.first->designator.isEmpty() ? side.first->id
                                                             : side.first->designator)
                        .arg(harnessPinLabel(*side.first, side.second)),
                    side.first->id, w.id);
            else
                pinTaken.insert(key, w.id);
        }
    }

    // (b) + (c) Shielded / twisted groups must be neighbours: a shield encloses a band of
    // wires and a twist braids adjacent ones, so an unshielded (or untwisted) wire in the
    // middle cannot be drawn.
    QSet<QString> shieldKeys, twistKeys;
    for (const auto& w : def.wires) {
        if (!w.shieldGroup.isEmpty()) shieldKeys.insert(w.shieldGroup);
        if (!w.twistGroup.isEmpty()) twistKeys.insert(w.twistGroup);
    }
    for (const QString& k : shieldKeys) {
        QString where;
        if (!groupIsContiguous(def, k, /*shield=*/true, where)) {
            const HarnessConnector* c = harnessConnById(def, where);
            add(QStringLiteral("shieldsplit:%1").arg(k),
                QStringLiteral("Shield \"%1\" is broken at %2: an unshielded wire runs "
                               "between its wires")
                    .arg(k, c && !c->designator.isEmpty() ? c->designator : where),
                where);
        }
    }
    for (const QString& k : twistKeys) {
        QString where;
        if (!groupIsContiguous(def, k, /*shield=*/false, where)) {
            const HarnessConnector* c = harnessConnById(def, where);
            add(QStringLiteral("twistsplit:%1").arg(k),
                QStringLiteral("Twisted group \"%1\" is broken at %2: another wire runs "
                               "between its wires")
                    .arg(k, c && !c->designator.isEmpty() ? c->designator : where),
                where);
        }
    }
    // A connector asking for an OVERALL shield must actually have wires arriving at it —
    // there is nothing for the screen to wrap otherwise, so the request would draw nothing.
    for (const auto& c : def.connectors) {
        const bool wantsOverall = (c.side == HarnessSide::Left) ? c.shieldLeft : c.shieldRight;
        const bool wantsBond = (c.side == HarnessSide::Left) ? c.bodyBondLeft : c.bodyBondRight;
        if (!wantsOverall && !wantsBond) continue;
        bool anyWire = false;
        for (const auto& w : def.wires)
            if (w.fromConn == c.id || w.toConn == c.id) { anyWire = true; break; }
        if (!anyWire)
            add(QStringLiteral("shieldnowire:%1").arg(c.id),
                QStringLiteral("%1 asks for an overall shield but no wire reaches it")
                    .arg(c.designator.isEmpty() ? c.id : c.designator),
                c.id);
        // A bond with no screen to bond to is a request that cannot be drawn.
        if (wantsBond && !wantsOverall)
            add(QStringLiteral("bondnoshield:%1").arg(c.id),
                QStringLiteral("%1 asks to bond its shield to the body but has no overall "
                               "shield — tick its overall shield too")
                    .arg(c.designator.isEmpty() ? c.id : c.designator),
                c.id);
    }

    // A twisted group of one has nothing to twist with.
    for (const QString& k : twistKeys) {
        int n = 0;
        for (const auto& w : def.wires)
            if (w.twistGroup == k) ++n;
        if (n < 2)
            add(QStringLiteral("twistalone:%1").arg(k),
                QStringLiteral("Twisted group \"%1\" has only one wire in it").arg(k));
    }
    return out;
}

int harnessCrossings(const HarnessDef& def) {
    // Every wire runs through the same middle corridor, so a crossing is the classic
    // two-rail inversion: order the wires by where they leave the LEFT rail and by where
    // they reach the RIGHT rail; each pair whose orders disagree crosses.
    //
    // The rail position has to be measured across the WHOLE side, not within one connector:
    // a wire from P1 down to P2 and one from P1 up to P3 cross each other even though they
    // end on different connectors. Comparing rows per connector-pair (as this did) simply
    // could not see those crossings, so the optimiser had nothing to fix.
    auto railPos = [&def](const QString& connId, int pin, bool& ok) {
        ok = false;
        const HarnessConnector* c = harnessConnById(def, connId);
        if (!c) return 0.0;
        // Stack the connectors of this side in placement order and count pins downward, so
        // the result is one monotone vertical coordinate for the whole rail.
        double base = 0.0;
        for (const HarnessConnector* other : sideConnectors(def, c->side)) {
            if (other->id == c->id) break;
            base += std::max(1, other->pinCount) + 1.0; // +1 = the gap between connectors
        }
        ok = true;
        return base + pinRow(*c, pin);
    };
    struct Span { double left = 0.0, right = 0.0; bool valid = false; };
    std::vector<Span> spans;
    for (const auto& w : def.wires) {
        const HarnessConnector* a = harnessConnById(def, w.fromConn);
        const HarnessConnector* b = harnessConnById(def, w.toConn);
        if (!a || !b) continue;
        // Whichever end is on the left rail is the "left" of the span; a wire between two
        // connectors on the same side has no corridor to cross in, so it is skipped.
        const bool aLeft = (a->side == HarnessSide::Left);
        const bool bLeft = (b->side == HarnessSide::Left);
        if (aLeft == bLeft) continue;
        bool ok1 = false, ok2 = false;
        Span s;
        s.left = aLeft ? railPos(w.fromConn, w.fromPin, ok1) : railPos(w.toConn, w.toPin, ok1);
        s.right = aLeft ? railPos(w.toConn, w.toPin, ok2) : railPos(w.fromConn, w.fromPin, ok2);
        s.valid = ok1 && ok2;
        if (s.valid) spans.push_back(s);
    }
    int crossings = 0;
    for (size_t i = 0; i < spans.size(); ++i)
        for (size_t j = i + 1; j < spans.size(); ++j) {
            const double dl = spans[i].left - spans[j].left;
            const double dr = spans[i].right - spans[j].right;
            if (std::abs(dl) > 1e-9 && std::abs(dr) > 1e-9 && (dl > 0.0) != (dr > 0.0))
                ++crossings;
        }
    return crossings;
}

int harnessBundleBreaks(const HarnessDef& def, bool shield) {
    // Per connector, per group: how many wires that do NOT belong to the group sit between
    // the group's topmost and bottommost pin row. That count is what has to reach zero for a
    // shield to enclose the band, or a twist to braid it — and because it is a COUNT rather
    // than a yes/no it gives the optimiser something to descend.
    std::vector<QString> keys;
    for (const auto& w : def.wires) {
        const QString k = shield ? w.shieldGroup : w.twistGroup;
        if (k.isEmpty()) continue;
        if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
    }
    int breaks = 0;
    for (const QString& key : keys) {
        struct Rows { std::vector<int> mine, others; };
        QHash<QString, Rows> byConn;
        for (const auto& w : def.wires) {
            const bool mine = ((shield ? w.shieldGroup : w.twistGroup) == key);
            const HarnessConnector* a = harnessConnById(def, w.fromConn);
            const HarnessConnector* b = harnessConnById(def, w.toConn);
            if (a) (mine ? byConn[a->id].mine : byConn[a->id].others).push_back(pinRow(*a, w.fromPin));
            if (b) (mine ? byConn[b->id].mine : byConn[b->id].others).push_back(pinRow(*b, w.toPin));
        }
        for (auto it = byConn.begin(); it != byConn.end(); ++it) {
            std::vector<int> mine = it.value().mine;
            if (mine.size() < 2) continue;
            std::sort(mine.begin(), mine.end());
            const int lo = mine.front(), hi = mine.back();
            for (int row : it.value().others)
                if (row > lo && row < hi) ++breaks;
        }
    }
    return breaks;
}

int harnessPinOrderPenalty(const HarnessDef& def) {
    int penalty = 0;
    for (const auto& c : def.connectors) {
        // Which of this connector's pins carry a wire.
        QSet<int> wired;
        for (const auto& w : def.wires) {
            if (w.fromConn == c.id) wired.insert(w.fromPin);
            if (w.toConn == c.id) wired.insert(w.toPin);
        }
        if (wired.isEmpty()) continue;
        // Count inversions: an unwired pin sitting ABOVE a wired one. Counting pairs (rather
        // than just "is it sorted") gives the optimiser a gradient to follow.
        const std::vector<int> seq = harnessPinSequence(c);
        int spareSoFar = 0;
        for (int pin : seq) {
            if (wired.contains(pin)) penalty += spareSoFar;
            else ++spareSoFar;
        }
    }
    return penalty;
}

int harnessLayoutCost(const HarnessDef& def, const HarnessOptimizeWeights& w) {
    return w.crossing * harnessCrossings(def) +
           w.shieldBreak * harnessBundleBreaks(def, /*shield=*/true) +
           w.twistBreak * harnessBundleBreaks(def, /*shield=*/false) +
           w.pinOrder * harnessPinOrderPenalty(def);
}

int optimizeHarnessPinout(HarnessDef& def, const HarnessOptimizeWeights& weights) {
    int best = harnessLayoutCost(def, weights);
    if (def.connectors.empty()) return best;
    // Only the pin ORDER moves — the connectors stay where they are and the wire list is
    // untouched. Two kinds of move are tried, sweeping until neither improves:
    //
    //   swap  — exchange two pin rows.
    //   block — lift a RUN of rows out and re-insert it elsewhere, keeping its internal order.
    //
    // The block move is what makes a bundled harness solvable. A twisted pair has to travel
    // as a unit, and no single swap can slide a pair past its neighbours without first
    // pulling it apart — which the bundle weights forbid. So a swap-only search sits happily
    // at "bundles intact, wires still crossing" and calls it done, which is exactly the
    // tangle that kept coming back. Sliding the pair whole reaches the untangled layout.
    constexpr int kMaxBlock = 6; // a bundle is small; no need to slide half the connector
    // These buttons run on the UI thread, so the search gets a budget rather than being free
    // to grind: a big harness with many pins would otherwise multiply out into a long freeze.
    // Running out simply stops improving and keeps the best layout found so far.
    int budget = 400000;
    bool improved = true;
    int guard = 0;
    while (improved && best > 0 && guard++ < 60 && budget > 0) {
        improved = false;
        for (auto& c : def.connectors) {
            std::vector<int> seq = harnessPinSequence(c);
            const int n = static_cast<int>(seq.size());
            // Adopt `trial` if it costs less, otherwise put the old order back.
            auto tryOrder = [&](const std::vector<int>& trial) {
                if (budget <= 0) return false;
                --budget;
                const std::vector<int> keep = c.pinOrder;
                c.pinOrder = trial;
                const int score = harnessLayoutCost(def, weights);
                if (score < best) {
                    best = score;
                    seq = trial;
                    improved = true;
                    return true;
                }
                c.pinOrder = keep;
                return false;
            };

            for (int i = 0; i < n && best > 0 && budget > 0; ++i)
                for (int j = i + 1; j < n; ++j) {
                    std::vector<int> trial = seq;
                    std::swap(trial[i], trial[j]);
                    tryOrder(trial);
                }

            for (int len = 1; len <= std::min(n, kMaxBlock) && best > 0 && budget > 0; ++len)
                for (int from = 0; from + len <= n; ++from)
                    for (int to = 0; to <= n - len; ++to) {
                        if (to == from) continue;
                        std::vector<int> rest = seq;
                        const std::vector<int> block(rest.begin() + from,
                                                     rest.begin() + from + len);
                        rest.erase(rest.begin() + from, rest.begin() + from + len);
                        std::vector<int> trial = rest;
                        trial.insert(trial.begin() + to, block.begin(), block.end());
                        tryOrder(trial);
                    }
        }
    }
    return best;
}

int minimizeHarnessCrossings(HarnessDef& def) {
    // Crossings AND bundling together: an untangled layout that splits a shield or a twisted
    // pair is not a layout that can be drawn, so the bundle terms outweigh the crossings.
    optimizeHarnessPinout(def, HarnessOptimizeWeights{});
    return harnessCrossings(def);
}

int optimizeHarnessForShields(HarnessDef& def) {
    // Shield adjacency above all else...
    HarnessOptimizeWeights w;
    w.crossing = 1;
    w.shieldBreak = 1000;
    w.twistBreak = 10; // still nudged, just not at the shield's expense
    optimizeHarnessPinout(def, w);
    // ...then tidy up under it. Optimising for one thing leaves the rest wherever it landed,
    // so a second pass with the standard weights takes out the crossings and closes the other
    // bundles WITHOUT undoing what was just achieved: the standard shield/twist weights are
    // heavy enough that no crossing saving can pay for re-splitting a group.
    optimizeHarnessPinout(def, HarnessOptimizeWeights{});
    return harnessBundleBreaks(def, /*shield=*/true);
}

int optimizeHarnessForTwists(HarnessDef& def) {
    HarnessOptimizeWeights w;
    w.crossing = 1;
    w.shieldBreak = 10;
    w.twistBreak = 1000;
    optimizeHarnessPinout(def, w);
    optimizeHarnessPinout(def, HarnessOptimizeWeights{}); // then tidy, as above
    return harnessBundleBreaks(def, /*shield=*/false);
}

namespace {
// What turning `first` nearer than `second` costs, in crossings. Rather than reason about which
// run can meet which, the pair is DRAWN - two right-angled wires, `first` turning first - and
// the crossings are counted with the same test the finished drawing is checked with. Only the
// ORDER of the two turns changes the answer, so stand-in x positions serve for any harness, and
// the score is exact: touching corners and runs that would lie along each other are all in it.
int kneePairCost(const std::pair<double, double>& first, const std::pair<double, double>& second) {
    constexpr double kLeft = 0.0, kRight = 1000.0, kNear = 400.0, kFar = 600.0;
    const Vec2 a[4] = {{kLeft, first.first},
                       {kNear, first.first},
                       {kNear, first.second},
                       {kRight, first.second}};
    const Vec2 b[4] = {{kLeft, second.first},
                       {kFar, second.first},
                       {kFar, second.second},
                       {kRight, second.second}};
    int cost = 0;
    for (int i = 1; i < 4; ++i)
        for (int j = 1; j < 4; ++j)
            if (segmentSegmentIntersection(a[i - 1], a[i], b[j - 1], b[j])) ++cost;
    return cost;
}
} // namespace

std::vector<int> harnessKneeLanes(const std::vector<std::pair<double, double>>& legs) {
    const int n = static_cast<int>(legs.size());
    std::vector<int> order(n);
    for (int i = 0; i < n; ++i) order[i] = i;
    if (n < 2) return order;

    // Every ordered pair scored once, up front: the descent below reads this table thousands of
    // times and the score never changes.
    std::vector<int> table(static_cast<size_t>(n) * n, 0);
    for (int a = 0; a < n; ++a)
        for (int b = 0; b < n; ++b)
            if (a != b) table[static_cast<size_t>(a) * n + b] = kneePairCost(legs[a], legs[b]);
    auto cost = [&table, n](int first, int second) {
        return table[static_cast<size_t>(first) * n + second];
    };
    auto total = [&](const std::vector<int>& ord) {
        int c = 0;
        for (int a = 0; a < n; ++a)
            for (int b = a + 1; b < n; ++b) c += cost(ord[a], ord[b]);
        return c;
    };
    // Improve an order by MOVING one wire's turn to a better place in the fan, and by SWAPPING
    // two of them, for as long as either pays. Adjacent swaps alone are not enough: a wire that
    // belongs several places along gets stuck, because each single step towards its place costs
    // more than it saves. Both moves are scored from the pairs they actually change - the wires
    // jumped over - so a pass costs n^2 pair tests instead of re-scoring the whole fan each time.
    auto descend = [&](std::vector<int>& ord) {
        auto moveGain = [&](int from, int to) {
            int gain = 0;
            const int v = ord[from];
            if (to > from)
                for (int k = from + 1; k <= to; ++k) // v ends up AFTER each of these
                    gain += cost(v, ord[k]) - cost(ord[k], v);
            else
                for (int k = to; k < from; ++k) // v ends up BEFORE each of these
                    gain += cost(ord[k], v) - cost(v, ord[k]);
            return gain; // > 0 removes that many crossings
        };
        auto swapGain = [&](int k, int m) { // k < m
            const int u = ord[k], v = ord[m];
            int gain = cost(u, v) - cost(v, u);
            for (int i = k + 1; i < m; ++i) {
                const int f = ord[i];
                gain += cost(u, f) - cost(v, f);   // u no longer precedes f, v does
                gain += cost(f, v) - cost(f, u);   // f no longer precedes v, u does
            }
            return gain;
        };
        for (int pass = 0; pass < 24; ++pass) {
            bool moved = false;
            for (int from = 0; from < n; ++from) {
                int bestTo = from, bestGain = 0;
                for (int to = 0; to < n; ++to) {
                    if (to == from) continue;
                    const int gain = moveGain(from, to);
                    if (gain > bestGain) { bestGain = gain; bestTo = to; }
                }
                if (bestGain > 0) {
                    const int v = ord[from];
                    ord.erase(ord.begin() + from);
                    ord.insert(ord.begin() + bestTo, v);
                    moved = true;
                }
            }
            for (int k = 0; k + 1 < n; ++k)
                for (int m = k + 1; m < n; ++m)
                    if (swapGain(k, m) > 0) {
                        std::swap(ord[k], ord[m]);
                        moved = true;
                    }
            if (!moved) break;
        }
    };

    // Two starting points, because a local search only ever finds the best order it can reach
    // from where it began: wires sorted by how far they reach, and the same the other way up.
    // The better of the two settled orders wins.
    std::vector<int> byReach = order;
    std::stable_sort(byReach.begin(), byReach.end(), [&legs](int a, int b) {
        return std::abs(legs[a].first - legs[a].second) <
               std::abs(legs[b].first - legs[b].second);
    });
    std::vector<int> byReachDown(byReach.rbegin(), byReach.rend());
    descend(byReach);
    descend(byReachDown);
    const std::vector<int>& best = total(byReachDown) < total(byReach) ? byReachDown : byReach;

    std::vector<int> lane(n, 0);
    for (int i = 0; i < n; ++i) lane[best[i]] = i;
    return lane;
}

HarnessBuild buildHarness(const HarnessDef& def) {
    HarnessBuild out;
    out.issues = validateHarness(def);
    const HarnessLayout& L = def.layout;
    const std::vector<Placed> placed = placeConnectors(def);

    // ---- Connectors ----
    for (const Placed& p : placed) {
        Entity e;
        e.type = EntityType::Connector;
        e.layer = QStringLiteral("Connectors");
        e.a = p.origin;
        writeConnectorSpec(e.meta, p.spec);
        out.entities.push_back(std::move(e));
    }

    // ---- Wires ----
    // Every wire runs pin -> out to its leg's trunk -> across -> in to the far pin, so runs
    // stay orthogonal and wires of one leg share a corridor.
    struct Routed { QString wireId; size_t entityIndex = 0; };
    std::vector<Routed> routed;
    const auto wires = orderedWires(def);

    // Work out every wire's two ends first, then decide the ORDER of the vertical lanes —
    // the lane order is what decides whether the knees cross each other.
    struct Leg {
        const HarnessWire* w = nullptr;
        const Placed* a = nullptr;
        const Placed* b = nullptr;
        Vec2 pa, pb;
        bool needsKnee = false;
        int lane = 0;
    };
    std::vector<Leg> legs;
    for (const HarnessWire* w : wires) {
        Leg L2;
        L2.w = w;
        L2.a = placedById(placed, w->fromConn);
        L2.b = placedById(placed, w->toConn);
        if (!L2.a || !L2.b) continue;
        if (!pinEndOf(*L2.a, w->fromPin, L2.pa) || !pinEndOf(*L2.b, w->toPin, L2.pb)) continue;
        L2.needsKnee = std::abs(L2.pa.y - L2.pb.y) >= 1e-6;
        legs.push_back(L2);
    }
    // Which order the wires turn in: the crossing-aware lane order, shared with the builder's
    // preview so the two pictures agree.
    int kneeCount = 0;
    {
        std::vector<Leg*> kneed;
        std::vector<std::pair<double, double>> rows;
        for (Leg& L2 : legs)
            if (L2.needsKnee) {
                kneed.push_back(&L2);
                // Always given LEFT row first: the lane order is worked out for a wire running
                // left to right, and a wire whose "from" end happens to be the right-hand one
                // would otherwise be scored back to front.
                const bool fromLeft = (L2.a->conn->side == HarnessSide::Left);
                rows.push_back(fromLeft ? std::make_pair(L2.pa.y, L2.pb.y)
                                        : std::make_pair(L2.pb.y, L2.pa.y));
            }
        const std::vector<int> lanes = harnessKneeLanes(rows);
        for (size_t i = 0; i < kneed.size(); ++i) kneed[i]->lane = lanes[i];
        kneeCount = static_cast<int>(kneed.size());
    }
    // Every knee is measured from ONE start line, a fixed pitch apart, so the turns form a
    // regular fan in the middle of the harness instead of each wire turning near whichever
    // pair of connectors it happens to join.
    double laneBase = 0.0, laneStep = 0.0; // the step IS the connector pitch
    {
        double leftMost = -1e18, rightMost = 1e18;
        for (const Placed& p : placed) {
            if (p.conn->side == HarnessSide::Left) leftMost = std::max(leftMost, p.trunkX);
            else rightMost = std::min(rightMost, p.trunkX);
            laneStep = std::max(laneStep, p.spec.pitch);
        }
        if (laneStep <= 0.0) laneStep = 15.0; // no connectors placed: a sane fallback
        if (leftMost < -1e17 || rightMost > 1e17) {
            laneBase = 0.0;
        } else {
            // Centred on the corridor, so the fan grows both ways from the middle and stays
            // clear of the connectors at either end.
            laneBase = (leftMost + rightMost) / 2.0 - (kneeCount - 1) * laneStep / 2.0;
        }
    }

    for (const Leg& L2 : legs) {
        const HarnessWire* w = L2.w;
        Entity e;
        e.type = EntityType::Wire;
        e.layer = QStringLiteral("Wires");
        // Lanes are spaced by the CONNECTOR PITCH, so the turns sit as far apart as the pins
        // they come from, and all of them are measured from the same start line.
        const double midX = laneBase + L2.lane * laneStep;
        const Vec2 pa = L2.pa, pb = L2.pb;
        if (!L2.needsKnee) {
            e.points = {pa, pb}; // straight across: no knee needed
        } else {
            e.points = {pa, {midX, pa.y}, {midX, pb.y}, pb};
        }
        e.meta.insert("wireColor", w->color);
        e.meta.insert("wireGauge", w->gauge);
        // The colour that is actually DRAWN is the wire's ordinary colour property. With
        // "By layer" it is left unset, so the wire takes the layer's colour.
        if (L.wireColorMode == QLatin1String("bylayer")) {
            e.meta.insert("wireColorMode", QStringLiteral("bylayer"));
        } else {
            e.color = wireColorHex(w->color);
        }
        // What the wire is marked with, straight from its row: the colour code and the gauge
        // read along the run, the label above it. Each is independent, and "annCount" is what
        // the renderer repeats them by.
        e.meta.insert("showColorName", w->showColor);
        e.meta.insert("showGaugeName", w->showGauge);
        if (w->showColor || w->showGauge) e.meta.insert("annCount", 1);
        if (!w->label.isEmpty()) e.meta.insert("wireLabel", w->label);
        e.meta.insert("showWireLabel", w->showLabel);
        e.meta.insert("showLabel", true);
        routed.push_back({w->id, out.entities.size()});
        out.entities.push_back(std::move(e));
    }

    // ---- Crossings: hop or plain cross (item 6) ----
    out.crossings = harnessCrossings(def);
    if (L.bridgeCrossings) {
        // Mark the actual geometric crossings so the renderer draws a bridge there. The
        // LATER wire hops over the earlier one, so each crossing is bridged exactly once.
        for (size_t i = 0; i < routed.size(); ++i)
            for (size_t j = i + 1; j < routed.size(); ++j) {
                Entity& wi = out.entities[routed[i].entityIndex];
                Entity& wj = out.entities[routed[j].entityIndex];
                QJsonArray hops = wj.meta.value("hops").toArray();
                for (size_t si = 1; si < wi.points.size(); ++si)
                    for (size_t sj = 1; sj < wj.points.size(); ++sj)
                        if (auto x = segmentSegmentIntersection(wi.points[si - 1], wi.points[si],
                                                                wj.points[sj - 1], wj.points[sj])) {
                            QJsonArray pt;
                            pt.append(x->x);
                            pt.append(x->y);
                            hops.append(pt);
                        }
                if (!hops.isEmpty()) wj.meta.insert("hops", hops);
            }
    }

    // ---- Shields (items 4, 9, 10) ----
    // One shield per shielded group, dropped just outside the pins of the connector the
    // definition marked "shield near", and placed `shieldAmount` times.
    auto groupKeys = [&def](bool shield) {
        std::vector<QString> keys;
        for (const auto& w : def.wires) {
            const QString k = shield ? w.shieldGroup : w.twistGroup;
            if (k.isEmpty()) continue;
            if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
        }
        return keys;
    };
    // The crossing points of a group's wires on a vertical cut at world x.
    auto cutAt = [&](const std::vector<QString>& wireIds, double x, std::vector<Vec2>& pts,
                     QJsonArray& ids, QString& gauge) {
        for (const Routed& r : routed) {
            if (std::find(wireIds.begin(), wireIds.end(), r.wireId) == wireIds.end()) continue;
            const Entity& we = out.entities[r.entityIndex];
            for (size_t s = 1; s < we.points.size(); ++s) {
                const Vec2 p1 = we.points[s - 1], p2 = we.points[s];
                if ((x < std::min(p1.x, p2.x) - 1e-9) || (x > std::max(p1.x, p2.x) + 1e-9))
                    continue;
                const double t = (std::abs(p2.x - p1.x) < 1e-9)
                                     ? 0.0
                                     : (x - p1.x) / (p2.x - p1.x);
                pts.push_back({x, p1.y + (p2.y - p1.y) * t});
                ids.append(r.wireId);
                if (gauge.isEmpty()) gauge = we.meta.value("wireGauge").toString();
                break;
            }
        }
    };

    // Placing one screen: cut the wires at world x, size the ellipse to the band, and hang
    // the drain below it. Returns the drain end so a body bond can reach it.
    const double legLen = std::max(0.0, L.shieldLegLength);
    auto placeScreen = [&](const std::vector<QString>& wireIds, double x, const QString& key,
                           bool overall, bool owns, Vec2& drainOut,
                           const Placed* body = nullptr) {
        std::vector<Vec2> cuts;
        QJsonArray refs;
        QString gauge;
        cutAt(wireIds, x, cuts, refs, gauge);
        if (cuts.empty()) return false;
        double yMin = cuts.front().y, yMax = cuts.front().y;
        for (const Vec2& c : cuts) { yMin = std::min(yMin, c.y); yMax = std::max(yMax, c.y); }
        // An overall screen sits a little wider, so an internal one nests visibly inside it.
        const double pad = overall ? 12.0 : 7.0;
        const double ry = (yMax - yMin) / 2.0 + pad;
        const double rx = overall ? 12.0 : 8.0;
        Entity e;
        e.type = EntityType::Screen;
        e.layer = QStringLiteral("Wires");
        e.points = cuts;
        // Which way the drain leaves the screen. A leg dropped straight down out of a band
        // that sits high on a connector would have to cut across every pin below it, so the
        // leg escapes towards whichever end of the connector is nearer: up out of a high
        // band, down out of a low one. It is also carried clear of the body's own extent, so
        // the bond that follows it runs entirely in the empty space beside the connector
        // instead of over the other pins' stubs.
        const bool up = body && ((yMin + yMax) * 0.5) >=
                                    (body->geom.rectMin.y + body->geom.rectMax.y) * 0.5;
        if (up) {
            double y = yMax + pad + legLen;
            if (body) y = std::max(y, body->geom.rectMax.y + legLen);
            drainOut = {x, y};
        } else {
            double y = yMin - pad - legLen;
            if (body) y = std::min(y, body->geom.rectMin.y - legLen);
            drainOut = {x, y};
        }
        e.points.push_back(drainOut);
        e.meta.insert("catch", static_cast<int>(cuts.size()));
        // Only the first symbol of a run truly catches the wires; the repeats are the same
        // screen drawn again further along.
        e.meta.insert("wires", owns ? refs : QJsonArray());
        e.meta.insert("rx", rx);
        e.meta.insert("ry", ry);
        e.meta.insert("rot", 1.5707963267948966); // perpendicular to the horizontal runs
        e.meta.insert("shape", L.shieldType == QLatin1String("capsule")
                                   ? QStringLiteral("capsule")
                                   : QStringLiteral("ellipse"));
        e.meta.insert("legDir", up ? QStringLiteral("top") : QStringLiteral("bottom"));
        e.meta.insert("showS", true);
        e.meta.insert("sSize", 4.0);
        // A screen is drawn with a SOLID outline: on a harness diagram the dashed one reads
        // as a note or a boundary rather than as the braid itself.
        e.meta.insert("lineType", QStringLiteral("solid"));
        e.meta.insert(overall ? "overallShield" : "shieldGroup", key);
        out.entities.push_back(std::move(e));
        return true;
    };
    // Bond a drain end back to a connector's own body joint: down from the corner, then out
    // to the drain, the way a screen is terminated onto a shell.
    auto bondToBody = [&](const Placed& p, const Vec2& drain, const QString& key) {
        const bool leftBody = (p.conn->side == HarnessSide::Left);
        // The joint the drain lands on: the body corner on the harness side, at whichever end
        // of the body the drain escaped past — so the bond leaves the shell at the nearest
        // corner and never has to travel back along it.
        const bool up = drain.y > (p.geom.rectMin.y + p.geom.rectMax.y) * 0.5;
        const Vec2 corner{leftBody ? p.geom.rectMax.x : p.geom.rectMin.x,
                          up ? p.geom.rectMax.y : p.geom.rectMin.y};
        // Out of the corner on a 45 degrees diagonal until it is level with the drain, then
        // straight along to it. Both legs run outside the body, so the bond crosses nothing.
        const double inward = leftBody ? 1.0 : -1.0;
        const double dy = std::abs(drain.y - corner.y);
        const double run = std::abs(drain.x - corner.x);
        const double diag = std::min(dy, run * 0.5);
        Entity bond;
        bond.type = EntityType::Polyline;
        bond.layer = QStringLiteral("Wires");
        bond.points = {corner, {corner.x + inward * diag, drain.y}, drain};
        bond.meta.insert("shieldBond", key);
        out.entities.push_back(std::move(bond));
    };

    // ---- Overall shields: one per connector that asks for one ----
    // The OVERALL screen wraps everything arriving at that connector, so it is asked for on
    // the connector rather than per wire, and it is drawn just inboard of its own pins — the
    // only place its wires actually run. Every connector marked "shield to body" is bonded to
    // ITS OWN body, so a harness screened at both ends gets a bond at each.
    for (const Placed& p : placed) {
        const bool wantsOverall = (p.conn->side == HarnessSide::Left) ? p.conn->shieldLeft
                                                                     : p.conn->shieldRight;
        const bool wantsBond = (p.conn->side == HarnessSide::Left) ? p.conn->bodyBondLeft
                                                                  : p.conn->bodyBondRight;
        if (!wantsOverall && !wantsBond) continue;
        // Everything that reaches this connector.
        std::vector<QString> ids;
        for (const auto& w : def.wires)
            if (w.fromConn == p.conn->id || w.toConn == p.conn->id) ids.push_back(w.id);
        if (ids.empty()) continue;
        const double inward = (p.conn->side == HarnessSide::Left) ? 1.0 : -1.0;
        const double baseX = p.geom.pinEnd.empty() ? p.origin.x : p.geom.pinEnd.front().x;
        const QString key = p.conn->designator.isEmpty() ? p.conn->id : p.conn->designator;
        bool haveDrain = false;
        Vec2 firstDrain{0.0, 0.0};
        for (int k = 0; k < std::max(1, L.shieldAmount); ++k) {
            Vec2 drain;
            if (!placeScreen(ids, baseX + inward * (25.0 + 34.0 * k), key, /*overall=*/true,
                             /*owns=*/k == 0, drain, &p))
                continue;
            if (!haveDrain) { firstDrain = drain; haveDrain = true; }
        }
        if (haveDrain && wantsBond) bondToBody(p, firstDrain, key);
    }

    // ---- Internal shields: one per wire-level shield group ----
    // An INTERNAL screen wraps a named subset of the wires and is drawn further along the run
    // than the overall one, so it reads as nested inside it.
    for (const QString& key : groupKeys(/*shield=*/true)) {
        std::vector<QString> ids;
        for (const auto& w : def.wires)
            if (w.shieldGroup == key) ids.push_back(w.id);
        if (ids.empty()) continue;
        // Span of the group's runs, so the symbols sit along the middle of it.
        double xa = 1e18, xb = -1e18;
        for (const Routed& r : routed) {
            if (std::find(ids.begin(), ids.end(), r.wireId) == ids.end()) continue;
            for (const Vec2& pt : out.entities[r.entityIndex].points) {
                xa = std::min(xa, pt.x);
                xb = std::max(xb, pt.x);
            }
        }
        if (xa > xb) continue;
        const int amount = std::max(1, L.wireShieldAmount);
        for (int k = 0; k < amount; ++k) {
            const double x = xa + (xb - xa) * (k + 1.0) / (amount + 1.0);
            Vec2 drain;
            placeScreen(ids, x, key, /*overall=*/false, /*owns=*/k == 0, drain);
        }
    }

    // ---- Twisted groups (items 4, 9) ----
    for (const QString& key : groupKeys(/*shield=*/false)) {
        std::vector<QString> ids;
        for (const auto& w : def.wires)
            if (w.twistGroup == key) ids.push_back(w.id);
        if (ids.size() < 2) continue; // nothing to braid
        // Twist symbols go mid-span, spaced along the run, `twistAmount` of them.
        double xa = 1e18, xb = -1e18;
        for (const Routed& r : routed) {
            if (std::find(ids.begin(), ids.end(), r.wireId) == ids.end()) continue;
            for (const Vec2& pt : out.entities[r.entityIndex].points) {
                xa = std::min(xa, pt.x);
                xb = std::max(xb, pt.x);
            }
        }
        if (xa > xb) continue;
        const int amount = std::max(1, L.twistAmount);
        const int want = static_cast<int>(ids.size()); // every wire of the pair/group
        // Each twist of a group must braid the SAME wires as the first one. A twist is only
        // valid where EVERY wire of the group still runs side by side — past the point where
        // one of them has turned away there is no pair left to braid, and placing a symbol
        // there would show a twist between the wrong wires. So candidate positions are swept
        // and only the ones that catch the whole group are used.
        std::vector<double> spots;
        constexpr int kProbe = 96;
        for (int probe = 1; probe < kProbe && static_cast<int>(spots.size()) < amount; ++probe) {
            // Sweep outward from the ideal even spacing so the symbols still land spread out.
            const double ideal = xa + (xb - xa) * (static_cast<int>(spots.size()) + 1.0) /
                                          (amount + 1.0);
            const double step = (xb - xa) / kProbe;
            for (int dir = 0; dir < 2 && static_cast<int>(spots.size()) < amount; ++dir) {
                const double x = ideal + (dir == 0 ? 1.0 : -1.0) * step * (probe - 1);
                if (x <= xa || x >= xb) continue;
                std::vector<Vec2> cuts;
                QJsonArray wireIds;
                QString gauge;
                cutAt(ids, x, cuts, wireIds, gauge);
                if (static_cast<int>(cuts.size()) != want) continue; // not the whole group here
                // Keep the symbols clear of each other.
                bool tooClose = false;
                for (double had : spots)
                    if (std::abs(had - x) < std::max(30.0, (xb - xa) / (amount + 2.0)))
                        tooClose = true;
                if (tooClose) continue;
                spots.push_back(x);
            }
        }
        std::sort(spots.begin(), spots.end());
        for (double x : spots) {
            std::vector<Vec2> cuts;
            QJsonArray wireIds;
            QString gauge;
            cutAt(ids, x, cuts, wireIds, gauge);
            if (static_cast<int>(cuts.size()) != want) continue;
            Entity e;
            e.type = EntityType::Twist;
            e.layer = QStringLiteral("Wires");
            e.points = cuts;
            const QString ty = L.twistType.isEmpty() ? QStringLiteral("jog") : L.twistType;
            e.meta.insert("twistType", ty);
            // EVERY symbol names the group's wires, so each one is a real twist on that pair
            // (and each wire reads as twisted wherever it is asked).
            e.meta.insert("wires", wireIds);
            e.meta.insert("wireGauge", gauge);
            e.meta.insert("repeats", std::max(1, L.twistCount));
            // Type 1 ("crossed") is drawn from a diagonal + caps instead of repeats.
            if (ty == QLatin1String("crossed")) {
                e.meta.insert("diagAngle", 15.0);
                e.meta.insert("capLen", 8.0);
            } else if (ty != QLatin1String("type3")) {
                e.meta.insert("flatLen", 6.0);
            }
            e.meta.insert("twistGroup", key);
            out.entities.push_back(std::move(e));
        }
    }

    // ---- Unconnected pins ----
    if (L.terminateUnconnected) {
        for (const Placed& p : placed) {
            for (int row = 0; row < static_cast<int>(p.geom.pinEnd.size()); ++row) {
                const int pin = (row < static_cast<int>(p.pinSeq.size())) ? p.pinSeq[row] : row + 1;
                bool wired = false;
                for (const auto& w : def.wires)
                    if ((w.fromConn == p.conn->id && w.fromPin == pin) ||
                        (w.toConn == p.conn->id && w.toPin == pin)) { wired = true; break; }
                if (wired) continue;
                Entity e;
                e.type = EntityType::NotConnected;
                e.layer = QStringLiteral("Wires");
                e.a = p.geom.pinEnd[row];
                e.meta.insert("ncSize", std::max(2.0, p.spec.pitch * 0.3));
                out.entities.push_back(std::move(e));
            }
        }
    }
    return out;
}

} // namespace Edraw_Desktop
