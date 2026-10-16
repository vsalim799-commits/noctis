// Editor functions callable from Python (unreal.NoctisEditorLibrary.*) and Editor Utility Blueprints.
// Used by Content/Python/setup_project.py to place the level (landscape transform, PlayerStart at the base camp)
// exactly where the simulation core puts things, without duplicating the core's terrain generator in Python.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "NoctisEditorLibrary.generated.h"

/** Where the core's valley lies in the Unreal world (current Project Settings > Game > Noctis). */
USTRUCT(BlueprintType)
struct FNoctisTerrainInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	FString EnvironmentId;

	/** Heightmap samples per side (Landscape resolution, e.g. 2017). */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	int32 Samples = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float SizeM = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float MinHeightM = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float MaxHeightM = 0.0f;

	/** Landscape actor transform matching heightmap.png from `noctis_sim export-terrain` (row 0 = north edge). */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	FVector LandscapeLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	FVector LandscapeScale = FVector::OneVector;

	/** Base camp on the ground (Unreal cm): researcher, vehicle and drone start there. */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	FVector BasecampLocation = FVector::ZeroVector;

	/** Valley-floor reference height (Unreal cm): base of the simulated cold-air fog layer. */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	double ValleyFloorZ = 0.0;
};

UCLASS()
class UNoctisEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Builds the terrain with the current settings (no animals; a few seconds at 2017 samples) and reports its placement. */
	UFUNCTION(BlueprintCallable, Category = "Noctis|Editor")
	static bool ComputeTerrainInfo(FNoctisTerrainInfo& OutInfo);

	/** Same as Tools > Noctis > Valider les données scientifiques. Returns the number of errors (-1: Data/ did not load). */
	UFUNCTION(BlueprintCallable, Category = "Noctis|Editor")
	static int32 ValidateScientificData(bool bOpenMessageLog = true);

	/** Rebuilds the valley in running PIE sessions. Returns the number of worlds asked to reload. */
	UFUNCTION(BlueprintCallable, Category = "Noctis|Editor")
	static int32 ReloadScientificData();

	/** Absolute data root used by the game (UNoctisSettings::ResolveDataRoot). */
	UFUNCTION(BlueprintPure, Category = "Noctis|Editor")
	static FString GetDataRoot();
};
