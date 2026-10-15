// Creature biomechanics and physiology: Froude dynamic similarity, Nagy field metabolic rates,
// physiological state bounds under long integrations.
#include "TestFramework.h"
#include "TestSupport.h"

#include "Noctis/Creatures/CreatureLogic.h"
#include "Noctis/Sim/WorldSimulation.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace noctis;

namespace
{
std::unique_ptr<WorldSimulation> emptyWorld()
{
    SimConfig cfg = noctistest::smallWorldConfig(5u, 1, 0.0f);
    cfg.spawnPopulation = false;
    cfg.initialRemains = false;
    return noctistest::makeWorld(cfg);
}

int agentSpecies(const WorldSimulation& w, const char* id)
{
    const int si = w.db().speciesIndex(id);
    if (si < 0 || w.db().speciesAt(si).sim.role != SimRole::Agent)
    {
        return -1;
    }
    return si;
}

// Spawns an adult on dry, gentle ground and returns its id.
EntityId spawnAdult(WorldSimulation& w, int si, u64 seed)
{
    Rng rng(seed, 3u);
    const SpeciesSim& s = w.db().speciesAt(si).sim;
    Vec2 best = w.findHabitatPoint(si, rng);
    float bestSlope = 1e9f;
    for (int i = 0; i < 200; ++i)
    {
        const Vec2 p = w.findHabitatPoint(si, rng);
        const float slope = w.terrain().slopeDegAt(p);
        if (w.water().depthAt(p, w.terrain()) <= 0.0f && slope < bestSlope)
        {
            bestSlope = slope;
            best = p;
        }
    }
    Creature& c = w.creatures().spawn(si, Sex::Female, std::max(1.0f, s.growth.maturityAgeYears * 1.5f), best, w.context());
    return c.id;
}
} // namespace

NOCTIS_TEST("creatures.locomotion.froude_stride_relation")
{
    std::unique_ptr<WorldSimulation> w = emptyWorld();
    REQUIRE(w != nullptr);
    int tested = 0;
    for (const char* id : {"tyrannosaurus_rex", "edmontosaurus_annectens", "triceratops_horridus", "struthiomimus_sedens"})
    {
        const int si = agentSpecies(*w, id);
        if (si < 0)
        {
            continue;
        }
        const SpeciesRuntime& sp = w->species()[static_cast<size_t>(si)];
        const EntityId cid = spawnAdult(*w, si, static_cast<u64>(si) + 1u);
        float lastStride = 0.0f;
        float lastDuty = 2.0f;
        for (const float frac : {0.15f, 0.3f, 0.5f, 0.75f, 1.0f})
        {
            Creature* c = w->creatures().find(cid);
            REQUIRE(c != nullptr);
            const float target = frac * sp.sim().locomotion.maxSpeedMs;
            c->behavior.desiredSpeed = target;
            c->behavior.desiredDir = Vec2::fromHeading(c->loco.heading);
            for (int k = 0; k < 200; ++k) // 10 s at 0.05 s
            {
                c->behavior.desiredDir = Vec2::fromHeading(c->loco.heading);
                integrateLocomotion(*c, sp, w->context(), 0.05f, true);
                c->pendingFootprints.clear();
            }
            const LocomotionState& L = c->loco;
            const float hip = c->hipHeightM;
            const double fr = static_cast<double>(L.speed) * L.speed / (9.81 * hip);
            CHECK_REL(L.froude, fr, 1e-4);
            // Alexander (1976): relative stride length lambda/h = 2.3 Fr^0.3 (floored at 0.5 at very low speed).
            const double rel = std::max(0.5, 2.3 * std::pow(std::max(1e-4, fr), 0.3));
            CHECK_REL(L.strideLengthM / hip, rel, 1e-3);
            if (L.speed > 0.05f)
            {
                CHECK_REL(L.strideFrequencyHz * L.strideLengthM, L.speed, 1e-3); // v = stride length x stride frequency
            }
            // Gait by dynamic similarity: walking below Fr 0.15, no aerial phase while duty >= 0.5.
            if (L.speed >= 0.05f && L.froude < 0.15f)
            {
                CHECK(L.gait == Gait::Walk);
            }
            if (L.gait == Gait::Run)
            {
                CHECK_LT(L.dutyFactor, 0.5f);
            }
            CHECK_GE(L.strideLengthM + 1e-5f, lastStride); // stride lengthens with speed
            CHECK_LE(L.dutyFactor, lastDuty + 1e-5f);      // duty factor falls with speed
            lastStride = L.strideLengthM;
            lastDuty = L.dutyFactor;
            NOTE("%-26s v=%5.2f m/s (target %5.2f) Fr=%.3f stride/h=%.3f (2.3 Fr^0.3 = %.3f) duty=%.2f gait=%s", id, static_cast<double>(L.speed),
                 static_cast<double>(target), fr, static_cast<double>(L.strideLengthM / hip), 2.3 * std::pow(std::max(1e-4, fr), 0.3),
                 static_cast<double>(L.dutyFactor), gaitLabelFr(L.gait));
        }
        ++tested;
    }
    CHECK_GE(tested, 2);
}

