#include "Noctis/World/Vegetation.h"

#include "Noctis/Core/Noise.h"
#include "Noctis/Core/Random.h"

#include <cmath>

namespace noctis
{
namespace vegimpl
{
// Fraction of plant height that carries edible foliage (lower bound of the foliage band).
float foliageBase(PlantType t)
{
    switch (t)
    {
    case PlantType::AngioTree:
    case PlantType::Conifer:
    case PlantType::Ginkgo: return 0.45f;
    case PlantType::Palm:
    case PlantType::Cycadophyte: return 0.6f;
    case PlantType::AngioShrub:
    case PlantType::XericShrub: return 0.15f;
    default: return 0.0f;
    }
}

// Optical extinction (1/m) of foliage at full density, within the plant's height band (game assumption).
float extinction(PlantType t)
{
    switch (t)
    {
    case PlantType::Fern: return 0.55f;
    case PlantType::Horsetail: return 0.35f;
    case PlantType::AngioHerb: return 0.3f;
    case PlantType::AngioShrub: return 0.45f;
    case PlantType::XericShrub: return 0.25f;
    case PlantType::AngioTree: return 0.06f;
    case PlantType::Conifer: return 0.07f;
    case PlantType::Palm: return 0.05f;
    case PlantType::Cycadophyte: return 0.2f;
    case PlantType::Ginkgo: return 0.05f;
    case PlantType::AquaticMacrophyte: return 0.0f;
    case PlantType::Count: break;
    }
    return 0.0f;
}

bool isTree(PlantType t)
{
    return t == PlantType::AngioTree || t == PlantType::Conifer || t == PlantType::Palm || t == PlantType::Ginkgo;
}

bool isLow(PlantType t)
{
    return t == PlantType::Fern || t == PlantType::Horsetail || t == PlantType::AngioHerb || t == PlantType::AquaticMacrophyte;
}

float temperatureFactor(float tC)
{
    // Broad thermal response for a warm-temperate flora (game assumption): no growth < 4 degC,
    // optimum 18–30 degC, decline above 36 degC.
    return saturate((tC - 4.0f) / 14.0f) * saturate((42.0f - tC) / 6.0f);
}
} // namespace vegimpl

void VegetationSystem::build(const EnvironmentSim& env, const Terrain& terrain, u64 seed, float cellSizeM)
{
    const int cells = std::max(4, static_cast<int>(terrain.size() / cellSizeM));
    const float cs = terrain.size() / static_cast<float>(cells);
    trampling_.resize(cells, cells, cs, terrain.origin(), 0.0f);
    trail_.resize(cells, cells, cs, terrain.origin(), 0.0f);
    litter_.resize(cells, cells, cs, terrain.origin(), 0.05f);
    nutrients_.resize(cells, cells, cs, terrain.origin(), 0.5f);
    fire_.resize(cells, cells, cs, terrain.origin(), 0);
    burnScar_.resize(cells, cells, cs, terrain.origin(), 0.0f);
    waterCell_.resize(cells, cells, cs, terrain.origin(), 0);
    for (int t = 0; t < kPlantTypeCount; ++t)
    {
        specs_[static_cast<size_t>(t)] = env.plants[t];
    }
    const size_t n = static_cast<size_t>(cells) * static_cast<size_t>(cells);
    biomass_.assign(n * kPlantTypeCount, 0.0f);
    capacity_.assign(n * kPlantTypeCount, 0.0f);
    const GradientNoise2D patch(hashCombine(seed, 77));
    for (int y = 0; y < cells; ++y)
    {
        for (int x = 0; x < cells; ++x)
        {
            const Vec2 c = trampling_.cellCenter(x, y);
            const Habitat hab = terrain.habitatAt(c);
            const bool water = hab == Habitat::Channel || hab == Habitat::OxbowLake || hab == Habitat::EphemeralPond;
            waterCell_.at(x, y) = water ? 1 : 0;
            const size_t ci = trampling_.index(x, y);
            for (int t = 0; t < kPlantTypeCount; ++t)
            {
                const PlantTypeSpec& sp = specs_[static_cast<size_t>(t)];
                if (!sp.present)
                {
                    continue;
                }
                const PlantType pt = static_cast<PlantType>(t);
                if (water != (pt == PlantType::AquaticMacrophyte))
                {
                    continue;
                }
                // Patchiness at ~100 m scale, independent per plant type.
                const float noiseV = patch.fbm(c.x / 110.0f + static_cast<float>(t) * 17.3f, c.y / 110.0f - static_cast<float>(t) * 9.1f, 3);
                const float k = sp.maxBiomassKgM2 * sp.habitat[static_cast<int>(hab)] * clampf(0.65f + 0.7f * noiseV, 0.1f, 1.3f);
                capacity_[ci * kPlantTypeCount + static_cast<size_t>(t)] = std::max(0.0f, k);
                biomass_[ci * kPlantTypeCount + static_cast<size_t>(t)] = std::max(0.0f, k) * 0.75f;
            }
        }
    }
}

void VegetationSystem::update(float dtDays, const VegetationClimate& climate, const Terrain& terrain, JobScheduler& jobs)
{
    const int w = trampling_.width();
    const int h = trampling_.height();
    const float fT = vegimpl::temperatureFactor(climate.airTempC);
    float treeMaxSum = 0.0f;
    for (int t = 0; t < kPlantTypeCount; ++t)
    {
        if (specs_[static_cast<size_t>(t)].present && vegimpl::isTree(static_cast<PlantType>(t)))
        {
            treeMaxSum += specs_[static_cast<size_t>(t)].maxBiomassKgM2;
        }
    }
    treeMaxSum = std::max(treeMaxSum, 0.1f);
    jobs.parallelFor(static_cast<size_t>(h), [&](size_t row) {
        const int y = static_cast<int>(row);
        for (int x = 0; x < w; ++x)
        {
            const size_t ci = trampling_.index(x, y);
            const Vec2 c = trampling_.cellCenter(x, y);
            const float m = terrain.moistureAt(c);
            const bool water = waterCell_[ci] != 0;
            float* b = &biomass_[ci * kPlantTypeCount];
            const float* k = &capacity_[ci * kPlantTypeCount];
            float canopy = 0.0f;
            for (int t = 0; t < kPlantTypeCount; ++t)
            {
                if (vegimpl::isTree(static_cast<PlantType>(t)))
                {
                    canopy += b[t];
                }
            }
            canopy = saturate(canopy / treeMaxSum);
            const float fN = 0.6f + 0.4f * nutrients_[ci];
            const float fM = water ? 1.0f : saturate(m / 0.45f) * (1.0f - 0.5f * smoothstep(0.92f, 1.0f, m));
            float litterIn = 0.0f;
            for (int t = 0; t < kPlantTypeCount; ++t)
            {
                if (k[t] <= 0.0f)
                {
                    continue;
                }
                const PlantType pt = static_cast<PlantType>(t);
                const PlantTypeSpec& sp = specs_[static_cast<size_t>(t)];
                const float fL = vegimpl::isTree(pt) ? 1.0f : 1.0f - 0.6f * canopy;
                const float r = sp.growthRatePerDay * fT * fM * fL * climate.seasonGrowthFactor * fN;
                float bt = b[t];
                const float growth = r * bt * (1.0f - bt / k[t]) + 0.002f * k[t] * fT * fM * climate.seasonGrowthFactor;
                const float turnover = 0.0015f * bt;
                const float trampleLoss = vegimpl::isLow(pt) || pt == PlantType::AngioShrub ? trampling_[ci] * 0.08f * bt : 0.0f;
                bt += (growth - turnover - trampleLoss) * dtDays;
                b[t] = clampf(bt, 0.0f, k[t] * 1.05f);
                litterIn += turnover * dtDays;
            }
            // Litter decomposition releases nutrients (temperature and moisture dependent).
            const float decomp = 0.012f * fT * saturate(m / 0.5f) * litter_[ci] * dtDays;
            litter_[ci] = std::max(0.0f, litter_[ci] + litterIn - decomp);
            nutrients_[ci] = clampf(nutrients_[ci] + decomp * 0.25f - 0.002f * (nutrients_[ci] - 0.5f) * dtDays, 0.0f, 1.0f);
            trampling_[ci] *= std::exp(-dtDays / 3.0f);
            trail_[ci] = std::max(0.0f, trail_[ci] - 0.008f * dtDays);
            burnScar_[ci] = std::max(0.0f, burnScar_[ci] - 0.004f * dtDays);
        }
    }, 4);
}

int VegetationSystem::updateFire(float dtHours, const VegetationClimate& climate, u64 tickSeed)
{
    const int w = fire_.width();
    const int h = fire_.height();
    std::vector<std::pair<int, int>> burning;
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            if (fire_.at(x, y) > 0)
            {
                burning.push_back({x, y});
            }
        }
    }
    if (burning.empty())
    {
        return 0;
    }
    Rng rng(tickSeed, 0xF1E);
    const bool raining = climate.rainMmPerHour > 1.5f;
    const Vec2 windDir = climate.wind.normalized();
    const float windSpeed = climate.wind.length();
    for (const auto& c : burning)
    {
        const size_t ci = fire_.index(c.first, c.second);
        float* b = &biomass_[ci * kPlantTypeCount];
        float ash = 0.0f;
        for (int t = 0; t < kPlantTypeCount; ++t)
        {
            const PlantType pt = static_cast<PlantType>(t);
            const float rate = vegimpl::isTree(pt) ? 0.08f : 0.45f;
            const float burnt = b[t] * saturate(rate * dtHours);
            b[t] -= burnt;
            ash += burnt;
        }
        litter_[ci] *= 0.3f;
        nutrients_[ci] = saturate(nutrients_[ci] + ash * 0.15f);
        burnScar_[ci] = 1.0f;
        const int left = static_cast<int>(fire_[ci]) - static_cast<int>(std::ceil(dtHours));
        fire_[ci] = static_cast<u8>(raining || left <= 0 ? 0 : left);
        if (raining)
        {
            continue;
        }
        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                const int nx = c.first + dx;
                const int ny = c.second + dy;
                if ((dx == 0 && dy == 0) || !fire_.inBounds(nx, ny) || fire_.at(nx, ny) > 0)
                {
                    continue;
                }
                const size_t ni = fire_.index(nx, ny);
                if (waterCell_[ni] != 0 || burnScar_[ni] > 0.5f)
                {
                    continue;
                }
                float fuel = litter_[ni];
                for (int t = 0; t < kPlantTypeCount; ++t)
                {
                    fuel += biomass_[ni * kPlantTypeCount + static_cast<size_t>(t)] * (vegimpl::isTree(static_cast<PlantType>(t)) ? 0.2f : 1.0f);
                }
                const Vec2 dir = Vec2{static_cast<float>(dx), static_cast<float>(dy)}.normalized();
                const float windBoost = 1.0f + 0.25f * windSpeed * std::max(0.0f, dot(dir, windDir));
                const float p = 0.12f * climate.dryness * saturate(fuel / 0.6f) * windBoost * dtHours;
                if (rng.chance(p))
                {
                    fire_.at(nx, ny) = static_cast<u8>(2 + rng.rangeInt(0, 3));
                }
            }
        }
    }
    return static_cast<int>(burning.size());
}

