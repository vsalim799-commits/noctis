// CreatureSystem — the AnimalSystem: owns every individual, runs the per-step pipeline
//   physiology -> perception -> decision -> locomotion (parallel, per individual)
//   -> interactions, feeding, reproduction, deaths, births, groups (serial, deterministic order)
// with level-of-detail scheduling so the valley stays alive everywhere at bounded cost.
#pragma once

#include "Noctis/Core/SpatialHash.h"
#include "Noctis/Creatures/SpeciesRuntime.h"
#include "Noctis/Sim/WorldContext.h"

#include <unordered_map>
#include <vector>

namespace noctis
{
enum class Formation : u8
{
    Dispersed,
    Loose,
    Compact,
    Fleeing
};
NOCTIS_API const char* formationLabelFr(Formation f);

struct Group
{
    u32 id = 0;
    i16 speciesIndex = -1;
    std::vector<EntityId> members;
    Vec2 centroid;
    Vec2 velocity;
    float meanNeighbourDistance = 0.0f;
    float spread = 0.0f;
    float alert = 0.0f;
    Formation formation = Formation::Loose;
    double formationSince = 0.0;
    EntityId leader;
    Vec2 destination;
    bool hasDestination = false;
};

// Minimal record of a dead individual (genealogy, reports, population statistics).
struct DeathRecord
{
    EntityId id;
    i16 speciesIndex = -1;
    Sex sex = Sex::Female;
    double birthTime = 0.0;
    double deathTime = 0.0;
    DeathCause cause = DeathCause::Unknown;
    EntityId mother;
    EntityId father;
    float massKg = 0.0f;
    u32 generation = 0;
    int scars = 0;
    Vec2 position;
};

struct LodSettings
{
    float fullRadiusM = 350.0f;
    float nearRadiusM = 1500.0f;
    float perceptionFullS = 0.25f;
    float perceptionNearS = 1.0f;
    float perceptionFarS = 6.0f;
    float decisionFullS = 0.5f;
    float decisionNearS = 1.5f;
    float decisionFarS = 8.0f;
    float locomotionNearS = 0.5f;
    float locomotionFarS = 4.0f;
    bool allowFull = true; // disabled at high time acceleration
};

struct SpeciesCensus
{
    int alive = 0;
    int juveniles = 0;
    int adults = 0;
    int females = 0;
    int regional = 0;
    int births = 0;
    int deaths = 0;
    int deathsByCause[11] = {};
    int immigrants = 0;
    int emigrants = 0;
    double biomassKg = 0.0;
};

class NOCTIS_API CreatureSystem
{
public:
    void initialize(const std::vector<SpeciesRuntime>* species, const Rect2& bounds, u64 seed);
    // Species that belong to this ecosystem (biogeographic coherence: only these can immigrate).
    void setWorldSpecies(std::vector<int> speciesIndices) { worldSpecies_ = std::move(speciesIndices); }
    const std::vector<int>& worldSpecies() const { return worldSpecies_; }

    // Spawns an individual (initial population, immigration, hatching, debug).
    Creature& spawn(int speciesIndex, Sex sex, float ageYears, const Vec2& position, WorldContext& ctx, u32 groupId = 0,
                    EntityId mother = kNoEntity, EntityId father = kNoEntity, u64 geneticSeed = 0);
    u32 createGroup(int speciesIndex);

    // Full per-step pipeline.
    void step(WorldContext& ctx);
    // Once per simulated day: growth, ageing/mortality hazard, breeding status, regional pool.
    void dailyUpdate(WorldContext& ctx);
    // Hourly: immigration/emigration exchanges with the regional population.
    void hourlyUpdate(WorldContext& ctx);

    Creature* find(EntityId id);
    const Creature* find(EntityId id) const;
    std::vector<Creature>& creatures() { return creatures_; }
    const std::vector<Creature>& creatures() const { return creatures_; }
    std::vector<Group>& groups() { return groups_; }
    const std::vector<Group>& groups() const { return groups_; }
    Group* findGroup(u32 id);
    const Group* findGroup(u32 id) const;
    const std::vector<DeathRecord>& deaths() const { return deaths_; }
    const std::vector<Creature>& regionalPool() const { return regional_; }

    // Indices into creatures() of living, spatially simulated individuals near p.
    void neighbours(const Vec2& p, float radius, std::vector<u32>& out) const;
    const SpatialHash& index() const { return index_; }

    // Hatchlings produced by the nest system; spawned during the next step.
    void queueHatchlings(std::vector<HatchRecord>&& records)
    {
        for (HatchRecord& r : records)
        {
            pendingHatch_.push_back(r);
        }
    }

    void applyInjury(Creature& c, const Injury& injury, WorldContext& ctx);
    void kill(Creature& c, DeathCause cause, WorldContext& ctx);

    SpeciesCensus census(int speciesIndex, double now) const;
    size_t aliveCount() const;
    LodSettings& lod() { return lod_; }
    const LodSettings& lod() const { return lod_; }
    const SpeciesRuntime& speciesOf(const Creature& c) const { return (*species_)[static_cast<size_t>(c.speciesIndex)]; }
    const std::vector<SpeciesRuntime>& species() const { return *species_; }

    // Persistence helpers.
    void clearAll();
    void restoreCreature(Creature c);
    void restoreGroup(Group g) { groups_.push_back(std::move(g)); }
    void restoreDeath(const DeathRecord& d) { deaths_.push_back(d); }
    void restoreRegional(Creature c) { regional_.push_back(std::move(c)); }
    u32 nextCreatureSerial() const { return nextSerial_; }
    u32 nextGroupId() const { return nextGroupId_; }
    void restoreCounters(u32 creatureSerial, u32 groupId) { nextSerial_ = creatureSerial; nextGroupId_ = groupId; }
    void rebuildIndex();

    int birthsTotal(int speciesIndex) const;
    int immigrantsTotal(int speciesIndex) const;
    int emigrantsTotal(int speciesIndex) const;

private:
    void assignLod(WorldContext& ctx);
    void updateGroups(WorldContext& ctx);
    void resolveIntents(WorldContext& ctx);
    void resolveContacts(WorldContext& ctx);
    void drainFootprints(WorldContext& ctx);
    void handleDeaths(WorldContext& ctx);
    void handleHatching(WorldContext& ctx);
    void emitCall(Creature& c, CallContext context, WorldContext& ctx);
    void resolveAttack(Creature& attacker, Creature& target, WorldContext& ctx);
    void attackEmitter(Creature& attacker, Emitter& target, WorldContext& ctx);
    void emigrate(Creature& c, WorldContext& ctx);
    void immigrate(int speciesIndex, WorldContext& ctx);

    const std::vector<SpeciesRuntime>* species_ = nullptr;
    Rect2 bounds_{};
    u64 seed_ = 0;
    Rng rng_;
    std::vector<Creature> creatures_;
    std::unordered_map<u32, u32> indexById_;
    std::vector<Group> groups_;
    std::vector<DeathRecord> deaths_;
    std::vector<Creature> regional_;
    SpatialHash index_;
    LodSettings lod_;
    u32 nextSerial_ = 1;
    u32 nextGroupId_ = 1;
    double lastLodAssign_ = -1e9;
    std::vector<int> births_;
    std::vector<int> immigrants_;
    std::vector<int> emigrants_;
    std::vector<int> initialCounts_;
    std::vector<HatchRecord> pendingHatch_;
    std::vector<int> worldSpecies_;
};
} // namespace noctis