NOCTIS_TEST("creatures.metabolism.nagy_fmr_scaling")
{
    // Nagy (2005, J. Exp. Biol. 208:1621): FMR [kJ/day] = a * M[g]^b; mammals a=4.82 b=0.734,
    // birds a=10.5 b=0.681, reptiles a=0.196 b=0.889.
    SpeciesRuntime mammal;
    mammal.fmrA = 4.82f;
    mammal.fmrB = 0.734f;
    mammal.fmrMesoBlend = 0.0f;
    SpeciesRuntime bird = mammal;
    bird.fmrA = 10.5f;
    bird.fmrB = 0.681f;
    SpeciesRuntime meso = bird;
    meso.fmrMesoBlend = 0.5f;
    SpeciesRuntime reptile = mammal;
    reptile.fmrA = 0.196f;
    reptile.fmrB = 0.889f;
    reptile.fmrMesoBlend = 1.0f;
    const double kjToW = 1000.0 / 86400.0;
    CHECK_REL(fieldMetabolicRateW(mammal, 1.0f), 4.82 * std::pow(1000.0, 0.734) * kjToW, 1e-4); // 8.87 W
    CHECK_REL(fieldMetabolicRateW(bird, 1.0f), 10.5 * std::pow(1000.0, 0.681) * kjToW, 1e-4);
    CHECK_REL(fieldMetabolicRateW(reptile, 1.0f), 0.196 * std::pow(1000.0, 0.889) * kjToW, 1e-4);
    // Mesotherm = geometric mean of avian and reptilian rates.
    CHECK_REL(fieldMetabolicRateW(meso, 100.0f), std::sqrt(static_cast<double>(fieldMetabolicRateW(bird, 100.0f)) * fieldMetabolicRateW(reptile, 100.0f)),
              1e-4);
    for (const SpeciesRuntime* sp : {&mammal, &bird, &meso, &reptile})
    {
        float lastW = 0.0f;
        float lastPerKg = 1e9f;
        for (const float m : {0.01f, 1.0f, 10.0f, 100.0f, 1000.0f, 7000.0f})
        {
            const float wv = fieldMetabolicRateW(*sp, m);
            CHECK_GT(wv, lastW);         // absolute FMR increases with mass ...
            CHECK_LT(wv / m, lastPerKg); // ... mass-specific FMR decreases (b < 1)
            lastW = wv;
            lastPerKg = wv / m;
        }
    }
    // Endotherms spend more than mesotherms, which spend more than ectotherms, at dinosaur sizes.
    for (const float m : {10.0f, 500.0f, 7000.0f})
    {
        CHECK_GT(fieldMetabolicRateW(bird, m), fieldMetabolicRateW(meso, m));
        CHECK_GT(fieldMetabolicRateW(meso, m), fieldMetabolicRateW(reptile, m));
    }
    NOTE("7-t animal: bird eq. %.0f W, mesotherm %.0f W, reptile eq. %.0f W", static_cast<double>(fieldMetabolicRateW(bird, 7000.0f)),
         static_cast<double>(fieldMetabolicRateW(meso, 7000.0f)), static_cast<double>(fieldMetabolicRateW(reptile, 7000.0f)));
    // Every species of the data set gets a scaling exponent in the allometric range.
    std::unique_ptr<WorldSimulation> w = emptyWorld();
    REQUIRE(w != nullptr);
    for (const SpeciesRuntime& sp : w->species())
    {
        CHECK_RANGE(sp.fmrB, 0.6, 0.95);
        CHECK_GT(fieldMetabolicRateW(sp, sp.sim().adultMassKg), fieldMetabolicRateW(sp, sp.sim().hatchlingMassKg));
    }
}

