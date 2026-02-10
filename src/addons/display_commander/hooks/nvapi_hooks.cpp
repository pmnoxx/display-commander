#include "nvapi_hooks.hpp"
#include "../../../external/nvapi/nvapi_interface.h"
#include "../globals.hpp"
#include "../settings/advanced_tab_settings.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../swapchain_events.hpp"
#include "../utils/detour_call_tracker.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "../utils/srwlock_wrapper.hpp"
#include "../utils/timing.hpp"
#include "dxgi/dxgi_present_hooks.hpp"
#include "hook_suppression_manager.hpp"

#include <MinHook.h>
#include <algorithm>

// Function pointer type definitions (following Special-K's approach)
using NvAPI_D3D_SetLatencyMarker_pfn = NvAPI_Status(__cdecl*)(__in IUnknown* pDev,
                                                              __in NV_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams);
using NvAPI_D3D_SetSleepMode_pfn = NvAPI_Status(__cdecl*)(__in IUnknown* pDev,
                                                          __in NV_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams);
using NvAPI_D3D_Sleep_pfn = NvAPI_Status(__cdecl*)(__in IUnknown* pDev);
using NvAPI_D3D_GetLatency_pfn = NvAPI_Status(__cdecl*)(__in IUnknown* pDev,
                                                        __in NV_LATENCY_RESULT_PARAMS* pGetLatencyParams);
using NvAPI_D3D_GetSleepStatus_pfn = NvAPI_Status(__cdecl*)(__in IUnknown* pDev,
                                                            __in NV_GET_SLEEP_STATUS_PARAMS* pGetSleepStatusParams);

// Original function pointers
NvAPI_Disp_GetHdrCapabilities_pfn NvAPI_Disp_GetHdrCapabilities_Original = nullptr;
NvAPI_D3D_SetLatencyMarker_pfn NvAPI_D3D_SetLatencyMarker_Original = nullptr;
NvAPI_D3D_SetSleepMode_pfn NvAPI_D3D_SetSleepMode_Original = nullptr;
NvAPI_D3D_Sleep_pfn NvAPI_D3D_Sleep_Original = nullptr;
NvAPI_D3D_GetLatency_pfn NvAPI_D3D_GetLatency_Original = nullptr;
NvAPI_D3D_GetSleepStatus_pfn NvAPI_D3D_GetSleepStatus_Original = nullptr;

// SRWLOCK for thread-safe NVAPI hook access
static SRWLOCK g_nvapi_lock = SRWLOCK_INIT;

// Timer handle for delay-present-start wait (created on first use in wait_until_ns)
static HANDLE g_timer_handle_delay_present_start = nullptr;

// Function to look up NVAPI function ID from interface table
namespace {
NvU32 GetNvAPIFunctionId(const char* functionName) {
    for (int i = 0; nvapi_interface_table[i].func != nullptr; i++) {
        if (strcmp(nvapi_interface_table[i].func, functionName) == 0) {
            return nvapi_interface_table[i].id;
        }
    }

    LogInfo("NVAPI hooks: Function '%s' not found in interface table", functionName);
    return 0;
}

}  // namespace

