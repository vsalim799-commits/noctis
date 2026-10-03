// A persistent individual animal and all of its state components.
#pragma once

#include "Noctis/Core/Ids.h"
#include "Noctis/Core/Math.h"
#include "Noctis/Core/Random.h"
#include "Noctis/Science/Taxonomy.h"
#include "Noctis/World/Traces.h"

#include <array>
#include <string>
#include <vector>

namespace noctis
{
enum class Sex : u8
{
    Female,
    Male
};

enum class LodLevel : u8
{
    Full,    // near an observer: full perception, decisions, physical locomotion with feet
    Near,    // medium distance: reduced rates, no feet, simplified perception
    Far,     // far: coarse kinematics and statistical encounters
    Regional // outside the valley: part of the regional population pool
};
NOCTIS_API const char* lodLabel(LodLevel l);

// ------------------------------------------------------------------ Anatomy

enum class BodyRegion : u8
{
    Head,
    Jaw,
    Neck,
    Torso,
    Ribs,
    Tail,
    ForelimbL,
    ForelimbR,
    HindlimbL,
    HindlimbR,
    Count
};
inline constexpr int kBodyRegionCount = static_cast<int>(BodyRegion::Count);
NOCTIS_API const char* bodyRegionLabelFr(BodyRegion r);
NOCTIS_API const char* bodyRegionKey(BodyRegion r);

// ------------------------------------------------------------------ Personality

struct Personality
{
    std::array<float, kPersonalityAxisCount> axis{};
    float get(PersonalityAxis a) const { return axis[static_cast<size_t>(a)]; }
};

// ------------------------------------------------------------------ Physiology

struct PhysiologyState
{
    float energyReserveMJ = 0.0f;  // mobilisable reserves (fat, glycogen)
    float energyCapacityMJ = 1.0f; // capacity for current body size
    float gutFillKg = 0.0f;        // undigested food (dry-matter equivalent)
    float gutCapacityKg = 1.0f;
    float gutEnergyMJPerKg = 10.0f;
    float hydration = 1.0f;        // 1 = fully hydrated
    float fatigue = 0.0f;          // short-term exertion 0..1
    float sleepPressure = 0.2f;    // circadian/homeostatic 0..1
    float bodyTempC = 35.0f;
    float stress = 0.0f;           // acute 0..1
    float chronicStress = 0.0f;    // slow 0..1 (repeated disturbance)
    float pain = 0.0f;             // 0..1
    float bloodLoss = 0.0f;        // fraction of blood volume lost
    float metabolicRateW = 0.0f;
    float respirationHz = 0.2f;
    float breathPhase = 0.0f;
    float condition = 0.7f;        // reserves / capacity
    float hunger = 0.0f;           // drive 0..1
    float thirst = 0.0f;           // drive 0..1
    float heatStress = 0.0f;       // drive 0..1 (too hot), negative values = cold
};

// ------------------------------------------------------------------ Injuries

enum class InjuryType : u8
{
    Laceration,
    Puncture,
    Fracture,
    Contusion,
    Sprain,
    Burn
};
NOCTIS_API const char* injuryTypeLabelFr(InjuryType t);

struct Injury
{
    BodyRegion region = BodyRegion::Torso;
    InjuryType type = InjuryType::Contusion;
    float severity = 0.0f;  // 0..1 current
    float initialSeverity = 0.0f;
    float infection = 0.0f; // 0..1
    float bleeding = 0.0f;  // fraction of blood volume per hour
    double time = 0.0;
    EntityId cause;
    i16 causeSpecies = -1;
};

struct Scar
{
    BodyRegion region = BodyRegion::Torso;
    InjuryType type = InjuryType::Laceration;
    float size = 0.0f;  // 0..1 relative to region
    u32 shapeSeed = 0;  // drives procedural decal placement in the renderer
    double time = 0.0;
};

struct InjuryState
{
    std::vector<Injury> active;
    std::vector<Scar> scars;
    float limp = 0.0f;       // 0..1 locomotor impairment asymmetry
    float limpSide = 0.0f;   // -1 left, +1 right
    float speedFactor = 1.0f;
    float feedingFactor = 1.0f;
    float defenseFactor = 1.0f;
    float balanceFactor = 1.0f;
    float healthIndex = 1.0f; // 1 healthy .. 0 dying
};

// ------------------------------------------------------------------ Perception

enum class AwarenessStage : u8
{
    Unaware,
    Detected,   // something registered (a sound, a movement, a smell)
    Oriented,   // turned head/body towards the stimulus
    Vigilant,   // attending, freezing, scanning
    Identified  // knows what it is
};
NOCTIS_API const char* awarenessStageLabelFr(AwarenessStage s);

enum class Relation : u8
{
    Unknown,
    GroupMate,
    Conspecific,
    Offspring,
    Parent,
    Mate,
    Predator,
    Prey,
    Competitor,
    Harmless,
    Researcher,
    Vehicle,
    Drone,
    Carcass,
    Nest
};
NOCTIS_API const char* relationLabelFr(Relation r);

enum Modality : u8
{
    ModVision = 1 << 0,
    ModHearing = 1 << 1,
    ModSmell = 1 << 2,
    ModVibration = 1 << 3,
    ModCall = 1 << 4 // learnt from a conspecific alarm/contact call
};

struct Awareness
{
    EntityId entity;
    i16 speciesIndex = -1;
    Relation relation = Relation::Unknown;
    AwarenessStage stage = AwarenessStage::Unaware;
    float evidence = 0.0f;      // accumulated, leaky
    float identification = 0.0f;
    Vec2 estimatedPos;
    float uncertaintyM = 100.0f;
    Vec2 estimatedVel;
    double lastUpdate = 0.0;
    u8 modalities = 0;
    float threat = 0.0f;        // appraisal 0..1
    float opportunity = 0.0f;   // prey/food value 0..1
};

struct PerceptionState
{
    std::vector<Awareness> known;
    float alertness = 0.0f;   // global arousal 0..1
    Vec2 lookTarget;
    bool hasLookTarget = false;
    double lastAlarmHeard = -1e9;
    Vec2 alarmDirection;
    EntityId alarmSource;
    float ambientNoiseDb = 30.0f;
    float lightLevel = 1.0f;  // effective visual performance 0..1
    double lastPerceptionTime = -1e9;
    const Awareness* find(EntityId e) const
    {
        for (const Awareness& a : known)
        {
            if (a.entity == e)
            {
                return &a;
            }
        }
        return nullptr;
    }
};

// ------------------------------------------------------------------ Memory

enum class MemoryKind : u8
{
    Water,
    Food,
    Danger,
    SafeRest,
    Nest,
    Carcass,
    ResearcherEncounter,
    PredatorSighting,
    Shelter,
    Intrusion
};
NOCTIS_API const char* memoryKindLabelFr(MemoryKind k);

struct PlaceMemory
{
    Vec2 position;
    MemoryKind kind = MemoryKind::Food;
    float valence = 0.0f;   // -1 aversive .. +1 attractive
    float strength = 0.0f;  // 0..1, decays
    double lastReinforced = 0.0;
    EntityId about;
};

struct SocialMemory
{
    EntityId other;
    float familiarity = 0.0f;
    float affinity = 0.0f;
    float dominance = 0.0f; // >0: I won previous contests
    double lastSeen = 0.0;
};

struct ThreatHistory
{
    float researcherExposure = 0.0f; // accumulated neutral exposure (habituation)
    float researcherNegative = 0.0f; // aversive events (sensitisation)
    float vehicleExposure = 0.0f;
    float vehicleNegative = 0.0f;
    float droneExposure = 0.0f;
    float droneNegative = 0.0f;
};

struct MemoryState
{
    std::vector<PlaceMemory> places;
    std::vector<SocialMemory> social;
    ThreatHistory threats;
    Vec2 homeCenter;
    float homeRadius = 1000.0f;
};

// ------------------------------------------------------------------ Behaviour

enum class BehaviorId : u8
{
    Rest,
    Sleep,
    Wander,
    Forage,
    Drink,
    Travel,
    FollowGroup,
    Vigilance,
    Flee,
    Freeze,
    Defend,
    DefendYoung,
    Investigate,
    Display,
    Patrol,
    Hunt,
    FeedCarcass,
    Scavenge,
    Thermoregulate,
    Court,
    NestTend,
    CareYoung,
    Emigrate,
    Groom,
    Socialize,
    Fight,
    Count
};
inline constexpr int kBehaviorCount = static_cast<int>(BehaviorId::Count);
NOCTIS_API const char* behaviorKey(BehaviorId b);
NOCTIS_API const char* behaviorLabelFr(BehaviorId b);

enum class HuntPhase : u8
{
    None,
    Search,
    Stalk,
    Approach,
    Chase,
    Attack,
    Abandon
};
NOCTIS_API const char* huntPhaseLabelFr(HuntPhase p);

enum class Posture : u8
{
    Normal,
    HeadUp,     // vigilance scan
    HeadDown,   // feeding / drinking / sniffing
    Crouch,     // stalking, freezing
    Display,    // threat or courtship display
    Lying,      // resting / sleeping
    Swimming
};
NOCTIS_API const char* postureLabelFr(Posture p);

enum class ActionKind : u8
{
    None,
    Attack,
    EatPlants,
    EatCarcass,
    Drink,
    Call,
    Mate,
    LayEggs,
    PredateNest,
    AttendNest,
    Display,
    Defecate,
    EatPopulation // fish, invertebrates, small vertebrates simulated as populations
};

struct ActionIntent
{
    ActionKind kind = ActionKind::None;
    EntityId target;
    Vec2 position;
    float amount = 0.0f;
    u8 param = 0; // call context, body region...
};

struct BehaviorState
{
    BehaviorId current = BehaviorId::Rest;
    BehaviorId previous = BehaviorId::Rest;
    double startedAt = 0.0;
    double commitUntil = 0.0;
    double nextDecision = 0.0;
    EntityId target;
    Vec2 targetPos;
    bool hasTargetPos = false;
    HuntPhase hunt = HuntPhase::None;
    double huntPhaseStart = 0.0;
    float chaseSeconds = 0.0f;
    float bestGap = 1e9f;
    EntityId nest;
    Posture posture = Posture::Normal;
    float desiredSpeed = 0.0f;
    Vec2 desiredDir{1.0f, 0.0f};
    float urgency = 0.0f;
    double lastCall = -1e9;
    double lastAttack = -1e9;
    std::array<float, kBehaviorCount> scores{};
    std::vector<ActionIntent> intents;
};

// ------------------------------------------------------------------ Locomotion

enum class Gait : u8
{
    Stand,
    Walk,
    FastWalk,
    Run,
    Swim,
    Fly
};
NOCTIS_API const char* gaitLabelFr(Gait g);

struct FootState
{
    Vec3 planted;          // world position of the current/last contact
    Vec3 target;           // next placement (for IK)
    bool inContact = true;
    float swing = 0.0f;    // 0..1 progress of the swing phase
    float contactForceN = 0.0f;
    float phaseOffset = 0.0f;
    bool left = true;
    bool fore = false;
};

struct LocomotionState
{
    Vec3 position;
    float heading = 0.0f;
    Vec2 velocity;
    float speed = 0.0f;
    float yawRate = 0.0f;
    float pitch = 0.0f;  // body pitch from terrain + acceleration (rad)
    float roll = 0.0f;
    Vec2 acceleration;   // horizontal, world frame
    Gait gait = Gait::Stand;
    float gaitPhase = 0.0f;
    float strideLengthM = 0.0f;
    float strideFrequencyHz = 0.0f;
    float dutyFactor = 0.7f;
    float froude = 0.0f;
    float sinkageM = 0.0f;
    float slip = 0.0f;
    float waterDepth = 0.0f;
    bool swimming = false;
    u8 footCount = 2;
    std::array<FootState, 4> feet{};
    double distanceTravelled = 0.0;
    float comHeight = 1.0f;
    float bodyBob = 0.0f;    // vertical COM oscillation (m)
    float metabolicCostW = 0.0f;
};

// ------------------------------------------------------------------ Soft tissue

inline constexpr int kSoftTissueNodeCount = 6;
enum class SoftTissueNode : u8
{
    BellyVertical,
    BellyLateral,
    NeckVertical,
    NeckLateral,
    TailVertical,
    TailLateral
};

struct SoftTissueState
{
    std::array<float, kSoftTissueNodeCount> offset{};   // m (or rad for tail/neck)
    std::array<float, kSoftTissueNodeCount> velocity{};
    std::array<float, 4> muscleActivation{};             // per limb 0..1
    float breath = 0.0f;                                 // chest expansion 0..1
};

// ------------------------------------------------------------------ Reproduction & social

struct ReproductionState
{
    bool receptive = false;
    bool gravid = false;
    double gravidSince = 0.0;
    EntityId mate;
    EntityId nest;
    int lastBreedingYear = -1000;
    int clutches = 0;
};

struct SocialState
{
    u32 groupId = 0;
    float rank = 0.5f;          // dominance rank within group (0 low .. 1 high)
    bool territoryHolder = false;
    Vec2 territoryCenter;
    float territoryRadius = 0.0f;
    EntityId followTarget;      // juveniles follow a parent
};

struct LifeHistory
{
    int huntsAttempted = 0;
    int huntsSucceeded = 0;
    int attacksSurvived = 0;
    int fightsWon = 0;
    int fightsLost = 0;
    int timesFled = 0;
    int researcherEncounters = 0;
    int offspringHatched = 0;
    double distanceTravelledKm = 0.0;
};

// ------------------------------------------------------------------ The individual

struct Creature
{
    EntityId id;
    i16 speciesIndex = -1;
    Sex sex = Sex::Female;
    double birthTime = 0.0;
    u32 generation = 0;
    EntityId mother;
    EntityId father;
    u64 seed = 0;
    Rng rng;

    float massKg = 1.0f;
    float lengthM = 1.0f;
    float hipHeightM = 0.5f;
    float sizeGene = 1.0f;       // heritable asymptotic size multiplier
    float markingSeed = 0.0f;    // drives the procedural integument pattern (coloration is a hypothesis)

    bool alive = true;
    double deathTime = 0.0;
    DeathCause deathCause = DeathCause::Unknown;
    LodLevel lod = LodLevel::Far;
    double lastUpdate = 0.0;
    double lastPerception = -1e9;
    double lastLocomotion = 0.0;

    Personality personality;
    PhysiologyState phys;
    InjuryState injuries;
    PerceptionState perception;
    MemoryState memory;
    BehaviorState behavior;
    LocomotionState loco;
    SoftTissueState soft;
    ReproductionState repro;
    SocialState social;
    LifeHistory history;

    // Per-step outputs (written in parallel phases, drained in the serial phase).
    std::vector<Footprint> pendingFootprints;

    double ageYears(double now) const { return (now - birthTime) / (365.25 * 86400.0); }
    Vec2 pos2() const { return loco.position.xy(); }
};
} // namespace noctis
