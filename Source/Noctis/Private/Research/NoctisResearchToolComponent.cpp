#include "Research/NoctisResearchToolComponent.h"

#include "NoctisLog.h"
#include "Sim/NoctisWorldSubsystem.h"

#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#include "Noctis/Research/ResearchSystem.h"

namespace NoctisToolsImpl
{
noctis::PhotoSettings ToCore(const FNoctisPhotoSettings& S)
{
	noctis::PhotoSettings P;
	P.focalMm = S.FocalMm;
	P.sensorWidthMm = S.SensorWidthMm;
	P.widthPx = S.WidthPx;
	P.shutterS = S.ShutterSeconds;
	P.aperture = S.Aperture;
	P.iso = S.Iso;
	P.laserRangefinder = S.bLaserRangefinder;
	return P;
}

noctis::SensorKind SensorFor(ENoctisTool Tool)
{
	switch (Tool)
	{
	case ENoctisTool::AudioRecorder: return noctis::SensorKind::AudioRecorder;
	case ENoctisTool::WeatherStation: return noctis::SensorKind::WeatherStation;
	default: return noctis::SensorKind::CameraTrap;
	}
}

float MountHeightM(ENoctisTool Tool)
{
	switch (Tool)
	{
	case ENoctisTool::AudioRecorder: return 1.0f;
	case ENoctisTool::WeatherStation: return 2.0f;
	default: return 0.5f;
	}
}
} // namespace NoctisToolsImpl

UNoctisResearchToolComponent::UNoctisResearchToolComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UNoctisResearchToolComponent::BeginPlay()
{
	Super::BeginPlay();
	if (const UCameraComponent* Camera = FindCamera())
	{
		DefaultFov = Camera->FieldOfView;
	}
	ApplyOptics();
}

UCameraComponent* UNoctisResearchToolComponent::FindCamera() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UCameraComponent>() : nullptr;
}

void UNoctisResearchToolComponent::SelectTool(ENoctisTool Tool)
{
	CurrentTool = Tool;
	ApplyOptics();
	OnToolChanged.Broadcast(CurrentTool);
}

void UNoctisResearchToolComponent::CycleTool(int32 Direction)
{
	const int32 Count = static_cast<int32>(ENoctisTool::Notebook) + 1;
	const int32 Next = ((static_cast<int32>(CurrentTool) + Direction) % Count + Count) % Count;
	SelectTool(static_cast<ENoctisTool>(Next));
}

void UNoctisResearchToolComponent::ApplyOptics()
{
	UCameraComponent* Camera = FindCamera();
	if (!Camera)
	{
		return;
	}
	float Fov = DefaultFov;
	if (CurrentTool == ENoctisTool::Binoculars)
	{
		Fov = DefaultFov / FMath::Max(1.0f, BinocularMagnification);
	}
	else if (CurrentTool == ENoctisTool::Camera)
	{
		// Horizontal angle of view of the lens on the sensor.
		Fov = FMath::RadiansToDegrees(2.0f * FMath::Atan(PhotoSettings.SensorWidthMm / (2.0f * FMath::Max(1.0f, PhotoSettings.FocalMm))));
	}
	Camera->SetFieldOfView(FMath::Clamp(Fov, 1.0f, 120.0f));
}

float UNoctisResearchToolComponent::GetLookSensitivityScale() const
{
	const UCameraComponent* Camera = FindCamera();
	return Camera ? FMath::Clamp(Camera->FieldOfView / FMath::Max(1.0f, DefaultFov), 0.05f, 1.0f) : 1.0f;
}

void UNoctisResearchToolComponent::AdjustZoom(float Delta)
{
	if (CurrentTool == ENoctisTool::Camera)
	{
		PhotoSettings.FocalMm = FMath::Clamp(PhotoSettings.FocalMm * (1.0f + 0.08f * Delta), 100.0f, 600.0f);
	}
	else if (CurrentTool == ENoctisTool::Binoculars)
	{
		BinocularMagnification = FMath::Clamp(BinocularMagnification + Delta, 6.0f, 15.0f);
	}
	ApplyOptics();
}

void UNoctisResearchToolComponent::UseCurrentTool()
{
	switch (CurrentTool)
	{
	case ENoctisTool::NakedEye: Observe(false); break;
	case ENoctisTool::Binoculars: Observe(true); break;
	case ENoctisTool::Camera: TakePhoto(); break;
	case ENoctisTool::Microphone: ToggleRecording(); break;
	case ENoctisTool::MicArray: ToggleMicArray(); break;
	case ENoctisTool::CameraTrap:
	case ENoctisTool::AudioRecorder:
	case ENoctisTool::WeatherStation: PlaceSensor(CurrentTool); break;
	case ENoctisTool::Notebook: break; // UI (field journal widget)
	}
}

