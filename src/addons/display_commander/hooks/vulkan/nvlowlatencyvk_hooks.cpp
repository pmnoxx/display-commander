#include "nvlowlatencyvk_hooks.hpp"
#include "../../globals.hpp"
#include "../../settings/advanced_tab_settings.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../swapchain_events.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/srwlock_wrapper.hpp"
#include "../../utils/timing.hpp"
#include "../dxgi/dxgi_present_hooks.hpp"

#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include <Windows.h>

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

static NvLL_VK_Status NvLL_VK_InitLowLatencyDevice_Detour(void* device, void* pSignalSemaphoreHandle);
static NvLL_VK_Status NvLL_VK_SetLatencyMarker_Detour(void* device, NVLL_VK_LATENCY_MARKER_PARAMS* params);
static NvLL_VK_Status NvLL_VK_SetSleepMode_Detour(void* device, NVLL_VK_SET_SLEEP_MODE_PARAMS* params);
static NvLL_VK_Status NvLL_VK_Sleep_Detour(void* device, uint64_t signalValue);

static NvLL_VK_InitLowLatencyDevice_pfn NvLL_VK_InitLowLatencyDevice_Original = nullptr;
static NvLL_VK_SetLatencyMarker_pfn NvLL_VK_SetLatencyMarker_Original = nullptr;
static NvLL_VK_SetSleepMode_pfn NvLL_VK_SetSleepMode_Original = nullptr;
static NvLL_VK_Sleep_pfn NvLL_VK_Sleep_Original = nullptr;

/** Table-driven hook install: name, detour, original. InitLowLatencyDevice is optional (skipped if not exported). */
struct NvllVkHookEntry {
    const char* name;
    LPVOID detour;
    LPVOID* original;
};
static constexpr std::size_t K_NVLL_VK_HOOK_COUNT = static_cast<std::size_t>(NvllVkHook::kNvllVkHookCount);
static const std::array<NvllVkHookEntry, K_NVLL_VK_HOOK_COUNT> K_NVLL_VK_HOOKS = {{
    {.name = "NvLL_VK_InitLowLatencyDevice",
     .detour = reinterpret_cast<LPVOID>(&NvLL_VK_InitLowLatencyDevice_Detour),
     .original = reinterpret_cast<LPVOID*>(&NvLL_VK_InitLowLatencyDevice_Original)},
    {.name = "NvLL_VK_SetLatencyMarker",
     .detour = reinterpret_cast<LPVOID>(&NvLL_VK_SetLatencyMarker_Detour),
     .original = reinterpret_cast<LPVOID*>(&NvLL_VK_SetLatencyMarker_Original)},
    {.name = "NvLL_VK_SetSleepMode",
     .detour = reinterpret_cast<LPVOID>(&NvLL_VK_SetSleepMode_Detour),
     .original = reinterpret_cast<LPVOID*>(&NvLL_VK_SetSleepMode_Original)},
    {.name = "NvLL_VK_Sleep",
     .detour = reinterpret_cast<LPVOID>(&NvLL_VK_Sleep_Detour),
     .original = reinterpret_cast<LPVOID*>(&NvLL_VK_Sleep_Original)},
}};

static std::atomic<bool> g_nvll_hooks_installed{false};
/** Call counts per hook (indexed by NvllVkHook); incremented on each detour entry. */
static std::atomic<uint64_t> g_nvll_hook_call_counts[K_NVLL_VK_HOOK_COUNT]{};
static std::atomic<uint64_t> g_nvll_marker_count_by_type[kNvllVkMarkerTypeCount] = {};
static std::atomic<int> g_nvll_last_marker_type{-1};
static std::atomic<uint64_t> g_nvll_last_frame_id{0};

// Last params the game tried to set via NvLL_VK_SetSleepMode (for re-apply on SIMULATION_START when not overriding)
static NVLL_VK_SET_SLEEP_MODE_PARAMS g_last_nvll_vk_game_sleep_mode_params = {};
static void* g_last_nvll_vk_sleep_mode_device = nullptr;
static std::atomic<bool> g_nvll_vk_has_stored_game_params{false};
// Last params actually sent to NvLL_VK_SetSleepMode_Original (for UI)
static NVLL_VK_SET_SLEEP_MODE_PARAMS g_last_nvll_vk_applied_sleep_mode_params = {};
static std::atomic<bool> g_nvll_vk_has_applied_params{false};

