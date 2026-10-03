#include "Noctis/World/Traces.h"

#include "Noctis/World/Vegetation.h"

#include <algorithm>
#include <cmath>

namespace noctis
{
// ------------------------------------------------------------------ Footprints

void TrackSystem::configure(const Rect2& bounds, float bucketSizeM, int maxPerBucket)
{
    bounds_ = bounds;
    bucketSize_ = bucketSizeM;
    maxPerBucket_ = maxPerBucket;
    bucketsX_ = std::max(1, static_cast<int>(std::ceil(bounds.size().x / bucketSizeM)));
    bucketsY_ = std::max(1, static_cast<int>(std::ceil(bounds.size().y / bucketSizeM)));
    buckets_.assign(static_cast<size_t>(bucketsX_) * static_cast<size_t>(bucketsY_), {});
}

int TrackSystem::bucketIndex(const Vec2& p) const
{
    int bx = static_cast<int>((p.x - bounds_.min.x) / bucketSize_);
    int by = static_cast<int>((p.y - bounds_.min.y) / bucketSize_);
    bx = std::clamp(bx, 0, bucketsX_ - 1);
    by = std::clamp(by, 0, bucketsY_ - 1);
    return by * bucketsX_ + bx;
}

void TrackSystem::add(Footprint f)
{
    if (buckets_.empty() || f.depthM < 0.002f)
    {
        return;
    }
    f.id = nextId_++;
    std::deque<Footprint>& b = buckets_[static_cast<size_t>(bucketIndex(f.position))];
    // Overprinting degrades footprints under the new foot.
    const float r2 = square(std::max(f.lengthM, 0.1f) * 0.6f);
    for (Footprint& old : b)
    {
        if (distanceSq(old.position, f.position) < r2)
        {
            old.sharpness *= 0.35f;
        }
    }
    b.push_back(f);
    while (static_cast<int>(b.size()) > maxPerBucket_)
    {
        // Evict the least legible footprint first, oldest on ties.
        auto worst = std::min_element(b.begin(), b.end(), [](const Footprint& a, const Footprint& c) {
            return a.sharpness * a.depthM < c.sharpness * c.depthM;
        });
        b.erase(worst);
    }
}

void TrackSystem::weather(float dtHours, float rainMmPerHour)
{
    for (std::deque<Footprint>& b : buckets_)
    {
        for (Footprint& f : b)
        {
            const SoilMechanics soil = soilMechanics(f.substrate, 0.6f);
            const float retention = std::max(0.05f, soil.trackRetention);
            const float erosion = (0.0015f + 0.04f * rainMmPerHour) * dtHours / retention;
            f.sharpness = std::max(0.0f, f.sharpness - erosion * 0.6f);
            f.depthM *= std::exp(-0.015f * rainMmPerHour * dtHours / retention - 0.0004f * dtHours);
        }
        b.erase(std::remove_if(b.begin(), b.end(), [](const Footprint& f) { return f.sharpness < 0.03f || f.depthM < 0.002f; }), b.end());
    }
}

void TrackSystem::query(const Vec2& center, float radius, std::vector<const Footprint*>& out) const
{
    if (buckets_.empty())
    {
        return;
    }
    const int bx0 = std::clamp(static_cast<int>((center.x - radius - bounds_.min.x) / bucketSize_), 0, bucketsX_ - 1);
    const int by0 = std::clamp(static_cast<int>((center.y - radius - bounds_.min.y) / bucketSize_), 0, bucketsY_ - 1);
    const int bx1 = std::clamp(static_cast<int>((center.x + radius - bounds_.min.x) / bucketSize_), 0, bucketsX_ - 1);
    const int by1 = std::clamp(static_cast<int>((center.y + radius - bounds_.min.y) / bucketSize_), 0, bucketsY_ - 1);
    const float r2 = radius * radius;
    for (int by = by0; by <= by1; ++by)
    {
        for (int bx = bx0; bx <= bx1; ++bx)
        {
            for (const Footprint& f : buckets_[static_cast<size_t>(by * bucketsX_ + bx)])
            {
                if (distanceSq(f.position, center) <= r2)
                {
                    out.push_back(&f);
                }
            }
        }
    }
}

const Footprint* TrackSystem::find(u32 id) const
{
    for (const auto& b : buckets_)
    {
        for (const Footprint& f : b)
        {
            if (f.id == id)
            {
                return &f;
            }
        }
    }
    return nullptr;
}

size_t TrackSystem::count() const
{
    size_t n = 0;
    for (const auto& b : buckets_)
    {
        n += b.size();
    }
    return n;
}

void TrackSystem::clear()
{
    for (auto& b : buckets_)
    {
        b.clear();
    }
}

// ------------------------------------------------------------------ Carcasses

const char* decayStageLabelFr(DecayStage s)
{
    switch (s)
    {
    case DecayStage::Fresh: return "Frais";
    case DecayStage::EarlyDecomposition: return "Décomposition précoce";
    case DecayStage::AdvancedDecomposition: return "Décomposition avancée";
    case DecayStage::Skeletonizing: return "Squelettisation";
    case DecayStage::Skeleton: return "Squelette";
    }
    return "?";
}

const char* deathCauseLabelFr(DeathCause c)
{
    switch (c)
    {
    case DeathCause::Unknown: return "Inconnue";
    case DeathCause::Predation: return "Prédation";
    case DeathCause::Injury: return "Blessures";
    case DeathCause::Starvation: return "Inanition";
    case DeathCause::Dehydration: return "Déshydratation";
    case DeathCause::Senescence: return "Sénescence";
    case DeathCause::Infection: return "Infection";
    case DeathCause::Drowning: return "Noyade";
    case DeathCause::Fire: return "Feu";
    case DeathCause::Exposure: return "Conditions climatiques";
    case DeathCause::Researcher: return "Intervention humaine";
    }
    return "?";
}

float CarcassSystem::totalBodyScore(float add)
{
    // Megyesi et al. 2005: ADD = 10^(0.002·TBS² + 1.81)  ->  TBS = sqrt((log10(ADD) − 1.81) / 0.002)
    const float l = std::log10(std::max(1.0f, add));
    if (l <= 1.81f)
    {
        return 3.0f;
    }
    return clampf(std::sqrt((l - 1.81f) / 0.002f), 3.0f, 35.0f);
}

float CarcassSystem::odourStrength(const Carcass& c)
{
    if (!c.active)
    {
        return 0.0f;
    }
    float stageFactor = 0.0f;
    switch (c.stage)
    {
    case DecayStage::Fresh: stageFactor = 0.25f; break;
    case DecayStage::EarlyDecomposition: stageFactor = 1.0f; break;
    case DecayStage::AdvancedDecomposition: stageFactor = 1.3f; break;
    case DecayStage::Skeletonizing: stageFactor = 0.4f; break;
    case DecayStage::Skeleton: stageFactor = 0.02f; break;
    }
    return stageFactor * std::sqrt(std::max(0.0f, c.softTissueKg) + 1.0f) * (1.0f - c.burial);
}

EntityId CarcassSystem::create(i16 species, EntityId individual, const Vec2& pos, float heading, float massKg, double time, DeathCause cause,
                               float ageYears)
{
    Carcass c;
    c.id = EntityId::make(EntityKind::Carcass, nextSerial_++);
    c.speciesIndex = species;
    c.individual = individual;
    c.position = pos;
    c.heading = heading;
    c.massAtDeathKg = massKg;
    // Skeletal mass fraction ~ 0.1 of body mass in terrestrial vertebrates (game assumption, order of magnitude).
    c.boneKg = massKg * 0.1f;
    c.softTissueKg = massKg - c.boneKg;
    c.timeOfDeath = time;
    c.cause = cause;
    c.ageYearsAtDeath = ageYears;
    carcasses_.push_back(c);
    return c.id;
}

void CarcassSystem::update(double now, float dtHours, float airTempC, bool flooding, VegetationSystem& vegetation, EventBus& events)
{
    for (Carcass& c : carcasses_)
    {
        if (!c.active)
        {
            continue;
        }
        const float dtDays = dtHours / 24.0f;
        const float tbsBefore = totalBodyScore(c.accumulatedDegreeDays);
        c.accumulatedDegreeDays += std::max(0.0f, airTempC) * dtDays;
        const float tbs = totalBodyScore(c.accumulatedDegreeDays);
        // Microbial + insect removal follows the TBS progression (fraction of soft tissue lost).
        const float lostFracBefore = smoothstep(6.0f, 30.0f, tbsBefore);
        const float lostFrac = smoothstep(6.0f, 30.0f, tbs);
        if (lostFrac > lostFracBefore && c.softTissueKg > 0.0f)
        {
            const float remainingShare = std::max(1e-3f, 1.0f - lostFracBefore);
            const float decomposed = std::min(c.softTissueKg, c.softTissueKg * (lostFrac - lostFracBefore) / remainingShare);
            c.softTissueKg -= decomposed;
            vegetation.addNutrients(c.position, decomposed * 0.3f);
        }
        DecayStage stage = DecayStage::Fresh;
        if (tbs > 30.0f || c.softTissueKg < c.massAtDeathKg * 0.01f)
        {
            stage = DecayStage::Skeleton;
        }
        else if (tbs > 22.0f || c.softTissueKg < c.massAtDeathKg * 0.15f)
        {
            stage = DecayStage::Skeletonizing;
        }
        else if (tbs > 14.0f)
        {
            stage = DecayStage::AdvancedDecomposition;
        }
        else if (tbs > 6.0f)
        {
            stage = DecayStage::EarlyDecomposition;
        }
        if (stage == DecayStage::Skeleton && c.skeletonSince < 0.0)
        {
            c.skeletonSince = now;
            SimEvent e;
            e.type = EventType::CarcassDepleted;
            e.time = now;
            e.subject = c.id;
            e.position = Vec3{c.position, 0.0f};
            events.emit(e);
        }
        c.stage = stage;
        // Bone weathering stages (Behrensmeyer 1978 timescales, used as a modern analogue).
        if (c.skeletonSince >= 0.0 && c.burial < 0.9f)
        {
            const double years = (now - c.skeletonSince) / (365.25 * 86400.0);
            c.weatheringStage = years < 1.0 ? 0 : years < 3.0 ? 1 : years < 6.0 ? 2 : years < 10.0 ? 3 : years < 15.0 ? 4 : 5;
        }
        if (flooding)
        {
            c.burial = saturate(c.burial + 0.02f * dtHours);
        }
        // Long-buried skeletons leave the active simulation but stay recorded for paleontology.
        if (c.burial >= 1.0f && c.stage == DecayStage::Skeleton)
        {
            c.active = false;
        }
    }
}

float CarcassSystem::feed(EntityId id, float kgWanted, i16 feederSpecies, double now)
{
    Carcass* c = findMutable(id);
    if (!c || !c->active || c->softTissueKg <= 0.0f)
    {
        return 0.0f;
    }
    // Late-stage carcasses are less edible for most consumers.
    const float stageFactor = c->stage == DecayStage::Skeletonizing ? 0.3f : (c->stage == DecayStage::Skeleton ? 0.0f : 1.0f);
    const float taken = std::min(kgWanted * stageFactor, c->softTissueKg);
    c->softTissueKg -= taken;
    c->scatter = saturate(c->scatter + taken / std::max(1.0f, c->massAtDeathKg) * 0.8f);
    if (std::find(c->feederSpecies.begin(), c->feederSpecies.end(), feederSpecies) == c->feederSpecies.end())
    {
        c->feederSpecies.push_back(feederSpecies);
    }
    (void)now;
    return taken;
}

void CarcassSystem::addBiteMark(EntityId id, const BiteMark& mark)
{
    if (Carcass* c = findMutable(id))
    {
        if (c->marks.size() < 64)
        {
            c->marks.push_back(mark);
        }
    }
}

const Carcass* CarcassSystem::find(EntityId id) const
{
    for (const Carcass& c : carcasses_)
    {
        if (c.id == id)
        {
            return &c;
        }
    }
    return nullptr;
}

Carcass* CarcassSystem::findMutable(EntityId id)
{
    for (Carcass& c : carcasses_)
    {
        if (c.id == id)
        {
            return &c;
        }
    }
    return nullptr;
}

void CarcassSystem::queryNear(const Vec2& p, float radius, std::vector<EntityId>& out) const
{
    const float r2 = radius * radius;
    for (const Carcass& c : carcasses_)
    {
        if (c.active && distanceSq(c.position, p) <= r2)
        {
            out.push_back(c.id);
        }
    }
}

// ------------------------------------------------------------------ Nests

EntityId NestSystem::create(i16 species, NestType type, EntityId mother, const Vec2& pos, double time)
{
    Nest n;
    n.id = EntityId::make(EntityKind::Nest, nextSerial_++);
    n.speciesIndex = species;
    n.type = type;
    n.mother = mother;
    n.position = pos;
    n.layTime = time;
    nests_.push_back(n);
    return n.id;
}

void NestSystem::addEgg(EntityId nest, const Egg& egg)
{
    if (Nest* n = findMutable(nest))
    {
        n->eggs.push_back(egg);
    }
}

void NestSystem::markAttended(EntityId nest, float amount)
{
    if (Nest* n = findMutable(nest))
    {
        n->attendance = saturate(n->attendance + amount);
    }
}

int NestSystem::predate(EntityId nest, int maxEggs, double now, EventBus& events, EntityId predator)
{
    Nest* n = findMutable(nest);
    if (!n || !n->active)
    {
        return 0;
    }
    int eaten = 0;
    for (Egg& e : n->eggs)
    {
        if (eaten >= maxEggs)
        {
            break;
        }
        if (e.present)
        {
            e.present = false;
            ++eaten;
        }
    }
    n->predated += eaten;
    if (eaten > 0)
    {
        SimEvent ev;
        ev.type = EventType::NestPredated;
        ev.time = now;
        ev.subject = predator;
        ev.other = n->id;
        ev.position = Vec3{n->position, 0.0f};
        ev.magnitude = static_cast<float>(eaten);
        events.emit(ev);
    }
    return eaten;
}

const Nest* NestSystem::find(EntityId id) const
{
    for (const Nest& n : nests_)
    {
        if (n.id == id)
        {
            return &n;
        }
    }
    return nullptr;
}

Nest* NestSystem::findMutable(EntityId id)
{
    for (Nest& n : nests_)
    {
        if (n.id == id)
        {
            return &n;
        }
    }
    return nullptr;
}

void NestSystem::queryNear(const Vec2& p, float radius, std::vector<EntityId>& out) const
{
    const float r2 = radius * radius;
    for (const Nest& n : nests_)
    {
        if (n.active && distanceSq(n.position, p) <= r2)
        {
            out.push_back(n.id);
        }
    }
}
} // namespace noctis
