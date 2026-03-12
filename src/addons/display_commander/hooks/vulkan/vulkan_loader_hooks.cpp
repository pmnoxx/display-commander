#include "vulkan_loader_hooks.hpp"
#include "../../globals.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../swapchain_events.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/srwlock_wrapper.hpp"
#include "../../utils/timing.hpp"
#include "../dxgi/dxgi_present_hooks.hpp"
#include "../hook_suppression_manager.hpp"

#include <MinHook.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <Windows.h>

#define VK_NO_PROTOTYPES 1
#include <vulkan/vulkan_core.h>

namespace {

// Vulkan non-dispatchable handles: 64-bit = pointer, 32-bit = uint64_t (see vulkan_core.h
// VK_DEFINE_NON_DISPATCHABLE_HANDLE). Use static_cast on 32-bit, reinterpret_cast on 64-bit.
template <typename Handle>
inline uint64_t VkHandleToU64(Handle h) {
#if (VK_USE_64_BIT_PTR_DEFINES == 1)
    return reinterpret_cast<uint64_t>(h);
#else
    return static_cast<uint64_t>(h);
#endif
}

template <typename Handle>
inline Handle U64ToVkHandle(uint64_t u) {
#if (VK_USE_64_BIT_PTR_DEFINES == 1)
    return reinterpret_cast<Handle>(u);
#else
    return static_cast<Handle>(u);
#endif
}

// Ensure handle types fit in uint64_t for atomic round-trip (64-bit: pointer; 32-bit: uint64_t per Vulkan spec).
static_assert(sizeof(uint64_t) == 8, "atomic handle storage is 64-bit");
static_assert(sizeof(VkSwapchainKHR) == 8, "VkSwapchainKHR must fit in uint64_t for round-trip");
static_assert(sizeof(VkSemaphore) == 8, "VkSemaphore must fit in uint64_t for round-trip");

using PFN_vkGetInstanceProcAddr_t = PFN_vkGetInstanceProcAddr;
using PFN_vkGetDeviceProcAddr_t = PFN_vkGetDeviceProcAddr;
using PFN_vkSetLatencyMarkerNV_t = PFN_vkSetLatencyMarkerNV;
using PFN_vkCreateDevice_t = PFN_vkCreateDevice;
using PFN_vkQueuePresentKHR_t = VkResult(VKAPI_CALL*)(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);
using PFN_vkCreateSwapchainKHR_t = PFN_vkCreateSwapchainKHR;
using PFN_vkBeginCommandBuffer_t = PFN_vkBeginCommandBuffer;
using PFN_vkSetLatencySleepModeNV_t = PFN_vkSetLatencySleepModeNV;
using PFN_vkLatencySleepNV_t = PFN_vkLatencySleepNV;
using PFN_vkCreateSemaphore_t = PFN_vkCreateSemaphore;
using PFN_vkGetSemaphoreCounterValue_t = PFN_vkGetSemaphoreCounterValue;
using PFN_vkWaitSemaphores_t = PFN_vkWaitSemaphores;

static PFN_vkGetInstanceProcAddr_t vkGetInstanceProcAddr_Original = nullptr;
static PFN_vkGetDeviceProcAddr_t vkGetDeviceProcAddr_Original = nullptr;
static PFN_vkCreateDevice_t g_real_vkCreateDevice = nullptr;

// Forward declarations for the detour table (defined below).
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr_Detour(VkInstance instance, const char* pName);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr_Detour(VkDevice device, const char* pName);
static VkResult VKAPI_CALL vkCreateDevice_Detour(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
                                                 const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);
static VkResult VKAPI_CALL vkQueuePresentKHR_Detour(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);
static VkResult VKAPI_CALL vkCreateSwapchainKHR_Detour(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                       const VkAllocationCallbacks* pAllocator,
                                                       VkSwapchainKHR* pSwapchain);
static VkResult VKAPI_CALL vkBeginCommandBuffer_Detour(VkCommandBuffer commandBuffer,
                                                       const VkCommandBufferBeginInfo* pBeginInfo);

/** Table-driven hook install: name, detour, original (same order as InstallVulkanLoaderHooks loop). */
struct VulkanLoaderHookEntry {
    const char* name;
    LPVOID detour;
    LPVOID* original;
};
static constexpr std::size_t kVulkanLoaderHookCount = 3;
static const VulkanLoaderHookEntry kVulkanLoaderHooks[kVulkanLoaderHookCount] = {
    {"vkGetInstanceProcAddr", reinterpret_cast<LPVOID>(&vkGetInstanceProcAddr_Detour),
     reinterpret_cast<LPVOID*>(&vkGetInstanceProcAddr_Original)},
    {"vkGetDeviceProcAddr", reinterpret_cast<LPVOID>(&vkGetDeviceProcAddr_Detour),
     reinterpret_cast<LPVOID*>(&vkGetDeviceProcAddr_Original)},
    {"vkCreateDevice", reinterpret_cast<LPVOID>(&vkCreateDevice_Detour),
     reinterpret_cast<LPVOID*>(&g_real_vkCreateDevice)},
};
/** Trampoline to the real vkSetLatencyMarkerNV (filled by MinHook when we hook the real). */
static PFN_vkSetLatencyMarkerNV_t g_real_vkSetLatencyMarkerNV = nullptr;
/** First time we see the real pointer we MinHook it so callers that already cached it still hit our detour. */
static void* g_hooked_vkSetLatencyMarkerNV_target = nullptr;

/** Trampoline to the real vkQueuePresentKHR (filled when we hook it via vkGetDeviceProcAddr). */
static PFN_vkQueuePresentKHR_t g_real_vkQueuePresentKHR = nullptr;
static void* g_hooked_vkQueuePresentKHR_target = nullptr;
static std::atomic<bool> g_loader_hooks_installed{false};
static std::atomic<uint64_t> g_loader_marker_count{0};
static std::atomic<int> g_loader_last_marker_type{-1};
static std::atomic<uint64_t> g_loader_last_present_id{0};
static std::atomic<uint64_t> g_loader_intercept_count{0};  // times we returned detour from vkGetDeviceProcAddr
/** Call counts for each detour (incremented on entry). */
static std::atomic<uint64_t> g_calls_vkGetInstanceProcAddr{0};
static std::atomic<uint64_t> g_calls_vkGetDeviceProcAddr{0};
static std::atomic<uint64_t> g_calls_vkCreateDevice{0};

/** Enabled device extensions from last vkCreateDevice (thread-safe). */
static std::vector<std::string> g_vulkan_enabled_extensions;
/** True once vkCreateDevice_Detour has been entered at least once (so UI can show "not called yet" vs "no extensions").
 */
static std::atomic<bool> g_vkCreateDevice_detour_ever_called{false};

// --- Injected Reflex (VK_NV_low_latency2) state ---
static PFN_vkCreateSwapchainKHR_t g_real_vkCreateSwapchainKHR = nullptr;
static PFN_vkBeginCommandBuffer_t g_real_vkBeginCommandBuffer = nullptr;
static PFN_vkSetLatencySleepModeNV_t g_real_vkSetLatencySleepModeNV = nullptr;
static PFN_vkLatencySleepNV_t g_real_vkLatencySleepNV = nullptr;
static PFN_vkCreateSemaphore_t g_real_vkCreateSemaphore = nullptr;
static PFN_vkGetSemaphoreCounterValue_t g_real_vkGetSemaphoreCounterValue = nullptr;
static PFN_vkWaitSemaphores_t g_real_vkWaitSemaphores = nullptr;

static std::atomic<VkDevice> g_injected_reflex_device{0};
static std::atomic<uint64_t> g_injected_reflex_swapchain_u64{0};  // VkSwapchainKHR as uint64_t
static std::atomic<uint64_t> g_injected_reflex_semaphore_u64{0};
static std::atomic<uint64_t> g_injected_reflex_present_id{0};
static std::atomic<uint64_t> g_injected_markers_count{0};
static std::atomic<uint64_t> g_injected_sleep_calls{0};
static std::atomic<uint64_t> g_injected_swapchain_latency_creates{0};
static std::atomic<uint64_t> g_injected_renderbatch_frame{0};  // once-per-frame guard for BeginCommandBuffer
static std::atomic<bool> g_injected_procs_resolved{false};

// VK_NV_low_latency2 marker enum (same order as NvLL): 0=SIMULATION_START, 4=PRESENT_START, 5=PRESENT_END
static constexpr int VK_LATENCY_MARKER_SIMULATION_START_NV = 0;
static constexpr int VK_LATENCY_MARKER_SIMULATION_END_NV = 1;
static constexpr int VK_LATENCY_MARKER_RENDERSUBMIT_START_NV = 2;
static constexpr int VK_LATENCY_MARKER_RENDERSUBMIT_END_NV = 3;
static constexpr int VK_LATENCY_MARKER_PRESENT_START_NV = 4;
static constexpr int VK_LATENCY_MARKER_PRESENT_END_NV = 5;

// Extensions to append in vkCreateDevice (Special K style; always on).
static const char* const kReflexExtensionNames[] = {
    "VK_NV_low_latency2",
    "VK_KHR_present_id",
    "VK_KHR_timeline_semaphore",
};

static bool HasExtension(const char* const* names, uint32_t count, const char* want) {
    for (uint32_t i = 0; i < count && names != nullptr; ++i) {
        if (names[i] != nullptr && std::strcmp(names[i], want) == 0) {
            return true;
        }
    }
    return false;
}

/** Walk pNext chain to find VkPresentIdKHR; returns nullptr if not found. */
static const VkPresentIdKHR* FindPresentIdInPresentInfo(const VkPresentInfoKHR* pPresentInfo) {
    const void* p = pPresentInfo->pNext;
    while (p != nullptr) {
        const auto* base = static_cast<const VkBaseInStructure*>(p);
        if (base->sType == VK_STRUCTURE_TYPE_PRESENT_ID_KHR) {
            return static_cast<const VkPresentIdKHR*>(p);
        }
        p = base->pNext;
    }
    return nullptr;
}

static void ResolveInjectedReflexProcs(VkDevice device) {
    if (g_injected_procs_resolved.load()) {
        return;
    }
    if (device == VK_NULL_HANDLE) {
        return;
    }
    PFN_vkGetDeviceProcAddr getProc = vkGetDeviceProcAddr_Original;
    if (!getProc) {
        return;
    }
    g_real_vkSetLatencySleepModeNV =
        reinterpret_cast<PFN_vkSetLatencySleepModeNV_t>(getProc(device, "vkSetLatencySleepModeNV"));
    g_real_vkLatencySleepNV = reinterpret_cast<PFN_vkLatencySleepNV_t>(getProc(device, "vkLatencySleepNV"));
    g_real_vkCreateSemaphore = reinterpret_cast<PFN_vkCreateSemaphore_t>(getProc(device, "vkCreateSemaphore"));
    g_real_vkGetSemaphoreCounterValue =
        reinterpret_cast<PFN_vkGetSemaphoreCounterValue_t>(getProc(device, "vkGetSemaphoreCounterValue"));
    g_real_vkWaitSemaphores = reinterpret_cast<PFN_vkWaitSemaphores_t>(getProc(device, "vkWaitSemaphores"));
    g_injected_procs_resolved.store(g_real_vkSetLatencySleepModeNV != nullptr && g_real_vkLatencySleepNV != nullptr
                                    && g_real_vkCreateSemaphore != nullptr
                                    && g_real_vkGetSemaphoreCounterValue != nullptr
                                    && g_real_vkWaitSemaphores != nullptr);
}

static VkResult VKAPI_CALL vkCreateDevice_Detour(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
                                                 const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    g_calls_vkCreateDevice.fetch_add(1);
    g_vkCreateDevice_detour_ever_called.store(true);
    if (g_real_vkCreateDevice == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const bool append_extensions = settings::g_mainTabSettings.vulkan_injected_reflex_enabled.GetValue();
    std::vector<std::string> exts_for_ui;

    if (pCreateInfo != nullptr && pCreateInfo->ppEnabledExtensionNames != nullptr
        && pCreateInfo->enabledExtensionCount > 0) {
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
            const char* name = pCreateInfo->ppEnabledExtensionNames[i];
            if (name != nullptr) {
                exts_for_ui.push_back(name);
            }
        }
    }

    if (append_extensions && pCreateInfo != nullptr) {
        std::vector<const char*> ptrs;
        if (pCreateInfo->ppEnabledExtensionNames != nullptr) {
            for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
                if (pCreateInfo->ppEnabledExtensionNames[i] != nullptr) {
                    ptrs.push_back(pCreateInfo->ppEnabledExtensionNames[i]);
                }
            }
        }
        for (const char* extra : kReflexExtensionNames) {
            if (!HasExtension(pCreateInfo->ppEnabledExtensionNames, pCreateInfo->enabledExtensionCount, extra)) {
                ptrs.push_back(extra);
                exts_for_ui.push_back(extra);
            }
        }
        if (ptrs.size() > (pCreateInfo->ppEnabledExtensionNames ? pCreateInfo->enabledExtensionCount : 0u)) {
            VkDeviceCreateInfo mod = *pCreateInfo;
            mod.ppEnabledExtensionNames = ptrs.data();
            mod.enabledExtensionCount = static_cast<uint32_t>(ptrs.size());
            VkResult r = g_real_vkCreateDevice(physicalDevice, &mod, pAllocator, pDevice);
            if (r == VK_SUCCESS) {
                LogInfo("VulkanLoader: vkCreateDevice with %zu extensions (appended Reflex) succeeded", ptrs.size());
                utils::SRWLockExclusive lock(utils::g_vulkan_extensions_lock);
                g_vulkan_enabled_extensions = std::move(exts_for_ui);
                return r;
            }
            LogInfo("VulkanLoader: vkCreateDevice with appended extensions failed (0x%x), falling back to original",
                    static_cast<unsigned>(r));
            exts_for_ui.clear();
            if (pCreateInfo->ppEnabledExtensionNames != nullptr) {
                for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
                    if (pCreateInfo->ppEnabledExtensionNames[i] != nullptr) {
                        exts_for_ui.push_back(pCreateInfo->ppEnabledExtensionNames[i]);
                    }
                }
            }
        }
    }

    VkResult r = g_real_vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (!exts_for_ui.empty()) {
        utils::SRWLockExclusive lock(utils::g_vulkan_extensions_lock);
        g_vulkan_enabled_extensions = std::move(exts_for_ui);
    }
    if (pCreateInfo != nullptr) {
        LogInfo("VulkanLoader: vkCreateDevice captured %u enabled extension(s)", pCreateInfo->enabledExtensionCount);
    }
    return r;
}

