// UNoctisResearchToolComponent — the researcher's instruments, all calling noctis::ResearchSystem.
// Every action is queued as a simulation command (it runs between steps on the simulation thread) and reports back
// on the game thread through the delegates below. Nothing is decided here: what the instrument resolves (species,
// features, distances, sound) is computed by the core from the physics of the scene.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "Research/NoctisResearchTypes.h"

#include "NoctisResearchToolComponent.generated.h"

class UCameraComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNoctisToolChanged, ENoctisTool, Tool);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNoctisObservationRecorded, int32, ObservationId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoctisPhotoTaken, int32, PhotoId, const FString&, ImageFile);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNoctisRecordingFinished, int32, RecordingId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoctisDevicePlaced, ENoctisTool, Tool, int32, DeviceId);

UCLASS(ClassGroup = (Noctis), meta = (BlueprintSpawnableComponent))
class NOCTIS_API UNoctisResearchToolComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNoctisResearchToolComponent();

	UFUNCTION(BlueprintCallable, Category = "Noctis|Tools")
	void SelectTool(ENoctisTool Tool);

	UFUNCTION(BlueprintCallable, Category = "Noctis|Tools")
	void CycleTool(int32 Direction);

	UFUNCTION(BlueprintPure, Category = "Noctis|Tools")
	ENoctisTool GetCurrentTool() const { return CurrentTool; }

	/** Primary action of the current tool (observe, shoot, start/stop recording, place a device). */
	UFUNCTION(BlueprintCallable, Category = "Noctis|Tools")
	void UseCurrentTool();

	/** Zoom input: focal length for the camera, magnification for binoculars (also narrows the camera FOV). */
	UFUNCTION(BlueprintCallable, Category = "Noctis|Tools")
	void AdjustZoom(float Delta);

	/** Starts / stops a continuous observation session (focal species optional) — the data hypotheses are tested on. */
	UFUNCTION(BlueprintCallable, Category = "Noctis|Tools")
	void StartObservationSession(FName FocalSpeciesId, bool bWithBinoculars);

	UFUNCTION(BlueprintCallable, Category = "Noctis|Tools")
	void StopObservationSession();

	UFUNCTION(BlueprintCallable, Category = "Noctis|Tools")
	void RetrieveDevice(int32 SensorId);

	float GetLookSensitivityScale() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis|Tools")
	FNoctisPhotoSettings PhotoSettings;

	/** Writes a real screenshot next to each photo record (Saved/NoctisPhotos/); the core only scores the photo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis|Tools")
	bool bCaptureScreenshots = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis|Tools")
	float BinocularMagnification = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis|Tools")
	float MicArrayRadiusM = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis|Tools")
	int32 MicArrayCount = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noctis|Tools")
	float PlacementReachCm = 300.0f;

	UPROPERTY(BlueprintAssignable, Category = "Noctis|Tools")
	FNoctisToolChanged OnToolChanged;

	UPROPERTY(BlueprintAssignable, Category = "Noctis|Tools")
	FNoctisObservationRecorded OnObservationRecorded;

	UPROPERTY(BlueprintAssignable, Category = "Noctis|Tools")
	FNoctisPhotoTaken OnPhotoTaken;

	UPROPERTY(BlueprintAssignable, Category = "Noctis|Tools")
	FNoctisRecordingFinished OnRecordingFinished;

	UPROPERTY(BlueprintAssignable, Category = "Noctis|Tools")
	FNoctisDevicePlaced OnDevicePlaced;

	UFUNCTION(BlueprintPure, Category = "Noctis|Tools")
	bool IsRecording() const { return RecordingHandle != 0; }

	UFUNCTION(BlueprintPure, Category = "Noctis|Tools")
	bool IsMicArrayDeployed() const { return MicArrayId != 0; }

protected:
	virtual void BeginPlay() override;

private:
	UCameraComponent* FindCamera() const;
	bool TracePlacement(FVector& OutLocation, float& OutYawDeg) const;
	void Observe(bool bBinoculars);
	void TakePhoto();
	void ToggleRecording();
	void ToggleMicArray();
	void PlaceSensor(ENoctisTool Tool);
	void ApplyOptics();

	ENoctisTool CurrentTool = ENoctisTool::Binoculars;
	float DefaultFov = 75.0f;
	uint32 RecordingHandle = 0;
	uint32 MicArrayId = 0;
	uint32 SessionId = 0;
};
