#include "tools/EntityTransforms.h"

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

// Apply a per-point function to every geometric point of an entity.
template <typename F>
void mapPoints(Entity& e, F fn) {
    e.a = fn(e.a);
    e.b = fn(e.b);
    e.center = fn(e.center);
    for (auto& p : e.points) p = fn(p);
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
    mapPoints(e, [&](const Vec2& p) { return scalePoint(p, base, factor); });
    e.radius *= std::abs(factor);
    e.radiusY *= std::abs(factor);
}

void mirrorEntity(Entity& e, const Vec2& a, const Vec2& b) {
    const Vec2 u = (b - a).normalized();
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
