#include "reflex_provider.hpp"
#include "../../../../external/Streamline/source/plugins/sl.pcl/pclstats.h"
#include "../globals.hpp"

// Define the PCLStats provider (must be in exactly one .cpp)
PCLSTATS_DEFINE()
#include "../hooks/nvidia/pclstats_etw_hooks.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "../utils/timing.hpp"

// Static member initialization
bool ReflexProvider::_is_pcl_initialized = false;

ReflexProvider::ReflexProvider() = default;
ReflexProvider::~ReflexProvider() = default;

bool ReflexProvider::Initialize(reshade::api::device* device) { return reflex_manager_.Initialize(device); }

bool ReflexProvider::InitializeNative(void* native_device, DeviceTypeDC device_type) {
    return reflex_manager_.InitializeNative(native_device, device_type);
}

void ReflexProvider::Shutdown() {
    // Only shutdown PCLStats if it was initialized
    if (_is_pcl_initialized) {
        PCLSTATS_SHUTDOWN();
        _is_pcl_initialized = false;
    }
    reflex_manager_.Shutdown();
}

void ReflexProvider::EnsurePCLStatsInitialized() {
    // Initialize when user has "PCL stats for injected reflex" on and we're not yet initialized.
    // Use only the setting here so init succeeds for injected reflex even if PCLStatsReportingAllowed()
    // is temporarily false (e.g. warmup, or game once called SetLatencyMarker). The caller only
    // invokes us when PCLStatsReportingEnabled() is true, so we only attempt init when we will emit.
    if (!_is_pcl_initialized && settings::g_mainTabSettings.pcl_stats_enabled.GetValue()) {
        PCLSTATS_INIT(0);
        _is_pcl_initialized = true;
    }
}

bool ReflexProvider::IsPCLStatsInitialized() { return _is_pcl_initialized; }

bool ReflexProvider::IsInitialized() const { return reflex_manager_.IsInitialized(); }

bool ReflexProvider::SetMarker(NV_LATENCY_MARKER_TYPE marker) {
    if (!IsInitialized()) return false;
    static bool first_call = true;
    if (first_call) {
        first_call = false;
        LogInfo("ReflexProvider::SetMarker: First call");
    }

    const bool result = reflex_manager_.SetMarker(marker);
    if (!result) return result;

    switch (marker) {
        case SIMULATION_START:
            g_reflex_marker_simulation_start_count.fetch_add(1, std::memory_order_relaxed);
            break;
        case SIMULATION_END:
            g_reflex_marker_simulation_end_count.fetch_add(1, std::memory_order_relaxed);
            break;
        case RENDERSUBMIT_START:
            g_reflex_marker_rendersubmit_start_count.fetch_add(1, std::memory_order_relaxed);
            break;
        case RENDERSUBMIT_END:
            g_reflex_marker_rendersubmit_end_count.fetch_add(1, std::memory_order_relaxed);
            break;
        case PRESENT_START:
            g_reflex_marker_present_start_count.fetch_add(1, std::memory_order_relaxed);
            break;
        case PRESENT_END:
            g_reflex_marker_present_end_count.fetch_add(1, std::memory_order_relaxed);
            break;
        case INPUT_SAMPLE:
            g_reflex_marker_input_sample_count.fetch_add(1, std::memory_order_relaxed);
            break;
        case PC_LATENCY_PING:
        default:
            break;
    }
    return result;
}

bool ReflexProvider::ApplySleepMode(bool low_latency, bool boost, bool use_markers, float fps_limit) {
    if (!IsInitialized()) return false;

    g_reflex_apply_sleep_mode_count.fetch_add(1, std::memory_order_relaxed);
    return reflex_manager_.ApplySleepMode(low_latency, boost, use_markers, fps_limit);
}

bool ReflexProvider::Sleep() {
    if (!IsInitialized()) return false;

    g_reflex_sleep_count.fetch_add(1, std::memory_order_relaxed);
    const LONGLONG sleep_start_ns = utils::get_now_ns();
    const bool result = reflex_manager_.Sleep();
    const LONGLONG sleep_end_ns = utils::get_now_ns();
    const LONGLONG sleep_duration_ns = sleep_end_ns - sleep_start_ns;
    const LONGLONG old_duration = g_reflex_sleep_duration_ns.load();
    const LONGLONG smoothed_duration = UpdateRollingAverage(sleep_duration_ns, old_duration);
    g_reflex_sleep_duration_ns.store(smoothed_duration);
    return result;
}

void ReflexProvider::UpdateCachedSleepStatus() {
    if (!IsInitialized()) return;
    NV_GET_SLEEP_STATUS_PARAMS sleep_status = {};
    sleep_status.version = NV_GET_SLEEP_STATUS_PARAMS_VER;
    (void)reflex_manager_.GetSleepStatus(&sleep_status);
}

bool ReflexProvider::GetSleepStatus(NV_GET_SLEEP_STATUS_PARAMS* status_params,
                                    SleepStatusUnavailableReason* out_reason) {
    if (!IsInitialized() || status_params == nullptr) return false;

    return reflex_manager_.GetSleepStatus(status_params, out_reason);
}
