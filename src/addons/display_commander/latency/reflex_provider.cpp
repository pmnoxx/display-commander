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

// Libraries <standard C++>
#include <algorithm>

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
    // Register the ETW provider only when both "PCL stats for injected reflex" and "Inject Reflex" are on,
    // and after the same frame warmup as ReflexManager::Sleep (avoid early-init during startup).
    // Without inject reflex we must not PCLSTATS_INIT(); EmitPclStatsMarker skips TraceLoggingWrite if not initialized.
    if (!_is_pcl_initialized && settings::g_mainTabSettings.pcl_stats_enabled.GetValue() &&
        settings::g_mainTabSettings.inject_reflex.GetValue() &&
        g_global_frame_id.load(std::memory_order_acquire) > 500 && !PclStatsForeignInitObserved()) {
        PCLSTATS_INIT(0);
        _is_pcl_initialized = true;
        g_pclstats_init_success_count.fetch_add(1, std::memory_order_relaxed);
        g_pclstats_last_init_time_ns.store(utils::get_now_ns(), std::memory_order_relaxed);
    }
}

void ReflexProvider::EmitPclStatsMarker(uint32_t marker, uint64_t frame_id) {
    EnsurePCLStatsInitialized();
    if (!_is_pcl_initialized) {
        return;
    }
    PCLSTATS_MARKER(marker, frame_id);
    g_pclstats_etw_total_count.fetch_add(1, std::memory_order_relaxed);
    std::size_t idx = static_cast<std::size_t>(marker);
    if (idx >= kPclStatsEtwMarkerSlotCount) {
        idx = kPclStatsEtwMarkerSlotCount - 1;
    }
    g_pclstats_etw_by_marker[idx].fetch_add(1, std::memory_order_relaxed);
}

bool ReflexProvider::IsPCLStatsInitialized() { return _is_pcl_initialized; }

bool ReflexProvider::IsInitialized() const { return reflex_manager_.IsInitialized(); }