bool VegetationSystem::ignite(const Vec2& p, const VegetationClimate& climate)
{
    const int ci = cellIndexAt(p);
    if (ci < 0 || waterCell_[static_cast<size_t>(ci)] != 0 || climate.dryness < 0.45f)
    {
        return false;
    }
    float fuel = litter_[static_cast<size_t>(ci)];
    for (int t = 0; t < kPlantTypeCount; ++t)
    {
        fuel += biomass_[static_cast<size_t>(ci) * kPlantTypeCount + static_cast<size_t>(t)];
    }
    if (fuel < 0.25f)
    {
        return false;
    }
    fire_[static_cast<size_t>(ci)] = 3;
    return true;
}

int VegetationSystem::cellIndexAt(const Vec2& p) const
{
    if (trampling_.empty())
    {
        return -1;
    }
    int x = 0;
    int y = 0;
    trampling_.worldToCell(p, x, y);
    if (!trampling_.inBounds(x, y))
    {
        return -1;
    }
    return static_cast<int>(trampling_.index(x, y));
}

float VegetationSystem::accessibleFraction(PlantType t, float reachMin, float reachMax) const
{
    const float hTop = std::max(0.05f, specs_[static_cast<size_t>(t)].heightM);
    const float hBase = hTop * vegimpl::foliageBase(t);
    const float lo = std::max(hBase, reachMin);
    const float hi = std::min(hTop, reachMax);
    if (hi <= lo)
    {
        return 0.0f;
    }
    return (hi - lo) / std::max(0.05f, hTop - hBase);
}