static NvLL_VK_Status NvLL_VK_SetLatencyMarker_Detour(void* device, NVLL_VK_LATENCY_MARKER_PARAMS* params) {
    g_nvll_hook_call_counts[static_cast<std::size_t>(NvllVkHook::SetLatencyMarker)].fetch_add(1);
    if (params == nullptr) {
        if (NvLL_VK_SetLatencyMarker_Original != nullptr) {
            return NvLL_VK_SetLatencyMarker_Original(device, params);
        }
        return 1;
    }
    static bool first_call = true;
    if (first_call) {
        first_call = false;
        LogInfo("NvLowLatencyVk: SetLatencyMarker first call");
    }
    const int marker_type = static_cast<int>(params->markerType);
    if (marker_type >= 0 && marker_type < static_cast<int>(kNvllVkMarkerTypeCount)) {
        g_nvll_marker_count_by_type[marker_type].fetch_add(1);
    }
    g_nvll_last_marker_type.store(marker_type);
    g_nvll_last_frame_id.store(params->frameID);

    // Re-apply SleepMode on SIMULATION_START (same idea as D3D ApplySleepMode on present): either our
    // overridden params or the last params the game tried to set.
    if (params != nullptr && params->markerType == VK_SIMULATION_START && NvLL_VK_SetSleepMode_Original != nullptr) {
        if (!settings::g_advancedTabSettings.reflex_supress_native.GetValue()) {
            if (ShouldReflexBeEnabled()) {
                const float fps_limit = GetTargetFps();
                NVLL_VK_SET_SLEEP_MODE_PARAMS overridden = {};
                overridden.bLowLatencyMode = ShouldReflexLowLatencyBeEnabled();
                overridden.bLowLatencyBoost = ShouldReflexBoostBeEnabled();
                overridden.minimumIntervalUs = (fps_limit > 0.0f && ShouldUseReflexAsFpsLimiter())
                                                   ? static_cast<uint32_t>(1000000.0f / fps_limit)
                                                   : 0u;
                {
                    utils::SRWLockExclusive lock(utils::g_nvll_sleep_mode_params_lock);
                    g_last_nvll_vk_applied_sleep_mode_params = overridden;
                    g_nvll_vk_has_applied_params.store(true);
                }
                (void)NvLL_VK_SetSleepMode_Original(device, &overridden);
            } else if (g_nvll_vk_has_stored_game_params.load()) {
                NVLL_VK_SET_SLEEP_MODE_PARAMS stored;
                void* stored_device = nullptr;
                {
                    utils::SRWLockShared lock(utils::g_nvll_sleep_mode_params_lock);
                    stored = g_last_nvll_vk_game_sleep_mode_params;
                    stored_device = g_last_nvll_vk_sleep_mode_device;
                }
                if (stored_device == device) {
                    {
                        utils::SRWLockExclusive lock(utils::g_nvll_sleep_mode_params_lock);
                        g_last_nvll_vk_applied_sleep_mode_params = stored;
                        g_nvll_vk_has_applied_params.store(true);
                    }
                    if (NvLL_VK_SetSleepMode_Original != nullptr) {
                        (void)NvLL_VK_SetSleepMode_Original(device, &stored);
                    }
                }
            }
        }
    }

    if (NvLL_VK_SetLatencyMarker_Original == nullptr) {
        return 1;  // error
    }
    // TODO: explain this, needed for Vulkan fps limiter
    const ReflexMarkerTypes vk_nvll_markers = {
        static_cast<int>(VK_SIMULATION_START),
        static_cast<int>(VK_PRESENT_START) - 1,
        static_cast<int>(VK_PRESENT_END) - 2,
    };
    const int r = ProcessReflexMarkerFpsLimiter(
        FpsLimiterCallSite::reflex_marker_vk_nvll, static_cast<int>(params->markerType), params->frameID,
        vk_nvll_markers, [&]() { return (NvLL_VK_SetLatencyMarker_Original(device, params) == NVLL_VK_OK) ? 0 : 1; });
    return (r == 0) ? NVLL_VK_OK : static_cast<NvLL_VK_Status>(r);
}

