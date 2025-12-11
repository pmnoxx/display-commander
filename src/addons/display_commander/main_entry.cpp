#include "addon.hpp"
#include "audio/audio_management.hpp"
#include "autoclick/autoclick_manager.hpp"
#include "config/display_commander_config.hpp"
#include "dx11_proxy/dx11_proxy_manager.hpp"
#include "exit_handler.hpp"
#include "globals.hpp"
#include "gpu_completion_monitoring.hpp"
#include "hooks/api_hooks.hpp"
#include "hooks/hid_suppression_hooks.hpp"
#include "hooks/timeslowdown_hooks.hpp"
#include "hooks/window_proc_hooks.hpp"
#include "latency/latency_manager.hpp"
#include "latent_sync/refresh_rate_monitor_integration.hpp"
#include "nvapi/nvapi_fullscreen_prevention.hpp"
#include "process_exit_hooks.hpp"
#include "settings/developer_tab_settings.hpp"
#include "settings/experimental_tab_settings.hpp"
#include "settings/hook_suppression_settings.hpp"
#include "settings/main_tab_settings.hpp"
#include "swapchain_events.hpp"
#include "swapchain_events_power_saving.hpp"
#include "ui/monitor_settings/monitor_settings.hpp"
#include "ui/new_ui/experimental_tab.hpp"
#include "ui/new_ui/main_new_tab.hpp"
#include "ui/new_ui/new_ui_main.hpp"
#include "res/forkawesome.h"
#include "res/ui_colors.hpp"
#include "utils/logging.hpp"
#include "utils/timing.hpp"
#include "version.hpp"
#include "widgets/dualsense_widget/dualsense_widget.hpp"

#include <d3d11.h>
#include <psapi.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>
#include <reshade.hpp>

// Forward declarations for ReShade event handlers
void OnInitEffectRuntime(reshade::api::effect_runtime* runtime);
bool OnReShadeOverlayOpen(reshade::api::effect_runtime* runtime, bool open, reshade::api::input_source source);

// Forward declaration for ReShade settings override
void OverrideReShadeSettings();

// Forward declaration for version check
bool CheckReShadeVersionCompatibility();

// Forward declaration for multiple ReShade detection
void DetectMultipleReShadeVersions();

// Forward declaration for safemode function
void HandleSafemode();

// Function to parse version string and check if it's 6.6.2 or above
bool IsVersion662OrAbove(const std::string& version_str) {
    if (version_str.empty()) {
        return false;
    }

    // Parse version string in format "major.minor.build.revision"
    // We need to check if version is >= 6.6.2.0
    int major = 0;
    int minor = 0;
    int build = 0;
    int revision = 0;

    if (sscanf_s(version_str.c_str(), "%d.%d.%d.%d", &major, &minor, &build, &revision) >= 2) {
        // Check if version is 6.6.2 or above
        if (major > 6) {
            return true;  // Major version > 6
        }
        if (major == 6) {
            if (minor > 6) {
                return true;  // 6.x where x > 6
            }
            if (minor == 6) {
                return build >= 2;  // 6.6.x where x >= 2
            }
        }
    }

    return false;
}

// Structure to store ReShade module detection debug information
struct ReShadeModuleInfo {
    std::string path;
    std::string version;
    bool has_imgui_support;
    bool is_version_662_or_above;
    HMODULE handle;
};

struct ReShadeDetectionDebugInfo {
    int total_modules_found = 0;
    std::vector<ReShadeModuleInfo> modules;
    bool detection_completed = false;
    std::string error_message;
};

// Global debug information storage
ReShadeDetectionDebugInfo g_reshade_debug_info;
namespace {
void OnRegisterOverlayDisplayCommander(reshade::api::effect_runtime* runtime) {
#ifdef TRY_CATCH_BLOCKS
    __try {
#endif
        const bool show_display_commander_ui = settings::g_mainTabSettings.show_display_commander_ui.GetValue();
        // Avoid displaying UI twice
        if (show_display_commander_ui) {
            return;
        }
        // Update UI draw time for auto-click optimization
        if (enabled_experimental_features) {
            autoclick::UpdateLastUIDrawTime();
        }

        ui::new_ui::NewUISystem::GetInstance().Draw(runtime);

        // Periodically save config to ensure settings are persisted
        static LONGLONG last_save_time = utils::get_now_ns();
        LONGLONG now = utils::get_now_ns();
        if ((now - last_save_time) >= 5 * utils::SEC_TO_NS) {
            display_commander::config::save_config("periodic save (every 5 seconds)");
            last_save_time = now;
        }
#ifdef TRY_CATCH_BLOCKS
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LogError("Exception occurred during Continuous Monitoring: 0x%x", GetExceptionCode());
    }
#endif
}
}  // namespace

// ReShade effect runtime event handler for input blocking
void OnInitEffectRuntime(reshade::api::effect_runtime* runtime) {
#ifdef TRY_CATCH_BLOCKS
    __try {
#endif
        if (runtime == nullptr) {
            return;
        }
        AddReShadeRuntime(runtime);
        LogInfo("ReShade effect runtime initialized - Input blocking now available");

        static bool initialized_with_hwnd = false;
        if (!initialized_with_hwnd) {
            // Set up window procedure hooks now that we have the runtime
            HWND game_window = static_cast<HWND>(runtime->get_hwnd());
            if (game_window != nullptr && IsWindow(game_window) != 0) {
                LogInfo("Game window detected - HWND: 0x%p", game_window);

                // Initialize if not already done
                DoInitializationWithHwnd(game_window);
            } else {
                LogWarn("ReShade runtime window is not valid - HWND: 0x%p", game_window);
            }
            initialized_with_hwnd = true;

            // Start the auto-click thread (always running, sleeps when disabled)
            if (enabled_experimental_features) {
                autoclick::StartAutoClickThread();
                autoclick::StartUpDownKeyPressThread();
                autoclick::StartButtonOnlyPressThread();
            }
        }
#ifdef TRY_CATCH_BLOCKS
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LogError("Exception occurred during OnInitEffectRuntime: 0x%x", GetExceptionCode());
    }
#endif
}

// ReShade overlay event handler for input blocking
bool OnReShadeOverlayOpen(reshade::api::effect_runtime* runtime, bool open, reshade::api::input_source source) {
    // store last frame id, when UI was opened
    g_last_ui_drawn_frame_id.store(g_global_frame_id.load());

    if (open) {
        LogInfo("ReShade overlay opened - Input blocking active");
        // When ReShade overlay opens, we can also use its input blocking
        if (runtime != nullptr) {
            AddReShadeRuntime(runtime);
        }
    } else {
        LogInfo("ReShade overlay closed - Input blocking inactive");
    }

    // Update auto-click UI state for optimization
    if (enabled_experimental_features) {
        autoclick::UpdateUIOverlayState(open);
    }

    return false;  // Don't prevent ReShade from opening/closing the overlay
}

