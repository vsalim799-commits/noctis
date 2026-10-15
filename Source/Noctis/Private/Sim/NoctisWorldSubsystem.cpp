#include "Sim/NoctisWorldSubsystem.h"

#include "NoctisLog.h"
#include "NoctisSettings.h"

#include "Audio/NoctisAudioDirector.h"
#include "Creatures/NoctisCreatureManager.h"
#include "Environment/NoctisEnvironmentController.h"
#include "Environment/NoctisVegetationInteraction.h"
#include "Environment/NoctisWaterActor.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#include "Noctis/Creatures/SpeciesRuntime.h"

UNoctisWorldSubsystem* UNoctisWorldSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject || !GEngine)
	{
		return nullptr;
	}
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<UNoctisWorldSubsystem>() : nullptr;
}

bool UNoctisWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// No simulation in editor preview worlds, thumbnails or the inactive editor world.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UNoctisWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Space.OffsetCm = UNoctisSettings::Get().WorldOffsetCm;
	PreActorTickHandle = FWorldDelegates::OnWorldPreActorTick.AddUObject(this, &UNoctisWorldSubsystem::OnPreActorTick);
	BeginBoot();
}

void UNoctisWorldSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldPreActorTick.Remove(PreActorTickHandle);
	PreActorTickHandle.Reset();
	if (bBooting)
	{
		BootTask.Wait();
		bBooting = false;
	}
	if (Runner)
	{
		Runner->Shutdown();
		Runner.Reset();
	}
	bReady = false;
	Super::Deinitialize();
}

void UNoctisWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bBegunPlay = true;
	if (bReady)
	{
		SpawnPresentationActors();
	}
}

noctis::SimConfig UNoctisWorldSubsystem::MakeConfig()
{
	const UNoctisSettings& Settings = UNoctisSettings::Get();
	noctis::SimConfig Config;
	Config.dataRoot = NoctisUnreal::ToStdString(Settings.ResolveDataRoot());
	Config.environmentId = NoctisUnreal::ToStdString(Settings.EnvironmentId);
	Config.profileId = NoctisUnreal::ToStdString(Settings.ProfileId);
	Config.seed = static_cast<noctis::u64>(Settings.Seed);
	Config.threads = Settings.Threads;
	Config.startDayOfYear = Settings.StartDayOfYear;
	Config.startHour = Settings.StartHour;
	Config.heightSamples = Settings.HeightSamples;
	Config.materialCells = Settings.MaterialCells;
	Config.vegetationCellM = Settings.VegetationCellM;
	Config.populationScale = Settings.PopulationScale;
	return Config;
}

FNoctisSimulationRunner::FParams UNoctisWorldSubsystem::MakeParams()
{
	const UNoctisSettings& Settings = UNoctisSettings::Get();
	FNoctisSimulationRunner::FParams Params;
	Params.FixedStepSeconds = Settings.FixedStepSeconds;
	Params.CoarseStepSeconds = Settings.CoarseStepSeconds;
	Params.FullDetailMaxTimeScale = Settings.FullDetailMaxTimeScale;
	Params.MaxStepsPerJob = Settings.MaxStepsPerJob;
	Params.MaxBacklogSeconds = Settings.MaxBacklogSeconds;
	Params.bAsync = Settings.bAsyncSimulation;
	Params.AuxIntervalSeconds = Settings.AuxDataIntervalSeconds;
	Params.AuxRadiusM = Settings.AuxDataRadiusM;
	Params.Scheduler = Settings.bUseParallelFor ? ENoctisSchedulerKind::ParallelFor : ENoctisSchedulerKind::ThreadPool;
	return Params;
}

void UNoctisWorldSubsystem::BeginBoot()
{
	bReady = false;
	bBooting = true;
	bBootSucceeded = false;
	BootError.Reset();
	Runner = MakeUnique<FNoctisSimulationRunner>();
	const noctis::SimConfig Config = MakeConfig();
	const FNoctisSimulationRunner::FParams Params = MakeParams();
	UE_LOG(LogNoctis, Log, TEXT("Construction de la vallée '%s' (profil '%s', graine %llu, données %s)..."), *NoctisUnreal::ToFString(Config.environmentId),
		   *NoctisUnreal::ToFString(Config.profileId), static_cast<unsigned long long>(Config.seed), *NoctisUnreal::ToFString(Config.dataRoot));

	FNoctisSimulationRunner* RawRunner = Runner.Get();
	if (UNoctisSettings::Get().bAsyncBoot)
	{
		// Terrain generation and population spawning take seconds: keep the game thread responsive.
		// Deinitialize() waits for this task, so 'this' and RawRunner outlive it.
		BootTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, RawRunner, Config, Params]() {
			FString Error;
			const bool bOk = RawRunner->Initialize(Config, Params, Error);
			BootError = Error;
			bBootSucceeded = bOk;
		});
	}
	else
	{
		bBootSucceeded = Runner->Initialize(Config, Params, BootError);
		FinishBoot();
	}
}

