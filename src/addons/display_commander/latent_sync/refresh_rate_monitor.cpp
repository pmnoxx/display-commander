#include "refresh_rate_monitor.hpp"
#include "../utils/logging.hpp"
#include "../utils/timing.hpp"
#include "../globals.hpp"
#include <dxgi.h>
#include <dwmapi.h>
#include <limits>
#include <sstream>
#include <iomanip>
#include <vector>

#pragma comment(lib, "dwmapi.lib")

// Simple logging wrapper to avoid dependency issues
namespace {
void LogMessage(const std::string &msg) { LogInfo("[RefreshRateMonitor] %s", msg.c_str()); }
} // namespace

namespace dxgi::fps_limiter {

RefreshRateMonitor::RefreshRateMonitor() {
    m_measured_refresh_rate = 0.0;
    m_smoothed_refresh_rate = 0.0;
    m_min_refresh_rate = 0.0;
    m_max_refresh_rate = 0.0;
    m_sample_count = 0;
    m_first_sample = true;
    m_initialization_failed = false;
    m_error_message.clear();
    m_present_event = CreateEvent(nullptr, FALSE, FALSE, nullptr); // Auto-reset event
    if (m_present_event == nullptr) {
        LogError("Failed to create present event: %lu", GetLastError());
    }
}

RefreshRateMonitor::~RefreshRateMonitor() {
    StopMonitoring();
    CleanupWaitForVBlank();
    if (m_present_event != nullptr) {
        CloseHandle(m_present_event);
        m_present_event = nullptr;
    }
}

void RefreshRateMonitor::StartMonitoring() {
    if (m_monitoring.load()) {
        return;
    }

    // Initialize WaitForVBlank function
    if (!InitializeWaitForVBlank()) {
        m_initialization_failed = true;
        m_error_message = "Failed to initialize WaitForVBlank function";
        LogMessage("Failed to start monitoring: " + m_error_message);
        return;
    }

    m_should_stop = false;
    m_monitor_thread = std::thread(&RefreshRateMonitor::MonitoringThread, this);
    m_monitoring = true;

    LogMessage("Refresh rate monitoring thread started");
    LogInfo("Refresh rate monitoring thread: StartMonitoring() called - thread created and started");
}

void RefreshRateMonitor::StopMonitoring() {
    if (!m_monitoring.load()) {
        return;
    }

    LogInfo("Refresh rate monitoring thread: StopMonitoring() called - stopping thread...");
    m_should_stop = true;
    if (m_monitor_thread.joinable()) {
        m_monitor_thread.join();
    }
    m_monitoring = false;

    LogMessage("Refresh rate monitoring thread stopped");
    LogInfo("Refresh rate monitoring thread: StopMonitoring() completed - thread joined and stopped");
}

bool RefreshRateMonitor::InitializeWaitForVBlank() {
    // WaitForVBlank is a method on IDXGIOutput interface, not a standalone function
    // We'll create a DXGI factory and get the output to use WaitForVBlank
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_dxgi_factory));
    if (FAILED(hr)) {
        LogError("Failed to create DXGI factory: 0x%08X", hr);
        return false;
    }

    // Get the first adapter
    IDXGIAdapter1* adapter = nullptr;
    hr = m_dxgi_factory->EnumAdapters1(0, &adapter);
    if (FAILED(hr)) {
        LogError("Failed to enumerate adapters: 0x%08X", hr);
        return false;
    }

    // Get the first output
    hr = adapter->EnumOutputs(0, &m_dxgi_output);
    adapter->Release();

    if (FAILED(hr)) {
        LogError("Failed to enumerate outputs: 0x%08X", hr);
        return false;
    }

    LogInfo("Successfully initialized DXGI output for WaitForVBlank");
    return true;
}

void RefreshRateMonitor::CleanupWaitForVBlank() {
    if (m_dxgi_output != nullptr) {
        m_dxgi_output->Release();
        m_dxgi_output = nullptr;
    }
    if (m_dxgi_factory != nullptr) {
        m_dxgi_factory->Release();
        m_dxgi_factory = nullptr;
    }
    // Note: m_dxgi_swapchain is not owned by us, so we don't release it
    m_dxgi_swapchain = nullptr;
}

