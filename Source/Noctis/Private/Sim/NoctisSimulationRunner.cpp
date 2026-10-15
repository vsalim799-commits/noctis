#include "Sim/NoctisSimulationRunner.h"

#include "NoctisLog.h"

#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include <algorithm>
#include <cmath>

FNoctisSimulationRunner::FNoctisSimulationRunner()
{
	for (TUniquePtr<FNoctisFrameOutput>& Buffer : Buffers)
	{
		Buffer = MakeUnique<FNoctisFrameOutput>();
	}
}

FNoctisSimulationRunner::~FNoctisSimulationRunner()
{
	Shutdown();
}

bool FNoctisSimulationRunner::Initialize(const noctis::SimConfig& Config, const FParams& InParams, FString& OutError)
{
	check(!bJobInFlight);
	TRACE_CPUPROFILER_EVENT_SCOPE(NoctisSim_Initialize);
	Params = InParams;
	std::unique_ptr<noctis::WorldSimulation> Sim = std::make_unique<noctis::WorldSimulation>();
	if (!Sim->initialize(Config, NoctisUnreal::MakeFileSystem(), NoctisUnreal::MakeScheduler(Params.Scheduler, Config.threads)))
	{
		OutError = FString::Printf(TEXT("noctis::WorldSimulation::initialize a échoué (données '%s', environnement '%s', profil '%s')."),
								   *NoctisUnreal::ToFString(Config.dataRoot), *NoctisUnreal::ToFString(Config.environmentId),
								   *NoctisUnreal::ToFString(Config.profileId));
		return false;
	}
	Simulation = std::move(Sim);
	Simulation->setTimeScale(TimeScale);
	AppliedTimeScale = TimeScale;

	// First frame: Front == Previous, so interpolation and the presentation actors are valid before the first job.
	FNoctisFrameOutput& Front = *Buffers[FrontIndex];
	Simulation->buildSnapshot(Front.Snapshot);
	BuildFrameExtras(Front, LatestInput);
	TSharedRef<FNoctisAuxData, ESPMode::ThreadSafe> Aux = MakeShared<FNoctisAuxData, ESPMode::ThreadSafe>();
	BuildAuxData(*Aux, Simulation->basecamp());
	Aux->Revision = ++AuxRevision;
	Front.Aux = Aux;
	Front.Sequence = ++Sequence;
	*Buffers[PreviousIndex] = Front;
	Front.Snapshot.newSounds.clear(); // never play the initial sounds twice
	Buffers[PreviousIndex]->Snapshot.newSounds.clear();
	Buffers[PreviousIndex]->SoundExtras.Reset();
	Front.SoundExtras.Reset();
	UE_LOG(LogNoctis, Log, TEXT("Simulation prête : %d individus, horloge %s."), Front.AliveCount, *Front.ClockText);
	return true;
}

void FNoctisSimulationRunner::Shutdown()
{
	if (bJobInFlight)
	{
		Job.Wait();
		bJobInFlight = false;
	}
	InFlightCommands.Reset();
	PendingCommands.Reset();
	Simulation.reset();
}

void FNoctisSimulationRunner::SetInput(const FNoctisFrameInput& Input)
{
	LatestInput = Input;
}

void FNoctisSimulationRunner::EnqueueCommand(TUniqueFunction<void(noctis::WorldSimulation&)> SimWork, TUniqueFunction<void()> OnGameThread)
{
	FPendingCommand& Command = PendingCommands.AddDefaulted_GetRef();
	Command.SimWork = MoveTemp(SimWork);
	Command.OnGameThread = MoveTemp(OnGameThread);
}

void FNoctisSimulationRunner::SetTimeScale(float Scale)
{
	TimeScale = FMath::Max(0.0f, Scale);
}

