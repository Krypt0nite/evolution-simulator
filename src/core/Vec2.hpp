#pragma once
#include <cmath>

namespace evo {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    constexpr Vec2() = default;
    constexpr Vec2(double x_, double y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(double s) const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    constexpr bool operator==(const Vec2&) const = default;

    constexpr double dot(Vec2 o) const { return x * o.x + y * o.y; }
    constexpr double lengthSq() const { return x * x + y * y; }
    double length() const { return std::sqrt(lengthSq()); }
    Vec2 normalized() const {
        const double l = length();
        return l > 1e-12 ? Vec2{x / l, y / l} : Vec2{0.0, 0.0};
    }
};

inline double distanceSq(Vec2 a, Vec2 b) { return (a - b).lengthSq(); }
inline double distance(Vec2 a, Vec2 b) { return (a - b).length(); }

}  // namespace evo
