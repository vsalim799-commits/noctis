#include "Audio/NoctisAudioDirector.h"

#include "NoctisLog.h"
#include "NoctisSettings.h"

#include "Audio/NoctisCallSoundWave.h"
#include "Creatures/NoctisCreatureActor.h"
#include "Creatures/NoctisCreatureManager.h"
#include "Creatures/NoctisSpeciesVisualData.h"
#include "Sim/NoctisWorldSubsystem.h"

#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace NoctisAudioDirectorImpl
{
// Baked event libraries produced by Tools/AudioFactory (manifest ue5.random_group, 4 variations per class) and imported
// by Content/Python/import_audio.py under /Game/Audio/<Category>/<subcategory>/<id>. Class keys and reference levels
// mirror the manifest (ambience.py THUNDER_DISTANCES, foley.py SPLASH_CLASSES / CRASH_CLASSES and their level laws).
constexpr int32 Variations = 4;
constexpr float ThunderClassesM[] = {500.0f, 2000.0f, 5000.0f, 12000.0f};
// Thunder files start 80 ms before the first arrival (manifest parameters.file_starts_s_after_flash).
constexpr double ThunderPreRollS = 0.08;
struct FMassClass
{
	const TCHAR* Name;
	float MassKg;
};
constexpr FMassClass SplashMassClasses[] = {{TEXT("small"), 1.0f}, {TEXT("medium"), 30.0f}, {TEXT("large"), 500.0f}, {TEXT("huge"), 5000.0f}};
constexpr FMassClass CrashMassClasses[] = {{TEXT("medium"), 100.0f}, {TEXT("large"), 1000.0f}, {TEXT("huge"), 6000.0f}};

USoundBase* LoadSound(const FString& Folder, const FString& Name)
{
	const FString Path = FString::Printf(TEXT("%s/%s.%s"), *Folder, *Name, *Name);
	return LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}
} // namespace NoctisAudioDirectorImpl

ANoctisAudioDirector::ANoctisAudioDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ANoctisAudioDirector::BeginPlay()
{
	Super::BeginPlay();
	const UNoctisSettings& Settings = UNoctisSettings::Get();
	CallAttenuation = Settings.CallAttenuation.LoadSynchronous();
	CallConcurrency = Settings.CallConcurrency.LoadSynchronous();
	ThunderSound = Settings.ThunderSound.LoadSynchronous();
	SplashSound = Settings.SplashSound.LoadSynchronous();
	CrashSound = Settings.VegetationCrashSound.LoadSynchronous();
	ImpactSound = Settings.ImpactSound.LoadSynchronous();
	LoadBakedLibraries();
	if (UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this))
	{
		FramePublishedHandle = Sub->OnFramePublished.AddUObject(this, &ANoctisAudioDirector::OnFramePublished);
	}
}

void ANoctisAudioDirector::LoadBakedLibraries()
{
	using namespace NoctisAudioDirectorImpl;
	BakedLibraryAssets.Reset();
	ThunderClasses.Reset();
	SplashClasses.Reset();
	CrashClasses.Reset();
	auto Fill = [this](FBakedClass& Class, const FString& Folder, const FString& Stem) {
		for (int32 K = 1; K <= Variations; ++K)
		{
			if (USoundBase* Sound = LoadSound(Folder, FString::Printf(TEXT("%s_%02d"), *Stem, K)))
			{
				BakedLibraryAssets.Add(Sound);
				Class.Sounds.Add(Sound);
			}
		}
	};
	for (const float D : ThunderClassesM)
	{
		FBakedClass& Class = ThunderClasses.AddDefaulted_GetRef();
		Class.Key = D;
		Fill(Class, TEXT("/Game/Audio/Weather/thunder"), FString::Printf(TEXT("amb_thunder_%dm"), FMath::RoundToInt(D)));
	}
	for (const FMassClass& C : SplashMassClasses)
	{
		FBakedClass& Class = SplashClasses.AddDefaulted_GetRef();
		Class.Key = C.MassKg;
		Class.ReferenceDb = 40.0f + 8.0f * FMath::LogX(10.0f, C.MassKg); // foley.py: water stroke level law at 1 m
		Fill(Class, TEXT("/Game/Audio/Foley/water"), FString::Printf(TEXT("fol_water_stroke_%s"), C.Name));
	}
	for (const FMassClass& C : CrashMassClasses)
	{
		FBakedClass& Class = CrashClasses.AddDefaulted_GetRef();
		Class.Key = C.MassKg;
		Class.ReferenceDb = 55.0f + 8.0f * FMath::LogX(10.0f, C.MassKg / 100.0f); // foley.py: vegetation crash level law at 1 m
		Fill(Class, TEXT("/Game/Audio/Foley/vegetation_crash"), FString::Printf(TEXT("fol_vegetation_crash_%s"), C.Name));
	}
	// Default ambient beds (their reference levels are the UPROPERTY defaults RainLoopAmbientDb / WindLoopAmbientDb).
	if (!RainLoop)
	{
		RainLoop = LoadSound(TEXT("/Game/Audio/Weather/rain"), TEXT("amb_hc_rain_heavy_forest"));
	}
	if (!WindLoop)
	{
		WindLoop = LoadSound(TEXT("/Game/Audio/Ambience/wind_broadleaf"), TEXT("amb_hc_wind_broadleaf_moderate"));
	}
}

void ANoctisAudioDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this))
	{
		Sub->OnFramePublished.Remove(FramePublishedHandle);
	}
	for (FPlaying& Entry : Playing)
	{
		if (UAudioComponent* Component = Entry.Component.Get())
		{
			Component->Stop();
		}
	}
	Playing.Reset();
	LiveWaves.Reset();
	Scheduled.Reset();
	Super::EndPlay(EndPlayReason);
}

void ANoctisAudioDirector::OnFramePublished(const FNoctisFrameOutput& Front)
{
	if (Front.TimeScale > MaxTimeScaleForEvents)
	{
		Scheduled.Reset();
		return;
	}
	const noctis::WeatherState& Weather = Front.Snapshot.weather;
	const float SpeedOfSound = 331.3f + 0.606f * Weather.temperatureC;
	for (int32 Index = 0; Index < static_cast<int32>(Front.Snapshot.newSounds.size()); ++Index)
	{
		const noctis::SoundSource& Source = Front.Snapshot.newSounds[static_cast<size_t>(Index)];
		if (!Front.SoundExtras.IsValidIndex(Index))
		{
			continue;
		}
		const FNoctisSoundExtra& Extra = Front.SoundExtras[Index];
		switch (Source.kind)
		{
		case noctis::SoundKind::Vocalization:
		case noctis::SoundKind::Thunder:
		case noctis::SoundKind::Splash:
		case noctis::SoundKind::VegetationCrash:
		case noctis::SoundKind::Impact:
			break;
		default:
			continue; // footfalls: creature MetaSounds; engine/rotor/voice/mechanical: the player's own pawns
		}
		if (Extra.ReceivedLevelDb < AudibleThresholdDb)
		{
			continue;
		}
		FScheduledSound& Sound = Scheduled.AddDefaulted_GetRef();
		Sound.Source = Source;
		Sound.DistanceM = Extra.DistanceM;
		Sound.ReceivedLevelDb = Extra.ReceivedLevelDb;
		Sound.ExtraGainDb = Extra.ExtraGainDb;
		Sound.ArrivalSimTime = Source.startTime + double(Extra.DistanceM / SpeedOfSound);
		if (Source.kind == noctis::SoundKind::Thunder)
		{
			Sound.ArrivalSimTime -= NoctisAudioDirectorImpl::ThunderPreRollS; // baked files start before the first arrival
		}
		Sound.TemperatureC = Weather.temperatureC;
		Sound.Humidity = Weather.humidity;
	}
	UpdateAmbience(Front);
}

