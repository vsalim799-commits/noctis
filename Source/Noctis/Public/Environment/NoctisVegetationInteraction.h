// ANoctisVegetationInteraction — feeds the core's vegetation state to Unreal materials, RVT and PCG.
//   * Trail wear, recent trampling and burn scars around the camera (auxiliary export, 1 Hz) are written to a
//     transient texture and copied into RT_NoctisTrails (a render target referenced statically by the landscape
//     material, its runtime-virtual-texture writer and the foliage materials). MPC_NoctisWeather.TrailMaskRect gives
//     the world rectangle (Unreal cm: min X, min Y, size X, size Y) for world-position -> UV.
//   * The N animals nearest to the camera (noctis.Vegetation.Interactors) are written to MPC vectors
//     Interactor0..Interactor31 (xyz = Unreal position of the body centre, w = body radius cm) for foliage bending.
//   * PCG: OnVegetationStateUpdated() is raised with the updated bounds, and listed PCG components can be
//     regenerated periodically (the PCG graph reads RT_NoctisTrails / the exported density masks).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "NoctisVegetationInteraction.generated.h"

class UMaterialParameterCollection;
class UPCGComponent;
class UTexture2D;
class UTextureRenderTarget2D;
struct FNoctisAuxData;

UCLASS(Blueprintable)
class NOCTIS_API ANoctisVegetationInteraction : public AActor
{
	GENERATED_BODY()

public:
	ANoctisVegetationInteraction();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Noctis")
	TObjectPtr<UTextureRenderTarget2D> TrailMaskTarget;

	/** PCG components regenerated every PCGRefreshSeconds while the vegetation state changes (empty = none). */
	UPROPERTY(EditInstanceOnly, Category = "Noctis|PCG")
	TArray<TObjectPtr<UPCGComponent>> PCGComponentsToRefresh;

	UPROPERTY(EditAnywhere, Category = "Noctis|PCG", meta = (ClampMin = "1.0"))
	float PCGRefreshSeconds = 30.0f;

	UFUNCTION(BlueprintImplementableEvent, Category = "Noctis")
	void OnVegetationStateUpdated(FBox WorldBounds);

private:
	void UpdateMask(const FNoctisAuxData& Aux);
	void UpdateInteractors();

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> MaskTexture;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> WeatherParameters;

	int32 LastAuxRevision = -1;
	float PCGTimer = 0.0f;
	bool bPCGDirty = false;
};
