#include "refresh_rate_monitor.hpp"
#include "../utils/logging.hpp"
#include "../utils/srwlock_wrapper.hpp"
#include <algorithm>
#include <dxgi.h>
#include <iostream>
#include <sstream>
#include <iomanip>

// Simple logging wrapper to avoid dependency issues
namespace {
void LogMessage(const std::string &msg) { std::cout << "[RefreshRateMonitor] " << msg << '\n'; }
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
}

RefreshRateMonitor::~RefreshRateMonitor() {
    StopMonitoring();
    CleanupWaitForVBlank();
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
        utils::SRWLockExclusive lock(m_recent_samples_mutex);
        m_recent_samples.clear();
    }

    // Get current time for first measurement
    m_last_vblank_time = std::chrono::high_resolution_clock::now();

    // Main monitoring loop
    while (!m_should_stop.load()) {
        try {
            if (m_dxgi_output == nullptr) {
                LogError("DXGI output is null");
                break;
            }

            // Wait for vblank using IDXGIOutput::WaitForVBlank
            HRESULT hr = m_dxgi_output->WaitForVBlank();
            if (FAILED(hr)) {
                LogError("WaitForVBlank failed with HRESULT: 0x%08X", hr);
                // Continue trying, but sleep a bit to avoid busy waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }

            // Get current time
            auto current_time = std::chrono::high_resolution_clock::now();

            if (!m_first_sample.load()) {
                // Calculate time difference
                auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(current_time - m_last_vblank_time);
                double duration_seconds = duration.count() / 1e9;

                if (duration_seconds > 0.0) {
                    // Calculate refresh rate
                    double refresh_rate = 1.0 / duration_seconds;

                    // Update measured refresh rate
                    m_measured_refresh_rate = refresh_rate;

                    // Update smoothed refresh rate (exponential moving average)
                    double current_smoothed = m_smoothed_refresh_rate.load();
                    double alpha = 0.1; // Smoothing factor
                    double new_smoothed = (current_smoothed * (1.0 - alpha)) + (refresh_rate * alpha);
                    m_smoothed_refresh_rate = new_smoothed;

                    // Update rolling window of last 60 samples
                    {
                        utils::SRWLockExclusive lock(m_recent_samples_mutex);
                        m_recent_samples.push_back(refresh_rate);

                        // Keep only last 60 samples
                        if (m_recent_samples.size() > 60) {
                            m_recent_samples.pop_front();
                        }

                        // Update min/max from recent samples
                        if (!m_recent_samples.empty()) {
                            auto minmax = std::minmax_element(m_recent_samples.begin(), m_recent_samples.end());
                            m_min_refresh_rate = *minmax.first;
                            m_max_refresh_rate = *minmax.second;
                        }
                    }

                    // Increment sample count
                    m_sample_count++;

                    // Log every 60 samples (approximately once per second at 60Hz)
                    if (m_sample_count.load() % 60 == 0) {
                        LogInfo("Refresh rate: %.2f Hz (smoothed: %.2f Hz, samples: %u)",
                               refresh_rate, new_smoothed, m_sample_count.load());
                    }
                }
            } else {
                // First sample - just record the time
                m_first_sample = false;
            }

            // Update last vblank time
            m_last_vblank_time = current_time;

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
