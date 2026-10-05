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

bool Route3D::aimNode(const QString& id, const V3& dir, double spinDeg) {
    for (RouteNode3D& n : nodes) {
        if (n.id != id) continue;
        n.normal = dir.length() < 1e-9 ? n.face : unitOr(dir, n.face);
        n.spinDeg = spinDeg;
        return true;
    }
    return false;
}

QString Route3D::addNode(RouteNodeKind kind, const V3& at, const V3& normal) {
    RouteNode3D n;
    n.id = freshId(QStringLiteral("n"));
    n.kind = kind;
    n.at = at;
    n.normal = unitOr(normal, {0.0, 0.0, 1.0});
    n.face = n.normal;
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

bool Route3D::lengthen(const QString& id, double target) {
    RouteSegment3D* s = segment(id);
    if (!s) return false;
    const RouteNode3D* a = node(s->a);
    const RouteNode3D* b = node(s->b);
    if (!a || !b) return false;
    // MORE POINTS TO BEND AT: at least a dozen along it, where it was laid through fewer.
    if (s->via.size() < 12) {
        std::vector<RouteVia3D> pts{{a->at, a->normal}};
        pts.insert(pts.end(), s->via.begin(), s->via.end());
        pts.push_back({b->at, b->normal});
        std::vector<double> along{0.0};
        for (size_t i = 1; i < pts.size(); ++i) along.push_back(along.back() + (pts[i].at - pts[i - 1].at).length());
        const double total = along.back();
        if (total < 1e-9) return false;
        std::vector<RouteVia3D> more;
        size_t k = 1;
        for (int i = 1; i <= 12; ++i) {
            const double want = total * i / 13.0;
            while (k + 1 < pts.size() && along[k] < want) ++k;
            const double t = (want - along[k - 1]) / std::max(1e-12, along[k] - along[k - 1]);
            more.push_back({pts[k - 1].at + (pts[k].at - pts[k - 1].at) * t,
                            (pts[k - 1].normal * (1.0 - t) + pts[k].normal * t).normalized()});
        }
        s->via = more;
    }
    // BOWED OUT FROM WHAT IT LIES ON: started off a hair, the most at its middle, so it has a way to go.
    {
        const double span = (b->at - a->at).length();
        const int n = static_cast<int>(s->via.size());
        for (int i = 0; i < n; ++i) s->via[static_cast<size_t>(i)].at = s->via[static_cast<size_t>(i)].at +
                                                                         s->via[static_cast<size_t>(i)].normal * (span * 0.002 * std::sin(kPi * (i + 1.0) / (n + 1.0)));
    }
    std::vector<double> step;
    {
        V3 from = a->at;
        for (const RouteVia3D& v : s->via) {
            step.push_back((v.at - from).length());
            from = v.at;
        }
        step.push_back((b->at - from).length());
    }
    double steps = 0.0;
    for (double d : step) steps += d;
    auto toward = [](const V3& from, const V3& to, double d) {
        const V3 dir = to - from;
        return dir.length() < 1e-12 ? from : from + dir.normalized() * d;
    };
    // EACH STEP LONGER, BOTH ENDS HELD (FABRIK), until it is drawn as long as it is to be.
    double scale = 1.0, lastScale = -1.0, lastNow = 0.0, slope = steps;
    for (int fit = 0; fit < 20; ++fit) {
        std::vector<V3> p{a->at};
        for (const RouteVia3D& v : s->via) p.push_back(v.at);
        p.push_back(b->at);
        const size_t n = p.size() - 1;
        for (int iter = 0; iter < 16; ++iter) {
            p[0] = a->at;
            for (size_t i = 1; i <= n; ++i) p[i] = toward(p[i - 1], p[i], step[i - 1] * scale);
            p[n] = b->at;
            for (size_t i = n; i-- > 0;) p[i] = toward(p[i + 1], p[i], step[i] * scale);
        }
        for (size_t i = 0; i < s->via.size(); ++i) s->via[i].at = p[i + 1];
        const double now = length(*s);
        if (std::abs(now - target) < 0.05) return true;
        if (lastScale >= 0.0 && std::abs(scale - lastScale) > 1e-9) {
            const double measured = (now - lastNow) / (scale - lastScale);
            if (measured > steps * 0.05) slope = measured;
        }
        lastScale = scale;
        lastNow = now;
        scale = std::clamp(scale + (target - now) / slope, 0.2, 10.0);
    }
    return true;
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

std::vector<V3> Route3D::cornerNormals(const RouteSegment3D& s) const {
    std::vector<V3> out;
    const RouteNode3D* a = node(s.a);
    const RouteNode3D* b = node(s.b);
    if (!a || !b) return out;
    // As corners() makes them, the same points dropped where two fall together.
    const std::vector<V3> pts = corners(s);
    std::vector<V3> all;
    if (a->kind == RouteNodeKind::Connector) all.push_back(a->normal);
    all.push_back(a->normal);
    for (const RouteVia3D& v : s.via) all.push_back(v.normal);
    all.push_back(b->normal);
    if (b->kind == RouteNodeKind::Connector) all.push_back(b->normal);
    for (size_t i = 0; i < pts.size(); ++i) out.push_back(all[std::min(i, all.size() - 1)]);
    return out;
}

std::vector<V3> Route3D::overSolids(const std::vector<V3>& p, const std::vector<V3>& nrm, double r) const {
    if (!solids || p.size() < 2) return p;
    std::vector<V3> out{p.front()};
    for (size_t i = 1; i < p.size(); ++i) {
        const V3 a = p[i - 1], b = p[i];
        const V3 d = b - a;
        const double len = d.length();
        if (len < 1e-6) continue;
        const V3 u = d / len;
        // CLEAR OF WHAT IT STARTS AND ENDS ON: a connector's own face, the face a point was laid on.
        const double eps = std::min(len * 0.25, std::max(0.5, r));
        V3 hit;
        if (len <= 2.0 * eps || !solids(a + u * eps, u, len - 2.0 * eps, hit)) {
            out.push_back(b);
            continue;
        }
        // THROUGH SOMETHING: laid over it - each point along it lifted, the way the faces at its ends look, to the top
        // of what is under it and its radius more.
        V3 up = i < nrm.size() ? nrm[i - 1] + nrm[i] : V3{0, 0, 1};
        if (up.length() < 1e-6) up = {0, 0, 1};
        up = up.normalized();
        if (std::abs(dot3(u, up)) > 0.95) {
            out.push_back(b);   // running that way itself: nothing to be laid over
            continue;
        }
        const double H = std::clamp(len, 20.0, 2000.0);
        const double step = std::max(r, len / 160.0);
        const int n = std::max(2, static_cast<int>(std::ceil(len / step)));
        std::vector<V3> run;
        std::vector<char> lifted;
        for (int k = 1; k < n; ++k) {
            const V3 x = a + d * (static_cast<double>(k) / n);
            V3 q = x, top;
            if (solids(x + up * H, up * -1.0, H + r, top)) {
                const double rise = dot3(top + up * r - x, up);
                if (rise > 1e-6) q = x + up * rise;
            }
            run.push_back(q);
            lifted.push_back((q - x).length() > 1e-6);
        }
        // Only where it is lifted, and a point either side - the rest of it straight as it was.
        for (size_t k = 0; k < run.size(); ++k) {
            const bool keep = lifted[k] || (k + 1 < run.size() && lifted[k + 1]) || (k > 0 && lifted[k - 1]);
            if (keep) out.push_back(run[k]);
        }
        out.push_back(b);
    }
    return out;
}

std::vector<V3> Route3D::centreline(const RouteSegment3D& s, int* tight) const {
    const std::vector<V3> p = solids ? overSolids(corners(s), cornerNormals(s), diameterOf(s) / 2.0) : corners(s);
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
        // Turned from its face, and round its axis - written only where it has been.
        if ((n.face - n.normal).length() > 1e-9) j[QStringLiteral("face")] = v3Json(n.face);
        if (std::abs(n.spinDeg) > 1e-9) j[QStringLiteral("spin")] = n.spinDeg;
        if (!n.brick.isEmpty()) {
            j[QStringLiteral("brick")] = n.brick;
            j[QStringLiteral("part")] = n.part;
        }
        if (!n.form.isEmpty() && n.form != QLatin1String("rect")) j[QStringLiteral("form")] = n.form;
        if (n.formW > 0.0) j[QStringLiteral("fw")] = n.formW;
        if (n.formH > 0.0) j[QStringLiteral("fh")] = n.formH;
        if (n.formL > 0.0) j[QStringLiteral("fl")] = n.formL;
        if (n.color.isValid()) j[QStringLiteral("color")] = n.color.name();
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
        if (s.color.isValid()) j[QStringLiteral("color")] = s.color.name();
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
        n.face = j.contains(QStringLiteral("face")) ? unitOr(v3From(j.value(QStringLiteral("face")), n.normal), n.normal) : n.normal;
        n.spinDeg = j.value(QStringLiteral("spin")).toDouble(0.0);
        n.brick = j.value(QStringLiteral("brick")).toString();
        n.part = j.value(QStringLiteral("part")).toString();
        n.form = j.value(QStringLiteral("form")).toString();
        n.formW = j.value(QStringLiteral("fw")).toDouble(0.0);
        n.formH = j.value(QStringLiteral("fh")).toDouble(0.0);
        n.formL = j.value(QStringLiteral("fl")).toDouble(0.0);
        if (j.contains(QStringLiteral("color"))) n.color = QColor(j.value(QStringLiteral("color")).toString());
        if (!n.id.isEmpty()) r.nodes.push_back(n);
    }
    for (const QJsonValue& v : o.value(QStringLiteral("segments")).toArray()) {
        const QJsonObject j = v.toObject();
        RouteSegment3D s;
        s.id = j.value(QStringLiteral("id")).toString();
        s.a = j.value(QStringLiteral("a")).toString();
        s.b = j.value(QStringLiteral("b")).toString();
        s.diameter = j.value(QStringLiteral("diameter")).toDouble(0.0);
        if (j.contains(QStringLiteral("color"))) s.color = QColor(j.value(QStringLiteral("color")).toString());
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
