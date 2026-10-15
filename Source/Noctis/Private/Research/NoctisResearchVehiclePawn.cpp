#include "Research/NoctisResearchVehiclePawn.h"

#include "NoctisLog.h"
#include "NoctisSettings.h"
#include "Research/NoctisDronePawn.h"
#include "Research/NoctisResearcherCharacter.h"
#include "Sim/NoctisWorldSubsystem.h"

#include "ChaosVehicleMovementComponent.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"

ANoctisResearchVehiclePawn::ANoctisResearchVehiclePawn(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	// 6x6: three axles (front, middle, rear), track 2.1 m. Must match the wheel setups of the Blueprint.
	const double Axles[3] = {190.0, 0.0, -190.0};
	for (const double X : Axles)
	{
		WheelOffsetsCm.Add(FVector(X, -105.0, -GroundOffsetCm));
		WheelOffsetsCm.Add(FVector(X, 105.0, -GroundOffsetCm));
	}
	auto Action = [](const TCHAR* Path) { return TSoftObjectPtr<UInputAction>(FSoftObjectPath(Path)); };
	ThrottleAction = Action(TEXT("/Game/Input/IA_Throttle.IA_Throttle"));
	BrakeAction = Action(TEXT("/Game/Input/IA_Brake.IA_Brake"));
	SteerAction = Action(TEXT("/Game/Input/IA_Steer.IA_Steer"));
	EngineAction = Action(TEXT("/Game/Input/IA_Engine.IA_Engine"));
	GeneratorAction = Action(TEXT("/Game/Input/IA_Generator.IA_Generator"));
	LightsAction = Action(TEXT("/Game/Input/IA_Lights.IA_Lights"));
	ExitAction = Action(TEXT("/Game/Input/IA_Interact.IA_Interact"));
	DroneAction = Action(TEXT("/Game/Input/IA_Drone.IA_Drone"));
	DroneClass = ANoctisDronePawn::StaticClass();
	Controls.engineOn = true;
}

void ANoctisResearchVehiclePawn::BeginPlay()
{
	Super::BeginPlay();
	if (Authority == ENoctisVehicleAuthority::CoreTerramechanics)
	{
		GetMesh()->SetSimulatePhysics(false);
	}
	if (DroneClass && GetWorld())
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Drone = GetWorld()->SpawnActor<ANoctisDronePawn>(DroneClass, GetActorTransform(), Params);
		if (Drone)
		{
			Drone->SetHomeVehicle(this);
		}
	}
}

void ANoctisResearchVehiclePawn::PawnClientRestart()
{
	Super::PawnClientRestart();
	const APlayerController* PC = Cast<APlayerController>(GetController());
	UEnhancedInputLocalPlayerSubsystem* Input = PC ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
	if (!Input)
	{
		return;
	}
	const UNoctisSettings& Settings = UNoctisSettings::Get();
	for (const TSoftObjectPtr<UInputMappingContext>& Soft : {Settings.ResearcherContext, Settings.DroneContext, Settings.ToolsContext})
	{
		if (UInputMappingContext* Context = Soft.LoadSynchronous())
		{
			Input->RemoveMappingContext(Context);
		}
	}
	if (UInputMappingContext* Context = Settings.VehicleContext.LoadSynchronous())
	{
		Input->AddMappingContext(Context, 0);
	}
}

void ANoctisResearchVehiclePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		return;
	}
	auto Bind = [this, Input](const TSoftObjectPtr<UInputAction>& Soft, ETriggerEvent Event, void (ANoctisResearchVehiclePawn::*Handler)(const FInputActionValue&)) {
		if (UInputAction* Action = Soft.LoadSynchronous())
		{
			Input->BindAction(Action, Event, this, Handler);
		}
	};
	Bind(ThrottleAction, ETriggerEvent::Triggered, &ANoctisResearchVehiclePawn::Throttle);
	Bind(ThrottleAction, ETriggerEvent::Completed, &ANoctisResearchVehiclePawn::Throttle);
	Bind(BrakeAction, ETriggerEvent::Triggered, &ANoctisResearchVehiclePawn::Brake);
	Bind(BrakeAction, ETriggerEvent::Completed, &ANoctisResearchVehiclePawn::Brake);
	Bind(SteerAction, ETriggerEvent::Triggered, &ANoctisResearchVehiclePawn::Steer);
	Bind(SteerAction, ETriggerEvent::Completed, &ANoctisResearchVehiclePawn::Steer);
	Bind(EngineAction, ETriggerEvent::Started, &ANoctisResearchVehiclePawn::ToggleEngine);
	Bind(GeneratorAction, ETriggerEvent::Started, &ANoctisResearchVehiclePawn::ToggleGenerator);
	Bind(LightsAction, ETriggerEvent::Started, &ANoctisResearchVehiclePawn::ToggleLights);
	Bind(ExitAction, ETriggerEvent::Started, &ANoctisResearchVehiclePawn::ExitPressed);
	Bind(DroneAction, ETriggerEvent::Started, &ANoctisResearchVehiclePawn::DronePressed);
}

