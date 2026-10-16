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
// Root motion: the simulation owns the actor's position, so authored root motion is extracted and discarded
// (RootMotionMode = IgnoreRootMotion; import_creatures.py enables root motion on the sequences so the root is locked).
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"

#include "NoctisTypes.h"

#include "NoctisCreatureAnimInstance.generated.h"

class UAnimSequenceBase;
class UNoctisSpeciesVisualData;

UCLASS(Transient, Blueprintable)
class NOCTIS_API UNoctisCreatureAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UNoctisCreatureAnimInstance(const FObjectInitializer& ObjectInitializer);

	/** Called by ANoctisCreatureActor every frame before the animation update. Inputs are in world space. */
	void SetSimulationInputs(const FNoctisCreatureAnimInputs& InInputs);

	/** Called on the game thread when the actor is bound to an individual: authored clips (sequences resolved here, so
	 *  the getters below are safe from thread-safe animation functions), their speeds and cycle lengths. */
	void SetSpeciesVisual(UNoctisSpeciesVisualData* InVisual);

	/** Sequence of a named clip of the species (DA_<species>.Clips, preloaded by the creature manager), or null. */
	UFUNCTION(BlueprintPure, Category = "Noctis", meta = (BlueprintThreadSafe))
	UAnimSequenceBase* GetClipSequence(FName ClipName) const;

	/** Locomotion sequence matching the simulated gait (idle / walk / walk_fast / run / swim), or null. */
	UFUNCTION(BlueprintPure, Category = "Noctis", meta = (BlueprintThreadSafe))
	UAnimSequenceBase* GetLocomotionSequence() const;

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

	/** Playrate that keeps the authored cycle in step with the simulated stride (1 = authored speed): simulated stride
	 *  frequency x authored cycle duration of the gait's clip (DA Clips), else AuthoredCycleSeconds. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Noctis")
	float LocomotionPlayRate = 1.0f;

	/** Visual data of the species this instance animates (set by ANoctisCreatureActor). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Noctis")
	TObjectPtr<UNoctisSpeciesVisualData> SpeciesVisual;

	/** Name of the clip GetLocomotionSequence() returns for the current gait (NAME_None when the species has none). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Noctis")
	FName LocomotionClip;

	/** Pelvis offset in component space (cm). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Noctis")
	FVector PelvisOffset = FVector::ZeroVector;

	/** Foot IK is meaningful at Full LOD only (the core simulates feet only there). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Noctis")
	bool bFootIK = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Noctis")
	float FootIKAlpha = 0.0f;

	/** Fallback duration (s) of one full stride of the authored locomotion cycle, used when the species' visual data has
	 *  no clip for the current gait. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Noctis", meta = (ClampMin = "0.05"))
	float AuthoredCycleSeconds = 1.2f;

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	/** Resolved clip sequences (game thread, SetSpeciesVisual). */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UAnimSequenceBase>> ClipSequences;

	FNoctisCreatureAnimInputs Pending;
	bool bHasPending = false;
};
