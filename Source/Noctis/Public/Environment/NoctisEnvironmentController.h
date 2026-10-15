// ANoctisEnvironmentController — makes the Unreal sky, light, fog, clouds and wind follow the core's SkySystem
// and WeatherSystem. The core is authoritative: sun and moon positions come from the environment's latitude,
// axial tilt and paleo-calendar; fog from its cold-air-pooling model; clouds, rain and wind from the regime
// Markov chain. Unreal only renders them. Values are also pushed to MPC_NoctisWeather for materials/Niagara.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "NoctisTypes.h"

#include "NoctisEnvironmentController.generated.h"

class ADirectionalLight;
class AExponentialHeightFog;
class ASkyAtmosphere;
class ASkyLight;
class AVolumetricCloud;
class AWindDirectionalSource;
class UMaterialParameterCollection;
class UNiagaraComponent;
class UNiagaraSystem;
struct FNoctisFrameOutput;

/** Blueprint view of the simulated environment (for UI and level scripting). */
USTRUCT(BlueprintType)
struct NOCTIS_API FNoctisEnvironmentState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float HourOfDay = 12.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float DayOfYear = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float SunElevationDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float IlluminanceLux = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float MoonIllumination = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	ENoctisWeatherRegime Regime = ENoctisWeatherRegime::Clear;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float TemperatureC = 15.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float Humidity = 0.7f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float CloudCover = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float RainMmPerHour = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float WindSpeedMs = 0.0f;

	/** Direction the wind blows TOWARDS, Unreal world space (unit, horizontal). */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	FVector WindDirection = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float FogExtinctionPerM = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float VisibilityM = 30000.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float RiverStageM = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	bool bFlooding = false;

	/** Ground wetness 0..1 (recent rain), for puddles and wet shading. */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float GroundWetness = 0.0f;
};

UCLASS(Blueprintable)
class NOCTIS_API ANoctisEnvironmentController : public AActor
{
	GENERATED_BODY()

public:
	ANoctisEnvironmentController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "Noctis")
	const FNoctisEnvironmentState& GetEnvironmentState() const { return State; }

	/** Called every frame after the scene was updated (designer hook). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Noctis")
	void OnEnvironmentUpdated(const FNoctisEnvironmentState& NewState);

	/** A lightning strike happened (thunder follows physically, distance / speed of sound later). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Noctis")
	void OnLightning(FVector Location);

	// Scene actors. Left empty they are found in the level (moon: a directional light tagged "Moon").
	UPROPERTY(EditInstanceOnly, Category = "Scene")
	TObjectPtr<ADirectionalLight> SunLight;

	UPROPERTY(EditInstanceOnly, Category = "Scene")
	TObjectPtr<ADirectionalLight> MoonLight;

	UPROPERTY(EditInstanceOnly, Category = "Scene")
	TObjectPtr<ASkyAtmosphere> SkyAtmosphere;

	UPROPERTY(EditInstanceOnly, Category = "Scene")
	TObjectPtr<AExponentialHeightFog> HeightFog;

	UPROPERTY(EditInstanceOnly, Category = "Scene")
	TObjectPtr<AVolumetricCloud> VolumetricCloud;

	UPROPERTY(EditInstanceOnly, Category = "Scene")
	TObjectPtr<ASkyLight> SkyLight;

	UPROPERTY(EditInstanceOnly, Category = "Scene")
	TObjectPtr<AWindDirectionalSource> WindSource;

	/** Rain/drizzle particles following the camera (user parameters: User.RainRate mm/h, User.Wind cm/s). */
	UPROPERTY(EditAnywhere, Category = "Scene")
	TObjectPtr<UNiagaraSystem> RainSystem;

	/** Scale applied to the core's fog extinction (1/m) to obtain Unreal's FogDensity (see the .cpp derivation). */
	UPROPERTY(EditAnywhere, Category = "Tuning", meta = (ClampMin = "0.0"))
	float FogDensityPerExtinction = 10.0f;

	/** Height (m) of the cold-air pooling layer: the core uses exp(-h / 8 m). */
	UPROPERTY(EditAnywhere, Category = "Tuning", meta = (ClampMin = "0.5"))
	float FogPoolingScaleHeightM = 8.0f;

	/** Recapture a non-real-time sky light when the sun moved more than this (degrees). */
	UPROPERTY(EditAnywhere, Category = "Tuning")
	float SkyRecaptureDeg = 2.0f;

private:
	void FindSceneActors();
	void ApplyFrame(const FNoctisFrameOutput& Previous, const FNoctisFrameOutput& Front, float Alpha, float DeltaSeconds);
	void PushParameters(float DeltaSeconds);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> WeatherParameters;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> RainComponent;

	FNoctisEnvironmentState State;
	float LightningFlash = 0.0f;
	float LastCapturedSunElevation = -1000.0f;
	uint64 LastSoundSequence = 0;
};
