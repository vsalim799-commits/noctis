#include "Noctis/Core/Noise.h"

#include "Noctis/Core/Random.h"

namespace noctis
{
namespace noiseimpl
{
inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline float grad(u8 hash, float x, float y)
{
    switch (hash & 7u)
    {
    case 0: return x + y;
    case 1: return -x + y;
    case 2: return x - y;
    case 3: return -x - y;
    case 4: return x;
    case 5: return -x;
    case 6: return y;
    default: return -y;
    }
}
} // namespace noiseimpl

GradientNoise2D::GradientNoise2D(u64 seed)
{
    u8 p[256];
    for (int i = 0; i < 256; ++i)
    {
        p[i] = static_cast<u8>(i);
    }
    Rng rng(seed, 0x51ED);
    for (int i = 255; i > 0; --i)
    {
        const int j = static_cast<int>(rng.nextU32() % static_cast<u32>(i + 1));
        const u8 tmp = p[i];
        p[i] = p[j];
        p[j] = tmp;
    }
    for (int i = 0; i < 512; ++i)
    {
        perm_[i] = p[i & 255];
    }
}

float GradientNoise2D::sample(float x, float y) const
{
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const int xi = static_cast<int>(fx) & 255;
    const int yi = static_cast<int>(fy) & 255;
    const float xf = x - fx;
    const float yf = y - fy;
    const float u = noiseimpl::fade(xf);
    const float v = noiseimpl::fade(yf);
    const u8 aa = perm_[perm_[xi] + yi];
    const u8 ab = perm_[perm_[xi] + yi + 1];
    const u8 ba = perm_[perm_[xi + 1] + yi];
    const u8 bb = perm_[perm_[xi + 1] + yi + 1];
    const float x1 = lerpf(noiseimpl::grad(aa, xf, yf), noiseimpl::grad(ba, xf - 1.0f, yf), u);
    const float x2 = lerpf(noiseimpl::grad(ab, xf, yf - 1.0f), noiseimpl::grad(bb, xf - 1.0f, yf - 1.0f), u);
    return lerpf(x1, x2, v) * 0.7071f;
}

float GradientNoise2D::fbm(float x, float y, int octaves, float lacunarity, float gain) const
{
    float sum = 0.0f;
    float amp = 1.0f;
    float norm = 0.0f;
    float fx = x;
    float fy = y;
    for (int o = 0; o < octaves; ++o)
    {
        sum += amp * sample(fx, fy);
        norm += amp;
        amp *= gain;
        fx *= lacunarity;
        fy *= lacunarity;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

float GradientNoise2D::ridged(float x, float y, int octaves, float lacunarity, float gain) const
{
    float sum = 0.0f;
    float amp = 1.0f;
    float norm = 0.0f;
    float fx = x;
    float fy = y;
    for (int o = 0; o < octaves; ++o)
    {
        const float n = 1.0f - std::fabs(sample(fx, fy));
        sum += amp * n * n;
        norm += amp;
        amp *= gain;
        fx *= lacunarity;
        fy *= lacunarity;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}
} // namespace noctis
