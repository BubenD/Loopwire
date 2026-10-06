#include "harness/RouteGravity.h"

#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

const std::vector<CableTension>& cableTensions() {
    static const std::vector<CableTension> all{
        {QStringLiteral("Slack"), 0.0},
        {QStringLiteral("Loose 0.5 N"), 0.5},
        {QStringLiteral("Normal 2 N"), 2.0},
        {QStringLiteral("Firm 10 N"), 10.0},
        {QStringLiteral("Taut 50 N"), 50.0},
    };
    return all;
}

QString cableTensionName(double newtons) {
    for (const CableTension& t : cableTensions())
        if (std::abs(t.newtons - newtons) < 1e-9) return t.name;
    return QStringLiteral("%1 N").arg(newtons, 0, 'g', 4);
}

double tensionOf(const Route3D& r, const RouteSegment3D& s) { return s.tension >= 0.0 ? s.tension : std::max(0.0, r.tension); }

double fallShown(double diameter) { return std::max(1.0, diameter * 0.25); }

int stiffnessOf(double tension) {
    return tension <= 0.0 ? 0 : std::clamp(static_cast<int>(std::lround(std::log2(1.0 + tension / 0.5))), 1, 6);
}

namespace {

double pathLength(const std::vector<V3>& p) {
    double l = 0.0;
    for (size_t i = 1; i < p.size(); ++i) l += (p[i] - p[i - 1]).length();
    return l;
}

// The path again, a point every `step` along it, its two ends kept; and how far along each point is.
std::vector<V3> resample(const std::vector<V3>& c, double step, std::vector<double>& along) {
    std::vector<V3> out{c.front()};
    along = {0.0};
    double gone = 0.0, next = step;
    for (size_t i = 1; i < c.size(); ++i) {
        const V3 a = c[i - 1], b = c[i];
        const double l = (b - a).length();
        while (l > 1e-12 && next <= gone + l - 1e-9) {
            out.push_back(a + (b - a) * ((next - gone) / l));
            along.push_back(next);
            next += step;
        }
        gone += l;
    }
    if ((out.back() - c.back()).length() > 1e-9) {
        out.push_back(c.back());
        along.push_back(gone);
    }
    return out;
}

struct Hanger {
    const Route3D& r;
    V3 down;
    double radius;
    double perMm;   // weight, N per mm
    std::vector<V3> laid;
    std::vector<char> held;   // its ends, and a connector's lead
    double spacing;

    // MOVED FROM ONE PLACE TOWARD ANOTHER, stopped by a solid in the way - its radius off it. Whether it was stopped.
    bool settle(const V3& from, const V3& to, V3& at) const {
        at = to;
        if (!r.solids) return false;
        const V3 d = to - from;
        const double l = d.length();
        if (l < 1e-9) return false;
        const V3 u = d / l;
        V3 hit;
        if (!r.solids(from, u, l + radius, hit)) return false;
        const double t = std::max(0.0, (hit - from).length() - radius);
        if (t >= l) return false;
        at = from + u * t;
        return true;
    }

    // ONE STRETCH IN THE AIR HUNG BY A TENSION, from point `a` to point `b` (both held): between each two points held,
    // the parabola a cable under tension makes under its weight (sagging w s^2 / 8T at the middle of a span s) - from
    // the way it is laid (`asLaid`: a cable put in with a tension keeps its shape and sags from it), or from a straight
    // line between them (a slack one hangs free) - and where that would take a point into a solid, the point rests on it
    // instead, held there, and the cable either side is hung again from it. Its points from `a` to `b`.
    std::vector<V3> hangSpan(size_t a0, size_t b0, double tension, bool asLaid) const {
        const size_t n = b0 - a0 + 1;
        std::vector<char> fixed(n, 0);
        fixed.front() = fixed.back() = 1;
        std::vector<V3> at(laid.begin() + static_cast<long>(a0), laid.begin() + static_cast<long>(b0) + 1);
        const double k = perMm * spacing * spacing / (2.0 * std::max(1e-9, tension));
        std::vector<V3> p(n);
        for (int round = 0; round < 24; ++round) {
            p = at;
            size_t a = 0;
            for (size_t b = 1; b < n; ++b) {
                if (!fixed[b]) continue;
                const double m = static_cast<double>(b - a);
                for (size_t i = a + 1; i < b; ++i) {
                    const double j = static_cast<double>(i - a);
                    const V3 from = asLaid ? laid[a0 + i] + ((at[a] - laid[a0 + a]) * (1.0 - j / m) + (at[b] - laid[a0 + b]) * (j / m))
                                           : at[a] + (at[b] - at[a]) * (j / m);
                    p[i] = from + down * (k * j * (m - j));
                }
                a = b;
            }
            bool rested = false;
            for (size_t i = 0; i < n; ++i) {
                if (fixed[i]) continue;
                V3 q;
                if (settle(laid[a0 + i], p[i], q)) {
                    fixed[i] = 1;
                    at[i] = q;
                    p[i] = q;
                    rested = true;
                }
            }
            if (!rested) break;
        }
        return p;
    }
};

} // namespace

