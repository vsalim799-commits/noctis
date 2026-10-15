#include "TestSupport.h"

#include "TestFramework.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace noctistest
{
using namespace noctis;

SimConfig smallWorldConfig(u64 seed, int threads, float populationScale)
{
    SimConfig cfg;
    cfg.dataRoot = dataRoot();
    cfg.environmentId = "hell_creek";
    cfg.profileId = "standard";
    cfg.seed = seed;
    cfg.threads = threads;
    cfg.heightSamples = 513;
    cfg.materialCells = 256;
    cfg.vegetationCellM = 32.0f;
    cfg.populationScale = populationScale;
    return cfg;
}

std::unique_ptr<WorldSimulation> makeWorld(const SimConfig& cfg)
{
    std::unique_ptr<WorldSimulation> w = std::make_unique<WorldSimulation>();
    if (!w->initialize(cfg))
    {
        fail(__FILE__, __LINE__, "WorldSimulation::initialize failed (data root: " + cfg.dataRoot + ")");
        return nullptr;
    }
    return w;
}

u64 hashWorldState(WorldSimulation& w)
{
    Hasher h;
    h.d(w.clock().seconds());
    h.u(w.tick());
    for (const Creature& c : w.creatures().creatures())
    {
        h.u(c.id.value);
        h.i(c.speciesIndex);
        h.u(c.alive ? 1u : 0u);
        h.u(static_cast<u64>(c.lod));
        h.v3(c.loco.position);
        h.f(c.loco.heading);
        h.f(c.loco.speed);
        h.f(c.massKg);
        h.f(c.phys.energyReserveMJ);
        h.f(c.phys.hydration);
        h.f(c.phys.fatigue);
        h.f(c.phys.stress);
        h.f(c.injuries.healthIndex);
        h.u(static_cast<u64>(c.behavior.current));
        h.d(c.behavior.nextDecision);
        h.u(c.social.groupId);
        h.u(c.rng.stateForSave());
        h.u(c.perception.known.size());
        h.u(c.memory.places.size());
    }
    const double now = w.clock().seconds();
    for (const int si : w.speciesInWorld())
    {
        const SpeciesCensus cs = w.creatures().census(si, now);
        h.i(cs.alive);
        h.i(cs.births);
        h.i(cs.deaths);
        h.i(cs.immigrants);
        h.i(cs.emigrants);
        h.d(cs.biomassKg);
    }
    for (const Group& g : w.creatures().groups())
    {
        h.u(g.id);
        h.u(g.members.size());
        h.u(static_cast<u64>(g.formation));
    }
    h.u(w.creatures().deaths().size());
    for (const Carcass& c : w.carcasses().all())
    {
        h.u(c.id.value);
        h.f(c.softTissueKg);
        h.u(c.active ? 1u : 0u);
    }
    h.u(w.nests().all().size());
    h.u(w.tracks().count());
    const WeatherState& ws = w.weather().state();
    h.u(static_cast<u64>(ws.regime));
    h.f(ws.temperatureC);
    h.f(ws.cloudCover);
    h.d(w.weather().accumulatedPrecipMm());
    h.d(w.vegetation().totalBiomassKg());
    for (const PopulationPool& p : w.ecology().pools())
    {
        h.d(p.abundance);
    }
    h.u(w.events().totalEmitted());
    return h.h;
}

std::string censusLine(const WorldSimulation& w)
{
    std::string s;
    const double now = w.clock().seconds();
    for (const int si : w.speciesInWorld())
    {
        if (w.db().speciesAt(si).sim.role != SimRole::Agent)
        {
            continue;
        }
        const SpeciesCensus cs = w.creatures().census(si, now);
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s%s=%d", s.empty() ? "" : " ", w.db().speciesAt(si).id.c_str(), cs.alive);
        s += buf;
    }
    return s;
}

std::vector<int> aliveBySpecies(const WorldSimulation& w)
{
    std::vector<int> out(w.db().species().size(), 0);
    for (const Creature& c : w.creatures().creatures())
    {
        if (c.alive && c.speciesIndex >= 0)
        {
            ++out[static_cast<size_t>(c.speciesIndex)];
        }
    }
    return out;
}

EventRecorder::EventRecorder(WorldSimulation& w) : world_(w)
{
    handle_ = w.events().subscribe([this](const SimEvent& e) { onEvent(e); });
}

EventRecorder::~EventRecorder() { world_.events().unsubscribe(handle_); }

void EventRecorder::reset()
{
    hasher_ = Hasher{};
    total_ = 0;
    perType_.fill(0);
    callsByEmitter_.clear();
    callsByContext_.fill(0);
    bad_ = 0;
    outOfOrder_ = 0;
    firstBad_.clear();
}

void EventRecorder::onEvent(const SimEvent& e)
{
    ++total_;
    const size_t t = std::min(static_cast<size_t>(e.type), static_cast<size_t>(EventType::Count));
    ++perType_[t];
    hasher_.u(static_cast<u64>(e.type));
    hasher_.d(e.time);
    hasher_.u(e.subject.value);
    hasher_.u(e.other.value);
    hasher_.v3(e.position);
    hasher_.f(e.magnitude);
    hasher_.u(e.param);
    hasher_.u(e.param2);
    const bool finite = std::isfinite(e.time) && std::isfinite(e.position.x) && std::isfinite(e.position.y) && std::isfinite(e.position.z) &&
                        std::isfinite(e.magnitude);
    if (!finite || e.type >= EventType::Count)
    {
        if (bad_ == 0)
        {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s t=%.3f pos=(%g,%g,%g) mag=%g", e.type < EventType::Count ? eventTypeName(e.type) : "?", e.time,
                          static_cast<double>(e.position.x), static_cast<double>(e.position.y), static_cast<double>(e.position.z),
                          static_cast<double>(e.magnitude));
            firstBad_ = buf;
        }
        ++bad_;
    }
    // Within a flush events are sorted by time; across flushes time should never go back.
    if (e.time < lastTime_ - 1e-6)
    {
        ++outOfOrder_;
    }
    lastTime_ = std::max(lastTime_, e.time);
    if (e.type == EventType::Vocalization)
    {
        const size_t ctxIndex = std::min<size_t>(e.param, 15);
        ++callsByContext_[ctxIndex];
        auto it = std::find_if(callsByEmitter_.begin(), callsByEmitter_.end(), [&](const EmitterCalls& p) { return p.emitter == e.subject.value; });
        if (it == callsByEmitter_.end())
        {
            callsByEmitter_.push_back(EmitterCalls{});
            it = callsByEmitter_.end() - 1;
            it->emitter = e.subject.value;
        }
        ++it->total;
        ++it->byContext[ctxIndex];
    }
}

