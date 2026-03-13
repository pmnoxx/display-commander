#pragma once

#include <windows.h>

// Install hooks on the game's/original dbghelp.dll when the module is loaded (e.g. from OnModuleLoaded).
// Intercepts StackWalk64 / StackWalkEx and symbol APIs to log stack traces from any thread.
bool InstallDbgHelpOriginalHooks(HMODULE dbghelp_module);