CableSag3D hangCable(const Route3D& r, const RouteSegment3D& s, const V3& downIn, double density) {
    CableSag3D out;
    out.segment = s.id;
    const std::vector<V3> c = r.centreline(s);
    if (c.size() < 2) return out;
    const V3 down = downIn.length() > 1e-9 ? downIn.normalized() : V3{0, 0, -1};
    const double d = r.diameterOf(s);
    const double total = pathLength(c);
    out.laidLength = total;
    std::vector<double> along;
    const double step = std::clamp(total / 80.0, 1.0, 20.0);
    Hanger h{r, down, d / 2.0, 0.0, resample(c, step, along), {}, 0.0};
    const size_t n = h.laid.size();
    h.spacing = total / std::max<size_t>(1, n - 1);
    // ITS WEIGHT: the bundle's section, as dense as a bundle of wires is, in newtons a millimetre.
    const double area = M_PI * d * d / 4.0;   // mm2
    h.perMm = density * area * 1e-6 * 9.81;   // g/cm3 x mm2 = mg/mm; to kg, x g
    // HELD: at its two nodes, and straight out of a connector as far as its lead.
    h.held.assign(n, 0);
    h.held.front() = h.held.back() = 1;
    const RouteNode3D* a = r.node(s.a);
    const RouteNode3D* b = r.node(s.b);
    const double leadA = a && a->kind == RouteNodeKind::Connector ? r.leadOf(*a, d / 2.0) : 0.0;
    const double leadB = b && b->kind == RouteNodeKind::Connector ? r.leadOf(*b, d / 2.0) : 0.0;
    for (size_t i = 0; i < n; ++i)
        if (along[i] <= leadA + 1e-9 || along[i] >= total - leadB - 1e-9) h.held[i] = 1;
    // LYING ON SOMETHING: a face right under it, the way gravity pulls - within its radius and a little more. There it
    // stays as it is laid.
    if (r.solids) {
        const double near = d / 2.0 + std::max(1.0, d * 0.25);
        for (size_t i = 0; i < n; ++i) {
            V3 hit;
            if (!h.held[i] && r.solids(h.laid[i], down, near, hit)) h.held[i] = 1;
        }
    }

    const double asked = tensionOf(r, s);
    std::vector<V3> hung = h.laid;
    double used = asked;
    const bool byLength = asked <= 0.0;
    // EACH STRETCH IN THE AIR, from where it is held to where it is held next. PUT IN WITH A TENSION: it hangs as that
    // tension holds it - and is as long as that makes it (hungLength against laidLength says by how much). SLACK: it
    // hangs by the length it was laid, the pull that leaves it found by halves, between a breath and a lot.
    // HELD UP BY ITS OWN STIFFNESS: a stretch in the air no longer than two of its bend radii - the bend down from a
    // connector to the floor, say - stays as it is laid.
    const double stiff = 2.0 * r.bendRadiusOf(s);
    for (size_t a0 = 0; a0 + 1 < n;) {
        size_t b0 = a0 + 1;
        while (b0 < n - 1 && !h.held[b0]) ++b0;
        if (b0 > a0 + 1 && along[b0] - along[a0] > stiff) {
            out.spans.push_back({static_cast<int>(a0), static_cast<int>(b0)});
            std::vector<V3> span;
            if (!byLength) {
                span = h.hangSpan(a0, b0, asked, true);
            } else {
                const std::vector<V3> was(h.laid.begin() + static_cast<long>(a0), h.laid.begin() + static_cast<long>(b0) + 1);
                const double length = pathLength(was);
                double lo = std::log(1e-5), hi = std::log(1e5);
                for (int i = 0; i < 48; ++i) {
                    const double mid = (lo + hi) / 2.0;
                    if (pathLength(h.hangSpan(a0, b0, std::exp(mid), false)) > length) lo = mid;
                    else hi = mid;
                }
                used = std::exp(hi);
                span = h.hangSpan(a0, b0, used, false);
            }
            for (size_t i = 0; i < span.size(); ++i) hung[a0 + i] = span[i];
        }
        a0 = b0;
    }
    out.down = down;
    out.hung = hung;
    out.laid = h.laid;
    out.tension = used;
    out.byLength = byLength;
    // Both measured through the same points, so what one has more than the other is what hanging takes.
    out.laidLength = pathLength(h.laid);
    out.hungLength = pathLength(hung);
    out.drop.resize(n);
    for (size_t i = 0; i < n; ++i) {
        out.drop[i] = std::max(0.0, dot3(hung[i] - h.laid[i], down));
        if (out.drop[i] > out.maxDrop) {
            out.maxDrop = out.drop[i];
            out.maxAt = hung[i];
        }
    }
    // WHERE IT FALLS: each stretch falling further than is worth showing, by its lowest point.
    const double shown = fallShown(d);
    for (size_t i = 0; i < n;) {
        if (out.drop[i] <= shown) {
            ++i;
            continue;
        }
        size_t lowest = i;
        for (; i < n && out.drop[i] > shown; ++i)
            if (out.drop[i] > out.drop[lowest]) lowest = i;
        out.falls.push_back(static_cast<int>(lowest));
    }
    return out;
}

std::vector<CableSag3D> hangRoute(const Route3D& r, const V3& down, double density) {
    std::vector<CableSag3D> out;
    for (const RouteSegment3D& s : r.segments) out.push_back(hangCable(r, s, down, density));
    return out;
}

} // namespace Edraw_Desktop
