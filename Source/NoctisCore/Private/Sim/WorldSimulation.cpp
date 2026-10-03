#include "Noctis/Sim/WorldSimulation.h"

#include "Noctis/Core/Log.h"
#include "Noctis/Creatures/CreatureLogic.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace noctis
{
namespace worldsimimpl
{
double nowMs()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void smooth(double& slot, double value) { slot = slot * 0.9 + value * 0.1; }
} // namespace worldsimimpl

WorldSimulation::WorldSimulation() = default;
WorldSimulation::~WorldSimulation() = default;

void WorldSimulation::refreshContext()
{
    ctx_.now = clock_.seconds();
    ctx_.seed = config_.seed;
    ctx_.timeScale = timeScale_;
    ctx_.db = &db_;
    ctx_.profile = profile_;
    ctx_.environment = env_;
    ctx_.species = &species_;
    ctx_.terrain = &terrain_;
    ctx_.water = &water_;
    ctx_.vegetation = &vegetation_;
    ctx_.weather = &weather_;
    ctx_.sky = &sky_;
    ctx_.season = &season_;
    ctx_.tracks = &tracks_;
    ctx_.carcasses = &carcasses_;
    ctx_.nests = &nests_;
    ctx_.sound = &sound_;
    ctx_.events = &events_;
    ctx_.jobs = jobs_.get();
    ctx_.clock = &clock_;
    ctx_.emitters = &emitters_;
    ctx_.harvester = &ecology_;
}

bool WorldSimulation::initialize(const SimConfig& config, std::unique_ptr<FileSystem> fs, std::unique_ptr<JobScheduler> jobs)
{
    config_ = config;
    fs_ = fs ? std::move(fs) : makeStdFileSystem();
    jobs_ = jobs ? std::move(jobs) : makeThreadPoolScheduler(config.threads);
    rng_.seed(config.seed, 0x5157);
    if (!db_.load(*fs_, config.dataRoot))
    {
        NOCTIS_LOG_ERROR("WorldSimulation: failed to load scientific data from '%s'", config.dataRoot.c_str());
        return false;
    }
    env_ = db_.findEnvironment(config.environmentId);
    if (!env_)
    {
        NOCTIS_LOG_ERROR("WorldSimulation: unknown environment '%s'", config.environmentId.c_str());
        return false;
    }
    profile_ = db_.findProfile(config.profileId);
    if (!profile_)
    {
        NOCTIS_LOG_WARN("WorldSimulation: profile '%s' not found, speculative behaviours disabled", config.profileId.c_str());
    }
    // Species runtime (all species, indices match the database).
    species_.clear();
    for (size_t i = 0; i < db_.species().size(); ++i)
    {
        species_.push_back(buildSpeciesRuntime(db_.species()[i], static_cast<int>(i)));
    }
    speciesInWorld_.clear();
    agentSpecies_.clear();
    std::vector<std::string> ids = env_->sim.speciesIds;
    if (ids.empty())
    {
        for (const SpeciesDefinition& s : db_.species())
        {
            if (std::find(s.regions.begin(), s.regions.end(), env_->id) != s.regions.end())
            {
                ids.push_back(s.id);
            }
        }
    }
    if (profile_)
    {
        ids.erase(std::remove_if(ids.begin(), ids.end(),
                                 [this](const std::string& id) {
                                     return std::find(profile_->excludedSpecies.begin(), profile_->excludedSpecies.end(), id) != profile_->excludedSpecies.end();
                                 }),
                  ids.end());
    }
    speciesInWorld_ = db_.speciesIndicesFor(ids);
    std::sort(speciesInWorld_.begin(), speciesInWorld_.end());
    for (const int si : speciesInWorld_)
    {
        if (db_.speciesAt(si).sim.role == SimRole::Agent)
        {
            agentSpecies_.push_back(si);
        }
    }
    NOCTIS_LOG_INFO("Environment %s: %zu species (%zu individually simulated)", env_->id.c_str(), speciesInWorld_.size(), agentSpecies_.size());

    // Calendar, sky, season, weather.
    clock_.configure(env_->sim.calendar, config.startDayOfYear, config.startHour, 0);
    sky_.configure(env_->sim.latitudeDeg, env_->sim.axialTiltDeg);
    season_.configure(env_->sim);
    weather_.configure(env_->sim, hashCombine(config.seed, 0x3E));
    // Terrain, water, vegetation.
    layout_ = TerrainGenerator::generate(env_->sim, hashCombine(config.seed, 0x7E), terrain_, config.heightSamples, config.materialCells);
    water_.build(layout_, terrain_);
    const double hoursPerYear = env_->sim.calendar.daysPerYear * env_->sim.calendar.dayLengthHours;
    water_.setMeanRainRate(static_cast<float>(env_->sim.annualPrecipMm / std::max(1.0, hoursPerYear)));
    vegetation_.build(env_->sim, terrain_, hashCombine(config.seed, 0x9E), config.vegetationCellM);
    tracks_.configure(terrain_.bounds(), 64.0f, 160);
    // Valley floor reference: 10th percentile of heights.
    {
        std::vector<float> h = terrain_.heights().raw();
        std::nth_element(h.begin(), h.begin() + static_cast<std::ptrdiff_t>(h.size() / 10), h.end());
        ctx_.valleyFloorZ = h[h.size() / 10];
    }
    creatures_.initialize(&species_, terrain_.bounds(), hashCombine(config.seed, 0xC7));
    creatures_.setWorldSpecies(agentSpecies_);
    ecology_.build(db_, speciesInWorld_, terrain_, water_, hashCombine(config.seed, 0xEC));
    refreshContext();
    sky_.update(clock_, weather_.state().cloudCover);
    season_.update(clock_);

    // Base camp: dry, gentle ground a few hundred metres from water, near the centre of the valley.
    {
        Vec2 best = terrain_.bounds().center();
        float bestScore = -1e9f;
        for (int i = 0; i < 400; ++i)
        {
            const Vec2 p{rng_.range(terrain_.size() * 0.3f, terrain_.size() * 0.7f), rng_.range(terrain_.size() * 0.3f, terrain_.size() * 0.7f)};
            const Habitat h = terrain_.habitatAt(p);
            if (h != Habitat::FloodplainOpen && h != Habitat::LeveeWoodland && h != Habitat::InterduneFlat && h != Habitat::XericScrub)
            {
                continue;
            }
            if (water_.depthAt(p, terrain_) > 0.0f)
            {
                continue;
            }
            DrinkSite site;
            float dWater = 2000.0f;
            if (water_.findDrinkSite(p, 2000.0f, site))
            {
                dWater = distance(site.position, p);
            }
            const float score = -std::fabs(dWater - 250.0f) / 100.0f - terrain_.slopeDegAt(p) * 0.5f - distance(p, terrain_.bounds().center()) / 1500.0f;
            if (score > bestScore)
            {
                bestScore = score;
                best = p;
            }
        }
        basecamp_ = best;
    }
    research_.initialize(db_, *env_, layout_, agentSpecies_, terrain_.bounds(), basecamp_, terrain_, hashCombine(config.seed, 0x5E));
    events_.subscribe([this](const SimEvent& e) { research_.onEvent(e, ctx_, creatures_); });

    if (config.spawnPopulation)
    {
        spawnInitialPopulations();
    }
    if (config.initialRemains)
    {
        // A few old carcasses already lie in the valley (bones, scavenged remains).
        for (int k = 0; k < 3 && !agentSpecies_.empty(); ++k)
        {
            const int si = agentSpecies_[static_cast<size_t>(rng_.rangeInt(0, static_cast<int>(agentSpecies_.size()) - 1))];
            const SpeciesSim& s = db_.speciesAt(si).sim;
            if (s.adultMassKg < 50.0f)
            {
                continue;
            }
            const Vec2 p = findHabitatPoint(si, rng_);
            const EntityId id = carcasses_.create(static_cast<i16>(si), kNoEntity, p, rng_.range(-kPi, kPi), s.adultMassKg * rng_.range(0.4f, 1.0f),
                                                  -rng_.range(5.0f, 120.0f) * 86400.0, DeathCause::Unknown, s.growth.maturityAgeYears);
            if (Carcass* c = carcasses_.findMutable(id))
            {
                const float days = static_cast<float>(-c->timeOfDeath / 86400.0);
                c->accumulatedDegreeDays = days * std::max(1.0f, env_->sim.meanAnnualTempC);
                c->scatter = rng_.range(0.2f, 0.8f);
            }
        }
    }
    creatures_.rebuildIndex();
    initialized_ = true;
    NOCTIS_LOG_INFO("World ready: %zu individuals, %zu groups, basecamp (%.0f, %.0f)", creatures_.aliveCount(), creatures_.groups().size(), basecamp_.x,
                    basecamp_.y);
    return true;
}

Vec2 WorldSimulation::findHabitatPoint(int speciesIndex, Rng& rng, const Vec2* near, float radius) const
{
    const SpeciesSim& s = db_.speciesAt(speciesIndex).sim;
    Vec2 best = terrain_.bounds().center();
    float bestScore = -1e9f;
    const bool aquatic = s.locomotion.swim && (s.habitat[static_cast<int>(Habitat::Channel)] > 0.5f || s.habitat[static_cast<int>(Habitat::OxbowLake)] > 0.5f);
    for (int i = 0; i < 40; ++i)
    {
        Vec2 p;
        if (near)
        {
            p = *near + Vec2::fromHeading(rng.range(0.0f, kTwoPi)) * radius * std::sqrt(rng.uniform());
        }
        else
        {
            p = Vec2{rng.range(100.0f, terrain_.size() - 100.0f), rng.range(100.0f, terrain_.size() - 100.0f)};
        }
        p = terrain_.clampToBounds(p, 50.0f);
        const float depth = water_.depthAt(p, terrain_);
        if (depth > 0.2f && !aquatic)
        {
            continue;
        }
        const float score = s.habitat[static_cast<int>(terrain_.habitatAt(p))] + 0.15f * rng.uniform();
        if (score > bestScore)
        {
            bestScore = score;
            best = p;
        }
    }
    return best;
}

void WorldSimulation::spawnInitialPopulations()
{
    for (const int si : agentSpecies_)
    {
        const SpeciesSim& s = db_.speciesAt(si).sim;
        int remaining = static_cast<int>(std::lround(s.population.initialCount * config_.populationScale));
        if (remaining <= 0 && s.population.initialCount > 0.0f && config_.populationScale > 0.0f)
        {
            remaining = 1;
        }
        const float life = std::max(2.0f, s.growth.maxLifespanYears);
        const float mature = std::max(0.5f, s.growth.maturityAgeYears);
        auto drawAge = [&]() {
            // Stationary age structure approximation: most animals are young adults and subadults.
            if (rng_.chance(0.6f))
            {
                return rng_.range(mature, std::max(mature + 0.5f, life * 0.7f));
            }
            return rng_.range(std::min(0.6f, mature * 0.3f), mature);
        };
        std::vector<Vec2> territoryCentres;
        while (remaining > 0)
        {
            if (s.social.groupSizeMean > 1.5f)
            {
                const int size = std::clamp(std::max(2, rng_.poisson(s.social.groupSizeMean)), 2, std::max(2, static_cast<int>(s.social.groupSizeMax)));
                const int n = std::min(size, remaining);
                const Vec2 centre = findHabitatPoint(si, rng_);
                const u32 g = n > 1 ? creatures_.createGroup(si) : 0u;
                for (int k = 0; k < n; ++k)
                {
                    const float spread = std::max(5.0f, s.adultLengthM * 3.0f * std::sqrt(static_cast<float>(n)));
                    const Vec2 p = findHabitatPoint(si, rng_, &centre, spread);
                    creatures_.spawn(si, rng_.chance(0.5f) ? Sex::Female : Sex::Male, drawAge(), p, ctx_, g);
                }
                remaining -= n;
            }
            else
            {
                Vec2 p = findHabitatPoint(si, rng_);
                if (s.social.territorial)
                {
                    // Space territorial individuals apart.
                    for (int attempt = 0; attempt < 10; ++attempt)
                    {
                        bool ok = true;
                        for (const Vec2& t : territoryCentres)
                        {
                            ok = ok && distance(t, p) > s.social.territoryRadiusM * 0.7f;
                        }
                        if (ok)
                        {
                            break;
                        }
                        p = findHabitatPoint(si, rng_);
                    }
                    territoryCentres.push_back(p);
                }
                creatures_.spawn(si, rng_.chance(0.5f) ? Sex::Female : Sex::Male, drawAge(), p, ctx_, 0);
                --remaining;
            }
        }
    }
}

void WorldSimulation::setTimeScale(float scale)
{
    timeScale_ = std::max(0.0f, scale);
    creatures_.lod().allowFull = timeScale_ <= 8.0f;
}

void WorldSimulation::handleLightning()
{
    for (const LightningStrike& s : weather_.takeStrikes())
    {
        SimEvent e;
        e.type = EventType::Lightning;
        e.time = s.time;
        e.position = Vec3{s.position, terrain_.heightAt(s.position)};
        events_.emit(e);
        SoundSource thunder;
        thunder.kind = SoundKind::Thunder;
        thunder.emitter = EntityId::make(EntityKind::Environment, 1);
        thunder.position = e.position + Vec3{0.0f, 0.0f, 500.0f};
        thunder.startTime = s.time;
        thunder.durationS = rng_.range(2.0f, 6.0f);
        thunder.levelDb = 125.0f; // equivalent SPL at 1 m (game assumption; ~65–70 dB at 1 km)
        thunder.f0Hz = 60.0f;
        thunder.tonal = false;
        thunder.seed = rng_.nextU32();
        sound_.emit(thunder);
        VegetationClimate vc;
        vc.airTempC = weather_.state().temperatureC;
        vc.rainMmPerHour = weather_.state().precipMmPerHour;
        vc.dryness = weather_.state().dryness;
        vc.wind = weather_.state().wind;
        if (rng_.chance(0.15f * vc.dryness) && vegetation_.ignite(s.position, vc))
        {
            SimEvent f;
            f.type = EventType::FireIgnition;
            f.time = s.time;
            f.position = e.position;
            events_.emit(f);
        }
    }
}

void WorldSimulation::step(double dt)
{
    using worldsimimpl::nowMs;
    if (!initialized_ || dt <= 0.0)
    {
        return;
    }
    const double t0 = nowMs();
    clock_.advance(dt);
    ++tick_;
    refreshContext();
    ctx_.dt = static_cast<float>(dt);
    ctx_.tick = tick_;
    sky_.update(clock_, weather_.state().cloudCover);
    season_.update(clock_);
    weather_.update(dt, clock_, season_.state(), sky_.state(), terrain_.bounds());
    handleLightning();
    rainAccumulatorMm_ += weather_.state().precipMmPerHour * dt / 3600.0;
    evapAccumulatorMm_ += weather_.state().potentialEvapMmPerHour * dt / 3600.0;
    tempAccumulator_ += weather_.state().temperatureC * dt;
    tempSamples_ += dt;
    emitters_.clear();
    research_.publishEmitters(emitters_);
    const double t1 = nowMs();
    creatures_.step(ctx_);
    const double t2 = nowMs();
    research_.update(static_cast<float>(dt), ctx_, creatures_);
    const double t3 = nowMs();
    events_.flush();
    sound_.prune(ctx_.now, 25.0);
    water_.pruneDisturbances(ctx_.now, 12.0);
    worldsimimpl::smooth(timings_.environment, t1 - t0);
    worldsimimpl::smooth(timings_.creatures, t2 - t1);
    worldsimimpl::smooth(timings_.research, t3 - t2);

    hourAccumulator_ += dt;
    sixHourAccumulator_ += dt;
    while (hourAccumulator_ >= 3600.0)
    {
        hourAccumulator_ -= 3600.0;
        const double h0 = nowMs();
        hourlyUpdate();
        worldsimimpl::smooth(timings_.hourly, nowMs() - h0);
    }
    while (sixHourAccumulator_ >= 6.0 * 3600.0)
    {
        sixHourAccumulator_ -= 6.0 * 3600.0;
        sixHourlyUpdate();
    }
    const int day = static_cast<int>(std::floor(clock_.absoluteDays()));
    if (lastDay_ < 0)
    {
        lastDay_ = day;
    }
    if (day != lastDay_)
    {
        lastDay_ = day;
        const double d0 = nowMs();
        dailyUpdate();
        worldsimimpl::smooth(timings_.daily, nowMs() - d0);
    }
}

void WorldSimulation::advance(double seconds, double maxStep)
{
    double remaining = seconds;
    while (remaining > 1e-6)
    {
        const double dt = std::min(remaining, maxStep);
        step(dt);
        remaining -= dt;
    }
}

void WorldSimulation::hourlyUpdate()
{
    const float rain = static_cast<float>(rainAccumulatorMm_);
    const float evap = static_cast<float>(evapAccumulatorMm_);
    rainAccumulatorMm_ = 0.0;
    evapAccumulatorMm_ = 0.0;
    const float meanTemp = tempSamples_ > 0.0 ? static_cast<float>(tempAccumulator_ / tempSamples_) : weather_.state().temperatureC;
    tempAccumulator_ = 0.0;
    tempSamples_ = 0.0;
    terrain_.updateMoisture(1.0f, rain, evap);
    water_.update(1.0f, rain, evap);
    water_.rebuildSurface(terrain_);
    VegetationClimate vc;
    vc.airTempC = meanTemp;
    vc.rainMmPerHour = rain;
    vc.dryness = weather_.state().dryness;
    vc.wind = weather_.state().wind;
    vegetation_.updateFire(1.0f, vc, hashCombine(config_.seed, tick_));
    carcasses_.update(ctx_.now, 1.0f, meanTemp * (0.7f + 0.3f * ctx_.insectActivity), water_.flooding(), vegetation_, events_);
    std::vector<HatchRecord> hatched;
    const float soil = env_->sim.meanAnnualTempC + season_.state().temperatureOffsetC * 0.6f;
    nests_.update(ctx_.now, 1.0f, meanTemp, soil,
                  [this](i16 species) { return species >= 0 ? db_.speciesAt(species).sim.reproduction.incubationDays : 60.0f; }, hatched, events_);
    creatures_.queueHatchlings(std::move(hatched));
    tracks_.weather(1.0f, rain);
    creatures_.hourlyUpdate(ctx_);
    events_.flush();
}

void WorldSimulation::sixHourlyUpdate()
{
    VegetationClimate vc;
    vc.airTempC = weather_.state().temperatureC;
    vc.rainMmPerHour = weather_.state().precipMmPerHour;
    vc.seasonGrowthFactor = season_.state().growthFactor;
    vc.dryness = weather_.state().dryness;
    vc.wind = weather_.state().wind;
    vegetation_.update(0.25f, vc, terrain_, *jobs_);
}

void WorldSimulation::dailyUpdate()
{
    creatures_.dailyUpdate(ctx_);
    ecology_.dailyUpdate(ctx_, creatures_);
    ecology_.sampleCensus(ctx_, creatures_);
    events_.flush();
}

void WorldSimulation::buildSnapshot(RenderSnapshot& out)
{
    out.time = clock_.seconds();
    out.hourOfDay = static_cast<float>(clock_.hourOfDay());
    out.dayOfYear = static_cast<float>(clock_.dayOfYear());
    out.sky = sky_.state();
    out.weather = weather_.state();
    out.riverStageM = water_.riverStage(0);
    out.flooding = water_.flooding();
    out.creatures.clear();
    for (const Creature& c : creatures_.creatures())
    {
        if (!c.alive)
        {
            continue;
        }
        const SpeciesRuntime& sp = species_[static_cast<size_t>(c.speciesIndex)];
        CreatureRenderState r;
        r.id = c.id.value;
        r.speciesIndex = c.speciesIndex;
        r.lod = c.lod;
        r.position = c.loco.position;
        r.heading = c.loco.heading;
        r.pitch = c.loco.pitch;
        r.roll = c.loco.roll;
        r.scale = c.lengthM / std::max(0.01f, sp.sim().adultLengthM);
        r.speed = c.loco.speed;
        r.gait = c.loco.gait;
        r.gaitPhase = c.loco.gaitPhase;
        r.strideLengthM = c.loco.strideLengthM;
        r.dutyFactor = c.loco.dutyFactor;
        r.bodyBob = c.loco.bodyBob;
        r.footCount = c.loco.footCount;
        for (size_t f = 0; f < 4; ++f)
        {
            r.footTargets[f] = c.loco.feet[f].inContact ? c.loco.feet[f].planted : c.loco.feet[f].target;
            r.footContact[f] = c.loco.feet[f].inContact ? 1 : 0;
            r.muscle[f] = c.soft.muscleActivation[f];
        }
        r.softTissue = c.soft.offset;
        r.breath = c.soft.breath;
        r.lookTarget = c.perception.lookTarget;
        r.hasLookTarget = c.perception.hasLookTarget;
        r.posture = c.behavior.posture;
        r.behavior = c.behavior.current;
        r.vocalizing = ctx_.now - c.behavior.lastCall < 2.0;
        r.limp = c.injuries.limp;
        r.limpSide = c.injuries.limpSide;
        r.scarCount = static_cast<u16>(c.injuries.scars.size());
        r.markingSeed = c.markingSeed;
        r.condition = c.phys.condition;
        r.wetness = saturate(c.loco.waterDepth / std::max(0.05f, c.hipHeightM));
        r.mud = saturate(c.loco.sinkageM / std::max(0.05f, c.hipHeightM * 0.2f));
        out.creatures.push_back(r);
    }
    out.carcasses.clear();
    for (const Carcass& c : carcasses_.all())
    {
        if (!c.active)
        {
            continue;
        }
        CarcassRenderState r;
        r.id = c.id.value;
        r.speciesIndex = c.speciesIndex;
        r.position = Vec3{c.position, terrain_.heightAt(c.position)};
        r.heading = c.heading;
        r.softTissueFraction = c.massAtDeathKg > 0.0f ? c.softTissueKg / (c.massAtDeathKg * 0.9f) : 0.0f;
        r.stage = static_cast<u8>(c.stage);
        r.scatter = c.scatter;
        if (c.speciesIndex >= 0)
        {
            r.scale = std::cbrt(c.massAtDeathKg / std::max(0.01f, db_.speciesAt(c.speciesIndex).sim.adultMassKg));
        }
        out.carcasses.push_back(r);
    }
    out.splashes = water_.disturbances();
    out.newSounds.clear();
    std::vector<SoundSource> sounds;
    sound_.collect(lastSnapshotTime_ - 1.0, ctx_.now, sounds);
    u32 maxId = lastSnapshotSoundId_;
    for (const SoundSource& s : sounds)
    {
        if (s.id > lastSnapshotSoundId_)
        {
            out.newSounds.push_back(s);
            maxId = std::max(maxId, s.id);
        }
    }
    lastSnapshotSoundId_ = maxId;
    lastSnapshotTime_ = ctx_.now;
    const ResearcherState& rs = research_.researcher().state();
    out.researcherPosition = rs.position;
    out.researcherHeading = rs.heading;
    out.researcherInVehicle = rs.inVehicle;
    const VehicleState& vs = research_.vehicle().state();
    out.vehiclePosition = vs.position;
    out.vehicleHeading = vs.heading;
    out.vehicleSinkage = vs.wheelSinkageM;
    const DroneState& ds = research_.drone().state();
    out.dronePosition = ds.position;
    out.droneHeading = ds.heading;
    out.droneAirborne = ds.mode != DroneMode::Docked && ds.mode != DroneMode::Lost;
}
} // namespace noctis