// Direct overlay draw callback (no settings2 indirection)
namespace {

// Cursor state machine for tracking cursor visibility
enum class CursorState {
    Unknown,    // Initial/unknown state
    Visible,    // Cursor is visible (UI is open)
    Hidden      // Cursor is hidden (UI is closed)
};

// Test callback for reshade_overlay event
void OnReShadeOverlayTest(reshade::api::effect_runtime* runtime) {
    const bool show_display_commander_ui = settings::g_mainTabSettings.show_display_commander_ui.GetValue();
    const bool show_tooltips = show_display_commander_ui; // only show tooltips if the UI is visible

    static CursorState last_cursor_state = CursorState::Unknown;

    if (show_display_commander_ui) {
        // Block input every frame while overlay is open
        if (runtime != nullptr) {
            runtime->block_input_next_frame();
        }

        last_cursor_state = CursorState::Visible;
        // Show cursor while overlay is open (same approach as ReShade)
        ImGuiIO& io = ImGui::GetIO();
        io.MouseDrawCursor = true;

        // Update UI draw time for auto-click optimization
        if (enabled_experimental_features) {
            autoclick::UpdateLastUIDrawTime();
        }

        // IMGui window with fixed width and saved position
        const float fixed_width = 1600.0f;
        float saved_x = settings::g_mainTabSettings.display_commander_ui_window_x.GetValue();
        float saved_y = settings::g_mainTabSettings.display_commander_ui_window_y.GetValue();

        // Restore saved position if available
        static float last_saved_x = 0.0f;
        static float last_saved_y = 0.0f;
        if (saved_x > 0.0f || saved_y > 0.0f) {
            // Only set position if it changed or window was just opened
            if (saved_x != last_saved_x || saved_y != last_saved_y) {
                ImGui::SetNextWindowPos(ImVec2(saved_x, saved_y), ImGuiCond_Once);
                last_saved_x = saved_x;
                last_saved_y = saved_y;
            }
        }

        ImGui::SetNextWindowSize(ImVec2(fixed_width, 0.0f), ImGuiCond_Always);
        ImGui::Begin("Display Commander", nullptr,
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
/*
        // Custom title bar with close button
        float titlebar_height = ImGui::GetTextLineHeight() + (ImGui::GetStyle().FramePadding.y * 2.0f);
        ImGui::BeginChild("##titlebar", ImVec2(0, titlebar_height), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        // Title text
        ImGui::Text("Display Commander");
        ImGui::SameLine();

        // Close button aligned to the right
        float button_size = ImGui::GetTextLineHeight() + (ImGui::GetStyle().FramePadding.x * 2.0f);
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - button_size);
        if (ImGui::Button(ICON_FK_CANCEL, ImVec2(button_size, titlebar_height))) {
            settings::g_mainTabSettings.show_display_commander_ui.SetValue(false);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Close");
        }

        ImGui::EndChild();
*/
        // Save window position when it changes
        ImVec2 current_pos = ImGui::GetWindowPos();
        if (current_pos.x != saved_x || current_pos.y != saved_y) {
            settings::g_mainTabSettings.display_commander_ui_window_x.SetValue(current_pos.x);
            settings::g_mainTabSettings.display_commander_ui_window_y.SetValue(current_pos.y);
            last_saved_x = current_pos.x;
            last_saved_y = current_pos.y;
        }

        // Render tabs
        ui::new_ui::NewUISystem::GetInstance().Draw(runtime);
        ImGui::End();
    } else {
        if (last_cursor_state != CursorState::Hidden) {
            last_cursor_state = CursorState::Hidden;
        // Hide cursor when overlay is closed (same approach as ReShade)
            ImGuiIO& io = ImGui::GetIO();
            io.MouseDrawCursor = false;
        }
    }

    // Check the setting from main tab first
    if (!settings::g_mainTabSettings.show_test_overlay.GetValue()) {
        return;
    }

    // Check which overlay components are enabled
    bool show_fps_counter = settings::g_mainTabSettings.show_fps_counter.GetValue();
    bool show_refresh_rate = settings::g_mainTabSettings.show_refresh_rate.GetValue();
    bool show_vrr_status = settings::g_mainTabSettings.show_vrr_status.GetValue();
    bool show_volume = settings::g_mainTabSettings.show_volume.GetValue();
    bool show_gpu_measurement = (settings::g_mainTabSettings.gpu_measurement_enabled.GetValue() != 0);
    bool show_frame_time_graph = settings::g_mainTabSettings.show_frame_time_graph.GetValue();
    bool show_cpu_usage = settings::g_mainTabSettings.show_cpu_usage.GetValue();
    bool show_enabledfeatures = display_commanderhooks::IsTimeslowdownEnabled() || ::g_auto_click_enabled.load();

    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
    // Set transparent background for the window (configurable opacity)
    float bg_alpha = settings::g_mainTabSettings.overlay_background_alpha.GetValue();
    ImGui::SetNextWindowBgAlpha(bg_alpha);
    ImGui::SetNextWindowSize(ImVec2(450, 65), ImGuiCond_FirstUseEver);
    // Auto size the window to the content
    ImGui::Begin("Test Window", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize
                     | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize);

    // Get current FPS from performance ring buffer

    if (settings::g_mainTabSettings.show_clock.GetValue()) {
        // Display current time
        SYSTEMTIME st;
        GetLocalTime(&st);
        ImGui::Text("%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    }

    // Show playtime (time from game start)
    if (settings::g_mainTabSettings.show_playtime.GetValue()) {
        LONGLONG game_start_time_ns = g_game_start_time_ns.load();
        if (game_start_time_ns > 0) {
            LONGLONG now_ns = utils::get_now_ns();
            LONGLONG playtime_ns = now_ns - game_start_time_ns;
            double playtime_seconds = static_cast<double>(playtime_ns) / static_cast<double>(utils::SEC_TO_NS);

            // Format as HH:MM:SS.mmm
            int hours = static_cast<int>(playtime_seconds / 3600.0);
            int minutes = static_cast<int>((playtime_seconds - (hours * 3600.0)) / 60.0);
            int seconds = static_cast<int>(playtime_seconds - (hours * 3600.0) - (minutes * 60.0));
            int milliseconds = static_cast<int>((playtime_seconds - static_cast<int>(playtime_seconds)) * 1000.0);

            if (settings::g_mainTabSettings.show_labels.GetValue()) {
                ImGui::Text("%02d:%02d:%02d", hours, minutes, seconds);
            } else {
                ImGui::Text("%02d:%02d:%02d", hours, minutes, seconds);
            }

            if (ImGui::IsItemHovered() && show_tooltips) {
                ImGui::SetTooltip("Playtime: Time elapsed since game start");
            }
        }
    }

    if (show_fps_counter) {
        const uint32_t head = ::g_perf_ring_head.load(std::memory_order_acquire);
        double total_time = 0.0;

        // Iterate through samples from the last second
        uint32_t sample_count = 0;

        // Iterate backwards through the ring buffer up to 1 second
        for (uint32_t i = 0; i < ::kPerfRingCapacity; ++i) {
            uint32_t idx = (head - 1 - i) & (::kPerfRingCapacity - 1);
            const ::PerfSample& sample = ::g_perf_ring[idx];

            // not enough data yet
            if (sample.dt == 0.0f || total_time >= 1.0) break;

            sample_count++;
            total_time += sample.dt;
        }

        // Calculate average
        if (sample_count > 0 && total_time >= 1.0) {
            auto average_fps = sample_count / total_time;

            // Check if native FPS should be shown
            bool show_native_fps = settings::g_mainTabSettings.show_native_fps.GetValue();
            if (show_native_fps) {
                // Check if native Reflex was updated within the last 5 seconds
                uint64_t last_sleep_timestamp = ::g_nvapi_last_sleep_timestamp_ns.load();
                uint64_t current_time = utils::get_now_ns();
                bool is_recent = (last_sleep_timestamp > 0) &&
                                (current_time - last_sleep_timestamp) < (5 * utils::SEC_TO_NS);

                // Calculate native FPS from native Reflex sleep interval
                LONGLONG native_sleep_ns_smooth = ::g_sleep_reflex_native_ns_smooth.load();
                double native_fps = 0.0;

                // Only calculate if we have valid native sleep data (> 0 and reasonable) and it's recent
                if (is_recent && native_sleep_ns_smooth > 0 && native_sleep_ns_smooth < 1 * utils::SEC_TO_NS) {
                    native_fps = static_cast<double>(utils::SEC_TO_NS) / static_cast<double>(native_sleep_ns_smooth);
                }

                // Display dual format: native FPS / regular FPS
                if (native_fps > 0.0) {
                    if (settings::g_mainTabSettings.show_labels.GetValue()) {
                        ImGui::Text("%.1f / %.1f fps", native_fps, average_fps);
                    } else {
                        ImGui::Text("%.1f / %.1f", native_fps, average_fps);
                    }
                } else {
                    // No valid native FPS data, show regular FPS only
                    if (settings::g_mainTabSettings.show_labels.GetValue()) {
                        ImGui::Text("%.1f fps", average_fps);
                    } else {
                        ImGui::Text("%.1f", average_fps);
                    }
                }
            } else {
                // Regular FPS display
                if (settings::g_mainTabSettings.show_labels.GetValue()) {
                    ImGui::Text("%.1f fps", average_fps);
                } else {
                    ImGui::Text("%.1f", average_fps);
                }
            }
        }
    }

    if (show_refresh_rate) {
        static double cached_refresh_rate = 0.0;
        static LONGLONG last_update_ns = 0;
        const LONGLONG update_interval_ns = 100 * utils::NS_TO_MS;  // 200ms in nanoseconds

        LONGLONG now_ns = utils::get_now_ns();

        // Update cached value every 50ms
        if (now_ns - last_update_ns >= update_interval_ns) {
            auto stats = dxgi::fps_limiter::GetRefreshRateStats();
            if (stats.is_valid && stats.sample_count > 0) {
                cached_refresh_rate = stats.smoothed_rate;
                last_update_ns = now_ns;
            }
        }

        // Display cached value
        if (cached_refresh_rate > 0.0) {
            if (settings::g_mainTabSettings.show_labels.GetValue()) {
                ImGui::Text("%.1fHz", cached_refresh_rate);
            } else {
                ImGui::Text("%.1f", cached_refresh_rate);
            }
        }
    }

    bool show_vrr_debug_mode = settings::g_mainTabSettings.vrr_debug_mode.GetValue();

    if (show_vrr_status || show_vrr_debug_mode) {
        static bool cached_vrr_active = false;
        static LONGLONG last_update_ns = 0;
        static LONGLONG last_valid_sample_ns = 0;
        static dxgi::fps_limiter::RefreshRateStats cached_stats{};
        const LONGLONG update_interval_ns = 100 * utils::NS_TO_MS;  // 100ms in nanoseconds
        const LONGLONG sample_timeout_ns = 1000 * utils::NS_TO_MS;  // 1 second in nanoseconds

        LONGLONG now_ns = utils::get_now_ns();

        // Update cached value every 100ms
        if (now_ns - last_update_ns >= update_interval_ns) {
            auto stats = dxgi::fps_limiter::GetRefreshRateStats();
            if (stats.is_valid && stats.sample_count > 0) {
                // VRR is active if refresh rate varies (max > min + 1.0 Hz threshold)
                cached_vrr_active = (stats.max_rate > stats.min_rate + 2.0);
                cached_stats = stats;
                last_update_ns = now_ns;
                last_valid_sample_ns = now_ns;
            }
        }

        // Check if we got a sample within the last 1 second
        bool has_recent_sample = (now_ns - last_valid_sample_ns) < sample_timeout_ns;

        // Display VRR status (only if show_vrr_status is enabled)
        if (show_vrr_status) {
            if (cached_stats.all_last_20_within_1s && cached_stats.samples_below_threshold_last_10s >= 2) {
                ImGui::TextColored(ui::colors::TEXT_SUCCESS, "VRR: On");
            } else {
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "VRR: Off");
            }
        }

        // Display debugging parameters below VRR status (only if vrr_debug_mode is enabled)
        if (show_vrr_debug_mode && has_recent_sample && cached_stats.is_valid) {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "  Fixed: %.2f Hz", cached_stats.fixed_refresh_hz);
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "  Threshold: %.2f Hz", cached_stats.threshold_hz);
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "  Total samples (10s): %u", cached_stats.total_samples_last_10s);
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "  Below threshold: %u", cached_stats.samples_below_threshold_last_10s);
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "  Last 20 within 1s: %s", cached_stats.all_last_20_within_1s ? "Yes" : "No");
        }
    }

    if (show_volume) {
        // Get current game volume
        float current_volume = 0.0f;
        if (!GetVolumeForCurrentProcess(&current_volume)) {
            // If we can't get current volume, use stored value
            current_volume = s_audio_volume_percent.load();
        }

        // Get current system volume
        float system_volume = 0.0f;
        if (!GetSystemVolume(&system_volume)) {
            system_volume = s_system_volume_percent.load();
        } else {
            s_system_volume_percent.store(system_volume);
        }

        // Check if audio is muted
        bool is_muted = g_muted_applied.load();

        // Display game volume and system volume
        if (settings::g_mainTabSettings.show_labels.GetValue()) {
            if (is_muted) {
                ImGui::Text("%.0f%% vol / %.0f%% sys muted", current_volume, system_volume);
            } else {
                ImGui::Text("%.0f%% vol / %.0f%% sys", current_volume, system_volume);
            }
        } else {
            if (is_muted) {
                ImGui::Text("%.0f%% / %.0f%% muted", current_volume, system_volume);
            } else {
                ImGui::Text("%.0f%% / %.0f%%", current_volume, system_volume);
            }
        }
        if (ImGui::IsItemHovered() && show_tooltips) {
            if (is_muted) {
                ImGui::SetTooltip("Game Volume: %.0f%% | System Volume: %.0f%% (Muted)", current_volume, system_volume);
            } else {
                ImGui::SetTooltip("Game Volume: %.0f%% | System Volume: %.0f%%", current_volume, system_volume);
            }
        }
    }

    if (show_gpu_measurement) {
        // Display sim-to-display latency
        LONGLONG latency_ns = ::g_sim_to_display_latency_ns.load();
        if (latency_ns > 0) {
            double latency_ms = (1.0 * latency_ns / utils::NS_TO_MS);
            if (settings::g_mainTabSettings.show_labels.GetValue()) {
                ImGui::Text("%.1f ms lat", latency_ms);
            } else {
                ImGui::Text("%.1f", latency_ms);
            }
        }
    }

    if (show_cpu_usage) {
        // Calculate CPU usage: (sim_duration / frame_time) * 100%
        // Get most recent frame time from performance ring buffer
     //   const uint32_t head = ::g_perf_ring_head.load(std::memory_order_acquire);
     // //  if (head > 0) {
       //     const uint32_t last_idx = (head - 1) & (::kPerfRingCapacity - 1);
        //    const ::PerfSample& last_sample = ::g_perf_ring[last_idx];

       //     if (last_sample.dt > 0.0f) {
                // Get simulation duration in nanoseconds
           //     LONGLONG sim_duration_ns = ::g_simulation_duration_ns.load();
            //    LONGLONG reshade_overhead_duration_ns = ::g_reshade_overhead_duration_ns.load();

        // missing time spend in onpresent
        // missing native reflex time
        LONGLONG cpu_time_ns = ::g_frame_time_ns.load() - fps_sleep_after_on_present_ns.load() - fps_sleep_before_on_present_ns.load();

        LONGLONG frame_time_ns = ::g_frame_time_ns.load();

        if (cpu_time_ns > 0 && frame_time_ns > 0) {
            // Calculate CPU usage percentage: (sim_duration / frame_time) * 100
            double cpu_usage_percent =
                (static_cast<double>(cpu_time_ns) / static_cast<double>(frame_time_ns)) * 100.0;

            // Clamp to 0-100%
            if (cpu_usage_percent < 0.0) cpu_usage_percent = 0.0;
            if (cpu_usage_percent > 100.0) cpu_usage_percent = 100.0;

            // Apply exponential smoothing with alpha 0.1
            static double smoothed_cpu_usage = cpu_usage_percent;
            const double alpha = 0.05;
            smoothed_cpu_usage = (1.0 - alpha) * smoothed_cpu_usage + alpha * cpu_usage_percent;

            // Track last 32 CPU usage values for max calculation
            static constexpr size_t kCpuUsageHistorySize = 64;
            static double cpu_usage_history[kCpuUsageHistorySize] = {};
            static size_t cpu_usage_history_index = 0;
            static size_t cpu_usage_history_count = 0;

            // Add current value to history
            cpu_usage_history[cpu_usage_history_index] = cpu_usage_percent;
            cpu_usage_history_index = (cpu_usage_history_index + 1) % kCpuUsageHistorySize;
            if (cpu_usage_history_count < kCpuUsageHistorySize) {
                cpu_usage_history_count++;
            }

            // Find maximum from last 32 frames
            double max_cpu_usage = cpu_usage_percent;
            for (size_t i = 0; i < cpu_usage_history_count; ++i) {
                max_cpu_usage = (std::max)(max_cpu_usage, cpu_usage_history[i]);
            }

            if (settings::g_mainTabSettings.show_labels.GetValue()) {
                ImGui::Text("%.1f%% cpu (max: %.1f%%)", smoothed_cpu_usage, max_cpu_usage);
            } else {
                ImGui::Text("%.1f%% (max: %.1f%%)", smoothed_cpu_usage, max_cpu_usage);
            }
        }
      //      }
    //    }
    }

    // Show stopwatch
    if (settings::g_mainTabSettings.show_stopwatch.GetValue()) {
        bool is_running = g_stopwatch_running.load();

        // Update elapsed time if running
        if (is_running) {
            LONGLONG start_time_ns = g_stopwatch_start_time_ns.load();
            LONGLONG now_ns = utils::get_now_ns();
            LONGLONG elapsed_ns = now_ns - start_time_ns;
            g_stopwatch_elapsed_time_ns.store(elapsed_ns);
        }

        LONGLONG elapsed_ns = g_stopwatch_elapsed_time_ns.load();
        double elapsed_seconds = static_cast<double>(elapsed_ns) / static_cast<double>(utils::SEC_TO_NS);

        // Format as HH:MM:SS.mmm
        int hours = static_cast<int>(elapsed_seconds / 3600.0);
        int minutes = static_cast<int>((elapsed_seconds - (hours * 3600.0)) / 60.0);
        int seconds = static_cast<int>(elapsed_seconds - (hours * 3600.0) - (minutes * 60.0));
        int milliseconds = static_cast<int>((elapsed_seconds - static_cast<int>(elapsed_seconds)) * 1000.0);

        if (is_running) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%02d:%02d:%02d.%03d", hours, minutes, seconds,
                               milliseconds);
        } else {
            ImGui::Text("%02d:%02d:%02d.%03d", hours, minutes, seconds, milliseconds);
        }

        if (ImGui::IsItemHovered() && show_tooltips) {
            if (is_running) {
                ImGui::SetTooltip("Stopwatch: Running\nPress Ctrl+S to pause");
            } else {
                ImGui::SetTooltip("Stopwatch: Paused\nPress Ctrl+S to reset and start");
            }
        }
    }

    // Show action notifications (volume, mute, etc.) for 10 seconds
    ActionNotification notification = g_action_notification.load();
    if (notification.type != ActionNotificationType::None) {
        LONGLONG now_ns = utils::get_now_ns();
        LONGLONG elapsed_ns = now_ns - notification.timestamp_ns;
        const LONGLONG display_duration_ns = 10 * utils::SEC_TO_NS; // 10 seconds

        if (elapsed_ns < display_duration_ns) {
            // Display based on notification type
            switch (notification.type) {
            case ActionNotificationType::Volume: {
                float volume_value = notification.float_value;
                bool is_muted = g_muted_applied.load();
                if (settings::g_mainTabSettings.show_labels.GetValue()) {
                    if (is_muted) {
                        ImGui::Text("%.0f%% vol muted", volume_value);
                    } else {
                        ImGui::Text("%.0f%% vol", volume_value);
                    }
                } else {
                    if (is_muted) {
                        ImGui::Text("%.0f%% muted", volume_value);
                    } else {
                        ImGui::Text("%.0f%%", volume_value);
                    }
                }
                if (ImGui::IsItemHovered() && show_tooltips) {
                    if (is_muted) {
                        ImGui::SetTooltip("Audio Volume: %.0f%% (Muted)", volume_value);
                    } else {
                        ImGui::SetTooltip("Audio Volume: %.0f%%", volume_value);
                    }
                }
                break;
            }
            case ActionNotificationType::Mute: {
                bool mute_state = notification.bool_value;
                if (settings::g_mainTabSettings.show_labels.GetValue()) {
                    ImGui::Text("%s", mute_state ? "Muted" : "Unmuted");
                } else {
                    ImGui::Text("%s", mute_state ? "Muted" : "Unmuted");
                }
                if (ImGui::IsItemHovered() && show_tooltips) {
                    ImGui::SetTooltip("Audio: %s", mute_state ? "Muted" : "Unmuted");
                }
                break;
            }
            case ActionNotificationType::GenericAction: {
                if (settings::g_mainTabSettings.show_labels.GetValue()) {
                    ImGui::Text("%s", notification.action_name);
                } else {
                    ImGui::Text("%s", notification.action_name);
                }
                if (ImGui::IsItemHovered() && show_tooltips) {
                    ImGui::SetTooltip("Gamepad Action: %s", notification.action_name);
                }
                break;
            }
            default:
                break;
            }
        } else {
            // Clear the notification after display duration expires
            ActionNotification clear_notification;
            clear_notification.type = ActionNotificationType::None;
            clear_notification.timestamp_ns = 0;
            clear_notification.float_value = 0.0f;
            clear_notification.bool_value = false;
            clear_notification.action_name[0] = '\0';
            g_action_notification.store(clear_notification);
        }
    }

    // Show enabled features indicator (time slowdown, auto-click, etc.)
    if (show_enabledfeatures) {
        char feature_text[512];
        char tooltip_text[512];
        feature_text[0] = '\0';
        tooltip_text[0] = '\0';

        bool first_feature = true;

        // Time Slowdown
        if (display_commanderhooks::IsTimeslowdownEnabled()) {
            float multiplier = display_commanderhooks::GetTimeslowdownMultiplier();
            if (first_feature) {
                if (settings::g_mainTabSettings.show_labels.GetValue()) {
                    snprintf(feature_text, sizeof(feature_text), "%.2fx TS", multiplier);
                } else {
                    snprintf(feature_text, sizeof(feature_text), "%.2fx", multiplier);
                }
                snprintf(tooltip_text, sizeof(tooltip_text), "Time Slowdown: %.2fx multiplier", multiplier);
                first_feature = false;
            } else {
                size_t len = strlen(feature_text);
                if (settings::g_mainTabSettings.show_labels.GetValue()) {
                    snprintf(feature_text + len, sizeof(feature_text) - len, ", %.2fx TS", multiplier);
                } else {
                    snprintf(feature_text + len, sizeof(feature_text) - len, ", %.2fx", multiplier);
                }
                len = strlen(tooltip_text);
                snprintf(tooltip_text + len, sizeof(tooltip_text) - len, " | Time Slowdown: %.2fx multiplier",
                         multiplier);
            }
        }

        // Auto-Click
        if (::g_auto_click_enabled.load()) {
            if (first_feature) {
                snprintf(feature_text, sizeof(feature_text), "AC");
                snprintf(tooltip_text, sizeof(tooltip_text), "Auto-Click: Enabled");
                first_feature = false;
            } else {
                size_t len = strlen(feature_text);
                snprintf(feature_text + len, sizeof(feature_text) - len, ", AC");
                len = strlen(tooltip_text);
                snprintf(tooltip_text + len, sizeof(tooltip_text) - len, " | Auto-Click: Enabled");
            }
        }

        // Add more features here as needed
        // Example:
        // if (some_other_feature_enabled) {
        //     if (first_feature) {
        //         snprintf(feature_text, sizeof(feature_text), "FEATURE");
        //         snprintf(tooltip_text, sizeof(tooltip_text), "Feature: Description");
        //         first_feature = false;
        //     } else {
        //         size_t len = strlen(feature_text);
        //         snprintf(feature_text + len, sizeof(feature_text) - len, ", FEATURE");
        //         len = strlen(tooltip_text);
        //         snprintf(tooltip_text + len, sizeof(tooltip_text) - len, " | Feature: Description");
        //     }
        // }

        if (feature_text[0] != '\0') {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "%s", feature_text);
            if (ImGui::IsItemHovered() && show_tooltips) {
                ImGui::SetTooltip("%s", tooltip_text);
            }
        }
    }

    if (show_frame_time_graph) {
        ui::new_ui::DrawFrameTimeGraphOverlay(show_tooltips);
    }

    if (settings::g_mainTabSettings.show_refresh_rate_frame_times.GetValue()) {
        ui::new_ui::DrawRefreshRateFrameTimesGraph(show_tooltips);
    }

    ImGui::End();

    // Test widget that appears in the main ReShade overlay
}
}  // namespace

