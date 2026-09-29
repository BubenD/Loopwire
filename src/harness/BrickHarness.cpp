#include "harness/BrickHarness.h"

#include "geometry/Geometry.h"
#include "harness/MechanicalHarness.h"
#include "harness/MechModels.h"
#include "tools/EntityTransforms.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <optional>

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

int brickKindFaceCount(PartKind kind) {
    if (kind == PartKind::YSplit) return 3;
    return brickKindIsJoint(kind) ? 2 : 1;
}

Vec2 turnQuarters(const Vec2& v, int rot) {
    Vec2 d = v;
    for (int i = 0; i < ((rot % 4) + 4) % 4; ++i) d = Vec2{d.y, -d.x};   // a quarter clockwise
    return d;
}

Vec2 BrickDesign::portWorldDir(const Brick& b, int port) const {
    const BrickShape s = shapeOf(b);
    if (port < 0 || port >= s.ports()) return Vec2{1.0, 0.0};
    return turnQuarters(s.faceOut[static_cast<size_t>(port)], b.rot);
}

Vec2 BrickDesign::portPaneDir(const Brick& b, int port) const {
    const Vec2 w = portWorldDir(b, port);
    return Vec2{w.x, -w.y};
}

int BrickDesign::facingPort(const Brick& self, const Vec2& toward,
                            const std::function<bool(int)>& allowed) const {
    const BrickShape s = shapeOf(self);
    int best = -1;
    double bestDot = -1e300;
    for (int p = 0; p < s.ports(); ++p) {
        if (allowed && !allowed(p)) continue;
        const Vec2 w = turnQuarters(s.faceOut[static_cast<size_t>(p)], self.rot);
        const double d = dot(Vec2{w.x, -w.y}, toward);
        if (d > bestDot + 1e-9) {
            bestDot = d;
            best = p;
        }
    }
    return best;
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

int BrickDesign::portOn(const QString& id, const QString& other) const {
    const BrickLink* l = link(id, other);
    if (!l) return -1;
    const int stated = l->a == id ? l->portA : l->portB;
    if (stated >= 0) return stated;
    const Brick* s = brick(id);
    const Brick* o = brick(other);
    return s && o ? std::max(0, facingPort(*s, Vec2{o->col - s->col, o->row - s->row})) : 0;
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

QStringList BrickDesign::sideOf(const QString& specialId, int port) const {
    QStringList out;
    for (const QString& o : linksOf(specialId))
        if (portOn(specialId, o) == port) out << o;
    return out;
}

int BrickDesign::fanoutSpreadSide(const Brick& s) const {
    if (s.run.fanSide == 0 || s.run.fanSide == 1) return s.run.fanSide;
    // THE END ON A PART: the side with several parts on it, else the side with a part at all.
    auto parts = [&](int port) {
        int n = 0;
        for (const QString& o : sideOf(s.id, port))
            if (const Brick* b = brick(o); b && b->type == BrickType::End) ++n;
        return n;
    };
    const int l = parts(kPortLeft), r = parts(kPortRight);
    if (r > 1 && l <= 1) return 1;
    if (l > 1 && r <= 1) return 0;
    if (r > 0 && l == 0) return 1;
    return 0;
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
    // A FANOUT OR A TWISTED RUN CARRIED ON INTO A CABLE OR HEATSHRINK: two specials joined end to end.
    // Each gives that side to the other; the wires pass straight through, packed.
    if (brickIsSpecial(x->type) && brickIsSpecial(y->type) &&
        (x->type == BrickType::Run || y->type == BrickType::Run)) {
        if (linked(a, b)) return refuse(QStringLiteral("Those two are already linked"));
        auto freePort = [&](const Brick& self, const Brick& other, int asked) {
            if (asked >= 0) return sideOf(self.id, asked).isEmpty() ? asked : -1;
            int p = std::max(0, facingPort(self, Vec2{other.col - self.col, other.row - self.row}));
            if (!sideOf(self.id, p).isEmpty()) p = 1 - p;
            return sideOf(self.id, p).isEmpty() ? p : -1;
        };
        const int pa = freePort(*x, *y, portA), pb = freePort(*y, *x, portB);
        if (pa < 0 || pb < 0)
            return refuse(QStringLiteral("A run joined to another has that end to itself - free an end first"));
        links.push_back({a, b, pa, pb, 0});
        if (why) why->clear();
        return true;
    }
    if (brickIsSpecial(x->type) == brickIsSpecial(y->type))
        return refuse(x->type == BrickType::End
                          ? QStringLiteral("Two end parts are joined by a harness: put a special "
                                           "between them and link each end to it")
                          : QStringLiteral("Two runs are joined when one is a cable / heatshrink run - a "
                                           "fanout or a twisted run carried on into it"));
    if (linked(a, b)) return refuse(QStringLiteral("Those two are already linked"));
    // Which side of the special it goes on, and which side of the end.
    const bool aIsSpecial = brickIsSpecial(x->type);
    const Brick& sp = aIsSpecial ? *x : *y;
    const Brick& en = aIsSpecial ? *y : *x;
    int sPort = aIsSpecial ? portA : portB;
    int ePort = aIsSpecial ? portB : portA;
    const QStringList starts = startsOf(sp.id), finishes = finishesOf(sp.id);
    auto taken = [&](int port) {
        int n = static_cast<int>((port == kPortLeft ? starts : finishes).size());
        for (const QString& o : sideOf(sp.id, port))
            if (const Brick* ob = brick(o); ob && brickIsSpecial(ob->type)) n += 100;   // joined: full
        return n;
    };
    const bool fanout = sp.type == BrickType::Fanout;
    // WHICH SIDES MAY TAKE SEVERAL PARTS: a fanout's, and a cable's where it has a fan to split.
    auto several = [&](int port) {
        if (fanout) return true;
        if (sp.type != BrickType::Run) return false;
        return (port == kPortRight ? sp.run.fanFinishMm : sp.run.fanStartMm) != 0.0;
    };
    if (sPort < 0) {
        sPort = std::max(0, facingPort(sp, Vec2{en.col - sp.col, en.row - sp.row}));
        // The side facing it is already used and the other side is free: the other side - a run
        // starts somewhere before it finishes anywhere, a fanout included.
        if (taken(sPort) >= 1 && taken(1 - sPort) == 0) sPort = 1 - sPort;
    }
    if (!several(sPort) && taken(sPort) >= 1)
        return refuse(QStringLiteral("That end of the run is already on a part - a fanout, or a "
                                     "cable with a fan there, finishes on several"));
    if (several(sPort) && taken(sPort) >= 1 && taken(1 - sPort) >= 2)
        return refuse(QStringLiteral("A fanout spreads from one part to several, not several to "
                                     "several"));
    const int enPorts = portCount(en);
    if (ePort >= enPorts || sPort > kPortRight)
        return refuse(QStringLiteral("That brick has no such port"));
    // AN END ON ITS FIRST LINK TURNS TO FACE IT: the port it is linked on toward the run - which
    // is the way round anybody means a part they have only just joined to something.
    const bool firstLink = linksOf(en.id).isEmpty();
    // Toward it along whichever way it mostly lies, so a part linked on the slant turns to face
    // along the harness rather than across it.
    const Vec2 rawToward{sp.col - en.col, sp.row - en.row};
    const Vec2 toward = std::abs(rawToward.x) >= std::abs(rawToward.y)
                            ? Vec2{rawToward.x >= 0.0 ? 1.0 : -1.0, 0.0}
                            : Vec2{0.0, rawToward.y >= 0.0 ? 1.0 : -1.0};
    if (ePort < 0) ePort = std::max(0, facingPort(en, toward));
    if (firstLink && toward.length() > 1e-9) {
        Brick* e = brick(en.id);
        int bestRot = e->rot;
        double bestDot = -1e300;
        for (int r = 0; r < 4; ++r) {
            Brick t = *e;
            t.rot = r;
            const double d = dot(portPaneDir(t, ePort), toward);
            if (d > bestDot + 1e-9) {
                bestDot = d;
                bestRot = r;
            }
        }
        e->rot = bestRot;
    }
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
                      {"row", b.row},      {"rot", b.rot},             {"balloon", b.balloon},
                      {"showDesignator", b.showDesignator}, {"designatorHeightMm", b.designatorHeightMm}};
        if (std::abs(b.scale - 1.0) > 1e-9) o.insert(QStringLiteral("scale"), b.scale);
        if (b.autoScale) o.insert(QStringLiteral("autoScale"), true);
        if (brickIsSpecial(b.type)) {
            const BrickRunSpec& r = b.run;
            QJsonArray labels;
            for (const BrickLabel& l : r.labels)
                labels.append(QJsonObject{{"lengthMm", l.lengthMm},
                                          {"clearShrink", l.clearShrink},
                                          {"leader", l.leader},
                                          {"wire", l.wire},
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
                                                   {"transitionWidthMm", r.transitionWidthMm},
                                                   {"dimTotal", r.dimTotal},
                                                   {"dimFans", r.dimFans},
                                                   {"dimBetween", r.dimBetween},
                                                   {"labels", labels},
                                                   {"nets", QJsonArray::fromStringList(r.nets)},
                                                   {"dimTextTotal", r.dimTextTotal},
                                                   {"dimTextFans", r.dimTextFans},
                                                   {"dimTextBetween", r.dimTextBetween},
                                                   {"fanSide", r.fanSide},
                                                   {"dimLinear", r.dimLinear},
                                                   {"dimTrace", r.dimTrace},
                                                   {"dimSingle", r.dimSingle},
                                                   {"trunkWires", r.trunkWires}};
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
        b.showDesignator = bo.value(QStringLiteral("showDesignator")).toBool(false);
        b.designatorHeightMm = std::clamp(bo.value(QStringLiteral("designatorHeightMm")).toDouble(3.5), 0.5, 50.0);
        b.scale = std::clamp(bo.value(QStringLiteral("scale")).toDouble(1.0), 0.05, 20.0);
        b.autoScale = bo.value(QStringLiteral("autoScale")).toBool();
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
            s.transitionWidthMm = std::max(0.0, r.value(QStringLiteral("transitionWidthMm")).toDouble(0.0));
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
                l.lengthMm = lo.value(QStringLiteral("lengthMm")).toDouble(0.0);
                l.leader = lo.value(QStringLiteral("leader")).toBool(true);
                l.clearShrink = lo.value(QStringLiteral("clearShrink")).toBool(false);
                s.labels.push_back(l);
            }
            for (const QJsonValue x : r.value(QStringLiteral("nets")).toArray()) s.nets << x.toString();
            s.dimTextTotal = r.value(QStringLiteral("dimTextTotal")).toString();
            s.dimTextFans = r.value(QStringLiteral("dimTextFans")).toString();
            s.dimTextBetween = r.value(QStringLiteral("dimTextBetween")).toString();
            s.fanSide = r.value(QStringLiteral("fanSide")).toInt(-1);
            s.dimLinear = r.value(QStringLiteral("dimLinear")).toBool(true);
            s.dimTrace = r.value(QStringLiteral("dimTrace")).toBool(false);
            s.dimSingle = r.value(QStringLiteral("dimSingle")).toBool(false);
            s.trunkWires = r.value(QStringLiteral("trunkWires")).toInt(0);
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

    // THE PARTS NOT LINKED YET are laid out symmetrically too: each column of them - in the order they are
    // in, left where they are along the harness - spread evenly either side of one axis. Three connectors
    // put down before any cable was drawn stayed where they had been dropped, and Symmetric did nothing.
    {
        std::map<long long, QStringList> byCol;
        int loose = 0;
        for (const Brick& b : design.bricks)
            if (b.type == BrickType::End && !joined.contains(b.id)) {
                byCol[std::llround(b.col * 2.0)] << b.id;
                ++loose;
            }
        if (loose > 0) {
            int tallest = 1;
            for (auto& [c, ids] : byCol) {
                std::stable_sort(ids.begin(), ids.end(), [&](const QString& x, const QString& y) {
                    return design.brick(x)->row < design.brick(y)->row;
                });
                tallest = std::max(tallest, static_cast<int>(ids.size()));
            }
            // Where nothing has been arranged yet, about where the parts already are; otherwise below.
            double axis = rowBase + (tallest - 1) / 2.0;
            if (rowBase == 0.0) {
                double sum = 0.0;
                for (auto& [c, ids] : byCol)
                    for (const QString& id : ids) sum += design.brick(id)->row;
                axis = std::round(sum / loose * 2.0) / 2.0;
            }
            for (auto& [c, ids] : byCol) {
                const int n = static_cast<int>(ids.size());
                for (int i = 0; i < n; ++i) design.brick(ids[i])->row = axis + (i - (n - 1) / 2.0) * 2.0;
            }
        }
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
        std::vector<Vec2> want(static_cast<size_t>(std::max(1, design.portCount(b))), Vec2{0.0, 0.0});
        bool any = false;
        for (const QString& o : design.linksOf(b.id)) {
            const Brick* ob = design.brick(o);
            const int p = design.portOn(b.id, o);
            if (!ob || p < 0 || p >= static_cast<int>(want.size())) continue;
            want[p] += Vec2{ob->col - b.col, ob->row - b.row};
            any = true;
        }
        if (!any) continue;
        int best = b.rot, bestScore = -1;
        double bestVal = -1e300;
        for (int r = 0; r < 4; ++r) {
            Brick t = b;
            t.rot = r;
            double v = 0.0;
            for (size_t p = 0; p < want.size(); ++p)
                v += dot(design.portPaneDir(t, static_cast<int>(p)), want[p]);
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
        // Level, or as near as makes no difference: straight across. A shift of a millimetre or two - two parts
        // in one row whose faces sit a hair apart - was drawn as a level shift, and its two bends, each with
        // its radius, came out as a pair of little hooks in the middle of a straight cable.
        if (std::abs(side) < std::max(1e-6, std::min(3.0, fwd * 0.02)) && fwd > 0.0) return done();
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

    // ON THE SLANT TO ONE ANOTHER (a Y-split's leg into a part): one bend where the two lines meet, when that
    // is ahead of both; else out, straight across to the other's lead, and in - never a turn back on itself.
    if (std::abs(dot(u, w)) >= 1e-3 && std::abs(cross(u, w)) > 1e-3) {
        const double t = cross(d, w) / cross(u, w);
        const double s = cross(d, u) / cross(u, w);
        if (t >= lead - 1e-9 && s >= lead - 1e-9) {
            path.push_back(a + u * t);
            return done();
        }
        path.push_back(a + u * lead);
        path.push_back(b + w * lead);
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

BrickShape specialShape() {
    BrickShape s;
    s.faceMid = {Vec2{-40.0, 0.0}, Vec2{40.0, 0.0}};
    s.faceOut = {Vec2{-1.0, 0.0}, Vec2{1.0, 0.0}};
    s.faceLen = {10.0, 10.0};
    s.half = Vec2{40.0, 15.0};
    return s;
}

namespace {
BrickShape endShapeAsDrawn(const Brick& b, const PartsCatalog* catalog);
} // namespace

// An end's shape at the size it is drawn: the drawing, its faces and its ends all scaled about its middle,
// so what joins it meets it at the faces where they now are.
BrickShape endShape(const Brick& b, const PartsCatalog* catalog) {
    BrickShape s = endShapeAsDrawn(b, catalog);
    const double k = std::clamp(b.scale, 0.05, 20.0);
    if (std::abs(k - 1.0) < 1e-9) return s;
    for (Entity& e : s.drawing) scaleEntity(e, Vec2{0.0, 0.0}, k);
    for (Vec2& m : s.faceMid) m = m * k;
    for (double& len : s.faceLen) len *= k;
    s.half = s.half * k;
    s.nearAt = s.nearAt * k;
    s.farAt = s.farAt * k;
    return s;
}

namespace {
BrickShape endShapeAsDrawn(const Brick& b, const PartsCatalog* catalog) {
    BrickShape s;
    // Placed at its size (placeLibraryPartAtFace sizes a part drawn to proportion by its entry face).
    const LibraryPart* part = catalog && !b.libPart.isEmpty() ? catalog->byId(b.libPart) : nullptr;
    if (part && part->hasMechanical()) {
        // THE LIBRARY PART, set down by its first face: looking right when it has only that one, left
        // when it has more - the way in of a joint - and every other face wherever the part has it.
        const bool many = part->outputs.size() >= 2;
        MechDrawOptions opt;
        const PlacedLibraryPart placed = placeLibraryPartAtFace(
            *part, Vec2{0.0, 0.0}, many ? M_PI : 0.0, many ? Vec2{1.0, 0.0} : Vec2{-1.0, 0.0}, opt);
        Bounds bb;
        for (const Entity& e : placed.entities) bb.expand(e.bounds());
        const Vec2 c = bb.valid ? (bb.min + bb.max) * 0.5 : Vec2{0.0, 0.0};
        s.drawing = placed.entities;
        for (Entity& e : s.drawing) translateEntity(e, c * -1.0);
        s.half = bb.valid ? (bb.max - bb.min) * 0.5 : Vec2{12.0, 10.0};
        if (!placed.faces.empty()) {
            for (size_t i = 0; i < placed.faces.size(); ++i) {
                const Vec2 a = placed.faces[i].first - c, e = placed.faces[i].second - c;
                const Vec2 mid = (a + e) * 0.5;
                Vec2 n = (e - a).perp().normalized();
                if (i == 0) n = many ? Vec2{-1.0, 0.0} : Vec2{1.0, 0.0};
                else if (dot(n, mid) < 0.0) n = n * -1.0;   // away from the middle of the part
                s.faceMid.push_back(mid);
                s.faceOut.push_back(n);
                s.faceLen.push_back(std::max(0.5, distance(a, e)));
            }
        } else {
            // No face marked: the one it was set down by, at the joint, looking right.
            s.faceMid.push_back(c * -1.0);
            s.faceOut.push_back(Vec2{1.0, 0.0});
            s.faceLen.push_back(placed.faceLengthMm > 1e-6 ? placed.faceLengthMm : s.half.y * 2.0);
        }
        if (placed.haveEnds) {
            s.haveEnds = true;
            s.nearAt = placed.nearAt - c;
            s.farAt = placed.farAt - c;
        }
        return s;
    }
    // A PLACEHOLDER of the kind's rough size, with the kind's usual faces.
    const Placeholder ph = placeholderFor(b.kind);
    const double hx = ph.along / 2.0, hy = ph.across / 2.0;
    s.half = Vec2{hx, hy};
    const int faces = brickKindFaceCount(b.kind);
    if (faces == 1) {
        s.faceMid = {Vec2{hx, 0.0}};
        s.faceOut = {Vec2{1.0, 0.0}};
        s.faceLen = {ph.across};
    } else if (faces == 2) {
        s.faceMid = {Vec2{-hx, 0.0}, Vec2{hx, 0.0}};
        s.faceOut = {Vec2{-1.0, 0.0}, Vec2{1.0, 0.0}};
        s.faceLen = {ph.across, ph.across};
    } else {
        // A Y: one way in on the left, two ways out on the right, one above the other.
        s.faceMid = {Vec2{-hx, 0.0}, Vec2{hx, hy / 2.0}, Vec2{hx, -hy / 2.0}};
        s.faceOut = {Vec2{-1.0, 0.0}, Vec2{1.0, 0.0}, Vec2{1.0, 0.0}};
        s.faceLen = {ph.across * 0.8, ph.across * 0.42, ph.across * 0.42};
    }
    Entity box;
    box.type = EntityType::Polyline;
    box.points = {Vec2{-hx, -hy}, Vec2{hx, -hy}, Vec2{hx, hy}, Vec2{-hx, hy}, Vec2{-hx, -hy}};
    box.a = box.points.front();
    box.b = box.points.back();
    s.drawing.push_back(box);
    s.drawing.push_back(makeTextAt(Vec2{-hx, hy + 2.0}, b.name.isEmpty() ? partKindName(b.kind) : b.name, 3.0));
    return s;
}
} // namespace

BrickShape BrickDesign::shapeOf(const Brick& b) const {
    if (b.type != BrickType::End) return specialShape();
    QString key = partKindKey(b.kind) + QLatin1Char('|') + b.libPart + QLatin1Char('|') + b.name +
                        QLatin1Char('|') + QString::number(reinterpret_cast<quintptr>(catalog)) +
                        QLatin1Char('|') + QString::number(b.scale, 'g', 10);
    // And the size its part is drawn at, which the library can change while the wizard is open.
    const QString sized = [&] {
        const LibraryPart* p = catalog && !b.libPart.isEmpty() ? catalog->byId(b.libPart) : nullptr;
        return p ? QString::number(partSizeFactor(*p), 'g', 10) : QString();
    }();
    key += QLatin1Char('|') + sized;
    const auto it = shapeCache.constFind(key);
    if (it != shapeCache.constEnd()) return it.value();
    const BrickShape s = endShape(b, catalog);
    shapeCache.insert(key, s);
    return s;
}

BrickBuild buildBrickHarness(const BrickDesign& design, const PartsCatalog* catalog,
                             const std::function<int(const QString& libPart)>& itemOf) {
    // AUTO SCALE: an end left on Auto is made the size of the drawing's usual entry - its entry faces as
    // long, on average, as the entry faces of all the parts on the drawing - so the parts read at one size
    // and the wires meet faces alike. The whole part scales, its near and far points and its faces with it.
    // Worked out on a first drawing, then drawn again at that size.
    bool anyAuto = false;
    for (const Brick& b : design.bricks) anyAuto = anyAuto || (b.type == BrickType::End && b.autoScale);
    if (anyAuto) {
        BrickDesign first = design;
        first.shapeCache.clear();
        for (Brick& b : first.bricks)
            if (b.autoScale) {
                b.autoScale = false;
                b.scale = 1.0;
            }
        const BrickBuild probe = buildBrickHarness(first, catalog, itemOf);
        BrickDesign sized = design;
        sized.shapeCache.clear();
        // The drawing's usual entry: the mean length of every entry face of every part on it.
        double sum = 0.0;
        int count = 0;
        for (auto it = probe.faces.cbegin(); it != probe.faces.cend(); ++it)
            if (it.value().lengthMm > 1e-6) {
                sum += it.value().lengthMm;
                ++count;
            }
        const double mean = count > 0 ? sum / count : 0.0;
        for (Brick& b : sized.bricks) {
            if (!b.autoScale) continue;
            b.autoScale = false;
            // This part's own entries, at the size it was first drawn.
            double own = 0.0;
            int n = 0;
            for (auto it = probe.faces.cbegin(); it != probe.faces.cend(); ++it)
                if (it.key().startsWith(b.id + QLatin1Char(':')) && it.value().lengthMm > 1e-6) {
                    own += it.value().lengthMm;
                    ++n;
                }
            b.scale = (n > 0 && mean > 1e-6) ? std::clamp(mean / (own / n), 0.1, 10.0) : 1.0;
        }
        BrickBuild done = buildBrickHarness(sized, catalog, itemOf);
        return done;
    }
    BrickBuild out;
    MechDrawOptions opt;
    for (const Brick& b : design.bricks)
        if (b.type == BrickType::End) out.scales.insert(b.id, b.scale);

    // WHAT EACH END IS: its shape, unturned, with a port for every connection face.
    QHash<QString, BrickShape> shapes;
    double widest = 0.0;
    for (const Brick& b : design.bricks) {
        if (b.type != BrickType::End) continue;
        const BrickShape s = endShape(b, catalog);
        widest = std::max({widest, s.half.x * 2.0, s.half.y * 2.0});
        shapes.insert(b.id, s);
    }
    const double rowPitch = std::max(40.0, widest + 25.0);

    // THE NODES: every end, and a JUNCTION wherever two specials are joined - a fanout or a twisted
    // run carried on into a cable. At a junction the wires pass straight through, packed.
    struct Junction {
        QString a, b;
        Vec2 grid;   // col, row on the pane
    };
    QHash<QString, Junction> junctions;
    auto junctionKey = [](QString a, QString b) {
        if (b < a) std::swap(a, b);
        return QStringLiteral("~J") + a + QLatin1Char('|') + b;
    };
    for (const BrickLink& l : design.links) {
        const Brick* a = design.brick(l.a);
        const Brick* b = design.brick(l.b);
        if (!a || !b || !brickIsSpecial(a->type) || !brickIsSpecial(b->type)) continue;
        junctions.insert(junctionKey(l.a, l.b),
                         {l.a, l.b, Vec2{(a->col + b->col) / 2.0, (a->row + b->row) / 2.0}});
    }
    // A face a special lands on, from its side: an end's port, or its side of a junction.
    struct FaceRef {
        QString id;   // face owner: an end's id, or "<junction>@<special>"
        int port = 0;
        QString node; // the node it is at: the end, or the junction
    };
    auto faceOf = [&](const QString& special, const QString& other) {
        FaceRef f;
        const Brick* o = design.brick(other);
        if (o && o->type == BrickType::End) {
            f.id = other;
            f.port = design.portOn(other, special);
            f.node = other;
        } else {
            f.node = junctionKey(special, other);
            f.id = f.node + QLatin1Char('@') + special;
        }
        return f;
    };
    auto gridOf = [&](const QString& node) {
        if (const Brick* b = design.brick(node)) return Vec2{b->col, b->row};
        return junctions.value(node).grid;
    };

    // THE BRANCHES: every stretch of harness from one face to another. A run is one; a fanout is one
    // to each part it finishes on; a cable finishing on several parts runs as one to where it splits.
    struct Branch {
        const Brick* special = nullptr;
        FaceRef a, b;          // b.id empty: a loose end
        bool freeStart = false; // a trunk with nothing at its start: it runs out loose from the split
        int wires = 2;
        bool trunk = false;
        int splitOf = -1;
        bool flipped = false;   // built from the special's finish side to its start
    };
    std::vector<Branch> branches;
    for (const Brick& s : design.bricks) {
        if (!brickIsSpecial(s.type)) continue;
        QList<FaceRef> a, b;
        for (const QString& o : design.sideOf(s.id, kPortLeft)) a << faceOf(s.id, o);
        for (const QString& o : design.sideOf(s.id, kPortRight)) b << faceOf(s.id, o);
        if (a.isEmpty() && b.isEmpty()) {
            out.problems << QStringLiteral("%1 is linked to nothing").arg(s.name.isEmpty() ? s.id : s.name);
            continue;
        }
        // SEVERAL ON THE START SIDE is the same harness the other way round.
        bool flipped = false;
        if (a.size() > 1 && b.size() <= 1) {
            std::swap(a, b);
            flipped = true;
        }
        if (a.isEmpty() && b.size() == 1) {   // on one part only: from it, loose
            std::swap(a, b);
            flipped = !flipped;
        }
        const bool several = (s.type == BrickType::Fanout || s.type == BrickType::Run) && b.size() > 1;
        if (several) {
            int trunkAt = -1;
            // One body up to where it divides - a cable's, or a fanout's gathered wires - and from
            // there only the spread is divided between the parts.
            if (s.type == BrickType::Run || s.type == BrickType::Fanout) {
                Branch t;
                t.special = &s;
                if (!a.isEmpty()) t.a = a.front();
                else t.freeStart = true;
                t.trunk = true;
                t.wires = 0;
                trunkAt = static_cast<int>(branches.size());
                branches.push_back(t);
            }
            // Each part it finishes on takes the wires its link says, or an even share of the rest.
            int given = 0, open = 0;
            QList<int> said;
            for (const FaceRef& f : b) {
                const BrickLink* l = design.link(s.id, f.node == f.id ? f.id : QString());
                const int w = l && l->wires > 0 ? l->wires : 0;
                said << w;
                if (w > 0) given += w;
                else ++open;
            }
            const int rest = std::max(0, s.run.wires - given);
            int extra = open > 0 ? rest % open : 0;
            const int base = open > 0 ? rest / open : 0;
            for (int k = 0; k < b.size(); ++k) {
                int share = said[k];
                if (share <= 0) {
                    share = base;
                    if (extra > 0) {
                        ++share;
                        --extra;
                    }
                }
                Branch br;
                br.special = &s;
                br.flipped = flipped || (trunkAt < 0 && a.isEmpty());
                br.wires = std::max(1, share);
                if (trunkAt >= 0) {
                    br.a = a.isEmpty() ? FaceRef{} : a.front();
                    br.b = b[k];
                    br.splitOf = trunkAt;
                    branches[static_cast<size_t>(trunkAt)].wires += br.wires;
                } else if (!a.isEmpty()) {
                    br.a = a.front();
                    br.b = b[k];
                } else {
                    br.a = b[k];   // a fanout from nothing: each part fans out loose
                }
                branches.push_back(br);
            }
            continue;
        }
        Branch br;
        br.special = &s;
        br.flipped = flipped;
        br.a = a.front();
        if (!b.isEmpty()) br.b = b.front();
        br.wires = s.type == BrickType::TwistedPair ? 2 : std::max(1, s.run.wires);
        branches.push_back(br);
    }

    // WHERE EVERYTHING IS: laid out by the LENGTHS of the runs from the part they share, not by the
    // columns of the pane - two runs of different lengths to one connector put their far ends at
    // different distances from it. Across the harness, the rows of the pane stand.
    QHash<QString, double> xOf;
    QHash<QString, double> stretchOf;   // how much longer a run is drawn than it is, for room to see it
    {
        struct Edge {
            QString to;
            double gap;       // along the harness, from the one face to the other
            Vec2 fromFace;    // where the face is on this node, from its centre (turned)
            Vec2 toFace;      // and on the other
        };
        QHash<QString, QList<Edge>> edges;
        // Where a face is on its part, from the part's centre, turned as the brick is; a junction is a point.
        auto faceAt = [&](const FaceRef& f) {
            const Brick* b = design.brick(f.node);
            const auto it = shapes.constFind(f.node);
            if (!b || b->type != BrickType::End || it == shapes.constEnd() || f.port < 0 ||
                f.port >= it->ports())
                return Vec2{0.0, 0.0};
            return turnQuarters(it->faceMid[static_cast<size_t>(f.port)], b->rot);
        };
        // A RUN'S LENGTH IS FROM FACE TO FACE, and it goes across the harness as well as along it: a run
        // to a part a row or two away climbs that far, so only what is left of its length is laid along
        // the harness. Measured from the centres, the parts on the far rows came in level with where they
        // started from and the wires had to turn back to reach them.
        //
        // ROOM TO SEE: two parts facing each other are never drawn closer than the fans between them need
        // - a short run would put one connector on top of the other. The run is drawn that much longer, and
        // its dimensions still say how long it really is (see stretchOf).
        constexpr double kRoomBetween = 60.0;
        constexpr double kStraightIn = 40.0;   // straight into a part after the last bend
        // Which way a face looks, turned as the brick is; a junction's is left to the run.
        auto faceOutOf = [&](const FaceRef& f) {
            const Brick* b = design.brick(f.node);
            const auto it = shapes.constFind(f.node);
            if (!b || b->type != BrickType::End || it == shapes.constEnd() || f.port < 0 ||
                f.port >= it->ports())
                return Vec2{0.0, 0.0};
            return turnQuarters(it->faceOut[static_cast<size_t>(f.port)], b->rot).normalized();
        };
        for (const Branch& br : branches) {
            if (br.a.node.isEmpty() || br.b.node.isEmpty()) continue;
            const double real = br.special->run.lengthMm;
            const Vec2 fa = faceAt(br.a);
            const Vec2 fb = faceAt(br.b);
            const double dy = (-gridOf(br.b.node).y * rowPitch + fb.y) - (-gridOf(br.a.node).y * rowPitch + fa.y);
            const double across = std::abs(dy);
            // AS FEW BENDS AS THE LENGTH ALLOWS. A face that looks out on the slant - a Y-split's leg - toward
            // the part: the run goes straight on out of it up to the other part's height and makes ONE bend
            // into it, so the part is put where that one-bend run is the run's length. Put anywhere nearer, the
            // wires ran on the slant past the part and came back to it in a hook.
            // Along the harness, which way from a to b (as the layout below will put it).
            const double ahead = gridOf(br.b.node).x - gridOf(br.a.node).x;
            auto slanted = [&](const Vec2& u, double toward, double forward) {
                return std::abs(u.x) > 0.1 && std::abs(u.y) > 0.1 && across > 1e-6 && std::abs(forward) > 1e-9 &&
                       (u.y > 0.0) == (toward > 0.0) && (u.x > 0.0) == (forward > 0.0);
            };
            const Vec2 ua = faceOutOf(br.a), ub = faceOutOf(br.b);
            const bool slantA = slanted(ua, dy, ahead), slantB = slanted(ub, -dy, -ahead);
            double gap = 0.0, drawn = 0.0;
            if (slantA || slantB) {
                const Vec2 u = slantA ? ua : ub;
                const double leg = across / std::abs(u.y);           // on the slant
                const double legAlong = across * std::abs(u.x) / std::abs(u.y);
                gap = std::max({real - leg + legAlong, legAlong + kStraightIn, kRoomBetween});
                drawn = leg + gap - legAlong;
            } else {
                // Square out of both faces: out, across on the slant (45 degrees), and in - which is longer than
                // the two straight legs by the slant's extra.
                const double extra = across * (std::sqrt(2.0) - 1.0);
                gap = std::max(real - extra, kRoomBetween);
                const double slant = std::min(across, std::max(0.0, gap - 2.0 * kStraightIn));
                drawn = gap - slant + std::hypot(slant, across);
            }
            if (drawn > real + 1e-6)
                stretchOf.insert(br.special->id, std::max(stretchOf.value(br.special->id), drawn - real));
            edges[br.a.node] << Edge{br.b.node, gap, fa, fb};
            edges[br.b.node] << Edge{br.a.node, gap, fb, fa};
        }
        QStringList nodes;
        for (const Brick& b : design.bricks)
            if (b.type == BrickType::End) nodes << b.id;
        for (auto it = junctions.begin(); it != junctions.end(); ++it) nodes << it.key();
        while (true) {
            QString root;
            for (const QString& n : nodes) {
                if (xOf.contains(n)) continue;
                if (root.isEmpty() || edges.value(n).size() > edges.value(root).size() ||
                    (edges.value(n).size() == edges.value(root).size() && gridOf(n).x < gridOf(root).x))
                    root = n;
            }
            if (root.isEmpty()) break;
            xOf.insert(root, gridOf(root).x * 75.0);
            std::deque<QString> queue{root};
            while (!queue.empty()) {
                const QString n = queue.front();
                queue.pop_front();
                for (const Edge& e : edges.value(n)) {
                    if (xOf.contains(e.to)) continue;
                    const double dc = gridOf(e.to).x - gridOf(n).x;
                    const double sign = dc > 1e-9 ? 1.0 : dc < -1e-9 ? -1.0 : 0.0;
                    // Face to face along the harness; one above the other, centre over centre.
                    xOf.insert(e.to, sign == 0.0 ? xOf.value(n)
                                                 : xOf.value(n) + e.fromFace.x + sign * e.gap - e.toFace.x);
                    queue.push_back(e.to);
                }
            }
        }
    }
    auto posOf = [&](const QString& node) { return Vec2{xOf.value(node), -gridOf(node).y * rowPitch}; };

    // THE ENDS: each its shape turned by the brick's quarters and set down on its place, a face for
    // every port, and its designator in front of it.
    for (const Brick& b : design.bricks) {
        if (b.type != BrickType::End) continue;
        const BrickShape& shape = shapes[b.id];
        const Vec2 at = posOf(b.id);
        const double turn = -b.rot * M_PI / 2.0;
        auto place = [&](const Vec2& v) { return at + turnQuarters(v, b.rot); };
        std::vector<Entity> body;
        for (Entity e : shape.drawing) {
            // The placeholder's own caption gives way to the designator, written below.
            if (e.type == EntityType::Text && !b.libPart.isEmpty()) continue;
            if (e.type == EntityType::Text) continue;
            if (b.rot % 4 != 0) rotateEntity(e, Vec2{0.0, 0.0}, turn);
            translateEntity(e, at);
            e.layer = opt.layer;
            body.push_back(e);
        }
        for (int p = 0; p < shape.ports(); ++p) {
            BrickFace f;
            f.centre = place(shape.faceMid[static_cast<size_t>(p)]);
            f.out = turnQuarters(shape.faceOut[static_cast<size_t>(p)], b.rot);
            f.lengthMm = shape.faceLen[static_cast<size_t>(p)];
            const Vec2 o = shape.faceOut[static_cast<size_t>(p)];
            const double depth = std::abs(o.x) * shape.half.x * 2.0 + std::abs(o.y) * shape.half.y * 2.0;
            f.nearAt = shape.haveEnds ? place(shape.nearAt) : f.centre;
            f.farAt = shape.haveEnds ? place(shape.farAt) : f.centre - f.out * depth;
            out.faces.insert(faceKey(b.id, p), f);
        }
        Bounds bb;
        for (const Entity& e : body) bb.expand(e.bounds());
        // THE DESIGNATOR, IN FRONT OF THE PART: past the end of it that faces away from the harness - the
        // mating end of a connector - or above a joint, which has no such end. Only where the part's eye is on,
        // at the height it says.
        if (b.showDesignator) {
            const QString said = b.name.isEmpty() ? partKindName(b.kind) : b.name;
            const double tall = std::max(0.5, b.designatorHeightMm);
            Entity t;
            if (shape.ports() == 1 && bb.valid) {
                const Vec2 out1 = turnQuarters(shape.faceOut[0], b.rot);
                const Vec2 c = (bb.min + bb.max) * 0.5;
                const double reach = std::abs(out1.x) * (bb.max.x - bb.min.x) / 2.0 +
                                     std::abs(out1.y) * (bb.max.y - bb.min.y) / 2.0;
                t = makeTextAt(c - out1 * (reach + 3.0), said, tall);
                t.meta.insert(QStringLiteral("justify"),
                              std::abs(out1.x) >= std::abs(out1.y)
                                  ? (out1.x > 0.0 ? QStringLiteral("MR") : QStringLiteral("ML"))
                                  : (out1.y > 0.0 ? QStringLiteral("TC") : QStringLiteral("BC")));
            } else {
                t = makeTextAt(bb.valid ? Vec2{(bb.min.x + bb.max.x) / 2.0, bb.max.y + 2.0} : at, said, tall);
                t.meta.insert(QStringLiteral("justify"), QStringLiteral("BC"));
            }
            t.meta.insert(QStringLiteral("mechRole"), QStringLiteral("designator"));
            t.layer = opt.layer;
            body.push_back(t);
        }
        // A BALLOON, its leader landing on the top edge of the part, numbered by its line of the bill.
        if (b.balloon && bb.valid) {
            const Vec2 tip{(bb.min.x + bb.max.x) / 2.0, bb.max.y};
            const Vec2 bat{tip.x - 18.0, bb.max.y + 16.0};
            const int item = itemOf && !b.libPart.isEmpty() ? itemOf(b.libPart) : 0;
            Entity ball;
            ball.type = EntityType::Circle;
            ball.center = bat;
            ball.a = bat;
            ball.radius = 4.5;
            ball.meta.insert(QStringLiteral("balloon"), true);
            ball.meta.insert(QStringLiteral("text"), item > 0 ? QString::number(item) : QStringLiteral("?"));
            ball.meta.insert(QStringLiteral("landing"), 6.0);
            ball.meta.insert(QStringLiteral("landingSide"), tip.x >= bat.x ? 1.0 : -1.0);
            // Built a step at a time: QJsonArray{QJsonArray{x, y}} is a COPY of the inner array, not an
            // array holding it, and the leaders came out as [x, y] - which the renderer reads as no tip.
            QJsonArray point;
            point.append(tip.x);
            point.append(tip.y);
            QJsonArray leaders;
            leaders.append(point);
            ball.meta.insert(QStringLiteral("leaders"), leaders);
            ball.meta.insert(QStringLiteral("mechRole"), QStringLiteral("balloon"));
            ball.meta.insert(QStringLiteral("arrowSize"), 2.2);
            body.push_back(ball);
        }
        stampBrick(body, b.id);
        for (Entity& e : body) out.entities.push_back(std::move(e));
    }

    // THE JUNCTIONS: a face each side, looking toward the special on that side, as wide as its wires
    // packed together.
    for (auto it = junctions.begin(); it != junctions.end(); ++it) {
        const Junction& j = it.value();
        const Vec2 at = posOf(it.key());
        for (const QString& sid : {j.a, j.b}) {
            const Brick* s = design.brick(sid);
            if (!s) continue;
            const Vec2 d{s->col - j.grid.x, s->row - j.grid.y};
            const Vec2 out1 = std::abs(d.x) >= std::abs(d.y) ? Vec2{d.x >= 0.0 ? 1.0 : -1.0, 0.0}
                                                             : Vec2{0.0, d.y >= 0.0 ? -1.0 : 1.0};
            // THE CABLE'S END is the face: as wide as the cable, which is what the wires go into.
            const Brick* cable = design.brick(j.a)->type == BrickType::Run ? design.brick(j.a) : design.brick(j.b);
            const double cww = std::max(0.1, cable->run.wireWidthMm);
            const int cw = std::max(1, cable->run.wires);
            BrickFace f;
            f.centre = at;
            f.out = out1;
            f.lengthMm = cable->run.bodyWidthMm > 1e-9 ? cable->run.bodyWidthMm
                                                       : cww * 1.5 * (cw - 1) + cww + 2.0;
            f.nearAt = f.farAt = at;
            out.faces.insert(faceKey(it.key() + QLatin1Char('@') + sid, 0), f);
        }
    }

    // WHERE ON ITS FACE EACH BRANCH LANDS. All the wires landing on a part's face are spread together
    // over the fan share of it - in the order of where they go, so they do not cross - each branch its
    // own stretch, never exceeding the face. At a junction nothing spreads: the wires go on packed.
    struct Landing {
        Vec2 centre;
        double lane = 1.0;
        double share = 1.0;
    };
    std::vector<Landing> atA(branches.size()), atB(branches.size());
    auto isJunction = [](const FaceRef& f) { return f.id.startsWith(QLatin1String("~J")); };
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
            if (!br.a.id.isEmpty() && br.splitOf < 0) {
                const QString ka = faceKey(br.a.id, br.a.port);
                const BrickFace fa = out.faces.value(ka);
                const Vec2 toB = br.b.id.isEmpty() ? fa.centre + fa.out : posOf(br.b.node);
                byFace[ka].push_back({i, false, br.wires, dot(toB, fa.out.perp())});
            }
            if (!br.b.id.isEmpty()) {
                const QString kb = faceKey(br.b.id, br.b.port);
                const BrickFace fb = out.faces.value(kb);
                const Vec2 toA = br.a.node.isEmpty() ? fb.centre + fb.out : posOf(br.a.node);
                byFace[kb].push_back({i, true, br.wires, dot(toA, fb.out.perp())});
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
            const bool junction = it.key().startsWith(QLatin1String("~J"));
            // At a junction the cable's own wires go on packed; a fan's enter across the drawing's
            // share of the cable's width (Options, Drawing: Auto scale fan).
            const bool cableSide = !uses.empty() && branches[uses.front().branch].special->type == BrickType::Run;
            const double spread = !junction ? scale / 100.0 * f.lengthMm
                                  : cableSide ? ww * 1.5 * (total - 1) + ww
                                              : design.faceFanPct / 100.0 * f.lengthMm;
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

    // WHERE A CABLE SPLITS: straight on from its part toward the parts it finishes on, short of the
    // nearest by room for the fans - or, with nothing at its start, a little way out in front of the
    // parts, the cable carrying on loose from there. Each run out of it starts side by side across the
    // cable's end, in the order of where they go.
    std::vector<Vec2> splitAt(branches.size()), splitDir(branches.size(), Vec2{1.0, 0.0});
    for (size_t t = 0; t < branches.size(); ++t) {
        if (!branches[t].trunk) continue;
        const BrickRunSpec& r = branches[t].special->run;
        const double ww = std::max(0.1, r.wireWidthMm);
        std::vector<size_t> subs;
        double fanMax = 0.0;
        Vec2 outSum{0.0, 0.0}, centreSum{0.0, 0.0};
        for (size_t k = 0; k < branches.size(); ++k) {
            if (branches[k].splitOf != static_cast<int>(t)) continue;
            subs.push_back(k);
            const int w = std::max(1, branches[k].wires);
            fanMax = std::max(fanMax, r.fanFinishMm > 0.0 ? r.fanFinishMm
                                                         : std::max(kWiresRunEnd, atB[k].lane * (w - 1) / 2.0));
            outSum += out.faces.value(faceKey(branches[k].b.id, branches[k].b.port)).out;
            centreSum += atB[k].centre;
        }
        if (subs.empty()) continue;
        Vec2 toward;   // the way the runs out of the split travel
        Vec2 at;
        if (!branches[t].freeStart) {
            const BrickFace fa = out.faces.value(faceKey(branches[t].a.id, branches[t].a.port));
            toward = fa.out;
            double nearest = 1e300;
            for (size_t k : subs) nearest = std::min(nearest, dot(atB[k].centre - atA[t].centre, fa.out));
            const double d = std::max(15.0, std::min(nearest * 0.45, nearest - fanMax - 10.0));
            at = atA[t].centre + fa.out * d;
        } else {
            const Vec2 faceOut = outSum.length() > 1e-9 ? outSum.normalized() : Vec2{-1.0, 0.0};
            toward = faceOut * -1.0;
            at = centreSum / static_cast<double>(subs.size()) + faceOut * (fanMax + 25.0);
            splitDir[t] = faceOut;   // the cable carries on away from the parts
        }
        splitAt[t] = at;
        std::stable_sort(subs.begin(), subs.end(), [&](size_t x, size_t y) {
            return dot(atB[x].centre, toward.perp()) < dot(atB[y].centre, toward.perp());
        });
        const double packed = ww * 1.5;
        double total = 0.0;
        for (size_t k : subs) total += std::max(1, branches[k].wires) * packed;
        double acc = -total / 2.0;
        for (size_t k : subs) {
            const double width = std::max(1, branches[k].wires) * packed;
            splitAt[k] = at + toward.perp() * (acc + width / 2.0);
            splitDir[k] = toward;
            acc += width;
        }
    }

    // A LABEL: the house label model - the one the Special ribbon puts on a harness - fitted across
    // what it goes round, along the run, MASKING what is under it so it reads as over the wire, kept
    // off any bend, with its text called out on a leader or written on it.
    std::optional<MechModel> labelModel, shrunkLabelModel;
    auto straightAround = [](const std::vector<Vec2>& path, double s, double& back, double& ahead) {
        // How far the path runs straight either side of the station, bends being turns of more than
        // a couple of degrees.
        back = ahead = 1e300;
        double acc = 0.0;
        for (size_t i = 1; i + 1 < path.size(); ++i) {
            acc += distance(path[i - 1], path[i]);
            const Vec2 d1 = (path[i] - path[i - 1]).normalized();
            const Vec2 d2 = (path[i + 1] - path[i]).normalized();
            if (std::abs(cross(d1, d2)) < std::sin(2.0 * M_PI / 180.0) && dot(d1, d2) > 0.0) continue;
            if (acc <= s) back = std::min(back, s - acc);
            else ahead = std::min(ahead, acc - s);
        }
    };
    auto labelEntities = [&](const std::vector<Vec2>& path, double s, double span, const QString& text,
                             bool small, const BrickLabel& lab) {
        if (!labelModel) {
            MechModelSet set;
            set.load();
            const MechModel* m = set.of(MechModelKind::Label);
            labelModel = m && !m->isEmpty() ? *m : builtInMechModel(MechModelKind::Label);
            const MechModel* h = set.of(MechModelKind::LabelUnderHeatshrink);
            shrunkLabelModel = h && !h->isEmpty() ? *h : builtInMechModel(MechModelKind::LabelUnderHeatshrink);
        }
        const MechModel& model = lab.clearShrink ? *shrunkLabelModel : *labelModel;
        Vec2 dir;
        const Vec2 mid0 = pointAlong(path, s, &dir);
        const double margin = model.exceedMm > 0.0 ? model.exceedMm
                                                         : std::max(1.0, span * 0.12);
        const double across = span + 2.0 * margin;
        double along = lab.lengthMm > 1e-9 ? lab.lengthMm
                       : small             ? std::max(8.0, across * 1.8)
                                           : std::max(18.0, across * 1.3);
        // KEPT OFF A BEND: no longer than the straight it sits on allows either side of it.
        double back, ahead;
        straightAround(path, s, back, ahead);
        const double room = 2.0 * std::min(back, ahead) - 1.0;
        if (room < along) along = std::max(3.0, room);
        std::vector<Entity> ents;
        Vec2 d = dir.normalized();
        if (d.x < -1e-9 || (std::abs(d.x) <= 1e-9 && d.y < 0.0)) d = d * -1.0;   // right way up
        const double turn = angleOf(d);
        // The mask first, so the label is drawn over the wires and not through them.
        {
            const Vec2 u = d * (along / 2.0), v = d.perp() * (across / 2.0);
            Entity w;
            w.type = EntityType::Wipeout;
            w.points = {mid0 - u - v, mid0 + u - v, mid0 + u + v, mid0 - u + v};
            w.meta.insert(QStringLiteral("noFrame"), true);
            w.meta.insert(QStringLiteral("labelMask"), true);
            w.meta.insert(QStringLiteral("mechRole"), QStringLiteral("label"));
            ents.push_back(w);
        }
        std::vector<Entity> drawn = mechModelEntities(model, Vec2{0.0, 0.0}, 0.0, along, across);
        Bounds bb;
        for (const Entity& e : drawn) bb.expand(e.bounds());
        const Vec2 c = bb.valid ? (bb.min + bb.max) * 0.5 : Vec2{0.0, 0.0};
        for (Entity& e : drawn) {
            translateEntity(e, c * -1.0);
            rotateEntity(e, Vec2{0.0, 0.0}, turn);
            translateEntity(e, mid0);
            e.meta.insert(QStringLiteral("mechRole"), QStringLiteral("label"));
            ents.push_back(e);
        }
        if (text.isEmpty()) return ents;
        if (lab.leader) {
            // CALLED OUT: a leader from the top of the label out to its text.
            const Vec2 up = d.perp().y >= 0.0 ? d.perp() : d.perp() * -1.0;
            const Vec2 tip = mid0 + up * (across / 2.0);
            const Vec2 at = tip + up * 12.0 + d * 10.0;
            Entity t;
            t.type = EntityType::Circle;
            t.center = at;
            t.a = at;
            t.radius = 2.5;
            t.meta.insert(QStringLiteral("balloon"), true);
            t.meta.insert(QStringLiteral("textLeader"), true);
            t.meta.insert(QStringLiteral("text"), text);
            t.meta.insert(QStringLiteral("landing"), 5.0);
            t.meta.insert(QStringLiteral("landingSide"), tip.x >= at.x ? 1.0 : -1.0);
            QJsonArray point;
            point.append(tip.x);
            point.append(tip.y);
            QJsonArray leaders;
            leaders.append(point);
            t.meta.insert(QStringLiteral("leaders"), leaders);
            t.meta.insert(QStringLiteral("arrowSize"), 1.8);
            t.meta.insert(QStringLiteral("mechRole"), QStringLiteral("label"));
            t.meta.insert(QStringLiteral("labelText"), text);
            t.meta.insert(QStringLiteral("labelMidX"), mid0.x);
            t.meta.insert(QStringLiteral("labelMidY"), mid0.y);
            ents.push_back(t);
        } else {
            Entity t = makeTextAt(mid0, text,
                                  std::clamp(across * 0.55, 1.2, along / std::max(1, int(text.size())) * 1.6), turn);
            t.meta.insert(QStringLiteral("justify"), QStringLiteral("MC"));
            t.meta.insert(QStringLiteral("mechRole"), QStringLiteral("label"));
            t.meta.insert(QStringLiteral("labelText"), text);
            t.meta.insert(QStringLiteral("labelMidX"), mid0.x);
            t.meta.insert(QStringLiteral("labelMidY"), mid0.y);
            ents.push_back(t);
        }
        return ents;
    };

    // Which run of a special its labels go on: the first, or the cable where it splits.
    QHash<const Brick*, size_t> labelBranch;
    for (size_t i = 0; i < branches.size(); ++i)
        if (branches[i].splitOf < 0 && !labelBranch.contains(branches[i].special))
            labelBranch.insert(branches[i].special, i);

    // THE RUNS.
    for (size_t i = 0; i < branches.size(); ++i) {
        const Branch& br = branches[i];
        const Brick& s = *br.special;
        const BrickRunSpec& r = s.run;
        const bool isTrunk = br.trunk;
        const bool isSub = br.splitOf >= 0;
        const bool loose = br.b.id.isEmpty();
        const bool jA = isJunction(br.a), jB = isJunction(br.b);
        const BrickFace fa = br.a.id.isEmpty() ? BrickFace{} : out.faces.value(faceKey(br.a.id, br.a.port));
        const BrickFace fb = loose ? BrickFace{} : out.faces.value(faceKey(br.b.id, br.b.port));
        const int wires = std::max(1, br.wires);
        const double ww = std::max(0.1, r.wireWidthMm);
        const double packed = ww * 1.5;
        Landing la = atA[i];
        if (isSub || br.freeStart) {
            la.centre = splitAt[i];
            la.lane = packed;
            la.share = packed * wires;
        }
        Landing lb = loose ? la : atB[i];

        // WHICH ENDS SPREAD. A run spreads at every part it lands on; nothing spreads at a junction or
        // where a cable splits. A FANOUT spreads at one end only - the end on a part, or where its
        // fanSide says - and is packed at the other.
        bool spreadA = !jA && !isSub && !br.freeStart;
        bool spreadB = !loose && !jB && !isTrunk;
        if (s.type == BrickType::Fanout && isTrunk) spreadA = false;   // the body: gathered
        if (s.type == BrickType::Fanout && !isTrunk && !isSub) {
            // In the special's own terms, turned round where the branch was built the other way.
            const int side = br.flipped ? 1 - design.fanoutSpreadSide(s) : design.fanoutSpreadSide(s);
            // Turned on purpose it spreads where it is told - at a junction too, as a breakout.
            const bool told = s.run.fanSide == 0 || s.run.fanSide == 1;
            spreadA = side == 0 && !isSub && !br.freeStart && (!jA || told);
            spreadB = side == 1 && !loose && !isTrunk && (!jB || told);
        }
        // NO FAN AT AN END (its fan length nought, the wizard's Fan button off): the wires go into the part
        // as the bundle they are, not spread across its face. In the special's own terms - start and
        // finish turned round where the branch was built the other way.
        const double fanSpecA = br.flipped ? r.fanFinishMm : r.fanStartMm;
        const double fanSpecB = br.flipped ? r.fanStartMm : r.fanFinishMm;
        if (fanSpecA == 0.0) spreadA = false;
        if (fanSpecB == 0.0) spreadB = false;
        // Where it does not spread its wires are packed - except where a fan goes into a cable, which
        // it enters across the drawing's share of the cable's width (the junction's landing).
        const bool intoCableA = jA && s.type != BrickType::Run;
        const bool intoCableB = jB && s.type != BrickType::Run;
        if (!spreadA && !intoCableA) la.lane = packed;
        else if (spreadA && jA) la.lane = std::max(la.lane, packed * 2.0);
        if (!spreadB && !loose && !intoCableB) lb.lane = packed;
        else if (spreadB && jB) lb.lane = std::max(lb.lane, packed * 2.0);
        const double fanA = !spreadA ? 0.0
                            : fanSpecA >= 0.0 ? fanSpecA
                                              : std::max(kWiresRunEnd, la.lane * (wires - 1) / 2.0);
        const double fanB = !spreadB ? 0.0
                            : fanSpecB >= 0.0 ? fanSpecB
                                              : std::max(kWiresRunEnd, lb.lane * (wires - 1) / 2.0);
        const double lead = std::max({fanA, fanB, 10.0}) + std::max(0.0, r.kneeRadiusMm) + 5.0;
        std::vector<Vec2> route;
        if (isTrunk && br.freeStart) route = {splitAt[i], splitAt[i] + splitDir[i] * r.lengthMm};
        else if (isTrunk) route = {la.centre, splitAt[i]};
        else if (isSub) route = routeBetweenFaces(la.centre, splitDir[i], lb.centre, fb.out, fanB + 5.0);
        else if (loose) route = {la.centre, la.centre + fa.out * r.lengthMm};
        else route = routeBetweenFaces(la.centre, fa.out, lb.centre, fb.out, lead);
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
                const double spreadAw = la.lane * (wires - 1) + ww;
                const double spreadBw = lb.lane * (wires - 1) + ww;
                run.meta.insert(QStringLiteral("twistWidth"), std::min(spreadAw, spreadBw));
                run.meta.insert(QStringLiteral("twistPitch"), kTwistedPairPitch);
                run.meta.insert(QStringLiteral("twistEnds"),
                                r.fanStartMm >= 0.0 ? r.fanStartMm : kTwistedPairLead);
            } else {
                run.meta.insert(QStringLiteral("twistSpread"), spreadA && spreadB ? QStringLiteral("both")
                                                               : spreadB           ? QStringLiteral("finish")
                                                                                   : QStringLiteral("start"));
                run.meta.insert(QStringLiteral("twistSidesSame"), false);
                run.meta.insert(QStringLiteral("twistLane"), std::max(1e-3, la.lane));
                run.meta.insert(QStringLiteral("twistLaneFinish"), std::max(1e-3, lb.lane));
                run.meta.insert(QStringLiteral("twistEnds"), fanA);
                run.meta.insert(QStringLiteral("twistEndsFinish"), fanB);
                if (r.fanKneeMm >= 0.0) {
                    run.meta.insert(QStringLiteral("twistFanKnee"), r.fanKneeMm);
                    run.meta.insert(QStringLiteral("twistFanKneeFinish"), r.fanKneeMm);
                }
                // THE BODY IS THE OUTERMOST OF THE STACK: a braid under a heatshrink is not seen.
                if (outer != QLatin1String("wires") && !isSub) {
                    const double share = loose ? la.share : std::min(la.share, lb.share);
                    const int layers = std::max(0, static_cast<int>(r.stack.size()) - 2);
                    bodyWidth = r.bodyWidthMm > 1e-9
                                    ? r.bodyWidthMm
                                    : std::max(r.bodyScalePct / 100.0 * share, packed * (wires - 1) * 0.6 + ww) +
                                          0.6 * layers;
                    run.meta.insert(QStringLiteral("twistBody"),
                                    outer == QLatin1String("heatshrink") ? QStringLiteral("heatshrink")
                                                                         : QStringLiteral("cable"));
                    run.meta.insert(QStringLiteral("twistBodyWidth"), bodyWidth);
                    // AT AN END THAT DOES NOT FAN - joined to a fanout, or where it splits - the
                    // transition is the end boot, so one asked for at both ends is at both.
                    const bool wantA = r.transitions == RunEnds::Start || r.transitions == RunEnds::Both;
                    const bool wantB = r.transitions == RunEnds::Finish || r.transitions == RunEnds::Both;
                    if ((wantA && !spreadA) || (wantB && !spreadB && !loose)) {
                        run.meta.insert(QStringLiteral("twistEndBoot"), true);
                        run.meta.insert(QStringLiteral("twistEndBootRecovered"), true);
                    }
                    // THE TRANSITION'S ENTRY the size of the bundle going into it.
                    if (r.transitions != RunEnds::None) {
                        const double entry = std::min(bodyWidth, packed * (wires - 1) + ww);
                        run.meta.insert(QStringLiteral("twistBodyFace"), entry);
                        // Its width as said - held at the cable's width if it is asked to be narrower.
                        if (r.transitionWidthMm > 1e-9)
                            run.meta.insert(QStringLiteral("twistBootWidth"), std::max(r.transitionWidthMm, bodyWidth));
                    }
                    run.meta.insert(QStringLiteral("twistBodyTransitionSides"),
                                    r.transitions == RunEnds::Start    ? QStringLiteral("start")
                                    : r.transitions == RunEnds::Finish ? QStringLiteral("finish")
                                    : r.transitions == RunEnds::Both   ? QStringLiteral("both")
                                                                       : QStringLiteral("none"));
                }
            }
            if (r.kneeRadiusMm >= 0.0) run.meta.insert(QStringLiteral("twistKnee"), r.kneeRadiusMm);
            // CONVERGED TO so many lines where the wires are gathered, when it is said.
            if (!laidUp && r.trunkWires > 0)
                run.meta.insert(QStringLiteral("twistTrunkWires"), std::clamp(r.trunkWires, 1, wires));
            made.push_back(run);
        }

        // How long it REALLY is: drawn longer only for room to see, the dimensions say the real figure.
        const double total = stretchOf.contains(s.id) ? s.run.lengthMm : routeLength(route);
        // WHAT AUTO CAME TO, on the special's first run, so the window can show it.
        if (!out.actual.contains(s.id) && !isSub) {
            BrickActual act;
            act.fanStart = br.flipped ? fanB : fanA;
            act.fanFinish = br.flipped ? fanA : fanB;
            act.bodyWidth = bodyWidth;
            act.transitionWidth = r.transitionWidthMm > 1e-9 ? std::max(r.transitionWidthMm, bodyWidth)
                                                             : bodyWidth * 1.25;
            for (const Entity& e : made)
                if (e.type == EntityType::TwistedPair) {
                    const TwistFigures fig = twistedPairFigures(e);
                    act.knee = fig.kneeRadius;
                    act.fanKnee = fig.fanKnee;
                    act.trunk = fig.trunk;
                }
            out.actual.insert(s.id, act);
        }
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
        // between the fans. A figure written in place of the measurement is what the dimension SAYS;
        // the measurement is kept on it too.
        const double gapOut = std::max(bodyWidth, la.lane * (wires - 1) + ww) / 2.0 + 10.0;
        auto fmt = [](double v) { return QString::number(v, 'f', 1); };
        const bool straight = route.size() == 2;
        auto dim = [&](const Vec2& p0, const Vec2& p1, double offset, double measured, bool sayMeasured,
                       const QString& written, const char* kind) {
            Entity d = makeDim(p0, p1, offset,
                               !written.isEmpty() ? written : sayMeasured ? fmt(measured) : QString());
            d.meta.insert(QStringLiteral("brickDim"), QLatin1String(kind));
            d.meta.insert(QStringLiteral("brickMeasured"), measured);
            made.push_back(d);
        };
        if (r.dimTotal && !loose && !isSub && !br.a.id.isEmpty()) {
            // A PART OF NOMINAL SIZE IS NEVER MEASURED THROUGH: its body is drawn to proportion, not to size, so
            // a dimension to its far end would give a figure nobody can build to. It is measured from its face.
            auto nominal = [&](const QString& id) {
                const Brick* e = design.brick(id);
                const LibraryPart* p = e && catalog && !e->libPart.isEmpty() ? catalog->byId(e->libPart) : nullptr;
                return p && partIsNominal(*p);
            };
            const Vec2 from = design.dimFromFar && !nominal(br.a.id) ? fa.farAt : fa.nearAt;
            const Vec2 to = design.dimToFar && !nominal(br.b.id) ? fb.farAt : fb.nearAt;
            const double along = total + distance(from, fa.centre) + distance(to, fb.centre);
            const QString said = !r.dimTextTotal.isEmpty() ? r.dimTextTotal : fmt(along);
            if (r.dimSingle) {
                // SINGLE-ENDED: from the start, a short line with one arrow and the figure over it.
                const Vec2 u = fa.out;
                const Vec2 side = u.perp() * -(gapOut + 10.0);
                const Vec2 p0 = from + side;
                const Vec2 p1 = p0 + u * 25.0;
                auto line = [&](const Vec2& a, const Vec2& b) {
                    Entity l;
                    l.type = EntityType::Line;
                    l.a = a;
                    l.b = b;
                    l.meta.insert(QStringLiteral("mechRole"), QStringLiteral("dim"));
                    l.meta.insert(QStringLiteral("brickDim"), QStringLiteral("total"));
                    l.meta.insert(QStringLiteral("brickMeasured"), along);
                    made.push_back(l);
                };
                line(from, p0 + side.normalized() * 1.5);                        // extension
                line(p0, p1);                                                    // the short line
                line(p0, p0 + u * 2.5 + u.perp() * 0.9);                         // the arrow
                line(p0, p0 + u * 2.5 - u.perp() * 0.9);
                Entity t = makeTextAt((p0 + p1) * 0.5 + side.normalized() * 1.2, said, 3.5,
                                      angleOf(u.x < -1e-9 ? u * -1.0 : u));
                t.meta.insert(QStringLiteral("justify"), QStringLiteral("BC"));
                t.meta.insert(QStringLiteral("mechRole"), QStringLiteral("dim"));
                t.meta.insert(QStringLiteral("brickDim"), QStringLiteral("total"));
                t.meta.insert(QStringLiteral("brickMeasured"), along);
                made.push_back(t);
            } else if (!r.dimLinear && r.dimTrace && !straight) {
                // TRACING THE RUN: the dimension line follows the harness round every bend, at
                // its knee radius, off to one side of it.
                double knee = std::max(0.0, r.kneeRadiusMm);
                for (const Entity& e : made)
                    if (e.type == EntityType::TwistedPair) knee = twistedPairFigures(e).kneeRadius;
                std::vector<double> radii(route.size(), knee);
                const std::vector<Vec2> round = roundedPolylinePerCorner(route, radii);
                const std::vector<Vec2> path = offsetPolyline(round, gapOut + 10.0);
                if (path.size() >= 2) {
                    Entity l;
                    l.type = EntityType::Polyline;
                    l.points = path;
                    l.a = path.front();
                    l.b = path.back();
                    l.meta.insert(QStringLiteral("mechRole"), QStringLiteral("dim"));
                    l.meta.insert(QStringLiteral("brickDim"), QStringLiteral("total"));
                    l.meta.insert(QStringLiteral("brickMeasured"), along);
                    l.meta.insert(QStringLiteral("dimTrace"), true);
                    made.push_back(l);
                    // the arrows at the two ends, and the extension lines back to the run
                    for (bool atEnd : {false, true}) {
                        const Vec2 tip = atEnd ? path.back() : path.front();
                        const Vec2 inward = (atEnd ? path[path.size() - 2] - path.back()
                                                   : path[1] - path.front()).normalized();
                        for (double sgn : {1.0, -1.0}) {
                            Entity a;
                            a.type = EntityType::Line;
                            a.a = tip;
                            a.b = tip + inward * 2.5 + inward.perp() * (0.9 * sgn);
                            a.meta.insert(QStringLiteral("mechRole"), QStringLiteral("dim"));
                            made.push_back(a);
                        }
                        Entity x;
                        x.type = EntityType::Line;
                        x.a = atEnd ? route.back() : route.front();
                        x.b = tip;
                        x.meta.insert(QStringLiteral("mechRole"), QStringLiteral("dim"));
                        made.push_back(x);
                    }
                    Vec2 tdir;
                    const Vec2 tAt = pointAlong(path, routeLength(path) / 2.0, &tdir);
                    const Vec2 td = tdir.x < -1e-9 ? tdir * -1.0 : tdir;
                    Entity t = makeTextAt(tAt + td.perp() * 1.2, said, 3.5, angleOf(td));
                    t.meta.insert(QStringLiteral("justify"), QStringLiteral("BC"));
                    t.meta.insert(QStringLiteral("mechRole"), QStringLiteral("dim"));
                    made.push_back(t);
                }
            } else {
                // LINEAR along the way the harness leaves, or at the angle from one point to the other.
                const Vec2 toAlong = straight || !r.dimLinear ? to : from + fa.out * dot(to - from, fa.out);
                dim(from, toAlong, -(gapOut + 10.0), along, !straight, r.dimTextTotal, "total");
            }
        }
        if (r.dimFans) {
            if (fanA > 1e-9)
                dim(route.front(), pointAlong(route, fanA), gapOut, fanA, false, r.dimTextFans, "fanStart");
            if (fanB > 1e-9)
                dim(pointAlong(route, total - fanB), route.back(), gapOut, fanB, false, r.dimTextFans,
                    "fanFinish");
        }
        if (r.dimBetween && !loose && !isSub && total > fanA + fanB) {
            dim(pointAlong(route, fanA), pointAlong(route, total - fanB), gapOut, total - fanA - fanB,
                !straight, r.dimTextBetween, "between");
        }

        // LABELS: on the whole bundle, or on one wire of a fan, so far along from the end chosen -
        // on the special's first run, or on the cable up to where it splits.
        if (labelBranch.value(&s) == i && !r.labels.empty()) {
            std::vector<std::vector<Vec2>> centres;
            for (const Entity& e : made) {
                if (e.type == EntityType::TwistedPair) {
                    const std::vector<std::vector<Vec2>> strands = twistedPairStrands(e);
                    for (size_t j = 0; j + 1 < strands.size(); j += 2) {
                        const std::vector<Vec2>& ea = strands[j];
                        const std::vector<Vec2>& eb = strands[j + 1];
                        if (ea.size() != eb.size() || ea.size() < 2) continue;
                        std::vector<Vec2> c(ea.size());
                        for (size_t q = 0; q < ea.size(); ++q) c[q] = (ea[q] + eb[q]) * 0.5;
                        centres.push_back(c);
                    }
                } else if (e.type == EntityType::Polyline && e.meta.value(QStringLiteral("double")).toBool()) {
                    centres.push_back(e.points);
                }
            }
            for (const BrickLabel& lab : r.labels) {
                const bool fromB = lab.fromFinish && !loose;
                if (lab.wire < 0) {
                    const double s0 = fromB ? total - lab.distanceMm : lab.distanceMm;
                    const double span = bodyWidth > 0.0 ? bodyWidth
                                                        : packed * (std::min(wires, 3) - 1) + ww;
                    for (Entity& e : labelEntities(route, s0, span, lab.text, false, lab)) made.push_back(e);
                    continue;
                }
                // ON ONE WIRE: the wires fanned at that end, numbered from the top down (from the
                // left, where the fan runs up or down the sheet), the label centred on that wire.
                const Vec2 endPt = fromB ? route.back() : route.front();
                const Vec2 endOut = fromB ? (loose ? fa.out : fb.out) : fa.out;
                const double reach = (fromB ? lb.lane : la.lane) * (wires - 1) / 2.0 + 2.0 * ww + 0.5;
                std::vector<std::vector<Vec2>> fan;
                for (const std::vector<Vec2>& c : centres) {
                    if (distance(c.front(), endPt) <= reach) fan.push_back(c);
                    else if (distance(c.back(), endPt) <= reach)
                        fan.push_back(std::vector<Vec2>(c.rbegin(), c.rend()));
                }
                if (fan.empty()) continue;
                const bool upDown = std::abs(endOut.y) > std::abs(endOut.x);
                std::stable_sort(fan.begin(), fan.end(), [upDown](const auto& x, const auto& y) {
                    return upDown ? x.front().x < y.front().x : x.front().y > y.front().y;
                });
                const int k = std::clamp(lab.wire, 0, static_cast<int>(fan.size()) - 1);
                QString text = lab.text;
                if (text.isEmpty() && k < r.nets.size()) text = r.nets[k];
                for (Entity& e : labelEntities(fan[static_cast<size_t>(k)], lab.distanceMm, ww, text, true, lab))
                    made.push_back(e);
            }
        }

        stampBrick(made, s.id);
        for (Entity& e : made) {
            e.meta.insert(QStringLiteral("brickBranch"), static_cast<int>(i));
            if (isTrunk) e.meta.insert(QStringLiteral("brickTrunk"), true);
            out.entities.push_back(std::move(e));
        }
    }
    return out;
}

} // namespace Edraw_Desktop
