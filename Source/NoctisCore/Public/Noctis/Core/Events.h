// World events. Systems never call each other for side effects that other systems care about;
// they emit events. Events are collected per step (thread-local buffers during parallel phases,
// merged in a deterministic order), dispatched to listeners, and kept in a bounded chronicle
// used by the debug tools and the world history.
#pragma once

#include "Noctis/Core/Ids.h"
#include "Noctis/Core/Math.h"

#include <functional>
#include <mutex>
#include <vector>

namespace noctis
{
enum class EventType : u16
{
    Footfall,
    Vocalization,
    Attack,
    Injury,
    Death,
    Birth,
    Hatch,
    EggsLaid,
    Mating,
    CarcassCreated,
    CarcassDepleted,
    Drink,
    Feed,
    Flee,
    VigilanceRaised,
    HuntSearch,
    HuntTargetSelected,
    HuntStalk,
    HuntChase,
    HuntAbandon,
    HuntKill,
    GroupFormationChange,
    GroupSplit,
    GroupMerge,
    Display,
    Fight,
    Slip,
    WeatherChange,
    Lightning,
    FireIgnition,
    SeasonChange,
    ResearcherNoticed,
    ResearcherWarned,
    ResearcherAttacked,
    DroneNoticed,
    VehicleNoticed,
    NestBuilt,
    NestPredated,
    Immigration,
    Emigration,
    ObservationLogged,
    PhotoTaken,
    RecordingMade,
    SensorTriggered,
    ToothShed,
    Defecation,
    Count
};

NOCTIS_API const char* eventTypeName(EventType t);

struct SimEvent
{
    EventType type = EventType::Count;
    double time = 0.0;
    EntityId subject;
    EntityId other;
    Vec3 position;
    float magnitude = 0.0f; // meaning depends on type (dB, kg, severity...)
    u32 param = 0;          // meaning depends on type (call type, body region, cause...)
    u32 param2 = 0;
};

class NOCTIS_API EventBus
{
public:
    using Listener = std::function<void(const SimEvent&)>;

    explicit EventBus(size_t chronicleCapacity = 20000) : chronicleCapacity_(chronicleCapacity) {}

    // Thread-safe emission (used from parallel phases). Events become visible at flush().
    void emit(const SimEvent& e);
    // Sorts pending events deterministically, dispatches them, appends to chronicle.
    void flush();
    int subscribe(Listener l);
    void unsubscribe(int handle);

    const std::vector<SimEvent>& lastFlushed() const { return lastFlushed_; }
    // Chronicle in chronological order (oldest first).
    std::vector<SimEvent> chronicle() const;
    size_t totalEmitted() const { return totalEmitted_; }

private:
    std::mutex mutex_;
    std::vector<SimEvent> pending_;
    std::vector<SimEvent> lastFlushed_;
    std::vector<SimEvent> chronicleRing_;
    size_t chronicleHead_ = 0;
    size_t chronicleCapacity_;
    size_t totalEmitted_ = 0;
    std::vector<std::pair<int, Listener>> listeners_;
    int nextHandle_ = 1;
};
} // namespace noctis