void ANoctisResearchVehiclePawn::Throttle(const FInputActionValue& Value)
{
	Controls.throttle = FMath::Clamp(Value.Get<float>(), -1.0f, 1.0f);
	if (UChaosVehicleMovementComponent* Movement = GetVehicleMovementComponent())
	{
		Movement->SetThrottleInput(FMath::Max(0.0f, Controls.throttle));
		Movement->SetBrakeInput(FMath::Max(Controls.brake, FMath::Max(0.0f, -Controls.throttle)));
	}
}

void ANoctisResearchVehiclePawn::Brake(const FInputActionValue& Value)
{
	Controls.brake = FMath::Clamp(Value.Get<float>(), 0.0f, 1.0f);
	if (UChaosVehicleMovementComponent* Movement = GetVehicleMovementComponent())
	{
		Movement->SetBrakeInput(Controls.brake);
	}
}

void ANoctisResearchVehiclePawn::Steer(const FInputActionValue& Value)
{
	Controls.steer = FMath::Clamp(Value.Get<float>(), -1.0f, 1.0f);
	if (UChaosVehicleMovementComponent* Movement = GetVehicleMovementComponent())
	{
		Movement->SetSteeringInput(Controls.steer);
	}
}

void ANoctisResearchVehiclePawn::ToggleEngine(const FInputActionValue& Value)
{
	Controls.engineOn = !Controls.engineOn;
}

void ANoctisResearchVehiclePawn::ToggleGenerator(const FInputActionValue& Value)
{
	Controls.generatorOn = !Controls.generatorOn;
}

void ANoctisResearchVehiclePawn::ToggleLights(const FInputActionValue& Value)
{
	Controls.lightsOn = !Controls.lightsOn;
}

void ANoctisResearchVehiclePawn::ExitPressed(const FInputActionValue& Value)
{
	ExitVehicle();
}

void ANoctisResearchVehiclePawn::DronePressed(const FInputActionValue& Value)
{
	LaunchAndPilotDrone();
}

bool ANoctisResearchVehiclePawn::EnterFrom(ANoctisResearcherCharacter* Researcher)
{
	AController* ResearcherController = Researcher ? Researcher->GetController() : nullptr;
	if (!ResearcherController)
	{
		return false;
	}
	Driver = Researcher;
	Researcher->SetActorHiddenInGame(true);
	Researcher->SetActorEnableCollision(false);
	ResearcherController->Possess(this);
	if (UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this))
	{
		Sub->EnqueueCommand([](noctis::WorldSimulation& Sim) { Sim.research().enterVehicle(true); });
	}
	return true;
}

void ANoctisResearchVehiclePawn::ExitVehicle()
{
	ANoctisResearcherCharacter* Researcher = Driver.Get();
	AController* OwningController = GetController();
	if (!Researcher || !OwningController)
	{
		return;
	}
	// Step out on the right-hand side, 3 m from the centreline (the core does the same in ResearchSystem::enterVehicle).
	const FVector Exit = GetActorLocation() + GetActorRightVector() * 300.0 + FVector(0.0, 0.0, 50.0);
	Researcher->TeleportTo(Exit, FRotator(0.0, GetActorRotation().Yaw, 0.0));
	Researcher->SetActorHiddenInGame(false);
	Researcher->SetActorEnableCollision(true);
	OwningController->Possess(Researcher);
	Driver.Reset();
	Controls.throttle = 0.0f;
	Controls.brake = 1.0f;
	if (UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this))
	{
		Sub->EnqueueCommand([](noctis::WorldSimulation& Sim) { Sim.research().enterVehicle(false); });
	}
}

void ANoctisResearchVehiclePawn::LaunchAndPilotDrone()
{
	if (Drone && GetController())
	{
		Drone->LaunchAndPilot(GetController(), this);
	}
}

void ANoctisResearchVehiclePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	if (!Sub)
	{
		return;
	}
	const FVector Location = GetActorLocation();
	const FVector Ground = Location - GetActorUpVector() * GroundOffsetCm;
	if (Authority == ENoctisVehicleAuthority::UnrealChaos)
	{
		TArray<FVector> Contacts;
		for (const FVector& Offset : WheelOffsetsCm)
		{
			Contacts.Add(GetActorTransform().TransformPosition(Offset));
		}
		const UChaosVehicleMovementComponent* Movement = GetVehicleMovementComponent();
		const float Mass = Movement ? Movement->Mass : 7200.0f;
		const float WheelLoadN = Mass * 9.81f / FMath::Max(1, WheelOffsetsCm.Num());
		Sub->SubmitVehicle(true, Ground, static_cast<float>(GetActorRotation().Yaw), static_cast<float>(GetVelocity().Size2D()), Controls, Contacts, WheelLoadN);
		ApplyCoreFeedback(DeltaSeconds);
	}
	else
	{
		// The core integrates the controls; follow the interpolated state kinematically.
		Sub->SubmitVehicle(false, Ground, static_cast<float>(GetActorRotation().Yaw), 0.0f, Controls, TArray<FVector>(), 0.0f);
		const FNoctisFrameOutput* Front = Sub->GetFrontFrame();
		const FNoctisFrameOutput* Previous = Sub->GetPreviousFrame();
		if (Front && Previous)
		{
			const float Alpha = Sub->GetInterpolationAlpha();
			const noctis::VehicleState& A = Previous->Vehicle;
			const noctis::VehicleState& B = Front->Vehicle;
			const noctis::Vec3 P = noctis::lerp(A.position, B.position, Alpha);
			const float Heading = A.heading + noctis::angleDelta(A.heading, B.heading) * Alpha;
			const FVector Target = Sub->GetSpace().ToUE(P) + GetActorUpVector() * GroundOffsetCm;
			SetActorLocationAndRotation(Target, FNoctisSpace::AttitudeToUE(Heading, FMath::Lerp(A.pitch, B.pitch, Alpha), FMath::Lerp(A.roll, B.roll, Alpha)), false, nullptr,
										ETeleportType::TeleportPhysics);
		}
	}
	if (const FNoctisFrameOutput* Front = Sub->GetFrontFrame())
	{
		BatteryKWh = Front->Vehicle.batteryKWh;
		FuelL = Front->Vehicle.fuelL;
		WheelSinkageM = Front->Vehicle.wheelSinkageM;
		bStuck = Front->Vehicle.stuck;
		NoiseDb = Front->Vehicle.noiseDb;
	}
}

void ANoctisResearchVehiclePawn::ApplyCoreFeedback(float DeltaSeconds)
{
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this);
	const FNoctisFrameOutput* Front = Sub ? Sub->GetFrontFrame() : nullptr;
	UChaosWheeledVehicleMovementComponent* Movement = Cast<UChaosWheeledVehicleMovementComponent>(GetVehicleMovementComponent());
	if (!bApplyTerramechanics || !Front || !Movement || Front->Wheels.Num() == 0)
	{
		return;
	}
	// Traction: Bekker/Mohr-Coulomb mu relative to a firm dirt road (mu ~ 0.8).
	float RollingForceN = 0.0f;
	const int32 Wheels = FMath::Min(Front->Wheels.Num(), WheelOffsetsCm.Num());
	const float WheelLoadN = Movement->Mass * 9.81f / FMath::Max(1, WheelOffsetsCm.Num());
	for (int32 Index = 0; Index < Wheels; ++Index)
	{
		const FNoctisWheelGround& Wheel = Front->Wheels[Index];
		// VERIFY: UChaosWheeledVehicleMovementComponent::SetWheelFrictionMultiplier(int32, float) (UE 5.1+).
		Movement->SetWheelFrictionMultiplier(Index, FMath::Clamp(Wheel.TractionMu / 0.8f, 0.05f, 1.5f));
		RollingForceN += Wheel.RollingCoef * WheelLoadN;
	}
	// Rolling resistance (sinkage-dependent) as a force against the motion; Unreal forces are in kg.cm/s^2 (= N x 100).
	const FVector Velocity = GetVelocity();
	if (Velocity.SizeSquared2D() > 1.0)
	{
		GetMesh()->AddForce(-Velocity.GetSafeNormal2D() * double(RollingForceN) * 100.0);
	}
	(void)DeltaSeconds;
}
