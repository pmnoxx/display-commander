#include "nvapi_hdr_monitor.hpp"
#include "nvapi_fullscreen_prevention.hpp"
#include "../utils.hpp"
#include <sstream>
#include <thread>
#include <chrono>
#include <algorithm>

// Background NVAPI HDR monitor thread
void RunBackgroundNvapiHdrMonitor() {
    // Ensure NVAPI is initialized if we plan to log
    if (s_nvapi_hdr_logging < 0.5f) return;
    if (!g_nvapiFullscreenPrevention.IsAvailable()) {
        if (!g_nvapiFullscreenPrevention.Initialize()) {
            LogWarn("NVAPI HDR monitor: failed to initialize NVAPI");
            return;
        }
    }

    LogInfo("NVAPI HDR monitor: started");

    while (!g_shutdown.load()) {
        if (s_nvapi_hdr_logging >= 0.5f) {
            bool hdr = false; std::string cs; std::string name;
            if (g_nvapiFullscreenPrevention.QueryHdrStatus(hdr, cs, name)) {
                std::ostringstream oss;
                oss << "NVAPI HDR: enabled=" << (hdr ? "true" : "false")
                    << ", colorspace=" << (cs.empty() ? "Unknown" : cs)
                    << ", output=" << (name.empty() ? "Unknown" : name);
                LogInfo(oss.str().c_str());

                // Also print full HDR details for debugging parameters (ST2086, etc.)
                std::string details;
                if (g_nvapiFullscreenPrevention.QueryHdrDetails(details)) {
                    LogInfo(details.c_str());
                }
            } else {
                LogInfo("NVAPI HDR: query failed or HDR not available on any connected display");
            }
        }

        int sleep_ms = (int)((std::max)(1.0f, s_nvapi_hdr_interval_sec) * 1000.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }

    LogInfo("NVAPI HDR monitor: stopped");
}

// On-demand single-shot log for debugging
void LogNvapiHdrOnce() {
    if (!g_nvapiFullscreenPrevention.IsAvailable()) {
        if (!g_nvapiFullscreenPrevention.Initialize()) {
            LogWarn("NVAPI HDR single-shot: failed to initialize NVAPI");
            return;
        }
    }
    bool hdr = false; std::string cs; std::string name;
    if (g_nvapiFullscreenPrevention.QueryHdrStatus(hdr, cs, name)) {
        std::ostringstream oss;
        oss << "NVAPI HDR (single): enabled=" << (hdr ? "true" : "false")
            << ", colorspace=" << (cs.empty() ? "Unknown" : cs)
            << ", output=" << (name.empty() ? "Unknown" : name);
        LogInfo(oss.str().c_str());
    } else {
        LogInfo("NVAPI HDR (single): query failed or HDR not available");
    }
}