double FNoctisSimulationRunner::StepSecondsForScale(float Scale) const
{
	const double Fixed = FMath::Max(0.001, double(Params.FixedStepSeconds));
	if (Scale <= Params.FullDetailMaxTimeScale)
	{
		return Fixed;
	}
	// Above the full-detail limit the core runs coarse kinematics only; larger steps keep the job count bounded.
	const double Scaled = Fixed * double(Scale) / FMath::Max(1.0, double(Params.FullDetailMaxTimeScale));
	return FMath::Clamp(Scaled, Fixed, FMath::Max(Fixed, double(Params.CoarseStepSeconds)));
}

void FNoctisSimulationRunner::Tick(float RealDeltaSeconds)
{
	if (!Simulation)
	{
		return;
	}
	if (bJobInFlight && (bLockstep || Job.IsCompleted()))
	{
		Job.Wait();
		Publish();
	}
	SecondsSinceAux += RealDeltaSeconds;
	const double Step = StepSecondsForScale(TimeScale);
	if (!bPaused && TimeScale > 0.0f)
	{
		const double Advance = double(RealDeltaSeconds) * double(TimeScale);
		SimSecondsSincePublish += Advance;
		// Debt beyond the backlog is dropped: under load simulated time slows down instead of spiralling.
		const double MaxBacklog = Step + double(Params.MaxBacklogSeconds) * double(TimeScale);
		AccumulatedSimSeconds = FMath::Min(AccumulatedSimSeconds + Advance, MaxBacklog);
	}
	if (bJobInFlight)
	{
		return;
	}
	const int32 Steps = FMath::Clamp(static_cast<int32>(FMath::FloorToDouble(AccumulatedSimSeconds / Step)), 0, FMath::Max(1, Params.MaxStepsPerJob));
	if (Steps == 0 && PendingCommands.Num() == 0)
	{
		return;
	}
	AccumulatedSimSeconds -= double(Steps) * Step;
	Launch(Steps, Step);
}

void FNoctisSimulationRunner::Launch(int32 Steps, double StepSeconds)
{
	check(!bJobInFlight);
	InFlightInput = LatestInput;
	InFlightCommands = MoveTemp(PendingCommands);
	PendingCommands.Reset();
	const bool bBuildAux = SecondsSinceAux >= double(Params.AuxIntervalSeconds);
	if (bBuildAux)
	{
		SecondsSinceAux = 0.0;
	}
	const float Scale = TimeScale;
	bJobInFlight = true;
	if (Params.bAsync)
	{
		// The lambda only touches Simulation, InFlightInput, InFlightCommands, AppliedTimeScale, AuxRevision and the
		// Back buffer, none of which the game thread reads or writes until Publish().
		Job = UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, Steps, StepSeconds, bBuildAux, Scale]() { RunJob(Steps, StepSeconds, bBuildAux, Scale); });
	}
	else
	{
		RunJob(Steps, StepSeconds, bBuildAux, Scale);
		Publish();
	}
}

void FNoctisSimulationRunner::RunJob(int32 Steps, double StepSeconds, bool bBuildAux, float Scale)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(NoctisSim_Job);
	const double T0 = FPlatformTime::Seconds();
	noctis::WorldSimulation& Sim = *Simulation;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(NoctisSim_Commands);
		for (FPendingCommand& Command : InFlightCommands)
		{
			if (Command.SimWork)
			{
				Command.SimWork(Sim);
			}
		}
	}
	if (Scale != AppliedTimeScale)
	{
		Sim.setTimeScale(Scale);
		AppliedTimeScale = Scale;
	}
	ApplyInput(InFlightInput);
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(NoctisSim_Steps);
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			Sim.step(StepSeconds);
		}
	}
	FNoctisFrameOutput& Out = *Buffers[BackIndex];
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(NoctisSim_Snapshot);
		Sim.buildSnapshot(Out.Snapshot);
		BuildFrameExtras(Out, InFlightInput);
	}
	Out.StepsInJob = Steps;
	Out.StepSeconds = StepSeconds;
	Out.TimeScale = Scale;
	if (bBuildAux)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(NoctisSim_Aux);
		TSharedRef<FNoctisAuxData, ESPMode::ThreadSafe> Aux = MakeShared<FNoctisAuxData, ESPMode::ThreadSafe>();
		const noctis::Vec2 Focus = InFlightInput.bHasFocus ? InFlightInput.Focus : Sim.research().eyePosition().xy();
		BuildAuxData(*Aux, Focus);
		Aux->Revision = ++AuxRevision;
		Out.Aux = Aux;
	}
	else
	{
		Out.Aux.Reset(); // Publish() carries the previous one forward
	}
	Out.JobMilliseconds = (FPlatformTime::Seconds() - T0) * 1000.0;
}

