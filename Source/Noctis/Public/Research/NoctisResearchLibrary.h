// Blueprint access to the player's science (journal, catalogue, hypotheses, recordings, map, reports).
// Read functions take a short exclusive lock on the simulation (UNoctisWorldSubsystem::WithSimulation: they wait
// for the running simulation job, at most one job ~ a few ms) — call them when a panel opens or refreshes, not
// every frame. Heavy work (spectral analysis) is asynchronous and runs on the simulation thread.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "Research/NoctisResearchTypes.h"

#include "NoctisResearchLibrary.generated.h"

class UTexture2D;

DECLARE_DYNAMIC_DELEGATE_OneParam(FNoctisRecordingAnalyzed, int32, RecordingId);

UCLASS()
class NOCTIS_API UNoctisResearchLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Noctis|Research", meta = (WorldContext = "WorldContextObject"))
	static TArray<FNoctisSpeciesView> GetSpeciesInWorld(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "Noctis|Research", meta = (WorldContext = "WorldContextObject"))
	static TArray<FNoctisObservationView> GetObservations(const UObject* WorldContextObject, int32 MaxCount = 200);

	UFUNCTION(BlueprintCallable, Category = "Noctis|Research", meta = (WorldContext = "WorldContextObject"))
	static TArray<FNoctisCatalogEntryView> GetCatalog(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "Noctis|Research", meta = (WorldContext = "WorldContextObject"))
	static bool RenameCatalogEntry(const UObject* WorldContextObject, int32 EntryId, const FString& Nickname);

	UFUNCTION(BlueprintCallable, Category = "Noctis|Research", meta = (WorldContext = "WorldContextObject"))
	static TArray<FNoctisHypothesisView> GetHypotheses(const UObject* WorldContextObject);

	/** Returns the new hypothesis id (0 when the draft is invalid: unknown measure or species). */
	UFUNCTION(BlueprintCallable, Category = "Noctis|Research", meta = (WorldContext = "WorldContextObject"))
	static int32 CreateHypothesis(const UObject* WorldContextObject, const FNoctisHypothesisDraft& Draft);

	UFUNCTION(BlueprintCallable, Category = "Noctis|Research", meta = (WorldContext = "WorldContextObject"))
	static FNoctisHypothesisView TestHypothesis(const UObject* WorldContextObject, int32 HypothesisId, bool bCurrentExpeditionOnly);

	UFUNCTION(BlueprintCallable, Category = "Noctis|Research", meta = (WorldContext = "WorldContextObject"))
	static TArray<FNoctisRecordingView> GetRecordings(const UObject* WorldContextObject);

	/** FFT spectrogram + event detection on the simulation thread; OnDone fires on the game thread. */
	UFUNCTION(BlueprintCallable, Category = "Noctis|Research", meta = (WorldContext = "WorldContextObject"))
	static void AnalyzeRecordingAsync(const UObject* WorldContextObject, int32 RecordingId, FNoctisRecordingAnalyzed OnDone);

	UFUNCTION(BlueprintCallable, Category = "Noctis|Research", meta = (WorldContext = "WorldContextObject"))
	static TArray<FNoctisAcousticEventView> GetRecordingEvents(const UObject* WorldContextObject, int32 RecordingId);

	/** Spectrogram of an analysed recording (x = time, y = frequency, top = MaxFrequencyHz), magma-like colour map. */
	UFUNCTION(BlueprintCallable, Category = "Noctis|Research", meta = (WorldContext = "WorldContextObject"))
	static UTexture2D* BuildSpectrogramTexture(const UObject* WorldContextObject, int32 RecordingId, float MinDb = 10.0f, float MaxDb = 90.0f,
											   float MaxFrequencyHz = 2000.0f);

	UFUNCTION(BlueprintCallable, Category = "Noctis|Research", meta = (WorldContext = "WorldContextObject"))
	static bool ExportRecordingWav(const UObject* WorldContextObject, int32 RecordingId, const FString& FilePath);

	/** The player's map: shaded relief where explored, observation effort as a warm overlay, unexplored ground dimmed. */
	UFUNCTION(BlueprintCallable, Category = "Noctis|Research", meta = (WorldContext = "WorldContextObject"))
	static UTexture2D* BuildFieldMapTexture(const UObject* WorldContextObject, int32 Resolution = 512, bool bShowEffort = true);

	UFUNCTION(BlueprintCallable, Category = "Noctis|Research", meta = (WorldContext = "WorldContextObject"))
	static TArray<FNoctisMapFeatureView> GetMapFeatures(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Noctis|Research", meta = (WorldContext = "WorldContextObject"))
	static FVector2D WorldToMapUV(const UObject* WorldContextObject, FVector WorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Noctis|Research", meta = (WorldContext = "WorldContextObject"))
	static TArray<FNoctisReportView> GetReports(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "Noctis|Research", meta = (WorldContext = "WorldContextObject"))
	static void AddFieldNote(const UObject* WorldContextObject, const FString& Text);
};
