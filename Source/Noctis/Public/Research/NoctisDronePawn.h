// ANoctisDronePawn — the survey quadcopter.
// Authority: the core (DroneSystem: battery from hover power, wind limit, radio range and line of sight, autopilot
// modes, rotor noise that animals hear). The pawn displays the published drone state and, while piloted, sends
// manual velocity/altitude or mode commands (orbit the point under the crosshair, follow the animal under the
// crosshair, return home). Its camera is the drone camera for observations.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"

#include "NoctisTypes.h"

#include "NoctisDronePawn.generated.h"

class UAudioComponent;
class UCameraComponent;
class UInputAction;
class UStaticMeshComponent;
struct FInputActionValue;

UCLASS(Blueprintable)
class NOCTIS_API ANoctisDronePawn : public APawn
{
	GENERATED_BODY()

public:
	ANoctisDronePawn();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;

	void SetHomeVehicle(APawn* Vehicle) { HomeVehicle = Vehicle; }

	/** Launches from the rover (if docked) and takes control; ReturnPawn is re-possessed when piloting ends. */
	void LaunchAndPilot(AController* PilotController, APawn* ReturnPawn);

	UFUNCTION(BlueprintCallable, Category = "Noctis")
	void StopPiloting();

	UFUNCTION(BlueprintCallable, Category = "Noctis")
	void OrbitCrosshairTarget(float RadiusM = 70.0f, float AltitudeM = 35.0f);

	UFUNCTION(BlueprintCallable, Category = "Noctis")
	void FollowCrosshairTarget(float AltitudeM = 40.0f);

	UFUNCTION(BlueprintCallable, Category = "Noctis")
	void ReturnHome();

	UFUNCTION(BlueprintCallable, Category = "Noctis")
	void ObserveFromDrone();

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	ENoctisDroneMode Mode = ENoctisDroneMode::Docked;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float BatteryWh = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float AltitudeAglM = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float Signal = 1.0f;

	/** Core warning (wind, rain, battery, signal), in French. */
	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	FString Warning;

	UPROPERTY(EditAnywhere, Category = "Noctis")
	float MaxManualSpeedMs = 12.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> AltitudeAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> OrbitAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> FollowAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> ReturnAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> ObserveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> ExitAction;

private:
	void Move(const FInputActionValue& Value);
	void Altitude(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void OrbitPressed(const FInputActionValue& Value);
	void FollowPressed(const FInputActionValue& Value);
	void ReturnPressed(const FInputActionValue& Value);
	void ObservePressed(const FInputActionValue& Value);
	void ExitPressed(const FInputActionValue& Value);
	bool TraceCrosshair(FHitResult& OutHit, ECollisionChannel Channel) const;

	UPROPERTY(VisibleAnywhere, Category = "Noctis")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Noctis")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Noctis")
	TObjectPtr<USceneComponent> Gimbal;

	UPROPERTY(VisibleAnywhere, Category = "Noctis")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "Noctis")
	TObjectPtr<UAudioComponent> RotorAudio;

	TWeakObjectPtr<APawn> HomeVehicle;
	TWeakObjectPtr<APawn> ReturnTo;
	FVector2D ManualInput = FVector2D::ZeroVector;
	float TargetAltitudeM = 40.0f;
	float GimbalPitchDeg = -30.0f;
	bool bPiloted = false;
};
