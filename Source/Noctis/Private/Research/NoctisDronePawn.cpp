#include "Research/NoctisDronePawn.h"

#include "NoctisSettings.h"
#include "Creatures/NoctisCreatureActor.h"
#include "Sim/NoctisWorldSubsystem.h"

#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"

#include "Noctis/Research/ResearchSystem.h"

ANoctisDronePawn::ANoctisDronePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Gimbal = CreateDefaultSubobject<USceneComponent>(TEXT("Gimbal"));
	Gimbal->SetupAttachment(Root);
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("DroneCamera"));
	Camera->SetupAttachment(Gimbal);
	Camera->SetFieldOfView(70.0f);
	RotorAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("Rotors"));
	RotorAudio->SetupAttachment(Root);
	RotorAudio->bAutoActivate = false;
	SetActorHiddenInGame(true);

	auto Action = [](const TCHAR* Path) { return TSoftObjectPtr<UInputAction>(FSoftObjectPath(Path)); };
	MoveAction = Action(TEXT("/Game/Input/IA_Move.IA_Move"));
	AltitudeAction = Action(TEXT("/Game/Input/IA_DroneAltitude.IA_DroneAltitude"));
	LookAction = Action(TEXT("/Game/Input/IA_Look.IA_Look"));
	OrbitAction = Action(TEXT("/Game/Input/IA_DroneOrbit.IA_DroneOrbit"));
	FollowAction = Action(TEXT("/Game/Input/IA_DroneFollow.IA_DroneFollow"));
	ReturnAction = Action(TEXT("/Game/Input/IA_DroneReturn.IA_DroneReturn"));
	ObserveAction = Action(TEXT("/Game/Input/IA_UseTool.IA_UseTool"));
	ExitAction = Action(TEXT("/Game/Input/IA_Interact.IA_Interact"));
}

void ANoctisDronePawn::PawnClientRestart()
{
	Super::PawnClientRestart();
	const APlayerController* PC = Cast<APlayerController>(GetController());
	UEnhancedInputLocalPlayerSubsystem* Input = PC ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
	if (!Input)
	{
		return;
	}
	const UNoctisSettings& Settings = UNoctisSettings::Get();
	for (const TSoftObjectPtr<UInputMappingContext>& Soft : {Settings.ResearcherContext, Settings.VehicleContext, Settings.ToolsContext})
	{
		if (UInputMappingContext* Context = Soft.LoadSynchronous())
		{
			Input->RemoveMappingContext(Context);
		}
	}
	if (UInputMappingContext* Context = Settings.DroneContext.LoadSynchronous())
	{
		Input->AddMappingContext(Context, 0);
	}
}

void ANoctisDronePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		return;
	}
	auto Bind = [this, Input](const TSoftObjectPtr<UInputAction>& Soft, ETriggerEvent Event, void (ANoctisDronePawn::*Handler)(const FInputActionValue&)) {
		if (UInputAction* Action = Soft.LoadSynchronous())
		{
			Input->BindAction(Action, Event, this, Handler);
		}
	};
	Bind(MoveAction, ETriggerEvent::Triggered, &ANoctisDronePawn::Move);
	Bind(MoveAction, ETriggerEvent::Completed, &ANoctisDronePawn::Move);
	Bind(AltitudeAction, ETriggerEvent::Triggered, &ANoctisDronePawn::Altitude);
	Bind(LookAction, ETriggerEvent::Triggered, &ANoctisDronePawn::Look);
	Bind(OrbitAction, ETriggerEvent::Started, &ANoctisDronePawn::OrbitPressed);
	Bind(FollowAction, ETriggerEvent::Started, &ANoctisDronePawn::FollowPressed);
	Bind(ReturnAction, ETriggerEvent::Started, &ANoctisDronePawn::ReturnPressed);
	Bind(ObserveAction, ETriggerEvent::Started, &ANoctisDronePawn::ObservePressed);
	Bind(ExitAction, ETriggerEvent::Started, &ANoctisDronePawn::ExitPressed);
}

void ANoctisDronePawn::LaunchAndPilot(AController* PilotController, APawn* ReturnPawn)
{
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	if (!Sub || !PilotController)
	{
		return;
	}
	if (Mode == ENoctisDroneMode::Docked)
	{
		Sub->EnqueueCommand([](noctis::WorldSimulation& Sim) { Sim.research().launchDrone(Sim.context()); });
	}
	ReturnTo = ReturnPawn;
	bPiloted = true;
	PilotController->Possess(this);
}

void ANoctisDronePawn::StopPiloting()
{
	AController* Pilot = GetController();
	bPiloted = false;
	ManualInput = FVector2D::ZeroVector;
	if (Pilot && ReturnTo.IsValid())
	{
		Pilot->Possess(ReturnTo.Get());
	}
}

void ANoctisDronePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	const FNoctisFrameOutput* Front = Sub ? Sub->GetFrontFrame() : nullptr;
	const FNoctisFrameOutput* Previous = Sub ? Sub->GetPreviousFrame() : nullptr;
	if (!Front || !Previous)
	{
		return;
	}
	const noctis::DroneState& A = Previous->Drone;
	const noctis::DroneState& B = Front->Drone;
	Mode = static_cast<ENoctisDroneMode>(B.mode);
	BatteryWh = B.batteryWh;
	AltitudeAglM = B.altitudeAglM;
	Signal = B.signal;
	Warning = NoctisUnreal::ToFString(B.warningFr);
	const bool bFlying = B.mode != noctis::DroneMode::Docked && B.mode != noctis::DroneMode::Lost;
	SetActorHiddenInGame(!bFlying);
	if (bFlying)
	{
		const float Alpha = Sub->GetInterpolationAlpha();
		const noctis::Vec3 P = noctis::lerp(A.position, B.position, Alpha);
		const float Heading = A.heading + noctis::angleDelta(A.heading, B.heading) * Alpha;
		SetActorLocationAndRotation(Sub->GetSpace().ToUE(P), FRotator(0.0, FNoctisSpace::HeadingToYawDeg(Heading), 0.0));
		if (RotorAudio->Sound && !RotorAudio->IsPlaying())
		{
			RotorAudio->Play();
		}
		RotorAudio->SetFloatParameter(TEXT("PowerW"), B.powerW);
	}
	else if (RotorAudio->IsPlaying())
	{
		RotorAudio->Stop();
	}
	Gimbal->SetRelativeRotation(FRotator(GimbalPitchDeg, 0.0, 0.0));

	if (bPiloted)
	{
		// Manual flight: stick in the camera frame, converted to a world velocity for the core's autopilot.
		if (!ManualInput.IsNearlyZero())
		{
			const FRotator Yaw(0.0, GetActorRotation().Yaw, 0.0);
			const FVector Velocity = (FRotationMatrix(Yaw).GetUnitAxis(EAxis::X) * ManualInput.Y + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) * ManualInput.X) *
									 double(MaxManualSpeedMs) * 100.0;
			Sub->SubmitDroneManual(Velocity, TargetAltitudeM);
		}
		if (!bFlying && B.mode == noctis::DroneMode::Docked && A.mode != noctis::DroneMode::Docked)
		{
			StopPiloting(); // landed on the rover (battery, return home)
		}
	}
}

void ANoctisDronePawn::Move(const FInputActionValue& Value)
{
	ManualInput = Value.Get<FVector2D>();
}

void ANoctisDronePawn::Altitude(const FInputActionValue& Value)
{
	// Sent with the next manual stick input (submitting it alone would switch an orbit/follow autopilot to manual).
	TargetAltitudeM = FMath::Clamp(TargetAltitudeM + Value.Get<float>() * 0.5f, 5.0f, 120.0f);
}

void ANoctisDronePawn::Look(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	GimbalPitchDeg = FMath::Clamp(GimbalPitchDeg + Axis.Y, -90.0f, 10.0f);
	// Yaw is commanded through the heading of manual flight; small yaw input rotates the view only.
	Gimbal->AddRelativeRotation(FRotator(0.0, Axis.X, 0.0));
}

bool ANoctisDronePawn::TraceCrosshair(FHitResult& OutHit, ECollisionChannel Channel) const
{
	const FVector Start = Camera->GetComponentLocation();
	const FVector End = Start + Camera->GetForwardVector() * 150000.0;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoctisDroneCrosshair), false, this);
	return GetWorld() && GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, Channel, Params);
}

void ANoctisDronePawn::OrbitCrosshairTarget(float RadiusM, float AltitudeM)
{
	FHitResult Hit;
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	if (!Sub || !TraceCrosshair(Hit, ECC_Visibility))
	{
		return;
	}
	const noctis::Vec2 Centre = Sub->GetSpace().FromUE2D(Hit.ImpactPoint);
	Sub->EnqueueCommand([Centre, RadiusM, AltitudeM](noctis::WorldSimulation& Sim) { Sim.research().drone().setOrbit(Centre, RadiusM, AltitudeM); });
}

void ANoctisDronePawn::FollowCrosshairTarget(float AltitudeM)
{
	FHitResult Hit;
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	// Creatures block the NoctisObservation channel (ECC_GameTraceChannel1, see DefaultEngine.ini).
	if (!Sub || !TraceCrosshair(Hit, ECC_GameTraceChannel1))
	{
		return;
	}
	const ANoctisCreatureActor* Creature = Cast<ANoctisCreatureActor>(Hit.GetActor());
	if (!Creature)
	{
		return;
	}
	const noctis::EntityId Target(Creature->GetCreatureIdRaw());
	Sub->EnqueueCommand([Target, AltitudeM](noctis::WorldSimulation& Sim) { Sim.research().drone().setFollow(Target, AltitudeM); });
}

void ANoctisDronePawn::ReturnHome()
{
	if (UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this))
	{
		Sub->EnqueueCommand([](noctis::WorldSimulation& Sim) { Sim.research().drone().command(noctis::DroneMode::ReturnHome); });
	}
}

void ANoctisDronePawn::ObserveFromDrone()
{
	if (UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this))
	{
		Sub->EnqueueCommand([](noctis::WorldSimulation& Sim) { Sim.research().observeNow(noctis::OpticsKind::DroneCamera, Sim.context(), Sim.creatures()); });
	}
}

void ANoctisDronePawn::OrbitPressed(const FInputActionValue& Value)
{
	OrbitCrosshairTarget();
}

void ANoctisDronePawn::FollowPressed(const FInputActionValue& Value)
{
	FollowCrosshairTarget();
}

void ANoctisDronePawn::ReturnPressed(const FInputActionValue& Value)
{
	ReturnHome();
}

void ANoctisDronePawn::ObservePressed(const FInputActionValue& Value)
{
	ObserveFromDrone();
}

void ANoctisDronePawn::ExitPressed(const FInputActionValue& Value)
{
	StopPiloting(); // the drone keeps its current autopilot mode (hover, orbit, follow)
}