// Override ReShade settings to set tutorial as viewed and disable auto updates
void OverrideReShadeSettings() {
    LogInfo("Overriding ReShade settings - Setting tutorial as viewed and disabling auto updates");

    //
    // [Window][Display Commander],Pos=1017,,20,Size=1344,,1255,Collapsed=0,DockId=0x00000001,,7

    {
        // Read Window config as string (ReShade stores docking data here)
        std::string window_config;
        size_t value_size = 0;

        // First call to get the required buffer size
        if (reshade::get_config_value(nullptr, "OVERLAY", "Window", nullptr, &value_size)) {
            // Allocate buffer and read the value
            window_config.resize(value_size);
            if (reshade::get_config_value(nullptr, "OVERLAY", "Window", window_config.data(), &value_size)) {
                // Remove null terminator if present (ReShade includes it in size)
                if (!window_config.empty() && window_config.back() == '\0') {
                    window_config.pop_back();
                }
            } else {
                window_config.clear();
            }
        }

        bool changed_window_config = false;

        // Add Display Commander window config if not present
        if (window_config.find("[Window][Display Commander]") == std::string::npos) {
            if (!window_config.empty()) {
                window_config.push_back('\0');
            }
            std::string to_add =
                "[Window][Display Commander],Pos=1017,,20,Size=1344,,1255,Collapsed=0,DockId=0x00000001,,999999,";
            for (size_t i = 0; i < to_add.size(); i++) {
                if (to_add[i] == ',') {
                    window_config.push_back('\0');
                } else {
                    window_config.push_back(to_add[i]);
                }
            }
            changed_window_config = true;
        }

        // Add RenoDX window config if not present
        if (window_config.find("[Window][RenoDX]") == std::string::npos) {
            if (!window_config.empty()) {
                window_config.push_back('\0');
            }
            std::string to_add =
                "[Window][RenoDX],Pos=1017,,20,Size=1344,,1255,Collapsed=0,DockId=0x00000001,,9999999,";
            for (size_t i = 0; i < to_add.size(); i++) {
                if (to_add[i] == ',') {
                    window_config.push_back('\0');
                } else {
                    window_config.push_back(to_add[i]);
                }
            }

            changed_window_config = true;
        }

        // Write back if changed
        if (changed_window_config) {
            reshade::set_config_value(nullptr, "OVERLAY", "Window", window_config.c_str(), window_config.size());
            LogInfo("Updated ReShade Window config with Display Commander and RenoDX docking settings");
        }
    }

    // Set tutorial progress to 4 (fully viewed)
    reshade::set_config_value(nullptr, "OVERLAY", "TutorialProgress", 4);
    // LogInfo("ReShade settings override - TutorialProgress set to 4 (viewed)");

    // Disable auto updates
    reshade::set_config_value(nullptr, "GENERAL", "CheckForUpdates", 0);
    LogInfo("ReShade settings override - CheckForUpdates set to 0 (disabled)");

    // Read LoadFromDllMain value from DisplayCommander.ini
    int32_t load_from_dll_main_from_display_commander = 1;  // Default to 1 if not found
    bool found_in_display_commander = display_commander::config::get_config_value(
        "DisplayCommander", "LoadFromDllMain", load_from_dll_main_from_display_commander);

    if (found_in_display_commander) {
        LogInfo("ReShade settings override - LoadFromDllMain value from DisplayCommander.ini: %d",
                load_from_dll_main_from_display_commander);
    } else {
        LogInfo(
            "ReShade settings override - LoadFromDllMain not found in DisplayCommander.ini, using default value: %d",
            load_from_dll_main_from_display_commander);
    }

    // Get current value from ReShade.ini for logging
    int32_t current_reshade_value = 0;
    reshade::get_config_value(nullptr, "ADDON", "LoadFromDllMain", current_reshade_value);
    LogInfo("ReShade settings override - LoadFromDllMain current ReShade value: %d", current_reshade_value);

    // Set LoadFromDllMain to the value from DisplayCommander.ini
    reshade::set_config_value(nullptr, "ADDON", "LoadFromDllMain", load_from_dll_main_from_display_commander);
    LogInfo("ReShade settings override - LoadFromDllMain set to %d (from DisplayCommander.ini)",
            load_from_dll_main_from_display_commander);

    LogInfo("ReShade settings override completed successfully");
}

