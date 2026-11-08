// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

// Proxy exports so a Display Commander dxgi.dll looks like ReShade to tools that
// require ReShadeRegisterAddon/UnregisterAddon on the same image (e.g. MFG Unlocker).
// Implementations forward to the real ReShade module when present.

#include <cstdint>

extern "C" bool ReShadeRegisterAddon(void* module, uint32_t api_version);
extern "C" void ReShadeUnregisterAddon(void* module);
