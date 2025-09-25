#pragma once

#include <atomic>

// Forward declarations
class NVAPIFullscreenPrevention;

// NVAPI HDR monitoring functions
void RunBackgroundNvapiHdrMonitor();
void LogNvapiHdrOnce();

// External variables needed by these functions
extern std::atomic<bool> s_nvapi_hdr_logging;
extern std::atomic<float> s_nvapi_hdr_interval_sec;
extern std::atomic<bool> g_shutdown;
extern NVAPIFullscreenPrevention g_nvapiFullscreenPrevention;
