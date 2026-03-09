#pragma once

// Source Code <Display Commander>
#include "../utils/dlss_fix_api_state.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

// Forward declarations for NGX types
struct NVSDK_NGX_Parameter;

// NGX hook functions
bool InstallNGXHooks(HMODULE ngx_module);
void CleanupNGXHooks();

// Internal vtable hooking function (context is logged when installing hooks, e.g. "D3D12_CreateFeature")
bool HookNGXParameterVTable(NVSDK_NGX_Parameter* Params, const char* context);

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

// True if HookNGXParameterVTable was called at least once (NGX Parameter vtable hooks are active)
bool AreNGXParameterVTableHooksInstalled();

// DLSS-fix: fill entries for the 14 NGX APIs that need proxy→native conversion (hooked + call count)
void GetDLSSFixNGXAPIEntries(std::vector<display_commander::DLSSFixAPIEntry>& out);
