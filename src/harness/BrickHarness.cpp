#include "harness/BrickHarness.h"

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
            PartKind::Splice,    PartKind::ContactPin};
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
    if (type == BrickType::Run) b.run.body = RunBody::Cable;
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

bool BrickDesign::linked(const QString& a, const QString& b) const {
    for (const BrickLink& l : links)
        if ((l.a == a && l.b == b) || (l.a == b && l.b == a)) return true;
    return false;
}

QStringList BrickDesign::linksOf(const QString& id) const {
    QStringList out;
    for (const BrickLink& l : links) {
        if (l.a == id) out << l.b;
        else if (l.b == id) out << l.a;
    }
    return out;
}

QStringList BrickDesign::endsOf(const QString& specialId) const {
    QStringList ends;
    for (const QString& o : linksOf(specialId))
        if (const Brick* b = brick(o); b && b->type == BrickType::End) ends << o;
    std::stable_sort(ends.begin(), ends.end(), [this](const QString& x, const QString& y) {
        const Brick* bx = brick(x);
        const Brick* by = brick(y);
        return bx && by && (bx->col < by->col || (bx->col == by->col && bx->row < by->row));
    });
    return ends;
}

bool BrickDesign::connect(const QString& a, const QString& b, QString* why) {
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
    const QString special = brickIsSpecial(x->type) ? a : b;
    if (endsOf(special).size() >= 2)
        return refuse(QStringLiteral("That run already goes between two end parts"));
    links.push_back({a, b});
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

namespace {

QString bodyKey(RunBody b) {
    return b == RunBody::Cable        ? QStringLiteral("cable")
           : b == RunBody::Heatshrink ? QStringLiteral("heatshrink")
                                      : QStringLiteral("wires");
}

RunBody bodyFromKey(const QString& k) {
    if (k == QLatin1String("cable")) return RunBody::Cable;
    if (k == QLatin1String("heatshrink")) return RunBody::Heatshrink;
    return RunBody::Wires;
}

} // namespace

QJsonObject BrickDesign::toJson() const {
    QJsonArray bs;
    for (const Brick& b : bricks) {
        QJsonObject o{{"id", b.id},
                      {"type", brickTypeKey(b.type)},
                      {"kind", partKindKey(b.kind)},
                      {"libPart", b.libPart},
                      {"name", b.name},
                      {"col", b.col},
                      {"row", b.row},
                      {"balloon", b.balloon}};
        if (brickIsSpecial(b.type)) {
            const BrickRunSpec& r = b.run;
            o[QStringLiteral("run")] = QJsonObject{{"wires", r.wires},
                                                   {"lengthMm", r.lengthMm},
                                                   {"wireWidthMm", r.wireWidthMm},
                                                   {"fanStartMm", r.fanStartMm},
                                                   {"fanFinishMm", r.fanFinishMm},
                                                   {"kneeRadiusMm", r.kneeRadiusMm},
                                                   {"fanKneeMm", r.fanKneeMm},
                                                   {"fanScalePct", r.fanScalePct},
                                                   {"body", bodyKey(r.body)},
                                                   {"bodyScalePct", r.bodyScalePct},
                                                   {"bodyWidthMm", r.bodyWidthMm}};
        }
        bs.append(o);
    }
    QJsonArray ls;
    for (const BrickLink& l : links) ls.append(QJsonArray{l.a, l.b});
    return QJsonObject{{"format", "edraw-brick-harness"}, {"bricks", bs}, {"links", ls}};
}

BrickDesign BrickDesign::fromJson(const QJsonObject& o) {
    BrickDesign d;
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
            s.body = bodyFromKey(r.value(QStringLiteral("body")).toString());
            s.bodyScalePct = r.value(QStringLiteral("bodyScalePct")).toDouble(s.bodyScalePct);
            s.bodyWidthMm = r.value(QStringLiteral("bodyWidthMm")).toDouble(s.bodyWidthMm);
        }
        d.bricks.push_back(b);
    }
    for (const QJsonValue v : o.value(QStringLiteral("links")).toArray()) {
        const QJsonArray l = v.toArray();
        if (l.size() == 2) d.links.push_back({l[0].toString(), l[1].toString()});
    }
    return d;
}

// ---- Arranging -----------------------------------------------------------------------------------

