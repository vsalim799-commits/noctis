// ANoctisResearchVehiclePawn — the 6x6 research rover with its mobile lab.
//
// Authority model (choose per instance with Authority):
//   * UnrealChaos (default, interactive play): Chaos Vehicles integrates motion and collisions. Each frame the pawn
//     sends its transform, speed, controls and wheel contact points to the core (ResearchSystem::setExternalVehicle):
//     the core books energy (battery, generator fuel) and noise (the rover is an Emitter animals hear), and returns
//     Bekker terramechanics per wheel (sinkage, rolling resistance, traction on wet clay/sand...) which the pawn applies
//     to Chaos as friction multipliers and a rolling-resistance force. Movement is responsive; soil physics is the core's.
//   * CoreTerramechanics (headless parity, cinematics, tests): the core's VehicleSystem integrates the controls and
//     the pawn is kinematic, following the published state. Identical to the headless runner.
#pragma once

#include "CoreMinimal.h"
#include "WheeledVehiclePawn.h"

#include "Noctis/Research/FieldGear.h"

#include "NoctisResearchVehiclePawn.generated.h"

class ANoctisDronePawn;
class ANoctisResearcherCharacter;
class UInputAction;
struct FInputActionValue;

UENUM(BlueprintType)
enum class ENoctisVehicleAuthority : uint8
{
	UnrealChaos,
	CoreTerramechanics
};

UCLASS(Blueprintable)
class NOCTIS_API ANoctisResearchVehiclePawn : public AWheeledVehiclePawn
{
	GENERATED_BODY()

public:
	ANoctisResearchVehiclePawn(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;

	/** The researcher climbs in: possession switches to the rover and the core seats the researcher. */
	bool EnterFrom(ANoctisResearcherCharacter* Researcher);

	UFUNCTION(BlueprintCallable, Category = "Noctis")
	void ExitVehicle();

	UFUNCTION(BlueprintCallable, Category = "Noctis")
	void LaunchAndPilotDrone();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctis")
	ENoctisVehicleAuthority Authority = ENoctisVehicleAuthority::UnrealChaos;

	/** Wheel contact offsets in actor space (cm), front to back, left then right. Matches a 3.8 m wheelbase 6x6. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctis")
	TArray<FVector> WheelOffsetsCm;

	/** Height of the actor origin above the ground contact (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctis")
	float GroundOffsetCm = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctis")
	bool bApplyTerramechanics = true;

	UPROPERTY(EditAnywhere, Category = "Noctis")
	TSubclassOf<ANoctisDronePawn> DroneClass;

	// HUD values from the core (energy, soil).
	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float BatteryKWh = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float FuelL = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float WheelSinkageM = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	bool bStuck = false;

	UPROPERTY(BlueprintReadOnly, Category = "Noctis")
	float NoiseDb = 0.0f;

	// Input (created by setup_project.py).
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> ThrottleAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> BrakeAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> SteerAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> EngineAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> GeneratorAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> LightsAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> ExitAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> DroneAction;

private:
	void Throttle(const FInputActionValue& Value);
	void Brake(const FInputActionValue& Value);
	void Steer(const FInputActionValue& Value);
	void ToggleEngine(const FInputActionValue& Value);
	void ToggleGenerator(const FInputActionValue& Value);
	void ToggleLights(const FInputActionValue& Value);
	void ExitPressed(const FInputActionValue& Value);
	void DronePressed(const FInputActionValue& Value);
	void ApplyCoreFeedback(float DeltaSeconds);

	noctis::VehicleControls Controls;
	TWeakObjectPtr<ANoctisResearcherCharacter> Driver;

	UPROPERTY(Transient)
	TObjectPtr<ANoctisDronePawn> Drone;
};
