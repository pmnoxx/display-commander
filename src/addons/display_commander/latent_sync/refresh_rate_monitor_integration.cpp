#include "refresh_rate_monitor.hpp"
#include "../utils/logging.hpp"
#include <memory>
#include <string>
#include <windows.h>

// Example of how to integrate RefreshRateMonitor with existing code
namespace dxgi::fps_limiter {

namespace {
// Global instance of the refresh rate monitor
std::unique_ptr<RefreshRateMonitor> g_refresh_rate_monitor;
} // anonymous namespace

// Function to start refresh rate monitoring
void StartRefreshRateMonitoring() {
    if (!g_refresh_rate_monitor) {
        g_refresh_rate_monitor = std::make_unique<RefreshRateMonitor>();
    }

    if (g_refresh_rate_monitor && !g_refresh_rate_monitor->IsMonitoring()) {
        g_refresh_rate_monitor->StartMonitoring();
        LogInfo("Refresh rate monitoring started via integration");
    }
}

// Function to stop refresh rate monitoring
void StopRefreshRateMonitoring() {
    if (g_refresh_rate_monitor && g_refresh_rate_monitor->IsMonitoring()) {
        g_refresh_rate_monitor->StopMonitoring();
        LogInfo("Refresh rate monitoring stopped via integration");
    }
}

// Function to check if monitoring is active
bool IsRefreshRateMonitoringActive() {
    return g_refresh_rate_monitor && g_refresh_rate_monitor->IsMonitoring();
}

// Function to get current measured refresh rate
double GetCurrentMeasuredRefreshRate() {
    if (!g_refresh_rate_monitor) {
        return 0.0;
    }
    return g_refresh_rate_monitor->GetMeasuredRefreshRate();
}

// Function to get smoothed refresh rate
double GetSmoothedRefreshRate() {
    if (!g_refresh_rate_monitor) {
        return 0.0;
    }
    return g_refresh_rate_monitor->GetSmoothedRefreshRate();
}

// Function to get refresh rate statistics
struct RefreshRateStats {
    double current_rate;
    double smoothed_rate;
    double min_rate;
    double max_rate;
    uint32_t sample_count;
    bool is_valid;
    std::string status;
};

RefreshRateStats GetRefreshRateStats() {
    RefreshRateStats stats{};

    if (!g_refresh_rate_monitor) {
        stats.status = "Not initialized";
        return stats;
    }

    stats.current_rate = g_refresh_rate_monitor->GetMeasuredRefreshRate();
    stats.smoothed_rate = g_refresh_rate_monitor->GetSmoothedRefreshRate();
    stats.min_rate = g_refresh_rate_monitor->GetMinRefreshRate();
    stats.max_rate = g_refresh_rate_monitor->GetMaxRefreshRate();
    stats.sample_count = g_refresh_rate_monitor->GetSampleCount();
    stats.is_valid = g_refresh_rate_monitor->IsDataValid();
    stats.status = g_refresh_rate_monitor->GetStatusString();

    return stats;
}

// Function to get status string for UI display
std::string GetRefreshRateStatusString() {
    if (!g_refresh_rate_monitor) {
        return "Not initialized";
    }
    return g_refresh_rate_monitor->GetStatusString();
}

} // namespace dxgi::fps_limiter
