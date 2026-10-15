#include "Environment/NoctisVegetationInteraction.h"

#include "NoctisLog.h"
#include "NoctisSettings.h"
#include "NoctisSpace.h"
#include "Sim/NoctisWorldSubsystem.h"

#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "PCGComponent.h"
#include "RHI.h"

static TAutoConsoleVariable<int32> CVarNoctisVegetationInteractors(TEXT("noctis.Vegetation.Interactors"), 16,
	TEXT("Nombre d'animaux (les plus proches de la caméra) transmis aux matériaux de végétation pour la courbure (max 32)."), ECVF_Scalability);

ANoctisVegetationInteraction::ANoctisVegetationInteraction()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ANoctisVegetationInteraction::BeginPlay()
{
	Super::BeginPlay();
	WeatherParameters = UNoctisSettings::Get().WeatherParameters.LoadSynchronous();
	if (!TrailMaskTarget)
	{
		TrailMaskTarget = LoadObject<UTextureRenderTarget2D>(nullptr, TEXT("/Game/Environment/Vegetation/RT_NoctisTrails.RT_NoctisTrails"));
	}
}

void ANoctisVegetationInteraction::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	const FNoctisFrameOutput* Front = Sub ? Sub->GetFrontFrame() : nullptr;
	if (!Front)
	{
		return;
	}
	if (Front->Aux.IsValid() && Front->Aux->Revision != LastAuxRevision)
	{
		LastAuxRevision = Front->Aux->Revision;
		UpdateMask(*Front->Aux);
	}
	UpdateInteractors();

	PCGTimer += DeltaSeconds;
	if (bPCGDirty && PCGTimer >= PCGRefreshSeconds)
	{
		PCGTimer = 0.0f;
		bPCGDirty = false;
		for (UPCGComponent* Component : PCGComponentsToRefresh)
		{
			if (Component)
			{
				// VERIFY: UPCGComponent::GenerateLocal(bool bForce) (UE 5.2+).
				Component->GenerateLocal(/*bForce*/ true);
			}
		}
	}
}

void ANoctisVegetationInteraction::UpdateMask(const FNoctisAuxData& Aux)
{
	const int32 W = Aux.VegW;
	const int32 H = Aux.VegH;
	if (W <= 0 || H <= 0 || Aux.Trail.Num() != W * H)
	{
		return;
	}
	if (!MaskTexture || MaskTexture->GetSizeX() != W || MaskTexture->GetSizeY() != H)
	{
		MaskTexture = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		MaskTexture->Filter = TF_Bilinear;
		MaskTexture->AddressX = TA_Clamp;
		MaskTexture->AddressY = TA_Clamp;
		MaskTexture->SRGB = false;
		MaskTexture->UpdateResource();
	}
	// Texture row 0 = northern edge (most negative Unreal Y), matching the terrain export convention.
	const int32 Bytes = W * H * 4;
	uint8* Pixels = new uint8[Bytes];
	for (int32 Row = 0; Row < H; ++Row)
	{
		const int32 SourceRow = H - 1 - Row;
		for (int32 X = 0; X < W; ++X)
		{
			const int32 K = SourceRow * W + X;
			uint8* Pixel = Pixels + (Row * W + X) * 4;
			Pixel[0] = Aux.Burnt[K];     // B
			Pixel[1] = Aux.Trampling[K]; // G
			Pixel[2] = Aux.Trail[K];     // R
			Pixel[3] = 255;              // A
		}
	}
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, static_cast<uint32>(W), static_cast<uint32>(H));
	MaskTexture->UpdateTextureRegions(0, 1, Region, static_cast<uint32>(W * 4), 4, Pixels,
									  [](uint8* SrcData, const FUpdateTextureRegion2D* Regions) {
										  delete[] SrcData;
										  delete Regions;
									  });

	if (TrailMaskTarget)
	{
		UCanvas* Canvas = nullptr;
		FVector2D Size;
		FDrawToRenderTargetContext Context;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, TrailMaskTarget, Canvas, Size, Context);
		if (Canvas)
		{
			Canvas->K2_DrawTexture(MaskTexture, FVector2D::ZeroVector, Size, FVector2D::ZeroVector, FVector2D::UnitVector, FLinearColor::White, BLEND_Opaque);
		}
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
	}

	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	if (!Sub)
	{
		return;
	}
	const FNoctisSpace& Space = Sub->GetSpace();
	const float SizeM = Aux.VegCellM;
	const FVector MinCorner = Space.ToUE(noctis::Vec2(Aux.VegOrigin.x, Aux.VegOrigin.y + float(H) * SizeM), 0.0); // north-west corner
	const FVector MaxCorner = Space.ToUE(noctis::Vec2(Aux.VegOrigin.x + float(W) * SizeM, Aux.VegOrigin.y), 0.0); // south-east corner
	if (WeatherParameters && GetWorld())
	{
		if (UMaterialParameterCollectionInstance* Instance = GetWorld()->GetParameterCollectionInstance(WeatherParameters))
		{
			Instance->SetVectorParameterValue(TEXT("TrailMaskRect"),
											  FLinearColor(float(MinCorner.X), float(MinCorner.Y), float(MaxCorner.X - MinCorner.X), float(MaxCorner.Y - MinCorner.Y)));
		}
	}
	const FBox Bounds(FVector(MinCorner.X, MinCorner.Y, -1e6), FVector(MaxCorner.X, MaxCorner.Y, 1e6));
	bPCGDirty = true;
	OnVegetationStateUpdated(Bounds);
}