static NvLL_VK_Status NvLL_VK_InitLowLatencyDevice_Detour(void* device, void* pSignalSemaphoreHandle) {
    g_nvll_hook_call_counts[static_cast<std::size_t>(NvllVkHook::InitLowLatencyDevice)].fetch_add(1);
    if (NvLL_VK_InitLowLatencyDevice_Original == nullptr) {
        return 1;
    }
    return NvLL_VK_InitLowLatencyDevice_Original(device, pSignalSemaphoreHandle);
}

static NvLL_VK_Status NvLL_VK_SetSleepMode_Detour(void* device, NVLL_VK_SET_SLEEP_MODE_PARAMS* params) {
    g_nvll_hook_call_counts[static_cast<std::size_t>(NvllVkHook::SetSleepMode)].fetch_add(1);
    if (NvLL_VK_SetSleepMode_Original == nullptr) {
        return 1;
    }
    // Record last params the game tried to set (for re-apply on SIMULATION_START when not overriding)
    if (params != nullptr) {
        utils::SRWLockExclusive lock(utils::g_nvll_sleep_mode_params_lock);
        g_last_nvll_vk_game_sleep_mode_params = *params;
        g_last_nvll_vk_sleep_mode_device = device;
        g_nvll_vk_has_stored_game_params.store(true);
        SetGameReflexSleepModeParams(params->bLowLatencyMode, params->bLowLatencyBoost, params->minimumIntervalUs);
    }
    // Override params from addon Reflex settings (same idea as D3D ApplySleepMode on present path).
    // For Vulkan there is no ReflexManager/ApplySleepMode on present, so we override in the detour.
    if (settings::g_advancedTabSettings.reflex_supress_native.GetValue()) {
        return NVLL_VK_OK;
    }
    if (ShouldReflexBeEnabled()) {
        const float fps_limit = GetTargetFps();
        NVLL_VK_SET_SLEEP_MODE_PARAMS overridden = {};
        overridden.bLowLatencyMode = ShouldReflexLowLatencyBeEnabled();
        overridden.bLowLatencyBoost = ShouldReflexBoostBeEnabled();
        overridden.minimumIntervalUs =
            (fps_limit > 0.0f && ShouldUseReflexAsFpsLimiter()) ? static_cast<uint32_t>(1000000.0f / fps_limit) : 0u;
        {
            utils::SRWLockExclusive lock(utils::g_nvll_sleep_mode_params_lock);
            g_last_nvll_vk_applied_sleep_mode_params = overridden;
            g_nvll_vk_has_applied_params.store(true);
        }
        return NvLL_VK_SetSleepMode_Original(device, &overridden);
    }
    if (params != nullptr) {
        utils::SRWLockExclusive lock(utils::g_nvll_sleep_mode_params_lock);
        g_last_nvll_vk_applied_sleep_mode_params = *params;
        g_nvll_vk_has_applied_params.store(true);
    }
    return NvLL_VK_SetSleepMode_Original(device, params);
}

static NvLL_VK_Status NvLL_VK_Sleep_Detour(void* device, uint64_t signalValue) {
    g_nvll_hook_call_counts[static_cast<std::size_t>(NvllVkHook::Sleep)].fetch_add(1);
    if (NvLL_VK_Sleep_Original == nullptr) {
        return 1;
    }
    return NvLL_VK_Sleep_Original(device, signalValue);
}

static void RollbackNvllVkHooks() {
    for (std::size_t j = 0; j < K_NVLL_VK_HOOK_COUNT; ++j) {
        LPVOID* const orig = K_NVLL_VK_HOOKS[j].original;
        if (orig != nullptr && *orig != nullptr) {
            MH_DisableHook(*orig);
            MH_RemoveHook(*orig);
            *orig = nullptr;
        }
    }
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

    for (std::size_t i = 0; i < K_NVLL_VK_HOOK_COUNT; ++i) {
        const NvllVkHookEntry& entry = K_NVLL_VK_HOOKS[i];
        FARPROC target = GetProcAddress(nvll_module, entry.name);
        if (target == nullptr) {
            if (i == static_cast<std::size_t>(NvllVkHook::InitLowLatencyDevice)) {
                LogInfo("NvLowLatencyVk: %s not exported, skipping", entry.name);
                continue;
            }
            LogInfo("NvLowLatencyVk: %s not exported", entry.name);
            return false;
        }
        if (!CreateAndEnableHook(reinterpret_cast<LPVOID>(target), entry.detour, entry.original, entry.name)) {
            LogInfo("NvLowLatencyVk: failed to hook %s", entry.name);
            RollbackNvllVkHooks();
            return false;
        }
    }

    g_nvll_hooks_installed.store(true);
    LogInfo("NvLowLatencyVk: hooks installed successfully NvLowLatencyVk.dll=%p", nvll_module);
    return true;
}

