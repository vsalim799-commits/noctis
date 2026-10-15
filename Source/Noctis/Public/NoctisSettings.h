// Project settings (Project Settings > Game > Noctis), stored in Config/DefaultEngine.ini [/Script/Noctis.NoctisSettings].
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "NoctisSettings.generated.h"

class UInputMappingContext;
class UMaterialParameterCollection;
class USoundAttenuation;
class USoundBase;
class USoundConcurrency;

UCLASS(config = Engine, defaultconfig, meta = (DisplayName = "Noctis"))
class NOCTIS_API UNoctisSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UNoctisSettings();

	static const UNoctisSettings& Get() { return *GetDefault<UNoctisSettings>(); }

	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }

	/** Resolves the scientific data root: override, then <Project>/Data, then <Content>/NoctisData. Absolute path. */
	FString ResolveDataRoot() const;

	// ------------------------------------------------------------------ World

	/** Absolute or project-relative path of the Data/ directory. Empty = automatic. */
	UPROPERTY(config, EditAnywhere, Category = "World")
	FString DataRootOverride;

	UPROPERTY(config, EditAnywhere, Category = "World")
	FString EnvironmentId = TEXT("hell_creek");

	UPROPERTY(config, EditAnywhere, Category = "World")
	FString ProfileId = TEXT("standard");

	UPROPERTY(config, EditAnywhere, Category = "World")
	int64 Seed = 7;

	/** Only used by the std::thread pool fallback (bUseParallelFor = false). 0 = hardware concurrency. */
	UPROPERTY(config, EditAnywhere, Category = "World", meta = (ClampMin = "0"))
	int32 Threads = 0;

	UPROPERTY(config, EditAnywhere, Category = "World", meta = (ClampMin = "0.0"))
	float PopulationScale = 1.0f;

	UPROPERTY(config, EditAnywhere, Category = "World", meta = (ClampMin = "129"))
	int32 HeightSamples = 2017;

	UPROPERTY(config, EditAnywhere, Category = "World", meta = (ClampMin = "64"))
	int32 MaterialCells = 1024;

	UPROPERTY(config, EditAnywhere, Category = "World", meta = (ClampMin = "4.0"))
	float VegetationCellM = 16.0f;

	UPROPERTY(config, EditAnywhere, Category = "World")
	double StartDayOfYear = 140.0;

	UPROPERTY(config, EditAnywhere, Category = "World")
	double StartHour = 5.0;

	/** Offset between the core frame (x east, y north, z up, metres) and the Unreal world (cm). */
	UPROPERTY(config, EditAnywhere, Category = "World")
	FVector WorldOffsetCm = FVector::ZeroVector;

	// ------------------------------------------------------------------ Stepping

	/** Fixed simulation step (simulated seconds) while time runs at <= FullDetailMaxTimeScale. */
	UPROPERTY(config, EditAnywhere, Category = "Stepping", meta = (ClampMin = "0.005", ClampMax = "0.5"))
	float FixedStepSeconds = 0.05f;

	/** Step used at high time acceleration (the core then suspends full-detail locomotion). */
	UPROPERTY(config, EditAnywhere, Category = "Stepping", meta = (ClampMin = "0.05", ClampMax = "10.0"))
	float CoarseStepSeconds = 2.0f;

	UPROPERTY(config, EditAnywhere, Category = "Stepping", meta = (ClampMin = "1.0"))
	float FullDetailMaxTimeScale = 8.0f;

	UPROPERTY(config, EditAnywhere, Category = "Stepping", meta = (ClampMin = "1"))
	int32 MaxStepsPerJob = 64;

	/** Real seconds of simulation debt kept when the core cannot keep up (older debt is dropped: time slows down). */
	UPROPERTY(config, EditAnywhere, Category = "Stepping", meta = (ClampMin = "0.0"))
	float MaxBacklogSeconds = 0.25f;

	/** Step on a background task (double-buffered snapshots). Off = step on the game thread (debugging). */
	UPROPERTY(config, EditAnywhere, Category = "Stepping")
	bool bAsyncSimulation = true;

	/** Use the engine's task graph (ParallelFor) for the core's parallel phases instead of its own std::thread pool. */
	UPROPERTY(config, EditAnywhere, Category = "Stepping")
	bool bUseParallelFor = true;

	/** Build the world on a background task so that loading the map does not freeze the game thread. */
	UPROPERTY(config, EditAnywhere, Category = "Stepping")
	bool bAsyncBoot = true;

	// ------------------------------------------------------------------ Presentation

	/** Spawn the creature manager, environment controller, water, vegetation and audio actors when absent from the level. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	bool bAutoSpawnPresentationActors = true;

	/** Interval (real seconds) of the auxiliary export: footprints, trails, trampling and water patch around the camera. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation", meta = (ClampMin = "0.1"))
	float AuxDataIntervalSeconds = 1.0f;

	UPROPERTY(config, EditAnywhere, Category = "Presentation", meta = (ClampMin = "50.0"))
	float AuxDataRadiusM = 600.0f;

	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	TSoftObjectPtr<UMaterialParameterCollection> WeatherParameters;

	/** Directional light intensity = clear-sky illuminance (lux) * scale. 1 = physical units (needs physical exposure). */
	UPROPERTY(config, EditAnywhere, Category = "Presentation", meta = (ClampMin = "0.0"))
	float SunIlluminanceScale = 1.0f;

	UPROPERTY(config, EditAnywhere, Category = "Presentation", meta = (ClampMin = "0.0"))
	float MoonIlluminanceScale = 1.0f;

	// ------------------------------------------------------------------ Audio

	/** Sound pressure level (dB SPL) mapped to digital full scale for procedurally synthesised calls. */
	UPROPERTY(config, EditAnywhere, Category = "Audio")
	float AudioFullScaleDbSpl = 110.0f;

	UPROPERTY(config, EditAnywhere, Category = "Audio", meta = (ClampMin = "0"))
	int32 MaxProceduralCalls = 12;

	UPROPERTY(config, EditAnywhere, Category = "Audio", meta = (ClampMin = "0.0"))
	float FootstepMaxDistanceM = 120.0f;

	/** Spatialisation only (distance attenuation off: the core already applied spreading and air absorption). */
	UPROPERTY(config, EditAnywhere, Category = "Audio")
	TSoftObjectPtr<USoundAttenuation> CallAttenuation;

	UPROPERTY(config, EditAnywhere, Category = "Audio")
	TSoftObjectPtr<USoundConcurrency> CallConcurrency;

	UPROPERTY(config, EditAnywhere, Category = "Audio")
	TSoftObjectPtr<USoundBase> ThunderSound;

	UPROPERTY(config, EditAnywhere, Category = "Audio")
	TSoftObjectPtr<USoundBase> SplashSound;

	UPROPERTY(config, EditAnywhere, Category = "Audio")
	TSoftObjectPtr<USoundBase> VegetationCrashSound;

	UPROPERTY(config, EditAnywhere, Category = "Audio")
	TSoftObjectPtr<USoundBase> ImpactSound;

	// ------------------------------------------------------------------ Input

	UPROPERTY(config, EditAnywhere, Category = "Input")
	TSoftObjectPtr<UInputMappingContext> ResearcherContext;

	UPROPERTY(config, EditAnywhere, Category = "Input")
	TSoftObjectPtr<UInputMappingContext> VehicleContext;

	UPROPERTY(config, EditAnywhere, Category = "Input")
	TSoftObjectPtr<UInputMappingContext> DroneContext;

	UPROPERTY(config, EditAnywhere, Category = "Input")
	TSoftObjectPtr<UInputMappingContext> ToolsContext;
};
