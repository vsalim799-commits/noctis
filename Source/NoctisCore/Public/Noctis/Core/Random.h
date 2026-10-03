// Deterministic random numbers (PCG32, O'Neill 2014) and hashing.
// Every individual owns its own stream so parallel updates stay deterministic.
#pragma once

#include "Noctis/Core/Platform.h"

#include <cmath>

namespace noctis
{
// SplitMix64 finaliser — good avalanche, used to derive seeds.
inline u64 mixHash64(u64 x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
inline u64 hashCombine(u64 a, u64 b) { return mixHash64(a ^ (mixHash64(b) + 0x9E3779B97F4A7C15ull + (a << 6) + (a >> 2))); }
inline u64 hashString(const char* s)
{
    u64 h = 1469598103934665603ull; // FNV-1a
    while (s && *s)
    {
        h ^= static_cast<u8>(*s++);
        h *= 1099511628211ull;
    }
    return h;
}

class Rng
{
public:
    Rng() { seed(0x853C49E6748FEA9Bull, 0xDA3E39CB94B95BDBull); }
    explicit Rng(u64 seedValue, u64 stream = 1) { seed(seedValue, stream); }

    void seed(u64 seedValue, u64 stream)
    {
        state_ = 0u;
        inc_ = (stream << 1u) | 1u;
        nextU32();
        state_ += seedValue;
        nextU32();
    }

    u32 nextU32()
    {
        const u64 old = state_;
        state_ = old * 6364136223846793005ull + inc_;
        const u32 xorshifted = static_cast<u32>(((old >> 18u) ^ old) >> 27u);
        const u32 rot = static_cast<u32>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }

    // [0, 1)
    float uniform() { return static_cast<float>(nextU32() >> 8) * (1.0f / 16777216.0f); }
    float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }
    int rangeInt(int lo, int hiInclusive)
    {
        if (hiInclusive <= lo)
        {
            return lo;
        }
        const u32 span = static_cast<u32>(hiInclusive - lo + 1);
        return lo + static_cast<int>(nextU32() % span);
    }
    bool chance(float p) { return uniform() < p; }

    // Standard normal via Box–Muller.
    float normal()
    {
        if (hasSpare_)
        {
            hasSpare_ = false;
            return spare_;
        }
        float u1 = uniform();
        if (u1 < 1e-7f)
        {
            u1 = 1e-7f;
        }
        const float u2 = uniform();
        const float r = std::sqrt(-2.0f * std::log(u1));
        const float theta = 6.28318530717958647692f * u2;
        spare_ = r * std::sin(theta);
        hasSpare_ = true;
        return r * std::cos(theta);
    }
    float normal(float mean, float sd) { return mean + sd * normal(); }
    float normalClamped(float mean, float sd, float lo, float hi)
    {
        const float v = normal(mean, sd);
        return v < lo ? lo : (v > hi ? hi : v);
    }
    // Poisson sample (Knuth for small lambda, normal approximation above 30).
    int poisson(float lambda)
    {
        if (lambda <= 0.0f)
        {
            return 0;
        }
        if (lambda > 30.0f)
        {
            const float v = normal(lambda, std::sqrt(lambda));
            return v < 0.0f ? 0 : static_cast<int>(v + 0.5f);
        }
        const float l = std::exp(-lambda);
        int k = 0;
        float p = 1.0f;
        do
        {
            ++k;
            p *= uniform();
        } while (p > l);
        return k - 1;
    }

    u64 stateForSave() const { return state_; }
    u64 incForSave() const { return inc_; }
    void restore(u64 state, u64 inc)
    {
        state_ = state;
        inc_ = inc;
        hasSpare_ = false;
    }

private:
    u64 state_ = 0;
    u64 inc_ = 1;
    float spare_ = 0.0f;
    bool hasSpare_ = false;
};
} // namespace noctis
