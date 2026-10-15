// World systems: terrain generation, hydrology, vegetation, sky, sound propagation.
#include "TestFramework.h"
#include "TestSupport.h"

#include "Noctis/Core/FileSystem.h"
#include "Noctis/Core/Jobs.h"
#include "Noctis/Science/EvidenceDatabase.h"
#include "Noctis/World/Atmosphere.h"
#include "Noctis/World/SoundField.h"
#include "Noctis/World/TerrainGenerator.h"
#include "Noctis/World/Vegetation.h"
#include "Noctis/World/Water.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace noctis;

namespace
{
const EvidenceDatabase* sharedDb()
{
    static std::unique_ptr<EvidenceDatabase> db;
    if (!db)
    {
        std::unique_ptr<FileSystem> fs = makeStdFileSystem();
        db = std::make_unique<EvidenceDatabase>();
        if (!db->load(*fs, noctistest::dataRoot()))
        {
            db.reset();
            return nullptr;
        }
    }
    return db.get();
}

u64 hashTerrain(const Terrain& t, const WorldLayout& layout)
{
    noctistest::Hasher h;
    for (const float v : t.heights().raw())
    {
        h.f(v);
    }
    for (const u8 v : t.substrates().raw())
    {
        h.u(v);
    }
    for (const u8 v : t.habitats().raw())
    {
        h.u(v);
    }
    for (const float v : t.baseWetness().raw())
    {
        h.f(v);
    }
    for (const RiverDesc& r : layout.rivers)
    {
        for (const RiverPoint& p : r.points)
        {
            h.f(p.position.x);
            h.f(p.position.y);
            h.f(p.width);
        }
    }
    for (const LakeDesc& l : layout.lakes)
    {
        h.f(l.center.x);
        h.f(l.center.y);
        h.f(l.maxDepth);
    }
    return h.h;
}

// Exact ISO midband frequencies f = 1000 * 10^(3k/10), k = -4..3 (nominal 63 Hz .. 8 kHz).
double isoMidband(int k) { return 1000.0 * std::pow(10.0, 0.3 * k); }
} // namespace

// ---------------------------------------------------------------- Terrain

NOCTIS_TEST("world.terrain.generation_is_deterministic")
{
    const EvidenceDatabase* db = sharedDb();
    REQUIRE(db != nullptr);
    for (const char* envId : {"hell_creek", "djadochta"})
    {
        const EnvironmentDefinition* env = db->findEnvironment(envId);
        REQUIRE(env != nullptr);
        Terrain a;
        Terrain b;
        Terrain c;
        const WorldLayout la = TerrainGenerator::generate(env->sim, 1234u, a, 257, 128);
        const WorldLayout lb = TerrainGenerator::generate(env->sim, 1234u, b, 257, 128);
        const WorldLayout lc = TerrainGenerator::generate(env->sim, 1235u, c, 257, 128);
        const u64 ha = hashTerrain(a, la);
        const u64 hb = hashTerrain(b, lb);
        const u64 hc = hashTerrain(c, lc);
        NOTE("%s seed 1234: hash %016llx (repeat %016llx), seed 1235: %016llx; relief %.1f m; %zu river(s), %zu lake(s)", envId,
             static_cast<unsigned long long>(ha), static_cast<unsigned long long>(hb), static_cast<unsigned long long>(hc),
             static_cast<double>(a.maxHeight() - a.minHeight()), la.rivers.size(), la.lakes.size());
        CHECK_EQ(ha, hb);
        CHECK_NE(ha, hc);
        int nonFinite = 0;
        for (const float v : a.heights().raw())
        {
            nonFinite += std::isfinite(v) ? 0 : 1;
        }
        CHECK_EQ(nonFinite, 0);
        const float relief = a.maxHeight() - a.minHeight();
        CHECK_GT(relief, 0.5f);
        CHECK_LT(relief, env->sim.reliefM * 6.0f + 50.0f);
        CHECK_NEAR(a.size(), env->sim.mapSizeM, 1e-3);
        // Every material cell has a valid substrate and habitat code.
        int badCodes = 0;
        for (const u8 v : a.substrates().raw())
        {
            badCodes += v < kSubstrateCount ? 0 : 1;
        }
        for (const u8 v : a.habitats().raw())
        {
            badCodes += v < kHabitatCount ? 0 : 1;
        }
        CHECK_EQ(badCodes, 0);
    }
}

