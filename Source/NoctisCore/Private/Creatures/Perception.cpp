// Perception and memory.
//
// Detection is evidence accumulation: each modality adds evidence per second according to the
// physics of the stimulus (angular size, motion, light, fog, foliage, terrain occlusion; received
// sound level against the audiogram and ambient masking; odour concentration from a Gaussian
// plume downwind). Awareness climbs through stages — detected, oriented, vigilant, identified —
// and position knowledge is an estimate with an uncertainty radius, never the exact position.
#include "Noctis/Creatures/CreatureLogic.h"

#include <algorithm>
#include <cmath>

namespace noctis
{
namespace perceptionimpl
{
Awareness& findOrAdd(PerceptionState& ps, EntityId e, double now)
{
    for (Awareness& a : ps.known)
    {
        if (a.entity == e)
        {
            return a;
        }
    }
    Awareness a;
    a.entity = e;
    a.lastUpdate = now;
    ps.known.push_back(a);
    return ps.known.back();
}

// Ground-level Gaussian plume, Briggs open-country neutral (class D) dispersion.
float plumeConcentration(const Vec2& source, const Vec2& receptor, const Vec2& wind, float emission)
{
    const float u = std::max(0.5f, wind.length());
    const Vec2 dir = wind.length() > 0.1f ? wind / wind.length() : Vec2{1.0f, 0.0f};
    const Vec2 rel = receptor - source;
    const float x = dot(rel, dir);
    const float y = cross(dir, rel);
    if (x < 1.0f)
    {
        // Upwind: only diffusion very close to the source.
        const float d = rel.length();
        return d < 6.0f ? emission * 0.02f / (1.0f + d) : 0.0f;
    }
    const float sy = 0.08f * x / std::sqrt(1.0f + 0.0001f * x);
    const float sz = 0.06f * x / std::sqrt(1.0f + 0.0015f * x);
    return emission / (kPi * u * sy * sz) * std::exp(-y * y / (2.0f * sy * sy));
}

float scentEmission(float massKg) { return std::pow(std::max(0.01f, massKg), 0.67f); }

bool isPredatorOf(const SpeciesRuntime& predator, float predatorMass, float preyMass)
{
    const SpeciesSim& s = predator.sim();
    if (s.predation.style == HuntStyle::None || (s.diet.type != DietType::Carnivore && s.diet.type != DietType::Omnivore &&
                                                 s.diet.type != DietType::Piscivore))
    {
        return false;
    }
    // Prey range scales with the predator's own size (juvenile predators take smaller prey).
    const float scale = std::max(0.02f, predatorMass / std::max(0.01f, s.adultMassKg));
    const float lo = s.diet.preyMassMinKg * scale;
    const float hi = s.diet.preyMassMaxKg * scale;
    return preyMass >= lo * 0.5f && preyMass <= hi * 1.15f && hi > 0.0f;
}

Relation classify(const Creature& self, const SpeciesRuntime& sp, const Creature& other, const SpeciesRuntime& osp)
{
    if (other.speciesIndex == self.speciesIndex)
    {
        if (other.mother == self.id || other.father == self.id)
        {
            return Relation::Offspring;
        }
        if (self.mother == other.id || self.father == other.id)
        {
            return Relation::Parent;
        }
        if (self.repro.mate == other.id)
        {
            return Relation::Mate;
        }
        if (self.social.groupId != 0 && self.social.groupId == other.social.groupId)
        {
            return Relation::GroupMate;
        }
        // Large conspecifics can be cannibalistic predators of small juveniles (inferred for some theropods).
        if (isPredatorOf(osp, other.massKg, self.massKg) && other.massKg > self.massKg * 8.0f)
        {
            return Relation::Predator;
        }
        return Relation::Conspecific;
    }
    if (isPredatorOf(osp, other.massKg, self.massKg))
    {
        return Relation::Predator;
    }
    if (isPredatorOf(sp, self.massKg, other.massKg))
    {
        return Relation::Prey;
    }
    const bool bothHerb = sp.sim().diet.type == DietType::Herbivore && osp.sim().diet.type == DietType::Herbivore;
    return bothHerb ? Relation::Competitor : Relation::Harmless;
}

} // namespace perceptionimpl

float flightInitiationDistance(const Creature& c, const SpeciesRuntime& sp)
{
    // Flight initiation distance scales with body size in extant animals; game coefficients.
    const float caution = c.personality.get(PersonalityAxis::Caution);
    const float bold = c.personality.get(PersonalityAxis::Boldness);
    return (25.0f + 9.0f * c.lengthM) * (0.6f + 0.8f * caution - 0.4f * bold) * (0.6f + 0.6f * sp.sim().defense.fleePreference);
}

void rememberPlace(Creature& c, MemoryKind kind, const Vec2& pos, float valence, double now, EntityId about)
{
    for (PlaceMemory& m : c.memory.places)
    {
        if (m.kind == kind && distanceSq(m.position, pos) < 60.0f * 60.0f)
        {
            m.position = lerp(m.position, pos, 0.3f);
            m.valence = lerpf(m.valence, valence, 0.4f);
            m.strength = std::min(1.0f, m.strength + 0.3f);
            m.lastReinforced = now;
            if (about.valid())
            {
                m.about = about;
            }
            return;
        }
    }
    PlaceMemory m;
    m.position = pos;
    m.kind = kind;
    m.valence = valence;
    m.strength = 0.6f;
    m.lastReinforced = now;
    m.about = about;
    if (c.memory.places.size() >= 48)
    {
        auto weakest = std::min_element(c.memory.places.begin(), c.memory.places.end(),
                                        [](const PlaceMemory& a, const PlaceMemory& b) { return a.strength < b.strength; });
        *weakest = m;
    }
    else
    {
        c.memory.places.push_back(m);
    }
}

void updateMemory(Creature& c, const SpeciesRuntime& sp, const WorldContext& ctx, float dt)
{
    (void)sp;
    (void)ctx;
    const float days = dt / 86400.0f;
    for (PlaceMemory& m : c.memory.places)
    {
        float halfLifeDays = 30.0f;
        switch (m.kind)
        {
        case MemoryKind::Water: halfLifeDays = 90.0f; break;
        case MemoryKind::Food: halfLifeDays = 25.0f; break;
        case MemoryKind::Danger:
        case MemoryKind::PredatorSighting: halfLifeDays = 20.0f; break;
        case MemoryKind::ResearcherEncounter: halfLifeDays = 120.0f; break;
        case MemoryKind::Carcass: halfLifeDays = 8.0f; break;
        case MemoryKind::Nest: halfLifeDays = 200.0f; break;
        default: break;
        }
        m.strength *= std::exp(-0.693f * days / halfLifeDays);
    }
    c.memory.places.erase(std::remove_if(c.memory.places.begin(), c.memory.places.end(), [](const PlaceMemory& m) { return m.strength < 0.03f; }),
                          c.memory.places.end());
    for (SocialMemory& s : c.memory.social)
    {
        s.familiarity *= std::exp(-0.693f * days / 365.0f);
    }
    ThreatHistory& t = c.memory.threats;
    t.researcherNegative *= std::exp(-0.693f * days / 60.0f);
    t.vehicleNegative *= std::exp(-0.693f * days / 60.0f);
    t.droneNegative *= std::exp(-0.693f * days / 45.0f);
}

void perceive(Creature& c, const SpeciesRuntime& sp, const CreatureSystem& world, const WorldContext& ctx, float dtSince)
{
    using namespace perceptionimpl;
    if (!c.alive)
    {
        return;
    }
    PerceptionState& ps = c.perception;
    const SpeciesSim::Senses& se = sp.sim().senses;
    const float dt = clampf(dtSince, 0.01f, 30.0f);
    const double now = ctx.now;
    const Vec2 p = c.pos2();
    const float groundZ = c.loco.position.z;
    const Vec3 eye{p, groundZ + c.hipHeightM * sp.eyeHeightFactor};
    const bool fullDetail = c.lod == LodLevel::Full;
    const WeatherState& w = ctx.weather->state();

    const float light = visualPerformance(ctx.sky->state().illuminanceLux, se.lowLight);
    ps.lightLevel = light;
    const bool nearWater = c.loco.waterDepth > 0.0f;
    const float fogBeta = ctx.weather->fogExtinctionAt(groundZ - ctx.valleyFloorZ, nearWater) + 3.912f / 40000.0f +
                          0.00045f * std::pow(std::max(0.0f, w.precipMmPerHour), 0.7f);
    float ambient = dbSumPower(w.windNoiseDb, w.rainNoiseDb);
    if (ctx.water)
    {
        ambient = dbSumPower(ambient, ctx.water->noiseLevelDb(p));
    }
    ps.ambientNoiseDb = ambient;

    const float visionRange = std::max(5.0f, se.visionRangeM * (0.25f + 0.75f * light));
    const float halfFov = 0.5f * se.fovDeg * kDegToRad;
    const float halfBino = 0.5f * se.binocularDeg * kDegToRad;
    const float vigilanceGain = 0.5f + ps.alertness + 0.5f * c.personality.get(PersonalityAxis::Vigilance) +
                                (c.behavior.posture == Posture::HeadUp ? 0.6f : 0.0f) - (c.behavior.posture == Posture::HeadDown ? 0.35f : 0.0f) -
                                (c.behavior.current == BehaviorId::Sleep ? 0.9f : 0.0f);

    // Leaky evidence: awareness fades without new stimuli; position uncertainty grows.
    for (Awareness& a : ps.known)
    {
        a.evidence *= std::exp(-dt / 25.0f);
        a.identification *= std::exp(-dt / 900.0f);
        a.uncertaintyM += dt * (1.0f + a.estimatedVel.length());
        a.estimatedPos += a.estimatedVel * dt * 0.5f;
    }

    std::vector<u32> nearby;
    const float queryRadius = std::min(1600.0f, std::max(visionRange, 350.0f));
    world.neighbours(p, queryRadius, nearby);
    const std::vector<Creature>& all = world.creatures();
    const Vec2 wind = w.wind;
    const float olfactionThreshold = 0.004f / std::max(0.02f, se.olfaction);

    auto visualEvidence = [&](const Vec2& targetPos, float targetZ, float targetHeight, float targetSpeed, float crypsis, float& qualityOut) {
        qualityOut = 0.0f;
        const Vec2 rel = targetPos - p;
        const float d = rel.length();
        if (d > visionRange || d < 0.01f)
        {
            return d < 0.01f ? 1.0f : 0.0f;
        }
        const float off = std::fabs(angleDelta(c.loco.heading, rel.heading()));
        if (off > halfFov && d > c.lengthM * 1.5f)
        {
            return 0.0f;
        }
        const float angular = targetHeight / d;
        const float sizeTerm = saturate(angular * 350.0f);
        const float motion = 1.0f + 2.0f * saturate(targetSpeed / 2.0f);
        const float fogT = std::exp(-fogBeta * d);
        float foliageT = 1.0f;
        if (ctx.vegetation)
        {
            foliageT = ctx.vegetation->transmittance(p, targetPos, std::min(eye.z - groundZ, targetHeight * 0.6f));
        }
        float los = 1.0f;
        if (fullDetail && d > 25.0f)
        {
            const Vec3 to{targetPos, targetZ + targetHeight * 0.7f};
            los = ctx.terrain->lineOfSight(eye, to, std::max(6.0f, d / 30.0f)) ? 1.0f : 0.0f;
        }
        const float binocular = off < halfBino ? 1.3f : 1.0f;
        const float quality = sizeTerm * light * fogT * foliageT * los * binocular * (1.0f - 0.7f * crypsis);
        qualityOut = quality;
        return 1.4f * quality * motion * std::max(0.05f, vigilanceGain);
    };

    // ---- Other animals ----
    for (const u32 idx : nearby)
    {
        const Creature& o = all[idx];
        if (o.id == c.id || !o.alive)
        {
            continue;
        }
        const SpeciesRuntime& osp = world.speciesOf(o);
        const Vec2 op = o.pos2();
        const float d = distance(p, op);
        float quality = 0.0f;
        const float crypsis = (o.behavior.posture == Posture::Crouch || o.behavior.current == BehaviorId::Freeze) ? 0.5f : 0.0f;
        float rate = visualEvidence(op, o.loco.position.z, o.hipHeightM * 1.3f, o.loco.speed, crypsis, quality);
        // Smell (downwind of the other animal).
        const float conc = plumeConcentration(op, p, wind, scentEmission(o.massKg)) * se.olfaction;
        float smellRate = 0.0f;
        if (conc > olfactionThreshold)
        {
            smellRate = saturate(std::log10(conc / olfactionThreshold) / 2.0f) * 0.5f;
        }
        if (rate <= 0.0f && smellRate <= 0.0f)
        {
            continue;
        }
        Awareness& a = findOrAdd(ps, o.id, now);
        a.speciesIndex = o.speciesIndex;
        const float gain = (rate + smellRate) * dt;
        a.evidence = std::min(1.5f, a.evidence + gain);
        if (rate > 0.0f)
        {
            a.modalities |= ModVision;
            a.identification += quality * 1.2f * dt;
            const float err = d * (0.02f + 0.1f * (1.0f - quality));
            const float blend = saturate(rate * dt * 2.0f);
            a.estimatedPos = lerp(a.estimatedPos, op + Vec2{c.rng.normal() * err * 0.3f, c.rng.normal() * err * 0.3f}, blend);
            a.uncertaintyM = std::min(a.uncertaintyM, err + 1.0f);
            a.estimatedVel = lerp(a.estimatedVel, o.loco.velocity, blend);
        }
        if (smellRate > 0.0f)
        {
            a.modalities |= ModSmell;
            a.identification += 0.25f * smellRate * dt;
            if (a.uncertaintyM > d * 0.3f)
            {
                // Smell gives direction upwind and a rough range only.
                const Vec2 upwind = wind.length() > 0.1f ? -wind.normalized() : (op - p).normalized();
                a.estimatedPos = p + upwind * d;
                a.uncertaintyM = d * 0.4f;
            }
        }
        a.lastUpdate = now;
        if (a.identification >= 1.0f)
        {
            a.relation = classify(c, sp, o, osp);
        }
        // Threat / opportunity appraisal (from observable cues only).
        if (a.relation == Relation::Predator)
        {
            const Vec2 toMe = (p - op).normalized();
            const float approach = dot(o.loco.velocity, toMe);
            float base = saturate(osp.sim().predation.attackStrength * std::sqrt(o.massKg / std::max(0.1f, c.massKg)) * 0.9f + 0.2f);
            const float fid = flightInitiationDistance(c, sp) * (1.0f + o.lengthM / std::max(0.5f, c.lengthM) * 0.2f);
            const float proximity = saturate(1.2f - d / std::max(10.0f, fid * 2.5f));
            float posture = 1.0f;
            if (o.behavior.posture == Posture::Lying)
            {
                posture = 0.45f;
            }
            else if (o.behavior.posture == Posture::HeadDown)
            {
                posture = 0.7f;
            }
            const float approachMul = approach > 0.5f ? (approach > 3.0f ? 2.0f : 1.4f) : 1.0f;
            a.threat = saturate(base * (0.3f + 0.7f * proximity) * posture * approachMul);
        }
        else if (a.relation == Relation::Prey)
        {
            const SpeciesSim& os = osp.sim();
            const float energy = saturate(o.massKg / std::max(1.0f, c.massKg * 0.4f));
            const float vulnerability = (1.0f - o.injuries.healthIndex) + (o.massKg < os.adultMassKg * 0.3f ? 0.4f : 0.0f) + o.phys.fatigue * 0.3f;
            const float risk = os.defense.defenseStrength * std::sqrt(o.massKg / std::max(0.1f, c.massKg)) * o.injuries.defenseFactor;
            a.opportunity = saturate(energy * (0.5f + vulnerability) / (0.4f + risk));
            a.threat = saturate(risk * 0.3f * (d < c.lengthM * 2.0f ? 1.0f : 0.3f));
        }
        else if (a.relation == Relation::Conspecific && sp.sim().social.territorial && o.massKg > c.massKg * 0.6f &&
                 o.ageYears(now) > sp.sim().growth.maturityAgeYears * 0.8f)
        {
            a.threat = saturate(0.25f * std::sqrt(o.massKg / std::max(0.1f, c.massKg)));
        }
        else
        {
            a.threat *= 0.9f;
        }
        if (a.relation == Relation::Predator && a.identification >= 1.0f && a.threat > 0.3f)
        {
            rememberPlace(c, MemoryKind::PredatorSighting, op, -0.7f, now, o.id);
        }
        if ((a.relation == Relation::GroupMate || a.relation == Relation::Conspecific || a.relation == Relation::Mate) && a.identification >= 1.0f)
        {
            bool found = false;
            for (SocialMemory& s : c.memory.social)
            {
                if (s.other == o.id)
                {
                    s.familiarity = std::min(1.0f, s.familiarity + dt / 3600.0f);
                    s.lastSeen = now;
                    found = true;
                    break;
                }
            }
            if (!found && c.memory.social.size() < 32)
            {
                c.memory.social.push_back({o.id, dt / 3600.0f, 0.0f, 0.0f, now});
            }
        }
    }

    // ---- Researcher, rover, drone, sensors ----
    if (ctx.emitters)
    {
        for (const Emitter& e : *ctx.emitters)
        {
            if (!e.active)
            {
                continue;
            }
            const Vec2 ep = e.position.xy();
            const float d = distance(p, ep);
            if (d > queryRadius)
            {
                continue;
            }
            float quality = 0.0f;
            const float crypsis = e.crouched ? 0.45f : 0.0f;
            float rate = visualEvidence(ep, e.position.z - (e.airborne ? 0.0f : 0.0f), e.heightM, e.velocity.length(), crypsis, quality);
            if (e.lightsOn && light < 0.5f && d < 600.0f)
            {
                rate += 0.6f; // artificial light at night is conspicuous
                quality = std::max(quality, 0.3f);
            }
            const float conc = plumeConcentration(ep, p, wind, e.scent * 3.0f) * se.olfaction;
            const float smellRate = conc > olfactionThreshold ? saturate(std::log10(conc / olfactionThreshold) / 2.0f) * 0.5f : 0.0f;
            if (rate <= 0.0f && smellRate <= 0.0f)
            {
                continue;
            }
            Awareness& a = findOrAdd(ps, e.id, now);
            a.evidence = std::min(1.5f, a.evidence + (rate + smellRate) * dt);
            a.modalities |= (rate > 0.0f ? ModVision : 0) | (smellRate > 0.0f ? ModSmell : 0);
            a.identification += (quality * 1.0f + smellRate * 0.2f) * dt;
            a.estimatedPos = lerp(a.estimatedPos, ep, saturate((rate + smellRate) * dt * 2.0f));
            a.estimatedVel = e.velocity;
            a.uncertaintyM = std::min(a.uncertaintyM, d * (rate > 0.0f ? 0.05f : 0.4f) + 1.0f);
            a.lastUpdate = now;
            const EntityKind kind = e.id.kind();
            a.relation = kind == EntityKind::Vehicle ? Relation::Vehicle : (kind == EntityKind::Drone ? Relation::Drone : Relation::Researcher);
            const ThreatHistory& th = c.memory.threats;
            float base = 0.0f;
            float habituation = 0.0f;
            float sensitisation = 0.0f;
            if (a.relation == Relation::Researcher)
            {
                // A ~80 kg biped is novel. Prey-sized for large predators (opportunity), a threat for small animals.
                if (isPredatorOf(sp, c.massKg, e.massKg))
                {
                    a.opportunity = saturate(0.35f + 0.5f * c.phys.hunger) * (0.5f + 0.5f * c.personality.get(PersonalityAxis::Boldness));
                }
                base = 0.2f + 0.45f * saturate(1.0f - std::log10(std::max(1.0f, c.massKg)) / 4.0f);
                habituation = th.researcherExposure / (th.researcherExposure + 5.0f * 3600.0f);
                sensitisation = saturate(th.researcherNegative);
            }
            else if (a.relation == Relation::Vehicle)
            {
                base = 0.25f + 0.25f * saturate(e.noiseDb / 90.0f) + 0.2f * saturate(e.massKg / std::max(10.0f, c.massKg) - 0.5f);
                habituation = th.vehicleExposure / (th.vehicleExposure + 8.0f * 3600.0f);
                sensitisation = saturate(th.vehicleNegative);
            }
            else
            {
                const float altitude = std::max(0.0f, e.position.z - groundZ);
                base = 0.15f + 0.35f * saturate(1.0f - altitude / 60.0f) + 0.2f * saturate((e.noiseDb - 60.0f) / 30.0f);
                habituation = th.droneExposure / (th.droneExposure + 3.0f * 3600.0f);
                sensitisation = saturate(th.droneNegative);
            }
            const Vec2 toMe = (p - ep).normalized();
            const float approach = dot(e.velocity, toMe);
            const float approachMul = approach > 0.3f ? (1.3f + 0.2f * std::min(3.0f, approach)) : (e.velocity.length() < 0.2f ? 0.8f : 1.0f);
            const float fid = flightInitiationDistance(c, sp);
            const float proximity = saturate(1.3f - d / std::max(10.0f, fid * 2.0f));
            a.threat = saturate(base * (1.0f - 0.7f * habituation) * approachMul * (0.35f + 0.65f * proximity) + 0.45f * sensitisation);
        }
    }

    // ---- Sounds (vocalizations, footfalls, engines, rotors, thunder) ----
    std::vector<SoundSource> sounds;
    ctx.sound->collectNear(c.lastPerception, now, p, 4000.0f, sounds);
    const PropagationEnv env = ctx.propagation();
    for (const SoundSource& s : sounds)
    {
        if (s.emitter == c.id)
        {
            continue;
        }
        const float level = SoundField::receivedLevelDb(s, eye, env);
        const float thr = SoundField::hearingThresholdDb(s.f0Hz * s.pitchShift, se.hearingMinHz, se.hearingMaxHz, se.hearingBestHz, se.hearingThresholdDb);
        const float masking = ambient - 8.0f;
        const float snr = level - std::max(thr, masking);
        // Vibration channel for heavy footfalls (speculative capacity, species-dependent).
        float vib = 0.0f;
        if (s.kind == SoundKind::Footfall && se.vibration > 0.0f)
        {
            const float dd = std::max(1.0f, distance(s.position.xy(), p));
            vib = se.vibration * saturate((s.levelDb - 60.0f) / 40.0f) * saturate(1.0f - dd / 150.0f);
        }
        if (snr <= 0.0f && vib <= 0.0f)
        {
            continue;
        }
        if (s.kind == SoundKind::Thunder)
        {
            ps.alertness = std::max(ps.alertness, 0.2f * saturate(snr / 30.0f));
            continue;
        }
        if (!s.emitter.valid())
        {
            continue;
        }
        Awareness& a = findOrAdd(ps, s.emitter, now);
        if (a.speciesIndex < 0)
        {
            a.speciesIndex = s.speciesIndex;
        }
        a.evidence = std::min(1.5f, a.evidence + saturate(snr / 20.0f) * 0.6f + vib * 0.4f);
        a.modalities |= (snr > 0.0f ? ModHearing : 0) | (vib > 0.0f ? ModVibration : 0);
        const float d = distance(s.position.xy(), p);
        // Low frequencies are harder to localise.
        const float dirErr = (s.f0Hz < 200.0f ? 0.35f : 0.18f) * (1.0f - 0.5f * saturate(snr / 30.0f));
        const float angle = (s.position.xy() - p).heading() + c.rng.normal() * dirErr;
        const float rangeGuess = d * (1.0f + 0.3f * c.rng.normal());
        if (a.uncertaintyM > d * 0.25f)
        {
            a.estimatedPos = p + Vec2::fromHeading(angle) * std::max(5.0f, rangeGuess);
            a.uncertaintyM = std::min(a.uncertaintyM, d * (0.25f + dirErr));
        }
        a.lastUpdate = now;
        if (s.kind == SoundKind::Vocalization)
        {
            // Calls of known species are recognisable.
            a.identification += 0.35f * saturate(snr / 15.0f);
            if (s.speciesIndex == c.speciesIndex && static_cast<CallContext>(s.callContext) == CallContext::Alarm)
            {
                ps.lastAlarmHeard = now;
                ps.alarmDirection = (s.position.xy() - p).normalized();
                ps.alarmSource = s.emitter;
                a.modalities |= ModCall;
            }
            if (s.speciesIndex == c.speciesIndex && static_cast<CallContext>(s.callContext) == CallContext::Distress)
            {
                ps.lastAlarmHeard = now;
                ps.alarmDirection = (s.position.xy() - p).normalized();
            }
        }
    }

    // ---- Stages ----
    float alert = 0.0f;
    const Awareness* salient = nullptr;
    float bestSalience = 0.0f;
    for (Awareness& a : ps.known)
    {
        a.identification = std::min(a.identification, 1.2f);
        if (a.identification >= 1.0f)
        {
            a.stage = AwarenessStage::Identified;
        }
        else if (a.evidence > 0.55f)
        {
            a.stage = AwarenessStage::Vigilant;
        }
        else if (a.evidence > 0.3f)
        {
            a.stage = AwarenessStage::Oriented;
        }
        else if (a.evidence > 0.12f)
        {
            a.stage = AwarenessStage::Detected;
        }
        else
        {
            a.stage = AwarenessStage::Unaware;
        }
        // Unidentified stimuli are mildly alarming in proportion to evidence.
        const float effectiveThreat = a.stage == AwarenessStage::Identified ? a.threat : std::max(a.threat * 0.6f, 0.3f * saturate(a.evidence));
        if (a.stage >= AwarenessStage::Oriented)
        {
            alert = std::max(alert, effectiveThreat);
        }
        const float salience = a.evidence * (0.3f + effectiveThreat + a.opportunity);
        if (a.stage >= AwarenessStage::Oriented && salience > bestSalience)
        {
            bestSalience = salience;
            salient = &a;
        }
    }
    const float alarmRecency = saturate(1.0f - static_cast<float>(now - ps.lastAlarmHeard) / 45.0f);
    alert = std::max(alert, 0.6f * alarmRecency);
    ps.alertness = std::max(alert, ps.alertness * std::exp(-dt / 40.0f));
    if (salient)
    {
        ps.lookTarget = salient->estimatedPos;
        ps.hasLookTarget = true;
    }
    else if (alarmRecency > 0.0f)
    {
        ps.lookTarget = p + ps.alarmDirection * 50.0f;
        ps.hasLookTarget = true;
    }
    else
    {
        ps.hasLookTarget = false;
    }

    // Habituation counters: neutral exposure to identified researcher/vehicle/drone.
    for (const Awareness& a : ps.known)
    {
        if (a.stage < AwarenessStage::Identified)
        {
            continue;
        }
        if (a.relation == Relation::Researcher)
        {
            c.memory.threats.researcherExposure += dt;
        }
        else if (a.relation == Relation::Vehicle)
        {
            c.memory.threats.vehicleExposure += dt;
        }
        else if (a.relation == Relation::Drone)
        {
            c.memory.threats.droneExposure += dt;
        }
    }

    // Forget stale awareness; keep the list bounded.
    ps.known.erase(std::remove_if(ps.known.begin(), ps.known.end(),
                                  [&](const Awareness& a) { return a.evidence < 0.02f && now - a.lastUpdate > 90.0; }),
                   ps.known.end());
    if (ps.known.size() > 32)
    {
        std::sort(ps.known.begin(), ps.known.end(), [](const Awareness& a, const Awareness& b) {
            return a.evidence * (1.0f + a.threat + a.opportunity) > b.evidence * (1.0f + b.threat + b.opportunity);
        });
        ps.known.resize(32);
    }
    ps.lastPerceptionTime = now;
    c.lastPerception = now;
}
} // namespace noctis