float VegetationSystem::availableForage(const Vec2& p, float reachMinM, float reachMaxM, const float* preferences) const
{
    const int ci = cellIndexAt(p);
    if (ci < 0)
    {
        return 0.0f;
    }
    const float area = trampling_.cellSize() * trampling_.cellSize();
    float total = 0.0f;
    for (int t = 0; t < kPlantTypeCount; ++t)
    {
        if (preferences[t] <= 0.0f)
        {
            continue;
        }
        const float acc = accessibleFraction(static_cast<PlantType>(t), reachMinM, reachMaxM);
        total += biomass_[static_cast<size_t>(ci) * kPlantTypeCount + static_cast<size_t>(t)] * area * acc * preferences[t];
    }
    return total;
}

ForageResult VegetationSystem::consume(const Vec2& p, float reachMinM, float reachMaxM, const float* preferences, float kgWanted)
{
    ForageResult result;
    if (kgWanted <= 0.0f)
    {
        return result;
    }
    const float area = trampling_.cellSize() * trampling_.cellSize();
    int cx = 0;
    int cy = 0;
    trampling_.worldToCell(p, cx, cy);
    for (int ring = 0; ring <= 1 && result.eatenKg < kgWanted; ++ring)
    {
        for (int dy = -ring; dy <= ring && result.eatenKg < kgWanted; ++dy)
        {
            for (int dx = -ring; dx <= ring && result.eatenKg < kgWanted; ++dx)
            {
                if (ring > 0 && std::abs(dx) != ring && std::abs(dy) != ring)
                {
                    continue;
                }
                if (!trampling_.inBounds(cx + dx, cy + dy))
                {
                    continue;
                }
                const size_t ci = trampling_.index(cx + dx, cy + dy);
                float weights[kPlantTypeCount];
                float wsum = 0.0f;
                for (int t = 0; t < kPlantTypeCount; ++t)
                {
                    const float avail = biomass_[ci * kPlantTypeCount + static_cast<size_t>(t)] * area *
                                        accessibleFraction(static_cast<PlantType>(t), reachMinM, reachMaxM);
                    weights[t] = avail * std::max(0.0f, preferences[t]);
                    wsum += weights[t];
                }
                if (wsum <= 1e-6f)
                {
                    continue;
                }
                const float want = kgWanted - result.eatenKg;
                for (int t = 0; t < kPlantTypeCount; ++t)
                {
                    if (weights[t] <= 0.0f)
                    {
                        continue;
                    }
                    const float acc = accessibleFraction(static_cast<PlantType>(t), reachMinM, reachMaxM);
                    const float availKg = biomass_[ci * kPlantTypeCount + static_cast<size_t>(t)] * area * acc;
                    // Foragers leave a residue (never strip a cell to zero: regrowth stays possible).
                    const float take = std::min(availKg * 0.6f, want * weights[t] / wsum);
                    biomass_[ci * kPlantTypeCount + static_cast<size_t>(t)] -= take / area;
                    result.eatenKg += take;
                    result.byType[static_cast<size_t>(t)] += take;
                }
            }
        }
    }
    return result;
}

