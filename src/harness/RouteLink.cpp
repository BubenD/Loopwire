#include "harness/RouteLink.h"
#include "harness/Harness3D.h"
#include "harness/Harness.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace Loopwire {

namespace {

QString titleOf(const Brick* b) {
    if (!b) return QString();
    return b->name.isEmpty() ? brickTypeKey(b->type) : b->name;
}

void constructionFromDrawing(RouteSegment3D& segment,const Entity& entity) {
    for(const char* key:{"crossSectionMm2","areaMm2"}) {
        const auto value=entity.meta.value(QLatin1String(key));
        const double area=value.isDouble() ? value.toDouble() : value.toString().toDouble();
        if(area>0)segment.conductorAreaMm2=area;
    }
    const auto awg=entity.meta.value(QStringLiteral("awg"));
    if(!awg.isUndefined()) {
        bool ok=false; const double gauge=awg.isDouble() ? awg.toDouble() : awg.toString().toDouble(&ok);
        if(awg.isDouble() || ok) {
            const double d=0.127*std::pow(92.0,(36.0-gauge)/39.0);
            segment.conductorAreaMm2=3.14159265358979323846*d*d/4.0;
        }
    }
    if(entity.meta.contains(QStringLiteral("minimumBendRadius")))
        segment.minimumBendRadius=entity.meta.value(QStringLiteral("minimumBendRadius")).toDouble();
    if(entity.meta.value(QStringLiteral("twistBodyOverCable")).toBool())segment.jacket=true;
}

} // namespace

QString routeNodeOfPart(const Route3D& route, const QString& brick) {
    if (brick.isEmpty()) return QString();
    for (const RouteNode3D& n : route.nodes)
        if (n.brick == brick) return n.id;
    return QString();
}

