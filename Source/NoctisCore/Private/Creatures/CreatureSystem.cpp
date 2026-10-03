#include "Noctis/Creatures/CreatureSystem.h"

#include "Noctis/Core/Log.h"
#include "Noctis/Creatures/CreatureLogic.h"

#include <algorithm>
#include <cmath>

namespace noctis
{
namespace creaturesysimpl
{
constexpr double kYearSeconds = 365.25 * 86400.0;

float lodInterval(LodLevel l, float full, float nearV, float farV)
{
    switch (l)
    {
    case LodLevel::Full: return full;
    case LodLevel::Near: return nearV;
    default: return farV;
    }
}

BodyRegion pickRegion(Rng& rng, float relAngle, bool attackerMuchSmaller)
{
    const float a = std::fabs(relAngle);
    struct W
    {
        BodyRegion r;
        float w;
    };
    std::vector<W> table;
    const bool leftSide = relAngle > 0.0f;
    if (a < 0.8f)
    {
        table = {{BodyRegion::Head, 0.3f}, {BodyRegion::Neck, 0.3f}, {BodyRegion::ForelimbL, 0.08f}, {BodyRegion::ForelimbR, 0.08f}, {BodyRegion::Torso, 0.24f}};
    }
    else if (a > 2.3f)
    {
        table = {{BodyRegion::Tail, 0.4f}, {BodyRegion::HindlimbL, 0.22f}, {BodyRegion::HindlimbR, 0.22f}, {BodyRegion::Torso, 0.16f}};
    }
    else
    {
        table = {{BodyRegion::Torso, 0.3f},
                 {BodyRegion::Ribs, 0.25f},
                 {leftSide ? BodyRegion::HindlimbL : BodyRegion::HindlimbR, 0.2f},
                 {BodyRegion::Neck, 0.12f},
                 {BodyRegion::Tail, 0.13f}};
    }
    if (attackerMuchSmaller)
    {
        // Small attackers reach the limbs and tail of a large animal more easily than its neck.
        for (W& w : table)
        {
            if (w.r == BodyRegion::Neck || w.r == BodyRegion::Head)
            {
                w.w *= 0.3f;
            }
        }
    }
    float sum = 0.0f;
    for (const W& w : table)
    {
        sum += w.w;
    }
    float u = rng.uniform() * sum;
    for (const W& w : table)
    {
        u -= w.w;
        if (u <= 0.0f)
        {
            return w.r;
        }
    }
    return table.back().r;
}

InjuryType weaponInjury(const SpeciesSim& s, BodyRegion targetRegion, float severity, Rng& rng)
{
    if (s.hasWeapon(Weapon::TailClub))
    {
        return severity > 0.55f ? InjuryType::Fracture : InjuryType::Contusion;
    }
    if (s.hasWeapon(Weapon::Horns))
    {
        return InjuryType::Puncture;
    }
    if (s.hasWeapon(Weapon::HeadButt))
    {
        return InjuryType::Contusion;
    }
    if (s.hasWeapon(Weapon::Claws))
    {
        return rng.chance(0.6f) ? InjuryType::Laceration : InjuryType::Puncture;
    }
    if (s.hasWeapon(Weapon::Bite))
    {
        if (severity > 0.75f && (targetRegion == BodyRegion::HindlimbL || targetRegion == BodyRegion::HindlimbR || targetRegion == BodyRegion::Tail))
        {
            return InjuryType::Fracture;
        }
        return rng.chance(0.55f) ? InjuryType::Puncture : InjuryType::Laceration;
    }
    if (s.hasWeapon(Weapon::Kick))
    {
        return severity > 0.6f ? InjuryType::Fracture : InjuryType::Contusion;
    }
    return InjuryType::Contusion;
}
} // namespace creaturesysimpl

const char* formationLabelFr(Formation f)
{
    switch (f)
    {
    case Formation::Dispersed: return "Dispersé";
    case Formation::Loose: return "Lâche";
    case Formation::Compact: return "Compact";
    case Formation::Fleeing: return "En fuite";
    }
    return "?";
}

void CreatureSystem::initialize(const std::vector<SpeciesRuntime>* species, const Rect2& bounds, u64 seed)
{
    species_ = species;
    bounds_ = bounds;
    seed_ = seed;
    rng_.seed(seed, 0xC4EA);
    index_.configure(bounds, 64.0f);
    const size_t n = species ? species->size() : 0;
    births_.assign(n, 0);
    immigrants_.assign(n, 0);
    emigrants_.assign(n, 0);
    initialCounts_.assign(n, 0);
}

u32 CreatureSystem::createGroup(int speciesIndex)
{
    Group g;
    g.id = nextGroupId_++;
    g.speciesIndex = static_cast<i16>(speciesIndex);
    groups_.push_back(g);
    return g.id;
}

Creature& CreatureSystem::spawn(int speciesIndex, Sex sex, float ageYears, const Vec2& position, WorldContext& ctx, u32 groupId, EntityId mother,
                                EntityId father, u64 geneticSeed)
{
    using namespace creaturesysimpl;
    const SpeciesRuntime& sp = (*species_)[static_cast<size_t>(speciesIndex)];
    const SpeciesSim& s = sp.sim();
    Creature c;
    c.id = EntityId::make(EntityKind::Creature, nextSerial_++);
    c.speciesIndex = static_cast<i16>(speciesIndex);
    c.sex = sex;
    c.seed = hashCombine(hashCombine(seed_, c.id.value), geneticSeed);
    c.rng.seed(c.seed, c.id.value);
    c.birthTime = ctx.now - static_cast<double>(ageYears) * kYearSeconds;
    c.mother = mother;
    c.father = father;

    const Creature* mum = mother.valid() ? find(mother) : nullptr;
    const Creature* dad = father.valid() ? find(father) : nullptr;
    c.generation = std::max(mum ? mum->generation + 1 : 0u, dad ? dad->generation + 1 : 0u);
    // Heritable size and personality (heritability ~0.35, game assumption).
    const float h2 = 0.35f;
    float parentalSize = 1.0f;
    if (mum || dad)
    {
        parentalSize = 0.5f * ((mum ? mum->sizeGene : 1.0f) + (dad ? dad->sizeGene : 1.0f));
    }
    c.sizeGene = clampf(lerpf(1.0f, parentalSize, h2) + c.rng.normal(0.0f, 0.06f), 0.8f, 1.2f);
    for (int a = 0; a < kPersonalityAxisCount; ++a)
    {
        const TraitDistribution& t = s.personality[a];
        float parental = t.mean;
        if (mum || dad)
        {
            parental = 0.5f * ((mum ? mum->personality.axis[static_cast<size_t>(a)] : t.mean) + (dad ? dad->personality.axis[static_cast<size_t>(a)] : t.mean));
        }
        c.personality.axis[static_cast<size_t>(a)] = clampf(lerpf(t.mean, parental, h2) + c.rng.normal(0.0f, t.sd), 0.0f, 1.0f);
    }
    c.markingSeed = c.rng.uniform();

    const float dimorph = std::max(0.3f, s.sexualDimorphismMass);
    const float sexFactor = sex == Sex::Female ? std::sqrt(dimorph) : 1.0f / std::sqrt(dimorph);
    applyBodySize(c, sp, massAtAge(sp, ageYears, c.sizeGene) * sexFactor);

    const float fmr = fieldMetabolicRateW(sp, c.massKg);
    c.phys.energyCapacityMJ = std::max(1e-3f, s.metabolism.maxFastingDays * fmr * 86400.0f * 1e-6f);
    c.phys.energyReserveMJ = c.phys.energyCapacityMJ * c.rng.range(0.45f, 0.85f);
    c.phys.condition = c.phys.energyReserveMJ / c.phys.energyCapacityMJ;
    c.phys.gutCapacityKg = sp.gutCapacityFraction * c.massKg;
    c.phys.gutFillKg = c.phys.gutCapacityKg * c.rng.range(0.1f, 0.5f);
    const bool carnivore = s.diet.type == DietType::Carnivore || s.diet.type == DietType::Piscivore;
    c.phys.gutEnergyMJPerKg = carnivore ? sp.assimilatedMJPerKgMeat : sp.assimilatedMJPerKgPlant;
    c.phys.hydration = c.rng.range(0.8f, 1.0f);
    c.phys.bodyTempC = sp.targetBodyTempC;
    c.phys.sleepPressure = c.rng.range(0.0f, 0.5f);

    const Vec2 p = ctx.terrain->clampToBounds(position, 5.0f);
    c.loco.position = Vec3{p, ctx.terrain->heightAt(p)};
    c.loco.heading = c.rng.range(-kPi, kPi);
    c.loco.footCount = static_cast<u8>(sp.legCount);
    c.behavior.desiredDir = Vec2::fromHeading(c.loco.heading);
    c.behavior.nextDecision = ctx.now + c.rng.uniform() * 2.0;
    c.lastUpdate = ctx.now;
    c.lastLocomotion = ctx.now;
    c.lastPerception = ctx.now - 1.0;

    c.memory.homeCenter = p;
    c.memory.homeRadius = std::max(150.0f, s.social.territorial ? s.social.territoryRadiusM : 250.0f + 450.0f * std::log10(std::max(1.0f, c.massKg)));
    DrinkSite site;
    if (ctx.water && ctx.water->findDrinkSite(p, 4000.0f, site))
    {
        rememberPlace(c, MemoryKind::Water, site.position, 0.6f, ctx.now);
    }
    if (s.social.territorial && ageYears >= s.growth.maturityAgeYears)
    {
        c.social.territoryHolder = true;
        c.social.territoryCenter = p;
        c.social.territoryRadius = s.social.territoryRadiusM;
    }
    if (mother.valid())
    {
        c.social.followTarget = mother;
    }
    c.social.groupId = groupId;
    c.social.rank = c.rng.uniform();
    c.lod = LodLevel::Far;

    if (groupId != 0)
    {
        if (Group* g = findGroup(groupId))
        {
            g->members.push_back(c.id);
        }
    }
    creatures_.push_back(std::move(c));
    indexById_[creatures_.back().id.value] = static_cast<u32>(creatures_.size() - 1);
    if (initialCounts_.size() > static_cast<size_t>(speciesIndex) && ctx.now <= 1.0)
    {
        ++initialCounts_[static_cast<size_t>(speciesIndex)];
    }
    index_.insert(static_cast<u32>(creatures_.size() - 1), p);
    return creatures_.back();
}

Creature* CreatureSystem::find(EntityId id)
{
    auto it = indexById_.find(id.value);
    if (it == indexById_.end() || it->second >= creatures_.size())
    {
        return nullptr;
    }
    return &creatures_[it->second];
}

const Creature* CreatureSystem::find(EntityId id) const
{
    auto it = indexById_.find(id.value);
    if (it == indexById_.end() || it->second >= creatures_.size())
    {
        return nullptr;
    }
    return &creatures_[it->second];
}

Group* CreatureSystem::findGroup(u32 id)
{
    for (Group& g : groups_)
    {
        if (g.id == id)
        {
            return &g;
        }
    }
    return nullptr;
}

const Group* CreatureSystem::findGroup(u32 id) const
{
    for (const Group& g : groups_)
    {
        if (g.id == id)
        {
            return &g;
        }
    }
    return nullptr;
}

void CreatureSystem::neighbours(const Vec2& p, float radius, std::vector<u32>& out) const { index_.query(p, radius, out); }

void CreatureSystem::rebuildIndex()
{
    index_.clear();
    indexById_.clear();
    for (size_t i = 0; i < creatures_.size(); ++i)
    {
        indexById_[creatures_[i].id.value] = static_cast<u32>(i);
        if (creatures_[i].alive)
        {
            index_.insert(static_cast<u32>(i), creatures_[i].pos2());
        }
    }
    index_.finalize();
}

void CreatureSystem::assignLod(WorldContext& ctx)
{
    std::vector<std::pair<Vec2, float>> observers;
    if (ctx.emitters)
    {
        for (const Emitter& e : *ctx.emitters)
        {
            if (e.active && e.observer)
            {
                observers.push_back({e.position.xy(), e.observerRadiusM});
            }
        }
    }
    for (Creature& c : creatures_)
    {
        if (!c.alive)
        {
            continue;
        }
        float best = 1e9f;
        float fullR = lod_.fullRadiusM;
        for (const auto& o : observers)
        {
            const float d = distance(o.first, c.pos2());
            if (d < best)
            {
                best = d;
                fullR = std::max(lod_.fullRadiusM, o.second);
            }
        }
        LodLevel l = LodLevel::Far;
        if (best < fullR && lod_.allowFull)
        {
            l = LodLevel::Full;
        }
        else if (best < lod_.nearRadiusM)
        {
            l = LodLevel::Near;
        }
        if (l == LodLevel::Full && c.lod != LodLevel::Full)
        {
            // Promotion: resynchronise timers so the individual does not "catch up" a large dt in one go.
            c.lastLocomotion = ctx.now;
            c.loco.gaitPhase = c.rng.uniform();
        }
        c.lod = l;
    }
}

void CreatureSystem::step(WorldContext& ctx)
{
    using namespace creaturesysimpl;
    if (!species_)
    {
        return;
    }
    const double now = ctx.now;
    if (now - lastLodAssign_ >= 1.0)
    {
        assignLod(ctx);
        lastLodAssign_ = now;
    }
    index_.finalize();
    const size_t n = creatures_.size();
    JobScheduler& jobs = *ctx.jobs;

    // 1) Body + 2) perception (parallel, writes only to self).
    jobs.parallelFor(n, [&](size_t i) {
        Creature& c = creatures_[i];
        if (!c.alive)
        {
            return;
        }
        const SpeciesRuntime& sp = (*species_)[static_cast<size_t>(c.speciesIndex)];
        const float bodyInterval = lodInterval(c.lod, 0.0f, 1.0f, 4.0f);
        const float elapsed = static_cast<float>(now - c.lastUpdate);
        if (elapsed >= bodyInterval && elapsed > 0.0f)
        {
            updatePhysiology(c, sp, ctx, elapsed);
            updateInjuries(c, sp, ctx, elapsed);
            updateMemory(c, sp, ctx, elapsed);
            c.lastUpdate = now;
        }
        const float perceptionInterval = lodInterval(c.lod, lod_.perceptionFullS, lod_.perceptionNearS, lod_.perceptionFarS);
        if (now - c.lastPerception >= perceptionInterval)
        {
            perceive(c, sp, *this, ctx, static_cast<float>(now - c.lastPerception));
        }
    }, 8);

    // 3) Decisions and behaviour execution (parallel).
    jobs.parallelFor(n, [&](size_t i) {
        Creature& c = creatures_[i];
        if (!c.alive || now < c.behavior.nextDecision)
        {
            return;
        }
        const SpeciesRuntime& sp = (*species_)[static_cast<size_t>(c.speciesIndex)];
        const float interval = lodInterval(c.lod, lod_.decisionFullS, lod_.decisionNearS, lod_.decisionFarS);
        const float since = static_cast<float>(std::min(30.0, now - (c.behavior.nextDecision - interval)));
        decide(c, sp, *this, ctx);
        executeBehavior(c, sp, *this, ctx, std::max(interval, since));
        c.behavior.nextDecision = now + interval * (0.85 + 0.3 * c.rng.uniform());
    }, 8);

    // 4) Locomotion (parallel).
    jobs.parallelFor(n, [&](size_t i) {
        Creature& c = creatures_[i];
        if (!c.alive)
        {
            return;
        }
        const SpeciesRuntime& sp = (*species_)[static_cast<size_t>(c.speciesIndex)];
        const float interval = lodInterval(c.lod, 0.0f, lod_.locomotionNearS, lod_.locomotionFarS);
        const float elapsed = static_cast<float>(now - c.lastLocomotion);
        if (elapsed < interval || elapsed <= 0.0f)
        {
            return;
        }
        // Large steps are sub-stepped to keep the dynamics stable.
        const float maxSub = c.lod == LodLevel::Full ? 0.1f : 1.0f;
        const int steps = std::max(1, static_cast<int>(std::ceil(elapsed / maxSub)));
        const float h = elapsed / static_cast<float>(steps);
        for (int k = 0; k < std::min(steps, 16); ++k)
        {
            integrateLocomotion(c, sp, ctx, h, c.lod == LodLevel::Full);
        }
        if (c.lod == LodLevel::Full)
        {
            updateSoftTissue(c, sp, elapsed);
        }
        c.lastLocomotion = now;
    }, 8);

    // 5) Serial resolution in deterministic order.
    resolveIntents(ctx);
    resolveContacts(ctx);
    drainFootprints(ctx);
    handleDeaths(ctx);
    handleHatching(ctx);
    updateGroups(ctx);
    rebuildIndex();
}

void CreatureSystem::emitCall(Creature& c, CallContext context, WorldContext& ctx)
{
    const SpeciesRuntime& sp = speciesOf(c);
    const SpeciesSim& s = sp.sim();
    const CallTypeSpec* spec = s.findCall(context);
    if (!spec)
    {
        if (s.vocal.calls.empty())
        {
            return;
        }
        spec = &s.vocal.calls.front();
    }
    size_t callIndex = 0;
    for (size_t i = 0; i < s.vocal.calls.size(); ++i)
    {
        if (&s.vocal.calls[i] == spec)
        {
            callIndex = i;
        }
    }
    SoundSource src;
    src.kind = SoundKind::Vocalization;
    src.emitter = c.id;
    src.speciesIndex = c.speciesIndex;
    src.position = c.loco.position + Vec3{0.0f, 0.0f, c.hipHeightM * 1.2f};
    src.startTime = ctx.now;
    src.durationS = spec->durationS * (0.9f + 0.2f * c.rng.uniform());
    const float sizeRatio = c.massKg / std::max(0.01f, s.adultMassKg);
    // Frequency scales with body size (allometric scaling of vocal frequencies, cf. Fletcher 2004) —
    // applied to call hypotheses; individual variation gives each animal a recognisable voice.
    src.pitchShift = std::pow(std::max(0.001f, sizeRatio), -0.4f) * (0.95f + 0.1f * c.markingSeed);
    src.f0Hz = spec->f0Hz;
    src.levelDb = spec->sourceLevelDb + 5.0f * std::log10(std::max(0.01f, sizeRatio));
    src.bandwidthHz = spec->f0Hz * 0.5f;
    src.tonal = true;
    src.callContext = static_cast<u8>(context);
    src.callIndex = static_cast<u8>(callIndex);
    src.seed = c.seed ^ static_cast<u64>(ctx.tick);
    ctx.sound->emit(src);
    SimEvent e;
    e.type = EventType::Vocalization;
    e.time = ctx.now;
    e.subject = c.id;
    e.position = src.position;
    e.magnitude = src.levelDb;
    e.param = static_cast<u32>(context);
    e.param2 = static_cast<u32>(callIndex);
    ctx.events->emit(e);
}

void CreatureSystem::applyInjury(Creature& c, const Injury& injury, WorldContext& ctx)
{
    if (!c.alive)
    {
        return;
    }
    c.injuries.active.push_back(injury);
    if (c.injuries.active.size() > 24)
    {
        // Merge the mildest injuries to bound memory.
        auto mildest = std::min_element(c.injuries.active.begin(), c.injuries.active.end(),
                                        [](const Injury& a, const Injury& b) { return a.severity < b.severity; });
        c.injuries.active.erase(mildest);
    }
    recomputeImpairment(c, speciesOf(c));
    SimEvent e;
    e.type = EventType::Injury;
    e.time = ctx.now;
    e.subject = c.id;
    e.other = injury.cause;
    e.position = c.loco.position;
    e.magnitude = injury.severity;
    e.param = static_cast<u32>(injury.region);
    e.param2 = static_cast<u32>(injury.type);
    ctx.events->emit(e);
}

void CreatureSystem::kill(Creature& c, DeathCause cause, WorldContext& ctx)
{
    if (!c.alive)
    {
        return;
    }
    c.alive = false;
    c.deathTime = ctx.now;
    c.deathCause = cause;
    c.behavior.intents.clear();
    const EntityId carcass = ctx.carcasses->create(c.speciesIndex, c.id, c.pos2(), c.loco.heading, c.massKg, ctx.now, cause,
                                                   static_cast<float>(c.ageYears(ctx.now)));
    // Healed bite injuries survive on the skeleton as healed bite marks (paleopathology).
    for (const Scar& s : c.injuries.scars)
    {
        if (s.type == InjuryType::Puncture || s.type == InjuryType::Laceration)
        {
            BiteMark m;
            m.type = BiteMarkType::Score;
            m.bodyRegion = static_cast<u8>(s.region);
            m.perimortem = false;
            m.time = s.time;
            ctx.carcasses->addBiteMark(carcass, m);
        }
    }
    SimEvent e;
    e.type = EventType::Death;
    e.time = ctx.now;
    e.subject = c.id;
    e.other = carcass;
    e.position = c.loco.position;
    e.magnitude = c.massKg;
    e.param = static_cast<u32>(cause);
    ctx.events->emit(e);
    SimEvent ce = e;
    ce.type = EventType::CarcassCreated;
    ce.subject = carcass;
    ce.other = c.id;
    ctx.events->emit(ce);
}

void CreatureSystem::resolveAttack(Creature& a, Creature& b, WorldContext& ctx)
{
    using namespace creaturesysimpl;
    const SpeciesRuntime& spA = speciesOf(a);
    const SpeciesRuntime& spB = speciesOf(b);
    const SpeciesSim& sa = spA.sim();
    const SpeciesSim& sb = spB.sim();
    a.behavior.lastAttack = ctx.now;
    const Vec2 fromB = a.pos2() - b.pos2();
    const float rel = angleDelta(b.loco.heading, fromB.heading());
    const BodyRegion region = pickRegion(a.rng, rel, a.massKg < b.massKg * 0.15f);
    const bool predatory = sa.predation.style != HuntStyle::None && a.behavior.current == BehaviorId::Hunt;
    const float strength = predatory ? sa.predation.attackStrength : std::max(sa.defense.defenseStrength, sa.predation.attackStrength);
    const float sizeTerm = clampf(std::sqrt(a.massKg / std::max(0.1f, b.massKg)), 0.15f, 2.5f);
    const float armor = spB.regions[static_cast<size_t>(region)].armor;
    float ritual = 1.0f;
    for (const ActionIntent& it : a.behavior.intents)
    {
        if (it.kind == ActionKind::Attack && it.target == b.id)
        {
            ritual = it.amount;
        }
    }
    const float severity = saturate((0.12f + 0.45f * strength * sizeTerm * a.rng.range(0.6f, 1.4f)) * (1.0f - armor) * ritual * a.injuries.defenseFactor);
    Injury inj;
    inj.region = region;
    inj.type = weaponInjury(sa, region, severity, a.rng);
    inj.severity = severity;
    inj.initialSeverity = severity;
    inj.bleeding = (inj.type == InjuryType::Puncture || inj.type == InjuryType::Laceration) ? severity * 0.3f : 0.0f;
    inj.time = ctx.now;
    inj.cause = a.id;
    inj.causeSpecies = a.speciesIndex;
    applyInjury(b, inj, ctx);

    SimEvent e;
    e.type = EventType::Attack;
    e.time = ctx.now;
    e.subject = a.id;
    e.other = b.id;
    e.position = b.loco.position;
    e.magnitude = severity;
    e.param = static_cast<u32>(region);
    ctx.events->emit(e);
    // Victim: instantly aware, frightened, remembers the place.
    {
        Awareness* aw = nullptr;
        for (Awareness& x : b.perception.known)
        {
            if (x.entity == a.id)
            {
                aw = &x;
            }
        }
        if (!aw)
        {
            b.perception.known.push_back(Awareness{});
            aw = &b.perception.known.back();
            aw->entity = a.id;
        }
        aw->speciesIndex = a.speciesIndex;
        aw->evidence = 1.5f;
        aw->identification = 1.2f;
        aw->stage = AwarenessStage::Identified;
        aw->relation = a.speciesIndex == b.speciesIndex ? Relation::Conspecific : Relation::Predator;
        aw->threat = 1.0f;
        aw->estimatedPos = a.pos2();
        aw->uncertaintyM = 1.0f;
        aw->lastUpdate = ctx.now;
        b.perception.alertness = 1.0f;
        b.phys.stress = 1.0f;
        rememberPlace(b, MemoryKind::Danger, b.pos2(), -1.0f, ctx.now, a.id);
        b.behavior.nextDecision = ctx.now; // react immediately
        if (!sb.vocal.calls.empty())
        {
            emitCall(b, CallContext::Distress, ctx);
        }
    }
    // Counter-attack with the prey's own weapons (horns face forward, tail clubs swing behind/sideways).
    const float relA = std::fabs(angleDelta(b.loco.heading, (a.pos2() - b.pos2()).heading()));
    const bool facing = relA < 1.0f;
    const bool behind = relA > 1.8f;
    bool canStrike = false;
    if (sb.hasWeapon(Weapon::Horns) || sb.hasWeapon(Weapon::Bite) || sb.hasWeapon(Weapon::HeadButt) || sb.hasWeapon(Weapon::Beak))
    {
        canStrike = canStrike || facing;
    }
    if (sb.hasWeapon(Weapon::TailClub) || sb.hasWeapon(Weapon::TailWhip))
    {
        canStrike = canStrike || behind || !facing;
    }
    if (sb.hasWeapon(Weapon::Kick) || sb.hasWeapon(Weapon::Claws))
    {
        canStrike = true;
    }
    if (canStrike && b.alive && b.rng.chance(sb.defense.defenseStrength * b.injuries.defenseFactor * 0.55f))
    {
        const BodyRegion r = pickRegion(b.rng, 0.0f, b.massKg < a.massKg * 0.15f);
        const float sev = saturate((0.1f + 0.5f * sb.defense.defenseStrength * clampf(std::sqrt(b.massKg / std::max(0.1f, a.massKg)), 0.15f, 2.5f) *
                                               b.rng.range(0.6f, 1.4f)) *
                                   (1.0f - spA.regions[static_cast<size_t>(r)].armor));
        Injury back;
        back.region = r;
        back.type = weaponInjury(sb, r, sev, b.rng);
        back.severity = sev;
        back.initialSeverity = sev;
        back.bleeding = (back.type == InjuryType::Puncture || back.type == InjuryType::Laceration) ? sev * 0.25f : 0.0f;
        back.time = ctx.now;
        back.cause = b.id;
        back.causeSpecies = b.speciesIndex;
        applyInjury(a, back, ctx);
        SimEvent ce;
        ce.type = EventType::Attack;
        ce.time = ctx.now;
        ce.subject = b.id;
        ce.other = a.id;
        ce.position = a.loco.position;
        ce.magnitude = sev;
        ce.param = static_cast<u32>(r);
        ctx.events->emit(ce);
        rememberPlace(a, MemoryKind::Danger, a.pos2(), -0.6f, ctx.now, b.id);
    }
    // Death of the victim.
    if (b.injuries.healthIndex <= 0.0f || b.phys.bloodLoss > 0.45f)
    {
        kill(b, DeathCause::Predation, ctx);
        const EntityId carcass = EntityId::make(EntityKind::Carcass, ctx.carcasses->nextSerial() - 1);
        BiteMark m;
        m.makerSpecies = a.speciesIndex;
        m.type = BiteMarkType::Puncture;
        m.bodyRegion = static_cast<u8>(region);
        m.toothSpacingMm = spA.toothSpacingMmPerM * a.lengthM;
        m.depthMm = severity * 30.0f;
        m.time = ctx.now;
        ctx.carcasses->addBiteMark(carcass, m);
        if (predatory)
        {
            ++a.history.huntsAttempted;
            ++a.history.huntsSucceeded;
            a.behavior.current = BehaviorId::FeedCarcass;
            a.behavior.target = carcass;
            a.behavior.hunt = HuntPhase::None;
            a.behavior.startedAt = ctx.now;
            a.behavior.commitUntil = ctx.now + 120.0;
            SimEvent k;
            k.type = EventType::HuntKill;
            k.time = ctx.now;
            k.subject = a.id;
            k.other = b.id;
            k.position = b.loco.position;
            k.magnitude = b.massKg;
            ctx.events->emit(k);
            rememberPlace(a, MemoryKind::Food, b.pos2(), 0.8f, ctx.now);
        }
    }
    else if (a.speciesIndex != b.speciesIndex)
    {
        ++b.history.attacksSurvived;
    }
    if (a.speciesIndex == b.speciesIndex)
    {
        // Intraspecific contest: update dominance memories.
        const bool aWins = a.massKg * a.phys.condition * (0.5f + a.personality.get(PersonalityAxis::Aggression)) >
                           b.massKg * b.phys.condition * (0.5f + b.personality.get(PersonalityAxis::Aggression));
        (aWins ? a : b).history.fightsWon++;
        (aWins ? b : a).history.fightsLost++;
        Creature& loser = aWins ? b : a;
        loser.social.rank = std::max(0.0f, loser.social.rank - 0.1f);
        (aWins ? a : b).social.rank = std::min(1.0f, (aWins ? a : b).social.rank + 0.1f);
        SimEvent f;
        f.type = EventType::Fight;
        f.time = ctx.now;
        f.subject = a.id;
        f.other = b.id;
        f.position = a.loco.position;
        f.magnitude = severity;
        ctx.events->emit(f);
    }
}

void CreatureSystem::attackEmitter(Creature& a, Emitter& target, WorldContext& ctx)
{
    const SpeciesRuntime& sp = speciesOf(a);
    a.behavior.lastAttack = ctx.now;
    const float sizeTerm = clampf(std::sqrt(a.massKg / std::max(1.0f, target.massKg)), 0.2f, 4.0f);
    const float severity = target.id.kind() == EntityKind::Vehicle
                               ? saturate(0.05f * sizeTerm * a.rng.range(0.5f, 1.5f))
                               : saturate((0.15f + 0.4f * std::max(sp.sim().predation.attackStrength, sp.sim().defense.defenseStrength) * sizeTerm) *
                                          a.rng.range(0.6f, 1.4f));
    SimEvent e;
    e.type = EventType::ResearcherAttacked;
    e.time = ctx.now;
    e.subject = a.id;
    e.other = target.id;
    e.position = target.position;
    e.magnitude = severity;
    ctx.events->emit(e);
}

void CreatureSystem::resolveIntents(WorldContext& ctx)
{
    const double now = ctx.now;
    for (size_t i = 0; i < creatures_.size(); ++i)
    {
        Creature& c = creatures_[i];
        if (!c.alive || c.behavior.intents.empty())
        {
            c.behavior.intents.clear();
            continue;
        }
        const SpeciesRuntime& sp = speciesOf(c);
        const SpeciesSim& s = sp.sim();
        std::vector<ActionIntent> intents;
        intents.swap(c.behavior.intents);
        for (const ActionIntent& it : intents)
        {
            if (!c.alive)
            {
                break;
            }
            switch (it.kind)
            {
            case ActionKind::EatPlants:
            {
                const float room = std::max(0.0f, c.phys.gutCapacityKg - c.phys.gutFillKg);
                const float sizeRatio = c.hipHeightM / std::max(0.05f, s.hipHeightM);
                const ForageResult r = ctx.vegetation->consume(c.pos2(), s.diet.browseMinM * sizeRatio, std::max(0.2f, s.diet.browseMaxM * sizeRatio),
                                                               s.diet.plantPreference, std::min(room, it.amount));
                if (r.eatenKg > 0.0f)
                {
                    c.phys.gutEnergyMJPerKg = lerpf(c.phys.gutEnergyMJPerKg, sp.assimilatedMJPerKgPlant, r.eatenKg / std::max(1e-3f, c.phys.gutFillKg + r.eatenKg));
                    c.phys.gutFillKg += r.eatenKg;
                    c.phys.hydration = std::min(1.0f, c.phys.hydration + r.eatenKg * 1.5f / std::max(1e-3f, sp.waterBufferFraction * c.massKg));
                    ctx.vegetation->addNutrients(c.pos2(), r.eatenKg * 0.35f); // dung returns part of the intake
                    if (c.rng.chance(0.05f))
                    {
                        rememberPlace(c, MemoryKind::Food, c.pos2(), 0.5f, now);
                    }
                }
                else
                {
                    c.behavior.hasTargetPos = false;
                }
                // Opportunistic egg predation by egg-eaters (unattended nests only).
                if (s.diet.eatsEggs)
                {
                    std::vector<EntityId> nests;
                    ctx.nests->queryNear(c.pos2(), 10.0f, nests);
                    for (const EntityId nid : nests)
                    {
                        const Nest* nest = ctx.nests->find(nid);
                        if (nest && nest->speciesIndex != c.speciesIndex && nest->attendance < 0.3f)
                        {
                            const int eaten = ctx.nests->predate(nid, 2, now, *ctx.events, c.id);
                            c.phys.gutFillKg += static_cast<float>(eaten) * 0.3f;
                        }
                    }
                }
                break;
            }
            case ActionKind::EatCarcass:
            {
                const float room = std::max(0.0f, c.phys.gutCapacityKg - c.phys.gutFillKg);
                const float taken = ctx.carcasses->feed(it.target, std::min(room, it.amount), c.speciesIndex, now);
                if (taken > 0.0f)
                {
                    c.phys.gutEnergyMJPerKg = lerpf(c.phys.gutEnergyMJPerKg, sp.assimilatedMJPerKgMeat, taken / std::max(1e-3f, c.phys.gutFillKg + taken));
                    c.phys.gutFillKg += taken;
                    c.phys.hydration = std::min(1.0f, c.phys.hydration + taken * 0.6f / std::max(1e-3f, sp.waterBufferFraction * c.massKg));
                    if (c.rng.chance(saturate(taken / 150.0f)))
                    {
                        BiteMark m;
                        m.makerSpecies = c.speciesIndex;
                        m.type = c.rng.chance(0.5f) ? BiteMarkType::Score : BiteMarkType::Pit;
                        m.bodyRegion = static_cast<u8>(c.rng.rangeInt(0, kBodyRegionCount - 1));
                        m.toothSpacingMm = sp.toothSpacingMmPerM * c.lengthM;
                        m.depthMm = c.rng.range(1.0f, 12.0f) * std::cbrt(c.massKg / 1000.0f);
                        m.time = now;
                        ctx.carcasses->addBiteMark(it.target, m);
                    }
                    if (sp.hasTeeth && c.rng.chance(saturate(taken / 400.0f)))
                    {
                        ctx.carcasses->addShedTooth(it.target);
                        SimEvent e;
                        e.type = EventType::ToothShed;
                        e.time = now;
                        e.subject = c.id;
                        e.other = it.target;
                        e.position = c.loco.position;
                        ctx.events->emit(e);
                    }
                    if (now - c.behavior.startedAt < 2.0)
                    {
                        SimEvent e;
                        e.type = EventType::Feed;
                        e.time = now;
                        e.subject = c.id;
                        e.other = it.target;
                        e.position = c.loco.position;
                        e.magnitude = taken;
                        ctx.events->emit(e);
                    }
                }
                break;
            }
            case ActionKind::Drink:
            {
                const float depth = ctx.water ? ctx.water->depthAt(it.position, *ctx.terrain) : 0.0f;
                DrinkSite site;
                const bool valid = depth > 0.02f || (ctx.water && ctx.water->findDrinkSite(c.pos2(), 12.0f, site));
                if (valid)
                {
                    const float before = c.phys.hydration;
                    c.phys.hydration = std::min(1.0f, c.phys.hydration + it.amount / std::max(1e-3f, sp.waterBufferFraction * c.massKg));
                    if (before < 0.9f && c.rng.chance(0.2f))
                    {
                        rememberPlace(c, MemoryKind::Water, c.pos2(), 0.6f, now);
                    }
                    if (now - c.behavior.startedAt < 60.0 && c.rng.chance(0.05f))
                    {
                        SimEvent e;
                        e.type = EventType::Drink;
                        e.time = now;
                        e.subject = c.id;
                        e.position = c.loco.position;
                        ctx.events->emit(e);
                    }
                    ctx.water->addDisturbance(it.position, c.massKg * 0.001f, now);
                }
                else
                {
                    c.behavior.hasTargetPos = false;
                }
                break;
            }
            case ActionKind::Call:
                emitCall(c, static_cast<CallContext>(it.param), ctx);
                break;
            case ActionKind::Attack:
            {
                const float cooldown = 0.8f + 0.15f * std::cbrt(c.massKg / 100.0f);
                if (now - c.behavior.lastAttack < cooldown)
                {
                    break;
                }
                if (it.target.kind() == EntityKind::Creature)
                {
                    Creature* t = find(it.target);
                    if (t && t->alive && t->id != c.id)
                    {
                        const float reach = c.lengthM * sp.headReach + t->lengthM * speciesOf(*t).bodyRadius + 1.0f;
                        if (distance(c.pos2(), t->pos2()) <= reach)
                        {
                            resolveAttack(c, *t, ctx);
                        }
                    }
                }
                else if (ctx.emitters)
                {
                    for (Emitter& e : *ctx.emitters)
                    {
                        if (e.id == it.target && e.active && distance(c.pos2(), e.position.xy()) <= c.lengthM * sp.headReach + 1.5f)
                        {
                            attackEmitter(c, e, ctx);
                        }
                    }
                }
                break;
            }
            case ActionKind::Mate:
            {
                Creature* m = find(it.target);
                if (!m || !m->alive || m->sex == c.sex || !m->repro.receptive || !c.repro.receptive || distance(c.pos2(), m->pos2()) > 6.0f + c.lengthM * 0.3f)
                {
                    break;
                }
                Creature& female = c.sex == Sex::Female ? c : *m;
                Creature& male = c.sex == Sex::Female ? *m : c;
                female.repro.gravid = true;
                female.repro.gravidSince = now;
                female.repro.mate = male.id;
                female.repro.receptive = false;
                female.repro.lastBreedingYear = ctx.clock->year();
                male.repro.mate = female.id;
                SimEvent e;
                e.type = EventType::Mating;
                e.time = now;
                e.subject = female.id;
                e.other = male.id;
                e.position = female.loco.position;
                ctx.events->emit(e);
                break;
            }
            case ActionKind::LayEggs:
            {
                // Egg formation time after mating (game assumption: 10 days).
                if (!c.repro.gravid || now - c.repro.gravidSince < 10.0 * 86400.0)
                {
                    break;
                }
                const EntityId nid = ctx.nests->create(c.speciesIndex, s.reproduction.nest, c.id, c.pos2(), now);
                const int count = std::clamp(static_cast<int>(std::lround(c.rng.normal(s.reproduction.clutchSizeMean, s.reproduction.clutchSizeMean * 0.2f))), 1,
                                             std::max(1, static_cast<int>(s.reproduction.clutchSizeMax)));
                for (int k = 0; k < count; ++k)
                {
                    Egg egg;
                    egg.mother = c.id;
                    egg.father = c.repro.mate;
                    egg.geneticSeed = c.rng.nextU32();
                    egg.viable = c.rng.chance(0.9f);
                    ctx.nests->addEgg(nid, egg);
                }
                c.repro.gravid = false;
                c.repro.nest = nid;
                ++c.repro.clutches;
                rememberPlace(c, MemoryKind::Nest, c.pos2(), 0.8f, now, nid);
                SimEvent e;
                e.type = EventType::EggsLaid;
                e.time = now;
                e.subject = c.id;
                e.other = nid;
                e.position = c.loco.position;
                e.magnitude = static_cast<float>(count);
                ctx.events->emit(e);
                SimEvent nb = e;
                nb.type = EventType::NestBuilt;
                ctx.events->emit(nb);
                break;
            }
            case ActionKind::AttendNest:
                ctx.nests->markAttended(it.target, it.amount);
                break;
            case ActionKind::PredateNest:
                ctx.nests->predate(it.target, 2, now, *ctx.events, c.id);
                break;
            case ActionKind::EatPopulation:
            {
                if (!ctx.harvester)
                {
                    break;
                }
                const float room = std::max(0.0f, c.phys.gutCapacityKg - c.phys.gutFillKg);
                const float got = ctx.harvester->harvest(static_cast<PopulationResource>(it.param), c.pos2(), std::min(room, it.amount), c.rng.nextU32());
                if (got > 0.0f)
                {
                    c.phys.gutEnergyMJPerKg = lerpf(c.phys.gutEnergyMJPerKg, sp.assimilatedMJPerKgMeat, got / std::max(1e-3f, c.phys.gutFillKg + got));
                    c.phys.gutFillKg += got;
                    c.phys.hydration = std::min(1.0f, c.phys.hydration + got * 0.6f / std::max(1e-3f, sp.waterBufferFraction * c.massKg));
                }
                break;
            }
            case ActionKind::Display:
            case ActionKind::Defecate:
            case ActionKind::None:
                break;
            }
        }
    }
}

void CreatureSystem::resolveContacts(WorldContext& ctx)
{
    (void)ctx;
    // Overlap resolution between nearby Full-LOD bodies; heavier animals are displaced less.
    std::vector<u32> near;
    for (size_t i = 0; i < creatures_.size(); ++i)
    {
        Creature& a = creatures_[i];
        if (!a.alive || a.lod != LodLevel::Full)
        {
            continue;
        }
        const float ra = a.lengthM * speciesOf(a).bodyRadius;
        near.clear();
        index_.query(a.pos2(), ra + 15.0f, near);
        for (const u32 j : near)
        {
            if (j <= i)
            {
                continue;
            }
            Creature& b = creatures_[j];
            if (!b.alive)
            {
                continue;
            }
            const float rb = b.lengthM * speciesOf(b).bodyRadius;
            const Vec2 d = b.pos2() - a.pos2();
            const float dist = d.length();
            const float minD = ra + rb;
            if (dist < minD && dist > 1e-4f)
            {
                const float overlap = minD - dist;
                const float wa = b.massKg / (a.massKg + b.massKg);
                const Vec2 n = d / dist;
                a.loco.position.x -= n.x * overlap * wa;
                a.loco.position.y -= n.y * overlap * wa;
                b.loco.position.x += n.x * overlap * (1.0f - wa);
                b.loco.position.y += n.y * overlap * (1.0f - wa);
            }
        }
    }
}

void CreatureSystem::drainFootprints(WorldContext& ctx)
{
    for (Creature& c : creatures_)
    {
        if (c.pendingFootprints.empty())
        {
            continue;
        }
        for (const Footprint& f : c.pendingFootprints)
        {
            ctx.vegetation->trample(f.position, c.massKg);
            if (f.depthM > 0.002f)
            {
                ctx.tracks->add(f);
            }
            if (ctx.water && c.lod != LodLevel::Far && ctx.water->depthAt(f.position, *ctx.terrain) > 0.05f)
            {
                ctx.water->addDisturbance(f.position, c.massKg * 0.002f * (1.0f + c.loco.speed), ctx.now);
            }
        }
        c.pendingFootprints.clear();
    }
}

void CreatureSystem::handleDeaths(WorldContext& ctx)
{
    for (Creature& c : creatures_)
    {
        if (!c.alive)
        {
            continue;
        }
        const bool dying = c.injuries.healthIndex <= 0.0f || c.phys.bloodLoss > 0.45f || c.phys.hydration <= 0.0f ||
                           (c.phys.condition <= 0.0f && c.phys.gutFillKg <= 0.0f && c.phys.hunger > 0.95f && c.rng.chance(0.001f));
        if (!dying)
        {
            continue;
        }
        DeathCause cause = DeathCause::Injury;
        float maxInfection = 0.0f;
        double lastAttack = -1e9;
        for (const Injury& i : c.injuries.active)
        {
            maxInfection = std::max(maxInfection, i.infection);
            if (i.causeSpecies >= 0 && i.causeSpecies != c.speciesIndex)
            {
                lastAttack = std::max(lastAttack, i.time);
            }
        }
        if (c.phys.hydration <= 0.0f)
        {
            cause = DeathCause::Dehydration;
        }
        else if (c.phys.condition <= 0.0f && c.injuries.active.empty())
        {
            cause = DeathCause::Starvation;
        }
        else if (maxInfection > 0.5f)
        {
            cause = DeathCause::Infection;
        }
        else if (ctx.now - lastAttack < 6.0 * 3600.0)
        {
            cause = DeathCause::Predation;
        }
        kill(c, cause, ctx);
    }
    // Compact dead individuals into the archive once in a while.
    size_t deadCount = 0;
    for (const Creature& c : creatures_)
    {
        deadCount += c.alive ? 0 : 1;
    }
    if (deadCount > 16 || (deadCount > 0 && creatures_.size() < 64))
    {
        std::vector<Creature> alive;
        alive.reserve(creatures_.size());
        for (Creature& c : creatures_)
        {
            if (c.alive)
            {
                alive.push_back(std::move(c));
                continue;
            }
            DeathRecord d;
            d.id = c.id;
            d.speciesIndex = c.speciesIndex;
            d.sex = c.sex;
            d.birthTime = c.birthTime;
            d.deathTime = c.deathTime;
            d.cause = c.deathCause;
            d.mother = c.mother;
            d.father = c.father;
            d.massKg = c.massKg;
            d.generation = c.generation;
            d.scars = static_cast<int>(c.injuries.scars.size());
            d.position = c.pos2();
            deaths_.push_back(d);
            if (Group* g = findGroup(c.social.groupId))
            {
                g->members.erase(std::remove(g->members.begin(), g->members.end(), c.id), g->members.end());
            }
        }
        creatures_.swap(alive);
        rebuildIndex();
    }
}

void CreatureSystem::handleHatching(WorldContext& ctx)
{
    if (pendingHatch_.empty())
    {
        return;
    }
    std::vector<HatchRecord> hatch;
    hatch.swap(pendingHatch_);
    for (const HatchRecord& h : hatch)
    {
        const Creature* mum = find(h.egg.mother);
        const u32 group = mum && mum->alive ? mum->social.groupId : 0u;
        Creature& baby = spawn(h.speciesIndex, rng_.chance(0.5f) ? Sex::Female : Sex::Male, 0.0f, h.position + Vec2{rng_.range(-1.0f, 1.0f), rng_.range(-1.0f, 1.0f)},
                               ctx, group, h.egg.mother, h.egg.father, h.egg.geneticSeed);
        ++births_[static_cast<size_t>(h.speciesIndex)];
        SimEvent e;
        e.type = EventType::Birth;
        e.time = ctx.now;
        e.subject = baby.id;
        e.other = h.egg.mother;
        e.position = baby.loco.position;
        ctx.events->emit(e);
        if (Creature* m = find(h.egg.mother))
        {
            ++m->history.offspringHatched;
        }
    }
}

void CreatureSystem::updateGroups(WorldContext& ctx)
{
    for (Group& g : groups_)
    {
        std::vector<const Creature*> members;
        g.members.erase(std::remove_if(g.members.begin(), g.members.end(), [&](EntityId id) {
                            const Creature* c = find(id);
                            if (!c || !c->alive || c->social.groupId != g.id)
                            {
                                return true;
                            }
                            members.push_back(c);
                            return false;
                        }),
                        g.members.end());
        if (members.empty())
        {
            continue;
        }
        Vec2 centroid;
        Vec2 vel;
        float meanLen = 0.0f;
        float alert = 0.0f;
        int fleeing = 0;
        const Creature* leader = members.front();
        for (const Creature* c : members)
        {
            centroid += c->pos2();
            vel += c->loco.velocity;
            meanLen += c->lengthM;
            alert = std::max(alert, c->perception.alertness);
            fleeing += c->behavior.current == BehaviorId::Flee ? 1 : 0;
            if (c->social.rank * c->massKg > leader->social.rank * leader->massKg)
            {
                leader = c;
            }
        }
        const float n = static_cast<float>(members.size());
        g.centroid = centroid / n;
        g.velocity = vel / n;
        meanLen /= n;
        g.alert = lerpf(g.alert, alert, 0.3f);
        g.leader = leader->id;
        float nnSum = 0.0f;
        float spread = 0.0f;
        for (const Creature* a : members)
        {
            float nn = 1e9f;
            for (const Creature* b : members)
            {
                if (a != b)
                {
                    nn = std::min(nn, distance(a->pos2(), b->pos2()));
                }
            }
            nnSum += nn < 1e8f ? nn : 0.0f;
            spread = std::max(spread, distance(a->pos2(), g.centroid));
        }
        g.meanNeighbourDistance = members.size() > 1 ? nnSum / n : 0.0f;
        g.spread = spread;
        if (leader->behavior.hasTargetPos &&
            (leader->behavior.current == BehaviorId::Drink || leader->behavior.current == BehaviorId::Forage || leader->behavior.current == BehaviorId::Wander))
        {
            g.destination = leader->behavior.targetPos;
            g.hasDestination = true;
        }
        else
        {
            g.hasDestination = false;
        }
        // Formation classification from measurable spacing — this is what an observer can measure too.
        // Hysteresis bands avoid flickering between classes when spacing hovers near a threshold.
        Formation f = g.formation;
        if (members.size() > 1)
        {
            const float nn = g.meanNeighbourDistance / std::max(0.1f, meanLen);
            const bool fleeingNow = static_cast<float>(fleeing) >= 0.4f * n && g.velocity.length() > 1.5f;
            if (fleeingNow)
            {
                f = Formation::Fleeing;
            }
            else if (g.formation == Formation::Fleeing)
            {
                f = nn < 1.8f ? Formation::Compact : Formation::Loose;
            }
            else if (nn < 1.5f || (g.formation == Formation::Compact && nn < 2.1f))
            {
                f = Formation::Compact;
            }
            else if (nn > 5.5f || (g.formation == Formation::Dispersed && nn > 4.5f))
            {
                f = Formation::Dispersed;
            }
            else
            {
                f = Formation::Loose;
            }
        }
        if (f != g.formation && ctx.now - g.formationSince > (f == Formation::Fleeing ? 5.0 : 120.0))
        {
            const Formation before = g.formation;
            g.formation = f;
            g.formationSince = ctx.now;
            if (members.size() < 3)
            {
                continue;
            }
            SimEvent e;
            e.type = EventType::GroupFormationChange;
            e.time = ctx.now;
            e.subject = EntityId::make(EntityKind::Group, g.id);
            e.other = g.leader;
            e.position = Vec3{g.centroid, 0.0f};
            e.magnitude = g.meanNeighbourDistance;
            e.param = static_cast<u32>(f);
            e.param2 = static_cast<u32>(before);
            ctx.events->emit(e);
        }
        // Fission: stragglers far from the group leave it.
        for (const Creature* c : members)
        {
            if (distance(c->pos2(), g.centroid) > 700.0f && members.size() > 2)
            {
                Creature* m = find(c->id);
                m->social.groupId = 0;
                SimEvent e;
                e.type = EventType::GroupSplit;
                e.time = ctx.now;
                e.subject = EntityId::make(EntityKind::Group, g.id);
                e.other = c->id;
                e.position = c->loco.position;
                ctx.events->emit(e);
            }
        }
    }
    // Fusion of nearby groups of the same species.
    for (size_t i = 0; i < groups_.size(); ++i)
    {
        for (size_t j = i + 1; j < groups_.size(); ++j)
        {
            Group& a = groups_[i];
            Group& b = groups_[j];
            if (a.speciesIndex != b.speciesIndex || a.members.empty() || b.members.empty())
            {
                continue;
            }
            const float maxSize = (*species_)[static_cast<size_t>(a.speciesIndex)].sim().social.groupSizeMax;
            if (distance(a.centroid, b.centroid) < 70.0f && static_cast<float>(a.members.size() + b.members.size()) <= maxSize)
            {
                for (const EntityId id : b.members)
                {
                    if (Creature* c = find(id))
                    {
                        c->social.groupId = a.id;
                    }
                    a.members.push_back(id);
                }
                b.members.clear();
                SimEvent e;
                e.type = EventType::GroupMerge;
                e.time = ctx.now;
                e.subject = EntityId::make(EntityKind::Group, a.id);
                e.other = EntityId::make(EntityKind::Group, b.id);
                e.position = Vec3{a.centroid, 0.0f};
                ctx.events->emit(e);
            }
        }
    }
    groups_.erase(std::remove_if(groups_.begin(), groups_.end(), [](const Group& g) { return g.members.empty(); }), groups_.end());
}

void CreatureSystem::emigrate(Creature& c, WorldContext& ctx)
{
    SimEvent e;
    e.type = EventType::Emigration;
    e.time = ctx.now;
    e.subject = c.id;
    e.position = c.loco.position;
    ctx.events->emit(e);
    ++emigrants_[static_cast<size_t>(c.speciesIndex)];
    c.lod = LodLevel::Regional;
    if (Group* g = findGroup(c.social.groupId))
    {
        g->members.erase(std::remove(g->members.begin(), g->members.end(), c.id), g->members.end());
    }
    c.social.groupId = 0;
    c.behavior = BehaviorState{};
    c.perception.known.clear();
    regional_.push_back(c);
    c.alive = false; // removed from the valley simulation (not dead: kept in the regional pool)
    c.deathCause = DeathCause::Unknown;
}

void CreatureSystem::immigrate(int speciesIndex, WorldContext& ctx)
{
    const SpeciesRuntime& sp = (*species_)[static_cast<size_t>(speciesIndex)];
    const Rect2 b = bounds_;
    // Entry point on a random edge, preferring suitable habitat.
    Vec2 best = b.center();
    float bestScore = -1.0f;
    for (int i = 0; i < 12; ++i)
    {
        const int edge = rng_.rangeInt(0, 3);
        const float t = rng_.uniform();
        Vec2 p = edge == 0 ? Vec2{b.min.x + 20.0f, lerpf(b.min.y, b.max.y, t)}
                 : edge == 1 ? Vec2{b.max.x - 20.0f, lerpf(b.min.y, b.max.y, t)}
                 : edge == 2 ? Vec2{lerpf(b.min.x, b.max.x, t), b.min.y + 20.0f}
                             : Vec2{lerpf(b.min.x, b.max.x, t), b.max.y - 20.0f};
        if (ctx.water && ctx.water->depthAt(p, *ctx.terrain) > 0.3f)
        {
            continue;
        }
        const float score = sp.sim().habitat[static_cast<int>(ctx.terrain->habitatAt(p))] + 0.1f * rng_.uniform();
        if (score > bestScore)
        {
            bestScore = score;
            best = p;
        }
    }
    // Returning individuals keep their identity (scars, memories, history).
    for (size_t i = 0; i < regional_.size(); ++i)
    {
        if (regional_[i].speciesIndex == speciesIndex)
        {
            Creature c = regional_[i];
            regional_.erase(regional_.begin() + static_cast<std::ptrdiff_t>(i));
            c.alive = true;
            c.lod = LodLevel::Far;
            c.loco.position = Vec3{best, ctx.terrain->heightAt(best)};
            c.loco.speed = 0.0f;
            c.behavior.current = BehaviorId::Wander;
            c.behavior.nextDecision = ctx.now;
            c.lastUpdate = ctx.now;
            c.lastLocomotion = ctx.now;
            c.lastPerception = ctx.now;
            c.memory.homeCenter = best;
            creatures_.push_back(std::move(c));
            indexById_[creatures_.back().id.value] = static_cast<u32>(creatures_.size() - 1);
            ++immigrants_[static_cast<size_t>(speciesIndex)];
            SimEvent e;
            e.type = EventType::Immigration;
            e.time = ctx.now;
            e.subject = creatures_.back().id;
            e.position = creatures_.back().loco.position;
            e.param = 1; // returning individual
            ctx.events->emit(e);
            return;
        }
    }
    const SpeciesSim& s = sp.sim();
    const int groupSize = s.social.groupSizeMean > 1.5f ? std::max(1, rng_.poisson(s.social.groupSizeMean * 0.5f)) : 1;
    const u32 group = groupSize > 1 ? createGroup(speciesIndex) : 0u;
    for (int k = 0; k < groupSize; ++k)
    {
        const float age = rng_.range(s.growth.maturityAgeYears * 0.6f, std::max(s.growth.maturityAgeYears * 0.7f, s.growth.maxLifespanYears * 0.6f));
        Creature& c = spawn(speciesIndex, rng_.chance(0.5f) ? Sex::Female : Sex::Male, age, best + Vec2{rng_.range(-15.0f, 15.0f), rng_.range(-15.0f, 15.0f)}, ctx,
                            group);
        ++immigrants_[static_cast<size_t>(speciesIndex)];
        SimEvent e;
        e.type = EventType::Immigration;
        e.time = ctx.now;
        e.subject = c.id;
        e.position = c.loco.position;
        ctx.events->emit(e);
    }
}

void CreatureSystem::hourlyUpdate(WorldContext& ctx)
{
    // Emigration at the valley edge.
    for (Creature& c : creatures_)
    {
        if (!c.alive || c.behavior.current != BehaviorId::Emigrate)
        {
            continue;
        }
        const Vec2 p = c.pos2();
        const float edge = std::min(std::min(p.x - bounds_.min.x, bounds_.max.x - p.x), std::min(p.y - bounds_.min.y, bounds_.max.y - p.y));
        if (edge < 40.0f)
        {
            emigrate(c, ctx);
        }
    }
    // Immigration from the regional population: balancing flux towards the initial valley abundance
    // (the valley is part of a larger landscape; game assumption).
    for (const int worldIndex : worldSpecies_)
    {
        const size_t sIdx = static_cast<size_t>(worldIndex);
        const SpeciesSim& s = (*species_)[sIdx].sim();
        if (s.role != SimRole::Agent || s.population.initialCount <= 0.0f)
        {
            continue;
        }
        int alive = 0;
        for (const Creature& c : creatures_)
        {
            alive += (c.alive && c.speciesIndex == static_cast<i16>(sIdx)) ? 1 : 0;
        }
        const float target = s.population.initialCount;
        // Background exchange (~a few individuals per month) plus a flux that refills a depleted valley.
        const float deficit = std::max(0.0f, target * 0.9f - static_cast<float>(alive)) / std::max(1.0f, target);
        float lambda = 0.0002f * target + 0.06f * deficit;
        if (static_cast<float>(alive) > target * 1.3f)
        {
            lambda *= 0.1f;
        }
        if (rng_.chance(std::min(0.9f, lambda)))
        {
            immigrate(static_cast<int>(sIdx), ctx);
        }
    }
    // Remove emigrated individuals (alive=false, deathCause Unknown, LOD Regional) from the valley list.
    creatures_.erase(std::remove_if(creatures_.begin(), creatures_.end(), [](const Creature& c) { return !c.alive && c.lod == LodLevel::Regional; }),
                     creatures_.end());
    rebuildIndex();
}

void CreatureSystem::dailyUpdate(WorldContext& ctx)
{
    const double doy = ctx.clock->dayOfYear();
    const int year = ctx.clock->year();
    std::vector<int> aliveBySpecies(species_->size(), 0);
    for (const Creature& c : creatures_)
    {
        if (c.alive)
        {
            ++aliveBySpecies[static_cast<size_t>(c.speciesIndex)];
        }
    }
    for (Creature& c : creatures_)
    {
        if (!c.alive)
        {
            continue;
        }
        const SpeciesRuntime& sp = speciesOf(c);
        const SpeciesSim& s = sp.sim();
        const float age = static_cast<float>(c.ageYears(ctx.now));
        // Growth along the ontogenetic curve, stalled by poor condition.
        const float dimorph = std::max(0.3f, s.sexualDimorphismMass);
        const float sexFactor = c.sex == Sex::Female ? std::sqrt(dimorph) : 1.0f / std::sqrt(dimorph);
        const float target = massAtAge(sp, age, c.sizeGene) * sexFactor;
        if (target > c.massKg && c.phys.condition > 0.3f)
        {
            applyBodySize(c, sp, c.massKg + (target - c.massKg) * saturate((c.phys.condition - 0.3f) * 2.0f));
        }
        else if (c.phys.condition <= 0.05f)
        {
            applyBodySize(c, sp, c.massKg * 0.995f); // wasting
        }
        // Background mortality (density-dependent for young animals).
        const float initial = std::max(1.0f, s.population.initialCount);
        const float density = static_cast<float>(aliveBySpecies[static_cast<size_t>(c.speciesIndex)]) / initial;
        const float hazard = backgroundHazardPerYear(sp, age, density);
        const float pDay = 1.0f - std::exp(-hazard / 365.25f);
        if (c.rng.chance(pDay))
        {
            kill(c, age > 0.75f * s.growth.maxLifespanYears ? DeathCause::Senescence : DeathCause::Unknown, ctx);
            continue;
        }
        // Breeding status.
        const bool mature = age >= s.growth.maturityAgeYears;
        const bool window = ctx.season->inWindow(doy, s.reproduction.breedingStartDoy, s.reproduction.breedingEndDoy);
        c.repro.receptive = mature && window && c.phys.condition > 0.45f && !c.repro.gravid && c.repro.lastBreedingYear != year &&
                            (c.sex == Sex::Male || !c.repro.nest.valid() || !ctx.nests->find(c.repro.nest) || !ctx.nests->find(c.repro.nest)->active);
        // Juveniles stop following parents as they grow.
        if (c.social.followTarget.valid() && age > std::max(0.5f, s.growth.maturityAgeYears * 0.15f))
        {
            c.social.followTarget = kNoEntity;
        }
        if (s.social.territorial && mature && !c.social.territoryHolder)
        {
            c.social.territoryHolder = true;
            c.social.territoryCenter = c.memory.homeCenter;
            c.social.territoryRadius = s.social.territoryRadiusM;
        }
    }
    // Regional pool: ageing and mortality off-map.
    for (size_t i = 0; i < regional_.size();)
    {
        Creature& c = regional_[i];
        const SpeciesRuntime& sp = speciesOf(c);
        const float hazard = backgroundHazardPerYear(sp, static_cast<float>(c.ageYears(ctx.now)), 1.0f);
        if (rng_.chance(1.0f - std::exp(-hazard / 365.25f)))
        {
            regional_.erase(regional_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
}

void CreatureSystem::clearAll()
{
    creatures_.clear();
    groups_.clear();
    deaths_.clear();
    regional_.clear();
    indexById_.clear();
    index_.clear();
}

void CreatureSystem::restoreCreature(Creature c)
{
    creatures_.push_back(std::move(c));
    indexById_[creatures_.back().id.value] = static_cast<u32>(creatures_.size() - 1);
}

SpeciesCensus CreatureSystem::census(int speciesIndex, double now) const
{
    SpeciesCensus s;
    const SpeciesRuntime& sp = (*species_)[static_cast<size_t>(speciesIndex)];
    for (const Creature& c : creatures_)
    {
        if (!c.alive || c.speciesIndex != speciesIndex)
        {
            continue;
        }
        ++s.alive;
        s.biomassKg += c.massKg;
        if (c.ageYears(now) < sp.sim().growth.maturityAgeYears)
        {
            ++s.juveniles;
        }
        else
        {
            ++s.adults;
        }
        s.females += c.sex == Sex::Female ? 1 : 0;
    }
    for (const Creature& c : regional_)
    {
        s.regional += c.speciesIndex == speciesIndex ? 1 : 0;
    }
    for (const DeathRecord& d : deaths_)
    {
        if (d.speciesIndex == speciesIndex)
        {
            ++s.deaths;
            ++s.deathsByCause[static_cast<size_t>(d.cause)];
        }
    }
    for (const Creature& c : creatures_)
    {
        if (!c.alive && c.lod != LodLevel::Regional && c.speciesIndex == speciesIndex)
        {
            ++s.deaths;
            ++s.deathsByCause[static_cast<size_t>(c.deathCause)];
        }
    }
    s.births = births_[static_cast<size_t>(speciesIndex)];
    s.immigrants = immigrants_[static_cast<size_t>(speciesIndex)];
    s.emigrants = emigrants_[static_cast<size_t>(speciesIndex)];
    return s;
}

size_t CreatureSystem::aliveCount() const
{
    size_t n = 0;
    for (const Creature& c : creatures_)
    {
        n += c.alive ? 1 : 0;
    }
    return n;
}

int CreatureSystem::birthsTotal(int speciesIndex) const { return births_[static_cast<size_t>(speciesIndex)]; }
int CreatureSystem::immigrantsTotal(int speciesIndex) const { return immigrants_[static_cast<size_t>(speciesIndex)]; }
int CreatureSystem::emigrantsTotal(int speciesIndex) const { return emigrants_[static_cast<size_t>(speciesIndex)]; }
} // namespace noctis