NOCTIS_TEST("world.terrain.queries_and_soil")
{
    const EvidenceDatabase* db = sharedDb();
    REQUIRE(db != nullptr);
    const EnvironmentDefinition* env = db->findEnvironment("hell_creek");
    REQUIRE(env != nullptr);
    Terrain t;
    TerrainGenerator::generate(env->sim, 99u, t, 257, 128);
    Rng rng(3u, 1u);
    for (int i = 0; i < 500; ++i)
    {
        const Vec2 p{rng.range(0.0f, t.size()), rng.range(0.0f, t.size())};
        const TerrainQuery q = t.query(p);
        CHECK_FINITE(q.height);
        CHECK_NEAR(q.normal.length(), 1.0, 1e-3);
        CHECK_RANGE(q.slopeDeg, 0.0, 90.0);
        CHECK_RANGE(q.moisture, 0.0, 1.0);
    }
    // Bekker sinkage grows with pressure and is bounded by the yielding layer.
    const Vec2 mid = t.bounds().center();
    float last = -1.0f;
    for (const float kpa : {10.0f, 50.0f, 150.0f, 400.0f})
    {
        const float z = t.sinkage(mid, kpa, 0.3f);
        CHECK_GE(z, last);
        last = z;
    }
    const SoilMechanics wetClay = soilMechanics(Substrate::Clay, 0.9f);
    CHECK_LE(t.sinkage(mid, 1.0e5f, 0.3f), std::max(wetClay.maxSinkageM, soilMechanics(t.substrateAt(mid), t.moistureAt(mid)).maxSinkageM) + 1e-3f);
}

// ---------------------------------------------------------------- Water

