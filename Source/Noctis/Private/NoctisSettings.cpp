#include "NoctisSettings.h"

#include "HAL/FileManager.h"
#include "InputMappingContext.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/Paths.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"

UNoctisSettings::UNoctisSettings()
{
	// Assets created by Content/Python/setup_project.py and import_audio.py.
	WeatherParameters = TSoftObjectPtr<UMaterialParameterCollection>(FSoftObjectPath(TEXT("/Game/Environment/MPC_NoctisWeather.MPC_NoctisWeather")));
	CallAttenuation = TSoftObjectPtr<USoundAttenuation>(FSoftObjectPath(TEXT("/Game/Audio/Attenuation/ATT_Noctis_SpatialOnly.ATT_Noctis_SpatialOnly")));
	CallConcurrency = TSoftObjectPtr<USoundConcurrency>(FSoftObjectPath(TEXT("/Game/Audio/Concurrency/SCON_Noctis_Calls.SCON_Noctis_Calls")));
	ThunderSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Weather/SC_Thunder.SC_Thunder")));
	SplashSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Water/SC_Splash.SC_Splash")));
	VegetationCrashSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Foley/SC_VegetationCrash.SC_VegetationCrash")));
	ImpactSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Foley/SC_Impact.SC_Impact")));
	ResearcherContext = TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/Game/Input/IMC_Researcher.IMC_Researcher")));
	VehicleContext = TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/Game/Input/IMC_Vehicle.IMC_Vehicle")));
	DroneContext = TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/Game/Input/IMC_Drone.IMC_Drone")));
	ToolsContext = TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/Game/Input/IMC_Tools.IMC_Tools")));
}

FString UNoctisSettings::ResolveDataRoot() const
{
	auto LooksLikeDataRoot = [](const FString& Dir) { return IFileManager::Get().DirectoryExists(*FPaths::Combine(Dir, TEXT("Species"))); };
	if (!DataRootOverride.IsEmpty())
	{
		FString Path = DataRootOverride;
		if (FPaths::IsRelative(Path))
		{
			Path = FPaths::Combine(FPaths::ProjectDir(), Path);
		}
		return FPaths::ConvertRelativePathToFull(Path);
	}
	// <Project>/Data: the repository's Data/ in the editor, and the RuntimeDependencies copy in staged builds.
	const FString ProjectData = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Data")));
	if (LooksLikeDataRoot(ProjectData))
	{
		return ProjectData;
	}
	// Optional copy under Content/NoctisData (staged as loose files, see DefaultGame.ini).
	const FString ContentData = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("NoctisData")));
	if (LooksLikeDataRoot(ContentData))
	{
		return ContentData;
	}
	return ProjectData;
}
