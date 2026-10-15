// Shared helpers for simulation-level tests: reduced world configuration, state hashing,
// event recording and finiteness checks.
#pragma once

#include "Noctis/Sim/WorldSimulation.h"

#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace noctistest
{
// FNV-1a (64-bit, standard offset basis) over raw bytes. Floats are hashed by bit pattern, so
// two runs only hash equal when they are bit-identical.
struct Hasher
{
    noctis::u64 h = 14695981039346656037ull;
    void bytes(const void* p, size_t n)
    {
        const unsigned char* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i)
        {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    }
    void u(noctis::u64 v) { bytes(&v, sizeof(v)); }
    void i(long long v) { bytes(&v, sizeof(v)); }
    void f(float v)
    {
        noctis::u32 bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        bytes(&bits, sizeof(bits));
    }
    void d(double v)
    {
        noctis::u64 bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        bytes(&bits, sizeof(bits));
    }
    void v3(const noctis::Vec3& v)
    {
        f(v.x);
        f(v.y);
        f(v.z);
    }
};

// Reduced world used by simulation-level tests: same map size and data as the game, coarser
// grids (513 height samples, 256 material cells, 32 m vegetation cells) and a fraction of the
// initial populations. Initialises in ~0.3 s instead of ~3 s.
noctis::SimConfig smallWorldConfig(noctis::u64 seed, int threads, float populationScale = 0.3f);

// Creates and initialises a world; returns nullptr (and records a test failure) on error.
std::unique_ptr<noctis::WorldSimulation> makeWorld(const noctis::SimConfig& cfg);

// Hash of the observable world state: every creature (alive or dead) in storage order, census,
// carcasses, nests, footprints, weather, vegetation, population pools, clock.
noctis::u64 hashWorldState(noctis::WorldSimulation& w);

// Per-species census summary as text (used in failure messages and notes).
std::string censusLine(const noctis::WorldSimulation& w);

// Total of agent individuals alive per species index (only species in the world).
std::vector<int> aliveBySpecies(const noctis::WorldSimulation& w);

// Records every dispatched event: count per type, ordered hash, per-emitter vocalisation counts,
// and the first non-finite event seen.
class EventRecorder
{
public:
    explicit EventRecorder(noctis::WorldSimulation& w);
    ~EventRecorder();
    EventRecorder(const EventRecorder&) = delete;
    EventRecorder& operator=(const EventRecorder&) = delete;

    noctis::u64 hash() const { return hasher_.h; }
    size_t total() const { return total_; }
    size_t count(noctis::EventType t) const { return perType_[static_cast<size_t>(t)]; }
    // Largest number of vocalisations attributed to a single individual (all call contexts, or one).
    size_t maxCallsPerIndividual(noctis::u32* who = nullptr) const;
    size_t maxCallsPerIndividual(noctis::CallContext context, noctis::u32* who = nullptr) const;
    // Vocalisations of one call context (alarm, contact, courtship...).
    size_t calls(noctis::CallContext context) const;
    // Events with a non-finite time/position/magnitude or an invalid type.
    size_t badEvents() const { return bad_; }
    // Events whose time is earlier than an event dispatched before them.
    size_t outOfOrder() const { return outOfOrder_; }
    const std::string& firstBad() const { return firstBad_; }
    void reset();

private:
    void onEvent(const noctis::SimEvent& e);

    noctis::WorldSimulation& world_;
    int handle_ = 0;
    Hasher hasher_;
    size_t total_ = 0;
    std::array<size_t, static_cast<size_t>(noctis::EventType::Count) + 1> perType_{};
    struct EmitterCalls
    {
        noctis::u32 emitter = 0;
        size_t total = 0;
        std::array<size_t, 16> byContext{};
    };
    std::vector<EmitterCalls> callsByEmitter_;
    std::array<size_t, 16> callsByContext_{};
    double lastTime_ = -1e300;
    size_t bad_ = 0;
    size_t outOfOrder_ = 0;
    std::string firstBad_;
};

// True when every float of the creature's simulated state is finite; 'what' names the first bad field.
bool creatureFinite(const noctis::Creature& c, std::string* what);
// Physiological state within its documented ranges (drives 0..1, reserves within capacity...).
bool creatureInBounds(const noctis::Creature& c, std::string* what);
} // namespace noctistest
