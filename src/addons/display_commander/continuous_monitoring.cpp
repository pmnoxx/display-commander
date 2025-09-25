#include "addon.hpp"
#include "adhd_multi_monitor/adhd_simple_api.hpp"
#include "background_window.hpp"
#include "globals.hpp"
#include "hooks/api_hooks.hpp"
#include "nvapi/dlss_preset_detector.hpp"
#include "nvapi/dlssfg_version_detector.hpp"
#include "settings/main_tab_settings.hpp"
#include "ui/ui_display_tab.hpp"
#include "utils.hpp"
#include "utils/timing.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <thread>


// External reference to screensaver mode setting
extern std::atomic<ScreensaverMode> s_screensaver_mode;

void every1s_checks() {
    static int seconds_counter = 0;

    // Get the current swapchain window
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd != nullptr && IsWindow(hwnd)) {
        // BACKGROUND DETECTION: Check if the app is in background using original GetForegroundWindow
        HWND actual_foreground = display_commanderhooks::GetForegroundWindow_Original
                                     ? display_commanderhooks::GetForegroundWindow_Original()
                                     : GetForegroundWindow();
        bool current_background = (actual_foreground != hwnd);
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
            oss << "Continuous monitoring: Background feature check - s_background_feature_enabled = "
                << (s_background_feature_enabled.load() ? "true" : "false")
                << ", has_background_window = " << g_backgroundWindowManager.HasBackgroundWindow();
            LogInfo(oss.str().c_str());
        }

        // FOCUS LOSS DETECTION: Close background window when main window loses focus
        HWND foreground_window = display_commanderhooks::GetForegroundWindow_Original
                                     ? display_commanderhooks::GetForegroundWindow_Original()
                                     : GetForegroundWindow();

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

    // SCREENSAVER MANAGEMENT: Update execution state based on screensaver mode and background status
    {
        ScreensaverMode screensaver_mode = s_screensaver_mode.load();
        bool is_background = g_app_in_background.load();
        EXECUTION_STATE desired_state = 0;

        switch (screensaver_mode) {
        case ScreensaverMode::kDisableWhenFocused:
            if (is_background) { // enabled when background
                desired_state = ES_CONTINUOUS;
            } else { // disabled when focused
                desired_state = ES_CONTINUOUS | ES_DISPLAY_REQUIRED;
            }
            break;
        case ScreensaverMode::kDisable: // always disable screensaver
            desired_state = ES_CONTINUOUS | ES_DISPLAY_REQUIRED;
            break;
        case ScreensaverMode::kDefault: // default behavior
            desired_state = ES_CONTINUOUS;
            break;
        }

        // Only call SetThreadExecutionState if the desired state is different from the last state
        static EXECUTION_STATE last_execution_state = 0;
        if (desired_state != last_execution_state) {
            last_execution_state = desired_state;
            if (display_commanderhooks::SetThreadExecutionState_Original) {
                EXECUTION_STATE result = display_commanderhooks::SetThreadExecutionState_Original(desired_state);
                if (result != 0) {
                    LogDebug("Screensaver management: SetThreadExecutionState(0x%x) = 0x%x", desired_state, result);
                }
            }
        }
    }

    // BACKGROUND: Composition state logging and periodic device/colorspace refresh
    // NOTE: This functionality has been moved to OnPresentUpdateAfter to avoid
    // accessing g_last_swapchain_ptr from the continuous monitoring thread

    // Aggregate FPS/frametime metrics and publish shared text once per second
    {
        extern std::atomic<uint32_t> g_perf_ring_head;
        extern PerfSample g_perf_ring[kPerfRingCapacity];
        extern std::atomic<double> g_perf_time_seconds;
        extern std::atomic<bool> g_perf_reset_requested;
        extern std::atomic<std::shared_ptr<const std::string>> g_perf_text_shared;

        const double now_s = g_perf_time_seconds.load(std::memory_order_acquire);

        // Handle reset: clear samples efficiently by advancing head and zeroing text
        if (g_perf_reset_requested.exchange(false, std::memory_order_acq_rel)) {
            g_perf_ring_head.store(0, std::memory_order_release);
        }

        // Copy samples since last reset into local vectors for computation
        std::vector<float> fps_values;
        std::vector<float> frame_times_ms;
        fps_values.reserve(2048);
        frame_times_ms.reserve(2048);
        const uint32_t head = g_perf_ring_head.load(std::memory_order_acquire);
        const uint32_t count =
            (head > static_cast<uint32_t>(kPerfRingCapacity)) ? static_cast<uint32_t>(kPerfRingCapacity) : head;
        const uint32_t start = head - count;
        for (uint32_t i = start; i < head; ++i) {
            const PerfSample &s = g_perf_ring[i & (kPerfRingCapacity - 1)];
            if (s.fps > 0.0f) {
                fps_values.push_back(s.fps);
                frame_times_ms.push_back(1000.0f / s.fps);
            }
        }

        float fps_display = 0.0f;
        float frame_time_ms = 0.0f;
        float one_percent_low = 0.0f;
        float point_one_percent_low = 0.0f;
        float p99_frame_time_ms = 0.0f;  // Top 1% (99th percentile) frame time
        float p999_frame_time_ms = 0.0f; // Top 0.1% (99.9th percentile) frame time

        if (!fps_values.empty()) {
            // Average FPS over entire interval since reset = frames / total_time
            std::vector<float> fps_sorted = fps_values;
            std::sort(fps_sorted.begin(), fps_sorted.end());
            const size_t n = fps_sorted.size();
            double total_frame_time_ms = 0.0;
            for (float ft : frame_times_ms)
                total_frame_time_ms += static_cast<double>(ft);
            const double total_seconds = total_frame_time_ms / 1000.0;
            fps_display = (total_seconds > 0.0) ? static_cast<float>(static_cast<double>(n) / total_seconds) : 0.0f;
            // Median frame time for display in ms
            std::vector<float> ft_for_median = frame_times_ms;
            std::sort(ft_for_median.begin(), ft_for_median.end());
            frame_time_ms =
                (n % 2 == 1) ? ft_for_median[n / 2] : 0.5f * (ft_for_median[n / 2 - 1] + ft_for_median[n / 2]);

            // Compute lows and top frame times using frame time distribution (more robust)
            std::vector<float> ft_sorted = frame_times_ms;
            std::sort(ft_sorted.begin(), ft_sorted.end()); // ascending: fast -> slow
            const size_t m = ft_sorted.size();
            const size_t count_1 = (std::max<size_t>)(static_cast<size_t>(static_cast<double>(m) * 0.01), size_t(1));
            const size_t count_01 = (std::max<size_t>)(static_cast<size_t>(static_cast<double>(m) * 0.001), size_t(1));

            // Average of slowest 1% and 0.1% frametimes, then convert to FPS
            double sum_ft_1 = 0.0;
            for (size_t i = m - count_1; i < m; ++i)
                sum_ft_1 += static_cast<double>(ft_sorted[i]);
            const double avg_ft_1 = sum_ft_1 / static_cast<double>(count_1);
            one_percent_low = (avg_ft_1 > 0.0) ? static_cast<float>(1000.0 / avg_ft_1) : 0.0f;

            double sum_ft_01 = 0.0;
            for (size_t i = m - count_01; i < m; ++i)
                sum_ft_01 += static_cast<double>(ft_sorted[i]);
            const double avg_ft_01 = sum_ft_01 / static_cast<double>(count_01);
            point_one_percent_low = (avg_ft_01 > 0.0) ? static_cast<float>(1000.0 / avg_ft_01) : 0.0f;

            // Percentile frame times (top 1%/0.1% = 99th/99.9th percentile)
            const size_t idx_p99 =
                (m > 1) ? (std::min<size_t>)(m - 1, static_cast<size_t>(std::ceil(static_cast<double>(m) * 0.99)) - 1)
                        : 0;
            const size_t idx_p999 =
                (m > 1) ? (std::min<size_t>)(m - 1, static_cast<size_t>(std::ceil(static_cast<double>(m) * 0.999)) - 1)
                        : 0;
            p99_frame_time_ms = ft_sorted[idx_p99];
            p999_frame_time_ms = ft_sorted[idx_p999];
        }

        // Publish shared text (once per loop ~1s)
        std::ostringstream fps_oss;
        fps_oss << "FPS: " << std::fixed << std::setprecision(1) << fps_display << " (" << std::setprecision(1)
                << frame_time_ms << " ms median)"
                << "   (1% Low: " << std::setprecision(1) << one_percent_low << ", 0.1% Low: " << std::setprecision(1)
                << point_one_percent_low << ")"
                << "   Top FT: P99 " << std::setprecision(1) << p99_frame_time_ms << " ms"
                << ", P99.9 " << std::setprecision(1) << p999_frame_time_ms << " ms"
                << " since reset";
        g_perf_text_shared.store(std::make_shared<const std::string>(fps_oss.str()));
    }

    // DLSS-FG Detection: Check every 5 seconds for runtime-loaded DLSS-G DLLs
    {
        if ((seconds_counter % 5) == 0) {
            // Only try to detect if we haven't found it yet
            if (!g_dlssfg_detected.load()) {
                if (g_dlssfgVersionDetector.RefreshVersion()) {
                    if (g_dlssfgVersionDetector.IsAvailable()) {
                        const auto &version = g_dlssfgVersionDetector.GetVersion();
                        LogInfo("DLSS-FG detected at runtime - Version: %s (DLL: %s)",
                                version.getFormattedVersion().c_str(), version.dll_path.c_str());
                        g_dlssfg_detected.store(true);

                        // Update global DLLS-G variables
                        g_dlls_g_loaded.store(true);
                        g_dlls_g_version.store(std::make_shared<const std::string>(version.getFormattedVersion()));
                    }
                }

                // DLSS Preset Detection: Check every 5 seconds for runtime-loaded DLSS presets
                if (!g_dlss_preset_detected.load()) {
                    if (g_dlssPresetDetector.RefreshPreset()) {
                        if (g_dlssPresetDetector.IsAvailable()) {
                            const auto &preset = g_dlssPresetDetector.GetPreset();
                            LogInfo("DLSS Preset detected at runtime - Preset: %s, Quality: %s",
                                    preset.getFormattedPreset().c_str(), preset.getFormattedQualityMode().c_str());
                            g_dlss_preset_detected.store(true);

                            // Update global DLSS preset variables
                            g_dlss_preset_name.store(std::make_shared<const std::string>(preset.getFormattedPreset()));
                            g_dlss_quality_mode.store(
                                std::make_shared<const std::string>(preset.getFormattedQualityMode()));
                        }
                    }
                }
            }
        }
    }
    ++seconds_counter;
}