size_t EventRecorder::maxCallsPerIndividual(u32* who) const
{
    size_t best = 0;
    for (const EmitterCalls& p : callsByEmitter_)
    {
        if (p.total > best)
        {
            best = p.total;
            if (who)
            {
                *who = p.emitter;
            }
        }
    }
    return best;
}

size_t EventRecorder::maxCallsPerIndividual(CallContext context, u32* who) const
{
    const size_t k = std::min<size_t>(static_cast<size_t>(context), 15);
    size_t best = 0;
    for (const EmitterCalls& p : callsByEmitter_)
    {
        if (p.byContext[k] > best)
        {
            best = p.byContext[k];
            if (who)
            {
                *who = p.emitter;
            }
        }
    }
    return best;
}

size_t EventRecorder::calls(CallContext context) const { return callsByContext_[std::min<size_t>(static_cast<size_t>(context), 15)]; }

namespace supportimpl
{
bool finiteField(float v, const char* name, std::string* what)
{
    if (!std::isfinite(v))
    {
        if (what)
        {
            *what = std::string(name) + " = " + toString(v);
        }
        return false;
    }
    return true;
}

bool rangeField(float v, float lo, float hi, const char* name, std::string* what)
{
    if (!(v >= lo && v <= hi))
    {
        if (what)
        {
            *what = std::string(name) + " = " + toString(v) + " not in [" + toString(lo) + ", " + toString(hi) + "]";
        }
        return false;
    }
    return true;
}
} // namespace supportimpl

