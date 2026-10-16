#include "NoctisEditorLibrary.h"

#include "NoctisDataCheck.h"
#include "NoctisEditorLog.h"

#include "NoctisSettings.h"
#include "NoctisSpace.h"
#include "Sim/NoctisUnrealAdapters.h"

#include "Misc/ScopedSlowTask.h"

#include "Noctis/Sim/WorldSimulation.h"

#include <memory>

#define LOCTEXT_NAMESPACE "NoctisEditorLibrary"

bool UNoctisEditorLibrary::ComputeTerrainInfo(FNoctisTerrainInfo& OutInfo)
{
	const UNoctisSettings& Settings = UNoctisSettings::Get();
	FScopedSlowTask Task(1.0f, LOCTEXT("Terrain", "Génération du terrain de la vallée (NoctisCore)..."));
	Task.MakeDialog();

	noctis::SimConfig Config;
	Config.dataRoot = NoctisUnreal::ToStdString(Settings.ResolveDataRoot());
	Config.environmentId = NoctisUnreal::ToStdString(Settings.EnvironmentId);
	Config.profileId = NoctisUnreal::ToStdString(Settings.ProfileId);
	Config.seed = static_cast<noctis::u64>(Settings.Seed);
	Config.heightSamples = Settings.HeightSamples;
	Config.materialCells = Settings.MaterialCells;
	Config.vegetationCellM = Settings.VegetationCellM;
	Config.spawnPopulation = false;
	Config.initialRemains = false;
	std::unique_ptr<noctis::WorldSimulation> World = std::make_unique<noctis::WorldSimulation>();
	if (!World->initialize(Config, NoctisUnreal::MakeFileSystem(), NoctisUnreal::MakeScheduler(ENoctisSchedulerKind::ParallelFor, 0)))
	{
		UE_LOG(LogNoctisEditor, Error, TEXT("ComputeTerrainInfo : initialisation du coeur impossible (voir LogNoctisCore)."));
		return false;
	}
	Task.EnterProgressFrame(1.0f);

	FNoctisSpace Space;
	Space.OffsetCm = Settings.WorldOffsetCm;
	const noctis::Terrain& Terrain = World->terrain();
	const noctis::Rect2 Bounds = Terrain.bounds();
	const float Lo = Terrain.minHeight();
	const float Hi = Terrain.maxHeight();
	OutInfo.EnvironmentId = Settings.EnvironmentId;
	OutInfo.Samples = Terrain.heights().width();
	OutInfo.SizeM = Terrain.size();
	OutInfo.MinHeightM = Lo;
	OutInfo.MaxHeightM = Hi;
	// Landscape origin = its first heightmap sample = north-west corner (min x, max y in the core frame).
	OutInfo.LandscapeLocation = Space.ToUE(noctis::Vec2(Bounds.min.x, Bounds.max.y), 0.5 * (double(Lo) + double(Hi)));
	// 16-bit heights span 512 * ScaleZ cm (Unreal landscape convention), centred on the actor.
	OutInfo.LandscapeScale = FVector(double(Terrain.spacing()) * 100.0, double(Terrain.spacing()) * 100.0, FMath::Max(0.01, (double(Hi) - double(Lo)) * 100.0 / 512.0));
	const noctis::Vec2 Basecamp = World->basecamp();
	OutInfo.BasecampLocation = Space.ToUE(Basecamp, double(Terrain.heightAt(Basecamp)));
	OutInfo.ValleyFloorZ = Space.ToUE(noctis::Vec2(0.0f, 0.0f), double(World->context().valleyFloorZ)).Z;
	return true;
}

int32 UNoctisEditorLibrary::ValidateScientificData(bool bOpenMessageLog)
{
	const FNoctisDataReport Report = NoctisEditorData::RunFullCheck(bOpenMessageLog);
	return Report.bLoaded ? Report.Errors : -1;
}

int32 UNoctisEditorLibrary::ReloadScientificData()
{
	return NoctisEditorData::ReloadRunningSimulations();
}

FString UNoctisEditorLibrary::GetDataRoot()
{
	return UNoctisSettings::Get().ResolveDataRoot();
}

#undef LOCTEXT_NAMESPACE
