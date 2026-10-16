// Simulation-level tests on a reduced valley: determinism across runs and thread counts, a soak
// run (long label), event-rate sanity and render-snapshot consistency.
#include "TestFramework.h"
#include "TestSupport.h"

#include "Noctis/Sim/WorldSimulation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace noctis;

namespace
{
struct RunDigest
{
    u64 stateHash = 0;
    u64 eventHash = 0;
    size_t events = 0;
    size_t hunts = 0; // HuntTargetSelected events: predator-on-prey perception ran
    size_t alive = 0;
    std::string census;
    double wallS = 0.0;
};

// Schedule exercising both regimes: real time with full-detail animals around the observer, then
// accelerated time (no full LOD, coarse steps) across midnight so hourly, 6-hourly and daily updates run.
RunDigest runSchedule(u64 seed, int threads, double realtimeS, double acceleratedH, double startHour)
{
    RunDigest d;
    noctis::SimConfig cfg = noctistest::smallWorldConfig(seed, threads, 0.3f);
    cfg.startHour = startHour;
    std::unique_ptr<WorldSimulation> w = noctistest::makeWorld(cfg);
    if (!w)
    {
        return d;
    }
    noctistest::EventRecorder rec(*w);
    const auto t0 = std::chrono::steady_clock::now();
    w->setTimeScale(1.0f);
    w->advance(realtimeS, 0.25);
    w->setTimeScale(100.0f);
    w->advance(acceleratedH * 3600.0, 10.0);
    d.wallS = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    d.stateHash = noctistest::hashWorldState(*w);
    d.eventHash = rec.hash();
    d.events = rec.total();
    d.hunts = rec.count(EventType::HuntTargetSelected);
    d.alive = w->creatures().aliveCount();
    d.census = noctistest::censusLine(*w);
    return d;
}

void compareDigests(const RunDigest& a, const RunDigest& b, const char* what)
{
    if (a.stateHash != b.stateHash || a.eventHash != b.eventHash || a.events != b.events || a.census != b.census)
    {
        noctistest::failf(__FILE__, __LINE__,
                          "%s differ:\n      state %016llx vs %016llx\n      events %016llx (%zu) vs %016llx (%zu)\n      census A: %s\n      census B: %s", what,
                          static_cast<unsigned long long>(a.stateHash), static_cast<unsigned long long>(b.stateHash),
                          static_cast<unsigned long long>(a.eventHash), a.events, static_cast<unsigned long long>(b.eventHash), b.events, a.census.c_str(),
                          b.census.c_str());
    }
}
} // namespace