void arrangeBricks(BrickDesign& design) {
    // The ends, and which ends each is joined to through a special.
    QHash<QString, QStringList> joined;
    for (const Brick& s : design.bricks) {
        if (!brickIsSpecial(s.type)) continue;
        const QStringList ends = design.endsOf(s.id);
        if (ends.size() == 2) {
            joined[ends[0]] << ends[1];
            joined[ends[1]] << ends[0];
        } else if (ends.size() == 1) {
            joined[ends[0]];   // on the harness, if only at a loose end
        }
    }
    QStringList pending;
    for (const Brick& b : design.bricks)
        if (b.type == BrickType::End && joined.contains(b.id)) pending << b.id;

    // One harness at a time - a design can hold more than one - each from its most connected end.
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

        // COLUMNS: out from the root, each end one column further than the one it was reached
        // from - on the side of the root it was already on, so a harness that goes both ways from
        // its middle keeps going both ways.
        QHash<QString, int> col;
        QHash<QString, QString> parent;
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
                parent[n] = id;
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
        if (ends.size() == 2) {
            const Brick* a = design.brick(ends[0]);
            const Brick* b = design.brick(ends[1]);
            s.col = (a->col + b->col) / 2.0;
            s.row = (a->row + b->row) / 2.0;
            if (a->col == b->col) s.col += 1.0;
        } else if (ends.size() == 1) {
            const Brick* a = design.brick(ends[0]);
            s.col = a->col + 1.0;
            s.row = a->row;
        }
    }
}

// ---- Routing -------------------------------------------------------------------------------------

namespace {

// No point twice, and no knee that is not a turn.
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
        if (std::abs(side) < 1e-6 && fwd > 0.0) return done();   // straight across
        if (fwd > 0.0) {
            // A LEVEL SHIFT: straight out, across on the slant, straight in.
            const double deg = std::clamp(shiftDeg, 1.0, 90.0);
            double slant = deg >= 89.999 ? 0.0 : std::abs(side) / std::tan(deg * M_PI / 180.0);
            const double room = fwd - 2.0 * std::min(lead, fwd / 2.0);
            if (slant > room) slant = std::max(0.0, room);   // steeper, so that it fits
            const double leg = (fwd - slant) / 2.0;
            const Vec2 p1 = a + u * leg;
            path.push_back(p1);
            path.push_back(p1 + u * slant + left * side);
            return done();
        }
        // Behind one another: out of each, and round.
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
        const Vec2 p1 = a + u * depth;
        path.push_back(p1);
        path.push_back(b + w * (depth - fwd));
        return done();
    }

    // Anything else: out of each by the lead, and one corner between.
    const Vec2 p1 = a + u * lead;
    const Vec2 p4 = b + w * lead;
    path.push_back(p1);
    path.push_back(p1 + u * dot(p4 - p1, u));
    path.push_back(p4);
    return done();
}

// ---- The drawing ---------------------------------------------------------------------------------

namespace {

// The drawing an end is shown with until a library part is chosen: a body of the kind's rough size
// with the face on the harness side.
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
    case PartKind::Splice: return {16.0, 8.0};
    case PartKind::ContactPin: return {12.0, 4.0};
    default: return {20.0, 14.0};
    }
}

void stampBrick(std::vector<Entity>& ents, const QString& id) {
    for (Entity& e : ents) e.meta.insert(QStringLiteral("brickId"), id);
}

} // namespace

