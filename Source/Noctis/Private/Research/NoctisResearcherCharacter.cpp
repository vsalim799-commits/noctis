#include "Research/NoctisResearcherCharacter.h"

#include "NoctisSettings.h"
#include "Research/NoctisResearchToolComponent.h"
#include "Research/NoctisResearchVehiclePawn.h"
#include "Sim/NoctisWorldSubsystem.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"

ANoctisResearcherCharacter::ANoctisResearcherCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	GetCapsuleComponent()->InitCapsuleSize(35.0f, 90.0f);
	BaseEyeHeight = 72.0f; // eye ~1.62 m above the ground, as the core assumes (+1.65 m)
	CrouchedEyeHeight = 20.0f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.0, 0.0, BaseEyeHeight));
	Camera->bUsePawnControlRotation = true;
	Camera->SetFieldOfView(75.0f);

	Headlamp = CreateDefaultSubobject<USpotLightComponent>(TEXT("Headlamp"));
	Headlamp->SetupAttachment(Camera);
	Headlamp->SetIntensityUnits(ELightUnits::Lumens);
	Headlamp->SetIntensity(300.0f);
	Headlamp->SetOuterConeAngle(28.0f);
	Headlamp->SetAttenuationRadius(3500.0f);
	Headlamp->SetVisibility(false);

	Tools = CreateDefaultSubobject<UNoctisResearchToolComponent>(TEXT("ResearchTools"));

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->GetNavAgentPropertiesRef().bCanCrouch = true;
	Movement->MaxWalkSpeed = 160.0f; // careful field walking; the core turns speed into footstep noise
	Movement->MaxWalkSpeedCrouched = 60.0f;
	Movement->bOrientRotationToMovement = false;
	bUseControllerRotationYaw = true;

	auto Action = [](const TCHAR* Path) { return TSoftObjectPtr<UInputAction>(FSoftObjectPath(Path)); };
	MoveAction = Action(TEXT("/Game/Input/IA_Move.IA_Move"));
	LookAction = Action(TEXT("/Game/Input/IA_Look.IA_Look"));
	CrouchAction = Action(TEXT("/Game/Input/IA_Crouch.IA_Crouch"));
	TalkAction = Action(TEXT("/Game/Input/IA_Talk.IA_Talk"));
	HeadlampAction = Action(TEXT("/Game/Input/IA_Headlamp.IA_Headlamp"));
	InteractAction = Action(TEXT("/Game/Input/IA_Interact.IA_Interact"));
	UseToolAction = Action(TEXT("/Game/Input/IA_UseTool.IA_UseTool"));
	NextToolAction = Action(TEXT("/Game/Input/IA_NextTool.IA_NextTool"));
	ZoomAction = Action(TEXT("/Game/Input/IA_Zoom.IA_Zoom"));
}

void ANoctisResearcherCharacter::BeginPlay()
{
	Super::BeginPlay();
}

void ANoctisResearcherCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	const APlayerController* PC = Cast<APlayerController>(GetController());
	UEnhancedInputLocalPlayerSubsystem* Input = PC ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
	if (!Input)
	{
		return;
	}
	const UNoctisSettings& Settings = UNoctisSettings::Get();
	if (UInputMappingContext* VehicleContext = Settings.VehicleContext.LoadSynchronous())
	{
		Input->RemoveMappingContext(VehicleContext);
	}
	if (UInputMappingContext* DroneContext = Settings.DroneContext.LoadSynchronous())
	{
		Input->RemoveMappingContext(DroneContext);
	}
	if (UInputMappingContext* Context = Settings.ResearcherContext.LoadSynchronous())
	{
		Input->AddMappingContext(Context, 0);
	}
	if (UInputMappingContext* Context = Settings.ToolsContext.LoadSynchronous())
	{
		Input->AddMappingContext(Context, 1);
	}
}

void ANoctisResearcherCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		return;
	}
	auto Bind = [this, Input](const TSoftObjectPtr<UInputAction>& Soft, ETriggerEvent Event, void (ANoctisResearcherCharacter::*Handler)(const FInputActionValue&)) {
		if (UInputAction* Action = Soft.LoadSynchronous())
		{
			Input->BindAction(Action, Event, this, Handler);
		}
	};
	Bind(MoveAction, ETriggerEvent::Triggered, &ANoctisResearcherCharacter::Move);
	Bind(LookAction, ETriggerEvent::Triggered, &ANoctisResearcherCharacter::Look);
	Bind(CrouchAction, ETriggerEvent::Started, &ANoctisResearcherCharacter::CrouchPressed);
	Bind(TalkAction, ETriggerEvent::Started, &ANoctisResearcherCharacter::TalkChanged);
	Bind(TalkAction, ETriggerEvent::Completed, &ANoctisResearcherCharacter::TalkChanged);
	Bind(HeadlampAction, ETriggerEvent::Started, &ANoctisResearcherCharacter::HeadlampPressed);
	Bind(InteractAction, ETriggerEvent::Started, &ANoctisResearcherCharacter::InteractPressed);
	Bind(UseToolAction, ETriggerEvent::Started, &ANoctisResearcherCharacter::UseToolPressed);
	Bind(NextToolAction, ETriggerEvent::Started, &ANoctisResearcherCharacter::NextToolPressed);
	Bind(ZoomAction, ETriggerEvent::Triggered, &ANoctisResearcherCharacter::ZoomChanged);
}

void ANoctisResearcherCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsPlayerControlled())
	{
		return; // in the vehicle: the core seats the researcher in the rover itself
	}
	if (UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(this))
	{
		Sub->SubmitResearcher(Camera->GetComponentLocation(), static_cast<float>(GetControlRotation().Yaw), GetVelocity(), bIsCrouched, bTalking, bHeadlampOn);
	}
}

void ANoctisResearcherCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	const FRotator Yaw(0.0, GetControlRotation().Yaw, 0.0);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Axis.X);
}

void ANoctisResearcherCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	const float Scale = Tools ? Tools->GetLookSensitivityScale() : 1.0f; // slower aiming through binoculars / long lenses
	AddControllerYawInput(Axis.X * Scale);
	AddControllerPitchInput(Axis.Y * Scale);
}

void ANoctisResearcherCharacter::CrouchPressed(const FInputActionValue& Value)
{
	if (bIsCrouched)
	{
		UnCrouch();
	}
	else
	{
		Crouch();
	}
}

void ANoctisResearcherCharacter::TalkChanged(const FInputActionValue& Value)
{
	bTalking = Value.Get<bool>();
}

void ANoctisResearcherCharacter::HeadlampPressed(const FInputActionValue& Value)
{
	ToggleHeadlamp();
}

void ANoctisResearcherCharacter::ToggleHeadlamp()
{
	bHeadlampOn = !bHeadlampOn;
	Headlamp->SetVisibility(bHeadlampOn);
}

void ANoctisResearcherCharacter::InteractPressed(const FInputActionValue& Value)
{
	TryEnterVehicle();
}

void ANoctisResearcherCharacter::UseToolPressed(const FInputActionValue& Value)
{
	if (Tools)
	{
		Tools->UseCurrentTool();
	}
}

void ANoctisResearcherCharacter::NextToolPressed(const FInputActionValue& Value)
{
	if (Tools)
	{
		Tools->CycleTool(1);
	}
}

void ANoctisResearcherCharacter::ZoomChanged(const FInputActionValue& Value)
{
	if (Tools)
	{
		Tools->AdjustZoom(Value.Get<float>());
	}
}

bool ANoctisResearcherCharacter::TryEnterVehicle()
{
	ANoctisResearchVehiclePawn* Best = nullptr;
	double BestDist = double(VehicleReachCm);
	for (TActorIterator<ANoctisResearchVehiclePawn> It(GetWorld()); It; ++It)
	{
		const double Dist = FVector::Dist(It->GetActorLocation(), GetActorLocation());
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Best = *It;
		}
	}
	return Best ? Best->EnterFrom(this) : false;
}
