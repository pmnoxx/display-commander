#include "nvlowlatencyvk_hooks.hpp"
#include "../../globals.hpp"
#include "../../settings/advanced_tab_settings.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../swapchain_events.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"
#include "../dxgi/dxgi_present_hooks.hpp"

#include <MinHook.h>
#include <atomic>
#include <cstdint>

// NvLowLatencyVk.dll path: hook SetLatencyMarker, Sleep, SetSleepMode (same as Special K SK_CreateDLLHook2).
// Many Vulkan Reflex games (e.g. Doom) use VK_NV_low_latency2 only (vkSetLatencyMarkerNV from vulkan-1.dll)
// and never load NvLowLatencyVk.dll, so NvLL_VK_SetLatencyMarker is not called for them; vulkan_loader_hooks
// handle that path. Minimal NvLowLatencyVk types (match Special K / NVIDIA; VkDevice as void*).
typedef DWORD NvLL_VK_Status;
static constexpr NvLL_VK_Status NVLL_VK_OK = 0;

enum NVLL_VK_LATENCY_MARKER_TYPE {
    VK_SIMULATION_START = 0,
    VK_SIMULATION_END = 1,
    VK_RENDERSUBMIT_START = 2,
    VK_RENDERSUBMIT_END = 3,
    VK_PRESENT_START = 4,
    VK_PRESENT_END = 5,
    VK_INPUT_SAMPLE = 6,
    VK_TRIGGER_FLASH = 7,
    VK_PC_LATENCY_PING = 8,
};

struct NVLL_VK_LATENCY_MARKER_PARAMS {
    uint64_t frameID;
    NVLL_VK_LATENCY_MARKER_TYPE markerType;
};

struct NVLL_VK_SET_SLEEP_MODE_PARAMS {
    bool bLowLatencyMode;
    bool bLowLatencyBoost;
    uint32_t minimumIntervalUs;
};

using NvLL_VK_SetLatencyMarker_pfn = NvLL_VK_Status (*)(void* device, NVLL_VK_LATENCY_MARKER_PARAMS* params);
using NvLL_VK_SetSleepMode_pfn = NvLL_VK_Status (*)(void* device, NVLL_VK_SET_SLEEP_MODE_PARAMS* params);
using NvLL_VK_Sleep_pfn = NvLL_VK_Status (*)(void* device, uint64_t signalValue);
// InitLowLatencyDevice(device, pSignalSemaphoreHandle) - same as Special K
using NvLL_VK_InitLowLatencyDevice_pfn = NvLL_VK_Status (*)(void* device, void* pSignalSemaphoreHandle);

static NvLL_VK_InitLowLatencyDevice_pfn NvLL_VK_InitLowLatencyDevice_Original = nullptr;
static NvLL_VK_SetLatencyMarker_pfn NvLL_VK_SetLatencyMarker_Original = nullptr;
static NvLL_VK_SetSleepMode_pfn NvLL_VK_SetSleepMode_Original = nullptr;
static NvLL_VK_Sleep_pfn NvLL_VK_Sleep_Original = nullptr;

static std::atomic<bool> g_nvll_hooks_installed{false};
static std::atomic<uint64_t> g_nvll_init_call_count{0};
static std::atomic<uint64_t> g_nvll_marker_call_count{0};
static std::atomic<uint64_t> g_nvll_set_sleep_mode_call_count{0};
static std::atomic<uint64_t> g_nvll_sleep_call_count{0};
static std::atomic<int> g_nvll_last_marker_type{-1};
static std::atomic<uint64_t> g_nvll_last_frame_id{0};

static NvLL_VK_Status NvLL_VK_SetLatencyMarker_Detour(void* device, NVLL_VK_LATENCY_MARKER_PARAMS* params) {
    static bool first_call = true;
    if (first_call) {
        first_call = false;
        LogInfo("NvLowLatencyVk: SetLatencyMarker first call");
    }
    if (params != nullptr) {
        g_nvll_marker_call_count.fetch_add(1);
        g_nvll_last_marker_type.store(static_cast<int>(params->markerType));
        g_nvll_last_frame_id.store(params->frameID);
    }

    const uint64_t now_ns = static_cast<uint64_t>(utils::get_now_ns());
    if (params != nullptr && params->markerType == VK_PRESENT_START) {
        ChooseFpsLimiter(now_ns, FpsLimiterCallSite::reflex_marker_vk_nvll);
    }

    const bool native_pacing_sim_start_only = settings::g_mainTabSettings.native_pacing_sim_start_only.GetValue();
    bool use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::reflex_marker_vk_nvll);

    if (native_pacing_sim_start_only) {
        if (use_fps_limiter && params != nullptr) {
            if (params->markerType == VK_SIMULATION_START) {
                OnPresentFlags2(false, true);
                RecordNativeFrameTime();
            }
            if (params->markerType == VK_SIMULATION_START) {
                display_commanderhooks::dxgi::HandlePresentAfter(true);
            }
        }
    } else {
        if (use_fps_limiter && params != nullptr) {
            if (params->markerType == VK_PRESENT_START) {
                OnPresentFlags2(false, true);
                RecordNativeFrameTime();
            }
            if (params->markerType == VK_PRESENT_END) {
                display_commanderhooks::dxgi::HandlePresentAfter(true);
            }
        }
    }

    if (NvLL_VK_SetLatencyMarker_Original == nullptr) {
        return 1;  // error
    }
    return NvLL_VK_SetLatencyMarker_Original(device, params);
}

