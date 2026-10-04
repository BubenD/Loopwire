#include "harness/Route3D.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

namespace {

constexpr double kPi = 3.14159265358979323846;

QJsonArray v3Json(const V3& v) { return QJsonArray{v.x, v.y, v.z}; }

V3 v3From(const QJsonValue& value, const V3& fallback = {}) {
    const QJsonArray a = value.toArray();
    if (a.size() != 3) return fallback;
    return {a.at(0).toDouble(), a.at(1).toDouble(), a.at(2).toDouble()};
}

V3 unitOr(const V3& v, const V3& fallback) {
    const V3 n = v.normalized();
    return n.length() > 0.5 ? n : fallback;
}

} // namespace

QString routeNodeKindName(RouteNodeKind kind) {
    switch (kind) {
    case RouteNodeKind::Connector: return QStringLiteral("connector");
    case RouteNodeKind::Clip: return QStringLiteral("clip");
    case RouteNodeKind::Branch: break;
    }
    return QStringLiteral("branch");
}

RouteNodeKind routeNodeKindFrom(const QString& name) {
    if (name == QLatin1String("connector")) return RouteNodeKind::Connector;
    if (name == QLatin1String("clip")) return RouteNodeKind::Clip;
    return RouteNodeKind::Branch;
}

const RouteNode3D* Route3D::node(const QString& id) const {
    for (const RouteNode3D& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

RouteNode3D* Route3D::node(const QString& id) {
    for (RouteNode3D& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

const RouteSegment3D* Route3D::segment(const QString& id) const {
    for (const RouteSegment3D& s : segments)
        if (s.id == id) return &s;
    return nullptr;
}

RouteSegment3D* Route3D::segment(const QString& id) {
    for (RouteSegment3D& s : segments)
        if (s.id == id) return &s;
    return nullptr;
}

QString Route3D::freshId(const QString& prefix) const {
    int most = 0;
    auto look = [&](const QString& id) {
        if (!id.startsWith(prefix)) return;
        bool ok = false;
        const int n = id.mid(prefix.size()).toInt(&ok);
        if (ok) most = std::max(most, n);
    };
    for (const RouteNode3D& n : nodes) look(n.id);
    for (const RouteSegment3D& s : segments) look(s.id);
    return prefix + QString::number(most + 1);
}

QString Route3D::addNode(RouteNodeKind kind, const V3& at, const V3& normal) {
    RouteNode3D n;
    n.id = freshId(QStringLiteral("n"));
    n.kind = kind;
    n.at = at;
    n.normal = unitOr(normal, {0.0, 0.0, 1.0});
    // LABELLED IN TURN by its kind, after the highest of that kind there is.
    const QString prefix = kind == RouteNodeKind::Connector ? QStringLiteral("X") : kind == RouteNodeKind::Clip ? QStringLiteral("C") : QStringLiteral("B");
    int most = 0;
    for (const RouteNode3D& o : nodes) {
        bool ok = false;
        const int k = o.label.startsWith(prefix) ? o.label.mid(prefix.size()).toInt(&ok) : 0;
        if (ok) most = std::max(most, k);
    }
    n.label = prefix + QString::number(most + 1);
    nodes.push_back(n);
    return n.id;
}

QString Route3D::addSegment(const QString& a, const QString& b, std::vector<RouteVia3D> via, double dia) {
    if (a == b || !node(a) || !node(b)) return QString();
    RouteSegment3D s;
    s.id = freshId(QStringLiteral("s"));
    s.a = a;
    s.b = b;
    s.via = std::move(via);
    for (RouteVia3D& v : s.via) v.normal = unitOr(v.normal, {0.0, 0.0, 1.0});
    s.diameter = dia;
    segments.push_back(std::move(s));
    return segments.back().id;
}

void Route3D::removeSegment(const QString& id) {
    segments.erase(std::remove_if(segments.begin(), segments.end(), [&](const RouteSegment3D& s) { return s.id == id; }),
                   segments.end());
}

void Route3D::removeNode(const QString& id) {
    segments.erase(std::remove_if(segments.begin(), segments.end(),
                                  [&](const RouteSegment3D& s) { return s.a == id || s.b == id; }),
                   segments.end());
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const RouteNode3D& n) { return n.id == id; }), nodes.end());
}

QString Route3D::splitSegment(const QString& id, const V3& at, const V3& normal, int afterVia, RouteNodeKind kind) {
    RouteSegment3D* s = segment(id);
    if (!s) return QString();
    const QString b = s->b;
    const double dia = s->diameter;
    afterVia = std::clamp(afterVia, 0, static_cast<int>(s->via.size()));
    std::vector<RouteVia3D> after(s->via.begin() + afterVia, s->via.end());
    s->via.resize(static_cast<size_t>(afterVia));
    const QString mid = addNode(kind, at, normal);
    // `s` may have moved: nodes and segments are separate lists, but take it again to be sure.
    s = segment(id);
    s->b = mid;
    addSegment(mid, b, std::move(after), dia);
    return mid;
}

int Route3D::degree(const QString& id) const {
    int n = 0;
    for (const RouteSegment3D& s : segments) n += (s.a == id ? 1 : 0) + (s.b == id ? 1 : 0);
    return n;
}

double Route3D::diameterOf(const RouteSegment3D& s) const { return s.diameter > 0.0 ? s.diameter : std::max(0.1, diameter); }

double Route3D::bendRadiusOf(const RouteSegment3D& s) const { return bendRadius > 0.0 ? bendRadius : 6.0 * diameterOf(s); }

std::vector<V3> Route3D::corners(const RouteSegment3D& s) const {
    std::vector<V3> pts;
    const RouteNode3D* a = node(s.a);
    const RouteNode3D* b = node(s.b);
    if (!a || !b) return pts;
    const double r = diameterOf(s) / 2.0;
    auto add = [&](const V3& p) {
        if (pts.empty() || (p - pts.back()).length() > 1e-9) pts.push_back(p);
    };
    if (a->kind == RouteNodeKind::Connector) {
        add(a->at);
        add(a->at + a->normal * std::max(lead, r));
    } else {
        add(a->at + a->normal * r);
    }
    for (const RouteVia3D& v : s.via) add(v.at + v.normal * r);
    if (b->kind == RouteNodeKind::Connector) {
        add(b->at + b->normal * std::max(lead, r));
        add(b->at);
    } else {
        add(b->at + b->normal * r);
    }
    return pts;
}

std::vector<V3> Route3D::centreline(const RouteSegment3D& s, int* tight) const {
    const std::vector<V3> p = corners(s);
    if (tight) *tight = 0;
    if (p.size() < 3) return p;
    const double bend = bendRadiusOf(s);
    std::vector<V3> out{p.front()};
    for (size_t i = 1; i + 1 < p.size(); ++i) {
        const V3 in = p[i] - p[i - 1], on = p[i + 1] - p[i];
        const double l1 = in.length(), l2 = on.length();
        const V3 d1 = in / l1, d2 = on / l2;
        const double turn = std::acos(std::clamp(dot3(d1, d2), -1.0, 1.0));
        if (turn < 1e-3 || turn > kPi - 1e-3) {
            out.push_back(p[i]);
            continue;
        }
        // AS WIDE AS THE BEND RADIUS, or as the straight runs either side leave room for - half of each, so the arcs
        // at its two ends never meet.
        const double half = std::tan(turn / 2.0);
        double t = bend * half;
        const double room = 0.5 * std::min(l1, l2);
        if (t > room + 1e-9) {
            t = room;
            if (tight) ++*tight;
        }
        const double radius = t / half;
        const V3 p1 = p[i] - d1 * t;
        const V3 n1 = (d2 - d1 * std::cos(turn)).normalized();
        const V3 centre = p1 + n1 * radius;
        const int steps = std::max(2, static_cast<int>(std::ceil(turn / (kPi / 18.0))));
        for (int k = 0; k <= steps; ++k) {
            const double phi = turn * k / steps;
            out.push_back(centre + (n1 * -std::cos(phi) + d1 * std::sin(phi)) * radius);
        }
    }
    out.push_back(p.back());
    return out;
}

double Route3D::length(const RouteSegment3D& s) const {
    const std::vector<V3> c = centreline(s);
    double l = 0.0;
    for (size_t i = 1; i < c.size(); ++i) l += (c[i] - c[i - 1]).length();
    return l;
}

QJsonObject Route3D::toJson() const {
    QJsonObject o;
    o[QStringLiteral("diameter")] = diameter;
    o[QStringLiteral("bendRadius")] = bendRadius;
    o[QStringLiteral("lead")] = lead;
    QJsonArray ns;
    for (const RouteNode3D& n : nodes) {
        QJsonObject j;
        j[QStringLiteral("id")] = n.id;
        j[QStringLiteral("kind")] = routeNodeKindName(n.kind);
        j[QStringLiteral("label")] = n.label;
        j[QStringLiteral("at")] = v3Json(n.at);
        j[QStringLiteral("normal")] = v3Json(n.normal);
        if (!n.brick.isEmpty()) {
            j[QStringLiteral("brick")] = n.brick;
            j[QStringLiteral("part")] = n.part;
        }
        ns.append(j);
    }
    o[QStringLiteral("nodes")] = ns;
    QJsonArray ss;
    for (const RouteSegment3D& s : segments) {
        QJsonObject j;
        j[QStringLiteral("id")] = s.id;
        j[QStringLiteral("a")] = s.a;
        j[QStringLiteral("b")] = s.b;
        if (s.diameter > 0.0) j[QStringLiteral("diameter")] = s.diameter;
        QJsonArray via;
        for (const RouteVia3D& v : s.via) via.append(QJsonObject{{QStringLiteral("at"), v3Json(v.at)}, {QStringLiteral("normal"), v3Json(v.normal)}});
        j[QStringLiteral("via")] = via;
        ss.append(j);
    }
    o[QStringLiteral("segments")] = ss;
    return o;
}

Route3D Route3D::fromJson(const QJsonObject& o) {
    Route3D r;
    r.diameter = o.value(QStringLiteral("diameter")).toDouble(8.0);
    r.bendRadius = o.value(QStringLiteral("bendRadius")).toDouble(0.0);
    r.lead = o.value(QStringLiteral("lead")).toDouble(10.0);
    for (const QJsonValue& v : o.value(QStringLiteral("nodes")).toArray()) {
        const QJsonObject j = v.toObject();
        RouteNode3D n;
        n.id = j.value(QStringLiteral("id")).toString();
        n.kind = routeNodeKindFrom(j.value(QStringLiteral("kind")).toString());
        n.label = j.value(QStringLiteral("label")).toString();
        n.at = v3From(j.value(QStringLiteral("at")));
        n.normal = unitOr(v3From(j.value(QStringLiteral("normal")), {0, 0, 1}), {0, 0, 1});
        n.brick = j.value(QStringLiteral("brick")).toString();
        n.part = j.value(QStringLiteral("part")).toString();
        if (!n.id.isEmpty()) r.nodes.push_back(n);
    }
    for (const QJsonValue& v : o.value(QStringLiteral("segments")).toArray()) {
        const QJsonObject j = v.toObject();
        RouteSegment3D s;
        s.id = j.value(QStringLiteral("id")).toString();
        s.a = j.value(QStringLiteral("a")).toString();
        s.b = j.value(QStringLiteral("b")).toString();
        s.diameter = j.value(QStringLiteral("diameter")).toDouble(0.0);
        for (const QJsonValue& w : j.value(QStringLiteral("via")).toArray()) {
            const QJsonObject k = w.toObject();
            s.via.push_back({v3From(k.value(QStringLiteral("at"))), unitOr(v3From(k.value(QStringLiteral("normal")), {0, 0, 1}), {0, 0, 1})});
        }
        // A bundle to a node that is not there is dropped, not kept dangling.
        if (!s.id.isEmpty() && r.node(s.a) && r.node(s.b) && s.a != s.b) r.segments.push_back(std::move(s));
    }
    return r;
}

} // namespace Edraw_Desktop