void ANoctisAudioDirector::Tick(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Noctis_AudioDirector);
	Super::Tick(DeltaSeconds);
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	const FNoctisFrameOutput* Front = Sub ? Sub->GetFrontFrame() : nullptr;
	const FNoctisFrameOutput* Previous = Sub ? Sub->GetPreviousFrame() : nullptr;
	if (!Front || !Previous)
	{
		return;
	}
	// Presentation time of the frame being rendered (the same interpolation the actors use).
	const double Now = Previous->Snapshot.time + double(Sub->GetInterpolationAlpha()) * (Front->Snapshot.time - Previous->Snapshot.time);

	// Loudest first, so that the concurrency budget keeps what the player would actually hear.
	Scheduled.Sort([](const FScheduledSound& A, const FScheduledSound& B) { return A.ReceivedLevelDb > B.ReceivedLevelDb; });
	for (int32 Index = Scheduled.Num() - 1; Index >= 0; --Index)
	{
		if (Scheduled[Index].ArrivalSimTime <= Now)
		{
			if (Now - Scheduled[Index].ArrivalSimTime < 1.0)
			{
				Play(Scheduled[Index]);
			}
			Scheduled.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}

	// Stop finished procedural sounds (procedural waves never end by themselves) and release their waves.
	const double RealNow = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	for (int32 Index = Playing.Num() - 1; Index >= 0; --Index)
	{
		UAudioComponent* Component = Playing[Index].Component.Get();
		if (!Component || RealNow >= Playing[Index].EndRealTime)
		{
			if (Component)
			{
				Component->Stop();
			}
			Playing.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}
	LiveWaves.RemoveAll([this](const TObjectPtr<UNoctisCallSoundWave>& Wave) {
		return !Playing.ContainsByPredicate([&Wave](const FPlaying& Entry) {
			const UAudioComponent* Component = Entry.Component.Get();
			return Component && Component->Sound.Get() == Wave.Get();
		});
	});
}

USoundBase* ANoctisAudioDirector::FindBakedSound(const FScheduledSound& Sound, float& OutReferenceDb) const
{
	const noctis::SoundSource& Source = Sound.Source;
	OutReferenceDb = Source.levelDb;
	auto Nearest = [](const TArray<FBakedClass>& Classes, float Value) -> const FBakedClass* {
		const FBakedClass* Best = nullptr;
		float BestDist = TNumericLimits<float>::Max();
		for (const FBakedClass& Class : Classes)
		{
			const float Dist = FMath::Abs(FMath::Loge(FMath::Max(Value, 1e-3f) / Class.Key));
			if (Class.Sounds.Num() > 0 && Dist < BestDist)
			{
				BestDist = Dist;
				Best = &Class;
			}
		}
		return Best;
	};
	auto Pick = [](const FBakedClass& Class, uint64 Seed) { return Class.Sounds[static_cast<int32>(Seed % static_cast<uint64>(Class.Sounds.Num()))]; };
	auto EmitterMassKg = [this](const noctis::SoundSource& S) {
		UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
		const FNoctisSpeciesInfo* Info = Sub ? Sub->GetSpeciesInfo(S.speciesIndex) : nullptr;
		return Info ? Info->AdultMassKg : 100.0f;
	};
	switch (Source.kind)
	{
	case noctis::SoundKind::Thunder:
		if (const FBakedClass* Class = Nearest(ThunderClasses, Sound.DistanceM))
		{
			// The file is calibrated on the level received at its class distance; refer it to the core's line-source
			// model at that distance (no wind / terrain) so that the director only applies the difference.
			OutReferenceDb = Source.levelDb - noctis::SoundField::spreadingLossDb(Source, Class->Key) -
							 noctis::SoundField::absorptionDbPerM(Source.f0Hz, Sound.TemperatureC, Sound.Humidity) * Class->Key;
			return Pick(*Class, Source.seed);
		}
		return ThunderSound;
	case noctis::SoundKind::Splash:
		if (const FBakedClass* Class = Nearest(SplashClasses, EmitterMassKg(Source)))
		{
			OutReferenceDb = Class->ReferenceDb;
			return Pick(*Class, Source.seed + Source.id);
		}
		return SplashSound;
	case noctis::SoundKind::VegetationCrash:
		if (const FBakedClass* Class = Nearest(CrashClasses, EmitterMassKg(Source)))
		{
			OutReferenceDb = Class->ReferenceDb;
			return Pick(*Class, Source.seed + Source.id);
		}
		return CrashSound;
	case noctis::SoundKind::Impact:
		return ImpactSound;
	default:
		break;
	}
	if (Source.kind != noctis::SoundKind::Vocalization)
	{
		return nullptr;
	}
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	const FNoctisSpeciesInfo* Info = Sub ? Sub->GetSpeciesInfo(Source.speciesIndex) : nullptr;
	if (!Info || !Info->CallIds.IsValidIndex(Source.callIndex))
	{
		return nullptr;
	}
	for (TActorIterator<ANoctisCreatureManager> It(GetWorld()); It; ++It)
	{
		if (ANoctisCreatureActor* Actor = It->FindActor(Source.emitter.value))
		{
			if (const UNoctisSpeciesVisualData* Visual = Actor->GetVisual())
			{
				if (!Visual->bProceduralCalls)
				{
					const FNoctisCallSound* Call = Visual->FindCall(Info->CallIds[Source.callIndex]);
					return Call ? Call->Sound.Get() : nullptr;
				}
			}
		}
		break;
	}
	return nullptr;
}

void ANoctisAudioDirector::Play(const FScheduledSound& Sound)
{
	UWorld* World = GetWorld();
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	if (!World || !Sub)
	{
		return;
	}
	const UNoctisSettings& Settings = UNoctisSettings::Get();
	const FVector Location = Sub->GetSpace().ToUE(Sound.Source.position);

	// Attach calls to the head of the visible animal when there is one.
	USceneComponent* AttachTo = nullptr;
	FName AttachBone = NAME_None;
	if (Sound.Source.kind == noctis::SoundKind::Vocalization)
	{
		for (TActorIterator<ANoctisCreatureManager> It(World); It; ++It)
		{
			if (ANoctisCreatureActor* Actor = It->FindActor(Sound.Source.emitter.value))
			{
				AttachTo = Actor->GetMesh();
				AttachBone = Actor->GetVisual() ? Actor->GetVisual()->HeadBone : NAME_None;
			}
			break;
		}
	}

	float ReferenceDb = Sound.Source.levelDb;
	USoundBase* Baked = FindBakedSound(Sound, ReferenceDb);
	UAudioComponent* Component = nullptr;
	double DurationS = 0.0;
	bool bProcedural = false;
	if (Baked)
	{
		// Baked assets are calibrated by import_audio.py: volume 1 = ReferenceDb (a call's or a splash's level at 1 m, the
		// level a thunder file represents at its class distance). The attenuation asset only spatialises: the whole
		// propagation loss predicted by the core is applied here. Up to +12 dB when the event is closer than its class.
		const float Volume = FMath::Clamp(FMath::Pow(10.0f, (Sound.ReceivedLevelDb - ReferenceDb) / 20.0f), 0.0f, 4.0f);
		Component = AttachTo ? UGameplayStatics::SpawnSoundAttached(Baked, AttachTo, AttachBone, FVector::ZeroVector, EAttachLocation::SnapToTarget, false, Volume, 1.0f, 0.0f,
																	CallAttenuation, CallConcurrency, true)
							 : UGameplayStatics::SpawnSoundAtLocation(this, Baked, Location, FRotator::ZeroRotator, Volume, 1.0f, 0.0f, CallAttenuation, CallConcurrency, true);
		DurationS = Baked->GetDuration() + 0.5;
	}
	else
	{
		int32 ActiveProcedural = 0;
		for (const FPlaying& Entry : Playing)
		{
			ActiveProcedural += Entry.bProcedural ? 1 : 0;
		}
		if (ActiveProcedural >= Settings.MaxProceduralCalls)
		{
			return;
		}
		UNoctisCallSoundWave* Wave = NewObject<UNoctisCallSoundWave>(this);
		Wave->StartRender(Sound.Source, Sound.DistanceM, Sound.ExtraGainDb, Sound.TemperatureC, Sound.Humidity, Settings.AudioFullScaleDbSpl);
		LiveWaves.Add(Wave);
		Component = AttachTo ? UGameplayStatics::SpawnSoundAttached(Wave, AttachTo, AttachBone, FVector::ZeroVector, EAttachLocation::SnapToTarget, false, 1.0f, 1.0f, 0.0f,
																	CallAttenuation, CallConcurrency, true)
							 : UGameplayStatics::SpawnSoundAtLocation(this, Wave, Location, FRotator::ZeroRotator, 1.0f, 1.0f, 0.0f, CallAttenuation, CallConcurrency, true);
		DurationS = Wave->GetRenderedDurationSeconds() + 0.2;
		bProcedural = true;
	}
	if (Component)
	{
		// Real seconds: at time scale s the sound plays at its natural speed (no pitch shifting of physical signals).
		Playing.Add({Component, World->GetRealTimeSeconds() + DurationS, bProcedural});
	}
}

UAudioComponent* ANoctisAudioDirector::EnsureLoop(TObjectPtr<UAudioComponent>& Slot, USoundBase* Sound, FName Name)
{
	if (!Sound)
	{
		return nullptr;
	}
	if (!Slot)
	{
		// Small non-zero start volume: fully silent sounds may be culled before the first SetVolumeMultiplier.
		Slot = UGameplayStatics::SpawnSound2D(this, Sound, 0.01f, 1.0f, 0.0f, nullptr, /*bPersistAcrossLevelTransition*/ false, /*bAutoDestroy*/ false);
		(void)Name;
	}
	return Slot;
}

void ANoctisAudioDirector::UpdateAmbience(const FNoctisFrameOutput& Front)
{
	const noctis::WeatherState& Weather = Front.Snapshot.weather;
	auto SetLevel = [](UAudioComponent* Component, float Volume) {
		if (Component)
		{
			Component->SetVolumeMultiplier(FMath::Clamp(Volume, 0.0f, 2.0f));
		}
	};
	// The bed assets already play at their manifest ambient level (import_audio.py calibration): apply only the difference
	// between the core's level and that reference (counting the absolute level once).
	SetLevel(EnsureLoop(RainAudio, RainLoop, TEXT("RainBed")),
			 Weather.precipMmPerHour > 0.0f ? FMath::Pow(10.0f, (Weather.rainNoiseDb - RainLoopAmbientDb) / 20.0f) : 0.0f);
	SetLevel(EnsureLoop(WindAudio, WindLoop, TEXT("WindBed")), FMath::Pow(10.0f, (Weather.windNoiseDb - WindLoopAmbientDb) / 20.0f));
	SetLevel(EnsureLoop(InsectAudio, InsectLoop, TEXT("InsectBed")), Front.InsectChorus);
	SetLevel(EnsureLoop(BirdAudio, BirdLoop, TEXT("BirdBed")), Front.BirdActivity);
}
