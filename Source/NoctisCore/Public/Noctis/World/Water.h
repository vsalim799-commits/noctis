// Surface hydrology: river discharge and stage, lake water balance, floods, drinking sites,
// water noise (masks animal hearing near the river) and splash disturbances for rendering.
//
// River stage follows at-a-station hydraulic geometry (depth ~ Q^0.4, Leopold & Maddock 1953);
// discharge comes from a linear-reservoir runoff model fed by the weather system. When stage
// exceeds bankfull depth the floodplain floods, connected cells first.
#pragma once

#include "Noctis/Core/Grid2D.h"
#include "Noctis/Core/SpatialHash.h"
#include "Noctis/World/TerrainGenerator.h"

#include <vector>

namespace noctis
{
struct WaterQuery
{
    bool isWater = false;
    float depth = 0.0f;     // m (0 when dry)
    float surfaceZ = 0.0f;  // absolute water surface height
    Vec2 flow;              // m/s
    int bodyId = -1;        // river index or 1000 + lake index
    bool flowing = false;
};

struct DrinkSite
{
    Vec2 position;
    int bodyId = -1;
    float slopeDeg = 0.0f;
};

struct WaterDisturbance
{
    Vec2 position;
    float strength = 0.0f; // kg·m/s scale impulse proxy
    double time = 0.0;
};

class NOCTIS_API WaterSystem
{
public:
    void build(const WorldLayout& layout, const Terrain& terrain);
    // Long-run mean precipitation rate (mm/h) of the environment: runoff is normalised by it.
    void setMeanRainRate(float mmPerHour) { meanRainRate_ = mmPerHour; }

    // Hourly hydrology. precip in mm/h over the catchment, potential evaporation in mm/h.
    void update(float dtHours, float precipMmPerHour, float evapMmPerHour);
    void rebuildSurface(const Terrain& terrain);

    WaterQuery query(const Vec2& p, const Terrain& terrain) const;
    float depthAt(const Vec2& p, const Terrain& terrain) const { return query(p, terrain).depth; }
    bool findDrinkSite(const Vec2& from, float maxRadius, DrinkSite& out) const;
    // Broadband water noise level (dB SPL) at a point — moving water masks quiet sounds.
    float noiseLevelDb(const Vec2& p) const;

    void addDisturbance(const Vec2& p, float strength, double time);
    const std::vector<WaterDisturbance>& disturbances() const { return disturbances_; }
    void pruneDisturbances(double now, double maxAgeS);

    float riverDischarge(int river = 0) const;
    float riverStage(int river = 0) const;
    float bankfullDepth(int river = 0) const;
    bool flooding() const { return flooding_; }
    float lakeDepth(int lake) const { return lakes_[static_cast<size_t>(lake)].depth; }
    size_t lakeCount() const { return lakes_.size(); }
    const WorldLayout& layout() const { return layout_; }
    const std::vector<DrinkSite>& drinkSites() const { return drinkSites_; }
    const Grid2D<float>& surface() const { return surface_; }

    // Persistence
    std::vector<float> saveState() const;
    void loadState(const std::vector<float>& state);

private:
    struct RiverState
    {
        float meanQ = 50.0f;
        float storage = 0.0f;
        float q = 50.0f;
        float stage = 2.4f;
        float bankfullDepth = 4.0f;
        float width = 50.0f;
    };
    struct LakeState
    {
        float depth = 1.5f;
        float maxDepth = 2.5f;
        bool ephemeral = false;
    };
    // Per material cell: which body governs its water level.
    enum class CellKind : u8
    {
        Dry,
        River,
        Lake,
        Floodable
    };

    WorldLayout layout_;
    std::vector<RiverState> rivers_;
    std::vector<LakeState> lakes_;
    Grid2D<u8> cellKind_;
    Grid2D<int> cellRef_;      // river point index or lake index
    Grid2D<u8> cellRiver_;     // river index for River/Floodable cells
    Grid2D<float> surface_;    // absolute water surface; very low when dry
    Grid2D<float> riverDist_;  // distance to the main river centreline
    std::vector<DrinkSite> drinkSites_;
    SpatialHash drinkIndex_;
    std::vector<WaterDisturbance> disturbances_;
    bool flooding_ = false;
    float meanRainRate_ = 0.12f;
};
} // namespace noctis