bool creatureFinite(const Creature& c, std::string* what)
{
    using supportimpl::finiteField;
    const LocomotionState& L = c.loco;
    const PhysiologyState& p = c.phys;
    bool ok = finiteField(L.position.x, "loco.position.x", what) && finiteField(L.position.y, "loco.position.y", what) &&
              finiteField(L.position.z, "loco.position.z", what) && finiteField(L.heading, "loco.heading", what) &&
              finiteField(L.speed, "loco.speed", what) && finiteField(L.velocity.x, "loco.velocity.x", what) &&
              finiteField(L.velocity.y, "loco.velocity.y", what) && finiteField(L.yawRate, "loco.yawRate", what) &&
              finiteField(L.pitch, "loco.pitch", what) && finiteField(L.roll, "loco.roll", what) && finiteField(L.froude, "loco.froude", what) &&
              finiteField(L.strideLengthM, "loco.strideLengthM", what) && finiteField(L.gaitPhase, "loco.gaitPhase", what) &&
              finiteField(L.sinkageM, "loco.sinkageM", what) && finiteField(L.bodyBob, "loco.bodyBob", what);
    ok = ok && finiteField(c.massKg, "massKg", what) && finiteField(c.lengthM, "lengthM", what) && finiteField(c.hipHeightM, "hipHeightM", what);
    ok = ok && finiteField(p.energyReserveMJ, "phys.energyReserveMJ", what) && finiteField(p.energyCapacityMJ, "phys.energyCapacityMJ", what) &&
         finiteField(p.gutFillKg, "phys.gutFillKg", what) && finiteField(p.hydration, "phys.hydration", what) && finiteField(p.fatigue, "phys.fatigue", what) &&
         finiteField(p.sleepPressure, "phys.sleepPressure", what) && finiteField(p.bodyTempC, "phys.bodyTempC", what) &&
         finiteField(p.stress, "phys.stress", what) && finiteField(p.chronicStress, "phys.chronicStress", what) && finiteField(p.pain, "phys.pain", what) &&
         finiteField(p.bloodLoss, "phys.bloodLoss", what) && finiteField(p.metabolicRateW, "phys.metabolicRateW", what) &&
         finiteField(p.condition, "phys.condition", what) && finiteField(p.hunger, "phys.hunger", what) && finiteField(p.thirst, "phys.thirst", what) &&
         finiteField(p.heatStress, "phys.heatStress", what);
    ok = ok && finiteField(c.injuries.healthIndex, "injuries.healthIndex", what) && finiteField(c.injuries.speedFactor, "injuries.speedFactor", what) &&
         finiteField(c.perception.alertness, "perception.alertness", what) && finiteField(c.behavior.desiredSpeed, "behavior.desiredSpeed", what) &&
         finiteField(c.behavior.desiredDir.x, "behavior.desiredDir.x", what) && finiteField(c.behavior.desiredDir.y, "behavior.desiredDir.y", what);
    for (size_t i = 0; ok && i < c.soft.offset.size(); ++i)
    {
        ok = finiteField(c.soft.offset[i], "soft.offset[i]", what) && finiteField(c.soft.velocity[i], "soft.velocity[i]", what);
    }
    for (size_t i = 0; ok && i < c.perception.known.size(); ++i)
    {
        const Awareness& a = c.perception.known[i];
        ok = finiteField(a.evidence, "perception.known[i].evidence", what) && finiteField(a.estimatedPos.x, "perception.known[i].estimatedPos.x", what) &&
             finiteField(a.estimatedPos.y, "perception.known[i].estimatedPos.y", what) && finiteField(a.threat, "perception.known[i].threat", what);
    }
    return ok;
}

bool creatureInBounds(const Creature& c, std::string* what)
{
    using supportimpl::rangeField;
    const PhysiologyState& p = c.phys;
    const float eps = 1e-3f;
    bool ok = rangeField(p.hydration, 0.0f, 1.0f + eps, "phys.hydration", what) && rangeField(p.fatigue, 0.0f, 1.0f + eps, "phys.fatigue", what) &&
              rangeField(p.sleepPressure, 0.0f, 1.0f + eps, "phys.sleepPressure", what) && rangeField(p.stress, 0.0f, 1.0f + eps, "phys.stress", what) &&
              rangeField(p.chronicStress, 0.0f, 1.0f + eps, "phys.chronicStress", what) && rangeField(p.pain, 0.0f, 1.0f + eps, "phys.pain", what) &&
              rangeField(p.bloodLoss, 0.0f, 1.0f + eps, "phys.bloodLoss", what) && rangeField(p.hunger, 0.0f, 1.0f + eps, "phys.hunger", what) &&
              rangeField(p.thirst, 0.0f, 1.0f + eps, "phys.thirst", what) && rangeField(p.condition, 0.0f, 1.0f + eps, "phys.condition", what) &&
              rangeField(p.heatStress, -1.0f - eps, 1.0f + eps, "phys.heatStress", what) && rangeField(p.bodyTempC, -5.0f, 50.0f, "phys.bodyTempC", what) &&
              rangeField(p.energyReserveMJ, 0.0f, p.energyCapacityMJ * 1.001f + eps, "phys.energyReserveMJ", what) &&
              rangeField(p.gutFillKg, 0.0f, p.gutCapacityKg * 1.001f + eps, "phys.gutFillKg", what) &&
              rangeField(p.metabolicRateW, 0.0f, 1.0e7f, "phys.metabolicRateW", what);
    ok = ok && rangeField(c.injuries.healthIndex, 0.0f, 1.0f + eps, "injuries.healthIndex", what) &&
         rangeField(c.injuries.speedFactor, 0.0f, 1.0f + eps, "injuries.speedFactor", what) &&
         rangeField(c.perception.alertness, 0.0f, 1.0f + eps, "perception.alertness", what) && rangeField(c.massKg, 0.0f, 2.0e4f, "massKg", what) &&
         rangeField(c.loco.speed, 0.0f, 40.0f, "loco.speed", what);
    return ok;
}
} // namespace noctistest
