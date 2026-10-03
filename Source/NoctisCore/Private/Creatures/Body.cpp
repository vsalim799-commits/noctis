// Physiology, injuries, growth and background mortality.
#include "Noctis/Creatures/CreatureLogic.h"

#include <cmath>

namespace noctis
{
namespace bodyimpl
{
constexpr float kSecondsPerDay = 86400.0f;

// Operative environmental temperature felt by the animal (air + solar load - evaporative/water cooling).
float operativeTemperature(const Creature& c, const WorldContext& ctx)
{
    const WeatherState& w = ctx.weather->state();
    const SkyState& sky = ctx.sky->state();
    float t = w.temperatureC;
    const float sun = saturate(sky.sunElevationDeg / 50.0f) * (1.0f - 0.8f * w.cloudCover);
    const float shade = ctx.vegetation ? ctx.vegetation->canopyCover(c.pos2()) : 0.0f;
    t += 9.0f * sun * (1.0f - 0.85f * shade);
    t -= 0.6f * std::sqrt(std::max(0.0f, w.windSpeedMs));
    if (c.loco.waterDepth > 0.3f * c.hipHeightM)
    {
        t = lerpf(t, w.temperatureC - 3.0f, saturate(c.loco.waterDepth / std::max(0.1f, c.hipHeightM)));
    }
    if (w.precipMmPerHour > 0.5f)
    {
        t -= 2.0f;
    }
    return t;
}
} // namespace bodyimpl

float visualPerformance(float illuminanceLux, float lowLight)
{
    // Log-illuminance response: full performance in daylight; low-light adapted eyes keep working
    // under moonlight/starlight (scotopic shift of ~2.5 log units at lowLight = 1).
    const float l = std::log10(std::max(1e-5f, illuminanceLux));
    const float halfPoint = 1.0f - 3.0f * saturate(lowLight); // log10 lux at 50 % performance
    return saturate(0.5f + (l - halfPoint) / 3.0f);
}

float circadianActivity(const SpeciesRuntime& sp, const WorldContext& ctx)
{
    const float el = ctx.sky->state().sunElevationDeg;
    const float day = smoothstep(-6.0f, 6.0f, el);
    const float twilight = 1.0f - std::fabs(clampf(el / 10.0f, -1.0f, 1.0f));
    switch (sp.def->sim.activity)
    {
    case ActivityPattern::Diurnal: return 0.15f + 0.85f * day;
    case ActivityPattern::Nocturnal: return 0.15f + 0.85f * (1.0f - day);
    case ActivityPattern::Crepuscular: return 0.25f + 0.75f * twilight;
    case ActivityPattern::Cathemeral: return 0.7f;
    }
    return 0.7f;
}

void updatePhysiology(Creature& c, const SpeciesRuntime& sp, const WorldContext& ctx, float dt)
{
    if (!c.alive || dt <= 0.0f)
    {
        return;
    }
    PhysiologyState& p = c.phys;
    const SpeciesSim& s = sp.sim();
    const float m = std::max(0.01f, c.massKg);

    // --- Energy expenditure ---
    const float fmr = fieldMetabolicRateW(sp, m);
    float activity = 1.0f;
    if (c.behavior.current == BehaviorId::Sleep)
    {
        activity = 0.55f;
    }
    else if (c.behavior.current == BehaviorId::Rest)
    {
        activity = 0.7f;
    }
    // Ectotherm/mesotherm metabolism scales with body temperature (Q10 = 2.5, game assumption).
    if (sp.fmrMesoBlend > 0.0f)
    {
        const float q = std::pow(2.5f, (p.bodyTempC - sp.targetBodyTempC) / 10.0f);
        activity *= lerpf(1.0f, q, sp.fmrMesoBlend);
    }
    const float healing = 0.15f * fmr * (1.0f - c.injuries.healthIndex);
    const float growthCost = 0.0f; // folded into FMR allometry
    p.metabolicRateW = fmr * activity + c.loco.metabolicCostW + healing + growthCost;

    // --- Digestion ---
    const float retentionS = sp.gutRetentionHours * 3600.0f;
    const float digested = std::min(p.gutFillKg, p.gutFillKg * dt / retentionS);
    p.gutFillKg -= digested;
    const float energyIn = digested * p.gutEnergyMJPerKg;
    p.energyReserveMJ += energyIn - p.metabolicRateW * dt * 1e-6f;
    p.energyCapacityMJ = std::max(1e-3f, s.metabolism.maxFastingDays * fmr * bodyimpl::kSecondsPerDay * 1e-6f);
    p.energyReserveMJ = clampf(p.energyReserveMJ, 0.0f, p.energyCapacityMJ);
    p.gutCapacityKg = std::max(1e-3f, sp.gutCapacityFraction * m);
    p.condition = p.energyReserveMJ / p.energyCapacityMJ;

    // --- Water ---
    const float needLPerDay = s.metabolism.waterLPerDayPer100kg * m / 100.0f * (1.0f + std::max(0.0f, p.heatStress));
    const float bufferL = sp.waterBufferFraction * m;
    p.hydration = clampf(p.hydration - needLPerDay * dt / bodyimpl::kSecondsPerDay / std::max(1e-3f, bufferL), 0.0f, 1.0f);

    // --- Drives ---
    const float gutFrac = p.gutFillKg / p.gutCapacityKg;
    p.hunger = saturate((0.9f - p.condition) / 0.6f) * (1.0f - 0.8f * saturate(gutFrac));
    p.thirst = saturate((0.92f - p.hydration) / 0.45f);

    // --- Exertion and sleep ---
    const float speedFrac = s.locomotion.maxSpeedMs > 0.0f ? c.loco.speed / s.locomotion.maxSpeedMs : 0.0f;
    if (speedFrac > 0.55f)
    {
        p.fatigue = saturate(p.fatigue + std::pow(speedFrac, 3.0f) * dt / 40.0f);
    }
    else
    {
        const float rec = (c.behavior.current == BehaviorId::Rest || c.behavior.current == BehaviorId::Sleep) ? 2.0f : 1.0f;
        p.fatigue = std::max(0.0f, p.fatigue - rec * dt / 240.0f);
    }
    if (c.behavior.current == BehaviorId::Sleep)
    {
        p.sleepPressure = std::max(0.0f, p.sleepPressure - dt / (6.0f * 3600.0f));
    }
    else
    {
        p.sleepPressure = std::min(1.0f, p.sleepPressure + dt / (16.0f * 3600.0f));
    }

    // --- Thermal balance (thermal time constant ~ M^(1/3)) ---
    const float tauH = 0.42f * std::cbrt(m);
    const float operative = bodyimpl::operativeTemperature(c, ctx);
    float target = sp.targetBodyTempC;
    switch (s.metabolism.thermo)
    {
    case ThermoClass::Endotherm: target = sp.targetBodyTempC + 1.5f * saturate(speedFrac) + 0.08f * std::max(0.0f, operative - 30.0f); break;
    case ThermoClass::Mesotherm: target = lerpf(operative, sp.targetBodyTempC, 0.6f) + 1.0f * saturate(speedFrac); break;
    case ThermoClass::Ectotherm: target = operative + 0.5f * saturate(speedFrac); break;
    }
    p.bodyTempC = lerpf(p.bodyTempC, target, approachFactor(dt, tauH * 3600.0f));
    const float comfortHigh = sp.targetBodyTempC + 2.5f;
    const float comfortLow = s.metabolism.thermo == ThermoClass::Endotherm ? sp.targetBodyTempC - 3.0f : 18.0f;
    if (p.bodyTempC > comfortHigh)
    {
        p.heatStress = saturate((p.bodyTempC - comfortHigh) / 4.0f);
    }
    else if (p.bodyTempC < comfortLow)
    {
        p.heatStress = -saturate((comfortLow - p.bodyTempC) / 8.0f);
    }
    else
    {
        p.heatStress = 0.0f;
    }

    // --- Stress ---
    p.stress = std::max(c.perception.alertness * 0.8f, p.stress * std::exp(-dt / 600.0f));
    p.chronicStress = clampf(p.chronicStress + (p.stress - p.chronicStress) * dt / (3.0f * bodyimpl::kSecondsPerDay), 0.0f, 1.0f);

    // --- Respiration (mammalian allometry f ≈ 53.5 M^-0.26 breaths/min, scaled by metabolic load) ---
    const float load = saturate(p.metabolicRateW / std::max(1.0f, fmr) / 3.0f);
    p.respirationHz = 0.89f * std::pow(m, -0.26f) * (0.8f + 1.6f * load);
    p.breathPhase = std::fmod(p.breathPhase + p.respirationHz * dt, 1.0f);
    c.soft.breath = 0.5f - 0.5f * std::cos(kTwoPi * p.breathPhase);
}

void recomputeImpairment(Creature& c, const SpeciesRuntime& sp)
{
    InjuryState& inj = c.injuries;
    std::array<float, kBodyRegionCount> impair{};
    float infection = 0.0f;
    for (const Injury& i : inj.active)
    {
        impair[static_cast<size_t>(i.region)] += i.severity;
        infection = std::max(infection, i.infection);
    }
    float loco = 0.0f;
    float feed = 0.0f;
    float defense = 0.0f;
    float lethal = 0.0f;
    float pain = 0.0f;
    for (int r = 0; r < kBodyRegionCount; ++r)
    {
        const RegionInfo& ri = sp.regions[static_cast<size_t>(r)];
        const float v = std::min(1.0f, impair[static_cast<size_t>(r)]);
        loco += ri.locomotion * v;
        feed += ri.feeding * v;
        defense += ri.defense * v;
        lethal += ri.lethality * v;
        pain += v * 0.5f;
    }
    // Limp from left/right asymmetry of limb injuries.
    const float left = impair[static_cast<size_t>(BodyRegion::HindlimbL)] + 0.6f * impair[static_cast<size_t>(BodyRegion::ForelimbL)];
    const float right = impair[static_cast<size_t>(BodyRegion::HindlimbR)] + 0.6f * impair[static_cast<size_t>(BodyRegion::ForelimbR)];
    inj.limp = saturate(std::fabs(left - right) + 0.5f * std::min(left, right));
    inj.limpSide = left > right ? -1.0f : (right > left ? 1.0f : 0.0f);
    inj.speedFactor = clampf(1.0f - 1.6f * loco, 0.12f, 1.0f) * (1.0f - 1.2f * c.phys.bloodLoss);
    inj.feedingFactor = clampf(1.0f - 1.5f * feed, 0.05f, 1.0f);
    inj.defenseFactor = clampf(1.0f - 1.2f * defense, 0.1f, 1.0f);
    inj.balanceFactor = clampf(1.0f - 1.2f * (impair[static_cast<size_t>(BodyRegion::Tail)] * 0.5f + loco), 0.2f, 1.0f);
    c.phys.pain = saturate(pain);
    const float starvation = c.phys.condition <= 0.0f ? 0.3f : 0.0f;
    const float dehydration = c.phys.hydration < 0.15f ? (0.15f - c.phys.hydration) * 4.0f : 0.0f;
    inj.healthIndex = 1.0f - saturate(lethal * 0.9f + c.phys.bloodLoss * 1.8f + infection * 0.5f + starvation + dehydration);
}

void updateInjuries(Creature& c, const SpeciesRuntime& sp, const WorldContext& ctx, float dt)
{
    if (!c.alive)
    {
        return;
    }
    InjuryState& inj = c.injuries;
    const float dtDays = dt / bodyimpl::kSecondsPerDay;
    const float thermoHeal = sp.sim().metabolism.thermo == ThermoClass::Endotherm ? 1.0f
                             : sp.sim().metabolism.thermo == ThermoClass::Mesotherm ? 0.8f
                                                                                     : 0.6f;
    float bleedingTotal = 0.0f;
    for (Injury& i : inj.active)
    {
        // Clotting reduces bleeding exponentially (tau ~ 40 min, game assumption).
        bleedingTotal += i.bleeding;
        i.bleeding *= std::exp(-dt / 2400.0f);
        // Infection risk for open wounds; bites carry more (oral flora — inferred from extant analogues
        // and paleopathological lesions, e.g. Wolff et al. 2009).
        const bool open = i.type == InjuryType::Laceration || i.type == InjuryType::Puncture;
        if (open && i.infection <= 0.0f)
        {
            const float p = 0.04f * i.severity * (i.causeSpecies >= 0 ? 1.5f : 1.0f) * (1.2f - c.phys.condition) * dtDays;
            if (c.rng.chance(p))
            {
                i.infection = 0.05f;
            }
        }
        else if (i.infection > 0.0f)
        {
            const float growth = 0.06f * (1.1f - c.phys.condition) - 0.04f * thermoHeal * c.phys.condition;
            i.infection = clampf(i.infection + growth * dtDays, 0.0f, 1.0f);
        }
        // Healing (paleopathology documents healed bites and fractures; rates are game assumptions).
        const float base = i.type == InjuryType::Fracture ? 0.008f : (i.type == InjuryType::Contusion ? 0.08f : 0.025f);
        const float heal = base * thermoHeal * (0.4f + c.phys.condition) * (1.0f - 0.8f * i.infection);
        i.severity = std::max(0.0f, i.severity - heal * dtDays);
    }
    c.phys.bloodLoss = clampf(c.phys.bloodLoss + bleedingTotal * dt / 3600.0f - 0.03f * dtDays * c.phys.condition, 0.0f, 1.0f);
    // Healed wounds leave scars (persistent identification features).
    for (size_t k = 0; k < inj.active.size();)
    {
        Injury& i = inj.active[k];
        if (i.severity < 0.03f && i.infection < 0.05f)
        {
            if (i.initialSeverity > 0.25f && i.type != InjuryType::Contusion && i.type != InjuryType::Sprain)
            {
                Scar s;
                s.region = i.region;
                s.type = i.type;
                s.size = i.initialSeverity;
                s.shapeSeed = c.rng.nextU32();
                s.time = ctx.now;
                inj.scars.push_back(s);
            }
            inj.active.erase(inj.active.begin() + static_cast<std::ptrdiff_t>(k));
            continue;
        }
        ++k;
    }
    recomputeImpairment(c, sp);
}

float massAtAge(const SpeciesRuntime& sp, float ageYears, float sizeGene)
{
    // Offset logistic: equals hatchling mass at age 0 and approaches the asymptote; maximum growth
    // rate r_max sets k = 4 r_max / M (logistic), centred on the inflection age.
    const SpeciesSim::Growth& g = sp.sim().growth;
    const float mInf = std::max(0.01f, g.asymptoticMassKg * sizeGene);
    const float m0 = std::max(0.0005f, sp.sim().hatchlingMassKg);
    const float k = 4.0f * std::max(1e-4f, g.maxGrowthRateKgPerYear) / mInf;
    auto logistic = [&](float t) { return 1.0f / (1.0f + std::exp(-k * (t - g.inflectionAgeYears))); };
    const float l0 = logistic(0.0f);
    const float l = logistic(std::max(0.0f, ageYears));
    const float frac = (l - l0) / std::max(1e-6f, 1.0f - l0);
    return m0 + (mInf - m0) * saturate(frac);
}

void applyBodySize(Creature& c, const SpeciesRuntime& sp, float massKg)
{
    c.massKg = std::max(0.0005f, massKg);
    // Isometric scaling from the adult: length ∝ M^(1/3). (Ontogenetic allometry exists — e.g. juvenile
    // tyrannosaurs were relatively long-legged — and is noted in the species files.)
    const float ratio = std::cbrt(c.massKg / std::max(0.01f, sp.sim().adultMassKg));
    c.lengthM = sp.sim().adultLengthM * ratio;
    c.hipHeightM = sp.sim().hipHeightM * ratio;
}

float backgroundHazardPerYear(const SpeciesRuntime& sp, float ageYears, float densityRatio)
{
    const SpeciesSim::Growth& g = sp.sim().growth;
    const float life = std::max(1.0f, g.maxLifespanYears);
    float h = 0.0f;
    if (ageYears < 1.0f)
    {
        // Neonatal mortality: high and density dependent (game assumption informed by survivorship
        // curves of extant archosaurs and by Erickson et al. 2006 for tyrannosaurs).
        h = 1.6f * std::pow(std::max(0.2f, densityRatio), 1.5f);
    }
    else if (ageYears < g.maturityAgeYears)
    {
        h = 0.06f * std::pow(std::max(0.2f, densityRatio), 1.0f);
    }
    else
    {
        h = 0.07f;
    }
    // Senescence (Gompertz-like rise after 75 % of maximum lifespan).
    if (ageYears > 0.75f * life)
    {
        h += 0.08f * std::exp(8.0f * (ageYears - 0.75f * life) / life);
    }
    return h;
}
} // namespace noctis