NOCTIS_TEST("world.water.linear_reservoir_mass_balance")
{
    const EvidenceDatabase* db = sharedDb();
    REQUIRE(db != nullptr);
    const EnvironmentDefinition* env = db->findEnvironment("hell_creek");
    REQUIRE(env != nullptr);
    Terrain t;
    const WorldLayout layout = TerrainGenerator::generate(env->sim, 42u, t, 257, 128);
    REQUIRE_GE(layout.rivers.size(), size_t(1));
    WaterSystem water;
    water.build(layout, t);
    const float meanRain = 0.12f;
    water.setMeanRainRate(meanRain);
    const float meanQ = layout.rivers[0].meanDischargeM3s;
    const size_t riverCount = layout.rivers.size();

    // 1) Storage bookkeeping: storage(t+1) - storage(t) = (inflow - q(t)) * dt for every hour, with
    //    inflow inferred from the storage change. Inflow must be non-negative and affine in rain,
    //    and equal the mean discharge when rain equals its long-run mean (runoff normalisation).
    auto storage = [&]() { return static_cast<double>(water.saveState()[0]); };
    auto inferredInflow = [&](float rain) {
        const double s0 = storage();
        const double q0 = water.riverDischarge(0);
        water.update(1.0f, rain, 0.1f);
        return (storage() - s0) / 3600.0 + q0;
    };
    const double inMean = inferredInflow(meanRain);
    const double inZero = inferredInflow(0.0f);
    const double inDouble = inferredInflow(2.0f * meanRain);
    const double inQuad = inferredInflow(4.0f * meanRain);
    NOTE("river 0: mean Q %.1f m3/s; inferred inflow at rain 0 / mean / 2x / 4x: %.2f / %.2f / %.2f / %.2f m3/s", static_cast<double>(meanQ), inZero,
         inMean, inDouble, inQuad);
    CHECK_REL(inMean, meanQ, 2e-3);
    CHECK_GE(inZero, 0.0);
    CHECK_REL(inQuad - inDouble, 2.0 * (inDouble - inMean), 5e-3); // affine in rain

    // 2) Steady state: constant mean rain for 40 days -> discharge -> mean discharge (outflow = inflow).
    for (int h = 0; h < 40 * 24; ++h)
    {
        water.update(1.0f, meanRain, 0.1f);
    }
    CHECK_REL(water.riverDischarge(0), meanQ, 0.01);
    CHECK_NEAR(water.riverStage(0), 0.6 * water.bankfullDepth(0), 0.01 * water.bankfullDepth(0)); // depth ~ Q^0.4 at Q = mean
    CHECK(!water.flooding());

    // 3) Recession without rain: monotonic decay, stays positive; stage follows (Q/Qmean)^0.4.
    float lastQ = water.riverDischarge(0);
    bool monotonic = true;
    for (int h = 0; h < 20 * 24; ++h)
    {
        water.update(1.0f, 0.0f, 0.2f);
        const float q = water.riverDischarge(0);
        monotonic = monotonic && q <= lastQ + 1e-4f && q > 0.0f;
        lastQ = q;
    }
    CHECK(monotonic);
    const float ratio = water.riverDischarge(0) / meanQ;
    CHECK_REL(water.riverStage(0), water.bankfullDepth(0) * 0.6f * std::pow(ratio, 0.4f), 1e-3);
    NOTE("after 20 dry days: Q/Qmean = %.3f, stage %.2f m (bankfull %.2f m)", static_cast<double>(ratio), static_cast<double>(water.riverStage(0)),
         static_cast<double>(water.bankfullDepth(0)));

    // 4) Storm: stage rises, flooding only above bankfull, stage capped.
    bool floodedConsistently = true;
    for (int h = 0; h < 72; ++h)
    {
        water.update(1.0f, 25.0f, 0.0f);
        floodedConsistently = floodedConsistently && (water.flooding() == (water.riverStage(0) > water.bankfullDepth(0)));
        CHECK_LE(water.riverStage(0), water.bankfullDepth(0) * 1.6f + 1e-4f);
    }
    CHECK(floodedConsistently);
    NOTE("after 72 h at 25 mm/h: stage %.2f m, flooding = %s", static_cast<double>(water.riverStage(0)), water.flooding() ? "yes" : "no");

    // 5) Lakes stay within [0, maxDepth] under extreme wet and dry forcing; drought lowers them.
    const std::vector<float> st = water.saveState();
    for (size_t l = 0; l < water.lakeCount(); ++l)
    {
        CHECK_RANGE(water.lakeDepth(static_cast<int>(l)), 0.0, layout.lakes[l].maxDepth + 1e-4);
    }
    std::vector<float> before(water.lakeCount());
    for (size_t l = 0; l < water.lakeCount(); ++l)
    {
        before[l] = water.lakeDepth(static_cast<int>(l));
    }
    for (int h = 0; h < 30 * 24; ++h)
    {
        water.update(1.0f, 0.0f, 0.6f);
    }
    for (size_t l = 0; l < water.lakeCount(); ++l)
    {
        const float d = water.lakeDepth(static_cast<int>(l));
        CHECK_RANGE(d, 0.0, layout.lakes[l].maxDepth + 1e-4);
        CHECK_LE(d, before[l] + 1e-5f);
    }
    CHECK_EQ(st.size(), riverCount * 3 + water.lakeCount());
}

// ---------------------------------------------------------------- Vegetation

