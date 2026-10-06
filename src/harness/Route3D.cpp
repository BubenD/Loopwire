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

double Route3D::diameterOf(const RouteSegment3D& s) const {
    if (s.diameter > 0.0) return s.diameter;
    if (const int n = wiresOf(s); n > 0) return packedDiameter(n, wireDOf(s));
    return std::max(0.1, diameter);
}

int Route3D::wiresOf(const RouteSegment3D& s) const {
    if (s.wires > 0) return s.wires;
    return s.diameter > 0.0 ? 0 : std::max(0, wires);
}

double Route3D::wireDOf(const RouteSegment3D& s) const { return s.wireD > 0.0 ? s.wireD : std::max(0.05, wireD); }

double Route3D::packedDiameter(int n, double d) {
    if (n <= 1) return d;
    // Round, about three quarters full; two or three wires side by side at the least.
    return std::max(std::sqrt(n / 0.75) * d, n <= 3 ? 2.0 * d : 0.0);
}

std::vector<std::pair<double, double>> Route3D::wireLayout(int n, double d) {
    std::vector<std::pair<double, double>> out;
    if (n <= 0) return out;
    if (n == 1) return {{0.0, 0.0}};
    // RING BY RING ROUND THE MIDDLE: one in the middle where that packs better, then as many to a ring as fit round it -
    // each ring a wire's width further out.
    const bool middle = n >= 7 || n == 1;
    if (middle) out.push_back({0.0, 0.0});
    double radius = middle ? d : (n <= 3 ? d / (2.0 * std::sin(kPi / n)) : d * 0.8);
    while (static_cast<int>(out.size()) < n) {
        const int fit = std::max(3, static_cast<int>(std::floor(2.0 * kPi * radius / d)));
        const int here = std::min(fit, n - static_cast<int>(out.size()));
        for (int k = 0; k < here; ++k) {
            const double a = 2.0 * kPi * k / here;
            out.push_back({radius * std::cos(a), radius * std::sin(a)});
        }
        radius += d * 0.87;
    }
    return out;
}

double Route3D::bendRadiusOf(const RouteSegment3D& s) const { return bendRadius > 0.0 ? bendRadius : 6.0 * diameterOf(s); }

double Route3D::housingLength(const RouteNode3D& n) const {
    return n.formL > 0.0 ? n.formL : std::max(0.5 * std::max(0.0, lead), 1.0);
}

double Route3D::bodyLength(const RouteNode3D& n) const { return n.back ? 0.6 * housingLength(n) : housingLength(n); }

V3 Route3D::exitOf(const RouteNode3D& n) const { return n.back ? n.normal * -1.0 : n.normal; }

