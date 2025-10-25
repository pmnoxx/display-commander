#include "addon.hpp"
#include "adhd_multi_monitor/adhd_simple_api.hpp"
#include "audio/audio_management.hpp"
#include "autoclick/autoclick_manager.hpp"
#include "background_window.hpp"
#include "globals.hpp"
#include "hooks/api_hooks.hpp"
#include "hooks/windows_hooks/windows_message_hooks.hpp"
#include "nvapi/reflex_manager.hpp"
#include "settings/experimental_tab_settings.hpp"
#include "settings/main_tab_settings.hpp"
#include "settings/developer_tab_settings.hpp"
#include "ui/new_ui/swapchain_tab.hpp"
#include "utils/logging.hpp"
#include "utils/timing.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <thread>

// #define TRY_CATCH_BLOCKS

// External reference to screensaver mode setting
extern std::atomic<ScreensaverMode> s_screensaver_mode;

HWND GetCurrentForeGroundWindow() {
    HWND foreground_window = display_commanderhooks::GetForegroundWindow_Direct();

    DWORD window_pid = 0;
    DWORD thread_id = GetWindowThreadProcessId(foreground_window, &window_pid);

    return window_pid == GetCurrentProcessId() ? foreground_window : nullptr;
}

void HandleReflexAutoConfigure() {
    // Only run if auto-configure is enabled
    if (!settings::g_developerTabSettings.reflex_auto_configure.GetValue()) {
        return;
    }


    // Check if native Reflex is active
    bool is_native_reflex_active = g_nvapi_event_counters[NVAPI_EVENT_D3D_SET_SLEEP_MODE].load() > 0;

    bool is_reflex_mode = static_cast<FpsLimiterMode>(settings::g_mainTabSettings.fps_limiter_mode.GetValue()) == FpsLimiterMode::kReflex;

    // Get current settings
    bool reflex_enable = settings::g_developerTabSettings.reflex_enable.GetValue();
    bool reflex_low_latency = settings::g_developerTabSettings.reflex_low_latency.GetValue();
    bool reflex_boost = settings::g_developerTabSettings.reflex_boost.GetValue();
    bool reflex_use_markers = settings::g_developerTabSettings.reflex_use_markers.GetValue();
    bool reflex_generate_markers = settings::g_developerTabSettings.reflex_generate_markers.GetValue();
    bool reflex_enable_sleep = settings::g_developerTabSettings.reflex_enable_sleep.GetValue();

    // Auto-configure Reflex settings
    if (reflex_enable != is_reflex_mode) {
        settings::g_developerTabSettings.reflex_enable.SetValue(is_reflex_mode);
        s_reflex_enable.store(is_reflex_mode);

        if (is_reflex_mode == false) {
            // TODO move logic to Con
            auto params = g_last_nvapi_sleep_mode_params.load();
            ReflexManager::RestoreSleepMode(g_last_nvapi_sleep_mode_dev_ptr.load(), params ? params.get() : nullptr);
        }
    }

   /* if (!reflex_low_latency) {
        settings::g_developerTabSettings.reflex_low_latency.SetValue(true);
        s_reflex_low_latency.store(true);
    }

    if (!reflex_boost) {
        settings::g_developerTabSettings.reflex_boost.SetValue(true);
        s_reflex_boost.store(true);
    } */
    {
        settings::g_developerTabSettings.reflex_use_markers.SetValue(true);
        s_reflex_use_markers.store(true);
    }

    if (reflex_generate_markers == is_native_reflex_active) {
        settings::g_developerTabSettings.reflex_generate_markers.SetValue(!is_native_reflex_active);
        s_reflex_generate_markers.store(!is_native_reflex_active);
    }

    if (reflex_enable_sleep == is_native_reflex_active) {
        settings::g_developerTabSettings.reflex_enable_sleep.SetValue(!is_native_reflex_active);
        s_reflex_enable_sleep.store(!is_native_reflex_active);
    }
}

