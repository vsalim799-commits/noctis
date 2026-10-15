// UNoctisWorldSubsystem — one living valley per game/PIE world.
//
// Owns the FNoctisSimulationRunner (and therefore the noctis::WorldSimulation), boots it (optionally on a
// background task), drives it once per frame from FWorldDelegates::OnWorldPreActorTick (before any actor
// ticks, so every presentation actor of the frame sees the same freshly published snapshot), gathers the
// game-side inputs (researcher, vehicle, drone, camera focus) and exposes the published frames.
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tasks/Task.h"

#include "NoctisSpace.h"
#include "NoctisTypes.h"
#include "Sim/NoctisSimulationRunner.h"

#include "NoctisWorldSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNoctisSimulationReadySignature);
DECLARE_MULTICAST_DELEGATE_OneParam(FNoctisFramePublished, const FNoctisFrameOutput& /*Front*/);

UCLASS()
class NOCTIS_API UNoctisWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UNoctisWorldSubsystem* Get(const UObject* WorldContextObject);

	// USubsystem / UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// ---- State ---------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Noctis")
	bool IsSimulationReady() const { return bReady; }

	UFUNCTION(BlueprintCallable, Category = "Noctis")
	void SetTimeScale(float Scale);

	UFUNCTION(BlueprintPure, Category = "Noctis")
	float GetTimeScale() const;

	UFUNCTION(BlueprintCallable, Category = "Noctis")
	void SetSimulationPaused(bool bPaused);

	UFUNCTION(BlueprintPure, Category = "Noctis")
	bool IsSimulationPaused() const;

	/** "An 2, jour 143, 06:12" — the paleo-calendar of the environment (day length is a parameter). */
	UFUNCTION(BlueprintPure, Category = "Noctis")
	FString GetClockText() const;

	UFUNCTION(BlueprintPure, Category = "Noctis")
	float GetHourOfDay() const;

	UFUNCTION(BlueprintPure, Category = "Noctis")
	int32 GetAliveCount() const;

	/** Milliseconds of wall time of the last simulation job (steps + snapshot). */
	UFUNCTION(BlueprintPure, Category = "Noctis")
	float GetLastJobMilliseconds() const;

	/** Re-reads Data/ and rebuilds the world with the current settings (editor "Reload scientific data"). */
	UFUNCTION(BlueprintCallable, Category = "Noctis")
	void RequestReload();

	UPROPERTY(BlueprintAssignable, Category = "Noctis")
	FNoctisSimulationReadySignature OnSimulationReady;

	/** Broadcast on the game thread each time a new frame is published (before actors tick). */
	FNoctisFramePublished OnFramePublished;

	// ---- Frames (game thread) ---------------------------------------------------------------------

	const FNoctisFrameOutput* GetFrontFrame() const;
	const FNoctisFrameOutput* GetPreviousFrame() const;
	float GetInterpolationAlpha() const;
	const FNoctisSpace& GetSpace() const { return Space; }
	/** Terrain rectangle in the core frame (immutable after boot). */
	const noctis::Rect2& GetCoreBounds() const { return CoreBounds; }

	// ---- Species ------------------------------------------------------------------------------------

	const TArray<FNoctisSpeciesInfo>& GetSpecies() const { return Species; }
	const FNoctisSpeciesInfo* GetSpeciesInfo(int32 SpeciesIndex) const;
	int32 FindSpeciesIndex(FName SpeciesId) const;

	// ---- Inputs (merged during the frame, handed to the runner at the next pre-actor tick) ----------

	void SubmitResearcher(const FVector& EyeLocation, float YawDeg, const FVector& Velocity, bool bCrouched, bool bTalking, bool bHeadlamp);
	void SubmitVehicle(bool bExternal, const FVector& Location, float YawDeg, float SpeedCmPerS, const noctis::VehicleControls& Controls,
					   const TArray<FVector>& WheelContacts, float WheelLoadN);
	void SubmitDroneManual(const FVector& VelocityCmPerS, float AltitudeM);
	void SubmitFocus(const FVector& Location);

	// ---- Commands -------------------------------------------------------------------------------------

	/** Runs Work on the simulation between steps; OnGameThread is called on the game thread when it has run. */
	void EnqueueCommand(TUniqueFunction<void(noctis::WorldSimulation&)> Work, TUniqueFunction<void()> OnGameThread = nullptr);

	/** Synchronous exclusive access on the game thread (waits for the running job). Keep the functor short. */
	template <typename FuncType>
	bool WithSimulation(FuncType&& Fn)
	{
		if (!bReady || !Runner)
		{
			return false;
		}
		Runner->WithSimulation(Forward<FuncType>(Fn));
		return true;
	}

	FNoctisSimulationRunner* GetRunner() const { return Runner.Get(); }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void BeginBoot();
	void FinishBoot();
	void OnPreActorTick(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds);
	void CacheSpecies();
	void SpawnPresentationActors();
	static noctis::SimConfig MakeConfig();
	static FNoctisSimulationRunner::FParams MakeParams();

	TUniquePtr<FNoctisSimulationRunner> Runner;
	FNoctisSpace Space;
	noctis::Rect2 CoreBounds{};
	TArray<FNoctisSpeciesInfo> Species;
	TMap<FName, int32> SpeciesById;
	FNoctisFrameInput NextInput;
	FDelegateHandle PreActorTickHandle;
	UE::Tasks::FTask BootTask;
	FString BootError;
	bool bBooting = false;
	bool bBootSucceeded = false;
	bool bReady = false;
	bool bBegunPlay = false;
	bool bReloadRequested = false;
	uint64 LastBroadcastSequence = 0;
	float PendingTimeScale = 1.0f;
	bool bPendingPaused = false;
};
