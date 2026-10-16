// Data validation (right click > Asset Actions > Validate, Tools > Validate Data, and at save/submit) for
// UNoctisSpeciesVisualData, against the scientific data the game will actually load:
//   * SpeciesId must name a species of Data/Species/*/*.json;
//   * every CallSounds entry must name one of the species' call types (sim.vocal.call_types[].id);
//   * quadrupeds need four foot bones (IK targets come from the four simulated feet);
//   * the mesh's length should match MeshBodyLengthM (the actor scale is length-based).
// The asset's own IsDataValid() covers what needs no scientific data (empty id, missing bones in the skeleton).
// Registered automatically: UEditorValidatorSubsystem instantiates every native UEditorValidatorBase subclass.
#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"

#include "NoctisSpeciesVisualDataValidator.generated.h"

UCLASS()
class UNoctisSpeciesVisualDataValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

public:
	UNoctisSpeciesVisualDataValidator();

protected:
	// VERIFY: these FAssetData/FDataValidationContext signatures are the UE 5.4/5.5 ones (5.3 and earlier used
	// CanValidateAsset_Implementation(UObject*) and ValidateLoadedAsset_Implementation(UObject*, TArray<FText>&)).
	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;
};