NOCTIS_TEST("world.vegetation.logistic_growth_bounds")
{
    const EvidenceDatabase* db = sharedDb();
    REQUIRE(db != nullptr);
    const EnvironmentDefinition* env = db->findEnvironment("hell_creek");
    REQUIRE(env != nullptr);
    Terrain t;
    TerrainGenerator::generate(env->sim, 7u, t, 257, 128);
    t.updateMoisture(24.0f, 2.0f, 0.0f); // moist soil: growth is not water-limited
    VegetationSystem veg;
    veg.build(env->sim, t, 7u, 64.0f);
    SerialScheduler jobs;
    const int cells = veg.width() * veg.height();
    REQUIRE_GT(cells, 0);
    // Strip a set of cells completely.
    float prefs[kPlantTypeCount];
    std::fill(prefs, prefs + kPlantTypeCount, 1.0f);
    std::vector<int> stripped;
    Rng rng(11u, 1u);
    for (int k = 0; k < 40; ++k)
    {
        const Vec2 p{rng.range(500.0f, t.size() - 500.0f), rng.range(500.0f, t.size() - 500.0f)};
        veg.consume(p, 0.0f, 50.0f, prefs, 1.0e9f);
        stripped.push_back(veg.cellIndexAt(p));
    }
    double strippedBefore = 0.0;
    double strippedCapacity = 0.0;
    for (const int c : stripped)
    {
        for (int ty = 0; ty < kPlantTypeCount; ++ty)
        {
            strippedBefore += veg.biomass(c, static_cast<PlantType>(ty));
            strippedCapacity += veg.capacity(c, static_cast<PlantType>(ty));
        }
    }
    VegetationClimate good;
    good.airTempC = 24.0f;
    good.rainMmPerHour = 0.2f;
    good.seasonGrowthFactor = 1.0f;
    const double total0 = veg.totalBiomassKg();
    int violations = 0;
    bool strippedMonotonic = true;
    std::vector<float> last(stripped.size() * kPlantTypeCount, 0.0f);
    for (size_t i = 0; i < stripped.size(); ++i)
    {
        for (int ty = 0; ty < kPlantTypeCount; ++ty)
        {
            last[i * kPlantTypeCount + static_cast<size_t>(ty)] = veg.biomass(stripped[i], static_cast<PlantType>(ty));
        }
    }
    for (int day = 0; day < 730; ++day)
    {
        veg.update(1.0f, good, t, jobs);
        if (day % 73 == 0 || day == 729)
        {
            for (int c = 0; c < cells; ++c)
            {
                for (int ty = 0; ty < kPlantTypeCount; ++ty)
                {
                    const float b = veg.biomass(c, static_cast<PlantType>(ty));
                    const float k = veg.capacity(c, static_cast<PlantType>(ty));
                    violations += (std::isfinite(b) && b >= 0.0f && b <= k * 1.05f + 1e-6f) ? 0 : 1;
                }
            }
        }
        for (size_t i = 0; i < stripped.size(); ++i)
        {
            for (int ty = 0; ty < kPlantTypeCount; ++ty)
            {
                const float b = veg.biomass(stripped[i], static_cast<PlantType>(ty));
                float& l = last[i * kPlantTypeCount + static_cast<size_t>(ty)];
                // Regrowth below capacity is monotonic (logistic regime, turnover << growth).
                if (b < 0.9f * veg.capacity(stripped[i], static_cast<PlantType>(ty)) && b + 1e-6f < l)
                {
                    strippedMonotonic = false;
                }
                l = b;
            }
        }
    }
    double strippedAfter = 0.0;
    for (const int c : stripped)
    {
        for (int ty = 0; ty < kPlantTypeCount; ++ty)
        {
            strippedAfter += veg.biomass(c, static_cast<PlantType>(ty));
        }
    }
    NOTE("stripped cells: %.3f -> %.3f kg/m2 summed (capacity %.3f) after 2 years; total biomass %.3g -> %.3g kg", strippedBefore, strippedAfter,
         strippedCapacity, total0, veg.totalBiomassKg());
    CHECK_EQ(violations, 0);
    CHECK(strippedMonotonic);
    CHECK_GT(strippedAfter, strippedBefore);
    CHECK_LE(strippedAfter, strippedCapacity * 1.05 + 1e-6);
    // Dormancy (no phenological growth): biomass can only decline.
    VegetationClimate dormant = good;
    dormant.seasonGrowthFactor = 0.0f;
    const double beforeDormant = veg.totalBiomassKg();
    for (int day = 0; day < 30; ++day)
    {
        veg.update(1.0f, dormant, t, jobs);
    }
    CHECK_LE(veg.totalBiomassKg(), beforeDormant * (1.0 + 1e-6));
}

