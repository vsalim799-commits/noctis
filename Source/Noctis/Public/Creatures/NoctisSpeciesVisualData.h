// UNoctisSpeciesVisualData — how one species looks and sounds in Unreal. One asset per species, created and
// filled by Content/Python/import_creatures.py in /Game/Creatures/<species_id>/DA_<species_id>.
// The scientific facts (size, gait, calls...) are NOT duplicated here: they come from Data/Species/*.json
// through the core. This asset only maps a species id to meshes, animation, materials and sounds.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "NoctisTypes.h"

#include "NoctisSpeciesVisualData.generated.h"

class UAnimInstance;
class UMaterialInterface;
class USkeletalMesh;
class USoundBase;
class UStaticMesh;

USTRUCT(BlueprintType)
struct NOCTIS_API FNoctisCallSound
{
	GENERATED_BODY()

	/** Call id from the species file (sim.vocal.calls[].id). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctis")
	FName CallId;

	/** Optional authored/baked sound (Tools/AudioFactory). Empty = synthesised live by the core's SignalSynth. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctis")
	TSoftObjectPtr<USoundBase> Sound;
};

UCLASS(BlueprintType)
class NOCTIS_API UNoctisSpeciesVisualData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UNoctisSpeciesVisualData();

	static const FPrimaryAssetType PrimaryAssetType;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	/** Species id exactly as in Data/Species/<env>/<id>.json (e.g. "tyrannosaurus_rex"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Species")
	FName SpeciesId;

	// ------------------------------------------------------------------ Mesh & animation

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	/** Animation Blueprint derived from UNoctisCreatureAnimInstance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	TSoftClassPtr<UAnimInstance> AnimClass;

	/** Body length (snout to tail tip, metres) of the mesh at scale 1, used to scale it to the simulated individual. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh", meta = (ClampMin = "0.01"))
	float MeshBodyLengthM = 1.0f;

	/** Extra yaw (degrees) when the mesh's forward axis is not +X. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	float MeshYawOffsetDeg = 0.0f;

	/** Material slot overrides (index = slot). Coloration is a hypothesis: profiles may swap these. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	TArray<TSoftObjectPtr<UMaterialInterface>> MaterialOverrides;

	/** Low-cost stand-in for distant animals (instanced static mesh). Optional. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	TSoftObjectPtr<UStaticMesh> FarImpostorMesh;

	// ------------------------------------------------------------------ Skeleton map (defaults = Tools/CreatureFactory rig:
	// root, pelvis, spine_01..03, neck_01..03, head, jaw, tail_01..10, thigh/shin/metatarsus/toes_l|r, upperarm/forearm/hand_l|r)

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skeleton")
	FName PelvisBone = TEXT("pelvis");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skeleton")
	FName HeadBone = TEXT("head");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skeleton")
	FName JawBone = TEXT("jaw");

	/** IK end bones indexed by ENoctisFootSlot (HindLeft, HindRight, ForeLeft, ForeRight). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skeleton")
	TArray<FName> FootBones;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skeleton")
	TArray<FName> NeckBones;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skeleton")
	TArray<FName> TailBones;

	// ------------------------------------------------------------------ Sound

	/** MetaSound source with inputs Footfall (trigger), Mass (float, kg), LevelDb (float, dB SPL at 1 m), Substrate (int32,
	 *  noctis::Substrate) and InWater (bool), set by ANoctisCreatureActor::PlayFootfall (see import_audio.py). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	TSoftObjectPtr<USoundBase> FootstepSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	TArray<FNoctisCallSound> CallSounds;

	/** Synthesise calls with the core's SignalSynth (physically propagated) when no baked sound exists. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	bool bProceduralCalls = true;

	// ------------------------------------------------------------------ Presentation

	/** Animals beyond this distance are never full actors for this species (small animals: lower). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (ClampMin = "10.0"))
	float MaxActorDistanceM = 600.0f;

	/** Enables URO (animation update rate optimisation) on this species' meshes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	bool bUpdateRateOptimizations = true;

	const FNoctisCallSound* FindCall(FName CallId) const;
};
