// Unreal automation tests for the NoctisCore integration.
//
// Run them from the editor (Tools > Session Frontend > Automation, filter "Noctis") or headless:
//   UnrealEditor-Cmd <Project>/Noctis.uproject -ExecCmds="Automation RunTests Noctis; Quit" -unattended -nullrhi -nosplash -log
// (see Docs/UE5_INTEGRATION.md, "Compiler et tester sur votre machine").
//
// What is covered:
//   Noctis.Core.*        coordinate conversions, the IFileManager file system and the ParallelFor scheduler adapters;
//   Noctis.Simulation.*  boot through the Unreal adapters, determinism (serial / ParallelFor / std::thread pool, bit
//                        for bit), snapshot consistency over real-time and accelerated stepping, and the asynchronous
//                        triple-buffered runner (same result as one blocking job, ordered frames, commands);
//   Noctis.World.*       UNoctisWorldSubsystem booting inside a real UWorld and publishing frames (latent test).
// The simulation-level checks share their hash/validation code with the standalone build
// (Private/Sim/NoctisSnapshotChecks.h has no Unreal dependency and was compiled and run against the CMake core).
//
// None of this has been compiled with Unreal Build Tool yet (no engine where it was written): calls whose exact
// signature differs between 5.4 and 5.5 are marked VERIFY.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "NoctisSettings.h"
#include "NoctisSpace.h"
#include "Sim/NoctisSimulationRunner.h"
#include "Sim/NoctisSnapshotChecks.h"
#include "Sim/NoctisUnrealAdapters.h"
#include "Sim/NoctisWorldSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"

#include "Noctis/Core/FileSystem.h"
#include "Noctis/Core/Jobs.h"
#include "Noctis/Sim/WorldSimulation.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

// Editor and -game sessions; ProductFilter = part of the project's regular suite (not a smoke test: boots take seconds).
#define NOCTIS_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace NoctisTests
{
/** Reduced valley used by every simulation test (boots in ~2-3 s on a desktop CPU, standalone measurement). */
static noctis::SimConfig SmallWorldConfig(noctis::u64 Seed)
{
	noctis::SimConfig Config;
	Config.dataRoot = NoctisUnreal::ToStdString(UNoctisSettings::Get().ResolveDataRoot());
	Config.environmentId = NoctisUnreal::ToStdString(UNoctisSettings::Get().EnvironmentId);
	Config.profileId = NoctisUnreal::ToStdString(UNoctisSettings::Get().ProfileId);
	Config.seed = Seed;
	Config.heightSamples = 513;
	Config.materialCells = 256;
	Config.populationScale = 0.25f;
	Config.startHour = 6.0; // dawn: animals active, sun crossing the horizon (sky/illuminance code paths)
	return Config;
}

static bool HasData(FAutomationTestBase& Test)
{
	const FString Root = UNoctisSettings::Get().ResolveDataRoot();
	if (!IFileManager::Get().DirectoryExists(*FPaths::Combine(Root, TEXT("Species"))))
	{
		Test.AddError(FString::Printf(TEXT("Données scientifiques introuvables (%s/Species). Le dépôt Data/ doit être à côté de Noctis.uproject "
										   "ou DataRootOverride renseigné (Project Settings > Game > Noctis)."),
									  *Root));
		return false;
	}
	return true;
}

/** Boots a world through the Unreal adapters (Unreal file system + the requested scheduler). */
static std::unique_ptr<noctis::WorldSimulation> MakeWorld(FAutomationTestBase& Test, noctis::u64 Seed, ENoctisSchedulerKind Scheduler, int32 Threads = 2)
{
	noctis::SimConfig Config = SmallWorldConfig(Seed);
	Config.threads = Threads;
	std::unique_ptr<noctis::WorldSimulation> World = std::make_unique<noctis::WorldSimulation>();
	if (!World->initialize(Config, NoctisUnreal::MakeFileSystem(), NoctisUnreal::MakeScheduler(Scheduler, Threads)))
	{
		Test.AddError(FString::Printf(TEXT("noctis::WorldSimulation::initialize a échoué (données %s)."), *NoctisUnreal::ToFString(Config.dataRoot)));
		return nullptr;
	}
	return World;
}

static FString Hex(noctis::u64 Value)
{
	return FString::Printf(TEXT("%016llx"), static_cast<unsigned long long>(Value));
}

static const TCHAR* SchedulerName(ENoctisSchedulerKind Kind)
{
	switch (Kind)
	{
	case ENoctisSchedulerKind::ParallelFor:
		return TEXT("ParallelFor");
	case ENoctisSchedulerKind::ThreadPool:
		return TEXT("pool std::thread");
	case ENoctisSchedulerKind::Serial:
		return TEXT("série");
	}
	return TEXT("?");
}
} // namespace NoctisTests

