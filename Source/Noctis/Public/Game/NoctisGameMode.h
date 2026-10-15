// Default game mode: the researcher on foot, a HUD widget with the field tools.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "NoctisGameMode.generated.h"

class UUserWidget;

UCLASS()
class NOCTIS_API ANoctisGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ANoctisGameMode();

	virtual void BeginPlay() override;

	/** Optional HUD (field journal shortcuts, clock, tool state), e.g. WBP_NoctisHUD created by setup_project.py. */
	UPROPERTY(EditAnywhere, Category = "Noctis")
	TSoftClassPtr<UUserWidget> HudWidgetClass;
};