void FNoctisSimulationRunner::ApplyInput(const FNoctisFrameInput& Input)
{
	noctis::ResearchSystem& Research = Simulation->research();
	if (Input.bHasResearcher)
	{
		noctis::ResearcherState& State = Research.researcher().state();
		if (!State.inVehicle)
		{
			Research.researcher().setTransform(Input.ResearcherEye, Input.ResearcherHeading, Input.ResearcherVelocity);
		}
		State.crouched = Input.bCrouched;
		State.talking = Input.bTalking;
		State.headlamp = Input.bHeadlamp;
	}
	Research.setExternalVehicle(Input.bVehicleExternal);
	if (Input.bVehicleExternal && Input.bHasVehicleTransform)
	{
		Research.vehicle().setTransform(Input.VehiclePosition, Input.VehicleHeading, Input.VehicleSpeed);
	}
	if (Input.bHasVehicleControls)
	{
		Research.setVehicleControls(Input.VehicleControls);
	}
	if (Input.bDroneManual)
	{
		// setManual() is ignored while docked or lost (DroneSystem::command).
		Research.drone().setManual(Input.DroneManualVelocity, Input.DroneManualAltitudeM);
	}
}

void FNoctisSimulationRunner::BuildFrameExtras(FNoctisFrameOutput& Out, const FNoctisFrameInput& Input)
{
	noctis::WorldSimulation& Sim = *Simulation;
	const noctis::Terrain& Terrain = Sim.terrain();
	noctis::WaterSystem& Water = Sim.water();

	Out.SoundExtras.SetNum(static_cast<int32>(Out.Snapshot.newSounds.size()));
	const noctis::Vec3 Listener = Sim.research().eyePosition();
	const noctis::PropagationEnv Env = Sim.context().propagation();
	for (int32 Index = 0; Index < Out.SoundExtras.Num(); ++Index)
	{
		const noctis::SoundSource& Sound = Out.Snapshot.newSounds[static_cast<size_t>(Index)];
		const noctis::Vec2 P = Sound.position.xy();
		FNoctisSoundExtra& Extra = Out.SoundExtras[Index];
		Extra.Substrate = static_cast<uint8>(Terrain.substrateAt(P));
		Extra.bInWater = Water.depthAt(P, Terrain) > 0.05f;
		Extra.DistanceM = noctis::distance(Sound.position, Listener);
		// Same decomposition as noctis::AcousticRecorder: what the full model adds beyond spreading and f0 absorption.
		const bool bLoud = Sound.kind == noctis::SoundKind::Vocalization || Sound.kind == noctis::SoundKind::Thunder;
		if (Extra.DistanceM <= (bLoud ? 6000.0f : 400.0f))
		{
			const float D = FMath::Max(1.0f, Extra.DistanceM);
			Extra.ReceivedLevelDb = noctis::SoundField::receivedLevelDb(Sound, Listener, Env);
			const float SpreadingAndF0 = Sound.levelDb - 20.0f * FMath::LogX(10.0f, D) -
										 noctis::SoundField::absorptionDbPerM(Sound.f0Hz * Sound.pitchShift, Env.temperatureC, Env.humidity) * D;
			Extra.ExtraGainDb = Extra.ReceivedLevelDb - SpreadingAndF0;
		}
	}

	const noctis::ResearchSystem& Research = Sim.research();
	Out.Researcher = Research.researcher().state();
	Out.Vehicle = Research.vehicle().state();
	Out.Drone = Research.drone().state();

	Out.Wheels.Reset();
	for (const noctis::Vec2& Contact : Input.WheelContacts)
	{
		FNoctisWheelGround& Wheel = Out.Wheels.AddDefaulted_GetRef();
		noctis::VehicleSystem::wheelGround(Terrain, Contact, Input.WheelLoadN, Input.WheelWidthM, Input.WheelRadiusM, Wheel.SinkageM, Wheel.RollingCoef,
										   Wheel.TractionMu);
		Wheel.Substrate = static_cast<uint8>(Terrain.substrateAt(Contact));
		Wheel.WaterDepthM = Water.depthAt(Contact, Terrain);
		Wheel.Moisture = Terrain.moistureAt(Contact);
	}

	Out.Timings = Sim.timings();
	Out.ClockText = NoctisUnreal::ToFString(Sim.clock().formatted());
	Out.Tick = Sim.tick();
	Out.AliveCount = static_cast<int32>(Sim.creatures().aliveCount());
	Out.ValleyFloorZ = Sim.context().valleyFloorZ;
	const bool bNight = Out.Snapshot.sky.sunElevationDeg < -6.0f;
	Out.InsectChorus = Sim.ecology().insectChorus(bNight);
	Out.BirdActivity = Sim.ecology().birdActivity(Out.Snapshot.sky.sunElevationDeg);
}