// Hooked NvAPI_Disp_GetHdrCapabilities function
NvAPI_Status __cdecl NvAPI_Disp_GetHdrCapabilities_Detour(NvU32 displayId, NV_HDR_CAPABILITIES* pHdrCapabilities) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // utils::SRWLockExclusive lock(g_nvapi_lock);
    // Increment counter
    g_nvapi_event_counters[NVAPI_EVENT_GET_HDR_CAPABILITIES].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 3) {
        LogInfo("NVAPI HDR Capabilities called - DisplayId: %u s_hide_hdr_capabilities: %d", displayId,
                s_hide_hdr_capabilities.load());
        log_count++;
    }

    // Check if HDR hiding is enabled
    extern std::atomic<bool> s_hide_hdr_capabilities;
    if (s_hide_hdr_capabilities.load()) {
        // Hide HDR capabilities by returning a modified structure
        if (pHdrCapabilities != nullptr) {
            // Call original function first to get the real capabilities
            NvAPI_Status result = NVAPI_OK;
            if (NvAPI_Disp_GetHdrCapabilities_Original != nullptr) {
                result = NvAPI_Disp_GetHdrCapabilities_Original(displayId, pHdrCapabilities);
            } else {
                result = NVAPI_NO_IMPLEMENTATION;
            }

            // If we got valid data, modify it to hide HDR capabilities
            if (result == NVAPI_OK) {
                // Set all HDR-related flags to false
                pHdrCapabilities->isST2084EotfSupported = 0;
                pHdrCapabilities->isTraditionalHdrGammaSupported = 0;
                pHdrCapabilities->isTraditionalSdrGammaSupported = 1;  // Keep SDR support
                pHdrCapabilities->isHdr10PlusSupported = 0;
                pHdrCapabilities->isHdr10PlusGamingSupported = 0;
                pHdrCapabilities->isDolbyVisionSupported = 0;

                // Set driver to not expand HDR parameters
                pHdrCapabilities->driverExpandDefaultHdrParameters = 0;

                static int hdr_hidden_count = 0;
                if (hdr_hidden_count < 3) {
                    LogInfo("NVAPI HDR hiding: Modified HDR capabilities for DisplayId: %u", displayId);
                    hdr_hidden_count++;
                }
            }

            return result;
        } else {
            // If pHdrCapabilities is null, just return error
            return NVAPI_NO_IMPLEMENTATION;
        }
    }

    // HDR hiding disabled - call original function normally
    if (NvAPI_Disp_GetHdrCapabilities_Original != nullptr) {
        return NvAPI_Disp_GetHdrCapabilities_Original(displayId, pHdrCapabilities);
    }

    // Fallback - return error if original function not available
    return NVAPI_NO_IMPLEMENTATION;
}

