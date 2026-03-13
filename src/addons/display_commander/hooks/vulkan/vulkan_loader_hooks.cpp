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
using PFN_vkSetLatencyMarkerNV_t = PFN_vkSetLatencyMarkerNV;
using PFN_vkCreateDevice_t = PFN_vkCreateDevice;
using PFN_vkQueuePresentKHR_t = VkResult(VKAPI_CALL*)(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);
using PFN_vkCreateSwapchainKHR_t = PFN_vkCreateSwapchainKHR;
using PFN_vkBeginCommandBuffer_t = PFN_vkBeginCommandBuffer;

static PFN_vkGetInstanceProcAddr_t vkGetInstanceProcAddr_Original = nullptr;
static PFN_vkCreateDevice_t g_real_vkCreateDevice = nullptr;
static PFN_vkCreateSwapchainKHR_t g_real_vkCreateSwapchainKHR = nullptr;
static PFN_vkQueuePresentKHR_t g_real_vkQueuePresentKHR = nullptr;
static PFN_vkBeginCommandBuffer_t g_real_vkBeginCommandBuffer = nullptr;
static PFN_vkSetLatencyMarkerNV_t g_real_vkSetLatencyMarkerNV = nullptr;

// Forward declarations for the detour table (defined below).
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr_Detour(VkInstance instance, const char* pName);
static VkResult VKAPI_CALL vkCreateDevice_Detour(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
                                                 const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);
static VkResult VKAPI_CALL vkQueuePresentKHR_Detour(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);
static VkResult VKAPI_CALL vkCreateSwapchainKHR_Detour(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                       const VkAllocationCallbacks* pAllocator,
                                                       VkSwapchainKHR* pSwapchain);
static VkResult VKAPI_CALL vkBeginCommandBuffer_Detour(VkCommandBuffer commandBuffer,
                                                       const VkCommandBufferBeginInfo* pBeginInfo);
void VKAPI_CALL vkSetLatencyMarkerNV_Detour(VkDevice device, VkSwapchainKHR swapchain,
                                            const VkSetLatencyMarkerInfoNV* pLatencyMarkerInfo);

/** Table-driven hook install: name, detour, original. All hooks installed from vulkan-1.dll exports (no
 * vkGetDeviceProcAddr). */
struct VulkanLoaderHookEntry {
    const char* name;
    LPVOID detour;
    LPVOID* original;
};
static const VulkanLoaderHookEntry kVulkanLoaderHooks[static_cast<std::size_t>(VulkanLoaderHook::Count)] = {
    {.name = "vkGetInstanceProcAddr",
     .detour = reinterpret_cast<LPVOID>(&vkGetInstanceProcAddr_Detour),
     .original = reinterpret_cast<LPVOID*>(&vkGetInstanceProcAddr_Original)},
    {.name = "vkCreateDevice",
     .detour = reinterpret_cast<LPVOID>(&vkCreateDevice_Detour),
     .original = reinterpret_cast<LPVOID*>(&g_real_vkCreateDevice)},
    {.name = "vkCreateSwapchainKHR",
     .detour = reinterpret_cast<LPVOID>(&vkCreateSwapchainKHR_Detour),
     .original = reinterpret_cast<LPVOID*>(&g_real_vkCreateSwapchainKHR)},
    {.name = "vkQueuePresentKHR",
     .detour = reinterpret_cast<LPVOID>(&vkQueuePresentKHR_Detour),
     .original = reinterpret_cast<LPVOID*>(&g_real_vkQueuePresentKHR)},
    {.name = "vkBeginCommandBuffer",
     .detour = reinterpret_cast<LPVOID>(&vkBeginCommandBuffer_Detour),
     .original = reinterpret_cast<LPVOID*>(&g_real_vkBeginCommandBuffer)},
    {.name = "vkSetLatencyMarkerNV",
     .detour = reinterpret_cast<LPVOID>(&vkSetLatencyMarkerNV_Detour),
     .original = reinterpret_cast<LPVOID*>(&g_real_vkSetLatencyMarkerNV)},
};
static std::atomic<bool> g_loader_hooks_installed{false};
static std::atomic<uint64_t> g_loader_marker_count{0};
static std::atomic<int> g_loader_last_marker_type{-1};
static std::atomic<uint64_t> g_loader_last_present_id{0};
/** Call counts per hook (indexed by VulkanLoaderHook); incremented on each detour entry. */
static std::atomic<uint64_t> g_loader_hook_call_counts[static_cast<std::size_t>(VulkanLoaderHook::Count)]{};

/** Enabled device extensions from last vkCreateDevice (thread-safe). */
static std::vector<std::string> g_vulkan_enabled_extensions;
/** True once vkCreateDevice_Detour has been entered at least once (so UI can show "not called yet" vs "no extensions").
 */
