#include "Noctis/World/Atmosphere.h"

#include <cmath>

namespace noctis
{
// ---------------------------------------------------------------- Sky

void SkySystem::configure(float latitudeDeg, float axialTiltDeg)
{
    latitudeDeg_ = latitudeDeg;
    tiltDeg_ = axialTiltDeg;
}

void SkySystem::solarPosition(float latitudeDeg, float axialTiltDeg, double dayOfYear, double daysPerYear, double dayFraction,
                              float& elevationDeg, float& azimuthDeg)
{
    const double phi = latitudeDeg * kDegToRad;
    // Declination: minimum near the northern winter solstice (~day 355 -> -10 days).
    const double decl = -axialTiltDeg * kDegToRad * std::cos(2.0 * 3.14159265358979 * (dayOfYear + 10.0) / daysPerYear);
    const double h = (dayFraction - 0.5) * 2.0 * 3.14159265358979; // hour angle, 0 at solar noon
    const double sinEl = std::sin(phi) * std::sin(decl) + std::cos(phi) * std::cos(decl) * std::cos(h);
    const double el = std::asin(std::max(-1.0, std::min(1.0, sinEl)));
    const double az = std::atan2(-std::sin(h), std::tan(decl) * std::cos(phi) - std::sin(phi) * std::cos(h));
    elevationDeg = static_cast<float>(el * kRadToDeg);
    float azDeg = static_cast<float>(az * kRadToDeg);
    if (azDeg < 0.0f)
    {
        azDeg += 360.0f;
    }
    azimuthDeg = azDeg;
}

float SkySystem::clearSkyIlluminance(float el)
{
    // Piecewise log-linear fit to standard twilight illuminance values (≈400 lx at sunrise,
    // ≈3 lx at civil twilight end, ≈0.008 lx at nautical, starlight ≈0.0005 lx).
    if (el >= 0.0f)
    {
        const float s = std::sin(el * kDegToRad);
        return 400.0f + 110000.0f * std::pow(s, 1.15f);
    }
    auto logInterp = [](float a, float b, float t) { return std::pow(10.0f, lerpf(std::log10(a), std::log10(b), t)); };
    if (el >= -6.0f)
    {
        return logInterp(3.4f, 400.0f, (el + 6.0f) / 6.0f);
    }
    if (el >= -12.0f)
    {
        return logInterp(0.008f, 3.4f, (el + 12.0f) / 6.0f);
    }
    if (el >= -18.0f)
    {
        return logInterp(0.0006f, 0.008f, (el + 18.0f) / 6.0f);
    }
    return 0.0006f;
}

void SkySystem::update(const SimClock& clock, float cloudCover)
{
    const double doy = clock.dayOfYear();
    const double year = clock.spec().daysPerYear;
    float el = 0.0f;
    float az = 0.0f;
    solarPosition(latitudeDeg_, tiltDeg_, doy, year, clock.dayFraction(), el, az);
    state_.sunElevationDeg = el;
    state_.sunAzimuthDeg = az;
    const float elr = el * kDegToRad;
    const float azr = az * kDegToRad;
    state_.sunDirection = Vec3{std::sin(azr) * std::cos(elr), std::cos(azr) * std::cos(elr), std::sin(elr)};

    // Moon: lags the sun by the synodic phase; declination swings with phase (simplified).
    const float phase = static_cast<float>(clock.moonPhase());
    state_.moonPhase = phase;
    state_.moonIllumination = 0.5f * (1.0f - std::cos(kTwoPi * phase));
    const double decl = -tiltDeg_ * std::cos(2.0 * 3.14159265358979 * (doy + 10.0) / year);
    const double moonDeclDeg = decl * std::cos(kTwoPi * phase);
    {
        const double phi = latitudeDeg_ * kDegToRad;
        const double dm = moonDeclDeg * kDegToRad;
        const double h = (clock.dayFraction() - 0.5 - phase) * 2.0 * 3.14159265358979;
        const double sinEl = std::sin(phi) * std::sin(dm) + std::cos(phi) * std::cos(dm) * std::cos(h);
        const double mel = std::asin(std::max(-1.0, std::min(1.0, sinEl)));
        double maz = std::atan2(-std::sin(h), std::tan(dm) * std::cos(phi) - std::sin(phi) * std::cos(h));
        state_.moonElevationDeg = static_cast<float>(mel * kRadToDeg);
        float mazDeg = static_cast<float>(maz * kRadToDeg);
        if (mazDeg < 0.0f)
        {
            mazDeg += 360.0f;
        }
        state_.moonAzimuthDeg = mazDeg;
        state_.moonDirection = Vec3{static_cast<float>(std::sin(maz) * std::cos(mel)), static_cast<float>(std::cos(maz) * std::cos(mel)),
                                    static_cast<float>(std::sin(mel))};
    }

    const float cloudDay = 1.0f - 0.75f * cloudCover;
    float lux = clearSkyIlluminance(el) * (el > -6.0f ? cloudDay : (1.0f - 0.6f * cloudCover));
    if (state_.moonElevationDeg > 0.0f)
    {
        // Full moon high in a clear sky gives ~0.1–0.3 lx.
        lux += 0.25f * state_.moonIllumination * std::sin(state_.moonElevationDeg * kDegToRad) * (1.0f - 0.85f * cloudCover);
    }
    state_.illuminanceLux = lux;

    const double tanProd = -std::tan(latitudeDeg_ * kDegToRad) * std::tan(decl * kDegToRad);
    state_.photoperiodHours = static_cast<float>(clock.spec().dayLengthHours * std::acos(std::max(-1.0, std::min(1.0, tanProd))) / 3.14159265358979);

    if (el > 6.0f)
    {
        state_.phase = DayPhase::Day;
    }
    else if (el > -12.0f)
    {
        state_.phase = clock.dayFraction() < 0.5 ? DayPhase::Dawn : DayPhase::Dusk;
    }
    else
    {
        state_.phase = DayPhase::Night;
    }
}

// ---------------------------------------------------------------- Seasons

void SeasonSystem::configure(const EnvironmentSim& env) { env_ = env; }

bool SeasonSystem::inWindow(double dayOfYear, float startDoy, float endDoy) const
{
    if (startDoy <= endDoy)
    {
        return dayOfYear >= startDoy && dayOfYear <= endDoy;
    }
    return dayOfYear >= startDoy || dayOfYear <= endDoy; // window wraps the new year
}

void SeasonSystem::update(const SimClock& clock)
{
    const double doy = clock.dayOfYear();
    const double year = clock.spec().daysPerYear;
    state_.yearFraction = static_cast<float>(doy / year);
    // Coldest about 3 weeks after the winter solstice (thermal lag; Northern Hemisphere).
    state_.temperatureOffsetC = -0.5f * env_.annualTempRangeC * static_cast<float>(std::cos(2.0 * 3.14159265358979 * (doy - 20.0) / year));
    state_.wetSeason = inWindow(doy, env_.wetSeasonStartDoy, env_.wetSeasonEndDoy);
    // Smooth wetness curve centred on the wet season.
    float centre = 0.5f * (env_.wetSeasonStartDoy + env_.wetSeasonEndDoy);
    if (env_.wetSeasonStartDoy > env_.wetSeasonEndDoy)
    {
        centre = std::fmod(0.5f * (env_.wetSeasonStartDoy + env_.wetSeasonEndDoy + static_cast<float>(year)), static_cast<float>(year));
    }
    const float phase = static_cast<float>(2.0 * 3.14159265358979 * (doy - centre) / year);
    state_.wetness = saturate(0.5f + 0.5f * env_.dryStrength * 2.0f * std::cos(phase) * 0.5f + (state_.wetSeason ? 0.15f : -0.15f) * env_.dryStrength);
    const float meanT = env_.meanAnnualTempC + state_.temperatureOffsetC;
    state_.growthFactor = saturate((meanT - 3.0f) / 12.0f) * (0.55f + 0.45f * state_.wetness);
    const float f = state_.yearFraction;
    if (f < 0.2f || f >= 0.95f)
    {
        state_.labelFr = "Hiver";
    }
    else if (f < 0.45f)
    {
        state_.labelFr = "Printemps";
    }
    else if (f < 0.7f)
    {
        state_.labelFr = "Été";
    }
    else
    {
        state_.labelFr = "Automne";
    }
    if (state_.wetSeason)
    {
        state_.labelFr += " (saison humide)";
    }
}

// ---------------------------------------------------------------- Weather

const char* weatherRegimeLabelFr(WeatherRegime r)
{
    switch (r)
    {
    case WeatherRegime::Clear: return "Ciel dégagé";
    case WeatherRegime::Fair: return "Beau temps, cumulus";
    case WeatherRegime::Overcast: return "Couvert";
    case WeatherRegime::LightRain: return "Pluie faible";
    case WeatherRegime::HeavyRain: return "Pluie forte";
    case WeatherRegime::Thunderstorm: return "Orage";
    case WeatherRegime::Count: break;
    }
    return "?";
}

namespace weatherimpl
{
float regimeCloud(WeatherRegime r)
{
    switch (r)
    {
    case WeatherRegime::Clear: return 0.05f;
    case WeatherRegime::Fair: return 0.35f;
    case WeatherRegime::Overcast: return 0.9f;
    case WeatherRegime::LightRain: return 0.95f;
    case WeatherRegime::HeavyRain: return 1.0f;
    case WeatherRegime::Thunderstorm: return 0.95f;
    case WeatherRegime::Count: break;
    }
    return 0.5f;
}

float regimeWindFactor(WeatherRegime r)
{
    switch (r)
    {
    case WeatherRegime::Clear: return 0.8f;
    case WeatherRegime::Fair: return 1.0f;
    case WeatherRegime::Overcast: return 1.1f;
    case WeatherRegime::LightRain: return 1.2f;
    case WeatherRegime::HeavyRain: return 1.5f;
    case WeatherRegime::Thunderstorm: return 2.4f;
    case WeatherRegime::Count: break;
    }
    return 1.0f;
}

// Hourly transition probabilities; 'wet' (0..1) is the seasonal wetness, 'storm' a storm propensity.
WeatherRegime nextRegime(WeatherRegime r, float wet, float storm, float u)
{
    const float w = 0.4f + 1.2f * wet; // rain propensity multiplier
    float p[static_cast<int>(WeatherRegime::Count)] = {};
    switch (r)
    {
    case WeatherRegime::Clear:
        p[0] = 0.9f; p[1] = 0.08f; p[2] = 0.02f * w; break;
    case WeatherRegime::Fair:
        p[0] = 0.07f / w; p[1] = 0.85f; p[2] = 0.06f * w; p[5] = 0.012f * storm; break;
    case WeatherRegime::Overcast:
        p[1] = 0.07f / w; p[2] = 0.82f; p[3] = 0.08f * w; p[4] = 0.02f * w; break;
    case WeatherRegime::LightRain:
        p[2] = 0.14f / w; p[3] = 0.78f; p[4] = 0.06f * w; break;
    case WeatherRegime::HeavyRain:
        p[3] = 0.25f; p[4] = 0.65f; p[5] = 0.08f * storm; p[2] = 0.05f; break;
    case WeatherRegime::Thunderstorm:
        p[4] = 0.35f; p[2] = 0.2f; p[5] = 0.45f; break;
    case WeatherRegime::Count: break;
    }
    float sum = 0.0f;
    for (const float v : p)
    {
        sum += v;
    }
    float acc = 0.0f;
    for (int i = 0; i < static_cast<int>(WeatherRegime::Count); ++i)
    {
        acc += p[i] / sum;
        if (u <= acc)
        {
            return static_cast<WeatherRegime>(i);
        }
    }
    return r;
}
} // namespace weatherimpl

float WeatherSystem::regimeRain(WeatherRegime r) const
{
    switch (r)
    {
    case WeatherRegime::LightRain: return 0.9f * rainScale_;
    case WeatherRegime::HeavyRain: return 4.5f * rainScale_;
    case WeatherRegime::Thunderstorm: return 11.0f * rainScale_;
    default: return 0.0f;
    }
}

void WeatherSystem::calibrate()
{
    // Run the regime chain for 4 synthetic years and scale rain intensities so the expected
    // annual total matches the environment's precipitation estimate.
    Rng rng(0xCA1B, 7);
    SeasonSystem season;
    season.configure(env_);
    SimClock clock;
    CalendarSpec cal = env_.calendar;
    clock.configure(cal, 0.0, 0.0);
    WeatherRegime r = WeatherRegime::Fair;
    rainScale_ = 1.0f;
    double rain = 0.0;
    const int hours = static_cast<int>(4.0 * cal.daysPerYear * cal.dayLengthHours);
    const float storm = std::max(0.05f, env_.stormProbabilityPerDay * 8.0f);
    for (int h = 0; h < hours; ++h)
    {
        clock.advance(3600.0);
        if (h % 24 == 0)
        {
            season.update(clock);
        }
        r = weatherimpl::nextRegime(r, season.state().wetness, storm, rng.uniform());
        rain += regimeRain(r);
    }
    const double annual = rain / 4.0;
    if (annual > 1.0)
    {
        rainScale_ = static_cast<float>(env_.annualPrecipMm / annual);
    }
}

void WeatherSystem::configure(const EnvironmentSim& env, u64 seed)
{
    env_ = env;
    rng_.seed(seed, 0x3EA7);
    calibrate();
    state_ = WeatherState{};
    state_.regime = WeatherRegime::Fair;
    state_.temperatureC = env.meanAnnualTempC;
    state_.humidity = env.humidityMean;
    state_.windFromDeg = env.prevailingWindDeg;
    state_.windSpeedMs = env.meanWindMs;
    targetCloud_ = weatherimpl::regimeCloud(state_.regime);
}

void WeatherSystem::stepRegime(const SeasonState& season)
{
    const float storm = std::max(0.05f, env_.stormProbabilityPerDay * 8.0f);
    const WeatherRegime before = state_.regime;
    state_.regime = weatherimpl::nextRegime(state_.regime, season.wetness, storm, rng_.uniform());
    if (state_.regime != before)
    {
        targetCloud_ = clampf(weatherimpl::regimeCloud(state_.regime) + rng_.range(-0.1f, 0.1f), 0.0f, 1.0f);
    }
}

void WeatherSystem::update(double dtSeconds, const SimClock& clock, const SeasonState& season, const SkyState& sky, const Rect2& worldBounds)
{
    const float dtH = static_cast<float>(dtSeconds / 3600.0);
    hourAccumulator_ += dtSeconds;
    while (hourAccumulator_ >= 3600.0)
    {
        hourAccumulator_ -= 3600.0;
        stepRegime(season);
        // Synoptic temperature anomaly: AR(1) with ~3-day memory, sd ~2.5 degC (game assumption).
        const float a = std::exp(-1.0f / 72.0f);
        tempAnomaly_ = a * tempAnomaly_ + std::sqrt(1.0f - a * a) * 2.5f * rng_.normal();
        windDirAnomaly_ = 0.97f * windDirAnomaly_ + 0.24f * 25.0f * rng_.normal();
    }
    const int dayIndex = static_cast<int>(clock.absoluteDays());
    if (dayIndex != lastDayIndex_)
    {
        lastDayIndex_ = dayIndex;
        fogProneDay_ = rng_.chance(env_.fogMorningProbability);
    }

    WeatherState& s = state_;
    s.cloudCover = lerpf(s.cloudCover, targetCloud_, approachFactor(static_cast<float>(dtSeconds), 1800.0f));
    const float rainTarget = regimeRain(s.regime);
    s.precipMmPerHour = lerpf(s.precipMmPerHour, rainTarget, approachFactor(static_cast<float>(dtSeconds), 600.0f));
    if (s.precipMmPerHour < 0.02f)
    {
        s.precipMmPerHour = rainTarget > 0.0f ? s.precipMmPerHour : 0.0f;
    }
    accumulatedPrecipMm_ += s.precipMmPerHour * dtH;

    // Temperature: seasonal mean + diurnal cycle damped by clouds + synoptic anomaly + rain cooling.
    const float hf = static_cast<float>(clock.dayFraction());
    const float diurnalShape = std::cos(kTwoPi * (hf - 0.625f)); // max ~15:00, min ~03:00
    const float dtr = env_.diurnalTempRangeC * (1.0f - 0.6f * s.cloudCover);
    const float targetT = env_.meanAnnualTempC + season.temperatureOffsetC + 0.5f * dtr * diurnalShape + tempAnomaly_ -
                          (s.precipMmPerHour > 0.5f ? 2.0f : 0.0f);
    s.temperatureC = lerpf(s.temperatureC, targetT, approachFactor(static_cast<float>(dtSeconds), 1200.0f));

    // Humidity: higher at night, during rain and under cloud.
    const float rhTarget = clampf(env_.humidityMean - 0.18f * diurnalShape + 0.08f * s.cloudCover + (s.precipMmPerHour > 0.1f ? 0.2f : 0.0f) -
                                      0.1f * (season.wetness < 0.4f ? 1.0f : 0.0f),
                                  0.15f, 1.0f);
    s.humidity = lerpf(s.humidity, rhTarget, approachFactor(static_cast<float>(dtSeconds), 1800.0f));
    // Magnus formula (Alduchov & Eskridge coefficients) for dew point.
    const float b = 17.62f;
    const float c = 243.12f;
    const float gamma = std::log(std::max(0.01f, s.humidity)) + b * s.temperatureC / (c + s.temperatureC);
    s.dewPointC = c * gamma / (b - gamma);

    // Wind.
    const float diurnalWind = 0.8f + 0.4f * std::max(0.0f, diurnalShape);
    const float targetWind = env_.meanWindMs * weatherimpl::regimeWindFactor(s.regime) * diurnalWind;
    s.windSpeedMs = lerpf(s.windSpeedMs, targetWind, approachFactor(static_cast<float>(dtSeconds), 900.0f));
    s.windFromDeg = std::fmod(env_.prevailingWindDeg + windDirAnomaly_ + 360.0f, 360.0f);
    gustPhase_ += static_cast<float>(dtSeconds) * 0.35f;
    s.gustMs = s.windSpeedMs * 0.35f * (0.5f + 0.5f * std::sin(gustPhase_) * std::sin(gustPhase_ * 0.37f + 1.3f));
    const float fromRad = s.windFromDeg * kDegToRad;
    s.wind = Vec2{-std::sin(fromRad), -std::cos(fromRad)} * (s.windSpeedMs + s.gustMs);

    // Radiation fog: forms on calm, near-saturated nights and mornings; burns off after sunrise.
    const float spread = s.temperatureC - s.dewPointC;
    const bool fogWindow = sky.sunElevationDeg < 12.0f && (hf < 0.45f || hf > 0.85f);
    float fogTarget = 0.0f;
    if (fogProneDay_ && fogWindow && s.windSpeedMs < 3.0f && spread < 2.5f && s.precipMmPerHour < 0.5f)
    {
        fogTarget = 0.02f * saturate((2.5f - spread) / 2.0f) * saturate((3.0f - s.windSpeedMs) / 2.0f);
    }
    if (forcedFog_ >= 0.0f)
    {
        fogTarget = forcedFog_;
    }
    s.fogExtinction = lerpf(s.fogExtinction, fogTarget, approachFactor(static_cast<float>(dtSeconds), fogTarget > s.fogExtinction ? 2400.0f : 1500.0f));
    const float betaAir = 3.912f / 40000.0f;
    const float betaRain = 0.00045f * std::pow(std::max(0.0f, s.precipMmPerHour), 0.7f);
    s.visibilityM = 3.912f / (betaAir + betaRain + s.fogExtinction);

    s.pressureHpa = 1013.0f - 12.0f * s.cloudCover - (s.regime == WeatherRegime::Thunderstorm ? 6.0f : 0.0f);

    // Dryness: days since meaningful rain, tempered by humidity.
    if (s.precipMmPerHour > 0.3f)
    {
        s.daysSinceRain = 0.0;
    }
    else
    {
        s.daysSinceRain += dtSeconds / clock.dayLengthSeconds();
    }
    s.dryness = saturate((1.0f - std::exp(-static_cast<float>(s.daysSinceRain) / 12.0f)) * (1.3f - s.humidity));

    // Potential evaporation (mm/h): temperature, sunlight and vapour deficit (simplified, game assumption).
    const float sun = saturate(sky.sunElevationDeg / 60.0f) * (1.0f - 0.7f * s.cloudCover);
    s.potentialEvapMmPerHour = std::max(0.0f, 0.02f + 0.012f * std::max(0.0f, s.temperatureC) * (0.3f + sun) * (1.1f - s.humidity) +
                                                  0.01f * s.windSpeedMs * (1.0f - s.humidity));

    // Ambient noise levels used for acoustic masking.
    s.rainNoiseDb = s.precipMmPerHour > 0.05f ? 38.0f + 8.0f * std::log10(1.0f + 4.0f * s.precipMmPerHour) * 2.0f : 0.0f;
    s.windNoiseDb = 22.0f + 2.2f * (s.windSpeedMs + s.gustMs);

    // Lightning.
    if (s.regime == WeatherRegime::Thunderstorm)
    {
        const float strikesPerHourOverMap = 25.0f; // game assumption
        const int n = rng_.poisson(strikesPerHourOverMap * dtH);
        for (int i = 0; i < n; ++i)
        {
            const Vec2 sz = worldBounds.size();
            strikes_.push_back({worldBounds.min + Vec2{rng_.uniform() * sz.x, rng_.uniform() * sz.y}, clock.seconds()});
        }
    }
}

float WeatherSystem::fogExtinctionAt(float heightAboveValleyFloorM, bool nearWater) const
{
    const float pooling = std::exp(-std::max(0.0f, heightAboveValleyFloorM) / 8.0f);
    return state_.fogExtinction * (0.25f + 0.75f * pooling) * (nearWater ? 1.35f : 1.0f);
}

float WeatherSystem::visibilityAt(float heightAboveValleyFloorM, bool nearWater) const
{
    const float betaAir = 3.912f / 40000.0f;
    const float betaRain = 0.00045f * std::pow(std::max(0.0f, state_.precipMmPerHour), 0.7f);
    return 3.912f / (betaAir + betaRain + fogExtinctionAt(heightAboveValleyFloorM, nearWater));
}

std::vector<LightningStrike> WeatherSystem::takeStrikes()
{
    std::vector<LightningStrike> out;
    out.swap(strikes_);
    return out;
}

void WeatherSystem::forceRegime(WeatherRegime r)
{
    state_.regime = r;
    targetCloud_ = weatherimpl::regimeCloud(r);
}

void WeatherSystem::forceFog(float extinction) { forcedFog_ = extinction; }

std::vector<double> WeatherSystem::saveState() const
{
    return {static_cast<double>(state_.regime), state_.temperatureC, state_.humidity, state_.cloudCover, state_.precipMmPerHour,
            state_.windSpeedMs, state_.fogExtinction, tempAnomaly_, windDirAnomaly_, state_.daysSinceRain, hourAccumulator_,
            static_cast<double>(rng_.stateForSave()), static_cast<double>(lastDayIndex_), fogProneDay_ ? 1.0 : 0.0, targetCloud_,
            accumulatedPrecipMm_};
}

void WeatherSystem::loadState(const std::vector<double>& s)
{
    if (s.size() < 16)
    {
        return;
    }
    state_.regime = static_cast<WeatherRegime>(static_cast<int>(s[0]));
    state_.temperatureC = static_cast<float>(s[1]);
    state_.humidity = static_cast<float>(s[2]);
    state_.cloudCover = static_cast<float>(s[3]);
    state_.precipMmPerHour = static_cast<float>(s[4]);
    state_.windSpeedMs = static_cast<float>(s[5]);
    state_.fogExtinction = static_cast<float>(s[6]);
    tempAnomaly_ = static_cast<float>(s[7]);
    windDirAnomaly_ = static_cast<float>(s[8]);
    state_.daysSinceRain = s[9];
    hourAccumulator_ = s[10];
    // RNG state cannot round-trip exactly through double; reseed deterministically from it.
    rng_.seed(static_cast<u64>(s[11]), 0x3EA7);
    lastDayIndex_ = static_cast<int>(s[12]);
    fogProneDay_ = s[13] > 0.5;
    targetCloud_ = static_cast<float>(s[14]);
    accumulatedPrecipMm_ = s[15];
}
} // namespace noctis
