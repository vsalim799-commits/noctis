// Gameplay Debugger category "Noctis" (apostrophe key, then the category's number): the hidden state of one animal —
// what the player never sees in the game. Debug actor = the selected ANoctisCreatureActor, or the individual nearest to
// the centre of the view. Shows physiology, behaviour scores, awareness of every perceived entity (stage, relation,
// evidence, threat), injuries, group and LOD; draws the look target and the believed positions of what it perceives.
#pragma once

#include "Debug/NoctisGameplayDebugger.h"

#if NOCTIS_WITH_GAMEPLAY_DEBUGGER

#include "CoreMinimal.h"
#include "GameplayDebuggerCategory.h"

class APlayerController;

class FGameplayDebuggerCategory_Noctis : public FGameplayDebuggerCategory
{
public:
	FGameplayDebuggerCategory_Noctis();

	virtual void CollectData(APlayerController* OwnerPC, AActor* DebugActor) override;

	static TSharedRef<FGameplayDebuggerCategory> MakeInstance();
};

#endif // NOCTIS_WITH_GAMEPLAY_DEBUGGER