static NvLL_VK_Status NvLL_VK_InitLowLatencyDevice_Detour(void* device, void* pSignalSemaphoreHandle) {
    g_nvll_init_call_count.fetch_add(1);
    if (NvLL_VK_InitLowLatencyDevice_Original == nullptr) {
        return 1;
    }
    return NvLL_VK_InitLowLatencyDevice_Original(device, pSignalSemaphoreHandle);
}

static NvLL_VK_Status NvLL_VK_SetSleepMode_Detour(void* device, NVLL_VK_SET_SLEEP_MODE_PARAMS* params) {
    g_nvll_set_sleep_mode_call_count.fetch_add(1);
    if (NvLL_VK_SetSleepMode_Original == nullptr) {
        return 1;
    }
    // Override params from addon Reflex settings (same idea as D3D ApplySleepMode on present path).
    // For Vulkan there is no ReflexManager/ApplySleepMode on present, so we override in the detour.
    if (settings::g_advancedTabSettings.reflex_supress_native.GetValue()) {
        return NVLL_VK_OK;
    }
    if (settings::g_advancedTabSettings.reflex_enable.GetValue()) {
        const float fps_limit = GetTargetFps();
        NVLL_VK_SET_SLEEP_MODE_PARAMS overridden = {};
        overridden.bLowLatencyMode = settings::g_advancedTabSettings.reflex_low_latency.GetValue();
        overridden.bLowLatencyBoost = settings::g_advancedTabSettings.reflex_boost.GetValue();
        overridden.minimumIntervalUs =
            (fps_limit > 0.0f) ? static_cast<uint32_t>(1000000.0f / fps_limit) : 0u;
        return NvLL_VK_SetSleepMode_Original(device, &overridden);
    }
    return NvLL_VK_SetSleepMode_Original(device, params);
}

static NvLL_VK_Status NvLL_VK_Sleep_Detour(void* device, uint64_t signalValue) {
    g_nvll_sleep_call_count.fetch_add(1);
    if (NvLL_VK_Sleep_Original == nullptr) {
        return 1;
    }
    return NvLL_VK_Sleep_Original(device, signalValue);
}