// ======================================================================================================== Core adapters

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNoctisCoordinateSpaceTest, "Noctis.Core.CoordinateSpace", NOCTIS_TEST_FLAGS)

bool FNoctisCoordinateSpaceTest::RunTest(const FString& Parameters)
{
	FNoctisSpace Space;
	Space.OffsetCm = FVector(1234.0, -5678.0, 90.0);

	// Core: x east, y north, z up (m). Unreal: X east, Y south, Z up (cm).
	const noctis::Vec3 North(0.0f, 10.0f, 0.0f);
	const FVector NorthUE = Space.ToUE(North) - Space.OffsetCm;
	TestEqual(TEXT("10 m vers le nord = -1000 cm en Y Unreal"), NorthUE.Y, -1000.0, 1e-6);
	TestEqual(TEXT("x est conservé"), Space.ToUE(noctis::Vec3(3.0f, 0.0f, 0.0f)).X - Space.OffsetCm.X, 300.0, 1e-6);

	const noctis::Vec3 P(-2471.25f, 3810.5f, 412.75f);
	const noctis::Vec3 Back = Space.FromUE(Space.ToUE(P));
	TestTrue(TEXT("aller-retour position (cm, < 1 mm)"), FMath::Abs(Back.x - P.x) < 1e-3f && FMath::Abs(Back.y - P.y) < 1e-3f && FMath::Abs(Back.z - P.z) < 1e-3f);

	// Heading: 0 = east, counter-clockwise in the core; yaw clockwise seen from above in Unreal.
	TestEqual(TEXT("cap 0 (est) = yaw 0"), FNoctisSpace::HeadingToYawDeg(0.0f), 0.0, 1e-9);
	const FVector NorthDir = FRotator(0.0, FNoctisSpace::HeadingToYawDeg(UE_HALF_PI), 0.0).Vector();
	TestTrue(TEXT("cap +pi/2 (nord) pointe vers -Y Unreal"), NorthDir.Y < -0.999);
	for (const float Heading : {-3.0f, -1.0f, 0.25f, 2.5f})
	{
		TestEqual(TEXT("aller-retour cap"), FNoctisSpace::YawDegToHeading(FNoctisSpace::HeadingToYawDeg(Heading)), Heading, 1e-5f);
	}

	// Velocity m/s <-> cm/s.
	const noctis::Vec2 V(1.5f, -2.0f);
	const noctis::Vec2 VBack = FNoctisSpace::VelocityFromUE(FNoctisSpace::VelocityToUE(V));
	TestTrue(TEXT("aller-retour vitesse"), FMath::Abs(VBack.x - V.x) < 1e-5f && FMath::Abs(VBack.y - V.y) < 1e-5f);

	// Attitude: nose up stays nose up (pitch sign), left side up stays left side up (roll sign).
	const FRotator Attitude = FNoctisSpace::AttitudeToUE(0.0f, 0.1f, 0.2f);
	TestTrue(TEXT("tangage et roulis : mêmes signes"), Attitude.Pitch > 0.0 && Attitude.Roll > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNoctisFileSystemAdapterTest, "Noctis.Core.FileSystemAdapter", NOCTIS_TEST_FLAGS)

bool FNoctisFileSystemAdapterTest::RunTest(const FString& Parameters)
{
	if (!NoctisTests::HasData(*this))
	{
		return false;
	}
	// Species indices follow file order: the Unreal file system must list exactly what the standalone one lists,
	// in the same (byte) order, or a packaged game would not reproduce a headless run with the same seed.
	const std::string Root = NoctisUnreal::ToStdString(UNoctisSettings::Get().ResolveDataRoot());
	const std::string SpeciesDir = noctis::joinPath(Root, "Species");
	const FNoctisUnrealFileSystem UnrealFs;
	const std::unique_ptr<noctis::FileSystem> StdFs = noctis::makeStdFileSystem();

	auto Tails = [](const std::vector<std::string>& Paths) {
		std::vector<std::string> Out;
		for (const std::string& P : Paths)
		{
			const size_t At = P.rfind("/Species/");
			Out.push_back(At == std::string::npos ? P : P.substr(At));
		}
		return Out;
	};
	const std::vector<std::string> FromUnreal = Tails(UnrealFs.listFiles(SpeciesDir, ".json", true));
	const std::vector<std::string> FromStd = Tails(StdFs->listFiles(SpeciesDir, ".json", true));
	TestTrue(TEXT("au moins une espèce"), FromUnreal.size() > 0);
	TestEqual(TEXT("même nombre de fichiers d'espèces"), static_cast<int32>(FromUnreal.size()), static_cast<int32>(FromStd.size()));
	if (FromUnreal == FromStd)
	{
		TestTrue(TEXT("même ordre de listage"), true);
	}
	else
	{
		for (size_t Index = 0; Index < FromUnreal.size() && Index < FromStd.size(); ++Index)
		{
			if (FromUnreal[Index] != FromStd[Index])
			{
				AddError(FString::Printf(TEXT("Ordre différent à l'indice %d : %s (Unreal) vs %s (std)"), static_cast<int32>(Index),
										 *NoctisUnreal::ToFString(FromUnreal[Index]), *NoctisUnreal::ToFString(FromStd[Index])));
				break;
			}
		}
	}

	// Bytes reach the core unchanged (UTF-8, French text).
	if (!FromStd.empty())
	{
		const std::vector<std::string> Full = StdFs->listFiles(SpeciesDir, ".json", true);
		std::string A, B;
		TestTrue(TEXT("lecture Unreal"), UnrealFs.readText(Full.front(), A));
		TestTrue(TEXT("lecture std"), StdFs->readText(Full.front(), B));
		TestTrue(TEXT("contenu identique octet par octet"), A == B);
	}

	// Write / exists / round trip under Saved/.
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NoctisTests"), TEXT("fs"));
	const std::string File = NoctisUnreal::ToStdString(FPaths::Combine(Dir, TEXT("probe.json")));
	const std::string Content = "{\"texte\": \"carnet de terrain — été\"}";
	TestTrue(TEXT("écriture"), UnrealFs.writeText(File, Content));
	TestTrue(TEXT("existence"), UnrealFs.exists(File));
	std::string ReadBack;
	TestTrue(TEXT("relecture"), UnrealFs.readText(File, ReadBack) && ReadBack == Content);
	IFileManager::Get().DeleteDirectory(*Dir, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNoctisParallelForSchedulerTest, "Noctis.Core.ParallelForScheduler", NOCTIS_TEST_FLAGS)

bool FNoctisParallelForSchedulerTest::RunTest(const FString& Parameters)
{
	FNoctisParallelForScheduler Scheduler;
	TestTrue(TEXT("au moins un worker"), Scheduler.workerCount() >= 1);
	for (const size_t Count : {size_t(0), size_t(1), size_t(15), size_t(16), size_t(17), size_t(1000), size_t(100003)})
	{
		std::vector<std::atomic<int>> Visits(Count);
		for (std::atomic<int>& V : Visits)
		{
			V.store(0);
		}
		Scheduler.parallelFor(Count, [&Visits](size_t Index) { Visits[Index].fetch_add(1, std::memory_order_relaxed); }, 16);
		bool bExactlyOnce = true;
		for (const std::atomic<int>& V : Visits)
		{
			bExactlyOnce &= V.load() == 1;
		}
		TestTrue(FString::Printf(TEXT("chaque indice visité une seule fois (n = %llu)"), static_cast<unsigned long long>(Count)), bExactlyOnce);
	}
	return true;
}

// ======================================================================================================== Simulation

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNoctisSimulationBootTest, "Noctis.Simulation.Boot", NOCTIS_TEST_FLAGS)

bool FNoctisSimulationBootTest::RunTest(const FString& Parameters)
{
	if (!NoctisTests::HasData(*this))
	{
		return false;
	}
	const double T0 = FPlatformTime::Seconds();
	std::unique_ptr<noctis::WorldSimulation> World = NoctisTests::MakeWorld(*this, 42, ENoctisSchedulerKind::ParallelFor);
	if (!World)
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("Démarrage : %.2f s, %d espèces chargées, %d dans la vallée, %d individus."), FPlatformTime::Seconds() - T0,
							static_cast<int32>(World->species().size()), static_cast<int32>(World->speciesInWorld().size()),
							static_cast<int32>(World->creatures().aliveCount())));
	TestTrue(TEXT("initialisée"), World->initialized());
	TestTrue(TEXT("espèces chargées"), World->species().size() > 0);
	TestTrue(TEXT("espèces présentes dans l'environnement"), World->speciesInWorld().size() > 0);
	TestTrue(TEXT("populations initiales"), World->creatures().aliveCount() > 0);
	TestTrue(TEXT("terrain non vide"), World->terrain().bounds().max.x > World->terrain().bounds().min.x);

	noctis::RenderSnapshot Snapshot;
	World->buildSnapshot(Snapshot);
	std::string Error;
	if (!NoctisChecks::ValidateSnapshot(Snapshot, *World, Error))
	{
		AddError(FString::Printf(TEXT("Instantané initial incohérent : %s"), *NoctisUnreal::ToFString(Error)));
	}
	TestTrue(TEXT("le chercheur est placé au camp de base"), NoctisChecks::Finite(Snapshot.researcherPosition));

	// A few real-time steps must not break anything.
	const double StartTime = Snapshot.time;
	for (int32 Step = 0; Step < 40; ++Step)
	{
		World->step(0.05);
	}
	World->buildSnapshot(Snapshot);
	if (!NoctisChecks::ValidateSnapshot(Snapshot, *World, Error))
	{
		AddError(FString::Printf(TEXT("Instantané incohérent après 2 s : %s"), *NoctisUnreal::ToFString(Error)));
	}
	TestEqual(TEXT("horloge avancée de 2 s"), Snapshot.time - StartTime, 2.0, 1e-6);
	TestEqual(TEXT("40 pas comptés"), static_cast<int64>(World->tick()), static_cast<int64>(40));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNoctisSimulationDeterminismTest, "Noctis.Simulation.Determinism", NOCTIS_TEST_FLAGS)

