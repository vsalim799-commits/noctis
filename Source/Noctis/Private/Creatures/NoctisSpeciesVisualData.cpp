#include "Creatures/NoctisSpeciesVisualData.h"

#include "Creatures/NoctisCreatureAnimInstance.h"

#include "Animation/AnimInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/DataValidation.h"
#include "ReferenceSkeleton.h"

#define LOCTEXT_NAMESPACE "NoctisSpeciesVisualData"

const FPrimaryAssetType UNoctisSpeciesVisualData::PrimaryAssetType(TEXT("NoctisSpeciesVisual"));

UNoctisSpeciesVisualData::UNoctisSpeciesVisualData()
{
	// Bone names of the rig produced by Tools/CreatureFactory (skeleton spec: Data/Anatomy/<species>.json, "bones").
	// import_creatures.py overwrites these from that file when it exists, so non-default rigs stay in sync.
	FootBones = {FName(TEXT("toes_l")), FName(TEXT("toes_r")), FName(TEXT("hand_l")), FName(TEXT("hand_r"))};
	NeckBones = {FName(TEXT("neck_01")), FName(TEXT("neck_02")), FName(TEXT("neck_03"))};
	for (int32 Index = 1; Index <= 10; ++Index)
	{
		TailBones.Add(FName(*FString::Printf(TEXT("tail_%02d"), Index)));
	}
}

FPrimaryAssetId UNoctisSpeciesVisualData::GetPrimaryAssetId() const
{
	// One asset per species: the species id is the primary asset name, whatever the package is called.
	return FPrimaryAssetId(PrimaryAssetType, SpeciesId.IsNone() ? GetFName() : SpeciesId);
}

const FNoctisCallSound* UNoctisSpeciesVisualData::FindCall(FName CallId) const
{
	return CallSounds.FindByPredicate([CallId](const FNoctisCallSound& Call) { return Call.CallId == CallId; });
}

#if WITH_EDITOR
EDataValidationResult UNoctisSpeciesVisualData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (SpeciesId.IsNone())
	{
		Context.AddError(LOCTEXT("NoSpeciesId", "SpeciesId est vide : il doit correspondre à un fichier Data/Species/<env>/<id>.json."));
		Result = EDataValidationResult::Invalid;
	}
	if (SkeletalMesh.IsNull())
	{
		Context.AddError(LOCTEXT("NoMesh", "Aucun maillage squelettique."));
		Result = EDataValidationResult::Invalid;
	}
	if (MeshBodyLengthM <= 0.01f)
	{
		Context.AddError(LOCTEXT("BadLength", "MeshBodyLengthM doit être la longueur museau-queue du maillage à l'échelle 1 (m)."));
		Result = EDataValidationResult::Invalid;
	}
	if (FootBones.Num() < 2)
	{
		Context.AddError(LOCTEXT("FootBones", "FootBones doit contenir au moins les deux pieds postérieurs (ordre : HindLeft, HindRight, ForeLeft, ForeRight)."));
		Result = EDataValidationResult::Invalid;
	}
	if (!AnimClass.IsNull())
	{
		const UClass* Loaded = AnimClass.LoadSynchronous();
		if (Loaded && !Loaded->IsChildOf(UNoctisCreatureAnimInstance::StaticClass()))
		{
			Context.AddWarning(LOCTEXT("AnimClass", "AnimClass ne dérive pas de UNoctisCreatureAnimInstance : les entrées de simulation (IK des pieds, tissus mous) ne seront pas transmises."));
		}
	}
	if (const USkeletalMesh* Mesh = SkeletalMesh.LoadSynchronous())
	{
		const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
		auto CheckBone = [&](FName Bone, const TCHAR* Role) {
			if (!Bone.IsNone() && RefSkeleton.FindBoneIndex(Bone) == INDEX_NONE)
			{
				Context.AddError(FText::Format(LOCTEXT("MissingBone", "Os '{0}' ({1}) absent du squelette de {2}."), FText::FromName(Bone),
											   FText::FromString(Role), FText::FromString(Mesh->GetName())));
				Result = EDataValidationResult::Invalid;
			}
		};
		CheckBone(PelvisBone, TEXT("bassin"));
		CheckBone(HeadBone, TEXT("tête"));
		CheckBone(JawBone, TEXT("mâchoire"));
		for (const FName& Bone : FootBones)
		{
			CheckBone(Bone, TEXT("pied"));
		}
	}
	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