NOCTIS_TEST("creatures.physiology.bounds_under_long_integration")
{
    std::unique_ptr<WorldSimulation> w = emptyWorld();
    REQUIRE(w != nullptr);
    struct Scheme
    {
        float dt;
        float seconds;
    };
    // Full-LOD steps, Far-LOD body interval, and coarse time-skip steps.
    const Scheme schemes[] = {{0.1f, 1800.0f}, {4.0f, 2.0f * 86400.0f}, {600.0f, 20.0f * 86400.0f}};
    int checked = 0;
    for (const int si : w->speciesInWorld())
    {
        if (w->db().speciesAt(si).sim.role != SimRole::Agent)
        {
            continue;
        }
        const SpeciesRuntime& sp = w->species()[static_cast<size_t>(si)];
        for (const Scheme& sc : schemes)
        {
            for (const bool starving : {false, true})
            {
                const EntityId id = spawnAdult(*w, si, static_cast<u64>(si) * 7u + (starving ? 1u : 0u));
                Creature* c = w->creatures().find(id);
                REQUIRE(c != nullptr);
                if (starving)
                {
                    c->phys.gutFillKg = 0.0f;
                    c->phys.energyReserveMJ = c->phys.energyCapacityMJ * 0.02f;
                    c->phys.hydration = 0.1f;
                }
                const int steps = static_cast<int>(sc.seconds / sc.dt);
                std::string what;
                bool ok = true;
                for (int k = 0; k < steps && ok; ++k)
                {
                    updatePhysiology(*c, sp, w->context(), sc.dt);
                    updateInjuries(*c, sp, w->context(), sc.dt);
                    ok = noctistest::creatureFinite(*c, &what) && noctistest::creatureInBounds(*c, &what);
                    if (!ok)
                    {
                        noctistest::failf(__FILE__, __LINE__, "%s, dt %.1f s%s, after %.0f s: %s", w->db().speciesAt(si).id.c_str(),
                                          static_cast<double>(sc.dt), starving ? " (starving)" : "", static_cast<double>(k) * sc.dt, what.c_str());
                    }
                }
                ++checked;
                // Starving animals lose condition, never gain it without food.
                if (starving && ok)
                {
                    CHECK_LE(c->phys.condition, 0.02f + 1e-4f);
                }
                c->alive = false; // keep the world small: retire the test animal
            }
        }
    }
    NOTE("%d species x dt-scheme x feeding-state combinations integrated", checked);
    CHECK_GT(checked, 10);
}

NOCTIS_TEST("creatures.growth.mass_at_age_monotonic")
{
    std::unique_ptr<WorldSimulation> w = emptyWorld();
    REQUIRE(w != nullptr);
    for (const int si : w->speciesInWorld())
    {
        const SpeciesSim& s = w->db().speciesAt(si).sim;
        if (s.role != SimRole::Agent)
        {
            continue;
        }
        const SpeciesRuntime& sp = w->species()[static_cast<size_t>(si)];
        float last = 0.0f;
        for (float age = 0.0f; age <= s.growth.maxLifespanYears; age += std::max(0.05f, s.growth.maxLifespanYears / 60.0f))
        {
            const float m = massAtAge(sp, age, 1.0f);
            if (!(std::isfinite(m) && m >= last - 1e-4f * std::max(1.0f, last) && m > 0.0f))
            {
                noctistest::failf(__FILE__, __LINE__, "%s: mass %.3f kg at %.2f y after %.3f kg", w->db().speciesAt(si).id.c_str(), static_cast<double>(m),
                                  static_cast<double>(age), static_cast<double>(last));
                break;
            }
            last = m;
        }
        CHECK_RANGE(massAtAge(sp, s.growth.maxLifespanYears, 1.0f), 0.5 * s.adultMassKg, 1.6 * s.growth.asymptoticMassKg + 1e-3);
        CHECK_LE(massAtAge(sp, 0.0f, 1.0f), s.adultMassKg * 0.5f);
    }
}
