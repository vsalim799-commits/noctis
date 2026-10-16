#include "Creatures/NoctisCreatureManager.h"

#include "NoctisLog.h"
#include "NoctisSettings.h"

#include "Creatures/NoctisCreatureActor.h"
#include "Creatures/NoctisSpeciesVisualData.h"
#include "Sim/NoctisWorldSubsystem.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

static TAutoConsoleVariable<int32> CVarNoctisMaxActors(TEXT("noctis.Creatures.MaxActors"), 96,
	TEXT("Nombre maximal d'acteurs squelettiques de créatures (pool)."), ECVF_Scalability);

static TAutoConsoleVariable<float> CVarNoctisActorRadius(TEXT("noctis.Creatures.ActorRadiusM"), 450.0f,
	TEXT("Rayon (m) autour de la caméra où les animaux simulés reçoivent un corps animé complet."), ECVF_Scalability);

static TAutoConsoleVariable<float> CVarNoctisFarRadius(TEXT("noctis.Creatures.FarInstanceRadiusM"), 2500.0f,
	TEXT("Rayon (m) des imposteurs instanciés pour les animaux lointains."), ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarNoctisShowLod(TEXT("noctis.ShowLOD"), 0,
	TEXT("Affiche le niveau de détail de SIMULATION de chaque animal : 0 non, 1 points colorés (vert Full, jaune Near, orange Far, gris Regional), ")
	TEXT("2 avec identifiant, espèce et comportement à moins de 300 m."),
	ECVF_Cheat);

ANoctisCreatureManager::ANoctisCreatureManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	CreatureActorClass = ANoctisCreatureActor::StaticClass();
}

void ANoctisCreatureManager::BeginPlay()
{
	Super::BeginPlay();
	if (UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this))
	{
		FramePublishedHandle = Sub->OnFramePublished.AddUObject(this, &ANoctisCreatureManager::OnFramePublished);
		Sub->OnSimulationReady.AddDynamic(this, &ANoctisCreatureManager::OnSimulationReady);
	}
	LoadVisuals();
}

void ANoctisCreatureManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this))
	{
		Sub->OnFramePublished.Remove(FramePublishedHandle);
		Sub->OnSimulationReady.RemoveDynamic(this, &ANoctisCreatureManager::OnSimulationReady);
	}
	ReleaseAll();
	VisualDataHandle.Reset();
	VisualAssetsHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

void ANoctisCreatureManager::LoadVisuals()
{
	UAssetManager& AssetManager = UAssetManager::Get();
	VisualAssetIds.Reset();
	AssetManager.GetPrimaryAssetIdList(UNoctisSpeciesVisualData::PrimaryAssetType, VisualAssetIds);
	if (VisualAssetIds.Num() == 0)
	{
		UE_LOG(LogNoctis, Warning,
			   TEXT("Aucune fiche visuelle d'espèce (NoctisSpeciesVisual) : lancer Content/Python/import_creatures.py. Les animaux restent simulés mais invisibles."));
		bVisualsLoaded = true;
		return;
	}
	VisualDataHandle = AssetManager.LoadPrimaryAssets(VisualAssetIds, TArray<FName>(),
													  FStreamableDelegate::CreateUObject(this, &ANoctisCreatureManager::OnVisualDataLoaded));
	if (!VisualDataHandle.IsValid() || VisualDataHandle->HasLoadCompleted())
	{
		OnVisualDataLoaded();
	}
}

