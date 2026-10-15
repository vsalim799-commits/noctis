#include "Save/NoctisSaveGame.h"

#include "NoctisLog.h"
#include "NoctisSettings.h"
#include "Research/NoctisResearchVehiclePawn.h"
#include "Sim/NoctisWorldSubsystem.h"

#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Compression.h"

bool UNoctisSaveLibrary::SaveExpedition(const UObject* WorldContextObject, const FString& SlotName)
{
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(WorldContextObject);
	if (!Sub || !Sub->IsSimulationReady())
	{
		return false;
	}
	UNoctisSaveGame* Save = Cast<UNoctisSaveGame>(UGameplayStatics::CreateSaveGameObject(UNoctisSaveGame::StaticClass()));
	if (!Save)
	{
		return false;
	}
	const UNoctisSettings& Settings = UNoctisSettings::Get();
	Save->EnvironmentId = Settings.EnvironmentId;
	Save->ProfileId = Settings.ProfileId;
	Save->Seed = Settings.Seed;
	Save->TimeScale = Sub->GetTimeScale();
	Save->SavedAt = FDateTime::UtcNow();

	TArray<uint8> Raw;
	Sub->WithSimulation([&](noctis::WorldSimulation& Sim) {
		Save->SimulatedSeconds = Sim.clock().seconds();
		Save->ClockText = NoctisUnreal::ToFString(Sim.clock().formatted());
#if NOCTIS_CORE_HAS_BLOB_SAVE
		std::vector<noctis::u8> Blob;
		if (Sim.saveToBlob(Blob))
		{
			Raw.Append(Blob.data(), static_cast<int32>(Blob.size()));
		}
#endif
	});
	if (Raw.Num() > 0)
	{
		// Compress the core blob (biomass grids, creatures, archives can reach tens of MB).
		int32 CompressedSize = FCompression::CompressMemoryBound(NAME_Zlib, Raw.Num());
		Save->CoreBlob.SetNumUninitialized(CompressedSize);
		if (FCompression::CompressMemory(NAME_Zlib, Save->CoreBlob.GetData(), CompressedSize, Raw.GetData(), Raw.Num()))
		{
			Save->CoreBlob.SetNum(CompressedSize);
			Save->CoreBlobUncompressedSize = Raw.Num();
			Save->CoreBlobFormat = FName(TEXT("noctis-blob-1+zlib"));
		}
		else
		{
			Save->CoreBlob.Reset();
		}
	}

	if (const APlayerController* PC = UGameplayStatics::GetPlayerController(WorldContextObject, 0))
	{
		if (const APawn* Pawn = PC->GetPawn())
		{
			Save->PlayerTransform = Pawn->GetActorTransform();
			Save->bPlayerInVehicle = Pawn->IsA<ANoctisResearchVehiclePawn>();
		}
		Save->ControlRotation = PC->GetControlRotation();
	}
	if (UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr)
	{
		for (TActorIterator<ANoctisResearchVehiclePawn> It(World); It; ++It)
		{
			Save->VehicleTransform = It->GetActorTransform();
			break;
		}
	}
	return UGameplayStatics::SaveGameToSlot(Save, SlotName, 0);
}

bool UNoctisSaveLibrary::LoadExpedition(const UObject* WorldContextObject, const FString& SlotName, bool& bIsApproximate)
{
	bIsApproximate = true;
	UNoctisWorldSubsystem* Sub = UNoctisWorldSubsystem::Get(WorldContextObject);
	UNoctisSaveGame* Save = Cast<UNoctisSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!Sub || !Sub->IsSimulationReady() || !Save)
	{
		return false;
	}
	if (Save->EnvironmentId != UNoctisSettings::Get().EnvironmentId || Save->Seed != UNoctisSettings::Get().Seed)
	{
		// The running valley was built from the project settings; a different recipe needs a settings change + reload.
		UE_LOG(LogNoctis, Warning, TEXT("Sauvegarde '%s' : environnement %s / graine %lld différents du monde courant (%s / %lld)."), *SlotName, *Save->EnvironmentId,
			   Save->Seed, *UNoctisSettings::Get().EnvironmentId, UNoctisSettings::Get().Seed);
		return false;
	}

	bool bRestoredCore = false;
#if NOCTIS_CORE_HAS_BLOB_SAVE
	if (Save->CoreBlob.Num() > 0 && Save->CoreBlobUncompressedSize > 0)
	{
		std::vector<noctis::u8> Blob(static_cast<size_t>(Save->CoreBlobUncompressedSize));
		if (FCompression::UncompressMemory(NAME_Zlib, Blob.data(), Save->CoreBlobUncompressedSize, Save->CoreBlob.GetData(), Save->CoreBlob.Num()))
		{
			Sub->WithSimulation([&](noctis::WorldSimulation& Sim) { bRestoredCore = Sim.loadFromBlob(Blob); });
		}
	}
#endif
	if (!bRestoredCore)
	{
		// Recipe restore: same seed -> same valley; advance the clock to the saved time at coarse steps (bounded).
		const double Target = Save->SimulatedSeconds;
		Sub->WithSimulation([Target](noctis::WorldSimulation& Sim) {
			const double Missing = Target - Sim.clock().seconds();
			if (Missing <= 0.0)
			{
				return;
			}
			const double Replay = FMath::Min(Missing, UNoctisSaveLibrary::MaxFastForwardHours * 3600.0);
			Sim.setTimeScale(1000.0f); // coarse kinematics, no full-detail LOD
			Sim.advance(Replay, 2.0);
			Sim.setTimeScale(1.0f);
			if (Missing > Replay)
			{
				Sim.clock().setSeconds(Target); // beyond the replay budget the clock jumps (daily updates catch up once)
			}
		});
	}
	bIsApproximate = !bRestoredCore;
	if (FNoctisSimulationRunner* Runner = Sub->GetRunner())
	{
		Runner->ForceReapplyTimeScale(); // the fast-forward changed the core's time scale behind the runner's back
	}
	Sub->SetTimeScale(Save->TimeScale);

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(WorldContextObject, 0))
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->TeleportTo(Save->PlayerTransform.GetLocation(), Save->PlayerTransform.Rotator());
		}
		PC->SetControlRotation(Save->ControlRotation);
	}
	return true;
}