double Route3D::leadOf(const RouteNode3D& n, double radius) const {
    return std::max({lead, radius, bodyLength(n) + radius});
}

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
        add(a->at + exitOf(*a) * leadOf(*a, r));
    } else {
        add(a->at + a->normal * r);
    }
    for (const RouteVia3D& v : s.via) add(v.at + v.normal * r);
    if (b->kind == RouteNodeKind::Connector) {
        add(b->at + exitOf(*b) * leadOf(*b, r));
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

bool Route3D::inside(const V3& p) const {
    if (!insideSolid) return false;
    const std::string key = QString::asprintf("%.2f %.2f %.2f", p.x, p.y, p.z).toStdString();
    if (const auto f = insideCache_.find(key); f != insideCache_.end()) return f->second;
    if (insideCache_.size() > 20000) insideCache_.clear();
    return insideCache_[key] = insideSolid(p);
}

std::vector<V3> Route3D::overSolids(const std::vector<V3>& pIn, const std::vector<V3>& nrm, double r, bool freeFirst, bool freeLast) const {
    if (!solids || pIn.size() < 2) return pIn;
    // A POINT IT RUNS THROUGH INSIDE A SOLID - pulled there, or laid on a face since moved: pushed back out the way its face
    // looks, its radius clear. Its ends (the nodes, a connector's lead) where they are.
    std::vector<V3> p = pIn;
    if (insideSolid && p.size() > 2) {
        const size_t first = freeFirst ? 2 : 1;
        const size_t last = p.size() - 1 - (freeLast ? 2 : 1);
        for (size_t k = first; k <= last && k < p.size(); ++k) {
            if (!inside(p[k])) continue;
            V3 up = k < nrm.size() ? nrm[k] : V3{0, 0, 1};
            if (up.length() < 1e-6) up = {0, 0, 1};
            up = up.normalized();
            const double step = std::max(0.5, r);
            for (double h = step; h <= 400.0; h += step)
                if (!inside(p[k] + up * h)) {
                    // Out: and its radius more, off the face it came out of.
                    p[k] = p[k] + up * (h + r);
                    break;
                }
        }
    }
    std::vector<V3> out{p.front()};
    for (size_t i = 1; i < p.size(); ++i) {
        const V3 a = p[i - 1], b = p[i];
        if ((b - a).length() < 1e-6) continue;
        if ((i == 1 && freeFirst) || (i + 1 == p.size() && freeLast)) {
            out.push_back(b);
            continue;
        }
        const V3 na = i - 1 < nrm.size() ? nrm[i - 1] : V3{0, 0, 1};
        const V3 nb = i < nrm.size() ? nrm[i] : V3{0, 0, 1};
        const std::vector<V3> over = overOne(a, b, na, nb, r);
        out.insert(out.end(), over.begin(), over.end());
    }
    return out;
}

std::vector<V3> Route3D::overOne(const V3& a, const V3& b, const V3& na, const V3& nb, double r) const {
    const V3 d = b - a;
    const double len = d.length();
    const V3 u = d / len;
    // CLEAR OF WHAT IT STARTS AND ENDS ON: a connector's own face, the face a point was laid on.
    const double eps = std::min(len * 0.25, std::max(0.5, r));
    auto through = [&](const V3& x, const V3& y, double ex, double ey) {
        const V3 e = y - x;
        const double l = e.length();
        if (l <= ex + ey + 1e-6) return false;
        const V3 w = e / l;
        V3 hit;
        return solids(x + w * ex, w, l - ex - ey, hit);
    };
    if (len <= 2.0 * eps || !through(a, b, eps, eps)) return {b};
    const std::string key = QString::asprintf("%.4f %.4f %.4f %.4f %.4f %.4f %.4f %.3f %.3f %.3f %.3f %.3f %.3f", a.x, a.y, a.z, b.x,
                                              b.y, b.z, r, na.x, na.y, na.z, nb.x, nb.y, nb.z)
                                .toStdString();
    if (const auto f = overCache_.find(key); f != overCache_.end()) return f->second;
    // A PATH CLEAR all the way: each stretch of it past nothing, its first and last clear of the faces they start on.
    auto clear = [&](const std::vector<V3>& path) {
        V3 from = a;
        for (size_t k = 0; k < path.size(); ++k) {
            if (through(from, path[k], k == 0 ? eps : 0.02, k + 1 == path.size() ? eps : 0.02)) return false;
            from = path[k];
        }
        return true;
    };
    std::vector<V3> laid{b};
    // THROUGH SOMETHING LOW: laid over it - each point along it lifted, the way the faces at its ends look, to the top
    // of what is under it and its radius more.
    V3 up = na + nb;
    if (up.length() < 1e-6) up = {0, 0, 1};
    up = up.normalized();
    bool done = false;
    if (std::abs(dot3(u, up)) <= 0.95) {
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
        auto keepOf = [&] {
            std::vector<V3> kept;
            for (size_t k = 0; k < run.size(); ++k) {
                const bool keep = lifted[k] || (k + 1 < run.size() && lifted[k + 1]) || (k > 0 && lifted[k - 1]);
                if (keep) kept.push_back(run[k]);
            }
            kept.push_back(b);
            return kept;
        };
        laid = keepOf();
        done = clear(laid);
        // CLIPPING AN EDGE on the way up or down: the point before it raised as high as the one on top, so it rises
        // clear of the edge and not across it.
        if (!done) {
            const std::vector<V3> was = run;
            for (size_t k = 0; k < run.size(); ++k) {
                if (lifted[k]) continue;
                double rise = 0.0;
                for (size_t o : {k - 1, k + 1})
                    if (o < run.size() && lifted[o]) rise = std::max(rise, dot3(was[o] - run[k], up));
                if (rise > 1e-6) run[k] = run[k] + up * rise;
            }
            const std::vector<V3> raised = keepOf();
            if (clear(raised)) {
                laid = raised;
                done = true;
            }
        }
    }
    // STILL THROUGH IT - A WALL, a rim, what it would have to climb: taken up off its ends, over the top and down again,
    // whichever way (the ways its faces look, then up, down and across) takes it over in the shortest way.
    if (!done) {
        const V3 ways[] = {up, na, nb, {0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
        double best = 1e300;
        std::vector<V3> chosen;
        const double far = std::clamp(4.0 * len, 200.0, 3000.0);
        for (V3 w : ways) {
            if (w.length() < 1e-6) continue;
            w = w.normalized();
            if (std::abs(dot3(u, w)) > 0.95) continue;
            for (double h = std::max(1.0, 2.0 * r); h <= far && 2.0 * h + len < best; h *= 1.35) {
                const V3 lift = w * (h + 1.5 * r);
                const std::vector<V3> path{a + lift, b + lift, b};
                if (!clear(path)) continue;
                best = 2.0 * h + len;
                chosen = path;
                break;
            }
        }
        if (!chosen.empty()) laid = chosen;
    }
    if (overCache_.size() > 4096) overCache_.clear();
    overCache_[key] = laid;
    return laid;
}

std::vector<V3> Route3D::centreline(const RouteSegment3D& s, int* tight) const {
    const RouteNode3D* na = node(s.a);
    const RouteNode3D* nb = node(s.b);
    const bool ca = na && na->kind == RouteNodeKind::Connector, cb = nb && nb->kind == RouteNodeKind::Connector;
    std::vector<V3> p = solids ? overSolids(corners(s), cornerNormals(s), diameterOf(s) / 2.0, ca, cb) : corners(s);
    // OUT OF THE END OF A CONNECTOR'S BODY, STRAIGHT: a point where the body ends, on the way to the lead's end, so a bend
    // there starts past the body and the cable comes out of its end face - never through its side.
    // The bend there may take all the way back to the end of the body - nothing bends at that point itself.
    size_t bodyEndA = 0, bodyEndB = 0;
    if (ca && p.size() >= 2) {
        const double body = bodyLength(*na);
        if (body > 1e-6 && body < (p[1] - p[0]).length() - 1e-6) {
            p.insert(p.begin() + 1, p[0] + exitOf(*na) * body);
            bodyEndA = 1;
        }
    }
    if (cb && p.size() >= 2) {
        const double body = bodyLength(*nb);
        const size_t e = p.size() - 1;
        if (body > 1e-6 && body < (p[e - 1] - p[e]).length() - 1e-6) {
            p.insert(p.begin() + static_cast<long long>(e), p[e] + exitOf(*nb) * body);
            bodyEndB = e;
        }
    }
    if (tight) *tight = 0;
    if (p.size() < 3) return p;
    const double bend = bendRadiusOf(s);
    // A connector's lead's end - its bend starts by the connector, where the cable comes out through what it is fitted in.
    const size_t leadA = ca ? (bodyEndA != 0 ? 2 : 1) : 0;
    const size_t leadB = cb ? (bodyEndB != 0 ? bodyEndB - 1 : p.size() - 2) : 0;
    auto arcClear = [&](const std::vector<V3>& arc) {
        for (size_t k = 1; k < arc.size(); ++k) {
            const V3 e = arc[k] - arc[k - 1];
            const double l = e.length();
            V3 hit;
            if (l > 1e-6 && solids(arc[k - 1], e / l, l, hit)) return false;
        }
        return true;
    };
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
        const double room = std::min(bodyEndA != 0 && i - 1 == bodyEndA ? l1 : 0.5 * l1, bodyEndB != 0 && i + 1 == bodyEndB ? l2 : 0.5 * l2);
        if (t > room + 1e-9) {
            t = room;
            if (tight) ++*tight;
        }
        const V3 n1 = (d2 - d1 * std::cos(turn)).normalized();
        const int steps = std::max(2, static_cast<int>(std::ceil(turn / (kPi / 18.0))));
        auto arcOf = [&](double tt) {
            const double radius = tt / half;
            const V3 centre = p[i] - d1 * tt + n1 * radius;
            std::vector<V3> arc;
            for (int k = 0; k <= steps; ++k) {
                const double phi = turn * k / steps;
                arc.push_back(centre + (n1 * -std::cos(phi) + d1 * std::sin(phi)) * radius);
            }
            return arc;
        };
        std::vector<V3> arc = arcOf(t);
        // THE ARC CUTTING INTO A SOLID - round the inside of a corner laid over an edge: turned tighter until it clears it.
        if (solids && i != leadA && i != leadB)
            for (int tries = 0; tries < 6 && !arcClear(arc); ++tries) {
                t *= 0.5;
                arc = arcOf(t);
                if (tight && tries == 0) ++*tight;
            }
        out.insert(out.end(), arc.begin(), arc.end());
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
    if (std::abs(tension - 2.0) > 1e-9) o[QStringLiteral("tension")] = tension;
    if (wires > 0) {
        o[QStringLiteral("wires")] = wires;
        o[QStringLiteral("wireD")] = wireD;
    }
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
        if (n.back) j[QStringLiteral("back")] = true;
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
        if (s.tension >= 0.0) j[QStringLiteral("tension")] = s.tension;
        if (s.wires > 0) j[QStringLiteral("wires")] = s.wires;
        if (s.wireD > 0.0) j[QStringLiteral("wireD")] = s.wireD;
        if (s.wireSame) j[QStringLiteral("wireSame")] = true;
        if (!s.wireColors.empty()) {
            QJsonArray colours;
            for (const QColor& c : s.wireColors) colours.append(c.isValid() ? c.name() : QString());
            j[QStringLiteral("wireColors")] = colours;
        }
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
    r.tension = o.value(QStringLiteral("tension")).toDouble(2.0);
    r.wires = o.value(QStringLiteral("wires")).toInt(0);
    r.wireD = o.value(QStringLiteral("wireD")).toDouble(0.6);
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
        n.back = j.value(QStringLiteral("back")).toBool(false);
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
        s.tension = j.value(QStringLiteral("tension")).toDouble(-1.0);
        s.wires = j.value(QStringLiteral("wires")).toInt(0);
        s.wireD = j.value(QStringLiteral("wireD")).toDouble(0.0);
        s.wireSame = j.value(QStringLiteral("wireSame")).toBool(false);
        for (const QJsonValue& c : j.value(QStringLiteral("wireColors")).toArray())
            s.wireColors.push_back(c.toString().isEmpty() ? QColor() : QColor(c.toString()));
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