DrawingHarnessSource mechanicalDrawingSource(const std::vector<Entity>& entities) {
    DrawingHarnessSource source;
    QHash<QString, Bounds> parts;
    QHash<QString, QString> names;
    for (const Entity& e : entities) {
        const QString key = harness3DPartKey(e);
        if (key.isEmpty() || e.type == EntityType::Text || e.type == EntityType::Dimension ||
            e.type == EntityType::Wipeout || e.type == EntityType::TwistedPair) continue;
        parts[key].expand(e.bounds());
        if (names.value(key).isEmpty()) names[key] = e.meta.value(QStringLiteral("partName")).toString(
            e.meta.value(QStringLiteral("block")).toString(e.meta.value(QStringLiteral("designator")).toString()));
    }
    auto end = [&](const Vec2& at) {
        QString key;
        double best = 1.0;
        for (auto it = parts.cbegin(); it != parts.cend(); ++it) {
            const Bounds& b = it.value();
            if (!b.valid) continue;
            const double gap = std::hypot(std::max({b.min.x - at.x, 0.0, at.x - b.max.x}),
                                          std::max({b.min.y - at.y, 0.0, at.y - b.max.y}));
            if (gap < best || (gap == best && it.key() < key)) { best = gap; key = it.key(); }
        }
        if (key.isEmpty()) key = QStringLiteral("drawing:end:%1:%2").arg(at.x, 0, 'f', 6).arg(at.y, 0, 'f', 6);
        if (!source.design.brick(key)) {
            const QString added = source.design.addBrick(BrickType::End, PartKind::Connector, 0, 0);
            Brick* part = source.design.brick(added);
            part->id = key;
            part->name = names.value(key, QStringLiteral("End %1").arg(source.positions.size() + 1));
            source.positions.insert(key, at);
        }
        return key;
    };
    for (const Entity& e : entities) {
        const bool pair = e.type == EntityType::TwistedPair;
        const bool simple = e.type == EntityType::Wire || e.type == EntityType::DoubleLine ||
            (e.type == EntityType::Polyline && e.meta.value(QStringLiteral("double")).toBool());
        if ((!pair && !simple) || (!pair && !harness3DPartKey(e).isEmpty())) continue;
        const std::vector<Vec2> path = e.points.size() >= 2 ? e.points : std::vector<Vec2>{e.a, e.b};
        double length = 0.0;
        for (size_t i = 1; i < path.size(); ++i) length += distance(path[i - 1], path[i]);
        if (length <= 1e-6) continue;
        const QString a = end(path.front()), b = end(path.back());
        if (a == b) continue;
        const QString added = source.design.addBrick(BrickType::Run, PartKind::Wire, 1, 0);
        Brick* run = source.design.brick(added);
        run->id = QStringLiteral("drawing:run:") + e.id;
        // addBrick(Run) starts with a cable jacket. The drawing owns its body type.
        run->run.stack = {QStringLiteral("wires")};
        const QString id = run->id;
        run->name = QStringLiteral("Drawn cable %1").arg(source.paths.size() + 1);
        run->run.lengthMm = length;
        run->run.wires = pair ? e.meta.value(QStringLiteral("twistWires")).toInt(2) : 1;
        run->run.wireWidthMm = e.meta.value(QStringLiteral("gap")).toDouble(1.0);
        if (pair) {
            const TwistFigures f = twistedPairFigures(e);
            run->run.wireWidthMm = f.wireWidth;
            run->run.bodyWidthMm = f.bodyWidth;
            run->run.fanStartMm = f.spreadStart ? f.fanLen : 0.0;
            run->run.fanFinishMm = f.spreadFinish ? f.fanLenFinish : 0.0;
            const QString body = e.meta.value(QStringLiteral("twistBody")).toString();
            // A BRAID OVER IT is drawn as a cable is, and routed as a braid.
            if (!body.isEmpty() && body != QLatin1String("none"))
                run->run.stack << (e.meta.value(QStringLiteral("twistBodyBraid")).toBool() ? QStringLiteral("braid") : body);
        } else if (e.meta.value(QStringLiteral("shrunkEnds")).toBool()) {
            run->run.stack << (sleeveIsBraid(e) ? QStringLiteral("braid") : QStringLiteral("heatshrink"));
            run->run.bodyWidthMm = e.meta.value(QStringLiteral("shrinkWidth")).toDouble(run->run.wireWidthMm);
        }
        source.design.connect(a, id, nullptr, 0, kPortLeft);
        source.design.connect(id, b, nullptr, kPortRight, 0);
        source.paths.insert(id, path);
    }
    return source;
}