void UNoctisResearchToolComponent::Observe(bool bBinoculars)
{
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	if (!Sub)
	{
		return;
	}
	const noctis::OpticsKind Kind = bBinoculars ? noctis::OpticsKind::Binoculars : noctis::OpticsKind::Eye;
	TSharedRef<noctis::u32, ESPMode::ThreadSafe> Result = MakeShared<noctis::u32, ESPMode::ThreadSafe>(0u);
	TWeakObjectPtr<UNoctisResearchToolComponent> WeakThis(this);
	Sub->EnqueueCommand([Kind, Result](noctis::WorldSimulation& Sim) { *Result = Sim.research().observeNow(Kind, Sim.context(), Sim.creatures()); },
						[WeakThis, Result]() {
							if (WeakThis.IsValid() && *Result != 0)
							{
								WeakThis->OnObservationRecorded.Broadcast(static_cast<int32>(*Result));
							}
						});
}

void UNoctisResearchToolComponent::TakePhoto()
{
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	if (!Sub)
	{
		return;
	}
	const noctis::PhotoSettings Settings = NoctisToolsImpl::ToCore(PhotoSettings);
	TSharedRef<noctis::u32, ESPMode::ThreadSafe> PhotoId = MakeShared<noctis::u32, ESPMode::ThreadSafe>(0u);
	TWeakObjectPtr<UNoctisResearchToolComponent> WeakThis(this);
	const bool bScreenshot = bCaptureScreenshots;
	Sub->EnqueueCommand([Settings, PhotoId](noctis::WorldSimulation& Sim) { *PhotoId = Sim.research().takePhoto(Settings, Sim.context(), Sim.creatures()); },
						[WeakThis, PhotoId, bScreenshot]() {
							UNoctisResearchToolComponent* Self = WeakThis.Get();
							if (!Self || *PhotoId == 0)
							{
								return;
							}
							FString File;
							if (bScreenshot)
							{
								const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NoctisPhotos"));
								IFileManager::Get().MakeDirectory(*Dir, true);
								File = FPaths::Combine(Dir, FString::Printf(TEXT("photo_%05u.png"), *PhotoId));
								// The image is captured on the next rendered frame; the core's photo metrics were computed at the shot.
								FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
								if (UNoctisWorldSubsystem* InnerSub = UNoctisWorldSubsystem::Get(Self))
								{
									const noctis::u32 Id = *PhotoId;
									const std::string Path = NoctisUnreal::ToStdString(File);
									InnerSub->EnqueueCommand([Id, Path](noctis::WorldSimulation& Sim) {
										for (noctis::PhotoRecord& Photo : Sim.research().observations().mutablePhotos())
										{
											if (Photo.id == Id)
											{
												Photo.imageFile = Path;
											}
										}
									});
								}
							}
							Self->OnPhotoTaken.Broadcast(static_cast<int32>(*PhotoId), File);
						});
}

void UNoctisResearchToolComponent::ToggleRecording()
{
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	if (!Sub)
	{
		return;
	}
	TWeakObjectPtr<UNoctisResearchToolComponent> WeakThis(this);
	if (RecordingHandle == 0)
	{
		TSharedRef<noctis::u32, ESPMode::ThreadSafe> Handle = MakeShared<noctis::u32, ESPMode::ThreadSafe>(0u);
		Sub->EnqueueCommand(
			[Handle](noctis::WorldSimulation& Sim) {
				noctis::ResearchSystem& Research = Sim.research();
				*Handle = Research.startRecording(noctis::MicSpec(), Research.eyePosition(), Research.viewHeading(), 0, "Enregistrement à la main", Sim.context());
			},
			[WeakThis, Handle]() {
				if (WeakThis.IsValid())
				{
					WeakThis->RecordingHandle = *Handle;
				}
			});
	}
	else
	{
		const noctis::u32 Handle = RecordingHandle;
		RecordingHandle = 0;
		TSharedRef<noctis::u32, ESPMode::ThreadSafe> RecordingId = MakeShared<noctis::u32, ESPMode::ThreadSafe>(0u);
		Sub->EnqueueCommand([Handle, RecordingId](noctis::WorldSimulation& Sim) { *RecordingId = Sim.research().stopRecording(Handle, Sim.context()); },
							[WeakThis, RecordingId]() {
								if (WeakThis.IsValid() && *RecordingId != 0)
								{
									WeakThis->OnRecordingFinished.Broadcast(static_cast<int32>(*RecordingId));
								}
							});
	}
}