bool FNoctisSimulationDeterminismTest::RunTest(const FString& Parameters)
{
	if (!NoctisTests::HasData(*this))
	{
		return false;
	}
	// Same seed, three schedulers: the core's parallel phases only write per-index data, so the execution order of the
	// engine's task graph must not change a single bit. Compared every second of simulated time, then after ten
	// minutes of accelerated (coarse) time.
	const ENoctisSchedulerKind Kinds[] = {ENoctisSchedulerKind::Serial, ENoctisSchedulerKind::ParallelFor, ENoctisSchedulerKind::ThreadPool};
	std::unique_ptr<noctis::WorldSimulation> Worlds[3];
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Worlds[Index] = NoctisTests::MakeWorld(*this, 42, Kinds[Index], 2);
		if (!Worlds[Index])
		{
			return false;
		}
	}
	noctis::RenderSnapshot Snapshots[3];
	auto Compare = [&](const TCHAR* Phase) {
		noctis::u64 Hashes[3];
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Worlds[Index]->buildSnapshot(Snapshots[Index]);
			Hashes[Index] = NoctisChecks::HashSnapshot(Snapshots[Index]);
		}
		bool bSame = true;
		for (int32 Index = 1; Index < 3; ++Index)
		{
			if (Hashes[Index] != Hashes[0])
			{
				AddError(FString::Printf(TEXT("%s, t = %.2f s : empreinte %s (%s) différente de %s (%s)"), Phase, Snapshots[0].time, *NoctisTests::Hex(Hashes[Index]),
										 NoctisTests::SchedulerName(Kinds[Index]), *NoctisTests::Hex(Hashes[0]), NoctisTests::SchedulerName(Kinds[0])));
				bSame = false;
			}
		}
		return bSame;
	};

	bool bOk = Compare(TEXT("initial"));
	for (int32 Step = 1; Step <= 600 && bOk; ++Step) // 30 s of real time at the default fixed step
	{
		for (std::unique_ptr<noctis::WorldSimulation>& World : Worlds)
		{
			World->step(0.05);
		}
		if (Step % 20 == 0)
		{
			bOk = Compare(TEXT("temps réel"));
		}
	}
	for (std::unique_ptr<noctis::WorldSimulation>& World : Worlds)
	{
		World->setTimeScale(100.0f);
	}
	for (int32 Step = 1; Step <= 300 && bOk; ++Step) // 10 min in 2 s coarse steps
	{
		for (std::unique_ptr<noctis::WorldSimulation>& World : Worlds)
		{
			World->step(2.0);
		}
		if (Step % 30 == 0)
		{
			bOk = Compare(TEXT("temps accéléré"));
		}
	}
	AddInfo(FString::Printf(TEXT("Empreinte finale %s à t = %.0f s (%d individus)."), *NoctisTests::Hex(NoctisChecks::HashSnapshot(Snapshots[0])), Snapshots[0].time,
							static_cast<int32>(Snapshots[0].creatures.size())));

	// The hash is not trivially constant: another seed gives another valley.
	std::unique_ptr<noctis::WorldSimulation> Other = NoctisTests::MakeWorld(*this, 43, ENoctisSchedulerKind::Serial);
	std::unique_ptr<noctis::WorldSimulation> Same = NoctisTests::MakeWorld(*this, 42, ENoctisSchedulerKind::Serial);
	if (Other && Same)
	{
		noctis::RenderSnapshot A, B;
		Other->buildSnapshot(A);
		Same->buildSnapshot(B);
		TestTrue(TEXT("une autre graine donne un autre état"), NoctisChecks::HashSnapshot(A) != NoctisChecks::HashSnapshot(B));
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNoctisSnapshotConsistencyTest, "Noctis.Simulation.SnapshotConsistency", NOCTIS_TEST_FLAGS)

