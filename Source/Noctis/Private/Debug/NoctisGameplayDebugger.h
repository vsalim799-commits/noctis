// Single switch for the Gameplay Debugger integration across engine versions
// (5.0: WITH_GAMEPLAY_DEBUGGER; 5.1+: the category/menu code is guarded by WITH_GAMEPLAY_DEBUGGER_MENU).
#pragma once

#if defined(WITH_GAMEPLAY_DEBUGGER_MENU)
#define NOCTIS_WITH_GAMEPLAY_DEBUGGER WITH_GAMEPLAY_DEBUGGER_MENU
#elif defined(WITH_GAMEPLAY_DEBUGGER)
#define NOCTIS_WITH_GAMEPLAY_DEBUGGER WITH_GAMEPLAY_DEBUGGER
#else
#define NOCTIS_WITH_GAMEPLAY_DEBUGGER 0
#endif
