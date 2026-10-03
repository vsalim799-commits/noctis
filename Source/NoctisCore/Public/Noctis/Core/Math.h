// Small, dependency-free vector math. World frame: x = east, y = north, z = up, metres.
// Headings are radians, 0 = +x (east), counter-clockwise positive.
#pragma once

#include "Noctis/Core/Platform.h"

#include <algorithm>
#include <cmath>

namespace noctis
{
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 6.28318530717958647692f;
inline constexpr float kHalfPi = 1.57079632679489661923f;
inline constexpr float kDegToRad = kPi / 180.0f;
inline constexpr float kRadToDeg = 180.0f / kPi;
inline constexpr float kGravity = 9.81f;          // m s^-2
inline constexpr float kSpeedOfSound = 343.0f;    // m s^-1 at 20 degC; corrected with temperature where needed
inline constexpr float kAirDensity = 1.225f;      // kg m^-3 at sea level, 15 degC
inline constexpr float kWaterDensity = 1000.0f;   // kg m^-3
inline constexpr float kEpsilon = 1e-6f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float saturate(float v) { return clampf(v, 0.0f, 1.0f); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float inverseLerp(float a, float b, float v) { return (std::fabs(b - a) < kEpsilon) ? 0.0f : (v - a) / (b - a); }
inline float remapClamped(float v, float inA, float inB, float outA, float outB)
{
    return lerpf(outA, outB, saturate(inverseLerp(inA, inB, v)));
}
inline float smoothstep(float e0, float e1, float x)
{
    const float t = saturate((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}
inline float square(float v) { return v * v; }
inline float signf(float v) { return v < 0.0f ? -1.0f : 1.0f; }

// Wrap angle to (-pi, pi].
inline float wrapAngle(float a)
{
    a = std::fmod(a + kPi, kTwoPi);
    if (a < 0.0f)
    {
        a += kTwoPi;
    }
    return a - kPi;
}
inline float angleDelta(float from, float to) { return wrapAngle(to - from); }

// Exponential approach factor for frame-rate independent smoothing: 1 - exp(-dt / tau).
inline float approachFactor(float dt, float tau) { return tau <= 0.0f ? 1.0f : 1.0f - std::exp(-dt / tau); }

struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float inX, float inY) : x(inX), y(inY) {}

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }

    float lengthSq() const { return x * x + y * y; }
    float length() const { return std::sqrt(lengthSq()); }
    Vec2 normalized() const
    {
        const float l = length();
        return l > kEpsilon ? Vec2{x / l, y / l} : Vec2{0.0f, 0.0f};
    }
    float heading() const { return std::atan2(y, x); }
    Vec2 perp() const { return {-y, x}; }
    static Vec2 fromHeading(float h) { return {std::cos(h), std::sin(h)}; }
};

inline float dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline float cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
inline float distance(const Vec2& a, const Vec2& b) { return (a - b).length(); }
inline float distanceSq(const Vec2& a, const Vec2& b) { return (a - b).lengthSq(); }
inline Vec2 lerp(const Vec2& a, const Vec2& b, float t) { return a + (b - a) * t; }
inline Vec2 rotate(const Vec2& v, float angle)
{
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float inX, float inY, float inZ) : x(inX), y(inY), z(inZ) {}
    constexpr Vec3(const Vec2& v, float inZ) : x(v.x), y(v.y), z(inZ) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

    float lengthSq() const { return x * x + y * y + z * z; }
    float length() const { return std::sqrt(lengthSq()); }
    Vec3 normalized() const
    {
        const float l = length();
        return l > kEpsilon ? Vec3{x / l, y / l, z / l} : Vec3{0.0f, 0.0f, 0.0f};
    }
    Vec2 xy() const { return {x, y}; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float distance(const Vec3& a, const Vec3& b) { return (a - b).length(); }
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

// Axis-aligned rectangle in the horizontal plane.
struct Rect2
{
    Vec2 min;
    Vec2 max;
    bool contains(const Vec2& p) const { return p.x >= min.x && p.y >= min.y && p.x <= max.x && p.y <= max.y; }
    Vec2 center() const { return (min + max) * 0.5f; }
    Vec2 size() const { return max - min; }
    Vec2 clampPoint(const Vec2& p) const { return {clampf(p.x, min.x, max.x), clampf(p.y, min.y, max.y)}; }
};

// Decibel helpers (sound pressure level re 20 uPa).
inline float dbToPressureRatio(float db) { return std::pow(10.0f, db / 20.0f); }
inline float pressureRatioToDb(float r) { return 20.0f * std::log10(std::max(r, 1e-12f)); }
inline float dbSumPower(float aDb, float bDb)
{
    return 10.0f * std::log10(std::pow(10.0f, aDb / 10.0f) + std::pow(10.0f, bDb / 10.0f));
}
} // namespace noctis