void ANoctisCreatureManager::OnVisualDataLoaded()
{
	if (bVisualDataProcessed)
	{
		return;
	}
	bVisualDataProcessed = true;
	UAssetManager& AssetManager = UAssetManager::Get();
	TArray<FSoftObjectPath> Paths;
	for (const FPrimaryAssetId& Id : VisualAssetIds)
	{
		UNoctisSpeciesVisualData* Visual = AssetManager.GetPrimaryAssetObject<UNoctisSpeciesVisualData>(Id);
		if (!Visual)
		{
			continue;
		}
		LoadedVisuals.Add(Visual);
		auto AddPath = [&Paths](const FSoftObjectPath& Path) {
			if (Path.IsValid())
			{
				Paths.AddUnique(Path);
			}
		};
		AddPath(Visual->SkeletalMesh.ToSoftObjectPath());
		AddPath(Visual->AnimClass.ToSoftObjectPath());
		AddPath(Visual->FarImpostorMesh.ToSoftObjectPath());
		AddPath(Visual->FootstepSound.ToSoftObjectPath());
		for (const TSoftObjectPtr<UMaterialInterface>& Material : Visual->MaterialOverrides)
		{
			AddPath(Material.ToSoftObjectPath());
		}
		for (const FNoctisCallSound& Call : Visual->CallSounds)
		{
			AddPath(Call.Sound.ToSoftObjectPath());
		}
		for (const TPair<FName, FNoctisAnimClip>& Clip : Visual->Clips)
		{
			AddPath(Clip.Value.Sequence.ToSoftObjectPath());
		}
	}
	if (Paths.Num() == 0)
	{
		OnVisualAssetsLoaded();
		return;
	}
	VisualAssetsHandle = AssetManager.GetStreamableManager().RequestAsyncLoad(
		Paths, FStreamableDelegate::CreateUObject(this, &ANoctisCreatureManager::OnVisualAssetsLoaded));
	if (!VisualAssetsHandle.IsValid() || VisualAssetsHandle->HasLoadCompleted())
	{
		OnVisualAssetsLoaded();
	}
}

void ANoctisCreatureManager::OnVisualAssetsLoaded()
{
	if (bVisualsLoaded)
	{
		return;
	}
	bVisualsLoaded = true;
	bVisualsMapped = false;
	UE_LOG(LogNoctis, Log, TEXT("%d fiches visuelles d'espèces chargées."), LoadedVisuals.Num());
}

void ANoctisCreatureManager::MapVisualsToSpecies()
{
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	if (!Sub || !Sub->IsSimulationReady())
	{
		return;
	}
	const TArray<FNoctisSpeciesInfo>& Species = Sub->GetSpecies();
	VisualsBySpecies.Reset();
	VisualsBySpecies.SetNum(Species.Num());
	for (UInstancedStaticMeshComponent* Impostor : ImpostorsBySpecies)
	{
		if (Impostor)
		{
			Impostor->DestroyComponent();
		}
	}
	ImpostorsBySpecies.Reset();
	ImpostorsBySpecies.SetNum(Species.Num());
	for (UNoctisSpeciesVisualData* Visual : LoadedVisuals)
	{
		const int32 SpeciesIndex = Visual ? Sub->FindSpeciesIndex(Visual->SpeciesId) : INDEX_NONE;
		if (SpeciesIndex == INDEX_NONE)
		{
			UE_LOG(LogNoctis, Warning, TEXT("Fiche visuelle %s : espèce '%s' inconnue des données scientifiques."), *GetNameSafe(Visual),
				   Visual ? *Visual->SpeciesId.ToString() : TEXT("?"));
			continue;
		}
		VisualsBySpecies[SpeciesIndex] = Visual;
		if (UStaticMesh* ImpostorMesh = Visual->FarImpostorMesh.Get())
		{
			UInstancedStaticMeshComponent* Impostor = NewObject<UInstancedStaticMeshComponent>(this);
			Impostor->SetStaticMesh(ImpostorMesh);
			Impostor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Impostor->SetCastShadow(true);
			Impostor->SetupAttachment(RootComponent);
			Impostor->RegisterComponent();
			ImpostorsBySpecies[SpeciesIndex] = Impostor;
		}
	}
	bVisualsMapped = true;
}

void ANoctisCreatureManager::OnSimulationReady()
{
	// A rebuilt world reuses ids from 1: never keep bodies bound to the previous valley.
	ReleaseAll();
	bVisualsMapped = false;
	PreviousIndexById.Reset();
	PreviousIndexSequence = 0;
}

FVector ANoctisCreatureManager::GetViewLocation() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			FVector Location;
			FRotator Rotation;
			PC->GetPlayerViewPoint(Location, Rotation);
			return Location;
		}
	}
	return GetActorLocation();
}

ANoctisCreatureActor* ANoctisCreatureManager::FindActor(uint32 CreatureId) const
{
	const TObjectPtr<ANoctisCreatureActor>* Found = Active.Find(CreatureId);
	return Found ? Found->Get() : nullptr;
}