BrickBuild buildBrickHarness(const BrickDesign& design, const PartsCatalog* catalog) {
    BrickBuild out;
    MechDrawOptions opt;

    // WHAT EACH END IS, and how long its face is.
    struct EndInfo {
        const Brick* brick = nullptr;
        const LibraryPart* part = nullptr;
        double faceLen = 20.0;
    };
    QHash<QString, EndInfo> ends;
    double widest = 0.0;
    for (const Brick& b : design.bricks) {
        if (b.type != BrickType::End) continue;
        EndInfo info;
        info.brick = &b;
        info.part = catalog && !b.libPart.isEmpty() ? catalog->byId(b.libPart) : nullptr;
        if (info.part && !info.part->hasMechanical()) info.part = nullptr;
        if (info.part) {
            const PlacedLibraryPart probe =
                placeLibraryPartAtFace(*info.part, Vec2{0, 0}, 0.0, Vec2{-1, 0}, opt);
            Bounds bb;
            for (const Entity& e : probe.entities) bb.expand(e.bounds());
            info.faceLen = probe.faceLengthMm > 1e-6 ? probe.faceLengthMm
                                                     : (bb.valid ? bb.max.y - bb.min.y : 20.0);
            widest = std::max(widest, bb.valid ? bb.max.y - bb.min.y : info.faceLen);
        } else {
            info.faceLen = placeholderFor(b.kind).across;
            widest = std::max(widest, info.faceLen);
        }
        ends.insert(b.id, info);
    }
    const double rowPitch = std::max(40.0, widest + 25.0);

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
    for (const Brick& s : design.bricks) {
        if (!brickIsSpecial(s.type)) continue;
        const QStringList e = design.endsOf(s.id);
        if (e.size() != 2) continue;
        const int i0 = colIndex(design.brick(e[0])->col);
        const int i1 = colIndex(design.brick(e[1])->col);
        const int lo = std::min(i0, i1), hi = std::max(i0, i1);
        if (hi == lo) continue;
        const double share = s.run.lengthMm / (hi - lo);
        for (int k = lo; k < hi; ++k) gap[k] = std::max(gap[k], share);
    }
    std::vector<double> colX(cols.size(), 0.0);
    for (size_t i = 1; i < cols.size(); ++i)
        colX[i] = colX[i - 1] + (gap[i - 1] > 1e-9 ? gap[i - 1] : 150.0);
    auto endAt = [&](const Brick& b) {
        return Vec2{colX[colIndex(b.col)], -b.row * rowPitch};
    };

    // WHICH WAY EACH END FACES: toward what it is joined to.
    QHash<QString, QStringList> runsAt;   // end -> the specials landing on it
    for (const Brick& s : design.bricks)
        if (brickIsSpecial(s.type))
            for (const QString& e : design.endsOf(s.id)) runsAt[e] << s.id;
    auto otherEnd = [&](const QString& special, const QString& end) -> const Brick* {
        for (const QString& e : design.endsOf(special))
            if (e != end) return design.brick(e);
        return nullptr;
    };
    for (auto it = ends.begin(); it != ends.end(); ++it) {
        const Brick& b = *it.value().brick;
        const Vec2 at = endAt(b);
        Vec2 toward{0.0, 0.0};
        for (const QString& s : runsAt.value(b.id)) {
            if (const Brick* o = otherEnd(s, b.id)) toward += endAt(*o) - at;
            else if (const Brick* sp = design.brick(s)) toward += Vec2{sp->col - b.col, 0.0};
        }
        Vec2 outDir{1.0, 0.0};
        if (std::abs(toward.x) > 1e-9) outDir = Vec2{toward.x > 0.0 ? 1.0 : -1.0, 0.0};
        else if (std::abs(toward.y) > 1e-9) outDir = Vec2{0.0, toward.y > 0.0 ? 1.0 : -1.0};
        BrickFace f;
        f.centre = at;
        f.out = outDir;
        f.lengthMm = it.value().faceLen;
        out.faces.insert(b.id, f);

        // The part itself.
        std::vector<Entity> body;
        if (const LibraryPart* part = it.value().part) {
            body = placeLibraryPartAtFace(*part, at, angleOf(outDir), outDir * -1.0, opt).entities;
        } else {
            const Placeholder ph = placeholderFor(b.kind);
            const Vec2 back = outDir * -1.0;
            const Vec2 across = outDir.perp() * (ph.across / 2.0);
            Entity box;
            box.type = EntityType::Polyline;
            box.points = {at + across, at + across + back * ph.along, at - across + back * ph.along,
                          at - across, at + across};
            box.a = box.points.front();
            box.b = box.points.back();
            box.layer = opt.layer;
            body.push_back(box);
            Entity label;
            label.type = EntityType::Text;
            label.a = at + back * (ph.along / 2.0) + Vec2{0.0, ph.across / 2.0 + 2.0};
            label.meta.insert(QStringLiteral("text"),
                              b.name.isEmpty() ? partKindName(b.kind) : b.name);
            label.meta.insert(QStringLiteral("height"), 3.0);
            label.layer = opt.layer;
            body.push_back(label);
        }
        stampBrick(body, b.id);
        for (Entity& e : body) out.entities.push_back(std::move(e));
    }

    // WHERE ON ITS FACE EACH RUN LANDS: a face with several runs on it is shared between them, in
    // the order of where they go, so they leave it side by side and do not cross.
    auto slice = [&](const QString& end, const QString& special, Vec2& centre, double& len) {
        const BrickFace f = out.faces.value(end);
        QStringList here = runsAt.value(end);
        std::stable_sort(here.begin(), here.end(), [&](const QString& x, const QString& y) {
            const Brick* ox = otherEnd(x, end);
            const Brick* oy = otherEnd(y, end);
            const Vec2 px = ox ? endAt(*ox) : f.centre;
            const Vec2 py = oy ? endAt(*oy) : f.centre;
            return dot(px, f.out.perp()) < dot(py, f.out.perp());
        });
        const int k = std::max<int>(1, static_cast<int>(here.size()));
        const int i = std::max<int>(0, static_cast<int>(here.indexOf(special)));
        len = f.lengthMm / k;
        centre = f.centre + f.out.perp() * (-f.lengthMm / 2.0 + len * (i + 0.5));
    };

    // THE RUNS.
    for (const Brick& s : design.bricks) {
        if (!brickIsSpecial(s.type)) continue;
        const QStringList e = design.endsOf(s.id);
        if (e.isEmpty()) continue;
        const BrickRunSpec& r = s.run;
        const int wires = std::clamp(r.wires, 2, kTwistedMaxWires);
        const double ww = std::max(0.1, r.wireWidthMm);
        auto laneFor = [&](double len) {
            return std::max(ww, (r.fanScalePct / 100.0 * len - ww) / double(wires - 1));
        };
        Vec2 ca, cb;
        double la = 0.0, lb = 0.0;
        slice(e[0], s.id, ca, la);
        const BrickFace fa = out.faces.value(e[0]);
        std::vector<Vec2> route;
        const double laneA = laneFor(la);
        double laneB = laneA;
        const double fanAuto = std::max(kWiresRunEnd, laneA * (wires - 1) / 2.0);
        const double lead = std::max({fanAuto, r.fanStartMm, r.fanFinishMm, 10.0}) +
                            std::max(0.0, r.kneeRadiusMm) + 5.0;
        if (e.size() == 2) {
            slice(e[1], s.id, cb, lb);
            laneB = laneFor(lb);
            route = routeBetweenFaces(ca, fa.out, cb, out.faces.value(e[1]).out, lead);
        } else {
            route = {ca, ca + fa.out * r.lengthMm};
        }
        if (route.size() < 2) continue;

        Entity run;
        run.type = EntityType::TwistedPair;
        run.layer = opt.layer;
        run.points = route;
        run.a = route.front();
        run.b = route.back();
        const bool laidUp = s.type == BrickType::TwistedPair || s.type == BrickType::TwistedBundle;
        run.meta.insert(QStringLiteral("twistWires"), s.type == BrickType::TwistedPair ? 2 : wires);
        run.meta.insert(QStringLiteral("twistStraight"), !laidUp);
        run.meta.insert(QStringLiteral("twistWireWidth"), ww);
        if (laidUp) {
            const double faceShare = e.size() == 2 ? std::min(la, lb) : la;
            run.meta.insert(QStringLiteral("twistWidth"), r.fanScalePct / 100.0 * faceShare);
            run.meta.insert(QStringLiteral("twistPitch"), kTwistedPairPitch);
            run.meta.insert(QStringLiteral("twistEnds"),
                            r.fanStartMm >= 0.0 ? r.fanStartMm : kTwistedPairLead);
        } else {
            run.meta.insert(QStringLiteral("twistSpread"),
                            e.size() == 2 ? QStringLiteral("both") : QStringLiteral("start"));
            run.meta.insert(QStringLiteral("twistSidesSame"), false);
            run.meta.insert(QStringLiteral("twistLane"), laneA);
            run.meta.insert(QStringLiteral("twistLaneFinish"), laneB);
            if (r.fanStartMm >= 0.0) run.meta.insert(QStringLiteral("twistEnds"), r.fanStartMm);
            if (r.fanFinishMm >= 0.0)
                run.meta.insert(QStringLiteral("twistEndsFinish"), r.fanFinishMm);
            if (r.fanKneeMm >= 0.0) {
                run.meta.insert(QStringLiteral("twistFanKnee"), r.fanKneeMm);
                run.meta.insert(QStringLiteral("twistFanKneeFinish"), r.fanKneeMm);
            }
            if (s.type == BrickType::Run && r.body != RunBody::Wires) {
                const double share = e.size() == 2 ? std::min(la, lb) : la;
                run.meta.insert(QStringLiteral("twistBody"), bodyKey(r.body));
                run.meta.insert(QStringLiteral("twistBodyWidth"),
                                r.bodyWidthMm > 1e-9 ? r.bodyWidthMm
                                                     : r.bodyScalePct / 100.0 * share);
                run.meta.insert(QStringLiteral("twistBodyTransition"), false);
            }
        }
        if (r.kneeRadiusMm >= 0.0) run.meta.insert(QStringLiteral("twistKnee"), r.kneeRadiusMm);
        std::vector<Entity> one{run};
        stampBrick(one, s.id);
        out.entities.push_back(one.front());
    }
    return out;
}

} // namespace Edraw_Desktop
