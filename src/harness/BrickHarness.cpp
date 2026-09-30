#include "harness/BrickHarness.h"

#include "geometry/Geometry.h"
#include "harness/MechanicalHarness.h"
#include "harness/MechModels.h"
#include "tools/EntityTransforms.h"

#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
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

Vec2 turnOnDrawing(const Brick& b, const Vec2& v, double extraRad) {
    auto rotate = [](const Vec2& p, double a) {
        if (std::abs(a) < 1e-12) return p;
        const double c = std::cos(a), s = std::sin(a);
        return Vec2{p.x * c - p.y * s, p.x * s + p.y * c};
    };
    Vec2 d = rotate(turnQuarters(v, b.rot), -b.drawTurnDeg * M_PI / 180.0);
    // Mirrored LEFT FOR RIGHT on the sheet, as it is turned there.
    if (b.drawMirror) d.x = -d.x;
    return rotate(d, extraRad);
}

Vec2 BrickDesign::portWorldDir(const Brick& b, int port) const {
    const BrickShape s = shapeOf(b);
    if (port < 0 || port >= s.ports()) return Vec2{1.0, 0.0};
    // Turned and mirrored on the drawing (R, Space, M), its connections are turned and mirrored on the brick too.
    return turnOnDrawing(asOnPane(b), s.faceOut[static_cast<size_t>(port)]).normalized();
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
        const Vec2 w = portWorldDir(self, p);
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
    // A JOINT AT ONE END (a Y-split, a splice) takes the wires as a bundle: it spreads at the other end, the part
    // the wires go into one by one - not across the joint's leg.
    auto joints = [&](int port) {
        int n = 0;
        for (const QString& o : sideOf(s.id, port))
            if (const Brick* b = brick(o); b && b->type == BrickType::End && brickKindIsJoint(b->kind)) ++n;
        return n;
    };
    const int jl = joints(kPortLeft), jr = joints(kPortRight);
    if (l > 0 && r > 0 && jl == l && jr < r) return 1;
    if (l > 0 && r > 0 && jr == r && jl < l) return 0;
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
        (x->type == BrickType::Run || y->type == BrickType::Run || brickIsBridge(*x) || brickIsBridge(*y))) {
        if (linked(a, b)) return refuse(QStringLiteral("Those two are already linked"));
        // A side is free for the join when nothing is on it - or, for a BRIDGE WIRE joining a cable, when only
        // parts (and other bridges) are there: it takes some of the wires, the rest go on to the parts.
        auto freeFor = [&](const Brick& self, int p, const Brick& other) {
            const QStringList on = sideOf(self.id, p);
            if (on.isEmpty()) return true;
            if (!brickIsBridge(other) || brickIsBridge(self)) return false;
            for (const QString& o : on)
                if (const Brick* ob = brick(o); ob && brickIsSpecial(ob->type) && !brickIsBridge(*ob)) return false;
            return true;
        };
        auto freePort = [&](const Brick& self, const Brick& other, int asked) {
            if (asked >= 0) return freeFor(self, asked, other) ? asked : -1;
            int p = std::max(0, facingPort(self, Vec2{other.col - self.col, other.row - self.row}));
            if (!freeFor(self, p, other)) p = 1 - p;
            return freeFor(self, p, other) ? p : -1;
        };
        const int pa = freePort(*x, *y, portA), pb = freePort(*y, *x, portB);
        if (pa < 0 || pb < 0)
            return refuse(QStringLiteral("A run joined to another has that end to itself - free an end first"));
        links.push_back({a, b, pa, pb, 0});
        if (why) why->clear();
        return true;
    }
    // A HEATSHRINK BOOT GOES ON A PART: linked straight to it - a connector, a backshell, any part - its face on the
    // part's face, and the cable out of its other side.
    if (x->type == BrickType::End && y->type == BrickType::End &&
        (x->kind == PartKind::HeatshrinkBoot || y->kind == PartKind::HeatshrinkBoot)) {
        if (linked(a, b)) return refuse(QStringLiteral("Those two are already linked"));
        auto usedPorts = [&](const QString& id) {
            QSet<int> used;
            for (const BrickLink& l : links) {
                if (l.a == id) used.insert(portOn(id, l.b));
                if (l.b == id) used.insert(portOn(id, l.a));
            }
            return used;
        };
        auto pick = [&](const Brick& self, const Brick& other, int asked) {
            if (asked >= 0) return asked;
            const QSet<int> used = usedPorts(self.id);
            const int facing = std::max(0, facingPort(self, Vec2{other.col - self.col, other.row - self.row}));
            if (!used.contains(facing)) return facing;
            for (int p = 0; p < portCount(self); ++p)
                if (!used.contains(p)) return p;
            return facing;
        };
        links.push_back({a, b, pick(*x, *y, portA), pick(*y, *x, portB), 0});
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
            if (const Brick* ob = brick(o); ob && brickIsSpecial(ob->type)) n += brickIsBridge(*ob) ? 1 : 100;   // joined: full
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
                      {"showDesignator", b.showDesignator}, {"designatorHeightMm", b.designatorHeightMm},
                      {"drawDy", b.drawDyMm}, {"z", b.z}, {"dimBelow", b.dimBelow}};
        if (std::abs(b.scale - 1.0) > 1e-9) o.insert(QStringLiteral("scale"), b.scale);
        if (b.autoScale) o.insert(QStringLiteral("autoScale"), true);
        if (std::abs(b.drawTurnDeg) > 1e-9) o.insert(QStringLiteral("drawTurn"), b.drawTurnDeg);
        if (b.drawMirror) o.insert(QStringLiteral("drawMirror"), true);
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
                                                   {"trunkWires", r.trunkWires},
                                                   {"pairs", r.pairs},
                                                   {"dimTotalBelow", r.dimTotalBelow},
                                                   {"dimFansBelow", r.dimFansBelow},
                                                   {"dimBetweenBelow", r.dimBetweenBelow},
                                                   {"bridgeWires", r.bridgeWires},
                                                   {"stackParts", [&r] {
                                                        QJsonObject sp;
                                                        for (auto it = r.stackParts.cbegin(); it != r.stackParts.cend(); ++it)
                                                            if (!it.value().isEmpty()) sp.insert(it.key(), it.value());
                                                        return sp;
                                                    }()}};
        }
        bs.append(o);
    }
    QJsonArray ls;
    for (const BrickLink& l : links)
        ls.append(QJsonObject{{"a", l.a}, {"b", l.b}, {"portA", l.portA}, {"portB", l.portB},
                              {"wires", l.wires}});
    QJsonArray ds;
    for (const BrickDim& dm : dims) ds.append(QJsonObject{{"a", dm.a}, {"b", dm.b}, {"below", dm.below}});
    return QJsonObject{{"format", "edraw-brick-harness"}, {"bricks", bs}, {"links", ls},
                       {"dimFromFar", dimFromFar},        {"dimToFar", dimToFar}, {"dims", ds},
                       {"dimFromMid", dimFromMid},        {"dimToMid", dimToMid},
                       {"tieBrickTurn", tieBrickTurn}};
}

