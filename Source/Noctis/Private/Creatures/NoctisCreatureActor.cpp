#include "Creatures/NoctisCreatureActor.h"

#include "Creatures/NoctisCreatureAnimInstance.h"
#include "Creatures/NoctisSpeciesVisualData.h"

#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"

namespace NoctisCreatureActorImpl
{
float Frac(float V)
{
	return V - FMath::FloorToFloat(V);
}

// Custom primitive data slots read by the creature master material (M_NoctisCreature, created by import_creatures.py).
constexpr int32 CpdWetness = 0;
constexpr int32 CpdMud = 1;
constexpr int32 CpdCondition = 2;
constexpr int32 CpdMarkingSeed = 3;
constexpr int32 CpdScarCount = 4;
} // namespace NoctisCreatureActorImpl

ANoctisCreatureActor::ANoctisCreatureActor()
{
	// The manager drives the actor; only the skeletal mesh (animation) ticks.
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionProfileName(TEXT("NoctisCreature"));
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	Mesh->bEnableUpdateRateOptimizations = true;
	Mesh->SetCastShadow(true);

	FootstepAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("Footsteps"));
	FootstepAudio->SetupAttachment(Root);
	FootstepAudio->bAutoActivate = false;

	SetActorHiddenInGame(true);
}

void ANoctisCreatureActor::ActivateFor(uint32 InCreatureId, const FNoctisSpeciesInfo& InSpecies, UNoctisSpeciesVisualData* InVisual, AActor* Driver)
{
	CreatureId = InCreatureId;
	Species = InSpecies;
	VocalRemainingS = 0.0f;
	VocalDurationS = 0.0f;
	bBoundEventSent = false;
	LastWetness = LastMud = LastCondition = -1.0f;

	if (InVisual)
	{
		Visual = InVisual;
		USkeletalMesh* MeshAsset = Visual->SkeletalMesh.Get();
		if (MeshAsset && Mesh->GetSkeletalMeshAsset() != MeshAsset)
		{
			// VERIFY: USkinnedMeshComponent::SetSkinnedAssetAndUpdate (UE 5.1+; replaces SetSkeletalMesh).
			Mesh->SetSkinnedAssetAndUpdate(MeshAsset, true);
		}
		UClass* AnimClass = Visual->AnimClass.Get();
		if (AnimClass && AnimClass != CurrentAnimClass)
		{
			Mesh->SetAnimInstanceClass(AnimClass);
			CurrentAnimClass = AnimClass;
		}
		if (UNoctisCreatureAnimInstance* Anim = Cast<UNoctisCreatureAnimInstance>(Mesh->GetAnimInstance()))
		{
			Anim->SetSpeciesVisual(Visual);
		}
		for (int32 Slot = 0; Slot < Visual->MaterialOverrides.Num(); ++Slot)
		{
			if (UMaterialInterface* Material = Visual->MaterialOverrides[Slot].Get())
			{
				Mesh->SetMaterial(Slot, Material);
			}
		}
		Mesh->SetRelativeRotation(FRotator(0.0, Visual->MeshYawOffsetDeg, 0.0));
		Mesh->bEnableUpdateRateOptimizations = Visual->bUpdateRateOptimizations;
		USoundBase* Footsteps = Visual->FootstepSound.Get();
		if (FootstepAudio->Sound != Footsteps)
		{
			FootstepAudio->SetSound(Footsteps);
		}
	}
	if (Driver && !bTickPrerequisiteSet)
	{
		// The manager writes the animation inputs in its tick; the mesh must evaluate after it.
		Mesh->AddTickPrerequisiteActor(Driver);
		bTickPrerequisiteSet = true;
	}
	SetActorHiddenInGame(false);
	Mesh->SetComponentTickEnabled(true);
	Mesh->SetCustomPrimitiveDataFloat(NoctisCreatureActorImpl::CpdMarkingSeed, 0.0f);
}

void ANoctisCreatureActor::Deactivate()
{
	CreatureId = 0;
	SetActorHiddenInGame(true);
	Mesh->SetComponentTickEnabled(false);
	if (FootstepAudio->IsPlaying())
	{
		FootstepAudio->Stop();
	}
}