// Function to detect multiple ReShade versions by scanning all modules
void DetectMultipleReShadeVersions() {
    LogInfo("=== ReShade Module Detection ===");

    // Reset debug info
    g_reshade_debug_info = ReShadeDetectionDebugInfo();

    HMODULE modules[1024];
    DWORD num_modules = 0;

    // Use K32EnumProcessModules for safe enumeration from DllMain
    if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &num_modules) == 0) {
        DWORD error = GetLastError();
        LogWarn("Failed to enumerate process modules: %lu", error);
        g_reshade_debug_info.error_message = "Failed to enumerate process modules: " + std::to_string(error);
        g_reshade_debug_info.detection_completed = true;
        return;
    }

    if (num_modules > sizeof(modules)) {
        num_modules = static_cast<DWORD>(sizeof(modules));
    }

    int reshade_module_count = 0;
    std::vector<HMODULE> reshade_modules;

    LogInfo("Scanning %lu modules for ReShade...", num_modules / sizeof(HMODULE));

    for (DWORD i = 0; i < num_modules / sizeof(HMODULE); ++i) {
        HMODULE module = modules[i];
        if (module == nullptr) continue;

        // Check if this module has ReShadeRegisterAddon
        FARPROC register_func = GetProcAddress(module, "ReShadeRegisterAddon");
        FARPROC unregister_func = GetProcAddress(module, "ReShadeUnregisterAddon");

        if (register_func != nullptr && unregister_func != nullptr) {
            reshade_module_count++;
            reshade_modules.push_back(module);

            // Create module info for debug storage
            ReShadeModuleInfo module_info;
            module_info.handle = module;

            // Get module file path for detailed logging
            wchar_t module_path[MAX_PATH];
            DWORD path_length = GetModuleFileNameW(module, module_path, MAX_PATH);

            if (path_length > 0) {
                // Convert wide string to narrow string for logging
                char narrow_path[MAX_PATH];
                WideCharToMultiByte(CP_UTF8, 0, module_path, -1, narrow_path, MAX_PATH, nullptr, nullptr);
                module_info.path = narrow_path;

                LogInfo("Found ReShade module #%d: 0x%p - %s", reshade_module_count, module, narrow_path);

                // Try to get version information
                DWORD version_dummy = 0;
                DWORD version_size = GetFileVersionInfoSizeW(module_path, &version_dummy);
                if (version_size > 0) {
                    std::vector<uint8_t> version_data(version_size);
                    if (GetFileVersionInfoW(module_path, version_dummy, version_size, version_data.data()) != 0) {
                        VS_FIXEDFILEINFO* version_info = nullptr;
                        UINT version_info_size = 0;
                        if (VerQueryValueW(version_data.data(), L"\\", reinterpret_cast<LPVOID*>(&version_info),
                                           &version_info_size)
                                != 0
                            && version_info != nullptr) {
                            char version_str[64];
                            snprintf(version_str, sizeof(version_str), "%hu.%hu.%hu.%hu",
                                     HIWORD(version_info->dwFileVersionMS), LOWORD(version_info->dwFileVersionMS),
                                     HIWORD(version_info->dwFileVersionLS), LOWORD(version_info->dwFileVersionLS));
                            module_info.version = version_str;
                            module_info.is_version_662_or_above = IsVersion662OrAbove(version_str);
                            LogInfo("  Version: %s", version_str);
                            LogInfo("  Version 6.6.2+: %s", module_info.is_version_662_or_above ? "Yes" : "No");
                        }
                    }
                }

                // Check if this module also has ReShadeGetImGuiFunctionTable
                FARPROC imgui_func = GetProcAddress(module, "ReShadeGetImGuiFunctionTable");
                module_info.has_imgui_support = (imgui_func != nullptr);
                LogInfo("  ImGui Support: %s", imgui_func != nullptr ? "Yes" : "No");

                // If version extraction failed, set compatibility to false
                if (module_info.version.empty()) {
                    module_info.is_version_662_or_above = false;
                    LogInfo("  Version 6.6.2+: No (version unknown)");
                }

            } else {
                module_info.path = "(path unavailable)";
                LogInfo("Found ReShade module #%d: 0x%p - (path unavailable)", reshade_module_count, module);
            }

            // Store module info for debug display
            g_reshade_debug_info.modules.push_back(module_info);
        }
    }

    LogInfo("=== ReShade Detection Complete ===");
    LogInfo("Total ReShade modules found: %d", reshade_module_count);

    // Check if any module meets version requirements
    bool has_compatible_version = false;
    for (const auto& module : g_reshade_debug_info.modules) {
        if (module.is_version_662_or_above) {
            has_compatible_version = true;
            LogInfo("Found compatible ReShade version: %s", module.version.c_str());
            break;
        }
    }

    if (!has_compatible_version && !g_reshade_debug_info.modules.empty()) {
        LogWarn("No ReShade modules found with version 6.6.2 or above");
    }

    // Update debug info
    g_reshade_debug_info.total_modules_found = reshade_module_count;
    g_reshade_debug_info.detection_completed = true;

    if (reshade_module_count > 1) {
        LogWarn("WARNING: Multiple ReShade versions detected! This may cause conflicts.");
        LogWarn("Found %d ReShade modules - only the first one will be used for registration.", reshade_module_count);

        // Log warning about potential conflicts
        for (size_t i = 0; i < reshade_modules.size(); ++i) {
            LogWarn("  ReShade module %zu: 0x%p", i + 1, reshade_modules[i]);
        }
    } else if (reshade_module_count == 1) {
        LogInfo("Single ReShade module detected - proceeding with registration.");
    } else {
        LogError("No ReShade modules found! Registration will likely fail.");
        g_reshade_debug_info.error_message = "No ReShade modules found! Registration will likely fail.";
    }
}