bool FNoctisSnapshotConsistencyTest::RunTest(const FString& Parameters)
{
	if (!NoctisTests::HasData(*this))
	{
		return false;
	}
	std::unique_ptr<noctis::WorldSimulation> World = NoctisTests::MakeWorld(*this, 7, ENoctisSchedulerKind::ParallelFor);
	if (!World)
	{
		return false;
	}
	noctis::RenderSnapshot Previous, Current;
	World->buildSnapshot(Previous);
	std::string Error;
	int32 Checked = 0;
	int32 SoundsSeen = 0;
	auto CheckOne = [&](const TCHAR* Phase) {
		World->buildSnapshot(Current);
		++Checked;
		SoundsSeen += static_cast<int32>(Current.newSounds.size());
		if (!NoctisChecks::ValidateSnapshot(Current, *World, Error))
		{
			AddError(FString::Printf(TEXT("%s, t = %.2f s : %s"), Phase, Current.time, *NoctisUnreal::ToFString(Error)));
			return false;
		}
		if (!(Current.time > Previous.time))
		{
			AddError(FString::Printf(TEXT("%s : le temps n'avance pas (%.3f -> %.3f)"), Phase, Previous.time, Current.time));
			return false;
		}
		if (!NoctisChecks::NoSoundRepeated(Previous, Current))
		{
			AddError(FString::Printf(TEXT("%s, t = %.2f s : un son publié deux fois (l'audio le jouerait deux fois)"), Phase, Current.time));
			return false;
		}
		if (Current.hourOfDay < 0.0f || Current.hourOfDay >= 24.0f)
		{
			AddError(FString::Printf(TEXT("%s : heure hors de [0, 24) : %.3f"), Phase, Current.hourOfDay));
			return false;
		}
		Previous = Current;
		return true;
	};

	bool bOk = true;
	// Real time, one snapshot per step (the runner publishes after every job, often after a single step).
	for (int32 Step = 0; Step < 400 && bOk; ++Step)
	{
		World->step(0.05);
		bOk = CheckOne(TEXT("temps réel"));
	}
	// Accelerated past the next hourly and 6-hourly updates (hydrology, vegetation, carcasses).
	World->setTimeScale(200.0f);
	for (int32 Step = 0; Step < 400 && bOk; ++Step)
	{
		World->step(20.0);
		if (Step % 4 == 0)
		{
			bOk = CheckOne(TEXT("temps accéléré"));
		}
	}
	AddInfo(FString::Printf(TEXT("%d instantanés vérifiés, %d sons publiés, %d individus à t = %.0f s."), Checked, SoundsSeen, static_cast<int32>(Previous.creatures.size()),
							Previous.time));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNoctisSimulationRunnerTest, "Noctis.Simulation.Runner", NOCTIS_TEST_FLAGS)