// Hooked NvAPI_D3D_SetLatencyMarker function
NvAPI_Status __cdecl NvAPI_D3D_SetLatencyMarker_Detour(IUnknown* pDev,
                                                       NV_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // utils::SRWLockExclusive lock(g_nvapi_lock);
    // Increment counter
    g_nvapi_event_counters[NVAPI_EVENT_D3D_SET_LATENCY_MARKER].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Thread tracking for first 6 marker types (SIMULATION_START..PRESENT_END)
    if (g_thread_tracking_enabled.load(std::memory_order_relaxed) && pSetLatencyMarkerParams != nullptr) {
        const int marker_type = static_cast<int>(pSetLatencyMarkerParams->markerType);
        if (marker_type >= 0 && marker_type < static_cast<int>(kLatencyMarkerTypeCountFirstSix)) {
            g_latency_marker_thread_id[marker_type].store(GetCurrentThreadId(), std::memory_order_relaxed);
            g_latency_marker_last_frame_id[marker_type].store(pSetLatencyMarkerParams->frameID,
                                                             std::memory_order_relaxed);
        }
    }

    // Filter out RTSS calls (following Special-K approach)
    // RTSS is not native Reflex, so ignore it
    static HMODULE hModRTSS =
        Is64BitBuild() ? GetModuleHandleW(L"RTSSHooks64.dll") : GetModuleHandleW(L"RTSSHooks.dll");

    // Get calling module using GetCallingDLL (similar to Special-K's SK_GetCallingDLL)
    HMODULE calling_module = GetCallingDLL();

    if (hModRTSS != nullptr && calling_module == hModRTSS) {
        // Ignore RTSS calls - it's not native Reflex
        return NVAPI_OK;
    }

    // only for first 6 latency marker types
    if (pSetLatencyMarkerParams != nullptr
        && pSetLatencyMarkerParams->markerType == NV_LATENCY_MARKER_TYPE::PRESENT_START) {
        ChooseFpsLimiter(static_cast<uint64_t>(utils::get_now_ns()), FpsLimiterCallSite::reflex_marker);
    }
    bool use_present_end = false;

    bool native_pacing_sim_start_only = settings::g_mainTabSettings.native_pacing_sim_start_only.GetValue();

    if (native_pacing_sim_start_only) {
        bool use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::reflex_marker);
        if (use_fps_limiter) {
            if (pSetLatencyMarkerParams != nullptr
                && pSetLatencyMarkerParams->markerType == NV_LATENCY_MARKER_TYPE::SIMULATION_START) {
                OnPresentFlags2(false,
                                true);  // Called from wrapper, not present_detour

                // Record native frame time for frames shown to display
                RecordNativeFrameTime();
                // display_commanderhooks::dxgi::HandlePresentBefore2();
            }
            if (pSetLatencyMarkerParams != nullptr
                && pSetLatencyMarkerParams->markerType == NV_LATENCY_MARKER_TYPE::SIMULATION_START) {
                display_commanderhooks::dxgi::HandlePresentAfter(true);
            }
        }
    } else {
        if (pSetLatencyMarkerParams != nullptr
            && pSetLatencyMarkerParams->markerType == NV_LATENCY_MARKER_TYPE::PRESENT_END
            && !settings::g_advancedTabSettings.reflex_supress_native.GetValue()) {
            NvAPI_D3D_SetLatencyMarker_Direct(pDev, pSetLatencyMarkerParams);
        }
        bool use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::reflex_marker);
        if (use_fps_limiter) {
            if (pSetLatencyMarkerParams != nullptr
                && pSetLatencyMarkerParams->markerType == NV_LATENCY_MARKER_TYPE::PRESENT_START) {
                OnPresentFlags2(false,
                                true);  // Called from wrapper, not present_detour

                // Record native frame time for frames shown to display
                RecordNativeFrameTime();
                // display_commanderhooks::dxgi::HandlePresentBefore2();
            }
            if (pSetLatencyMarkerParams != nullptr
                && pSetLatencyMarkerParams->markerType == NV_LATENCY_MARKER_TYPE::PRESENT_END) {
                display_commanderhooks::dxgi::HandlePresentAfter(true);
            }
        }
        if (pSetLatencyMarkerParams != nullptr
            && pSetLatencyMarkerParams->markerType == NV_LATENCY_MARKER_TYPE::PRESENT_END) {
            return NVAPI_OK;
        }
    }

    // Cyclic buffer: record timestamp when this marker was called, keyed by (frame_id, markerType)
    if (pSetLatencyMarkerParams != nullptr) {
        const uint64_t frame_id = pSetLatencyMarkerParams->frameID;
        const int marker_type = static_cast<int>(pSetLatencyMarkerParams->markerType);
        if (marker_type >= 0 && marker_type < static_cast<int>(kLatencyMarkerTypeCount)) {
            const size_t slot = static_cast<size_t>(frame_id % kFrameDataBufferSize);
            const LONGLONG now_ns = utils::get_now_ns();
            g_latency_marker_buffer[slot].frame_id.store(frame_id, std::memory_order_relaxed);
            g_latency_marker_buffer[slot].marker_time_ns[marker_type].store(now_ns, std::memory_order_relaxed);
        }
    }

    // Delay PRESENT_START until (SIMULATION_START + delay_present_start_frames * frame_time) when enabled
    if (pSetLatencyMarkerParams != nullptr
        && pSetLatencyMarkerParams->markerType == NV_LATENCY_MARKER_TYPE::PRESENT_START
        && pSetLatencyMarkerParams->frameID > 300) {
        const bool delay_enabled = settings::g_mainTabSettings.delay_present_start_after_sim_enabled.GetValue();
        const float delay_frames = settings::g_mainTabSettings.delay_present_start_frames.GetValue();
        if (delay_enabled && delay_frames > 0.0f) {
            const uint64_t frame_id = pSetLatencyMarkerParams->frameID;
            const size_t slot = static_cast<size_t>(frame_id % kFrameDataBufferSize);
            const LONGLONG sim_start_ns =
                g_latency_marker_buffer[slot]
                    .marker_time_ns[static_cast<int>(NV_LATENCY_MARKER_TYPE::SIMULATION_START)]
                    .load(std::memory_order_relaxed);
            // Prefer frame time derived from FPS limit (and FG mode) when set; otherwise Reflex/OnPresentSync or
            // measured
            float effective_fps = settings::g_mainTabSettings.fps_limit.GetValue();
            if (effective_fps > 0.0f) {
                const DLSSGSummaryLite lite = GetDLSSGSummaryLite();
                if (lite.dlss_g_active) {
                    switch (lite.fg_mode) {
                        case DLSSGFgMode::k2x: effective_fps /= 2.0f; break;
                        case DLSSGFgMode::k3x: effective_fps /= 3.0f; break;
                        case DLSSGFgMode::k4x: effective_fps /= 4.0f; break;
                        default:               break;
                    }
                }
            }
            LONGLONG frame_time_ns = (effective_fps > 0.0f)
                                         ? static_cast<LONGLONG>(1'000'000'000.0 / static_cast<double>(effective_fps))
                                         : 0;
            /*
        if (frame_time_ns <= 0) {
            frame_time_ns = g_sleep_reflex_injected_ns_smooth.load(std::memory_order_relaxed);
        }
        if (frame_time_ns <= 0) {
            frame_time_ns = g_onpresent_sync_frame_time_ns.load(std::memory_order_relaxed);
        }
        if (frame_time_ns <= 0) {
            frame_time_ns = g_frame_time_ns.load(std::memory_order_relaxed);
        }*/
            frame_time_ns = (std::max)(frame_time_ns, static_cast<LONGLONG>(1));
            const LONGLONG delay_ns = static_cast<LONGLONG>(delay_frames * static_cast<float>(frame_time_ns));
            const LONGLONG target_ns = sim_start_ns + delay_ns;
            const LONGLONG now_ns = utils::get_now_ns();
            if (sim_start_ns > 0 && target_ns > now_ns) {
                utils::wait_until_ns(target_ns, g_timer_handle_delay_present_start);
            }
        }
    }

    if (settings::g_advancedTabSettings.reflex_supress_native.GetValue()) {
        return NVAPI_OK;
    }

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 3) {
        LogInfo("NVAPI SetLatencyMarker called - MarkerType: %d",
                pSetLatencyMarkerParams ? pSetLatencyMarkerParams->markerType : -1);
        log_count++;
    }

    return NvAPI_D3D_SetLatencyMarker_Direct(pDev, pSetLatencyMarkerParams);
}

