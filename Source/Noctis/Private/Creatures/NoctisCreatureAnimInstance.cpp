#include "Creatures/NoctisCreatureAnimInstance.h"

#include "Components/SkeletalMeshComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

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
	// Speed and StrideLength are both in cm (world): strides per second, then the playrate of the authored cycle.
	StrideFrequencyHz = Inputs.StrideLength > 1.0f ? Inputs.Speed / Inputs.StrideLength : 0.0f;
	LocomotionPlayRate = StrideFrequencyHz > 0.0f ? FMath::Clamp(StrideFrequencyHz * AuthoredCycleSeconds, 0.2f, 4.0f) : 1.0f;
	Speed = Inputs.Speed;
	Gait = Inputs.Gait;
	GaitPhase = Inputs.GaitPhase;
	bFootIK = Inputs.Lod == ENoctisLod::Full;
	FootIKAlpha = FMath::FInterpTo(FootIKAlpha, bFootIK ? 1.0f : 0.0f, DeltaSeconds, 6.0f);
}
