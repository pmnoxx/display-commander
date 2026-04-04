#pragma once

// Source Code <Display Commander>

#include <cstdint>
#include <string>

#include <windows.h>

// Forward declarations for NGX types
struct NVSDK_NGX_Parameter;
enum class NGXCounterKind : int;

// NGX hook functions
bool InstallNGXHooks(HMODULE ngx_module);
void CleanupNGXHooks();

// Internal vtable hooking function (context is logged when installing hooks, e.g. "D3D12_CreateFeature")
bool HookNGXParameterVTable(NVSDK_NGX_Parameter* Params, const char* context);

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

/** Labels and values for debug NGX counter table (`NGXCounterKind` in globals.hpp). */
const char* GetNGXCounterKindLabel(NGXCounterKind kind);
uint32_t GetNGXCounterValue(NGXCounterKind kind);

/** Debug-only frame-gen multiplier override on D3D11/D3D12 EvaluateFeature: -1 = game default, 0–5 → 1x–6x. */
int GetDebugDLSSGMultiFrameCountOverride();
void SetDebugDLSSGMultiFrameCountOverride(int multiframe_count);

/** Debug-only frame-gen operating mode on the same EvaluateFeature path: -1 = game default, 0 = off, 1 = on, 2 = auto. */
int GetDebugDLSSGModeOverride();
void SetDebugDLSSGModeOverride(int mode);

/** Debug-only generated-frame interpolation flag on the same path: -1 = game default, 0 = off, 1 = on. */
int GetDebugDLSSGEnableInterpOverride();
void SetDebugDLSSGEnableInterpOverride(int enable_interp);