void UNoctisWorldSubsystem::FinishBoot()
{
	bBooting = false;
	if (!bBootSucceeded)
	{
		UE_LOG(LogNoctis, Error, TEXT("La simulation n'a pas pu démarrer : %s"), *BootError);
		Runner.Reset();
		return;
	}
	CacheSpecies();
	Runner->SetTimeScale(PendingTimeScale);
	Runner->SetPaused(bPendingPaused);
	bReady = true;
	LastBroadcastSequence = Runner->GetPublishedSequence();
	if (bBegunPlay)
	{
		SpawnPresentationActors();
	}
	OnSimulationReady.Broadcast();
}

void UNoctisWorldSubsystem::CacheSpecies()
{
	Species.Reset();
	SpeciesById.Reset();
	const noctis::WorldSimulation& Sim = Runner->GetSimulationChecked();
	for (const noctis::SpeciesRuntime& Runtime : Sim.species())
	{
		FNoctisSpeciesInfo& Info = Species.AddDefaulted_GetRef();
		const noctis::SpeciesDefinition& Def = *Runtime.def;
		Info.Index = Runtime.index;
		Info.Id = FName(*NoctisUnreal::ToFString(Def.id));
		Info.ScientificName = NoctisUnreal::ToFString(Def.scientificName);
		Info.CommonNameFr = NoctisUnreal::ToFString(Def.commonNameFr);
		Info.BodyPlanKey = NoctisUnreal::ToFString(noctis::bodyPlanKey(Def.sim.bodyPlan));
		Info.AdultLengthM = Def.sim.adultLengthM;
		Info.HipHeightM = Def.sim.hipHeightM;
		Info.AdultMassKg = Def.sim.adultMassKg;
		Info.LegCount = Runtime.legCount;
		Info.bAgent = Def.sim.role == noctis::SimRole::Agent;
		Info.bClosedMouthCalls = Def.sim.vocal.closedMouth;
		for (int32 Foot = 0; Foot < 4; ++Foot)
		{
			const bool bFore = Runtime.footIsFore[static_cast<size_t>(Foot)];
			const bool bLeft = Runtime.footIsLeft[static_cast<size_t>(Foot)];
			Info.FootSlots[Foot] = bFore ? (bLeft ? ENoctisFootSlot::ForeLeft : ENoctisFootSlot::ForeRight)
										 : (bLeft ? ENoctisFootSlot::HindLeft : ENoctisFootSlot::HindRight);
		}
		for (const noctis::CallTypeSpec& Call : Def.sim.vocal.calls)
		{
			Info.CallIds.Add(FName(*NoctisUnreal::ToFString(Call.id)));
			Info.CallDurations.Add(Call.durationS);
		}
		SpeciesById.Add(Info.Id, Info.Index);
	}
	for (const int SpeciesIndex : Sim.speciesInWorld())
	{
		if (Species.IsValidIndex(SpeciesIndex))
		{
			Species[SpeciesIndex].bInWorld = true;
		}
	}
}

void UNoctisWorldSubsystem::SpawnPresentationActors()
{
	UWorld* World = GetWorld();
	if (!World || !UNoctisSettings::Get().bAutoSpawnPresentationActors)
	{
		return;
	}
	auto EnsureActor = [World](UClass* Class) {
		if (TActorIterator<AActor>(World, Class))
		{
			return; // already placed in the level (designers can place and configure them)
		}
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<AActor>(Class, FTransform::Identity, Params);
	};
	EnsureActor(ANoctisCreatureManager::StaticClass());
	EnsureActor(ANoctisEnvironmentController::StaticClass());
	EnsureActor(ANoctisWaterActor::StaticClass());
	EnsureActor(ANoctisVegetationInteraction::StaticClass());
	EnsureActor(ANoctisAudioDirector::StaticClass());
}

void UNoctisWorldSubsystem::OnPreActorTick(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds)
{
	if (InWorld != GetWorld())
	{
		return;
	}
	if (bBooting)
	{
		if (!BootTask.IsCompleted())
		{
			return;
		}
		BootTask.Wait();
		FinishBoot();
	}
	if (bReloadRequested)
	{
		bReloadRequested = false;
		if (Runner)
		{
			Runner->Shutdown();
		}
		BeginBoot();
		return;
	}
	if (!bReady || !Runner)
	{
		return;
	}
	Runner->SetInput(NextInput);
	// State inputs persist (pawns resubmit every frame); one-shot inputs are cleared.
	NextInput.bDroneManual = false;
	NextInput.WheelContacts.Reset();
	Runner->SetPaused(bPendingPaused || InWorld->IsPaused());
	Runner->Tick(TickType == LEVELTICK_PauseTick ? 0.0f : DeltaSeconds);
	if (Runner->GetPublishedSequence() != LastBroadcastSequence)
	{
		LastBroadcastSequence = Runner->GetPublishedSequence();
		OnFramePublished.Broadcast(Runner->GetFront());
	}
}

