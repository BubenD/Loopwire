#pragma once

#include <cmath>

namespace Edraw_Desktop {

// 2D point/vector in world (drawing) coordinates. Doubles for CAD precision.
struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    constexpr Vec2() = default;
    constexpr Vec2(double x_, double y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(double s) const { return {x / s, y / s}; }

    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(double s) { x *= s; y *= s; return *this; }

    constexpr bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    constexpr bool operator!=(const Vec2& o) const { return !(*this == o); }

    double length() const { return std::sqrt(x * x + y * y); }
    double lengthSq() const { return x * x + y * y; }

    Vec2 normalized() const {
        const double len = length();
        return len > 0.0 ? Vec2{x / len, y / len} : Vec2{0.0, 0.0};
    }

    // Perpendicular (rotated +90 degrees).
    constexpr Vec2 perp() const { return {-y, x}; }
};

constexpr Vec2 operator*(double s, const Vec2& v) { return {v.x * s, v.y * s}; }

inline double dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline double cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
inline double distance(const Vec2& a, const Vec2& b) { return (a - b).length(); }
inline double distanceSq(const Vec2& a, const Vec2& b) { return (a - b).lengthSq(); }

// Angle of vector in radians, measured CCW from +X axis.
inline double angleOf(const Vec2& v) { return std::atan2(v.y, v.x); }

// Linear interpolation a -> b.
inline Vec2 lerp(const Vec2& a, const Vec2& b, double t) { return a + (b - a) * t; }

} // namespace Edraw_Desktop
