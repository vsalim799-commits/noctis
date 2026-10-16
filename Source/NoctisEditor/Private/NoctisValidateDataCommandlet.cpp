#include "NoctisValidateDataCommandlet.h"

#include "NoctisDataCheck.h"
#include "NoctisEditorLog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

UNoctisValidateDataCommandlet::UNoctisValidateDataCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UNoctisValidateDataCommandlet::Main(const FString& Params)
{
	const bool bWarningsAsErrors = FParse::Param(*Params, TEXT("WarningsAsErrors"));
	// The visual data cross-check needs the full asset registry.
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.SearchAllAssets(/*bSynchronousSearch*/ true);

	const FNoctisDataReport Report = NoctisEditorData::RunFullCheck(/*bOpenMessageLog*/ false);
	UE_LOG(LogNoctisEditor, Display, TEXT("NoctisValidateData : %s, %d espèces, %d environnements, %d fiches visuelles, %d erreur(s), %d avertissement(s)."),
		   *Report.DataRoot, Report.SpeciesCount, Report.EnvironmentCount, Report.VisualAssetCount, Report.Errors, Report.Warnings);
	const bool bOk = Report.bLoaded && Report.Errors == 0 && (!bWarningsAsErrors || Report.Warnings == 0);
	return bOk ? 0 : 1;
}
