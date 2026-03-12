#include "vulkan_loader_hooks.hpp"
#include "../../globals.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../hook_suppression_manager.hpp"
#include "../../swapchain_events.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/srwlock_wrapper.hpp"
#include "../../utils/timing.hpp"
#include "../dxgi/dxgi_present_hooks.hpp"

#include <MinHook.h>
#include <atomic>
#include <cstddef>
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

static PFN_vkGetInstanceProcAddr_t vkGetInstanceProcAddr_Original = nullptr;
static PFN_vkGetDeviceProcAddr_t vkGetDeviceProcAddr_Original = nullptr;
static PFN_vkCreateDevice_t g_real_vkCreateDevice = nullptr;

// Forward declarations for the detour table (defined below).
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr_Detour(VkInstance instance, const char* pName);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr_Detour(VkDevice device, const char* pName);

/** Table-driven hook install: name, detour, original (same order as InstallVulkanLoaderHooks loop). */
struct VulkanLoaderHookEntry {
    const char* name;
    LPVOID detour;
    LPVOID* original;
};
static constexpr std::size_t kVulkanLoaderHookCount = 2;
static const VulkanLoaderHookEntry kVulkanLoaderHooks[kVulkanLoaderHookCount] = {
    {"vkGetInstanceProcAddr", reinterpret_cast<LPVOID>(&vkGetInstanceProcAddr_Detour),
     reinterpret_cast<LPVOID*>(&vkGetInstanceProcAddr_Original)},
    {"vkGetDeviceProcAddr", reinterpret_cast<LPVOID>(&vkGetDeviceProcAddr_Detour),
     reinterpret_cast<LPVOID*>(&vkGetDeviceProcAddr_Original)},
};
/** Trampoline to the real vkSetLatencyMarkerNV (filled by MinHook when we hook the real). */
static PFN_vkSetLatencyMarkerNV_t g_real_vkSetLatencyMarkerNV = nullptr;
/** First time we see the real pointer we MinHook it so callers that already cached it still hit our detour. */
static void* g_hooked_vkSetLatencyMarkerNV_target = nullptr;
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
/** True once vkCreateDevice_Detour has been entered at least once (so UI can show "not called yet" vs "no extensions"). */
static std::atomic<bool> g_vkCreateDevice_detour_ever_called{false};

// VK_NV_low_latency2 marker enum (same order as NvLL): 0=SIMULATION_START, 4=PRESENT_START, 5=PRESENT_END
static constexpr int VK_LATENCY_MARKER_SIMULATION_START_NV = 0;
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

static VkResult VKAPI_CALL vkCreateDevice_Detour(VkPhysicalDevice physicalDevice,
                                                 const VkDeviceCreateInfo* pCreateInfo,
                                                 const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    g_calls_vkCreateDevice.fetch_add(1);
    g_vkCreateDevice_detour_ever_called.store(true);
    if (g_real_vkCreateDevice == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const bool append_extensions = true;  // Always append Reflex extensions in vkCreateDevice.
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

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr_Detour(VkInstance instance, const char* pName) {
    g_calls_vkGetInstanceProcAddr.fetch_add(1);
    if (vkGetInstanceProcAddr_Original == nullptr) {
        return nullptr;
    }
    PFN_vkVoidFunction result = vkGetInstanceProcAddr_Original(instance, pName);
    if (pName != nullptr && std::strcmp(pName, "vkCreateDevice") == 0 && result != nullptr) {
        g_real_vkCreateDevice = reinterpret_cast<PFN_vkCreateDevice_t>(result);
        result = reinterpret_cast<PFN_vkVoidFunction>(&vkCreateDevice_Detour);
        LogInfo("VulkanLoader: vkGetInstanceProcAddr returning vkCreateDevice detour");
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
            if (CreateAndEnableHook(
                    reinterpret_cast<LPVOID>(result), reinterpret_cast<LPVOID>(&vkSetLatencyMarkerNV_Detour),
                    reinterpret_cast<LPVOID*>(&g_real_vkSetLatencyMarkerNV), "vkSetLatencyMarkerNV")) {
                g_hooked_vkSetLatencyMarkerNV_target = result;
            } else {
                g_real_vkSetLatencyMarkerNV = reinterpret_cast<PFN_vkSetLatencyMarkerNV_t>(result);
            }
        }
        result = reinterpret_cast<PFN_vkVoidFunction>(&vkSetLatencyMarkerNV_Detour);
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
    LogInfo("VulkanLoader: VK_NV_low_latency2 hooks installed (vkGetInstanceProcAddr + vkGetDeviceProcAddr)");
    return true;
}

}  // namespace

bool InstallVulkanLoaderHooks(void* vulkan1_module) {
    return InstallVulkanLoaderHooksImpl(vulkan1_module);
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
