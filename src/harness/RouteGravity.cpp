#include "harness/RouteGravity.h"

#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

const std::vector<CableTension>& cableTensions() {
    static const std::vector<CableTension> all{
        {QStringLiteral("Slack - hangs by its length"), 0.0},
        {QStringLiteral("Loose - 0.5 N"), 0.5},
        {QStringLiteral("Normal - 2 N"), 2.0},
        {QStringLiteral("Firm - 10 N"), 10.0},
        {QStringLiteral("Taut - 50 N"), 50.0},
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

    // HUNG BY A TENSION: between each two points held, a straight line and the parabola a cable under tension makes
    // under its weight (sagging w s^2 / 8T at the middle of a span s) - and where that would take a point into a solid,
    // the point rests on it instead, held there, and the cable either side is hung again from it.
    std::vector<V3> hang(double tension) const {
        const size_t n = laid.size();
        std::vector<char> fixed = held;
        std::vector<V3> at = laid;
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
                    p[i] = at[a] + (at[b] - at[a]) * (j / m) + down * (k * j * (m - j));
                }
                a = b;
            }
            bool rested = false;
            for (size_t i = 0; i < n; ++i) {
                if (fixed[i]) continue;
                V3 q;
                if (settle(laid[i], p[i], q)) {
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

    const double asked = tensionOf(r, s);
    std::vector<V3> hung;
    double used = asked;
    bool byLength = asked <= 0.0;
    // PUT IN WITH A TENSION: it hangs as that tension holds it - and is cut to the length that makes (longer or
    // shorter than it was laid; hungLength says by how much). SLACK: it hangs by the length it was laid.
    if (!byLength) hung = h.hang(asked);
    if (byLength) {
        // THE PULL ITS LENGTH LEAVES IT: as slack as the cable is long - found by halves, between a breath and a lot.
        double lo = std::log(1e-5), hi = std::log(1e5);
        for (int i = 0; i < 48; ++i) {
            const double mid = (lo + hi) / 2.0;
            if (pathLength(h.hang(std::exp(mid))) > total) lo = mid;
            else hi = mid;
        }
        used = std::exp(hi);
        hung = h.hang(used);
    }
    out.hung = hung;
    out.laid = h.laid;
    out.tension = used;
    out.byLength = byLength;
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
