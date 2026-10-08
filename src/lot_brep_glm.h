#pragma once

// B-rep 이식용 glm 대역. 네이티브 lot_brep_shape / lot_brep_tessellator 를 거의 그대로 옮기려고,
// 거기서 쓰는 만큼만 glm 이름으로 흉내 낸다 (vec3 는 엔진의 것, dvec2/dvec3 는 여기 것).
// 다른 파일은 이 헤더를 쓰지 않는다 - 엔진 수학은 lot_math.h 그대로.

#include "lot_math.h"

#include <algorithm>
#include <cmath>

// vec3 에 없던 연산 (네이티브 코드가 glm 에 기대던 것들)
inline vec3 operator-(const vec3& v) { return vec3{-v.x, -v.y, -v.z}; }
inline vec3 operator*(float s, const vec3& v) { return v * s; }
inline vec3 operator/(const vec3& v, float s) { return vec3{v.x / s, v.y / s, v.z / s}; }
inline vec3& operator+=(vec3& a, const vec3& b) { a = a + b; return a; }
inline vec3& operator-=(vec3& a, const vec3& b) { a = a - b; return a; }
inline vec3& operator*=(vec3& a, float s) { a = a * s; return a; }

namespace glm {

using vec3 = ::vec3;

struct dvec2 {
    double x = 0.0, y = 0.0;
    dvec2() = default;
    dvec2(double x_, double y_) : x(x_), y(y_) {}
    explicit dvec2(double s) : x(s), y(s) {}
};
inline dvec2 operator+(const dvec2& a, const dvec2& b) { return {a.x + b.x, a.y + b.y}; }
inline dvec2 operator-(const dvec2& a, const dvec2& b) { return {a.x - b.x, a.y - b.y}; }
inline dvec2 operator*(const dvec2& a, double s) { return {a.x * s, a.y * s}; }
inline dvec2 operator/(const dvec2& a, double s) { return {a.x / s, a.y / s}; }
inline dvec2& operator/=(dvec2& a, double s) { a = a / s; return a; }

struct dvec3 {
    double x = 0.0, y = 0.0, z = 0.0;
    dvec3() = default;
    dvec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    explicit dvec3(double s) : x(s), y(s), z(s) {}
    explicit dvec3(const vec3& v) : x(v.x), y(v.y), z(v.z) {}
};
inline dvec3 operator+(const dvec3& a, const dvec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline dvec3 operator-(const dvec3& a, const dvec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline dvec3 operator*(const dvec3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline dvec3 operator/(const dvec3& a, double s) { return {a.x / s, a.y / s, a.z / s}; }
inline dvec3& operator+=(dvec3& a, const dvec3& b) { a = a + b; return a; }

inline float dot(const vec3& a, const vec3& b) { return ::dot(a, b); }
inline double dot(const dvec2& a, const dvec2& b) { return a.x * b.x + a.y * b.y; }
inline double dot(const dvec3& a, const dvec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(const vec3& a, const vec3& b) { return ::cross(a, b); }
inline dvec3 cross(const dvec3& a, const dvec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const vec3& v) { return std::sqrt(::dot(v, v)); }
inline double length(const dvec2& v) { return std::sqrt(dot(v, v)); }
inline double length(const dvec3& v) { return std::sqrt(dot(v, v)); }
inline vec3 normalize(const vec3& v) { return ::normalize(v); }
inline dvec3 normalize(const dvec3& v) { const double l = length(v); return l > 0.0 ? v / l : v; }
inline dvec2 min(const dvec2& a, const dvec2& b) { return {std::min(a.x, b.x), std::min(a.y, b.y)}; }
inline dvec2 max(const dvec2& a, const dvec2& b) { return {std::max(a.x, b.x), std::max(a.y, b.y)}; }

}  // namespace glm