bool AreNvLowLatencyVkHooksInstalled() { return g_nvll_hooks_installed.load(); }

void UninstallNvLowLatencyVkHooks() {
    if (!g_nvll_hooks_installed.exchange(false)) {
        return;
    }
    RollbackNvllVkHooks();
    LogInfo("NvLowLatencyVk: hooks uninstalled on unload");
}

void GetNvLowLatencyVkLastMarkerState(int* out_last_marker_type, uint64_t* out_last_frame_id) {
    if (out_last_marker_type) {
        *out_last_marker_type = g_nvll_last_marker_type.load();
    }
    if (out_last_frame_id) {
        *out_last_frame_id = g_nvll_last_frame_id.load();
    }
}

void GetNvllVkHookCallCounts(uint64_t* out_counts, std::size_t count) {
    if (out_counts == nullptr) return;
    const std::size_t n = (count < K_NVLL_VK_HOOK_COUNT) ? count : K_NVLL_VK_HOOK_COUNT;
    for (std::size_t i = 0; i < n; ++i) {
        out_counts[i] = g_nvll_hook_call_counts[i].load();
    }
}

void GetNvLowLatencyVkMarkerCountsByType(uint64_t* out_counts, size_t max_count) {
    if (out_counts == nullptr) return;
    const size_t n = (max_count < kNvllVkMarkerTypeCount) ? max_count : kNvllVkMarkerTypeCount;
    for (size_t i = 0; i < n; ++i) {
        out_counts[i] = g_nvll_marker_count_by_type[i].load();
    }
}

const char* GetNvllVkHookName(NvllVkHook hook) {
    static const char* const kNames[] = {
        "NvLL_VK_InitLowLatencyDevice",
        "NvLL_VK_SetLatencyMarker",
        "NvLL_VK_SetSleepMode",
        "NvLL_VK_Sleep",
    };
    const std::size_t idx = static_cast<std::size_t>(hook);
    if (idx >= K_NVLL_VK_HOOK_COUNT) {
        return "(unknown)";
    }
    return kNames[idx];
}

const char* GetNvLowLatencyVkMarkerTypeName(int index) {
    if (index < 0 || index >= static_cast<int>(kNvllVkMarkerTypeCount)) return "?";
    switch (index) {
        case 0:  return "SIMULATION_START";
        case 1:  return "SIMULATION_END";
        case 2:  return "RENDERSUBMIT_START";
        case 3:  return "RENDERSUBMIT_END";
        case 4:  return "PRESENT_START";
        case 5:  return "PRESENT_END";
        case 6:  return "INPUT_SAMPLE";
        case 7:  return "TRIGGER_FLASH";
        case 8:  return "PC_LATENCY_PING";
        default: return "?";
    }
}

void GetNvLowLatencyVkLastAppliedSleepModeParams(NvLLVkSleepModeParamsView* out) {
    if (out == nullptr) return;
    *out = {};
    if (!g_nvll_vk_has_applied_params.load()) return;
    utils::SRWLockShared lock(utils::g_nvll_sleep_mode_params_lock);
    out->low_latency = g_last_nvll_vk_applied_sleep_mode_params.bLowLatencyMode;
    out->boost = g_last_nvll_vk_applied_sleep_mode_params.bLowLatencyBoost;
    out->minimum_interval_us = g_last_nvll_vk_applied_sleep_mode_params.minimumIntervalUs;
    out->has_value = true;
}

void GetNvLowLatencyVkGameSleepModeParams(NvLLVkSleepModeParamsView* out) {
    if (out == nullptr) return;
    *out = {};
    if (!g_nvll_vk_has_stored_game_params.load()) return;
    utils::SRWLockShared lock(utils::g_nvll_sleep_mode_params_lock);
    out->low_latency = g_last_nvll_vk_game_sleep_mode_params.bLowLatencyMode;
    out->boost = g_last_nvll_vk_game_sleep_mode_params.bLowLatencyBoost;
    out->minimum_interval_us = g_last_nvll_vk_game_sleep_mode_params.minimumIntervalUs;
    out->has_value = true;
}