static std::atomic<bool> g_vkCreateDevice_detour_ever_called{false};

// VK_NV_low_latency2 marker enum (same order as NvLL): 0=SIMULATION_START, 4=PRESENT_START, 5=PRESENT_END
static constexpr int VK_LATENCY_MARKER_SIMULATION_START_NV = 0;
static constexpr int VK_LATENCY_MARKER_SIMULATION_END_NV = 1;
static constexpr int VK_LATENCY_MARKER_RENDERSUBMIT_START_NV = 2;
static constexpr int VK_LATENCY_MARKER_RENDERSUBMIT_END_NV = 3;
static constexpr int VK_LATENCY_MARKER_PRESENT_START_NV = 4;
static constexpr int VK_LATENCY_MARKER_PRESENT_END_NV = 5;

/** Walk pNext chain to find VkPresentIdKHR; returns nullptr if not found. Used for stats only. */
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

// Injected Reflex state kept for GetVulkanInjectedReflexDebugState (always zero/false; injection removed).
static std::atomic<VkDevice> g_injected_reflex_device{0};
static std::atomic<uint64_t> g_injected_reflex_swapchain_u64{0};
static std::atomic<uint64_t> g_injected_reflex_semaphore_u64{0};
static std::atomic<uint64_t> g_injected_reflex_present_id{0};
static std::atomic<uint64_t> g_injected_markers_count{0};
static std::atomic<uint64_t> g_injected_sleep_calls{0};
static std::atomic<uint64_t> g_injected_swapchain_latency_creates{0};
static std::atomic<bool> g_injected_procs_resolved{false};

// Extension names for injection (stable pointers for VkDeviceCreateInfo). Order: dependencies first, then
// VK_NV_low_latency2.
static const char* const kVkKHRPresentIdExtensionName = "VK_KHR_present_id";
static const char* const kVkKHRTimelineSemaphoreExtensionName = "VK_KHR_timeline_semaphore";
static const char* const kVkNVLowLatency2ExtensionName = "VK_NV_low_latency2";

static bool HasExtension(const VkDeviceCreateInfo* pCreateInfo, const char* extName) {
    if (pCreateInfo == nullptr || pCreateInfo->ppEnabledExtensionNames == nullptr || extName == nullptr) {
        return false;
    }
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
        const char* name = pCreateInfo->ppEnabledExtensionNames[i];
        if (name != nullptr && std::strcmp(name, extName) == 0) {
            return true;
        }
    }
    return false;
}

static VkResult VKAPI_CALL vkCreateDevice_Detour(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
                                                 const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    g_loader_hook_call_counts[static_cast<std::size_t>(VulkanLoaderHook::CreateDevice)].fetch_add(1);
    g_vkCreateDevice_detour_ever_called.store(true);
    if (g_real_vkCreateDevice == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const VkDeviceCreateInfo* createInfoToUse = pCreateInfo;
    std::vector<const char*> injected_extension_ptrs;  // Only used when we inject; must outlive the call below
    VkDeviceCreateInfo injected_create_info;           // Copy when we inject

    const bool want_inject = settings::g_mainTabSettings.vulkan_inject_extensions_enabled.GetValue()
                             && pCreateInfo != nullptr && pCreateInfo->ppEnabledExtensionNames != nullptr
                             && pCreateInfo->enabledExtensionCount > 0;
    const bool need_present_id = want_inject && !HasExtension(pCreateInfo, kVkKHRPresentIdExtensionName);
    const bool need_timeline_semaphore =
        want_inject && !HasExtension(pCreateInfo, kVkKHRTimelineSemaphoreExtensionName);
    const bool need_low_latency2 = want_inject && !HasExtension(pCreateInfo, kVkNVLowLatency2ExtensionName);
    const bool inject_extensions = need_present_id || need_timeline_semaphore || need_low_latency2;

    if (inject_extensions) {
        const uint32_t extra =
            (need_present_id ? 1u : 0u) + (need_timeline_semaphore ? 1u : 0u) + (need_low_latency2 ? 1u : 0u);
        injected_extension_ptrs.reserve(static_cast<std::size_t>(pCreateInfo->enabledExtensionCount) + extra);
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
            injected_extension_ptrs.push_back(pCreateInfo->ppEnabledExtensionNames[i]);
        }
        if (need_present_id) {
            injected_extension_ptrs.push_back(kVkKHRPresentIdExtensionName);
        }
        if (need_timeline_semaphore) {
            injected_extension_ptrs.push_back(kVkKHRTimelineSemaphoreExtensionName);
        }
        if (need_low_latency2) {
            injected_extension_ptrs.push_back(kVkNVLowLatency2ExtensionName);
        }

        injected_create_info = *pCreateInfo;
        injected_create_info.ppEnabledExtensionNames = injected_extension_ptrs.data();
        injected_create_info.enabledExtensionCount = static_cast<uint32_t>(injected_extension_ptrs.size());
        createInfoToUse = &injected_create_info;
        LogInfo("VulkanLoader: injecting Vulkan extensions in vkCreateDevice (%u -> %u):%s%s%s",
                pCreateInfo->enabledExtensionCount, injected_create_info.enabledExtensionCount,
                need_present_id ? " VK_KHR_present_id" : "",
                need_timeline_semaphore ? " VK_KHR_timeline_semaphore" : "",
                need_low_latency2 ? " VK_NV_low_latency2" : "");
    }

    std::vector<std::string> exts_for_ui;
    if (createInfoToUse != nullptr && createInfoToUse->ppEnabledExtensionNames != nullptr
        && createInfoToUse->enabledExtensionCount > 0) {
        for (uint32_t i = 0; i < createInfoToUse->enabledExtensionCount; ++i) {
            const char* name = createInfoToUse->ppEnabledExtensionNames[i];
            if (name != nullptr) {
                exts_for_ui.push_back(name);
            }
        }
    }

    const VkResult r = g_real_vkCreateDevice(physicalDevice, createInfoToUse, pAllocator, pDevice);
    if (!exts_for_ui.empty()) {
        utils::SRWLockExclusive lock(utils::g_vulkan_extensions_lock);
        g_vulkan_enabled_extensions = std::move(exts_for_ui);
    }
    if (pCreateInfo != nullptr) {
        LogInfo("VulkanLoader: vkCreateDevice captured %u enabled extension(s)",
                createInfoToUse->enabledExtensionCount);
    }
    return r;
}

