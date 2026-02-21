#include "vulkan_loader_hooks.hpp"
#include "../../globals.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../swapchain_events.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/srwlock_wrapper.hpp"
#include "../../utils/timing.hpp"
#include "../dxgi/dxgi_present_hooks.hpp"

#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#define VK_NO_PROTOTYPES 1
#include <vulkan/vulkan_core.h>

namespace {

using PFN_vkGetInstanceProcAddr_t = PFN_vkGetInstanceProcAddr;
using PFN_vkGetDeviceProcAddr_t = PFN_vkGetDeviceProcAddr;
using PFN_vkSetLatencyMarkerNV_t = PFN_vkSetLatencyMarkerNV;
using PFN_vkCreateDevice_t = PFN_vkCreateDevice;

// Dummy implementations returned when the loader reports null for VK_NV_low_latency2 (so we can see if the game calls
// them).
static std::atomic<uint64_t> g_dummy_SetLatencySleepModeNV_calls{0};
static std::atomic<uint64_t> g_dummy_LatencySleepNV_calls{0};
static std::atomic<uint64_t> g_dummy_SetLatencyMarkerNV_calls{0};
static std::atomic<uint64_t> g_dummy_GetLatencyTimingsNV_calls{0};

static VkResult VKAPI_CALL Dummy_vkSetLatencySleepModeNV(VkDevice device, VkSwapchainKHR swapchain,
                                                         const VkLatencySleepModeInfoNV* pSleepModeInfo) {
    (void)device;
    (void)swapchain;
    (void)pSleepModeInfo;
    g_dummy_SetLatencySleepModeNV_calls.fetch_add(1);
    LogInfo("VulkanLoader: Dummy_vkSetLatencySleepModeNV called (driver returned null)");
    return VK_SUCCESS;
}

static VkResult VKAPI_CALL Dummy_vkLatencySleepNV(VkDevice device, VkSwapchainKHR swapchain,
                                                  const VkLatencySleepInfoNV* pSleepInfo) {
    (void)device;
    (void)swapchain;
    (void)pSleepInfo;
    g_dummy_LatencySleepNV_calls.fetch_add(1);
    LogInfo("VulkanLoader: Dummy_vkLatencySleepNV called (driver returned null)");
    return VK_SUCCESS;
}

static void VKAPI_CALL Dummy_vkSetLatencyMarkerNV(VkDevice device, VkSwapchainKHR swapchain,
                                                  const VkSetLatencyMarkerInfoNV* pLatencyMarkerInfo) {
    (void)device;
    (void)swapchain;
    uint64_t count = g_dummy_SetLatencyMarkerNV_calls.fetch_add(1) + 1;
    int marker = (pLatencyMarkerInfo != nullptr) ? static_cast<int>(pLatencyMarkerInfo->marker) : -1;
    uint64_t present_id = (pLatencyMarkerInfo != nullptr) ? pLatencyMarkerInfo->presentID : 0;
    LogInfo(
        "VulkanLoader: Dummy_vkSetLatencyMarkerNV called (driver returned null) marker=%d presentID=%llu "
        "total_calls=%llu",
        marker, static_cast<unsigned long long>(present_id), static_cast<unsigned long long>(count));
}

static void VKAPI_CALL Dummy_vkGetLatencyTimingsNV(VkDevice device, VkSwapchainKHR swapchain,
                                                   VkGetLatencyMarkerInfoNV* pLatencyMarkerInfo) {
    (void)device;
    (void)swapchain;
    (void)pLatencyMarkerInfo;
    uint64_t count = g_dummy_GetLatencyTimingsNV_calls.fetch_add(1) + 1;
    LogInfo("VulkanLoader: Dummy_vkGetLatencyTimingsNV called (driver returned null) total_calls=%llu",
            static_cast<unsigned long long>(count));
}

static PFN_vkGetInstanceProcAddr_t vkGetInstanceProcAddr_Original = nullptr;
static PFN_vkGetDeviceProcAddr_t vkGetDeviceProcAddr_Original = nullptr;
static PFN_vkCreateDevice_t g_real_vkCreateDevice = nullptr;
/** Trampoline to the real vkSetLatencyMarkerNV (filled by MinHook when we hook the real). */
static PFN_vkSetLatencyMarkerNV_t g_real_vkSetLatencyMarkerNV = nullptr;
/** First time we see the real pointer we MinHook it so callers that already cached it still hit our wrapper. */
static void* g_hooked_vkSetLatencyMarkerNV_target = nullptr;
static std::atomic<bool> g_loader_hooks_installed{false};
static std::atomic<uint64_t> g_loader_marker_count{0};
static std::atomic<int> g_loader_last_marker_type{-1};
static std::atomic<uint64_t> g_loader_last_present_id{0};
static std::atomic<uint64_t> g_loader_intercept_count{0};  // times we returned wrapper from vkGetDeviceProcAddr

/** Enabled device extensions from last vkCreateDevice (thread-safe). */
static SRWLOCK g_vulkan_extensions_lock = SRWLOCK_INIT;
static std::vector<std::string> g_vulkan_enabled_extensions;

// VK_NV_low_latency2 marker enum (same order as NvLL): 0=SIMULATION_START, 4=PRESENT_START, 5=PRESENT_END
static constexpr int VK_LATENCY_MARKER_SIMULATION_START_NV = 0;
static constexpr int VK_LATENCY_MARKER_PRESENT_START_NV = 4;
static constexpr int VK_LATENCY_MARKER_PRESENT_END_NV = 5;

// Extensions to append when vulkan_append_reflex_extensions is on (Special K style).
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

static VkResult VKAPI_CALL vkCreateDevice_Wrapper(VkPhysicalDevice physicalDevice,
                                                  const VkDeviceCreateInfo* pCreateInfo,
                                                  const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    if (g_real_vkCreateDevice == nullptr) {
        LogInfo("VulkanLoader: vkCreateDevice_Wrapper called but real pointer not set");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const bool append_extensions = settings::g_mainTabSettings.vulkan_append_reflex_extensions.GetValue();
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
                utils::SRWLockExclusive lock(g_vulkan_extensions_lock);
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
        utils::SRWLockExclusive lock(g_vulkan_extensions_lock);
        g_vulkan_enabled_extensions = std::move(exts_for_ui);
    }
    if (pCreateInfo != nullptr) {
        LogInfo("VulkanLoader: vkCreateDevice captured %u enabled extension(s)", pCreateInfo->enabledExtensionCount);
    }
    return r;
}

void VKAPI_CALL VkSetLatencyMarkerNV_Wrapper(VkDevice device, VkSwapchainKHR swapchain,
                                             const VkSetLatencyMarkerInfoNV* pLatencyMarkerInfo) {
    (void)swapchain;
    LogInfo("VulkanLoader: VkSetLatencyMarkerNV_Wrapper called marker=%d presentID=%llu", pLatencyMarkerInfo->marker,
            pLatencyMarkerInfo->presentID);
    if (pLatencyMarkerInfo != nullptr) {
        g_loader_marker_count.fetch_add(1);
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

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr_Detour(VkInstance instance, const char* pName) {
    PFN_vkGetInstanceProcAddr_t orig = vkGetInstanceProcAddr_Original;
    if (orig == nullptr) {
        return nullptr;
    }
    PFN_vkVoidFunction result = orig(instance, pName);
    if (pName != nullptr && std::strcmp(pName, "vkCreateDevice") == 0 && result != nullptr) {
        g_real_vkCreateDevice = reinterpret_cast<PFN_vkCreateDevice_t>(result);
        result = reinterpret_cast<PFN_vkVoidFunction>(&vkCreateDevice_Wrapper);
        LogInfo("VulkanLoader: vkGetInstanceProcAddr returning vkCreateDevice wrapper");
    }
    return result;
}

/** When the loader returns null for a VK_NV_low_latency2 proc, return our dummy so we can see if the game calls it. */
static PFN_vkVoidFunction ReturnDummyIfNull(const char* pName, PFN_vkVoidFunction result) {
    if (pName == nullptr || result != nullptr) return result;
    if (std::strcmp(pName, "vkSetLatencySleepModeNV") == 0) {
        LogInfo("VulkanLoader: vkGetDeviceProcAddr(%s) was null, returning dummy", pName);
        return reinterpret_cast<PFN_vkVoidFunction>(&Dummy_vkSetLatencySleepModeNV);
    }
    if (std::strcmp(pName, "vkLatencySleepNV") == 0) {
        LogInfo("VulkanLoader: vkGetDeviceProcAddr(%s) was null, returning dummy", pName);
        return reinterpret_cast<PFN_vkVoidFunction>(&Dummy_vkLatencySleepNV);
    }
    if (std::strcmp(pName, "vkSetLatencyMarkerNV") == 0) {
        LogInfo("VulkanLoader: vkGetDeviceProcAddr(%s) was null, returning dummy", pName);
        return reinterpret_cast<PFN_vkVoidFunction>(&Dummy_vkSetLatencyMarkerNV);
    }
    if (std::strcmp(pName, "vkGetLatencyTimingsNV") == 0) {
        LogInfo("VulkanLoader: vkGetDeviceProcAddr(%s) was null, returning dummy", pName);
        return reinterpret_cast<PFN_vkVoidFunction>(&Dummy_vkGetLatencyTimingsNV);
    }
    return result;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr_Detour(VkDevice device, const char* pName) {
    PFN_vkGetDeviceProcAddr_t orig = vkGetDeviceProcAddr_Original;
    if (orig == nullptr) {
        LogInfo("VulkanLoader: vkGetDeviceProcAddr_Detour called with nullptr");
        return nullptr;
    }
    PFN_vkVoidFunction result = orig(device, pName);
    LogInfo("VulkanLoader: vkGetDeviceProcAddr_Detour called with pName=%s result=%p", pName ? pName : "(null)",
            result);

    // If loader returned null for a Reflex proc, return our dummies so we can observe if the game calls them.
    result = ReturnDummyIfNull(pName, result);

    if (pName != nullptr && std::strcmp(pName, "vkSetLatencyMarkerNV") == 0 && result != nullptr) {
        // result may be real (from loader) or our dummy (from above). Only hook the real one.
        if (result != reinterpret_cast<PFN_vkVoidFunction>(&Dummy_vkSetLatencyMarkerNV)) {
            LogInfo("VulkanLoader: vkSetLatencyMarkerNV found (real)");
            g_loader_intercept_count.fetch_add(1);
            if (g_real_vkSetLatencyMarkerNV == nullptr) {
                if (CreateAndEnableHook(
                        reinterpret_cast<LPVOID>(result), reinterpret_cast<LPVOID>(&VkSetLatencyMarkerNV_Wrapper),
                        reinterpret_cast<LPVOID*>(&g_real_vkSetLatencyMarkerNV), "vkSetLatencyMarkerNV")) {
                    g_hooked_vkSetLatencyMarkerNV_target = result;
                    LogInfo(
                        "VulkanLoader: hooked real vkSetLatencyMarkerNV (callers that cached the pointer will now go "
                        "through our wrapper)");
                } else {
                    g_real_vkSetLatencyMarkerNV = reinterpret_cast<PFN_vkSetLatencyMarkerNV_t>(result);
                    LogInfo("VulkanLoader: vkSetLatencyMarkerNV found but MinHook failed, returning wrapper only");
                }
            }
            result = reinterpret_cast<PFN_vkVoidFunction>(&VkSetLatencyMarkerNV_Wrapper);
        }
    }
    return result;
}

}  // namespace

bool InstallVulkanLoaderHooks(void* vulkan1_module) {
    auto* module = static_cast<HMODULE>(vulkan1_module);
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
    if (!settings::g_mainTabSettings.vulkan_vk_loader_hooks_enabled.GetValue()) {
        LogInfo("VulkanLoader: hooks disabled by setting");
        return false;
    }

    // Hook vkGetInstanceProcAddr so we can wrap vkCreateDevice and capture enabled extensions.
    auto* pGetInstanceProcAddr =
        reinterpret_cast<PFN_vkGetInstanceProcAddr_t>(GetProcAddress(module, "vkGetInstanceProcAddr"));
    if (pGetInstanceProcAddr != nullptr) {
        if (CreateAndEnableHook(reinterpret_cast<LPVOID>(pGetInstanceProcAddr),
                                reinterpret_cast<LPVOID>(&vkGetInstanceProcAddr_Detour),
                                reinterpret_cast<LPVOID*>(&vkGetInstanceProcAddr_Original), "vkGetInstanceProcAddr")) {
            LogInfo("VulkanLoader: vkGetInstanceProcAddr hooked (vkCreateDevice wrapper for extensions)");
        } else {
            LogInfo("VulkanLoader: failed to hook vkGetInstanceProcAddr");
        }
    } else {
        LogInfo("VulkanLoader: vkGetInstanceProcAddr not found");
    }

    auto* pGetDeviceProcAddr =
        reinterpret_cast<PFN_vkGetDeviceProcAddr_t>(GetProcAddress(module, "vkGetDeviceProcAddr"));
    if (pGetDeviceProcAddr == nullptr) {
        LogInfo("VulkanLoader: vkGetDeviceProcAddr not found");
        if (vkGetInstanceProcAddr_Original != nullptr) {
            MH_DisableHook(vkGetInstanceProcAddr_Original);
            MH_RemoveHook(vkGetInstanceProcAddr_Original);
            vkGetInstanceProcAddr_Original = nullptr;
        }
        return false;
    }

    if (!CreateAndEnableHook(reinterpret_cast<LPVOID>(pGetDeviceProcAddr),
                             reinterpret_cast<LPVOID>(&vkGetDeviceProcAddr_Detour),
                             reinterpret_cast<LPVOID*>(&vkGetDeviceProcAddr_Original), "vkGetDeviceProcAddr")) {
        LogInfo("VulkanLoader: failed to hook vkGetDeviceProcAddr");
        if (vkGetInstanceProcAddr_Original != nullptr) {
            MH_DisableHook(vkGetInstanceProcAddr_Original);
            MH_RemoveHook(vkGetInstanceProcAddr_Original);
            vkGetInstanceProcAddr_Original = nullptr;
        }
        return false;
    }

    g_loader_hooks_installed.store(true);
    LogInfo("VulkanLoader: VK_NV_low_latency2 hooks installed (vkGetInstanceProcAddr + vkGetDeviceProcAddr)");
    return true;
}

void UninstallVulkanLoaderHooks() {
    if (!g_loader_hooks_installed.exchange(false)) {
        return;
    }
    if (vkGetInstanceProcAddr_Original != nullptr) {
        MH_DisableHook(vkGetInstanceProcAddr_Original);
        MH_RemoveHook(vkGetInstanceProcAddr_Original);
        vkGetInstanceProcAddr_Original = nullptr;
    }
    if (vkGetDeviceProcAddr_Original != nullptr) {
        MH_DisableHook(vkGetDeviceProcAddr_Original);
        MH_RemoveHook(vkGetDeviceProcAddr_Original);
        vkGetDeviceProcAddr_Original = nullptr;
    }
    if (g_hooked_vkSetLatencyMarkerNV_target != nullptr) {
        MH_DisableHook(g_hooked_vkSetLatencyMarkerNV_target);
        MH_RemoveHook(g_hooked_vkSetLatencyMarkerNV_target);
        g_hooked_vkSetLatencyMarkerNV_target = nullptr;
    }
    g_real_vkCreateDevice = nullptr;
    g_real_vkSetLatencyMarkerNV = nullptr;
    {
        utils::SRWLockExclusive lock(g_vulkan_extensions_lock);
        g_vulkan_enabled_extensions.clear();
    }
    LogInfo("VulkanLoader: hooks uninstalled");
}

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

void GetVulkanEnabledExtensions(std::vector<std::string>& out) {
    out.clear();
    utils::SRWLockShared lock(g_vulkan_extensions_lock);
    out = g_vulkan_enabled_extensions;
}

void GetVulkanLoaderDummyCallCounts(uint64_t* out_set_sleep_mode, uint64_t* out_latency_sleep,
                                    uint64_t* out_set_latency_marker, uint64_t* out_get_latency_timings) {
    if (out_set_sleep_mode) *out_set_sleep_mode = g_dummy_SetLatencySleepModeNV_calls.load();
    if (out_latency_sleep) *out_latency_sleep = g_dummy_LatencySleepNV_calls.load();
    if (out_set_latency_marker) *out_set_latency_marker = g_dummy_SetLatencyMarkerNV_calls.load();
    if (out_get_latency_timings) *out_get_latency_timings = g_dummy_GetLatencyTimingsNV_calls.load();
}