bool VegetationSystem::findForage(const Vec2& from, float radius, float reachMinM, float reachMaxM, const float* preferences, u64 seed,
                                  Vec2& out, float& valueKg) const
{
    Rng rng(seed, 0xF0A6E);
    float best = -1.0f;
    for (int i = 0; i < 14; ++i)
    {
        Vec2 c = from;
        if (i > 0)
        {
            const float a = rng.range(0.0f, kTwoPi);
            const float r = radius * std::sqrt(rng.uniform());
            c = from + Vec2::fromHeading(a) * r;
        }
        const int ci = cellIndexAt(c);
        if (ci < 0)
        {
            continue;
        }
        const float v = availableForage(c, reachMinM, reachMaxM, preferences) * (1.0f - 0.5f * trampling_[static_cast<size_t>(ci)]);
        const float score = v / (1.0f + distance(c, from) / std::max(1.0f, radius));
        if (score > best)
        {
            best = score;
            out = c;
            valueKg = v;
        }
    }
    return best > 0.0f;
}

void VegetationSystem::trample(const Vec2& p, float bodyMassKg)
{
    const int ci = cellIndexAt(p);
    if (ci < 0)
    {
        return;
    }
    const float m = std::max(0.0f, bodyMassKg);
    // Heavier animals damage more per step; saturating.
    trampling_[static_cast<size_t>(ci)] = saturate(trampling_[static_cast<size_t>(ci)] + 0.0004f * std::sqrt(m));
    trail_[static_cast<size_t>(ci)] = saturate(trail_[static_cast<size_t>(ci)] + 0.00006f * std::sqrt(m));
}

