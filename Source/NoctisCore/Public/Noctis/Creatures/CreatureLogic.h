// Per-individual creature logic. Each function only writes to the individual it is given
// (plus thread-safe sinks such as the sound field), so the CreatureSystem can run them in parallel.
#pragma once

#include "Noctis/Creatures/CreatureSystem.h"

namespace noctis
{
// Physiology: energy budget, digestion, hydration, fatigue, sleep, thermal balance, stress.
NOCTIS_API void updatePhysiology(Creature& c, const SpeciesRuntime& sp, const WorldContext& ctx, float dt);
// Injuries: bleeding, infection, healing, scarring, functional impairment, pain.
NOCTIS_API void updateInjuries(Creature& c, const SpeciesRuntime& sp, const WorldContext& ctx, float dt);
NOCTIS_API void recomputeImpairment(Creature& c, const SpeciesRuntime& sp);
// Growth towards the species' ontogenetic curve (called daily).
NOCTIS_API float massAtAge(const SpeciesRuntime& sp, float ageYears, float sizeGene);
NOCTIS_API void applyBodySize(Creature& c, const SpeciesRuntime& sp, float massKg);
// Annual mortality hazard (per year) excluding explicit causes (predation, starvation, injury).
NOCTIS_API float backgroundHazardPerYear(const SpeciesRuntime& sp, float ageYears, float densityRatio);

// Perception: vision, hearing, smell, vibration -> progressive awareness.
NOCTIS_API void perceive(Creature& c, const SpeciesRuntime& sp, const CreatureSystem& world, const WorldContext& ctx, float dtSince);
// Memory consolidation and decay.
NOCTIS_API void updateMemory(Creature& c, const SpeciesRuntime& sp, const WorldContext& ctx, float dt);
NOCTIS_API void rememberPlace(Creature& c, MemoryKind kind, const Vec2& pos, float valence, double now, EntityId about = kNoEntity);

// Utility-based decision making and behaviour execution (sets locomotion goals and intents).
NOCTIS_API void decide(Creature& c, const SpeciesRuntime& sp, const CreatureSystem& world, const WorldContext& ctx);
NOCTIS_API void executeBehavior(Creature& c, const SpeciesRuntime& sp, const CreatureSystem& world, const WorldContext& ctx, float dtSince);

// Physical locomotion with gait, footfalls, terrain and water interaction.
NOCTIS_API void integrateLocomotion(Creature& c, const SpeciesRuntime& sp, const WorldContext& ctx, float dt, bool withFeet);
// Secondary motion of soft tissues driven by body accelerations and footfall impulses.
NOCTIS_API void updateSoftTissue(Creature& c, const SpeciesRuntime& sp, float dt);
// Activity level 0..1 for the species' diel pattern given current light.
NOCTIS_API float circadianActivity(const SpeciesRuntime& sp, const WorldContext& ctx);
// Effective visual performance given illuminance and the species' low-light capability.
NOCTIS_API float visualPerformance(float illuminanceLux, float lowLight);
// Distance at which this individual starts to treat an approaching threat seriously (m).
NOCTIS_API float flightInitiationDistance(const Creature& c, const SpeciesRuntime& sp);
} // namespace noctis
