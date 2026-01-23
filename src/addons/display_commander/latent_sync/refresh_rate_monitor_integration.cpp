#include "refresh_rate_monitor_integration.hpp"
#include <windows.h>
#include <memory>
#include <string>
#include "../display_cache.hpp"
#include "../utils/logging.hpp"
#include "refresh_rate_monitor.hpp"


// Example of how to integrate RefreshRateMonitor with existing code
namespace dxgi::fps_limiter {

// Global instance of the refresh rate monitor
std::unique_ptr<RefreshRateMonitor> g_refresh_rate_monitor;

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
bool IsRefreshRateMonitoringActive() { return g_refresh_rate_monitor && g_refresh_rate_monitor->IsMonitoring(); }

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

RefreshRateStats GetRefreshRateStats() {
    RefreshRateStats stats{};

    if (!g_refresh_rate_monitor) {
        stats.status = "Not initialized";
        stats.all_last_20_within_1s = false;
        stats.fixed_refresh_hz = 0.0;
        stats.threshold_hz = 0.0;
        stats.total_samples_last_10s = 0;
        stats.samples_below_threshold_last_10s = 0;
        return stats;
    }

    stats.current_rate = g_refresh_rate_monitor->GetMeasuredRefreshRate();
    stats.smoothed_rate = g_refresh_rate_monitor->GetSmoothedRefreshRate();
    stats.min_rate = g_refresh_rate_monitor->GetMinRefreshRate();
    stats.max_rate = g_refresh_rate_monitor->GetMaxRefreshRate();
    stats.sample_count = g_refresh_rate_monitor->GetSampleCount();
    stats.is_valid = g_refresh_rate_monitor->IsDataValid();
    stats.all_last_20_within_1s = g_refresh_rate_monitor->AreLast20SamplesWithin1Second();

    // Count total samples within last 10 seconds
    stats.total_samples_last_10s = g_refresh_rate_monitor->CountTotalSamplesLast10Seconds();

    // Get fixed refresh rate from display cache (try primary display first, then display 0)
    double fixed_refresh_hz = 0.0;
    if (display_cache::g_displayCache.IsInitialized()) {
        display_cache::RationalRefreshRate refresh_rate;
        // Try to get refresh rate from primary display or display 0
        if (display_cache::g_displayCache.GetCurrentRefreshRate(0, refresh_rate)) {
            fixed_refresh_hz = refresh_rate.ToHz();
        } else {
            // Try to find primary display
            auto displays = display_cache::g_displayCache.GetDisplays();
            if (displays && !displays->empty()) {
                // Find primary display
                for (size_t i = 0; i < displays->size(); ++i) {
                    const auto& display = (*displays)[i];
                    if (display && display->is_primary) {
                        if (display_cache::g_displayCache.GetCurrentRefreshRate(i, refresh_rate)) {
                            fixed_refresh_hz = refresh_rate.ToHz();
                            break;
                        }
                    }
                }
                // If no primary found, use first display
                if (fixed_refresh_hz == 0.0 && displays->size() > 0) {
                    if (display_cache::g_displayCache.GetCurrentRefreshRate(0, refresh_rate)) {
                        fixed_refresh_hz = refresh_rate.ToHz();
                    }
                }
            }
        }
    }

    stats.fixed_refresh_hz = fixed_refresh_hz;

    // Calculate threshold: fixed_refresh_hz - X
    // where X = fFixedRefreshHz - (fFixedRefreshHz * fFixedRefreshHz) / 3600.0f
    if (fixed_refresh_hz > 0.0) {
        stats.threshold_hz = fixed_refresh_hz - ((fixed_refresh_hz * fixed_refresh_hz) / 3600.0);
        stats.samples_below_threshold_last_10s = g_refresh_rate_monitor->CountSamplesBelowThreshold(fixed_refresh_hz);
    } else {
        stats.threshold_hz = 0.0;
        stats.samples_below_threshold_last_10s = 0;
    }

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

// Signal monitoring thread (called from render thread after Present)
void SignalRefreshRateMonitor() {
    if (g_refresh_rate_monitor && g_refresh_rate_monitor->IsMonitoring()) {
        g_refresh_rate_monitor->SignalPresent();
    }
}

// Process frame statistics (called from render thread after caching stats)
void ProcessFrameStatistics(DXGI_FRAME_STATISTICS& stats) {
    // Frame statistics are already cached in g_cached_frame_stats by the Present detour
    // This function can be used for any additional processing needed on the cached stats
    // Currently, the monitoring thread reads from g_cached_frame_stats directly
    // This is a placeholder for future processing if needed
    if (g_refresh_rate_monitor && g_refresh_rate_monitor->IsMonitoring()) {
        g_refresh_rate_monitor->ProcessFrameStatistics(stats);
        // Additional processing can be added here if needed
    }
}

}  // namespace dxgi::fps_limiter
