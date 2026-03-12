#pragma once

#include <cstdint>
#include <string>
#include <vector>

/** Install hooks on vulkan-1.dll exports (vkGetInstanceProcAddr, vkCreateDevice, vkCreateSwapchainKHR, vkQueuePresentKHR,
 *  vkBeginCommandBuffer, vkSetLatencyMarkerNV). No vkGetDeviceProcAddr; all hooks are from GetProcAddress(module, name).
 *  Logging and stats only; no Reflex injection. Missing exports (e.g. vkSetLatencyMarkerNV) are skipped.
 *  Called from LoadLibrary detour when vulkan-1.dll is loaded, or when user enables the setting and the loader is already loaded.
 */
bool InstallVulkanLoaderHooks(void* vulkan1_module);

/** Returns true if Vulkan loader hooks were successfully installed on vulkan-1.dll. */
bool AreVulkanLoaderHooksInstalled();

/** Debug state for VK_NV_low_latency2 path. All params nullable.
 *  out_intercept_count is unused (always 0; we no longer hook vkGetDeviceProcAddr). */
void GetVulkanLoaderDebugState(uint64_t* out_marker_count,
                               int* out_last_marker_type,
                               uint64_t* out_last_present_id,
                               uint64_t* out_intercept_count = nullptr);

/** Call counts for each Vulkan loader detour (incremented on every entry). All params nullable. */
void GetVulkanLoaderCallCounts(uint64_t* out_vkGetInstanceProcAddr,
                               uint64_t* out_vkGetDeviceProcAddr,
                               uint64_t* out_vkCreateDevice,
                               uint64_t* out_vkSetLatencyMarkerNV);

/** Copy of enabled device extension names from last vkCreateDevice (empty if not yet called or hooks not installed). */
void GetVulkanEnabledExtensions(std::vector<std::string>& out);

/** True if vkCreateDevice_Detour has been entered at least once (hooks installed and game called vkCreateDevice). */
bool HasVulkanCreateDeviceBeenCalled();

/** Injected Reflex debug state for Vulkan tab (injection removed; fields always zero/false for compatibility). All params nullable. */
struct VulkanInjectedReflexDebugState {
    bool enabled;           /**< vulkan_injected_reflex_enabled setting. */
    bool loader_hooks_on;   /**< Vulkan loader hooks installed. */
    bool nvll_loaded;       /**< NvLowLatencyVk.dll loaded (injection skipped when true). */
    bool injecting;        /**< Actually injecting this frame (enabled + hooks + !nvll + device/swapchain/sem ready). */
    uint64_t present_id;   /**< Last present ID used (game or our counter). */
    uint64_t markers_injected;   /**< Total markers we injected (present path + BeginCommandBuffer). */
    uint64_t sleep_calls;  /**< vkLatencySleepNV calls we issued. */
    uint64_t swapchain_latency_creates; /**< Swapchains created with latency mode. */
    bool has_device;       /**< We have a tracked VkDevice. */
    bool has_swapchain;     /**< We have a tracked VkSwapchainKHR. */
    bool has_semaphore;    /**< We have a timeline semaphore for sleep. */
    bool procs_resolved;   /**< vkSetLatencySleepModeNV etc. resolved. */
};
void GetVulkanInjectedReflexDebugState(VulkanInjectedReflexDebugState* out);
