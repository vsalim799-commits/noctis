// Blueprint-visible mirrors of the core enumerations and the per-frame animation inputs of a creature.
// The numeric values match the core enums one to one (checked by static_asserts in NoctisTypes.cpp),
// so conversions are plain casts.
#pragma once

#include "CoreMinimal.h"

#include "NoctisTypes.generated.h"

UENUM(BlueprintType)
enum class ENoctisLod : uint8
{
	Full,
	Near,
	Far,
	Regional
};

UENUM(BlueprintType)
enum class ENoctisGait : uint8
{
	Stand,
	Walk,
	FastWalk,
	Run,
	Swim,
	Fly
};

UENUM(BlueprintType)
enum class ENoctisPosture : uint8
{
	Normal,
	HeadUp,
	HeadDown,
	Crouch,
	Display,
	Lying,
	Swimming
};

UENUM(BlueprintType)
enum class ENoctisBehavior : uint8
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
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ENoctisWeatherRegime : uint8
{
	Clear,
	Fair,
	Overcast,
	LightRain,
	HeavyRain,
	Thunderstorm
};

UENUM(BlueprintType)
enum class ENoctisSubstrate : uint8
{
	Rock,
	Gravel,
	Sand,
	Silt,
	Mud,
	Clay,
	ForestSoil,
	Peat
};

UENUM(BlueprintType)
enum class ENoctisDroneMode : uint8
{
	Docked,
	Hover,
	Manual,
	Orbit,
	Follow,
	Survey,
	ReturnHome,
	Landing,
	Lost
};

UENUM(BlueprintType)
enum class ENoctisHypothesisStatus : uint8
{
	Open,
	Supported,
	NotSupported,
	Inconclusive
};

/** Foot slot in the core's per-species foot order is mapped to one of these by the species runtime (left/fore flags). */
UENUM(BlueprintType)
enum class ENoctisFootSlot : uint8
{
	HindLeft,
	HindRight,
	ForeLeft,
	ForeRight
};

/** Everything the animation graph needs for one creature, refreshed every frame from the interpolated render snapshot. */
USTRUCT(BlueprintType)
struct NOCTIS_API FNoctisCreatureAnimInputs
{
	GENERATED_BODY()

	/** Ground speed (cm/s, Unreal units) and the core's gait classification. */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Locomotion")
	float Speed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Locomotion")
	ENoctisGait Gait = ENoctisGait::Stand;

	/** Stride phase 0..1 from the core's gait model: drive a sync group / stride-warping playrate from it. */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Locomotion")
	float GaitPhase = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Locomotion")
	float StrideLength = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Locomotion")
	float DutyFactor = 0.7f;

	/** Vertical oscillation of the centre of mass (cm). Apply on the pelvis. */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Locomotion")
	float BodyBob = 0.0f;

	/** Body pitch/roll from terrain and inertial lean (degrees, Unreal convention). */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Locomotion")
	float BodyPitch = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Locomotion")
	float BodyRoll = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Behaviour")
	ENoctisPosture Posture = ENoctisPosture::Normal;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Behaviour")
	ENoctisBehavior Behavior = ENoctisBehavior::Rest;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Behaviour")
	ENoctisLod Lod = ENoctisLod::Far;

	/** Foot IK goals in world space (Unreal cm), indexed by ENoctisFootSlot. */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis|IK")
	TArray<FVector> FootTargets;

	/** Foot IK goals in component space (convenient for Two Bone IK / Control Rig inputs). */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis|IK")
	TArray<FVector> FootTargetsComponent;

	/** 1 while the foot is planted (stance), 0 in swing; use as IK alpha and for foot locking. */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis|IK")
	TArray<float> FootContact;

	/** Per-limb muscle activation 0..1 (stance load): drives corrective morphs / muscle flex shaders. */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis|SoftTissue")
	TArray<float> Muscle;

	/** Damped soft-tissue offsets from the core: belly V/L (cm), neck V/L (deg), tail V/L (deg). */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis|SoftTissue")
	float BellyVertical = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|SoftTissue")
	float BellyLateral = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|SoftTissue")
	float NeckVertical = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|SoftTissue")
	float NeckLateral = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|SoftTissue")
	float TailVertical = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|SoftTissue")
	float TailLateral = 0.0f;

	/** Chest expansion 0..1 (breathing cycle). */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis|SoftTissue")
	float Breath = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Head")
	bool bHasLookTarget = false;

	/** World-space point the animal attends to (eye height of the target estimate). */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Head")
	FVector LookTarget = FVector::ZeroVector;

	/** Vocal envelope 0..1 of the current call; JawOpen is 0 for closed-mouth species (throat inflation instead). */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Head")
	bool bVocalizing = false;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Head")
	float VocalEnvelope = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Head")
	float JawOpen = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Head")
	float ThroatInflation = 0.0f;

	/** Locomotor impairment (0..1) and side (-1 left .. +1 right) from injuries. */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Condition")
	float Limp = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Condition")
	float LimpSide = 0.0f;

	/** Body condition (reserves/capacity), wetness and mud coating 0..1 (material parameters). */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Condition")
	float Condition = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Condition")
	float Wetness = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis|Condition")
	float Mud = 0.0f;

	FNoctisCreatureAnimInputs()
	{
		FootTargets.Init(FVector::ZeroVector, 4);
		FootTargetsComponent.Init(FVector::ZeroVector, 4);
		FootContact.Init(1.0f, 4);
		Muscle.Init(0.0f, 4);
	}
};

/** Static per-species facts the presentation layer needs (copied once from the core after boot). */
struct FNoctisSpeciesInfo
{
	int32 Index = -1;
	FName Id;
	FString ScientificName;
	FString CommonNameFr;
	FString BodyPlanKey;
	float AdultLengthM = 1.0f;
	float HipHeightM = 0.5f;
	float AdultMassKg = 1.0f;
	int32 LegCount = 2;
	bool bAgent = true;
	bool bClosedMouthCalls = true;
	bool bInWorld = false;
	/** Core foot index -> slot (from SpeciesRuntime::footIsLeft / footIsFore). */
	ENoctisFootSlot FootSlots[4] = {ENoctisFootSlot::HindLeft, ENoctisFootSlot::HindRight, ENoctisFootSlot::ForeLeft, ENoctisFootSlot::ForeRight};
	/** Call ids in the core's order (SoundSource::callIndex) with their nominal duration. */
	TArray<FName> CallIds;
	TArray<float> CallDurations;
};
