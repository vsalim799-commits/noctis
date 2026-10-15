// Compile-time guarantees that the Blueprint mirrors match the core enumerations value for value.
#include "NoctisTypes.h"

#include "Noctis/Creatures/Creature.h"
#include "Noctis/Research/FieldGear.h"
#include "Noctis/Research/Hypothesis.h"
#include "Noctis/World/Atmosphere.h"
#include "Noctis/World/Terrain.h"

static_assert(static_cast<int32>(ENoctisBehavior::Count) == noctis::kBehaviorCount, "ENoctisBehavior out of sync with noctis::BehaviorId");
static_assert(static_cast<int32>(ENoctisBehavior::Fight) == static_cast<int32>(noctis::BehaviorId::Fight), "ENoctisBehavior order");
static_assert(static_cast<int32>(ENoctisBehavior::Hunt) == static_cast<int32>(noctis::BehaviorId::Hunt), "ENoctisBehavior order");
static_assert(static_cast<int32>(ENoctisGait::Fly) == static_cast<int32>(noctis::Gait::Fly), "ENoctisGait out of sync");
static_assert(static_cast<int32>(ENoctisPosture::Swimming) == static_cast<int32>(noctis::Posture::Swimming), "ENoctisPosture out of sync");
static_assert(static_cast<int32>(ENoctisLod::Regional) == static_cast<int32>(noctis::LodLevel::Regional), "ENoctisLod out of sync");
static_assert(static_cast<int32>(ENoctisWeatherRegime::Thunderstorm) + 1 == static_cast<int32>(noctis::WeatherRegime::Count), "ENoctisWeatherRegime out of sync");
static_assert(static_cast<int32>(ENoctisSubstrate::Peat) + 1 == noctis::kSubstrateCount, "ENoctisSubstrate out of sync");
static_assert(static_cast<int32>(ENoctisDroneMode::Lost) == static_cast<int32>(noctis::DroneMode::Lost), "ENoctisDroneMode out of sync");
static_assert(static_cast<int32>(ENoctisHypothesisStatus::Inconclusive) == static_cast<int32>(noctis::HypothesisStatus::Inconclusive),
			  "ENoctisHypothesisStatus out of sync");
static_assert(noctis::kSoftTissueNodeCount == 6, "FNoctisCreatureAnimInputs exposes six soft-tissue channels");
