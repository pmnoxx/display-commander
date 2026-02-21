#pragma once

#include <cstdint>
#include <windows.h>

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

/** Uninstall NvLowLatencyVk hooks. */
void UninstallNvLowLatencyVkHooks();

/** Returns true if NvLowLatencyVk hooks are currently installed. */
bool AreNvLowLatencyVkHooksInstalled();

/** Debug state for Vulkan tab: marker call count, last marker type (0-8), last frame ID. */
void GetNvLowLatencyVkDebugState(uint64_t* out_marker_count, int* out_last_marker_type, uint64_t* out_last_frame_id);

/** Per-detour call counts for Vulkan tab debug. All params nullable. */
void GetNvLowLatencyVkDetourCallCounts(uint64_t* out_init_count,
                                      uint64_t* out_set_latency_marker_count,
                                      uint64_t* out_set_sleep_mode_count,
                                      uint64_t* out_sleep_count);

/** Last params actually sent to NvLL_VK_SetSleepMode_Original (from detour or SIMULATION_START re-apply). */
void GetNvLowLatencyVkLastAppliedSleepModeParams(NvLLVkSleepModeParamsView* out);

/** Last params the game tried to set via NvLL_VK_SetSleepMode (before any override). */
void GetNvLowLatencyVkGameSleepModeParams(NvLLVkSleepModeParamsView* out);
