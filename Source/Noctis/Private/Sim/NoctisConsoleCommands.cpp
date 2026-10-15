// Console commands (PIE / development builds). Each command queues its work on the simulation thread or takes a
// short exclusive lock; none of them reads simulation memory while a job runs.
//
//   noctis.TimeScale <x>                 time acceleration (1 = real time; > 8 suspends full-detail LOD)
//   noctis.Pause [0|1]                   pause / resume the simulation (toggles without argument)
//   noctis.Census                        population census per species (alive, juveniles, regional pool, births, deaths)
//   noctis.Spawn <species_id> [n] [m]    spawns n adults m metres in front of the camera (debug; logged as such)
//   noctis.Kill <id|nearest> [radius]    kills an individual (DeathCause::Unknown), 'nearest' within radius (m) of the camera
//   noctis.Weather <regime|fog x|nofog>  forces clear|fair|overcast|lightrain|heavyrain|storm, or valley fog extinction (1/m)
//   noctis.Hash                          bitwise hash of the current snapshot (determinism checks between machines)
//   noctis.ShowLOD <0|1|2>               console variable, see NoctisCreatureManager.cpp
#include "NoctisLog.h"
#include "Sim/NoctisSnapshotChecks.h"
#include "Sim/NoctisWorldSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace NoctisConsoleImpl
{
UNoctisWorldSubsystem* Subsystem(UWorld* World)
{
	UNoctisWorldSubsystem* Sub = World ? World->GetSubsystem<UNoctisWorldSubsystem>() : nullptr;
	if (!Sub || !Sub->IsSimulationReady())
	{
		UE_LOG(LogNoctis, Warning, TEXT("Simulation Noctis indisponible dans ce monde."));
		return nullptr;
	}
	return Sub;
}

void Report(const FString& Text)
{
	UE_LOG(LogNoctis, Display, TEXT("%s"), *Text);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 8.0f, FColor::Cyan, Text);
	}
}

bool ViewPoint(UWorld* World, FVector& Location, FRotator& Rotation)
{
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return false;
	}
	PC->GetPlayerViewPoint(Location, Rotation);
	return true;
}

void TimeScale(const TArray<FString>& Args, UWorld* World)
{
	if (UNoctisWorldSubsystem* Sub = Subsystem(World))
	{
		if (Args.Num() > 0)
		{
			Sub->SetTimeScale(FCString::Atof(*Args[0]));
		}
		Report(FString::Printf(TEXT("noctis.TimeScale = %.2f"), Sub->GetTimeScale()));
	}
}

void Pause(const TArray<FString>& Args, UWorld* World)
{
	if (UNoctisWorldSubsystem* Sub = Subsystem(World))
	{
		Sub->SetSimulationPaused(Args.Num() > 0 ? FCString::Atoi(*Args[0]) != 0 : !Sub->IsSimulationPaused());
		Report(Sub->IsSimulationPaused() ? TEXT("Simulation en pause") : TEXT("Simulation en cours"));
	}
}

void Census(const TArray<FString>& Args, UWorld* World)
{
	UNoctisWorldSubsystem* Sub = Subsystem(World);
	if (!Sub)
	{
		return;
	}
	TArray<FString> Lines;
	Sub->WithSimulation([&Lines](noctis::WorldSimulation& Sim) {
		const double Now = Sim.clock().seconds();
		Lines.Add(FString::Printf(TEXT("Recensement — %s — %d individus simulés"), *NoctisUnreal::ToFString(Sim.clock().formatted()),
								  static_cast<int32>(Sim.creatures().aliveCount())));
		for (const int SpeciesIndex : Sim.creatures().worldSpecies())
		{
			const noctis::SpeciesCensus C = Sim.creatures().census(SpeciesIndex, Now);
			Lines.Add(FString::Printf(TEXT("  %-34s vivants %4d (juv. %3d, adultes %3d, femelles %3d)  régional %4d  naissances %3d  morts %3d  biomasse %.1f t"),
									  *NoctisUnreal::ToFString(Sim.db().speciesAt(SpeciesIndex).scientificName), C.alive, C.juveniles, C.adults, C.females, C.regional,
									  C.births, C.deaths, C.biomassKg / 1000.0));
		}
		for (const noctis::PopulationPool& Pool : Sim.ecology().pools())
		{
			Lines.Add(FString::Printf(TEXT("  %-34s population %.0f (capacité %.0f)"), *NoctisUnreal::ToFString(Sim.db().speciesAt(Pool.speciesIndex).scientificName),
									  Pool.abundance, Pool.capacity));
		}
	});
	for (const FString& Line : Lines)
	{
		UE_LOG(LogNoctis, Display, TEXT("%s"), *Line);
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 12.0f, FColor::Cyan, FString::Join(Lines, TEXT("\n")));
	}
}

