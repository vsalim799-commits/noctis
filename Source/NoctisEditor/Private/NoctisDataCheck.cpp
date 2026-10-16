#include "NoctisDataCheck.h"

#include "NoctisEditorLog.h"

#include "Creatures/NoctisSpeciesVisualData.h"
#include "NoctisSettings.h"
#include "Sim/NoctisUnrealAdapters.h"
#include "Sim/NoctisWorldSubsystem.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Logging/MessageLog.h"

#include "Noctis/Science/EvidenceDatabase.h"
#include "Noctis/Science/Taxonomy.h"

#include <algorithm>
#include <memory>

#define LOCTEXT_NAMESPACE "NoctisDataCheck"

namespace NoctisEditorData
{
const FName MessageLogName(TEXT("Noctis"));

namespace
{
std::unique_ptr<noctis::EvidenceDatabase> GDatabase;
FString GDatabaseRoot;
bool GDatabaseFailed = false;

FText Utf8(const std::string& Text)
{
	return FText::FromString(NoctisUnreal::ToFString(Text));
}

bool InEnvironment(const noctis::SpeciesDefinition& Species, const std::string& EnvironmentId)
{
	return std::find(Species.regions.begin(), Species.regions.end(), EnvironmentId) != Species.regions.end();
}

TArray<UNoctisSpeciesVisualData*> LoadVisualAssets()
{
	TArray<UNoctisSpeciesVisualData*> Result;
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FARFilter Filter;
	Filter.ClassPaths.Add(UNoctisSpeciesVisualData::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);
	for (const FAssetData& Asset : Assets)
	{
		// Small data assets: loading them all is cheap (meshes stay soft references).
		if (UNoctisSpeciesVisualData* Visual = Cast<UNoctisSpeciesVisualData>(Asset.GetAsset()))
		{
			Result.Add(Visual);
		}
	}
	return Result;
}
} // namespace

const noctis::EvidenceDatabase* GetDatabase()
{
	const FString Root = UNoctisSettings::Get().ResolveDataRoot();
	if (GDatabase && GDatabaseRoot == Root)
	{
		return GDatabase.get();
	}
	if (GDatabaseFailed && GDatabaseRoot == Root)
	{
		return nullptr;
	}
	GDatabaseRoot = Root;
	std::unique_ptr<noctis::EvidenceDatabase> Database = std::make_unique<noctis::EvidenceDatabase>();
	const FNoctisUnrealFileSystem FileSystem;
	if (!Database->load(FileSystem, NoctisUnreal::ToStdString(Root)))
	{
		UE_LOG(LogNoctisEditor, Warning, TEXT("Données scientifiques non chargées depuis %s (lancer Outils > Noctis > Valider les données scientifiques)."), *Root);
		GDatabase.reset();
		GDatabaseFailed = true;
		return nullptr;
	}
	GDatabaseFailed = false;
	GDatabase = std::move(Database);
	return GDatabase.get();
}

void Invalidate()
{
	GDatabase.reset();
	GDatabaseFailed = false;
	GDatabaseRoot.Reset();
}

FNoctisDataReport RunFullCheck(bool bOpenMessageLog)
{
	FNoctisDataReport Report;
	Report.DataRoot = UNoctisSettings::Get().ResolveDataRoot();
	FMessageLog Log(MessageLogName);
	Log.NewPage(FText::Format(LOCTEXT("Page", "Validation des données scientifiques ({0})"), FText::AsDateTime(FDateTime::Now())));

	auto Add = [&Log, &Report](noctis::ValidationIssue::Severity Severity, const FText& Text) {
		switch (Severity)
		{
		case noctis::ValidationIssue::Severity::Error:
			++Report.Errors;
			Log.Error(Text);
			UE_LOG(LogNoctisEditor, Error, TEXT("%s"), *Text.ToString());
			break;
		case noctis::ValidationIssue::Severity::Warning:
			++Report.Warnings;
			Log.Warning(Text);
			UE_LOG(LogNoctisEditor, Warning, TEXT("%s"), *Text.ToString());
			break;
		case noctis::ValidationIssue::Severity::Info:
			++Report.Infos;
			Log.Info(Text);
			UE_LOG(LogNoctisEditor, Log, TEXT("%s"), *Text.ToString());
			break;
		}
	};
	auto AddIssue = [&Add](const noctis::ValidationIssue& Issue) {
		const FText Where = Issue.file.empty() ? FText::GetEmpty() : FText::Format(LOCTEXT("IssueFile", "{0} : "), Utf8(Issue.file));
		Add(Issue.severity, FText::Format(LOCTEXT("Issue", "{0}{1}"), Where, Utf8(Issue.message)));
	};

	Invalidate();
	const noctis::EvidenceDatabase* Database = GetDatabase();
	if (!Database)
	{
		Add(noctis::ValidationIssue::Severity::Error,
			FText::Format(LOCTEXT("LoadFailed", "Impossible de charger les données depuis {0}. Vérifier que Data/ est à côté de Noctis.uproject "
												"ou renseigner Project Settings > Game > Noctis > DataRootOverride."),
						  FText::FromString(Report.DataRoot)));
		// Re-run the load in a scratch database just to list what went wrong.
		noctis::EvidenceDatabase Scratch;
		const FNoctisUnrealFileSystem FileSystem;
		Scratch.load(FileSystem, NoctisUnreal::ToStdString(Report.DataRoot));
		for (const noctis::ValidationIssue& Issue : Scratch.loadIssues())
		{
			AddIssue(Issue);
		}
		if (bOpenMessageLog)
		{
			Log.Open(EMessageSeverity::Info, true);
		}
		return Report;
	}
	Report.bLoaded = true;
	Report.SpeciesCount = static_cast<int32>(Database->species().size());
	Report.EnvironmentCount = static_cast<int32>(Database->environments().size());

	// 1. The core's own checks: load issues, then cross-reference consistency.
	for (const noctis::ValidationIssue& Issue : Database->loadIssues())
	{
		AddIssue(Issue);
	}
	for (const noctis::ValidationIssue& Issue : Database->validate())
	{
		AddIssue(Issue);
	}

	// 2. The configured world must exist.
	const UNoctisSettings& Settings = UNoctisSettings::Get();
	const std::string EnvironmentId = NoctisUnreal::ToStdString(Settings.EnvironmentId);
	if (!Database->findEnvironment(EnvironmentId))
	{
		Add(noctis::ValidationIssue::Severity::Error,
			FText::Format(LOCTEXT("NoEnvironment", "L'environnement '{0}' (Project Settings > Game > Noctis > EnvironmentId) n'existe pas dans Data/Environments."),
						  FText::FromString(Settings.EnvironmentId)));
	}
	if (!Database->findProfile(NoctisUnreal::ToStdString(Settings.ProfileId)))
	{
		Add(noctis::ValidationIssue::Severity::Error,
			FText::Format(LOCTEXT("NoProfile", "Le profil de reconstruction '{0}' (ProfileId) n'existe pas dans Data/Profiles."), FText::FromString(Settings.ProfileId)));
	}

	// 3. Species visual data assets <-> species files.
	const TArray<UNoctisSpeciesVisualData*> Visuals = LoadVisualAssets();
	Report.VisualAssetCount = Visuals.Num();
	TMap<FName, UNoctisSpeciesVisualData*> BySpecies;
	for (UNoctisSpeciesVisualData* Visual : Visuals)
	{
		const FText AssetName = FText::FromString(Visual->GetPathName());
		if (Visual->SpeciesId.IsNone())
		{
			Add(noctis::ValidationIssue::Severity::Error, FText::Format(LOCTEXT("VisualNoId", "{0} : SpeciesId vide."), AssetName));
			continue;
		}
		if (UNoctisSpeciesVisualData** Existing = BySpecies.Find(Visual->SpeciesId))
		{
			Add(noctis::ValidationIssue::Severity::Error,
				FText::Format(LOCTEXT("VisualDuplicate", "Deux fiches visuelles pour l'espèce '{0}' : {1} et {2} (le gestionnaire de créatures n'en utiliserait qu'une)."),
							  FText::FromName(Visual->SpeciesId), FText::FromString((*Existing)->GetPathName()), AssetName));
			continue;
		}
		BySpecies.Add(Visual->SpeciesId, Visual);
		if (!Database->findSpecies(NoctisUnreal::ToStdString(Visual->SpeciesId.ToString())))
		{
			Add(noctis::ValidationIssue::Severity::Error,
				FText::Format(LOCTEXT("VisualUnknown", "{0} : l'espèce '{1}' n'existe dans aucun fichier Data/Species/*/*.json."), AssetName, FText::FromName(Visual->SpeciesId)));
		}
	}
	int32 Missing = 0;
	for (const noctis::SpeciesDefinition& Species : Database->species())
	{
		if (!InEnvironment(Species, EnvironmentId) || Species.sim.role != noctis::SimRole::Agent)
		{
			continue; // population-level taxa (insects, fish...) are rendered by ambient systems, not actors
		}
		if (!BySpecies.Contains(FName(*NoctisUnreal::ToFString(Species.id))))
		{
			++Missing;
			Add(noctis::ValidationIssue::Severity::Warning,
				FText::Format(LOCTEXT("VisualMissing", "Espèce '{0}' ({1}) simulée dans '{2}' sans fiche visuelle : ses individus vivent dans la simulation mais "
													   "ne sont pas affichés. Importer ses assets avec Content/Python/import_creatures.py."),
							  Utf8(Species.id), Utf8(Species.scientificName), FText::FromString(Settings.EnvironmentId)));
		}
	}

	Add(noctis::ValidationIssue::Severity::Info,
		FText::Format(LOCTEXT("Summary", "Données : {0} — {1} espèces, {2} environnements, {3} fiches visuelles ({4} espèces de '{5}' sans fiche). "
										 "{6} erreur(s), {7} avertissement(s)."),
					  FText::FromString(Report.DataRoot), Report.SpeciesCount, Report.EnvironmentCount, Report.VisualAssetCount, Missing,
					  FText::FromString(Settings.EnvironmentId), Report.Errors, Report.Warnings));
	if (bOpenMessageLog)
	{
		Log.Open(EMessageSeverity::Info, true);
	}
	return Report;
}

int32 ReloadRunningSimulations()
{
	Invalidate();
	int32 Count = 0;
	if (!GEngine)
	{
		return Count;
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (!World || (Context.WorldType != EWorldType::PIE && Context.WorldType != EWorldType::Game))
		{
			continue;
		}
		if (UNoctisWorldSubsystem* Sub = World->GetSubsystem<UNoctisWorldSubsystem>())
		{
			// Rebuilt at the next pre-actor tick: Data/ is re-read from disk with the current settings.
			Sub->RequestReload();
			++Count;
		}
	}
	UE_LOG(LogNoctisEditor, Log, TEXT("Rechargement des données scientifiques demandé pour %d monde(s) en cours d'exécution%s."), Count,
		   Count == 0 ? TEXT(" (aucune session PIE : la prochaine lira Data/ à son démarrage)") : TEXT(""));
	return Count;
}
} // namespace NoctisEditorData

#undef LOCTEXT_NAMESPACE