void FNoctisSimulationRunner::BuildAuxData(FNoctisAuxData& Aux, const noctis::Vec2& Focus) const
{
	noctis::WorldSimulation& Sim = *Simulation;
	const noctis::Terrain& Terrain = Sim.terrain();
	noctis::WaterSystem& Water = Sim.water();
	const float Radius = FMath::Max(50.0f, Params.AuxRadiusM);
	Aux.Time = Sim.clock().seconds();
	Aux.Focus = Focus;
	Aux.RadiusM = Radius;

	// Footprints (visible ones only; the hidden maker identity is kept for debug tools but never displayed).
	{
		std::vector<const noctis::Footprint*> Prints;
		Sim.tracks().query(Focus, Radius, Prints);
		Aux.Footprints.reserve(Prints.size());
		for (const noctis::Footprint* Print : Prints)
		{
			if (Print && Sim.tracks().visible(*Print))
			{
				Aux.Footprints.push_back(*Print);
			}
		}
	}

	// Window helper on a Grid2D.
	auto Window = [&Focus, Radius](const auto& Grid, int32& X0, int32& Y0, int32& W, int32& H) {
		int X0i = 0, Y0i = 0, X1i = 0, Y1i = 0;
		Grid.worldToCell(Focus - noctis::Vec2(Radius, Radius), X0i, Y0i);
		Grid.worldToCell(Focus + noctis::Vec2(Radius, Radius), X1i, Y1i);
		X0 = FMath::Clamp(X0i, 0, Grid.width() - 1);
		Y0 = FMath::Clamp(Y0i, 0, Grid.height() - 1);
		const int32 X1 = FMath::Clamp(X1i, 0, Grid.width() - 1);
		const int32 Y1 = FMath::Clamp(Y1i, 0, Grid.height() - 1);
		W = FMath::Max(0, X1 - X0 + 1);
		H = FMath::Max(0, Y1 - Y0 + 1);
	};

	// Vegetation: trail wear, recent trampling, fire.
	{
		const noctis::VegetationSystem& Vegetation = Sim.vegetation();
		const noctis::Grid2D<float>& Trails = Vegetation.trails();
		const noctis::Grid2D<float>& Trampling = Vegetation.trampling();
		const noctis::Grid2D<noctis::u8>& Fire = Vegetation.fireState();
		if (!Trails.empty())
		{
			int32 X0 = 0, Y0 = 0;
			Window(Trails, X0, Y0, Aux.VegW, Aux.VegH);
			Aux.VegCellM = Trails.cellSize();
			Aux.VegOrigin = Trails.origin() + noctis::Vec2(float(X0) * Aux.VegCellM, float(Y0) * Aux.VegCellM);
			const int32 Count = Aux.VegW * Aux.VegH;
			Aux.Trail.SetNumUninitialized(Count);
			Aux.Trampling.SetNumUninitialized(Count);
			Aux.Burnt.SetNumUninitialized(Count);
			const bool bTramplingSameGrid = Trampling.width() == Trails.width() && Trampling.height() == Trails.height();
			const bool bFireSameGrid = Fire.width() == Trails.width() && Fire.height() == Trails.height();
			for (int32 Y = 0; Y < Aux.VegH; ++Y)
			{
				for (int32 X = 0; X < Aux.VegW; ++X)
				{
					const int32 K = Y * Aux.VegW + X;
					Aux.Trail[K] = static_cast<uint8>(FMath::RoundToInt(noctis::saturate(Trails.at(X0 + X, Y0 + Y)) * 255.0f));
					Aux.Trampling[K] = bTramplingSameGrid ? static_cast<uint8>(FMath::RoundToInt(noctis::saturate(Trampling.at(X0 + X, Y0 + Y)) * 255.0f)) : 0;
					Aux.Burnt[K] = (bFireSameGrid && Fire.at(X0 + X, Y0 + Y) != 0) ? 255 : 0;
				}
			}
		}
	}

	// Water surface patch (lakes, flooded floodplain, river reach near the camera).
	{
		const noctis::Grid2D<float>& Surface = Water.surface();
		if (!Surface.empty())
		{
			int32 X0 = 0, Y0 = 0;
			Window(Surface, X0, Y0, Aux.WaterW, Aux.WaterH);
			Aux.WaterCellM = Surface.cellSize();
			Aux.WaterOrigin = Surface.origin() + noctis::Vec2(float(X0) * Aux.WaterCellM, float(Y0) * Aux.WaterCellM);
			const int32 Count = Aux.WaterW * Aux.WaterH;
			Aux.WaterSurface.SetNumUninitialized(Count);
			Aux.GroundHeight.SetNumUninitialized(Count);
			Aux.WaterFlow.SetNumZeroed(Count);
			for (int32 Y = 0; Y < Aux.WaterH; ++Y)
			{
				for (int32 X = 0; X < Aux.WaterW; ++X)
				{
					const int32 K = Y * Aux.WaterW + X;
					const noctis::Vec2 P = Surface.cellCenter(X0 + X, Y0 + Y);
					const float Ground = Terrain.heightAt(P);
					const float Level = Surface.at(X0 + X, Y0 + Y);
					Aux.GroundHeight[K] = Ground;
					Aux.WaterSurface[K] = Level;
					if (Level > Ground + 0.01f)
					{
						const noctis::WaterQuery Q = Water.query(P, Terrain);
						Aux.WaterFlow[K] = FVector2f(Q.flow.x, Q.flow.y);
					}
				}
			}
		}
	}

	// River ribbons (whole valley, one sample every ~15 m along the centreline).
	{
		const noctis::WorldLayout& Layout = Sim.layout();
		Aux.RiverStageM = Water.riverStage(0);
		Aux.bFlooding = Water.flooding();
		for (int32 RiverIndex = 0; RiverIndex < static_cast<int32>(Layout.rivers.size()); ++RiverIndex)
		{
			const noctis::RiverDesc& River = Layout.rivers[static_cast<size_t>(RiverIndex)];
			FNoctisRiverSurface& Out = Aux.Rivers.AddDefaulted_GetRef();
			const float Stage = Water.riverStage(RiverIndex);
			const float Bankfull = FMath::Max(0.1f, Water.bankfullDepth(RiverIndex));
			// At-a-station hydraulic geometry: width ~ Q^0.5 and depth ~ Q^0.4, hence width ~ depth^1.25.
			const float WidthFactor = FMath::Clamp(FMath::Pow(FMath::Max(0.0f, Stage) / Bankfull, 1.25f), 0.15f, Water.flooding() ? 1.6f : 1.0f);
			float LastKept = -1e9f;
			for (size_t PointIndex = 0; PointIndex < River.points.size(); ++PointIndex)
			{
				const noctis::RiverPoint& Point = River.points[PointIndex];
				const bool bLast = PointIndex + 1 == River.points.size();
				if (Point.distanceAlong - LastKept < 15.0f && !bLast)
				{
					continue;
				}
				LastKept = Point.distanceAlong;
				const noctis::WaterQuery Q = Water.query(Point.position, Terrain);
				const float SurfaceZ = Q.isWater ? Q.surfaceZ : Point.floodplainZ - Point.depth + Stage;
				Out.Points.Add(FVector3f(Point.position.x, Point.position.y, SurfaceZ));
				Out.Tangents.Add(FVector2f(Point.tangent.x, Point.tangent.y));
				Out.HalfWidthM.Add(0.5f * Point.width * WidthFactor);
				Out.Flow.Add(FVector2f(Q.flow.x, Q.flow.y));
			}
		}
	}
}

