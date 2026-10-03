// Physical traces animals leave in the world: footprints, carcasses (with bite marks and
// weathering bones) and nests. These are the same kinds of evidence paleontologists work from,
// and the player's tools read them back with the same methods.
#pragma once

#include "Noctis/Core/Events.h"
#include "Noctis/Core/SpatialHash.h"
#include "Noctis/Science/Taxonomy.h"
#include "Noctis/World/Terrain.h"

#include <deque>
#include <vector>

namespace noctis
{
class VegetationSystem;

// ------------------------------------------------------------------ Footprints

struct Footprint
{
    u32 id = 0;
    Vec2 position;
    float heading = 0.0f;
    i16 speciesIndex = -1;
    EntityId maker;          // hidden truth: never shown directly to the player
    float lengthM = 0.2f;
    float widthM = 0.15f;
    float depthM = 0.0f;
    float sharpness = 1.0f;  // 1 = crisp, decays with rain, time, overprinting
    Substrate substrate = Substrate::Silt;
    bool left = true;
    bool manus = false;      // fore-foot impression (quadrupeds)
    double time = 0.0;
    float makerSpeedMs = 0.0f; // hidden truth used to evaluate the player's speed estimates
};

class NOCTIS_API TrackSystem
{
public:
    void configure(const Rect2& bounds, float bucketSizeM = 64.0f, int maxPerBucket = 160);
    // Records a footprint (serial phase). Overprints older ones under the new foot.
    void add(Footprint f);
    // Erosion by rain and time. Hourly.
    void weather(float dtHours, float rainMmPerHour);
    void query(const Vec2& center, float radius, std::vector<const Footprint*>& out) const;
    const Footprint* find(u32 id) const;
    size_t count() const;
    bool visible(const Footprint& f) const { return f.depthM > 0.004f && f.sharpness > 0.12f; }
    template <typename Fn>
    void forEach(Fn&& fn) const
    {
        for (const auto& b : buckets_)
        {
            for (const Footprint& f : b)
            {
                fn(f);
            }
        }
    }
    void clear();

private:
    int bucketIndex(const Vec2& p) const;
    Rect2 bounds_{};
    float bucketSize_ = 64.0f;
    int bucketsX_ = 1;
    int bucketsY_ = 1;
    int maxPerBucket_ = 160;
    u32 nextId_ = 1;
    std::vector<std::deque<Footprint>> buckets_;
};

// ------------------------------------------------------------------ Carcasses

enum class DecayStage : u8
{
    Fresh,
    EarlyDecomposition, // discolouration, bloat
    AdvancedDecomposition,
    Skeletonizing,
    Skeleton
};
NOCTIS_API const char* decayStageLabelFr(DecayStage s);

enum class BiteMarkType : u8
{
    Pit,
    Score,
    Puncture,
    Furrow
};

enum class DeathCause : u8
{
    Unknown,
    Predation,
    Injury,
    Starvation,
    Dehydration,
    Senescence,
    Infection,
    Drowning,
    Fire,
    Exposure,
    Researcher // the player's own impact is recorded honestly
};
NOCTIS_API const char* deathCauseLabelFr(DeathCause c);

struct BiteMark
{
    i16 makerSpecies = -1;
    BiteMarkType type = BiteMarkType::Pit;
    u8 bodyRegion = 0;
    float toothSpacingMm = 0.0f;
    float depthMm = 0.0f;
    double time = 0.0;
    bool perimortem = true; // made at/after death (feeding) vs. healed (survived attack)
};

struct Carcass
{
    EntityId id;
    i16 speciesIndex = -1;
    EntityId individual;
    Vec2 position;
    float heading = 0.0f;
    float massAtDeathKg = 0.0f;
    float softTissueKg = 0.0f;
    float boneKg = 0.0f;
    double timeOfDeath = 0.0;
    double skeletonSince = -1.0;
    float accumulatedDegreeDays = 0.0f;
    DecayStage stage = DecayStage::Fresh;
    DeathCause cause = DeathCause::Unknown;
    float ageYearsAtDeath = 0.0f;
    float scatter = 0.0f;       // 0 articulated .. 1 fully disarticulated/scattered
    int weatheringStage = 0;    // Behrensmeyer 0..5
    float burial = 0.0f;        // 0 exposed .. 1 buried (floods, sediment)
    std::vector<BiteMark> marks;
    std::vector<i16> feederSpecies;
    int shedTeeth = 0;          // teeth lost by feeding theropods (found around feeding sites)
    bool active = true;
};

class NOCTIS_API CarcassSystem
{
public:
    EntityId create(i16 species, EntityId individual, const Vec2& pos, float heading, float massKg, double time, DeathCause cause,
                    float ageYears);
    // Hourly: degree-day driven decomposition (Megyesi et al. 2005 relation used as an analogue),
    // nutrient release, odour, bone weathering after skeletonisation, burial during floods.
    void update(double now, float dtHours, float airTempC, bool flooding, VegetationSystem& vegetation, EventBus& events);
    // Feeding (serial phase). Returns kg of soft tissue actually removed.
    float feed(EntityId id, float kgWanted, i16 feederSpecies, double now);
    void addBiteMark(EntityId id, const BiteMark& mark);
    void addShedTooth(EntityId id) { if (Carcass* c = findMutable(id)) { ++c->shedTeeth; } }

