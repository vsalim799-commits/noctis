// Creature biomechanics and physiology: Froude dynamic similarity, Nagy field metabolic rates,
// physiological state bounds under long integrations.
#include "TestFramework.h"
#include "TestSupport.h"

#include "Noctis/Creatures/CreatureLogic.h"
#include "Noctis/Sim/WorldSimulation.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

using namespace noctis;

namespace
{
std::unique_ptr<WorldSimulation> emptyWorld(double startHour = 5.0)
{
    SimConfig cfg = noctistest::smallWorldConfig(5u, 1, 0.0f);
    cfg.spawnPopulation = false;
    cfg.initialRemains = false;
    cfg.startHour = startHour;
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
        float lastSpeed = 0.0f;
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
            // Stride lengthens and duty factor falls with speed (compared only when the animal actually went
            // faster: at the highest targets fatigue and terrain can cap the speed reached).
            if (L.speed >= lastSpeed)
            {
                CHECK_GE(L.strideLengthM + 1e-5f, lastStride);
                CHECK_LE(L.dutyFactor, lastDuty + 1e-5f);
            }
            lastStride = L.strideLengthM;
            lastDuty = L.dutyFactor;
            lastSpeed = L.speed;
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
    // Every species of the data set gets a scaling exponent in the allometric range, and individually
    // simulated species spend more as adults than as hatchlings (population-level taxa such as insects
    // sit below the 1 g floor of the equation and are not individually simulated).
    std::unique_ptr<WorldSimulation> w = emptyWorld();
    REQUIRE(w != nullptr);
    for (const SpeciesRuntime& sp : w->species())
    {
        CHECK_RANGE(sp.fmrB, 0.6, 0.95);
        if (sp.sim().role == SimRole::Agent)
        {
            CHECK_GT(fieldMetabolicRateW(sp, sp.sim().adultMassKg), fieldMetabolicRateW(sp, sp.sim().hatchlingMassKg));
        }
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

NOCTIS_TEST("creatures.locomotion.heading_converges_at_coarse_step")
{
    // Near/Far LOD integrate locomotion in 1 s sub-steps. The heading controller must converge there too
    // (explicit Euler with gain 2.5/s used to flip the error sign every step for fast-turning species).
    std::unique_ptr<WorldSimulation> w = emptyWorld();
    REQUIRE(w != nullptr);
    int tested = 0;
    for (const char* id : {"didelphodon_vorax", "acheroraptor_temertyorum", "triceratops_horridus"})
    {
        const int si = agentSpecies(*w, id);
        if (si < 0)
        {
            continue;
        }
        const SpeciesRuntime& sp = w->species()[static_cast<size_t>(si)];
        for (const float h : {1.0f, 0.1f})
        {
            const EntityId cid = spawnAdult(*w, si, static_cast<u64>(si) * 13u + 5u);
            Creature* c = w->creatures().find(cid);
            REQUIRE(c != nullptr);
            const float goal = wrapAngle(c->loco.heading + 1.6f); // ~92 deg to the left
            c->behavior.desiredDir = Vec2::fromHeading(goal);
            c->behavior.desiredSpeed = sp.sim().locomotion.walkSpeedMs * 0.8f;
            int flips = 0;
            float lastErr = angleDelta(c->loco.heading, goal);
            const int steps = static_cast<int>(30.0f / h);
            for (int k = 0; k < steps; ++k)
            {
                integrateLocomotion(*c, sp, w->context(), h, false);
                c->pendingFootprints.clear();
                const float err = angleDelta(c->loco.heading, goal);
                flips += (err * lastErr < 0.0f && std::fabs(err) > 0.01f) ? 1 : 0;
                lastErr = err;
            }
            CHECK_LT(std::fabs(lastErr), 0.02f);
            CHECK_LE(flips, 1);
            NOTE("%-26s h=%.1f s: final heading error %.4f rad, %d sign flips", id, static_cast<double>(h), static_cast<double>(lastErr), flips);
            c->alive = false;
        }
        ++tested;
    }
    CHECK_GE(tested, 2);
}

NOCTIS_TEST("creatures.death_cause.attribution")
{
    // Only wounds inflicted by a hunting predator make a death "predation"; a wound from another species
    // received in a defensive fight or as a counter-attack is an injury death; infection wins when severe.
    std::unique_ptr<WorldSimulation> w = emptyWorld();
    REQUIRE(w != nullptr);
    const int rex = agentSpecies(*w, "tyrannosaurus_rex");
    const int trike = agentSpecies(*w, "triceratops_horridus");
    if (rex < 0 || trike < 0)
    {
        NOTE("T. rex or Triceratops absent from the data set: skipped");
        return;
    }
    struct Case
    {
        int victim;
        int causeSpecies;
        bool predatory;
        float infection;
        DeathCause expected;
        const char* what;
    };
    const Case cases[] = {
        {rex, trike, false, 0.0f, DeathCause::Injury, "T. rex bleeding from a Triceratops counter-attack"},
        {trike, rex, true, 0.0f, DeathCause::Predation, "Triceratops bleeding from a hunting T. rex"},
        {trike, trike, false, 0.0f, DeathCause::Injury, "Triceratops bleeding from an intraspecific fight"},
        {trike, rex, true, 0.8f, DeathCause::Infection, "Triceratops with an infected wound"},
    };
    u64 seed = 101u;
    for (const Case& k : cases)
    {
        const EntityId id = spawnAdult(*w, k.victim, seed++);
        Creature* c = w->creatures().find(id);
        REQUIRE(c != nullptr);
        Injury inj;
        inj.region = BodyRegion::Torso;
        inj.type = InjuryType::Puncture;
        inj.severity = 0.5f;
        inj.initialSeverity = 0.5f;
        inj.bleeding = 2.0f; // fraction of blood volume per hour: exaggerated so the test is short
        inj.infection = k.infection;
        inj.time = w->context().now;
        inj.causeSpecies = static_cast<i16>(k.causeSpecies);
        inj.predatory = k.predatory;
        c->injuries.active.push_back(inj);
        for (int s = 0; s < 3600; ++s)
        {
            w->step(1.0);
            const Creature* x = w->creatures().find(id);
            if (!x || !x->alive)
            {
                break;
            }
        }
        const DeathRecord* rec = nullptr;
        for (const DeathRecord& d : w->creatures().deaths())
        {
            if (d.id == id)
            {
                rec = &d;
            }
        }
        DeathCause got = DeathCause::Unknown;
        if (rec)
        {
            got = rec->cause;
        }
        else if (const Creature* x = w->creatures().find(id); x && !x->alive)
        {
            got = x->deathCause;
        }
        if (got != k.expected)
        {
            noctistest::failf(__FILE__, __LINE__, "%s: cause %s, expected %s", k.what, deathCauseLabelFr(got), deathCauseLabelFr(k.expected));
        }
        NOTE("%-50s -> %s", k.what, deathCauseLabelFr(got));
    }
}

NOCTIS_TEST("creatures.drink.stale_water_memory_retarget")
{
    // A thirsty animal whose nearest water memory is the dry spot where it stands (left there after drinking with only
    // its muzzle in the channel), 12-20 m from the nearest drink site and facing away from it, must give up that memory
    // and walk to the site. It used to re-target the same dry spot at every decision and stand there until it died.
    std::unique_ptr<WorldSimulation> w = emptyWorld();
    REQUIRE(w != nullptr);
    const int si = agentSpecies(*w, "edmontosaurus_annectens");
    if (si < 0)
    {
        NOTE("Edmontosaurus absent from the data set: skipped");
        return;
    }
    const Terrain& t = w->terrain();
    const WaterSystem& wa = w->water();
    // A dry, gentle spot 14 m from a usable drink site, with no usable site within 12 m and an easy, dry walk to it.
    Vec2 spot;
    Vec2 site;
    bool found = false;
    const std::vector<DrinkSite>& sites = wa.drinkSites();
    for (size_t k = 0; k < sites.size() && !found; k += 3)
    {
        DrinkSite usable;
        if (!wa.findDrinkSite(sites[k].position, 0.5f, usable))
        {
            continue; // dried-up body
        }
        for (int a = 0; a < 8 && !found; ++a)
        {
            const Vec2 dir = Vec2::fromHeading(static_cast<float>(a) * kTwoPi / 8.0f);
            const Vec2 q = usable.position + dir * 14.0f;
            DrinkSite near;
            bool easy = wa.depthAt(q, t) <= 0.0f && t.slopeDegAt(q) < 6.0f && !wa.findDrinkSite(q, 12.0f, near);
            for (int s = 1; s <= 7 && easy; ++s)
            {
                const Vec2 r = q - dir * (2.0f * static_cast<float>(s));
                easy = wa.depthAt(r, t) <= 0.0f && std::fabs(t.heightAt(r) - t.heightAt(r + dir * 2.0f)) < 0.5f;
            }
            if (easy)
            {
                spot = q;
                site = usable.position;
                found = true;
            }
        }
    }
    REQUIRE(found);
    Creature& c0 = w->creatures().spawn(si, Sex::Female, 12.0f, spot, w->context());
    const EntityId id = c0.id;
    c0.loco.heading = (spot - site).heading(); // facing away from the water
    c0.memory.places.clear();
    rememberPlace(c0, MemoryKind::Water, spot, 0.6f, w->context().now);
    c0.phys.hydration = 0.3f;
    for (int m = 0; m < 15; ++m)
    {
        w->advance(60.0, 1.0);
    }
    const Creature* c = w->creatures().find(id);
    REQUIRE(c != nullptr && c->alive);
    CHECK_GT(c->phys.hydration, 0.5f);
    NOTE("hydration 0.30 -> %.2f after 15 min; moved %.1f m, now %.1f m from the drink site", static_cast<double>(c->phys.hydration),
         static_cast<double>(distance(c->pos2(), spot)), static_cast<double>(distance(c->pos2(), site)));
}

NOCTIS_TEST("creatures.hunting.terrestrial_predator_stops_at_deep_water")
{
    // A hungry Acheroraptor on the bank hunting a Champsosaurus in water deeper than the raptor can wade (> 0.9 x hip)
    // gives up (HuntAbandon, param 2) and never strikes it there. It used to swim in after it and kill it with about one
    // bite per second while both swam, the Champsosaurus fleeing at its land speed instead of its faster swim speed.
    // Several river sites, one fresh world each.
    std::vector<std::pair<Vec2, Vec2>> sites; // (prey in deep water, predator on the bank 14 m away)
    {
        std::unique_ptr<WorldSimulation> w = emptyWorld();
        REQUIRE(w != nullptr);
        const Terrain& t = w->terrain();
        const WaterSystem& wa = w->water();
        const Rect2 bounds = t.bounds();
        for (float y = bounds.min.y + 100.0f; y < bounds.max.y - 100.0f && sites.size() < 4; y += 37.0f)
        {
            for (float x = bounds.min.x + 100.0f; x < bounds.max.x - 100.0f && sites.size() < 4; x += 37.0f)
            {
                const Vec2 q{x, y};
                bool farFromOthers = true;
                for (const auto& st : sites)
                {
                    farFromOthers = farFromOthers && distance(st.first, q) > 800.0f;
                }
                if (!farFromOthers || wa.depthAt(q, t) < 1.5f)
                {
                    continue;
                }
                for (int a = 0; a < 16; ++a)
                {
                    const Vec2 b = q + Vec2::fromHeading(static_cast<float>(a) * kTwoPi / 16.0f) * 14.0f;
                    if (bounds.contains(b) && wa.depthAt(b, t) <= 0.0f && t.slopeDegAt(b) < 8.0f)
                    {
                        sites.emplace_back(q, b);
                        break;
                    }
                }
            }
        }
    }
    REQUIRE(!sites.empty());
    int deepStrikes = 0;
    int strikes = 0;
    int waterAbandons = 0;
    int preyDead = 0;
    for (const auto& st : sites)
    {
        std::unique_ptr<WorldSimulation> w = emptyWorld(19.5); // dusk: Acheroraptor is crepuscular
        REQUIRE(w != nullptr);
        const int raptor = agentSpecies(*w, "acheroraptor_temertyorum");
        const int champ = agentSpecies(*w, "champsosaurus");
        if (raptor < 0 || champ < 0)
        {
            NOTE("Acheroraptor or Champsosaurus absent from the data set: skipped");
            return;
        }
        const SpeciesSim& rs = w->db().speciesAt(raptor).sim;
        const SpeciesSim& cs = w->db().speciesAt(champ).sim;
        const EntityId pid = w->creatures().spawn(raptor, Sex::Male, std::max(1.0f, rs.growth.maturityAgeYears * 1.5f), st.second, w->context()).id;
        const EntityId qid = w->creatures().spawn(champ, Sex::Female, std::max(1.0f, cs.growth.maturityAgeYears * 1.5f), st.first, w->context()).id;
        float raptorHip = 0.5f;
        WorldSimulation* wp = w.get();
        const int handle = w->events().subscribe([&](const SimEvent& e) {
            if (e.type == EventType::Attack && e.subject == pid && e.other == qid)
            {
                ++strikes;
                deepStrikes += wp->water().depthAt(e.position.xy(), wp->terrain()) > 0.9f * raptorHip ? 1 : 0;
            }
            if (e.type == EventType::HuntAbandon && e.subject == pid && e.param == 2u)
            {
                ++waterAbandons;
            }
        });
        for (int s = 0; s < 240; ++s)
        {
            Creature* p = w->creatures().find(pid);
            REQUIRE(p != nullptr && p->alive);
            raptorHip = p->hipHeightM;
            p->phys.hunger = 0.95f;
            if (s == 5)
            {
                // Force the hunt on this individual (the predator has had 5 s to perceive it).
                p->behavior.current = BehaviorId::Hunt;
                p->behavior.hunt = HuntPhase::Stalk;
                p->behavior.target = qid;
                p->behavior.huntPhaseStart = w->context().now;
                p->behavior.startedAt = w->context().now;
                p->behavior.commitUntil = w->context().now + 30.0;
                p->behavior.nextDecision = w->context().now;
            }
            w->advance(1.0, 1.0);
        }
        w->events().unsubscribe(handle);
        const Creature* q = w->creatures().find(qid);
        preyDead += q && q->alive ? 0 : 1;
    }
    CHECK_EQ(deepStrikes, 0);
    CHECK_EQ(preyDead, 0);
    CHECK_GE(waterAbandons, static_cast<int>(sites.size()));
    NOTE("%zu sites: %d strikes (%d in deep water), %d prey killed, %d abandons at deep water", sites.size(), strikes, deepStrikes, preyDead, waterAbandons);
}

NOCTIS_TEST("creatures.hunting.prey_appraisal_armour_and_size")
{
    // An adult T. rex appraises an adult Ankylosaurus (osteoderm armour, tail club, its own size) as poor prey and an adult
    // Triceratops as modest prey, both clearly below a juvenile hadrosaur. Before, the armoured adult was as attractive
    // as the juvenile (opportunity 0.37 vs 0.38).
    std::unique_ptr<WorldSimulation> w = emptyWorld();
    REQUIRE(w != nullptr);
    const int rex = agentSpecies(*w, "tyrannosaurus_rex");
    const int anky = agentSpecies(*w, "ankylosaurus_magniventris");
    const int trike = agentSpecies(*w, "triceratops_horridus");
    const int edmo = agentSpecies(*w, "edmontosaurus_annectens");
    if (rex < 0 || anky < 0 || trike < 0 || edmo < 0)
    {
        NOTE("a species is absent from the data set: skipped");
        return;
    }
    const EntityId rid = spawnAdult(*w, rex, 41u);
    Creature* r = w->creatures().find(rid);
    REQUIRE(r != nullptr);
    r->phys.hunger = 0.0f; // appraisal only: no hunt
    const Vec2 p = r->pos2();
    const EntityId aid = w->creatures().spawn(anky, Sex::Female, 15.0f, p + Vec2{25.0f, 0.0f}, w->context()).id;
    const EntityId tid = w->creatures().spawn(trike, Sex::Female, 15.0f, p + Vec2{0.0f, 25.0f}, w->context()).id;
    const EntityId jid = w->creatures().spawn(edmo, Sex::Female, 2.0f, p + Vec2{-25.0f, 0.0f}, w->context()).id;
    float oppA = -1.0f;
    float oppT = -1.0f;
    float oppJ = -1.0f;
    for (int s = 0; s < 30 && (oppA < 0.0f || oppT < 0.0f || oppJ < 0.0f); ++s)
    {
        w->advance(1.0, 1.0);
        r = w->creatures().find(rid);
        REQUIRE(r != nullptr);
        for (const Awareness& a : r->perception.known)
        {
            if (a.relation != Relation::Prey)
            {
                continue;
            }
            if (a.entity == aid)
            {
                oppA = a.opportunity;
            }
            else if (a.entity == tid)
            {
                oppT = a.opportunity;
            }
            else if (a.entity == jid)
            {
                oppJ = a.opportunity;
            }
        }
    }
    REQUIRE(oppA >= 0.0f && oppT >= 0.0f && oppJ >= 0.0f);
    CHECK_LT(oppA, 0.1f);
    CHECK_LT(oppT, 0.25f);
    CHECK_GT(oppJ, 2.0f * oppA);
    CHECK_GT(oppJ, oppT);
    NOTE("adult T. rex: adult Ankylosaurus %.3f, adult Triceratops %.3f, 2-year-old Edmontosaurus %.3f", static_cast<double>(oppA), static_cast<double>(oppT),
         static_cast<double>(oppJ));
}

NOCTIS_TEST("creatures.death_cause.delayed_predation_credited")
{
    // Prey that dies of a hunting predator's wounds after the strike (bleeding out, or the predator's counter-blow while
    // the prey was striking) is a predation death and is credited to that predator: HuntKill (param 1), hunt history and,
    // while it is still in the struggle, feeding. Before, only a death at the predator's own strike was credited, so
    // predation deaths outnumbered HuntKill events.
    std::unique_ptr<WorldSimulation> w = emptyWorld();
    REQUIRE(w != nullptr);
    const int rex = agentSpecies(*w, "tyrannosaurus_rex");
    const int trike = agentSpecies(*w, "triceratops_horridus");
    if (rex < 0 || trike < 0)
    {
        NOTE("T. rex or Triceratops absent from the data set: skipped");
        return;
    }
    const EntityId vid = spawnAdult(*w, trike, 201u);
    Creature* v = w->creatures().find(vid);
    REQUIRE(v != nullptr);
    const EntityId rid = w->creatures().spawn(rex, Sex::Male, 25.0f, v->pos2() + Vec2{40.0f, 0.0f}, w->context()).id;
    Creature* r = w->creatures().find(rid);
    v = w->creatures().find(vid);
    REQUIRE(r != nullptr && v != nullptr);
    r->phys.hunger = 0.0f;
    r->behavior.engagedPrey = vid; // still in the hunting struggle it started
    r->behavior.engagedPreyUntil = w->context().now + 7200.0;
    Injury inj;
    inj.region = BodyRegion::Torso;
    inj.type = InjuryType::Puncture;
    inj.severity = 0.5f;
    inj.initialSeverity = 0.5f;
    inj.bleeding = 2.0f; // exaggerated so the test is short
    inj.time = w->context().now;
    inj.cause = rid;
    inj.causeSpecies = static_cast<i16>(rex);
    inj.predatory = true;
    v->injuries.active.push_back(inj);
    int kills = 0;
    u32 killParam = 99u;
    bool feeding = false;
    w->events().subscribe([&](const SimEvent& e) {
        if (e.type == EventType::HuntKill && e.subject == rid && e.other == vid)
        {
            ++kills;
            killParam = e.param;
            const Creature* k = w->creatures().find(rid);
            feeding = k && k->behavior.current == BehaviorId::FeedCarcass;
        }
    });
    DeathCause cause = DeathCause::Unknown;
    for (int s = 0; s < 3600; ++s)
    {
        w->step(1.0);
        const Creature* x = w->creatures().find(vid);
        if (!x || !x->alive)
        {
            cause = x ? x->deathCause : DeathCause::Unknown;
            break;
        }
    }
    for (const DeathRecord& d : w->creatures().deaths())
    {
        if (d.id == vid)
        {
            cause = d.cause;
        }
    }
    const Creature* k = w->creatures().find(rid);
    REQUIRE(k != nullptr);
    CHECK(cause == DeathCause::Predation);
    CHECK_EQ(kills, 1);
    CHECK_EQ(killParam, 1u);
    CHECK(feeding);
    CHECK_EQ(k->history.huntsSucceeded, 1);
    NOTE("Triceratops bleeding from a T. rex bite -> %s, HuntKill x%d (param %u), T. rex feeding: %s", deathCauseLabelFr(cause), kills, killParam,
         feeding ? "yes" : "no");
}

NOCTIS_TEST("creatures.combat.giants_disengage")
{
    // An adult T. rex that is not hunting and an adult Triceratops a few metres apart: a fight may start (the
    // Triceratops defends itself against the predator), but blows between multi-tonne animals are scaled to their size
    // and a hurt animal breaks off, so neither dies within 10 min. Both used to keep striking in Defend until one or
    // both died (about 25 blows in 25 s).
    int deaths = 0;
    int blows = 0;
    for (u64 seed = 301u; seed < 305u; ++seed)
    {
        std::unique_ptr<WorldSimulation> w = emptyWorld();
        REQUIRE(w != nullptr);
        const int rex = agentSpecies(*w, "tyrannosaurus_rex");
        const int trike = agentSpecies(*w, "triceratops_horridus");
        if (rex < 0 || trike < 0)
        {
            NOTE("T. rex or Triceratops absent from the data set: skipped");
            return;
        }
        const EntityId tid = spawnAdult(*w, trike, seed);
        Creature* t = w->creatures().find(tid);
        REQUIRE(t != nullptr);
        const Vec2 tp = t->pos2();
        const EntityId rid = w->creatures().spawn(rex, Sex::Male, 25.0f, tp + Vec2::fromHeading(t->loco.heading) * 6.0f, w->context()).id;
        const int handle = w->events().subscribe([&](const SimEvent& e) {
            if (e.type == EventType::Attack && (e.subject == rid || e.subject == tid))
            {
                ++blows;
            }
        });
        for (int s = 0; s < 600; ++s)
        {
            if (Creature* r = w->creatures().find(rid); r && r->alive)
            {
                r->phys.hunger = 0.0f; // not hunting
            }
            w->advance(1.0, 1.0);
        }
        w->events().unsubscribe(handle);
        for (const EntityId id : {rid, tid})
        {
            const Creature* x = w->creatures().find(id);
            deaths += x && x->alive ? 0 : 1;
        }
    }
    CHECK_EQ(deaths, 0);
    NOTE("4 encounters: %d blows exchanged, %d deaths", blows, deaths);
}