void UNoctisResearchToolComponent::ToggleMicArray()
{
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	if (!Sub)
	{
		return;
	}
	TWeakObjectPtr<UNoctisResearchToolComponent> WeakThis(this);
	if (MicArrayId == 0)
	{
		FVector Location;
		float Yaw = 0.0f;
		if (!TracePlacement(Location, Yaw))
		{
			return;
		}
		const noctis::Vec2 Centre = Sub->GetSpace().FromUE2D(Location);
		const float Radius = MicArrayRadiusM;
		const int32 Count = FMath::Clamp(MicArrayCount, 3, 8);
		TSharedRef<noctis::u32, ESPMode::ThreadSafe> ArrayId = MakeShared<noctis::u32, ESPMode::ThreadSafe>(0u);
		Sub->EnqueueCommand([Centre, Radius, Count, ArrayId](noctis::WorldSimulation& Sim) {
								*ArrayId = Sim.research().deployMicArray(Centre, Radius, Count, noctis::MicSpec(), Sim.context());
							},
							[WeakThis, ArrayId]() {
								if (WeakThis.IsValid())
								{
									WeakThis->MicArrayId = *ArrayId;
									WeakThis->OnDevicePlaced.Broadcast(ENoctisTool::MicArray, static_cast<int32>(*ArrayId));
								}
							});
	}
	else
	{
		const noctis::u32 ArrayId = MicArrayId;
		MicArrayId = 0;
		TSharedRef<TArray<int32>, ESPMode::ThreadSafe> Recordings = MakeShared<TArray<int32>, ESPMode::ThreadSafe>();
		Sub->EnqueueCommand(
			[ArrayId, Recordings](noctis::WorldSimulation& Sim) {
				for (const noctis::u32 Id : Sim.research().stopMicArray(ArrayId, Sim.context()))
				{
					Recordings->Add(static_cast<int32>(Id));
				}
			},
			[WeakThis, Recordings]() {
				if (WeakThis.IsValid())
				{
					for (const int32 Id : *Recordings)
					{
						WeakThis->OnRecordingFinished.Broadcast(Id);
					}
				}
			});
	}
}

bool UNoctisResearchToolComponent::TracePlacement(FVector& OutLocation, float& OutYawDeg) const
{
	const UCameraComponent* Camera = FindCamera();
	UWorld* World = GetWorld();
	if (!Camera || !World)
	{
		return false;
	}
	const FVector Forward = FVector(Camera->GetForwardVector().X, Camera->GetForwardVector().Y, 0.0).GetSafeNormal();
	const FVector Start = Camera->GetComponentLocation() + Forward * PlacementReachCm + FVector(0.0, 0.0, 200.0);
	const FVector End = Start - FVector(0.0, 0.0, 1000.0);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoctisPlacement), false, GetOwner());
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		return false;
	}
	OutLocation = Hit.ImpactPoint;
	OutYawDeg = static_cast<float>(Forward.Rotation().Yaw);
	return true;
}

void UNoctisResearchToolComponent::PlaceSensor(ENoctisTool Tool)
{
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	FVector Location;
	float Yaw = 0.0f;
	if (!Sub || !TracePlacement(Location, Yaw))
	{
		return;
	}
	noctis::Vec3 Position = Sub->GetSpace().FromUE(Location);
	Position.z += NoctisToolsImpl::MountHeightM(Tool);
	const float Heading = FNoctisSpace::YawDegToHeading(Yaw);
	const noctis::SensorKind Kind = NoctisToolsImpl::SensorFor(Tool);
	TSharedRef<noctis::u32, ESPMode::ThreadSafe> SensorId = MakeShared<noctis::u32, ESPMode::ThreadSafe>(0u);
	TWeakObjectPtr<UNoctisResearchToolComponent> WeakThis(this);
	Sub->EnqueueCommand([Kind, Position, Heading, SensorId](noctis::WorldSimulation& Sim) { *SensorId = Sim.research().placeSensor(Kind, Position, Heading, Sim.context()); },
						[WeakThis, Tool, SensorId]() {
							if (WeakThis.IsValid() && *SensorId != 0)
							{
								WeakThis->OnDevicePlaced.Broadcast(Tool, static_cast<int32>(*SensorId));
							}
						});
}

void UNoctisResearchToolComponent::RetrieveDevice(int32 SensorId)
{
	if (UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this))
	{
		const noctis::u32 Id = static_cast<noctis::u32>(SensorId);
		Sub->EnqueueCommand([Id](noctis::WorldSimulation& Sim) { Sim.research().retrieveSensor(Id); });
	}
}

void UNoctisResearchToolComponent::StartObservationSession(FName FocalSpeciesId, bool bWithBinoculars)
{
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	if (!Sub)
	{
		return;
	}
	const noctis::i16 Focus = static_cast<noctis::i16>(FocalSpeciesId.IsNone() ? -1 : Sub->FindSpeciesIndex(FocalSpeciesId));
	const noctis::ObservationMethod Method = bWithBinoculars ? noctis::ObservationMethod::Binoculars : noctis::ObservationMethod::NakedEye;
	TSharedRef<noctis::u32, ESPMode::ThreadSafe> Id = MakeShared<noctis::u32, ESPMode::ThreadSafe>(0u);
	TWeakObjectPtr<UNoctisResearchToolComponent> WeakThis(this);
	Sub->EnqueueCommand([Method, Focus, Id](noctis::WorldSimulation& Sim) { *Id = Sim.research().startSession(Method, Focus, Sim.context()); },
						[WeakThis, Id]() {
							if (WeakThis.IsValid())
							{
								WeakThis->SessionId = *Id;
							}
						});
}

void UNoctisResearchToolComponent::StopObservationSession()
{
	if (UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this))
	{
		Sub->EnqueueCommand([](noctis::WorldSimulation& Sim) { Sim.research().stopSession(Sim.context()); });
		SessionId = 0;
	}
}
