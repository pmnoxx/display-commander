#pragma once

#include <cstddef>
#include <cstdint>

#include <windows.h>

/** NvLowLatencyVk hook indices; use for stats arrays. Order matches the hook table in nvlowlatencyvk_hooks.cpp. */
enum class NvllVkHook : std::size_t {
    InitLowLatencyDevice = 0,
    SetLatencyMarker,
    SetSleepMode,
    Sleep,
    kNvllVkHookCount
};

/** Human-readable name for an NvLL VK hook (for UI). */
const char* GetNvllVkHookName(NvllVkHook hook);

/** Fill call counts for each NvLL VK hook. out_counts must have at least kNvllVkHookCount elements. */
void GetNvllVkHookCallCounts(uint64_t* out_counts, std::size_t count);

/** View struct for NvLL VK SetSleepMode params (for UI; no dependency on internal types). */
struct NvLLVkSleepModeParamsView {
    bool low_latency = false;
    bool boost = false;
    uint32_t minimum_interval_us = 0;
    bool has_value = false;
};

/** Install hooks on NvLowLatencyVk.dll when the given HMODULE is that DLL.
 *  Called from LoadLibrary detour when NvLowLatencyVk.dll is loaded, or when user enables the setting and DLL is already loaded.
 *  Returns true if hooks were installed (or already installed). */
bool InstallNvLowLatencyVkHooks(HMODULE nvll_module);

/** Returns true if NvLowLatencyVk hooks are currently installed. */
bool AreNvLowLatencyVkHooksInstalled();

/** Last NvLL_VK_SetLatencyMarker type (0-8) and frame ID for Vulkan tab. All params nullable. */
void GetNvLowLatencyVkLastMarkerState(int* out_last_marker_type, uint64_t* out_last_frame_id);

/** Number of NvLL VK latency marker types (0..8 = SIMULATION_START .. PC_LATENCY_PING). */
constexpr size_t kNvllVkMarkerTypeCount = 9;

/** Get call counts for reflex_marker_vk_nvll (NvLL_VK_SetLatencyMarker) per marker type.
 *  out_counts must hold at least kNvllVkMarkerTypeCount elements. */
void GetNvLowLatencyVkMarkerCountsByType(uint64_t* out_counts, size_t max_count);

/** Display name for marker type index (0..8). Returns "?" if index out of range. */
const char* GetNvLowLatencyVkMarkerTypeName(int index);

/** Last params actually sent to NvLL_VK_SetSleepMode_Original (from detour or SIMULATION_START re-apply). */
void GetNvLowLatencyVkLastAppliedSleepModeParams(NvLLVkSleepModeParamsView* out);

/** Last params the game tried to set via NvLL_VK_SetSleepMode (before any override). */
void GetNvLowLatencyVkGameSleepModeParams(NvLLVkSleepModeParamsView* out);
