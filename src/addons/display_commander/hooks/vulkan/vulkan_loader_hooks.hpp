#pragma once

#include <cstdint>
#include <string>
#include <vector>

/** Install hooks on vulkan-1.dll (vkGetInstanceProcAddr, vkGetDeviceProcAddr) so we can wrap vkCreateDevice
 *  (capture enabled extensions) and vkSetLatencyMarkerNV (VK_NV_low_latency2).
 *  Called from LoadLibrary detour when vulkan-1.dll is loaded, or when user enables the setting and the loader is already loaded.
 */
bool InstallVulkanLoaderHooks(void* vulkan1_module);

/** Returns true if vulkan-1 vkGetDeviceProcAddr hook is installed (VK_NV_low_latency2 wrapper active when game requests it). */
bool AreVulkanLoaderHooksInstalled();

/** Debug state for VK_NV_low_latency2 path. All params nullable.
 *  out_intercept_count = number of times vkGetDeviceProcAddr returned our vkSetLatencyMarkerNV detour. */
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

/** Injected Reflex (VK_NV_low_latency2) debug state for Vulkan tab. All params nullable. */
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
