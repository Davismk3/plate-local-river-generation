#pragma once

#include <algorithm>
#include <cmath>

namespace finite_world {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(double s, Vec3 a) { return a * s; }
inline Vec3 operator/(Vec3 a, double s) { return {a.x / s, a.y / s, a.z / s}; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    const double n = length(a);
    return n > 1e-300 ? a / n : Vec3{0.0, 0.0, 0.0};
}

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

inline double distance(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Straight segment in a plate's 2D chart.
struct Segment2D {
    Vec2 a;
    Vec2 b;

    double parameter(Vec2 p) const {
        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const double denominator = dx * dx + dy * dy;
        if (denominator <= 1e-24) return 0.0;
        return std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / denominator, 0.0, 1.0);
    }
    double distance(Vec2 p) const {
        const double t = parameter(p);
        return finite_world::distance({a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)}, p);
    }
};

// Row-major 3x3 rotation matrix.
struct Mat3 {
    double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

    Vec3 apply(Vec3 v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
    Vec3 applyTransposed(Vec3 v) const {
        return {m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z,
                m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z,
                m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z};
    }
};

inline Mat3 operator*(const Mat3& a, const Mat3& b) {
    Mat3 r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
        }
    }
    return r;
}

inline Mat3 rotationX(double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    Mat3 r;
    r.m[1][1] = c; r.m[1][2] = -s;
    r.m[2][1] = s; r.m[2][2] = c;
    return r;
}

inline Mat3 rotationY(double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    Mat3 r;
    r.m[0][0] = c; r.m[0][2] = s;
    r.m[2][0] = -s; r.m[2][2] = c;
    return r;
}

} // namespace finite_world