void ANoctisVegetationInteraction::UpdateInteractors()
{
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	const FNoctisFrameOutput* Front = Sub ? Sub->GetFrontFrame() : nullptr;
	UWorld* World = GetWorld();
	if (!Front || !World || !WeatherParameters)
	{
		return;
	}
	UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(WeatherParameters);
	if (!Instance)
	{
		return;
	}
	FVector View = GetActorLocation();
	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		FRotator Rotation;
		PC->GetPlayerViewPoint(View, Rotation);
	}
	const int32 Count = FMath::Clamp(CVarNoctisVegetationInteractors.GetValueOnGameThread(), 0, 32);
	const FNoctisSpace& Space = Sub->GetSpace();

	struct FNear
	{
		FVector Location;
		float RadiusCm;
		double DistSq;
	};
	TArray<FNear> Nearest;
	Nearest.Reserve(64);
	for (const noctis::CreatureRenderState& C : Front->Snapshot.creatures)
	{
		if (C.lod == noctis::LodLevel::Regional)
		{
			continue;
		}
		const FVector Location = Space.ToUE(C.position);
		const double DistSq = FVector::DistSquared(Location, View);
		if (DistSq > 400.0 * 100.0 * 400.0 * 100.0)
		{
			continue;
		}
		const FNoctisSpeciesInfo* Info = Sub->GetSpeciesInfo(C.speciesIndex);
		const float LengthM = Info ? Info->AdultLengthM * C.scale : 1.0f;
		const float HipM = Info ? Info->HipHeightM * C.scale : 0.5f;
		Nearest.Add({Location + FVector(0.0, 0.0, HipM * 50.0), LengthM * 0.12f * 100.0f + 30.0f, DistSq});
	}
	Nearest.Sort([](const FNear& A, const FNear& B) { return A.DistSq < B.DistSq; });
	static const TArray<FName> Names = [] {
		TArray<FName> Result;
		for (int32 Index = 0; Index < 32; ++Index)
		{
			Result.Add(FName(*FString::Printf(TEXT("Interactor%d"), Index)));
		}
		return Result;
	}();
	for (int32 Index = 0; Index < Names.Num(); ++Index)
	{
		if (Index < Count && Index < Nearest.Num())
		{
			const FNear& N = Nearest[Index];
			Instance->SetVectorParameterValue(Names[Index], FLinearColor(float(N.Location.X), float(N.Location.Y), float(N.Location.Z), N.RadiusCm));
		}
		else
		{
			Instance->SetVectorParameterValue(Names[Index], FLinearColor(0.0f, 0.0f, -1e7f, 0.0f));
		}
	}
	Instance->SetScalarParameterValue(TEXT("InteractorCount"), static_cast<float>(FMath::Min(Count, Nearest.Num())));
}