void Spawn(const TArray<FString>& Args, UWorld* World)
{
	UNoctisWorldSubsystem* Sub = Subsystem(World);
	if (!Sub || Args.Num() < 1)
	{
		Report(TEXT("Usage : noctis.Spawn <species_id> [nombre=1] [distance_m=40]"));
		return;
	}
	const int32 SpeciesIndex = Sub->FindSpeciesIndex(FName(*Args[0]));
	if (SpeciesIndex == INDEX_NONE)
	{
		Report(FString::Printf(TEXT("Espèce inconnue : %s"), *Args[0]));
		return;
	}
	const int32 Count = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), 1, 50) : 1;
	const float DistanceM = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 40.0f;
	FVector View;
	FRotator Rotation;
	if (!ViewPoint(World, View, Rotation))
	{
		return;
	}
	const FVector Target = View + FRotator(0.0, Rotation.Yaw, 0.0).Vector() * double(DistanceM) * 100.0;
	const noctis::Vec2 Centre = Sub->GetSpace().FromUE2D(Target);
	TSharedRef<int32, ESPMode::ThreadSafe> Spawned = MakeShared<int32, ESPMode::ThreadSafe>(0);
	const FString SpeciesId = Args[0];
	Sub->EnqueueCommand(
		[SpeciesIndex, Count, Centre, Spawned](noctis::WorldSimulation& Sim) {
			const noctis::SpeciesSim& S = Sim.db().speciesAt(SpeciesIndex).sim;
			if (S.role != noctis::SimRole::Agent)
			{
				return; // populations (fish, insects...) are not individuals
			}
			noctis::Rng Rng(Sim.config().seed ^ Sim.tick(), 0xDEB6);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const noctis::Vec2 P = Sim.terrain().clampToBounds(Centre + noctis::Vec2::fromHeading(Rng.range(0.0f, noctis::kTwoPi)) * Rng.range(0.0f, 8.0f + S.adultLengthM * 2.0f), 20.0f);
				Sim.creatures().spawn(SpeciesIndex, Rng.chance(0.5f) ? noctis::Sex::Female : noctis::Sex::Male, FMath::Max(1.0f, S.growth.maturityAgeYears * 1.3f), P,
									  Sim.context());
				++(*Spawned);
			}
			Sim.creatures().rebuildIndex();
		},
		[Spawned, SpeciesId]() { Report(FString::Printf(TEXT("noctis.Spawn : %d %s (DEBUG — ces individus ne sont pas issus de la dynamique de population)"), *Spawned, *SpeciesId)); });
}

void Kill(const TArray<FString>& Args, UWorld* World)
{
	UNoctisWorldSubsystem* Sub = Subsystem(World);
	if (!Sub || Args.Num() < 1)
	{
		Report(TEXT("Usage : noctis.Kill <id|nearest> [rayon_m=100]"));
		return;
	}
	FVector View;
	FRotator Rotation;
	ViewPoint(World, View, Rotation);
	const noctis::Vec2 From = Sub->GetSpace().FromUE2D(View);
	const bool bNearest = Args[0].Equals(TEXT("nearest"), ESearchCase::IgnoreCase);
	const noctis::u32 Serial = bNearest ? 0u : static_cast<noctis::u32>(FCString::Strtoui64(*Args[0], nullptr, 10));
	const float Radius = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 100.0f;
	TSharedRef<FString, ESPMode::ThreadSafe> Result = MakeShared<FString, ESPMode::ThreadSafe>();
	Sub->EnqueueCommand(
		[bNearest, Serial, From, Radius, Result](noctis::WorldSimulation& Sim) {
			noctis::Creature* Victim = nullptr;
			if (bNearest)
			{
				float Best = Radius;
				for (noctis::Creature& C : Sim.creatures().creatures())
				{
					const float D = noctis::distance(C.pos2(), From);
					if (C.alive && D < Best)
					{
						Best = D;
						Victim = &C;
					}
				}
			}
			else
			{
				// Accept both the raw id and the serial shown by noctis.ShowLOD 2 / the gameplay debugger.
				Victim = Sim.creatures().find(noctis::EntityId(Serial));
				if (!Victim)
				{
					Victim = Sim.creatures().find(noctis::EntityId::make(noctis::EntityKind::Creature, Serial));
				}
			}
			if (Victim && Victim->alive)
			{
				*Result = FString::Printf(TEXT("noctis.Kill : #%u (%s) tué (DEBUG)"), Victim->id.serial(),
										  *NoctisUnreal::ToFString(Sim.db().speciesAt(Victim->speciesIndex).id));
				Sim.creatures().kill(*Victim, noctis::DeathCause::Unknown, Sim.context());
			}
			else
			{
				*Result = TEXT("noctis.Kill : aucun individu vivant correspondant");
			}
		},
		[Result]() { Report(*Result); });
}

