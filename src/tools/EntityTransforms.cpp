#include "tools/EntityTransforms.h"

#include "harness/Harness.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

namespace {

Vec2 rotatePoint(const Vec2& p, const Vec2& pivot, double c, double s) {
    const Vec2 d = p - pivot;
    return {pivot.x + d.x * c - d.y * s, pivot.y + d.x * s + d.y * c};
}

Vec2 scalePoint(const Vec2& p, const Vec2& base, double f) {
    return base + (p - base) * f;
}

// Reflect point p across the line through a with unit direction u.
Vec2 mirrorPoint(const Vec2& p, const Vec2& a, const Vec2& u) {
    const Vec2 d = p - a;
    const double proj = dot(d, u);
    const Vec2 onLine = a + u * proj;
    return onLine * 2.0 - p;
}

// Apply a per-point function to every geometric point of an entity — including
// world-space geometry stored in meta (a double line's line-start reference and
// trim cut lines/circles), so a moved/rotated/mirrored double line keeps its caps.
template <typename F>
void mapPoints(Entity& e, F fn) {
    e.a = fn(e.a);
    e.b = fn(e.b);
    e.center = fn(e.center);
    for (auto& p : e.points) p = fn(p);

    auto xf = [&](const char* kx, const char* ky) {
        if (e.meta.contains(QLatin1String(kx))) {
            const Vec2 p = fn({e.meta.value(QLatin1String(kx)).toDouble(),
                               e.meta.value(QLatin1String(ky)).toDouble()});
            e.meta.insert(QLatin1String(kx), p.x);
            e.meta.insert(QLatin1String(ky), p.y);
        }
    };
    xf("lsRefAx", "lsRefAy");
    xf("lsRefBx", "lsRefBy");
    xf("textPosX", "textPosY"); // moved dimension-text position follows the entity
    xf("leaderX", "leaderY");   // legacy single balloon leader tip follows the entity
    // Every balloon leader arrow tip (array form) follows the entity too.
    if (e.meta.contains(QLatin1String("leaders"))) {
        QJsonArray arr = e.meta.value(QLatin1String("leaders")).toArray();
        for (int i = 0; i < arr.size(); ++i) {
            const QJsonArray a = arr[i].toArray();
            if (a.size() < 2) continue;
            const Vec2 q = fn({a[0].toDouble(), a[1].toDouble()});
            QJsonArray np;
            np.append(q.x);
            np.append(q.y);
            arr[i] = np;
        }
        e.meta.insert(QLatin1String("leaders"), arr);
    }
    xf("cutStartAx", "cutStartAy");
    xf("cutStartBx", "cutStartBy");
    xf("cutEndAx", "cutEndAy");
    xf("cutEndBx", "cutEndBy");
    xf("cutStartCx", "cutStartCy"); // circle-cut centre
    xf("cutEndCx", "cutEndCy");
    // Extend cut-cap edge sets (flat [ax,ay,bx,by,...]) follow the entity too.
    for (const char* k : {"cutStartPoly", "cutEndPoly"}) {
        if (!e.meta.contains(QLatin1String(k))) continue;
        QJsonArray arr = e.meta.value(QLatin1String(k)).toArray();
        for (int i = 0; i + 1 < arr.size(); i += 2) {
            const Vec2 q = fn({arr[i].toDouble(), arr[i + 1].toDouble()});
            arr[i] = q.x;
            arr[i + 1] = q.y;
        }
        e.meta.insert(QLatin1String(k), arr);
    }
}

} // namespace

void translateEntity(Entity& e, const Vec2& delta) {
    mapPoints(e, [&](const Vec2& p) { return p + delta; });
}

void rotateEntity(Entity& e, const Vec2& pivot, double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    mapPoints(e, [&](const Vec2& p) { return rotatePoint(p, pivot, c, s); });
    e.rotation += angle;
    e.startAngle += angle;
    e.endAngle += angle;
}

void scaleEntity(Entity& e, const Vec2& base, double factor) {
    const double s = std::abs(factor);
    mapPoints(e, [&](const Vec2& p) { return scalePoint(p, base, factor); });
    e.radius *= s;
    e.radiusY *= s;
    // Scale every length-valued meta so the shape scales uniformly: the double-line
    // gap, trim-cap radii, and the uniform corner radius.
    for (const char* k : {"gap", "cutStartR", "cutEndR", "cornerRadius", "landing"})
        if (e.meta.contains(QLatin1String(k)))
            e.meta.insert(QLatin1String(k), e.meta.value(QLatin1String(k)).toDouble() * s);
    // Per-corner fillet radii / chamfer setbacks are lengths too — scale each entry.
    for (const char* k : {"cornerRadii", "cornerChamfers", "cornerChamfers2"}) {
        if (!e.meta.contains(QLatin1String(k))) continue;
        QJsonArray arr = e.meta.value(QLatin1String(k)).toArray();
        for (int i = 0; i < arr.size(); ++i) arr[i] = arr[i].toDouble() * s;
        e.meta.insert(QLatin1String(k), arr);
    }
}

void mirrorEntity(Entity& e, const Vec2& a, const Vec2& b) {
    const Vec2 u = (b - a).normalized();
    // A connector is parametric (axis-aligned body + horizontal pins): mirror its
    // body box and flip the matching orientation flag (mirrorX for a vertical mirror
    // line, mirrorY for a horizontal one), re-anchoring the top corner. Its text
    // offsets stay relative, so skip the generic point map.
    if (e.type == EntityType::Connector) {
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
        const ConnectorGeometry g = connectorGeometry(e.a, spec);
        const Vec2 m0 = mirrorPoint(g.rectMin, a, u), m1 = mirrorPoint(g.rectMax, a, u);
        const double minX = std::min(m0.x, m1.x), maxX = std::max(m0.x, m1.x);
        const double maxY = std::max(m0.y, m1.y);
        const bool vertical = std::abs(u.x) < std::abs(u.y); // mirror line is vertical-ish
        bool mx = spec.mirrorX, my = spec.mirrorY;
        if (vertical) mx = !mx; else my = !my;
        e.meta["mirrorX"] = mx;
        e.meta["mirrorY"] = my;
        e.a = {mx ? maxX : minX, maxY}; // origin = top-left (mx=false) / top-right (mx=true)
        return;
    }
    mapPoints(e, [&](const Vec2& p) { return mirrorPoint(p, a, u); });
    // Reflect arc angles about the mirror line direction.
    const double axis = std::atan2(u.y, u.x);
    if (e.type == EntityType::Arc) {
        const double ns = 2.0 * axis - e.startAngle;
        const double ne = 2.0 * axis - e.endAngle;
        // Mirroring reverses orientation: swap start/end.
        e.startAngle = ne;
        e.endAngle = ns;
    }
    e.rotation = 2.0 * axis - e.rotation;
}

} // namespace Edraw_Desktop
