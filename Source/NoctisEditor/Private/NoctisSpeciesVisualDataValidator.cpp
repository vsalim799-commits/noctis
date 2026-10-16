#include "NoctisSpeciesVisualDataValidator.h"

#include "NoctisDataCheck.h"

#include "Creatures/NoctisSpeciesVisualData.h"
#include "Sim/NoctisUnrealAdapters.h"

#include "Engine/SkeletalMesh.h"
#include "Misc/DataValidation.h"

#include "Noctis/Science/EvidenceDatabase.h"
#include "Noctis/Science/Taxonomy.h"

#include <algorithm>

#define LOCTEXT_NAMESPACE "NoctisSpeciesVisualDataValidator"

UNoctisSpeciesVisualDataValidator::UNoctisSpeciesVisualDataValidator()
{
	bIsEnabled = true;
}

bool UNoctisSpeciesVisualDataValidator::CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const
{
	return InObject && InObject->IsA<UNoctisSpeciesVisualData>();
}

EDataValidationResult UNoctisSpeciesVisualDataValidator::ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context)
{
	const UNoctisSpeciesVisualData* Visual = CastChecked<UNoctisSpeciesVisualData>(InAsset);
	if (Visual->SpeciesId.IsNone())
	{
		// Reported by UNoctisSpeciesVisualData::IsDataValid.
		return EDataValidationResult::NotValidated;
	}
	const noctis::EvidenceDatabase* Database = NoctisEditorData::GetDatabase();
	if (!Database)
	{
		AssetWarning(InAsset, LOCTEXT("NoData", "Données scientifiques (Data/) non chargées : correspondance espèce non vérifiée. Voir Outils > Noctis > Valider les données scientifiques."));
		AssetPasses(InAsset);
		return EDataValidationResult::Valid;
	}
	const std::string Id = NoctisUnreal::ToStdString(Visual->SpeciesId.ToString());
	const noctis::SpeciesDefinition* Species = Database->findSpecies(Id);
	if (!Species)
	{
		AssetFails(InAsset, FText::Format(LOCTEXT("Unknown", "SpeciesId '{0}' ne correspond à aucun fichier Data/Species/*/{0}.json."), FText::FromName(Visual->SpeciesId)));
		return EDataValidationResult::Invalid;
	}

	bool bFailed = false;
	// Calls: ids from sim.vocal.call_types.
	for (const FNoctisCallSound& Call : Visual->CallSounds)
	{
		const std::string CallId = NoctisUnreal::ToStdString(Call.CallId.ToString());
		const bool bKnown = std::any_of(Species->sim.vocal.calls.begin(), Species->sim.vocal.calls.end(),
										[&CallId](const noctis::CallTypeSpec& Spec) { return Spec.id == CallId; });
		if (!bKnown)
		{
			AssetFails(InAsset, FText::Format(LOCTEXT("UnknownCall", "Cri '{0}' absent de sim.vocal.call_types de {1} : il ne serait jamais joué."), FText::FromName(Call.CallId),
											  FText::FromName(Visual->SpeciesId)));
			bFailed = true;
		}
	}

	// Feet: the core simulates 2 feet for bipeds, 4 for quadrupeds (SpeciesRuntime::legCount).
	const int32 Legs = noctis::isBiped(Species->sim.bodyPlan) ? 2 : 4;
	if (Visual->FootBones.Num() < Legs)
	{
		AssetFails(InAsset, FText::Format(LOCTEXT("Feet", "{0} est quadrupède dans la simulation ({1}) : FootBones doit lister {2} os (HindLeft, HindRight, ForeLeft, ForeRight)."),
										  FText::FromName(Visual->SpeciesId), FText::FromString(NoctisUnreal::ToFString(noctis::bodyPlanKey(Species->sim.bodyPlan))), Legs));
		bFailed = true;
	}

	// Length: actors are scaled by (simulated body length / MeshBodyLengthM). A wrong value makes every animal too big
	// or too small, and foot IK then stretches the legs.
	if (const USkeletalMesh* Mesh = Visual->SkeletalMesh.LoadSynchronous())
	{
		const FBoxSphereBounds Bounds = Mesh->GetImportedBounds();
		const double LengthM = 2.0 * FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y) / 100.0;
		const double Ratio = Visual->MeshBodyLengthM > 0.01f ? LengthM / Visual->MeshBodyLengthM : 0.0;
		if (Ratio < 0.75 || Ratio > 1.35)
		{
			AssetWarning(InAsset, FText::Format(LOCTEXT("Length", "Longueur du maillage {0} m (boîte englobante) mais MeshBodyLengthM = {1} m : vérifier l'échelle d'import (1 unité = 1 cm)."),
												FText::AsNumber(LengthM), FText::AsNumber(Visual->MeshBodyLengthM)));
		}
		const double AdultM = Species->sim.adultLengthM;
		if (AdultM > 0.0 && (Visual->MeshBodyLengthM < 0.2 * AdultM || Visual->MeshBodyLengthM > 2.0 * AdultM))
		{
			AssetWarning(InAsset, FText::Format(LOCTEXT("Adult", "MeshBodyLengthM = {0} m très éloigné de la longueur adulte simulée ({1} m)."), FText::AsNumber(Visual->MeshBodyLengthM),
												FText::AsNumber(AdultM)));
		}
	}

	if (bFailed)
	{
		return EDataValidationResult::Invalid;
	}
	AssetPasses(InAsset);
	return EDataValidationResult::Valid;
}

#undef LOCTEXT_NAMESPACE
