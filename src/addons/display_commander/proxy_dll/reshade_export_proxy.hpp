// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

// Proxy exports of the full ReShade add-on C API so a Display Commander proxy (e.g. dxgi.dll) can stand in
// for ReShade: tools that require ReShadeRegisterAddon/UnregisterAddon on the same image (MFG Unlocker), and
// add-ons using upstream reshade.hpp that bind to the first module exporting them (RenoDX).
// Every function forwards to the real ReShade module when present (see reshade_export_proxy.cpp).

#include <cstdint>

extern "C" bool ReShadeRegisterAddon(void* module, uint32_t api_version);
extern "C" void ReShadeUnregisterAddon(void* module);