NOCTIS_TEST("sim.determinism.same_seed_threads")
{
    const int n = std::max(2, noctistest::testThreads());
    // Schedule A, seed 11: start at 21:00 so that 6.5 accelerated hours cross midnight (daily update) and a
    // 6-hourly update. Its animals never hunt (0 HuntTargetSelected), so it does not exercise predator-on-prey
    // perception: schedule B below covers that.
    const RunDigest a = runSchedule(11u, 1, 600.0, 6.5, 21.0);
    const RunDigest b = runSchedule(11u, 1, 600.0, 6.5, 21.0);
    const RunDigest c = runSchedule(11u, n, 600.0, 6.5, 21.0);
    REQUIRE_GT(a.alive, size_t(0));
    NOTE("A, 1 thread : state %016llx events %016llx (%zu events, %zu hunts, %zu alive, %.1f s)", static_cast<unsigned long long>(a.stateHash),
         static_cast<unsigned long long>(a.eventHash), a.events, a.hunts, a.alive, a.wallS);
    NOTE("A, 1 thread': state %016llx events %016llx (%zu events, %.1f s)", static_cast<unsigned long long>(b.stateHash),
         static_cast<unsigned long long>(b.eventHash), b.events, b.wallS);
    NOTE("A, %d threads: state %016llx events %016llx (%zu events, %.1f s)", n, static_cast<unsigned long long>(c.stateHash),
         static_cast<unsigned long long>(c.eventHash), c.events, c.wallS);
    NOTE("A census: %s", a.census.c_str());
    compareDigests(a, b, "schedule A: two single-threaded runs with the same seed");
    compareDigests(a, c, "schedule A: single-threaded and multi-threaded runs");

    // Schedule B, seed 1: the schedule of sim.events.rate_sanity (05:00, 30 min of real time, then 12 accelerated
    // hours), where predators meet an Edmontosaurus herd within the first hours. Predators then evaluate prey
    // (perceive() reads other animals' body state) and several threads emit events at the same instant, the two
    // paths where thread scheduling could leak into the results (perception / body-update race,
    // Docs/PERFORMANCE.md section 6.3; same-time event order, which EventBus::flush's total sort key makes
    // independent of the threads). Compared twice against the single-threaded run: without a sanitizer such a race
    // shows only in some runs; ThreadSanitizer (CI) is the reliable detector. Until the D1 fix
    // (Docs/patches/D1_perception_body_phase_split.diff) is applied, these two comparisons fail now and then:
    // that is the defect they are meant to catch, not a flaky test.
    const RunDigest h = runSchedule(1u, 1, 1800.0, 12.0, 5.0);
    REQUIRE_GT(h.hunts, size_t(0)); // otherwise this schedule no longer covers predator-on-prey perception
    const RunDigest hn1 = runSchedule(1u, n, 1800.0, 12.0, 5.0);
    const RunDigest hn2 = runSchedule(1u, n, 1800.0, 12.0, 5.0);
    NOTE("B, 1 thread : state %016llx events %016llx (%zu events, %zu hunts, %zu alive, %.1f s)", static_cast<unsigned long long>(h.stateHash),
         static_cast<unsigned long long>(h.eventHash), h.events, h.hunts, h.alive, h.wallS);
    NOTE("B, %d threads: state %016llx events %016llx (%zu events, %.1f s); again: %016llx / %016llx (%zu events, %.1f s)", n,
         static_cast<unsigned long long>(hn1.stateHash), static_cast<unsigned long long>(hn1.eventHash), hn1.events, hn1.wallS,
         static_cast<unsigned long long>(hn2.stateHash), static_cast<unsigned long long>(hn2.eventHash), hn2.events, hn2.wallS);
    compareDigests(h, hn1, "schedule B: single-threaded and multi-threaded runs (1st; race D1 if its patch is not applied, Docs/PERFORMANCE.md 6.3)");
    compareDigests(h, hn2, "schedule B: single-threaded and multi-threaded runs (2nd; race D1 if its patch is not applied, Docs/PERFORMANCE.md 6.3)");

    // A different seed gives a different valley (the hash is not trivially constant). Compared right after
    // initialize, where the clock and the tick count are the same for both seeds, so only the seed can make the
    // two hashes differ.
    std::unique_ptr<WorldSimulation> w11 = noctistest::makeWorld(noctistest::smallWorldConfig(11u, 1, 0.3f));
    std::unique_ptr<WorldSimulation> w12 = noctistest::makeWorld(noctistest::smallWorldConfig(12u, 1, 0.3f));
    REQUIRE(w11 != nullptr && w12 != nullptr);
    CHECK_EQ(w11->tick(), w12->tick());
    CHECK_EQ(w11->clock().seconds(), w12->clock().seconds());
    CHECK_NE(noctistest::hashWorldState(*w11), noctistest::hashWorldState(*w12));
}

NOCTIS_TEST_LONG("sim.determinism.three_days_threads")
{
    const int n = std::max(2, noctistest::testThreads());
    const RunDigest a = runSchedule(3u, 1, 1800.0, 72.0, 5.0);
    const RunDigest c = runSchedule(3u, n, 1800.0, 72.0, 5.0);
    NOTE("72 h: 1 thread %016llx/%016llx (%zu events, %.1f s); %d threads %016llx/%016llx (%.1f s)", static_cast<unsigned long long>(a.stateHash),
         static_cast<unsigned long long>(a.eventHash), a.events, a.wallS, n, static_cast<unsigned long long>(c.stateHash),
         static_cast<unsigned long long>(c.eventHash), c.wallS);
    compareDigests(a, c, "72-hour single- and multi-threaded runs");
}