// Main monitoring thread function
void ContinuousMonitoringThread() {
    LogInfo("Continuous monitoring thread started");

    auto start_time = utils::get_now_ns();
    LONGLONG last_cache_refresh_ns = start_time;
    LONGLONG last_60fps_update_ns = start_time;
    LONGLONG last_1s_update_ns = start_time;
    const LONGLONG FPS_60_INTERVAL_NS = utils::SEC_TO_NS / 60; // ~16.67ms for 60 FPS

    while (g_monitoring_thread_running.load()) {
        // Periodic display cache refresh off the UI thread
        {
            LONGLONG now_ns = utils::get_now_ns();
            if (now_ns - last_cache_refresh_ns >= 2 * utils::SEC_TO_NS) {
                display_cache::g_displayCache.Refresh();
                last_cache_refresh_ns = now_ns;
                auto labels = ui::GetMonitorLabelsFromCache();
                auto next = std::make_shared<const std::vector<std::string>>(std::move(labels));
                ::g_monitor_labels.store(next, std::memory_order_release);
            }
        }
        // Wait for 1 second to start
        if (utils::get_now_ns() - start_time < 1 * utils::SEC_TO_NS) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        // Check if monitoring is still enabled
        if (!s_continuous_monitoring_enabled.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // 60 FPS updates (every ~16.67ms)
        LONGLONG now_ns = utils::get_now_ns();
        if (now_ns - last_60fps_update_ns >= FPS_60_INTERVAL_NS) {
            last_60fps_update_ns = now_ns;
            adhd_multi_monitor::api::Initialize();
            adhd_multi_monitor::api::SetEnabled(settings::g_mainTabSettings.adhd_multi_monitor_enabled.GetValue());

            // Update ADHD Multi-Monitor Mode at 60 FPS
            adhd_multi_monitor::api::Update();
        }

        if (now_ns - last_1s_update_ns >= 1 * utils::SEC_TO_NS) {
            last_1s_update_ns = now_ns;
            every1s_checks();
        }

        // Sleep for 1 second
        // Sleep for 60 FPS timing (~16.67ms)
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    LogInfo("Continuous monitoring thread stopped");
}

// Start continuous monitoring
void StartContinuousMonitoring() {
    if (g_monitoring_thread_running.load()) {
        LogDebug("Continuous monitoring already running");
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
