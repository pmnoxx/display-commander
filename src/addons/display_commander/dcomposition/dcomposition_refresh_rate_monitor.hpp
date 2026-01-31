#pragma once

namespace display_commander::dcomposition {

// Start DirectComposition refresh rate monitoring (creates DComp device, samples GetFrameStatistics).
// Call when setting is on and a ReShade runtime exists.
void StartDCompRefreshRateMonitoring();

// Stop monitoring and release DComp device. Call when setting is off or runtime is destroyed.
void StopDCompRefreshRateMonitoring();

// True if DComp device is created and we are sampling.
bool IsDCompRefreshRateMonitoringActive();

// Current composition rate in Hz from DCOMPOSITION_FRAME_STATISTICS.currentCompositionRate.
// Returns 0.0 if not active, device is null, or GetFrameStatistics fails / invalid rational.
double GetDCompCompositionRateHz();

// Measured refresh rate in Hz by counting composition frame boundaries (lastFrameTime changes)
// over a 1-second sliding window. Returns 0.0 if not active or no samples yet.
double GetDCompMeasuredRefreshRateHz();

}  // namespace display_commander::dcomposition