NOCTIS_TEST("sim.events.rate_sanity")
{
    // Seed 1 places an Edmontosaurus herd where it meets predators within the first hours: the
    // scenario that exposes alarm-call feedback (see Docs/PERFORMANCE.md, QA findings).
    noctis::SimConfig cfg = noctistest::smallWorldConfig(1u, noctistest::testThreads(), 0.3f);
    std::unique_ptr<WorldSimulation> w = noctistest::makeWorld(cfg);
    REQUIRE(w != nullptr);
    noctistest::EventRecorder rec(*w);
    const size_t groups0 = std::max<size_t>(1, w->creatures().groups().size());
    double creatureHours = 0.0;
    // 30 min of real time with full-detail animals, then 12 accelerated hours, sampled hourly.
    w->setTimeScale(1.0f);
    for (int k = 0; k < 6; ++k)
    {
        w->advance(300.0, 0.25);
        creatureHours += static_cast<double>(w->creatures().aliveCount()) * 300.0 / 3600.0;
    }
    w->setTimeScale(100.0f);
    for (int h = 0; h < 12; ++h)
    {
        w->advance(3600.0, 10.0);
        creatureHours += static_cast<double>(w->creatures().aliveCount());
    }
    const double hours = 12.5;
    const double perCH = 1.0 / std::max(1.0, creatureHours);
    u32 loudest = 0;
    const size_t maxCalls = rec.maxCallsPerIndividual(&loudest);
    const Creature* lc = w->creatures().find(EntityId(loudest));
    u32 alarmist = 0;
    const size_t maxAlarms = rec.maxCallsPerIndividual(CallContext::Alarm, &alarmist);
    const Creature* ac = w->creatures().find(EntityId(alarmist));
    NOTE("alarm calls: %zu in total (%.2f /creature/h), at most %zu by one individual (%.1f /h, %s)", rec.calls(CallContext::Alarm),
         static_cast<double>(rec.calls(CallContext::Alarm)) * perCH, maxAlarms, static_cast<double>(maxAlarms) / hours,
         ac ? w->db().speciesAt(ac->speciesIndex).id.c_str() : "-");
    NOTE("%.0f creature-hours, %zu events: vocalisations %.2f /creature/h (max individual %zu in %.1f h, %s), formation changes %.2f /group/h, "
         "deaths %zu, births %zu, immigration %zu",
         creatureHours, rec.total(), static_cast<double>(rec.count(EventType::Vocalization)) * perCH, maxCalls, hours,
         lc ? w->db().speciesAt(lc->speciesIndex).id.c_str() : "-", static_cast<double>(rec.count(EventType::GroupFormationChange)) / (static_cast<double>(groups0) * hours),
         rec.count(EventType::Death), rec.count(EventType::Birth) + rec.count(EventType::Hatch), rec.count(EventType::Immigration));
    // Every event is well-formed.
    if (rec.badEvents() > 0)
    {
        noctistest::failf(__FILE__, __LINE__, "%zu malformed event(s), first: %s", rec.badEvents(), rec.firstBad().c_str());
    }
    // Events are dispatched in time order (EventBus::flush sorts each step's events by time first).
    CHECK_EQ(rec.outOfOrder(), size_t(0));
    CHECK_GT(rec.total(), size_t(0));
    // Biological plausibility bounds (generous): an animal calling more than once a minute, on
    // average over half a day, or a population-wide rate above 20 calls per animal-hour indicates a
    // feedback loop (e.g. alarm calls re-triggering alarm calls), not behaviour.
    CHECK_LE(static_cast<double>(rec.count(EventType::Vocalization)) * perCH, 20.0);
    CHECK_LE(static_cast<double>(maxCalls) / hours, 60.0);
    // Alarm calls answer a detected predator: an individual repeating them more than every 3 minutes
    // for half a day, or more than 2 alarm calls per animal-hour across the community, is a loop.
    CHECK_LE(static_cast<double>(maxAlarms) / hours, 20.0);
    CHECK_LE(static_cast<double>(rec.calls(CallContext::Alarm)) * perCH, 2.0);
    // Group formation flapping: more than one change every 5 minutes per group.
    CHECK_LE(static_cast<double>(rec.count(EventType::GroupFormationChange)) / (static_cast<double>(groups0) * hours), 12.0);
    // Mortality over half a day: a few percent at most without catastrophes.
    CHECK_LE(static_cast<double>(rec.count(EventType::Death)), 0.05 * creatureHours / hours + 3.0);
    // Sound field and footprint stores stay bounded.
    CHECK_LE(w->sound().size(), size_t(20000));
}