int addDrawingToRoute(Route3D& route, const std::vector<Entity>& entities, bool fixedLengths, bool labelsOnly) {
    const Route3D before = route;
    const DrawingHarnessSource source = mechanicalDrawingSource(entities);
    int laid = 0;
    for (const Brick& run : source.design.bricks) {
        if (!brickIsSpecial(run.type)) continue;
        // Refresh older saved routes too, without changing their routed path or cut length.
        for (auto& segment : route.segments) if (segment.designCable == run.id) {
            for(const auto& entity:entities) if(QStringLiteral("drawing:run:")+entity.id==run.id)constructionFromDrawing(segment,entity);
            segment.covering = run.run.stack.size() > 1 ? run.run.stack.last() : QString();
            segment.coverStart = std::max(0.0, run.run.fanStartMm);
            segment.coverFinish = std::max(0.0, run.run.fanFinishMm);
            segment.wires = run.run.wires;
            segment.wireD = run.run.wireWidthMm;
        }
        if (labelsOnly) continue;
        const QStringList starts = source.design.startsOf(run.id), finishes = source.design.finishesOf(run.id);
        if (starts.isEmpty() || finishes.isEmpty()) continue;
        bool exists = false;
        for (const RouteSegment3D& s : route.segments) exists |= s.designCable == run.id;
        if (exists) continue;
        const std::vector<Vec2> path = source.paths.value(run.id);
        auto nodeFor = [&](const QString& part, bool finish) {
            QString id = routeNodeOfPart(route, part);
            if (!id.isEmpty()) return id;
            const Vec2 at = finish ? path.back() : path.front();
            const Vec2 dir = finish ? path[path.size() - 2] - at : path[1] - at;
            id = route.addNode(RouteNodeKind::Connector, {at.x, at.y, 0}, {dir.x, dir.y, 0});
            RouteNode3D* n = route.node(id);
            n->brick = part;
            n->part = source.design.brick(part)->name;
            n->label = n->part;
            return id;
        };
        const QString a = nodeFor(starts.first(), false), b = nodeFor(finishes.first(), true);
        std::vector<RouteVia3D> via;
        for (size_t i = 1; i + 1 < path.size(); ++i) via.push_back({{path[i].x, path[i].y, 0}, {0, 0, 1}});
        const double diameter = run.run.bodyWidthMm > 0.0 && run.run.stack.size() > 1 ? run.run.bodyWidthMm
            : Route3D::packedDiameter(run.run.wires, run.run.wireWidthMm);
        const QString id = route.addSegment(a, b, via, diameter);
        RouteSegment3D* s = route.segment(id);
        if (!s) continue;
        s->designCable = run.id;
        for(const auto& entity:entities) if(QStringLiteral("drawing:run:")+entity.id==run.id)constructionFromDrawing(*s,entity);
        s->wires = run.run.wires;
        s->wireD = run.run.wireWidthMm;
        s->covering = run.run.stack.size() > 1 ? run.run.stack.last() : QString();
        s->coverStart = std::max(0.0, run.run.fanStartMm);
        s->coverFinish = std::max(0.0, run.run.fanFinishMm);
        s->designLength = run.run.lengthMm;
        if (fixedLengths) {
            s->fixedLength = run.run.lengthMm;
            if (!route.fitFixedLengths()) { route = before; return -2; }
        }
        ++laid;
    }
    // Labels placed on a freestyle cable are attached by their distance along its original path.
    for (const auto& mask : entities) {
        if (!mask.meta.value(QStringLiteral("labelMask")).toBool() || mask.points.size() < 4) continue;
        const Vec2 centre = (mask.points[0] + mask.points[2]) * 0.5;
        double nearest = 1e300, bestAlong = 0.0, bestTotal = 0.0;
        QString runId;
        for (auto it = source.paths.cbegin(); it != source.paths.cend(); ++it) {
            double along = 0.0, total = 0.0;
            const auto& path = it.value();
            for (size_t i = 1; i < path.size(); ++i) total += distance(path[i-1], path[i]);
            for (size_t i = 1; i < path.size(); ++i) {
                const Vec2 d = path[i] - path[i-1];
                const double len = d.length();
                if (len < 1e-9) continue;
                const double f = std::clamp(dot(centre - path[i-1], d) / (len * len), 0.0, 1.0);
                const double gap = distance(centre, path[i-1] + d * f);
                if (gap < nearest) { nearest = gap; bestAlong = along + len * f; bestTotal = total; runId = it.key(); }
                along += len;
            }
        }
        for (auto& segment : route.segments) {
            if (segment.designCable != runId || nearest > std::max(20.0, route.diameterOf(segment) * 2.0)) continue;
            const QString group = mask.meta.value(QStringLiteral("labelGroup")).toString();
            const QString labelId = group.isEmpty() ? mask.id : group;
            // A routed cable may have been split at a branch. Update the segment already carrying
            // this label instead of adding a second copy to the first piece of that cable.
            const auto carrier = std::find_if(route.segments.begin(), route.segments.end(), [&](const RouteSegment3D& piece) {
                return piece.designCable == runId && std::any_of(piece.labels.begin(), piece.labels.end(),
                    [&](const RouteLabel3D& saved) { return saved.id == labelId; });
            });
            if (carrier != route.segments.end() && &segment != &*carrier) continue;
            RouteLabel3D label;
            label.id = labelId;
            label.brick = harness3DPartKey(mask);
            if (label.brick.isEmpty()) label.brick = QStringLiteral("drawing:special:") + mask.id;
            label.position = bestAlong / std::max(1e-9, bestTotal);
            label.length = mask.meta.value(QStringLiteral("labelAlong")).toDouble(distance(mask.points[0], mask.points[1]));
            label.flexible = mask.meta.value(QStringLiteral("labelFlexible")).toBool(true);
            label.stiffness = mask.meta.value(QStringLiteral("labelStiffness")).toDouble(1.0);
            label.text = mask.meta.value(QStringLiteral("labelCaption")).toString();
            label.clearShrink = mask.meta.value(QStringLiteral("labelClearShrink")).toBool();
            label.textAlwaysVisible = mask.meta.value(QStringLiteral("labelTextAlwaysVisible")).toBool(true);
            label.wire = mask.meta.value(QStringLiteral("labelWire")).toInt(-1);
            int sleeveMasks = 0;
            for (const auto& e : entities) {
                const bool same = !group.isEmpty() && e.meta.value(QStringLiteral("labelGroup")).toString() == group;
                if (!same) continue;
                if (e.type == EntityType::Wipeout) ++sleeveMasks;
                if (e.type == EntityType::Text) {
                    label.text = e.meta.value(QStringLiteral("text")).toString();
                    label.textHeight = std::max(0.0, e.meta.value(QStringLiteral("height")).toDouble());
                }
                if (e.meta.value(QStringLiteral("linetype")).toString() == QLatin1String("HIDDEN")) label.clearShrink = true;
            }
            // Older drawings did not mark the clear sleeve explicitly; its second mask identifies it.
            if (sleeveMasks > 1) label.clearShrink = true;
            auto old = std::find_if(segment.labels.begin(), segment.labels.end(),
                                    [&](const RouteLabel3D& saved) { return saved.id == labelId; });
            if (old == segment.labels.end()) segment.labels.push_back(label);
            else {
                label.color = old->color;
                label.textColor = old->textColor;
                if (old->textVisibilityOverride) {
                    label.textAlwaysVisible = old->textAlwaysVisible;
                    label.textVisibilityOverride = true;
                }
                if (carrier != route.segments.end() &&
                    std::count_if(route.segments.begin(), route.segments.end(), [&](const RouteSegment3D& piece) {
                        return piece.designCable == runId;
                    }) > 1) label.position = old->position;
                if (!mask.meta.contains(QStringLiteral("labelFlexible"))) label.flexible = old->flexible;
                if (!mask.meta.contains(QStringLiteral("labelStiffness"))) label.stiffness = old->stiffness;
                if(old->stiffnessOverride){label.flexible=old->flexible;label.stiffness=old->stiffness;label.stiffnessOverride=true;}
                *old = label;
            }
            break;
        }
    }
    return laid;
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

int addHarnessToRoute(Route3D& route, const BrickDesign& design, const std::function<bool(const QString& brick, V3& at)>& placeOf,
                      bool fixedLengths) {
    const Route3D before = route;
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
        const double coverDiameter = s.run.stack.size() > 1 ? (s.run.bodyWidthMm > 0.0 ? s.run.bodyWidthMm
            : Route3D::packedDiameter(s.run.wires, s.run.wireWidthMm)) : 0.0;
        const QString trunk = route.addSegment(ha, hb, {}, coverDiameter);
        double legs = 0.0;
        struct Leg { QString id; double length; };
        std::vector<Leg> fixedLegs;
        for (const auto& [side, centre] : {std::pair<const QStringList*, QString>{&a, ha}, {&b, hb}}) {
            if (side->size() < 2) continue;
            double longest = 0.0;
            std::vector<Leg> sideLegs;
            for (const QString& n : *side) {
                const QString leg = route.addSegment(centre, n, {}, coverDiameter);
                const RouteSegment3D* l = route.segment(leg);
                if (!l) continue;
                const RouteNode3D* end = route.node(n);
                const BrickLink* link = end ? design.link(s.id, end->brick) : nullptr;
                const double own = link && link->lengthMm > 0.0 ? link->lengthMm : s.run.lengthMm;
                const double difference = s.run.lengthMm - own;
                longest = std::max(longest, route.length(*l) + (fixedLengths ? difference : 0.0));
                sideLegs.push_back({leg, difference});
            }
            for (const Leg& leg : sideLegs) fixedLegs.push_back({leg.id, longest - leg.length});
            legs += longest;
        }
        // ITS CONSTRUCTION FIRST - its wires, what covers it, how thick: they set how it bends, and so how long it is
        // drawn; fitted to its length before it had them, it would be drawn shorter or longer once it did.
        for (const QString& id : [&] {
            QStringList ids{trunk};
            for (const Leg& leg : fixedLegs) if (!ids.contains(leg.id)) ids << leg.id;
            return ids;
        }()) {
            if (RouteSegment3D* segment = route.segment(id)) {
                segment->designCable = s.id;
                if (s.run.stack.size() > 1) segment->covering = s.run.stack.last();
                segment->braidedShield=s.run.stack.contains(QStringLiteral("braid"));
                segment->heatshrink=s.run.stack.contains(QStringLiteral("heatshrink"));
                segment->jacket=s.run.stack.contains(QStringLiteral("cable"));
                segment->wires = s.run.wires;
                segment->wireD = s.run.wireWidthMm;
                segment->coverStart = std::max(0.0, s.run.fanStartMm);
                segment->coverFinish = std::max(0.0, s.run.fanFinishMm);
                if (!segment->covering.isEmpty()) segment->diameter = s.run.bodyWidthMm > 0.0 ? s.run.bodyWidthMm
                    : Route3D::packedDiameter(s.run.wires, s.run.wireWidthMm);
            }
        }
        // AS LONG AS THE DESIGN SAYS, where that is longer than the way between them.
        if (const RouteSegment3D* t = route.segment(trunk); t && s.run.lengthMm - legs > route.length(*t) + 1.0)
            route.lengthen(trunk, s.run.lengthMm - legs);
        if (fixedLengths) {
            if (s.run.lengthMm <= legs || !route.segment(trunk)) { route = before; return -2; }
            fixedLegs.push_back({trunk, s.run.lengthMm - legs});
            for (const Leg& leg : fixedLegs) {
                RouteSegment3D* segment = route.segment(leg.id);
                if (!segment) continue;
                segment->designCable = s.id;
                segment->fixedLength = leg.length;
            }
            if (!route.fitFixedLengths()) { route = before; return -2; }
        }
        // Labels defined by the wizard remain on their cable, with diameter derived at draw time.
        if (auto* segment = route.segment(trunk)) {
            segment->designLength = segment->fixedLength > 0.0 ? segment->fixedLength : s.run.lengthMm;
            for (size_t i = 0; i < s.run.labels.size(); ++i) {
                const auto& from = s.run.labels[i];
                RouteLabel3D label;
                label.id = s.id + QStringLiteral(":label:%1").arg(i);
                label.text = from.text; label.wire = from.wire; label.clearShrink = from.clearShrink;
                if (label.text.isEmpty() && from.wire >= 0 && from.wire < s.run.nets.size()) label.text = s.run.nets[from.wire];
                const double span = from.wire >= 0 ? route.wireDOf(*segment) : route.diameterOf(*segment);
                const double across = span + 2.0 * std::max(1.0, span * 0.12);
                label.length = from.lengthMm > 0.0 ? from.lengthMm
                    : from.wire >= 0 ? std::max(8.0, across * 1.8) : std::max(18.0, across * 1.3);
                const double fraction = from.distanceMm / std::max(1.0, s.run.lengthMm);
                label.position = std::clamp(from.fromFinish ? 1.0 - fraction : fraction, 0.0, 1.0);
                segment->labels.push_back(label);
            }
        }
        for (auto& segment : route.segments)
            if (segment.designCable == s.id && segment.designLength <= 0.0)
                segment.designLength = segment.fixedLength > 0.0 ? segment.fixedLength : route.length(segment);
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
        const bool fixed = std::any_of(route.segments.begin(), route.segments.end(), [&](const RouteSegment3D& segment) {
            return segment.designCable == s.id && segment.fixedLength > 0.0;
        });
        if (fixed) {
            out.notes << QStringLiteral("%1: fixed designed length is kept").arg(name);
            continue;
        }
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

} // namespace Loopwire
