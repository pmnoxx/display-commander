#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <string>
#include <thread>
#include <windows.h>
#include <dxgi.h>
#include "../utils/srwlock_wrapper.hpp"

namespace dxgi::fps_limiter {

/**
 * Refresh Rate Monitor
 *
 * Measures actual display refresh rate by calling WaitForVBlank in a loop
 * and calculating the time between vblank events. This provides real-time
 * measurement of the actual refresh rate, which may differ from the
 * configured refresh rate due to VRR, power management, or other factors.
 */
class RefreshRateMonitor {
public:
    RefreshRateMonitor();
    ~RefreshRateMonitor();

    // Start/stop monitoring
    void StartMonitoring();
    void StopMonitoring();
    bool IsMonitoring() const { return m_monitoring.load(); }

    // Get measured refresh rate
    double GetMeasuredRefreshRate() const { return m_measured_refresh_rate.load(); }
    double GetSmoothedRefreshRate() const { return m_smoothed_refresh_rate.load(); }

    // Get statistics
    double GetMinRefreshRate() const { return m_min_refresh_rate.load(); }
    double GetMaxRefreshRate() const { return m_max_refresh_rate.load(); }
    uint32_t GetSampleCount() const { return m_sample_count.load(); }

    // Get status information
    std::string GetStatusString() const;
    bool IsDataValid() const { return m_sample_count.load() > 0; }

private:
    void MonitoringThread();
    bool InitializeWaitForVBlank();
    void CleanupWaitForVBlank();
    // Monitoring state
    std::thread m_monitor_thread;
    std::atomic<bool> m_monitoring{false};
    std::atomic<bool> m_should_stop{false};

    // Refresh rate measurements
    std::atomic<double> m_measured_refresh_rate{0.0};
    std::atomic<double> m_smoothed_refresh_rate{0.0};
    std::atomic<double> m_min_refresh_rate{0.0};
    std::atomic<double> m_max_refresh_rate{0.0};
    std::atomic<uint32_t> m_sample_count{0};

    // Rolling window of last 60 samples for min/max calculation
    std::deque<double> m_recent_samples;
    mutable SRWLOCK m_recent_samples_mutex = SRWLOCK_INIT;

    // Timing data
    LONGLONG m_last_vblank_time{0};
    std::atomic<bool> m_first_sample{true};

    // DXGI interfaces for WaitForVBlank
    IDXGIFactory1* m_dxgi_factory{nullptr};
    IDXGIOutput* m_dxgi_output{nullptr};

    // Error state
    std::atomic<bool> m_initialization_failed{false};
    std::string m_error_message;
};

} // namespace dxgi::fps_limiter