ANoctisCreatureActor* ANoctisCreatureManager::Acquire(int32 SpeciesIndex)
{
	if (TArray<TObjectPtr<ANoctisCreatureActor>>* Free = Pool.Find(SpeciesIndex))
	{
		while (Free->Num() > 0)
		{
			ANoctisCreatureActor* Actor = Free->Pop(EAllowShrinking::No);
			if (IsValid(Actor))
			{
				return Actor;
			}
		}
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UClass* Class = CreatureActorClass ? CreatureActorClass.Get() : ANoctisCreatureActor::StaticClass();
	ANoctisCreatureActor* Actor = World->SpawnActor<ANoctisCreatureActor>(Class, FTransform::Identity, Params);
	if (Actor)
	{
		AllActors.Add(Actor);
	}
	return Actor;
}

void ANoctisCreatureManager::Release(uint32 CreatureId)
{
	TObjectPtr<ANoctisCreatureActor> Actor;
	if (Active.RemoveAndCopyValue(CreatureId, Actor) && IsValid(Actor))
	{
		const int32 SpeciesIndex = Actor->GetSpeciesIndex();
		Actor->Deactivate();
		Pool.FindOrAdd(SpeciesIndex).Add(Actor);
	}
}

void ANoctisCreatureManager::ReleaseAll()
{
	TArray<uint32> Ids;
	Active.GetKeys(Ids);
	for (const uint32 Id : Ids)
	{
		Release(Id);
	}
}

void ANoctisCreatureManager::Tick(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Noctis_CreatureManager);
	Super::Tick(DeltaSeconds);
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	const FNoctisFrameOutput* Front = Sub ? Sub->GetFrontFrame() : nullptr;
	const FNoctisFrameOutput* Previous = Sub ? Sub->GetPreviousFrame() : nullptr;
	if (!Front || !Previous)
	{
		return;
	}
	if (bVisualsLoaded && !bVisualsMapped)
	{
		MapVisualsToSpecies();
	}
	const FNoctisSpace& Space = Sub->GetSpace();
	const FVector View = GetViewLocation();
	Sub->SubmitFocus(View);

	if (Previous->Sequence != PreviousIndexSequence)
	{
		PreviousIndexById.Reset();
		PreviousIndexById.Reserve(static_cast<int32>(Previous->Snapshot.creatures.size()));
		for (int32 Index = 0; Index < static_cast<int32>(Previous->Snapshot.creatures.size()); ++Index)
		{
			PreviousIndexById.Add(Previous->Snapshot.creatures[static_cast<size_t>(Index)].id, Index);
		}
		PreviousIndexSequence = Previous->Sequence;
	}

	const float ActorRadiusCm = FMath::Max(10.0f, CVarNoctisActorRadius.GetValueOnGameThread()) * 100.0f;
	const float FarRadiusCm = FMath::Max(ActorRadiusCm, CVarNoctisFarRadius.GetValueOnGameThread() * 100.0f);
	const int32 MaxActors = FMath::Max(0, CVarNoctisMaxActors.GetValueOnGameThread());

	struct FCandidate
	{
		int32 Index;
		double DistSq;
	};
	TArray<FCandidate> Candidates;
	if (bVisualsMapped)
	{
		const std::vector<noctis::CreatureRenderState>& Creatures = Front->Snapshot.creatures;
		Candidates.Reserve(FMath::Min<int32>(static_cast<int32>(Creatures.size()), MaxActors * 2 + 16));
		for (int32 Index = 0; Index < static_cast<int32>(Creatures.size()); ++Index)
		{
			const noctis::CreatureRenderState& C = Creatures[static_cast<size_t>(Index)];
			if (C.lod == noctis::LodLevel::Regional || !VisualsBySpecies.IsValidIndex(C.speciesIndex))
			{
				continue;
			}
			const UNoctisSpeciesVisualData* Visual = VisualsBySpecies[C.speciesIndex];
			if (!Visual || !Visual->SkeletalMesh.Get())
			{
				continue;
			}
			const double DistSq = FVector::DistSquared2D(Space.ToUE(C.position), View);
			const double SpeciesRadius = FMath::Min<double>(ActorRadiusCm, Visual->MaxActorDistanceM * 100.0);
			const double Limit = Active.Contains(C.id) ? SpeciesRadius * ReleaseHysteresis : SpeciesRadius;
			if (DistSq <= Limit * Limit)
			{
				Candidates.Add({Index, DistSq});
			}
		}
		Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.DistSq < B.DistSq; });
		if (Candidates.Num() > MaxActors)
		{
			Candidates.SetNum(MaxActors);
		}
	}

	// Release bodies no longer wanted (out of range, dead, over budget).
	TSet<uint32> Wanted;
	Wanted.Reserve(Candidates.Num());
	for (const FCandidate& Candidate : Candidates)
	{
		Wanted.Add(Front->Snapshot.creatures[static_cast<size_t>(Candidate.Index)].id);
	}
	TArray<uint32> ToRelease;
	for (const TPair<uint32, TObjectPtr<ANoctisCreatureActor>>& Pair : Active)
	{
		if (!Wanted.Contains(Pair.Key))
		{
			ToRelease.Add(Pair.Key);
		}
	}
	for (const uint32 Id : ToRelease)
	{
		Release(Id);
	}

	// Present the selected individuals.
	const float Alpha = Sub->GetInterpolationAlpha();
	for (const FCandidate& Candidate : Candidates)
	{
		const noctis::CreatureRenderState& C = Front->Snapshot.creatures[static_cast<size_t>(Candidate.Index)];
		ANoctisCreatureActor* Actor = FindActor(C.id);
		if (!Actor)
		{
			const FNoctisSpeciesInfo* Info = Sub->GetSpeciesInfo(C.speciesIndex);
			Actor = Info ? Acquire(C.speciesIndex) : nullptr;
			if (!Actor)
			{
				continue;
			}
			Actor->ActivateFor(C.id, *Info, VisualsBySpecies[C.speciesIndex], this);
			Active.Add(C.id, Actor);
		}
		const int32* PreviousIndex = PreviousIndexById.Find(C.id);
		const noctis::CreatureRenderState* P = PreviousIndex ? &Previous->Snapshot.creatures[static_cast<size_t>(*PreviousIndex)] : nullptr;
		Actor->ApplyState(P, C, Alpha, Space, DeltaSeconds);
	}

	ImpostorTimer += DeltaSeconds;
	if (ImpostorTimer >= 0.25f && bVisualsMapped)
	{
		ImpostorTimer = 0.0f;
		UpdateImpostors(*Front, View, ActorRadiusCm, FarRadiusCm);
	}
	if (CVarNoctisShowLod.GetValueOnGameThread() > 0)
	{
		DrawLodDebug(*Front, View);
	}
}

