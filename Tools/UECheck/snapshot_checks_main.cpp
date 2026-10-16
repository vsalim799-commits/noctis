// Standalone mirror of the Unreal automation tests Noctis.Simulation.* (Source/Noctis/Private/Tests/NoctisSimulationTests.cpp).
// Same reduced valley, same step sizes, same hash and validation code (Source/Noctis/Private/Sim/NoctisSnapshotChecks.h has
// no Unreal dependency), but the core's own schedulers instead of the engine's ParallelFor: it shows, without an engine,
// that the core side of what the automation tests assert holds on the current sources.
//
//   python3 Tools/UECheck/check_ue_layer.py --snapshot          (builds against build/libnoctis_core.a and runs)
//   build/uecheck/snapshot_checks [Data dir]
#include "Sim/NoctisSnapshotChecks.h"

#include "Noctis/Core/Jobs.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

namespace
{
using namespace noctis;

int GFailures = 0;

void Expect(bool Condition, const char* What)
{
	std::printf("  %s %s\n", Condition ? "ok  " : "ECHEC", What);
	GFailures += Condition ? 0 : 1;
}

SimConfig SmallWorldConfig(const std::string& DataRoot, u64 Seed)
{
	SimConfig Config;
	Config.dataRoot = DataRoot;
	Config.seed = Seed;
	Config.heightSamples = 513;
	Config.materialCells = 256;
	Config.populationScale = 0.25f;
	Config.startHour = 6.0;
	return Config;
}

std::unique_ptr<WorldSimulation> MakeWorld(const std::string& DataRoot, u64 Seed, int Threads)
{
	auto World = std::make_unique<WorldSimulation>();
	std::unique_ptr<JobScheduler> Jobs = Threads <= 1 ? std::unique_ptr<JobScheduler>(std::make_unique<SerialScheduler>()) : makeThreadPoolScheduler(Threads);
	SimConfig Config = SmallWorldConfig(DataRoot, Seed);
	Config.threads = Threads;
	if (!World->initialize(Config, nullptr, std::move(Jobs)))
	{
		return nullptr;
	}
	return World;
}

double Seconds()
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Boot(const std::string& Data)
{
	std::printf("Noctis.Simulation.Boot\n");
	const double T0 = Seconds();
	auto World = MakeWorld(Data, 42, 2);
	Expect(World != nullptr, "initialisation");
	if (!World)
	{
		return;
	}
	std::printf("  démarrage %.2f s, %zu espèces, %zu dans la vallée, %zu individus\n", Seconds() - T0, World->species().size(),
				World->speciesInWorld().size(), World->creatures().aliveCount());
	RenderSnapshot S;
	World->buildSnapshot(S);
	std::string Error;
	Expect(NoctisChecks::ValidateSnapshot(S, *World, Error), "instantané initial cohérent");
	const double Start = S.time;
	for (int Step = 0; Step < 40; ++Step)
	{
		World->step(0.05);
	}
	World->buildSnapshot(S);
	Expect(NoctisChecks::ValidateSnapshot(S, *World, Error), "instantané cohérent après 2 s");
	Expect(std::fabs(S.time - Start - 2.0) < 1e-6, "horloge avancée de 2 s");
	Expect(World->tick() == 40, "40 pas comptés");
}

void Determinism(const std::string& Data)
{
	std::printf("Noctis.Simulation.Determinism (série / pool 2 / pool 3 au lieu de série / ParallelFor / pool)\n");
	std::unique_ptr<WorldSimulation> Worlds[3] = {MakeWorld(Data, 42, 1), MakeWorld(Data, 42, 2), MakeWorld(Data, 42, 3)};
	for (auto& W : Worlds)
	{
		if (!W)
		{
			Expect(false, "initialisation");
			return;
		}
	}
	RenderSnapshot S[3];
	int Compared = 0;
	auto Same = [&]() {
		for (int I = 0; I < 3; ++I)
		{
			Worlds[I]->buildSnapshot(S[I]);
		}
		++Compared;
		return NoctisChecks::HashSnapshot(S[0]) == NoctisChecks::HashSnapshot(S[1]) && NoctisChecks::HashSnapshot(S[0]) == NoctisChecks::HashSnapshot(S[2]);
	};
	bool Ok = Same();
	for (int Step = 1; Step <= 600 && Ok; ++Step)
	{
		for (auto& W : Worlds)
		{
			W->step(0.05);
		}
		if (Step % 20 == 0)
		{
			Ok = Same();
		}
	}
	Expect(Ok, "mêmes empreintes en temps réel (30 s, toutes les secondes)");
	for (auto& W : Worlds)
	{
		W->setTimeScale(100.0f);
	}
	for (int Step = 1; Step <= 300 && Ok; ++Step)
	{
		for (auto& W : Worlds)
		{
			W->step(2.0);
		}
		if (Step % 30 == 0)
		{
			Ok = Same();
		}
	}
	Expect(Ok, "mêmes empreintes en temps accéléré (10 min)");
	std::printf("  %d comparaisons, empreinte finale %016llx à t = %.0f s (%zu individus)\n", Compared,
				static_cast<unsigned long long>(NoctisChecks::HashSnapshot(S[0])), S[0].time, S[0].creatures.size());
	auto Other = MakeWorld(Data, 43, 1);
	auto Again = MakeWorld(Data, 42, 1);
	if (Other && Again)
	{
		RenderSnapshot A, B;
		Other->buildSnapshot(A);
		Again->buildSnapshot(B);
		Expect(NoctisChecks::HashSnapshot(A) != NoctisChecks::HashSnapshot(B), "une autre graine donne un autre état");
	}
}

void SnapshotConsistency(const std::string& Data)
{
	std::printf("Noctis.Simulation.SnapshotConsistency\n");
	auto World = MakeWorld(Data, 7, 2);
	if (!World)
	{
		Expect(false, "initialisation");
		return;
	}
	RenderSnapshot Previous, Current;
	World->buildSnapshot(Previous);
	std::string Error;
	int Checked = 0;
	int Sounds = 0;
	auto CheckOne = [&]() {
		World->buildSnapshot(Current);
		++Checked;
		Sounds += static_cast<int>(Current.newSounds.size());
		if (!NoctisChecks::ValidateSnapshot(Current, *World, Error))
		{
			std::printf("  t = %.2f s : %s\n", Current.time, Error.c_str());
			return false;
		}
		const bool Ok = Current.time > Previous.time && NoctisChecks::NoSoundRepeated(Previous, Current) && Current.hourOfDay >= 0.0f && Current.hourOfDay < 24.0f;
		Previous = Current;
		return Ok;
	};
	bool Ok = true;
	for (int Step = 0; Step < 400 && Ok; ++Step)
	{
		World->step(0.05);
		Ok = CheckOne();
	}
	Expect(Ok, "temps réel : 400 instantanés cohérents (un par pas)");
	World->setTimeScale(200.0f);
	for (int Step = 0; Step < 400 && Ok; ++Step)
	{
		World->step(20.0);
		if (Step % 4 == 0)
		{
			Ok = CheckOne();
		}
	}
	Expect(Ok, "temps accéléré : mises à jour horaires et 6-horaires traversées");
	std::printf("  %d instantanés vérifiés, %d sons publiés, %zu individus à t = %.0f s\n", Checked, Sounds, Previous.creatures.size(), Previous.time);
}

void RunnerChunking(const std::string& Data)
{
	std::printf("Noctis.Simulation.Runner (découpage en jobs du runner, sans UE::Tasks)\n");
	auto Async = MakeWorld(Data, 42, 2);
	auto Blocking = MakeWorld(Data, 42, 2);
	if (!Async || !Blocking)
	{
		Expect(false, "initialisation");
		return;
	}
	// Job 0: a command and zero steps (the runner launches a job as soon as a command is pending), then jobs of 1 or 2
	// steps (30 fps against a 50 ms step), each followed by a snapshot as the runner publishes one per job.
	const u64 TickSeenByCommand = Async->tick();
	RenderSnapshot S;
	Async->buildSnapshot(S);
	int Steps = 0;
	double Accumulator = 0.0;
	for (int Frame = 0; Frame < 90; ++Frame)
	{
		Accumulator += 1.0 / 30.0;
		const int N = static_cast<int>(std::floor(Accumulator / 0.05));
		Accumulator -= N * 0.05;
		for (int I = 0; I < N; ++I)
		{
			Async->step(0.05);
		}
		Steps += N;
		if (N > 0)
		{
			Async->buildSnapshot(S);
		}
	}
	for (int I = 0; I < Steps; ++I)
	{
		Blocking->step(0.05);
	}
	RenderSnapshot B;
	Blocking->buildSnapshot(B);
	Expect(TickSeenByCommand == 0, "la commande voit la simulation avant tout pas");
	Expect(S.time == B.time, "même horloge");
	Expect(NoctisChecks::HashSnapshot(S, false) == NoctisChecks::HashSnapshot(B, false), "même état bit à bit (sans les sons, dépendants de la cadence)");
	std::printf("  %d pas, t = %.2f s\n", Steps, S.time);
}
} // namespace

int main(int argc, char** argv)
{
	const std::string Data = argc > 1 ? argv[1] : "Data";
	const double T0 = Seconds();
	Boot(Data);
	Determinism(Data);
	SnapshotConsistency(Data);
	RunnerChunking(Data);
	std::printf("%s : %d échec(s), %.1f s\n", GFailures == 0 ? "SUCCÈS" : "ÉCHEC", GFailures, Seconds() - T0);
	return GFailures == 0 ? 0 : 1;
}