void UNoctisWorldSubsystem::SetTimeScale(float Scale)
{
	PendingTimeScale = FMath::Max(0.0f, Scale);
	if (Runner && bReady)
	{
		Runner->SetTimeScale(PendingTimeScale);
	}
}

float UNoctisWorldSubsystem::GetTimeScale() const
{
	return PendingTimeScale;
}

void UNoctisWorldSubsystem::SetSimulationPaused(bool bPaused)
{
	bPendingPaused = bPaused;
}

bool UNoctisWorldSubsystem::IsSimulationPaused() const
{
	return bPendingPaused;
}

FString UNoctisWorldSubsystem::GetClockText() const
{
	const FNoctisFrameOutput* Front = GetFrontFrame();
	return Front ? Front->ClockText : FString();
}

float UNoctisWorldSubsystem::GetHourOfDay() const
{
	const FNoctisFrameOutput* Front = GetFrontFrame();
	return Front ? Front->Snapshot.hourOfDay : 12.0f;
}

int32 UNoctisWorldSubsystem::GetAliveCount() const
{
	const FNoctisFrameOutput* Front = GetFrontFrame();
	return Front ? Front->AliveCount : 0;
}

float UNoctisWorldSubsystem::GetLastJobMilliseconds() const
{
	const FNoctisFrameOutput* Front = GetFrontFrame();
	return Front ? static_cast<float>(Front->JobMilliseconds) : 0.0f;
}

void UNoctisWorldSubsystem::RequestReload()
{
	bReloadRequested = true;
}

const FNoctisFrameOutput* UNoctisWorldSubsystem::GetFrontFrame() const
{
	return (bReady && Runner) ? &Runner->GetFront() : nullptr;
}

const FNoctisFrameOutput* UNoctisWorldSubsystem::GetPreviousFrame() const
{
	return (bReady && Runner) ? &Runner->GetPrevious() : nullptr;
}

float UNoctisWorldSubsystem::GetInterpolationAlpha() const
{
	return (bReady && Runner) ? Runner->GetInterpolationAlpha() : 1.0f;
}

const FNoctisSpeciesInfo* UNoctisWorldSubsystem::GetSpeciesInfo(int32 SpeciesIndex) const
{
	return Species.IsValidIndex(SpeciesIndex) ? &Species[SpeciesIndex] : nullptr;
}

int32 UNoctisWorldSubsystem::FindSpeciesIndex(FName SpeciesId) const
{
	const int32* Found = SpeciesById.Find(SpeciesId);
	return Found ? *Found : INDEX_NONE;
}

void UNoctisWorldSubsystem::SubmitResearcher(const FVector& EyeLocation, float YawDeg, const FVector& Velocity, bool bCrouched, bool bTalking, bool bHeadlamp)
{
	NextInput.bHasResearcher = true;
	NextInput.ResearcherEye = Space.FromUE(EyeLocation);
	NextInput.ResearcherHeading = FNoctisSpace::YawDegToHeading(YawDeg);
	NextInput.ResearcherVelocity = FNoctisSpace::VelocityFromUE(Velocity);
	NextInput.bCrouched = bCrouched;
	NextInput.bTalking = bTalking;
	NextInput.bHeadlamp = bHeadlamp;
}

void UNoctisWorldSubsystem::SubmitVehicle(bool bExternal, const FVector& Location, float YawDeg, float SpeedCmPerS, const noctis::VehicleControls& Controls,
										  const TArray<FVector>& WheelContacts, float WheelLoadN)
{
	NextInput.bVehicleExternal = bExternal;
	NextInput.bHasVehicleTransform = bExternal;
	NextInput.VehiclePosition = Space.FromUE(Location);
	NextInput.VehicleHeading = FNoctisSpace::YawDegToHeading(YawDeg);
	NextInput.VehicleSpeed = SpeedCmPerS / 100.0f;
	NextInput.bHasVehicleControls = true;
	NextInput.VehicleControls = Controls;
	NextInput.WheelContacts.Reset();
	for (const FVector& Contact : WheelContacts)
	{
		NextInput.WheelContacts.Add(Space.FromUE2D(Contact));
	}
	NextInput.WheelLoadN = WheelLoadN;
}

void UNoctisWorldSubsystem::SubmitDroneManual(const FVector& VelocityCmPerS, float AltitudeM)
{
	NextInput.bDroneManual = true;
	NextInput.DroneManualVelocity = FNoctisSpace::VelocityFromUE(VelocityCmPerS);
	NextInput.DroneManualAltitudeM = AltitudeM;
}

void UNoctisWorldSubsystem::SubmitFocus(const FVector& Location)
{
	NextInput.bHasFocus = true;
	NextInput.Focus = Space.FromUE2D(Location);
}

void UNoctisWorldSubsystem::EnqueueCommand(TUniqueFunction<void(noctis::WorldSimulation&)> Work, TUniqueFunction<void()> OnGameThread)
{
	if (Runner && bReady)
	{
		Runner->EnqueueCommand(MoveTemp(Work), MoveTemp(OnGameThread));
	}
}
