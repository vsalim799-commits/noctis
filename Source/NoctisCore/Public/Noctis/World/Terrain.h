// Terrain: heightfield, substrate, habitat map, soil moisture and soil mechanics.
//
// Soil mechanics use Bekker's pressure–sinkage relation p = (kc / b + kphi) * z^n, the standard
// terramechanics model also used for the research vehicle's wheels, so a 7-tonne theropod, a
// 1-tonne hadrosaur and the 6x6 rover all sink into the same wet clay by the same physics.
#pragma once

#include "Noctis/Core/Grid2D.h"
#include "Noctis/Science/Taxonomy.h"

#include <vector>

namespace noctis
{
enum class Substrate : u8
{
    Rock,
    Gravel,
    Sand,
    Silt,
    Mud,
    Clay,
    ForestSoil,
    Peat,
    Count
};
inline constexpr int kSubstrateCount = static_cast<int>(Substrate::Count);

NOCTIS_API const char* substrateKey(Substrate s);
NOCTIS_API const char* substrateLabelFr(Substrate s);

// Bekker parameters for one substrate at a given moisture state.
struct SoilMechanics
{
    float n = 1.0f;        // sinkage exponent
    float kc = 1.0f;       // cohesive modulus  kN / m^(n+1)
    float kphi = 1000.0f;  // frictional modulus kN / m^(n+2)
    float cohesionKPa = 1.0f;
    float frictionAngleDeg = 30.0f;
    float tractionMu = 0.6f;       // effective traction coefficient for feet/tyres
    float rollingResistance = 0.05f;
    float trackRetention = 0.5f;   // 0..1: how crisp footprints stay (cohesive, moist sediments best)
    float maxSinkageM = 0.3f;      // thickness of the yielding layer above firmer sediment
};

NOCTIS_API SoilMechanics soilMechanics(Substrate s, float moisture01);

struct TerrainQuery
{
    float height = 0.0f;
    Vec3 normal{0.0f, 0.0f, 1.0f};
    float slopeDeg = 0.0f;
    Substrate substrate = Substrate::Silt;
    Habitat habitat = Habitat::FloodplainOpen;
    float moisture = 0.5f;
};

class NOCTIS_API Terrain
{
public:
    // Height samples are vertex-based: sample (i, j) sits at origin + (i, j) * spacing.
    void create(int samplesPerSide, float sizeM, Vec2 origin, int materialCellsPerSide);

    int samplesPerSide() const { return samples_; }
    float spacing() const { return spacing_; }
    float size() const { return size_; }
    Vec2 origin() const { return origin_; }
    Rect2 bounds() const { return {origin_, origin_ + Vec2{size_, size_}}; }
    bool contains(const Vec2& p) const { return bounds().contains(p); }
    Vec2 clampToBounds(const Vec2& p, float margin = 1.0f) const;

    float heightAt(const Vec2& p) const;
    Vec3 normalAt(const Vec2& p) const;
    float slopeDegAt(const Vec2& p) const;
    Substrate substrateAt(const Vec2& p) const;
    Habitat habitatAt(const Vec2& p) const;
    float moistureAt(const Vec2& p) const;
    TerrainQuery query(const Vec2& p) const;

    // Footprint/wheel sinkage (m) for a contact patch of given pressure (kPa) and smaller dimension (m).
    float sinkage(const Vec2& p, float pressureKPa, float contactWidthM) const;

    // Line of sight over the heightfield (vegetation is handled by the perception model).
    // Returns true when unobstructed. Heights are absolute (m).
    bool lineOfSight(const Vec3& from, const Vec3& to, float stepM = 0.0f) const;

    // Soil moisture dynamics (hourly): rain in, evaporation out, wet habitats stay wet.
    void updateMoisture(float dtHours, float rainMmPerHour, float potentialEvapMmPerHour);

    // Raw grids (exported to Unreal as heightmap / weightmaps).
    Grid2D<float>& heights() { return height_; }
    const Grid2D<float>& heights() const { return height_; }
    Grid2D<u8>& substrates() { return substrate_; }
    const Grid2D<u8>& substrates() const { return substrate_; }
    Grid2D<u8>& habitats() { return habitat_; }
    const Grid2D<u8>& habitats() const { return habitat_; }
    Grid2D<float>& moisture() { return moisture_; }
    const Grid2D<float>& moisture() const { return moisture_; }
    Grid2D<float>& baseWetness() { return baseWetness_; }
    const Grid2D<float>& baseWetness() const { return baseWetness_; }

    float minHeight() const;
    float maxHeight() const;

private:
    float sampleVertex(int i, int j) const { return height_.atClamped(i, j); }

    int samples_ = 0;
    float spacing_ = 1.0f;
    float size_ = 0.0f;
    Vec2 origin_;
    Grid2D<float> height_;      // vertex samples
    Grid2D<u8> substrate_;      // material cells (cell-centred)
    Grid2D<u8> habitat_;        // material cells
    Grid2D<float> moisture_;    // material cells, 0..1 dynamic
    Grid2D<float> baseWetness_; // material cells, 0..1 static wetness index (groundwater proximity)
};
} // namespace noctis
