// ANoctisResearcherCharacter — the walking field scientist.
// Authority: Unreal (CharacterMovement, collisions with landscape and props). Every frame the eye position, heading,
// velocity and posture are submitted to the core, which turns them into an Emitter (footstep noise by substrate,
// voice, scent, headlamp) that every simulated animal perceives through its own senses.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"

#include "NoctisResearcherCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UNoctisResearchToolComponent;
class USpotLightComponent;
struct FInputActionValue;

UCLASS(Blueprintable)
class NOCTIS_API ANoctisResearcherCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ANoctisResearcherCharacter();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;

	UFUNCTION(BlueprintCallable, Category = "Noctis")
	void SetTalking(bool bInTalking) { bTalking = bInTalking; }

	UFUNCTION(BlueprintCallable, Category = "Noctis")
	void ToggleHeadlamp();

	/** Enters the nearest research vehicle within reach (possesses it). */
	UFUNCTION(BlueprintCallable, Category = "Noctis")
	bool TryEnterVehicle();

	UNoctisResearchToolComponent* GetTools() const { return Tools; }
	UCameraComponent* GetCamera() const { return Camera; }

	// Input actions (created by Content/Python/setup_project.py in /Game/Input).
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> CrouchAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> TalkAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> HeadlampAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> UseToolAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> NextToolAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> ZoomAction;

	/** Reach (cm) to enter a vehicle. */
	UPROPERTY(EditAnywhere, Category = "Noctis")
	float VehicleReachCm = 450.0f;

protected:
	virtual void BeginPlay() override;

private:
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void CrouchPressed(const FInputActionValue& Value);
	void TalkChanged(const FInputActionValue& Value);
	void HeadlampPressed(const FInputActionValue& Value);
	void InteractPressed(const FInputActionValue& Value);
	void UseToolPressed(const FInputActionValue& Value);
	void NextToolPressed(const FInputActionValue& Value);
	void ZoomChanged(const FInputActionValue& Value);

	UPROPERTY(VisibleAnywhere, Category = "Noctis")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "Noctis")
	TObjectPtr<USpotLightComponent> Headlamp;

	UPROPERTY(VisibleAnywhere, Category = "Noctis")
	TObjectPtr<UNoctisResearchToolComponent> Tools;

	bool bTalking = false;
	bool bHeadlampOn = false;
};
