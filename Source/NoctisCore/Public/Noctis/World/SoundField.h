// The acoustic world. Every sound — a hadrosaur call, a footfall, thunder, the rover's motor,
// the drone's rotors — is a SoundSource with a physical level and spectrum. Animal hearing and
// the player's microphones both receive it through the same propagation model:
// spherical spreading, ISO 9613-1 atmospheric absorption, ISO 9613-2 foliage attenuation and a
// simple wind-gradient term. What an animal "hears" and what the player records are therefore
// the same physical signal.
#pragma once

#include "Noctis/Core/Ids.h"
#include "Noctis/Core/Math.h"

#include <mutex>
#include <vector>

namespace noctis
{
class VegetationSystem;
class Terrain;

enum class SoundKind : u8
{
    Vocalization,
    Footfall,
    VegetationCrash,
    Splash,
    Thunder,
    Engine,
    Rotor,
    Voice,
    Impact,
    Mechanical // camera shutter, equipment
};
NOCTIS_API const char* soundKindLabelFr(SoundKind k);

struct SoundSource
{
    u32 id = 0;
    SoundKind kind = SoundKind::Vocalization;
    EntityId emitter;
    i16 speciesIndex = -1;
    Vec3 position;
    double startTime = 0.0;
    float durationS = 0.5f;
    float levelDb = 60.0f;   // SPL at 1 m
    float f0Hz = 200.0f;     // fundamental (tonal) or spectral centroid (noise)
    float bandwidthHz = 100.0f;
    bool tonal = true;
    u8 callContext = 0;      // CallContext for vocalizations
    u8 callIndex = 0;        // index into species call list
    float pitchShift = 1.0f; // individual variation of f0 (size, identity)
    u64 seed = 0;            // synthesis variation
};

struct PropagationEnv
{
    float temperatureC = 15.0f;
    float humidity = 0.7f;
    float pressureKPa = 101.325f;
    Vec2 wind;               // vector the air moves towards
    const VegetationSystem* vegetation = nullptr;
    const Terrain* terrain = nullptr;
};

class NOCTIS_API SoundField
{
public:
    // Thread-safe.
    u32 emit(SoundSource s);
    void prune(double now, double keepSeconds = 20.0);
    // Sources overlapping [t0, t1].
    void collect(double t0, double t1, std::vector<SoundSource>& out) const;
    void collectNear(double t0, double t1, const Vec2& p, float radius, std::vector<SoundSource>& out) const;
    size_t size() const { return sources_.size(); }

    // ISO 9613-1 pure-tone atmospheric absorption coefficient (dB per metre).
    static float absorptionDbPerM(float frequencyHz, float temperatureC, float relativeHumidity01, float pressureKPa = 101.325f);
    // ISO 9613-2 foliage attenuation per metre of dense foliage (dB/m), 63 Hz..8 kHz.
    static float foliageDbPerM(float frequencyHz);
    // Received SPL at listener (dB). Includes spreading, absorption, foliage and wind gradient terms.
    static float receivedLevelDb(const SoundSource& s, const Vec3& listener, const PropagationEnv& env, float* outFoliageDb = nullptr);
    // Simplified audiogram: threshold (dB SPL) at frequency f for a species' hearing parameters.
    static float hearingThresholdDb(float f, float minHz, float maxHz, float bestHz, float bestThresholdDb);

private:
    mutable std::mutex mutex_;
    std::vector<SoundSource> sources_;
    u32 nextId_ = 1;
};
} // namespace noctis