static VkResult VKAPI_CALL vkCreateSwapchainKHR_Detour(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                       const VkAllocationCallbacks* pAllocator,
                                                       VkSwapchainKHR* pSwapchain) {
    if (g_real_vkCreateSwapchainKHR == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const bool inject = settings::g_mainTabSettings.vulkan_injected_reflex_enabled.GetValue()
                        && settings::g_mainTabSettings.vulkan_vk_loader_hooks_enabled.GetValue();
    if (!inject) {
        return g_real_vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    }
    VkSwapchainLatencyCreateInfoNV latency_info = {};
    latency_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_LATENCY_CREATE_INFO_NV;
    latency_info.pNext = pCreateInfo->pNext;
    latency_info.latencyModeEnable = VK_TRUE;
    VkSwapchainCreateInfoKHR mod = *pCreateInfo;
    mod.pNext = &latency_info;
    VkResult r = g_real_vkCreateSwapchainKHR(device, &mod, pAllocator, pSwapchain);
    if (r == VK_SUCCESS && pSwapchain != nullptr && *pSwapchain != VK_NULL_HANDLE) {
        ResolveInjectedReflexProcs(device);
        if (g_real_vkCreateSemaphore != nullptr) {
            VkSemaphoreTypeCreateInfo type_info = {};
            type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
            type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            type_info.initialValue = 0;
            VkSemaphoreCreateInfo sem_info = {};
            sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            sem_info.pNext = &type_info;
            VkSemaphore sem = VK_NULL_HANDLE;
            if (g_real_vkCreateSemaphore(device, &sem_info, pAllocator, &sem) == VK_SUCCESS) {
                g_injected_reflex_device.store(device);
                g_injected_reflex_swapchain_u64.store(VkHandleToU64(*pSwapchain));
                g_injected_reflex_semaphore_u64.store(VkHandleToU64(sem));
                g_injected_swapchain_latency_creates.fetch_add(1);
            }
        }
    }
    return r;
}

static VkResult VKAPI_CALL vkBeginCommandBuffer_Detour(VkCommandBuffer commandBuffer,
                                                       const VkCommandBufferBeginInfo* pBeginInfo) {
    if (g_real_vkBeginCommandBuffer == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const bool inject = settings::g_mainTabSettings.vulkan_injected_reflex_enabled.GetValue()
                        && settings::g_mainTabSettings.vulkan_vk_loader_hooks_enabled.GetValue();
    if (inject && GetModuleHandleW(L"NvLowLatencyVk.dll") == nullptr) {
        VkDevice device = g_injected_reflex_device.load();
        VkSwapchainKHR swapchain = U64ToVkHandle<VkSwapchainKHR>(g_injected_reflex_swapchain_u64.load());
        if (device != VK_NULL_HANDLE && swapchain != VK_NULL_HANDLE && g_real_vkSetLatencyMarkerNV != nullptr) {
            const uint64_t frame_id = g_injected_reflex_present_id.load();
            uint64_t expected = g_injected_renderbatch_frame.load();
            if (expected < frame_id && g_injected_renderbatch_frame.compare_exchange_strong(expected, frame_id)) {
                VkSetLatencyMarkerInfoNV marker = {};
                marker.sType = VK_STRUCTURE_TYPE_SET_LATENCY_MARKER_INFO_NV;
                marker.presentID = frame_id;
                marker.marker = static_cast<VkLatencyMarkerNV>(VK_LATENCY_MARKER_SIMULATION_START_NV);
                g_real_vkSetLatencyMarkerNV(device, swapchain, &marker);
                marker.marker = static_cast<VkLatencyMarkerNV>(VK_LATENCY_MARKER_SIMULATION_END_NV);
                g_real_vkSetLatencyMarkerNV(device, swapchain, &marker);
                marker.marker = static_cast<VkLatencyMarkerNV>(VK_LATENCY_MARKER_RENDERSUBMIT_START_NV);
                g_real_vkSetLatencyMarkerNV(device, swapchain, &marker);
                g_injected_markers_count.fetch_add(3);
            }
        }
    }
    return g_real_vkBeginCommandBuffer(commandBuffer, pBeginInfo);
}

void VKAPI_CALL vkSetLatencyMarkerNV_Detour(VkDevice device, VkSwapchainKHR swapchain,
                                            const VkSetLatencyMarkerInfoNV* pLatencyMarkerInfo) {
    (void)swapchain;
    g_loader_marker_count.fetch_add(1);
    LogInfo("VulkanLoader: vkSetLatencyMarkerNV_Detour called marker=%d presentID=%llu", pLatencyMarkerInfo->marker,
            pLatencyMarkerInfo->presentID);
    if (pLatencyMarkerInfo != nullptr) {
        g_loader_last_marker_type.store(static_cast<int>(pLatencyMarkerInfo->marker));
        g_loader_last_present_id.store(pLatencyMarkerInfo->presentID);
    }

    const uint64_t now_ns = static_cast<uint64_t>(utils::get_now_ns());
    if (pLatencyMarkerInfo != nullptr
        && static_cast<int>(pLatencyMarkerInfo->marker) == VK_LATENCY_MARKER_PRESENT_START_NV) {
        ChooseFpsLimiter(now_ns, FpsLimiterCallSite::reflex_marker_vk_loader);
    }

    const bool native_pacing_sim_start_only = settings::g_mainTabSettings.native_pacing_sim_start_only.GetValue();
    const bool use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::reflex_marker_vk_loader);

    if (native_pacing_sim_start_only) {
        if (use_fps_limiter && pLatencyMarkerInfo != nullptr) {
            if (static_cast<int>(pLatencyMarkerInfo->marker) == VK_LATENCY_MARKER_SIMULATION_START_NV) {
                OnPresentFlags2(false, true);
                RecordNativeFrameTime();
            }
            if (static_cast<int>(pLatencyMarkerInfo->marker) == VK_LATENCY_MARKER_SIMULATION_START_NV) {
                display_commanderhooks::dxgi::HandlePresentAfter(true);
            }
        }
    } else {
        if (use_fps_limiter && pLatencyMarkerInfo != nullptr) {
            if (static_cast<int>(pLatencyMarkerInfo->marker) == VK_LATENCY_MARKER_PRESENT_START_NV) {
                OnPresentFlags2(false, true);
                RecordNativeFrameTime();
            }
            if (static_cast<int>(pLatencyMarkerInfo->marker) == VK_LATENCY_MARKER_PRESENT_END_NV) {
                display_commanderhooks::dxgi::HandlePresentAfter(true);
            }
        }
    }

    if (g_real_vkSetLatencyMarkerNV != nullptr) {
        g_real_vkSetLatencyMarkerNV(device, swapchain, pLatencyMarkerInfo);
    }
}

// vkQueuePresentKHR detour: FPS limiter + optional injected Reflex (present ID, markers, sleep).
static VkResult VKAPI_CALL vkQueuePresentKHR_Detour(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    if (g_real_vkQueuePresentKHR == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const LONGLONG now_ns = utils::get_now_ns();
    ChooseFpsLimiter(static_cast<uint64_t>(now_ns), FpsLimiterCallSite::vk_queue_present_khr);
    const bool use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::vk_queue_present_khr);
    if (use_fps_limiter) {
        OnPresentFlags2(true, false);
        RecordNativeFrameTime();
    }
    if (GetChosenFrameTimeLocation() == FpsLimiterCallSite::vk_queue_present_khr) {
        RecordFrameTime(FrameTimeMode::kPresent);
    }

    const bool inject_reflex = settings::g_mainTabSettings.vulkan_injected_reflex_enabled.GetValue()
                               && settings::g_mainTabSettings.vulkan_vk_loader_hooks_enabled.GetValue()
                               && (GetModuleHandleW(L"NvLowLatencyVk.dll") == nullptr);
    const VkPresentInfoKHR* pPresent = pPresentInfo;
    uint64_t present_id_value = 0;
    VkPresentIdKHR present_id_struct = {};
    VkLatencySubmissionPresentIdNV latency_submission = {};
    VkPresentInfoKHR present_info_copy = {};
    if (inject_reflex && pPresentInfo->swapchainCount == 1) {
        const VkPresentIdKHR* pGamePresentId = FindPresentIdInPresentInfo(pPresentInfo);
        if (pGamePresentId != nullptr && pGamePresentId->pPresentIds != nullptr && pGamePresentId->swapchainCount > 0) {
            present_id_value = pGamePresentId->pPresentIds[0];
        } else {
            present_id_value = g_injected_reflex_present_id.load();
        }
        g_injected_reflex_present_id.store(present_id_value + 1);

        latency_submission.sType = VK_STRUCTURE_TYPE_LATENCY_SUBMISSION_PRESENT_ID_NV;
        latency_submission.pNext = pPresentInfo->pNext;
        latency_submission.presentID = present_id_value;
        present_id_struct.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
        present_id_struct.pNext = &latency_submission;
        present_id_struct.swapchainCount = 1;
        present_id_struct.pPresentIds = &present_id_value;
        present_info_copy = *pPresentInfo;
        present_info_copy.pNext = &present_id_struct;
        pPresent = &present_info_copy;

        VkDevice device = g_injected_reflex_device.load();
        VkSwapchainKHR swapchain = U64ToVkHandle<VkSwapchainKHR>(g_injected_reflex_swapchain_u64.load());
        if (device != VK_NULL_HANDLE && swapchain != VK_NULL_HANDLE && g_real_vkSetLatencyMarkerNV != nullptr) {
            VkSetLatencyMarkerInfoNV marker = {};
            marker.sType = VK_STRUCTURE_TYPE_SET_LATENCY_MARKER_INFO_NV;
            marker.presentID = present_id_value;
            if (present_id_value == 0) {
                marker.marker = static_cast<VkLatencyMarkerNV>(VK_LATENCY_MARKER_SIMULATION_START_NV);
                g_real_vkSetLatencyMarkerNV(device, swapchain, &marker);
                g_injected_markers_count.fetch_add(1);
            }
            const uint64_t batch = g_injected_renderbatch_frame.load();
            if (batch != present_id_value) {
                marker.marker = static_cast<VkLatencyMarkerNV>(VK_LATENCY_MARKER_SIMULATION_END_NV);
                g_real_vkSetLatencyMarkerNV(device, swapchain, &marker);
                marker.marker = static_cast<VkLatencyMarkerNV>(VK_LATENCY_MARKER_RENDERSUBMIT_START_NV);
                g_real_vkSetLatencyMarkerNV(device, swapchain, &marker);
                g_injected_markers_count.fetch_add(2);
            }
            marker.marker = static_cast<VkLatencyMarkerNV>(VK_LATENCY_MARKER_RENDERSUBMIT_END_NV);
            g_real_vkSetLatencyMarkerNV(device, swapchain, &marker);
            marker.marker = static_cast<VkLatencyMarkerNV>(VK_LATENCY_MARKER_PRESENT_START_NV);
            g_real_vkSetLatencyMarkerNV(device, swapchain, &marker);
            g_injected_markers_count.fetch_add(2);
        }
    }

    const VkResult ret = g_real_vkQueuePresentKHR(queue, pPresent);

    if (inject_reflex && ret == VK_SUCCESS && pPresentInfo->swapchainCount == 1) {
        VkDevice device = g_injected_reflex_device.load();
        VkSwapchainKHR swapchain = U64ToVkHandle<VkSwapchainKHR>(g_injected_reflex_swapchain_u64.load());
        VkSemaphore semaphore = U64ToVkHandle<VkSemaphore>(g_injected_reflex_semaphore_u64.load());
        if (device != VK_NULL_HANDLE && swapchain != VK_NULL_HANDLE && g_real_vkSetLatencyMarkerNV != nullptr) {
            VkSetLatencyMarkerInfoNV marker = {};
            marker.sType = VK_STRUCTURE_TYPE_SET_LATENCY_MARKER_INFO_NV;
            marker.presentID = present_id_value;
            marker.marker = static_cast<VkLatencyMarkerNV>(VK_LATENCY_MARKER_PRESENT_END_NV);
            g_real_vkSetLatencyMarkerNV(device, swapchain, &marker);
            g_injected_markers_count.fetch_add(1);
        }
        if (g_real_vkSetLatencySleepModeNV != nullptr && g_real_vkLatencySleepNV != nullptr
            && g_real_vkGetSemaphoreCounterValue != nullptr && g_real_vkWaitSemaphores != nullptr
            && semaphore != VK_NULL_HANDLE) {
            ResolveInjectedReflexProcs(device);
            uint64_t sem_value = 0;
            if (g_real_vkGetSemaphoreCounterValue(device, semaphore, &sem_value) == VK_SUCCESS) {
                sem_value += 1;
                VkLatencySleepModeInfoNV sleep_mode = {};
                sleep_mode.sType = VK_STRUCTURE_TYPE_LATENCY_SLEEP_MODE_INFO_NV;
                sleep_mode.lowLatencyMode = VK_TRUE;
                sleep_mode.lowLatencyBoost = VK_FALSE;
                sleep_mode.minimumIntervalUs = 0;
                g_real_vkSetLatencySleepModeNV(device, swapchain, &sleep_mode);
                VkLatencySleepInfoNV sleep_info = {};
                sleep_info.sType = VK_STRUCTURE_TYPE_LATENCY_SLEEP_INFO_NV;
                sleep_info.signalSemaphore = semaphore;
                sleep_info.value = sem_value;
                if (g_real_vkLatencySleepNV(device, swapchain, &sleep_info) == VK_SUCCESS) {
                    g_injected_sleep_calls.fetch_add(1);
                    VkSemaphoreWaitInfo wait_info = {};
                    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
                    wait_info.semaphoreCount = 1;
                    wait_info.pSemaphores = &semaphore;
                    wait_info.pValues = &sem_value;
                    g_real_vkWaitSemaphores(device, &wait_info, UINT64_MAX);
                }
            }
        }
    }

    if (use_fps_limiter) {
        display_commanderhooks::dxgi::HandlePresentAfter(false);
    }
    return ret;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr_Detour(VkInstance instance, const char* pName) {
    g_calls_vkGetInstanceProcAddr.fetch_add(1);
    if (vkGetInstanceProcAddr_Original == nullptr) {
        return nullptr;
    }
    PFN_vkVoidFunction result = vkGetInstanceProcAddr_Original(instance, pName);
    // Return our detour so callers that resolve vkCreateDevice via vkGetInstanceProcAddr hit us.
    // (Direct export hook catches GetProcAddress("vkCreateDevice") on vulkan-1.dll.)
    if (pName != nullptr && std::strcmp(pName, "vkCreateDevice") == 0 && result != nullptr) {
        result = reinterpret_cast<PFN_vkVoidFunction>(&vkCreateDevice_Detour);
    }
    return result;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr_Detour(VkDevice device, const char* pName) {
    g_calls_vkGetDeviceProcAddr.fetch_add(1);
    if (vkGetDeviceProcAddr_Original == nullptr) {
        return nullptr;
    }
    PFN_vkVoidFunction result = vkGetDeviceProcAddr_Original(device, pName);
    if (pName != nullptr && std::strcmp(pName, "vkSetLatencyMarkerNV") == 0 && result != nullptr) {
        g_loader_intercept_count.fetch_add(1);
        if (g_real_vkSetLatencyMarkerNV == nullptr) {
            if (CreateAndEnableHook(reinterpret_cast<LPVOID>(result),
                                    reinterpret_cast<LPVOID>(&vkSetLatencyMarkerNV_Detour),
                                    reinterpret_cast<LPVOID*>(&g_real_vkSetLatencyMarkerNV), "vkSetLatencyMarkerNV")) {
                g_hooked_vkSetLatencyMarkerNV_target = result;
            } else {
                g_real_vkSetLatencyMarkerNV = reinterpret_cast<PFN_vkSetLatencyMarkerNV_t>(result);
            }
        }
        result = reinterpret_cast<PFN_vkVoidFunction>(&vkSetLatencyMarkerNV_Detour);
    }
    if (pName != nullptr && std::strcmp(pName, "vkQueuePresentKHR") == 0 && result != nullptr) {
        if (g_real_vkQueuePresentKHR == nullptr) {
            if (CreateAndEnableHook(reinterpret_cast<LPVOID>(result),
                                    reinterpret_cast<LPVOID>(&vkQueuePresentKHR_Detour),
                                    reinterpret_cast<LPVOID*>(&g_real_vkQueuePresentKHR), "vkQueuePresentKHR")) {
                g_hooked_vkQueuePresentKHR_target = result;
                LogInfo("VulkanLoader: vkQueuePresentKHR hooked for FPS limiter");
            } else {
                g_real_vkQueuePresentKHR = reinterpret_cast<PFN_vkQueuePresentKHR_t>(result);
            }
        }
        result = reinterpret_cast<PFN_vkVoidFunction>(&vkQueuePresentKHR_Detour);
    }
    if (pName != nullptr && std::strcmp(pName, "vkCreateSwapchainKHR") == 0 && result != nullptr) {
        if (g_real_vkCreateSwapchainKHR == nullptr) {
            g_real_vkCreateSwapchainKHR = reinterpret_cast<PFN_vkCreateSwapchainKHR_t>(result);
        }
        result = reinterpret_cast<PFN_vkVoidFunction>(&vkCreateSwapchainKHR_Detour);
    }
    if (pName != nullptr && std::strcmp(pName, "vkBeginCommandBuffer") == 0 && result != nullptr) {
        if (g_real_vkBeginCommandBuffer == nullptr) {
            g_real_vkBeginCommandBuffer = reinterpret_cast<PFN_vkBeginCommandBuffer_t>(result);
        }
        result = reinterpret_cast<PFN_vkVoidFunction>(&vkBeginCommandBuffer_Detour);
    }
    return result;
}

static void RollbackVulkanLoaderHooks(std::size_t installed_count) {
    for (std::size_t j = installed_count; j-- > 0;) {
        LPVOID* const orig = kVulkanLoaderHooks[j].original;
        if (orig != nullptr && *orig != nullptr) {
            MH_DisableHook(*orig);
            MH_RemoveHook(*orig);
            *orig = nullptr;
        }
    }
}

static bool InstallVulkanLoaderHooksImpl(void* vulkan1_module) {
    HMODULE module = static_cast<HMODULE>(vulkan1_module);
    if (module == nullptr) {
        module = GetModuleHandleW(L"vulkan-1.dll");
    }
    if (module == nullptr) {
        LogInfo("VulkanLoader: vulkan-1.dll not loaded");
        return false;
    }
    if (g_loader_hooks_installed.load()) {
        LogInfo("VulkanLoader: hooks already installed");
        return true;
    }
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::VULKAN_LOADER)) {
        LogInfo("VulkanLoader: hooks installation suppressed by user setting");
        return false;
    }
    if (!settings::g_mainTabSettings.vulkan_vk_loader_hooks_enabled.GetValue()) {
        LogInfo("VulkanLoader: hooks disabled by setting");
        return false;
    }

    for (std::size_t i = 0; i < kVulkanLoaderHookCount; ++i) {
        const VulkanLoaderHookEntry& entry = kVulkanLoaderHooks[i];
        if (!CreateAndEnableHookFromModule(module, entry.name, entry.detour, entry.original, entry.name)) {
            LogInfo("VulkanLoader: failed to hook %s", entry.name);
            RollbackVulkanLoaderHooks(i);
            return false;
        }
    }

    g_loader_hooks_installed.store(true);
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::VULKAN_LOADER);
    LogInfo(
        "VulkanLoader: VK_NV_low_latency2 hooks installed (vkGetInstanceProcAddr + vkGetDeviceProcAddr + "
        "vkCreateDevice export)");
    return true;
}

}  // namespace