bool FNoctisSimulationRunnerTest::RunTest(const FString& Parameters)
{
	if (!NoctisTests::HasData(*this))
	{
		return false;
	}
	FNoctisSimulationRunner::FParams Params;
	Params.FixedStepSeconds = 0.05f;
	Params.bAsync = true;
	Params.MaxBacklogSeconds = 10.0f; // never drop debt here: the step count must only depend on the ticks
	Params.Scheduler = ENoctisSchedulerKind::ParallelFor;

	// --- A: asynchronous jobs on UE::Tasks, frame lockstep so that the job boundaries are reproducible.
	FNoctisSimulationRunner Async;
	FString Error;
	if (!Async.Initialize(NoctisTests::SmallWorldConfig(42), Params, Error))
	{
		AddError(Error);
		return false;
	}
	const double StartTime = Async.GetFront().Snapshot.time;
	Async.SetLockstep(true);

	// A command runs on the simulation between two steps, its completion on the game thread after publication.
	uint64 TickSeenByCommand = MAX_uint64;
	bool bCompletionCalled = false;
	uint64 SequenceAtCompletion = 0;
	Async.EnqueueCommand([&TickSeenByCommand](noctis::WorldSimulation& Sim) { TickSeenByCommand = Sim.tick(); },
						 [&bCompletionCalled, &SequenceAtCompletion, &Async]() {
							 bCompletionCalled = true;
							 SequenceAtCompletion = Async.GetPublishedSequence();
						 });

	uint64 LastSequence = Async.GetPublishedSequence();
	int32 Publications = 0;
	bool bOrdered = true;
	for (int32 Frame = 0; Frame < 90; ++Frame) // 3 s at 30 fps
	{
		Async.Tick(1.0f / 30.0f);
		const float Alpha = Async.GetInterpolationAlpha();
		bOrdered &= Alpha >= 0.0f && Alpha <= 1.0f;
		bOrdered &= Async.GetFront().Snapshot.time >= Async.GetPrevious().Snapshot.time;
		if (Async.GetPublishedSequence() != LastSequence)
		{
			++Publications;
			bOrdered &= Async.GetFront().Sequence == Async.GetPublishedSequence();
			LastSequence = Async.GetPublishedSequence();
		}
	}
	Async.Flush();
	TestTrue(TEXT("trames ordonnées (Previous <= Front, alpha dans [0, 1], séquence croissante)"), bOrdered);
	TestTrue(TEXT("des trames ont été publiées"), Publications > 10);
	TestTrue(TEXT("la commande a vu la simulation avant tout pas"), TickSeenByCommand == 0);
	TestTrue(TEXT("complétion appelée sur le thread de jeu après publication"), bCompletionCalled && SequenceAtCompletion > 1);

	const double Simulated = Async.GetFront().Snapshot.time - StartTime;
	const int32 Steps = static_cast<int32>(FMath::RoundToDouble(Simulated / 0.05));
	TestTrue(FString::Printf(TEXT("temps simulé ≈ temps réel (%.3f s pour 3 s)"), Simulated), Simulated > 2.8 && Simulated < 3.05);
	TestTrue(TEXT("frames sonores alignées sur l'instantané"), Async.GetFront().SoundExtras.Num() == static_cast<int32>(Async.GetFront().Snapshot.newSounds.size()));
	TestTrue(TEXT("données auxiliaires (traces, eau) exportées"), Async.GetFront().Aux.IsValid());

	// --- B: the same number of steps in one blocking job must give the same state, bit for bit. Sounds are left out of
	// the comparison: they are reported in the first snapshot after they start, which depends on the publication cadence.
	FNoctisSimulationRunner Blocking;
	Params.bAsync = false;
	if (!Blocking.Initialize(NoctisTests::SmallWorldConfig(42), Params, Error))
	{
		AddError(Error);
		return false;
	}
	Blocking.StepBlocking(Steps, 0.05);
	const noctis::u64 HashAsync = NoctisChecks::HashSnapshot(Async.GetFront().Snapshot, false);
	const noctis::u64 HashBlocking = NoctisChecks::HashSnapshot(Blocking.GetFront().Snapshot, false);
	TestEqual(TEXT("même horloge"), Async.GetFront().Snapshot.time, Blocking.GetFront().Snapshot.time, 1e-9);
	if (HashAsync != HashBlocking)
	{
		AddError(FString::Printf(TEXT("Exécution asynchrone (%d publications) %s différente d'un seul job bloquant de %d pas %s."), Publications,
								 *NoctisTests::Hex(HashAsync), Steps, *NoctisTests::Hex(HashBlocking)));
	}

	// Exclusive access waits for the in-flight job and sees exactly the published state.
	Async.SetLockstep(false);
	Async.Tick(0.5f);
	bool bConsistent = false;
	std::string Problem;
	Async.WithSimulation([&](noctis::WorldSimulation& Sim) {
		bConsistent = NoctisChecks::ValidateSnapshot(Async.GetFront().Snapshot, Sim, Problem);
	});
	TestTrue(FString::Printf(TEXT("WithSimulation voit l'état publié %s"), *NoctisUnreal::ToFString(Problem)), bConsistent);

	Async.Shutdown();
	Blocking.Shutdown();
	return true;
}

