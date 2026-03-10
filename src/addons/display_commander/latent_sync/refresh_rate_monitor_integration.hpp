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
double GetCurrentMeasuredRefreshRate();
double GetSmoothedRefreshRate();

// Signal monitoring thread (called from render thread after Present). Pass the swap chain that just presented; it is AddRef'd and stored for GetFrameStatistics-based refresh rate measurement.
void SignalRefreshRateMonitor(IDXGISwapChain* swap_chain);

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

/** True if the RefreshRateMonitor thread is running (StartRefreshRateMonitoring was called and not stopped). */
bool IsRefreshRateMonitorThreadRunning();
/** Time (ns) when frame statistics were last successfully processed. 0 if never or no monitor. */
long long GetRefreshRateMonitorLastStatsTimeNs();
/** Number of times SignalRefreshRateMonitor was called (Present detours). Resets only when process restarts. */
uint64_t GetRefreshRateMonitorSignalCount();
/** Number of times the monitor thread loop body ran (including timeouts). Resets when monitoring (re)starts. */
uint64_t GetRefreshRateMonitorLoopCount();
/** True if the monitor has a stored swap chain (from SignalPresent). */
bool RefreshRateMonitorHasSwapChain();
/** GetFrameStatistics call count on the stored swap chain. */
uint64_t GetRefreshRateMonitorFrameStatsTried();
/** GetFrameStatistics success count. */
uint64_t GetRefreshRateMonitorFrameStatsOk();
/** ProcessFrameStatistics skipped (refresh count diff <= 0). */
uint64_t GetRefreshRateMonitorProcessSkippedNoDiff();
/** Last GetFrameStatistics failure HRESULT (0 if none or success). */
HRESULT GetRefreshRateMonitorLastFrameStatisticsHr();

} // namespace dxgi::fps_limiter
