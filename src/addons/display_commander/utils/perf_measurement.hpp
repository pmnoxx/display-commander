#pragma once

#include "../globals.hpp"
#include "../settings/experimental_tab_settings.hpp"
#include "timing.hpp"

#include <array>
#include <atomic>
#include <cstdint>

namespace perf_measurement {

enum class Metric : std::uint8_t {
    Overlay = 0,
    HandlePresentBefore,
    TrackPresentStatistics,
    OnPresentFlags2,
    HandlePresentAfter,
    Count
};

struct Snapshot {
    std::uint64_t samples = 0;
    std::uint64_t total_ns = 0;
    std::uint64_t last_ns = 0;
};

// Master enable (default off). When false, no QPC reads and no atomic updates are performed.
inline bool IsEnabled() {
    return settings::g_experimentalTabSettings.performance_measurement_enabled.GetAtomic().load(std::memory_order_relaxed);
}

inline bool IsMetricEnabled(Metric metric) {
    switch (metric) {
    case Metric::Overlay:
        return settings::g_experimentalTabSettings.perf_measure_overlay_enabled.GetAtomic().load(std::memory_order_relaxed);
    case Metric::HandlePresentBefore:
        return settings::g_experimentalTabSettings.perf_measure_handle_present_before_enabled.GetAtomic().load(std::memory_order_relaxed);
    case Metric::TrackPresentStatistics:
        return settings::g_experimentalTabSettings.perf_measure_track_present_statistics_enabled.GetAtomic().load(std::memory_order_relaxed);
    case Metric::OnPresentFlags2:
        return settings::g_experimentalTabSettings.perf_measure_on_present_flags2_enabled.GetAtomic().load(std::memory_order_relaxed);
    case Metric::HandlePresentAfter:
        return settings::g_experimentalTabSettings.perf_measure_handle_present_after_enabled.GetAtomic().load(std::memory_order_relaxed);
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
        start_ns_ = static_cast<std::uint64_t>(utils::get_now_ns());
    }

    ScopedTimer(const ScopedTimer &) = delete;
    ScopedTimer &operator=(const ScopedTimer &) = delete;

    ~ScopedTimer() {
        if (!active_) {
            return;
        }
        const std::uint64_t end_ns = static_cast<std::uint64_t>(utils::get_now_ns());
        const std::uint64_t dt_ns = (end_ns >= start_ns_) ? (end_ns - start_ns_) : 0ULL;
        Record(metric_, dt_ns);
    }

  private:
    static void Record(Metric metric, std::uint64_t dt_ns);

    Metric metric_;
    bool active_ = false;
    std::uint64_t start_ns_ = 0;
};

} // namespace perf_measurement