// Version compatibility check function
bool CheckReShadeVersionCompatibility() {
    static bool first_time = true;
    if (!first_time) {
        return false;
    }
    first_time = false;
    // This function will be called after registration fails
    // We'll display a helpful error message to the user
    LogError("ReShade addon registration failed - API version not supported");

    // Build debug information string
    std::string debug_info = "ERROR DETAILS:\n";
    debug_info += "• Required API Version: 17 (ReShade 6.6.2+)\n";

    // Check if we have version information
    bool has_version_info = false;
    bool has_compatible_version = false;
    std::string detected_versions;

    if (g_reshade_debug_info.detection_completed && !g_reshade_debug_info.modules.empty()) {
        for (const auto& module : g_reshade_debug_info.modules) {
            if (!module.version.empty()) {
                has_version_info = true;
                if (!detected_versions.empty()) {
                    detected_versions += ", ";
                }
                detected_versions += module.version;

                if (module.is_version_662_or_above) {
                    has_compatible_version = true;
                }
            }
        }
    }

    if (has_version_info) {
        debug_info += "• Detected ReShade Versions: " + detected_versions + "\n";
        debug_info += "• Version 6.6.2+ Compatible: " + std::string(has_compatible_version ? "Yes" : "No") + "\n";
    } else {
        debug_info += "• Your ReShade Version: Unknown (version detection failed)\n";
    }
    debug_info += "• Status: Incompatible\n\n";

    // Add module detection debug information
    if (g_reshade_debug_info.detection_completed) {
        debug_info += "MODULE DETECTION RESULTS:\n";
        debug_info +=
            "• Total ReShade modules found: " + std::to_string(g_reshade_debug_info.total_modules_found) + "\n";

        if (!g_reshade_debug_info.error_message.empty()) {
            debug_info += "• Error: " + g_reshade_debug_info.error_message + "\n";
        }

        if (!g_reshade_debug_info.modules.empty()) {
            debug_info += "• Detected modules:\n";
            for (size_t i = 0; i < g_reshade_debug_info.modules.size(); ++i) {
                const auto& module = g_reshade_debug_info.modules[i];
                debug_info += "  " + std::to_string(i + 1) + ". " + module.path + "\n";
                if (!module.version.empty()) {
                    debug_info += "     Version: " + module.version + "\n";
                    debug_info +=
                        "     Version 6.6.2+: " + std::string(module.is_version_662_or_above ? "Yes" : "No") + "\n";
                } else {
                    debug_info += "     Version: Unknown\n";
                    debug_info += "     Version 6.6.2+: No (version unknown)\n";
                }
                debug_info += "     ImGui Support: " + std::string(module.has_imgui_support ? "Yes" : "No") + "\n";
                debug_info += "     Handle: 0x" + std::to_string(reinterpret_cast<uintptr_t>(module.handle)) + "\n";
            }
        } else {
            debug_info += "• No ReShade modules detected\n";
        }
        debug_info += "\n";
    } else {
        debug_info += "MODULE DETECTION:\n";
        debug_info += "• Detection not completed or failed\n\n";
    }

    debug_info += "SOLUTION:\n";
    debug_info += "1. Download the latest ReShade from: https://reshade.me/\n";
    debug_info += "2. Install ReShade 6.6.2 or newer\n";
    debug_info += "3. Restart your game to load the updated ReShade\n\n";
    debug_info += "This addon uses advanced features that require the newer ReShade API.";

    // Display detailed error message to user
    MessageBoxA(nullptr, debug_info.c_str(), "ReShade Version Incompatible - Update Required",
                MB_OK | MB_ICONERROR | MB_TOPMOST);

    return false;
}