bool InstallNvLowLatencyVkHooks(HMODULE nvll_module) {
    if (nvll_module == nullptr) {
        nvll_module = GetModuleHandleW(L"NvLowLatencyVk.dll");
    }
    if (nvll_module == nullptr) {
        LogInfo("NvLowLatencyVk: DLL not loaded");
        return false;
    }
    if (g_nvll_hooks_installed.load()) {
        LogInfo("NvLowLatencyVk: hooks already installed");
        return true;
    }
    if (!settings::g_mainTabSettings.vulkan_nvll_hooks_enabled.GetValue()) {
        LogInfo("NvLowLatencyVk: hooks disabled by setting");
        return false;
    }

    auto* pInitLowLatencyDevice =
        reinterpret_cast<NvLL_VK_InitLowLatencyDevice_pfn>(GetProcAddress(nvll_module, "NvLL_VK_InitLowLatencyDevice"));
    auto* pSetLatencyMarker =
        reinterpret_cast<NvLL_VK_SetLatencyMarker_pfn>(GetProcAddress(nvll_module, "NvLL_VK_SetLatencyMarker"));
    auto* pSetSleepMode =
        reinterpret_cast<NvLL_VK_SetSleepMode_pfn>(GetProcAddress(nvll_module, "NvLL_VK_SetSleepMode"));
    auto* pSleep = reinterpret_cast<NvLL_VK_Sleep_pfn>(GetProcAddress(nvll_module, "NvLL_VK_Sleep"));

    if (pSetLatencyMarker == nullptr || pSetSleepMode == nullptr || pSleep == nullptr) {
        LogInfo("NvLowLatencyVk: one or more exports not found");
        return false;
    }

    if (pInitLowLatencyDevice != nullptr) {
        if (!CreateAndEnableHook(reinterpret_cast<LPVOID>(pInitLowLatencyDevice),
                                 reinterpret_cast<LPVOID>(&NvLL_VK_InitLowLatencyDevice_Detour),
                                 reinterpret_cast<LPVOID*>(&NvLL_VK_InitLowLatencyDevice_Original),
                                 "NvLL_VK_InitLowLatencyDevice")) {
            LogInfo("NvLowLatencyVk: failed to hook InitLowLatencyDevice");
            return false;
        }
    }

    if (!CreateAndEnableHook(
            reinterpret_cast<LPVOID>(pSetLatencyMarker), reinterpret_cast<LPVOID>(&NvLL_VK_SetLatencyMarker_Detour),
            reinterpret_cast<LPVOID*>(&NvLL_VK_SetLatencyMarker_Original), "NvLL_VK_SetLatencyMarker")) {
        LogInfo("NvLowLatencyVk: failed to hook SetLatencyMarker");
        return false;
    }
    if (!CreateAndEnableHook(reinterpret_cast<LPVOID>(pSetSleepMode),
                             reinterpret_cast<LPVOID>(&NvLL_VK_SetSleepMode_Detour),
                             reinterpret_cast<LPVOID*>(&NvLL_VK_SetSleepMode_Original), "NvLL_VK_SetSleepMode")) {
        LogInfo("NvLowLatencyVk: failed to hook SetSleepMode");
        return false;
    }
    if (!CreateAndEnableHook(reinterpret_cast<LPVOID>(pSleep), reinterpret_cast<LPVOID>(&NvLL_VK_Sleep_Detour),
                             reinterpret_cast<LPVOID*>(&NvLL_VK_Sleep_Original), "NvLL_VK_Sleep")) {
        LogInfo("NvLowLatencyVk: failed to hook Sleep");
        return false;
    }

    g_nvll_hooks_installed.store(true);
    LogInfo("NvLowLatencyVk: hooks installed successfully NvLowLatencyVk.dll=%p", nvll_module);
    return true;
}

void UninstallNvLowLatencyVkHooks() {
    if (!g_nvll_hooks_installed.exchange(false)) {
        return;
    }
    if (NvLL_VK_InitLowLatencyDevice_Original) {
        MH_DisableHook(NvLL_VK_InitLowLatencyDevice_Original);
        MH_RemoveHook(NvLL_VK_InitLowLatencyDevice_Original);
        NvLL_VK_InitLowLatencyDevice_Original = nullptr;
    }
    if (NvLL_VK_SetLatencyMarker_Original) {
        MH_DisableHook(NvLL_VK_SetLatencyMarker_Original);
        MH_RemoveHook(NvLL_VK_SetLatencyMarker_Original);
        NvLL_VK_SetLatencyMarker_Original = nullptr;
    }
    if (NvLL_VK_SetSleepMode_Original) {
        MH_DisableHook(NvLL_VK_SetSleepMode_Original);
        MH_RemoveHook(NvLL_VK_SetSleepMode_Original);
        NvLL_VK_SetSleepMode_Original = nullptr;
    }
    if (NvLL_VK_Sleep_Original) {
        MH_DisableHook(NvLL_VK_Sleep_Original);
        MH_RemoveHook(NvLL_VK_Sleep_Original);
        NvLL_VK_Sleep_Original = nullptr;
    }
    LogInfo("NvLowLatencyVk: hooks uninstalled");
}

bool AreNvLowLatencyVkHooksInstalled() { return g_nvll_hooks_installed.load(); }

void GetNvLowLatencyVkDebugState(uint64_t* out_marker_count, int* out_last_marker_type, uint64_t* out_last_frame_id) {
    if (out_marker_count) {
        *out_marker_count = g_nvll_marker_call_count.load();
    }
    if (out_last_marker_type) {
        *out_last_marker_type = g_nvll_last_marker_type.load();
    }
    if (out_last_frame_id) {
        *out_last_frame_id = g_nvll_last_frame_id.load();
    }
}

void GetNvLowLatencyVkDetourCallCounts(uint64_t* out_init_count, uint64_t* out_set_latency_marker_count,
                                       uint64_t* out_set_sleep_mode_count, uint64_t* out_sleep_count) {
    if (out_init_count) {
        *out_init_count = g_nvll_init_call_count.load();
    }
    if (out_set_latency_marker_count) {
        *out_set_latency_marker_count = g_nvll_marker_call_count.load();
    }
    if (out_set_sleep_mode_count) {
        *out_set_sleep_mode_count = g_nvll_set_sleep_mode_call_count.load();
    }
    if (out_sleep_count) {
        *out_sleep_count = g_nvll_sleep_call_count.load();
    }
}