static VkResult VKAPI_CALL vkCreateSwapchainKHR_Detour(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                       const VkAllocationCallbacks* pAllocator,
                                                       VkSwapchainKHR* pSwapchain) {
    g_loader_hook_call_counts[static_cast<std::size_t>(VulkanLoaderHook::CreateSwapchainKHR)].fetch_add(1);
    if (g_real_vkCreateSwapchainKHR == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return g_real_vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
}

static VkResult VKAPI_CALL vkBeginCommandBuffer_Detour(VkCommandBuffer commandBuffer,
                                                       const VkCommandBufferBeginInfo* pBeginInfo) {
    g_loader_hook_call_counts[static_cast<std::size_t>(VulkanLoaderHook::BeginCommandBuffer)].fetch_add(1);
    if (g_real_vkBeginCommandBuffer == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return g_real_vkBeginCommandBuffer(commandBuffer, pBeginInfo);
}

void VKAPI_CALL vkSetLatencyMarkerNV_Detour(VkDevice device, VkSwapchainKHR swapchain,
                                            const VkSetLatencyMarkerInfoNV* pLatencyMarkerInfo) {
    g_loader_hook_call_counts[static_cast<std::size_t>(VulkanLoaderHook::SetLatencyMarkerNV)].fetch_add(1);
    g_loader_marker_count.fetch_add(1);
    const bool disabled = true;
    if (disabled) {
        if (g_real_vkSetLatencyMarkerNV != nullptr) {
            g_real_vkSetLatencyMarkerNV(device, swapchain, pLatencyMarkerInfo);
        }
        return;
    }
    (void)swapchain;
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

// vkQueuePresentKHR detour: FPS limiter + logging/stats only (no Reflex injection).
static VkResult VKAPI_CALL vkQueuePresentKHR_Detour(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    g_loader_hook_call_counts[static_cast<std::size_t>(VulkanLoaderHook::QueuePresentKHR)].fetch_add(1);
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

    // Stats: capture present ID from game when available (for debug UI).
    const VkPresentIdKHR* pGamePresentId = FindPresentIdInPresentInfo(pPresentInfo);
    if (pGamePresentId != nullptr && pGamePresentId->pPresentIds != nullptr && pGamePresentId->swapchainCount > 0) {
        g_loader_last_present_id.store(pGamePresentId->pPresentIds[0]);
    }

    const VkResult ret = g_real_vkQueuePresentKHR(queue, pPresentInfo);

    if (use_fps_limiter) {
        display_commanderhooks::dxgi::HandlePresentAfter(false);
    }
    return ret;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr_Detour(VkInstance instance, const char* pName) {
    g_loader_hook_call_counts[static_cast<std::size_t>(VulkanLoaderHook::GetInstanceProcAddr)].fetch_add(1);
    if (vkGetInstanceProcAddr_Original == nullptr) {
        return nullptr;
    }
    PFN_vkVoidFunction result = vkGetInstanceProcAddr_Original(instance, pName);
    // Return our detour so callers that resolve vkCreateDevice via vkGetInstanceProcAddr hit us.
    if (pName != nullptr && std::strcmp(pName, "vkCreateDevice") == 0 && result != nullptr) {
        result = reinterpret_cast<PFN_vkVoidFunction>(&vkCreateDevice_Detour);
    }
    return result;
}

static void RollbackVulkanLoaderHooks() {
    for (std::size_t j = 0; j < static_cast<std::size_t>(VulkanLoaderHook::Count); ++j) {
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

    for (std::size_t i = 0; i < static_cast<std::size_t>(VulkanLoaderHook::Count); ++i) {
        const VulkanLoaderHookEntry& entry = kVulkanLoaderHooks[i];
        FARPROC target = GetProcAddress(module, entry.name);
        if (target == nullptr) {
            LogInfo("VulkanLoader: %s not exported by vulkan-1.dll, skipping", entry.name);
            continue;
        }
        if (!CreateAndEnableHook(reinterpret_cast<LPVOID>(target), entry.detour, entry.original, entry.name)) {
            LogInfo("VulkanLoader: failed to hook %s", entry.name);
            RollbackVulkanLoaderHooks();
            return false;
        }
    }

    g_loader_hooks_installed.store(true);
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::VULKAN_LOADER);
    LogInfo(
        "VulkanLoader: hooks installed (vkGetInstanceProcAddr, vkCreateDevice, vkCreateSwapchainKHR, "
        "vkQueuePresentKHR, vkBeginCommandBuffer, vkSetLatencyMarkerNV from exports)");
    return true;
}

void GetVulkanLoaderHookCallCountsImpl(uint64_t* out_counts, std::size_t count) {
    if (out_counts == nullptr) {
        return;
    }
    const std::size_t n = (count < static_cast<std::size_t>(VulkanLoaderHook::Count)) ? count : static_cast<std::size_t>(VulkanLoaderHook::Count);
    for (std::size_t i = 0; i < n; ++i) {
        out_counts[i] = g_loader_hook_call_counts[i].load();
    }
}

}  // namespace

const char* GetVulkanLoaderHookName(VulkanLoaderHook hook) {
    static const char* const kNames[] = {
        "vkGetInstanceProcAddr",
        "vkCreateDevice",
        "vkCreateSwapchainKHR",
        "vkQueuePresentKHR",
        "vkBeginCommandBuffer",
        "vkSetLatencyMarkerNV",
    };
    const std::size_t idx = static_cast<std::size_t>(hook);
    const std::size_t max_hook = static_cast<std::size_t>(VulkanLoaderHook::Count);
    if (idx >= max_hook) {
        return "(unknown)";
    }
    return kNames[idx];
}

void GetVulkanLoaderHookCallCounts(uint64_t* out_counts, std::size_t count) {
    GetVulkanLoaderHookCallCountsImpl(out_counts, count);
}

bool InstallVulkanLoaderHooks(void* vulkan1_module) { return InstallVulkanLoaderHooksImpl(vulkan1_module); }

bool AreVulkanLoaderHooksInstalled() { return g_loader_hooks_installed.load(); }

void UninstallVulkanLoaderHooks() {
    if (!g_loader_hooks_installed.exchange(false)) {
        return;
    }
    RollbackVulkanLoaderHooks();
    LogInfo("VulkanLoader: hooks uninstalled on unload");
}

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
        *out_intercept_count = 0;  // No longer used (hooks are from exports, not vkGetDeviceProcAddr).
    }
}

void GetVulkanLoaderCallCounts(uint64_t* out_vkGetInstanceProcAddr, uint64_t* out_vkGetDeviceProcAddr,
                               uint64_t* out_vkCreateDevice, uint64_t* out_vkSetLatencyMarkerNV) {
    if (out_vkGetInstanceProcAddr) {
        *out_vkGetInstanceProcAddr =
            g_loader_hook_call_counts[static_cast<std::size_t>(VulkanLoaderHook::GetInstanceProcAddr)].load();
    }
    if (out_vkGetDeviceProcAddr) *out_vkGetDeviceProcAddr = 0;  // vkGetDeviceProcAddr not hooked.
    if (out_vkCreateDevice) {
        *out_vkCreateDevice =
            g_loader_hook_call_counts[static_cast<std::size_t>(VulkanLoaderHook::CreateDevice)].load();
    }
    if (out_vkSetLatencyMarkerNV) {
        *out_vkSetLatencyMarkerNV =
            g_loader_hook_call_counts[static_cast<std::size_t>(VulkanLoaderHook::SetLatencyMarkerNV)].load();
    }
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
