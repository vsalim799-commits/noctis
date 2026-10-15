#include "Environment/NoctisEnvironmentController.h"

#include "NoctisLog.h"
#include "NoctisSettings.h"
#include "NoctisSpace.h"
#include "Sim/NoctisWorldSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/WindDirectionalSourceComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/WindDirectionalSource.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

// ASkyAtmosphere and AVolumetricCloud are declared next to their components.
#include "Components/VolumetricCloudComponent.h"

namespace NoctisEnvironmentImpl
{
const FName MoonTag(TEXT("Moon"));
// Clear-air extinction used by the core's visibility model (Koschmieder, 40 km visual range).
constexpr float BetaAir = 3.912f / 40000.0f;
} // namespace NoctisEnvironmentImpl

ANoctisEnvironmentController::ANoctisEnvironmentController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ANoctisEnvironmentController::BeginPlay()
{
	Super::BeginPlay();
	FindSceneActors();
	WeatherParameters = UNoctisSettings::Get().WeatherParameters.LoadSynchronous();
	if (!WeatherParameters)
	{
		UE_LOG(LogNoctis, Warning, TEXT("MPC_NoctisWeather introuvable : les matériaux ne recevront pas la météo (lancer setup_project.py)."));
	}
}

void ANoctisEnvironmentController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (RainComponent)
	{
		RainComponent->DestroyComponent();
		RainComponent = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void ANoctisEnvironmentController::FindSceneActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		ADirectionalLight* Light = *It;
		if (Light->ActorHasTag(NoctisEnvironmentImpl::MoonTag))
		{
			MoonLight = MoonLight ? MoonLight.Get() : Light;
		}
		else if (!SunLight)
		{
			SunLight = Light;
		}
	}
	if (!SkyAtmosphere)
	{
		TActorIterator<ASkyAtmosphere> It(World);
		SkyAtmosphere = It ? *It : nullptr;
	}
	if (!HeightFog)
	{
		TActorIterator<AExponentialHeightFog> It(World);
		HeightFog = It ? *It : nullptr;
	}
	if (!VolumetricCloud)
	{
		TActorIterator<AVolumetricCloud> It(World);
		VolumetricCloud = It ? *It : nullptr;
	}
	if (!SkyLight)
	{
		TActorIterator<ASkyLight> It(World);
		SkyLight = It ? *It : nullptr;
	}
	if (!WindSource)
	{
		TActorIterator<AWindDirectionalSource> It(World);
		WindSource = It ? *It : nullptr;
	}
	if (SunLight)
	{
		if (UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(SunLight->GetLightComponent()))
		{
			Sun->SetMobility(EComponentMobility::Movable);
			Sun->SetAtmosphereSunLight(true);
			Sun->SetAtmosphereSunLightIndex(0);
		}
	}
	if (MoonLight)
	{
		if (UDirectionalLightComponent* Moon = Cast<UDirectionalLightComponent>(MoonLight->GetLightComponent()))
		{
			Moon->SetMobility(EComponentMobility::Movable);
			Moon->SetAtmosphereSunLight(true);
			Moon->SetAtmosphereSunLightIndex(1);
		}
	}
	if (!SunLight || !HeightFog || !SkyAtmosphere)
	{
		UE_LOG(LogNoctis, Warning, TEXT("Environnement : soleil %s, brouillard %s, atmosphère %s (placer ces acteurs dans la carte)."),
			   SunLight ? TEXT("ok") : TEXT("absent"), HeightFog ? TEXT("ok") : TEXT("absent"), SkyAtmosphere ? TEXT("ok") : TEXT("absent"));
	}
}

void ANoctisEnvironmentController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	const FNoctisFrameOutput* Front = Sub ? Sub->GetFrontFrame() : nullptr;
	const FNoctisFrameOutput* Previous = Sub ? Sub->GetPreviousFrame() : nullptr;
	if (!Front || !Previous)
	{
		return;
	}
	ApplyFrame(*Previous, *Front, Sub->GetInterpolationAlpha(), DeltaSeconds);
	PushParameters(DeltaSeconds);
	OnEnvironmentUpdated(State);
}

