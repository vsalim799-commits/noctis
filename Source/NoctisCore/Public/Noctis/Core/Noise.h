// Deterministic gradient noise for terrain, weather fields and individual markings.
#pragma once

#include "Noctis/Core/Math.h"

namespace noctis
{
class NOCTIS_API GradientNoise2D
{
public:
    explicit GradientNoise2D(u64 seed = 1);
    // Range approximately [-1, 1].
    float sample(float x, float y) const;
    // Fractal Brownian motion.
    float fbm(float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;
    // Ridged multifractal in [0, 1].
    float ridged(float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;

private:
    u8 perm_[512];
};
} // namespace noctis