// ======================================================================================================== World subsystem

namespace NoctisTests
{
/** A bare game world (no map, no game mode) with temporary settings: small valley, no presentation actors. */
struct FTestWorld
{
	UWorld* World = nullptr;
	int32 SavedHeightSamples = 0;
	int32 SavedMaterialCells = 0;
	float SavedPopulationScale = 1.0f;
	bool bSavedAutoSpawn = true;

	FTestWorld()
	{
		UNoctisSettings* Settings = GetMutableDefault<UNoctisSettings>();
		SavedHeightSamples = Settings->HeightSamples;
		SavedMaterialCells = Settings->MaterialCells;
		SavedPopulationScale = Settings->PopulationScale;
		bSavedAutoSpawn = Settings->bAutoSpawnPresentationActors;
		Settings->HeightSamples = 513;
		Settings->MaterialCells = 256;
		Settings->PopulationScale = 0.25f;
		Settings->bAutoSpawnPresentationActors = false;

		// VERIFY (5.4/5.5): same pattern as the engine's own world-based tests. UNoctisWorldSubsystem is created by
		// InitWorld (inside CreateWorld) and starts booting the valley on a background task right away.
		World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("NoctisAutomationWorld")));
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
	}

	~FTestWorld()
	{
		if (World)
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			if (World->IsRooted())
			{
				World->RemoveFromRoot();
			}
		}
		UNoctisSettings* Settings = GetMutableDefault<UNoctisSettings>();
		Settings->HeightSamples = SavedHeightSamples;
		Settings->MaterialCells = SavedMaterialCells;
		Settings->PopulationScale = SavedPopulationScale;
		Settings->bAutoSpawnPresentationActors = bSavedAutoSpawn;
	}
};

