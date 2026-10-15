#include "Environment/NoctisWaterActor.h"

#include "NoctisSpace.h"
#include "Sim/NoctisWorldSubsystem.h"

#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ProceduralMeshComponent.h"

namespace NoctisWaterImpl
{
struct FMeshBuffers
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	int32 AddVertex(const FVector& P, const FVector2D& UV, const FLinearColor& Color)
	{
		Normals.Add(FVector::UpVector);
		UV0.Add(UV);
		Colors.Add(Color);
		Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
		return Vertices.Add(P);
	}

	// Front faces must face up. Unreal's front-face normal is (V1 - V0) x (V2 - V0); swap when it points down.
	void AddTriangle(int32 A, int32 B, int32 C)
	{
		const FVector N = FVector::CrossProduct(Vertices[B] - Vertices[A], Vertices[C] - Vertices[A]);
		Triangles.Add(A);
		Triangles.Add(N.Z >= 0.0 ? B : C);
		Triangles.Add(N.Z >= 0.0 ? C : B);
	}
};

bool SampleSurface(const FNoctisAuxData& Aux, const noctis::Vec2& P, float& OutSurfaceZ)
{
	if (Aux.WaterW <= 0 || Aux.WaterH <= 0 || Aux.WaterCellM <= 0.0f)
	{
		return false;
	}
	const int32 X = FMath::FloorToInt32((P.x - Aux.WaterOrigin.x) / Aux.WaterCellM);
	const int32 Y = FMath::FloorToInt32((P.y - Aux.WaterOrigin.y) / Aux.WaterCellM);
	if (X < 0 || Y < 0 || X >= Aux.WaterW || Y >= Aux.WaterH)
	{
		return false;
	}
	const int32 K = Y * Aux.WaterW + X;
	if (Aux.WaterSurface[K] <= Aux.GroundHeight[K])
	{
		return false;
	}
	OutSurfaceZ = Aux.WaterSurface[K];
	return true;
}

FLinearColor FlowColor(const FVector2f& FlowCore, float DepthM)
{
	// Flow in Unreal axes (Y mirrored), packed to [0, 1] over +-2 m/s.
	const float FX = FMath::Clamp(0.5f + FlowCore.X / 4.0f, 0.0f, 1.0f);
	const float FY = FMath::Clamp(0.5f - FlowCore.Y / 4.0f, 0.0f, 1.0f);
	return FLinearColor(FX, FY, FMath::Clamp(DepthM / 5.0f, 0.0f, 1.0f), 1.0f);
}
} // namespace NoctisWaterImpl

ANoctisWaterActor::ANoctisWaterActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.0f;
	WaterMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WaterMesh"));
	RootComponent = WaterMesh;
	WaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WaterMesh->SetCastShadow(false);
	WaterMesh->bUseAsyncCooking = true;
}

void ANoctisWaterActor::BeginPlay()
{
	Super::BeginPlay();
	if (!WaterMaterial)
	{
		WaterMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Environment/Water/M_NoctisWater.M_NoctisWater"));
	}
}

void ANoctisWaterActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	const FNoctisFrameOutput* Front = Sub ? Sub->GetFrontFrame() : nullptr;
	if (!Front)
	{
		return;
	}
	const FNoctisSpace& Space = Sub->GetSpace();
	if (Front->Aux.IsValid() && Front->Aux->Revision != LastAuxRevision)
	{
		const FNoctisAuxData& Aux = *Front->Aux;
		LastAuxRevision = Aux.Revision;
		const FVector2D Focus(Aux.Focus.x, Aux.Focus.y);
		if (FMath::Abs(Aux.RiverStageM - LastRiverStage) > StageRebuildM || FVector2D::Distance(Focus, LastRiverFocus) > FocusRebuildM)
		{
			RebuildRivers(Aux, Space);
			LastRiverStage = Aux.RiverStageM;
			LastRiverFocus = Focus;
		}
		RebuildLocalPatch(Aux, Space);
	}

	// Splashes (new since the last frame), near the camera only.
	if (SplashSystem)
	{
		FVector View = GetActorLocation();
		if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		{
			FRotator Rotation;
			PC->GetPlayerViewPoint(View, Rotation);
		}
		double Newest = LastSplashTime;
		int32 Spawned = 0;
		for (const noctis::WaterDisturbance& Splash : Front->Snapshot.splashes)
		{
			if (Splash.time <= LastSplashTime || Spawned >= 8)
			{
				continue;
			}
			Newest = FMath::Max(Newest, Splash.time);
			const FVector Location = Space.ToUE(Splash.position, 0.0);
			if (FVector::Dist2D(Location, View) > SplashMaxDistanceM * 100.0)
			{
				continue;
			}
			// The splash record is 2D: take the water height from the local surface patch.
			float SurfaceZ = 0.0f;
			if (!Front->Aux.IsValid() || !NoctisWaterImpl::SampleSurface(*Front->Aux, Splash.position, SurfaceZ))
			{
				continue;
			}
			const float Scale = FMath::Clamp(FMath::Sqrt(FMath::Max(0.0f, Splash.strength)) / 30.0f, 0.2f, 4.0f);
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, SplashSystem, Space.ToUE(Splash.position, double(SurfaceZ)), FRotator::ZeroRotator, FVector(Scale));
			++Spawned;
		}
		LastSplashTime = Newest;
	}
}

