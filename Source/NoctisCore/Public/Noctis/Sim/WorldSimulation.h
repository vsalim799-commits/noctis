// WorldSimulation — owns and schedules every system. One instance = one living valley.
//
//   step(dt): sky, season, weather -> emitters -> creatures (parallel pipeline + serial resolution)
//             -> research -> events -> sound/water housekeeping
//   hourly:   soil moisture, hydrology, carcasses, nests/hatching, track erosion, fire, migration
//   6-hourly: vegetation growth
//   daily:    growth/ageing/mortality, breeding status, population pools, census
#pragma once

#include "Noctis/Core/FileSystem.h"
#include "Noctis/Creatures/CreatureSystem.h"
#include "Noctis/Ecology/Ecology.h"
#include "Noctis/Research/ResearchSystem.h"
#include "Noctis/Sim/Snapshot.h"
#include "Noctis/World/TerrainGenerator.h"

#include <memory>
#include <string>
#include <vector>

namespace noctis
{
struct SimConfig
{
    std::string dataRoot = "Data";
    std::string environmentId = "hell_creek";
    std::string profileId = "standard";
    u64 seed = 1;
    int threads = 0;
    double startDayOfYear = 140.0;
    double startHour = 5.0;
    int heightSamples = 2017;
    int materialCells = 1024;
    float vegetationCellM = 16.0f;
    float populationScale = 1.0f;
    bool spawnPopulation = true;
    bool initialRemains = true;
};

class NOCTIS_API WorldSimulation
{
public:
    WorldSimulation();
    ~WorldSimulation();
    WorldSimulation(const WorldSimulation&) = delete;
    WorldSimulation& operator=(const WorldSimulation&) = delete;

    // fs/jobs may be null: the standard file system and a thread pool are created.
    bool initialize(const SimConfig& config, std::unique_ptr<FileSystem> fs = nullptr, std::unique_ptr<JobScheduler> jobs = nullptr);
    bool initialized() const { return initialized_; }

    // Advances simulated time by dt seconds (one step).
    void step(double dt);
    // Advances by 'seconds' using steps no larger than maxStep (headless runs, time skips).
    void advance(double seconds, double maxStep);
    // Time acceleration hint: at high acceleration, full-detail simulation is suspended.
    void setTimeScale(float scale);

    void buildSnapshot(RenderSnapshot& out);

    // Accessors.
    const SimConfig& config() const { return config_; }
    const EvidenceDatabase& db() const { return db_; }
    const EnvironmentDefinition& environment() const { return *env_; }
    const ReconstructionProfile* profile() const { return profile_; }
    const std::vector<SpeciesRuntime>& species() const { return species_; }
    const std::vector<int>& speciesInWorld() const { return speciesInWorld_; }
    const SimClock& clock() const { return clock_; }
    SimClock& clock() { return clock_; }
    Terrain& terrain() { return terrain_; }
    const Terrain& terrain() const { return terrain_; }
    const WorldLayout& layout() const { return layout_; }
    WaterSystem& water() { return water_; }
    VegetationSystem& vegetation() { return vegetation_; }
    const VegetationSystem& vegetation() const { return vegetation_; }
    WeatherSystem& weather() { return weather_; }
    const WeatherSystem& weather() const { return weather_; }
    SkySystem& sky() { return sky_; }
    SeasonSystem& season() { return season_; }
    TrackSystem& tracks() { return tracks_; }
    CarcassSystem& carcasses() { return carcasses_; }
    NestSystem& nests() { return nests_; }
    SoundField& sound() { return sound_; }
    EventBus& events() { return events_; }
    CreatureSystem& creatures() { return creatures_; }
    const CreatureSystem& creatures() const { return creatures_; }
    EcologySystem& ecology() { return ecology_; }
    const EcologySystem& ecology() const { return ecology_; }
    ResearchSystem& research() { return research_; }
    const ResearchSystem& research() const { return research_; }
    WorldContext& context() { return ctx_; }
    const FileSystem& fileSystem() const { return *fs_; }
    JobScheduler& jobs() { return *jobs_; }
    u64 tick() const { return tick_; }
    Vec2 basecamp() const { return basecamp_; }

    // Spawns the initial populations (called by initialize when config.spawnPopulation).
    void spawnInitialPopulations();
    // Finds a spawn point suited to a species (habitat preference, dry ground).
    Vec2 findHabitatPoint(int speciesIndex, Rng& rng, const Vec2* near = nullptr, float radius = 0.0f) const;

    // Persistence (JSON + binary arrays). Returns false on I/O error.
    bool save(const std::string& basePath) const;
    bool load(const std::string& basePath);

    // Profiling (ms per system, smoothed).
    struct Timings
    {
        double creatures = 0.0;
        double research = 0.0;
        double environment = 0.0;
        double hourly = 0.0;
        double daily = 0.0;
    };
    const Timings& timings() const { return timings_; }

private:
    void hourlyUpdate();
    void sixHourlyUpdate();
    void dailyUpdate();
    void handleLightning();
    void refreshContext();

    SimConfig config_;
    std::unique_ptr<FileSystem> fs_;
    std::unique_ptr<JobScheduler> jobs_;
    EvidenceDatabase db_;
    const EnvironmentDefinition* env_ = nullptr;
    const ReconstructionProfile* profile_ = nullptr;
    std::vector<SpeciesRuntime> species_;
    std::vector<int> speciesInWorld_;
    std::vector<int> agentSpecies_;
    SimClock clock_;
    Terrain terrain_;
    WorldLayout layout_;
    WaterSystem water_;
    VegetationSystem vegetation_;
    WeatherSystem weather_;
    SkySystem sky_;
    SeasonSystem season_;
    TrackSystem tracks_;
    CarcassSystem carcasses_;
    NestSystem nests_;
    SoundField sound_;
    EventBus events_;
    CreatureSystem creatures_;
    EcologySystem ecology_;
    ResearchSystem research_;
    std::vector<Emitter> emitters_;
    WorldContext ctx_;
    Rng rng_;
    u64 tick_ = 0;
    double hourAccumulator_ = 0.0;
    double sixHourAccumulator_ = 0.0;
    double rainAccumulatorMm_ = 0.0;
    double evapAccumulatorMm_ = 0.0;
    double tempAccumulator_ = 0.0;
    double tempSamples_ = 0.0;
    int lastDay_ = -1;
    float timeScale_ = 1.0f;
    double lastSnapshotTime_ = -1e9;
    u32 lastSnapshotSoundId_ = 0;
    Vec2 basecamp_;
    bool initialized_ = false;
    Timings timings_;
};
} // namespace noctis