/** Ticks the world until the valley is booted, then for a few seconds, checking what the subsystem publishes. */
class FWaitAndCheckSubsystem final : public IAutomationLatentCommand
{
public:
	FWaitAndCheckSubsystem(FAutomationTestBase* InTest, TSharedRef<FTestWorld> InWorld) : Test(InTest), TestWorld(InWorld), StartSeconds(FPlatformTime::Seconds()) {}

	virtual bool Update() override
	{
		UWorld* World = TestWorld->World;
		UNoctisWorldSubsystem* Sub = World ? World->GetSubsystem<UNoctisWorldSubsystem>() : nullptr;
		if (!Sub)
		{
			Test->AddError(TEXT("UNoctisWorldSubsystem absent du monde de jeu."));
			return true;
		}
		const float Dt = 1.0f / 30.0f;
		World->Tick(LEVELTICK_All, Dt);
		if (!Sub->IsSimulationReady())
		{
			if (FPlatformTime::Seconds() - StartSeconds > 180.0)
			{
				Test->AddError(TEXT("La vallée n'a pas démarré en 180 s (voir LogNoctis / LogNoctisCore)."));
				return true;
			}
			return false;
		}
		if (FirstSequence == 0)
		{
			FirstSequence = Sub->GetRunner()->GetPublishedSequence() + 1;
			Test->AddInfo(FString::Printf(TEXT("Vallée prête après %.1f s : %d individus, %s."), FPlatformTime::Seconds() - StartSeconds, Sub->GetAliveCount(),
										  *Sub->GetClockText()));
		}
		if (++FramesAfterReady < 120)
		{
			return false;
		}
		// 4 s of game time at 30 fps.
		const FNoctisFrameOutput* Front = Sub->GetFrontFrame();
		Test->TestTrue(TEXT("des trames sont publiées"), Front && Front->Sequence >= FirstSequence);
		Test->TestTrue(TEXT("espèces en cache"), Sub->GetSpecies().Num() > 0);
		bool bValid = false;
		std::string Error;
		Sub->WithSimulation([&](noctis::WorldSimulation& Sim) { bValid = NoctisChecks::ValidateSnapshot(Sub->GetFrontFrame()->Snapshot, Sim, Error); });
		Test->TestTrue(FString::Printf(TEXT("instantané publié cohérent %s"), *NoctisUnreal::ToFString(Error)), bValid);
		// Copy the time: Front points into the triple buffer, which the next publications recycle.
		const double TimeBefore = Sub->GetFrontFrame()->Snapshot.time;
		Sub->SetTimeScale(60.0f);
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			World->Tick(LEVELTICK_All, Dt);
		}
		Test->TestTrue(TEXT("le temps accéléré avance"), Sub->GetFrontFrame()->Snapshot.time > TimeBefore + 1.0);
		return true;
	}

private:
	FAutomationTestBase* Test;
	TSharedRef<FTestWorld> TestWorld;
	double StartSeconds;
	uint64 FirstSequence = 0;
	int32 FramesAfterReady = 0;
};
} // namespace NoctisTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNoctisWorldSubsystemTest, "Noctis.World.SubsystemBoot", NOCTIS_TEST_FLAGS)

bool FNoctisWorldSubsystemTest::RunTest(const FString& Parameters)
{
	if (!NoctisTests::HasData(*this) || !GEngine)
	{
		return false;
	}
	TSharedRef<NoctisTests::FTestWorld> TestWorld = MakeShared<NoctisTests::FTestWorld>();
	ADD_LATENT_AUTOMATION_COMMAND(NoctisTests::FWaitAndCheckSubsystem(this, TestWorld));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
