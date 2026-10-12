#include "harness/HarnessBuilder.h"

#include "geometry/Geometry.h"

#include <QJsonArray>
#include <QJsonObject>
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

bool harnessIsCoaxKind(const QString& kind) {
    return kind == QLatin1String("coax") || kind == QLatin1String("coax-splice");
}

bool harnessCoaxSplices(const HarnessDef& def, const QString& bundle) {
    if (bundle.isEmpty()) return false;
    // One row saying so makes the cable a spliced one: both conductors of a cable transition
    // together, so the rows cannot disagree about it.
    for (const auto& w : def.wires)
        if (w.bundle == bundle && w.kind == QLatin1String("coax-splice")) return true;
    return false;
}

int harnessCoaxIndex(const HarnessDef& def, const HarnessWire& wire) {
    if (!harnessIsCoaxKind(wire.kind) || wire.bundle.isEmpty()) return 0;
    // The number names the COAX, not the row: a coax is a cable with two conductors in it, and
    // both rows of bundle COAX1 are conductors of coax 1 - Signal 1 and Shield 1. Numbering the
    // rows instead gave "Signal 1" and "Shield 2" for the two halves of one cable, which reads
    // as two different coaxes. Bundles are counted in the order they first appear, so the number
    // follows the drawing rather than a name that happens to have a digit in it.
    QStringList bundles;
    for (const auto& w : def.wires) {
        if (!harnessIsCoaxKind(w.kind) || w.bundle.isEmpty()) continue;
        if (!bundles.contains(w.bundle)) bundles << w.bundle;
    }
    // Numbered even when it is the only one: bundle COAX1 is coax 1, and its two conductors read
    // Signal 1 / Shield 1. Only a coax with no bundle at all is left unnumbered.
    const int at = bundles.indexOf(wire.bundle);
    return at < 0 ? 0 : at + 1;
}

QString harnessOpenEndpointKey() { return QStringLiteral("#OPEN"); }

bool harnessIsOpenEndpoint(const QString& key) { return key == QLatin1String("#OPEN"); }

bool harnessEndIsOpen(const QString& conn, int pin) {
    // A shield endpoint is a real termination (the drain lands on the screen's leg), so it is not open
    // even though it has no pin number.
    if (harnessIsShieldEndpoint(conn)) return false;
    return harnessIsOpenEndpoint(conn) || pin < 1;
}

QString harnessOverallShieldKey(const QString& connId, HarnessSide side) {
    return QStringLiteral("#S:%1:%2").arg(connId, side == HarnessSide::Left
                                                      ? QStringLiteral("left")
                                                      : QStringLiteral("right"));
}

QString harnessInternalShieldKey(const QString& group) {
    return QStringLiteral("#I:") + group;
}

bool harnessIsShieldEndpoint(const QString& key) {
    return key.startsWith(QLatin1String("#S:")) || key.startsWith(QLatin1String("#I:"));
}

QString harnessShieldEndpointGroup(const QString& key) {
    return key.startsWith(QLatin1String("#I:")) ? key.mid(3) : QString();
}

bool harnessShieldEndpointConn(const QString& key, QString& connId, HarnessSide& side) {
    if (!key.startsWith(QLatin1String("#S:"))) return false;
    const QString rest = key.mid(3);
    const int cut = rest.lastIndexOf(QLatin1Char(':'));
    if (cut <= 0) return false;
    connId = rest.left(cut);
    side = (rest.mid(cut + 1) == QLatin1String("right")) ? HarnessSide::Right : HarnessSide::Left;
    return true;
}

QString harnessShieldEndpointLabel(const HarnessDef& def, const QString& key) {
    const QString group = harnessShieldEndpointGroup(key);
    if (!group.isEmpty()) return QStringLiteral("Shield %1 (internal)").arg(group);
    QString connId;
    HarnessSide side = HarnessSide::Left;
    if (!harnessShieldEndpointConn(key, connId, side)) return key;
    const HarnessConnector* c = harnessConnById(def, connId);
    if (!c) return key;
    return QStringLiteral("%1 shield (%2)")
        .arg(c->designator.isEmpty() ? c->id : c->designator,
             side == HarnessSide::Left ? QStringLiteral("left") : QStringLiteral("right"));
}

QStringList harnessShieldEndpoints(const HarnessDef& def) {
    QStringList out;
    // Only the screens that exist: a drop-down of shields the harness has not been given would
    // be noise, and a wire could be pointed at one that is never drawn.
    for (const auto& c : def.connectors) {
        if (c.shieldLeft) out << harnessOverallShieldKey(c.id, HarnessSide::Left);
        if (c.shieldRight) out << harnessOverallShieldKey(c.id, HarnessSide::Right);
    }
    for (const auto& w : def.wires) {
        if (w.shieldGroup.isEmpty()) continue;
        const QString key = harnessInternalShieldKey(w.shieldGroup);
        if (!out.contains(key)) out << key;
    }
    return out;
}

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

namespace {
// An end lands on a real pin when it names an actual connector - not the open marker, not a
// shield's drain - at a pin that has been landed (0 = "not landed yet" while a row is filled
// in). Only such an end has a "from"/"to" cell to print.
bool connectionTableEndIsReal(const HarnessDef& def, const QString& conn, int pin) {
    return pin >= 1 && !harnessIsOpenEndpoint(conn) && !harnessIsShieldEndpoint(conn) &&
           harnessConnById(def, conn) != nullptr;
}
// An order-independent key for the pair of connectors a wire runs between, so every wire
// touching either one sorts next to the rest - which is what keeps an overall shield's own
// wires (everything touching ONE connector) contiguous whenever that connector only ever
// talks to one other one, the ordinary case.
QString connectionTablePairKey(const QString& a, const QString& b) {
    return a <= b ? a + QLatin1Char('|') + b : b + QLatin1Char('|') + a;
}
} // namespace

ConnectionTable harnessConnectionTable(const HarnessDef& def) {
    ConnectionTable out;
    struct SortRow {
        ConnectionTableRow row;
        QString pairKey, shieldGroup, twistGroup;
        int origIndex = 0;
    };
    std::vector<SortRow> rows;
    int origIndex = 0;
    for (const auto& w : def.wires) {
        ++origIndex;
        if (!connectionTableEndIsReal(def, w.fromConn, w.fromPin) ||
            !connectionTableEndIsReal(def, w.toConn, w.toPin))
            continue;
        const HarnessConnector* fc = harnessConnById(def, w.fromConn);
        const HarnessConnector* tc = harnessConnById(def, w.toConn);
        SortRow sr;
        sr.row.wireId = w.id;
        sr.row.from = QStringLiteral("%1-%2").arg(
            fc->designator.isEmpty() ? fc->id : fc->designator, harnessPinLabel(*fc, w.fromPin));
        sr.row.to = QStringLiteral("%1-%2").arg(
            tc->designator.isEmpty() ? tc->id : tc->designator, harnessPinLabel(*tc, w.toPin));
        sr.row.color = w.color;
        sr.row.gauge = w.gauge;
        sr.pairKey = connectionTablePairKey(w.fromConn, w.toConn);
        sr.shieldGroup = w.shieldGroup;
        sr.twistGroup = w.twistGroup;
        sr.origIndex = origIndex;
        rows.push_back(std::move(sr));
    }
    // Grouped so a shield/twist group's rows land next to each other: by which two connectors
    // the wire runs between, then by its internal shield group, then by its twist group, non-
    // empty first at each step (a wire with no group sorts after the ones that have one).
    std::stable_sort(rows.begin(), rows.end(), [](const SortRow& a, const SortRow& b) {
        if (a.pairKey != b.pairKey) return a.pairKey < b.pairKey;
        const bool ase = a.shieldGroup.isEmpty(), bse = b.shieldGroup.isEmpty();
        if (ase != bse) return bse;
        if (a.shieldGroup != b.shieldGroup) return a.shieldGroup < b.shieldGroup;
        const bool ate = a.twistGroup.isEmpty(), bte = b.twistGroup.isEmpty();
        if (ate != bte) return bte;
        if (a.twistGroup != b.twistGroup) return a.twistGroup < b.twistGroup;
        return a.origIndex < b.origIndex;
    });
    out.rows.reserve(rows.size());
    for (const SortRow& r : rows) out.rows.push_back(r.row);

    // One span per MAXIMAL RUN of rows where `member` holds - never assumed to be a single
    // span, so a group that the sort above could not keep together (a connector talking to
    // more than one other connector, say) still comes out as separate, correct brackets rather
    // than one that is wrong.
    auto spansFor = [&](auto member) {
        std::vector<std::pair<int, int>> runs;
        int start = -1;
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            if (member(i)) {
                if (start < 0) start = i;
            } else if (start >= 0) {
                runs.push_back({start, i - 1});
                start = -1;
            }
        }
        if (start >= 0) runs.push_back({start, static_cast<int>(rows.size()) - 1});
        return runs;
    };

    // Overall shields: the union of every wire touching a connector whose OWN side asks for
    // one - one shared bracket set, since two connectors that both ask for an overall shield
    // over the very same wires would otherwise draw two identical, redundant brackets.
    QSet<QString> overallWireIds;
    for (const auto& c : def.connectors) {
        const bool wants = (c.side == HarnessSide::Left) ? c.shieldLeft : c.shieldRight;
        if (!wants) continue;
        for (const auto& w : def.wires)
            if (w.fromConn == c.id || w.toConn == c.id) overallWireIds.insert(w.id);
    }
    if (!overallWireIds.isEmpty())
        for (const auto& run : spansFor([&](int i) { return overallWireIds.contains(rows[i].row.wireId); }))
            out.shieldSpans.push_back({run.first, run.second, 0, QStringLiteral("SHIELD"), 0});

    // Internal shield groups, one bracket set per distinct name, nested inside the overall one.
    QStringList shieldGroups;
    for (const SortRow& r : rows)
        if (!r.shieldGroup.isEmpty() && !shieldGroups.contains(r.shieldGroup))
            shieldGroups << r.shieldGroup;
    for (const QString& g : shieldGroups)
        for (const auto& run : spansFor([&](int i) { return rows[i].shieldGroup == g; }))
            out.shieldSpans.push_back({run.first, run.second, 1, g, 0});

    // Twist groups: a merged run in the Twist column, one per distinct name.
    QStringList twistGroups;
    for (const SortRow& r : rows)
        if (!r.twistGroup.isEmpty() && !twistGroups.contains(r.twistGroup))
            twistGroups << r.twistGroup;
    for (const QString& g : twistGroups)
        for (const auto& run : spansFor([&](int i) { return rows[i].twistGroup == g; }))
            out.twistSpans.push_back(
                {run.first, run.second, 0, g, run.second - run.first + 1});

    return out;
}

