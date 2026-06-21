#pragma once

#include "geometry/Vec2.h"
#include "model/Entity.h"

namespace Edraw_Desktop {

// In-place geometric transforms applied uniformly across an entity's geometry
// (named points, center, and the points vector). Angles in radians.
void translateEntity(Entity& e, const Vec2& delta);
void rotateEntity(Entity& e, const Vec2& pivot, double angle);
void scaleEntity(Entity& e, const Vec2& base, double factor);

// Mirror across the line through a->b.
void mirrorEntity(Entity& e, const Vec2& a, const Vec2& b);

} // namespace Edraw_Desktop
