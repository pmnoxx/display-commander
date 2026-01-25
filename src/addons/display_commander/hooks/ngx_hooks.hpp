#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

// Forward declarations for NGX types
struct NVSDK_NGX_Parameter;

// NGX hook functions
bool InstallNGXHooks(HMODULE ngx_module);
void CleanupNGXHooks();

// Internal vtable hooking function
bool HookNGXParameterVTable(NVSDK_NGX_Parameter* Params);

// Get NGX hook statistics
uint64_t GetNGXHookCount(int event_type);
uint64_t GetTotalNGXHookCount();

// Feature status checking functions
bool IsDLSSEnabled();
bool IsDLSSGEnabled();
bool IsRayReconstructionEnabled();
std::string GetEnabledFeaturesSummary();

// NGX preset management functions
void ResetNGXPresetInitialization();

// Force apply NGX parameter override via API call
bool ApplyNGXParameterOverride(const char* param_name, const char* param_type);
