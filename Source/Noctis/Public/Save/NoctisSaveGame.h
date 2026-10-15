// Save/load glue.
//
// STATUS: noctis::WorldSimulation::save/load are declared in the core but NOT implemented (calling them would not
// link). The Unreal side is therefore built around a future binary blob API that the core should expose:
//
//     // Proposed for NoctisCore (not implemented yet):
//     bool WorldSimulation::saveToBlob(std::vector<u8>& out) const;   // versioned, little-endian, chunked
//     bool WorldSimulation::loadFromBlob(const std::vector<u8>& in);  // (tag, size, payload) chunks
//
// UNoctisSaveGame stores that blob opaquely (CoreBlob + CoreBlobFormat), compressed by Unreal, next to the Unreal-side
// state (player transforms, time scale, UI). Until the blob exists, a save keeps the world RECIPE (environment,
// profile, seed, simulated time) and loading rebuilds the valley from the same seed, then fast-forwards the clock at
// coarse steps (bounded): the landscape, rivers and species are identical, but individual animals, the player's
// observations, recordings and hypotheses are NOT restored. This limitation is reported in the UI (bIsApproximate).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "NoctisSaveGame.generated.h"

/** Set to 1 once NoctisCore implements saveToBlob/loadFromBlob (see the header comment). */
#ifndef NOCTIS_CORE_HAS_BLOB_SAVE
#define NOCTIS_CORE_HAS_BLOB_SAVE 0
#endif

UCLASS()
class NOCTIS_API UNoctisSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentVersion = 1;

	UPROPERTY()
	int32 Version = CurrentVersion;

	// World recipe (always valid).
	UPROPERTY()
	FString EnvironmentId;

	UPROPERTY()
	FString ProfileId;

	UPROPERTY()
	int64 Seed = 0;

	UPROPERTY()
	double SimulatedSeconds = 0.0;

	UPROPERTY()
	FString ClockText;

	// Core state (empty until the core exposes saveToBlob).
	UPROPERTY()
	FName CoreBlobFormat;

	UPROPERTY()
	TArray<uint8> CoreBlob;

	UPROPERTY()
	int32 CoreBlobUncompressedSize = 0;

	// Unreal-side state.
	UPROPERTY()
	FTransform PlayerTransform;

	UPROPERTY()
	FRotator ControlRotation;

	UPROPERTY()
	bool bPlayerInVehicle = false;

	UPROPERTY()
	FTransform VehicleTransform;

	UPROPERTY()
	float TimeScale = 1.0f;

	UPROPERTY()
	FDateTime SavedAt;
};

UCLASS()
class NOCTIS_API UNoctisSaveLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Noctis|Save", meta = (WorldContext = "WorldContextObject"))
	static bool SaveExpedition(const UObject* WorldContextObject, const FString& SlotName);

	/** bIsApproximate = true when only the world recipe could be restored (core blob API not available yet). */
	UFUNCTION(BlueprintCallable, Category = "Noctis|Save", meta = (WorldContext = "WorldContextObject"))
	static bool LoadExpedition(const UObject* WorldContextObject, const FString& SlotName, bool& bIsApproximate);

	/** Maximum simulated hours replayed when restoring from the recipe (coarse steps, time-accelerated LOD). */
	static constexpr double MaxFastForwardHours = 48.0;
};
