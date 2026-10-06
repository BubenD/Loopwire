#include "harness/RouteLink.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace Edraw_Desktop {

namespace {

QString titleOf(const Brick* b) {
    if (!b) return QString();
    return b->name.isEmpty() ? brickTypeKey(b->type) : b->name;
}

} // namespace

QString routeNodeOfPart(const Route3D& route, const QString& brick) {
    if (brick.isEmpty()) return QString();
    for (const RouteNode3D& n : route.nodes)
        if (n.brick == brick) return n.id;
    return QString();
}

double routeDistance(const Route3D& route, const QString& from, const QString& to) {
    if (from.isEmpty() || to.isEmpty() || !route.node(from) || !route.node(to)) return -1.0;
    if (from == to) return 0.0;
    // DIJKSTRA over the bundles, each as long as its path.
    QHash<QString, double> best;
    using Item = std::pair<double, QString>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    best.insert(from, 0.0);
    open.push({0.0, from});
    while (!open.empty()) {
        const auto [d, at] = open.top();
        open.pop();
        if (d > best.value(at, std::numeric_limits<double>::max())) continue;
        if (at == to) return d;
        for (const RouteSegment3D& s : route.segments) {
            if (s.a != at && s.b != at) continue;
            const QString next = s.a == at ? s.b : s.a;
            const double nd = d + route.length(s);
            if (nd < best.value(next, std::numeric_limits<double>::max())) {
                best.insert(next, nd);
                open.push({nd, next});
            }
        }
    }
    return -1.0;
}

int addHarnessToRoute(Route3D& route, const BrickDesign& design, const std::function<bool(const QString& brick, V3& at)>& placeOf) {
    // EACH PART A NODE, where the harness draws it - in a row beyond it where it draws none - linked to the part.
    int unplaced = 0;
    for (const Brick& b : design.bricks) {
        if (b.type != BrickType::End || !routeNodeOfPart(route, b.id).isEmpty()) continue;
        V3 at;
        if (!placeOf || !placeOf(b.id, at)) at = {unplaced++ * 60.0, -150.0, 0.0};
        const QString id = route.addNode(b.kind == PartKind::Connector ? RouteNodeKind::Connector : RouteNodeKind::Branch, at, {0, 0, 1});
        if (RouteNode3D* n = route.node(id)) {
            n->brick = b.id;
            n->part = b.name.isEmpty() ? partKindName(b.kind) : b.name;
            n->label = n->part;
        }
    }
    // EACH CABLE between the parts it joins, unless the route joins them already.
    int laid = 0;
    for (const Brick& s : design.bricks) {
        if (!brickIsSpecial(s.type) || brickIsBranch(s)) continue;
        const QStringList starts = design.startsOf(s.id), finishes = design.finishesOf(s.id);
        if (starts.isEmpty() || finishes.isEmpty()) continue;
        QStringList a, b;
        for (const QString& p : starts) a << routeNodeOfPart(route, p);
        for (const QString& p : finishes) b << routeNodeOfPart(route, p);
        if (a.contains(QString()) || b.contains(QString())) continue;
        if (routeDistance(route, a.first(), b.first()) >= 0.0) continue;
        // SEVERAL PARTS AT AN END: a branch a quarter of the way from them toward the other end, a leg to each.
        auto middle = [&route](const QStringList& ids) {
            V3 c;
            for (const QString& id : ids) c = c + route.node(id)->at;
            return c * (1.0 / ids.size());
        };
        auto hub = [&](const QStringList& side, const QStringList& other) {
            if (side.size() == 1) return side.first();
            const V3 here = middle(side), there = middle(other);
            return route.addNode(RouteNodeKind::Branch, here + (there - here) * 0.25, {0, 0, 1});
        };
        const QString ha = hub(a, b), hb = hub(b, a);
        const QString trunk = route.addSegment(ha, hb, {});
        double legs = 0.0;
        for (const auto& [side, centre] : {std::pair<const QStringList*, QString>{&a, ha}, {&b, hb}}) {
            if (side->size() < 2) continue;
            double longest = 0.0;
            for (const QString& n : *side) {
                const QString leg = route.addSegment(centre, n, {});
                if (const RouteSegment3D* l = route.segment(leg)) longest = std::max(longest, route.length(*l));
            }
            legs += longest;
        }
        // AS LONG AS THE DESIGN SAYS, where that is longer than the way between them.
        if (const RouteSegment3D* t = route.segment(trunk); t && s.run.lengthMm - legs > route.length(*t) + 1.0)
            route.lengthen(trunk, s.run.lengthMm - legs);
        ++laid;
    }
    return laid;
}

