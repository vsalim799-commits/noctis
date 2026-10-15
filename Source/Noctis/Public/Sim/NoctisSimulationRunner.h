// FNoctisSimulationRunner — owns one noctis::WorldSimulation and steps it off the game thread.
//
// Threading model (see Docs/UE5_INTEGRATION.md):
//   * The game thread never touches the simulation while a job is in flight. It only reads published frames.
//   * Once per frame (UNoctisWorldSubsystem, before actors tick) Tick():
//       1. if the in-flight job has finished, publish it: Previous <- Front <- Back (triple buffer, no copy),
//          then run the game-thread completions of the commands that job executed;
//       2. accumulate simulated time (real dt * time scale, clamped to a backlog);
//       3. if no job is in flight, launch one: apply queued commands and the latest input, run N fixed steps,
//          build the RenderSnapshot (+ extras) into the Back buffer. The job runs on a UE::Tasks worker while the
//          game thread and the renderer keep going; the core's own parallel phases go through ParallelFor.
//   * Actors interpolate between Previous and Front (alpha = time since publish * time scale / frame interval).
//   * Synchronous access (UI queries, console commands, tests): WithSimulation() waits for the in-flight job,
//     publishes it and runs the functor on the calling (game) thread; no new job can start meanwhile because jobs
//     are only launched from Tick() on the game thread.
// Determinism: a run is a pure function of (config, seed, sequence of step sizes, commands at step boundaries).
// The scheduler choice (serial, thread pool, ParallelFor) must not change results — checked by the automation tests.
#pragma once

#include "CoreMinimal.h"
#include "Tasks/Task.h"
#include "Templates/Function.h"

#include "Noctis/Sim/WorldSimulation.h"
#include "Sim/NoctisUnrealAdapters.h"

#include <memory>

/** Game -> simulation inputs, sampled once per job (latest value wins). Core frame and units. */
struct FNoctisFrameInput
{
	// Walking researcher: Unreal is authoritative (ACharacter + CharacterMovement), the core gets the eye transform.
	bool bHasResearcher = false;
	noctis::Vec3 ResearcherEye;
	float ResearcherHeading = 0.0f;
	noctis::Vec2 ResearcherVelocity;
	bool bCrouched = false;
	bool bTalking = false;
	bool bHeadlamp = false;

	// Research vehicle. bVehicleExternal: Chaos drives the rover and the core only books energy and noise;
	// otherwise the core's VehicleSystem (Bekker terramechanics) integrates the controls.
	bool bVehicleExternal = false;
	bool bHasVehicleTransform = false;
	noctis::Vec3 VehiclePosition;
	float VehicleHeading = 0.0f;
	float VehicleSpeed = 0.0f;
	bool bHasVehicleControls = false;
	noctis::VehicleControls VehicleControls;
	/** Wheel contact points (core frame) for which terramechanics feedback is computed this job. */
	TArray<noctis::Vec2, TInlineAllocator<6>> WheelContacts;
	float WheelLoadN = 11800.0f;
	float WheelWidthM = 0.42f;
	float WheelRadiusM = 0.6f;

	// Drone manual piloting (other modes are commands: orbit, follow, survey, return home).
	bool bDroneManual = false;
	noctis::Vec2 DroneManualVelocity;
	float DroneManualAltitudeM = 40.0f;

	// Area of interest of the auxiliary export (the active camera).
	bool bHasFocus = false;
	noctis::Vec2 Focus;
};

/** Per-sound data the core does not carry but the audio layer needs (computed on the simulation thread). */
struct FNoctisSoundExtra
{
	uint8 Substrate = 0;
	bool bInWater = false;
	/** Propagation to the researcher's ears, computed with the core's model (spreading, ISO 9613 absorption,
	 *  foliage, wind gradient, terrain barrier). ReceivedLevelDb <= -100 means "not evaluated / inaudible". */
	float DistanceM = 0.0f;
	float ReceivedLevelDb = -100.0f;
	/** Gain to pass to noctis::SignalSynth::render on top of spreading and per-harmonic absorption. */
	float ExtraGainDb = 0.0f;
};

