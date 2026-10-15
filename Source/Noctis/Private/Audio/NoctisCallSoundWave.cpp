#include "Audio/NoctisCallSoundWave.h"

#include "Tasks/Task.h"

#include "Noctis/Research/Acoustics.h"

#include <vector>

UNoctisCallSoundWave::UNoctisCallSoundWave(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	NumChannels = 1;
	SetSampleRate(RenderSampleRate);
	bLooping = false;
	Buffer = MakeShared<FNoctisCallRenderBuffer, ESPMode::ThreadSafe>();
}

void UNoctisCallSoundWave::StartRender(const noctis::SoundSource& Source, float DistanceM, float ExtraGainDb, float TemperatureC, float Humidity,
									   float FullScaleDbSpl)
{
	// Tail for reverberant decay of noise-like sources (thunder rumbles on).
	const float Tail = Source.kind == noctis::SoundKind::Thunder ? 1.0f : 0.15f;
	RenderedDuration = FMath::Max(0.05f, Source.durationS) + Tail;
	Duration = RenderedDuration;
	TSharedPtr<FNoctisCallRenderBuffer, ESPMode::ThreadSafe> Target = Buffer;
	UE::Tasks::Launch(UE_SOURCE_LOCATION, [Target, Source, DistanceM, ExtraGainDb, TemperatureC, Humidity, FullScaleDbSpl, Length = RenderedDuration]() {
		noctis::PropagationEnv Env; // air only: vegetation/terrain terms are already folded into ExtraGainDb
		Env.temperatureC = TemperatureC;
		Env.humidity = Humidity;
		const int32 Count = FMath::CeilToInt(Length * float(RenderSampleRate));
		std::vector<float> Pascals(static_cast<size_t>(Count), 0.0f);
		// bufferStart = arrival time: the propagation delay is applied by the director when it schedules playback.
		const float SpeedOfSound = noctis::AcousticAnalysis::speedOfSound(TemperatureC);
		const double Arrival = Source.startTime + double(FMath::Max(1.0f, DistanceM) / SpeedOfSound);
		noctis::SignalSynth::render(Source, nullptr, Arrival, RenderSampleRate, DistanceM, ExtraGainDb, Env, Pascals);
		const float FullScalePa = 20e-6f * FMath::Pow(10.0f, FullScaleDbSpl / 20.0f);
		Target->Samples.SetNumUninitialized(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float Normalised = FMath::Clamp(Pascals[static_cast<size_t>(Index)] / FullScalePa, -1.0f, 1.0f);
			Target->Samples[Index] = static_cast<int16>(FMath::RoundToInt(Normalised * 32767.0f));
		}
		Target->bReady.store(true, std::memory_order_release);
	});
}

Audio::EAudioMixerStreamDataFormat::Type UNoctisCallSoundWave::GetGeneratedPCMDataFormat() const
{
	return Audio::EAudioMixerStreamDataFormat::Int16;
}

int32 UNoctisCallSoundWave::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
	// Audio render thread. Always deliver NumSamples (silence before the render is ready and after the end).
	OutAudio.SetNumZeroed(NumSamples * static_cast<int32>(sizeof(int16)));
	if (!Buffer.IsValid() || !Buffer->bReady.load(std::memory_order_acquire))
	{
		return NumSamples;
	}
	const TArray<int16>& Samples = Buffer->Samples;
	const int32 Available = FMath::Clamp(Samples.Num() - ReadPosition, 0, NumSamples);
	if (Available > 0)
	{
		FMemory::Memcpy(OutAudio.GetData(), Samples.GetData() + ReadPosition, Available * sizeof(int16));
		ReadPosition += Available;
	}
	return NumSamples;
}