void RefreshRateMonitor::SignalPresent() {
    if (m_present_event != nullptr && m_monitoring.load()) {
        SetEvent(m_present_event);
    }
}

void RefreshRateMonitor::ProcessFrameStatistics(DXGI_FRAME_STATISTICS& stats) {
        static uint64_t m_last_present_refresh_count = 0;

    // Get current frame statistics (should now be accurate after DWM flush)
       // DXGI_FRAME_STATISTICS stats = {};
        if (!GetCurrentVBlankTime(stats)) {
      //      // If we can't get frame statistics, skip this sample
     //       LogError("Failed to get frame statistics - skipping sample");
            //  std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return;
        }

        // Extract time from frame statistics
        LONGLONG current_time = stats.SyncQPCTime.QuadPart * utils::QPC_TO_NS;
        uint64_t current_present_refresh_count = stats.SyncRefreshCount;

        // Calculate present refresh count difference
        uint64_t present_refresh_count_diff = current_present_refresh_count - m_last_present_refresh_count;

        if (present_refresh_count_diff <= 0) {
            return;
        }
        //m_last_vblank_time = stats.SyncQPCTime.QuadPart * utils::QPC_TO_NS;

        if (!m_first_sample.load()) {
            // Calculate time difference
            LONGLONG duration_ns = (current_time - m_last_vblank_time) / present_refresh_count_diff;
            double duration_seconds = duration_ns / 1e9;

            if (duration_seconds > 0.0 && duration_ns >= 1000) { // 1000ns is the minimum time between vblank events
                // Calculate refresh rate
                double refresh_rate = static_cast<double>(1) / duration_seconds;

                // Update measured refresh rate
                m_measured_refresh_rate = refresh_rate;

                // Update smoothed refresh rate (exponential moving average)
                double current_smoothed = m_smoothed_refresh_rate.load();
                double alpha = 0.2; // Smoothing factor
                double new_smoothed = (current_smoothed * (1.0 - alpha)) + (refresh_rate * alpha);
                m_smoothed_refresh_rate = new_smoothed;

                // Update rolling window of last 256 samples (circular buffer, lock-free)
                LONGLONG sample_timestamp_ns = current_time;
                for (size_t i = 0; i < present_refresh_count_diff; ++i) {
                    // Get current write position
                    size_t write_idx = m_recent_samples_write_index.load(std::memory_order_relaxed);

                    // Write to current position (both refresh rate and timestamp)
                    m_recent_samples[write_idx].refresh_rate = refresh_rate;
                    m_recent_samples[write_idx].timestamp_ns = sample_timestamp_ns;

                    // Advance write index (circular)
                    size_t new_write_idx = (write_idx + 1) % RECENT_SAMPLES_SIZE;
                    m_recent_samples_write_index.store(new_write_idx, std::memory_order_release);

                    // Update count (capped at RECENT_SAMPLES_SIZE)
                    size_t count = m_recent_samples_count.load(std::memory_order_relaxed);
                    if (count < RECENT_SAMPLES_SIZE) {
                        m_recent_samples_count.store(count + 1, std::memory_order_release);
                    }

                    // Update min/max from recent samples (lock-free iteration)
                    count = m_recent_samples_count.load(std::memory_order_acquire);
                    write_idx = m_recent_samples_write_index.load(std::memory_order_acquire);
                    if (count > 0) {
                        double min_val = (std::numeric_limits<double>::max)();
                        double max_val = (std::numeric_limits<double>::lowest)();

                        // Iterate through valid samples (handle circular buffer correctly)
                        if (count < RECENT_SAMPLES_SIZE) {
                            // Buffer not full yet, check samples from 0 to count
                            for (size_t i = 0; i < count; ++i) {
                                if (m_recent_samples[i].refresh_rate < min_val) min_val = m_recent_samples[i].refresh_rate;
                                if (m_recent_samples[i].refresh_rate > max_val) max_val = m_recent_samples[i].refresh_rate;
                            }
                        } else {
                            // Buffer is full, iterate starting from write_index (oldest)
                            for (size_t i = 0; i < RECENT_SAMPLES_SIZE; ++i) {
                                size_t idx = (write_idx + i) % RECENT_SAMPLES_SIZE;
                                if (m_recent_samples[idx].refresh_rate < min_val) min_val = m_recent_samples[idx].refresh_rate;
                                if (m_recent_samples[idx].refresh_rate > max_val) max_val = m_recent_samples[idx].refresh_rate;
                            }
                        }

                        m_min_refresh_rate.store(min_val, std::memory_order_release);
                        m_max_refresh_rate.store(max_val, std::memory_order_release);
                    }
                }

                // Increment sample count
                m_sample_count++;
            }
        } else {
            // First sample - just record the time
            m_first_sample = false;
        }

        // Update last present refresh count
        m_last_present_refresh_count = current_present_refresh_count;

        // Update last vblank time
        m_last_vblank_time = current_time;
}

