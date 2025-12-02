#pragma once

#include <atomic>
#include <array>
#include <string>
#include <thread>
#include <windows.h>
#include <dxgi.h>


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

    // Check if all last 20 samples were within 1 second
    bool AreLast20SamplesWithin1Second() const;

    // Count total samples within last 10 seconds
    uint32_t CountTotalSamplesLast10Seconds() const;

    // Count samples within last 10 seconds that are > 0 and X below fixed refresh rate
    // where X = fFixedRefreshHz - (fFixedRefreshHz * fFixedRefreshHz) / 3600.0f
    // This simplifies to: count samples where sample > 0 && sample < (fFixedRefreshHz^2) / 3600.0
    uint32_t CountSamplesBelowThreshold(double fixed_refresh_hz) const;

    // Iterate through recent samples (lock-free, thread-safe)
    // The callback is called for each sample. Data may be slightly stale during iteration.
    template<typename Callback>
    void ForEachRecentSample(Callback&& callback) const {
        // Snapshot atomic values for lock-free iteration
        size_t count = m_recent_samples_count.load(std::memory_order_acquire);
        size_t write_index = m_recent_samples_write_index.load(std::memory_order_acquire);

        if (count == 0) {
            return;
        }

        // Iterate through valid samples in chronological order (oldest to newest)
        if (count < RECENT_SAMPLES_SIZE) {
            // Buffer not full yet, iterate from start
            for (size_t i = 0; i < count; ++i) {
                callback(m_recent_samples[i].refresh_rate);
            }
        } else {
            // Buffer is full, iterate starting from write_index (oldest)
            for (size_t i = 0; i < RECENT_SAMPLES_SIZE; ++i) {
                size_t idx = (write_index + i) % RECENT_SAMPLES_SIZE;
                callback(m_recent_samples[idx].refresh_rate);
            }
        }
    }

    // Signal monitoring thread (called from render thread after Present)
    void SignalPresent();

    // Process frame statistics (called from render thread after caching stats)
    void ProcessFrameStatistics(DXGI_FRAME_STATISTICS& stats);

private:
    void MonitoringThread();
    bool InitializeWaitForVBlank();
    void CleanupWaitForVBlank();
    bool GetCurrentVBlankTime(DXGI_FRAME_STATISTICS& stats); // Get frame statistics from swapchain, no fallback
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

    // Sample structure to store both refresh rate and timestamp
    struct Sample {
        double refresh_rate;
        LONGLONG timestamp_ns;
    };

    // Rolling window of last 1024 samples for min/max calculation (fixed-size circular buffer)
    static constexpr size_t RECENT_SAMPLES_SIZE = 1024;
    std::array<Sample, RECENT_SAMPLES_SIZE> m_recent_samples{};
    std::atomic<size_t> m_recent_samples_write_index{0}; // Current write position in circular buffer
    std::atomic<size_t> m_recent_samples_count{0}; // Number of valid samples (0-256)

    // Timing data
    LONGLONG m_last_vblank_time{0};
    std::atomic<bool> m_first_sample{true};

    // DXGI interfaces for WaitForVBlank
    IDXGIFactory1* m_dxgi_factory{nullptr};
    IDXGIOutput* m_dxgi_output{nullptr};

    // Optional swapchain for GetFrameStatistics (more accurate timing)
    IDXGISwapChain* m_dxgi_swapchain{nullptr};

    // Synchronization for signaling from render thread
    HANDLE m_present_event{nullptr};

    // Error state
    std::atomic<bool> m_initialization_failed{false};
    std::string m_error_message;
};

} // namespace dxgi::fps_limiter
