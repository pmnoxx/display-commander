#pragma once

#include "../globals.hpp"
#include "../settings/experimental_tab_settings.hpp"
#include "timing.hpp"

#include <atomic>
#include <cstdint>

namespace perf_measurement {

enum class Metric : std::uint8_t {
    Overlay = 0,
    OverlayShowVolume,
    OverlayShowVrrStatus,
    HandlePresentBefore,
    HandlePresentBefore_DeviceQuery,
    HandlePresentBefore_RecordFrameTime,
    HandlePresentBefore_FrameStatistics,
    TrackPresentStatistics,
    OnPresentFlags2,
    HandlePresentAfter,
    FlushCommandQueueFromSwapchain,
    EnqueueGPUCompletion,
    GetIndependentFlipState,
    OnPresentUpdateBefore,
    Count
};

struct Snapshot {
    std::uint64_t samples = 0;
    std::uint64_t total_ns = 0;
    std::uint64_t last_ns = 0;
    std::uint64_t max_ns = 0;
};

// Master enable (default off). When false, no QPC reads and no atomic updates are performed.
inline bool IsEnabled() {
    return settings::g_experimentalTabSettings.performance_measurement_enabled.GetAtomic().load(std::memory_order_relaxed);
}

inline bool IsMetricEnabled(Metric metric) {
    switch (metric) {
    case Metric::Overlay:
        return settings::g_experimentalTabSettings.perf_measure_overlay_enabled.GetAtomic().load(std::memory_order_relaxed);
    case Metric::OverlayShowVolume:
        return settings::g_experimentalTabSettings.perf_measure_overlay_show_volume_enabled.GetAtomic().load(std::memory_order_relaxed);
    case Metric::OverlayShowVrrStatus:
        return settings::g_experimentalTabSettings.perf_measure_overlay_show_vrr_status_enabled.GetAtomic().load(std::memory_order_relaxed);
    case Metric::HandlePresentBefore:
        return settings::g_experimentalTabSettings.perf_measure_handle_present_before_enabled.GetAtomic().load(std::memory_order_relaxed);
    case Metric::HandlePresentBefore_DeviceQuery:
        return settings::g_experimentalTabSettings.perf_measure_handle_present_before_device_query_enabled.GetAtomic().load(std::memory_order_relaxed);
    case Metric::HandlePresentBefore_RecordFrameTime:
        return settings::g_experimentalTabSettings.perf_measure_handle_present_before_record_frame_time_enabled.GetAtomic().load(std::memory_order_relaxed);
    case Metric::HandlePresentBefore_FrameStatistics:
        return settings::g_experimentalTabSettings.perf_measure_handle_present_before_frame_statistics_enabled.GetAtomic().load(std::memory_order_relaxed);
    case Metric::TrackPresentStatistics:
        return settings::g_experimentalTabSettings.perf_measure_track_present_statistics_enabled.GetAtomic().load(std::memory_order_relaxed);
    case Metric::OnPresentFlags2:
        return settings::g_experimentalTabSettings.perf_measure_on_present_flags2_enabled.GetAtomic().load(std::memory_order_relaxed);
    case Metric::HandlePresentAfter:
        return settings::g_experimentalTabSettings.perf_measure_handle_present_after_enabled.GetAtomic().load(std::memory_order_relaxed);
    case Metric::FlushCommandQueueFromSwapchain:
        return settings::g_experimentalTabSettings.perf_measure_flush_command_queue_from_swapchain_enabled.GetAtomic().load(std::memory_order_relaxed);
    case Metric::EnqueueGPUCompletion:
        return settings::g_experimentalTabSettings.perf_measure_enqueue_gpu_completion_enabled.GetAtomic().load(std::memory_order_relaxed);
    case Metric::GetIndependentFlipState:
        return settings::g_experimentalTabSettings.perf_measure_get_independent_flip_state_enabled.GetAtomic().load(std::memory_order_relaxed);
    case Metric::OnPresentUpdateBefore:
        return settings::g_experimentalTabSettings.perf_measure_on_present_update_before_enabled.GetAtomic().load(std::memory_order_relaxed);
    default:
        return false;
    }
}

// Suppression (debug) - optional. When enabled, selected functions will early-out to help isolate performance cost.
inline bool IsSuppressionEnabled() {
    return settings::g_experimentalTabSettings.performance_suppression_enabled.GetAtomic().load(std::memory_order_relaxed);
}

inline bool IsMetricSuppressed(Metric metric) {
    switch (metric) {
    case Metric::Overlay:
        return settings::g_experimentalTabSettings.perf_suppress_overlay.GetAtomic().load(std::memory_order_relaxed);
    case Metric::OverlayShowVolume:
        return settings::g_experimentalTabSettings.perf_suppress_overlay_show_volume.GetAtomic().load(std::memory_order_relaxed);
    case Metric::OverlayShowVrrStatus:
        return settings::g_experimentalTabSettings.perf_suppress_overlay_show_vrr_status.GetAtomic().load(std::memory_order_relaxed);
    case Metric::HandlePresentBefore:
        return settings::g_experimentalTabSettings.perf_suppress_handle_present_before.GetAtomic().load(std::memory_order_relaxed);
    case Metric::HandlePresentBefore_DeviceQuery:
        return settings::g_experimentalTabSettings.perf_suppress_handle_present_before_device_query.GetAtomic().load(std::memory_order_relaxed);
    case Metric::HandlePresentBefore_RecordFrameTime:
        return settings::g_experimentalTabSettings.perf_suppress_handle_present_before_record_frame_time.GetAtomic().load(std::memory_order_relaxed);
    case Metric::HandlePresentBefore_FrameStatistics:
        return settings::g_experimentalTabSettings.perf_suppress_handle_present_before_frame_statistics.GetAtomic().load(std::memory_order_relaxed);
    case Metric::TrackPresentStatistics:
        return settings::g_experimentalTabSettings.perf_suppress_track_present_statistics.GetAtomic().load(std::memory_order_relaxed);
    case Metric::OnPresentFlags2:
        return settings::g_experimentalTabSettings.perf_suppress_on_present_flags2.GetAtomic().load(std::memory_order_relaxed);
    case Metric::HandlePresentAfter:
        return settings::g_experimentalTabSettings.perf_suppress_handle_present_after.GetAtomic().load(std::memory_order_relaxed);
    case Metric::FlushCommandQueueFromSwapchain:
        return settings::g_experimentalTabSettings.perf_suppress_flush_command_queue_from_swapchain.GetAtomic().load(std::memory_order_relaxed);
    case Metric::EnqueueGPUCompletion:
        return settings::g_experimentalTabSettings.perf_suppress_enqueue_gpu_completion.GetAtomic().load(std::memory_order_relaxed);
    case Metric::GetIndependentFlipState:
        return settings::g_experimentalTabSettings.perf_suppress_get_independent_flip_state.GetAtomic().load(std::memory_order_relaxed);
    case Metric::OnPresentUpdateBefore:
        return settings::g_experimentalTabSettings.perf_suppress_on_present_update_before.GetAtomic().load(std::memory_order_relaxed);
    default:
        return false;
    }
}

void ResetAll();
Snapshot GetSnapshot(Metric metric);

class ScopedTimer {
  public:
    explicit ScopedTimer(Metric metric)
        : metric_(metric) {
        if (!IsEnabled() || !IsMetricEnabled(metric_)) {
            active_ = false;
            return;
        }
        active_ = true;
        paused_ = false;
        accumulated_ns_ = 0;
        start_ns_ = static_cast<std::uint64_t>(utils::get_now_ns());
    }

