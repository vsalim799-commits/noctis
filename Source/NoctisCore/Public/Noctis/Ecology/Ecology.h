// EcologySystem / PopulationSystem — populations that are not simulated as individuals
// (fish, insects, small vertebrate assemblages, birds, molluscs), coupled to the individual-based
// layer through the food web:
//   vegetation -> insects (herbivory) -> birds / small vertebrates / insectivores
//   water bodies -> fish -> crocodyliforms, champsosaurs, wading predators
//   carcasses -> carrion insects -> faster decomposition -> nutrients -> vegetation
// and the per-species census time series used by the debug tools and the player's reports.
#pragma once

#include "Noctis/Creatures/CreatureSystem.h"
#include "Noctis/Sim/WorldContext.h"

#include <string>
#include <vector>

namespace noctis
{
enum class PopulationGuild : u8
{
    Fish,
    Insects,
    SmallVertebrates,
    Birds,
    Molluscs,
    Other
};
NOCTIS_API const char* guildLabelFr(PopulationGuild g);

struct PopulationPool
{
    int speciesIndex = -1;
    PopulationGuild guild = PopulationGuild::Other;
    double abundance = 0.0;      // individuals in the valley
    double capacity = 0.0;       // current carrying capacity
    double baseCapacity = 0.0;   // nominal capacity from the species file
    float individualMassKg = 0.01f;
    float growthPerDay = 0.01f;  // intrinsic rate (allometric, game assumption)
    double harvestedKg = 0.0;    // eaten by individual-based predators (cumulative)
    double diedOff = 0.0;        // die-offs (e.g. fish in drying ponds)
};

struct CensusSample
{
    double time = 0.0;
    std::vector<int> agents;     // per species (agent role)
    std::vector<double> pools;   // per species (population role)
    double vegetationKg = 0.0;
    float temperatureC = 0.0f;
    float precipMm = 0.0f;
    int carcasses = 0;
    int nests = 0;
    int footprints = 0;
};

class NOCTIS_API EcologySystem final : public PopulationHarvester
{
public:
    void build(const EvidenceDatabase& db, const std::vector<int>& speciesInWorld, const Terrain& terrain, const WaterSystem& water, u64 seed);
    // Daily population dynamics and trophic coupling.
    void dailyUpdate(WorldContext& ctx, const CreatureSystem& creatures);
    void sampleCensus(const WorldContext& ctx, const CreatureSystem& creatures);

    float harvest(PopulationResource resource, const Vec2& p, float kgWanted, u64 seed) override;
    float availability(PopulationResource resource, const Vec2& p) const override;

    float insectActivity() const { return insectActivity_; }
    // Relative intensity 0..1 of insect and bird soundscapes (drives ambient audio; see docs for status).
    float insectChorus(bool night) const;
    float birdActivity(float sunElevationDeg) const;

    const std::vector<PopulationPool>& pools() const { return pools_; }
    std::vector<PopulationPool>& mutablePools() { return pools_; }
    const std::vector<CensusSample>& history() const { return history_; }
    void restoreHistory(std::vector<CensusSample> h) { history_ = std::move(h); }

private:
    PopulationPool* poolFor(PopulationGuild g);
    const PopulationPool* poolFor(PopulationGuild g) const;
    std::vector<PopulationPool> pools_;
    std::vector<CensusSample> history_;
    const WaterSystem* water_ = nullptr;
    const Terrain* terrain_ = nullptr;
    float insectActivity_ = 1.0f;
    double baseWaterVolume_ = 1.0;
    double baseVegetation_ = 1.0;
    Rng rng_;
};
} // namespace noctis