void ANoctisEnvironmentController::ApplyFrame(const FNoctisFrameOutput& Previous, const FNoctisFrameOutput& Front, float Alpha, float DeltaSeconds)
{
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	const FNoctisSpace& Space = Sub->GetSpace();
	const UNoctisSettings& Settings = UNoctisSettings::Get();
	const noctis::SkyState& SkyA = Previous.Snapshot.sky;
	const noctis::SkyState& SkyB = Front.Snapshot.sky;
	const noctis::WeatherState& W = Front.Snapshot.weather;

	// ---- Sun and moon: interpolated directions (time acceleration makes snapshots far apart) ----------
	const noctis::Vec3 SunDir = noctis::lerp(SkyA.sunDirection, SkyB.sunDirection, Alpha).normalized();
	const noctis::Vec3 MoonDir = noctis::lerp(SkyA.moonDirection, SkyB.moonDirection, Alpha).normalized();
	const float SunElevation = FMath::Lerp(SkyA.sunElevationDeg, SkyB.sunElevationDeg, Alpha);
	const float MoonElevation = FMath::Lerp(SkyA.moonElevationDeg, SkyB.moonElevationDeg, Alpha);

	if (SunLight)
	{
		if (UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(SunLight->GetLightComponent()))
		{
			// A directional light shines along its forward axis: from the sun towards the ground.
			Sun->SetWorldRotation((-FNoctisSpace::DirectionToUE(SunDir)).Rotation());
			// Direct beam: clear-sky illuminance from the core. With volumetric clouds the cloud shadows do the dimming;
			// without them the core's cloud attenuation is applied here.
			float Lux = noctis::SkySystem::clearSkyIlluminance(SunElevation);
			if (!VolumetricCloud)
			{
				Lux *= 1.0f - 0.75f * W.cloudCover;
			}
			Sun->SetIntensity(FMath::Max(0.0f, Lux * Settings.SunIlluminanceScale));
			Sun->SetVisibility(SunElevation > -4.0f);
		}
	}
	if (MoonLight)
	{
		if (UDirectionalLightComponent* Moon = Cast<UDirectionalLightComponent>(MoonLight->GetLightComponent()))
		{
			Moon->SetWorldRotation((-FNoctisSpace::DirectionToUE(MoonDir)).Rotation());
			// Full moon high in a clear sky gives ~0.25 lux at the ground.
			const float MoonLux = 0.25f * SkyB.moonIllumination * FMath::Clamp(FMath::Sin(FMath::DegreesToRadians(MoonElevation)), 0.0f, 1.0f) *
								  (1.0f - 0.8f * W.cloudCover);
			Moon->SetIntensity(MoonLux * Settings.MoonIlluminanceScale);
			Moon->SetVisibility(MoonElevation > -2.0f && SunElevation < 4.0f);
		}
	}

	// ---- Fog: two exponential layers reproducing the core's profile ---------------------------------
	// Core: extinction(h) = e0 * (0.25 + 0.75 * exp(-h / 8 m)) above the valley floor, plus clear air and rain.
	// Unreal (5.x): density per cm = FogDensity / 1000 at the fog height, decaying as exp2(-Falloff / 1000 * dz_cm)
	// (VERIFY on the target version: FExponentialHeightFogSceneInfo divides FogDensity and FogHeightFalloff by 1000).
	//   => FogDensity = 10 * extinction(1/m)   and   Falloff = 1000 / (H_m * 100 * ln 2) for a scale height H_m.
	if (HeightFog)
	{
		if (UExponentialHeightFogComponent* Fog = HeightFog->GetComponent())
		{
			const float E0 = FMath::Lerp(Previous.Snapshot.weather.fogExtinction, W.fogExtinction, Alpha);
			const float BetaRain = 0.00045f * FMath::Pow(FMath::Max(0.0f, W.precipMmPerHour), 0.7f);
			const float Uniform = NoctisEnvironmentImpl::BetaAir + BetaRain + 0.25f * E0;
			const float Pooled = 0.75f * E0;
			const float PoolFalloff = 1000.0f / (FogPoolingScaleHeightM * 100.0f * 0.69314718f);
			Fog->SetWorldLocation(FVector(Fog->GetComponentLocation().X, Fog->GetComponentLocation().Y,
										  Space.OffsetCm.Z + double(Front.ValleyFloorZ) * FNoctisSpace::CmPerM));
			Fog->SetFogDensity(Uniform * FogDensityPerExtinction);
			Fog->SetFogHeightFalloff(0.02f);
			Fog->SetSecondFogDensity(Pooled * FogDensityPerExtinction);
			Fog->SetSecondFogHeightFalloff(PoolFalloff);
			Fog->SetSecondFogHeightOffset(0.0f);
		}
	}

	// ---- Atmosphere haze: Mie scattering follows humidity (default Unreal Mie scale 0.003996) ----------
	if (SkyAtmosphere)
	{
		if (USkyAtmosphereComponent* Atmosphere = SkyAtmosphere->GetComponent())
		{
			const float Haze = 0.5f + 1.5f * W.humidity * W.humidity + 0.5f * W.dryness;
			Atmosphere->SetMieScatteringScale(0.003996f * Haze);
		}
	}

	// ---- Wind ---------------------------------------------------------------------------------------
	const FVector WindUE = FNoctisSpace::VelocityToUE(W.wind) / 100.0; // m/s, Unreal axes
	const FVector WindDir = WindUE.GetSafeNormal2D(UE_SMALL_NUMBER, FVector::ForwardVector);
	if (WindSource)
	{
		WindSource->SetActorRotation(WindDir.Rotation());
		if (UWindDirectionalSourceComponent* Wind = WindSource->GetComponent())
		{
			Wind->SetStrength(FMath::Clamp(W.windSpeedMs / 15.0f, 0.0f, 2.0f));
			Wind->SetSpeed(W.windSpeedMs);
			Wind->SetMinimumGustAmount(0.0f);
			Wind->SetMaximumGustAmount(FMath::Clamp(W.gustMs / FMath::Max(1.0f, W.windSpeedMs), 0.0f, 2.0f));
		}
	}

	// ---- Sky light recapture (only needed when it is not in real-time capture mode) -------------------
	if (SkyLight)
	{
		if (USkyLightComponent* Sky = SkyLight->GetLightComponent())
		{
			if (!Sky->bRealTimeCapture && FMath::Abs(SunElevation - LastCapturedSunElevation) > SkyRecaptureDeg)
			{
				Sky->RecaptureSky();
				LastCapturedSunElevation = SunElevation;
			}
		}
	}

	// ---- Rain particles around the camera ------------------------------------------------------------
	if (RainSystem)
	{
		if (!RainComponent)
		{
			if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
			{
				if (PC->PlayerCameraManager)
				{
					RainComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(RainSystem, PC->PlayerCameraManager->GetRootComponent(), NAME_None,
																				 FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false);
				}
			}
		}
		if (RainComponent)
		{
			RainComponent->SetVariableFloat(FName(TEXT("User.RainRate")), W.precipMmPerHour);
			RainComponent->SetVariableVec3(FName(TEXT("User.Wind")), WindUE * 100.0);
		}
	}

	// ---- Lightning: thunder sources carry the strike position (500 m above it) ------------------------
	if (Front.Sequence != LastSoundSequence)
	{
		LastSoundSequence = Front.Sequence;
		for (const noctis::SoundSource& Sound : Front.Snapshot.newSounds)
		{
			if (Sound.kind == noctis::SoundKind::Thunder)
			{
				LightningFlash = 1.0f;
				OnLightning(Space.ToUE(Sound.position - noctis::Vec3(0.0f, 0.0f, 500.0f)));
			}
		}
	}
	LightningFlash = FMath::Max(0.0f, LightningFlash - DeltaSeconds * 4.0f);

	// ---- Blueprint state ------------------------------------------------------------------------------
	State.HourOfDay = Front.Snapshot.hourOfDay;
	State.DayOfYear = Front.Snapshot.dayOfYear;
	State.SunElevationDeg = SunElevation;
	State.IlluminanceLux = FMath::Lerp(SkyA.illuminanceLux, SkyB.illuminanceLux, Alpha);
	State.MoonIllumination = SkyB.moonIllumination;
	State.Regime = static_cast<ENoctisWeatherRegime>(W.regime);
	State.TemperatureC = W.temperatureC;
	State.Humidity = W.humidity;
	State.CloudCover = FMath::Lerp(Previous.Snapshot.weather.cloudCover, W.cloudCover, Alpha);
	State.RainMmPerHour = W.precipMmPerHour;
	State.WindSpeedMs = W.windSpeedMs;
	State.WindDirection = WindDir;
	State.FogExtinctionPerM = W.fogExtinction;
	State.VisibilityM = W.visibilityM;
	State.RiverStageM = Front.Snapshot.riverStageM;
	State.bFlooding = Front.Snapshot.flooding;
	State.GroundWetness = FMath::Clamp(FMath::Max(W.precipMmPerHour / 2.0f, FMath::Exp(-static_cast<float>(W.daysSinceRain) * 1.5f)), 0.0f, 1.0f);
}

