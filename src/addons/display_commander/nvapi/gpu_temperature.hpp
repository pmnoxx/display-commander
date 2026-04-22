#pragma once

namespace nvapi {

// OSD: set request to current g_global_frame_id while enabled, or clear when disabled.
void RequestGpuTemperatureFromOverlay(bool enabled);

// Continuous monitoring: service overlay request (NVAPI) under frame-based freshness and spacing rules.
void ProcessGpuTemperatureRequestInContinuousMonitoring();

// Last good sample in Celsius. Returns false if never sampled successfully.
bool GetCachedGpuTemperatureCelsius(unsigned& out_celsius);

}  // namespace nvapi
