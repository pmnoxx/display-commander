#pragma once

#include <string>

namespace dxgi::fps_limiter {

// Function declarations for refresh rate monitoring integration
void StartRefreshRateMonitoring();
void StopRefreshRateMonitoring();
bool IsRefreshRateMonitoringActive();
double GetCurrentMeasuredRefreshRate();
double GetSmoothedRefreshRate();

// Refresh rate statistics structure
struct RefreshRateStats {
    double current_rate;
    double smoothed_rate;
    double min_rate;
    double max_rate;
    uint32_t sample_count;
    bool is_valid;
    std::string status;
};

RefreshRateStats GetRefreshRateStats();
std::string GetRefreshRateStatusString();

} // namespace dxgi::fps_limiter