bool ReflexProvider::SetMarker(NV_LATENCY_MARKER_TYPE marker) {
    if (!IsInitialized()) return false;
    if (settings::g_mainTabSettings.pcl_stats_enabled.GetValue() &&
        settings::g_mainTabSettings.inject_reflex.GetValue()) {
        EnsurePCLStatsInitialized();
    }
    static bool first_call = true;
    if (first_call) {
        first_call = false;
        LogInfo("ReflexProvider::SetMarker: First call");
    }

    const bool result = reflex_manager_.SetMarker(marker);
    if (!result) return result;

    const LONGLONG now_ns = utils::get_now_ns();
    switch (marker) {
        case SIMULATION_START:
            g_reflex_marker_simulation_start_count.fetch_add(1, std::memory_order_relaxed);
            g_injected_reflex_last_marker_time_ns[0].store(now_ns, std::memory_order_relaxed);
            break;
        case SIMULATION_END:
            g_reflex_marker_simulation_end_count.fetch_add(1, std::memory_order_relaxed);
            g_injected_reflex_last_marker_time_ns[1].store(now_ns, std::memory_order_relaxed);
            break;
        case RENDERSUBMIT_START:
            g_reflex_marker_rendersubmit_start_count.fetch_add(1, std::memory_order_relaxed);
            g_injected_reflex_last_marker_time_ns[2].store(now_ns, std::memory_order_relaxed);
            break;
        case RENDERSUBMIT_END:
            g_reflex_marker_rendersubmit_end_count.fetch_add(1, std::memory_order_relaxed);
            g_injected_reflex_last_marker_time_ns[3].store(now_ns, std::memory_order_relaxed);
            break;
        case PRESENT_START:
            g_reflex_marker_present_start_count.fetch_add(1, std::memory_order_relaxed);
            g_injected_reflex_last_marker_time_ns[4].store(now_ns, std::memory_order_relaxed);
            break;
        case PRESENT_END:
            g_reflex_marker_present_end_count.fetch_add(1, std::memory_order_relaxed);
            g_injected_reflex_last_marker_time_ns[5].store(now_ns, std::memory_order_relaxed);
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
    g_injected_reflex_last_sleep_time_ns.store(sleep_start_ns, std::memory_order_relaxed);
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

bool ReflexProvider::GetLatencyMetrics(NvapiLatencyMetrics& out_metrics) {
    if (!IsInitialized()) {
        return false;
    }

#if defined(_M_AMD64) || defined(__x86_64__)
    NV_LATENCY_RESULT_PARAMS_V1 params = {};
    params.version = NV_LATENCY_RESULT_PARAMS_VER1;
    if (!reflex_manager_.GetLatency(reinterpret_cast<NV_LATENCY_RESULT_PARAMS*>(&params))) {
        return false;
    }

    // Compute average over all valid frame reports to smooth jitter.
    constexpr int kMaxReports = 64;
    double sum_pc_latency_ms = 0.0;
    double sum_gpu_ms = 0.0;
    int valid_count = 0;
    uint64_t max_frame_id = 0;

    for (int i = 0; i < kMaxReports; ++i) {
        const auto& fr = params.frameReport[i];
        if (fr.frameID == 0) {
            continue;
        }
        if (fr.gpuRenderEndTime <= fr.inputSampleTime) {
            continue;
        }

        // NVAPI latency timestamps are in microseconds (same domain as gpuFrameTimeUs).
        // Convert input->GPU-end delta from µs to ms to match gpuFrameTimeUs/1000.
        const double pc_latency_us = fr.inputSampleTime != 0 ?
            static_cast<double>(fr.gpuRenderEndTime - fr.inputSampleTime) : static_cast<double>(fr.gpuRenderEndTime - fr.simStartTime);
        const double pc_latency_ms = pc_latency_us / 1000.0;
        const double gpu_ms = static_cast<double>(fr.gpuFrameTimeUs) / 1000.0;

        sum_pc_latency_ms += pc_latency_ms;
        sum_gpu_ms += gpu_ms;
        ++valid_count;

        const uint64_t fid = static_cast<uint64_t>(fr.frameID);
        if (fid > max_frame_id) {
            max_frame_id = fid;
        }
    }

    if (valid_count == 0) {
        return false;
    }

    out_metrics.pc_latency_ms = sum_pc_latency_ms / static_cast<double>(valid_count);
    out_metrics.gpu_frame_time_ms = sum_gpu_ms / static_cast<double>(valid_count);
    out_metrics.frame_id = max_frame_id;
    return true;
#else
    (void)out_metrics;
    return false;
#endif
}

bool ReflexProvider::GetRecentLatencyFrames(std::vector<NvapiLatencyFrame>& out_frames, std::size_t max_frames) {
    out_frames.clear();
    if (!IsInitialized() || max_frames == 0) {
        return false;
    }

#if defined(_M_AMD64) || defined(__x86_64__)
    NV_LATENCY_RESULT_PARAMS_V1 params = {};
    params.version = NV_LATENCY_RESULT_PARAMS_VER1;
    if (!reflex_manager_.GetLatency(reinterpret_cast<NV_LATENCY_RESULT_PARAMS*>(&params))) {
        return false;
    }

    constexpr int kMaxReports = 64;
    for (int i = 0; i < kMaxReports; ++i) {
        const auto& fr = params.frameReport[i];
        if (fr.frameID == 0) {
            continue;
        }

        NvapiLatencyFrame frame{};
        constexpr uint64_t us_to_ns = 1000ULL;
        frame.frame_id = static_cast<uint64_t>(fr.frameID);
        frame.input_sample_time_ns = static_cast<uint64_t>(fr.inputSampleTime) * us_to_ns;
        frame.sim_start_time_ns = static_cast<uint64_t>(fr.simStartTime) * us_to_ns;
        frame.sim_end_time_ns = static_cast<uint64_t>(fr.simEndTime) * us_to_ns;
        frame.render_submit_start_time_ns = static_cast<uint64_t>(fr.renderSubmitStartTime) * us_to_ns;
        frame.render_submit_end_time_ns = static_cast<uint64_t>(fr.renderSubmitEndTime) * us_to_ns;
        frame.present_start_time_ns = static_cast<uint64_t>(fr.presentStartTime) * us_to_ns;
        frame.present_end_time_ns = static_cast<uint64_t>(fr.presentEndTime) * us_to_ns;
        frame.driver_start_time_ns = static_cast<uint64_t>(fr.driverStartTime) * us_to_ns;
        frame.driver_end_time_ns = static_cast<uint64_t>(fr.driverEndTime) * us_to_ns;
        frame.os_render_queue_start_time_ns = static_cast<uint64_t>(fr.osRenderQueueStartTime) * us_to_ns;
        frame.os_render_queue_end_time_ns = static_cast<uint64_t>(fr.osRenderQueueEndTime) * us_to_ns;
        frame.gpu_render_start_time_ns = static_cast<uint64_t>(fr.gpuRenderStartTime) * us_to_ns;
        frame.gpu_render_end_time_ns = static_cast<uint64_t>(fr.gpuRenderEndTime) * us_to_ns;
        frame.gpu_frame_time_us = static_cast<uint32_t>(fr.gpuFrameTimeUs);
        frame.gpu_active_render_time_us = static_cast<uint32_t>(fr.gpuActiveRenderTimeUs);
        out_frames.push_back(frame);
    }

    if (out_frames.empty()) {
        return false;
    }

    std::sort(out_frames.begin(), out_frames.end(),
              [](const NvapiLatencyFrame& a, const NvapiLatencyFrame& b) { return a.frame_id > b.frame_id; });
    if (out_frames.size() > max_frames) {
        out_frames.resize(max_frames);
    }
    return true;
#else
    return false;
#endif
}