bool RefreshRateMonitor::GetCurrentVBlankTime(DXGI_FRAME_STATISTICS& stats) {
    // First, try to get from cached frame statistics (updated in present detour)
    // Use memory_order_acquire to ensure we see the latest value and proper synchronization
    // The local variable 'cached_stats' keeps the shared_ptr alive during dereference,
    // preventing the object from being destroyed by another thread between load and dereference
 /*   auto cached_stats = g_cached_frame_stats.load(std::memory_order_acquire);
    if (cached_stats != nullptr) {
        // Copy the stats - cached_stats keeps the shared_ptr alive during this operation
        stats = *cached_stats;
        return true;
    }
*/
    global_dxgi_swapchain_inuse.store(true, std::memory_order_release);
    // Fallback: try to get swapchain from tracked swapchains
    // Iterate through all tracked swapchains while holding the lock
    bool found = false;
    auto global_dxgi_swapchain_ptr = global_dxgi_swapchain.load(std::memory_order_acquire);

    if (global_dxgi_swapchain_ptr != nullptr) {
        // dxgi_output from swapchain
        IDXGIOutput* dxgi_output = nullptr;
        HRESULT hr = global_dxgi_swapchain_ptr->GetContainingOutput(&dxgi_output);
        if (FAILED(hr)) {
            LogError("GetContainingOutput failed with HRESULT: 0x%08X", hr);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return false;
        }

        // Wait for vblank using IDXGIOutput::WaitForVBlank
        hr = dxgi_output->WaitForVBlank();
        if (FAILED(hr)) {
            LogError("WaitForVBlank failed with HRESULT: 0x%08X", hr);
            // Continue trying, but sleep a bit to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return false;
        }
        hr = global_dxgi_swapchain_ptr->GetFrameStatistics(&stats);
        if (SUCCEEDED(hr)) {
            global_dxgi_swapchain_inuse.store(false, std::memory_order_release);
            found = true;
        } else {
            return false;
        }
    }

    global_dxgi_swapchain_inuse.store(false, std::memory_order_release);

    // Return true if we found a valid swapchain with frame statistics, false otherwise
    return found;
}

std::string RefreshRateMonitor::GetStatusString() const {
    std::ostringstream oss;

    if (m_initialization_failed.load()) {
        oss << "Error: " << m_error_message;
        return oss.str();
    }

    if (!m_monitoring.load()) {
        oss << "Not monitoring";
        return oss.str();
    }

    if (!IsDataValid()) {
        oss << "Monitoring (no data yet)";
        return oss.str();
    }

    double current_rate = m_smoothed_refresh_rate.load();
    double min_rate = m_min_refresh_rate.load();
    double max_rate = m_max_refresh_rate.load();
    uint32_t samples = m_sample_count.load();

    oss << std::fixed << std::setprecision(2);
    oss << "Current: " << current_rate << " Hz";
    oss << " | Min: " << min_rate << " Hz";
    oss << " | Max: " << max_rate << " Hz";
    oss << " | Samples: " << samples;

    return oss.str();
}

