#include "addon.hpp"
#include "adhd_multi_monitor/adhd_simple_api.hpp"
#include "audio/audio_management.hpp"
#include "autoclick/autoclick_manager.hpp"
#include "background_window.hpp"
#include "globals.hpp"
#include "hooks/api_hooks.hpp"
#include "hooks/windows_hooks/windows_message_hooks.hpp"
#include "nvapi/reflex_manager.hpp"
#include "settings/developer_tab_settings.hpp"
#include "settings/experimental_tab_settings.hpp"
#include "settings/main_tab_settings.hpp"
#include "ui/new_ui/swapchain_tab.hpp"
#include "ui/new_ui/hotkeys_tab.hpp"
#include "utils/logging.hpp"
#include "utils/timing.hpp"
#include "utils/overlay_window_detector.hpp"
#include "widgets/resolution_widget/resolution_widget.hpp"
#include "widgets/resolution_widget/resolution_settings.hpp"

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
    uint64_t now_ns = utils::get_now_ns();
    bool is_native_reflex_active = IsNativeReflexActive(now_ns);

    bool is_reflex_mode =
        static_cast<FpsLimiterMode>(settings::g_mainTabSettings.fps_limiter_mode.GetValue()) == FpsLimiterMode::kReflex
        || (static_cast<FpsLimiterMode>(settings::g_mainTabSettings.fps_limiter_mode.GetValue()) == FpsLimiterMode::kOnPresentSync && settings::g_mainTabSettings.onpresent_sync_enable_reflex.GetValue());

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

        if (is_reflex_mode == false) {
            // TODO move logic to Con
            auto params = g_last_nvapi_sleep_mode_params.load();
            ReflexManager::RestoreSleepMode(g_last_nvapi_sleep_mode_dev_ptr.load(), params ? params.get() : nullptr);
        }
    }

     if (!reflex_low_latency) {
         settings::g_developerTabSettings.reflex_low_latency.SetValue(true);
     }
/*
     if (!reflex_boost) {
         settings::g_developerTabSettings.reflex_boost.SetValue(true);
     } */
    {
        if (!settings::g_developerTabSettings.reflex_use_markers.GetValue()) {
            settings::g_developerTabSettings.reflex_use_markers.SetValue(true);
        }
    }

    if (reflex_generate_markers == is_native_reflex_active && settings::g_developerTabSettings.reflex_generate_markers.GetValue() != !is_native_reflex_active) {
        settings::g_developerTabSettings.reflex_generate_markers.SetValue(!is_native_reflex_active);
    }

    if (reflex_enable_sleep == is_native_reflex_active && settings::g_developerTabSettings.reflex_enable_sleep.GetValue() != !is_native_reflex_active) {
        settings::g_developerTabSettings.reflex_enable_sleep.SetValue(!is_native_reflex_active);
    }
}

void check_is_background() {
    // Get the current swapchain window
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd != nullptr) {
        // BACKGROUND DETECTION: Check if the app is in background using original GetForegroundWindow
        HWND current_foreground_hwnd = display_commanderhooks::GetForegroundWindow_Direct();

        // current pid
        DWORD current_pid = GetCurrentProcessId();

        // foreground pid
        DWORD foreground_pid = 0;
        DWORD foreground_tid = GetWindowThreadProcessId(current_foreground_hwnd, &foreground_pid);

        bool app_in_background = foreground_pid != current_pid;

        if (app_in_background != g_app_in_background.load()) {
            g_app_in_background.store(app_in_background);

            if (app_in_background) {
                LogInfo("Continuous monitoring: App moved to BACKGROUND");
                ReleaseCapture();
                // Release cursor clipping when going to background
                display_commanderhooks::ClipCursor_Direct(nullptr);

                // Set cursor to default arrow when moving to background
                // display_commanderhooks::SetCursor_Direct(LoadCursor(nullptr, IDC_ARROW));

                // Hide cursor when moving to background
                // display_commanderhooks::ShowCursor_Direct(TRUE);
            } else {
                LogInfo("Continuous monitoring: App moved to FOREGROUND");
                // Restore cursor clipping when coming to foreground
                display_commanderhooks::RestoreClipCursor();
                LogInfo("Continuous monitoring: Restored cursor clipping for foreground");

                // If clip cursor feature is enabled, clip cursor to game window
                if (settings::g_mainTabSettings.clip_cursor_enabled.GetValue()) {
                    display_commanderhooks::ClipCursorToGameWindow();
                }

                // display_commanderhooks::RestoreSetCursor();

                // display_commanderhooks::RestoreShowCursor();
            }
        }

        // Apply window changes - the function will automatically determine what needs to be changed
        // Skip if suppress_window_changes is enabled (compatibility feature) or if window mode is kNoChanges
        if (!settings::g_developerTabSettings.suppress_window_changes.GetValue() &&
            s_window_mode.load() != WindowMode::kNoChanges) {
            ApplyWindowChange(hwnd, "continuous_monitoring_auto_fix");
        }

        if (s_background_feature_enabled.load()) {
            // Only create/update background window if main window has focus
            if (current_foreground_hwnd != nullptr) {
                g_backgroundWindowManager.UpdateBackgroundWindow(current_foreground_hwnd);
            }
        }
    }
}

