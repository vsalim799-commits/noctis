// MapSystem — the player's scientific map. It contains only what the player's own data revealed:
// explored ground (actual viewsheds), observation effort, discovered water, nests, carcasses,
// fossil exposures, trackways, sightings of catalogued individuals, danger, events and sensors.
// Territories are estimated from sightings (minimum convex polygon), densities are normalised by
// effort (sightings per hour of observation), never read from the simulation's ground truth.
#pragma once

#include "Noctis/World/Terrain.h"

#include <map>
#include <string>
#include <vector>

namespace noctis
{
enum class MapFeatureKind : u8
{
    Water,
    Nest,
    Carcass,
    FossilSite,
    Trackway,
    Trail,
    Sighting,
    Danger,
    Event,
    Sensor,
    Vehicle,
    Note
};
NOCTIS_API const char* mapFeatureLabelFr(MapFeatureKind k);

struct MapFeature
{
    u32 id = 0;
    MapFeatureKind kind = MapFeatureKind::Note;
    Vec2 position;
    double time = 0.0;
    std::string label;
    u32 ref = 0;
    i16 speciesIndex = -1;
    float value = 0.0f;
};

struct Sighting
{
    Vec2 position;
    double time = 0.0;
    u32 observationId = 0;
};

struct TerritoryEstimate
{
    bool valid = false;
    int points = 0;
    float mcpAreaKm2 = 0.0f;
    Vec2 centroid;
    float radius95M = 0.0f;
    std::vector<Vec2> hull;
};

class NOCTIS_API FieldMap
{
public:
    void configure(const Rect2& bounds, float cellSizeM = 64.0f);
    // Marks cells visible from 'eye' within radius (sampled viewshed over the heightfield) and adds effort.
    void reveal(const Vec3& eye, float radiusM, const Terrain& terrain, double time, float effortSeconds);
    bool explored(const Vec2& p) const;
    float exploredFraction() const;
    u32 addFeature(MapFeatureKind kind, const Vec2& pos, double time, const std::string& label, u32 ref = 0, i16 species = -1, float value = 0.0f,
                   float dedupeRadiusM = 25.0f);
    void addSighting(u32 catalogId, i16 species, const Vec2& pos, double time, u32 observationId);
    void addSpeciesSighting(i16 species, const Vec2& pos);
    TerritoryEstimate territory(u32 catalogId) const;
    // Sightings per hour of effort in each cell for a species (NaN-free: 0 where no effort).
    std::vector<float> encounterRate(i16 species) const;

    const std::vector<MapFeature>& features() const { return features_; }
    std::vector<MapFeature>& mutableFeatures() { return features_; }
    const std::vector<u8>& exploredCells() const { return explored_; }
    std::vector<u8>& mutableExplored() { return explored_; }
    const std::vector<float>& effort() const { return effortS_; }
    std::vector<float>& mutableEffort() { return effortS_; }
    const std::map<u32, std::vector<Sighting>>& sightings() const { return sightings_; }
    std::map<u32, std::vector<Sighting>>& mutableSightings() { return sightings_; }
    std::map<int, std::vector<int>>& mutableSpeciesCounts() { return speciesCounts_; }
    const std::map<int, std::vector<int>>& speciesCounts() const { return speciesCounts_; }
    int width() const { return w_; }
    int height() const { return h_; }
    float cellSize() const { return cell_; }
    Rect2 bounds() const { return bounds_; }
    u32 nextFeatureId() const { return nextId_; }
    void restoreNextFeatureId(u32 v) { nextId_ = v; }

private:
    int cellOf(const Vec2& p) const;
    Rect2 bounds_{};
    float cell_ = 64.0f;
    int w_ = 0;
    int h_ = 0;
    std::vector<u8> explored_;
    std::vector<float> effortS_;
    std::vector<MapFeature> features_;
    std::map<u32, std::vector<Sighting>> sightings_;
    std::map<int, std::vector<int>> speciesCounts_; // per species: sightings per cell
    u32 nextId_ = 1;
};
} // namespace noctis
