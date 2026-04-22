// Source Code <Display Commander> // CPU telemetry feature slice
#pragma once

namespace display_commander::feature::cpu_telemetry {

// OSD request signals (frame-id based), set while row is enabled.
void RequestProcessCpuLoadFromOverlay(bool enabled);
void RequestSystemCpuLoadFromOverlay(bool enabled);

// Continuous monitoring worker for process/system CPU load requests.
void ProcessCpuLoadRequestsInContinuousMonitoring();

// Cached latest values (0-100). Return false until first valid sample exists.
bool GetCachedProcessCpuLoadPercent(double& out_percent);
bool GetCachedSystemCpuLoadPercent(double& out_percent);

}  // namespace display_commander::feature::cpu_telemetry
