// ANoctisCreatureActor — the visible body of one simulated individual. Pooled and recycled by
// ANoctisCreatureManager: it holds no simulation state of its own, only the presentation of the interpolated
// CreatureRenderState (transform, animation inputs, material parameters, footstep and voice hooks).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "NoctisSpace.h"
#include "NoctisTypes.h"

#include "Noctis/Sim/Snapshot.h"

#include "NoctisCreatureActor.generated.h"

class UAudioComponent;
class UNoctisCreatureAnimInstance;
class UNoctisSpeciesVisualData;
class USkeletalMeshComponent;

UCLASS(Blueprintable, NotPlaceable)
class NOCTIS_API ANoctisCreatureActor : public AActor
{
	GENERATED_BODY()

public:
	ANoctisCreatureActor();

	/** Binds the pooled actor to an individual. Visual assets must already be loaded (the manager preloads them). */
	void ActivateFor(uint32 InCreatureId, const FNoctisSpeciesInfo& InSpecies, UNoctisSpeciesVisualData* InVisual, AActor* Driver);
	void Deactivate();
	bool IsActive() const { return CreatureId != 0; }

	/** Presents the individual between two snapshots (Previous may be null when it just appeared). */
	void ApplyState(const noctis::CreatureRenderState* Previous, const noctis::CreatureRenderState& Current, float Alpha, const FNoctisSpace& Space,
					float DeltaSeconds);

	/** Footfall reported by the core's sound field (Full LOD only): triggers the species footstep MetaSound. */
	void PlayFootfall(float LevelDb, float MassKg, uint8 Substrate, bool bInWater);

	/** A call started: drives the jaw / throat envelope for its duration. */
	void BeginVocalization(float DurationS);

	UFUNCTION(BlueprintPure, Category = "Noctis")
	int64 GetCreatureId() const { return static_cast<int64>(CreatureId); }

	UFUNCTION(BlueprintPure, Category = "Noctis")
	int32 GetSpeciesIndex() const { return Species.Index; }

	uint32 GetCreatureIdRaw() const { return CreatureId; }
	const FNoctisCreatureAnimInputs& GetAnimInputs() const { return AnimInputs; }
	USkeletalMeshComponent* GetMesh() const { return Mesh; }
	UNoctisSpeciesVisualData* GetVisual() const { return Visual; }

	/** Blueprint hook (decals for scars, cosmetic effects) called when the actor is bound to an individual. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Noctis")
	void OnBoundToIndividual(int64 InCreatureId, FName SpeciesId, float MarkingSeed, int32 ScarCount);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Noctis")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Noctis")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Noctis")
	TObjectPtr<UAudioComponent> FootstepAudio;

	UPROPERTY(Transient)
	TObjectPtr<UNoctisSpeciesVisualData> Visual;

	UPROPERTY(Transient)
	TObjectPtr<UClass> CurrentAnimClass;

private:
	uint32 CreatureId = 0;
	FNoctisSpeciesInfo Species;
	FNoctisCreatureAnimInputs AnimInputs;
	float VocalRemainingS = 0.0f;
	float VocalDurationS = 0.0f;
	bool bBoundEventSent = false;
	bool bTickPrerequisiteSet = false;
	float LastWetness = -1.0f;
	float LastMud = -1.0f;
	float LastCondition = -1.0f;
};
