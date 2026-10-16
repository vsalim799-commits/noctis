// ANoctisAudioDirector — plays the simulated soundscape.
//
// Every SoundSource the core emits (calls, thunder, splashes, crashes) is scheduled at its physical arrival time
// (emission time + distance / speed of sound at the current air temperature) and played with the level the core's
// propagation model predicts at the researcher's ears. Calls are synthesised by UNoctisCallSoundWave (SignalSynth) unless
// the species provides baked sounds; footsteps are handled by the creature actors' MetaSounds. Thunder, splashes and
// vegetation crashes use the baked libraries of Tools/AudioFactory (Data/Audio/manifest.json): the variation of the class
// closest to the event (thunder by distance, splash / crash by the emitter's mass) is played at the strike / emitter
// position and its level is corrected from the class reference to the level the core predicts. Ambient beds (rain, wind,
// insect chorus, birds) follow the weather and the population layer: the bed assets are calibrated by import_audio.py
// to play at their manifest ambient level (level.ambient_spl_db) at volume 1, so the director applies only the
// difference between the core's level and that reference. At high time acceleration event sounds are skipped.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Noctis/World/SoundField.h"

#include "NoctisAudioDirector.generated.h"

class UAudioComponent;
class UNoctisCallSoundWave;
class USoundAttenuation;
class USoundBase;
class USoundConcurrency;
struct FNoctisFrameOutput;
struct FNoctisSoundExtra;

UCLASS(Blueprintable)
class NOCTIS_API ANoctisAudioDirector : public AActor
{
	GENERATED_BODY()

public:
	ANoctisAudioDirector();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Sounds whose predicted level at the ears is below this (dB SPL) are not played. */
	UPROPERTY(EditAnywhere, Category = "Noctis")
	float AudibleThresholdDb = 12.0f;

	/** Above this time scale only ambient beds play (event sounds would pile up). */
	UPROPERTY(EditAnywhere, Category = "Noctis")
	float MaxTimeScaleForEvents = 4.0f;

	/** Rain bed (default: amb_hc_rain_heavy_forest). */
	UPROPERTY(EditAnywhere, Category = "Noctis|Ambience")
	TObjectPtr<USoundBase> RainLoop;

	/** Ambient level (dB SPL) the rain bed asset plays at volume 1: manifest level.ambient_spl_db of RainLoop. */
	UPROPERTY(EditAnywhere, Category = "Noctis|Ambience")
	float RainLoopAmbientDb = 70.07f;

	/** Wind bed (default: amb_hc_wind_broadleaf_moderate). */
	UPROPERTY(EditAnywhere, Category = "Noctis|Ambience")
	TObjectPtr<USoundBase> WindLoop;

	/** Ambient level (dB SPL) the wind bed asset plays at volume 1: manifest level.ambient_spl_db of WindLoop. */
	UPROPERTY(EditAnywhere, Category = "Noctis|Ambience")
	float WindLoopAmbientDb = 30.91f;

	UPROPERTY(EditAnywhere, Category = "Noctis|Ambience")
	TObjectPtr<USoundBase> InsectLoop;

	UPROPERTY(EditAnywhere, Category = "Noctis|Ambience")
	TObjectPtr<USoundBase> BirdLoop;

private:
	struct FScheduledSound
	{
		noctis::SoundSource Source;
		float DistanceM = 0.0f;
		float ReceivedLevelDb = -100.0f;
		float ExtraGainDb = 0.0f;
		double ArrivalSimTime = 0.0;
		float TemperatureC = 15.0f;
		float Humidity = 0.7f;
	};

	struct FPlaying
	{
		TWeakObjectPtr<UAudioComponent> Component;
		double EndRealTime = 0.0;
		bool bProcedural = false;
	};

	/** Variations of one class of a baked event library and the level (dB SPL) they represent at volume 1. */
	struct FBakedClass
	{
		float Key = 0.0f;          // class distance (m) for thunder, class mass (kg) for splash / crash
		float ReferenceDb = 0.0f;  // splash / crash: level at 1 m; thunder: computed per event (core level at Key)
		TArray<USoundBase*> Sounds;
	};

	void OnFramePublished(const FNoctisFrameOutput& Front);
	void Play(const FScheduledSound& Sound);
	void LoadBakedLibraries();
	/** Baked asset for the source, and the level (dB SPL) the asset represents at volume multiplier 1. */
	USoundBase* FindBakedSound(const FScheduledSound& Sound, float& OutReferenceDb) const;
	void UpdateAmbience(const FNoctisFrameOutput& Front);
	UAudioComponent* EnsureLoop(TObjectPtr<UAudioComponent>& Slot, USoundBase* Sound, FName Name);

	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> CallAttenuation;

	UPROPERTY(Transient)
	TObjectPtr<USoundConcurrency> CallConcurrency;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ThunderSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> SplashSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> CrashSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ImpactSound;

	/** Keeps the baked event libraries referenced (FBakedClass holds raw pointers into it). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> BakedLibraryAssets;

	TArray<FBakedClass> ThunderClasses;
	TArray<FBakedClass> SplashClasses;
	TArray<FBakedClass> CrashClasses;

	/** Procedural waves kept alive while they play. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNoctisCallSoundWave>> LiveWaves;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> RainAudio;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> WindAudio;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> InsectAudio;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BirdAudio;

	TArray<FScheduledSound> Scheduled;
	TArray<FPlaying> Playing;
	FDelegateHandle FramePublishedHandle;
};
