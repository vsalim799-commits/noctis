// ANoctisCreatureManager — decides which simulated individuals get a body.
//
// Every frame (TG_PrePhysics, after UNoctisWorldSubsystem published the frame):
//   * near the camera (noctis.Creatures.ActorRadiusM, capped per species): pooled ANoctisCreatureActor with full
//     animation, foot IK and sound — at most noctis.Creatures.MaxActors, nearest first, with hysteresis;
//   * up to noctis.Creatures.FarInstanceRadiusM: one instanced static mesh (impostor) per species, refreshed at 4 Hz;
//   * beyond: nothing (the core still simulates them, at Far/Regional LOD).
// The simulation LOD (who has feet, who perceives at full rate) is decided by the core around its observers; the
// actor LOD here is purely visual. noctis.ShowLOD draws the core's LOD per animal.
#pragma once

#include "CoreMinimal.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Actor.h"

#include "NoctisCreatureManager.generated.h"

class ANoctisCreatureActor;
class UInstancedStaticMeshComponent;
class UNoctisSpeciesVisualData;
struct FNoctisFrameOutput;

UCLASS(Blueprintable)
class NOCTIS_API ANoctisCreatureManager : public AActor
{
	GENERATED_BODY()

public:
	ANoctisCreatureManager();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Actor currently presenting this individual, if any. */
	ANoctisCreatureActor* FindActor(uint32 CreatureId) const;

	UFUNCTION(BlueprintPure, Category = "Noctis")
	int32 GetActiveActorCount() const { return Active.Num(); }

	UFUNCTION(BlueprintPure, Category = "Noctis")
	bool AreVisualsLoaded() const { return bVisualsLoaded; }

	/** Releases every actor (world rebuilt, data reloaded). */
	void ReleaseAll();

	/** Class spawned for each pooled body (a Blueprint subclass may add decals, effects). */
	UPROPERTY(EditAnywhere, Category = "Noctis")
	TSubclassOf<ANoctisCreatureActor> CreatureActorClass;

	/** Distance beyond which an actor is released, as a fraction above the spawn radius (hysteresis). */
	UPROPERTY(EditAnywhere, Category = "Noctis", meta = (ClampMin = "1.0", ClampMax = "2.0"))
	float ReleaseHysteresis = 1.15f;

private:
	void LoadVisuals();
	void OnVisualDataLoaded();
	void OnVisualAssetsLoaded();
	void OnFramePublished(const FNoctisFrameOutput& Front);
	void MapVisualsToSpecies();

	UFUNCTION()
	void OnSimulationReady();
	FVector GetViewLocation() const;
	ANoctisCreatureActor* Acquire(int32 SpeciesIndex);
	void Release(uint32 CreatureId);
	void UpdateImpostors(const FNoctisFrameOutput& Front, const FVector& View, float ActorRadiusCm, float FarRadiusCm);
	void DrawLodDebug(const FNoctisFrameOutput& Front, const FVector& View) const;

	/** Visual data indexed by core species index (null when the species has no asset yet). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNoctisSpeciesVisualData>> VisualsBySpecies;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UNoctisSpeciesVisualData>> LoadedVisuals;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ANoctisCreatureActor>> AllActors;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> ImpostorsBySpecies;

	TMap<uint32, TObjectPtr<ANoctisCreatureActor>> Active;
	TMap<int32, TArray<TObjectPtr<ANoctisCreatureActor>>> Pool;

	/** Index of each creature id in the Previous snapshot (rebuilt when a frame is published). */
	TMap<uint32, int32> PreviousIndexById;
	uint64 PreviousIndexSequence = 0;

	TArray<FPrimaryAssetId> VisualAssetIds;
	TSharedPtr<FStreamableHandle> VisualDataHandle;
	TSharedPtr<FStreamableHandle> VisualAssetsHandle;
	FDelegateHandle FramePublishedHandle;
	bool bVisualsLoaded = false;
	bool bVisualDataProcessed = false;
	bool bVisualsMapped = false;
	float ImpostorTimer = 0.0f;
};
