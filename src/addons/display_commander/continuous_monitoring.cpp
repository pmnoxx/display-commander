#include "addon.hpp"
#include "utils.hpp"
#include "background_window.hpp"
#include "dxgi/dxgi_device_info.hpp"
#include <thread>
#include <chrono>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include "globals.hpp"

// Forward declarations
void ComputeDesiredSize(int& out_w, int& out_h);

// Main monitoring thread function
void ContinuousMonitoringThread() {
    LogInfo("Continuous monitoring thread started");
    
    static int seconds_counter = 0;
    auto last_cache_refresh = std::chrono::steady_clock::now();
    while (g_monitoring_thread_running.load()) {
        // Check if monitoring is still enabled
        if (!s_continuous_monitoring_enabled.load()) {
            break;
        }
        
        // Get the current swapchain window
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd != nullptr && IsWindow(hwnd)) {
            // BACKGROUND DETECTION: Check if the app is in background
            bool current_background = (GetForegroundWindow() != hwnd);
            bool background_changed = (current_background != g_app_in_background.load());
            
            if (background_changed) {
                g_app_in_background.store(current_background);
                
                if (current_background) {
                    LogInfo("Continuous monitoring: App moved to BACKGROUND");
                } else {
                    LogInfo("Continuous monitoring: App moved to FOREGROUND");
                }
            }
            
            // Apply window changes - the function will automatically determine what needs to be changed
            ApplyWindowChange(hwnd, "continuous_monitoring_auto_fix");
            
            // BACKGROUND WINDOW MANAGEMENT: Update background window if feature is enabled
            static int background_check_counter = 0;
            if (++background_check_counter % 10 == 0) { // Log every 10 seconds
                std::ostringstream oss;
                oss << "Continuous monitoring: Background feature check - s_background_feature_enabled = " << (s_background_feature_enabled.load() ? "true" : "false") 
                    << ", has_background_window = " << g_backgroundWindowManager.HasBackgroundWindow();
                LogInfo(oss.str().c_str());
            }
            
            // FOCUS LOSS DETECTION: Close background window when main window loses focus
            HWND foreground_window = GetForegroundWindow();
            if (foreground_window != hwnd && g_backgroundWindowManager.HasBackgroundWindow()) {
                // Main window lost focus, close background window
                LogInfo("Continuous monitoring: Main window lost focus - closing background window");
         
         //       g_backgroundWindowManager.DestroyBackgroundWindow();
            }
            
            if (s_background_feature_enabled.load()) {
                // Only create/update background window if main window has focus
                if (foreground_window == hwnd) {
                    LogInfo("Continuous monitoring: Calling UpdateBackgroundWindow for background window management");
                    g_backgroundWindowManager.UpdateBackgroundWindow(hwnd);
                } else {
                    if (background_check_counter % 10 == 0) { // Log occasionally
                        LogInfo("Continuous monitoring: Skipping background window update - main window not focused");
                    }
                }
            } else {
                if (background_check_counter % 10 == 0) { // Log occasionally
                    LogInfo("Continuous monitoring: Background feature disabled (s_background_feature_enabled = false)");
                }
            }
        }

        // Update monitor labels cache periodically off the UI thread
        {
            // Refresh every 2 seconds to avoid excessive work
            if ((seconds_counter % 2) == 0) {
                auto labels = MakeMonitorLabels();
                auto next = std::make_shared<const std::vector<std::string>>(std::move(labels));
                ::g_monitor_labels.store(next, std::memory_order_release);
            }
        }

        // BACKGROUND: Composition state logging and periodic device/colorspace refresh
        {
            auto* swapchain = g_last_swapchain_ptr.load();
            if (swapchain != nullptr) {
                // Compute DXGI composition state and log on change
                DxgiBypassMode mode = GetIndependentFlipState(swapchain);
                int state = 0;
                switch (mode) {
                    case DxgiBypassMode::kComposed: state = 1; break;
                    case DxgiBypassMode::kOverlay: state = 2; break;
                    case DxgiBypassMode::kIndependentFlip: state = 3; break;
                    case DxgiBypassMode::kUnknown:
                    default: state = 0; break;
                }

                // Update shared state for fast reads on present
                s_dxgi_composition_state.store(state);

                int last = g_comp_last_logged.load();
                if (state != last) {
                    g_comp_last_logged.store(state);
                    std::ostringstream oss;
                    oss << "DXGI Composition State (background): " << DxgiBypassModeToString(mode) << " (" << state << ")";
                    LogInfo(oss.str().c_str());
                }

                // Periodically refresh colorspace and enumerate devices (approx every 4 seconds)
                if ((seconds_counter % 4) == 0) {
                    g_current_colorspace = swapchain->get_color_space();

                    extern std::unique_ptr<DXGIDeviceInfoManager> g_dxgiDeviceInfoManager;
                    if (g_dxgiDeviceInfoManager && g_dxgiDeviceInfoManager->IsInitialized()) {
                        g_dxgiDeviceInfoManager->EnumerateDevicesOnPresent();
                    }
                }
            }
        }

        // Aggregate FPS/frametime metrics and publish shared text once per second
        {
            extern std::atomic<uint32_t> g_perf_ring_head;
            extern PerfSample g_perf_ring[kPerfRingCapacity];
            extern std::atomic<double> g_perf_time_seconds;
            extern std::atomic<bool> g_perf_reset_requested;
            extern std::string g_perf_text_shared;

            const double now_s = g_perf_time_seconds.load(std::memory_order_acquire);

            // Handle reset: clear samples efficiently by advancing head and zeroing text
            if (g_perf_reset_requested.exchange(false, std::memory_order_acq_rel)) {
                g_perf_ring_head.store(0, std::memory_order_release);
                g_perf_text_shared.clear();
            }

            // Copy samples since last reset into local vectors for computation
            std::vector<float> fps_values;
            std::vector<float> frame_times_ms;
            fps_values.reserve(2048);
            frame_times_ms.reserve(2048);
            const uint32_t head = g_perf_ring_head.load(std::memory_order_acquire);
            const uint32_t count = (head > static_cast<uint32_t>(kPerfRingCapacity)) ? static_cast<uint32_t>(kPerfRingCapacity) : head;
            const uint32_t start = head - count;
            for (uint32_t i = start; i < head; ++i) {
                const PerfSample& s = g_perf_ring[i & (kPerfRingCapacity - 1)];
                if (s.fps > 0.0f) {
                    fps_values.push_back(s.fps);
                    frame_times_ms.push_back(1000.0f / s.fps);
                }
            }

            float fps_display = 0.0f;
            float frame_time_ms = 0.0f;
            float one_percent_low = 0.0f;
            float point_one_percent_low = 0.0f;
            float p99_frame_time_ms = 0.0f;    // Top 1% (99th percentile) frame time
            float p999_frame_time_ms = 0.0f;   // Top 0.1% (99.9th percentile) frame time

            if (!fps_values.empty()) {
                // Average FPS over entire interval since reset = frames / total_time
                std::vector<float> fps_sorted = fps_values;
                std::sort(fps_sorted.begin(), fps_sorted.end());
                const size_t n = fps_sorted.size();
                double total_frame_time_ms = 0.0;
                for (float ft : frame_times_ms) total_frame_time_ms += static_cast<double>(ft);
                const double total_seconds = total_frame_time_ms / 1000.0;
                fps_display = (total_seconds > 0.0) ? static_cast<float>(static_cast<double>(n) / total_seconds) : 0.0f;
                // Median frame time for display in ms
                std::vector<float> ft_for_median = frame_times_ms;
                std::sort(ft_for_median.begin(), ft_for_median.end());
                frame_time_ms = (n % 2 == 1) ? ft_for_median[n / 2] : 0.5f * (ft_for_median[n / 2 - 1] + ft_for_median[n / 2]);

                // Compute lows and top frame times using frame time distribution (more robust)
                std::vector<float> ft_sorted = frame_times_ms;
                std::sort(ft_sorted.begin(), ft_sorted.end()); // ascending: fast -> slow
                const size_t m = ft_sorted.size();
                const size_t count_1 = (std::max<size_t>)(static_cast<size_t>(static_cast<double>(m) * 0.01), size_t(1));
                const size_t count_01 = (std::max<size_t>)(static_cast<size_t>(static_cast<double>(m) * 0.001), size_t(1));

                // Average of slowest 1% and 0.1% frametimes, then convert to FPS
                double sum_ft_1 = 0.0;
                for (size_t i = m - count_1; i < m; ++i) sum_ft_1 += static_cast<double>(ft_sorted[i]);
                const double avg_ft_1 = sum_ft_1 / static_cast<double>(count_1);
                one_percent_low = (avg_ft_1 > 0.0) ? static_cast<float>(1000.0 / avg_ft_1) : 0.0f;

                double sum_ft_01 = 0.0;
                for (size_t i = m - count_01; i < m; ++i) sum_ft_01 += static_cast<double>(ft_sorted[i]);
                const double avg_ft_01 = sum_ft_01 / static_cast<double>(count_01);
                point_one_percent_low = (avg_ft_01 > 0.0) ? static_cast<float>(1000.0 / avg_ft_01) : 0.0f;

                // Percentile frame times (top 1%/0.1% = 99th/99.9th percentile)
                const size_t idx_p99 = (m > 1) ? (std::min<size_t>)(m - 1, static_cast<size_t>(std::ceil(static_cast<double>(m) * 0.99)) - 1) : 0;
                const size_t idx_p999 = (m > 1) ? (std::min<size_t>)(m - 1, static_cast<size_t>(std::ceil(static_cast<double>(m) * 0.999)) - 1) : 0;
                p99_frame_time_ms = ft_sorted[idx_p99];
                p999_frame_time_ms = ft_sorted[idx_p999];
            }

            // Publish shared text (once per loop ~1s)
            std::ostringstream fps_oss;
            fps_oss << "FPS: " << std::fixed << std::setprecision(1) << fps_display
                    << " (" << std::setprecision(1) << frame_time_ms << " ms median)"
                    << "   (1% Low: " << std::setprecision(1) << one_percent_low
                    << ", 0.1% Low: " << std::setprecision(1) << point_one_percent_low << ")"
                    << "   Top FT: P99 " << std::setprecision(1) << p99_frame_time_ms << " ms"
                    << ", P99.9 " << std::setprecision(1) << p999_frame_time_ms << " ms"
                    << " since reset";
            ::g_perf_text_lock.lock();
            g_perf_text_shared = fps_oss.str();
            ::g_perf_text_lock.unlock();
        }
        
        // Periodic display cache refresh off the UI thread
        {
            auto now = std::chrono::steady_clock::now();
            if (now - last_cache_refresh >= std::chrono::seconds(5)) {
                display_cache::g_displayCache.Refresh();
                last_cache_refresh = now;
            }
        }
        
        // Sleep for 1 second
        std::this_thread::sleep_for(std::chrono::seconds(1));
        ++seconds_counter;
    }
    
    LogInfo("Continuous monitoring thread stopped");
}

// Start continuous monitoring
void StartContinuousMonitoring() {
    if (g_monitoring_thread_running.load()) {
        LogDebug("Continuous monitoring already running");
        return;
    }
    
    if (!s_continuous_monitoring_enabled.load()) {
        LogDebug("Continuous monitoring disabled, not starting");
        return;
    }
    
    g_monitoring_thread_running.store(true);
    
    // Start the monitoring thread
    if (g_monitoring_thread.joinable()) {
        g_monitoring_thread.join();
    }
    
    g_monitoring_thread = std::thread(ContinuousMonitoringThread);
    
    LogInfo("Continuous monitoring started");
}

// Stop continuous monitoring
void StopContinuousMonitoring() {
    if (!g_monitoring_thread_running.load()) {
        LogDebug("Continuous monitoring not running");
        return;
    }
    
    g_monitoring_thread_running.store(false);
    
    // Wait for thread to finish
    if (g_monitoring_thread.joinable()) {
        g_monitoring_thread.join();
    }
    
    LogInfo("Continuous monitoring stopped");
}
