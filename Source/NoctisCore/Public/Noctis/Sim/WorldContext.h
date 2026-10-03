// Read/write access to every world system for one simulation step.
// Systems receive this instead of reaching into globals, which keeps dependencies explicit
// and lets the headless runner, the tests and Unreal assemble the same world.
#pragma once

#include "Noctis/Core/Events.h"
#include "Noctis/Core/Jobs.h"
#include "Noctis/Core/Time.h"
#include "Noctis/Science/EvidenceDatabase.h"
#include "Noctis/World/Atmosphere.h"
#include "Noctis/World/SoundField.h"
#include "Noctis/World/Terrain.h"
#include "Noctis/World/Traces.h"
#include "Noctis/World/Vegetation.h"
#include "Noctis/World/Water.h"

#include <vector>

namespace noctis
{
struct SpeciesRuntime;

// Food resources that exist as populations rather than individuals.
enum class PopulationResource : u8
{
    Fish,
    Invertebrates,
    SmallVertebrates
};

class PopulationHarvester
{
public:
    virtual ~PopulationHarvester() = default;
    // Returns kg actually obtained at p (serial phase only).
    virtual float harvest(PopulationResource resource, const Vec2& p, float kgWanted, u64 seed) = 0;
    // Relative availability 0..1 at p (read-only, safe in parallel phases).
    virtual float availability(PopulationResource resource, const Vec2& p) const = 0;
};

// Anything an animal can perceive that is not another animal: the researcher, the rover, the drone,
// passive sensors. Emitters are refreshed every step by the research layer.
struct Emitter
{
    EntityId id;
    Vec3 position;
    Vec2 velocity;
    float heading = 0.0f;
    float heightM = 1.8f;        // visual size
    float massKg = 80.0f;
    float noiseDb = 30.0f;       // SPL at 1 m right now
    float noiseHz = 500.0f;
    float scent = 1.0f;          // relative odour emission
    bool lightsOn = false;
    bool crouched = false;
    bool airborne = false;
    bool active = true;
    bool observer = true;        // drives simulation LOD around it
    float observerRadiusM = 350.0f;
};

struct WorldContext
{
    double now = 0.0;
    float dt = 0.1f;
    u64 tick = 0;
    u64 seed = 0;
    float timeScale = 1.0f;
    const EvidenceDatabase* db = nullptr;
    const ReconstructionProfile* profile = nullptr;
    const EnvironmentDefinition* environment = nullptr;
    const std::vector<SpeciesRuntime>* species = nullptr;
    Terrain* terrain = nullptr;
    WaterSystem* water = nullptr;
    VegetationSystem* vegetation = nullptr;
    WeatherSystem* weather = nullptr;
    SkySystem* sky = nullptr;
    SeasonSystem* season = nullptr;
    TrackSystem* tracks = nullptr;
    CarcassSystem* carcasses = nullptr;
    NestSystem* nests = nullptr;
    SoundField* sound = nullptr;
    EventBus* events = nullptr;
    JobScheduler* jobs = nullptr;
    const SimClock* clock = nullptr;
    std::vector<Emitter>* emitters = nullptr;
    PopulationHarvester* harvester = nullptr;
    float valleyFloorZ = 0.0f;
    float insectActivity = 1.0f; // from the population layer: carrion insects speed up decomposition

    bool toggle(const char* id) const { return profile != nullptr && profile->enabled(id); }
    PropagationEnv propagation() const
    {
        PropagationEnv e;
        if (weather)
        {
            e.temperatureC = weather->state().temperatureC;
            e.humidity = weather->state().humidity;
            e.wind = weather->state().wind;
        }
        e.vegetation = vegetation;
        e.terrain = terrain;
        return e;
    }
};
} // namespace noctis
