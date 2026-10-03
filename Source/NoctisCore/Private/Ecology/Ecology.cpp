#include "Noctis/Ecology/Ecology.h"

#include <cmath>

namespace noctis
{
const char* guildLabelFr(PopulationGuild g)
{
    switch (g)
    {
    case PopulationGuild::Fish: return "Poissons";
    case PopulationGuild::Insects: return "Insectes";
    case PopulationGuild::SmallVertebrates: return "Petits vertébrés";
    case PopulationGuild::Birds: return "Oiseaux";
    case PopulationGuild::Molluscs: return "Mollusques";
    case PopulationGuild::Other: return "Autres";
    }
    return "?";
}

namespace ecologyimpl
{
PopulationGuild guildFor(const SpeciesDefinition& def)
{
    switch (def.sim.bodyPlan)
    {
    case BodyPlan::Fish: return PopulationGuild::Fish;
    case BodyPlan::BirdFlyer: return PopulationGuild::Birds;
    case BodyPlan::Invertebrate:
        return def.sim.diet.type == DietType::Herbivore || def.id.find("insect") != std::string::npos ? PopulationGuild::Insects : PopulationGuild::Molluscs;
    case BodyPlan::SmallReptile:
    case BodyPlan::QuadrupedSmallMammal: return PopulationGuild::SmallVertebrates;
    default: return PopulationGuild::Other;
    }
}

double waterVolume(const WaterSystem& w)
{
    double v = 0.0;
    for (size_t i = 0; i < w.lakeCount(); ++i)
    {
        v += w.lakeDepth(static_cast<int>(i));
    }
    v += w.riverStage(0) * 2.0;
    return std::max(0.01, v);
}
} // namespace ecologyimpl

void EcologySystem::build(const EvidenceDatabase& db, const std::vector<int>& speciesInWorld, const Terrain& terrain, const WaterSystem& water, u64 seed)
{
    pools_.clear();
    water_ = &water;
    terrain_ = &terrain;
    rng_.seed(seed, 0xEC0);
    for (const int si : speciesInWorld)
    {
        const SpeciesDefinition& def = db.speciesAt(si);
        if (def.sim.role != SimRole::Population)
        {
            continue;
        }
        PopulationPool p;
        p.speciesIndex = si;
        p.guild = ecologyimpl::guildFor(def);
        p.baseCapacity = std::max(1.0, static_cast<double>(def.sim.population.initialCount));
        p.capacity = p.baseCapacity;
        p.abundance = p.baseCapacity * 0.8;
        p.individualMassKg = std::max(1e-5f, def.sim.adultMassKg);
        // Intrinsic growth rate scales as M^-1/4 (metabolic theory; coefficient is a game assumption).
        p.growthPerDay = std::min(0.2f, 0.012f * std::pow(p.individualMassKg, -0.25f));
        pools_.push_back(p);
    }
    baseWaterVolume_ = ecologyimpl::waterVolume(water);
    baseVegetation_ = 1.0;
}

PopulationPool* EcologySystem::poolFor(PopulationGuild g)
{
    for (PopulationPool& p : pools_)
    {
        if (p.guild == g)
        {
            return &p;
        }
    }
    return nullptr;
}

const PopulationPool* EcologySystem::poolFor(PopulationGuild g) const
{
    for (const PopulationPool& p : pools_)
    {
        if (p.guild == g)
        {
            return &p;
        }
    }
    return nullptr;
}

void EcologySystem::dailyUpdate(WorldContext& ctx, const CreatureSystem& creatures)
{
    (void)creatures;
    const float temp = ctx.weather->state().temperatureC;
    const float thermal = saturate((temp - 5.0f) / 20.0f);
    const double vegNow = ctx.vegetation->totalBiomassKg();
    if (baseVegetation_ <= 1.0)
    {
        baseVegetation_ = std::max(1.0, vegNow);
    }
    const double vegRatio = vegNow / baseVegetation_;
    const double waterRatio = ecologyimpl::waterVolume(*ctx.water) / baseWaterVolume_;
    const PopulationPool* insects = poolFor(PopulationGuild::Insects);
    const double insectRatio = insects ? insects->abundance / std::max(1.0, insects->baseCapacity) : 1.0;

    for (PopulationPool& p : pools_)
    {
        double k = p.baseCapacity;
        switch (p.guild)
        {
        case PopulationGuild::Fish:
        {
            // Fish track water volume; drying ponds concentrate then kill fish (die-off).
            k = p.baseCapacity * std::min(1.2, waterRatio);
            if (p.abundance > k * 1.3)
            {
                const double dead = (p.abundance - k) * 0.3;
                p.abundance -= dead;
                p.diedOff += dead;
            }
            break;
        }
        case PopulationGuild::Insects: k = p.baseCapacity * std::min(1.5, vegRatio) * (0.3 + 0.7 * thermal); break;
        case PopulationGuild::Birds:
        case PopulationGuild::SmallVertebrates: k = p.baseCapacity * std::min(1.3, 0.5 + 0.5 * insectRatio) * std::min(1.2, vegRatio); break;
        case PopulationGuild::Molluscs: k = p.baseCapacity * std::min(1.1, waterRatio); break;
        case PopulationGuild::Other: break;
        }
        p.capacity = std::max(1.0, k);
        const double r = p.growthPerDay * (p.guild == PopulationGuild::Insects ? thermal : 1.0);
        p.abundance += r * p.abundance * (1.0 - p.abundance / p.capacity);
        p.abundance = std::max(p.baseCapacity * 0.01, p.abundance);
    }
    // Insect herbivory: a small daily fraction of foliage (leaf damage is documented on Hell Creek leaves).
    if (insects && ctx.vegetation)
    {
        insectActivity_ = static_cast<float>(saturate(static_cast<float>(insectRatio) * (0.3f + 0.7f * thermal)));
    }
    else
    {
        insectActivity_ = thermal;
    }
    ctx.insectActivity = insectActivity_;
}

void EcologySystem::sampleCensus(const WorldContext& ctx, const CreatureSystem& creatures)
{
    CensusSample s;
    s.time = ctx.now;
    s.agents.assign(creatures.species().size(), 0);
    for (const Creature& c : creatures.creatures())
    {
        if (c.alive)
        {
            ++s.agents[static_cast<size_t>(c.speciesIndex)];
        }
    }
    s.pools.assign(creatures.species().size(), 0.0);
    for (const PopulationPool& p : pools_)
    {
        s.pools[static_cast<size_t>(p.speciesIndex)] = p.abundance;
    }
    s.vegetationKg = ctx.vegetation->totalBiomassKg();
    s.temperatureC = ctx.weather->state().temperatureC;
    s.precipMm = static_cast<float>(ctx.weather->accumulatedPrecipMm());
    for (const Carcass& c : ctx.carcasses->all())
    {
        s.carcasses += c.active ? 1 : 0;
    }
    for (const Nest& n : ctx.nests->all())
    {
        s.nests += n.active ? 1 : 0;
    }
    s.footprints = static_cast<int>(ctx.tracks->count());
    history_.push_back(std::move(s));
    if (history_.size() > 4000)
    {
        history_.erase(history_.begin(), history_.begin() + 1000);
    }
}

float EcologySystem::availability(PopulationResource resource, const Vec2& p) const
{
    switch (resource)
    {
    case PopulationResource::Fish:
    {
        const PopulationPool* f = poolFor(PopulationGuild::Fish);
        if (!f || !water_ || !terrain_ || water_->depthAt(p, *terrain_) < 0.1f)
        {
            return 0.0f;
        }
        return static_cast<float>(saturate(static_cast<float>(f->abundance / std::max(1.0, f->capacity))));
    }
    case PopulationResource::Invertebrates:
    {
        const PopulationPool* i = poolFor(PopulationGuild::Insects);
        return i ? static_cast<float>(saturate(static_cast<float>(i->abundance / std::max(1.0, i->baseCapacity)))) * insectActivity_ : 0.0f;
    }
    case PopulationResource::SmallVertebrates:
    {
        const PopulationPool* s = poolFor(PopulationGuild::SmallVertebrates);
        return s ? static_cast<float>(saturate(static_cast<float>(s->abundance / std::max(1.0, s->baseCapacity)))) : 0.0f;
    }
    }
    return 0.0f;
}

float EcologySystem::harvest(PopulationResource resource, const Vec2& p, float kgWanted, u64 seed)
{
    PopulationGuild g = PopulationGuild::Fish;
    if (resource == PopulationResource::Invertebrates)
    {
        g = PopulationGuild::Insects;
    }
    else if (resource == PopulationResource::SmallVertebrates)
    {
        g = PopulationGuild::SmallVertebrates;
    }
    PopulationPool* pool = poolFor(g);
    if (!pool || kgWanted <= 0.0f)
    {
        return 0.0f;
    }
    const float avail = availability(resource, p);
    // Capture success is stochastic and density dependent (type II functional response shape).
    Rng r(seed, 0x4A57);
    const float success = avail / (avail + 0.3f);
    if (!r.chance(success))
    {
        return 0.0f;
    }
    const double maxTake = pool->abundance * pool->individualMassKg * 0.0005; // never more than 0.05 % of the stock per bite
    const float got = static_cast<float>(std::min(static_cast<double>(kgWanted), maxTake));
    pool->abundance = std::max(0.0, pool->abundance - got / pool->individualMassKg);
    pool->harvestedKg += got;
    return got;
}

float EcologySystem::insectChorus(bool night) const
{
    const PopulationPool* i = poolFor(PopulationGuild::Insects);
    if (!i)
    {
        return 0.0f;
    }
    return insectActivity_ * (night ? 1.0f : 0.4f);
}

float EcologySystem::birdActivity(float sunElevationDeg) const
{
    const PopulationPool* b = poolFor(PopulationGuild::Birds);
    if (!b)
    {
        return 0.0f;
    }
    const float dawn = std::exp(-square((sunElevationDeg - 3.0f) / 8.0f)); // dawn activity peak in extant birds
    return static_cast<float>(saturate(static_cast<float>(b->abundance / std::max(1.0, b->baseCapacity)))) * (0.3f + 0.7f * dawn) *
           (sunElevationDeg > -8.0f ? 1.0f : 0.1f);
}
} // namespace noctis