// Hooked NvAPI_D3D_SetSleepMode function
NvAPI_Status __cdecl NvAPI_D3D_SetSleepMode_Detour(IUnknown* pDev, NV_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // utils::SRWLockExclusive lock(g_nvapi_lock);
    // Increment counter
    g_nvapi_event_counters[NVAPI_EVENT_D3D_SET_SLEEP_MODE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    if (settings::g_advancedTabSettings.reflex_supress_native.GetValue()) {
        return NVAPI_OK;
    }

    // Store the parameters for UI display
    if (pSetSleepModeParams != nullptr) {
        auto params = std::make_shared<NV_SET_SLEEP_MODE_PARAMS>(*pSetSleepModeParams);
        g_last_nvapi_sleep_mode_params.store(params);
        g_last_nvapi_sleep_mode_dev_ptr.store(pDev);
    }
    // Suppress detour if _Direct was called within the last 5 frames
    uint64_t current_frame_id = g_global_frame_id.load();
    uint64_t last_direct_frame_id = g_last_set_sleep_mode_direct_frame_id.load();
    if (last_direct_frame_id > 0 && current_frame_id >= last_direct_frame_id) {
        uint64_t frames_since_direct = current_frame_id - last_direct_frame_id;
        if (frames_since_direct <= 5) {
            return NVAPI_OK;
        }
    }

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 3) {
        if (pSetSleepModeParams != nullptr) {
            float fps_limit = 0.0f;
            if (pSetSleepModeParams->minimumIntervalUs > 0) {
                fps_limit = 1000000.0f / static_cast<float>(pSetSleepModeParams->minimumIntervalUs);
            }
            LogInfo(
                "NVAPI SetSleepMode called - Version: %u, LowLatency: %d, Boost: %d, UseMarkers: %d, "
                "MinimumIntervalUs: %u (%.2f FPS limit)",
                pSetSleepModeParams->version, pSetSleepModeParams->bLowLatencyMode,
                pSetSleepModeParams->bLowLatencyBoost, pSetSleepModeParams->bUseMarkersToOptimize,
                pSetSleepModeParams->minimumIntervalUs, fps_limit);
        } else {
            LogInfo("NVAPI SetSleepMode called - pSetSleepModeParams is nullptr");
        }
        log_count++;
    }
    if (!IsNativeReflexActive()) {
        return NVAPI_OK;
    }

    // Call original function
    if (NvAPI_D3D_SetSleepMode_Original != nullptr) {
        return NvAPI_D3D_SetSleepMode_Original(pDev, pSetSleepModeParams);
    }

    return NVAPI_NO_IMPLEMENTATION;
}

// Direct call to NvAPI_D3D_SetSleepMode without stats tracking
// For internal use to avoid inflating statistics
NvAPI_Status NvAPI_D3D_SetSleepMode_Direct(IUnknown* pDev, NV_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams) {
    // utils::SRWLockExclusive lock(g_nvapi_lock);

    // Track the frame when this function was called
    g_last_set_sleep_mode_direct_frame_id.store(g_global_frame_id.load());

    if (NvAPI_D3D_SetSleepMode_Original != nullptr) {
        return NvAPI_D3D_SetSleepMode_Original(pDev, pSetSleepModeParams);
    }
    return NVAPI_NO_IMPLEMENTATION;
}