void ANoctisEnvironmentController::PushParameters(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !WeatherParameters)
	{
		return;
	}
	UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(WeatherParameters);
	if (!Instance)
	{
		return;
	}
	// Parameter names created by Content/Python/setup_project.py (MPC_NoctisWeather).
	Instance->SetScalarParameterValue(TEXT("CloudCover"), State.CloudCover);
	Instance->SetScalarParameterValue(TEXT("RainRate"), State.RainMmPerHour);
	Instance->SetScalarParameterValue(TEXT("GroundWetness"), State.GroundWetness);
	Instance->SetScalarParameterValue(TEXT("WindSpeed"), State.WindSpeedMs);
	Instance->SetVectorParameterValue(TEXT("WindDirection"), FLinearColor(float(State.WindDirection.X), float(State.WindDirection.Y), 0.0f, 0.0f));
	Instance->SetScalarParameterValue(TEXT("FogExtinction"), State.FogExtinctionPerM);
	Instance->SetScalarParameterValue(TEXT("Illuminance"), State.IlluminanceLux);
	Instance->SetScalarParameterValue(TEXT("SunElevation"), State.SunElevationDeg);
	Instance->SetScalarParameterValue(TEXT("Temperature"), State.TemperatureC);
	Instance->SetScalarParameterValue(TEXT("Humidity"), State.Humidity);
	Instance->SetScalarParameterValue(TEXT("LightningFlash"), LightningFlash);
	Instance->SetScalarParameterValue(TEXT("RiverStage"), State.RiverStageM);
	Instance->SetScalarParameterValue(TEXT("Flooding"), State.bFlooding ? 1.0f : 0.0f);
	Instance->SetScalarParameterValue(TEXT("HourOfDay"), State.HourOfDay);
	(void)DeltaSeconds;
}
