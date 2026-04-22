#pragma once

namespace nvapi {

// OSD: set request to current g_global_frame_id while enabled, or clear when disabled.
void RequestGpuDynamicUtilizationFromOverlay(bool enabled);

// Continuous monitoring: service overlay request (NVAPI) under frame-based freshness and spacing rules.
void ProcessGpuDynamicUtilizationRequestInContinuousMonitoring();

// Last good sample from Process... (0-100). Returns false if never sampled successfully.
bool GetCachedGpuDynamicUtilizationPercent(unsigned& out_percent);

}  // namespace nvapi