// Safemode function - handles safemode logic
void HandleSafemode() {
    // Developer settings already loaded at startup
    bool safemode_enabled = settings::g_developerTabSettings.safemode.GetValue();

    // Wait for DLLs to load before Display Commander
    std::string dlls_to_load = settings::g_developerTabSettings.dlls_to_load_before.GetValue();
    if (!dlls_to_load.empty()) {
        LogInfo("Waiting for DLLs to load before Display Commander: %s", dlls_to_load.c_str());

        // Replace semicolons with commas to support both separators
        std::replace(dlls_to_load.begin(), dlls_to_load.end(), ';', ',');

        // Parse comma-separated DLL list
        std::istringstream iss(dlls_to_load);
        std::string dll_name;
        const int max_wait_time_ms = 30000; // Maximum 30 seconds per DLL
        const int check_interval_ms = 100;   // Check every 100ms

        while (std::getline(iss, dll_name, ',')) {
            // Trim whitespace
            dll_name.erase(0, dll_name.find_first_not_of(" \t\n\r"));
            dll_name.erase(dll_name.find_last_not_of(" \t\n\r") + 1);

            if (dll_name.empty()) {
                continue;
            }

            // Convert to wide string for GetModuleHandleW
            std::wstring w_dll_name(dll_name.begin(), dll_name.end());

            LogInfo("Waiting for DLL to load: %s", dll_name.c_str());

            // Wait for DLL to be loaded (with timeout per DLL)
            int waited_ms = 0;
            bool dll_loaded = false;
            while (waited_ms < max_wait_time_ms) {
                HMODULE hMod = GetModuleHandleW(w_dll_name.c_str());
                if (hMod != nullptr) {
                    LogInfo("DLL loaded successfully: %s (0x%p)", dll_name.c_str(), hMod);
                    dll_loaded = true;
                    break;
                }

                Sleep(check_interval_ms);
                waited_ms += check_interval_ms;
            }

            if (!dll_loaded) {
                LogWarn("Timeout waiting for DLL to load: %s (waited %d ms)", dll_name.c_str(), waited_ms);
            }
        }

        LogInfo("Finished waiting for DLLs to load");
    }

    // Apply DLL loading delay if configured
    int delay_ms = settings::g_developerTabSettings.dll_loading_delay_ms.GetValue();
    if (delay_ms > 0) {
        LogInfo("DLL loading delay: waiting %d ms before installing LoadLibrary hooks", delay_ms);
        Sleep(delay_ms);
        LogInfo("DLL loading delay complete, proceeding with initialization");
    }
    // rewrite settings::g_developerTabSettings.dll_loading_delay_ms
    settings::g_developerTabSettings.dll_loading_delay_ms.SetValue(settings::g_developerTabSettings.dll_loading_delay_ms.GetValue());

    if (safemode_enabled) {
        LogInfo(
            "Safemode enabled - disabling auto-apply settings, continue rendering, FPS limiter, XInput hooks, MinHook "
            "initialization, and "
            "Streamline loading");

        // Set safemode to 0 (force set to 0)
        settings::g_developerTabSettings.safemode.SetValue(false);
        settings::g_developerTabSettings.prevent_fullscreen.SetValue(false);
        settings::g_developerTabSettings.continue_rendering.SetValue(false);
        settings::g_developerTabSettings.suppress_minhook.SetValue(true);

        settings::g_mainTabSettings.fps_limiter_mode.SetValue((int)FpsLimiterMode::kDisabled);

        // Disable all auto-apply settings
        ui::monitor_settings::g_setting_auto_apply_resolution.SetValue(false);
        ui::monitor_settings::g_setting_auto_apply_refresh.SetValue(false);
        ui::monitor_settings::g_setting_apply_display_settings_at_start.SetValue(false);

        // Disable XInput hooks
        settings::g_hook_suppression_settings.suppress_xinput_hooks.SetValue(true);

        // Enable MinHook suppression
        settings::g_developerTabSettings.suppress_minhook.SetValue(true);

#if 0
        // Disable Streamline loading
        settings::g_developerTabSettings.load_streamline.SetValue(false);

        // Disable _nvngx loading
        settings::g_developerTabSettings.load_nvngx.SetValue(false);

        // Disable nvapi64 loading
        settings::g_developerTabSettings.load_nvapi64.SetValue(false);
#endif

        // Save the changes
        settings::g_developerTabSettings.SaveAll();

        LogInfo(
            "Safemode applied - auto-apply settings disabled, continue rendering disabled, FPS limiter set to "
            "disabled, XInput hooks disabled, MinHook initialization suppressed, Streamline loading disabled, _nvngx "
            "loading disabled, nvapi64 loading "
            "disabled, XInput loading disabled");
    } else {
        // If unset, force set to 0 so it appears in config
        settings::g_developerTabSettings.safemode.SetValue(false);

        // forces entry to be saved to config
        if (!settings::g_experimentalTabSettings.d3d9_flipex_enabled.GetValue()) {
            settings::g_experimentalTabSettings.d3d9_flipex_enabled.SetValue(false);
        }
        settings::g_developerTabSettings.SaveAll();

        LogInfo("Safemode not enabled - setting to 0 for config visibility");
    }
}

