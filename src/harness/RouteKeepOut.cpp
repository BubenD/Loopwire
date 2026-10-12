#include "harness/RouteKeepOut.h"

#include <algorithm>
#include <cmath>

namespace Loopwire {

namespace {

double along(const V3& p, int axis) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }

} // namespace

QString keepOutName(const KeepOut3D& k) {
    if (!k.name.trimmed().isEmpty()) return k.name.trimmed();
    if (k.zone) return QStringLiteral("Zone %1").arg(k.id);
    return QStringLiteral("Level %1 %2").arg(QLatin1Char("XYZ"[std::clamp(k.axis, 0, 2)])).arg(k.at, 0, 'f', 1);
}

bool crossesKeepOut(const KeepOut3D& k, const V3& a, const V3& b, double radius, V3& at) {
    if (!k.zone) {
        // A LEVEL: crossed from one side to the other, or come within the cable's radius of it.
        const double da = along(a, k.axis) - k.at, db = along(b, k.axis) - k.at;
        if ((da > radius && db > radius) || (da < -radius && db < -radius)) return false;
        double t = 0.0;
        if (std::abs(da) <= radius) t = 0.0;
        else if (std::abs(db - da) > 1e-12) t = std::clamp(((da > 0 ? radius : -radius) - da) / (db - da), 0.0, 1.0);
        at = a + (b - a) * t;
        return true;
    }
    // A ZONE: the box, grown by the cable's radius, entered anywhere along the stretch (slabs).
    const V3 lo{std::min(k.lo.x, k.hi.x) - radius, std::min(k.lo.y, k.hi.y) - radius, std::min(k.lo.z, k.hi.z) - radius};
    const V3 hi{std::max(k.lo.x, k.hi.x) + radius, std::max(k.lo.y, k.hi.y) + radius, std::max(k.lo.z, k.hi.z) + radius};
    const V3 d = b - a;
    double t0 = 0.0, t1 = 1.0;
    for (int ax = 0; ax < 3; ++ax) {
        const double o = along(a, ax), dd = along(d, ax), l = along(lo, ax), h = along(hi, ax);
        if (std::abs(dd) < 1e-12) {
            if (o < l || o > h) return false;
            continue;
        }
        double u = (l - o) / dd, v = (h - o) / dd;
        if (u > v) std::swap(u, v);
        t0 = std::max(t0, u);
        t1 = std::min(t1, v);
        if (t0 > t1) return false;
    }
    at = a + d * t0;
    return true;
}

std::vector<KeepOutHit> keepOutHits(const Route3D& r) {
    std::vector<KeepOutHit> out;
    if (r.keepOuts.empty()) return out;
    for (const RouteSegment3D& s : r.segments) {
        const std::vector<V3> c = r.centreline(s);
        const double radius = r.diameterOf(s) / 2.0;
        for (const KeepOut3D& k : r.keepOuts)
            for (size_t i = 1; i < c.size(); ++i) {
                V3 at;
                if (crossesKeepOut(k, c[i - 1], c[i], radius, at)) {
                    out.push_back({s.id, k.id, at, k.block});
                    break;
                }
            }
    }
    return out;
}

} // namespace Loopwire