void check_is_background() {
    // Get the current swapchain window
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd != nullptr) {
        // BACKGROUND DETECTION: Check if the app is in background using original GetForegroundWindow
        HWND current_foreground_hwnd = GetCurrentForeGroundWindow();
        bool app_in_background = current_foreground_hwnd == nullptr;

        if (app_in_background != g_app_in_background.load()) {
            g_app_in_background.store(app_in_background);

            if (app_in_background) {
                LogInfo("Continuous monitoring: App moved to BACKGROUND");
                ReleaseCapture();
                // Release cursor clipping when going to background
                display_commanderhooks::ClipCursor_Direct(nullptr);

                // Set cursor to default arrow when moving to background
                //display_commanderhooks::SetCursor_Direct(LoadCursor(nullptr, IDC_ARROW));

                // Hide cursor when moving to background
                //display_commanderhooks::ShowCursor_Direct(TRUE);
            } else {
                LogInfo("Continuous monitoring: App moved to FOREGROUND");
                // Restore cursor clipping when coming to foreground
                display_commanderhooks::RestoreClipCursor();
                LogInfo("Continuous monitoring: Restored cursor clipping for foreground");

                //display_commanderhooks::RestoreSetCursor();

                //display_commanderhooks::RestoreShowCursor();
            }
        }

        // Apply window changes - the function will automatically determine what needs to be changed
        ApplyWindowChange(hwnd, "continuous_monitoring_auto_fix");

        if (s_background_feature_enabled.load()) {
            // Only create/update background window if main window has focus
            if (current_foreground_hwnd != nullptr) {  //            if (foreground_window == hwnd) {
                LogInfo("Continuous monitoring: Calling UpdateBackgroundWindow for background window management");
                g_backgroundWindowManager.UpdateBackgroundWindow(current_foreground_hwnd);
            }
        }
    }

}

