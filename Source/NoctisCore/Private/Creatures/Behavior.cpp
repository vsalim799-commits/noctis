// Motivation-driven decision making and behaviour execution.
//
// No behaviour is triggered by "player detected". Every choice is a utility comparison between
// motivations computed from physiology (hunger, thirst, fatigue, heat), perception (threats,
// opportunities, unidentified stimuli, alarm calls), memory (water, food, danger, habituation to
// people), social context (group spacing, offspring, mates, rivals), season and personality.
// The current behaviour carries a commitment bonus so animals do not dither.
#include "Noctis/Creatures/CreatureLogic.h"

#include <algorithm>
#include <cmath>

namespace noctis
{
namespace behaviorimpl
{
struct Context
{
    Creature& c;
    const SpeciesRuntime& sp;
    const CreatureSystem& world;
    const WorldContext& ctx;
};

float sizeScale(const Creature& c, const SpeciesRuntime& sp)
{
    // Dynamic similarity: comfortable speeds scale with sqrt(leg length).
    return std::sqrt(std::max(0.05f, c.hipHeightM / std::max(0.05f, sp.sim().hipHeightM)));
}

float walkSpeed(const Creature& c, const SpeciesRuntime& sp) { return sp.sim().locomotion.walkSpeedMs * sizeScale(c, sp); }
float maxSpeed(const Creature& c, const SpeciesRuntime& sp)
{
    return sp.sim().locomotion.maxSpeedMs * sizeScale(c, sp) * c.injuries.speedFactor * (1.0f - 0.45f * c.phys.fatigue) *
           (0.6f + 0.4f * saturate(c.phys.condition * 2.0f));
}

float reach(const Creature& c, const SpeciesRuntime& sp) { return c.lengthM * sp.headReach; }
float radius(const Creature& c, const SpeciesRuntime& sp) { return c.lengthM * sp.bodyRadius; }

bool isAdult(const Creature& c, const SpeciesRuntime& sp, double now) { return c.ageYears(now) >= sp.sim().growth.maturityAgeYears; }

const Awareness* strongestThreat(const Creature& c)
{
    const Awareness* best = nullptr;
    float bestT = 0.0f;
    for (const Awareness& a : c.perception.known)
    {
        if (a.stage < AwarenessStage::Oriented)
        {
            continue;
        }
        const float t = a.stage == AwarenessStage::Identified ? a.threat : std::max(a.threat * 0.6f, 0.3f * saturate(a.evidence));
        if (t > bestT)
        {
            bestT = t;
            best = &a;
        }
    }
    return best;
}

float effectiveThreat(const Awareness& a)
{
    return a.stage == AwarenessStage::Identified ? a.threat : std::max(a.threat * 0.6f, 0.3f * saturate(a.evidence));
}

Vec2 fleeVector(const Creature& c)
{
    Vec2 v;
    const Vec2 p = c.pos2();
    for (const Awareness& a : c.perception.known)
    {
        const float t = effectiveThreat(a);
        if (t < 0.15f || a.stage < AwarenessStage::Detected)
        {
            continue;
        }
        const Vec2 away = p - a.estimatedPos;
        const float d = away.length();
        v += away.normalized() * (t / (1.0f + d / 60.0f));
    }
    return v;
}

// Feasibility of a short step in direction dir (water depth, slope, bounds).
bool stepFeasible(const Creature& c, const SpeciesRuntime& sp, const WorldContext& ctx, const Vec2& dir, float lookahead)
{
    const Vec2 p = c.pos2();
    const Vec2 q = p + dir * lookahead;
    if (!ctx.terrain->bounds().contains(q))
    {
        return false;
    }
    const float dz = ctx.terrain->heightAt(q) - ctx.terrain->heightAt(p);
    const float slope = std::atan2(std::fabs(dz), lookahead) * kRadToDeg;
    if (slope > sp.sim().locomotion.maxSlopeDeg && dz > 0.0f)
    {
        return false;
    }
    if (ctx.water)
    {
        const float depth = ctx.water->depthAt(q, *ctx.terrain);
        const float wadeLimit = c.hipHeightM * 0.9f;
        if (depth > wadeLimit && !sp.sim().locomotion.swim && depth > c.loco.waterDepth + 0.05f)
        {
            return false;
        }
    }
    return true;
}

Vec2 steer(const Creature& c, const SpeciesRuntime& sp, const WorldContext& ctx, const Vec2& desired, float speed)
{
    if (desired.lengthSq() < 1e-6f)
    {
        return desired;
    }
    const Vec2 dir = desired.normalized();
    const float look = std::max(4.0f, std::min(40.0f, speed * 2.5f + c.lengthM));
    if (stepFeasible(c, sp, ctx, dir, look))
    {
        return dir;
    }
    const float side = (c.id.serial() & 1u) ? 1.0f : -1.0f; // consistent preference avoids oscillation
    for (const float deg : {35.0f, 70.0f, 105.0f, 140.0f})
    {
        for (const float s : {side, -side})
        {
            const Vec2 d = rotate(dir, s * deg * kDegToRad);
            if (stepFeasible(c, sp, ctx, d, look))
            {
                return d;
            }
        }
    }
    return -dir;
}

Vec2 separation(const Creature& c, const SpeciesRuntime& sp, const CreatureSystem& world)
{
    std::vector<u32> near;
    const float r = std::max(2.0f, c.lengthM * 1.2f);
    world.neighbours(c.pos2(), r * 2.0f, near);
    Vec2 push;
    for (const u32 i : near)
    {
        const Creature& o = world.creatures()[i];
        if (o.id == c.id || !o.alive)
        {
            continue;
        }
        const Vec2 d = c.pos2() - o.pos2();
        const float dist = d.length();
        const float minD = radius(c, sp) + radius(o, world.speciesOf(o)) + 0.5f;
        if (dist < minD * 1.6f && dist > 1e-3f)
        {
            push += d / dist * (1.0f - dist / (minD * 1.6f));
        }
    }
    return push;
}

void setMove(Creature& c, const Vec2& dir, float speed, Posture posture)
{
    if (dir.lengthSq() > 1e-6f)
    {
        c.behavior.desiredDir = dir.normalized();
    }
    c.behavior.desiredSpeed = std::max(0.0f, speed);
    c.behavior.posture = posture;
}

void face(Creature& c, const Vec2& target)
{
    const Vec2 d = target - c.pos2();
    if (d.lengthSq() > 1e-4f)
    {
        c.behavior.desiredDir = d.normalized();
    }
    c.behavior.desiredSpeed = 0.0f;
}

void pushIntent(Creature& c, ActionKind kind, EntityId target, const Vec2& pos, float amount, u8 param = 0)
{
    ActionIntent i;
    i.kind = kind;
    i.target = target;
    i.position = pos;
    i.amount = amount;
    i.param = param;
    c.behavior.intents.push_back(i);
}

void requestCall(Creature& c, CallContext context, double now, float minInterval)
{
    if (now - c.behavior.lastCall < minInterval)
    {
        return;
    }
    pushIntent(c, ActionKind::Call, kNoEntity, c.pos2(), 0.0f, static_cast<u8>(context));
    c.behavior.lastCall = now;
}

bool findWater(Context& k, Vec2& out)
{
    const Vec2 p = k.c.pos2();
    float best = 1e30f;
    bool found = false;
    for (const PlaceMemory& m : k.c.memory.places)
    {
        if (m.kind == MemoryKind::Water && m.strength > 0.1f)
        {
            // Danger memories near the water make it less attractive.
            float penalty = 0.0f;
            for (const PlaceMemory& d : k.c.memory.places)
            {
                if ((d.kind == MemoryKind::Danger || d.kind == MemoryKind::PredatorSighting) && distanceSq(d.position, m.position) < 150.0f * 150.0f)
                {
                    penalty += d.strength * 400.0f;
                }
            }
            const float cost = distance(p, m.position) + penalty;
            if (cost < best)
            {
                best = cost;
                out = m.position;
                found = true;
            }
        }
    }
    DrinkSite site;
    if (k.ctx.water && k.ctx.water->findDrinkSite(p, 3500.0f, site))
    {
        const float cost = distance(p, site.position);
        if (cost < best)
        {
            out = site.position;
            found = true;
        }
    }
    return found;
}

bool knownCarcass(Context& k, float radiusM, EntityId& out, Vec2& pos)
{
    std::vector<EntityId> ids;
    k.ctx.carcasses->queryNear(k.c.pos2(), radiusM, ids);
    float best = 1e30f;
    const Vec2 p = k.c.pos2();
    const Vec2 wind = k.ctx.weather->state().wind;
    const float olf = k.sp.sim().senses.olfaction;
    for (const EntityId id : ids)
    {
        const Carcass* cc = k.ctx.carcasses->find(id);
        if (!cc || cc->softTissueKg < 5.0f)
        {
            continue;
        }
        const float d = distance(p, cc->position);
        // Detectable by sight when close, by smell when downwind.
        const Vec2 rel = p - cc->position;
        const float downwind = wind.length() > 0.1f ? dot(rel.normalized(), wind.normalized()) : 0.0f;
        const float smellRange = 150.0f + 2500.0f * olf * saturate(downwind) * saturate(CarcassSystem::odourStrength(*cc) / 20.0f);
        if (d > std::max(60.0f, smellRange))
        {
            continue;
        }
        if (d < best)
        {
            best = d;
            out = id;
            pos = cc->position;
        }
    }
    return best < 1e29f;
}

Vec2 nearestEdgePoint(const Vec2& p, const Rect2& b)
{
    const float dl = p.x - b.min.x;
    const float dr = b.max.x - p.x;
    const float db = p.y - b.min.y;
    const float dt = b.max.y - p.y;
    const float m = std::min(std::min(dl, dr), std::min(db, dt));
    if (m == dl)
    {
        return {b.min.x, p.y};
    }
    if (m == dr)
    {
        return {b.max.x, p.y};
    }
    if (m == db)
    {
        return {p.x, b.min.y};
    }
    return {p.x, b.max.y};
}

float minDuration(BehaviorId b)
{
    switch (b)
    {
    case BehaviorId::Flee: return 6.0f;
    case BehaviorId::Vigilance: return 3.0f;
    case BehaviorId::Freeze: return 8.0f;
    case BehaviorId::Rest: return 60.0f;
    case BehaviorId::Sleep: return 900.0f;
    case BehaviorId::Forage: return 40.0f;
    case BehaviorId::Drink: return 30.0f;
    case BehaviorId::Hunt: return 30.0f;
    case BehaviorId::FeedCarcass: return 60.0f;
    case BehaviorId::Wander: return 45.0f;
    case BehaviorId::Display: return 6.0f;
    case BehaviorId::Defend: return 4.0f;
    default: return 15.0f;
    }
}

void emitEvent(const WorldContext& ctx, EventType t, const Creature& c, EntityId other, float magnitude, u32 param = 0)
{
    SimEvent e;
    e.type = t;
    e.time = ctx.now;
    e.subject = c.id;
    e.other = other;
    e.position = c.loco.position;
    e.magnitude = magnitude;
    e.param = param;
    ctx.events->emit(e);
}
} // namespace behaviorimpl

void decide(Creature& c, const SpeciesRuntime& sp, const CreatureSystem& world, const WorldContext& ctx)
{
    using namespace behaviorimpl;
    if (!c.alive)
    {
        return;
    }
    Context k{c, sp, world, ctx};
    const SpeciesSim& s = sp.sim();
    const double now = ctx.now;
    const PhysiologyState& ph = c.phys;
    const Personality& pers = c.personality;
    std::array<float, kBehaviorCount>& sc = c.behavior.scores;
    sc.fill(0.0f);

    const float activity = circadianActivity(sp, ctx);
    const bool adult = isAdult(c, sp, now);
    const float ageY = c.ageYears(now);
    const Awareness* threat = strongestThreat(c);
    float fear = threat ? effectiveThreat(*threat) : 0.0f;
    const float alarmRecency = saturate(1.0f - static_cast<float>(now - c.perception.lastAlarmHeard) / 40.0f);
    fear = std::max(fear, 0.55f * alarmRecency * (0.6f + 0.4f * pers.get(PersonalityAxis::Caution)));
    const float dThreat = threat ? distance(c.pos2(), threat->estimatedPos) : 1e9f;
    const Group* group = c.social.groupId ? world.findGroup(c.social.groupId) : nullptr;

    const float defendTendency = s.defense.defenseStrength * (1.0f - s.defense.fleePreference) * (0.5f + pers.get(PersonalityAxis::Boldness)) *
                                 c.injuries.defenseFactor * saturate(c.massKg / std::max(1.0f, s.adultMassKg) * 2.0f);
    const bool predator = s.predation.style != HuntStyle::None;
    const bool herbivore = s.diet.type == DietType::Herbivore || s.diet.type == DietType::Omnivore;

    // --- Threat responses: ignore -> attend -> vigilance -> warn -> flee/defend -> attack ---
    if (fear > 0.22f)
    {
        sc[static_cast<size_t>(BehaviorId::Flee)] = fear * (1.15f - 0.75f * defendTendency) * (0.8f + 0.4f * pers.get(PersonalityAxis::FlightTendency));
        const bool close = dThreat < std::max(8.0f, 2.5f * c.lengthM);
        sc[static_cast<size_t>(BehaviorId::Defend)] = fear * defendTendency * (close ? 1.4f : 0.35f);
        if (fear < 0.65f && !close)
        {
            sc[static_cast<size_t>(BehaviorId::Display)] = fear * defendTendency * 1.1f;
        }
        const bool cryptic = c.massKg < 60.0f && ctx.vegetation->cover(c.pos2()) > 0.35f;
        if (cryptic && fear < 0.6f && !close)
        {
            sc[static_cast<size_t>(BehaviorId::Freeze)] = fear * (0.6f + pers.get(PersonalityAxis::Caution));
        }
    }
    if (group && group->formation == Formation::Fleeing)
    {
        sc[static_cast<size_t>(BehaviorId::Flee)] = std::max(sc[static_cast<size_t>(BehaviorId::Flee)], 0.55f + 0.3f * pers.get(PersonalityAxis::Sociability));
    }
    // Offspring nearby under threat.
    if (fear > 0.2f && s.reproduction.parentalCare > 0.1f)
    {
        for (const Awareness& a : c.perception.known)
        {
            if (a.relation == Relation::Offspring && distance(a.estimatedPos, c.pos2()) < 60.0f)
            {
                sc[static_cast<size_t>(BehaviorId::DefendYoung)] = fear * s.reproduction.parentalCare * 1.5f;
                break;
            }
        }
    }
    // Unidentified stimuli and alarm calls -> vigilance.
    float unknownStim = 0.0f;
    for (const Awareness& a : c.perception.known)
    {
        if (a.stage >= AwarenessStage::Detected && a.stage < AwarenessStage::Identified)
        {
            unknownStim = std::max(unknownStim, saturate(a.evidence));
        }
    }
    sc[static_cast<size_t>(BehaviorId::Vigilance)] = (0.7f * c.perception.alertness + 0.55f * unknownStim + 0.5f * alarmRecency) *
                                                     (0.6f + pers.get(PersonalityAxis::Vigilance)) * (fear < 0.7f ? 1.0f : 0.3f);
    // Routine scanning: smaller groups scan more ("many eyes" effect in extant herds).
    if (herbivore && fear < 0.2f)
    {
        const float n = group ? static_cast<float>(group->members.size()) : 1.0f;
        if (c.rng.chance(0.25f))
        {
            sc[static_cast<size_t>(BehaviorId::Vigilance)] = std::max(sc[static_cast<size_t>(BehaviorId::Vigilance)], 0.12f + 0.25f / std::sqrt(n));
        }
    }

    // Curiosity: approach a novel, low-threat stimulus (e.g. a quiet researcher at a distance).
    for (const Awareness& a : c.perception.known)
    {
        if (a.stage >= AwarenessStage::Oriented && effectiveThreat(a) < 0.3f &&
            (a.relation == Relation::Researcher || a.relation == Relation::Vehicle || a.relation == Relation::Drone || a.stage < AwarenessStage::Identified))
        {
            sc[static_cast<size_t>(BehaviorId::Investigate)] =
                std::max(sc[static_cast<size_t>(BehaviorId::Investigate)], pers.get(PersonalityAxis::Curiosity) * 0.6f * (1.0f - fear) * activity);
        }
    }

    // --- Maintenance ---
    // Survival urgency: severe thirst or hunger overrides the diel rhythm (an animal wakes up to drink).
    const float thirstUrgency = smoothstep(0.6f, 0.95f, ph.thirst);
    const float hungerUrgency = smoothstep(0.75f, 1.0f, ph.hunger) * (ph.condition < 0.15f ? 1.0f : 0.5f);
    sc[static_cast<size_t>(BehaviorId::Drink)] = ph.thirst * 1.15f * (0.5f + 0.5f * activity) + 1.2f * thirstUrgency;
    if (herbivore)
    {
        sc[static_cast<size_t>(BehaviorId::Forage)] = ph.hunger * (0.35f + 0.65f * activity) * c.injuries.feedingFactor + 0.8f * hungerUrgency;
    }
    EntityId carcassId;
    Vec2 carcassPos;
    const bool carnivorous = s.diet.type == DietType::Carnivore || s.diet.type == DietType::Omnivore || s.diet.type == DietType::Piscivore;
    if (carnivorous && knownCarcass(k, 3000.0f, carcassId, carcassPos))
    {
        const float d = distance(c.pos2(), carcassPos);
        const float scav = s.diet.scavenges ? 1.0f : 0.5f;
        if (d < 60.0f)
        {
            sc[static_cast<size_t>(BehaviorId::FeedCarcass)] = ph.hunger * 1.35f * scav + 0.1f;
        }
        else
        {
            sc[static_cast<size_t>(BehaviorId::Scavenge)] = ph.hunger * 0.95f * scav;
        }
    }
    if (predator && ageY > 0.3f)
    {
        bool preyKnown = false;
        for (const Awareness& a : c.perception.known)
        {
            if ((a.relation == Relation::Prey || (a.relation == Relation::Researcher && a.opportunity > 0.4f)) && a.opportunity > 0.15f)
            {
                preyKnown = true;
                break;
            }
        }
        sc[static_cast<size_t>(BehaviorId::Hunt)] = ph.hunger * (0.35f + 0.65f * activity) * (0.6f + 0.6f * pers.get(PersonalityAxis::Aggression)) *
                                                    c.injuries.speedFactor * (1.0f - ph.fatigue) * (preyKnown ? 1.2f : 0.7f) +
                                                0.8f * hungerUrgency;
    }
    sc[static_cast<size_t>(BehaviorId::Rest)] = (0.8f * ph.fatigue + 0.45f * (1.0f - activity) + 0.3f * c.phys.pain) * (1.0f - fear) + 0.08f;
    sc[static_cast<size_t>(BehaviorId::Sleep)] = ph.sleepPressure * (1.0f - activity) * 1.25f * (1.0f - c.perception.alertness);
    sc[static_cast<size_t>(BehaviorId::Thermoregulate)] = std::max(0.0f, ph.heatStress) * 1.2f;
    sc[static_cast<size_t>(BehaviorId::Wander)] = 0.1f + 0.15f * pers.get(PersonalityAxis::Exploration) * activity;

    // --- Social ---
    if (group && group->members.size() > 1)
    {
        const float spacing = std::max(3.0f, c.lengthM * (1.5f + 2.5f * (1.0f - group->alert)));
        const float allowed = spacing * std::sqrt(static_cast<float>(group->members.size())) * 1.3f;
        const float d = distance(c.pos2(), group->centroid);
        sc[static_cast<size_t>(BehaviorId::FollowGroup)] = saturate((d - allowed) / (allowed * 2.0f)) * (0.6f + pers.get(PersonalityAxis::Sociability)) * 1.3f +
                                                           (group->velocity.length() > 0.4f ? 0.25f : 0.05f);
        sc[static_cast<size_t>(BehaviorId::Socialize)] = 0.08f * pers.get(PersonalityAxis::Sociability) * activity;
    }
    if (c.social.followTarget.valid() && ageY < std::max(0.5f, s.growth.maturityAgeYears * 0.15f))
    {
        const Creature* parent = world.find(c.social.followTarget);
        if (parent && parent->alive)
        {
            const float d = distance(c.pos2(), parent->pos2());
            sc[static_cast<size_t>(BehaviorId::FollowGroup)] = std::max(sc[static_cast<size_t>(BehaviorId::FollowGroup)], saturate((d - 4.0f) / 20.0f) * 1.2f + 0.2f);
        }
    }
    if (s.social.territorial && adult)
    {
        sc[static_cast<size_t>(BehaviorId::Patrol)] = 0.16f * activity * (0.5f + pers.get(PersonalityAxis::Dominance));
    }
    // --- Reproduction and parental care ---
    if (c.repro.receptive && adult)
    {
        sc[static_cast<size_t>(BehaviorId::Court)] = 0.45f * saturate(c.phys.condition * 1.5f) * activity;
    }
    if (c.repro.gravid || c.repro.nest.valid())
    {
        const Nest* nest = c.repro.nest.valid() ? ctx.nests->find(c.repro.nest) : nullptr;
        const bool activeNest = nest && nest->active;
        if (c.repro.gravid || activeNest)
        {
            const float neglect = activeNest ? (1.0f - nest->attendance) : 1.0f;
            sc[static_cast<size_t>(BehaviorId::NestTend)] = (c.repro.gravid ? 0.6f : s.reproduction.parentalCare * 0.75f) * (0.6f + 0.6f * neglect);
        }
    }
    if (s.reproduction.parentalCare > 0.2f && adult)
    {
        for (const Awareness& a : c.perception.known)
        {
            if (a.relation == Relation::Offspring)
            {
                const float d = distance(a.estimatedPos, c.pos2());
                sc[static_cast<size_t>(BehaviorId::CareYoung)] = std::max(sc[static_cast<size_t>(BehaviorId::CareYoung)],
                                                                          s.reproduction.parentalCare * 0.4f * saturate(d / 30.0f) + 0.05f);
            }
        }
    }
    // --- Leaving the valley (resource scarcity) ---
    if (ph.hunger > 0.75f && ph.condition < 0.3f && ageY > 1.0f)
    {
        sc[static_cast<size_t>(BehaviorId::Emigrate)] = 0.4f * (1.0f - ph.condition) * pers.get(PersonalityAxis::Exploration);
    }

    // Commitment / hysteresis.
    const BehaviorId cur = c.behavior.current;
    if (now < c.behavior.commitUntil)
    {
        sc[static_cast<size_t>(cur)] += 0.15f;
    }
    else
    {
        sc[static_cast<size_t>(cur)] += 0.05f;
    }
    // Emergencies interrupt everything else.
    BehaviorId best = BehaviorId::Rest;
    float bestScore = -1.0f;
    for (int i = 0; i < kBehaviorCount; ++i)
    {
        if (sc[static_cast<size_t>(i)] > bestScore)
        {
            bestScore = sc[static_cast<size_t>(i)];
            best = static_cast<BehaviorId>(i);
        }
    }
    if (best != cur)
    {
        c.behavior.previous = cur;
        c.behavior.current = best;
        c.behavior.startedAt = now;
        c.behavior.commitUntil = now + minDuration(best) * (0.75f + 0.5f * c.rng.uniform());
        c.behavior.hasTargetPos = false;
        if (best != BehaviorId::Hunt && cur == BehaviorId::Hunt &&
            (c.behavior.hunt == HuntPhase::Stalk || c.behavior.hunt == HuntPhase::Chase || c.behavior.hunt == HuntPhase::Attack || c.behavior.hunt == HuntPhase::Approach))
        {
            emitEvent(ctx, EventType::HuntAbandon, c, c.behavior.target, c.behavior.chaseSeconds);
        }
        if (best != BehaviorId::Hunt)
        {
            c.behavior.hunt = HuntPhase::None;
        }
        if (best == BehaviorId::Flee)
        {
            ++c.history.timesFled;
            emitEvent(ctx, EventType::Flee, c, threat ? threat->entity : kNoEntity, fear);
            const EntityKind tk = threat ? threat->entity.kind() : EntityKind::None;
            if (tk == EntityKind::Researcher)
            {
                emitEvent(ctx, EventType::ResearcherNoticed, c, threat->entity, fear, 1);
            }
            else if (tk == EntityKind::Drone)
            {
                emitEvent(ctx, EventType::DroneNoticed, c, threat->entity, fear, 1);
            }
            else if (tk == EntityKind::Vehicle)
            {
                emitEvent(ctx, EventType::VehicleNoticed, c, threat->entity, fear, 1);
            }
        }
        else if (best == BehaviorId::Vigilance && (c.perception.alertness > 0.35f || alarmRecency > 0.0f))
        {
            // Routine scanning is not an event; a vigilance response to a stimulus is.
            emitEvent(ctx, EventType::VigilanceRaised, c, threat ? threat->entity : c.perception.alarmSource, c.perception.alertness);
        }
        else if (best == BehaviorId::Display && threat)
        {
            emitEvent(ctx, threat->entity.kind() == EntityKind::Researcher ? EventType::ResearcherWarned : EventType::Display, c, threat->entity, fear);
        }
        if (best == BehaviorId::Hunt)
        {
            c.behavior.hunt = HuntPhase::Search;
            c.behavior.huntPhaseStart = now;
            c.behavior.target = kNoEntity;
            c.behavior.chaseSeconds = 0.0f;
            c.behavior.bestGap = 1e9f;
            emitEvent(ctx, EventType::HuntSearch, c, kNoEntity, ph.hunger);
        }
        if (best == BehaviorId::FeedCarcass || best == BehaviorId::Scavenge)
        {
            c.behavior.target = carcassId;
            c.behavior.targetPos = carcassPos;
            c.behavior.hasTargetPos = true;
        }
    }
    c.behavior.urgency = std::max(fear, ph.thirst > 0.8f ? ph.thirst : 0.0f);
}

namespace behaviorimpl
{
void executeHunt(Context& k, float dt)
{
    Creature& c = k.c;
    const SpeciesSim& s = k.sp.sim();
    const double now = k.ctx.now;
    BehaviorState& b = c.behavior;
    const Vec2 p = c.pos2();
    const float vmax = maxSpeed(c, k.sp);
    const float walk = walkSpeed(c, k.sp);

    // Target (re)selection among perceived prey — the predator never "knows" where prey is unless perceived.
    const Awareness* target = b.target.valid() ? c.perception.find(b.target) : nullptr;
    if (!target || target->stage < AwarenessStage::Detected)
    {
        if (b.hunt == HuntPhase::Chase || b.hunt == HuntPhase::Attack)
        {
            emitEvent(k.ctx, EventType::HuntAbandon, c, b.target, b.chaseSeconds, 1);
            ++c.history.huntsAttempted;
            rememberPlace(c, MemoryKind::Food, p, 0.3f, now);
        }
        target = nullptr;
        b.target = kNoEntity;
        float best = 0.0f;
        for (const Awareness& a : c.perception.known)
        {
            const bool preyLike = a.relation == Relation::Prey || (a.relation == Relation::Researcher && a.opportunity > 0.4f);
            if (!preyLike || a.stage < AwarenessStage::Oriented)
            {
                continue;
            }
            const float d = distance(p, a.estimatedPos);
            const float u = a.opportunity / (1.0f + d / 250.0f) * (0.7f + 0.6f * c.personality.get(PersonalityAxis::RiskTolerance));
            if (u > best)
            {
                best = u;
                target = &a;
            }
        }
        if (target && best > 0.08f)
        {
            b.target = target->entity;
            b.hunt = s.predation.style == HuntStyle::Pursuit ? HuntPhase::Approach : HuntPhase::Stalk;
            b.huntPhaseStart = now;
            b.chaseSeconds = 0.0f;
            b.bestGap = 1e9f;
            emitEvent(k.ctx, EventType::HuntTargetSelected, c, b.target, best);
        }
        else
        {
            target = nullptr;
            b.hunt = HuntPhase::Search;
        }
    }

    if (!target)
    {
        // Population-level prey: fishing in shallow water, snapping up small vertebrates and invertebrates.
        if (k.ctx.harvester)
        {
            const bool fisher = s.diet.eatsFish && (c.loco.waterDepth > 0.15f || (k.ctx.water && k.ctx.water->depthAt(p + Vec2::fromHeading(c.loco.heading) * (c.lengthM * 0.5f), *k.ctx.terrain) > 0.2f));
            const float rate = c.phys.gutCapacityKg / (5.0f * 3600.0f);
            if (fisher && k.ctx.harvester->availability(PopulationResource::Fish, p) > 0.05f)
            {
                setMove(c, c.behavior.desiredDir, walk * 0.15f, Posture::Crouch);
                pushIntent(c, ActionKind::EatPopulation, kNoEntity, p, rate * dt, static_cast<u8>(PopulationResource::Fish));
                return;
            }
            if (c.massKg < 150.0f && c.rng.chance(0.3f) && k.ctx.harvester->availability(PopulationResource::SmallVertebrates, p) > 0.05f)
            {
                pushIntent(c, ActionKind::EatPopulation, kNoEntity, p, rate * dt * 0.5f, static_cast<u8>(PopulationResource::SmallVertebrates));
            }
        }
        // Search: move through the range; prefer remembered prey areas and water sources (where prey gathers).
        if (!b.hasTargetPos || distance(p, b.targetPos) < 15.0f)
        {
            Vec2 goal = p + Vec2::fromHeading(c.rng.range(0.0f, kTwoPi)) * c.rng.range(150.0f, 450.0f);
            float bestScore = -1.0f;
            for (const PlaceMemory& m : c.memory.places)
            {
                if ((m.kind == MemoryKind::Food || m.kind == MemoryKind::Water) && m.strength > 0.15f)
                {
                    const float score = m.strength / (1.0f + distance(p, m.position) / 800.0f) * c.rng.range(0.5f, 1.0f);
                    if (score > bestScore)
                    {
                        bestScore = score;
                        goal = m.position;
                    }
                }
            }
            b.targetPos = k.ctx.terrain->clampToBounds(goal, 50.0f);
            b.hasTargetPos = true;
        }
        setMove(c, steer(c, k.sp, k.ctx, b.targetPos - p, walk), walk * 0.9f, Posture::Normal);
        return;
    }

    const Vec2 tp = target->estimatedPos;
    const float d = distance(p, tp);
    const Creature* prey = target->entity.kind() == EntityKind::Creature ? k.world.find(target->entity) : nullptr;
    const float preyRadius = prey ? radius(*prey, k.world.speciesOf(*prey)) : 0.4f;
    const float strike = reach(c, k.sp) + preyRadius + 0.6f;
    const float rushDistance = std::max(12.0f, vmax * 2.5f);
    const Vec2 preyVel = target->estimatedVel;
    const bool preyFleeing = preyVel.length() > walk * 1.4f && dot(preyVel, (tp - p).normalized()) > 0.0f;

    switch (b.hunt)
    {
    case HuntPhase::Stalk:
    {
        // Slow approach using cover; drift to stay downwind of the prey (scent carries downwind).
        Vec2 dir = (tp - p).normalized();
        const Vec2 wind = k.ctx.weather->state().wind;
        if (wind.length() > 0.5f)
        {
            const Vec2 fromPrey = (p - tp).normalized();
            const float downwindness = dot(fromPrey, wind.normalized());
            if (downwindness < 0.3f)
            {
                dir = (dir + wind.normalized().perp() * (cross(wind, fromPrey) > 0.0f ? 0.5f : -0.5f)).normalized();
            }
        }
        if (s.predation.style == HuntStyle::Ambush)
        {
            // Ambush: hold still near water/cover until prey is within striking distance.
            face(c, tp);
            b.posture = Posture::Crouch;
            if (d < strike + 2.5f)
            {
                b.hunt = HuntPhase::Attack;
                b.huntPhaseStart = now;
                emitEvent(k.ctx, EventType::HuntChase, c, b.target, d);
            }
            else if (d > 60.0f)
            {
                setMove(c, steer(c, k.sp, k.ctx, dir, walk * 0.3f), walk * 0.3f, Posture::Crouch);
            }
            break;
        }
        setMove(c, steer(c, k.sp, k.ctx, dir, walk * 0.45f), walk * 0.45f, Posture::Crouch);
        if (now - b.huntPhaseStart < 1.0)
        {
            emitEvent(k.ctx, EventType::HuntStalk, c, b.target, d);
        }
        if (d < rushDistance || (preyFleeing && d < rushDistance * 3.0f))
        {
            b.hunt = HuntPhase::Chase;
            b.huntPhaseStart = now;
            b.chaseSeconds = 0.0f;
            b.bestGap = d;
            emitEvent(k.ctx, EventType::HuntChase, c, b.target, d);
        }
        else if (preyFleeing)
        {
            b.hunt = HuntPhase::Abandon;
        }
        break;
    }
    case HuntPhase::Approach:
    {
        setMove(c, steer(c, k.sp, k.ctx, tp - p, walk * 1.4f), walk * 1.4f, Posture::Normal);
        if (d < rushDistance * 2.5f || preyFleeing)
        {
            b.hunt = HuntPhase::Chase;
            b.huntPhaseStart = now;
            b.chaseSeconds = 0.0f;
            b.bestGap = d;
            emitEvent(k.ctx, EventType::HuntChase, c, b.target, d);
        }
        break;
    }
    case HuntPhase::Chase:
    {
        b.chaseSeconds += dt;
        // Pure pursuit with lead: aim at the predicted interception point.
        const float tIntercept = d / std::max(1.0f, vmax);
        const Vec2 aim = tp + preyVel * std::min(3.0f, tIntercept);
        setMove(c, steer(c, k.sp, k.ctx, aim - p, vmax), vmax, Posture::Normal);
        if (d < b.bestGap - 0.5f)
        {
            b.bestGap = d;
            b.huntPhaseStart = now;
        }
        const bool losing = now - b.huntPhaseStart > 4.0 && d > b.bestGap + 3.0f;
        const float maxChase = std::max(5.0f, s.predation.maxChaseS) * (0.8f + 0.4f * c.personality.get(PersonalityAxis::Aggression));
        if (d < strike)
        {
            b.hunt = HuntPhase::Attack;
            b.huntPhaseStart = now;
        }
        else if (losing || b.chaseSeconds > maxChase || c.phys.fatigue > 0.85f)
        {
            b.hunt = HuntPhase::Abandon;
        }
        break;
    }
    case HuntPhase::Attack:
    {
        const float speed = std::min(vmax, preyVel.length() + 1.0f);
        setMove(c, (tp - p), speed, Posture::Normal);
        if (d < strike + 0.5f)
        {
            // The serial phase rate-limits strikes (attack cooldown), so asking every tick is safe.
            pushIntent(c, ActionKind::Attack, b.target, tp, 1.0f);
        }
        else if (d > strike * 2.5f)
        {
            b.hunt = HuntPhase::Chase;
            b.huntPhaseStart = now;
        }
        break;
    }
    case HuntPhase::Abandon:
    {
        emitEvent(k.ctx, EventType::HuntAbandon, c, b.target, b.chaseSeconds);
        ++c.history.huntsAttempted;
        rememberPlace(c, MemoryKind::Food, tp, 0.3f, now); // prey was here
        b.target = kNoEntity;
        b.hunt = HuntPhase::Search;
        b.commitUntil = now; // allow re-evaluation (rest after a failed chase is likely: fatigue)
        setMove(c, c.behavior.desiredDir, 0.0f, Posture::Normal);
        break;
    }
    default:
        b.hunt = HuntPhase::Search;
        break;
    }
}
} // namespace behaviorimpl

void executeBehavior(Creature& c, const SpeciesRuntime& sp, const CreatureSystem& world, const WorldContext& ctx, float dtSince)
{
    using namespace behaviorimpl;
    if (!c.alive)
    {
        return;
    }
    Context k{c, sp, world, ctx};
    const SpeciesSim& s = sp.sim();
    const double now = ctx.now;
    const Vec2 p = c.pos2();
    const float walk = walkSpeed(c, sp);
    const float vmax = maxSpeed(c, sp);
    const float dt = std::max(0.05f, dtSince);
    BehaviorState& b = c.behavior;
    const Group* group = c.social.groupId ? world.findGroup(c.social.groupId) : nullptr;
    const float sizeRatio = c.hipHeightM / std::max(0.05f, s.hipHeightM);

    auto cohesionBias = [&](Vec2 dir) {
        if (!group || group->members.size() < 2)
        {
            return dir;
        }
        const float spacing = std::max(3.0f, c.lengthM * (1.5f + 2.5f * (1.0f - group->alert)));
        const float allowed = spacing * std::sqrt(static_cast<float>(group->members.size())) * 1.3f;
        const Vec2 toC = group->centroid - p;
        const float d = toC.length();
        if (d > allowed)
        {
            dir = (dir.normalized() + toC.normalized() * saturate((d - allowed) / allowed) * s.social.cohesion * 2.0f).normalized();
        }
        return dir;
    };

    switch (b.current)
    {
    case BehaviorId::Rest:
        setMove(c, b.desiredDir, 0.0f, now - b.startedAt > 120.0 ? Posture::Lying : Posture::Normal);
        break;
    case BehaviorId::Sleep:
        setMove(c, b.desiredDir, 0.0f, Posture::Lying);
        break;
    case BehaviorId::Wander:
    {
        if (!b.hasTargetPos || distance(p, b.targetPos) < 8.0f)
        {
            Vec2 best = p;
            float bestScore = -1.0f;
            for (int i = 0; i < 6; ++i)
            {
                const Vec2 cand = ctx.terrain->clampToBounds(p + Vec2::fromHeading(c.rng.range(0.0f, kTwoPi)) * c.rng.range(40.0f, 220.0f), 30.0f);
                float score = s.habitat[static_cast<int>(ctx.terrain->habitatAt(cand))] + 0.1f * c.rng.uniform();
                for (const PlaceMemory& m : c.memory.places)
                {
                    if (m.valence < 0.0f && distanceSq(m.position, cand) < 200.0f * 200.0f)
                    {
                        score -= m.strength * 0.8f;
                    }
                }
                score -= 0.3f * saturate((distance(cand, c.memory.homeCenter) - c.memory.homeRadius) / c.memory.homeRadius);
                if (score > bestScore)
                {
                    bestScore = score;
                    best = cand;
                }
            }
            b.targetPos = best;
            b.hasTargetPos = true;
        }
        const Vec2 dir = cohesionBias(b.targetPos - p) + separation(c, sp, world) * 0.5f;
        setMove(c, steer(c, sp, ctx, dir, walk), walk * 0.8f, Posture::Normal);
        break;
    }
    case BehaviorId::Forage:
    {
        const float reachMin = s.diet.browseMinM * sizeRatio;
        const float reachMax = std::max(0.2f, s.diet.browseMaxM * sizeRatio);
        if (!b.hasTargetPos || c.rng.chance(dt / 45.0f) ||
            ctx.vegetation->availableForage(b.targetPos, reachMin, reachMax, s.diet.plantPreference) < 0.5f)
        {
            Vec2 spot;
            float value = 0.0f;
            const Vec2 center = group ? lerp(p, group->centroid, 0.3f) : p;
            if (ctx.vegetation->findForage(center, 70.0f, reachMin, reachMax, s.diet.plantPreference, c.seed ^ static_cast<u64>(now * 10.0), spot, value))
            {
                b.targetPos = spot;
                b.hasTargetPos = true;
            }
        }
        if (b.hasTargetPos && distance(p, b.targetPos) > std::max(2.5f, c.lengthM * 0.4f))
        {
            const Vec2 dir = cohesionBias(b.targetPos - p) + separation(c, sp, world) * 0.5f;
            setMove(c, steer(c, sp, ctx, dir, walk * 0.6f), walk * 0.6f, Posture::Normal);
        }
        else
        {
            // Intake rate fills the gut in ~6 h of feeding (game assumption), reduced by jaw/neck injuries.
            const float rate = c.phys.gutCapacityKg / (6.0f * 3600.0f) * c.injuries.feedingFactor;
            setMove(c, b.desiredDir, 0.0f, Posture::HeadDown);
            pushIntent(c, ActionKind::EatPlants, kNoEntity, p, rate * dt);
            if (s.diet.eatsInvertebrates)
            {
                pushIntent(c, ActionKind::EatPopulation, kNoEntity, p, rate * dt * 0.3f, static_cast<u8>(PopulationResource::Invertebrates));
            }
        }
        break;
    }
    case BehaviorId::Drink:
    {
        if (!b.hasTargetPos)
        {
            Vec2 w;
            if (findWater(k, w))
            {
                b.targetPos = w;
                b.hasTargetPos = true;
            }
        }
        if (!b.hasTargetPos)
        {
            setMove(c, b.desiredDir, walk, Posture::Normal);
            break;
        }
        const float d = distance(p, b.targetPos);
        if (d > std::max(2.0f, c.lengthM * 0.35f))
        {
            setMove(c, steer(c, sp, ctx, cohesionBias(b.targetPos - p), walk), walk * (d > 300.0f ? 1.2f : 0.8f), Posture::Normal);
        }
        else
        {
            setMove(c, b.desiredDir, 0.0f, Posture::HeadDown);
            const float litresPerS = 0.0003f * c.massKg + 0.002f;
            pushIntent(c, ActionKind::Drink, kNoEntity, b.targetPos, litresPerS * dt);
        }
        break;
    }
    case BehaviorId::Travel:
        setMove(c, steer(c, sp, ctx, b.targetPos - p, walk), walk, Posture::Normal);
        break;
    case BehaviorId::FollowGroup:
    {
        Vec2 target = p;
        float speed = walk;
        const Creature* parent = c.social.followTarget.valid() ? world.find(c.social.followTarget) : nullptr;
        if (parent && parent->alive && c.ageYears(now) < std::max(0.5f, s.growth.maturityAgeYears * 0.15f))
        {
            target = parent->pos2() + rotate(Vec2{-2.0f - parent->lengthM * 0.3f, 1.5f}, parent->loco.heading);
            speed = std::max(walk, parent->loco.speed * 1.2f);
        }
        else if (group)
        {
            const float angle = static_cast<float>(c.seed % 628) / 100.0f;
            const float spacing = std::max(3.0f, c.lengthM * (1.5f + 2.5f * (1.0f - group->alert)));
            const float ring = spacing * std::sqrt(static_cast<float>(group->members.size())) * 0.5f;
            target = group->centroid + Vec2::fromHeading(angle) * ring * (0.4f + 0.6f * static_cast<float>((c.seed >> 8) % 100) / 100.0f);
            if (group->hasDestination)
            {
                target += (group->destination - group->centroid).normalized() * spacing * 2.0f;
            }
            speed = std::max(walk * 0.8f, group->velocity.length() * 1.15f);
        }
        const float d = distance(p, target);
        if (d < 2.0f)
        {
            setMove(c, b.desiredDir, 0.0f, Posture::Normal);
        }
        else
        {
            const Vec2 dir = (target - p) + separation(c, sp, world) * 2.0f;
            setMove(c, steer(c, sp, ctx, dir, speed), std::min(vmax, speed * (d > 40.0f ? 1.5f : 1.0f)), Posture::Normal);
        }
        break;
    }
    case BehaviorId::Vigilance:
    {
        b.posture = Posture::HeadUp;
        if (c.perception.hasLookTarget)
        {
            face(c, c.perception.lookTarget);
        }
        else
        {
            c.behavior.desiredSpeed = 0.0f;
        }
        // Social species with alarm calls (speculative reconstruction toggle) call on strong unidentified threats.
        if (s.social.alarmCalls && ctx.toggle("alarm_calls") && c.perception.alertness > 0.55f)
        {
            requestCall(c, CallContext::Alarm, now, 20.0f);
        }
        break;
    }
    case BehaviorId::Flee:
    {
        Vec2 away = fleeVector(c);
        if (away.lengthSq() < 1e-4f)
        {
            away = c.perception.alarmDirection.lengthSq() > 0.0f ? -c.perception.alarmDirection : b.desiredDir;
        }
        if (group && group->formation == Formation::Fleeing && group->velocity.length() > 0.5f)
        {
            away = away.normalized() + group->velocity.normalized() * (0.5f + s.social.cohesion);
        }
        const Vec2 dir = steer(c, sp, ctx, away + separation(c, sp, world) * 1.5f, vmax);
        setMove(c, dir, vmax * (0.65f + 0.35f * saturate(b.urgency)), Posture::Normal);
        if (s.social.alarmCalls && ctx.toggle("alarm_calls") && now - b.startedAt < 2.0)
        {
            requestCall(c, CallContext::Alarm, now, 15.0f);
        }
        if (c.injuries.healthIndex < 0.6f && now - b.startedAt < 2.0)
        {
            requestCall(c, CallContext::Distress, now, 10.0f);
        }
        break;
    }
    case BehaviorId::Freeze:
        setMove(c, b.desiredDir, 0.0f, Posture::Crouch);
        break;
    case BehaviorId::Defend:
    case BehaviorId::DefendYoung:
    {
        const Awareness* t = strongestThreat(c);
        if (!t)
        {
            setMove(c, b.desiredDir, 0.0f, Posture::HeadUp);
            break;
        }
        Vec2 stand = p;
        if (b.current == BehaviorId::DefendYoung)
        {
            for (const Awareness& a : c.perception.known)
            {
                if (a.relation == Relation::Offspring)
                {
                    stand = a.estimatedPos + (t->estimatedPos - a.estimatedPos).normalized() * std::max(4.0f, c.lengthM * 0.6f);
                    break;
                }
            }
        }
        const float d = distance(p, t->estimatedPos);
        if (distance(p, stand) > 3.0f)
        {
            setMove(c, steer(c, sp, ctx, stand - p, vmax * 0.7f), vmax * 0.7f, Posture::Display);
        }
        else
        {
            face(c, t->estimatedPos);
            b.posture = Posture::Display;
        }
        const Creature* other = t->entity.kind() == EntityKind::Creature ? world.find(t->entity) : nullptr;
        const float otherR = other ? radius(*other, world.speciesOf(*other)) : 0.4f;
        if (d < reach(c, sp) + otherR + 1.0f)
        {
            pushIntent(c, ActionKind::Attack, t->entity, t->estimatedPos, 0.8f);
        }
        requestCall(c, CallContext::Threat, now, 12.0f);
        break;
    }
    case BehaviorId::Display:
    {
        const Awareness* t = strongestThreat(c);
        if (t)
        {
            face(c, t->estimatedPos);
            // Occasional bluff charge by bold individuals.
            if (c.personality.get(PersonalityAxis::Boldness) > 0.6f && c.rng.chance(dt * 0.08f))
            {
                setMove(c, t->estimatedPos - p, vmax * 0.6f, Posture::Display);
            }
        }
        b.posture = Posture::Display;
        pushIntent(c, ActionKind::Display, t ? t->entity : kNoEntity, p, 1.0f);
        requestCall(c, CallContext::Threat, now, 15.0f);
        break;
    }
    case BehaviorId::Investigate:
    {
        const Awareness* best = nullptr;
        for (const Awareness& a : c.perception.known)
        {
            if (a.stage >= AwarenessStage::Oriented && effectiveThreat(a) < 0.3f && (!best || a.evidence > best->evidence))
            {
                best = &a;
            }
        }
        if (!best)
        {
            setMove(c, b.desiredDir, 0.0f, Posture::HeadUp);
            break;
        }
        const float keep = flightInitiationDistance(c, sp);
        const float d = distance(p, best->estimatedPos);
        if (d > keep * 1.2f)
        {
            setMove(c, steer(c, sp, ctx, best->estimatedPos - p, walk * 0.6f), walk * 0.6f, Posture::HeadUp);
        }
        else
        {
            face(c, best->estimatedPos);
            b.posture = Posture::HeadUp;
        }
        break;
    }
    case BehaviorId::Patrol:
    {
        if (!b.hasTargetPos || distance(p, b.targetPos) < 20.0f)
        {
            const Vec2 center = c.social.territoryRadius > 0.0f ? c.social.territoryCenter : c.memory.homeCenter;
            const float r = c.social.territoryRadius > 0.0f ? c.social.territoryRadius : c.memory.homeRadius;
            b.targetPos = ctx.terrain->clampToBounds(center + Vec2::fromHeading(c.rng.range(0.0f, kTwoPi)) * r * c.rng.range(0.4f, 0.8f), 40.0f);
            b.hasTargetPos = true;
        }
        setMove(c, steer(c, sp, ctx, b.targetPos - p, walk), walk, Posture::Normal);
        if (c.rng.chance(dt / 600.0f))
        {
            requestCall(c, CallContext::Territorial, now, 300.0f);
        }
        break;
    }
    case BehaviorId::Hunt:
        executeHunt(k, dt);
        break;
    case BehaviorId::FeedCarcass:
    case BehaviorId::Scavenge:
    {
        const Carcass* cc = b.target.kind() == EntityKind::Carcass ? ctx.carcasses->find(b.target) : nullptr;
        if (!cc || cc->softTissueKg < 1.0f)
        {
            b.commitUntil = now;
            setMove(c, b.desiredDir, 0.0f, Posture::Normal);
            break;
        }
        const float d = distance(p, cc->position);
        const float feedDist = c.lengthM * 0.5f + 2.0f;
        if (d > feedDist)
        {
            const float speed = b.current == BehaviorId::Scavenge ? walk * 1.1f : walk * 0.8f;
            setMove(c, steer(c, sp, ctx, cc->position - p, speed), speed, Posture::Normal);
        }
        else
        {
            face(c, cc->position);
            b.posture = Posture::HeadDown;
            const float kgPerS = 0.00005f * std::pow(c.massKg, 0.9f) * c.injuries.feedingFactor;
            pushIntent(c, ActionKind::EatCarcass, cc->id, cc->position, kgPerS * dt);
        }
        break;
    }
    case BehaviorId::Thermoregulate:
    {
        if (!b.hasTargetPos)
        {
            Vec2 best = p;
            float bestScore = ctx.vegetation->canopyCover(p);
            for (int i = 0; i < 10; ++i)
            {
                const Vec2 cand = ctx.terrain->clampToBounds(p + Vec2::fromHeading(c.rng.range(0.0f, kTwoPi)) * c.rng.range(20.0f, 180.0f), 30.0f);
                float score = ctx.vegetation->canopyCover(cand);
                if (ctx.water && ctx.water->depthAt(cand, *ctx.terrain) > 0.2f && ctx.water->depthAt(cand, *ctx.terrain) < c.hipHeightM * 0.6f)
                {
                    score += 0.6f; // wading cools
                }
                if (score > bestScore)
                {
                    bestScore = score;
                    best = cand;
                }
            }
            b.targetPos = best;
            b.hasTargetPos = true;
        }
        if (distance(p, b.targetPos) > 3.0f)
        {
            setMove(c, steer(c, sp, ctx, b.targetPos - p, walk), walk * 0.8f, Posture::Normal);
        }
        else
        {
            setMove(c, b.desiredDir, 0.0f, Posture::Lying);
        }
        break;
    }
    case BehaviorId::Court:
    {
        const Awareness* mate = nullptr;
        float best = 1e9f;
        for (const Awareness& a : c.perception.known)
        {
            if ((a.relation == Relation::Conspecific || a.relation == Relation::GroupMate || a.relation == Relation::Mate) && a.stage == AwarenessStage::Identified)
            {
                const Creature* o = world.find(a.entity);
                if (o && o->alive && o->sex != c.sex && o->repro.receptive)
                {
                    const float d = distance(p, a.estimatedPos);
                    if (d < best)
                    {
                        best = d;
                        mate = &a;
                    }
                }
            }
        }
        if (!mate)
        {
            setMove(c, steer(c, sp, ctx, cohesionBias(b.desiredDir), walk), walk * 0.7f, Posture::Normal);
            if (c.rng.chance(dt / 300.0f))
            {
                requestCall(c, CallContext::Courtship, now, 120.0f);
            }
            break;
        }
        if (best > 4.0f + c.lengthM * 0.3f)
        {
            setMove(c, steer(c, sp, ctx, mate->estimatedPos - p, walk), walk * 0.8f, best < 20.0f ? Posture::Display : Posture::Normal);
            if (best < 30.0f)
            {
                requestCall(c, CallContext::Courtship, now, 30.0f);
            }
        }
        else
        {
            face(c, mate->estimatedPos);
            b.posture = Posture::Display;
            pushIntent(c, ActionKind::Mate, mate->entity, p, 1.0f);
        }
        break;
    }
    case BehaviorId::NestTend:
    {
        const Nest* nest = c.repro.nest.valid() ? ctx.nests->find(c.repro.nest) : nullptr;
        if (c.repro.gravid && (!nest || !nest->active))
        {
            // Choose a nest site: preferred habitat, away from remembered danger, near the group/home range.
            if (!b.hasTargetPos)
            {
                Vec2 best = p;
                float bestScore = -1.0f;
                for (int i = 0; i < 12; ++i)
                {
                    const Vec2 cand = ctx.terrain->clampToBounds(p + Vec2::fromHeading(c.rng.range(0.0f, kTwoPi)) * c.rng.range(20.0f, 300.0f), 40.0f);
                    if (ctx.water && ctx.water->depthAt(cand, *ctx.terrain) > 0.0f)
                    {
                        continue;
                    }
                    float score = s.habitat[static_cast<int>(ctx.terrain->habitatAt(cand))] + 0.2f * c.rng.uniform();
                    for (const PlaceMemory& m : c.memory.places)
                    {
                        if (m.valence < 0.0f && distanceSq(m.position, cand) < 250.0f * 250.0f)
                        {
                            score -= m.strength;
                        }
                        if (m.kind == MemoryKind::Nest && distanceSq(m.position, cand) < 300.0f * 300.0f)
                        {
                            score += 0.3f * m.strength; // nest-site fidelity
                        }
                    }
                    if (score > bestScore)
                    {
                        bestScore = score;
                        best = cand;
                    }
                }
                b.targetPos = best;
                b.hasTargetPos = true;
            }
            if (distance(p, b.targetPos) > 2.0f)
            {
                setMove(c, steer(c, sp, ctx, b.targetPos - p, walk), walk * 0.8f, Posture::Normal);
            }
            else
            {
                setMove(c, b.desiredDir, 0.0f, Posture::HeadDown);
                pushIntent(c, ActionKind::LayEggs, kNoEntity, p, 1.0f);
            }
            break;
        }
        if (nest && nest->active)
        {
            const float d = distance(p, nest->position);
            if (d > 5.0f)
            {
                setMove(c, steer(c, sp, ctx, nest->position - p, walk), walk, Posture::Normal);
            }
            else
            {
                const bool brooding = nest->type == NestType::Brooded;
                setMove(c, b.desiredDir, 0.0f, brooding ? Posture::Lying : Posture::HeadUp);
                pushIntent(c, ActionKind::AttendNest, nest->id, nest->position, dt / 3600.0f * 2.0f);
            }
        }
        else
        {
            b.commitUntil = now;
        }
        break;
    }
    case BehaviorId::CareYoung:
    {
        const Awareness* young = nullptr;
        for (const Awareness& a : c.perception.known)
        {
            if (a.relation == Relation::Offspring && (!young || distance(p, a.estimatedPos) > distance(p, young->estimatedPos)))
            {
                young = &a;
            }
        }
        if (young && distance(p, young->estimatedPos) > 8.0f)
        {
            setMove(c, steer(c, sp, ctx, young->estimatedPos - p, walk * 0.7f), walk * 0.7f, Posture::Normal);
        }
        else
        {
            setMove(c, b.desiredDir, 0.0f, Posture::HeadUp);
            if (c.rng.chance(dt / 240.0f))
            {
                requestCall(c, CallContext::Contact, now, 120.0f);
            }
        }
        break;
    }
    case BehaviorId::Emigrate:
    {
        const Vec2 edge = nearestEdgePoint(p, ctx.terrain->bounds());
        setMove(c, steer(c, sp, ctx, edge - p, walk), walk * 1.1f, Posture::Normal);
        break;
    }
    case BehaviorId::Socialize:
    case BehaviorId::Groom:
    {
        setMove(c, b.desiredDir, 0.0f, Posture::Normal);
        if (s.vocal.calls.size() > 0 && c.rng.chance(dt / 900.0f))
        {
            requestCall(c, CallContext::Contact, now, 300.0f);
        }
        break;
    }
    case BehaviorId::Fight:
    {
        const Awareness* rival = strongestThreat(c);
        if (rival)
        {
            setMove(c, rival->estimatedPos - p, walk, Posture::Display);
            if (distance(p, rival->estimatedPos) < reach(c, sp) + 1.5f)
            {
                pushIntent(c, ActionKind::Attack, rival->entity, rival->estimatedPos, 0.4f);
            }
        }
        break;
    }
    case BehaviorId::Count:
        break;
    }
    // Contact calls when an individual is separated from its group (speculative acoustics, inferred existence).
    if (group && group->members.size() > 1 && s.vocal.calls.size() > 0 && ctx.toggle("contact_calls"))
    {
        const float spacing = std::max(3.0f, c.lengthM * (1.5f + 2.5f * (1.0f - group->alert)));
        const float allowed = spacing * std::sqrt(static_cast<float>(group->members.size())) * 1.3f;
        const float separation = distance(p, group->centroid) / std::max(1.0f, allowed);
        if (separation > 1.5f && c.rng.chance(dt / 300.0f * std::min(3.0f, separation)))
        {
            requestCall(c, CallContext::Contact, now, 240.0f);
        }
    }
}
} // namespace noctis
