// Vegetation as plant-functional-type biomass on a grid.
//
// Growth is logistic towards a habitat-dependent capacity, modulated by temperature, soil
// moisture, canopy light, season and soil nutrients. Herbivores remove biomass within their
// reachable height band; trampling damages low plants and wears trails; litter and carcass
// decomposition feed the nutrient pool; fire (lightning ignition) burns dry fuel.
// The same grid answers perception queries (how much foliage blocks a line of sight) and
// is exported to Unreal to drive procedural foliage placement and interactive bending.
#pragma once

#include "Noctis/Core/Grid2D.h"
#include "Noctis/Core/Jobs.h"
#include "Noctis/Science/Environment.h"
#include "Noctis/World/Terrain.h"

#include <array>
#include <vector>

namespace noctis
{
struct ForageResult
{
    float eatenKg = 0.0f;
    std::array<float, kPlantTypeCount> byType{};
};

struct VegetationClimate
{
    float airTempC = 15.0f;
    float rainMmPerHour = 0.0f;
    float seasonGrowthFactor = 1.0f; // phenology (0 dormant .. 1 peak)
    float dryness = 0.0f;            // 0 wet .. 1 very dry (fire weather)
    Vec2 wind;
};

class NOCTIS_API VegetationSystem
{
public:
    void build(const EnvironmentSim& env, const Terrain& terrain, u64 seed, float cellSizeM = 16.0f);
    void update(float dtDays, const VegetationClimate& climate, const Terrain& terrain, JobScheduler& jobs);

    // Fire spread runs hourly when any cell burns. Returns number of burning cells.
    int updateFire(float dtHours, const VegetationClimate& climate, u64 tickSeed);
    bool ignite(const Vec2& p, const VegetationClimate& climate);

    // Edible biomass (kg) reachable by a forager at p within one cell, weighted by preferences.
    float availableForage(const Vec2& p, float reachMinM, float reachMaxM, const float* preferences) const;
    // Removes up to kgWanted from the cell at p (and its neighbours if needed). Serial phase only.
    ForageResult consume(const Vec2& p, float reachMinM, float reachMaxM, const float* preferences, float kgWanted);
    // Samples candidate cells around 'from' and returns the most rewarding foraging spot.
    bool findForage(const Vec2& from, float radius, float reachMinM, float reachMaxM, const float* preferences, u64 seed, Vec2& out,
                    float& valueKg) const;

    // Footfalls: pressure-weighted damage to low plants + trail wear. Serial phase only.
    void trample(const Vec2& p, float bodyMassKg);
    // Dung / carcass fluids / ash: adds to nutrients (kg of organic matter).
    void addNutrients(const Vec2& p, float organicKg);

    // Perception: foliage optical density (extinction per metre) at height z above ground.
    float opticalDensity(const Vec2& p, float heightAboveGroundM) const;
    // Integrated visibility (0..1) along a horizontal line at eye height.
    float transmittance(const Vec2& from, const Vec2& to, float heightAboveGroundM) const;
    // 0..1 hiding cover at ground level (for ambush predators and prey).
    float cover(const Vec2& p) const;
    float canopyCover(const Vec2& p) const;
    float trail(const Vec2& p) const;
    bool burning(const Vec2& p) const;

    float biomass(int cell, PlantType t) const { return biomass_[static_cast<size_t>(cell) * kPlantTypeCount + static_cast<size_t>(t)]; }
    float capacity(int cell, PlantType t) const { return capacity_[static_cast<size_t>(cell) * kPlantTypeCount + static_cast<size_t>(t)]; }
    int cellIndexAt(const Vec2& p) const;
    const Grid2D<float>& nutrients() const { return nutrients_; }
    const Grid2D<float>& trails() const { return trail_; }
    const Grid2D<float>& trampling() const { return trampling_; }
    const Grid2D<u8>& fireState() const { return fire_; }
    int width() const { return nutrients_.width(); }
    int height() const { return nutrients_.height(); }
    float cellSize() const { return nutrients_.cellSize(); }
    // Density 0..1 of a plant type (biomass / max biomass) per cell — exported as foliage masks.
    std::vector<float> densityMap(PlantType t) const;

    double totalBiomassKg() const;
    double totalBiomassKg(PlantType t) const;
    const PlantTypeSpec& spec(PlantType t) const { return specs_[static_cast<size_t>(t)]; }

    // Persistence (biomass, trails, nutrients, litter).
    std::vector<float> saveState() const;
    void loadState(const std::vector<float>& s);

private:
    float accessibleFraction(PlantType t, float reachMin, float reachMax) const;

    std::array<PlantTypeSpec, kPlantTypeCount> specs_{};
    std::vector<float> biomass_;  // cell * kPlantTypeCount + type, kg/m^2 (dry)
    std::vector<float> capacity_; // same layout, kg/m^2
    Grid2D<float> trampling_;     // 0..1 recent damage
    Grid2D<float> trail_;         // 0..1 trail wear
    Grid2D<float> litter_;        // kg/m^2
    Grid2D<float> nutrients_;     // 0..1 relative soil fertility
    Grid2D<u8> fire_;             // 0 none, 1..n burning hours left, 255 burnt scar marker handled by burnScar_
    Grid2D<float> burnScar_;      // 0..1 recency of burn
    Grid2D<u8> waterCell_;        // 1 if permanent water (only aquatic plants grow)
};
} // namespace noctis