void every1s_checks() {

    // SCREENSAVER MANAGEMENT: Update execution state based on screensaver mode and background status
    {
        ScreensaverMode screensaver_mode = s_screensaver_mode.load();
        bool is_background = g_app_in_background.load();
        EXECUTION_STATE desired_state = 0;

        switch (screensaver_mode) {
            case ScreensaverMode::kDisableWhenFocused:
                if (is_background) {  // enabled when background
                    desired_state = ES_CONTINUOUS;
                } else {  // disabled when focused
                    desired_state = ES_CONTINUOUS | ES_DISPLAY_REQUIRED;
                }
                break;
            case ScreensaverMode::kDisable:  // always disable screensaver
                desired_state = ES_CONTINUOUS | ES_DISPLAY_REQUIRED;
                break;
            case ScreensaverMode::kDefault:  // default behavior
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
        float p99_frame_time_ms = 0.0f;   // Top 1% (99th percentile) frame time
        float p999_frame_time_ms = 0.0f;  // Top 0.1% (99.9th percentile) frame time

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
            frame_time_ms =
                (n % 2 == 1) ? ft_for_median[n / 2] : 0.5f * (ft_for_median[n / 2 - 1] + ft_for_median[n / 2]);

            // Compute lows and top frame times using frame time distribution (more robust)
            std::vector<float> ft_sorted = frame_times_ms;
            std::sort(ft_sorted.begin(), ft_sorted.end());  // ascending: fast -> slow
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
                << ", P99.9 " << std::setprecision(1) << p999_frame_time_ms << " ms";
        g_perf_text_shared.store(std::make_shared<const std::string>(fps_oss.str()));
    }

}

void HandleKeyboardShortcuts() {
      // Check if hotkeys are enabled globally
      if (!s_enable_hotkeys.load()) {
          return; // Exit early if hotkeys are disabled
      }

      // Handle keyboard shortcuts (only when game is in foreground)
      HWND game_hwnd = g_last_swapchain_hwnd.load();
      bool is_game_in_foreground = (game_hwnd != nullptr && GetForegroundWindow() == game_hwnd);

      if (s_enable_mute_unmute_shortcut.load() && is_game_in_foreground) {
          // Use our keyboard tracker instead of ReShade runtime
          if (display_commanderhooks::keyboard_tracker::IsKeyPressed('M') &&
              display_commanderhooks::keyboard_tracker::IsKeyDown(VK_CONTROL)) {
              // Toggle mute state
              bool new_mute_state = !s_audio_mute.load();
              if (SetMuteForCurrentProcess(new_mute_state)) {
                  s_audio_mute.store(new_mute_state);
                  g_muted_applied.store(new_mute_state);

                  // Log the action
                  std::ostringstream oss;
                  oss << "Audio " << (new_mute_state ? "muted" : "unmuted") << " via Ctrl+M shortcut";
                  LogInfo(oss.str().c_str());
              }
          }
      }



      // Handle Ctrl+P shortcut for auto-click toggle (only when game is in foreground and advanced settings enabled)
      if (s_enable_autoclick_shortcut.load() && is_game_in_foreground && settings::g_mainTabSettings.advanced_settings_enabled.GetValue()) {
          // Use our keyboard tracker instead of ReShade runtime
          if (display_commanderhooks::keyboard_tracker::IsKeyPressed('P') &&
              display_commanderhooks::keyboard_tracker::IsKeyDown(VK_CONTROL)) {
              LogInfo("Ctrl+P shortcut detected - toggling auto-click");
              // Toggle auto-click sequences
              autoclick::ToggleAutoClickEnabled();
          }
      }

      // Handle Ctrl+R shortcut for background toggle (only when game is in
      // foreground)
      if (s_enable_background_toggle_shortcut.load() && is_game_in_foreground) {
          // Use our keyboard tracker instead of ReShade runtime
          if (display_commanderhooks::keyboard_tracker::IsKeyPressed('R') &&
              display_commanderhooks::keyboard_tracker::IsKeyDown(VK_CONTROL)) {
              // Toggle render setting and make present follow the same state
              bool new_render_state = !s_no_render_in_background.load();
              bool new_present_state = new_render_state; // Present always follows render state

              s_no_render_in_background.store(new_render_state);
              s_no_present_in_background.store(new_present_state);

              // Update the settings in the UI as well
              settings::g_mainTabSettings.no_render_in_background.SetValue(new_render_state);
              settings::g_mainTabSettings.no_present_in_background.SetValue(new_present_state);

              // Log the action
              std::ostringstream oss;
              oss << "Background settings toggled via Ctrl+R shortcut - Both Render "
                     "and Present: "
                  << (new_render_state ? "disabled" : "enabled");
              LogInfo(oss.str().c_str());
          }
      }

      // Handle Ctrl+T shortcut for time slowdown toggle (only when game is in foreground)
      if (s_enable_timeslowdown_shortcut.load() && is_game_in_foreground) {
          // Use our keyboard tracker instead of ReShade runtime
          if (display_commanderhooks::keyboard_tracker::IsKeyPressed('T') &&
              display_commanderhooks::keyboard_tracker::IsKeyDown(VK_CONTROL)) {
              // Toggle time slowdown state
              bool current_state = settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue();
              bool new_state = !current_state;

              settings::g_experimentalTabSettings.timeslowdown_enabled.SetValue(new_state);

              // Log the action
              std::ostringstream oss;
              oss << "Time Slowdown " << (new_state ? "enabled" : "disabled") << " via Ctrl+T shortcut";
              LogInfo(oss.str().c_str());
          }
      }

      // Handle Ctrl+O shortcut for performance overlay toggle (only when game is in foreground)
      if (is_game_in_foreground) {
          // Use our keyboard tracker instead of ReShade runtime
          if (display_commanderhooks::keyboard_tracker::IsKeyPressed('O') &&
              display_commanderhooks::keyboard_tracker::IsKeyDown(VK_CONTROL)) {
              // Toggle performance overlay state
              bool current_state = settings::g_mainTabSettings.show_test_overlay.GetValue();
              bool new_state = !current_state;

              settings::g_mainTabSettings.show_test_overlay.SetValue(new_state);

              // Log the action
              std::ostringstream oss;
              oss << "Performance overlay " << (new_state ? "enabled" : "disabled") << " via Ctrl+O shortcut";
              LogInfo(oss.str().c_str());
          }
      }

      // Handle Ctrl+D shortcut for ADHD toggle (only when game is in foreground)
      if (s_enable_adhd_toggle_shortcut.load() && is_game_in_foreground) {
          // Use our keyboard tracker instead of ReShade runtime
          if (display_commanderhooks::keyboard_tracker::IsKeyPressed('D') &&
              display_commanderhooks::keyboard_tracker::IsKeyDown(VK_CONTROL)) {
              // Toggle ADHD Multi-Monitor Mode state
              bool current_state = settings::g_mainTabSettings.adhd_multi_monitor_enabled.GetValue();
              bool new_state = !current_state;

              settings::g_mainTabSettings.adhd_multi_monitor_enabled.SetValue(new_state);
              adhd_multi_monitor::api::SetEnabled(new_state);

              // Log the action
              std::ostringstream oss;
              oss << "ADHD Multi-Monitor Mode " << (new_state ? "enabled" : "disabled") << " via Ctrl+D shortcut";
              LogInfo(oss.str().c_str());
          }
      }

      // Handle Ctrl+I shortcut for input blocking toggle (only when game is in foreground)
      if (s_enable_input_blocking_shortcut.load() && is_game_in_foreground) {
          // Use our keyboard tracker instead of ReShade runtime
          if (display_commanderhooks::keyboard_tracker::IsKeyPressed('I') &&
              display_commanderhooks::keyboard_tracker::IsKeyDown(VK_CONTROL)) {
              // Toggle input blocking state
              bool current_state = s_input_blocking_toggle.load();
              bool new_state = !current_state;

              s_input_blocking_toggle.store(new_state);

              // Log the action
              std::ostringstream oss;
              oss << "Input Blocking " << (new_state ? "enabled" : "disabled") << " via Ctrl+I shortcut";
              LogInfo(oss.str().c_str());
          }
      }
}

// Main monitoring thread function
void ContinuousMonitoringThread() {
#ifdef TRY_CATCH_BLOCKS
    __try {
#endif
    LogInfo("Continuous monitoring thread started");

    auto start_time = utils::get_now_ns();
    LONGLONG last_cache_refresh_ns = start_time;
    LONGLONG last_60fps_update_ns = start_time;
    LONGLONG last_1s_update_ns = start_time;
    const LONGLONG fps_120_interval_ns = utils::SEC_TO_NS / 120;

    while (g_monitoring_thread_running.load()) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(fps_120_interval_ns));
        // Periodic display cache refresh off the UI thread
        {
            LONGLONG now_ns = utils::get_now_ns();
            if (now_ns - last_cache_refresh_ns >= 2 * utils::SEC_TO_NS) {
                display_cache::g_displayCache.Refresh();
                last_cache_refresh_ns = now_ns;
                // No longer need to cache monitor labels - UI calls GetDisplayInfoForUI() directly
            }
        }
        // Wait for 1 second to start
        if (utils::get_now_ns() - start_time < 1 * utils::SEC_TO_NS) {
            continue;
        }
        // Auto-apply is always enabled (checkbox removed)
        // 60 FPS updates (every ~16.67ms)
        LONGLONG now_ns = utils::get_now_ns();
        if (now_ns - last_60fps_update_ns >= fps_120_interval_ns) {
            check_is_background();
            last_60fps_update_ns = now_ns;
            adhd_multi_monitor::api::Initialize();
            adhd_multi_monitor::api::SetEnabled(settings::g_mainTabSettings.adhd_multi_monitor_enabled.GetValue());

            // Update ADHD Multi-Monitor Mode at 60 FPS
            adhd_multi_monitor::api::Update();

            // Update keyboard tracking system
            display_commanderhooks::keyboard_tracker::Update();

            // Handle keyboard shortcuts
            HandleKeyboardShortcuts();

            // Reset keyboard frame states for next frame
            display_commanderhooks::keyboard_tracker::ResetFrame();
        }

        if (now_ns - last_1s_update_ns >= 1 * utils::SEC_TO_NS) {
            last_1s_update_ns = now_ns;
            every1s_checks();

            // wait 10s before configuring reflex
            if (now_ns - start_time >= 10 * utils::SEC_TO_NS) {
                HandleReflexAutoConfigure();
            }

            // Call auto-apply HDR metadata trigger
            ui::new_ui::AutoApplyTrigger();
        }
    }
#ifdef TRY_CATCH_BLOCKS
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        LogError("Exception occurred during Continuous Monitoring: 0x%x", GetExceptionCode());
    }
#endif

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