void DoInitializationWithoutHwnd(HMODULE h_module, DWORD fdw_reason) {
    // Initialize QPC timing constants based on actual frequency
    utils::initialize_qpc_timing_constants();

    // Setup high-resolution timer for maximum precision
    if (utils::setup_high_resolution_timer()) {
        LogInfo("High-resolution timer setup successful");
    } else {
        LogWarn("Failed to setup high-resolution timer");
    }

    LogInfo("DLLMain (DisplayCommander) %lld %d h_module: 0x%p", utils::get_now_ns(), fdw_reason,
            reinterpret_cast<uintptr_t>(h_module));

    // Load all settings at startup
    settings::LoadAllSettingsAtStartup();

    // Log current logging level (always logs, even if logging is disabled)
    LogCurrentLogLevel();

    HandleSafemode();

    // Pin the module to prevent premature unload
    HMODULE pinned_module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(h_module), &pinned_module)
        != 0) {
        LogInfo("Module pinned successfully: 0x%p", pinned_module);
    } else {
        DWORD error = GetLastError();
        LogWarn("Failed to pin module: 0x%p, Error: %lu", h_module, error);
    }

    // Register reshade_overlay event for test code
    reshade::register_event<reshade::addon_event::reshade_overlay>(OnReShadeOverlayTest);

    // Register device creation event for D3D9 to D3D9Ex upgrade
    reshade::register_event<reshade::addon_event::create_device>(OnCreateDevice);

    // Capture sync interval on swapchain creation for UI
    reshade::register_event<reshade::addon_event::create_swapchain>(OnCreateSwapchainCapture);

    reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);

    // Register ReShade effect runtime events for input blocking
    reshade::register_event<reshade::addon_event::init_effect_runtime>(OnInitEffectRuntime);
    reshade::register_event<reshade::addon_event::destroy_effect_runtime>(OnDestroyEffectRuntime);
    reshade::register_event<reshade::addon_event::reshade_open_overlay>(OnReShadeOverlayOpen);

    // Defer NVAPI init until after settings are loaded below

    // Register our fullscreen prevention event handler
    // NOTE: Fullscreen prevention is now handled directly in IDXGISwapChain_SetFullscreenState_Detour
    // reshade::register_event<reshade::addon_event::set_fullscreen_state>(OnSetFullscreenState);

    // NVAPI HDR monitor will be started after settings load below if enabled
    // Seed default fps limit snapshot
    // GetFpsLimit removed from proxy, use s_fps_limit directly
    reshade::register_event<reshade::addon_event::present>(OnPresentUpdateBefore);
    // reshade::register_event<reshade::addon_event::finish_present>(OnPresentUpdateAfter);

    // Register draw event handlers for render timing
    reshade::register_event<reshade::addon_event::draw>(OnDraw);
    reshade::register_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
    reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(OnDrawOrDispatchIndirect);

    // Register power saving event handlers for additional GPU operations
    reshade::register_event<reshade::addon_event::dispatch>(OnDispatch);
    reshade::register_event<reshade::addon_event::dispatch_mesh>(OnDispatchMesh);
    reshade::register_event<reshade::addon_event::dispatch_rays>(OnDispatchRays);
    reshade::register_event<reshade::addon_event::copy_resource>(OnCopyResource);
    reshade::register_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion);
    // reshade::register_event<reshade::addon_event::update_buffer_region_command>(OnUpdateBufferRegionCommand);

    // Register buffer resolution upgrade event handlers
    reshade::register_event<reshade::addon_event::create_resource>(OnCreateResource);
    reshade::register_event<reshade::addon_event::create_resource_view>(OnCreateResourceView);
    reshade::register_event<reshade::addon_event::create_sampler>(OnCreateSampler);
    reshade::register_event<reshade::addon_event::bind_viewports>(OnSetViewport);
    reshade::register_event<reshade::addon_event::bind_scissor_rects>(OnSetScissorRects);
    // Note: bind_resource, map_resource, unmap_resource events don't exist in ReShade API
    // These operations are handled differently in ReShade
    // Register device destroy event for restore-on-exit
    reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);

    // Install process-exit safety hooks to restore display on abnormal exits
    process_exit_hooks::Initialize();

    LogInfo("DLL initialization complete - DXGI calls now enabled");

    // Install API hooks for continue rendering
    LogInfo("DLL_THREAD_ATTACH: Installing API hooks...");
    display_commanderhooks::InstallApiHooks();

    g_dll_initialization_complete.store(true);
    // Override ReShade settings early to set tutorial as viewed and disable auto updates
    OverrideReShadeSettings();
}

BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {
    switch (fdw_reason) {
        case DLL_PROCESS_ATTACH: {
            OutputDebugStringA("DisplayCommander: DLL_PROCESS_ATTACH\n");
            g_shutdown.store(false);

            if (g_dll_initialization_complete.load()) {
                LogError("DLLMain(DisplayCommander) already initialized");
                return FALSE;
            }

            OutputDebugStringA("DisplayCommander: About to register addon\n");
            if (!reshade::register_addon(h_module)) {
                // Registration failed - likely due to API version mismatch
                OutputDebugStringA("DisplayCommander: ReShade addon registration FAILED\n");
                LogError("ReShade addon registration failed - this usually indicates an API version mismatch");
                LogError("Display Commander requires ReShade 6.6.2+ (API version 17) but detected older version");

                DetectMultipleReShadeVersions();
                CheckReShadeVersionCompatibility();
                return FALSE;
            }

            DetectMultipleReShadeVersions();
            OutputDebugStringA("DisplayCommander: ReShade addon registration SUCCESS\n");

            // Registration successful - log version compatibility
            LogInfo("Display Commander v%s - ReShade addon registration successful (API version 17 supported)",
                    DISPLAY_COMMANDER_VERSION_STRING);

            // Register overlay early so it appears as a tab by default
            reshade::register_overlay("Display Commander", OnRegisterOverlayDisplayCommander);
            LogInfo("Display Commander overlay registered");

            // Initialize DisplayCommander config system before handling safemode
            display_commander::config::DisplayCommanderConfigManager::GetInstance().Initialize();
            LogInfo("DisplayCommander config system initialized");

            // Handle safemode after config system is initialized

            // Detect multiple ReShade versions AFTER successful registration to avoid interference
            // This prevents our module scanning from interfering with ReShade's internal module detection
            OutputDebugStringA("DisplayCommander: About to detect ReShade modules\n");

            // Store module handle for pinning
            g_hmodule = h_module;

            OutputDebugStringA("DisplayCommander: About to call DoInitializationWithoutHwnd\n");
            DoInitializationWithoutHwnd(h_module, fdw_reason);
            OutputDebugStringA("DisplayCommander: DoInitializationWithoutHwnd completed\n");

            break;
        }
        case DLL_THREAD_ATTACH: {
            break;
        }
        case DLL_THREAD_DETACH: {
            // Log exit detection
            // exit_handler::OnHandleExit(exit_handler::ExitSource::DLL_THREAD_DETACH_EVENT, "DLL thread detach");
            break;
        }

        case DLL_PROCESS_DETACH:
            LogInfo("DLL_PROCESS_DETACH: DLL process detach");
            g_shutdown.store(true);

            // Log exit detection
            exit_handler::OnHandleExit(exit_handler::ExitSource::DLL_PROCESS_DETACH_EVENT, "DLL process detach");

            // Clean up input blocking system
            // Input blocking cleanup is now handled by Windows message hooks

            // Clean up window procedure hooks
            display_commanderhooks::UninstallWindowProcHooks();

            // Clean up API hooks
            display_commanderhooks::UninstallApiHooks();

            // Clean up continuous monitoring if it's running
            StopContinuousMonitoring();
            StopGPUCompletionMonitoring();

            // Clean up refresh rate monitoring
            dxgi::fps_limiter::StopRefreshRateMonitoring();

            // Clean up experimental tab threads
            ui::new_ui::CleanupExperimentalTab();

            // Clean up DualSense support
            display_commander::widgets::dualsense_widget::CleanupDualSenseWidget();

            // Clean up HID suppression hooks
            renodx::hooks::UninstallHIDSuppressionHooks();

            // Clean up DX11 proxy device
            dx11_proxy::DX11ProxyManager::GetInstance().Shutdown();

            // Clean up NVAPI instances before shutdown
            if (g_latencyManager) {
                g_latencyManager->Shutdown();
            }

            // Clean up NVAPI fullscreen prevention
            g_nvapiFullscreenPrevention.Cleanup();

            // Clean up fake NVAPI
            nvapi::g_fakeNvapiManager.Cleanup();

            // Note: reshade::unregister_addon() will automatically unregister all events and overlays
            // registered by this add-on, so manual unregistration is not needed and can cause issues
            // display_restore::RestoreAllIfEnabled(); // restore display settings on exit

            // Unpin the module before unregistration
            if (g_hmodule != nullptr) {
                if (FreeLibrary(g_hmodule) != 0) {
                    LogInfo("Module unpinned successfully: 0x%p", g_hmodule);
                } else {
                    DWORD error = GetLastError();
                    LogWarn("Failed to unpin module: 0x%p, Error: %lu", g_hmodule, error);
                }
                g_hmodule = nullptr;
            }

            reshade::unregister_addon(h_module);

            break;
    }

    return TRUE;
}

// CONTINUOUS RENDERING FUNCTIONS REMOVED - Focus spoofing is now handled by Win32 hooks

// CONTINUOUS RENDERING THREAD REMOVED - Focus spoofing is now handled by Win32 hooks
// This provides a much cleaner and more effective solution
