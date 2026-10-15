// UNoctisCreatureAnimInstance — base class of every creature Animation Blueprint.
//
// The simulation decides WHERE each foot goes and WHEN (gait phase, duty factor, stance/swing) and how soft
// tissue swings; the animation only realises it. Suggested AnimGraph (per species ABP, see Docs/UE5_INTEGRATION.md):
//   Locomotion state machine or blend space on (Speed, Gait) synced to GaitPhase
//   -> pelvis offset (BodyBob) and body lean (BodyPitch/BodyRoll)
//   -> foot IK: Control Rig "CR_NoctisLegIK" or Two Bone IK per leg on FootTargetsComponent, alpha = FootContact
//   -> soft tissue: Transform (Modify) Bone on neck/tail chains from NeckVertical/NeckLateral/TailVertical/TailLateral,
//      belly morph or bone from BellyVertical/BellyLateral, chest from Breath
//   -> Look At on HeadBone towards LookTarget (bHasLookTarget), jaw from JawOpen, throat morph from ThroatInflation.
// All members are written on the game thread in NativeUpdateAnimation and read by the (worker-thread) AnimGraph.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"

#include "NoctisTypes.h"

#include "NoctisCreatureAnimInstance.generated.h"

UCLASS(Transient, Blueprintable)
class NOCTIS_API UNoctisCreatureAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Called by ANoctisCreatureActor every frame before the animation update. Inputs are in world space. */
	void SetSimulationInputs(const FNoctisCreatureAnimInputs& InInputs);

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Noctis")
	FNoctisCreatureAnimInputs Inputs;

	// Flattened copies for blend spaces and fast-path property access.
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Noctis")
	float Speed = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Noctis")
	ENoctisGait Gait = ENoctisGait::Stand;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Noctis")
	float GaitPhase = 0.0f;

	/** Simulated stride frequency (strides per second = speed / stride length). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Noctis")
	float StrideFrequencyHz = 0.0f;

	/** Playrate that keeps the authored cycle in step with the simulated stride (1 = authored speed). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Noctis")
	float LocomotionPlayRate = 1.0f;

	/** Pelvis offset in component space (cm). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Noctis")
	FVector PelvisOffset = FVector::ZeroVector;

	/** Foot IK is meaningful at Full LOD only (the core simulates feet only there). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Noctis")
	bool bFootIK = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Noctis")
	float FootIKAlpha = 0.0f;

	/** Duration (s) of one full stride of the authored locomotion cycle; set per species in the ABP class defaults. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Noctis", meta = (ClampMin = "0.05"))
	float AuthoredCycleSeconds = 1.2f;

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	FNoctisCreatureAnimInputs Pending;
	bool bHasPending = false;
};