// Direct call to NvAPI_D3D_Sleep without stats tracking
// For internal use to avoid inflating statistics
NvAPI_Status NvAPI_D3D_Sleep_Direct(IUnknown* pDev) {
    // utils::SRWLockExclusive lock(g_nvapi_lock);
    {
        static LONGLONG last_call = 0;
        auto now = utils::get_now_ns();
        LONGLONG delta = now - last_call;
        g_sleep_reflex_injected_ns.store(delta);
        if (delta > 0 && delta < 1 * utils::SEC_TO_NS) {
            LONGLONG old_smooth = g_sleep_reflex_injected_ns_smooth.load();
            LONGLONG new_smooth = UpdateRollingAverage<LONGLONG>(delta, old_smooth);
            g_sleep_reflex_injected_ns_smooth.store(new_smooth);
        }
        last_call = now;
    }

    if (NvAPI_D3D_Sleep_Original != nullptr) {
        return NvAPI_D3D_Sleep_Original(pDev);
    }
    return NVAPI_NO_IMPLEMENTATION;
}

// Direct call to NvAPI_D3D_SetLatencyMarker without stats tracking
// For internal use to avoid inflating statistics
NvAPI_Status NvAPI_D3D_SetLatencyMarker_Direct(IUnknown* pDev, NV_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams) {
    // utils::SRWLockExclusive lock(g_nvapi_lock);
    if (NvAPI_D3D_SetLatencyMarker_Original != nullptr) {
        return NvAPI_D3D_SetLatencyMarker_Original(pDev, pSetLatencyMarkerParams);
    }
    return NVAPI_NO_IMPLEMENTATION;
}

// Direct call to NvAPI_D3D_GetLatency without stats tracking
// For internal use to avoid inflating statistics
NvAPI_Status NvAPI_D3D_GetLatency_Direct(IUnknown* pDev, NV_LATENCY_RESULT_PARAMS* pGetLatencyParams) {
    // utils::SRWLockExclusive lock(g_nvapi_lock);
    if (NvAPI_D3D_GetLatency_Original != nullptr) {
        return NvAPI_D3D_GetLatency_Original(pDev, pGetLatencyParams);
    }
    return NVAPI_NO_IMPLEMENTATION;
}

// Hooked NvAPI_D3D_GetSleepStatus function
NvAPI_Status __cdecl NvAPI_D3D_GetSleepStatus_Detour(IUnknown* pDev,
                                                     NV_GET_SLEEP_STATUS_PARAMS* pGetSleepStatusParams) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_nvapi_event_counters[NVAPI_EVENT_D3D_GET_SLEEP_STATUS].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    if (NvAPI_D3D_GetSleepStatus_Original != nullptr) {
        return NvAPI_D3D_GetSleepStatus_Original(pDev, pGetSleepStatusParams);
    }
    return NVAPI_NO_IMPLEMENTATION;
}

// Direct call to NvAPI_D3D_GetSleepStatus without stats tracking
// For internal use to query Reflex sleep status (uses hooked original when hooks are installed)
NvAPI_Status NvAPI_D3D_GetSleepStatus_Direct(IUnknown* pDev, NV_GET_SLEEP_STATUS_PARAMS* pGetSleepStatusParams) {
    if (NvAPI_D3D_GetSleepStatus_Original != nullptr) {
        return NvAPI_D3D_GetSleepStatus_Original(pDev, pGetSleepStatusParams);
    }
    return NVAPI_NO_IMPLEMENTATION;
}