    const Carcass* find(EntityId id) const;
    Carcass* findMutable(EntityId id);
    const std::vector<Carcass>& all() const { return carcasses_; }
    void queryNear(const Vec2& p, float radius, std::vector<EntityId>& out) const;
    // Odour source strength (relative units, 0 when nothing left to smell).
    static float odourStrength(const Carcass& c);
    // Total body score after Megyesi et al. 2005, inverted from ADD.
    static float totalBodyScore(float accumulatedDegreeDays);

    std::vector<Carcass>& mutableAll() { return carcasses_; }
    void restoreNextSerial(u32 s) { nextSerial_ = s; }
    u32 nextSerial() const { return nextSerial_; }

private:
    std::vector<Carcass> carcasses_;
    u32 nextSerial_ = 1;
};

// ------------------------------------------------------------------ Nests

struct Egg
{
    EntityId mother;
    EntityId father;
    float development = 0.0f; // 0..1
    bool viable = true;
    bool present = true;      // false once eaten/hatched
    u64 geneticSeed = 0;
};

struct Nest
{
    EntityId id;
    i16 speciesIndex = -1;
    Vec2 position;
    NestType type = NestType::OpenScrape;
    EntityId mother;
    std::vector<Egg> eggs;
    double layTime = 0.0;
    float temperatureC = 20.0f;
    float attendance = 0.0f; // recent parental presence 0..1
    int predated = 0;
    int hatched = 0;
    bool active = true;
};

struct HatchRecord
{
    EntityId nest;
    i16 speciesIndex = -1;
    Vec2 position;
    Egg egg;
};

class NOCTIS_API NestSystem
{
public:
    EntityId create(i16 species, NestType type, EntityId mother, const Vec2& pos, double time);
    void addEgg(EntityId nest, const Egg& egg);
    // Hourly: incubation temperature by nest type, development, failure, hatching.
    // incubationDays is looked up per species through the callback.
    template <typename IncubationFn>
    void update(double now, float dtHours, float airTempC, float soilTempC, IncubationFn incubationDaysFor, std::vector<HatchRecord>& hatched,
                EventBus& events);
    void markAttended(EntityId nest, float amount);
    int predate(EntityId nest, int maxEggs, double now, EventBus& events, EntityId predator);
    const Nest* find(EntityId id) const;
    Nest* findMutable(EntityId id);
    const std::vector<Nest>& all() const { return nests_; }
    std::vector<Nest>& mutableAll() { return nests_; }
    void queryNear(const Vec2& p, float radius, std::vector<EntityId>& out) const;
    u32 nextSerial() const { return nextSerial_; }
    void restoreNextSerial(u32 s) { nextSerial_ = s; }

private:
    std::vector<Nest> nests_;
    u32 nextSerial_ = 1;
};

template <typename IncubationFn>
void NestSystem::update(double now, float dtHours, float airTempC, float soilTempC, IncubationFn incubationDaysFor, std::vector<HatchRecord>& hatched,
                        EventBus& events)
{
    for (Nest& n : nests_)
    {
        if (!n.active)
        {
            continue;
        }
        // Incubation microclimate (inferred; see species reproduction claims):
        // mound nests gain heat from decomposing vegetation, buried clutches follow soil temperature,
        // brooded clutches approach a contact-incubation temperature when attended.
        float t = airTempC;
        switch (n.type)
        {
        case NestType::Mound: t = soilTempC + 4.0f; break;
        case NestType::Buried: t = soilTempC; break;
        case NestType::Brooded: t = lerpf(airTempC, 32.0f, n.attendance); break;
        case NestType::OpenScrape: t = 0.5f * (airTempC + soilTempC) + 2.0f * n.attendance; break;
        default: break;
        }
        n.temperatureC = t;
        // Thermal performance of development (game assumption): Q10-like around a 30 degC reference,
        // no development below 15 degC, lethal above 40 degC.
        const float rel = t < 15.0f ? 0.0f : std::pow(2.0f, (std::min(t, 34.0f) - 30.0f) / 10.0f);
        const float days = std::max(1.0f, incubationDaysFor(n.speciesIndex));
        const float increment = rel * dtHours / (days * 24.0f);
        bool anyLeft = false;
        for (Egg& e : n.eggs)
        {
            if (!e.present)
            {
                continue;
            }
            if (t > 40.0f || (n.type == NestType::OpenScrape && t < 5.0f))
            {
                e.viable = false;
            }
            if (e.viable)
            {
                e.development += increment;
            }
            if (e.viable && e.development >= 1.0f)
            {
                e.present = false;
                ++n.hatched;
                hatched.push_back({n.id, n.speciesIndex, n.position, e});
                SimEvent ev;
                ev.type = EventType::Hatch;
                ev.time = now;
                ev.subject = n.id;
                ev.other = e.mother;
                ev.position = Vec3{n.position, 0.0f};
                events.emit(ev);
                continue;
            }
            anyLeft = true;
        }
        n.attendance = std::max(0.0f, n.attendance - 0.05f * dtHours);
        // Inviable clutches eventually stop being "nests" but the site keeps its eggshell record.
        const double ageDays = (now - n.layTime) / 86400.0;
        if (!anyLeft || ageDays > static_cast<double>(incubationDaysFor(n.speciesIndex)) * 2.0)
        {
            n.active = false;
        }
    }
}
} // namespace noctis