/** Terramechanics feedback for one wheel (Bekker sinkage, rolling resistance, traction). */
struct FNoctisWheelGround
{
	float SinkageM = 0.0f;
	float RollingCoef = 0.05f;
	float TractionMu = 0.6f;
	uint8 Substrate = 0;
	float WaterDepthM = 0.0f;
	float Moisture = 0.0f;
};

/** River centreline with current water surface, for the river ribbon mesh. */
struct FNoctisRiverSurface
{
	TArray<FVector3f> Points; // core frame, z = water surface (m)
	TArray<FVector2f> Tangents;
	TArray<float> HalfWidthM;
	TArray<FVector2f> Flow; // m/s
};

/** Low-rate export around the focus point: traces, vegetation state, water. Shared, immutable once published. */
struct FNoctisAuxData
{
	double Time = 0.0;
	noctis::Vec2 Focus;
	float RadiusM = 0.0f;

	std::vector<noctis::Footprint> Footprints;

	// Vegetation window (row-major, x fastest). Values 0..255.
	noctis::Vec2 VegOrigin;
	float VegCellM = 16.0f;
	int32 VegW = 0;
	int32 VegH = 0;
	TArray<uint8> Trail;
	TArray<uint8> Trampling;
	TArray<uint8> Burnt;

	// Water window: absolute surface and ground heights (m) on the material grid.
	noctis::Vec2 WaterOrigin;
	float WaterCellM = 8.0f;
	int32 WaterW = 0;
	int32 WaterH = 0;
	TArray<float> WaterSurface;
	TArray<float> GroundHeight;
	TArray<FVector2f> WaterFlow;

	TArray<FNoctisRiverSurface> Rivers;
	float RiverStageM = 0.0f;
	bool bFlooding = false;
	int32 Revision = 0;
};

/** One published simulation frame. */
struct FNoctisFrameOutput
{
	noctis::RenderSnapshot Snapshot;
	TArray<FNoctisSoundExtra> SoundExtras; // parallel to Snapshot.newSounds
	noctis::ResearcherState Researcher;
	noctis::VehicleState Vehicle;
	noctis::DroneState Drone;
	TArray<FNoctisWheelGround, TInlineAllocator<6>> Wheels;
	noctis::WorldSimulation::Timings Timings;
	TSharedPtr<const FNoctisAuxData, ESPMode::ThreadSafe> Aux;
	FString ClockText;
	uint64 Tick = 0;
	uint64 Sequence = 0;
	int32 StepsInJob = 0;
	double StepSeconds = 0.0;
	double JobMilliseconds = 0.0;
	int32 AliveCount = 0;
	float TimeScale = 1.0f;
	/** Valley-floor reference height (m, 10th percentile of terrain): base of the cold-air fog layer. */
	float ValleyFloorZ = 0.0f;
	/** Environment soundscape intensities 0..1 from the population layer (insects, birds). */
	float InsectChorus = 0.0f;
	float BirdActivity = 0.0f;
};

class NOCTIS_API FNoctisSimulationRunner
{
public:
	struct FParams
	{
		float FixedStepSeconds = 0.05f;
		float CoarseStepSeconds = 2.0f;
		float FullDetailMaxTimeScale = 8.0f;
		int32 MaxStepsPerJob = 64;
		float MaxBacklogSeconds = 0.25f;
		bool bAsync = true;
		float AuxIntervalSeconds = 1.0f;
		float AuxRadiusM = 600.0f;
		ENoctisSchedulerKind Scheduler = ENoctisSchedulerKind::ParallelFor;
	};

	FNoctisSimulationRunner();
	~FNoctisSimulationRunner();
	FNoctisSimulationRunner(const FNoctisSimulationRunner&) = delete;
	FNoctisSimulationRunner& operator=(const FNoctisSimulationRunner&) = delete;

	/** Builds the world (blocking: terrain generation, populations). Safe to call from a worker thread. */
	bool Initialize(const noctis::SimConfig& Config, const FParams& Params, FString& OutError);
	void Shutdown();
	bool IsInitialized() const { return Simulation != nullptr; }