// Hooked NvAPI_D3D_Sleep function
NvAPI_Status __cdecl NvAPI_D3D_Sleep_Detour(IUnknown* pDev) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // utils::SRWLockExclusive lock(g_nvapi_lock);
    // Increment counter
    g_nvapi_event_counters[NVAPI_EVENT_D3D_SLEEP].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    // Record timestamp of this sleep call
    g_nvapi_last_sleep_timestamp_ns.store(utils::get_now_ns());

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 3) {
        LogInfo("NVAPI Sleep called");
        log_count++;
    }

    {
        static LONGLONG last_call = 0;
        auto now = utils::get_now_ns();
        LONGLONG delta = now - last_call;
        g_sleep_reflex_native_ns.store(delta);
        // Update smoothed rolling average if delta is reasonable (<1s and >0)
        if (delta > 0 && delta < 1 * utils::SEC_TO_NS) {
            LONGLONG old_smooth = g_sleep_reflex_native_ns_smooth.load();
            LONGLONG new_smooth = UpdateRollingAverage<LONGLONG>(delta, old_smooth);
            g_sleep_reflex_native_ns_smooth.store(new_smooth);
        }
        last_call = now;
    }
    // Check if Reflex sleep suppression is enabled
    if ((settings::g_mainTabSettings.suppress_reflex_sleep.GetValue()
         && settings::g_mainTabSettings.fps_limiter_mode.GetValue() == static_cast<int>(FpsLimiterMode::kReflex))
        || settings::g_mainTabSettings.fps_limiter_mode.GetValue()
               == static_cast<int>(FpsLimiterMode::kOnPresentSync)) {
        return NVAPI_OK;
    }

    if (!IsNativeReflexActive()) {
        return NVAPI_OK;
    }

    if (settings::g_advancedTabSettings.reflex_supress_native.GetValue()) {
        return NVAPI_OK;
    }

    // Call original function
    if (NvAPI_D3D_Sleep_Original != nullptr) {
        return NvAPI_D3D_Sleep_Original(pDev);
    }

    return NVAPI_NO_IMPLEMENTATION;
}

// Hooked NvAPI_D3D_GetLatency function
NvAPI_Status __cdecl NvAPI_D3D_GetLatency_Detour(IUnknown* pDev, NV_LATENCY_RESULT_PARAMS* pGetLatencyParams) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // utils::SRWLockExclusive lock(g_nvapi_lock);
    // Increment counter
    g_nvapi_event_counters[NVAPI_EVENT_D3D_GET_LATENCY].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 3) {
        LogInfo("NVAPI GetLatency called");
        log_count++;
    }

    // Call original function
    if (NvAPI_D3D_GetLatency_Original != nullptr) {
        return NvAPI_D3D_GetLatency_Original(pDev, pGetLatencyParams);
    }

    return NVAPI_NO_IMPLEMENTATION;
}