BrickDesign BrickDesign::fromJson(const QJsonObject& o) {
    BrickDesign d;
    d.dimFromFar = o.value(QStringLiteral("dimFromFar")).toBool();
    d.dimToFar = o.value(QStringLiteral("dimToFar")).toBool();
    d.dimFromMid = o.value(QStringLiteral("dimFromMid")).toBool();
    d.dimToMid = o.value(QStringLiteral("dimToMid")).toBool();
    d.tieBrickTurn = o.value(QStringLiteral("tieBrickTurn")).toBool(true);
    for (const QJsonValue v : o.value(QStringLiteral("dims")).toArray()) {
        const QJsonObject dm = v.toObject();
        d.dims.push_back({dm.value(QStringLiteral("a")).toString(), dm.value(QStringLiteral("b")).toString(),
                          dm.value(QStringLiteral("below")).toBool()});
    }
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
        b.drawDyMm = bo.value(QStringLiteral("drawDy")).toDouble(0.0);
        b.drawTurnDeg = bo.value(QStringLiteral("drawTurn")).toDouble(0.0);
        b.drawMirror = bo.value(QStringLiteral("drawMirror")).toBool(false);
        b.z = bo.value(QStringLiteral("z")).toInt(0);
        b.dimBelow = bo.value(QStringLiteral("dimBelow")).toBool(false);
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
            s.bridgeWires = r.value(QStringLiteral("bridgeWires")).toString();
            {
                const QJsonObject sp = r.value(QStringLiteral("stackParts")).toObject();
                for (auto it = sp.begin(); it != sp.end(); ++it) s.stackParts.insert(it.key(), it.value().toString());
            }
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
            s.pairs = std::max(1, r.value(QStringLiteral("pairs")).toInt(1));
            s.dimTotalBelow = r.value(QStringLiteral("dimTotalBelow")).toBool(true);
            s.dimFansBelow = r.value(QStringLiteral("dimFansBelow")).toBool(false);
            s.dimBetweenBelow = r.value(QStringLiteral("dimBetweenBelow")).toBool(false);
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

bool specialOneToOne(const BrickDesign& d, const QString& special, QString* start, QString* finish) {
    const Brick* s = d.brick(special);
    if (!s || !brickIsSpecial(s->type)) return false;
    const QStringList a = d.startsOf(special), b = d.finishesOf(special);
    if (a.size() != 1 || b.size() != 1) return false;
    // Parts on both ends - a special joined to another special at one end is measured along the harness, not
    // from a part.
    for (const QString& o : d.sideOf(special, 0) + d.sideOf(special, 1))
        if (const Brick* ob = d.brick(o); ob && brickIsSpecial(ob->type)) return false;
    // Tied parts at both ends: a joint stands alone, and a run to it is not measured off the pane.
    for (const QString& e : {a.front(), b.front()})
        if (const Brick* eb = d.brick(e); !eb || !brickIsTied(*eb)) return false;
    if (start) *start = a.front();
    if (finish) *finish = b.front();
    return true;
}

namespace {
double signOr(double v, double dflt) { return v > 1e-9 ? 1.0 : v < -1e-9 ? -1.0 : dflt; }
} // namespace

bool fitEndToLength(BrickDesign& d, const QString& special, const QString& end) {
    Brick* s = d.brick(special);
    if (!s || !brickIsSpecial(s->type)) return false;
    const QStringList a = d.startsOf(special), b = d.finishesOf(special);
    const bool atStart = a.contains(end);
    const QStringList others = atStart ? b : a;
    if (others.size() != 1 || others.front() == end) return false;
    Brick* e = d.brick(end);
    const Brick* o = d.brick(others.front());
    if (!e || !o || !brickIsTied(*e) || !brickIsTied(*o)) return false;
    const double along = s->run.lengthMm / kBrickMmPerCol;
    // On the side it already is of the other part - or the side the special is on, or ahead.
    const double side = signOr(e->col - o->col, signOr(s->col - o->col, atStart ? -1.0 : 1.0));
    e->col = o->col + side * along;
    s->col = (e->col + o->col) / 2.0;
    return true;
}

bool lengthFromPositions(BrickDesign& d, const QString& special, double fraction) {
    QString a, b;
    if (!specialOneToOne(d, special, &a, &b)) return false;
    Brick* s = d.brick(special);
    const Brick* ea = d.brick(a);
    const Brick* eb = d.brick(b);
    const double apart = std::abs(eb->col - ea->col);
    if (apart < 1e-9) return false;
    s->run.lengthMm = std::max(1.0, std::round(apart * kBrickMmPerCol));
    const double f = fraction >= 0.0 ? std::clamp(fraction, 0.0, 1.0) : 0.5;
    s->col = ea->col + (eb->col - ea->col) * f;
    return true;
}

bool pushForLength(BrickDesign& d, const QString& special, double oldLen) {
    QString a, b;
    if (!specialOneToOne(d, special, &a, &b)) return false;
    Brick* s = d.brick(special);
    const Brick* ea = d.brick(a);
    Brick* eb = d.brick(b);
    const double span = eb->col - ea->col;
    const double dir = signOr(span, 1.0);
    const double fraction = std::abs(span) > 1e-9 ? (s->col - ea->col) / span : 0.5;
    const double delta = dir * (s->run.lengthMm - oldLen) / kBrickMmPerCol;
    if (std::abs(delta) < 1e-12) return true;
    // THE FINISH PART AND EVERYTHING BEYOND IT: out from it along every link but the one back through this
    // special, and never back to the part it starts on.
    QSet<QString> moving{b};
    std::deque<QString> queue{b};
    while (!queue.empty()) {
        const QString n = queue.front();
        queue.pop_front();
        for (const QString& o : d.linksOf(n)) {
            if (o == special || o == a || moving.contains(o)) continue;
            moving.insert(o);
            queue.push_back(o);
        }
    }
    for (const QString& id : moving)
        if (Brick* m = d.brick(id)) m->col += delta;
    s->col = ea->col + (eb->col - ea->col) * fraction;
    return true;
}

void fitPositionsToLengths(BrickDesign& d) {
    // Out from the most linked part, each special's far parts put at its length from the near one.
    QString root;
    for (const Brick& b : d.bricks)
        if (b.type == BrickType::End &&
            (root.isEmpty() || d.linksOf(b.id).size() > d.linksOf(root).size()))
            root = b.id;
    if (root.isEmpty()) return;
    QSet<QString> seen{root};
    std::deque<QString> queue{root};
    while (!queue.empty()) {
        const QString e = queue.front();
        queue.pop_front();
        for (const QString& sid : d.linksOf(e)) {
            Brick* s = d.brick(sid);
            if (!s || !brickIsSpecial(s->type) || seen.contains(sid)) continue;
            seen.insert(sid);
            const QStringList a = d.startsOf(sid), b = d.finishesOf(sid);
            const QStringList far = a.contains(e) ? b : a;
            const Brick* near = d.brick(e);
            double mid = 0.0;
            int n = 0;
            for (const QString& f : far) {
                Brick* fb = d.brick(f);
                if (!fb || seen.contains(f)) continue;
                const double side = signOr(fb->col - near->col, a.contains(e) ? 1.0 : -1.0);
                fb->col = near->col + side * s->run.lengthMm / kBrickMmPerCol;
                mid += fb->col;
                ++n;
                seen.insert(f);
                queue.push_back(f);
            }
            if (n > 0) s->col = (near->col + mid / n) / 2.0;
        }
    }
}

QList<int> parseWireList(const QString& text, int most) {
    QList<int> out;
    for (const QString& partRaw : text.split(QRegularExpression(QStringLiteral("[,; ]+")), Qt::SkipEmptyParts)) {
        const QString part = partRaw.trimmed();
        const QStringList ends = part.split(QLatin1Char('-'));
        bool ok1 = false, ok2 = true;
        const int from = ends.value(0).toInt(&ok1);
        const int to = ends.size() > 1 ? ends.value(1).toInt(&ok2) : from;
        if (!ok1 || !ok2) continue;
        const int step = to >= from ? 1 : -1;
        for (int w = from;; w += step) {
            if (w >= 1 && (most <= 0 || w <= most) && !out.contains(w)) out << w;
            if (w == to || out.size() > 4096) break;
        }
    }
    return out;
}

QString joinedSpecial(const BrickDesign& d, const QString& special) {
    for (const QString& o : d.linksOf(special))
        if (const Brick* b = d.brick(o); b && brickIsSpecial(b->type)) return o;
    return QString();
}

int syncBridgeWires(BrickDesign& d) {
    int changed = 0;
    for (Brick& s : d.bricks) {
        if (!brickIsSpecial(s.type) || s.run.bridgeWires.trimmed().isEmpty()) continue;
        const Brick* from = d.brick(joinedSpecial(d, s.id));
        if (!from) continue;
        // The cable's shield, taken out as a drain: one wire, no wire of the cable's own.
        const bool shield = s.run.bridgeWires.trimmed() == QLatin1String(kBridgeShield);
        const QList<int> take = shield ? QList<int>() : parseWireList(s.run.bridgeWires, from->run.wires);
        if (take.isEmpty() && !shield) continue;
        QStringList nets;
        for (int w : take) nets << from->run.nets.value(w - 1);
        if (shield) nets = QStringList{QStringLiteral("SHIELD")};
        const int wires = s.type == BrickType::TwistedPair ? 2 * std::max(1, s.run.pairs) : shield ? 1 : static_cast<int>(take.size());
        if (s.run.wires != wires || s.run.nets != nets) ++changed;
        s.run.wires = wires;
        s.run.nets = nets;
        // And the cable gives that many of its wires on to it (none, for its shield).
        if (BrickLink* l = d.link(from->id, s.id)) l->wires = shield ? 0 : wires;
    }
    return changed;
}

bool brickIsTied(const Brick& b) { return b.type == BrickType::End && !brickKindIsJoint(b.kind); }

void symmetricOnDrawing(BrickDesign& d, double rowPitch) {
    if (rowPitch <= 0.0) rowPitch = brickRowPitchMm(d);
    BrickDesign laid = d;
    arrangeBricks(laid);
    // Drawn at -row x pitch + dy: the dy that puts each part where the symmetric layout's row would.
    for (Brick& b : d.bricks) {
        if (b.type != BrickType::End) continue;
        const Brick* s = laid.brick(b.id);
        if (s) b.drawDyMm = (b.row - s->row) * rowPitch;
    }
}

double brickRowPitchMm(const BrickDesign& d) {
    // The same figure the drawing lays its rows out at (see buildBrickHarness).
    double widest = 0.0;
    for (const Brick& b : d.bricks) {
        if (b.type != BrickType::End) continue;
        const BrickShape s = d.shapeOf(b);
        widest = std::max({widest, s.half.x * 2.0, s.half.y * 2.0});
    }
    return std::max(40.0, widest + 25.0);
}

QString dragPartOnDrawing(BrickDesign& d, const BrickBuild& built, const QString& part, double dx, double dy) {
    auto faceKey = [](const QString& end, int port) { return end + QLatin1Char(':') + QString::number(port); };
    Brick* p = d.brick(part);
    if (!p || p->type != BrickType::End) return QString();
    const auto pf = built.faces.constFind(faceKey(part, 0));
    // The run it hangs on from another part: the first special on it whose other end is on a part.
    for (const QString& sid : d.linksOf(part)) {
        Brick* s = d.brick(sid);
        if (!s || !brickIsSpecial(s->type)) continue;
        const QStringList a = d.startsOf(sid), b = d.finishesOf(sid);
        QStringList other = a.contains(part) ? b : a;
        // A BRIDGE WIRE's other end is on its cable, not a part: measured from the part that cable goes to there,
        // where the bridge comes out of its fan.
        if (other.isEmpty() && brickIsBridge(*s)) {
            const QString cable = joinedSpecial(d, sid);
            if (!cable.isEmpty())
                for (const QString& e : d.sideOf(cable, d.portOn(cable, sid)))
                    if (const Brick* eb = d.brick(e); eb && eb->type == BrickType::End) {
                        other = QStringList{e};
                        break;
                    }
            if (other.size() == 1) {
                const auto of = built.faces.constFind(faceKey(other.front(), d.portOn(other.front(), cable)));
                const auto mine = built.faces.constFind(faceKey(part, d.portOn(part, sid)));
                if (of == built.faces.cend() || mine == built.faces.cend()) continue;
                const Vec2 from = of->centre, was = mine->centre, now = was + Vec2{dx, dy};
                const double k = std::sqrt(2.0) - 1.0;
                const double before = std::abs(was.x - from.x) + std::abs(was.y - from.y) * k;
                const double after = std::abs(now.x - from.x) + std::abs(now.y - from.y) * k;
                s->run.lengthMm = std::max(0.0, std::round(s->run.lengthMm + after - before));
                p->drawDyMm += dy;
                return sid;
            }
        }
        // JOINED TO ANOTHER RUN at that end (a fanout carried on into a cable): measured from where the two join.
        if (other.isEmpty()) {
            const QString j = joinedSpecial(d, sid);
            if (!j.isEmpty()) {
                const QString jk = QStringLiteral("~J") + (sid < j ? sid + QLatin1Char('|') + j : j + QLatin1Char('|') + sid);
                const auto of = built.faces.constFind(jk + QLatin1Char('@') + sid + QStringLiteral(":0"));
                const auto mine = built.faces.constFind(faceKey(part, d.portOn(part, sid)));
                if (of != built.faces.cend() && mine != built.faces.cend()) {
                    const Vec2 from = of->centre, was = mine->centre, now = was + Vec2{dx, dy};
                    const double k = std::sqrt(2.0) - 1.0;
                    const double before = std::abs(was.x - from.x) + std::abs(was.y - from.y) * k;
                    const double after = std::abs(now.x - from.x) + std::abs(now.y - from.y) * k;
                    s->run.lengthMm = std::max(0.0, std::round(s->run.lengthMm + after - before));
                    p->drawDyMm += dy;
                    return sid;
                }
            }
        }
        if (other.size() != 1) continue;
        const auto of = built.faces.constFind(faceKey(other.front(), d.portOn(other.front(), sid)));
        const auto mine = built.faces.constFind(faceKey(part, d.portOn(part, sid)));
        if (of == built.faces.cend() || mine == built.faces.cend()) continue;
        const Vec2 from = of->centre, was = mine->centre, now = was + Vec2{dx, dy};
        // As the drawing lays a run out: along, plus the slant's extra across.
        const double k = std::sqrt(2.0) - 1.0;
        const double before = std::abs(was.x - from.x) + std::abs(was.y - from.y) * k;
        const double after = std::abs(now.x - from.x) + std::abs(now.y - from.y) * k;
        // NEVER SHORTER THAN THE DRAWING CAN SHOW: two parts are kept far enough apart for the fans between them
        // (kBrickRoomBetween), so a part dragged closer than that stops there - and so does the length. Below it the
        // run was still drawn at that room while its brick said one millimetre.
        const double least = (brickIsBridge(*s) ? 0.0 : kBrickRoomBetween) + std::abs(now.y - from.y) * k;
        s->run.lengthMm = std::max({0.0, std::round(least), std::round(s->run.lengthMm + after - before)});
        p->drawDyMm += dy;
        (void)pf;
        return sid;
    }
    return QString();
}

bool brickIsBridge(const Brick& b) {
    return brickIsSpecial(b.type) && !b.run.bridgeWires.trimmed().isEmpty();
}

void makeBridgeWire(Brick& b) {
    b.run.wires = 1;
    b.run.stack = {QStringLiteral("wires")};
    if (b.run.bridgeWires.trimmed().isEmpty()) b.run.bridgeWires = QStringLiteral("1");
    if (b.name.isEmpty()) b.name = QStringLiteral("Bridge wire");
}

QHash<QString, BrickFollow> bricksFollowing(const BrickDesign& d, const QStringList& moving) {
    QHash<QString, BrickFollow> out;
    const QSet<QString> set(moving.begin(), moving.end());
    for (const QString& id : moving) {
        const Brick* m = d.brick(id);
        if (!m) continue;
        for (const QString& o : d.linksOf(id)) {
            if (set.contains(o)) continue;
            const Brick* ob = d.brick(o);
            if (!ob) continue;
            const bool inLine = std::abs(ob->row - m->row) < 1e-6;
            // A joint stands alone: it neither takes its specials with it nor is taken by them.
            if ((m->type == BrickType::End && !brickIsTied(*m)) || (ob->type == BrickType::End && !brickIsTied(*ob)))
                continue;
            if (m->type == BrickType::End && brickIsSpecial(ob->type)) {
                // Its special: up and down with it when in its line; along with it too when the special is not
                // strung between two parts (a part at each end sets where it sits along).
                BrickFollow& f = out[o];
                f.y = f.y || inLine;
                f.x = f.x || !specialOneToOne(d, o, nullptr, nullptr);
                if (!f.x && !f.y) out.remove(o);
            } else if (brickIsSpecial(m->type) && ob->type == BrickType::End && inLine) {
                out[o].y = true;   // the part in the special's line goes up and down with it
            }
        }
    }
    return out;
}

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
        // A BRIDGE WIRE's part is laid out with the part its cable goes to on that end - it is on that harness.
        if (brickIsBridge(s)) {
            const QString cable = joinedSpecial(design, s.id);
            if (!cable.isEmpty()) {
                const int port = design.portOn(cable, s.id);
                for (const QString& e : design.sideOf(cable, port)) {
                    const Brick* eb = design.brick(e);
                    if (!eb || eb->type != BrickType::End) continue;
                    for (const QString& x : a + b) {
                        joined[x] << e;
                        joined[e] << x;
                    }
                }
            }
        }
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
                                    double leadMm, double shiftDeg, double bendAt, int kneeAt) {
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
            // WHERE THE SLANT IS: as far along as the special's brick sits between its parts, never so near
            // either face that the run does not leave it square.
            const double straight = fwd - slant;
            const double keep = std::min(lead, straight / 2.0);
            // The knee at the special's brick: the first bend there, or the second, or the slant centred there.
            const double t = std::clamp(bendAt, 0.0, 1.0);
            const double want = kneeAt == 0 ? fwd * t : kneeAt == 1 ? fwd * t - slant : straight * t;
            const double leg = std::clamp(want, keep, straight - keep);
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

bool laidUpType(BrickType t) { return t == BrickType::TwistedPair || t == BrickType::TwistedBundle; }

// A route with every corner rounded to `radius` (kept to half of the shorter side either way), as points along it.
std::vector<Vec2> filletedRoute(const std::vector<Vec2>& r, double radius) {
    if (r.size() < 3 || radius <= 1e-9) return r;
    std::vector<Vec2> out{r.front()};
    for (size_t i = 1; i + 1 < r.size(); ++i) {
        const Vec2 a = r[i - 1], c = r[i], b = r[i + 1];
        const Vec2 u = (a - c).normalized(), v = (b - c).normalized();
        const double cosT = std::clamp(dot(u, v), -1.0, 1.0);
        const double theta = std::acos(cosT);   // the angle between the two legs at the corner
        if (theta < 1e-3 || std::abs(M_PI - theta) < 1e-3) {
            out.push_back(c);
            continue;
        }
        const double back = std::min({radius / std::tan(theta / 2.0), distance(a, c) / 2.0, distance(b, c) / 2.0});
        const double rr = back * std::tan(theta / 2.0);
        const Vec2 p1 = c + u * back, p2 = c + v * back;
        const Vec2 centre = c + (u + v).normalized() * (rr / std::sin(theta / 2.0));
        const double a1 = std::atan2(p1.y - centre.y, p1.x - centre.x);
        double a2 = std::atan2(p2.y - centre.y, p2.x - centre.x);
        double sweep = a2 - a1;
        while (sweep > M_PI) sweep -= 2.0 * M_PI;
        while (sweep < -M_PI) sweep += 2.0 * M_PI;
        const int steps = 8;
        for (int k = 0; k <= steps; ++k) {
            const double t = a1 + sweep * k / steps;
            out.push_back(centre + Vec2{std::cos(t), std::sin(t)} * rr);
        }
    }
    out.push_back(r.back());
    return out;
}

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
    // A HEATSHRINK BOOT HAS TWO SIDES: the part it goes over, and the cable out of it - even where its drawing
    // marks only the one face.
    if (b.kind == PartKind::HeatshrinkBoot && s.ports() == 1) {
        s.faceMid.push_back(Vec2{-s.faceMid[0].x, s.faceMid[0].y});
        s.faceOut.push_back(Vec2{-s.faceOut[0].x, -s.faceOut[0].y});
        s.faceLen.push_back(s.faceLen[0] * 1.3);
    }
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
    // A BRICK SETS ITS PART DOWN BY ITS MARKS, each meaning one thing:
    //   the CONNECTION POINT is where the part is - the point on the joint;
    //   NEAR and FAR say which way it faces - from the far end (the back of it) to the near end (the side the
    //     harness is on), which is turned to look out of the brick's port;
    //   the ENTRY FACE is where the cable or the fanout lands on it, and nothing else.
    // Set down by its entry face, a part whose face was not where it connects - or whose body lay the other side
    // of the face from how the brick was joined - came down facing the wrong way. Where a part has no near and
    // far, the arrow of its connection point says the way, read as leaving the body.
    if (part && part->hasMechanical() && (part->hasConnect || (part->hasNearPoint && part->hasFarPoint))) {
        const LibraryPart sized = partSized(*part);
        Bounds raw;
        for (const Entity& e : partDrawingEntities(sized.mechanical)) raw.expand(e.bounds());
        Vec2 toHarness{0.0, 0.0};
        if (sized.hasNearPoint && sized.hasFarPoint && distance(sized.nearPoint, sized.farPoint) > 1e-9) {
            toHarness = (sized.nearPoint - sized.farPoint).normalized();
        } else if (sized.hasConnect) {
            toHarness = Vec2{std::cos(sized.connectAngleRad), std::sin(sized.connectAngleRad)};
            if (raw.valid && dot(sized.connectPoint - raw.center(), toHarness) < 0.0) toHarness = toHarness * -1.0;
        }
        if (toHarness.lengthSq() > 1e-12) {
            const bool many = sized.outputs.size() >= 2;
            const Vec2 look = many ? Vec2{-1.0, 0.0} : Vec2{1.0, 0.0};   // the first port: in on a joint, out on an end
            const Vec2 anchor = sized.hasConnect ? sized.connectPoint : sized.nearPoint;
            // Set down by the connection point, turned so near-far looks out of the port: the marks rewritten to
            // exactly that, the faces kept aside (they only say where the wires land), no legacy line to follow.
            LibraryPart q = sized;
            q.outputs.clear();
            q.hasConnect = true;
            q.connectPoint = anchor;
            q.connectAngleRad = std::atan2(toHarness.y, toHarness.x);
            {
                std::vector<Entity> ents = partDrawingEntities(q.mechanical);
                for (Entity& e : ents) e.meta.remove(QLatin1String(kConnectionLineKey));
                q.mechanical = partDrawingFromEntities(ents);
            }
            MechDrawOptions opt;
            const double dirRad = std::atan2(look.y, look.x);
            const PlacedLibraryPart placed = placeLibraryPartAtFace(q, Vec2{0.0, 0.0}, dirRad, Vec2{0.0, 0.0}, opt);
            const double turn = dirRad - q.connectAngleRad;
            const double tc = std::cos(turn), ts = std::sin(turn);
            auto put = [&](const Vec2& p) {
                const Vec2 d = p - anchor;
                return Vec2{d.x * tc - d.y * ts, d.x * ts + d.y * tc};
            };
            Bounds bb;
            for (const Entity& e : placed.entities) bb.expand(e.bounds());
            const Vec2 c = bb.valid ? (bb.min + bb.max) * 0.5 : Vec2{0.0, 0.0};
            s.drawing = placed.entities;
            for (Entity& e : s.drawing) translateEntity(e, c * -1.0);
            s.half = bb.valid ? (bb.max - bb.min) * 0.5 : Vec2{12.0, 10.0};
            if (!sized.outputs.empty()) {
                for (size_t i = 0; i < sized.outputs.size(); ++i) {
                    const Vec2 a = put(sized.outputs[i].a) - c, e = put(sized.outputs[i].b) - c;
                    const Vec2 mid = (a + e) * 0.5;
                    Vec2 n = (e - a).perp().normalized();
                    if (i == 0) n = look;
                    else if (dot(n, mid) < 0.0) n = n * -1.0;   // away from the middle of the part
                    s.faceMid.push_back(mid);
                    s.faceOut.push_back(n);
                    s.faceLen.push_back(std::max(0.5, distance(a, e)));
                }
            } else {
                // No entry face marked: the wires land at the connection point, across the part's end.
                s.faceMid.push_back(c * -1.0);
                s.faceOut.push_back(look);
                s.faceLen.push_back(std::max(0.5, std::abs(look.x) > 0.5 ? s.half.y * 2.0 * 0.8 : s.half.x * 2.0 * 0.8));
            }
            if (sized.hasNearPoint && sized.hasFarPoint) {
                s.haveEnds = true;
                s.nearAt = put(sized.nearPoint) - c;
                s.farAt = put(sized.farPoint) - c;
            }
            return s;
        }
    }
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
    out.rowPitch = rowPitch;

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
    // Up or down on the drawing, as a part was dragged there.
    auto drawDyOf = [&](const QString& node) {
        const Brick* b = design.brick(node);
        return b && b->type == BrickType::End ? b->drawDyMm : 0.0;
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
    // Bridge wires by their junction: the special whose fan they leave, and the part that fan goes into.
    struct BridgeOff {
        QString source;
        QString partThere;
    };
    QHash<QString, BridgeOff> bridgeOff;
    for (const Brick& s : design.bricks) {
        if (!brickIsSpecial(s.type)) continue;
        QList<FaceRef> a, b;
        for (const QString& o : design.sideOf(s.id, kPortLeft)) a << faceOf(s.id, o);
        for (const QString& o : design.sideOf(s.id, kPortRight)) b << faceOf(s.id, o);
        // A BRIDGE WIRE ON THIS END takes its wires out of this special's fan, where the fan leaves the body: it
        // is not a place the special runs to, so it is taken off the list (and its wires off the count), and the
        // bridge starts where this special's fan does (bridgeStart, below).
        int bridged = 0;
        if (!brickIsBridge(s)) {
            for (QList<FaceRef>* side : {&a, &b}) {
                QString partThere;
                for (const FaceRef& f : *side)
                    if (f.node == f.id) partThere = f.node;
                for (int k = side->size() - 1; k >= 0; --k) {
                    const FaceRef& f = side->at(k);
                    if (f.node == f.id || !junctions.contains(f.node)) continue;
                    const Junction& jn = junctions.value(f.node);
                    const QString other = jn.a == s.id ? jn.b : jn.a;
                    const Brick* ob = design.brick(other);
                    if (!ob || !brickIsBridge(*ob)) continue;
                    const BrickLink* l = design.link(s.id, other);
                    // Its shield taken out is no wire of the cable's: nothing comes off the count.
                    if (ob->run.bridgeWires.trimmed() != QLatin1String(kBridgeShield))
                        bridged += std::max(1, l && l->wires > 0 ? l->wires : ob->run.wires);
                    bridgeOff.insert(f.node, BridgeOff{s.id, partThere});
                    side->removeAt(k);
                }
            }
        }
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
                // A part's link - or, at a junction, the link to the special there (a bridge wire's wires).
                QString other = f.node == f.id ? f.id : QString();
                if (other.isEmpty() && junctions.contains(f.node)) {
                    const Junction& jn = junctions.value(f.node);
                    other = jn.a == s.id ? jn.b : jn.a;
                }
                const BrickLink* l = design.link(s.id, other);
                const int w = l && l->wires > 0 ? l->wires : 0;
                said << w;
                if (w > 0) given += w;
                else ++open;
            }
            const int rest = std::max(0, s.run.wires - bridged - given);
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
        br.wires = s.type == BrickType::TwistedPair ? 2 * std::max(1, s.run.pairs) : std::max(1, s.run.wires - bridged);
        branches.push_back(br);
    }

    // WHERE EVERYTHING IS: laid out by the LENGTHS of the runs from the part they share, not by the
    // columns of the pane - two runs of different lengths to one connector put their far ends at
    // different distances from it. Across the harness, the rows of the pane stand.
    QHash<QString, double> xOf;
    QHash<QString, QPair<QString, double>> yLock;   // a boot drawn level with the part it is on: that part, and how far off
    QHash<QString, double> stretchOf;   // how much longer a run is drawn than it is, for room to see it
    {
        struct Edge {
            QString to;
            double gap;       // along the harness, from the one face to the other
            Vec2 fromFace;    // where the face is on this node, from its centre (turned)
            Vec2 toFace;      // and on the other
            bool pinned = false;   // face on face, whatever the columns say (a bridge wire on its cable's part)
            Vec2 fromOut{0.0, 0.0};   // which way the face on this node looks - the way the run goes from it
        };
        QHash<QString, QList<Edge>> edges;
        // Where a face is on its part, from the part's centre, turned as the brick is; a junction is a point.
        auto faceAt = [&](const FaceRef& f) {
            const Brick* b = design.brick(f.node);
            const auto it = shapes.constFind(f.node);
            if (!b || b->type != BrickType::End || it == shapes.constEnd() || f.port < 0 ||
                f.port >= it->ports())
                return Vec2{0.0, 0.0};
            return turnOnDrawing(*b, it->faceMid[static_cast<size_t>(f.port)]);
        };
        // A RUN'S LENGTH IS FROM FACE TO FACE, and it goes across the harness as well as along it: a run
        // to a part a row or two away climbs that far, so only what is left of its length is laid along
        // the harness. Measured from the centres, the parts on the far rows came in level with where they
        // started from and the wires had to turn back to reach them.
        //
        // ROOM TO SEE: two parts facing each other are never drawn closer than the fans between them need
        // - a short run would put one connector on top of the other. The run is drawn that much longer, and
        // its dimensions still say how long it really is (see stretchOf).
        constexpr double kRoomBetween = kBrickRoomBetween;
        constexpr double kStraightIn = 40.0;   // straight into a part after the last bend
        // Which way a face looks, turned as the brick is; a junction's is left to the run.
        auto faceOutOf = [&](const FaceRef& f) {
            const Brick* b = design.brick(f.node);
            const auto it = shapes.constFind(f.node);
            if (!b || b->type != BrickType::End || it == shapes.constEnd() || f.port < 0 ||
                f.port >= it->ports())
                return Vec2{0.0, 0.0};
            return turnOnDrawing(*b, it->faceOut[static_cast<size_t>(f.port)]).normalized();
        };
        for (const Branch& br : branches) {
            if (br.a.node.isEmpty() || br.b.node.isEmpty()) continue;
            const double real = br.special->run.lengthMm;
            const Vec2 fa = faceAt(br.a);
            const Vec2 fb = faceAt(br.b);
            const double dy = (-gridOf(br.b.node).y * rowPitch + drawDyOf(br.b.node) + fb.y) -
                              (-gridOf(br.a.node).y * rowPitch + drawDyOf(br.a.node) + fa.y);
            const double across = std::abs(dy);
            // AS FEW BENDS AS THE LENGTH ALLOWS. A face that looks out on the slant - a Y-split's leg - toward
            // the part: the run goes straight on out of it up to the other part's height and makes ONE bend
            // into it, so the part is put where that one-bend run is the run's length. Put anywhere nearer, the
            // wires ran on the slant past the part and came back to it in a hook.
            // Along the harness, which way from a to b (as the layout below will put it): the way a's face looks.
            const Vec2 ua0 = faceOutOf(br.a);
            const double ahead = std::abs(ua0.x) > 0.3 ? ua0.x : gridOf(br.b.node).x - gridOf(br.a.node).x;
            auto slanted = [&](const Vec2& u, double toward, double forward) {
                return std::abs(u.x) > 0.1 && std::abs(u.y) > 0.1 && across > 1e-6 && std::abs(forward) > 1e-9 &&
                       (u.y > 0.0) == (toward > 0.0) && (u.x > 0.0) == (forward > 0.0);
            };
            const Vec2 ua = faceOutOf(br.a), ub = faceOutOf(br.b);
            const bool slantA = slanted(ua, dy, ahead), slantB = slanted(ub, -dy, -ahead);
            double gap = 0.0, drawn = 0.0;
            // A bridge wire may bring its part level with the part its cable goes to: no room kept between.
            const double room = brickIsBridge(*br.special) ? 0.0 : kRoomBetween;
            if (slantA || slantB) {
                const Vec2 u = slantA ? ua : ub;
                const double leg = across / std::abs(u.y);           // on the slant
                const double legAlong = across * std::abs(u.x) / std::abs(u.y);
                gap = std::max({real - leg + legAlong, legAlong + kStraightIn, room});
                drawn = leg + gap - legAlong;
            } else {
                // Square out of both faces: out, across on the slant (45 degrees), and in - which is longer than
                // the two straight legs by the slant's extra.
                const double extra = across * (std::sqrt(2.0) - 1.0);
                gap = std::max(real - extra, room);
                const double slant = std::min(across, std::max(0.0, gap - 2.0 * kStraightIn));
                drawn = gap - slant + std::hypot(slant, across);
            }
            if (drawn > real + 1e-6)
                stretchOf.insert(br.special->id, std::max(stretchOf.value(br.special->id), drawn - real));
            edges[br.a.node] << Edge{br.b.node, gap, fa, fb, false, ua};
            edges[br.b.node] << Edge{br.a.node, gap, fb, fa, false, ub};
        }
        // A BOOT ON A PART: face on face, level with it - wherever the two bricks are on the pane.
        for (const BrickLink& l : design.links) {
            const Brick* ba = design.brick(l.a);
            const Brick* bb = design.brick(l.b);
            if (!ba || !bb || ba->type != BrickType::End || bb->type != BrickType::End) continue;
            FaceRef fa;
            fa.id = fa.node = l.a;
            fa.port = design.portOn(l.a, l.b);
            FaceRef fb;
            fb.id = fb.node = l.b;
            fb.port = design.portOn(l.b, l.a);
            const Vec2 pa = faceAt(fa), pb = faceAt(fb);
            edges[l.a] << Edge{l.b, 0.0, pa, pb, true};
            edges[l.b] << Edge{l.a, 0.0, pb, pa, true};
            const bool bootIsA = ba->kind == PartKind::HeatshrinkBoot;
            if (bootIsA) yLock.insert(l.a, {l.b, pb.y - pa.y});
            else yLock.insert(l.b, {l.a, pa.y - pb.y});
        }
        // A BRIDGE WIRE STARTS AT ITS CABLE'S PART - where that fan is - so its own part can stand level with it.
        for (auto it = bridgeOff.cbegin(); it != bridgeOff.cend(); ++it) {
            if (it.value().partThere.isEmpty()) continue;
            FaceRef pf;
            pf.node = it.value().partThere;
            pf.id = pf.node;
            pf.port = design.portOn(pf.node, it.value().source);
            const Vec2 at = faceAt(pf);
            // AT THE CABLE'S END FACE, not at the part: the bridge comes out where the fan starts, a fan's length
            // off the part - so its own part may stand nearer than the part the fan goes into.
            double fan = kWiresRunEnd;
            if (const Brick* src = design.brick(it.value().source)) {
                const int srcPort = design.portOn(src->id, pf.node);
                const double said = srcPort == kPortRight ? src->run.fanFinishMm : src->run.fanStartMm;
                const auto sh = shapes.constFind(pf.node);
                const double faceLen = sh != shapes.cend() && pf.port >= 0 && pf.port < sh->ports()
                                           ? sh->faceLen[static_cast<size_t>(pf.port)]
                                           : 10.0;
                fan = said >= 0.0 ? said : std::max(kWiresRunEnd, faceLen * design.faceFanPct / 100.0 / 2.0);
            }
            const Vec2 face = at + faceOutOf(pf) * fan;
            edges[pf.node] << Edge{it.key(), 0.0, face, Vec2{0.0, 0.0}, true};
            edges[it.key()] << Edge{pf.node, 0.0, Vec2{0.0, 0.0}, face, true};
        }
        double anchorCol = 0.0;
        bool haveAnchor = false;
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
            // THE FIRST PART ANCHORS THE DRAWING: it is at nought whatever column its brick is moved to on the
            // pane - moving bricks there does not move the drawing - and any other harness keeps its distance.
            if (!haveAnchor) {
                anchorCol = gridOf(root).x;
                haveAnchor = true;
            }
            xOf.insert(root, (gridOf(root).x - anchorCol) * 75.0);
            std::deque<QString> queue{root};
            while (!queue.empty()) {
                const QString n = queue.front();
                queue.pop_front();
                for (const Edge& e : edges.value(n)) {
                    if (xOf.contains(e.to)) continue;
                    // WHICH WAY THE RUN GOES: the way the face it leaves looks - not where the bricks sit on the
                    // pane, which says what is joined to what and nothing about the drawing. Only a face looking up or
                    // down leaves it to the pane's columns, and failing those, onward to the right: the run's length
                    // always counts.
                    const double dc = gridOf(e.to).x - gridOf(n).x;
                    // One face carrying more than one run (a part in the middle of a chain drawn with a single
                    // face) says nothing about which of them goes which way: the pane's columns do.
                    int sameFace = 0;
                    for (const Edge& o : edges.value(n))
                        if (!o.pinned && distance(o.fromFace, e.fromFace) < 1e-6) ++sameFace;
                    const bool faceSays = sameFace <= 1;
                    const double sign = faceSays && e.fromOut.x > 0.3    ? 1.0
                                        : faceSays && e.fromOut.x < -0.3 ? -1.0
                                        : dc > 1e-9          ? 1.0
                                        : dc < -1e-9         ? -1.0
                                                             : 1.0;
                    // Face to face along the harness.
                    xOf.insert(e.to, e.pinned ? xOf.value(n) + e.fromFace.x - e.toFace.x
                                              : xOf.value(n) + e.fromFace.x + sign * e.gap - e.toFace.x);
                    queue.push_back(e.to);
                }
            }
        }
    }
    // LINED UP WITH THE FACE IT MEETS: a part on a run too short to bend - pulled in toward nought - is set straight
    // out of the face at the run's length, turned to look back into it (at a slant too, a Y-split's leg), rather than
    // left beside it with no room for the wires to get across. Where it goes, and the turn it takes for it.
    struct Lined {
        Vec2 at;
        double turn = 0.0;   // counter-clockwise, on top of its own
    };
    QHash<QString, Lined> linedUp;
    auto posOf = [&](const QString& node) {
        if (linedUp.contains(node)) return linedUp.value(node).at;
        if (yLock.contains(node)) {
            const auto& lock = yLock.value(node);
            return Vec2{xOf.value(node), -gridOf(lock.first).y * rowPitch + drawDyOf(lock.first) + lock.second};
        }
        return Vec2{xOf.value(node), -gridOf(node).y * rowPitch + drawDyOf(node)};
    };
    {
        // How many runs each part is on: only a part on one run and nothing else is lined up.
        QHash<QString, int> runsOn;
        for (const Branch& br : branches) {
            if (!br.a.node.isEmpty()) ++runsOn[br.a.node];
            if (!br.b.node.isEmpty()) ++runsOn[br.b.node];
        }
        for (const BrickLink& l : design.links) {
            const Brick* la = design.brick(l.a);
            const Brick* lb = design.brick(l.b);
            if (la && lb && la->type == BrickType::End && lb->type == BrickType::End) {
                runsOn[l.a] += 2;
                runsOn[l.b] += 2;
            }
        }
        for (const Branch& br : branches) {
            if (br.a.node.isEmpty() || br.b.node.isEmpty() || br.trunk || br.splitOf >= 0) continue;
            if (brickIsBridge(*br.special)) continue;
            const Brick* ba = design.brick(br.a.node);
            const Brick* bb = design.brick(br.b.node);
            if (!ba || !bb || ba->type != BrickType::End || bb->type != BrickType::End) continue;
            // The part that moves: the one on this run alone - of two, the one further along the pane.
            const bool aAlone = runsOn.value(br.a.node) == 1, bAlone = runsOn.value(br.b.node) == 1;
            if (!aAlone && !bAlone) continue;
            const bool moveB = bAlone && (!aAlone || bb->col >= ba->col);
            const FaceRef& mine = moveB ? br.b : br.a;
            const FaceRef& other = moveB ? br.a : br.b;
            const Brick* mb = moveB ? bb : ba;
            const Brick* ob = moveB ? ba : bb;
            if (linedUp.contains(other.node) || yLock.contains(mine.node)) continue;
            const auto ms = shapes.constFind(mine.node);
            const auto os = shapes.constFind(other.node);
            if (ms == shapes.cend() || os == shapes.cend() || mine.port < 0 || mine.port >= ms->ports() ||
                other.port < 0 || other.port >= os->ports())
                continue;
            const Vec2 oOut = turnOnDrawing(*ob, os->faceOut[static_cast<size_t>(other.port)]).normalized();
            const Vec2 oFace = posOf(other.node) + turnOnDrawing(*ob, os->faceMid[static_cast<size_t>(other.port)]);
            const Vec2 mOut = turnOnDrawing(*mb, ms->faceOut[static_cast<size_t>(mine.port)]).normalized();
            const Vec2 mFace = posOf(mine.node) + turnOnDrawing(*mb, ms->faceMid[static_cast<size_t>(mine.port)]);
            // Room to bend: each end straight for its fan and its bend. Pulled in shorter than that there is no bending
            // across to it at all. (Longer, a run short for how far across it goes is drawn longer than it is, its
            // rows kept - see stretchOf.)
            const BrickRunSpec& r = br.special->run;
            const double across = std::abs(dot(mFace - oFace, oOut.perp()));
            const bool turned = dot(mOut, oOut * -1.0) < std::cos(1.0 * M_PI / 180.0);
            if (across < 0.5 && !turned) continue;
            const double straight = 15.0 + std::max(0.0, r.kneeRadiusMm);
            if (r.lengthMm >= 2.0 * straight) continue;
            Lined ln;
            ln.turn = angleOf(oOut * -1.0) - angleOf(mOut);
            const Vec2 target = oFace + oOut * std::max(0.0, r.lengthMm);
            ln.at = target - turnOnDrawing(*mb, ms->faceMid[static_cast<size_t>(mine.port)], ln.turn);
            linedUp.insert(mine.node, ln);
        }
    }

    // THE ENDS: each its shape turned by the brick's quarters and set down on its place, a face for
    // every port, and its designator in front of it.
    for (const Brick& b : design.bricks) {
        if (b.type != BrickType::End) continue;
        const BrickShape& shape = shapes[b.id];
        const Vec2 at = posOf(b.id);
        const double extra = linedUp.contains(b.id) ? linedUp.value(b.id).turn : 0.0;
        const double turn = -b.rot * M_PI / 2.0 - b.drawTurnDeg * M_PI / 180.0;
        auto place = [&](const Vec2& v) { return at + turnOnDrawing(b, v, extra); };
        std::vector<Entity> body;
        for (Entity e : shape.drawing) {
            // The placeholder's own caption gives way to the designator, written below.
            if (e.type == EntityType::Text && !b.libPart.isEmpty()) continue;
            if (e.type == EntityType::Text) continue;
            // As turnOnDrawing: turned, mirrored left for right on the sheet, then lined up with a face.
            if (std::abs(turn) > 1e-12) rotateEntity(e, Vec2{0.0, 0.0}, turn);
            if (b.drawMirror) mirrorEntity(e, Vec2{0.0, 0.0}, Vec2{0.0, 1.0});
            if (std::abs(extra) > 1e-12) rotateEntity(e, Vec2{0.0, 0.0}, extra);
            translateEntity(e, at);
            e.layer = opt.layer;
            body.push_back(e);
        }
        for (int p = 0; p < shape.ports(); ++p) {
            BrickFace f;
            f.centre = place(shape.faceMid[static_cast<size_t>(p)]);
            f.out = turnOnDrawing(b, shape.faceOut[static_cast<size_t>(p)], extra).normalized();
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
                const Vec2 out1 = turnOnDrawing(b, shape.faceOut[0], extra).normalized();
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
            // THE LEADER TOUCHES THE PART: its tip on the part's own outline nearest the top middle - the top of
            // the box round it is empty paper wherever the part is not as tall as its tallest point.
            Vec2 tip{(bb.min.x + bb.max.x) / 2.0, bb.max.y};
            {
                const Vec2 aim = tip;
                double best = 1e300;
                Vec2 on = tip;
                auto take = [&](const Vec2& p) {
                    const double dd = distance(p, aim);
                    if (dd < best) {
                        best = dd;
                        on = p;
                    }
                };
                auto segs = [&](const std::vector<Vec2>& pts, bool closed) {
                    for (size_t k = 0; k + 1 < pts.size(); ++k) take(closestPointOnSegment(aim, pts[k], pts[k + 1]).point);
                    if (closed && pts.size() > 2) take(closestPointOnSegment(aim, pts.back(), pts.front()).point);
                };
                for (const Entity& e : body) {
                    if (e.meta.value(QStringLiteral("mechRole")).toString() == QStringLiteral("designator")) continue;
                    switch (e.type) {
                        case EntityType::Line: take(closestPointOnSegment(aim, e.a, e.b).point); break;
                        case EntityType::Polyline: segs(e.points, e.meta.value(QStringLiteral("closed")).toBool()); break;
                        case EntityType::Rect: segs(e.points, true); break;
                        case EntityType::Circle:
                            if (e.radius > 1e-9)
                                take(e.center + ((aim - e.center).length() > 1e-9 ? (aim - e.center).normalized()
                                                                                    : Vec2{0.0, 1.0}) * e.radius);
                            break;
                        default: break;
                    }
                }
                if (best < 1e299) tip = on;
            }
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
        // SEVERAL RUNS ON ONE FACE: where this one lies in the bundle they make together, side by side and close -
        // across the face from its middle - once they have left their fans.
        bool shared = false;
        double compact = 0.0;
        // Never wider than they land: on a small face (a Y-split's leg) the bundle is taken in to the width they
        // land across, so they go straight on rather than spreading out after the face.
        double compactScale = 1.0;
    };
    std::vector<Landing> atA(branches.size()), atB(branches.size());
    auto isJunction = [](const FaceRef& f) { return f.id.startsWith(QLatin1String("~J")); };
    {
        struct Use {
            size_t branch;
            bool isB;
            int wires;
            double order;
            double tie = 0.0;   // for runs going the same way: the same order at both their faces
        };
        QHash<QString, std::vector<Use>> byFace;
        for (size_t i = 0; i < branches.size(); ++i) {
            const Branch& br = branches[i];
            if (!br.a.id.isEmpty() && br.splitOf < 0) {
                const QString ka = faceKey(br.a.id, br.a.port);
                const BrickFace fa = out.faces.value(ka);
                const Vec2 toB = br.b.id.isEmpty() ? fa.centre + fa.out : posOf(br.b.node);
                // Two runs going to the same place: ordered across the face by where their bricks are, measured
                // the same way on the drawing at both ends - so the one along the bottom here is along the bottom
                // there too, and they do not cross on the way.
                const Vec2 perp = fa.out.perp();
                const double tie = dot(Vec2{br.special->col, -br.special->row * 10.0}, perp) +
                                   (perp.y >= 0.0 ? 1e-3 : -1e-3) * static_cast<double>(i);
                byFace[ka].push_back({i, false, br.wires, dot(toB, perp), tie});
            }
            if (!br.b.id.isEmpty()) {
                const QString kb = faceKey(br.b.id, br.b.port);
                const BrickFace fb = out.faces.value(kb);
                const Vec2 toA = br.a.node.isEmpty() ? fb.centre + fb.out : posOf(br.a.node);
                const Vec2 perp = fb.out.perp();
                const double tie = dot(Vec2{br.special->col, -br.special->row * 10.0}, perp) +
                                   (perp.y >= 0.0 ? 1e-3 : -1e-3) * static_cast<double>(i);
                byFace[kb].push_back({i, true, br.wires, dot(toA, perp), tie});
            }
        }
        for (auto it = byFace.begin(); it != byFace.end(); ++it) {
            const BrickFace f = out.faces.value(it.key());
            std::vector<Use>& uses = it.value();
            std::stable_sort(uses.begin(), uses.end(), [](const Use& x, const Use& y) {
                if (std::abs(x.order - y.order) > 1e-6) return x.order < y.order;
                return x.tie < y.tie;
            });
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
            // The bundle they make together once out of their fans: each as wide as its wires packed, side by side.
            double bundle = 0.0;
            std::vector<double> widths;
            for (const Use& u : uses) {
                const double w = ww * 1.5 * (std::max(1, u.wires) - 1) + ww;   // touching, no gap
                widths.push_back(w);
                bundle += w;
            }
            int k = 0;
            double stacked = -bundle / 2.0;
            // The span of their landings' middles against the span of their places in the bundle.
            double compactScale = 1.0;
            if (uses.size() > 1) {
                const double landFirst = -(total - 1) / 2.0 + (uses.front().wires - 1) / 2.0;
                const double landLast = (total - 1) / 2.0 - (uses.back().wires - 1) / 2.0;
                const double landSpan = (landLast - landFirst) * lane;
                const double bundleSpan = bundle - widths.front() / 2.0 - widths.back() / 2.0;
                if (bundleSpan > 1e-9) compactScale = std::clamp(landSpan / bundleSpan, 0.0, 1.0);
            }
            for (size_t n = 0; n < uses.size(); ++n) {
                const Use& u = uses[n];
                Landing l;
                const double first = -(total - 1) / 2.0 + k;
                const double mid = first + (u.wires - 1) / 2.0;
                l.centre = f.centre + f.out.perp() * (mid * lane);
                l.lane = lane;
                l.share = f.lengthMm * u.wires / std::max(1, total);
                l.shared = uses.size() > 1 && !junction;
                l.compact = stacked + widths[n] / 2.0;
                l.compactScale = compactScale;
                stacked += widths[n];
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

    // THE RUNS - bridge wires last, since each starts where the fan of the run it leaves does.
    struct BridgeStart {
        Vec2 at;
        Vec2 dir;
        double half = 0.0;   // half the width of the face it comes out of
    };
    QHash<QString, BridgeStart> bridgeStart;
    std::vector<size_t> runOrder;
    for (size_t i = 0; i < branches.size(); ++i)
        if (!brickIsBridge(*branches[i].special)) runOrder.push_back(i);
    for (size_t i = 0; i < branches.size(); ++i)
        if (brickIsBridge(*branches[i].special)) runOrder.push_back(i);
    for (const size_t i : runOrder) {
        const Branch& br = branches[i];
        const Brick& s = *br.special;
        const BrickRunSpec& r = s.run;
        const bool isTrunk = br.trunk;
        const bool isSub = br.splitOf >= 0;
        const bool loose = br.b.id.isEmpty();
        const bool jA = isJunction(br.a), jB = isJunction(br.b);
        BrickFace fa = br.a.id.isEmpty() ? BrickFace{} : out.faces.value(faceKey(br.a.id, br.a.port));
        BrickFace fb = loose ? BrickFace{} : out.faces.value(faceKey(br.b.id, br.b.port));
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
        // A BRIDGE WIRE starts where the fan of the run it leaves begins - out of that fan, heading the way it
        // goes - rather than at a junction that would split the run and widen its fan.
        // It comes out of the fan ON THE SLANT, toward its own part - not straight on through the part the fan
        // goes into - and makes one bend into its part.
        bool bridgeOut = false;
        bool bridgeAtA = false;
        if (brickIsBridge(s)) {
            // OUT OF THE CABLE'S END FACE: on the face - the square end of the cover - toward the side its part is
            // on, both its lines leaving that face square to it, and turning at once toward its part.
            auto onFace = [](const BridgeStart& st, const Vec2& target) {
                Vec2 side = st.dir.perp();
                if (dot(target - st.at, side) < 0.0) side = side * -1.0;
                return st.at + side * (st.half * 0.55);
            };
            if (jA && bridgeStart.contains(br.a.node)) {
                const BridgeStart st = bridgeStart.value(br.a.node);
                la.centre = onFace(st, lb.centre);
                fa.out = st.dir;
                bridgeOut = true;
                bridgeAtA = true;
            } else if (jB && bridgeStart.contains(br.b.node)) {
                const BridgeStart st = bridgeStart.value(br.b.node);
                lb.centre = onFace(st, la.centre);
                fb.out = st.dir;
                bridgeOut = true;
            }
        }

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
        // GATHERED INTO A PART'S FACE, the bundle is never wider than the face: it converges to fit it (a Y-split's
        // leg is often narrower than the wires would lie packed).
        auto fitFace = [wires, ww](double share) {
            return wires > 1 ? std::max(ww * 0.6, (share * 0.95 - ww) / (wires - 1)) : 1.0;
        };
        if (!spreadA && !intoCableA) la.lane = jA ? packed : std::min(packed, fitFace(la.share));
        else if (spreadA && jA) la.lane = std::max(la.lane, packed * 2.0);
        if (!spreadB && !loose && !intoCableB) lb.lane = jB ? packed : std::min(packed, fitFace(lb.share));
        else if (spreadB && jB) lb.lane = std::max(lb.lane, packed * 2.0);
        double fanA = !spreadA ? 0.0
                            : fanSpecA >= 0.0 ? fanSpecA
                                              : std::max(kWiresRunEnd, la.lane * (wires - 1) / 2.0);
        double fanB = !spreadB ? 0.0
                            : fanSpecB >= 0.0 ? fanSpecB
                                              : std::max(kWiresRunEnd, lb.lane * (wires - 1) / 2.0);
        // Out of a cable's face a bridge wire turns at once: no straight lead of its own there.
        const double lead = bridgeOut ? 1.0 : std::max({fanA, fanB, 10.0}) + std::max(0.0, r.kneeRadiusMm) + 5.0;
        std::vector<Vec2> route;
        if (isTrunk && br.freeStart) route = {splitAt[i], splitAt[i] + splitDir[i] * r.lengthMm};
        else if (isTrunk) route = {la.centre, splitAt[i]};
        else if (isSub) route = routeBetweenFaces(la.centre, splitDir[i], lb.centre, fb.out, fanB + 5.0);
        else if (loose) route = {la.centre, la.centre + fa.out * r.lengthMm};
        else {
            // The bend where the special's brick is between its two parts on the pane.
            const double colA = gridOf(br.a.node).x, colB = gridOf(br.b.node).x;
            const double bendAt =
                std::abs(colB - colA) > 1e-6 ? std::clamp((s.col - colA) / (colB - colA), 0.05, 0.95) : 0.5;
            // In line with one of its parts, the special's brick is the knee where the run leaves that line.
            const double rowA = gridOf(br.a.node).y, rowB = gridOf(br.b.node).y;
            int knee = std::abs(rowA - rowB) < 1e-6        ? -1
                       : std::abs(s.row - rowA) < 0.25 ? 0
                       : std::abs(s.row - rowB) < 0.25 ? 1
                                                           : -1;
            double bend = bendAt;
            // A bridge wire turns as it leaves the cable's face.
            if (bridgeOut) {
                knee = bridgeAtA ? 0 : 1;
                bend = bridgeAtA ? 0.0 : 1.0;
            }
            route = routeBetweenFaces(la.centre, fa.out, lb.centre, fb.out, lead, 45.0, bend, knee);
            // SEVERAL RUNS BETWEEN THE SAME TWO FACES run together, close, not each straight from its own share of
            // the face with the face's width between them: out of its fan, over to its place in the bundle they make
            // (along the route of the faces' middles, offset by that place), and back out to its share of the other
            // face before its fan there.
            if (!bridgeOut && la.shared && lb.shared && !jA && !jB) {
                // Faces facing each other nearly in line: the bundle goes STRAIGHT across - no jog of its own for an
                // offset of a millimetre or two.
                const double sideways = dot(fb.centre - fa.centre, fa.out.perp());
                std::vector<Vec2> trunk;
                if (dot(fa.out, fb.out) < -0.999 && std::abs(sideways) < 3.0)
                    trunk = {fa.centre, fa.centre + fa.out * dot(fb.centre - fa.centre, fa.out)};
                else
                    trunk = routeBetweenFaces(fa.centre, fa.out, fb.centre, fb.out, lead, 45.0, bend, knee);
                // No point twice: a segment of no length has no side, and one at the start put the run on the wrong
                // side of the bundle - the runs crossing over each other on the way.
                std::vector<Vec2> clean;
                for (const Vec2& p : trunk)
                    if (clean.empty() || distance(clean.back(), p) > 1e-6) clean.push_back(p);
                // Offset square to the way out of the face A: this run's place in the bundle, the same side all along.
                const double place = la.compact * std::min(la.compactScale, lb.compactScale);
                const std::vector<Vec2> off = clean.size() >= 2 ? offsetPolyline(clean, place) : clean;
                if (off.size() >= 2) {
                    const double L1 = fanA + 2.0, L2 = fanB + 2.0;
                    const double dA = std::abs(dot(la.centre - off.front(), fa.out.perp()));
                    const double dB = std::abs(dot(lb.centre - off.back(), fb.out.perp()));
                    const double length = routeLength(off);
                    // Converging gently - over twice the distance it moves across - not in a sharp jog.
                    const double sA = L1 + 2.0 * dA, sB = length - (L2 + 2.0 * dB);
                    if (sB > sA + 1.0) {
                        std::vector<Vec2> together{la.centre, la.centre + fa.out * L1, pointAlong(off, sA)};
                        double at = 0.0;
                        for (size_t k = 1; k + 1 < off.size(); ++k) {
                            at += distance(off[k - 1], off[k]);
                            if (at > sA + 1e-6 && at < sB - 1e-6) together.push_back(off[k]);
                        }
                        together.push_back(pointAlong(off, sB));
                        together.push_back(lb.centre + fb.out * L2);
                        together.push_back(lb.centre);
                        // NO BEND THAT IS NOT NEEDED: a point where the run goes straight on is dropped, so a run
                        // already in its place goes straight through.
                        // The straight out of each face is kept: the wires settle from the face into the bundle along it.
                        const Vec2 keepA = la.centre + fa.out * L1, keepB = lb.centre + fb.out * L2;
                        auto kept = [&](const Vec2& p) { return distance(p, keepA) < 1e-6 || distance(p, keepB) < 1e-6; };
                        route.clear();
                        for (const Vec2& p : together) {
                            if (!route.empty() && distance(route.back(), p) < 1e-6) continue;
                            if (route.size() >= 2) {
                                const Vec2 d1 = (route.back() - route[route.size() - 2]).normalized();
                                const Vec2 d2 = (p - route.back()).normalized();
                                if (std::abs(cross(d1, d2)) < 1e-3 && dot(d1, d2) > 0.0 && !kept(route.back())) route.back() = p;
                                else route.push_back(p);
                            } else {
                                route.push_back(p);
                            }
                        }
                        // Nor a jog of a fraction of a millimetre: a point hardly off the line from the one before it
                        // to the one after it is dropped - the run goes straight on there.
                        for (size_t k = 1; k + 1 < route.size();) {
                            const Vec2 a = route[k - 1], c = route[k + 1];
                            const double len = distance(a, c);
                            const double off = len > 1e-9 ? std::abs(cross(c - a, route[k] - a)) / len : 0.0;
                            if (off < 0.5 && dot(route[k] - a, c - route[k]) > 0.0 && !kept(route[k])) route.erase(route.begin() + static_cast<long>(k));
                            else ++k;
                        }
                    }
                }
            }
        }
        if (route.size() < 2) continue;
        // A FAN ENDS BEFORE THE BEND: fanned wires are straight, and one run on past a knee was drawn as a spiral.
        // And a fan AS LONG AS THE WHOLE RUN leaves no cover at all: the wires fan out of one face and gather into
        // the other, as wires with no cable round them.
        bool noCover = false;
        if (!laidUpType(s.type) && wires > 1 && !isTrunk && !isSub) {
            const double total = routeLength(route);
            if (fanA + fanB >= total - 1.0 && (spreadA || spreadB)) {
                noCover = true;
                fanA = spreadA ? total / 2.0 : 0.0;
                fanB = spreadB ? total / 2.0 : 0.0;
            } else if (route.size() > 2) {
                const double knee = std::max(0.0, r.kneeRadiusMm) + 2.0;
                fanA = std::min(fanA, std::max(0.0, distance(route[0], route[1]) - knee));
                fanB = std::min(fanB, std::max(0.0, distance(route[route.size() - 2], route.back()) - knee));
            }
        }
        std::vector<Entity> made;

        const bool laidUp = s.type == BrickType::TwistedPair || s.type == BrickType::TwistedBundle;
        const QString outer = noCover ? QStringLiteral("wires") : r.outermost();
        double bodyWidth = 0.0;
        if (wires == 1) {
            // ONE WIRE is a wire, not a bundle.
            Entity w;
            w.type = EntityType::Polyline;
            // ITS BENDS ROUNDED like a bundle's - the bend radius said, or three times the wire across.
            w.points = filletedRoute(route, r.kneeRadiusMm >= 0.0 ? r.kneeRadiusMm : ww * 3.0);
            w.a = w.points.front();
            w.b = w.points.back();
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
                                    : std::min(std::max(r.bodyScalePct / 100.0 * share, packed * (wires - 1) * 0.6 + ww) +
                                                   0.6 * layers,
                                               // NEVER WIDER THAN THE FACE IT GOES INTO: a part scaled down takes the
                                               // cable down with it, converging to its entry face.
                                               std::max(ww * 1.5, loose ? la.share : std::min(la.share, lb.share)));
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
            // NO COVER - THE WIRES GATHERED ALL THE SAME. With the cable taken off, the wires ran the whole way
            // at the pitch they fan at, as wide as the face they left, and no fan could be seen. They gather
            // into a bundle the width a cable would have had, and fan out from it to fill each entry face.
            if (!laidUp && outer == QLatin1String("wires") && (spreadA || spreadB) && wires > 1) {
                const double share = loose ? la.share : std::min(la.share, lb.share);
                const double pitch = std::clamp(r.bodyScalePct / 100.0 * share / (wires - 1), ww, ww * 1.5);
                run.meta.insert(QStringLiteral("twistBodyPitch"), pitch);
            }
            if (r.kneeRadiusMm >= 0.0) run.meta.insert(QStringLiteral("twistKnee"), r.kneeRadiusMm);
            // CONVERGED TO so many lines where the wires are gathered, when it is said.
            if (!laidUp && r.trunkWires > 0)
                run.meta.insert(QStringLiteral("twistTrunkWires"), std::clamp(r.trunkWires, 1, wires));
            made.push_back(run);
        }

        // Where each bridge wire on this run leaves it: its end face, where its fan starts at that end, going on out
        // that way - and how wide the face is there, so the bridge comes out of the face and not its middle.
        for (auto it = bridgeOff.cbegin(); it != bridgeOff.cend(); ++it) {
            if (it.value().source != s.id || it.value().partThere.isEmpty()) continue;
            const bool atB = br.b.node == it.value().partThere, atA = br.a.node == it.value().partThere;
            if (!atA && !atB) continue;
            std::vector<Vec2> rev = route;
            if (atB) std::reverse(rev.begin(), rev.end());
            const double fan = atB ? fanB : fanA;
            Vec2 dir;
            const Vec2 at = pointAlong(rev, std::max(0.0, fan), &dir);
            const double half = std::max(bodyWidth / 2.0, packed * (wires - 1) / 2.0 + ww / 2.0);
            bridgeStart.insert(it.key(), BridgeStart{at, dir * -1.0, half});
        }
        // THE LAYERS' PART NUMBERS go with the drawing, so what it is made of can be looked up on it.
        if (!r.stackParts.isEmpty()) {
            QJsonArray ids;
            for (const QString& layer : r.stack)
                if (!r.stackParts.value(layer).isEmpty()) ids.append(r.stackParts.value(layer));
            if (!ids.isEmpty())
                for (Entity& e : made) e.meta.insert(QStringLiteral("stackParts"), ids);
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
        // ABOVE OR BELOW, as each dimension is told: the offset's sign that puts the line on that side, whichever
        // way the two points it measures between run.
        auto sideOff = [](const Vec2& p0, const Vec2& p1, double mag, bool below) {
            const Vec2 n = (p1 - p0).normalized().perp();
            const double up = std::abs(n.y) > 1e-9 ? (n.y > 0.0 ? 1.0 : -1.0) : (n.x < 0.0 ? 1.0 : -1.0);
            return up * (below ? -mag : mag);
        };
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
            // Near, far, or the middle of the part (halfway between the two); a nominal part by its face. And a
            // JOINT - a Y-split, a splice, anything with several faces - by the face this run goes into: its near and
            // far points are its own ends, and the run ends at its leg.
            auto byFace = [&](const QString& id) {
                const Brick* e = design.brick(id);
                return !e || e->type != BrickType::End || brickKindIsJoint(e->kind) || design.portCount(*e) > 1;
            };
            const Vec2 from = byFace(br.a.id) ? fa.centre
                              : nominal(br.a.id)    ? fa.nearAt
                              : design.dimFromMid ? (fa.nearAt + fa.farAt) * 0.5
                              : design.dimFromFar ? fa.farAt
                                                  : fa.nearAt;
            const Vec2 to = byFace(br.b.id) ? fb.centre
                            : nominal(br.b.id)    ? fb.nearAt
                            : design.dimToMid ? (fb.nearAt + fb.farAt) * 0.5
                            : design.dimToFar ? fb.farAt
                                              : fb.nearAt;
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
                dim(from, toAlong, sideOff(from, toAlong, gapOut + 10.0, r.dimTotalBelow), along, !straight,
                    r.dimTextTotal, "total");
            }
        }
        if (r.dimFans) {
            if (fanA > 1e-9) {
                const Vec2 p0 = route.front(), p1 = pointAlong(route, fanA);
                dim(p0, p1, sideOff(p0, p1, gapOut, r.dimFansBelow), fanA, false, r.dimTextFans, "fanStart");
            }
            if (fanB > 1e-9) {
                const Vec2 p0 = pointAlong(route, total - fanB), p1 = route.back();
                dim(p0, p1, sideOff(p0, p1, gapOut, r.dimFansBelow), fanB, false, r.dimTextFans, "fanFinish");
            }
        }
        if (r.dimBetween && !loose && !isSub && total > fanA + fanB) {
            const Vec2 p0 = pointAlong(route, fanA), p1 = pointAlong(route, total - fanB);
            dim(p0, p1, sideOff(p0, p1, gapOut, r.dimBetweenBelow), total - fanA - fanB, !straight, r.dimTextBetween,
                "between");
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

    // DRAWING ORDER: the bricks set to be drawn over others last, in the order they are told; the rest as built.
    // And a brick BROUGHT TO THE FRONT covers what it crosses: a mask the shape of it goes down first - the part's
    // closed outlines, a run's band along its route - so the wires and parts under it are hidden, not seen through.
    {
        bool any = false;
        for (const Brick& b : design.bricks) any = any || b.z != 0;
        if (any) {
            auto zOf = [&design](const Entity& e) {
                const Brick* b = design.brick(e.meta.value(QStringLiteral("brickId")).toString());
                return b ? b->z : 0;
            };
            std::stable_sort(out.entities.begin(), out.entities.end(),
                             [&zOf](const Entity& x, const Entity& y) { return zOf(x) < zOf(y); });
            auto maskOf = [](const Entity& e) {
                std::vector<Entity> m;
                auto wipe = [&m, &e](std::vector<Vec2> pts) {
                    if (pts.size() < 3) return;
                    Entity w;
                    w.type = EntityType::Wipeout;
                    w.points = std::move(pts);
                    w.meta.insert(QStringLiteral("noFrame"), true);
                    w.meta.insert(QStringLiteral("brickMask"), true);
                    w.meta.insert(QStringLiteral("brickId"), e.meta.value(QStringLiteral("brickId")));
                    m.push_back(w);
                };
                if (e.type == EntityType::Rect && e.points.size() >= 4) {
                    wipe(e.points);
                } else if (e.type == EntityType::Polyline && e.points.size() >= 4 &&
                           (e.meta.value(QStringLiteral("closed")).toBool() || distance(e.points.front(), e.points.back()) < 1e-6)) {
                    wipe(e.points);
                } else if (e.type == EntityType::Circle && e.radius > 1e-9) {
                    std::vector<Vec2> ring;
                    for (int k = 0; k < 24; ++k)
                        ring.push_back(e.center + Vec2{std::cos(k * M_PI / 12.0), std::sin(k * M_PI / 12.0)} * e.radius);
                    wipe(ring);
                } else if (e.type == EntityType::Polyline && e.points.size() >= 2 && e.meta.value(QStringLiteral("double")).toBool()) {
                    // ONE WIRE (a bridge wire): a band as wide as it is drawn, along it.
                    const double half = e.meta.value(QStringLiteral("gap")).toDouble(1.0) / 2.0 + 0.6;
                    for (size_t k = 0; k + 1 < e.points.size(); ++k) {
                        const Vec2 a = e.points[k], b = e.points[k + 1];
                        if (distance(a, b) < 1e-9) continue;
                        const Vec2 n = (b - a).normalized().perp() * half;
                        wipe({a - n, b - n, b + n, a + n});
                    }
                } else if (e.type == EntityType::TwistedPair && e.points.size() >= 2) {
                    // A band along the route, as wide as the body (or the wires) on it.
                    const double half = std::max(e.meta.value(QStringLiteral("twistBodyWidth")).toDouble(0.0),
                                                 e.meta.value(QStringLiteral("twistLane")).toDouble(1.0) *
                                                     (e.meta.value(QStringLiteral("twistWires")).toInt(2) - 1)) / 2.0 + 1.0;
                    for (size_t k = 0; k + 1 < e.points.size(); ++k) {
                        const Vec2 a = e.points[k], b = e.points[k + 1];
                        const Vec2 n = (b - a).normalized().perp() * half;
                        wipe({a - n, b - n, b + n, a + n});
                    }
                }
                return m;
            };
            std::vector<Entity> ordered;
            ordered.reserve(out.entities.size() * 2);
            for (const Entity& e : out.entities) {
                if (zOf(e) > 0 && e.type != EntityType::Text && e.type != EntityType::Dimension)
                    for (Entity& w : maskOf(e)) ordered.push_back(std::move(w));
                ordered.push_back(e);
            }
            out.entities = std::move(ordered);
        }
    }

    // DIMENSIONS PUT ON BY HAND: between two bricks' reference points (near or far, as the drawing says; a part of
    // nominal size by its face), the line above both or below both. Two parts on one run say the run's length -
    // what a harness is built to - and otherwise how far apart they are along the harness.
    if (!design.dims.empty()) {
        QHash<QString, Bounds> boxOf;
        for (const Entity& e : out.entities) {
            const QString id = e.meta.value(QStringLiteral("brickId")).toString();
            if (id.isEmpty() || e.meta.value(QStringLiteral("mechRole")).toString() == QStringLiteral("dim")) continue;
            const Bounds eb = e.bounds();
            if (eb.valid) {
                boxOf[id].expand(eb.min);
                boxOf[id].expand(eb.max);
            }
        }
        auto nominal = [&](const QString& id) {
            const Brick* e = design.brick(id);
            const LibraryPart* p = e && catalog && !e->libPart.isEmpty() ? catalog->byId(e->libPart) : nullptr;
            return p && partIsNominal(*p);
        };
        auto refOf = [&](const QString& id, bool far, bool mid, Vec2* face) {
            const auto it = out.faces.constFind(faceKey(id, 0));
            if (it == out.faces.cend()) {
                const Bounds bx = boxOf.value(id);
                const Vec2 c = bx.valid ? (bx.min + bx.max) * 0.5 : Vec2{};
                if (face) *face = c;
                return c;
            }
            if (face) *face = it->centre;
            if (const Brick* e = design.brick(id);
                e && (brickKindIsJoint(e->kind) || design.portCount(*e) > 1))
                return it->centre;   // a joint by its face
            if (nominal(id)) return it->nearAt;
            return mid ? (it->nearAt + it->farAt) * 0.5 : far ? it->farAt : it->nearAt;
        };
        // FIRST WHAT EACH ONE MEASURES AND WHICH SIDE IT GOES (the first part's side: above unless it says below),
        // THEN WHERE: on each side from one line past every part dimensioned there, the shortest nearest the
        // parts and each further one a step out wherever it would cross another - they never cross.
        struct Planned {
            Vec2 p0, p1;
            bool below = false;
            double said = 0.0;
            double top = 0.0, bottom = 0.0;
            int level = 0;
        };
        std::vector<Planned> plan;
        for (const BrickDim& dm : design.dims) {
            const Brick* ba = design.brick(dm.a);
            const Brick* bb = design.brick(dm.b);
            if (!ba || !bb || dm.a == dm.b) continue;
            Vec2 faceA, faceB;
            Vec2 p0 = refOf(dm.a, design.dimFromFar, design.dimFromMid, &faceA);
            Vec2 p1 = refOf(dm.b, design.dimToFar, design.dimToMid, &faceB);
            if (p1.x < p0.x) {
                std::swap(p0, p1);
                std::swap(faceA, faceB);
            }
            const Bounds boxA = boxOf.value(dm.a), boxB = boxOf.value(dm.b);
            double top = std::max(p0.y, p1.y), bottom = std::min(p0.y, p1.y);
            for (const Bounds& bx : {boxA, boxB})
                if (bx.valid) {
                    top = std::max(top, bx.max.y);
                    bottom = std::min(bottom, bx.min.y);
                }
            // On one run: the run's length, from reference to reference.
            double said = std::abs(p1.x - p0.x);
            for (const Brick& s : design.bricks) {
                QString sa, sb;
                if (!brickIsSpecial(s.type) || !specialOneToOne(design, s.id, &sa, &sb)) continue;
                if (!((sa == dm.a && sb == dm.b) || (sa == dm.b && sb == dm.a))) continue;
                said = s.run.lengthMm + distance(p0, faceA) + distance(p1, faceB);
                break;
            }
            plan.push_back(Planned{p0, p1, dm.below || ba->dimBelow, said, top, bottom, 0});
        }
        double baseAbove = -1e300, baseBelow = 1e300;
        for (const Planned& pl : plan) {
            if (pl.below) baseBelow = std::min(baseBelow, pl.bottom);
            else baseAbove = std::max(baseAbove, pl.top);
        }
        std::vector<size_t> byWidth(plan.size());
        for (size_t k = 0; k < plan.size(); ++k) byWidth[k] = k;
        std::stable_sort(byWidth.begin(), byWidth.end(), [&plan](size_t x, size_t y) {
            return plan[x].p1.x - plan[x].p0.x < plan[y].p1.x - plan[y].p0.x;
        });
        std::vector<size_t> placed;
        for (size_t k : byWidth) {
            Planned& pl = plan[k];
            int level = 0;
            for (bool clash = true; clash; ++level) {
                clash = false;
                for (size_t q : placed) {
                    const Planned& o = plan[q];
                    if (o.below != pl.below || o.level != level) continue;
                    if (pl.p0.x < o.p1.x + 2.0 && o.p0.x < pl.p1.x + 2.0) clash = true;
                }
                if (!clash) break;
            }
            pl.level = level;
            placed.push_back(k);
        }
        int n = 0;
        for (const Planned& pl : plan) {
            const Vec2 p0 = pl.p0, p1 = pl.p1;
            const double said = pl.said;
            const double lineY = pl.below ? baseBelow - 12.0 - pl.level * 10.0 : baseAbove + 12.0 + pl.level * 10.0;
            Entity d;
            d.type = EntityType::Dimension;
            d.a = p0;
            d.b = p1;
            d.points = {p0, p1, Vec2{(p0.x + p1.x) / 2.0, lineY}};
            d.layer = QStringLiteral("0");
            d.meta.insert(QStringLiteral("dimType"), QStringLiteral("linear"));
            d.meta.insert(QStringLiteral("textHeight"), 3.5);
            d.meta.insert(QStringLiteral("arrowSize"), 2.3);
            d.meta.insert(QStringLiteral("extOffset"), 2.0);
            d.meta.insert(QStringLiteral("decimals"), 1);
            d.meta.insert(QStringLiteral("mechRole"), QStringLiteral("dim"));
            d.meta.insert(QStringLiteral("brickDim"), QStringLiteral("manual"));
            d.meta.insert(QStringLiteral("brickDimIndex"), n++);
            d.meta.insert(QStringLiteral("brickMeasured"), said);
            d.meta.insert(QStringLiteral("textOverride"), QString::number(said, 'f', 1));
            out.entities.push_back(d);
        }
    }
    return out;
}

} // namespace Edraw_Desktop