NOCTIS_TEST("sim.no_nan_and_snapshot")
{
    noctis::SimConfig cfg = noctistest::smallWorldConfig(21u, noctistest::testThreads(), 0.3f);
    std::unique_ptr<WorldSimulation> w = noctistest::makeWorld(cfg);
    REQUIRE(w != nullptr);
    w->setTimeScale(1.0f);
    w->advance(300.0, 0.1);
    w->setTimeScale(100.0f);
    w->advance(8.0 * 3600.0, 10.0);
    w->setTimeScale(1.0f);
    w->advance(120.0, 0.1);
    int bad = 0;
    int outOfBounds = 0;
    for (const Creature& c : w->creatures().creatures())
    {
        if (!c.alive)
        {
            continue;
        }
        std::string what;
        if (!noctistest::creatureFinite(c, &what))
        {
            if (bad++ < 5)
            {
                noctistest::failf(__FILE__, __LINE__, "creature %u (%s): %s", c.id.value, w->db().speciesAt(c.speciesIndex).id.c_str(), what.c_str());
            }
        }
        else if (!noctistest::creatureInBounds(c, &what))
        {
            if (outOfBounds++ < 5)
            {
                noctistest::failf(__FILE__, __LINE__, "creature %u (%s) out of bounds: %s", c.id.value, w->db().speciesAt(c.speciesIndex).id.c_str(),
                                  what.c_str());
            }
        }
        if (!w->terrain().bounds().contains(c.pos2()))
        {
            noctistest::failf(__FILE__, __LINE__, "creature %u left the map", c.id.value);
        }
    }
    RenderSnapshot snap;
    w->buildSnapshot(snap);
    CHECK_EQ(snap.creatures.size(), w->creatures().aliveCount());
    int snapBad = 0;
    for (const CreatureRenderState& r : snap.creatures)
    {
        snapBad += (std::isfinite(r.position.x) && std::isfinite(r.position.y) && std::isfinite(r.position.z) && std::isfinite(r.heading) &&
                    std::isfinite(r.scale) && r.scale > 0.0f && r.footCount <= 4)
                       ? 0
                       : 1;
    }
    CHECK_EQ(snapBad, 0);
    CHECK_RANGE(snap.hourOfDay, 0.0, 24.0);
    CHECK(std::isfinite(snap.sky.illuminanceLux) && snap.sky.illuminanceLux >= 0.0f);
    // Second snapshot only reports sounds started since the first.
    RenderSnapshot snap2;
    w->buildSnapshot(snap2);
    CHECK_EQ(snap2.newSounds.size(), size_t(0));
    NOTE("after 8.1 simulated hours: %zu alive, %zu carcasses, %zu new sounds in first snapshot", snap.creatures.size(), snap.carcasses.size(),
         snap.newSounds.size());
}

