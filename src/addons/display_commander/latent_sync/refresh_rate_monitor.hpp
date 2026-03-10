#pragma once

#include <dxgi.h>
#include <windows.h>
#include <array>
#include <atomic>
#include <string>
#include <thread>

namespace dxgi::fps_limiter {

/**
 * Refresh Rate Monitor
 *
 * Measures actual display refresh rate using the global DXGI swap chain
 * (set via SignalPresent from Present detours). Uses IDXGISwapChain::GetFrameStatistics
 * to compute time between presents; no CreateDXGIFactory1_Direct or WaitForVBlank.
 * Reflects actual refresh rate which may differ from configured due to VRR, etc.
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

    /** Time (ns, utils::get_now_ns style) when frame statistics were last successfully processed. 0 if never. */
    LONGLONG GetLastStatsTimeNs() const { return m_last_stats_time_ns.load(std::memory_order_acquire); }
    /** Number of times the monitoring thread loop body ran (including timeouts). Debug stat. */
    uint64_t GetLoopCount() const { return m_loop_count.load(std::memory_order_acquire); }
    /** True if a swap chain is currently stored (from SignalPresent). Debug. */
    bool HasSwapChain() const { return m_dxgi_swapchain.load(std::memory_order_acquire) != nullptr; }
    /** Number of times GetFrameStatistics was called on the swap chain. Debug. */
    uint64_t GetFrameStatsTried() const { return m_get_frame_stats_tried.load(std::memory_order_acquire); }
    /** Number of times GetFrameStatistics succeeded. Debug. */
    uint64_t GetFrameStatsOk() const { return m_get_frame_stats_ok.load(std::memory_order_acquire); }
    /** Number of times ProcessFrameStatistics skipped due to zero/negative refresh count diff. Debug. */
    uint64_t GetProcessSkippedNoDiff() const { return m_process_skipped_no_diff.load(std::memory_order_acquire); }
    /** Last HRESULT from GetFrameStatistics when it failed (0 = none or success). Debug. */
    HRESULT GetLastFrameStatisticsHr() const { return m_last_get_frame_stats_hr.load(std::memory_order_acquire); }

    // Check if all last 20 samples were within 1 second
    bool AreLast20SamplesWithin1Second() const;

    // Count total samples within last 10 seconds
    uint32_t CountTotalSamplesLast10Seconds() const;

    // Count samples within last 10 seconds that are > 0 and below G-Sync cap threshold
    // Threshold = 3600 × fixed_refresh_hz / (fixed_refresh_hz + 3600)
    uint32_t CountSamplesBelowThreshold(double fixed_refresh_hz) const;

    // Iterate through recent samples (lock-free, thread-safe)
    // The callback is called for each sample. Data may be slightly stale during iteration.
    template <typename Callback>
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

    // Signal monitoring thread (called from render thread after Present). Stores swap_chain
    // (AddRef'd) for use in GetCurrentVBlankTime; pass nullptr to clear.
    void SignalPresent(IDXGISwapChain* swap_chain);

    // Process frame statistics (called from render thread after caching stats)
    void ProcessFrameStatistics(DXGI_FRAME_STATISTICS& stats);

   private:
    void MonitoringThread();
    void CleanupDxgiResources();                              // Release stored swap chain only (no factory/output)
    bool GetCurrentVBlankTime(DXGI_FRAME_STATISTICS& stats);  // Get frame statistics from stored swap chain
    // Monitoring state
    std::thread m_monitor_thread;
    std::atomic<bool> m_monitoring{false};
    std::atomic<bool> m_should_stop{false};
    /** Number of times MonitoringThread loop body ran (debug). */
    std::atomic<uint64_t> m_loop_count{0};
    /** GetFrameStatistics call count (debug). */
    std::atomic<uint64_t> m_get_frame_stats_tried{0};
    /** GetFrameStatistics success count (debug). */
    std::atomic<uint64_t> m_get_frame_stats_ok{0};
    /** ProcessFrameStatistics skipped (present_refresh_count_diff <= 0) (debug). */
    std::atomic<uint64_t> m_process_skipped_no_diff{0};
    /** Last GetFrameStatistics failure HRESULT (for debug UI/log). */
    std::atomic<HRESULT> m_last_get_frame_stats_hr{0};

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
    std::atomic<size_t> m_recent_samples_write_index{0};  // Current write position in circular buffer
    std::atomic<size_t> m_recent_samples_count{0};        // Number of valid samples (0-256)

    // Timing data
    LONGLONG m_last_vblank_time{0};
    std::atomic<bool> m_first_sample{true};
    /** Set when frame statistics are successfully processed (ProcessFrameStatistics updates samples). */
    std::atomic<LONGLONG> m_last_stats_time_ns{0};

    // Stored swap chain for GetFrameStatistics (set from render thread via SignalPresent = global DXGI swap chain; read
    // by monitor thread). AddRef/Release on store; monitor thread AddRefs for use then Releases.
    std::atomic<IDXGISwapChain*> m_dxgi_swapchain{nullptr};

    // Synchronization for signaling from render thread
    HANDLE m_present_event{nullptr};

    // Error state
    std::atomic<bool> m_initialization_failed{false};
    std::string m_error_message;
};

}  // namespace dxgi::fps_limiter