    ScopedTimer(const ScopedTimer &) = delete;
    ScopedTimer &operator=(const ScopedTimer &) = delete;

    void pause() {
        if (!active_ || paused_) {
            return;
        }
        const std::uint64_t end_ns = static_cast<std::uint64_t>(utils::get_now_ns());
        const std::uint64_t dt_ns = (end_ns >= start_ns_) ? (end_ns - start_ns_) : 0ULL;
        accumulated_ns_ += dt_ns;
        paused_ = true;
    }

    void resume() {
        if (!active_ || !paused_) {
            return;
        }
        start_ns_ = static_cast<std::uint64_t>(utils::get_now_ns());
        paused_ = false;
    }

    ~ScopedTimer() {
        if (!active_) {
            return;
        }
        std::uint64_t total_ns = accumulated_ns_;
        if (!paused_) {
            const std::uint64_t end_ns = static_cast<std::uint64_t>(utils::get_now_ns());
            const std::uint64_t dt_ns = (end_ns >= start_ns_) ? (end_ns - start_ns_) : 0ULL;
            total_ns += dt_ns;
        }
        if (total_ns > 0) {
            Record(metric_, total_ns);
        }
    }

  private:
    static void Record(Metric metric, std::uint64_t dt_ns);

    Metric metric_;
    bool active_ = false;
    bool paused_ = false;
    std::uint64_t accumulated_ns_ = 0;
    std::uint64_t start_ns_ = 0;
};

} // namespace perf_measurement


