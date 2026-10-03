// Derived per-species constants used by the creature systems (computed once from the data files).
#pragma once

#include "Noctis/Creatures/Creature.h"
#include "Noctis/Science/EvidenceDatabase.h"

namespace noctis
{
struct RegionInfo
{
    float massFraction = 0.1f;
    float locomotion = 0.0f;  // contribution to locomotor capacity
    float feeding = 0.0f;
    float defense = 0.0f;
    float armor = 0.0f;       // 0..1 damage reduction
    float lethality = 0.3f;   // how much damage here threatens life
};

struct SoftTissueSpec
{
    float naturalHz = 2.0f;
    float damping = 0.3f;     // damping ratio
    float gain = 0.05f;       // response to body acceleration (m per m/s^2 or rad per m/s^2)
};

struct SpeciesRuntime
{
    int index = -1;
    const SpeciesDefinition* def = nullptr;
    bool biped = true;
    int legCount = 2;
    bool isMammal = false;
    bool isDinosaur = true;
    bool hasTeeth = false;          // replaces/sheds teeth while feeding (theropods, crocodylians)
    std::array<RegionInfo, kBodyRegionCount> regions{};
    std::array<Vec2, 4> footOffsets{}; // body frame, fraction of body length (x forward, y left)
    std::array<float, 4> walkPhase{};
    std::array<float, 4> runPhase{};
    std::array<bool, 4> footIsFore{};
    std::array<bool, 4> footIsLeft{};
    std::array<SoftTissueSpec, kSoftTissueNodeCount> soft{};
    float yawInertiaK = 0.06f;      // I = k m L^2
    float headReach = 0.45f;        // snout distance ahead of COM, fraction of length
    float bodyRadius = 0.12f;       // collision radius, fraction of length
    float eyeHeightFactor = 1.3f;   // eye height / hip height
    float footAreaFill = 0.55f;     // fraction of foot bounding box in contact
    // Metabolism (field metabolic rate, Nagy 2005: FMR[kJ/day] = a * M[g]^b).
    float fmrA = 10.5f;
    float fmrB = 0.681f;
    float fmrMesoBlend = 0.0f;      // 0 = endotherm equation, 1 = reptile equation
    float targetBodyTempC = 38.0f;
    float assimilatedMJPerKgPlant = 7.0f;
    float assimilatedMJPerKgMeat = 6.0f;
    float gutCapacityFraction = 0.04f;
    float gutRetentionHours = 30.0f;
    float waterBufferFraction = 0.12f;
    float toothSpacingMmPerM = 2.0f;
    float meanAdultMassKg = 1.0f;

    const SpeciesSim& sim() const { return def->sim; }
};

NOCTIS_API SpeciesRuntime buildSpeciesRuntime(const SpeciesDefinition& def, int index);

// Field metabolic rate in watts for a given mass (kg) with the species' thermoregulatory model.
NOCTIS_API float fieldMetabolicRateW(const SpeciesRuntime& sp, float massKg);
// Resting metabolic rate in watts (fraction of FMR; game assumption 0.4 of FMR).
inline float restingMetabolicRateW(const SpeciesRuntime& sp, float massKg) { return 0.4f * fieldMetabolicRateW(sp, massKg); }
} // namespace noctis
