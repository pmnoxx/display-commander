#pragma once

#include <Windows.h>
#include <string>

// Load ReShade DLL (ReShade64.dll or ReShade32.dll) from the game directory
// Returns the module handle if successful, nullptr otherwise
HMODULE LoadReShadeDll();