// ---------------------------------------------------------------- Sky

NOCTIS_TEST("world.atmosphere.sun_position")
{
    // Pure spherical astronomy with the declination model decl = -tilt * cos(2 pi (doy + 10) / year):
    // equinox at doy 81.25 (365-day year), June solstice at doy 172.5, December solstice at doy 355.
    const float lat = 45.0f;
    const float tilt = 23.44f;
    float el = 0.0f;
    float az = 0.0f;
    SkySystem::solarPosition(lat, tilt, 81.25, 365.0, 0.5, el, az);
    CHECK_NEAR(el, 90.0 - 45.0, 0.01);
    CHECK_NEAR(az, 180.0, 0.05); // due south at solar noon
    SkySystem::solarPosition(lat, tilt, 172.5, 365.0, 0.5, el, az);
    CHECK_NEAR(el, 90.0 - 45.0 + 23.44, 0.01);
    SkySystem::solarPosition(lat, tilt, 355.0, 365.0, 0.5, el, az);
    CHECK_NEAR(el, 90.0 - 45.0 - 23.44, 0.01);
    // Equinox sunrise and sunset: on the horizon, due east / due west.
    SkySystem::solarPosition(lat, tilt, 81.25, 365.0, 0.25, el, az);
    CHECK_NEAR(el, 0.0, 0.01);
    CHECK_NEAR(az, 90.0, 0.05);
    SkySystem::solarPosition(lat, tilt, 81.25, 365.0, 0.75, el, az);
    CHECK_NEAR(el, 0.0, 0.01);
    CHECK_NEAR(az, 270.0, 0.05);
    // Midnight at the equinox: depressed by the co-latitude.
    SkySystem::solarPosition(lat, tilt, 81.25, 365.0, 0.0, el, az);
    CHECK_NEAR(el, -45.0, 0.01);
    // Equator, equinox noon: zenith.
    SkySystem::solarPosition(0.0f, tilt, 81.25, 365.0, 0.5, el, az);
    CHECK_NEAR(el, 90.0, 0.01);
    // Paleo-calendar (372 days): equinox shifts proportionally.
    SkySystem::solarPosition(lat, tilt, 0.25 * 372.0 - 10.0, 372.0, 0.5, el, az);
    CHECK_NEAR(el, 45.0, 0.01);
    // Southern hemisphere June solstice noon: low sun due north.
    SkySystem::solarPosition(-45.0f, tilt, 172.5, 365.0, 0.5, el, az);
    CHECK_NEAR(el, 90.0 - 45.0 - 23.44, 0.01);
    CHECK(az < 0.5f || az > 359.5f);
}

