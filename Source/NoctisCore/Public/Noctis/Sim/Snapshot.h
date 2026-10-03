// Render snapshot: the per-frame state a renderer (Unreal) needs, copied out of the simulation so
// rendering never reads simulation memory while it is being updated on worker threads.
#pragma once

#include "Noctis/Creatures/Creature.h"
#include "Noctis/World/Atmosphere.h"
#include "Noctis/World/SoundField.h"
#include "Noctis/World/Water.h"

#include <array>
#include <vector>

namespace noctis
{
struct CreatureRenderState
{
    u32 id = 0;
    i16 speciesIndex = -1;
    LodLevel lod = LodLevel::Far;
    Vec3 position;
    float heading = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float scale = 1.0f;       // body length / adult length of the species
    float speed = 0.0f;
    Gait gait = Gait::Stand;
    float gaitPhase = 0.0f;
    float strideLengthM = 0.0f;
    float dutyFactor = 0.7f;
    float bodyBob = 0.0f;
    u8 footCount = 2;
    std::array<Vec3, 4> footTargets{};
    std::array<u8, 4> footContact{};
    std::array<float, kSoftTissueNodeCount> softTissue{};
    std::array<float, 4> muscle{};
    float breath = 0.0f;
    Vec2 lookTarget;
    bool hasLookTarget = false;
    Posture posture = Posture::Normal;
    BehaviorId behavior = BehaviorId::Rest;
    bool vocalizing = false;
    float limp = 0.0f;
    float limpSide = 0.0f;
    u16 scarCount = 0;
    float markingSeed = 0.0f;
    float condition = 1.0f;
    float wetness = 0.0f;     // water depth relative to hip height (for wet skin shading)
    float mud = 0.0f;         // mud coating from recent sinkage
};

struct CarcassRenderState
{
    u32 id = 0;
    i16 speciesIndex = -1;
    Vec3 position;
    float heading = 0.0f;
    float softTissueFraction = 1.0f;
    u8 stage = 0;
    float scatter = 0.0f;
    float scale = 1.0f;
};

struct RenderSnapshot
{
    double time = 0.0;
    float hourOfDay = 12.0f;
    float dayOfYear = 0.0f;
    SkyState sky;
    WeatherState weather;
    float riverStageM = 0.0f;
    bool flooding = false;
    std::vector<CreatureRenderState> creatures;
    std::vector<CarcassRenderState> carcasses;
    std::vector<WaterDisturbance> splashes;
    std::vector<SoundSource> newSounds; // started since the previous snapshot (for audio playback)
    Vec3 researcherPosition;
    float researcherHeading = 0.0f;
    bool researcherInVehicle = false;
    Vec3 vehiclePosition;
    float vehicleHeading = 0.0f;
    float vehicleSinkage = 0.0f;
    Vec3 dronePosition;
    float droneHeading = 0.0f;
    bool droneAirborne = false;
};
} // namespace noctis