void Weather(const TArray<FString>& Args, UWorld* World)
{
	UNoctisWorldSubsystem* Sub = Subsystem(World);
	if (!Sub || Args.Num() < 1)
	{
		Report(TEXT("Usage : noctis.Weather clear|fair|overcast|lightrain|heavyrain|storm | fog <extinction 1/m, ex. 0.012> | nofog"));
		return;
	}
	const FString Mode = Args[0].ToLower();
	if (Mode == TEXT("fog") || Mode == TEXT("nofog"))
	{
		const float Extinction = Mode == TEXT("nofog") ? -1.0f : (Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.012f);
		Sub->EnqueueCommand([Extinction](noctis::WorldSimulation& Sim) { Sim.weather().forceFog(Extinction); });
		Report(FString::Printf(TEXT("noctis.Weather : brouillard %s"), Extinction < 0.0f ? TEXT("naturel") : *FString::Printf(TEXT("%.4f /m"), Extinction)));
		return;
	}
	static const TCHAR* Names[] = {TEXT("clear"), TEXT("fair"), TEXT("overcast"), TEXT("lightrain"), TEXT("heavyrain"), TEXT("storm")};
	for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Names)); ++Index)
	{
		if (Mode == Names[Index])
		{
			const noctis::WeatherRegime Regime = static_cast<noctis::WeatherRegime>(Index);
			Sub->EnqueueCommand([Regime](noctis::WorldSimulation& Sim) { Sim.weather().forceRegime(Regime); });
			Report(FString::Printf(TEXT("noctis.Weather : régime %s (l'évolution naturelle reprend ensuite)"), *NoctisUnreal::ToFString(noctis::weatherRegimeLabelFr(Regime))));
			return;
		}
	}
	Report(FString::Printf(TEXT("Régime inconnu : %s"), *Args[0]));
}

void Hash(const TArray<FString>& Args, UWorld* World)
{
	if (UNoctisWorldSubsystem* Sub = Subsystem(World))
	{
		if (const FNoctisFrameOutput* Front = Sub->GetFrontFrame())
		{
			Report(FString::Printf(TEXT("noctis.Hash : tick %llu, t = %.2f s, hash %016llx"), static_cast<unsigned long long>(Front->Tick), Front->Snapshot.time,
								   static_cast<unsigned long long>(NoctisChecks::HashSnapshot(Front->Snapshot))));
		}
	}
}
} // namespace NoctisConsoleImpl

static FAutoConsoleCommandWithWorldAndArgs GNoctisTimeScaleCommand(TEXT("noctis.TimeScale"), TEXT("Accélération du temps simulé (1 = temps réel)."),
																   FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NoctisConsoleImpl::TimeScale));
static FAutoConsoleCommandWithWorldAndArgs GNoctisPauseCommand(TEXT("noctis.Pause"), TEXT("Met en pause / relance la simulation."),
															   FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NoctisConsoleImpl::Pause));
static FAutoConsoleCommandWithWorldAndArgs GNoctisCensusCommand(TEXT("noctis.Census"), TEXT("Recensement des populations par espèce."),
																FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NoctisConsoleImpl::Census));
static FAutoConsoleCommandWithWorldAndArgs GNoctisSpawnCommand(TEXT("noctis.Spawn"), TEXT("noctis.Spawn <species_id> [n] [distance_m] — débogage."),
															   FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NoctisConsoleImpl::Spawn));
static FAutoConsoleCommandWithWorldAndArgs GNoctisKillCommand(TEXT("noctis.Kill"), TEXT("noctis.Kill <id|nearest> [rayon_m] — débogage."),
															  FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NoctisConsoleImpl::Kill));
static FAutoConsoleCommandWithWorldAndArgs GNoctisWeatherCommand(TEXT("noctis.Weather"), TEXT("Force un régime météo ou le brouillard de vallée."),
																 FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NoctisConsoleImpl::Weather));
static FAutoConsoleCommandWithWorldAndArgs GNoctisHashCommand(TEXT("noctis.Hash"), TEXT("Empreinte binaire de l'instantané courant (déterminisme)."),
															  FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NoctisConsoleImpl::Hash));