NOCTIS_TEST("world.atmosphere.illuminance_and_photoperiod")
{
    // Monotonic in elevation, anchored to standard twilight values.
    float last = 0.0f;
    for (float e = -30.0f; e <= 90.0f; e += 0.5f)
    {
        const float lux = SkySystem::clearSkyIlluminance(e);
        CHECK_GE(lux, last);
        last = lux;
    }
    CHECK_NEAR(SkySystem::clearSkyIlluminance(0.0f), 400.0, 1e-3);
    CHECK_NEAR(SkySystem::clearSkyIlluminance(-6.0f), 3.4, 1e-4);
    CHECK_NEAR(SkySystem::clearSkyIlluminance(-12.0f), 0.008, 1e-6);
    CHECK_RANGE(SkySystem::clearSkyIlluminance(90.0f), 1.0e5, 1.3e5);
    // Photoperiod: 12 h at the equinox, acos(-tan(lat) tan(decl)) / pi * 24 h at the solstice.
    CalendarSpec spec;
    SimClock clock;
    SkySystem sky;
    sky.configure(45.0f, 23.44f);
    // The clock's day-of-year includes the time of day: day 81 at 12:00 is doy 81.5, 0.25 d after the
    // model's equinox (declination +0.1 deg, photoperiod 12.01 h).
    clock.configure(spec, 81.0, 12.0, 0);
    sky.update(clock, 0.0f);
    CHECK_NEAR(sky.state().photoperiodHours, 12.0, 0.03);
    CHECK(sky.state().phase == DayPhase::Day);
    clock.configure(spec, 172.0, 12.0, 0); // doy 172.5 = June solstice of the model, at noon
    sky.update(clock, 0.0f);
    const double expected = 24.0 * std::acos(-std::tan(45.0 * 3.14159265358979 / 180.0) * std::tan(23.44 * 3.14159265358979 / 180.0)) / 3.14159265358979;
    CHECK_NEAR(sky.state().photoperiodHours, expected, 0.01);
    NOTE("photoperiod at 45 N, June solstice: %.2f h (expected %.2f h)", static_cast<double>(sky.state().photoperiodHours), expected);
    // Clouds dim daylight.
    const float clear = sky.state().illuminanceLux;
    sky.update(clock, 1.0f);
    CHECK_LT(sky.state().illuminanceLux, clear * 0.5f);
    clock.configure(spec, 172.0, 0.0, 0); // solstice midnight: sun 21.6 deg below the horizon
    sky.update(clock, 0.0f);
    CHECK_NEAR(sky.state().sunElevationDeg, -(90.0 - 45.0 - 23.44), 0.2);
    CHECK(sky.state().phase == DayPhase::Night);
    CHECK_LT(sky.state().illuminanceLux, 1.0f);
}

// ---------------------------------------------------------------- Sound

NOCTIS_TEST("world.sound.iso9613_1_absorption")
{
    // Reference: ISO 9613-2:1996 Table 2 (atmospheric attenuation coefficient, dB/km, computed from
    // ISO 9613-1 at the exact midband frequencies 1000 * 10^(3k/10), p = 101.325 kPa), rounded to 0.1.
    // Re-derived independently from the ISO 9613-1 equations (python, double precision) for this test;
    // both sets agree within the table's rounding.
    struct Row
    {
        float tempC;
        float rh;
        double dbPerKm[8]; // 63, 125, 250, 500, 1k, 2k, 4k, 8k Hz
    };
    const Row rows[] = {
        {10.0f, 0.70f, {0.1, 0.4, 1.0, 1.9, 3.7, 9.7, 32.8, 117.0}},
        {20.0f, 0.70f, {0.1, 0.3, 1.1, 2.8, 5.0, 9.0, 22.9, 76.6}},
        {30.0f, 0.70f, {0.1, 0.3, 1.0, 3.1, 7.4, 12.7, 23.1, 59.3}},
        {15.0f, 0.20f, {0.3, 0.6, 1.2, 2.7, 8.2, 28.2, 88.8, 202.0}},
        {15.0f, 0.50f, {0.1, 0.5, 1.2, 2.2, 4.2, 10.8, 36.2, 129.0}},
        {15.0f, 0.80f, {0.1, 0.3, 1.1, 2.4, 4.1, 8.3, 23.7, 82.8}},
    };
    double worst = 0.0;
    for (const Row& r : rows)
    {
        for (int k = 0; k < 8; ++k)
        {
            const double f = isoMidband(k - 4);
            const double a = 1000.0 * SoundField::absorptionDbPerM(static_cast<float>(f), r.tempC, r.rh);
            const double ref = r.dbPerKm[k];
            // Table values are rounded to 0.1 dB/km (1 dB/km above 100): allow half a unit plus 0.5 %.
            const double tol = (ref >= 100.0 ? 0.5 : 0.05) + 0.005 * ref + 1e-6;
            worst = std::max(worst, std::fabs(a - ref) / tol);
            if (!(std::fabs(a - ref) <= tol))
            {
                noctistest::failf(__FILE__, __LINE__, "alpha(%.0f Hz, %.0f C, %.0f %%) = %.3f dB/km, ISO table %.1f", f, static_cast<double>(r.tempC),
                                  static_cast<double>(r.rh) * 100.0, a, ref);
            }
        }
    }
    // Unrounded values from the standard's equations (20 C, 70 %, exact midbands).
    CHECK_REL(1000.0 * SoundField::absorptionDbPerM(static_cast<float>(isoMidband(0)), 20.0f, 0.7f), 4.978, 2e-3);
    CHECK_REL(1000.0 * SoundField::absorptionDbPerM(static_cast<float>(isoMidband(1)), 20.0f, 0.7f), 9.016, 2e-3);
    CHECK_REL(1000.0 * SoundField::absorptionDbPerM(static_cast<float>(isoMidband(3)), 20.0f, 0.7f), 76.621, 2e-3);
    CHECK_REL(1000.0 * SoundField::absorptionDbPerM(static_cast<float>(isoMidband(-3)), 10.0f, 0.7f), 0.411, 5e-3);
    NOTE("worst deviation from ISO 9613-2 Table 2: %.2f of the allowed tolerance", worst);
    // Lower pressure (altitude) changes absorption; result stays finite and positive.
    const float alt = SoundField::absorptionDbPerM(2000.0f, 15.0f, 0.5f, 80.0f);
    CHECK(std::isfinite(alt) && alt > 0.0f);
}