void FNoctisSimulationRunner::Publish()
{
	bJobInFlight = false;
	FNoctisFrameOutput& Back = *Buffers[BackIndex];
	if (!Back.Aux.IsValid())
	{
		Back.Aux = Buffers[FrontIndex]->Aux;
	}
	Back.Sequence = ++Sequence;
	const int32 OldPrevious = PreviousIndex;
	PreviousIndex = FrontIndex;
	FrontIndex = BackIndex;
	BackIndex = OldPrevious;
	SimSecondsSincePublish = 0.0;

	TArray<FPendingCommand> Completed = MoveTemp(InFlightCommands);
	InFlightCommands.Reset();
	for (FPendingCommand& Command : Completed)
	{
		if (Command.OnGameThread)
		{
			Command.OnGameThread();
		}
	}
}

void FNoctisSimulationRunner::Flush()
{
	if (!Simulation)
	{
		return;
	}
	if (bJobInFlight)
	{
		Job.Wait();
		Publish();
	}
	if (PendingCommands.Num() > 0)
	{
		TArray<FPendingCommand> Commands = MoveTemp(PendingCommands);
		PendingCommands.Reset();
		for (FPendingCommand& Command : Commands)
		{
			if (Command.SimWork)
			{
				Command.SimWork(*Simulation);
			}
		}
		for (FPendingCommand& Command : Commands)
		{
			if (Command.OnGameThread)
			{
				Command.OnGameThread();
			}
		}
	}
}

void FNoctisSimulationRunner::StepBlocking(int32 Steps, double StepSeconds)
{
	if (!Simulation)
	{
		return;
	}
	Flush();
	InFlightInput = LatestInput;
	InFlightCommands.Reset();
	bJobInFlight = true;
	RunJob(FMath::Max(0, Steps), FMath::Max(1e-4, StepSeconds), /*bBuildAux*/ true, TimeScale);
	Publish();
}

float FNoctisSimulationRunner::GetInterpolationAlpha() const
{
	const double Interval = GetFront().Snapshot.time - GetPrevious().Snapshot.time;
	if (Interval <= 1e-9)
	{
		return 1.0f;
	}
	return static_cast<float>(FMath::Clamp(SimSecondsSincePublish / Interval, 0.0, 1.0));
}

noctis::WorldSimulation& FNoctisSimulationRunner::GetSimulationChecked()
{
	check(Simulation && !bJobInFlight);
	return *Simulation;
}
