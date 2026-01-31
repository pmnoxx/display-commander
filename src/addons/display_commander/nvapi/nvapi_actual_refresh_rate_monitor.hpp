#pragma once

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

}  // namespace display_commander::nvapi