void ANoctisCreatureActor::ApplyState(const noctis::CreatureRenderState* Previous, const noctis::CreatureRenderState& Current, float Alpha,
									  const FNoctisSpace& Space, float DeltaSeconds)
{
	using namespace NoctisCreatureActorImpl;
	const noctis::CreatureRenderState* P = (Previous && Previous->id == Current.id) ? Previous : nullptr;
	const float T = FMath::Clamp(Alpha, 0.0f, 1.0f);

	// ---- Transform -------------------------------------------------------------------------------
	const noctis::Vec3 Position = P ? noctis::lerp(P->position, Current.position, T) : Current.position;
	const float Heading = P ? P->heading + noctis::angleDelta(P->heading, Current.heading) * T : Current.heading;
	const float Pitch = P ? FMath::Lerp(P->pitch, Current.pitch, T) : Current.pitch;
	const float Roll = P ? FMath::Lerp(P->roll, Current.roll, T) : Current.roll;
	SetActorLocationAndRotation(Space.ToUE(Position), FNoctisSpace::AttitudeToUE(Heading, Pitch, Roll), false, nullptr, ETeleportType::TeleportPhysics);

	const float LengthM = FMath::Max(0.01f, Species.AdultLengthM * Current.scale);
	const float MeshLength = Visual ? FMath::Max(0.01f, Visual->MeshBodyLengthM) : 1.0f;
	const float Scale = LengthM / MeshLength;
	if (!FMath::IsNearlyEqual(static_cast<float>(Mesh->GetRelativeScale3D().X), Scale, 1e-3f))
	{
		Mesh->SetRelativeScale3D(FVector(Scale));
	}

	// ---- Animation inputs ------------------------------------------------------------------------
	FNoctisCreatureAnimInputs& A = AnimInputs;
	A.Speed = (P ? FMath::Lerp(P->speed, Current.speed, T) : Current.speed) * 100.0f;
	A.Gait = static_cast<ENoctisGait>(Current.gait);
	if (P)
	{
		float Delta = Current.gaitPhase - P->gaitPhase;
		if (Delta < -0.5f)
		{
			Delta += 1.0f; // phase wrapped between the two snapshots
		}
		A.GaitPhase = Frac(P->gaitPhase + Delta * T);
	}
	else
	{
		A.GaitPhase = Current.gaitPhase;
	}
	A.StrideLength = Current.strideLengthM * 100.0f;
	A.DutyFactor = Current.dutyFactor;
	A.BodyBob = (P ? FMath::Lerp(P->bodyBob, Current.bodyBob, T) : Current.bodyBob) * 100.0f;
	A.BodyPitch = FMath::RadiansToDegrees(Pitch);
	A.BodyRoll = FMath::RadiansToDegrees(Roll);
	A.Posture = static_cast<ENoctisPosture>(Current.posture);
	A.Behavior = static_cast<ENoctisBehavior>(Current.behavior);
	A.Lod = static_cast<ENoctisLod>(Current.lod);

	for (int32 Slot = 0; Slot < 4; ++Slot)
	{
		A.FootContact[Slot] = 0.0f;
	}
	const int32 FeetCount = FMath::Min<int32>(Current.footCount, 4);
	const bool bSameFeet = P && P->footCount == Current.footCount;
	for (int32 Foot = 0; Foot < FeetCount; ++Foot)
	{
		const int32 Slot = static_cast<int32>(Species.FootSlots[Foot]);
		const noctis::Vec3 Target = bSameFeet ? noctis::lerp(P->footTargets[Foot], Current.footTargets[Foot], T) : Current.footTargets[Foot];
		A.FootTargets[Slot] = Space.ToUE(Target);
		A.FootContact[Slot] = Current.footContact[Foot] ? 1.0f : 0.0f;
		A.Muscle[Slot] = Current.muscle[Foot];
	}

	// Soft tissue: belly in metres -> cm, neck and tail in radians -> degrees (lateral sign flips with the Y mirror).
	A.BellyVertical = Current.softTissue[static_cast<size_t>(noctis::SoftTissueNode::BellyVertical)] * 100.0f;
	A.BellyLateral = -Current.softTissue[static_cast<size_t>(noctis::SoftTissueNode::BellyLateral)] * 100.0f;
	A.NeckVertical = FMath::RadiansToDegrees(Current.softTissue[static_cast<size_t>(noctis::SoftTissueNode::NeckVertical)]);
	A.NeckLateral = -FMath::RadiansToDegrees(Current.softTissue[static_cast<size_t>(noctis::SoftTissueNode::NeckLateral)]);
	A.TailVertical = FMath::RadiansToDegrees(Current.softTissue[static_cast<size_t>(noctis::SoftTissueNode::TailVertical)]);
	A.TailLateral = -FMath::RadiansToDegrees(Current.softTissue[static_cast<size_t>(noctis::SoftTissueNode::TailLateral)]);
	A.Breath = Current.breath;

	A.bHasLookTarget = Current.hasLookTarget;
	if (Current.hasLookTarget)
	{
		const double EyeHeightM = double(Species.HipHeightM) * double(Current.scale) * 1.3;
		A.LookTarget = Space.ToUE(Current.lookTarget, double(Position.z) + EyeHeightM);
	}

	// Voice: envelope over the call duration (sin^0.5 attack/decay); closed-mouth species inflate the throat instead.
	if (VocalRemainingS > 0.0f)
	{
		VocalRemainingS = FMath::Max(0.0f, VocalRemainingS - DeltaSeconds);
	}
	const float Progress = VocalDurationS > 0.0f ? 1.0f - VocalRemainingS / VocalDurationS : 1.0f;
	const float Envelope = VocalRemainingS > 0.0f ? FMath::Sqrt(FMath::Max(0.0f, FMath::Sin(UE_PI * Progress))) : 0.0f;
	A.bVocalizing = Current.vocalizing || VocalRemainingS > 0.0f;
	A.VocalEnvelope = Envelope;
	A.JawOpen = Species.bClosedMouthCalls ? 0.0f : Envelope;
	A.ThroatInflation = Species.bClosedMouthCalls ? Envelope : 0.2f * Envelope;

	A.Limp = Current.limp;
	A.LimpSide = Current.limpSide; // anatomical side (-1 left, +1 right): frame independent
	A.Condition = Current.condition;
	A.Wetness = Current.wetness;
	A.Mud = Current.mud;

	if (UNoctisCreatureAnimInstance* Anim = Cast<UNoctisCreatureAnimInstance>(Mesh->GetAnimInstance()))
	{
		Anim->SetSimulationInputs(A);
	}

	// Material parameters through custom primitive data (no dynamic material instances per animal).
	if (FMath::Abs(A.Wetness - LastWetness) > 0.02f)
	{
		Mesh->SetCustomPrimitiveDataFloat(CpdWetness, A.Wetness);
		LastWetness = A.Wetness;
	}
	if (FMath::Abs(A.Mud - LastMud) > 0.02f)
	{
		Mesh->SetCustomPrimitiveDataFloat(CpdMud, A.Mud);
		LastMud = A.Mud;
	}
	if (FMath::Abs(A.Condition - LastCondition) > 0.02f)
	{
		Mesh->SetCustomPrimitiveDataFloat(CpdCondition, A.Condition);
		LastCondition = A.Condition;
	}
	if (!bBoundEventSent)
	{
		bBoundEventSent = true;
		Mesh->SetCustomPrimitiveDataFloat(CpdMarkingSeed, Current.markingSeed);
		Mesh->SetCustomPrimitiveDataFloat(CpdScarCount, static_cast<float>(Current.scarCount));
		OnBoundToIndividual(static_cast<int64>(CreatureId), Species.Id, Current.markingSeed, Current.scarCount);
	}
}

void ANoctisCreatureActor::PlayFootfall(float LevelDb, float MassKg, uint8 Substrate, bool bInWater)
{
	if (!FootstepAudio || !FootstepAudio->Sound)
	{
		return;
	}
	if (!FootstepAudio->IsPlaying())
	{
		FootstepAudio->Play();
	}
	// MetaSound inputs (see Content/Python/import_audio.py, MS_Footstep_* interface).
	FootstepAudio->SetFloatParameter(TEXT("Mass"), MassKg);
	FootstepAudio->SetFloatParameter(TEXT("LevelDb"), LevelDb);
	FootstepAudio->SetIntParameter(TEXT("Substrate"), static_cast<int32>(Substrate));
	FootstepAudio->SetBoolParameter(TEXT("InWater"), bInWater);
	FootstepAudio->SetTriggerParameter(TEXT("Footfall"));
}

void ANoctisCreatureActor::BeginVocalization(float DurationS)
{
	VocalDurationS = FMath::Max(0.05f, DurationS);
	VocalRemainingS = VocalDurationS;
}
