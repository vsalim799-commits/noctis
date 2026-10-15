// UNoctisCallSoundWave — a procedural sound wave rendered by the core's SignalSynth.
//
// The signal is synthesised exactly as the player's microphones would record it at the listener's distance:
// harmonic structure and frequency contour from the species call, per-harmonic ISO 9613-1 absorption, foliage /
// wind / terrain terms from the core's propagation model (FNoctisSoundExtra::ExtraGainDb). Unreal then only
// spatialises it (attenuation asset with distance attenuation OFF, spatialisation ON) — so what the player hears
// and what the recorder captures are the same physics. Pascals are mapped to digital full scale with
// UNoctisSettings::AudioFullScaleDbSpl.
// Rendering happens on a background task; the audio render thread outputs silence until it is ready (a few ms).
#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"

#include "Noctis/World/SoundField.h"

#include <atomic>

#include "NoctisCallSoundWave.generated.h"

/** Shared between the render task and the audio render thread (outlives the UObject if needed). */
struct FNoctisCallRenderBuffer
{
	TArray<int16> Samples;
	std::atomic<bool> bReady{false};
};

UCLASS()
class NOCTIS_API UNoctisCallSoundWave : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	UNoctisCallSoundWave(const FObjectInitializer& ObjectInitializer);

	/** Starts the background render. TemperatureC/Humidity: air at the time of the call (absorption). */
	void StartRender(const noctis::SoundSource& Source, float DistanceM, float ExtraGainDb, float TemperatureC, float Humidity, float FullScaleDbSpl);

	/** Length of the rendered signal in seconds (valid immediately). */
	float GetRenderedDurationSeconds() const { return RenderedDuration; }

	// USoundWaveProcedural
	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;
	virtual Audio::EAudioMixerStreamDataFormat::Type GetGeneratedPCMDataFormat() const override;

	static constexpr int32 RenderSampleRate = 48000;

private:
	TSharedPtr<FNoctisCallRenderBuffer, ESPMode::ThreadSafe> Buffer;
	int32 ReadPosition = 0; // audio render thread only
	float RenderedDuration = 0.0f;
};
