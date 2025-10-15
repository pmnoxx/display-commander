#pragma once

#include <cstdint>

// Forward declarations for NGX types
struct NVSDK_NGX_Parameter;
enum NVSDK_NGX_Result;

// NGX hook functions
bool InstallNGXHooks();

// Internal vtable hooking function
bool HookNGXParameterVTable(NVSDK_NGX_Parameter* Params);

// Get NGX hook statistics
uint64_t GetNGXHookCount(int event_type);
uint64_t GetTotalNGXHookCount();