void ANoctisCreatureManager::UpdateImpostors(const FNoctisFrameOutput& Front, const FVector& View, float ActorRadiusCm, float FarRadiusCm)
{
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	if (!Sub)
	{
		return;
	}
	const FNoctisSpace& Space = Sub->GetSpace();
	TArray<TArray<FTransform>> PerSpecies;
	PerSpecies.SetNum(ImpostorsBySpecies.Num());
	for (const noctis::CreatureRenderState& C : Front.Snapshot.creatures)
	{
		if (!ImpostorsBySpecies.IsValidIndex(C.speciesIndex) || !ImpostorsBySpecies[C.speciesIndex] || C.lod == noctis::LodLevel::Regional ||
			Active.Contains(C.id))
		{
			continue;
		}
		const FVector Location = Space.ToUE(C.position);
		const double DistSq = FVector::DistSquared2D(Location, View);
		if (DistSq > double(FarRadiusCm) * double(FarRadiusCm))
		{
			continue;
		}
		const FNoctisSpeciesInfo* Info = Sub->GetSpeciesInfo(C.speciesIndex);
		const UNoctisSpeciesVisualData* Visual = VisualsBySpecies[C.speciesIndex];
		const float Scale = (Info && Visual) ? Info->AdultLengthM * C.scale / FMath::Max(0.01f, Visual->MeshBodyLengthM) : 1.0f;
		// Same composition as the full actor: body attitude, then the mesh's local yaw offset (not a rotator sum, which
		// would apply the slope pitch about the wrong axis when the offset is not zero).
		const FQuat Rotation = FNoctisSpace::AttitudeToUE(C.heading, C.pitch, C.roll).Quaternion() *
							   FRotator(0.0, Visual ? double(Visual->MeshYawOffsetDeg) : 0.0, 0.0).Quaternion();
		PerSpecies[C.speciesIndex].Add(FTransform(Rotation, Location, FVector(Scale)));
	}
	(void)ActorRadiusCm;
	for (int32 SpeciesIndex = 0; SpeciesIndex < ImpostorsBySpecies.Num(); ++SpeciesIndex)
	{
		if (UInstancedStaticMeshComponent* Impostor = ImpostorsBySpecies[SpeciesIndex])
		{
			Impostor->ClearInstances();
			if (PerSpecies[SpeciesIndex].Num() > 0)
			{
				Impostor->AddInstances(PerSpecies[SpeciesIndex], /*bShouldReturnIndices*/ false, /*bWorldSpace*/ true);
			}
		}
	}
}

