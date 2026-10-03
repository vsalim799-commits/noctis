#include "Noctis/World/SoundField.h"

#include "Noctis/World/Terrain.h"
#include "Noctis/World/Vegetation.h"

#include <algorithm>
#include <cmath>

namespace noctis
{
const char* soundKindLabelFr(SoundKind k)
{
    switch (k)
    {
    case SoundKind::Vocalization: return "Vocalisation";
    case SoundKind::Footfall: return "Pas";
    case SoundKind::VegetationCrash: return "Végétation brisée";
    case SoundKind::Splash: return "Éclaboussure";
    case SoundKind::Thunder: return "Tonnerre";
    case SoundKind::Engine: return "Moteur";
    case SoundKind::Rotor: return "Rotors du drone";
    case SoundKind::Voice: return "Voix humaine";
    case SoundKind::Impact: return "Impact";
    case SoundKind::Mechanical: return "Bruit mécanique";
    }
    return "?";
}

u32 SoundField::emit(SoundSource s)
{
    std::lock_guard<std::mutex> lock(mutex_);
    s.id = nextId_++;
    sources_.push_back(s);
    return s.id;
}

void SoundField::prune(double now, double keepSeconds)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sources_.erase(std::remove_if(sources_.begin(), sources_.end(),
                                  [&](const SoundSource& s) { return s.startTime + s.durationS < now - keepSeconds; }),
                   sources_.end());
}

void SoundField::collect(double t0, double t1, std::vector<SoundSource>& out) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const SoundSource& s : sources_)
    {
        if (s.startTime <= t1 && s.startTime + s.durationS >= t0)
        {
            out.push_back(s);
        }
    }
}

void SoundField::collectNear(double t0, double t1, const Vec2& p, float radius, std::vector<SoundSource>& out) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const float r2 = radius * radius;
    for (const SoundSource& s : sources_)
    {
        if (s.startTime <= t1 && s.startTime + s.durationS >= t0 && distanceSq(s.position.xy(), p) <= r2)
        {
            out.push_back(s);
        }
    }
}

float SoundField::absorptionDbPerM(float f, float temperatureC, float rh01, float pressureKPa)
{
    // ISO 9613-1:1993, equations (3)–(5) and Annex B.
    const double T = static_cast<double>(temperatureC) + 273.15;
    const double T0 = 293.15;
    const double T01 = 273.16;
    const double pr = 101.325;
    const double pa = static_cast<double>(pressureKPa);
    const double C = -6.8346 * std::pow(T01 / T, 1.261) + 4.6151;
    const double psatRatio = std::pow(10.0, C);
    const double h = static_cast<double>(clampf(rh01, 0.0f, 1.0f)) * 100.0 * psatRatio / (pa / pr); // molar concentration of water vapour, %
    const double frO = (pa / pr) * (24.0 + 4.04e4 * h * (0.02 + h) / (0.391 + h));
    const double frN = (pa / pr) * std::pow(T / T0, -0.5) * (9.0 + 280.0 * h * std::exp(-4.170 * (std::pow(T / T0, -1.0 / 3.0) - 1.0)));
    const double ff = static_cast<double>(f) * static_cast<double>(f);
    const double alpha = 8.686 * ff *
                         (1.84e-11 * std::pow(pa / pr, -1.0) * std::pow(T / T0, 0.5) +
                          std::pow(T / T0, -2.5) * (0.01275 * std::exp(-2239.1 / T) / (frO + ff / frO) + 0.1068 * std::exp(-3352.0 / T) / (frN + ff / frN)));
    return static_cast<float>(alpha);
}

float SoundField::foliageDbPerM(float f)
{
    // ISO 9613-2 Table A.1: ~0.02 dB/m at 63 Hz rising to ~0.12 dB/m at 8 kHz (paths 20–200 m).
    const float octaves = std::log2(std::max(63.0f, f) / 63.0f);
    return 0.02f + 0.1f * saturate(octaves / 7.0f);
}

float SoundField::hearingThresholdDb(float f, float minHz, float maxHz, float bestHz, float bestThresholdDb)
{
    if (f <= 0.0f || maxHz <= minHz)
    {
        return 200.0f;
    }
    const float lf = std::log2(std::max(1.0f, f));
    const float lb = std::log2(std::max(1.0f, bestHz));
    const float lmin = std::log2(std::max(1.0f, minHz));
    const float lmax = std::log2(std::max(1.0f, maxHz));
    // U-shaped audiogram: +25 dB at the stated range limits, steep roll-off beyond.
    float t = 0.0f;
    if (lf < lb)
    {
        t = (lb - lf) / std::max(0.1f, lb - lmin);
    }
    else
    {
        t = (lf - lb) / std::max(0.1f, lmax - lb);
    }
    float thr = bestThresholdDb + 25.0f * t * t;
    if (t > 1.0f)
    {
        thr += 40.0f * (t - 1.0f);
    }
    return thr;
}

float SoundField::receivedLevelDb(const SoundSource& s, const Vec3& listener, const PropagationEnv& env, float* outFoliageDb)
{
    const float r = std::max(1.0f, distance(s.position, listener));
    float level = s.levelDb - 20.0f * std::log10(r);
    level -= absorptionDbPerM(s.f0Hz, env.temperatureC, env.humidity, env.pressureKPa) * r;
    float foliageDb = 0.0f;
    if (env.vegetation && r > 10.0f)
    {
        const float h = 1.5f;
        const float tr = env.vegetation->transmittance(s.position.xy(), listener.xy(), h);
        // Convert optical depth to an equivalent length of dense foliage (game assumption: tau 0.3/m of dense foliage).
        const float foliageMeters = std::min(200.0f, -std::log(std::max(1e-4f, tr)) / 0.3f);
        foliageDb = foliageMeters * foliageDbPerM(s.f0Hz);
        level -= foliageDb;
    }
    if (outFoliageDb)
    {
        *outFoliageDb = foliageDb;
    }
    // Wind gradient: upwind shadowing, slight downwind enhancement (game assumption, qualitative ISO behaviour).
    const Vec2 dir = (listener.xy() - s.position.xy()).normalized();
    const float along = dot(env.wind, dir); // >0: listener downwind
    const float rangeFactor = saturate(r / 300.0f);
    level += along > 0.0f ? std::min(2.0f, along * 0.4f) * rangeFactor : std::max(-12.0f, along * 1.8f) * rangeFactor;
    // Terrain occlusion: a ridge between source and listener (barrier attenuation ~10 dB, capped).
    if (env.terrain && r > 30.0f)
    {
        const Vec3 a{s.position.x, s.position.y, env.terrain->heightAt(s.position.xy()) + 1.5f};
        const Vec3 b{listener.x, listener.y, std::max(listener.z, env.terrain->heightAt(listener.xy()) + 1.5f)};
        if (!env.terrain->lineOfSight(a, b, std::max(8.0f, r / 40.0f)))
        {
            level -= 10.0f;
        }
    }
    return level;
}
} // namespace noctis
