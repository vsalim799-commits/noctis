#include "Game/NoctisGameMode.h"

#include "Research/NoctisResearcherCharacter.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

ANoctisGameMode::ANoctisGameMode()
{
	DefaultPawnClass = ANoctisResearcherCharacter::StaticClass();
	HudWidgetClass = TSoftClassPtr<UUserWidget>(FSoftObjectPath(TEXT("/Game/UI/WBP_NoctisHUD.WBP_NoctisHUD_C")));
}

void ANoctisGameMode::BeginPlay()
{
	Super::BeginPlay();
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (UClass* WidgetClass = HudWidgetClass.LoadSynchronous())
	{
		if (UUserWidget* Hud = CreateWidget<UUserWidget>(PC, WidgetClass))
		{
			Hud->AddToViewport();
		}
	}
}