RouteLengths routeLengthsFor(const BrickDesign& design, const Route3D& route) {
    RouteLengths out;
    for (const Brick& s : design.bricks) {
        if (!brickIsSpecial(s.type)) continue;
        const QStringList starts = design.startsOf(s.id), finishes = design.finishesOf(s.id);
        const QString name = titleOf(&s);
        if (brickIsBranch(s)) {
            out.notes << QStringLiteral("%1 branches off a cable's body - its length is set by hand").arg(name);
            continue;
        }
        if (starts.isEmpty() || finishes.isEmpty()) continue;
        // EVERY PART OF IT PUT IN 3D, or it is not measured.
        QStringList missing;
        for (const QString& p : starts + finishes)
            if (routeNodeOfPart(route, p).isEmpty()) missing << titleOf(design.brick(p));
        if (!missing.isEmpty()) {
            out.notes << QStringLiteral("%1: %2 not put in 3D").arg(name, missing.join(QStringLiteral(", ")));
            continue;
        }
        // EACH PART'S WAY to the nearest part at the other end.
        QHash<QString, double> own;
        QStringList unjoined;
        auto measure = [&](const QStringList& side, const QStringList& other) {
            for (const QString& p : side) {
                double nearest = -1.0;
                for (const QString& q : other) {
                    const double d = routeDistance(route, routeNodeOfPart(route, p), routeNodeOfPart(route, q));
                    if (d >= 0.0 && (nearest < 0.0 || d < nearest)) nearest = d;
                }
                if (nearest < 0.0)
                    unjoined << titleOf(design.brick(p));
                else
                    own.insert(p, std::round(nearest));
            }
        };
        measure(starts, finishes);
        measure(finishes, starts);
        if (!unjoined.isEmpty()) {
            out.notes << QStringLiteral("%1: no bundles join %2 to the other end").arg(name, unjoined.join(QStringLiteral(", ")));
            continue;
        }
        double length = 0.0;
        for (double v : own) length = std::max(length, v);
        if (length <= 0.0) continue;
        ++out.measured;
        const QString ends = QStringLiteral("%1 - %2").arg(titleOf(design.brick(starts.first())), titleOf(design.brick(finishes.first())));
        if (std::abs(length - s.run.lengthMm) >= 0.5)
            out.changes.push_back({s.id, QString(), QStringLiteral("%1 (%2)").arg(name, ends), s.run.lengthMm, length});
        // A BRANCH TO ONE OF SEVERAL PARTS AT AN END: its own length where it is shorter; locked to the cable's where
        // it is as long - and a lone part at an end always with the cable.
        for (const QStringList* side : {&starts, &finishes}) {
            for (const QString& p : *side) {
                const BrickLink* link = design.link(s.id, p);
                if (!link) continue;
                const double want = side->size() > 1 && std::abs(own.value(p) - length) >= 0.5 ? own.value(p) : 0.0;
                if (std::abs(want - link->lengthMm) < 0.5) continue;
                out.changes.push_back({s.id, p, QStringLiteral("%1 to %2").arg(name, titleOf(design.brick(p))),
                                       link->lengthMm > 0.0 ? link->lengthMm : s.run.lengthMm, want});
            }
        }
    }
    return out;
}

void applyRouteLengths(BrickDesign& design, const RouteLengths& lengths) {
    for (const RouteLengthChange& c : lengths.changes) {
        if (c.part.isEmpty()) {
            if (Brick* b = design.brick(c.special)) b->run.lengthMm = c.now;
        } else if (BrickLink* l = design.link(c.special, c.part)) {
            l->lengthMm = c.now;
        }
    }
}

} // namespace Edraw_Desktop
