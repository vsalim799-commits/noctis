#include "Creatures/NoctisCreatureAnimInstance.h"

#include "Creatures/NoctisSpeciesVisualData.h"

#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

UNoctisCreatureAnimInstance::UNoctisCreatureAnimInstance(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	// The simulation places the actor; authored root motion must neither move the mesh nor be applied to the actor.
	RootMotionMode = ERootMotionMode::IgnoreRootMotion;
}

void UNoctisCreatureAnimInstance::SetSpeciesVisual(UNoctisSpeciesVisualData* InVisual)
{
	SpeciesVisual = InVisual;
	ClipSequences.Reset();
	if (SpeciesVisual)
	{
		for (const TPair<FName, FNoctisAnimClip>& Clip : SpeciesVisual->Clips)
		{
			if (UAnimSequenceBase* Sequence = Clip.Value.Sequence.Get())
			{
				ClipSequences.Add(Clip.Key, Sequence);
			}
		}
	}
}

UAnimSequenceBase* UNoctisCreatureAnimInstance::GetClipSequence(FName ClipName) const
{
	const TObjectPtr<UAnimSequenceBase>* Found = ClipSequences.Find(ClipName);
	return Found ? Found->Get() : nullptr;
}

UAnimSequenceBase* UNoctisCreatureAnimInstance::GetLocomotionSequence() const
{
	return GetClipSequence(LocomotionClip);
}

void UNoctisCreatureAnimInstance::SetSimulationInputs(const FNoctisCreatureAnimInputs& InInputs)
{
	Pending = InInputs;
	bHasPending = true;
}

void UNoctisCreatureAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Noctis_AnimUpdate);
	Super::NativeUpdateAnimation(DeltaSeconds);
	if (!bHasPending)
	{
		return;
	}
	Inputs = Pending;
	bHasPending = false;

	const USkeletalMeshComponent* MeshComponent = GetSkelMeshComponent();
	if (MeshComponent)
	{
		const FTransform& ComponentToWorld = MeshComponent->GetComponentTransform();
		for (int32 Index = 0; Index < Inputs.FootTargets.Num() && Index < Inputs.FootTargetsComponent.Num(); ++Index)
		{
			Inputs.FootTargetsComponent[Index] = ComponentToWorld.InverseTransformPosition(Inputs.FootTargets[Index]);
		}
		// BodyBob is a world-space vertical offset (cm); express it in component space (mesh may be scaled).
		const float Scale = FMath::Max(UE_KINDA_SMALL_NUMBER, static_cast<float>(ComponentToWorld.GetScale3D().Z));
		PelvisOffset = FVector(0.0, 0.0, Inputs.BodyBob / Scale);
	}
	// Speed and StrideLength are both in cm (world): strides per second, then the playrate of the authored cycle
	// (cycle duration of the clip that plays this gait; the ABP's fallback when the species has no clip data).
	Speed = Inputs.Speed;
	Gait = Inputs.Gait;
	StrideFrequencyHz = Inputs.StrideLength > 1.0f ? Inputs.Speed / Inputs.StrideLength : 0.0f;
	LocomotionClip = SpeciesVisual ? SpeciesVisual->FindLocomotionClipName(Gait) : NAME_None;
	const FNoctisAnimClip* Clip = (SpeciesVisual && Gait != ENoctisGait::Stand) ? SpeciesVisual->Clips.Find(LocomotionClip) : nullptr;
	const float CycleSeconds = (Clip && Clip->CycleSeconds > 0.0f) ? Clip->CycleSeconds : AuthoredCycleSeconds;
	LocomotionPlayRate = StrideFrequencyHz > 0.0f ? FMath::Clamp(StrideFrequencyHz * CycleSeconds, 0.2f, 4.0f) : 1.0f;
	GaitPhase = Inputs.GaitPhase;
	bFootIK = Inputs.Lod == ENoctisLod::Full;
	FootIKAlpha = FMath::FInterpTo(FootIKAlpha, bFootIK ? 1.0f : 0.0f, DeltaSeconds, 6.0f);
}
