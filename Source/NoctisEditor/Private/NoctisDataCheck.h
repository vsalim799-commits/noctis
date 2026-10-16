// Editor-side checks of the scientific data (Data/**/*.json) and of the species visual data assets.
//
// The data is loaded exactly as the game loads it: noctis::EvidenceDatabase through FNoctisUnrealFileSystem, from
// UNoctisSettings::ResolveDataRoot(). Results go to the Message Log listing "Noctis" (Window > Message Log) and to
// LogNoctisEditor, so the same code serves the Tools menu, the console command and the NoctisValidateData commandlet.
#pragma once

#include "CoreMinimal.h"

namespace noctis
{
class EvidenceDatabase;
}

struct FNoctisDataReport
{
	bool bLoaded = false;
	int32 Errors = 0;
	int32 Warnings = 0;
	int32 Infos = 0;
	int32 SpeciesCount = 0;
	int32 EnvironmentCount = 0;
	int32 VisualAssetCount = 0;
	FString DataRoot;
};

namespace NoctisEditorData
{
/** Name of the Message Log listing registered by the editor module. */
extern const FName MessageLogName;

/** Database shared by the asset validator, loaded on first use from the current data root. Game thread only.
 *  Returns null when Data/ cannot be loaded (the full check reports why). */
const noctis::EvidenceDatabase* GetDatabase();

/** Drops the cached database (after "reload scientific data" or a change of DataRootOverride). */
void Invalidate();

/** Loads Data/, runs the core's consistency checks (EvidenceDatabase::validate) and cross-checks every
 *  UNoctisSpeciesVisualData asset against it. Writes everything to the Message Log. */
FNoctisDataReport RunFullCheck(bool bOpenMessageLog);

/** Asks every running PIE / game world to rebuild its valley from Data/ (UNoctisWorldSubsystem::RequestReload). */
int32 ReloadRunningSimulations();
} // namespace NoctisEditorData