	// ---- Game thread API ---------------------------------------------------------------------------

	void SetInput(const FNoctisFrameInput& Input);
	/** Runs SimWork on the simulation thread between steps; OnGameThread runs on the game thread when that job is published. */
	void EnqueueCommand(TUniqueFunction<void(noctis::WorldSimulation&)> SimWork, TUniqueFunction<void()> OnGameThread = nullptr);
	void SetTimeScale(float Scale);
	float GetTimeScale() const { return TimeScale; }
	void SetPaused(bool bInPaused) { bPaused = bInPaused; }
	bool IsPaused() const { return bPaused; }
	/** Blocks until a running job finishes (frame lockstep; debugging and capture). */
	void SetLockstep(bool bInLockstep) { bLockstep = bInLockstep; }

	/** Publishes a finished job and launches the next one. Never blocks unless lockstep is on. */
	void Tick(float RealDeltaSeconds);

	/** Waits for the in-flight job, publishes it and executes pending commands synchronously. */
	void Flush();

	/** Synchronous stepping on the calling thread (tests, time skips). Publishes one frame at the end. */
	void StepBlocking(int32 Steps, double StepSeconds);

	/** Exclusive access to the simulation on the game thread (no job in flight while Fn runs). */
	template <typename FuncType>
	void WithSimulation(FuncType&& Fn)
	{
		check(IsInGameThread() || !bJobInFlight);
		Flush();
		if (Simulation)
		{
			Fn(*Simulation);
		}
	}

	const FNoctisFrameOutput& GetFront() const { return *Buffers[FrontIndex]; }
	const FNoctisFrameOutput& GetPrevious() const { return *Buffers[PreviousIndex]; }
	/** 0 = Previous, 1 = Front. */
	float GetInterpolationAlpha() const;
	bool IsJobInFlight() const { return bJobInFlight; }
	double GetCurrentStepSeconds() const { return StepSecondsForScale(TimeScale); }
	uint64 GetPublishedSequence() const { return Sequence; }

	/** Call after changing the core's time scale directly inside WithSimulation(): the next job re-applies TimeScale. */
	void ForceReapplyTimeScale() { check(!bJobInFlight); AppliedTimeScale = -1.0f; }

	/** Direct access; only valid while no job is in flight (asserts). Prefer WithSimulation(). */
	noctis::WorldSimulation& GetSimulationChecked();

private:
	struct FPendingCommand
	{
		TUniqueFunction<void(noctis::WorldSimulation&)> SimWork;
		TUniqueFunction<void()> OnGameThread;
	};

	double StepSecondsForScale(float Scale) const;
	void Launch(int32 Steps, double StepSeconds);
	/** Job body: runs on a worker (or inline when not async). Touches only Simulation, InFlight* and the Back buffer. */
	void RunJob(int32 Steps, double StepSeconds, bool bBuildAux, float Scale);
	void Publish();
	void BuildFrameExtras(FNoctisFrameOutput& Out, const FNoctisFrameInput& Input);
	void BuildAuxData(FNoctisAuxData& Aux, const noctis::Vec2& Focus) const;
	void ApplyInput(const FNoctisFrameInput& Input);

	std::unique_ptr<noctis::WorldSimulation> Simulation;
	FParams Params;

	TUniquePtr<FNoctisFrameOutput> Buffers[3];
	int32 FrontIndex = 0;
	int32 PreviousIndex = 1;
	int32 BackIndex = 2;

	UE::Tasks::FTask Job;
	bool bJobInFlight = false;

	FNoctisFrameInput LatestInput;
	FNoctisFrameInput InFlightInput;
	TArray<FPendingCommand> PendingCommands;
	TArray<FPendingCommand> InFlightCommands;

	double AccumulatedSimSeconds = 0.0;
	double SimSecondsSincePublish = 0.0;
	double SecondsSinceAux = 1e9;
	float TimeScale = 1.0f;
	float AppliedTimeScale = -1.0f;
	bool bPaused = false;
	bool bLockstep = false;
	uint64 Sequence = 0;
	int32 AuxRevision = 0;
};
