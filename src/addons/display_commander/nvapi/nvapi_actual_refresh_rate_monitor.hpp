#pragma once

#include <cstddef>
#include <utility>

namespace display_commander::nvapi {

// Start background thread that polls NvAPI_DISP_GetAdaptiveSyncData and derives
// actual refresh rate from lastFlipRefreshCount + lastFlipTimeStamp.
// Uses display_id from vrr_status::cached_nvapi_vrr (must be resolved).
void StartNvapiActualRefreshRateMonitoring();

void StopNvapiActualRefreshRateMonitoring();

bool IsNvapiActualRefreshRateMonitoringActive();

// Actual refresh rate in Hz from Adaptive Sync flip data. Returns 0.0 if not
// active, no display_id, or query/sample failed.
double GetNvapiActualRefreshRateHz();

// Internal: count and logical-indexed access (0 = oldest). For lock-free iteration.
size_t GetNvapiActualRefreshRateRecentCount();
double GetNvapiActualRefreshRateRecentSampleAt(size_t logical_index);

// Iterate through recent actual refresh rate samples (Hz) for the time graph.
// Lock-free; callback is invoked for each sample (oldest to newest).
template <typename Callback>
void ForEachNvapiActualRefreshRateSample(Callback&& callback) {
    size_t count = GetNvapiActualRefreshRateRecentCount();
    for (size_t i = 0; i < count; ++i) {
        double rate = GetNvapiActualRefreshRateRecentSampleAt(i);
        std::forward<Callback>(callback)(rate);
    }
}

}  // namespace display_commander::nvapi