NOCTIS_TEST("world.sound.spherical_spreading")
{
    SoundSource s;
    s.levelDb = 100.0f;
    s.f0Hz = 1000.0f;
    s.position = Vec3{0.0f, 0.0f, 2.0f};
    PropagationEnv env; // no vegetation, no terrain, no wind
    env.temperatureC = 20.0f;
    env.humidity = 0.7f;
    for (const float r : {10.0f, 100.0f, 1000.0f})
    {
        const float level = SoundField::receivedLevelDb(s, Vec3{r, 0.0f, 2.0f}, env);
        const double expected = 100.0 - 20.0 * std::log10(r) - SoundField::absorptionDbPerM(1000.0f, 20.0f, 0.7f) * r;
        CHECK_NEAR(level, expected, 1e-3);
    }
    // Doubling distance: -6.02 dB of spreading plus the extra absorption.
    const float l1 = SoundField::receivedLevelDb(s, Vec3{200.0f, 0.0f, 2.0f}, env);
    const float l2 = SoundField::receivedLevelDb(s, Vec3{400.0f, 0.0f, 2.0f}, env);
    CHECK_NEAR(l1 - l2, 6.0206 + 200.0 * SoundField::absorptionDbPerM(1000.0f, 20.0f, 0.7f), 1e-3);
    // Upwind listener is shadowed relative to downwind.
    env.wind = Vec2{5.0f, 0.0f};
    const float down = SoundField::receivedLevelDb(s, Vec3{500.0f, 0.0f, 2.0f}, env);
    const float up = SoundField::receivedLevelDb(s, Vec3{-500.0f, 0.0f, 2.0f}, env);
    CHECK_GT(down, up);
    // Foliage attenuation (ISO 9613-2 Annex A) increases with frequency.
    CHECK_LT(SoundField::foliageDbPerM(63.0f), SoundField::foliageDbPerM(8000.0f));
}

NOCTIS_TEST("world.sound.field_thread_safe_collect")
{
    SoundField field;
    std::unique_ptr<JobScheduler> jobs = makeThreadPoolScheduler(4);
    jobs->parallelFor(4000, [&](size_t i) {
        SoundSource s;
        s.startTime = static_cast<double>(i % 100);
        s.durationS = 1.0f;
        s.position = Vec3{static_cast<float>(i % 50), 0.0f, 0.0f};
        field.emit(s);
    }, 16);
    CHECK_EQ(field.size(), size_t(4000));
    std::vector<SoundSource> out;
    field.collect(10.0, 10.5, out);
    // Sources starting at 9 (overlapping until 10) and at 10.
    CHECK_EQ(out.size(), size_t(80));
    std::vector<u32> ids;
    out.clear();
    field.collect(-1.0, 1e9, out);
    for (const SoundSource& s : out)
    {
        ids.push_back(s.id);
    }
    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end()); // unique ids
    field.prune(200.0, 20.0);
    CHECK_EQ(field.size(), size_t(0));
}
