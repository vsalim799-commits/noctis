// ANoctisWaterActor — renders the core's surface hydrology.
//   Section 0: river ribbons for the whole valley (centreline from the terrain generator, width from the current
//              stage by at-a-station hydraulic geometry, height from the core's water surface), outside the focus area;
//   Section 1: the exact water surface grid around the camera (lakes, oxbows, flooded floodplain, the local river reach),
//              rebuilt from the auxiliary export (1 Hz by default).
// Vertex colours carry flow (RG, 0.5 = still, +-2 m/s range) and depth (B, 0..5 m) for a flow-map water material.
// Splash disturbances spawn a Niagara burst. Unreal's Water plugin is intentionally not used: its bodies are
// authored splines, while here the water level is simulated (floods, drying ponds).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "NoctisWaterActor.generated.h"

class UMaterialInterface;
class UNiagaraSystem;
class UProceduralMeshComponent;
struct FNoctisAuxData;
struct FNoctisSpace;

UCLASS(Blueprintable)
class NOCTIS_API ANoctisWaterActor : public AActor
{
	GENERATED_BODY()

public:
	ANoctisWaterActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Noctis")
	TObjectPtr<UMaterialInterface> WaterMaterial;

	UPROPERTY(EditAnywhere, Category = "Noctis")
	TObjectPtr<UNiagaraSystem> SplashSystem;

	/** River ribbons are rebuilt when the stage changed by more than this (m) or the focus moved by FocusRebuildM. */
	UPROPERTY(EditAnywhere, Category = "Noctis")
	float StageRebuildM = 0.02f;

	UPROPERTY(EditAnywhere, Category = "Noctis")
	float FocusRebuildM = 150.0f;

	/** Cells with less water than this (m) are left dry (avoids z-fighting with wet mud). */
	UPROPERTY(EditAnywhere, Category = "Noctis")
	float MinDepthM = 0.03f;

	UPROPERTY(EditAnywhere, Category = "Noctis")
	float SplashMaxDistanceM = 150.0f;

private:
	void RebuildRivers(const FNoctisAuxData& Aux, const FNoctisSpace& Space);
	void RebuildLocalPatch(const FNoctisAuxData& Aux, const FNoctisSpace& Space);

	UPROPERTY(VisibleAnywhere, Category = "Noctis")
	TObjectPtr<UProceduralMeshComponent> WaterMesh;

	int32 LastAuxRevision = -1;
	float LastRiverStage = -1000.0f;
	FVector2D LastRiverFocus = FVector2D(1e12, 1e12);
	double LastSplashTime = -1e9;
};