void ANoctisWaterActor::RebuildRivers(const FNoctisAuxData& Aux, const FNoctisSpace& Space)
{
	using namespace NoctisWaterImpl;
	FMeshBuffers Mesh;
	const float SkipRadius = FMath::Max(0.0f, Aux.RadiusM - 20.0f); // the local patch covers the river there
	const FVector2f Focus(Aux.Focus.x, Aux.Focus.y);
	for (const FNoctisRiverSurface& River : Aux.Rivers)
	{
		int32 PrevLeft = INDEX_NONE;
		int32 PrevRight = INDEX_NONE;
		float Along = 0.0f;
		for (int32 Index = 0; Index < River.Points.Num(); ++Index)
		{
			const FVector3f& P = River.Points[Index];
			if (Index > 0)
			{
				Along += FVector2f::Distance(FVector2f(P.X, P.Y), FVector2f(River.Points[Index - 1].X, River.Points[Index - 1].Y));
			}
			const bool bInsideLocal = FVector2f::Distance(FVector2f(P.X, P.Y), Focus) < SkipRadius;
			if (bInsideLocal)
			{
				PrevLeft = PrevRight = INDEX_NONE;
				continue;
			}
			const FVector2f T = River.Tangents[Index].GetSafeNormal();
			const FVector2f Left(-T.Y, T.X);
			const float HalfWidth = River.HalfWidthM[Index];
			const FLinearColor Color = FlowColor(River.Flow[Index], 1.0f);
			const noctis::Vec3 LeftCore(P.X + Left.X * HalfWidth, P.Y + Left.Y * HalfWidth, P.Z);
			const noctis::Vec3 RightCore(P.X - Left.X * HalfWidth, P.Y - Left.Y * HalfWidth, P.Z);
			const int32 L = Mesh.AddVertex(Space.ToUE(LeftCore), FVector2D(0.0, Along / 10.0), Color);
			const int32 R = Mesh.AddVertex(Space.ToUE(RightCore), FVector2D(1.0, Along / 10.0), Color);
			if (PrevLeft != INDEX_NONE)
			{
				Mesh.AddTriangle(PrevLeft, PrevRight, R);
				Mesh.AddTriangle(PrevLeft, R, L);
			}
			PrevLeft = L;
			PrevRight = R;
		}
	}
	if (Mesh.Triangles.Num() == 0)
	{
		WaterMesh->ClearMeshSection(0);
		return;
	}
	WaterMesh->CreateMeshSection_LinearColor(0, Mesh.Vertices, Mesh.Triangles, Mesh.Normals, Mesh.UV0, Mesh.Colors, Mesh.Tangents, false);
	if (WaterMaterial)
	{
		WaterMesh->SetMaterial(0, WaterMaterial);
	}
}

void ANoctisWaterActor::RebuildLocalPatch(const FNoctisAuxData& Aux, const FNoctisSpace& Space)
{
	using namespace NoctisWaterImpl;
	FMeshBuffers Mesh;
	const float Cell = Aux.WaterCellM;
	for (int32 Y = 0; Y < Aux.WaterH; ++Y)
	{
		for (int32 X = 0; X < Aux.WaterW; ++X)
		{
			const int32 K = Y * Aux.WaterW + X;
			const float Surface = Aux.WaterSurface[K];
			const float Depth = Surface - Aux.GroundHeight[K];
			if (Depth < MinDepthM)
			{
				continue;
			}
			const float X0 = Aux.WaterOrigin.x + float(X) * Cell;
			const float Y0 = Aux.WaterOrigin.y + float(Y) * Cell;
			const FLinearColor Color = FlowColor(Aux.WaterFlow[K], Depth);
			const int32 A = Mesh.AddVertex(Space.ToUE(noctis::Vec3(X0, Y0, Surface)), FVector2D(X0 / 10.0f, Y0 / 10.0f), Color);
			const int32 B = Mesh.AddVertex(Space.ToUE(noctis::Vec3(X0 + Cell, Y0, Surface)), FVector2D((X0 + Cell) / 10.0f, Y0 / 10.0f), Color);
			const int32 C = Mesh.AddVertex(Space.ToUE(noctis::Vec3(X0 + Cell, Y0 + Cell, Surface)), FVector2D((X0 + Cell) / 10.0f, (Y0 + Cell) / 10.0f), Color);
			const int32 D = Mesh.AddVertex(Space.ToUE(noctis::Vec3(X0, Y0 + Cell, Surface)), FVector2D(X0 / 10.0f, (Y0 + Cell) / 10.0f), Color);
			Mesh.AddTriangle(A, B, C);
			Mesh.AddTriangle(A, C, D);
		}
	}
	if (Mesh.Triangles.Num() == 0)
	{
		WaterMesh->ClearMeshSection(1);
		return;
	}
	WaterMesh->CreateMeshSection_LinearColor(1, Mesh.Vertices, Mesh.Triangles, Mesh.Normals, Mesh.UV0, Mesh.Colors, Mesh.Tangents, false);
	if (WaterMaterial)
	{
		WaterMesh->SetMaterial(1, WaterMaterial);
	}
}