void VegetationSystem::addNutrients(const Vec2& p, float organicKg)
{
    const int ci = cellIndexAt(p);
    if (ci < 0)
    {
        return;
    }
    const float area = trampling_.cellSize() * trampling_.cellSize();
    litter_[static_cast<size_t>(ci)] += organicKg / area;
}

float VegetationSystem::opticalDensity(const Vec2& p, float heightAboveGroundM) const
{
    const int ci = cellIndexAt(p);
    if (ci < 0)
    {
        return 0.0f;
    }
    float d = 0.0f;
    for (int t = 0; t < kPlantTypeCount; ++t)
    {
        const PlantTypeSpec& sp = specs_[static_cast<size_t>(t)];
        if (!sp.present || sp.maxBiomassKgM2 <= 0.0f)
        {
            continue;
        }
        const PlantType pt = static_cast<PlantType>(t);
        const float density = biomass_[static_cast<size_t>(ci) * kPlantTypeCount + static_cast<size_t>(t)] / sp.maxBiomassKgM2;
        const float base = sp.heightM * vegimpl::foliageBase(pt);
        if (heightAboveGroundM <= sp.heightM && heightAboveGroundM >= base)
        {
            d += density * vegimpl::extinction(pt);
        }
        else if (vegimpl::isTree(pt) && heightAboveGroundM < base)
        {
            d += density * 0.02f; // trunks
        }
    }
    return d;
}

float VegetationSystem::transmittance(const Vec2& from, const Vec2& to, float heightAboveGroundM) const
{
    const float len = distance(from, to);
    if (len < 2.0f)
    {
        return 1.0f;
    }
    const float step = trampling_.cellSize() * 0.5f;
    const int n = std::max(1, static_cast<int>(len / step));
    float tau = 0.0f;
    for (int i = 1; i < n; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        tau += opticalDensity(lerp(from, to, t), heightAboveGroundM) * (len / static_cast<float>(n));
        if (tau > 8.0f)
        {
            break;
        }
    }
    return std::exp(-tau);
}

float VegetationSystem::cover(const Vec2& p) const
{
    return saturate(opticalDensity(p, 0.5f) * 2.0f);
}