std::vector<Entity> harnessConnectionTableEntities(const HarnessDef& def) {
    const ConnectionTable ct = harnessConnectionTable(def);
    std::vector<Entity> out;
    if (ct.rows.empty()) return out;

    static const QStringList kHeaders = {QStringLiteral("#"),     QStringLiteral("From"),
                                         QStringLiteral("To"),    QStringLiteral("Color"),
                                         QStringLiteral("Gauge"), QStringLiteral("Twist")};
    const int cols = kHeaders.size();
    const int twistCol = cols - 1;
    const int nRows = static_cast<int>(ct.rows.size()) + 1;
    const double rowH = 8.0, textH = rowH * 0.5;

    // Only a twist span's ANCHOR row (its first) carries the cell's words/icon - the rows it
    // covers are merged into that one cell and print nothing of their own.
    QHash<int, const ConnectionTableSpan*> twistAnchorAtRow;
    for (const auto& sp : ct.twistSpans) twistAnchorAtRow.insert(sp.firstRow, &sp);
    const bool numberTwists = ct.twistSpans.size() > 1;

    QJsonArray cells, cellIconKind;
    for (const QString& h : kHeaders) { cells.append(h); cellIconKind.append(QString()); }
    for (int i = 0; i < static_cast<int>(ct.rows.size()); ++i) {
        const ConnectionTableRow& r = ct.rows[i];
        for (const QString& s : {QString::number(i + 1), r.from, r.to, r.color, r.gauge}) {
            cells.append(s);
            cellIconKind.append(QString());
        }
        QString twistText, twistIcon;
        const auto found = twistAnchorAtRow.constFind(i);
        if (found != twistAnchorAtRow.constEnd()) {
            const ConnectionTableSpan& sp = *found.value();
            twistText = numberTwists ? QStringLiteral("%1 (%2)").arg(sp.label).arg(sp.memberCount)
                                     : QString::number(sp.memberCount);
            twistIcon = QStringLiteral("twist");
        }
        cells.append(twistText);
        cellIconKind.append(twistIcon);
    }
    // Each column is as wide as the widest thing in it, down to a floor - the same estimate the
    // BOM table uses.
    const double pad = textH * 1.6, minW = 14.0;
    std::vector<double> colW(static_cast<size_t>(cols), minW);
    for (int i = 0; i < cells.size(); ++i) {
        const int c = i % cols;
        const double need = pad + 0.62 * textH * cells[i].toString().size();
        colW[static_cast<size_t>(c)] = std::max(colW[static_cast<size_t>(c)], need);
    }
    double W = 0.0;
    for (int c = 0; c < cols; ++c) W += colW[static_cast<size_t>(c)];
    const double H = nRows * rowH;

    Entity t;
    t.type = EntityType::Rect;
    t.points = {{0, -H}, {W, -H}, {W, 0}, {0, 0}};
    t.a = {0, 0};
    QJsonArray colFrac, rowFrac;
    for (int c = 0; c < cols; ++c) colFrac.append(colW[static_cast<size_t>(c)] / W);
    for (int i = 0; i < nRows; ++i) rowFrac.append(1.0 / nRows);
    QJsonArray cellAlign;
    for (int i = 0; i < cells.size(); ++i) cellAlign.append(QStringLiteral("MC"));
    t.meta.insert(QStringLiteral("table"), true);
    t.meta.insert(QStringLiteral("rows"), nRows);
    t.meta.insert(QStringLiteral("cols"), cols);
    t.meta.insert(QStringLiteral("colFrac"), colFrac);
    t.meta.insert(QStringLiteral("rowFrac"), rowFrac);
    t.meta.insert(QStringLiteral("cells"), cells);
    t.meta.insert(QStringLiteral("cellAlign"), cellAlign);
    t.meta.insert(QStringLiteral("headerRow"), true);
    t.meta.insert(QStringLiteral("textSize"), textH);
    t.meta.insert(QStringLiteral("cellIconKind"), cellIconKind);
    t.meta.insert(QStringLiteral("connectionTable"), true); // so it can be found later
    std::vector<TableMerge> merges;
    for (const auto& sp : ct.twistSpans)
        if (sp.lastRow > sp.firstRow) // one row alone is not a merge - nothing to join it to
            merges.push_back({sp.firstRow + 1, twistCol, sp.lastRow - sp.firstRow + 1, 1});
    if (!merges.empty()) setTableMerges(t, merges);
    out.push_back(t);

    // Shield brackets, left of the table: level 0 (overall) in the outer lane, level 1
    // (internal) in the inner one, so a nested shield reads as nested rather than as a second
    // unrelated one.
    const double laneGap = 10.0, tickLen = 3.0, labelPad = 2.0;
    for (const auto& sp : ct.shieldSpans) {
        const double top = -(sp.firstRow + 1) * rowH;
        const double bottom = -(sp.lastRow + 2) * rowH;
        const double xLine = -(2 - sp.level) * laneGap;
        auto braLine = [&](const Vec2& a, const Vec2& b) {
            Entity l;
            l.type = EntityType::Line;
            l.a = a;
            l.b = b;
            l.meta.insert(QStringLiteral("connectionTableBracket"), true);
            return l;
        };
        out.push_back(braLine({xLine, top}, {xLine, bottom}));              // spine
        out.push_back(braLine({xLine, top}, {xLine + tickLen, top}));       // top tick
        out.push_back(braLine({xLine, bottom}, {xLine + tickLen, bottom})); // bottom tick
        Entity label;
        label.type = EntityType::Text;
        label.a = {xLine - labelPad, (top + bottom) / 2.0};
        label.meta.insert(QStringLiteral("text"), sp.label);
        label.meta.insert(QStringLiteral("size"), textH);
        label.meta.insert(QStringLiteral("justify"), QStringLiteral("MR"));
        label.meta.insert(QStringLiteral("connectionTableBracket"), true);
        out.push_back(label);
    }
    return out;
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
        // A coax is a cable of two conductors: one row for the SIGNAL and one for the SHIELD, in
        // the same bundle. With only one of them there is nothing to draw - a signal with no braid
        // is a plain wire, and a braid with no signal has nothing to screen.
        if (harnessIsCoaxKind(w.kind)) {
            if (w.bundle.isEmpty()) {
                add(QStringLiteral("coaxnobundle:%1").arg(w.id),
                    QStringLiteral("A coax row is in no bundle, so its signal and shield cannot "
                                 "be paired - unify the two rows as a coax"),
                    QString(), w.id);
            } else {
                bool haveSignal = false, haveShield = false;
                for (const auto& o : def.wires) {
                    if (!harnessIsCoaxKind(o.kind) || o.bundle != w.bundle) continue;
                    if (o.color.startsWith(QLatin1String("SHIELD"))) haveShield = true;
                    else haveSignal = true;
                }
                if (!haveSignal || !haveShield)
                    add(QStringLiteral("coaxhalf:%1").arg(w.bundle),
                        QStringLiteral("Coax %1 has only its %2: a coax needs one Signal row and "
                                     "one Shield row")
                            .arg(w.bundle, haveShield ? QStringLiteral("shield")
                                                      : QStringLiteral("signal")),
                        QString(), w.id);
            }
        }

        // An OPEN end lands on nothing, so only the other end is checked. An end with no pin - pin 0 -
        // is open in exactly the same way: there is nothing there to be wrong about, and "P1 has no
        // pin 0" told the user off for a row the app itself had left unlanded.
        if (harnessEndIsOpen(w.fromConn, w.fromPin) || harnessEndIsOpen(w.toConn, w.toPin)) {
            const bool fromOpen = harnessEndIsOpen(w.fromConn, w.fromPin);
            if (fromOpen && harnessEndIsOpen(w.toConn, w.toPin)) {
                add(QStringLiteral("bothopen:%1").arg(w.id),
                    QStringLiteral("A wire is open at both ends, so it joins nothing"),
                    QString(), w.id);
                continue;
            }
            const HarnessConnector* c = harnessConnById(def, fromOpen ? w.toConn : w.fromConn);
            const int pin = fromOpen ? w.toPin : w.fromPin;
            if (harnessIsShieldEndpoint(fromOpen ? w.toConn : w.fromConn)) continue;
            if (!c)
                add(QStringLiteral("badconn:%1").arg(w.id),
                    QStringLiteral("An open-ended wire has no connector at its other end"),
                    QString(), w.id);
            else if (pin < 1 || pin > std::max(1, c->pinCount))
                add(QStringLiteral("badpin:%1:%2").arg(w.id).arg(pin),
                    QStringLiteral("%1 has no pin %2")
                        .arg(c->designator.isEmpty() ? c->id : c->designator).arg(pin),
                    c->id, w.id);
            continue;
        }
        // A drain wire ends on a SCREEN, not on a pin: it is checked against the shields the
        // harness has rather than against a connector's pins.
        const QStringList shields = harnessShieldEndpoints(def);
        bool shieldEnd = false;
        for (const QString& key : {w.fromConn, w.toConn}) {
            if (!harnessIsShieldEndpoint(key)) continue;
            shieldEnd = true;
            if (!shields.contains(key))
                add(QStringLiteral("badshield:%1").arg(w.id),
                    QStringLiteral("A wire drains to \"%1\", which the harness does not have")
                        .arg(harnessShieldEndpointLabel(def, key)),
                    QString(), w.id);
        }
        const HarnessConnector* a = harnessConnById(def, w.fromConn);
        const HarnessConnector* b = harnessConnById(def, w.toConn);
        if (shieldEnd) {
            // The other end still has to be a real pin.
            const HarnessConnector* pinSide = a ? a : b;
            const int pin = a ? w.fromPin : w.toPin;
            if (!pinSide) {
                add(QStringLiteral("badconn:%1").arg(w.id),
                    QStringLiteral("A drain wire has no connector end"), QString(), w.id);
            } else if (pin < 1 || pin > std::max(1, pinSide->pinCount)) {
                add(QStringLiteral("badpin:%1:%2").arg(w.id).arg(pin),
                    QStringLiteral("%1 has no pin %2")
                        .arg(pinSide->designator.isEmpty() ? pinSide->id : pinSide->designator)
                        .arg(pin),
                    pinSide->id, w.id);
            }
            continue;
        }
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

int harnessWireSpanPenalty(const HarnessDef& def) {
    int gaps = 0;
    for (const auto& c : def.connectors) {
        const std::vector<int> seq = harnessPinSequence(c);
        int first = -1, last = -1, wired = 0;
        for (int row = 0; row < static_cast<int>(seq.size()); ++row) {
            bool used = false;
            for (const auto& w : def.wires)
                if ((w.fromConn == c.id && w.fromPin == seq[row]) ||
                    (w.toConn == c.id && w.toPin == seq[row])) { used = true; break; }
            if (!used) continue;
            if (first < 0) first = row;
            last = row;
            ++wired;
        }
        // The spare pins caught inside the band the wires occupy.
        if (wired > 1) gaps += (last - first + 1) - wired;
    }
    return gaps;
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
           w.pinOrder * harnessPinOrderPenalty(def) +
           w.wireSpan * harnessWireSpanPenalty(def);
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

QJsonObject harnessDefToJson(const HarnessDef& def) {
    QJsonObject root;
    QJsonArray conns;
    for (const auto& c : def.connectors) {
        QJsonObject o;
        o["id"] = c.id;
        o["side"] = (c.side == HarnessSide::Right) ? QStringLiteral("right") : QStringLiteral("left");
        o["designator"] = c.designator;
        o["partNumber"] = c.partNumber;
        o["libPart"] = c.libPart;
        o["annotation"] = c.annotation;
        o["pinCount"] = c.pinCount;
        o["pitch"] = c.pitch;
        o["pinLength"] = c.pinLength;
        o["order"] = c.order;
        o["leg"] = c.leg;
        o["shieldLeft"] = c.shieldLeft;
        o["shieldRight"] = c.shieldRight;
        o["bodyBondLeft"] = c.bodyBondLeft;
        o["bodyBondRight"] = c.bodyBondRight;
        QJsonArray po;
        for (int p : c.pinOrder) po.append(p);
        o["pinOrder"] = po;
        conns.append(o);
    }
    QJsonArray wires;
    for (const auto& w : def.wires) {
        QJsonObject o;
        o["id"] = w.id;
        o["fromConn"] = w.fromConn;
        o["fromPin"] = w.fromPin;
        o["toConn"] = w.toConn;
        o["toPin"] = w.toPin;
        o["gauge"] = w.gauge;
        o["color"] = w.color;
        o["kind"] = w.kind;
        o["bundle"] = w.bundle;
        o["label"] = w.label;
        o["showColor"] = w.showColor;
        o["showGauge"] = w.showGauge;
        o["showLabel"] = w.showLabel;
        o["shieldGroup"] = w.shieldGroup;
        o["twistGroup"] = w.twistGroup;
        o["libPart"] = w.libPart;
        wires.append(o);
    }
    QJsonObject lay;
    lay["leftX"] = def.layout.leftX;
    lay["leftY"] = def.layout.leftY;
    lay["rightX"] = def.layout.rightX;
    lay["connectorGap"] = def.layout.connectorGap;
    lay["legGap"] = def.layout.legGap;
    lay["bridgeCrossings"] = def.layout.bridgeCrossings;
    lay["terminateUnconnected"] = def.layout.terminateUnconnected;
    lay["twistCount"] = def.layout.twistCount;
    lay["twistAmount"] = def.layout.twistAmount;
    lay["shieldAmount"] = def.layout.shieldAmount;
    lay["wireShieldAmount"] = def.layout.wireShieldAmount;
    lay["shieldLegLength"] = def.layout.shieldLegLength;
    lay["leadLength"] = def.layout.leadLength;
    lay["pinPitch"] = def.layout.pinPitch;
    lay["twistType"] = def.layout.twistType;
    lay["shieldType"] = def.layout.shieldType;
    lay["wireColorMode"] = def.layout.wireColorMode;
    lay["textGap"] = def.layout.textGap;
    lay["spliceSize"] = def.layout.spliceSize;
    lay["textHeight"] = def.layout.textHeight;
    lay["colorNameMode"] = def.layout.colorNameMode;
    lay["terminateUnconnectedWires"] = def.layout.terminateUnconnectedWires;
    lay["spliceAtJoins"] = def.layout.spliceAtJoins;
    root["format"] = QStringLiteral("edraw-harness-builder");
    // Version 2: an overall shield is drawn ONCE per connector by default. A version-1 file was
    // written before that was the default, so its stored 2 was nobody's choice - see the loader.
    // Version 3: each row says what it is (a wire, a coax...) and which cable it belongs to - before it, a cable or a
    // coax came back from a file, a preset or Generate's own preset as loose wires.
    root["version"] = 3;
    root["connectors"] = conns;
    root["wires"] = wires;
    root["layout"] = lay;
    return root;
}

HarnessDef harnessDefFromJson(const QJsonObject& root) {
    HarnessDef loaded;
    for (const auto& v : root.value("connectors").toArray()) {
        const QJsonObject o = v.toObject();
        HarnessConnector c;
        c.id = o.value("id").toString();
        c.side = o.value("side").toString() == QLatin1String("right") ? HarnessSide::Right
                                                                     : HarnessSide::Left;
        c.designator = o.value("designator").toString();
        c.partNumber = o.value("partNumber").toString();
        c.libPart = o.value("libPart").toString();
        c.annotation = o.value("annotation").toString(QStringLiteral("numbers"));
        c.pinCount = o.value("pinCount").toInt(4);
        c.pitch = o.value("pitch").toDouble(15.0);
        c.pinLength = o.value("pinLength").toDouble(40.0);
        c.order = o.value("order").toInt(0);
        c.leg = o.value("leg").toString();
        c.shieldLeft = o.value("shieldLeft").toBool(false);
        c.shieldRight = o.value("shieldRight").toBool(false);
        c.bodyBondLeft = o.value("bodyBondLeft").toBool(false);
        c.bodyBondRight = o.value("bodyBondRight").toBool(false);
        for (const auto& p : o.value("pinOrder").toArray()) c.pinOrder.push_back(p.toInt());
        if (!c.id.isEmpty()) loaded.connectors.push_back(c);
    }
    for (const auto& v : root.value("wires").toArray()) {
        const QJsonObject o = v.toObject();
        HarnessWire w;
        w.id = o.value("id").toString();
        w.fromConn = o.value("fromConn").toString();
        w.fromPin = o.value("fromPin").toInt(1);
        w.toConn = o.value("toConn").toString();
        w.toPin = o.value("toPin").toInt(1);
        w.gauge = o.value("gauge").toString(QStringLiteral("0.5"));
        w.color = o.value("color").toString(QStringLiteral("WH"));
        w.kind = o.value("kind").toString(QStringLiteral("wire"));
        w.bundle = o.value("bundle").toString();
        w.label = o.value("label").toString();
        w.showColor = o.value("showColor").toBool(false);
        w.showGauge = o.value("showGauge").toBool(false);
        w.showLabel = o.value("showLabel").toBool(true);
        w.shieldGroup = o.value("shieldGroup").toString();
        w.twistGroup = o.value("twistGroup").toString();
        w.libPart = o.value("libPart").toString();
        if (!w.id.isEmpty()) loaded.wires.push_back(w);
    }
    const QJsonObject lay = root.value("layout").toObject();
    loaded.layout.leftX = lay.value("leftX").toDouble(100.0);
    loaded.layout.leftY = lay.value("leftY").toDouble(300.0);
    loaded.layout.rightX = lay.value("rightX").toDouble(700.0);
    loaded.layout.connectorGap = lay.value("connectorGap").toDouble(30.0);
    loaded.layout.legGap = lay.value("legGap").toDouble(60.0);
    loaded.layout.bridgeCrossings = lay.value("bridgeCrossings").toBool(true);
    loaded.layout.terminateUnconnected = lay.value("terminateUnconnected").toBool(false);
    loaded.layout.twistCount = lay.value("twistCount").toInt(2);
    loaded.layout.twistAmount = lay.value("twistAmount").toInt(2);
    // An overall screen is drawn once unless asked for more. A pattern written before that was
    // the default stored the old 2 without anyone choosing it, so a version-1 file takes the new
    // default instead; from version 2 on, whatever the file says is what the user set.
    const int fileVersion = root.value(QStringLiteral("version")).toInt(1);
    loaded.layout.shieldAmount = fileVersion >= 2 ? lay.value("shieldAmount").toInt(1) : 1;
    loaded.layout.wireShieldAmount = lay.value("wireShieldAmount").toInt(2);
    loaded.layout.shieldLegLength = lay.value("shieldLegLength").toDouble(20.0);
    loaded.layout.leadLength = lay.value("leadLength").toDouble(0.0);
    loaded.layout.pinPitch = lay.value("pinPitch").toDouble(15.0);
    loaded.layout.twistType = lay.value("twistType").toString(QStringLiteral("jog"));
    loaded.layout.shieldType = lay.value("shieldType").toString(QStringLiteral("ellipse"));
    loaded.layout.wireColorMode =
        lay.value("wireColorMode").toString(QStringLiteral("bylayer"));
    loaded.layout.textGap = lay.value("textGap").toDouble(0.0);
    loaded.layout.spliceSize = lay.value("spliceSize").toDouble(3.0);
    loaded.layout.textHeight = lay.value("textHeight").toDouble(3.5);
    // A file from before colour names had a long form was drawn with the short ones, so a file that does not say
    // takes "short" - a builder opened afresh takes the long ones (HarnessLayout's own default).
    loaded.layout.colorNameMode =
        lay.value("colorNameMode").toString(QStringLiteral("short"));
    loaded.layout.terminateUnconnectedWires =
        lay.value("terminateUnconnectedWires").toBool(false);
    loaded.layout.spliceAtJoins = lay.value("spliceAtJoins").toBool(true);
    return loaded;
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
        // Which library part this connector is, so the bill of materials can order it.
        if (p.conn && !p.conn->libPart.isEmpty()) e.meta.insert("libPart", p.conn->libPart);
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
        // A coax's SHIELD row is the braid, not a run of its own: it is drawn as a stub into the
        // screen symbol on the signal (see the coax pass below), so it is not routed here.
        if (harnessIsCoaxKind(w->kind) && w->color.startsWith(QLatin1String("SHIELD")))
            continue;
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
        // A coax's signal is numbered when its cable holds more than one coax, so the two
        // conductors of each can be told apart on the drawing.
        {
            const int coaxNo = harnessCoaxIndex(def, *w);
            e.meta.insert("wireColor",
                          (coaxNo > 0 && w->color == QLatin1String("SIGNAL"))
                              ? QStringLiteral("SIGNAL%1").arg(coaxNo)
                              : w->color);
        }
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
        if (L.textGap > 0.0) e.meta.insert("textGap", L.textGap);
        // Each marking's own height above the wire, 10 apart: the cross-section nearest it, then
        // the colour, then the label.
        for (const QString& part : wireAnnotParts())
            setWireAnnotGapFor(e, part, L.textGap + wireAnnotDefaultGap(part));
        e.meta.insert("annSize", L.textHeight);
        e.meta.insert("colorNameMode", L.colorNameMode == QLatin1String("long")
                                          ? QStringLiteral("full")
                                          : QStringLiteral("code"));
        if (w->kind != QLatin1String("wire")) e.meta.insert("wireKind", w->kind);
        // Which definition row this run came from. The twists and screens name their wires by
        // that id, so this is what lets them be re-pointed at the real entities exactly.
        e.meta.insert("defWire", w->id);
        // Which library part this run is made of, so the bill of materials can order it.
        if (!w->libPart.isEmpty()) e.meta.insert("libPart", w->libPart);
        if (!w->bundle.isEmpty()) e.meta.insert("bundle", w->bundle);
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
    // The proportion between a screen's two radii. The large one follows the band of wires it
    // has to enclose, so the small one follows from it - a screen keeps the same shape whatever
    // it is drawn around, instead of going from a circle to a sliver as the band grows.
    constexpr double kShieldRatio = 3.0;
    // A leg that terminates on something reaches as far as the Layout says. One that reaches
    // NOTHING is a stub with an X on the end: it only has to be long enough to read as a leg, and
    // drawing it full length leaves a long line pointing at nothing.
    constexpr double kOpenLegLength = 5.0;
    auto placeScreen = [&](const std::vector<QString>& wireIds, double x, const QString& key,
                           bool overall, bool owns, Vec2& drainOut,
                           const Placed* body = nullptr, bool terminated = false) {
        std::vector<Vec2> cuts;
        QJsonArray refs;
        QString gauge;
        cutAt(wireIds, x, cuts, refs, gauge);
        if (cuts.empty()) return false;
        double yMin = cuts.front().y, yMax = cuts.front().y;
        for (const Vec2& c : cuts) { yMin = std::min(yMin, c.y); yMax = std::max(yMax, c.y); }
        // How far the outline reaches past the outermost wire it encloses: a quarter of the pin
        // pitch, so the clearance is in proportion to the harness rather than a fixed number of
        // units that looks tight at a wide pitch and loose at a narrow one. An overall screen
        // sits a little wider again, so an internal one nests visibly inside it.
        const double pitchRef = body ? body->spec.pitch : (laneStep > 0.0 ? laneStep : 15.0);
        const double clearance = std::max(1.0, pitchRef / 4.0);
        const double pad = overall ? clearance * 1.35 : clearance;
        const double ry = (yMax - yMin) / 2.0 + pad;
        const double rx = std::max(2.0, ry / kShieldRatio);
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
        const double legLenHere = terminated ? legLen : kOpenLegLength;
        const bool up = body && ((yMin + yMax) * 0.5) >=
                                    (body->geom.rectMin.y + body->geom.rectMax.y) * 0.5;
        if (up) {
            double y = yMax + pad + legLenHere;
            // A leg that goes somewhere is carried clear of the connector body on its way; a stub
            // that ends in an X has nowhere to go, so it stays the length it was given.
            if (body && terminated) y = std::max(y, body->geom.rectMax.y + legLenHere);
            drainOut = {x, y};
        } else {
            double y = yMin - pad - legLenHere;
            if (body && terminated) y = std::min(y, body->geom.rectMin.y - legLenHere);
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
        e.meta.insert("showS", false); // the outline says it is a screen; the letter is opt-in
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
    // The bond is a WIRE - the drain - not a decorative line: it carries the screen to the
    // shell, so it is drawn, counted and edited like the conductor it is.
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
        bond.type = EntityType::Wire;
        bond.layer = QStringLiteral("Wires");
        bond.points = {corner, {corner.x + inward * diag, drain.y}, drain};
        bond.meta.insert("shieldBond", key); // which screen's drain this is
        bond.meta.insert("drainWire", true);
        bond.meta.insert("wireColor", QStringLiteral("DRAIN"));
        bond.meta.insert("wireGauge", QStringLiteral("0.5"));
        if (L.wireColorMode == QLatin1String("bylayer"))
            bond.meta.insert("wireColorMode", QStringLiteral("bylayer"));
        else
            bond.color = wireColorHex(QStringLiteral("DRAIN"));
        out.entities.push_back(std::move(bond));
    };

    // ---- Flying leads: a wire with one OPEN end ----
    // The wire leaves its pin, runs out into the corridor and stops. It cannot go through the
    // ordinary routing, which needs a pin at both ends, so it is drawn here - and it is
    // terminated with the same X the unconnected-pin rule uses, because an end that reaches
    // nothing is exactly what that mark is for.
    //
    // This is done BEFORE the screens, and each lead is registered as a routed run, for one reason:
    // a screen is drawn around the wires it finds at its own x, and a lead that did not exist yet
    // was found by nothing. A cable whose screened pair came out as two flying leads therefore had
    // its screen silently skipped - the pair was drawn outside every screen it is actually inside.
    // Now a lead is enclosed by the same screens as any other conductor of its cable: its own, and
    // the overall one at the end it arrives at.
    for (const auto& w : def.wires) {
        // An end with no PIN is open too - see harnessEndIsOpen. Such a wire used to be drawn as
        // nothing at all: the routing needs a pin at both ends and quietly skipped it, so a conductor
        // the builder listed was missing from the drawing.
        const bool fromOpen = harnessEndIsOpen(w.fromConn, w.fromPin);
        const bool toOpen = harnessEndIsOpen(w.toConn, w.toPin);
        if (fromOpen == toOpen) continue; // both ends open is not a wire; neither is neither
        const QString connId = fromOpen ? w.toConn : w.fromConn;
        const int pin = fromOpen ? w.toPin : w.fromPin;
        const Placed* p = placedById(placed, connId);
        if (!p) continue;
        Vec2 pinPt;
        if (!pinEndOf(*p, pin, pinPt)) continue;
        // How far it reaches into the corridor. Far enough to be INSIDE the screens of the cable it
        // belongs to - a lead that stops short of them is drawn as though it were outside the
        // jacket - which means clearing the outermost overall screen symbol at this end, with room
        // past it for the group's own screen. A length given by hand wins: the lead is a flying
        // lead, and how long a flying lead is left is the loom maker's decision, not the drawing's.
        double reach = std::max(laneStep * 1.5, 20.0);
        if (L.leadLength > 0.0) {
            reach = L.leadLength;
        } else {
            // Clear of the outermost overall screen at this end AND of its width - a screen is an
            // ellipse, not a line, and a lead that stops level with its centre is drawn cut in half by
            // it - with room past that for the lead's own group to be screened without the two
            // outlines crossing.
            const double outermost = 25.0 + 34.0 * (std::max(1, L.shieldAmount) - 1);
            int arriving = 0;
            for (const auto& other : def.wires)
                if (other.fromConn == p->conn->id || other.toConn == p->conn->id) ++arriving;
            // The same shape placeScreen will give it: half the band it encloses, plus its padding,
            // divided by the outline's aspect.
            const double ry = std::max(1, arriving - 1) * p->spec.pitch * 0.5 + p->spec.pitch * 0.34;
            const double rx = std::max(2.0, ry / 3.0);
            // Room past the overall screen for the lead's own screen AND for a twist mark, if it is
            // one of a twisted pair: both are drawn along the lead, and a lead too short for them is a
            // pair that comes out neither screened nor twisted on the drawing.
            reach = std::max(reach, outermost + rx * 2.0 + std::max(30.0, p->spec.pitch * 2.0) +
                                        (w.twistGroup.isEmpty() ? 0.0 : p->spec.pitch * 2.5));
        }
        const double inward = (p->conn->side == HarnessSide::Left) ? 1.0 : -1.0;
        const Vec2 tip{pinPt.x + inward * reach, pinPt.y};
        Entity e;
        e.type = EntityType::Wire;
        e.layer = QStringLiteral("Wires");
        e.points = {pinPt, tip};
        e.meta.insert("wireColor", w.color);
        e.meta.insert("wireGauge", w.gauge);
        e.meta.insert("openEnd", true);
        // Which cable it is one of. A flying lead has nothing else to say what it belongs to: the
        // rest of the bundle is a screen and a jacket it is drawn beside rather than inside.
        if (!w.bundle.isEmpty()) e.meta.insert("bundle", w.bundle);
        if (!w.label.isEmpty()) e.meta.insert("wireLabel", w.label);
        e.meta.insert("showWireLabel", w.showLabel);
        e.meta.insert("showColorName", w.showColor);
        e.meta.insert("showGaugeName", w.showGauge);
        if (w.showColor || w.showGauge) e.meta.insert("annCount", 1);
        if (L.textGap > 0.0) e.meta.insert("textGap", L.textGap);
        // Each marking's own height above the wire, 10 apart: the cross-section nearest it, then
        // the colour, then the label.
        for (const QString& part : wireAnnotParts())
            setWireAnnotGapFor(e, part, L.textGap + wireAnnotDefaultGap(part));
        if (L.wireColorMode == QLatin1String("bylayer"))
            e.meta.insert("wireColorMode", QStringLiteral("bylayer"));
        else
            e.color = wireColorHex(w.color);
        out.entities.push_back(std::move(e));
        // Routed like any other run, so every screen that should catch this lead can find it.
        routed.push_back({w.id, out.entities.size() - 1});
        Entity x;
        x.type = EntityType::NotConnected;
        x.layer = QStringLiteral("Wires");
        x.a = tip;
        x.meta.insert("ncSize", std::max(2.0, p->spec.pitch * 0.3));
        out.entities.push_back(std::move(x));
        // The BUNDLE MARK: a band across the lead, in the marking orange, saying this loose end is
        // part of a cable and not a wire of its own. Two ticks, the way a bundle is banded by hand.
        if (!w.bundle.isEmpty()) {
            const double band = std::max(2.0, p->spec.pitch * 0.28);
            const double at = tip.x - inward * band * 2.2;   // just inboard of the terminated end
            for (int k = 0; k < 2; ++k) {
                Entity m;
                m.type = EntityType::Line;
                m.layer = QStringLiteral("Wires");
                m.color = QStringLiteral("#ff8c00");
                const double mx = at - inward * band * 0.8 * k;
                m.a = {mx, tip.y - band};
                m.b = {mx, tip.y + band};
                m.meta.insert("bundleMark", w.bundle);
                out.entities.push_back(std::move(m));
            }
        }
    }

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
        // Is anything going to land on this screen's leg? A bond to the body, or a wire the
        // definition runs to this very screen. If not, the leg is a short stub with an X.
        bool overallTerminated = wantsBond;
        {
            const QString endpointKey = harnessOverallShieldKey(p.conn->id, p.conn->side);
            for (const auto& w : def.wires)
                if (w.fromConn == endpointKey || w.toConn == endpointKey) overallTerminated = true;
        }
        bool haveDrain = false;
        Vec2 firstDrain{0.0, 0.0};
        for (int k = 0; k < std::max(1, L.shieldAmount); ++k) {
            Vec2 drain;
            if (!placeScreen(ids, baseX + inward * (25.0 + 34.0 * k), key, /*overall=*/true,
                             /*owns=*/k == 0, drain, &p, /*terminated=*/overallTerminated))
                continue;
            if (!haveDrain) { firstDrain = drain; haveDrain = true; }
        }
        if (haveDrain && wantsBond) {
            bondToBody(p, firstDrain, key);
            // Terminated on the drain: the X marker belongs on a leg that reaches nothing.
            for (Entity& e : out.entities)
                if (e.type == EntityType::Screen &&
                    e.meta.value("overallShield").toString() == key)
                    e.meta.insert("notConnected", false);
        }
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
        // How many symbols this screen gets. Twice - once near each end - for a group that runs
        // between two connectors, because that is where a screen is caught and terminated. But a group
        // whose wires are all FLYING LEADS has one end and a short run: two symbols on it are the same
        // screen drawn twice over a few tens of units, and they crowd out the twist mark the pair also
        // needs. Such a group is marked once.
        bool allLeads = true;
        for (const auto& w : def.wires) {
            if (w.shieldGroup != key) continue;
            if (!harnessIsOpenEndpoint(w.fromConn) && !harnessIsOpenEndpoint(w.toConn))
                allLeads = false;
        }
        const int amount = allLeads ? 1 : std::max(1, L.wireShieldAmount);
        // A screen must not be drawn on top of a screen already there. On a long run the two never
        // met; on a SHORT one - a screened pair that comes out as a pair of flying leads, whose whole
        // span is a few tens of units - the group's symbols land right on the overall screen at that
        // end and the two outlines cross into an unreadable knot. Each symbol is nudged along the run
        // until it is clear of the ones already placed.
        auto clearOf = [&out](double x, double margin) {
            for (int guard = 0; guard < 8; ++guard) {
                bool moved = false;
                for (const Entity& e : out.entities) {
                    if (e.type != EntityType::Screen || e.points.empty()) continue;
                    // Its own half-width, so the outlines do not cross rather than merely the centres
                    // not coinciding.
                    const double keepAway = e.meta.value("rx").toDouble() + margin;
                    if (std::abs(e.points.front().x - x) < keepAway) {
                        x = e.points.front().x + keepAway;
                        moved = true;
                    }
                }
                if (!moved) break;
            }
            return x;
        };
        for (int k = 0; k < amount; ++k) {
            double x = xa + (xb - xa) * (k + 1.0) / (amount + 1.0);
            // Kept inside the group's own run: a screen drawn past the end of the wires it encloses
            // encloses nothing.
            const double keepAway = std::max(6.0, (xb - xa) * 0.12);
            x = std::min(clearOf(x, keepAway), xb - keepAway * 0.5);
            Vec2 drain;
            bool innerTerminated = false;
            {
                const QString endpointKey = harnessInternalShieldKey(key);
                for (const auto& w : def.wires)
                    if (w.fromConn == endpointKey || w.toConn == endpointKey)
                        innerTerminated = true;
            }
            placeScreen(ids, x, key, /*overall=*/false, /*owns=*/k == 0, drain, nullptr,
                        /*terminated=*/innerTerminated);
        }
    }

    // ---- Coax: a signal with its braid, drawn as a screen on the signal ----
    // A coax is a cable of TWO conductors, given as two rows of one bundle: a SIGNAL and a
    // SHIELD. The signal runs pin to pin like any wire. The screen is not a second line beside
    // it - it is the braid AROUND it - so it is drawn as a screen symbol on the signal near each
    // end, and the shield row's own conductor is brought from its pin along and up into that
    // symbol. The shield row is therefore not routed pin to pin; it is this stub at each end.
    {
        std::vector<Entity> extra;
        // The coax bundles, and which row of each is the signal and which the shield.
        QStringList bundles;
        for (const auto& w : def.wires) {
            if (!harnessIsCoaxKind(w.kind) || w.bundle.isEmpty()) continue;
            if (!bundles.contains(w.bundle)) bundles << w.bundle;
        }
        for (const QString& bundle : bundles) {
            const HarnessWire* signal = nullptr;
            const HarnessWire* shield = nullptr;
            for (const auto& w : def.wires) {
                if (!harnessIsCoaxKind(w.kind) || w.bundle != bundle) continue;
                if (w.color.startsWith(QLatin1String("SHIELD"))) shield = &w;
                else signal = &w; // a row that is not the shield is the signal
            }
            if (!signal || !shield) continue; // half a coax cannot be drawn; the validator says so
            // The signal as it was routed.
            int sigIdx = -1;
            for (const Routed& r : routed)
                if (r.wireId == signal->id) sigIdx = r.entityIndex;
            if (sigIdx < 0 || out.entities[sigIdx].points.size() < 2) continue;
            const std::vector<Vec2> sigRun = out.entities[sigIdx].points;
            const int coaxNo = harnessCoaxIndex(def, *signal);
            // Only a coax-splice reaches the harness through a splice. A plain coax runs pin to
            // pin as one cable, so nothing is cut and no joint is drawn.
            const bool splices = harnessCoaxSplices(def, bundle);
            // Where each end's splice landed, so the conductors can be cut there afterwards.
            std::vector<std::pair<int, double>> cutAtEnd; // end index, x

            // One braid symbol per END, just inboard of the connector the shield row lands on.
            for (int end = 0; end < 2; ++end) {
                const QString connId = end == 0 ? shield->fromConn : shield->toConn;
                const int pin = end == 0 ? shield->fromPin : shield->toPin;
                const Placed* p = placedById(placed, connId);
                if (!p) continue;
                Vec2 pinPt;
                if (!pinEndOf(*p, pin, pinPt)) continue;
                const double inward = (p->conn->side == HarnessSide::Left) ? 1.0 : -1.0;
                const double baseX = p->geom.pinEnd.empty() ? p->origin.x
                                                            : p->geom.pinEnd.front().x;
                // The splice sits this far outboard of the braid: pins, splice, then cable. Far
                // enough that the splice housing and the braid do not touch.
                const double spliceGap =
                    std::max(14.0, p->spec.pitch * 0.9) + std::max(0.0, L.spliceSize) * 2.2;
                // Neither may land on a screen already drawn at this end (the connector's own
                // overall screen sits in the same place the braid would like to go).
                auto blocked = [&out](double x) {
                    for (const Entity& e : out.entities) {
                        if (e.type != EntityType::Screen || e.points.empty()) continue;
                        const int k = std::min(e.meta.value("catch").toInt(
                                                   static_cast<int>(e.points.size())),
                                               static_cast<int>(e.points.size()));
                        double cx = 0.0;
                        for (int i = 0; i < k; ++i) cx += e.points[i].x;
                        if (k > 0) cx /= k;
                        if (std::abs(cx - x) < e.meta.value("rx").toDouble(12.0) + 4.0) return true;
                    }
                    return false;
                };
                double screenX = baseX + inward * std::max(20.0, p->spec.pitch * 1.6);
                for (int guard = 0; guard < 60; ++guard) {
                    if (!blocked(screenX) && !blocked(screenX - inward * spliceGap)) break;
                    screenX += inward * 4.0;
                }
                // Where the signal runs at that x - the braid sits ON the signal.
                bool onSignal = false;
                double sigY = sigRun.front().y;
                for (size_t i = 1; i < sigRun.size(); ++i) {
                    const Vec2 a = sigRun[i - 1], b = sigRun[i];
                    if (screenX < std::min(a.x, b.x) - 1e-9 || screenX > std::max(a.x, b.x) + 1e-9)
                        continue;
                    const double t = (std::abs(b.x - a.x) < 1e-9) ? 0.0
                                                                 : (screenX - a.x) / (b.x - a.x);
                    sigY = a.y + (b.y - a.y) * t;
                    onSignal = true;
                    break;
                }
                if (!onSignal) continue;
                const Vec2 catchPt{screenX, sigY};

                // The braid: a screen catching the signal, drawn where it crosses.
                Entity sc;
                sc.type = EntityType::Screen;
                sc.layer = QStringLiteral("Wires");
                sc.points = {catchPt};
                sc.meta.insert("catch", 1);
                QJsonArray refs;
                refs.append(signal->id); // re-pointed at the real entity id when stamped
                sc.meta.insert("wires", end == 0 ? refs : QJsonArray());
                // Same proportion as every other screen: the small radius follows the large one.
                sc.meta.insert("ry", std::max(5.0, p->spec.pitch * 0.42));
                sc.meta.insert("rx", std::max(2.0, std::max(5.0, p->spec.pitch * 0.42) / 3.0));
                sc.meta.insert("rot", 1.5707963267948966);
                sc.meta.insert("shape", QStringLiteral("ellipse"));
                sc.meta.insert("lineType", QStringLiteral("solid"));
                sc.meta.insert("legDir", QStringLiteral("bottom"));
                sc.meta.insert("showS", false);
                sc.meta.insert("notConnected", false); // the shield conductor terminates it
                sc.meta.insert("coaxScreenOf", signal->id);
                if (coaxNo > 0) sc.meta.insert("coaxNumber", coaxNo);
                extra.push_back(sc);

                // The shield conductor: out of its pin, along, and up to the BRAID - stopping on
                // the screen's outline, not carrying on to the signal centreline. The braid is
                // what it joins; running it into the signal would draw the screen shorted to the
                // conductor it screens.
                const double ryHere = std::max(5.0, p->spec.pitch * 0.42);
                const bool fromBelow = pinPt.y < sigY;
                const Vec2 joinPt{screenX, sigY + (fromBelow ? -ryHere : ryHere)};
                const double spliceX = screenX - inward * spliceGap;
                Entity sh;
                sh.type = EntityType::Wire;
                sh.layer = QStringLiteral("Wires");
                // With a splice, the cable's own shield conductor starts AT the joint - the run
                // from the pin to the joint is a separate harness wire, added below.
                const Vec2 shieldStart = splices ? Vec2{spliceX, pinPt.y} : pinPt;
                if (std::abs(shieldStart.y - joinPt.y) < 1e-6) sh.points = {shieldStart, joinPt};
                else sh.points = {shieldStart, {screenX, shieldStart.y}, joinPt};
                sh.meta.insert("wireColor", coaxNo > 0
                                                ? QStringLiteral("SHIELD%1").arg(coaxNo)
                                                : QStringLiteral("SHIELD"));
                sh.meta.insert("wireGauge", shield->gauge);
                sh.meta.insert("wireKind", QStringLiteral("coaxShield"));
                // The cable's shield section IS the shield row, at this end.
                if (end == 0) sh.meta.insert("defWire", shield->id);
                sh.meta.insert("coaxScreenOf", signal->id);
                sh.meta.insert("bundle", bundle);
                if (!shield->label.isEmpty()) sh.meta.insert("wireLabel", shield->label);
                sh.meta.insert("showWireLabel", shield->showLabel);
                sh.meta.insert("showColorName", shield->showColor);
                sh.meta.insert("showGaugeName", shield->showGauge);
                if (shield->showColor || shield->showGauge) sh.meta.insert("annCount", 1);
                if (L.textGap > 0.0) sh.meta.insert("textGap", L.textGap);
                for (const QString& part : wireAnnotParts())
                    setWireAnnotGapFor(sh, part, L.textGap + wireAnnotDefaultGap(part));
                sh.meta.insert("annSize", L.textHeight);
                if (L.wireColorMode == QLatin1String("bylayer"))
                    sh.meta.insert("wireColorMode", QStringLiteral("bylayer"));
                else
                    sh.color = wireColorHex(QStringLiteral("SHIELD"));
                extra.push_back(sh);

                // Where the cable joins the harness, both of its conductors are spliced - so the
                // splice is ONE part with two joints: the signal above, the shield below, tied by
                // a bar. It sits between the pins and the braid, which is where the coax proper
                // begins.
                if (splices) {
                    bool onRun = false;
                    double spliceY = sigY;
                    for (size_t i = 1; i < sigRun.size(); ++i) {
                        const Vec2 ra = sigRun[i - 1], rb = sigRun[i];
                        if (spliceX < std::min(ra.x, rb.x) - 1e-9 ||
                            spliceX > std::max(ra.x, rb.x) + 1e-9)
                            continue;
                        const double t = (std::abs(rb.x - ra.x) < 1e-9)
                                             ? 0.0
                                             : (spliceX - ra.x) / (rb.x - ra.x);
                        spliceY = ra.y + (rb.y - ra.y) * t;
                        onRun = true;
                        break;
                    }
                    // Only when the shield really runs alongside there: a shield leaving from a
                    // pin level with the signal has no second joint to draw.
                    if (onRun && std::abs(pinPt.y - spliceY) > 1e-6) {
                        Entity sp;
                        sp.type = EntityType::Splice;
                        sp.layer = QStringLiteral("Wires");
                        sp.a = {spliceX, spliceY};
                        sp.b = {spliceX, pinPt.y};
                        sp.meta.insert("pair", true);
                        // A cable transition is a small joint whatever the general splice size:
                        // two of them plus the housing would swamp the cable at size 3.
                        sp.meta.insert("size", 1.0);
                        // The body around each joint is sized from the connector pitch, so a small
                        // joint still reads as a splice instead of collapsing into its own dot.
                        sp.meta.insert("houseRx", std::max(3.0, p->spec.pitch * 0.22));
                        sp.meta.insert("houseRy", std::max(2.0, p->spec.pitch * 0.15));
                        sp.meta.insert("spliceText", QStringLiteral("JOINT"));
                        sp.meta.insert("textSize", L.textHeight);
                        sp.meta.insert("bundle", bundle);
                        sp.meta.insert("coaxScreenOf", signal->id);
                        if (coaxNo > 0) sp.meta.insert("coaxNumber", coaxNo);
                        extra.push_back(sp);
                        cutAtEnd.push_back({end, spliceX});

                        // The pigtail: pin to the joint, on the shield's own level. It is a wire
                        // in its own right - not part of the cable - so it carries no bundle and
                        // keeps its own colour and gauge.
                        Entity tail;
                        tail.type = EntityType::Wire;
                        tail.layer = QStringLiteral("Wires");
                        tail.points = {pinPt, {spliceX, pinPt.y}};
                        tail.meta.insert("wireColor", shield->color);
                        tail.meta.insert("wireGauge", shield->gauge);
                        tail.meta.insert("annSize", L.textHeight);
                        if (L.textGap > 0.0) tail.meta.insert("textGap", L.textGap);
                        for (const QString& part : wireAnnotParts())
                            setWireAnnotGapFor(tail, part,
                                               L.textGap + wireAnnotDefaultGap(part));
                        if (L.wireColorMode == QLatin1String("bylayer"))
                            tail.meta.insert("wireColorMode", QStringLiteral("bylayer"));
                        else
                            tail.color = wireColorHex(shield->color);
                        extra.push_back(tail);
                    }
                }
            }

            // ---- Past the splice the signal is a separate wire too ----
            // The cable is what runs BETWEEN the two joints; the stretch from each pin out to its
            // joint is an ordinary harness wire. Cutting the routed signal leaves the cable
            // section in place (the braid and the splices already point at it) and adds the
            // pigtails as wires of their own.
            for (const auto& cut : cutAtEnd) {
                std::vector<Vec2>& run = out.entities[sigIdx].points;
                if (run.size() < 2) continue;
                const bool head = cut.first == 0; // end 0 is the run's first vertex
                const size_t i = head ? 0 : run.size() - 2;
                const Vec2 a = run[i], b = run[i + 1];
                const double x = cut.second;
                if (x < std::min(a.x, b.x) + 1e-9 || x > std::max(a.x, b.x) - 1e-9)
                    continue; // the joint is not on this end's own segment; leave the run whole
                const double t = (x - a.x) / (b.x - a.x);
                const Vec2 at{x, a.y + (b.y - a.y) * t};
                Entity tail = out.entities[sigIdx];
                tail.id.clear();
                tail.points = head ? std::vector<Vec2>{a, at} : std::vector<Vec2>{at, b};
                tail.meta.remove(QStringLiteral("bundle")); // a pigtail is not part of the cable
                tail.meta.remove(QStringLiteral("wireKind"));
                // A pigtail is a new conductor, not the definition's row: leaving the row's id on
                // it would let a twist or a screen catch the pigtail instead of the cable.
                tail.meta.remove(QStringLiteral("defWire"));
                tail.meta.insert("wireColor", signal->color);
                // The markings stay on the cable itself, so the same colour and gauge are not
                // printed three times over one conductor. They can be switched on per wire.
                tail.meta.insert("showColorName", false);
                tail.meta.insert("showGaugeName", false);
                tail.meta.insert("showWireLabel", false);
                extra.push_back(tail);
                if (head) run[0] = at;
                else run[run.size() - 1] = at;
            }
        }
        for (Entity& e : extra) out.entities.push_back(std::move(e));
    }

    // ---- Drain wires: a wire whose end is a SHIELD ----
    // A wire can be defined pin-to-shield, which is what a drain is. It cannot be routed with
    // the others because it needs the screen's leg, and the screens are placed off the wires
    // they catch - so it is routed here, once every leg exists. The wire is brought to the leg
    // and joined to it with a splice, the way a drain is terminated.
    for (const auto& w : def.wires) {
        const bool fromShield = harnessIsShieldEndpoint(w.fromConn);
        const bool toShield = harnessIsShieldEndpoint(w.toConn);
        if (fromShield == toShield) continue; // pin-to-pin (routed above), or shield-to-shield
        const QString shieldKey = fromShield ? w.fromConn : w.toConn;
        const QString connId = fromShield ? w.toConn : w.fromConn;
        const int pin = fromShield ? w.toPin : w.fromPin;
        const Placed* p = placedById(placed, connId);
        if (!p) continue;
        Vec2 pinPt;
        if (!pinEndOf(*p, pin, pinPt)) continue;
        // The leg of the screen this drains: the internal group by name, the overall one by the
        // connector and end it belongs to. The NEAREST leg wins when a screen was placed more
        // than once, so the drain takes the shortest way to it.
        const QString group = harnessShieldEndpointGroup(shieldKey);
        QString wantConn;
        HarnessSide wantSide = HarnessSide::Left;
        const bool overall = harnessShieldEndpointConn(shieldKey, wantConn, wantSide);
        const HarnessConnector* wantC = overall ? harnessConnById(def, wantConn) : nullptr;
        const QString overallName =
            wantC ? (wantC->designator.isEmpty() ? wantC->id : wantC->designator) : QString();
        Vec2 leg{0.0, 0.0};
        bool haveLeg = false;
        for (const Entity& e : out.entities) {
            if (e.type != EntityType::Screen || e.points.empty()) continue;
            const bool match = overall
                                   ? e.meta.value("overallShield").toString() == overallName
                                   : e.meta.value("shieldGroup").toString() == group;
            if (!match) continue;
            const int k = std::min(e.meta.value("catch").toInt(
                                       static_cast<int>(e.points.size())),
                                   static_cast<int>(e.points.size()));
            if (static_cast<int>(e.points.size()) <= k) continue;
            const Vec2 cand = e.points.back();
            if (!haveLeg || distance(cand, pinPt) < distance(leg, pinPt)) {
                leg = cand;
                haveLeg = true;
            }
        }
        if (!haveLeg) continue;
        Entity e;
        e.type = EntityType::Wire;
        e.layer = QStringLiteral("Wires");
        // Out of the pin, then up or down to the leg: a right-angled run like every other wire.
        if (std::abs(pinPt.y - leg.y) < 1e-6) e.points = {pinPt, leg};
        else e.points = {pinPt, {leg.x, pinPt.y}, leg};
        e.meta.insert("wireColor", w.color);
        e.meta.insert("wireGauge", w.gauge);
        e.meta.insert("shieldLeg", shieldKey); // which screen's leg it lands on
        e.meta.insert("drainWire", true);
        if (!w.label.isEmpty()) e.meta.insert("wireLabel", w.label);
        e.meta.insert("showWireLabel", w.showLabel);
        e.meta.insert("showColorName", w.showColor);
        e.meta.insert("showGaugeName", w.showGauge);
        if (w.showColor || w.showGauge) e.meta.insert("annCount", 1);
        if (L.textGap > 0.0) e.meta.insert("textGap", L.textGap);
        // Each marking's own height above the wire, 10 apart: the cross-section nearest it, then
        // the colour, then the label.
        for (const QString& part : wireAnnotParts())
            setWireAnnotGapFor(e, part, L.textGap + wireAnnotDefaultGap(part));
        if (L.wireColorMode == QLatin1String("bylayer"))
            e.meta.insert("wireColorMode", QStringLiteral("bylayer"));
        else
            e.color = wireColorHex(w.color);
        out.entities.push_back(std::move(e));
        // The joint on the leg is placed by the splice pass at the end. The screen, though, is
        // no longer a leg that reaches nothing.
        for (Entity& sc : out.entities) {
            if (sc.type != EntityType::Screen || sc.points.empty()) continue;
            const bool match = overall
                                   ? sc.meta.value("overallShield").toString() == overallName
                                   : sc.meta.value("shieldGroup").toString() == group;
            if (match && distance(sc.points.back(), leg) < 1e-6)
                sc.meta.insert("notConnected", false);
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
                // How far the symbol will reach along the wires, from the same formula the
                // renderer uses - everything below is judged against the WHOLE symbol, not just
                // its centre.
                double band = 0.0;
                for (const Vec2& c1 : cuts)
                    for (const Vec2& c2 : cuts) band = std::max(band, std::abs(c1.y - c2.y));
                const double half = twistBandWidth(std::max(1, L.twistCount), band, 6.0) / 2.0;
                // The symbol takes over a stretch of straight wire, so that whole stretch has to
                // BE straight on every wire of the group: a corner inside it would be drawn as a
                // wire running out of the braid into nothing.
                bool overCorner = false;
                for (const Routed& r : routed) {
                    if (std::find(ids.begin(), ids.end(), r.wireId) == ids.end()) continue;
                    const std::vector<Vec2>& run = out.entities[r.entityIndex].points;
                    for (size_t i = 1; i + 1 < run.size(); ++i)
                        if (std::abs(run[i].x - x) < half + 2.0) overCorner = true;
                }
                if (overCorner) continue;
                // Keep clear of the screens already placed: a braid drawn over a screen hides the
                // crossings the twist is there to show. Judged against each screen's own width
                // plus the symbol's reach, so the two never touch.
                bool underScreen = false;
                for (const Entity& sc : out.entities) {
                    if (sc.type != EntityType::Screen || sc.points.empty()) continue;
                    const int k = std::min(sc.meta.value("catch").toInt(
                                               static_cast<int>(sc.points.size())),
                                           static_cast<int>(sc.points.size()));
                    double cx = 0.0;
                    for (int i = 0; i < k; ++i) cx += sc.points[i].x;
                    if (k > 0) cx /= k;
                    const double reach = sc.meta.value("rx").toDouble(12.0) + half + 2.0;
                    if (std::abs(cx - x) < reach) { underScreen = true; break; }
                }
                if (underScreen) continue;
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

    // ---- Unconnected wire ends ----
    // The other half of the same rule: a wire END that reaches no pin. It happens when a wire
    // names a pin the connector does not have, or names no connector at all - the validator
    // reports it, and with this rule on the drawing marks it too, so the fault is visible on
    // the sheet rather than only in the issue list.
    if (L.terminateUnconnectedWires) {
        std::vector<Vec2> pinEnds;
        for (const Placed& p : placed)
            for (const Vec2& pe : p.geom.pinEnd) pinEnds.push_back(pe);
        // A conductor cut at a cable transition ends ON its splice, which is a termination like
        // any other - marking it open would put an X on a joint.
        for (const Entity& e : out.entities) {
            if (e.type != EntityType::Splice) continue;
            pinEnds.push_back(e.a);
            if (e.meta.value("pair").toBool(false)) pinEnds.push_back(e.b);
        }
        auto onAPin = [&pinEnds](const Vec2& pt) {
            for (const Vec2& pe : pinEnds)
                if (std::abs(pe.x - pt.x) < 1e-6 && std::abs(pe.y - pt.y) < 1e-6) return true;
            return false;
        };
        for (const Routed& r : routed) {
            const Entity& we = out.entities[r.entityIndex];
            if (we.points.size() < 2) continue;
            for (const Vec2& tip : {we.points.front(), we.points.back()}) {
                if (onAPin(tip)) continue;
                Entity e;
                e.type = EntityType::NotConnected;
                e.layer = QStringLiteral("Wires");
                e.a = tip;
                e.meta.insert("ncSize", 3.0);
                out.entities.push_back(std::move(e));
            }
        }
    }

    // ---- Splices: every point where wires actually MEET ----
    // A joint is where one conductor joins another, and that is the only place a dot belongs.
    // Judged from the finished drawing, over EVERY wire in it - the pin-to-pin runs, the drains,
    // a coax screen and its bond alike - so nothing is missed and nothing is invented:
    //   - two wire ENDS at the same point, or
    //   - one wire's END landing on another wire's RUN (a T-junction).
    // A connector PIN is not a joint: two wires on one pin is a wiring fault, which the
    // validator reports; marking it as a splice would say the drawing is fine.
    if (L.spliceAtJoins) {
        std::vector<const Entity*> wires;
        std::vector<Vec2> legEnds; // a screen's drain leg: a conductor end that is not a Wire
        // A screen whose drain is bonded to the connector BODY is already terminated there. A wire
        // brought to the same leg is joining a conductor that has an end of its own, so the joint
        // is the bond - putting a splice dot on top of it says there are two joints where there is
        // one. Those legs are left out of the meeting-point sweep below.
        std::vector<Vec2> bondedLegs;
        for (const Entity& e : out.entities)
            if (e.type == EntityType::Wire && e.meta.contains(QStringLiteral("shieldBond")) &&
                e.points.size() >= 2)
                bondedLegs.push_back(e.points.back());
        for (const Entity& e : out.entities) {
            if (e.type == EntityType::Wire && e.points.size() >= 2) {
                wires.push_back(&e);
                continue;
            }
            if (e.type != EntityType::Screen || e.points.empty()) continue;
            const int k = std::min(e.meta.value("catch").toInt(
                                       static_cast<int>(e.points.size())),
                                   static_cast<int>(e.points.size()));
            if (static_cast<int>(e.points.size()) > k) {
                bool bonded = false;
                for (const Vec2& bl : bondedLegs)
                    if (std::abs(bl.x - e.points.back().x) < 1e-6 &&
                        std::abs(bl.y - e.points.back().y) < 1e-6)
                        bonded = true;
                if (!bonded) legEnds.push_back(e.points.back());
            }
        }
        std::vector<Vec2> pinEnds;
        for (const Placed& p : placed)
            for (const Vec2& pe : p.geom.pinEnd) pinEnds.push_back(pe);
        auto same = [](const Vec2& a, const Vec2& b) {
            return std::abs(a.x - b.x) < 1e-6 && std::abs(a.y - b.y) < 1e-6;
        };
        auto onAPin = [&pinEnds, &same](const Vec2& pt) {
            for (const Vec2& pe : pinEnds)
                if (same(pe, pt)) return true;
            return false;
        };
        // A point strictly INSIDE a segment (an end that merely continues its own wire, or one
        // that lands on a neighbour's corner, is not a T-junction).
        auto insideSegment = [](const Vec2& pt, const Vec2& a, const Vec2& b) {
            const double len = distance(a, b);
            if (len < 1e-9) return false;
            const double cross = std::abs((b.x - a.x) * (pt.y - a.y) - (b.y - a.y) * (pt.x - a.x));
            if (cross / len > 1e-6) return false;
            const double t = ((pt.x - a.x) * (b.x - a.x) + (pt.y - a.y) * (b.y - a.y)) / (len * len);
            return t > 1e-6 && t < 1.0 - 1e-6;
        };
        // The coax assembly already carries its own splice; a plain dot on either of its joints
        // would read as a second splice on the same conductors.
        std::vector<Vec2> joints;
        for (const Entity& e : out.entities)
            if (e.type == EntityType::Splice && e.meta.value("pair").toBool(false)) {
                bondedLegs.push_back(e.a);
                bondedLegs.push_back(e.b);
            }
        auto note = [&joints, &same, &bondedLegs](const Vec2& pt) {
            // A leg bonded to the connector body is already terminated there: the bond IS the
            // joint, so a dot on top of it would claim two joints where there is one.
            for (const Vec2& bl : bondedLegs)
                if (same(bl, pt)) return;
            for (const Vec2& j : joints)
                if (same(j, pt)) return;
            joints.push_back(pt);
        };
        for (size_t i = 0; i < wires.size(); ++i) {
            for (int end = 0; end < 2; ++end) {
                const Vec2 tip = end == 0 ? wires[i]->points.front() : wires[i]->points.back();
                if (onAPin(tip)) continue;
                // A wire brought onto a screen's leg is joined to the screen there.
                bool metLeg = false;
                for (const Vec2& leg : legEnds)
                    if (same(leg, tip)) { metLeg = true; break; }
                if (metLeg) {
                    note(tip);
                    continue;
                }
                for (size_t j = 0; j < wires.size(); ++j) {
                    if (i == j) continue;
                    if (same(wires[j]->points.front(), tip) ||
                        same(wires[j]->points.back(), tip)) {
                        note(tip);
                        break;
                    }
                    bool onRun = false;
                    for (size_t sg = 1; sg < wires[j]->points.size(); ++sg)
                        if (insideSegment(tip, wires[j]->points[sg - 1], wires[j]->points[sg])) {
                            onRun = true;
                            break;
                        }
                    if (onRun) {
                        note(tip);
                        break;
                    }
                }
            }
        }
        for (const Vec2& j : joints) {
            Entity e;
            e.type = EntityType::Splice;
            e.layer = QStringLiteral("Wires");
            e.a = j;
            e.meta.insert("size", L.spliceSize);
            out.entities.push_back(std::move(e));
        }
    }
    return out;
}

} // namespace Edraw_Desktop
