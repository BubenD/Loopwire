#include "harness/BrickHarness.h"

#include "geometry/Geometry.h"
#include "harness/MechanicalHarness.h"
#include "tools/EntityTransforms.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>

namespace Edraw_Desktop {

// ---- Names ---------------------------------------------------------------------------------------

QString brickTypeName(BrickType type) {
    switch (type) {
    case BrickType::End: return QStringLiteral("End part");
    case BrickType::Fanout: return QStringLiteral("Fanout");
    case BrickType::Run: return QStringLiteral("Cable / heatshrink run");
    case BrickType::TwistedPair: return QStringLiteral("Twisted pair");
    case BrickType::TwistedBundle: return QStringLiteral("Twisted bundle");
    }
    return QString();
}

QString brickTypeKey(BrickType type) {
    switch (type) {
    case BrickType::End: return QStringLiteral("end");
    case BrickType::Fanout: return QStringLiteral("fanout");
    case BrickType::Run: return QStringLiteral("run");
    case BrickType::TwistedPair: return QStringLiteral("twisted-pair");
    case BrickType::TwistedBundle: return QStringLiteral("twisted-bundle");
    }
    return QStringLiteral("end");
}

BrickType brickTypeFromKey(const QString& key) {
    for (BrickType t : {BrickType::End, BrickType::Fanout, BrickType::Run, BrickType::TwistedPair,
                        BrickType::TwistedBundle})
        if (brickTypeKey(t) == key) return t;
    return BrickType::End;
}

std::vector<PartKind> brickEndKinds() {
    return {PartKind::Connector, PartKind::Ferrule, PartKind::Crimp, PartKind::Terminal,
            PartKind::ContactPin};
}

std::vector<PartKind> brickJointKinds() {
    return {PartKind::Splice,     PartKind::Resistor,    PartKind::Heatshrink, PartKind::YSplit,
            PartKind::Capacitor,  PartKind::Inductor,    PartKind::FerriteBead,
            PartKind::HeatshrinkBoot};
}

bool brickKindIsJoint(PartKind kind) {
    const std::vector<PartKind> j = brickJointKinds();
    return std::find(j.begin(), j.end(), kind) != j.end();
}

QString BrickRunSpec::outermost() const {
    for (int i = stack.size() - 1; i >= 0; --i)
        if (stack[i] != QLatin1String("wires")) return stack[i];
    return QStringLiteral("wires");
}

// ---- Ports ---------------------------------------------------------------------------------------

Vec2 brickPortPaneDir(const Brick& b, int port) {
    Vec2 d = port == kPortLeft ? Vec2{-1.0, 0.0} : Vec2{1.0, 0.0};
    for (int i = 0; i < ((b.rot % 4) + 4) % 4; ++i) d = Vec2{-d.y, d.x};   // a quarter clockwise
    return d;
}

Vec2 brickPortWorldDir(const Brick& b, int port) {
    const Vec2 d = brickPortPaneDir(b, port);
    return Vec2{d.x, -d.y};
}

// ---- The design ----------------------------------------------------------------------------------

Brick* BrickDesign::brick(const QString& id) {
    for (Brick& b : bricks)
        if (b.id == id) return &b;
    return nullptr;
}

const Brick* BrickDesign::brick(const QString& id) const {
    for (const Brick& b : bricks)
        if (b.id == id) return &b;
    return nullptr;
}

QString BrickDesign::addBrick(BrickType type, PartKind kind, double col, double row) {
    int next = 1;
    for (const Brick& b : bricks)
        if (b.id.startsWith(QLatin1Char('B'))) next = std::max(next, b.id.mid(1).toInt() + 1);
    Brick b;
    b.id = QStringLiteral("B%1").arg(next);
    b.type = type;
    b.kind = kind;
    b.col = col;
    b.row = row;
    if (type == BrickType::TwistedPair) b.run.wires = 2;
    if (type == BrickType::Run) b.run.stack = {QStringLiteral("wires"), QStringLiteral("cable")};
    bricks.push_back(b);
    return b.id;
}

void BrickDesign::removeBrick(const QString& id) {
    bricks.erase(std::remove_if(bricks.begin(), bricks.end(),
                                [&](const Brick& b) { return b.id == id; }),
                 bricks.end());
    links.erase(std::remove_if(links.begin(), links.end(),
                               [&](const BrickLink& l) { return l.a == id || l.b == id; }),
                links.end());
}

bool BrickDesign::linked(const QString& a, const QString& b) const { return link(a, b) != nullptr; }

BrickLink* BrickDesign::link(const QString& a, const QString& b) {
    for (BrickLink& l : links)
        if ((l.a == a && l.b == b) || (l.a == b && l.b == a)) return &l;
    return nullptr;
}

const BrickLink* BrickDesign::link(const QString& a, const QString& b) const {
    for (const BrickLink& l : links)
        if ((l.a == a && l.b == b) || (l.a == b && l.b == a)) return &l;
    return nullptr;
}

QStringList BrickDesign::linksOf(const QString& id) const {
    QStringList out;
    for (const BrickLink& l : links) {
        if (l.a == id) out << l.b;
        else if (l.b == id) out << l.a;
    }
    return out;
}

namespace {

// The port of `self` that faces `other` on the pane.
int facingPort(const Brick& self, const Brick& other) {
    const Vec2 d{other.col - self.col, other.row - self.row};
    return dot(d, brickPortPaneDir(self, kPortRight)) >= 0.0 ? kPortRight : kPortLeft;
}

} // namespace

int BrickDesign::portOn(const QString& id, const QString& other) const {
    const BrickLink* l = link(id, other);
    if (!l) return -1;
    const int stated = l->a == id ? l->portA : l->portB;
    if (stated >= 0) return stated;
    const Brick* s = brick(id);
    const Brick* o = brick(other);
    return s && o ? facingPort(*s, *o) : kPortRight;
}

QStringList BrickDesign::startsOf(const QString& specialId) const {
    QStringList out;
    for (const QString& o : linksOf(specialId))
        if (const Brick* b = brick(o); b && b->type == BrickType::End &&
                                       portOn(specialId, o) == kPortLeft)
            out << o;
    return out;
}

QStringList BrickDesign::finishesOf(const QString& specialId) const {
    QStringList out;
    for (const QString& o : linksOf(specialId))
        if (const Brick* b = brick(o); b && b->type == BrickType::End &&
                                       portOn(specialId, o) == kPortRight)
            out << o;
    return out;
}

QStringList BrickDesign::endsOf(const QString& specialId) const {
    return startsOf(specialId) + finishesOf(specialId);
}

bool BrickDesign::connect(const QString& a, const QString& b, QString* why, int portA, int portB) {
    auto refuse = [why](const QString& w) {
        if (why) *why = w;
        return false;
    };
    const Brick* x = brick(a);
    const Brick* y = brick(b);
    if (!x || !y || a == b) return refuse(QStringLiteral("Pick two different bricks"));
    if (brickIsSpecial(x->type) == brickIsSpecial(y->type))
        return refuse(x->type == BrickType::End
                          ? QStringLiteral("Two end parts are joined by a harness: put a special "
                                           "between them and link each end to it")
                          : QStringLiteral("A special runs between end parts, not into another "
                                           "special"));
    if (linked(a, b)) return refuse(QStringLiteral("Those two are already linked"));
    // Which side of the special it goes on, and which side of the end.
    const bool aIsSpecial = brickIsSpecial(x->type);
    const Brick& sp = aIsSpecial ? *x : *y;
    const Brick& en = aIsSpecial ? *y : *x;
    int sPort = aIsSpecial ? portA : portB;
    int ePort = aIsSpecial ? portB : portA;
    const QStringList starts = startsOf(sp.id), finishes = finishesOf(sp.id);
    auto taken = [&](int port) { return (port == kPortLeft ? starts : finishes).size(); };
    const bool fanout = sp.type == BrickType::Fanout;
    if (sPort < 0) {
        sPort = facingPort(sp, en);
        // The side facing it is already used and the other side is free: the other side - a run
        // starts somewhere before it finishes anywhere, a fanout included.
        if (taken(sPort) >= 1 && taken(1 - sPort) == 0) sPort = 1 - sPort;
    }
    if (!fanout && taken(sPort) >= 1)
        return refuse(QStringLiteral("That end of the run is already on a part - only a fanout "
                                     "finishes on several"));
    if (fanout && taken(sPort) >= 1 && taken(1 - sPort) >= 2)
        return refuse(QStringLiteral("A fanout spreads from one part to several, not several to "
                                     "several"));
    if (ePort < 0) ePort = facingPort(en, sp);
    BrickLink l;
    l.a = a;
    l.b = b;
    l.portA = aIsSpecial ? sPort : ePort;
    l.portB = aIsSpecial ? ePort : sPort;
    links.push_back(l);
    if (why) why->clear();
    return true;
}

void BrickDesign::disconnect(const QString& a, const QString& b) {
    links.erase(std::remove_if(links.begin(), links.end(),
                               [&](const BrickLink& l) {
                                   return (l.a == a && l.b == b) || (l.a == b && l.b == a);
                               }),
                links.end());
}

// ---- Saving --------------------------------------------------------------------------------------

namespace {

QString endsKey(RunEnds e) {
    return e == RunEnds::Start    ? QStringLiteral("start")
           : e == RunEnds::Finish ? QStringLiteral("finish")
           : e == RunEnds::Both   ? QStringLiteral("both")
                                  : QStringLiteral("none");
}

RunEnds endsFromKey(const QString& k) {
    if (k == QLatin1String("start")) return RunEnds::Start;
    if (k == QLatin1String("finish")) return RunEnds::Finish;
    if (k == QLatin1String("both")) return RunEnds::Both;
    return RunEnds::None;
}

} // namespace

QJsonObject BrickDesign::toJson() const {
    QJsonArray bs;
    for (const Brick& b : bricks) {
        QJsonObject o{{"id", b.id},        {"type", brickTypeKey(b.type)}, {"kind", partKindKey(b.kind)},
                      {"libPart", b.libPart}, {"name", b.name},         {"col", b.col},
                      {"row", b.row},      {"rot", b.rot},             {"balloon", b.balloon}};
        if (brickIsSpecial(b.type)) {
            const BrickRunSpec& r = b.run;
            QJsonArray labels;
            for (const BrickLabel& l : r.labels)
                labels.append(QJsonObject{{"wire", l.wire},
                                          {"fromFinish", l.fromFinish},
                                          {"distanceMm", l.distanceMm},
                                          {"text", l.text}});
            o[QStringLiteral("run")] = QJsonObject{{"wires", r.wires},
                                                   {"lengthMm", r.lengthMm},
                                                   {"wireWidthMm", r.wireWidthMm},
                                                   {"fanStartMm", r.fanStartMm},
                                                   {"fanFinishMm", r.fanFinishMm},
                                                   {"kneeRadiusMm", r.kneeRadiusMm},
                                                   {"fanKneeMm", r.fanKneeMm},
                                                   {"fanScalePct", r.fanScalePct},
                                                   {"bodyScalePct", r.bodyScalePct},
                                                   {"bodyWidthMm", r.bodyWidthMm},
                                                   {"stack", QJsonArray::fromStringList(r.stack)},
                                                   {"transitions", endsKey(r.transitions)},
                                                   {"dimTotal", r.dimTotal},
                                                   {"dimFans", r.dimFans},
                                                   {"dimBetween", r.dimBetween},
                                                   {"labels", labels}};
        }
        bs.append(o);
    }
    QJsonArray ls;
    for (const BrickLink& l : links)
        ls.append(QJsonObject{{"a", l.a}, {"b", l.b}, {"portA", l.portA}, {"portB", l.portB},
                              {"wires", l.wires}});
    return QJsonObject{{"format", "edraw-brick-harness"}, {"bricks", bs}, {"links", ls},
                       {"dimFromFar", dimFromFar},        {"dimToFar", dimToFar}};
}

BrickDesign BrickDesign::fromJson(const QJsonObject& o) {
    BrickDesign d;
    d.dimFromFar = o.value(QStringLiteral("dimFromFar")).toBool();
    d.dimToFar = o.value(QStringLiteral("dimToFar")).toBool();
    for (const QJsonValue v : o.value(QStringLiteral("bricks")).toArray()) {
        const QJsonObject bo = v.toObject();
        Brick b;
        b.id = bo.value(QStringLiteral("id")).toString();
        b.type = brickTypeFromKey(bo.value(QStringLiteral("type")).toString());
        b.kind = partKindFromKey(bo.value(QStringLiteral("kind")).toString());
        b.libPart = bo.value(QStringLiteral("libPart")).toString();
        b.name = bo.value(QStringLiteral("name")).toString();
        b.col = bo.value(QStringLiteral("col")).toDouble();
        b.row = bo.value(QStringLiteral("row")).toDouble();
        b.rot = bo.value(QStringLiteral("rot")).toInt();
        b.balloon = bo.value(QStringLiteral("balloon")).toBool();
        const QJsonObject r = bo.value(QStringLiteral("run")).toObject();
        if (!r.isEmpty()) {
            BrickRunSpec& s = b.run;
            s.wires = r.value(QStringLiteral("wires")).toInt(s.wires);
            s.lengthMm = r.value(QStringLiteral("lengthMm")).toDouble(s.lengthMm);
            s.wireWidthMm = r.value(QStringLiteral("wireWidthMm")).toDouble(s.wireWidthMm);
            s.fanStartMm = r.value(QStringLiteral("fanStartMm")).toDouble(s.fanStartMm);
            s.fanFinishMm = r.value(QStringLiteral("fanFinishMm")).toDouble(s.fanFinishMm);
            s.kneeRadiusMm = r.value(QStringLiteral("kneeRadiusMm")).toDouble(s.kneeRadiusMm);
            s.fanKneeMm = r.value(QStringLiteral("fanKneeMm")).toDouble(s.fanKneeMm);
            s.fanScalePct = r.value(QStringLiteral("fanScalePct")).toDouble(s.fanScalePct);
            s.bodyScalePct = r.value(QStringLiteral("bodyScalePct")).toDouble(s.bodyScalePct);
            s.bodyWidthMm = r.value(QStringLiteral("bodyWidthMm")).toDouble(s.bodyWidthMm);
            if (r.contains(QStringLiteral("stack"))) {
                s.stack.clear();
                for (const QJsonValue x : r.value(QStringLiteral("stack")).toArray())
                    s.stack << x.toString();
                if (s.stack.isEmpty()) s.stack << QStringLiteral("wires");
            }
            s.transitions = endsFromKey(r.value(QStringLiteral("transitions")).toString());
            s.dimTotal = r.value(QStringLiteral("dimTotal")).toBool();
            s.dimFans = r.value(QStringLiteral("dimFans")).toBool();
            s.dimBetween = r.value(QStringLiteral("dimBetween")).toBool();
            for (const QJsonValue x : r.value(QStringLiteral("labels")).toArray()) {
                const QJsonObject lo = x.toObject();
                BrickLabel l;
                l.wire = lo.value(QStringLiteral("wire")).toInt(-1);
                l.fromFinish = lo.value(QStringLiteral("fromFinish")).toBool();
                l.distanceMm = lo.value(QStringLiteral("distanceMm")).toDouble(l.distanceMm);
                l.text = lo.value(QStringLiteral("text")).toString(l.text);
                s.labels.push_back(l);
            }
        }
        d.bricks.push_back(b);
    }
    for (const QJsonValue v : o.value(QStringLiteral("links")).toArray()) {
        const QJsonObject lo = v.toObject();
        BrickLink l;
        l.a = lo.value(QStringLiteral("a")).toString();
        l.b = lo.value(QStringLiteral("b")).toString();
        l.portA = lo.value(QStringLiteral("portA")).toInt(-1);
        l.portB = lo.value(QStringLiteral("portB")).toInt(-1);
        l.wires = lo.value(QStringLiteral("wires")).toInt(0);
        d.links.push_back(l);
    }
    return d;
}

// ---- Arranging -----------------------------------------------------------------------------------

void arrangeBricks(BrickDesign& design) {
    // The ends, and which ends each is joined to through a special.
    QHash<QString, QStringList> joined;
    for (const Brick& s : design.bricks) {
        if (!brickIsSpecial(s.type)) continue;
        const QStringList a = design.startsOf(s.id), b = design.finishesOf(s.id);
        for (const QString& x : a)
            for (const QString& y : b) {
                joined[x] << y;
                joined[y] << x;
            }
        for (const QString& x : a + b) joined[x];   // on the harness, if only at a loose end
    }
    QStringList pending;
    for (const Brick& b : design.bricks)
        if (b.type == BrickType::End && joined.contains(b.id)) pending << b.id;

    double rowBase = 0.0;
    QSet<QString> done;
    while (true) {
        QString root;
        for (const QString& id : pending) {
            if (done.contains(id)) continue;
            const Brick* b = design.brick(id);
            const Brick* r = root.isEmpty() ? nullptr : design.brick(root);
            if (!r || joined[id].size() > joined[root].size() ||
                (joined[id].size() == joined[root].size() &&
                 (b->col < r->col || (b->col == r->col && b->row < r->row))))
                root = id;
        }
        if (root.isEmpty()) break;

        // COLUMNS: out from the root, each end one column further than the one it was reached from -
        // on the side of the root it was already on, so a harness that goes both ways from its middle
        // keeps going both ways.
        QHash<QString, int> col;
        std::deque<QString> queue{root};
        col[root] = 0;
        const double rootWas = design.brick(root)->col;
        while (!queue.empty()) {
            const QString id = queue.front();
            queue.pop_front();
            done.insert(id);
            for (const QString& n : joined[id]) {
                if (col.contains(n)) continue;
                int sign = col[id] > 0 ? 1 : col[id] < 0 ? -1 : 0;
                if (sign == 0) sign = design.brick(n)->col < rootWas ? -1 : 1;
                col[n] = col[id] + sign;
                queue.push_back(n);
            }
        }
        // ROWS: each column spread evenly either side of the axis, in the order it was in.
        std::map<int, QStringList> byCol;
        for (auto it = col.begin(); it != col.end(); ++it) byCol[it.value()] << it.key();
        int tallest = 1;
        for (auto& [c, ids] : byCol) {
            std::stable_sort(ids.begin(), ids.end(), [&](const QString& x, const QString& y) {
                return design.brick(x)->row < design.brick(y)->row;
            });
            tallest = std::max(tallest, static_cast<int>(ids.size()));
        }
        const double axis = rowBase + (tallest - 1) / 2.0;
        for (auto& [c, ids] : byCol) {
            const int n = static_cast<int>(ids.size());
            for (int i = 0; i < n; ++i) {
                Brick* b = design.brick(ids[i]);
                b->col = c * 2.0;   // two steps a column: the specials sit in the step between
                b->row = axis + (i - (n - 1) / 2.0);
            }
        }
        rowBase += tallest + 1.0;
    }

    // Each special halfway between its ends - or a step out from its one end.
    for (Brick& s : design.bricks) {
        if (!brickIsSpecial(s.type)) continue;
        const QStringList ends = design.endsOf(s.id);
        if (ends.isEmpty()) continue;
        double c = 0.0, r = 0.0;
        for (const QString& e : ends) {
            c += design.brick(e)->col;
            r += design.brick(e)->row;
        }
        c /= ends.size();
        r /= ends.size();
        if (ends.size() == 1) c += 1.0;
        const QStringList a = design.startsOf(s.id);
        if (a.size() == 1 && design.finishesOf(s.id).size() > 1) {
            // A fanout sits a step out from the part it spreads from.
            const Brick* src = design.brick(a.front());
            c = src->col + (c > src->col ? 1.0 : -1.0);
            r = src->row;
        }
        s.col = c;
        s.row = r;
    }

    // AND EVERY BRICK TURNED TO FACE WHAT IT IS LINKED TO: each port toward the bricks on it.
    for (Brick& b : design.bricks) {
        Vec2 want[2] = {{0.0, 0.0}, {0.0, 0.0}};
        bool any = false;
        for (const QString& o : design.linksOf(b.id)) {
            const Brick* ob = design.brick(o);
            const int p = design.portOn(b.id, o);
            if (!ob || p < 0) continue;
            want[p] += Vec2{ob->col - b.col, ob->row - b.row};
            any = true;
        }
        if (!any) continue;
        int best = b.rot, bestScore = -1;
        double bestVal = -1e300;
        for (int r = 0; r < 4; ++r) {
            Brick t = b;
            t.rot = r;
            const double v = dot(brickPortPaneDir(t, kPortLeft), want[0]) +
                             dot(brickPortPaneDir(t, kPortRight), want[1]);
            if (v > bestVal + 1e-9) {
                bestVal = v;
                best = r;
                bestScore = r;
            }
        }
        (void)bestScore;
        b.rot = best;
    }
}

// ---- Routing -------------------------------------------------------------------------------------

namespace {

void tidyRoute(std::vector<Vec2>& p) {
    std::vector<Vec2> out;
    for (const Vec2& v : p)
        if (out.empty() || distance(out.back(), v) > 1e-6) out.push_back(v);
    bool changed = true;
    while (changed && out.size() > 2) {
        changed = false;
        for (size_t i = 1; i + 1 < out.size(); ++i) {
            const Vec2 d1 = out[i] - out[i - 1];
            const Vec2 d2 = out[i + 1] - out[i];
            if (std::abs(cross(d1.normalized(), d2.normalized())) < 1e-9 && dot(d1, d2) > 0.0) {
                out.erase(out.begin() + static_cast<long>(i));
                changed = true;
                break;
            }
        }
    }
    p = out;
}

} // namespace

std::vector<Vec2> routeBetweenFaces(const Vec2& a, const Vec2& aOut, const Vec2& b, const Vec2& bOut,
                                    double leadMm, double shiftDeg) {
    const Vec2 u = aOut.normalized();
    const Vec2 w = bOut.normalized();
    const Vec2 left = u.perp();
    const Vec2 d = b - a;
    const double fwd = dot(d, u);
    const double side = dot(d, left);
    const double lead = std::max(0.0, leadMm);
    std::vector<Vec2> path{a};
    auto done = [&]() {
        path.push_back(b);
        tidyRoute(path);
        return path;
    };

    // FACING EACH OTHER.
    if (dot(u, w) < -0.999) {
        if (std::abs(side) < 1e-6 && fwd > 0.0) return done();
        if (fwd > 0.0) {
            // A LEVEL SHIFT: straight out, across on the slant, straight in.
            const double deg = std::clamp(shiftDeg, 1.0, 90.0);
            double slant = deg >= 89.999 ? 0.0 : std::abs(side) / std::tan(deg * M_PI / 180.0);
            const double room = fwd - 2.0 * std::min(lead, fwd / 2.0);
            if (slant > room) slant = std::max(0.0, room);
            const double leg = (fwd - slant) / 2.0;
            const Vec2 p1 = a + u * leg;
            path.push_back(p1);
            path.push_back(p1 + u * slant + left * side);
            return done();
        }
        const Vec2 p1 = a + u * lead;
        const Vec2 p4 = b + w * lead;
        const double mid = side / 2.0;
        path.push_back(p1);
        path.push_back(p1 + left * mid);
        path.push_back(p4 + left * (mid - side));
        path.push_back(p4);
        return done();
    }

    // SQUARE TO ONE ANOTHER: one bend, where the two lines cross - when that is ahead of both.
    if (std::abs(dot(u, w)) < 1e-3) {
        const double t = cross(d, w) / cross(u, w);
        const double s = cross(d, u) / cross(u, w);
        if (t >= lead - 1e-9 && s >= lead - 1e-9) {
            path.push_back(a + u * t);
            return done();
        }
    }

    // THE SAME WAY ROUND: out past the further face, across, and back in.
    if (dot(u, w) > 0.999) {
        const double depth = std::max(lead, lead + fwd);
        path.push_back(a + u * depth);
        path.push_back(b + w * (depth - fwd));
        return done();
    }

    const Vec2 p1 = a + u * lead;
    const Vec2 p4 = b + w * lead;
    path.push_back(p1);
    path.push_back(p1 + u * dot(p4 - p1, u));
    path.push_back(p4);
    return done();
}

// ---- The drawing ---------------------------------------------------------------------------------

namespace {

struct Placeholder {
    double along;
    double across;
};

Placeholder placeholderFor(PartKind k) {
    switch (k) {
    case PartKind::Connector: return {24.0, 20.0};
    case PartKind::Ferrule: return {12.0, 6.0};
    case PartKind::Crimp: return {14.0, 8.0};
    case PartKind::Terminal: return {16.0, 10.0};
    case PartKind::ContactPin: return {12.0, 4.0};
    case PartKind::Splice: return {16.0, 10.0};
    case PartKind::Resistor: return {18.0, 8.0};
    case PartKind::Capacitor: return {14.0, 10.0};
    case PartKind::Heatshrink: return {30.0, 14.0};
    case PartKind::YSplit: return {26.0, 22.0};
    default: return {18.0, 12.0};
    }
}

void stampBrick(std::vector<Entity>& ents, const QString& id) {
    for (Entity& e : ents) e.meta.insert(QStringLiteral("brickId"), id);
}

QString faceKey(const QString& end, int port) { return end + QLatin1Char(':') + QString::number(port); }

double routeLength(const std::vector<Vec2>& r) {
    double s = 0.0;
    for (size_t i = 1; i < r.size(); ++i) s += distance(r[i - 1], r[i]);
    return s;
}

// The point so far along a route, and the way it runs there.
Vec2 pointAlong(const std::vector<Vec2>& r, double s, Vec2* dir = nullptr) {
    if (r.size() < 2) return r.empty() ? Vec2{} : r.front();
    s = std::clamp(s, 0.0, routeLength(r));
    for (size_t i = 1; i < r.size(); ++i) {
        const double seg = distance(r[i - 1], r[i]);
        if (s <= seg || i + 1 == r.size()) {
            const Vec2 d = (r[i] - r[i - 1]).normalized();
            if (dir) *dir = d;
            return r[i - 1] + d * std::min(s, seg);
        }
        s -= seg;
    }
    return r.back();
}

Entity makeDim(const Vec2& a, const Vec2& b, double offset, const QString& text) {
    Entity e;
    e.type = EntityType::Dimension;
    e.a = a;
    e.b = b;
    const Vec2 n = (b - a).normalized().perp();
    e.points = {a, b, (a + b) * 0.5 + n * offset};
    e.meta.insert(QStringLiteral("dimType"), QStringLiteral("aligned"));
    e.meta.insert(QStringLiteral("textHeight"), 3.5);
    e.meta.insert(QStringLiteral("arrowSize"), 2.3);
    e.meta.insert(QStringLiteral("extOffset"), 2.0);
    e.meta.insert(QStringLiteral("decimals"), 1);
    e.meta.insert(QStringLiteral("mechRole"), QStringLiteral("dim"));
    if (!text.isEmpty()) e.meta.insert(QStringLiteral("textOverride"), text);
    return e;
}

Entity makeBoxAt(const Vec2& mid, const Vec2& along, double length, double width) {
    const Vec2 u = along.normalized() * (length / 2.0);
    const Vec2 v = along.normalized().perp() * (width / 2.0);
    Entity e;
    e.type = EntityType::Polyline;
    e.points = {mid - u - v, mid + u - v, mid + u + v, mid - u + v, mid - u - v};
    e.a = e.points.front();
    e.b = e.points.back();
    return e;
}

Entity makeTextAt(const Vec2& at, const QString& text, double height, double rotation = 0.0) {
    Entity e;
    e.type = EntityType::Text;
    e.a = at;
    e.rotation = rotation;
    e.meta.insert(QStringLiteral("text"), text);
    e.meta.insert(QStringLiteral("height"), height);
    return e;
}

} // namespace

BrickBuild buildBrickHarness(const BrickDesign& design, const PartsCatalog* catalog,
                             const std::function<int(const QString& libPart)>& itemOf) {
    BrickBuild out;
    MechDrawOptions opt;

    // WHAT EACH END IS.
    struct EndInfo {
        const Brick* brick = nullptr;
        const LibraryPart* part = nullptr;
        bool joint = false;
        double across = 20.0;
    };
    QHash<QString, EndInfo> ends;
    double widest = 0.0;
    for (const Brick& b : design.bricks) {
        if (b.type != BrickType::End) continue;
        EndInfo info;
        info.brick = &b;
        info.joint = brickKindIsJoint(b.kind);
        info.part = catalog && !b.libPart.isEmpty() ? catalog->byId(b.libPart) : nullptr;
        if (info.part && !info.part->hasMechanical()) info.part = nullptr;
        if (info.part) {
            const PlacedLibraryPart probe =
                placeLibraryPartAtFace(*info.part, Vec2{0, 0}, 0.0, Vec2{-1, 0}, opt);
            Bounds bb;
            for (const Entity& e : probe.entities) bb.expand(e.bounds());
            info.across = bb.valid ? std::max(bb.max.y - bb.min.y, bb.max.x - bb.min.x) : 20.0;
        } else {
            const Placeholder ph = placeholderFor(b.kind);
            info.across = std::max(ph.across, ph.along);
        }
        widest = std::max(widest, info.across);
        ends.insert(b.id, info);
    }
    const double rowPitch = std::max(40.0, widest + 25.0);

    // THE BRANCHES: every stretch of harness from one face to another. A run is one; a fanout is one
    // to each part it finishes on.
    struct Branch {
        const Brick* special = nullptr;
        QString endA, endB;   // endB empty: a loose end
        int portA = 0, portB = 0;
        int wires = 2;
    };
    std::vector<Branch> branches;
    for (const Brick& s : design.bricks) {
        if (!brickIsSpecial(s.type)) continue;
        QStringList a = design.startsOf(s.id), b = design.finishesOf(s.id);
        if (a.isEmpty() && b.isEmpty()) {
            out.problems << QStringLiteral("%1 is linked to no part").arg(s.name.isEmpty() ? s.id : s.name);
            continue;
        }
        if (a.isEmpty()) std::swap(a, b);   // a run on one part only: from it, loose
        if (s.type == BrickType::Fanout && b.size() > 1) {
            // Each part it finishes on takes the wires its link says, or an even share of the rest.
            int given = 0, open = 0;
            for (const QString& e : b) {
                const BrickLink* l = design.link(s.id, e);
                if (l && l->wires > 0) given += l->wires;
                else ++open;
            }
            // The rest shared as evenly as whole wires go: the first parts take one more each.
            const int rest = std::max(0, s.run.wires - given);
            int extra = open > 0 ? rest % open : 0;
            const int base = open > 0 ? rest / open : 0;
            for (const QString& e : b) {
                const BrickLink* l = design.link(s.id, e);
                int share = base;
                if (!(l && l->wires > 0) && extra > 0) {
                    ++share;
                    --extra;
                }
                share = std::max(1, share);
                Branch br;
                br.special = &s;
                br.endA = a.front();
                br.endB = e;
                br.portA = design.portOn(a.front(), s.id);
                br.portB = design.portOn(e, s.id);
                br.wires = l && l->wires > 0 ? l->wires : share;
                branches.push_back(br);
            }
            continue;
        }
        Branch br;
        br.special = &s;
        br.endA = a.front();
        br.portA = design.portOn(a.front(), s.id);
        if (!b.isEmpty()) {
            br.endB = b.front();
            br.portB = design.portOn(b.front(), s.id);
        }
        br.wires = s.type == BrickType::TwistedPair ? 2 : std::max(1, s.run.wires);
        branches.push_back(br);
    }

    // THE COLUMNS, as far apart as the runs between them are long.
    std::vector<double> cols;
    for (const auto& info : ends) cols.push_back(info.brick->col);
    std::sort(cols.begin(), cols.end());
    cols.erase(std::unique(cols.begin(), cols.end(),
                           [](double x, double y) { return std::abs(x - y) < 1e-9; }),
               cols.end());
    auto colIndex = [&](double c) {
        for (size_t i = 0; i < cols.size(); ++i)
            if (std::abs(cols[i] - c) < 1e-9) return static_cast<int>(i);
        return 0;
    };
    std::vector<double> gap(cols.size() > 0 ? cols.size() - 1 : 0, 0.0);
    for (const Branch& br : branches) {
        if (br.endB.isEmpty()) continue;
        const int i0 = colIndex(design.brick(br.endA)->col);
        const int i1 = colIndex(design.brick(br.endB)->col);
        const int lo = std::min(i0, i1), hi = std::max(i0, i1);
        if (hi == lo) continue;
        const double share = br.special->run.lengthMm / (hi - lo);
        for (int k = lo; k < hi; ++k) gap[k] = std::max(gap[k], share);
    }
    std::vector<double> colX(cols.size(), 0.0);
    for (size_t i = 1; i < cols.size(); ++i)
        colX[i] = colX[i - 1] + (gap[i - 1] > 1e-9 ? gap[i - 1] : 150.0);
    auto endAt = [&](const Brick& b) { return Vec2{colX[colIndex(b.col)], -b.row * rowPitch}; };

    // THE ENDS, and their faces.
    for (auto it = ends.begin(); it != ends.end(); ++it) {
        const EndInfo& info = it.value();
        const Brick& b = *info.brick;
        const Vec2 at = endAt(b);
        // The port a one-faced part faces on: the one its links are on.
        int facePort = kPortRight;
        {
            int onLeft = 0, onRight = 0;
            for (const QString& o : design.linksOf(b.id))
                (design.portOn(b.id, o) == kPortLeft ? onLeft : onRight)++;
            if (onLeft > onRight) facePort = kPortLeft;
        }
        std::vector<Entity> body;
        if (info.part && !info.joint) {
            const Vec2 d = brickPortWorldDir(b, facePort);
            const PlacedLibraryPart placed =
                placeLibraryPartAtFace(*info.part, at, angleOf(d), d * -1.0, opt);
            body = placed.entities;
            Bounds bb;
            for (const Entity& e : body) bb.expand(e.bounds());
            BrickFace f;
            f.centre = at;
            f.out = d;
            f.lengthMm = placed.faceLengthMm > 1e-6
                             ? placed.faceLengthMm
                             : (bb.valid ? std::abs(dot(bb.max - bb.min, d.perp())) : 20.0);
            f.nearAt = placed.haveEnds ? placed.nearAt : at;
            double back = 0.0;
            if (bb.valid)
                for (const Vec2& c : {bb.min, bb.max, Vec2{bb.min.x, bb.max.y}, Vec2{bb.max.x, bb.min.y}})
                    back = std::max(back, dot(at - c, d));
            f.farAt = placed.haveEnds ? placed.farAt : at - d * back;
            // Both ports land on the one face it has.
            out.faces.insert(faceKey(b.id, kPortLeft), f);
            out.faces.insert(faceKey(b.id, kPortRight), f);
        } else if (info.part && info.joint) {
            // A JOINT: set down by its first face on the left port, the other face wherever the part
            // has it, and the whole centred on its place.
            const Vec2 dl = brickPortWorldDir(b, kPortLeft);
            const PlacedLibraryPart placed =
                placeLibraryPartAtFace(*info.part, Vec2{0, 0}, angleOf(dl), dl * -1.0, opt);
            body = placed.entities;
            Bounds bb;
            for (const Entity& e : body) bb.expand(e.bounds());
            const Vec2 bbMid = bb.valid ? (bb.min + bb.max) * 0.5 : Vec2{};
            std::pair<Vec2, Vec2> f0{Vec2{0, 0} + dl.perp() * 5.0, Vec2{0, 0} - dl.perp() * 5.0};
            if (!placed.faces.empty()) f0 = placed.faces[0];
            std::pair<Vec2, Vec2> f1;
            if (placed.faces.size() >= 2) f1 = placed.faces[1];
            else f1 = {bbMid * 2.0 - f0.first, bbMid * 2.0 - f0.second};
            const Vec2 m0 = (f0.first + f0.second) * 0.5, m1 = (f1.first + f1.second) * 0.5;
            const Vec2 shift = at - (m0 + m1) * 0.5;
            for (Entity& e : body) translateEntity(e, shift);
            BrickFace fl, fr;
            fl.centre = m0 + shift;
            fr.centre = m1 + shift;
            fl.out = dl;
            fr.out = (m1 - m0).length() > 1e-9 ? (m1 - m0).normalized() : dl * -1.0;
            fl.lengthMm = distance(f0.first, f0.second);
            fr.lengthMm = distance(f1.first, f1.second);
            fl.nearAt = fl.centre;
            fl.farAt = fr.centre;
            fr.nearAt = fr.centre;
            fr.farAt = fl.centre;
            out.faces.insert(faceKey(b.id, kPortLeft), fl);
            out.faces.insert(faceKey(b.id, kPortRight), fr);
        } else {
            const Placeholder ph = placeholderFor(b.kind);
            Entity box;
            box.type = EntityType::Polyline;
            box.layer = opt.layer;
            if (info.joint) {
                // A body with a face each side.
                const Vec2 dr = brickPortWorldDir(b, kPortRight);
                const Vec2 u = dr * (ph.along / 2.0), v = dr.perp() * (ph.across / 2.0);
                box.points = {at - u - v, at + u - v, at + u + v, at - u + v, at - u - v};
                BrickFace fl, fr;
                fl.centre = at - u;
                fr.centre = at + u;
                fl.out = dr * -1.0;
                fr.out = dr;
                fl.lengthMm = fr.lengthMm = ph.across;
                fl.nearAt = fl.centre;
                fl.farAt = fr.centre;
                fr.nearAt = fr.centre;
                fr.farAt = fl.centre;
                out.faces.insert(faceKey(b.id, kPortLeft), fl);
                out.faces.insert(faceKey(b.id, kPortRight), fr);
            } else {
                const Vec2 d = brickPortWorldDir(b, facePort);
                const Vec2 back = d * -ph.along, v = d.perp() * (ph.across / 2.0);
                box.points = {at + v, at + v + back, at - v + back, at - v, at + v};
                BrickFace f;
                f.centre = at;
                f.out = d;
                f.lengthMm = ph.across;
                f.nearAt = at;
                f.farAt = at + back;
                out.faces.insert(faceKey(b.id, kPortLeft), f);
                out.faces.insert(faceKey(b.id, kPortRight), f);
            }
            box.a = box.points.front();
            box.b = box.points.back();
            body.push_back(box);
            Entity label = makeTextAt(at + Vec2{-ph.along / 2.0, ph.across / 2.0 + 2.0},
                                      b.name.isEmpty() ? partKindName(b.kind) : b.name, 3.0);
            if (!info.joint) label.a = out.faces.value(faceKey(b.id, facePort)).farAt +
                                       Vec2{0.0, ph.across / 2.0 + 2.0};
            label.layer = opt.layer;
            body.push_back(label);
        }
        // A BALLOON, pointing at the middle of the part, numbered by its line of the bill.
        if (b.balloon) {
            Bounds bb;
            for (const Entity& e : body) bb.expand(e.bounds());
            if (bb.valid) {
                const Vec2 tip = (bb.min + bb.max) * 0.5;
                const Vec2 at{tip.x - 12.0, bb.max.y + 14.0};
                const int item = itemOf && !b.libPart.isEmpty() ? itemOf(b.libPart) : 0;
                Entity ball;
                ball.type = EntityType::Circle;
                ball.center = at;
                ball.a = at;
                ball.radius = 4.5;
                ball.meta.insert(QStringLiteral("balloon"), true);
                ball.meta.insert(QStringLiteral("text"), item > 0 ? QString::number(item) : QStringLiteral("?"));
                ball.meta.insert(QStringLiteral("landing"), 6.0);
                ball.meta.insert(QStringLiteral("landingSide"), tip.x >= at.x ? 1.0 : -1.0);
                ball.meta.insert(QStringLiteral("leaders"), QJsonArray{QJsonArray{tip.x, tip.y}});
                ball.meta.insert(QStringLiteral("mechRole"), QStringLiteral("balloon"));
                body.push_back(ball);
            }
        }
        stampBrick(body, b.id);
        for (Entity& e : body) out.entities.push_back(std::move(e));
    }

    // WHERE ON ITS FACE EACH BRANCH LANDS. All the wires landing on one face are spread together over
    // the fan share of it - in the order of where they go, so they do not cross - and each branch takes
    // its own wires' stretch of that. However many land there, together they never exceed the face.
    struct Landing {
        Vec2 centre;
        double lane = 1.0;
        double share = 1.0;    // the stretch of face its wires take
    };
    std::vector<Landing> atA(branches.size()), atB(branches.size());
    {
        struct Use {
            size_t branch;
            bool isB;
            int wires;
            double order;
        };
        QHash<QString, std::vector<Use>> byFace;
        for (size_t i = 0; i < branches.size(); ++i) {
            const Branch& br = branches[i];
            const QString ka = faceKey(br.endA, br.portA);
            const BrickFace fa = out.faces.value(ka);
            const Vec2 toB = br.endB.isEmpty() ? fa.centre + fa.out
                                               : endAt(*design.brick(br.endB));
            byFace[ka].push_back({i, false, br.wires, dot(toB, fa.out.perp())});
            if (!br.endB.isEmpty()) {
                const QString kb = faceKey(br.endB, br.portB);
                const BrickFace fb = out.faces.value(kb);
                byFace[kb].push_back(
                    {i, true, br.wires, dot(endAt(*design.brick(br.endA)), fb.out.perp())});
            }
        }
        for (auto it = byFace.begin(); it != byFace.end(); ++it) {
            const BrickFace f = out.faces.value(it.key());
            std::vector<Use>& uses = it.value();
            std::stable_sort(uses.begin(), uses.end(),
                             [](const Use& x, const Use& y) { return x.order < y.order; });
            int total = 0;
            double scale = 100.0;
            double ww = 0.1;
            for (const Use& u : uses) {
                total += u.wires;
                scale = std::min(scale, branches[u.branch].special->run.fanScalePct);
                ww = std::max(ww, branches[u.branch].special->run.wireWidthMm);
            }
            const double spread = scale / 100.0 * f.lengthMm;
            const double lane = total > 1 ? std::max(0.0, spread - ww) / (total - 1) : 0.0;
            int k = 0;
            for (const Use& u : uses) {
                Landing l;
                const double first = -(total - 1) / 2.0 + k;
                const double mid = first + (u.wires - 1) / 2.0;
                l.centre = f.centre + f.out.perp() * (mid * lane);
                l.lane = lane;
                l.share = f.lengthMm * u.wires / std::max(1, total);
                (u.isB ? atB : atA)[u.branch] = l;
                k += u.wires;
            }
        }
    }

    // THE RUNS.
    for (size_t i = 0; i < branches.size(); ++i) {
        const Branch& br = branches[i];
        const Brick& s = *br.special;
        const BrickRunSpec& r = s.run;
        const BrickFace fa = out.faces.value(faceKey(br.endA, br.portA));
        const bool loose = br.endB.isEmpty();
        const BrickFace fb = loose ? BrickFace{} : out.faces.value(faceKey(br.endB, br.portB));
        const int wires = std::max(1, br.wires);
        const double ww = std::max(0.1, r.wireWidthMm);
        const Landing la = atA[i];
        const Landing lb = loose ? la : atB[i];
        // The fans: as long as asked, or as long as the wires need to spread.
        const double fanA = r.fanStartMm >= 0.0 ? r.fanStartMm
                                                : std::max(kWiresRunEnd, la.lane * (wires - 1) / 2.0);
        const double fanB = loose ? 0.0
                            : r.fanFinishMm >= 0.0 ? r.fanFinishMm
                                                   : std::max(kWiresRunEnd, lb.lane * (wires - 1) / 2.0);
        const double lead = std::max({fanA, fanB, 10.0}) + std::max(0.0, r.kneeRadiusMm) + 5.0;
        std::vector<Vec2> route = loose ? std::vector<Vec2>{la.centre, la.centre + fa.out * r.lengthMm}
                                        : routeBetweenFaces(la.centre, fa.out, lb.centre, fb.out, lead);
        if (route.size() < 2) continue;
        std::vector<Entity> made;

        const bool laidUp = s.type == BrickType::TwistedPair || s.type == BrickType::TwistedBundle;
        const QString outer = r.outermost();
        double bodyWidth = 0.0;
        if (wires == 1) {
            // ONE WIRE is a wire, not a bundle.
            Entity w;
            w.type = EntityType::Polyline;
            w.points = route;
            w.a = route.front();
            w.b = route.back();
            w.meta.insert(QStringLiteral("double"), true);
            w.meta.insert(QStringLiteral("gap"), ww);
            made.push_back(w);
        } else {
            Entity run;
            run.type = EntityType::TwistedPair;
            run.layer = opt.layer;
            run.points = route;
            run.a = route.front();
            run.b = route.back();
            run.meta.insert(QStringLiteral("twistWires"), std::clamp(wires, 2, kTwistedMaxWires));
            run.meta.insert(QStringLiteral("twistStraight"), !laidUp);
            run.meta.insert(QStringLiteral("twistWireWidth"), ww);
            if (laidUp) {
                const double spreadA = la.lane * (wires - 1) + ww;
                const double spreadB = lb.lane * (wires - 1) + ww;
                run.meta.insert(QStringLiteral("twistWidth"), std::min(spreadA, spreadB));
                run.meta.insert(QStringLiteral("twistPitch"), kTwistedPairPitch);
                run.meta.insert(QStringLiteral("twistEnds"),
                                r.fanStartMm >= 0.0 ? r.fanStartMm : kTwistedPairLead);
            } else {
                run.meta.insert(QStringLiteral("twistSpread"),
                                loose ? QStringLiteral("start") : QStringLiteral("both"));
                run.meta.insert(QStringLiteral("twistSidesSame"), false);
                run.meta.insert(QStringLiteral("twistLane"), std::max(1e-3, la.lane));
                run.meta.insert(QStringLiteral("twistLaneFinish"), std::max(1e-3, lb.lane));
                run.meta.insert(QStringLiteral("twistEnds"), fanA);
                run.meta.insert(QStringLiteral("twistEndsFinish"), loose ? 0.0 : fanB);
                if (r.fanKneeMm >= 0.0) {
                    run.meta.insert(QStringLiteral("twistFanKnee"), r.fanKneeMm);
                    run.meta.insert(QStringLiteral("twistFanKneeFinish"), r.fanKneeMm);
                }
                // THE BODY IS THE OUTERMOST OF THE STACK: a braid under a heatshrink is not seen.
                if (outer != QLatin1String("wires")) {
                    const double share = loose ? la.share : std::min(la.share, lb.share);
                    const int layers = std::max(0, static_cast<int>(r.stack.size()) - 2);
                    bodyWidth = r.bodyWidthMm > 1e-9 ? r.bodyWidthMm
                                                     : r.bodyScalePct / 100.0 * share + 0.6 * layers;
                    run.meta.insert(QStringLiteral("twistBody"),
                                    outer == QLatin1String("heatshrink") ? QStringLiteral("heatshrink")
                                                                         : QStringLiteral("cable"));
                    run.meta.insert(QStringLiteral("twistBodyWidth"), bodyWidth);
                    run.meta.insert(QStringLiteral("twistBodyTransitionSides"),
                                    r.transitions == RunEnds::Start    ? QStringLiteral("start")
                                    : r.transitions == RunEnds::Finish ? QStringLiteral("finish")
                                    : r.transitions == RunEnds::Both   ? QStringLiteral("both")
                                                                       : QStringLiteral("none"));
                }
            }
            if (r.kneeRadiusMm >= 0.0) run.meta.insert(QStringLiteral("twistKnee"), r.kneeRadiusMm);
            made.push_back(run);
        }

        const double total = routeLength(route);
        // A BRAID, when it is the outermost, is shown as braid over the body between the fans.
        if (outer == QLatin1String("braid") && bodyWidth > 0.0 && total > fanA + fanB + 1.0) {
            std::vector<Vec2> mid{pointAlong(route, fanA)};
            double acc = 0.0;
            for (size_t k = 1; k + 1 < route.size(); ++k) {
                acc += distance(route[k - 1], route[k]);
                if (acc > fanA && acc < total - fanB) mid.push_back(route[k]);
            }
            mid.push_back(pointAlong(route, total - fanB));
            const std::vector<Vec2> up = offsetPolyline(mid, bodyWidth / 2.0);
            std::vector<Vec2> down = offsetPolyline(mid, -bodyWidth / 2.0);
            if (up.size() >= 2 && down.size() >= 2) {
                Entity h;
                h.type = EntityType::Hatch;
                h.points = up;
                for (auto itd = down.rbegin(); itd != down.rend(); ++itd) h.points.push_back(*itd);
                h.meta.insert(QStringLiteral("pattern"), QStringLiteral("braid"));
                h.meta.insert(QStringLiteral("hatchScale"), 0.5);
                h.meta.insert(QStringLiteral("mechRole"), QStringLiteral("braid"));
                made.push_back(h);
            }
        }

        // DIMENSIONS: the whole run between the two parts' reference points, each fan, and what is
        // between the fans. Along a bent run the figure is the length along it.
        const double gapOut = std::max(bodyWidth, la.lane * (wires - 1) + ww) / 2.0 + 10.0;
        auto fmt = [](double v) { return QString::number(v, 'f', 1); };
        const bool straight = route.size() == 2;
        if (r.dimTotal && !loose) {
            const Vec2 from = design.dimFromFar ? fa.farAt : fa.nearAt;
            const Vec2 to = design.dimToFar ? fb.farAt : fb.nearAt;
            const double along = total + distance(from, fa.centre) + distance(to, fb.centre);
            // Drawn along the way the harness leaves its start, not corner to corner: on a bent run
            // the figure is the length along it, and a line slanted across the sheet says nothing.
            const Vec2 toAlong = straight ? to : from + fa.out * dot(to - from, fa.out);
            Entity d = makeDim(from, toAlong, -(gapOut + 10.0), straight ? QString() : fmt(along));
            d.meta.insert(QStringLiteral("brickDim"), QStringLiteral("total"));
            made.push_back(d);
        }
        if (r.dimFans) {
            Entity d = makeDim(route.front(), pointAlong(route, fanA), gapOut, QString());
            d.meta.insert(QStringLiteral("brickDim"), QStringLiteral("fanStart"));
            made.push_back(d);
            if (!loose) {
                Entity e = makeDim(pointAlong(route, total - fanB), route.back(), gapOut, QString());
                e.meta.insert(QStringLiteral("brickDim"), QStringLiteral("fanFinish"));
                made.push_back(e);
            }
        }
        if (r.dimBetween && !loose && total > fanA + fanB) {
            const Vec2 p0 = pointAlong(route, fanA), p1 = pointAlong(route, total - fanB);
            Entity d = makeDim(p0, p1, gapOut, straight ? QString() : fmt(total - fanA - fanB));
            d.meta.insert(QStringLiteral("brickDim"), QStringLiteral("between"));
            made.push_back(d);
        }

        // LABELS: on the whole bundle, or on one wire of a fan, so far along from the end chosen.
        for (const BrickLabel& lab : r.labels) {
            const bool fromB = lab.fromFinish && !loose;
            const double s0 = fromB ? total - lab.distanceMm : lab.distanceMm;
            Vec2 dir;
            const Vec2 at = pointAlong(route, s0, &dir);
            if (lab.wire < 0) {
                const double across = std::max(bodyWidth, 1.5 * ww * std::min(wires, 3)) + 2.0;
                Entity box = makeBoxAt(at, dir, 14.0, across);
                box.meta.insert(QStringLiteral("mechRole"), QStringLiteral("label"));
                made.push_back(box);
                Entity t = makeTextAt(at + dir.perp() * (across / 2.0 + 2.0), lab.text, 2.5, angleOf(dir));
                t.meta.insert(QStringLiteral("mechRole"), QStringLiteral("label"));
                made.push_back(t);
            } else {
                // On one wire of the fan: where that wire is at that distance into the fan.
                const int k = std::clamp(lab.wire, 0, wires - 1);
                const Landing& l = fromB ? lb : la;
                const double fan = fromB ? fanB : fanA;
                const double t = fan > 1e-9 ? std::clamp(lab.distanceMm / fan, 0.0, 1.0) : 1.0;
                const double packed = ww * 1.5;
                const double slot = k - (wires - 1) / 2.0;
                const double off = slot * (l.lane * (1.0 - t) + packed * t);
                // Across the run as it runs from that end.
                const Vec2 side = (fromB ? dir * -1.0 : dir).perp();
                const Vec2 p = at + side * off;
                Entity box = makeBoxAt(p, dir, 8.0, ww + 1.5);
                box.meta.insert(QStringLiteral("mechRole"), QStringLiteral("label"));
                made.push_back(box);
                Entity tx = makeTextAt(p + side * (slot >= 0 ? ww + 2.0 : -(ww + 4.0)), lab.text, 1.8,
                                       angleOf(dir));
                tx.meta.insert(QStringLiteral("mechRole"), QStringLiteral("label"));
                made.push_back(tx);
            }
        }

        stampBrick(made, s.id);
        for (Entity& e : made) {
            e.meta.insert(QStringLiteral("brickBranch"), static_cast<int>(i));
            out.entities.push_back(std::move(e));
        }
    }
    return out;
}

} // namespace Edraw_Desktop