float VegetationSystem::canopyCover(const Vec2& p) const
{
    const int ci = cellIndexAt(p);
    if (ci < 0)
    {
        return 0.0f;
    }
    float c = 0.0f;
    float maxSum = 0.0f;
    for (int t = 0; t < kPlantTypeCount; ++t)
    {
        if (vegimpl::isTree(static_cast<PlantType>(t)) && specs_[static_cast<size_t>(t)].present)
        {
            c += biomass_[static_cast<size_t>(ci) * kPlantTypeCount + static_cast<size_t>(t)];
            maxSum += specs_[static_cast<size_t>(t)].maxBiomassKgM2;
        }
    }
    return maxSum > 0.0f ? saturate(c / maxSum * 1.5f) : 0.0f;
}

float VegetationSystem::trail(const Vec2& p) const
{
    const int ci = cellIndexAt(p);
    return ci < 0 ? 0.0f : trail_[static_cast<size_t>(ci)];
}

bool VegetationSystem::burning(const Vec2& p) const
{
    const int ci = cellIndexAt(p);
    return ci >= 0 && fire_[static_cast<size_t>(ci)] > 0;
}

std::vector<float> VegetationSystem::densityMap(PlantType t) const
{
    std::vector<float> out(trampling_.cellCount(), 0.0f);
    const PlantTypeSpec& sp = specs_[static_cast<size_t>(t)];
    if (!sp.present || sp.maxBiomassKgM2 <= 0.0f)
    {
        return out;
    }
    for (size_t i = 0; i < out.size(); ++i)
    {
        out[i] = saturate(biomass_[i * kPlantTypeCount + static_cast<size_t>(t)] / sp.maxBiomassKgM2);
    }
    return out;
}

double VegetationSystem::totalBiomassKg() const
{
    double sum = 0.0;
    for (const float b : biomass_)
    {
        sum += b;
    }
    return sum * static_cast<double>(trampling_.cellSize()) * static_cast<double>(trampling_.cellSize());
}

double VegetationSystem::totalBiomassKg(PlantType t) const
{
    double sum = 0.0;
    for (size_t i = static_cast<size_t>(t); i < biomass_.size(); i += kPlantTypeCount)
    {
        sum += biomass_[i];
    }
    return sum * static_cast<double>(trampling_.cellSize()) * static_cast<double>(trampling_.cellSize());
}

std::vector<float> VegetationSystem::saveState() const
{
    std::vector<float> s;
    s.reserve(biomass_.size() + trampling_.cellCount() * 4);
    s.insert(s.end(), biomass_.begin(), biomass_.end());
    s.insert(s.end(), trail_.raw().begin(), trail_.raw().end());
    s.insert(s.end(), nutrients_.raw().begin(), nutrients_.raw().end());
    s.insert(s.end(), litter_.raw().begin(), litter_.raw().end());
    s.insert(s.end(), burnScar_.raw().begin(), burnScar_.raw().end());
    return s;
}

void VegetationSystem::loadState(const std::vector<float>& s)
{
    const size_t cells = trampling_.cellCount();
    if (s.size() != biomass_.size() + cells * 4)
    {
        return;
    }
    size_t o = 0;
    std::copy(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(biomass_.size()), biomass_.begin());
    o += biomass_.size();
    std::copy(s.begin() + static_cast<std::ptrdiff_t>(o), s.begin() + static_cast<std::ptrdiff_t>(o + cells), trail_.raw().begin());
    o += cells;
    std::copy(s.begin() + static_cast<std::ptrdiff_t>(o), s.begin() + static_cast<std::ptrdiff_t>(o + cells), nutrients_.raw().begin());
    o += cells;
    std::copy(s.begin() + static_cast<std::ptrdiff_t>(o), s.begin() + static_cast<std::ptrdiff_t>(o + cells), litter_.raw().begin());
    o += cells;
    std::copy(s.begin() + static_cast<std::ptrdiff_t>(o), s.begin() + static_cast<std::ptrdiff_t>(o + cells), burnScar_.raw().begin());
}
} // namespace noctis
