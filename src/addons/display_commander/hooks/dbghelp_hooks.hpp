#pragma once

#include <windows.h>

// Install hooks on dbghelp.dll when the module is loaded (e.g. from OnModuleLoaded).
// Intercepts StackWalk64 and logs stack traces from any thread that queries the stack.
bool InstallDbgHelpHooks(HMODULE dbghelp_module);
