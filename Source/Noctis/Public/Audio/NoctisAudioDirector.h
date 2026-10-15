// ANoctisAudioDirector — plays the simulated soundscape.
//
// Every SoundSource the core emits (calls, thunder, splashes, crashes) is scheduled at its physical arrival time
// (emission time + distance / speed of sound at the current air temperature) and played with the level the core's
// propagation model predicts at the researcher's ears. Calls are synthesised by UNoctisCallSoundWave (SignalSynth) unless
// the species provides baked sounds; footsteps are handled by the creature actors' MetaSounds. Ambient beds (rain, wind,
// insect chorus, birds) follow the weather and the population layer. At high time acceleration event sounds are skipped.
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

	UPROPERTY(EditAnywhere, Category = "Noctis|Ambience")
	TObjectPtr<USoundBase> RainLoop;

	UPROPERTY(EditAnywhere, Category = "Noctis|Ambience")
	TObjectPtr<USoundBase> WindLoop;

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

	void OnFramePublished(const FNoctisFrameOutput& Front);
	void Play(const FScheduledSound& Sound);
	USoundBase* FindBakedSound(const noctis::SoundSource& Source) const;
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