bool InstallVulkanLoaderHooks(void* vulkan1_module) { return InstallVulkanLoaderHooksImpl(vulkan1_module); }

bool AreVulkanLoaderHooksInstalled() { return g_loader_hooks_installed.load(); }

void GetVulkanLoaderDebugState(uint64_t* out_marker_count, int* out_last_marker_type, uint64_t* out_last_present_id,
                               uint64_t* out_intercept_count) {
    if (out_marker_count) {
        *out_marker_count = g_loader_marker_count.load();
    }
    if (out_last_marker_type) {
        *out_last_marker_type = g_loader_last_marker_type.load();
    }
    if (out_last_present_id) {
        *out_last_present_id = g_loader_last_present_id.load();
    }
    if (out_intercept_count) {
        *out_intercept_count = g_loader_intercept_count.load();
    }
}

void GetVulkanLoaderCallCounts(uint64_t* out_vkGetInstanceProcAddr, uint64_t* out_vkGetDeviceProcAddr,
                               uint64_t* out_vkCreateDevice, uint64_t* out_vkSetLatencyMarkerNV) {
    if (out_vkGetInstanceProcAddr) *out_vkGetInstanceProcAddr = g_calls_vkGetInstanceProcAddr.load();
    if (out_vkGetDeviceProcAddr) *out_vkGetDeviceProcAddr = g_calls_vkGetDeviceProcAddr.load();
    if (out_vkCreateDevice) *out_vkCreateDevice = g_calls_vkCreateDevice.load();
    if (out_vkSetLatencyMarkerNV) *out_vkSetLatencyMarkerNV = g_loader_marker_count.load();
}

