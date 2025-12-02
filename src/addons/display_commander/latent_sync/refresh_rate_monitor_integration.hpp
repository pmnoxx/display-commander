#pragma once

#include "refresh_rate_monitor.hpp"
#include <memory>
#include <string>

namespace dxgi::fps_limiter {

// Forward declaration for global instance (defined in .cpp)
extern std::unique_ptr<RefreshRateMonitor> g_refresh_rate_monitor;

// Function declarations for refresh rate monitoring integration
void StartRefreshRateMonitoring();
void StopRefreshRateMonitoring();
bool IsRefreshRateMonitoringActive();
double GetCurrentMeasuredRefreshRate();
double GetSmoothedRefreshRate();

// Signal monitoring thread (called from render thread after Present)
void SignalRefreshRateMonitor();

// Process frame statistics (called from render thread after caching stats)
void ProcessFrameStatistics(DXGI_FRAME_STATISTICS& stats);

// Refresh rate statistics structure
struct RefreshRateStats {
    double current_rate;
    double smoothed_rate;
    double min_rate;
    double max_rate;
    uint32_t sample_count;
    bool is_valid;
    bool all_last_20_within_1s;
    double fixed_refresh_hz;
    double threshold_hz;
    uint32_t total_samples_last_10s;
    uint32_t samples_below_threshold_last_10s;
    std::string status;
};

RefreshRateStats GetRefreshRateStats();
std::string GetRefreshRateStatusString();

// Iterate through recent refresh rate samples (lock-free, thread-safe)
// The callback is called for each sample. Data may be slightly stale during iteration.
template<typename Callback>
void ForEachRefreshRateSample(Callback&& callback) {
    if (!g_refresh_rate_monitor) {
        return;
    }
    g_refresh_rate_monitor->ForEachRecentSample(std::forward<Callback>(callback));
}

} // namespace dxgi::fps_limiter