NOCTIS_TEST_LONG("sim.soak.20_days")
{
    noctis::SimConfig cfg = noctistest::smallWorldConfig(2024u, noctistest::testThreads(), 0.25f);
    std::unique_ptr<WorldSimulation> w = noctistest::makeWorld(cfg);
    REQUIRE(w != nullptr);
    noctistest::EventRecorder rec(*w);
    const std::vector<int> initial = noctistest::aliveBySpecies(*w);
    std::vector<int> initialAdults(initial.size(), 0);
    for (size_t si = 0; si < initial.size(); ++si)
    {
        if (initial[si] > 0)
        {
            initialAdults[si] = w->creatures().census(static_cast<int>(si), w->clock().seconds()).adults;
        }
    }
    const size_t alive0 = w->creatures().aliveCount();
    const double biomass0 = w->vegetation().totalBiomassKg();
    std::vector<double> pool0;
    for (const PopulationPool& p : w->ecology().pools())
    {
        pool0.push_back(p.abundance);
    }
    w->setTimeScale(100.0f);
    const auto t0 = std::chrono::steady_clock::now();
    size_t minAlive = alive0;
    size_t maxAlive = alive0;
    double creatureDays = 0.0;
    int firstNanDay = -1;
    for (int day = 1; day <= 20; ++day)
    {
        w->advance(86400.0, 10.0);
        const size_t alive = w->creatures().aliveCount();
        creatureDays += static_cast<double>(alive);
        minAlive = std::min(minAlive, alive);
        maxAlive = std::max(maxAlive, alive);
        int nan = 0;
        int oob = 0;
        std::string what;
        std::string firstWhat;
        for (const Creature& c : w->creatures().creatures())
        {
            if (!c.alive)
            {
                continue;
            }
            if (!noctistest::creatureFinite(c, &what))
            {
                firstWhat = firstWhat.empty() ? w->db().speciesAt(c.speciesIndex).id + ": " + what : firstWhat;
                ++nan;
            }
            else if (!noctistest::creatureInBounds(c, &what))
            {
                firstWhat = firstWhat.empty() ? w->db().speciesAt(c.speciesIndex).id + ": " + what : firstWhat;
                ++oob;
            }
        }
        if ((nan > 0 || oob > 0) && firstNanDay < 0)
        {
            firstNanDay = day;
            noctistest::failf(__FILE__, __LINE__, "day %d: %d creature(s) with non-finite state, %d out of bounds; first: %s", day, nan, oob,
                              firstWhat.c_str());
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        NOTE("day %2d: %4zu alive, %zu deaths so far, %zu carcasses, %zu footprints, veg %.3g kg, %.0f s wall  %s", day, alive, w->creatures().deaths().size(),
             w->carcasses().all().size(), w->tracks().count(), w->vegetation().totalBiomassKg(), secs, noctistest::censusLine(*w).c_str());
    }
    // Population neither explodes nor collapses. Hatchling cohorts are expected in the breeding season
    // (the run starts on day 140), so the bounds are on adults (which can only increase through
    // immigration in 20 days) and loose on totals: community within [0.4x, 4x] of the start, adults of a
    // species at most 2x (+5), all individuals of a species at most 8x (+10).
    CHECK_GE(static_cast<double>(minAlive), 0.4 * static_cast<double>(alive0));
    CHECK_LE(static_cast<double>(maxAlive), 4.0 * static_cast<double>(alive0));
    const std::vector<int> fin = noctistest::aliveBySpecies(*w);
    const double now = w->clock().seconds();
    for (size_t si = 0; si < fin.size(); ++si)
    {
        if (initial[si] <= 0)
        {
            continue;
        }
        const SpeciesCensus cs = w->creatures().census(static_cast<int>(si), now);
        const char* id = w->db().speciesAt(static_cast<int>(si)).id.c_str();
        NOTE("%-32s %3d -> %3d alive (%3d juv., %3d adults; adults at start %3d) births %3d deaths %3d immig. %3d emig. %3d", id, initial[si], cs.alive,
             cs.juveniles, cs.adults, initialAdults[si], cs.births, cs.deaths, cs.immigrants, cs.emigrants);
        if (cs.adults > 2 * initialAdults[si] + 5)
        {
            noctistest::failf(__FILE__, __LINE__, "%s adults exploded: %d -> %d", id, initialAdults[si], cs.adults);
        }
        if (fin[si] > 8 * initial[si] + 10)
        {
            noctistest::failf(__FILE__, __LINE__, "%s exploded: %d -> %d", id, initial[si], fin[si]);
        }
    }
    // Vegetation and population pools stay finite and bounded.
    const double biomass = w->vegetation().totalBiomassKg();
    CHECK(std::isfinite(biomass));
    CHECK_RANGE(biomass, 0.3 * biomass0, 1.5 * biomass0);
    for (size_t i = 0; i < w->ecology().pools().size(); ++i)
    {
        const PopulationPool& p = w->ecology().pools()[i];
        CHECK(std::isfinite(p.abundance) && p.abundance >= 0.0);
        CHECK_LE(p.abundance, std::max(10.0 * pool0[i], 10.0 * p.baseCapacity) + 10.0);
    }
    // Memory-bearing stores stay bounded.
    CHECK_LE(w->sound().size(), size_t(20000));
    CHECK_LE(w->tracks().count(), size_t(2000000));
    if (rec.badEvents() > 0)
    {
        noctistest::failf(__FILE__, __LINE__, "%zu malformed event(s), first: %s", rec.badEvents(), rec.firstBad().c_str());
    }
    CHECK_EQ(rec.outOfOrder(), size_t(0));
    u32 loudest = 0;
    const size_t maxCalls = rec.maxCallsPerIndividual(&loudest);
    const Creature* lc = w->creatures().find(EntityId(loudest));
    const double vocRate = static_cast<double>(rec.count(EventType::Vocalization)) / std::max(1.0, creatureDays * 24.0);
    NOTE("20 days: alive %zu -> %zu (min %zu, max %zu), %zu events, %.2f calls/creature/h, max %zu calls by one individual (%s)", alive0,
         w->creatures().aliveCount(), minAlive, maxAlive, rec.total(), vocRate, maxCalls,
         lc ? w->db().speciesAt(lc->speciesIndex).id.c_str() : "-");
    u32 alarmist = 0;
    const size_t maxAlarms = rec.maxCallsPerIndividual(CallContext::Alarm, &alarmist);
    const Creature* ac = w->creatures().find(EntityId(alarmist));
    const double alarmRate = static_cast<double>(rec.calls(CallContext::Alarm)) / std::max(1.0, creatureDays * 24.0);
    NOTE("alarm calls: %zu (%.2f /creature/h), at most %zu by one individual (%.1f /h, %s%s)", rec.calls(CallContext::Alarm), alarmRate, maxAlarms,
         static_cast<double>(maxAlarms) / (20.0 * 24.0), ac ? w->db().speciesAt(ac->speciesIndex).id.c_str() : "-",
         ac ? (ac->alive ? (ac->ageYears(w->clock().seconds()) < w->creatures().speciesOf(*ac).sim().growth.maturityAgeYears ? ", juvenile" : ", adult") : ", dead") : "");
    CHECK_LE(vocRate, 20.0);
    CHECK_LE(static_cast<double>(maxCalls) / (20.0 * 24.0), 60.0);
    CHECK_LE(static_cast<double>(maxAlarms) / (20.0 * 24.0), 20.0);
    CHECK_LE(alarmRate, 2.0);
}