// Install NVAPI hooks
bool InstallNVAPIHooks(HMODULE nvapi_dll) {
    // Check if NVAPI hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::NVAPI)) {
        LogInfo("NVAPI hooks installation suppressed by user setting");
        return false;
    }

    // Get NvAPI_QueryInterface function (this is the key function that Special-K uses)
    NvAPI_QueryInterface_pfn queryInterface =
        reinterpret_cast<NvAPI_QueryInterface_pfn>(GetProcAddress(nvapi_dll, "nvapi_QueryInterface"));

    if (!queryInterface) {
        LogInfo("NVAPI hooks: Failed to get nvapi_QueryInterface address");
        return false;
    }

    LogInfo("NVAPI hooks: Found nvapi_QueryInterface, getting NvAPI_Disp_GetHdrCapabilities");

    // Get function ID from interface table
    NvU32 functionId = GetNvAPIFunctionId("NvAPI_Disp_GetHdrCapabilities");
    if (functionId == 0) {
        LogInfo("NVAPI hooks: Failed to get NvAPI_Disp_GetHdrCapabilities function ID");
        return false;
    }

    // Use QueryInterface to get the actual function address (same as Special-K)
    NvAPI_Disp_GetHdrCapabilities_pfn original_func =
        reinterpret_cast<NvAPI_Disp_GetHdrCapabilities_pfn>(queryInterface(functionId));

    if (!original_func) {
        LogInfo("NVAPI hooks: Failed to get NvAPI_Disp_GetHdrCapabilities via QueryInterface");
        return false;
    }

    LogInfo("NVAPI hooks: Successfully got NvAPI_Disp_GetHdrCapabilities address via QueryInterface");

    // Create and enable hook
    if (!CreateAndEnableHook(original_func, NvAPI_Disp_GetHdrCapabilities_Detour,
                             reinterpret_cast<LPVOID*>(&NvAPI_Disp_GetHdrCapabilities_Original),
                             "NvAPI_Disp_GetHdrCapabilities")) {
        LogInfo("NVAPI hooks: Failed to create and enable NvAPI_Disp_GetHdrCapabilities hook");
        return false;
    }

    LogInfo("NVAPI hooks: Successfully installed NvAPI_Disp_GetHdrCapabilities hook");

    // Install Reflex hooks
    const char* reflex_functions[] = {"NvAPI_D3D_SetLatencyMarker", "NvAPI_D3D_SetSleepMode", "NvAPI_D3D_Sleep",
                                      "NvAPI_D3D_GetLatency", "NvAPI_D3D_GetSleepStatus"};

    NvAPI_Status (*detour_functions[])(IUnknown*, void*) = {
        (NvAPI_Status (*)(IUnknown*, void*))NvAPI_D3D_SetLatencyMarker_Detour,
        (NvAPI_Status (*)(IUnknown*, void*))NvAPI_D3D_SetSleepMode_Detour,
        (NvAPI_Status (*)(IUnknown*, void*))NvAPI_D3D_Sleep_Detour,
        (NvAPI_Status (*)(IUnknown*, void*))NvAPI_D3D_GetLatency_Detour,
        (NvAPI_Status (*)(IUnknown*, void*))NvAPI_D3D_GetSleepStatus_Detour};

    void** original_functions[] = {(void**)&NvAPI_D3D_SetLatencyMarker_Original,
                                   (void**)&NvAPI_D3D_SetSleepMode_Original, (void**)&NvAPI_D3D_Sleep_Original,
                                   (void**)&NvAPI_D3D_GetLatency_Original, (void**)&NvAPI_D3D_GetSleepStatus_Original};

    for (int i = 0; i < 5; i++) {
        NvU32 functionId = GetNvAPIFunctionId(reflex_functions[i]);
        if (functionId == 0) {
            LogInfo("NVAPI hooks: Failed to get %s function ID", reflex_functions[i]);
            continue;
        }

        void* original_func = queryInterface(functionId);
        if (!original_func) {
            LogInfo("NVAPI hooks: Failed to get %s via QueryInterface", reflex_functions[i]);
            continue;
        }

        if (!CreateAndEnableHook(original_func, detour_functions[i], original_functions[i], reflex_functions[i])) {
            LogInfo("NVAPI hooks: Failed to create and enable %s hook", reflex_functions[i]);
            continue;
        }

        LogInfo("NVAPI hooks: Successfully installed %s hook", reflex_functions[i]);
    }

    // Mark NVAPI hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::NVAPI);

    return true;
}

// Uninstall NVAPI hooks
void UninstallNVAPIHooks() {
    if (NvAPI_Disp_GetHdrCapabilities_Original) {
        MH_DisableHook(NvAPI_Disp_GetHdrCapabilities_Original);
        MH_RemoveHook(NvAPI_Disp_GetHdrCapabilities_Original);
        NvAPI_Disp_GetHdrCapabilities_Original = nullptr;
    }

    if (NvAPI_D3D_SetLatencyMarker_Original) {
        MH_DisableHook(NvAPI_D3D_SetLatencyMarker_Original);
        MH_RemoveHook(NvAPI_D3D_SetLatencyMarker_Original);
        NvAPI_D3D_SetLatencyMarker_Original = nullptr;
    }

    if (NvAPI_D3D_SetSleepMode_Original) {
        MH_DisableHook(NvAPI_D3D_SetSleepMode_Original);
        MH_RemoveHook(NvAPI_D3D_SetSleepMode_Original);
        NvAPI_D3D_SetSleepMode_Original = nullptr;
    }

    if (NvAPI_D3D_Sleep_Original) {
        MH_DisableHook(NvAPI_D3D_Sleep_Original);
        MH_RemoveHook(NvAPI_D3D_Sleep_Original);
        NvAPI_D3D_Sleep_Original = nullptr;
    }

    if (NvAPI_D3D_GetLatency_Original) {
        MH_DisableHook(NvAPI_D3D_GetLatency_Original);
        MH_RemoveHook(NvAPI_D3D_GetLatency_Original);
        NvAPI_D3D_GetLatency_Original = nullptr;
    }

    if (NvAPI_D3D_GetSleepStatus_Original) {
        MH_DisableHook(NvAPI_D3D_GetSleepStatus_Original);
        MH_RemoveHook(NvAPI_D3D_GetSleepStatus_Original);
        NvAPI_D3D_GetSleepStatus_Original = nullptr;
    }
}

bool IsNvapiLockHeld() { return utils::TryIsSRWLockHeld(g_nvapi_lock); }
