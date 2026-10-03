// Species definition: the scientific record (shown in the field encyclopedia) and the
// simulation parameters (consumed by the systems), each parameter traceable to its basis.
#pragma once

#include "Noctis/Science/Confidence.h"
#include "Noctis/Science/Taxonomy.h"

#include <string>
#include <utility>
#include <vector>

namespace noctis
{
struct TraitDistribution
{
    float mean = 0.5f;
    float sd = 0.15f;
};

struct CallTypeSpec
{
    std::string id;
    CallContext context = CallContext::Other;
    std::string contextText;
    float durationS = 1.0f;
    float f0Hz = 100.0f;
    float sourceLevelDb = 80.0f;
    Confidence confidence = Confidence::Speculative;
};

struct SpeciesSim
{
    SimRole role = SimRole::Agent;
    BodyPlan bodyPlan = BodyPlan::BipedSmallOrnithischian;
    float adultMassKg = 100.0f;
    float adultLengthM = 3.0f;
    float hipHeightM = 1.0f;
    float footLengthM = 0.25f;
    float footWidthM = 0.2f;
    float hatchlingMassKg = 0.5f;
    float sexualDimorphismMass = 1.0f;

    struct Growth
    {
        float asymptoticMassKg = 100.0f;
        float maxGrowthRateKgPerYear = 50.0f;
        float inflectionAgeYears = 4.0f;
        float maturityAgeYears = 5.0f;
        float maxLifespanYears = 20.0f;
    } growth;

    struct Locomotion
    {
        float walkSpeedMs = 1.2f;
        float maxSpeedMs = 5.0f;
        float maxAccelMs2 = 2.0f;
        float maxDecelMs2 = 3.0f;
        float maxYawRateDps = 90.0f;
        float maxSlopeDeg = 30.0f;
        bool swim = false;
        float swimSpeedMs = 0.0f;
        bool fly = false;
        float flySpeedMs = 0.0f;
        float dutyFactorWalk = 0.65f;
        float dutyFactorRun = 0.45f;
    } locomotion;

    struct Metabolism
    {
        ThermoClass thermo = ThermoClass::Endotherm;
        float maxFastingDays = 30.0f;
        float waterLPerDayPer100kg = 3.0f;
    } metabolism;

    struct Diet
    {
        DietType type = DietType::Herbivore;
        float plantPreference[kPlantTypeCount] = {};
        float browseMinM = 0.0f;
        float browseMaxM = 1.0f;
        float preyMassMinKg = 0.0f;
        float preyMassMaxKg = 0.0f;
        bool scavenges = false;
        bool eatsFish = false;
        bool eatsInvertebrates = false;
        bool eatsEggs = false;
    } diet;

    struct Senses
    {
        float visionRangeM = 500.0f;
        float fovDeg = 270.0f;
        float binocularDeg = 30.0f;
        float lowLight = 0.3f;
        float hearingMinHz = 50.0f;
        float hearingMaxHz = 3000.0f;
        float hearingBestHz = 800.0f;
        float hearingThresholdDb = 20.0f;
        float olfaction = 0.5f;
        float vibration = 0.2f;
    } senses;

    struct Social
    {
        float groupSizeMean = 1.0f;
        float groupSizeMax = 1.0f;
        float cohesion = 0.1f;
        bool territorial = false;
        float territoryRadiusM = 0.0f;
        bool alarmCalls = false;
    } social;

    struct Defense
    {
        u16 weapons = 0;
        float armor = 0.0f;
        float defenseStrength = 0.2f;
        float fleePreference = 0.8f;
    } defense;

    struct Predation
    {
        HuntStyle style = HuntStyle::None;
        float attackStrength = 0.0f;
        float biteForceN = 0.0f;
        float maxChaseS = 0.0f;
    } predation;

    struct Reproduction
    {
        float clutchSizeMean = 10.0f;
        float clutchSizeMax = 20.0f;
        float incubationDays = 90.0f;
        float breedingStartDoy = 90.0f;
        float breedingEndDoy = 150.0f;
        float parentalCare = 0.3f;
        NestType nest = NestType::OpenScrape;
    } reproduction;

    struct Vocal
    {
        bool closedMouth = true;
        float f0MinHz = 50.0f;
        float f0MaxHz = 500.0f;
        std::vector<CallTypeSpec> calls;
    } vocal;

    ActivityPattern activity = ActivityPattern::Diurnal;
    TraitDistribution personality[kPersonalityAxisCount];
    float habitat[kHabitatCount] = {};

    struct Population
    {
        float initialCount = 0.0f;
        float regionalDensityPerKm2 = 0.0f;
    } population;

    bool hasWeapon(Weapon w) const { return (defense.weapons & static_cast<u16>(w)) != 0; }
    const CallTypeSpec* findCall(CallContext c) const
    {
        for (const CallTypeSpec& call : vocal.calls)
        {
            if (call.context == c)
            {
                return &call;
            }
        }
        return nullptr;
    }
};

struct SpeciesDefinition
{
    std::string id;
    std::string reconstructionVersion;
    std::string lastReviewed;
    std::string scientificName;
    std::string authority;
    std::string commonNameFr;
    std::string commonNameEn;
    std::string catalogPrefix; // e.g. "TRX" — used for the player's individual catalogue codes
    std::vector<std::string> regions;
    std::vector<std::pair<std::string, Claim>> science;
    std::vector<CompetingHypothesis> competing;
    SpeciesSim sim;
    std::vector<std::pair<std::string, std::string>> simBasis;
    std::string sourceFile;

    const Claim* claim(const std::string& topic) const
    {
        for (const auto& c : science)
        {
            if (c.first == topic)
            {
                return &c.second;
            }
        }
        return nullptr;
    }
    // Returns the recorded basis, or "game_assumption: non documenté" when absent.
    std::string basisFor(const std::string& path) const
    {
        for (const auto& b : simBasis)
        {
            if (b.first == path)
            {
                return b.second;
            }
        }
        return "game_assumption: non documenté dans sim_basis";
    }
    bool isAssumption(const std::string& path) const { return basisFor(path).rfind("game_assumption", 0) == 0; }
};

// Science topics that every species file must contain (see Data/SCHEMA.md).
NOCTIS_API const std::vector<std::string>& requiredSpeciesTopics();
} // namespace noctis
