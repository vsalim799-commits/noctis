#include "NoctisLog.h"

#include "Modules/ModuleManager.h"

#include "Debug/NoctisGameplayDebugger.h"
#if NOCTIS_WITH_GAMEPLAY_DEBUGGER
#include "Debug/GameplayDebuggerCategory_Noctis.h"
#include "GameplayDebugger.h"
#endif

DEFINE_LOG_CATEGORY(LogNoctis);

class FNoctisGameModule final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
#if NOCTIS_WITH_GAMEPLAY_DEBUGGER
		IGameplayDebugger& Debugger = IGameplayDebugger::Get();
		Debugger.RegisterCategory(TEXT("Noctis"), IGameplayDebugger::FOnGetCategory::CreateStatic(&FGameplayDebuggerCategory_Noctis::MakeInstance),
								  EGameplayDebuggerCategoryState::EnabledInGameAndSimulate, 5);
		Debugger.NotifyCategoriesChanged();
#endif
	}

	virtual void ShutdownModule() override
	{
#if NOCTIS_WITH_GAMEPLAY_DEBUGGER
		if (IGameplayDebugger::IsAvailable())
		{
			IGameplayDebugger& Debugger = IGameplayDebugger::Get();
			Debugger.UnregisterCategory(TEXT("Noctis"));
			Debugger.NotifyCategoriesChanged();
		}
#endif
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FNoctisGameModule, Noctis, "Noctis");