bool RefreshRateMonitor::AreLast20SamplesWithin1Second() const {
    constexpr size_t CHECK_COUNT = 20;
    constexpr LONGLONG ONE_SECOND_NS = 1000000000LL; // 1 second in nanoseconds

    // Snapshot atomic values for lock-free iteration
    size_t count = m_recent_samples_count.load(std::memory_order_acquire);
    size_t write_index = m_recent_samples_write_index.load(std::memory_order_acquire);

    if (count == 0) {
        return false;
    }

    // Determine how many samples to check (up to 20, or all available if less than 20)
    size_t samples_to_check = (count < CHECK_COUNT) ? count : CHECK_COUNT;

    // Collect the last N samples (most recent)
    std::vector<LONGLONG> timestamps;
    timestamps.reserve(samples_to_check);

    if (count < RECENT_SAMPLES_SIZE) {
        // Buffer not full yet, get last N samples from the end
        size_t start_idx = (count >= samples_to_check) ? (count - samples_to_check) : 0;
        for (size_t i = start_idx; i < count; ++i) {
            timestamps.push_back(m_recent_samples[i].timestamp_ns);
        }
    } else {
        // Buffer is full, get last N samples (newest are just before write_index)
        // write_index points to where the next write will happen, so newest is at (write_index - 1)
        for (size_t i = 0; i < samples_to_check; ++i) {
            // Calculate index: go backwards from write_index (newest first)
            size_t idx = (write_index + RECENT_SAMPLES_SIZE - 1 - i) % RECENT_SAMPLES_SIZE;
            timestamps.push_back(m_recent_samples[idx].timestamp_ns);
        }
    }

    if (timestamps.size() < 2) {
        return false; // Need at least 2 samples to check time difference
    }

    // Check if all timestamps are within 1 second of each other
    // Find min and max timestamps
    LONGLONG min_timestamp = timestamps[0];
    LONGLONG max_timestamp = timestamps[0];
    for (LONGLONG ts : timestamps) {
        if (ts < min_timestamp) min_timestamp = ts;
        if (ts > max_timestamp) max_timestamp = ts;
    }

    // Check if the time span is within 1 second
    LONGLONG time_span_ns = max_timestamp - min_timestamp;
    return time_span_ns <= ONE_SECOND_NS;
}

uint32_t RefreshRateMonitor::CountTotalSamplesLast10Seconds() const {
    constexpr LONGLONG TEN_SECONDS_NS = 10000000000LL; // 10 seconds in nanoseconds

    // Get current time
    LONGLONG now_ns = utils::get_now_ns();
    LONGLONG cutoff_time_ns = now_ns - TEN_SECONDS_NS;

    // Snapshot atomic values for lock-free iteration
    size_t count = m_recent_samples_count.load(std::memory_order_acquire);
    size_t write_index = m_recent_samples_write_index.load(std::memory_order_acquire);

    if (count == 0) {
        return 0;
    }

    uint32_t total_samples = 0;

    // Iterate through samples within the last 10 seconds
    if (count < RECENT_SAMPLES_SIZE) {
        // Buffer not full yet, iterate from start
        for (size_t i = 0; i < count; ++i) {
            const Sample& sample = m_recent_samples[i];
            // Check if sample is within last 10 seconds
            if (sample.timestamp_ns >= cutoff_time_ns) {
                total_samples++;
            }
        }
    } else {
        // Buffer is full, iterate starting from write_index (oldest)
        for (size_t i = 0; i < RECENT_SAMPLES_SIZE; ++i) {
            size_t idx = (write_index + i) % RECENT_SAMPLES_SIZE;
            const Sample& sample = m_recent_samples[idx];
            // Check if sample is within last 10 seconds
            if (sample.timestamp_ns >= cutoff_time_ns) {
                total_samples++;
            }
        }
    }

    return total_samples;
}

