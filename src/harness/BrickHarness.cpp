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
                                                   {"labels", labels},
                                                   {"nets", QJsonArray::fromStringList(r.nets)}};
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
            for (const QJsonValue x : r.value(QStringLiteral("nets")).toArray()) s.nets << x.toString();
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

BrickShape specialShape() {
    BrickShape s;
    s.faceMid = {Vec2{-40.0, 0.0}, Vec2{40.0, 0.0}};
    s.faceOut = {Vec2{-1.0, 0.0}, Vec2{1.0, 0.0}};
    s.faceLen = {10.0, 10.0};
    s.half = Vec2{40.0, 15.0};
    return s;
}

BrickShape endShape(const Brick& b, const PartsCatalog* catalog) {
    BrickShape s;
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

BrickShape BrickDesign::shapeOf(const Brick& b) const {
    if (b.type != BrickType::End) return specialShape();
    const QString key = partKindKey(b.kind) + QLatin1Char('|') + b.libPart + QLatin1Char('|') + b.name +
                        QLatin1Char('|') + QString::number(reinterpret_cast<quintptr>(catalog));
    const auto it = shapeCache.constFind(key);
    if (it != shapeCache.constEnd()) return it.value();
    const BrickShape s = endShape(b, catalog);
    shapeCache.insert(key, s);
    return s;
}

BrickBuild buildBrickHarness(const BrickDesign& design, const PartsCatalog* catalog,
                             const std::function<int(const QString& libPart)>& itemOf) {
    BrickBuild out;
    MechDrawOptions opt;

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

    // THE BRANCHES: every stretch of harness from one face to another. A run is one; a fanout is one
    // to each part it finishes on.
    struct Branch {
        const Brick* special = nullptr;
        QString endA, endB;   // endB empty: a loose end
        int portA = 0, portB = 0;
        int wires = 2;
        bool trunk = false;   // a cable that splits: the stretch up to where it does
        int splitOf = -1;     // one of the runs out of that split: the trunk's index
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
        // A FANOUT, or A CABLE WITH A FAN, finishing on several parts. A fanout sends a branch from
        // the part to each; a cable runs as one to where it splits, and its wires fan out from there
        // to each part - the fan split between the connectors.
        if ((s.type == BrickType::Fanout || s.type == BrickType::Run) && b.size() > 1) {
            const bool split = s.type == BrickType::Run;
            int trunkAt = -1;
            if (split) {
                Branch t;
                t.special = &s;
                t.endA = a.front();
                t.portA = design.portOn(a.front(), s.id);
                t.trunk = true;
                t.wires = 0;
                trunkAt = static_cast<int>(branches.size());
                branches.push_back(t);
            }
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
                br.splitOf = trunkAt;
                if (trunkAt >= 0) branches[static_cast<size_t>(trunkAt)].wires += br.wires;
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
    for (const Brick& b : design.bricks)
        if (b.type == BrickType::End) cols.push_back(b.col);
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

    // THE ENDS: each its shape turned by the brick's quarters and set down on its place, a face for
    // every port.
    for (const Brick& b : design.bricks) {
        if (b.type != BrickType::End) continue;
        const BrickShape& shape = shapes[b.id];
        const Vec2 at = endAt(b);
        const double turn = -b.rot * M_PI / 2.0;
        auto place = [&](const Vec2& v) { return at + turnQuarters(v, b.rot); };
        std::vector<Entity> body = shape.drawing;
        for (Entity& e : body) {
            if (b.rot % 4 != 0) rotateEntity(e, Vec2{0.0, 0.0}, turn);
            translateEntity(e, at);
            e.layer = opt.layer;
        }
        for (int p = 0; p < shape.ports(); ++p) {
            BrickFace f;
            f.centre = place(shape.faceMid[static_cast<size_t>(p)]);
            f.out = turnQuarters(shape.faceOut[static_cast<size_t>(p)], b.rot);
            f.lengthMm = shape.faceLen[static_cast<size_t>(p)];
            // The part's reach behind this face, for a dimension taken from its far point.
            const Vec2 o = shape.faceOut[static_cast<size_t>(p)];
            const double depth = std::abs(o.x) * shape.half.x * 2.0 + std::abs(o.y) * shape.half.y * 2.0;
            f.nearAt = shape.haveEnds ? place(shape.nearAt) : f.centre;
            f.farAt = shape.haveEnds ? place(shape.farAt) : f.centre - f.out * depth;
            out.faces.insert(faceKey(b.id, p), f);
        }
        // A BALLOON, pointing at the middle of the part, numbered by its line of the bill.
        if (b.balloon) {
            Bounds bb;
            for (const Entity& e : body) bb.expand(e.bounds());
            if (bb.valid) {
                // THE LEADER lands on the top edge of the part, from a balloon up and to one side
                // of it - far enough off that the leader is a line and not a stub inside the part.
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
                // array holding it, and the leaders came out as [x, y] - which the renderer reads as no tip at all.
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
            if (br.splitOf < 0) byFace[ka].push_back({i, false, br.wires, dot(toB, fa.out.perp())});
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

    // WHERE A CABLE SPLITS: straight out of its part, short of the nearest part it finishes on by
    // room for the fans, and each run out of it starting side by side across the cable's end, in
    // the order of where they go.
    std::vector<Vec2> splitAt(branches.size()), splitDir(branches.size(), Vec2{1.0, 0.0});
    for (size_t t = 0; t < branches.size(); ++t) {
        if (!branches[t].trunk) continue;
        const BrickRunSpec& r = branches[t].special->run;
        const BrickFace fa = out.faces.value(faceKey(branches[t].endA, branches[t].portA));
        const double ww = std::max(0.1, r.wireWidthMm);
        std::vector<size_t> subs;
        double nearest = 1e300, fanMax = 0.0;
        for (size_t k = 0; k < branches.size(); ++k) {
            if (branches[k].splitOf != static_cast<int>(t)) continue;
            subs.push_back(k);
            nearest = std::min(nearest, dot(atB[k].centre - atA[t].centre, fa.out));
            const int w = std::max(1, branches[k].wires);
            fanMax = std::max(fanMax, r.fanFinishMm > 0.0 ? r.fanFinishMm
                                                         : std::max(kWiresRunEnd, atB[k].lane * (w - 1) / 2.0));
        }
        if (subs.empty()) continue;
        const double d = std::max(15.0, std::min(nearest * 0.45, nearest - fanMax - 10.0));
        const Vec2 at = atA[t].centre + fa.out * d;
        splitAt[t] = at;
        std::stable_sort(subs.begin(), subs.end(), [&](size_t x, size_t y) {
            return dot(atB[x].centre, fa.out.perp()) < dot(atB[y].centre, fa.out.perp());
        });
        const double packed = ww * 1.5;
        double total = 0.0;
        for (size_t k : subs) total += std::max(1, branches[k].wires) * packed;
        double acc = -total / 2.0;
        for (size_t k : subs) {
            const double width = std::max(1, branches[k].wires) * packed;
            splitAt[k] = at + fa.out.perp() * (acc + width / 2.0);
            splitDir[k] = fa.out;
            acc += width;
        }
    }

    // A LABEL: the house label model - the one the Special ribbon puts on a harness - fitted across
    // what it goes round, along the run, with its text on it.
    std::optional<MechModel> labelModel;
    auto labelEntities = [&](const Vec2& mid, const Vec2& dir, double span, const QString& text,
                             bool small) {
        if (!labelModel) {
            MechModelSet set;
            set.load();
            const MechModel* m = set.of(MechModelKind::Label);
            labelModel = m && !m->isEmpty() ? *m : builtInMechModel(MechModelKind::Label);
        }
        const double margin = labelModel->exceedMm > 0.0 ? labelModel->exceedMm
                                                         : std::max(1.0, span * 0.12);
        const double across = span + 2.0 * margin;
        const double along = small ? std::max(8.0, across * 1.8) : std::max(18.0, across * 1.3);
        std::vector<Entity> ents = mechModelEntities(*labelModel, Vec2{0.0, 0.0}, 0.0, along, across);
        Bounds bb;
        for (const Entity& e : ents) bb.expand(e.bounds());
        const Vec2 c = bb.valid ? (bb.min + bb.max) * 0.5 : Vec2{0.0, 0.0};
        // Read the right way up: a run going leftward has its label turned round.
        Vec2 d = dir.normalized();
        if (d.x < -1e-9 || (std::abs(d.x) <= 1e-9 && d.y < 0.0)) d = d * -1.0;
        const double turn = angleOf(d);
        for (Entity& e : ents) {
            translateEntity(e, c * -1.0);
            rotateEntity(e, Vec2{0.0, 0.0}, turn);
            translateEntity(e, mid);
            e.meta.insert(QStringLiteral("mechRole"), QStringLiteral("label"));
        }
        if (!text.isEmpty()) {
            Entity t = makeTextAt(mid, text, std::clamp(across * 0.55, 1.2, along / std::max(1, int(text.size())) * 1.6), turn);
            t.meta.insert(QStringLiteral("justify"), QStringLiteral("MC"));
            t.meta.insert(QStringLiteral("mechRole"), QStringLiteral("label"));
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
        const BrickFace fa = out.faces.value(faceKey(br.endA, br.portA));
        const bool isTrunk = br.trunk;
        const bool isSub = br.splitOf >= 0;
        const bool loose = br.endB.isEmpty();
        const BrickFace fb = loose ? BrickFace{} : out.faces.value(faceKey(br.endB, br.portB));
        const int wires = std::max(1, br.wires);
        const double ww = std::max(0.1, r.wireWidthMm);
        Landing la = atA[i];
        if (isSub) {
            la.centre = splitAt[i];
            la.lane = ww * 1.5;
            la.share = ww * 1.5 * wires;
        }
        const Landing lb = loose ? la : atB[i];
        // The fans: as long as asked, or as long as the wires need to spread.
        const double fanA = isSub ? 0.0
                            : r.fanStartMm >= 0.0 ? r.fanStartMm
                                                : std::max(kWiresRunEnd, la.lane * (wires - 1) / 2.0);
        const double fanB = loose ? 0.0
                            : r.fanFinishMm >= 0.0 ? r.fanFinishMm
                                                   : std::max(kWiresRunEnd, lb.lane * (wires - 1) / 2.0);
        const double lead = std::max({fanA, fanB, 10.0}) + std::max(0.0, r.kneeRadiusMm) + 5.0;
        std::vector<Vec2> route;
        if (isTrunk) route = {la.centre, splitAt[i]};
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
                const double spreadA = la.lane * (wires - 1) + ww;
                const double spreadB = lb.lane * (wires - 1) + ww;
                run.meta.insert(QStringLiteral("twistWidth"), std::min(spreadA, spreadB));
                run.meta.insert(QStringLiteral("twistPitch"), kTwistedPairPitch);
                run.meta.insert(QStringLiteral("twistEnds"),
                                r.fanStartMm >= 0.0 ? r.fanStartMm : kTwistedPairLead);
            } else {
                run.meta.insert(QStringLiteral("twistSpread"), isSub   ? QStringLiteral("finish")
                                                               : loose ? QStringLiteral("start")
                                                                       : QStringLiteral("both"));
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
                if (outer != QLatin1String("wires") && !isSub) {
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
        if (r.dimTotal && !loose && !isSub) {
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
            if (!isSub) {
                Entity d = makeDim(route.front(), pointAlong(route, fanA), gapOut, QString());
                d.meta.insert(QStringLiteral("brickDim"), QStringLiteral("fanStart"));
                made.push_back(d);
            }
            if (!loose) {
                Entity e = makeDim(pointAlong(route, total - fanB), route.back(), gapOut, QString());
                e.meta.insert(QStringLiteral("brickDim"), QStringLiteral("fanFinish"));
                made.push_back(e);
            }
        }
        if (r.dimBetween && !loose && !isSub && total > fanA + fanB) {
            const Vec2 p0 = pointAlong(route, fanA), p1 = pointAlong(route, total - fanB);
            Entity d = makeDim(p0, p1, gapOut, straight ? QString() : fmt(total - fanA - fanB));
            d.meta.insert(QStringLiteral("brickDim"), QStringLiteral("between"));
            made.push_back(d);
        }

        // LABELS: on the whole bundle, or on one wire of a fan, so far along from the end chosen -
        // on the special's first run, or on the cable up to where it splits.
        if (labelBranch.value(&s) == i && !r.labels.empty()) {
            // EVERY WIRE'S OWN PATH, as the harness draws it: each wire is drawn as two edges, and
            // the middle of the two is where a label on that wire is centred.
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
                    Vec2 dir;
                    const Vec2 at = pointAlong(route, s0, &dir);
                    const double span = bodyWidth > 0.0 ? bodyWidth
                                                        : ww * 1.5 * (std::min(wires, 3) - 1) + ww;
                    for (Entity& e : labelEntities(at, dir, span, lab.text, false)) made.push_back(e);
                    continue;
                }
                // ON ONE WIRE: the wires fanned at that end, numbered from the top down (from the
                // left, where the fan runs up or down the sheet), and the label centred on that
                // wire so far from the face.
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
                Vec2 dir;
                const Vec2 at = pointAlong(fan[static_cast<size_t>(k)], lab.distanceMm, &dir);
                QString text = lab.text;
                if (text.isEmpty() && k < r.nets.size()) text = r.nets[k];
                for (Entity& e : labelEntities(at, dir, ww, text, true)) made.push_back(e);
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