void ANoctisCreatureManager::DrawLodDebug(const FNoctisFrameOutput& Front, const FVector& View) const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	if (!World || !Sub)
	{
		return;
	}
	const bool bLabels = CVarNoctisShowLod.GetValueOnGameThread() >= 2;
	const FNoctisSpace& Space = Sub->GetSpace();
	for (const noctis::CreatureRenderState& C : Front.Snapshot.creatures)
	{
		const FVector Location = Space.ToUE(C.position) + FVector(0.0, 0.0, 200.0);
		const double Dist = FVector::Dist2D(Location, View);
		if (Dist > 300000.0)
		{
			continue;
		}
		FColor Color = FColor(128, 128, 128);
		switch (C.lod)
		{
		case noctis::LodLevel::Full: Color = FColor::Green; break;
		case noctis::LodLevel::Near: Color = FColor::Yellow; break;
		case noctis::LodLevel::Far: Color = FColor::Orange; break;
		case noctis::LodLevel::Regional: break;
		}
		DrawDebugPoint(World, Location, Dist < 50000.0 ? 12.0f : 6.0f, Color, false, -1.0f, SDPG_Foreground);
		if (bLabels && Dist < 30000.0)
		{
			const FNoctisSpeciesInfo* Info = Sub->GetSpeciesInfo(C.speciesIndex);
			const FString Label = FString::Printf(TEXT("#%u %s\n%s / %s"), noctis::EntityId(C.id).serial(), Info ? *Info->Id.ToString() : TEXT("?"),
												  UTF8_TO_TCHAR(noctis::behaviorKey(C.behavior)), UTF8_TO_TCHAR(noctis::lodLabel(C.lod)));
			DrawDebugString(World, Location + FVector(0.0, 0.0, 100.0), Label, nullptr, Color, -1.0f, true, 1.0f);
		}
	}
#endif
}

void ANoctisCreatureManager::OnFramePublished(const FNoctisFrameOutput& Front)
{
	const float MaxFootstepCm = UNoctisSettings::Get().FootstepMaxDistanceM * 100.0f;
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	if (!Sub)
	{
		return;
	}
	const FVector View = GetViewLocation();
	const std::vector<noctis::SoundSource>& Sounds = Front.Snapshot.newSounds;
	for (int32 Index = 0; Index < static_cast<int32>(Sounds.size()); ++Index)
	{
		const noctis::SoundSource& Sound = Sounds[static_cast<size_t>(Index)];
		if (Sound.emitter.kind() != noctis::EntityKind::Creature)
		{
			continue;
		}
		ANoctisCreatureActor* Actor = FindActor(Sound.emitter.value);
		if (!Actor)
		{
			continue;
		}
		if (Sound.kind == noctis::SoundKind::Footfall)
		{
			if (FVector::Dist(Actor->GetActorLocation(), View) <= MaxFootstepCm && Front.SoundExtras.IsValidIndex(Index))
			{
				const FNoctisSpeciesInfo* Info = Sub->GetSpeciesInfo(Actor->GetSpeciesIndex());
				const FNoctisSoundExtra& Extra = Front.SoundExtras[Index];
				Actor->PlayFootfall(Sound.levelDb, Info ? Info->AdultMassKg : 100.0f, Extra.Substrate, Extra.bInWater);
			}
		}
		else if (Sound.kind == noctis::SoundKind::Vocalization)
		{
			Actor->BeginVocalization(Sound.durationS);
		}
	}
}