void GetVulkanEnabledExtensions(std::vector<std::string>& out) {
    out.clear();
    utils::SRWLockShared lock(utils::g_vulkan_extensions_lock);
    out = g_vulkan_enabled_extensions;
}

bool HasVulkanCreateDeviceBeenCalled() { return g_vkCreateDevice_detour_ever_called.load(); }

void GetVulkanInjectedReflexDebugState(VulkanInjectedReflexDebugState* out) {
    if (out == nullptr) {
        return;
    }
    out->enabled = settings::g_mainTabSettings.vulkan_injected_reflex_enabled.GetValue();
    out->loader_hooks_on = g_loader_hooks_installed.load();
    out->nvll_loaded = (GetModuleHandleW(L"NvLowLatencyVk.dll") != nullptr);
    const bool has_dev = g_injected_reflex_device.load() != VK_NULL_HANDLE;
    const bool has_swap = g_injected_reflex_swapchain_u64.load() != 0;
    const bool has_sem = g_injected_reflex_semaphore_u64.load() != 0;
    out->injecting = out->enabled && out->loader_hooks_on && !out->nvll_loaded && has_dev && has_swap
                     && g_real_vkSetLatencyMarkerNV != nullptr;
    out->present_id = g_injected_reflex_present_id.load();
    out->markers_injected = g_injected_markers_count.load();
    out->sleep_calls = g_injected_sleep_calls.load();
    out->swapchain_latency_creates = g_injected_swapchain_latency_creates.load();
    out->has_device = has_dev;
    out->has_swapchain = has_swap;
    out->has_semaphore = has_sem;
    out->procs_resolved = g_injected_procs_resolved.load();
}