uint32_t RefreshRateMonitor::CountSamplesBelowThreshold(double fixed_refresh_hz) const {
    constexpr LONGLONG TEN_SECONDS_NS = 10000000000LL; // 10 seconds in nanoseconds

    if (fixed_refresh_hz <= 0.0) {
        return 0;
    }

    double threshold = fixed_refresh_hz - ((fixed_refresh_hz * fixed_refresh_hz) / 3600.0);

    // Get current time
    LONGLONG now_ns = utils::get_now_ns();
    LONGLONG cutoff_time_ns = now_ns - TEN_SECONDS_NS;

    // Snapshot atomic values for lock-free iteration
    size_t count = m_recent_samples_count.load(std::memory_order_acquire);
    size_t write_index = m_recent_samples_write_index.load(std::memory_order_acquire);

    if (count == 0) {
        return 0;
    }

    uint32_t matching_samples = 0;

    // Iterate through samples within the last 10 seconds
    if (count < RECENT_SAMPLES_SIZE) {
        // Buffer not full yet, iterate from start
        for (size_t i = 0; i < count; ++i) {
            const Sample& sample = m_recent_samples[i];
            // Check if sample is within last 10 seconds
            if (sample.timestamp_ns >= cutoff_time_ns) {
                // Check if sample is > 0 and < threshold
                if (sample.refresh_rate > 0.0 && sample.refresh_rate < threshold) {
                    matching_samples++;
                }
            }
        }
    } else {
        // Buffer is full, iterate starting from write_index (oldest)
        for (size_t i = 0; i < RECENT_SAMPLES_SIZE; ++i) {
            size_t idx = (write_index + i) % RECENT_SAMPLES_SIZE;
            const Sample& sample = m_recent_samples[idx];
            // Check if sample is within last 10 seconds
            if (sample.timestamp_ns >= cutoff_time_ns) {
                // Check if sample is > 0 and < threshold
                if (sample.refresh_rate > 0.0 && sample.refresh_rate < threshold) {
                    matching_samples++;
                }
            }
        }
    }

    return matching_samples;
}

void RefreshRateMonitor::MonitoringThread() {
    LogInfo("Refresh rate monitoring thread: entering main loop");
    LogInfo("Refresh rate monitoring thread: STARTED - measuring actual refresh rate via WaitForVBlank");

    // Wait a bit for the system to stabilize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Reset statistics
    m_measured_refresh_rate = 0.0;
    m_smoothed_refresh_rate = 0.0;
    m_min_refresh_rate = 0.0;
    m_max_refresh_rate = 0.0;
    m_sample_count = 0;
    m_first_sample = true;

    // Clear recent samples
    {
        m_recent_samples_write_index.store(0, std::memory_order_release);
        m_recent_samples_count.store(0, std::memory_order_release);
        m_recent_samples.fill({});
    }



    //uint64_t m_last_present_refresh_count = 0;
    // Main monitoring loop
    while (!m_should_stop.load()) {
        try {
            if (m_dxgi_output == nullptr) {
                LogError("DXGI output is null");
                break;
            }

            // Wait for signal from render thread (after Present is called)
            // This ensures DWM has been flushed before we check frame statistics
            if (m_present_event != nullptr) {
                DWORD wait_result = WaitForSingleObject(m_present_event, 1000); // 1 second timeout
                if (wait_result == WAIT_TIMEOUT) {
                    // Timeout - continue to check if we should stop
                    continue;
                } else if (wait_result != WAIT_OBJECT_0) {
                    LogError("WaitForSingleObject failed: %lu", GetLastError());
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
            } else {
                // Fallback if event creation failed
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            DwmFlush();

            DXGI_FRAME_STATISTICS stats = {};
            ProcessFrameStatistics(stats);


        } catch (const std::exception& e) {
            LogError("Exception in refresh rate monitoring thread: %s", e.what());
            break;
        } catch (...) {
            LogError("Unknown exception in refresh rate monitoring thread");
            break;
        }
    }

    LogInfo("Refresh rate monitoring thread: exiting main loop");
    LogInfo("Refresh rate monitoring thread: STOPPED - final sample count: %u", m_sample_count.load());
}

} // namespace dxgi::fps_limiter
