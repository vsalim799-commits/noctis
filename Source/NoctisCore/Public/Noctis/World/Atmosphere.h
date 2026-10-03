// Sky (sun, moon, illuminance), seasons and weather.
#pragma once

#include "Noctis/Core/Math.h"
#include "Noctis/Core/Random.h"
#include "Noctis/Core/Time.h"
#include "Noctis/Science/Environment.h"

#include <string>
#include <vector>

namespace noctis
{
enum class DayPhase : u8
{
    Night,
    Dawn,
    Day,
    Dusk
};

struct SkyState
{
    float sunElevationDeg = 0.0f;
    float sunAzimuthDeg = 0.0f; // clockwise from north
    Vec3 sunDirection{0.0f, 0.0f, 1.0f};
    float moonElevationDeg = 0.0f;
    float moonAzimuthDeg = 0.0f;
    Vec3 moonDirection{0.0f, 0.0f, 1.0f};
    float moonPhase = 0.0f;        // 0 new .. 0.5 full
    float moonIllumination = 0.0f; // illuminated fraction
    float illuminanceLux = 0.0f;   // horizontal illuminance at ground including clouds
    float photoperiodHours = 12.0f;
    DayPhase phase = DayPhase::Day;
};

class NOCTIS_API SkySystem
{
public:
    void configure(float latitudeDeg, float axialTiltDeg);
    void update(const SimClock& clock, float cloudCover);
    const SkyState& state() const { return state_; }

    static float clearSkyIlluminance(float sunElevationDeg);
    // Solar position for a given day/time (pure function; used by tests and Unreal).
    static void solarPosition(float latitudeDeg, float axialTiltDeg, double dayOfYear, double daysPerYear, double dayFraction,
                              float& elevationDeg, float& azimuthDeg);

private:
    float latitudeDeg_ = 45.0f;
    float tiltDeg_ = 23.4f;
    SkyState state_;
};

struct SeasonState
{
    float yearFraction = 0.0f;
    float temperatureOffsetC = 0.0f; // seasonal departure from the annual mean
    float wetness = 0.5f;            // 0 driest .. 1 wettest part of the year
    float growthFactor = 1.0f;       // vegetation phenology 0..1
    bool wetSeason = false;
    std::string labelFr;
};

class NOCTIS_API SeasonSystem
{
public:
    void configure(const EnvironmentSim& env);
    void update(const SimClock& clock);
    const SeasonState& state() const { return state_; }
    bool inWindow(double dayOfYear, float startDoy, float endDoy) const;

private:
    EnvironmentSim env_;
    SeasonState state_;
};

enum class WeatherRegime : u8
{
    Clear,
    Fair,
    Overcast,
    LightRain,
    HeavyRain,
    Thunderstorm,
    Count
};

NOCTIS_API const char* weatherRegimeLabelFr(WeatherRegime r);

struct WeatherState
{
    WeatherRegime regime = WeatherRegime::Clear;
    float temperatureC = 15.0f;
    float humidity = 0.7f;      // relative 0..1
    float dewPointC = 10.0f;
    float cloudCover = 0.2f;
    float precipMmPerHour = 0.0f;
    float windSpeedMs = 2.0f;
    float windFromDeg = 270.0f;
    Vec2 wind;                  // vector the air moves towards (m/s)
    float gustMs = 0.0f;
    float fogExtinction = 0.0f; // 1/m (valley floor)
    float visibilityM = 30000.0f;
    float pressureHpa = 1013.0f;
    float dryness = 0.0f;       // 0 wet .. 1 very dry
    float potentialEvapMmPerHour = 0.1f;
    float rainNoiseDb = 0.0f;
    float windNoiseDb = 0.0f;
    double daysSinceRain = 0.0;
};

struct LightningStrike
{
    Vec2 position;
    double time = 0.0;
};

class NOCTIS_API WeatherSystem
{
public:
    void configure(const EnvironmentSim& env, u64 seed);
    // Advances the weather. Regime transitions happen hourly; continuous fields every call.
    void update(double dtSeconds, const SimClock& clock, const SeasonState& season, const SkyState& sky, const Rect2& worldBounds);
    const WeatherState& state() const { return state_; }
    // Fog is denser in low ground and near water (cold-air pooling).
    float fogExtinctionAt(float heightAboveValleyFloorM, bool nearWater) const;
    float visibilityAt(float heightAboveValleyFloorM, bool nearWater) const;
    // Strikes since last call (consumed by the world to emit events, thunder and ignitions).
    std::vector<LightningStrike> takeStrikes();

    // Forces a regime (debug tools / scripted tests). Natural evolution resumes afterwards.
    void forceRegime(WeatherRegime r);
    void forceFog(float extinction);

    float annualPrecipTargetMm() const { return env_.annualPrecipMm; }
    float rainScale() const { return rainScale_; }
    double accumulatedPrecipMm() const { return accumulatedPrecipMm_; }

    std::vector<double> saveState() const;
    void loadState(const std::vector<double>& s);

private:
    void stepRegime(const SeasonState& season);
    float regimeRain(WeatherRegime r) const;
    void calibrate();

    EnvironmentSim env_;
    Rng rng_;
    WeatherState state_;
    double regimeTimer_ = 0.0;
    double hourAccumulator_ = 0.0;
    float tempAnomaly_ = 0.0f;
    float windDirAnomaly_ = 0.0f;
    bool fogProneDay_ = false;
    int lastDayIndex_ = -1;
    float rainScale_ = 1.0f;
    float targetCloud_ = 0.2f;
    float gustPhase_ = 0.0f;
    float forcedFog_ = -1.0f;
    double accumulatedPrecipMm_ = 0.0;
    std::vector<LightningStrike> strikes_;
};
} // namespace noctis