void HandleDiscordOverlayAutoHide() {
    // Only run if auto-hide is enabled
    if (!settings::g_developerTabSettings.auto_hide_discord_overlay.GetValue()) {
        return;
    }

    // Get the game window
    HWND game_window = g_last_swapchain_hwnd.load();
    if (game_window == nullptr || IsWindow(game_window) == FALSE) {
        return;
    }

    // Check if game window is active
    if (g_app_in_background.load()) {
        return;  // Don't hide overlay when game is in background
    }

    // Get all overlapping windows
    auto overlays = display_commander::utils::DetectOverlayWindows(game_window);

    // Find Discord Overlay window
    for (const auto& overlay : overlays) {
        if (overlay.is_above_game && overlay.is_visible) {
            // Check if window title contains "Discord Overlay" (case-insensitive)
            std::wstring title_lower = overlay.window_title;
            std::transform(title_lower.begin(), title_lower.end(), title_lower.begin(), ::towlower);

            if (title_lower.find(L"discord overlay") != std::wstring::npos) {
                // Hide the Discord Overlay window
                ShowWindow(overlay.hwnd, SW_HIDE);
                LogInfo("Auto-hid Discord Overlay window (HWND: 0x%p) to prevent MPO iFlip interference", overlay.hwnd);
                break;  // Only hide the first matching window
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
        extern utils::LockFreeRingBuffer<PerfSample, kPerfRingCapacity> g_perf_ring;
        extern std::atomic<double> g_perf_time_seconds;
        extern std::atomic<bool> g_perf_reset_requested;
        extern std::atomic<std::shared_ptr<const std::string>> g_perf_text_shared;

        const double now_s = g_perf_time_seconds.load(std::memory_order_acquire);

        // Handle reset: clear samples efficiently by resetting ring buffer and zeroing text
        if (g_perf_reset_requested.exchange(false, std::memory_order_acq_rel)) {
            g_perf_ring.Reset();
        }

        // Copy samples since last reset into local vectors for computation
        std::vector<float> fps_values;
        std::vector<float> frame_times_ms;
        fps_values.reserve(2048);
        frame_times_ms.reserve(2048);
        const uint32_t count = g_perf_ring.GetCount();
        for (uint32_t i = 0; i < count; ++i) {
            const PerfSample& s = g_perf_ring.GetSample(i);
            if (s.dt > 0.0f) {
                fps_values.push_back(1.0f / s.dt);
                frame_times_ms.push_back(1000.0f * s.dt);
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
        bool show_labels = settings::g_mainTabSettings.show_labels.GetValue();
        if (show_labels) {
            fps_oss << "FPS: " << std::fixed << std::setprecision(1) << fps_display << " (" << std::setprecision(1)
                    << frame_time_ms << " ms median)"
                    << "   (1% Low: " << std::setprecision(1) << one_percent_low
                    << ", 0.1% Low: " << std::setprecision(1) << point_one_percent_low << ")"
                    << "   Top FT: P99 " << std::setprecision(1) << p99_frame_time_ms << " ms"
                    << ", P99.9 " << std::setprecision(1) << p999_frame_time_ms << " ms";
        } else {
            fps_oss << std::fixed << std::setprecision(1) << fps_display << " (" << std::setprecision(1)
                    << frame_time_ms << " ms median)"
                    << "   (1%: " << std::setprecision(1) << one_percent_low << ", 0.1%: " << std::setprecision(1)
                    << point_one_percent_low << ")"
                    << "   P99 " << std::setprecision(1) << p99_frame_time_ms << " ms"
                    << ", P99.9 " << std::setprecision(1) << p999_frame_time_ms << " ms";
        }
        g_perf_text_shared.store(std::make_shared<const std::string>(fps_oss.str()));
    }
}

void HandleKeyboardShortcuts() {
    // Use new hotkey system
    ui::new_ui::ProcessHotkeys();
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

            // Apply CPU affinity mask if configured
            {
                static int last_cpu_cores = -1;
                int cpu_cores = settings::g_mainTabSettings.cpu_cores.GetValue();

                if (cpu_cores != last_cpu_cores) {
                    last_cpu_cores = cpu_cores;

                    HANDLE process_handle = GetCurrentProcess();
                    DWORD_PTR process_affinity_mask = 0;
                    DWORD_PTR system_affinity_mask = 0;

                    // Get current process affinity
                    if (GetProcessAffinityMask(process_handle, &process_affinity_mask, &system_affinity_mask)) {
                        if (cpu_cores == 0) {
                            // Default: restore system affinity (no change)
                            if (SetProcessAffinityMask(process_handle, system_affinity_mask)) {
                                LogInfo("CPU affinity restored to default (all available cores)");
                            } else {
                                LogError("Failed to restore CPU affinity to default: %lu", GetLastError());
                            }
                        } else {
                            // Create affinity mask for specified number of cores
                            SYSTEM_INFO sys_info = {};
                            GetSystemInfo(&sys_info);
                            DWORD max_cores = sys_info.dwNumberOfProcessors;

                            if (cpu_cores > 0 && cpu_cores <= static_cast<int>(max_cores)) {
                                // Create mask with first N cores enabled
                                DWORD_PTR new_mask = 0;
                                for (DWORD i = 0; i < static_cast<DWORD>(cpu_cores); ++i) {
                                    new_mask |= (static_cast<DWORD_PTR>(1) << i);
                                }

                                // Only apply if mask is valid and different from current
                                if (new_mask != 0 && new_mask != process_affinity_mask) {
                                    if (SetProcessAffinityMask(process_handle, new_mask)) {
                                        LogInfo("CPU affinity set to %d core(s) (mask: 0x%llx)", cpu_cores, static_cast<unsigned long long>(new_mask));
                                    } else {
                                        LogError("Failed to set CPU affinity to %d cores: %lu", cpu_cores, GetLastError());
                                    }
                                }
                            } else {
                                LogError("Invalid CPU cores value: %d (max: %lu)", cpu_cores, max_cores);
                            }
                        }
                    } else {
                        LogError("Failed to get process affinity mask: %lu", GetLastError());
                    }
                }
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

                // Auto-hide Discord Overlay (runs every second)
                HandleDiscordOverlayAutoHide();

                // wait 10s before configuring reflex
                if (now_ns - start_time >= 10 * utils::SEC_TO_NS) {
                    HandleReflexAutoConfigure();
                }

                // Call auto-apply HDR metadata trigger
                ui::new_ui::AutoApplyTrigger();

                // Auto-apply resolution on game start
                static bool auto_apply_on_start_done = false;
                namespace res_widget = display_commander::widgets::resolution_widget;
                if (!auto_apply_on_start_done && res_widget::g_resolution_settings) {
                    if (res_widget::g_resolution_settings->GetAutoApplyOnStart()) {
                        LONGLONG game_start_time_ns = g_game_start_time_ns.load();
                        if (game_start_time_ns > 0) {
                            int delay_seconds = res_widget::g_resolution_settings->GetAutoApplyOnStartDelay();
                            LONGLONG elapsed_ns = now_ns - game_start_time_ns;
                            LONGLONG delay_ns = static_cast<LONGLONG>(delay_seconds) * utils::SEC_TO_NS;

                            if (elapsed_ns >= delay_ns) {
                                LogInfo("Auto-apply on start: %lld seconds elapsed (delay: %d), applying resolution",
                                        elapsed_ns / utils::SEC_TO_NS, delay_seconds);

                                // Apply resolution using the resolution widget
                                if (res_widget::g_resolution_widget) {
                                    // Prepare widget (ensures initialization and settings are loaded)
                                    res_widget::g_resolution_widget->PrepareForAutoApply();

                                    // Apply the resolution
                                    bool success = res_widget::g_resolution_widget->ApplyCurrentSelection();
                                    if (success) {
                                        LogInfo("Auto-apply on start: Successfully applied resolution");
                                    } else {
                                        LogWarn("Auto-apply on start: Failed to apply resolution");
                                    }
                                } else {
                                    LogWarn("Auto-apply on start: Resolution widget not available");
                                }

                                auto_apply_on_start_done = true;
                            }
                        }
                    }
                }
            }
        }
#ifdef TRY_CATCH_BLOCKS
    } __except (EXCEPTION_EXECUTE_HANDLER) {
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
