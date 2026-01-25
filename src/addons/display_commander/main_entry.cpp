#include <array>
#include <filesystem>
#include <iostream>
#include <set>
#include <thread>
#include "addon.hpp"
#include "audio/audio_management.hpp"
#include "autoclick/autoclick_manager.hpp"
#include "config/display_commander_config.hpp"
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
#include "nvapi/vrr_status.hpp"
#include "presentmon/presentmon_manager.hpp"
#include "process_exit_hooks.hpp"
#include "proxy_dll/proxy_detection.hpp"
#include "res/forkawesome.h"
#include "res/ui_colors.hpp"
#include "settings/developer_tab_settings.hpp"
#include "settings/experimental_tab_settings.hpp"
#include "settings/hook_suppression_settings.hpp"
#include "settings/main_tab_settings.hpp"
#include "settings/reshade_tab_settings.hpp"
#include "swapchain_events.hpp"
#include "swapchain_events_power_saving.hpp"
#include "ui/monitor_settings/monitor_settings.hpp"
#include "ui/new_ui/experimental_tab.hpp"
#include "ui/new_ui/main_new_tab.hpp"
#include "ui/new_ui/new_ui_main.hpp"
#include "utils/detour_call_tracker.hpp"
#include "utils/display_commander_logger.hpp"
#include "utils/logging.hpp"
#include "utils/platform_api_detector.hpp"
#include "utils/srwlock_wrapper.hpp"
#include "utils/timing.hpp"
#include "version.hpp"
#include "widgets/dualsense_widget/dualsense_widget.hpp"

#include <d3d11.h>
#include <dxgi1_6.h>
#include <psapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <winver.h>
#include <wrl/client.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <reshade.hpp>
#include <sstream>
#include <string>
#include <vector>

// Forward declarations for ReShade event handlers
void OnInitEffectRuntime(reshade::api::effect_runtime* runtime);
bool OnReShadeOverlayOpen(reshade::api::effect_runtime* runtime, bool open, reshade::api::input_source source);
// Note: OnInitDevice, OnDestroySwapchain, OnDestroyResource are declared in swapchain_events.hpp
void OnInitCommandList(reshade::api::command_list* cmd_list);
void OnDestroyCommandList(reshade::api::command_list* cmd_list);
void OnInitCommandQueue(reshade::api::command_queue* queue);
void OnDestroyCommandQueue(reshade::api::command_queue* queue);
void OnExecuteCommandList(reshade::api::command_queue* queue, reshade::api::command_list* cmd_list);
void OnFinishPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain);
void OnReShadeBeginEffects(reshade::api::effect_runtime* runtime, reshade::api::command_list* cmd_list,
                           reshade::api::resource_view rtv, reshade::api::resource_view rtv_srgb);
void OnReShadeFinishEffects(reshade::api::effect_runtime* runtime, reshade::api::command_list* cmd_list,
                            reshade::api::resource_view rtv, reshade::api::resource_view rtv_srgb);

// Forward declaration for ReShade settings override
void OverrideReShadeSettings();

// Forward declaration for version check
bool CheckReShadeVersionCompatibility();

// Forward declaration for multiple ReShade detection
void DetectMultipleReShadeVersions();

// Forward declaration for multiple Display Commander detection
// Returns true if multiple versions detected (should refuse to load), false otherwise
bool DetectMultipleDisplayCommanderVersions();

// Forward declaration for safemode function
void HandleSafemode();

// Forward declaration for loading addons from Plugins directory
void LoadAddonsFromPluginsDirectory();

static bool TryGetDxgiOutputDeviceNameFromLastSwapchain(wchar_t out_device_name[32]) {
    if (out_device_name == nullptr) return false;

    out_device_name[0] = L'\0';

    auto* swapchain_ptr =
        reinterpret_cast<reshade::api::swapchain*>(g_last_swapchain_ptr_unsafe.load(std::memory_order_acquire));
    if (swapchain_ptr == nullptr) return false;

    auto* unknown = reinterpret_cast<IUnknown*>(swapchain_ptr->get_native());
    if (unknown == nullptr) return false;

    Microsoft::WRL::ComPtr<IDXGISwapChain> dxgi_swapchain;
    if (FAILED(unknown->QueryInterface(IID_PPV_ARGS(&dxgi_swapchain))) || dxgi_swapchain == nullptr) return false;

    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    if (FAILED(dxgi_swapchain->GetContainingOutput(&output)) || output == nullptr) return false;

    Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
    if (FAILED(output->QueryInterface(IID_PPV_ARGS(&output6))) || output6 == nullptr) return false;

    DXGI_OUTPUT_DESC1 desc1 = {};
    if (FAILED(output6->GetDesc1(&desc1))) return false;

    wcsncpy_s(out_device_name, 32, desc1.DeviceName, _TRUNCATE);
    return out_device_name[0] != L'\0';
}

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

// Store entry point detected in DLLMain for saving after initialization
static std::string g_entry_point_to_save;

void OnRegisterOverlayDisplayCommander(reshade::api::effect_runtime* runtime) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
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
}  // namespace

// ReShade effect runtime event handler for input blocking
void OnInitCommandList(reshade::api::command_list* cmd_list) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Command list initialization tracking
    if (cmd_list == nullptr) {
        return;
    }
    // Add any initialization logic here if needed
}

void OnDestroyCommandList(reshade::api::command_list* cmd_list) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Command list destruction tracking
    if (cmd_list == nullptr) {
        return;
    }
    // Add any cleanup logic here if needed
}

void OnInitCommandQueue(reshade::api::command_queue* queue) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Command queue initialization tracking
    if (queue == nullptr) {
        return;
    }
    // Add any initialization logic here if needed
}

void OnDestroyCommandQueue(reshade::api::command_queue* queue) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Command queue destruction tracking
    if (queue == nullptr) {
        return;
    }
    // Add any cleanup logic here if needed
}

void OnExecuteCommandList(reshade::api::command_queue* queue, reshade::api::command_list* cmd_list) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Command list execution tracking
    if (queue == nullptr || cmd_list == nullptr) {
        return;
    }
    // Add any tracking logic here if needed
}

void OnFinishPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Present completion tracking
    if (queue == nullptr || swapchain == nullptr) {
        return;
    }
    // Add any tracking logic here if needed
}

void OnReShadeBeginEffects(reshade::api::effect_runtime* runtime, reshade::api::command_list* cmd_list,
                           reshade::api::resource_view rtv, reshade::api::resource_view rtv_srgb) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // ReShade effects begin tracking
    if (runtime == nullptr || cmd_list == nullptr) {
        return;
    }
    // Add any tracking logic here if needed
}

void OnReShadeFinishEffects(reshade::api::effect_runtime* runtime, reshade::api::command_list* cmd_list,
                            reshade::api::resource_view rtv, reshade::api::resource_view rtv_srgb) {
    display_commanderhooks::InstallApiHooks();
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // ReShade effects finish tracking
    if (runtime == nullptr || cmd_list == nullptr) {
        return;
    }
    // Add any tracking logic here if needed
}

void OnInitEffectRuntime(reshade::api::effect_runtime* runtime) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
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
    RECORD_DETOUR_CALL(utils::get_now_ns());
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
    Unknown,  // Initial/unknown state
    Visible,  // Cursor is visible (UI is open)
    Hidden    // Cursor is hidden (UI is closed)
};

// Test callback for reshade_overlay event
void OnReShadeOverlayTest(reshade::api::effect_runtime* runtime) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    const bool show_display_commander_ui = settings::g_mainTabSettings.show_display_commander_ui.GetValue();
    const bool show_tooltips = show_display_commander_ui;  // only show tooltips if the UI is visible

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
        // Use local bool for ImGui window close button - ImGui will modify this when X is clicked
        bool window_open = true;
        if (ImGui::Begin("Display Commander", &window_open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize)) {
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
        }
        ImGui::End();

        // If window was closed via X button, update the setting
        if (!window_open) {
            settings::g_mainTabSettings.show_display_commander_ui.SetValue(false);
        }
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
    bool show_flip_status = settings::g_mainTabSettings.show_flip_status.GetValue();
    bool show_volume = settings::g_mainTabSettings.show_volume.GetValue();
    bool show_gpu_measurement = (settings::g_mainTabSettings.gpu_measurement_enabled.GetValue() != 0);
    bool show_frame_time_graph = settings::g_mainTabSettings.show_frame_time_graph.GetValue();
    bool show_native_frame_time_graph = settings::g_mainTabSettings.show_native_frame_time_graph.GetValue();
    bool show_cpu_usage = settings::g_mainTabSettings.show_cpu_usage.GetValue();
    bool show_fg_mode = settings::g_mainTabSettings.show_fg_mode.GetValue();
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
        const uint32_t count = ::g_perf_ring.GetCount();
        double total_time = 0.0;

        // Iterate through samples from the last second
        uint32_t sample_count = 0;

        // Iterate backwards through the ring buffer up to 1 second
        for (uint32_t i = 0; i < count && i < ::kPerfRingCapacity; ++i) {
            const ::PerfSample& sample = ::g_perf_ring.GetSample(i);

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
                bool is_recent =
                    (last_sleep_timestamp > 0) && (current_time - last_sleep_timestamp) < (5 * utils::SEC_TO_NS);

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

        // NVAPI VRR (more authoritative on NVIDIA). Cache it to keep overlay overhead low.
        static bool cached_nvapi_ok = false;
        static nvapi::VrrStatus cached_nvapi_vrr{};
        static LONGLONG last_nvapi_update_ns = 0;
        static wchar_t cached_output_device_name[32] = {};
        const LONGLONG nvapi_update_interval_ns = 1000 * utils::NS_TO_MS;  // 1s in nanoseconds

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

        // Update NVAPI VRR once per second (if we can resolve the current output device name).
        if (now_ns - last_nvapi_update_ns >= nvapi_update_interval_ns) {
            wchar_t output_device_name[32] = {};
            if (TryGetDxgiOutputDeviceNameFromLastSwapchain(output_device_name)) {
                // If output changed, force refresh.
                if (wcscmp(output_device_name, cached_output_device_name) != 0) {
                    wcsncpy_s(cached_output_device_name, 32, output_device_name, _TRUNCATE);
                }

                nvapi::VrrStatus vrr{};
                cached_nvapi_ok = nvapi::TryQueryVrrStatusFromDxgiOutputDeviceName(cached_output_device_name, vrr);
                cached_nvapi_vrr = vrr;
            } else {
                cached_nvapi_ok = false;
                cached_nvapi_vrr = nvapi::VrrStatus{};
                cached_output_device_name[0] = L'\0';
            }
            last_nvapi_update_ns = now_ns;
        }

        // Display VRR status (only if show_vrr_status is enabled)
        if (show_vrr_status) {
            // Prefer NVAPI when available; fall back to the existing DXGI heuristic otherwise.
            if (cached_nvapi_ok) {
                if (cached_nvapi_vrr.is_display_in_vrr_mode && cached_nvapi_vrr.is_vrr_enabled) {
                    ImGui::TextColored(ui::colors::TEXT_SUCCESS, "VRR: On");

                } else if (cached_nvapi_vrr.is_display_in_vrr_mode) {
                    ImGui::TextColored(ui::colors::TEXT_WARNING, "VRR: Capable");
                } else if (cached_nvapi_vrr.is_vrr_requested) {
                    ImGui::TextColored(ui::colors::TEXT_WARNING, "VRR: Requested");
                } else {
                    ImGui::TextColored(ui::colors::TEXT_DIMMED, "VRR: Off");
                }
            } else {
                if (cached_stats.all_last_20_within_1s && cached_stats.samples_below_threshold_last_10s >= 2) {
                    ImGui::TextColored(ui::colors::TEXT_SUCCESS, "VRR: On");
                } else {
                    ImGui::TextColored(ui::colors::TEXT_DIMMED, "VRR: Off");
                }
            }
        }

        // Display debugging parameters below VRR status (only if vrr_debug_mode is enabled)
        if (show_vrr_debug_mode && has_recent_sample && cached_stats.is_valid) {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "  Fixed: %.2f Hz", cached_stats.fixed_refresh_hz);
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "  Threshold: %.2f Hz", cached_stats.threshold_hz);
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "  Total samples (10s): %u",
                               cached_stats.total_samples_last_10s);
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "  Below threshold: %u",
                               cached_stats.samples_below_threshold_last_10s);
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "  Last 20 within 1s: %s",
                               cached_stats.all_last_20_within_1s ? "Yes" : "No");
        }

        // NVAPI debug info (optional, shown only in VRR debug mode)
        if (show_vrr_debug_mode) {
            if (!cached_nvapi_vrr.nvapi_initialized) {
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "  NVAPI: Unavailable");
            } else if (!cached_nvapi_vrr.display_id_resolved) {
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "  NVAPI: No displayId (st=%d)",
                                   (int)cached_nvapi_vrr.resolve_status);
                if (!cached_nvapi_vrr.nvapi_display_name.empty()) {
                    ImGui::TextColored(ui::colors::TEXT_DIMMED, "  NVAPI Name: %s",
                                       cached_nvapi_vrr.nvapi_display_name.c_str());
                }
            } else if (!cached_nvapi_ok) {
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "  NVAPI: Query failed (st=%d)",
                                   (int)cached_nvapi_vrr.query_status);
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "  NVAPI DisplayId: %u", cached_nvapi_vrr.display_id);
            } else {
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "  NVAPI: enabled=%d req=%d poss=%d in_mode=%d",
                                   (int)cached_nvapi_vrr.is_vrr_enabled, (int)cached_nvapi_vrr.is_vrr_requested,
                                   (int)cached_nvapi_vrr.is_vrr_possible, (int)cached_nvapi_vrr.is_display_in_vrr_mode);
                // Show which field is causing "VRR: On" to display
                if (cached_nvapi_vrr.is_display_in_vrr_mode) {
                    ImGui::TextColored(ui::colors::TEXT_DIMMED, "  -> Display is in VRR mode (authoritative)");
                } else if (cached_nvapi_vrr.is_vrr_enabled) {
                    ImGui::TextColored(ui::colors::TEXT_DIMMED, "  -> VRR enabled (fallback)");
                }
            }
        }
    }

    if (show_flip_status) {
        // Get current API to determine flip state
        int current_api = 0;  // Default to 0 if runtime/device not available
        if (runtime != nullptr && runtime->get_device() != nullptr) {
            current_api = static_cast<int>(runtime->get_device()->get_api());
        }

        DxgiBypassMode flip_state = GetFlipStateForAPI(current_api);
        const char* flip_state_str = DxgiBypassModeToString(flip_state);

        // Color code based on flip state
        ImVec4 flip_color;
        if (flip_state == DxgiBypassMode::kComposed) {
            flip_color = ui::colors::FLIP_COMPOSED;  // Red - bad
        } else if (flip_state == DxgiBypassMode::kOverlay || flip_state == DxgiBypassMode::kIndependentFlip) {
            flip_color = ui::colors::FLIP_INDEPENDENT;  // Green - good
        } else if (flip_state == DxgiBypassMode::kQueryFailedSwapchainNull
                   || flip_state == DxgiBypassMode::kQueryFailedNoSwapchain1
                   || flip_state == DxgiBypassMode::kQueryFailedNoMedia
                   || flip_state == DxgiBypassMode::kQueryFailedNoStats) {
            flip_color = ui::colors::TEXT_ERROR;  // Red - query failed
        } else {
            flip_color = ui::colors::FLIP_UNKNOWN;  // Yellow - unknown/unset
        }

        // Display flip status with appropriate color
        if (settings::g_mainTabSettings.show_labels.GetValue()) {
            ImGui::TextColored(flip_color, "Flip: %s", flip_state_str);
        } else {
            ImGui::TextColored(flip_color, "%s", flip_state_str);
        }

        if (ImGui::IsItemHovered() && show_tooltips) {
            ImGui::SetTooltip("DXGI Flip Mode: %s", flip_state_str);
        }
    }

    if (show_fg_mode) {
        const DLSSGSummary dlssg_summary = GetDLSSGSummary();

        // Only show the 4 requested buckets: OFF / 2x / 3x / 4x
        if (dlssg_summary.dlss_g_active
            && (dlssg_summary.fg_mode == "2x" || dlssg_summary.fg_mode == "3x" || dlssg_summary.fg_mode == "4x")) {
            ImGui::Text("FG: %s", dlssg_summary.fg_mode.c_str());
        } else {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "FG: OFF");
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
        LONGLONG cpu_time_ns =
            ::g_frame_time_ns.load() - fps_sleep_after_on_present_ns.load() - fps_sleep_before_on_present_ns.load();

        LONGLONG frame_time_ns = ::g_frame_time_ns.load();

        if (cpu_time_ns > 0 && frame_time_ns > 0) {
            // Calculate CPU usage percentage: (sim_duration / frame_time) * 100
            double cpu_usage_percent = (static_cast<double>(cpu_time_ns) / static_cast<double>(frame_time_ns)) * 100.0;

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
        const LONGLONG display_duration_ns = 10 * utils::SEC_TO_NS;  // 10 seconds

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
                default: break;
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

            // Calculate QPC difference in seconds
            double qpc_difference_seconds = 0.0;
            if (display_commanderhooks::QueryPerformanceCounter_Original
                && display_commanderhooks::QueryPerformanceFrequency_Original) {
                LARGE_INTEGER frequency;
                if (display_commanderhooks::QueryPerformanceFrequency_Original(&frequency) && frequency.QuadPart > 0) {
                    LARGE_INTEGER original_qpc;
                    if (display_commanderhooks::QueryPerformanceCounter_Original(&original_qpc)) {
                        LONGLONG spoofed_qpc = display_commanderhooks::ApplyTimeslowdownToQPC(original_qpc.QuadPart);
                        double original_qpc_seconds =
                            static_cast<double>(original_qpc.QuadPart) / static_cast<double>(frequency.QuadPart);
                        double spoofed_qpc_seconds =
                            static_cast<double>(spoofed_qpc) / static_cast<double>(frequency.QuadPart);
                        qpc_difference_seconds = spoofed_qpc_seconds - original_qpc_seconds;
                    }
                }
            }

            if (first_feature) {
                if (settings::g_mainTabSettings.show_labels.GetValue()) {
                    snprintf(feature_text, sizeof(feature_text), "%.2fx TS (%+.1fs)", multiplier,
                             qpc_difference_seconds);
                } else {
                    snprintf(feature_text, sizeof(feature_text), "%.2fx (%+.1fs)", multiplier, qpc_difference_seconds);
                }
                snprintf(tooltip_text, sizeof(tooltip_text), "Time Slowdown: %.2fx multiplier, QPC diff: %+.1f s",
                         multiplier, qpc_difference_seconds);
                first_feature = false;
            } else {
                size_t len = strlen(feature_text);
                if (settings::g_mainTabSettings.show_labels.GetValue()) {
                    snprintf(feature_text + len, sizeof(feature_text) - len, ", %.2fx TS (%+.1fs)", multiplier,
                             qpc_difference_seconds);
                } else {
                    snprintf(feature_text + len, sizeof(feature_text) - len, ", %.2fx (%+.1fs)", multiplier,
                             qpc_difference_seconds);
                }
                len = strlen(tooltip_text);
                snprintf(tooltip_text + len, sizeof(tooltip_text) - len,
                         " | Time Slowdown: %.2fx multiplier, QPC diff: %+.1f s", multiplier, qpc_difference_seconds);
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

    if (show_native_frame_time_graph) {
        ui::new_ui::DrawNativeFrameTimeGraphOverlay(show_tooltips);
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

    // Disable clock display (if setting is enabled)
    if (settings::g_reshadeTabSettings.suppress_reshade_clock.GetValue()) {
        reshade::set_config_value(nullptr, "OVERLAY", "ShowClock", 0);
        LogInfo("ReShade settings override - ShowClock set to 0 (disabled)");
    }

    // Check if we've already set LoadFromDllMain to 0 at least once
    bool load_from_dll_main_set_once = false;
    display_commander::config::get_config_value("DisplayCommander", "LoadFromDllMainSetOnce",
                                                load_from_dll_main_set_once);

    if (!load_from_dll_main_set_once) {
        // Get current value from ReShade.ini for logging
        int32_t current_reshade_value = 0;
        reshade::get_config_value(nullptr, "ADDON", "LoadFromDllMain", current_reshade_value);
        LogInfo("ReShade settings override - LoadFromDllMain current ReShade value: %d", current_reshade_value);

        // Set LoadFromDllMain to 0 (first time only)
        // reshade::set_config_value(nullptr, "ADDON", "LoadFromDllMain", 0);
        LogInfo("ReShade settings override - LoadFromDllMain set to 0 (first time)");

        // Mark that we've set it at least once
        display_commander::config::set_config_value("DisplayCommander", "LoadFromDllMainSetOnce", true);
        display_commander::config::save_config("LoadFromDllMainSetOnce flag set");
        LogInfo("ReShade settings override - LoadFromDllMainSetOnce flag saved to DisplayCommander config");
    } else {
        LogInfo("ReShade settings override - LoadFromDllMain already set to 0 previously, skipping");
    }

    // Add Display Commander ReShade paths to EffectSearchPaths and TextureSearchPaths
    {
        wchar_t documents_path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_MYDOCUMENTS, nullptr, SHGFP_TYPE_CURRENT, documents_path))) {
            std::filesystem::path documents_dir(documents_path);
            std::filesystem::path dc_base_dir = documents_dir / L"Display Commander" / L"Reshade";
            std::filesystem::path shaders_dir = dc_base_dir / L"Shaders";
            std::filesystem::path textures_dir = dc_base_dir / L"Textures";

            // Create directories if they don't exist
            try {
                std::error_code ec;
                std::filesystem::create_directories(shaders_dir, ec);
                if (ec) {
                    LogWarn("Failed to create shaders directory: %ls (error: %s)", shaders_dir.c_str(),
                            ec.message().c_str());
                } else {
                    LogInfo("Created/verified shaders directory: %ls", shaders_dir.c_str());
                }

                std::filesystem::create_directories(textures_dir, ec);
                if (ec) {
                    LogWarn("Failed to create textures directory: %ls (error: %s)", textures_dir.c_str(),
                            ec.message().c_str());
                } else {
                    LogInfo("Created/verified textures directory: %ls", textures_dir.c_str());
                }
            } catch (const std::exception& e) {
                LogWarn("Exception while creating directories: %s", e.what());
            }

            // Helper function to add path to search paths if not already present
            auto addPathToSearchPaths = [](const char* section, const char* key,
                                           const std::filesystem::path& path_to_add) -> bool {
                char buffer[4096] = {0};
                size_t buffer_size = sizeof(buffer);

                // Read current paths
                std::vector<std::string> existing_paths;
                if (reshade::get_config_value(nullptr, section, key, buffer, &buffer_size)) {
                    // Parse null-terminated string array
                    const char* ptr = buffer;
                    while (*ptr != '\0' && ptr < buffer + buffer_size) {
                        std::string path(ptr);
                        if (!path.empty()) {
                            existing_paths.push_back(path);
                        }
                        ptr += path.length() + 1;
                    }
                }

                // Convert path to string (use backslashes, add \** for recursive search)
                std::string path_str = path_to_add.string();
                path_str += "\\**";

                // Helper to normalize path for comparison (remove \** suffix if present)
                auto normalizeForComparison = [](const std::string& path) -> std::string {
                    std::string normalized = path;
                    // Remove trailing \** if present
                    if (normalized.length() >= 3 && normalized.substr(normalized.length() - 3) == "\\**") {
                        normalized = normalized.substr(0, normalized.length() - 3);
                    }
                    return normalized;
                };

                // Check if path already exists (case-insensitive comparison)
                bool path_exists = false;
                std::string normalized_path = normalizeForComparison(path_str);
                for (const auto& existing_path : existing_paths) {
                    std::string normalized_existing = normalizeForComparison(existing_path);
                    // Case-insensitive comparison (check length first, then content)
                    if (normalized_path.length() == normalized_existing.length()
                        && std::equal(normalized_path.begin(), normalized_path.end(), normalized_existing.begin(),
                                      [](char a, char b) { return std::tolower(a) == std::tolower(b); })) {
                        path_exists = true;
                        break;
                    }
                }

                // Add path if it doesn't exist
                if (!path_exists) {
                    existing_paths.push_back(path_str);

                    // Combine paths with null terminators
                    std::string combined;
                    for (const auto& path : existing_paths) {
                        combined += path;
                        combined += '\0';
                    }

                    // Write back to ReShade config (use array version for null-terminated strings)
                    reshade::set_config_value(nullptr, section, key, combined.c_str(), combined.size());
                    LogInfo("Added path to ReShade %s::%s: %s", section, key, path_str.c_str());
                    return true;
                } else {
                    LogInfo("Path already exists in ReShade %s::%s: %s", section, key, normalized_path.c_str());
                    return false;
                }
            };

            // Add shaders path to EffectSearchPaths
            addPathToSearchPaths("GENERAL", "EffectSearchPaths", shaders_dir);

            // Add textures path to TextureSearchPaths
            addPathToSearchPaths("GENERAL", "TextureSearchPaths", textures_dir);
        } else {
            LogWarn("Failed to get Documents folder path, skipping ReShade path configuration");
        }
    }

    LogInfo("ReShade settings override completed successfully");
}

// ReShade loaded status (declared here so it's available to LoadAddonsFromPluginsDirectory)
std::atomic<bool> g_reshade_loaded(false);
std::atomic<bool> g_wait_and_inject_stop(false);

// Helper function to check if an addon is enabled (whitelist approach)
static bool IsAddonEnabledForLoading(const std::string& addon_name, const std::string& addon_file) {
    std::vector<std::string> enabled_addons;
    display_commander::config::get_config_value("ADDONS", "EnabledAddons", enabled_addons);

    // Create identifier in format "name@file"
    std::string identifier = addon_name + "@" + addon_file;

    // Check if this addon is in the enabled list
    for (const auto& enabled_entry : enabled_addons) {
        if (enabled_entry == identifier) {
            return true;
        }
    }

    return false;
}

// Function to load enabled .addon64/.addon32 files from Documents\Display Commander\Reshade\Addons
void LoadAddonsFromPluginsDirectory() {
    OutputDebugStringA("Loading addons from Addons directory");
    wchar_t documents_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_MYDOCUMENTS, nullptr, SHGFP_TYPE_CURRENT, documents_path))) {
        LogWarn("Failed to get Documents folder path, skipping addon loading from Addons directory");
        return;
    }

    std::filesystem::path documents_dir(documents_path);
    std::filesystem::path addons_dir = documents_dir / L"Display Commander" / L"Reshade" / L"Addons";

    // Create Addons directory if it doesn't exist
    try {
        std::error_code ec;
        std::filesystem::create_directories(addons_dir, ec);
        if (ec) {
            LogWarn("Failed to create Addons directory: %ls (error: %s)", addons_dir.c_str(), ec.message().c_str());
            return;
        } else {
            LogInfo("Created/verified Addons directory: %ls", addons_dir.c_str());
        }
    } catch (const std::exception& e) {
        LogWarn("Exception while creating Addons directory: %s", e.what());
        return;
    }

    // Check if ReShade is loaded before attempting to load addons
    if (!g_reshade_loaded.load()) {
        LogInfo("ReShade not loaded yet, skipping addon loading from Addons directory");
        return;
    }
    OutputDebugStringA("ReShade loaded, attempting to load addons from Addons directory");

    // Iterate through all files in the Addons directory
    try {
        std::error_code ec;
        int loaded_count = 0;
        int failed_count = 0;
        int skipped_count = 0;

        for (const auto& entry : std::filesystem::directory_iterator(
                 addons_dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) {
                LogWarn("Error accessing Addons directory: %s", ec.message().c_str());
                continue;
            }

            if (!entry.is_regular_file()) {
                continue;
            }

            const auto& path = entry.path();
            const auto extension = path.extension();

            // Check for .addon64 (64-bit) or .addon32 (32-bit) extensions
            if (extension != L".addon64" && extension != L".addon32") {
                continue;
            }

            // Only load architecture-appropriate addons
#ifdef _WIN64
            if (extension != L".addon64") {
                continue;
            }
#else
            if (extension != L".addon32") {
                continue;
            }
#endif

            // Get addon name and file name
            std::string addon_name = path.stem().string();
            std::string addon_file = path.filename().string();

            // Check if addon is enabled (whitelist approach - default is disabled)
            if (!IsAddonEnabledForLoading(addon_name, addon_file)) {
                LogInfo("Skipping disabled addon: %ls", path.c_str());
                skipped_count++;
                continue;
            }

            LogInfo("Loading enabled addon from Addons directory: %ls", path.c_str());

            // Use LoadLibraryExW with search flags similar to ReShade's addon_manager
            HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
                                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (module == nullptr) {
                DWORD error_code = GetLastError();
                LogError("Failed to load addon from '%ls' (error: %lu)", path.c_str(), error_code);
                failed_count++;
            } else {
                LogInfo("Successfully loaded addon from '%ls'", path.c_str());
                loaded_count++;
            }
        }

        if (loaded_count > 0 || failed_count > 0 || skipped_count > 0) {
            LogInfo("Addon loading from Addons directory completed: %d loaded, %d failed, %d skipped (disabled)",
                    loaded_count, failed_count, skipped_count);
        }
    } catch (const std::exception& e) {
        LogWarn("Exception while loading addons from Addons directory: %s", e.what());
    }
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

// Function to detect multiple Display Commander versions by scanning all modules
// Returns true if multiple versions detected (should refuse to load), false otherwise
bool DetectMultipleDisplayCommanderVersions() {
    // Type definition for the version export function
    typedef const char* (*GetDisplayCommanderVersionFunc)();
    // Type definition for the load timestamp export function
    typedef LONGLONG (*GetLoadedNsFunc)();

    HMODULE modules[1024];
    DWORD num_modules = 0;

    // Use K32EnumProcessModules for safe enumeration
    if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &num_modules) == 0) {
        DWORD error = GetLastError();
        // Use OutputDebugStringA since logging might not be initialized yet
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                 "[DisplayCommander] Failed to enumerate process modules for Display Commander detection: %lu\n",
                 error);
        OutputDebugStringA(error_msg);
        return false;  // Can't detect, so allow loading
    }

    if (num_modules > sizeof(modules)) {
        num_modules = static_cast<DWORD>(sizeof(modules));
    }

    int dc_module_count = 0;
    HMODULE current_module = g_hmodule;
    LONGLONG current_load_time_ns = g_dll_load_time_ns.load(std::memory_order_acquire);
    bool should_refuse_load = false;

    // Use OutputDebugStringA for early detection logging
    char scan_msg[256];
    snprintf(scan_msg, sizeof(scan_msg), "[DisplayCommander] === Display Commander Module Detection ===\n");
    OutputDebugStringA(scan_msg);
    snprintf(scan_msg, sizeof(scan_msg), "[DisplayCommander] Scanning %u modules for Display Commander...\n",
             static_cast<unsigned int>(num_modules / sizeof(HMODULE)));
    OutputDebugStringA(scan_msg);
    snprintf(scan_msg, sizeof(scan_msg), "[DisplayCommander] Current instance load time: %lld ns\n",
             current_load_time_ns);
    OutputDebugStringA(scan_msg);

    for (DWORD i = 0; i < num_modules / sizeof(HMODULE); ++i) {
        HMODULE module = modules[i];
        if (module == nullptr) continue;

        // Skip the current module (ourselves)
        if (module == current_module) {
            continue;
        }

        // Check if this module has GetDisplayCommanderVersion export
        GetDisplayCommanderVersionFunc version_func =
            reinterpret_cast<GetDisplayCommanderVersionFunc>(GetProcAddress(module, "GetDisplayCommanderVersion"));

        if (version_func != nullptr) {
            dc_module_count++;

            // Get module file path for logging
            wchar_t module_path[MAX_PATH];
            DWORD path_length = GetModuleFileNameW(module, module_path, MAX_PATH);

            char narrow_path[MAX_PATH] = "(path unavailable)";
            if (path_length > 0) {
                // Convert wide string to narrow string for logging
                WideCharToMultiByte(CP_UTF8, 0, module_path, -1, narrow_path, MAX_PATH, nullptr, nullptr);
            }

            // Get version string from the other instance
            const char* other_version = version_func();
            const char* current_version = DISPLAY_COMMANDER_VERSION_STRING;

            // Get load timestamp from the other instance
            GetLoadedNsFunc loaded_ns_func = reinterpret_cast<GetLoadedNsFunc>(GetProcAddress(module, "LoadedNs"));
            LONGLONG other_load_time_ns = 0;
            bool has_load_time = false;
            if (loaded_ns_func != nullptr) {
                other_load_time_ns = loaded_ns_func();
                has_load_time = (other_load_time_ns > 0);
            }

            // Log to debug output
            char found_msg[512];
            snprintf(found_msg, sizeof(found_msg), "[DisplayCommander] Found Display Commander module #%d: 0x%p - %s\n",
                     dc_module_count, module, narrow_path);
            OutputDebugStringA(found_msg);
            snprintf(found_msg, sizeof(found_msg), "[DisplayCommander]   Other version: %s\n",
                     other_version ? other_version : "(unknown)");
            OutputDebugStringA(found_msg);
            snprintf(found_msg, sizeof(found_msg), "[DisplayCommander]   Current version: %s\n", current_version);
            OutputDebugStringA(found_msg);

            if (has_load_time) {
                snprintf(found_msg, sizeof(found_msg), "[DisplayCommander]   Other load time: %lld ns\n",
                         other_load_time_ns);
                OutputDebugStringA(found_msg);
                snprintf(found_msg, sizeof(found_msg), "[DisplayCommander]   Current load time: %lld ns\n",
                         current_load_time_ns);
                OutputDebugStringA(found_msg);

                // Compare load timestamps to determine which DLL was loaded first
                if (other_load_time_ns < current_load_time_ns) {
                    // Other instance was loaded first - we should refuse to load
                    snprintf(
                        found_msg, sizeof(found_msg),
                        "[DisplayCommander]   Conflict resolution: Other instance loaded first (difference: %lld ns). "
                        "Refusing to load current instance.\n",
                        current_load_time_ns - other_load_time_ns);
                    OutputDebugStringA(found_msg);
                } else {
                    // We were loaded first - allow loading but notify the other instance
                    snprintf(found_msg, sizeof(found_msg),
                             "[DisplayCommander]   Conflict resolution: Current instance loaded first (difference: "
                             "%lld ns). "
                             "Allowing current instance to load.\n",
                             other_load_time_ns - current_load_time_ns);
                    OutputDebugStringA(found_msg);
                }
            } else {
                OutputDebugStringA("[DisplayCommander]   Load timestamp not available from other instance.\n");
            }

            // Compare versions (simple string comparison - assumes semantic versioning)
            if (other_version != nullptr) {
                // Store the other version in our global atomic for UI display
                extern std::atomic<std::shared_ptr<const std::string>> g_other_dc_version_detected;
                auto version_str = std::make_shared<const std::string>(other_version);
                g_other_dc_version_detected.store(version_str);

                // Try to notify the other instance about multiple versions
                typedef void (*NotifyMultipleVersionsFunc)(const char*);
                NotifyMultipleVersionsFunc notify_func = reinterpret_cast<NotifyMultipleVersionsFunc>(
                    GetProcAddress(module, "NotifyDisplayCommanderMultipleVersions"));

                if (notify_func != nullptr) {
                    // Call the notification function on the other instance with our version
                    notify_func(current_version);
                    OutputDebugStringA("[DisplayCommander] Notified other instance of multiple versions.\n");
                }

                // Resolve conflict based on load timestamps
                bool instance_should_refuse = false;
                if (has_load_time) {
                    // If other instance was loaded first, refuse to load
                    if (other_load_time_ns < current_load_time_ns) {
                        instance_should_refuse = true;
                        should_refuse_load = true;  // Set global flag
                    }
                } /* else {
                    // If we can't determine load order, it will be set to greater value than current instance
                    instance_should_refuse = true;
                    should_refuse_load = true;  // Set global flag
                }*/

                if (instance_should_refuse) {
                    // Log to ReShade's log as error (exception to the rule) if ReShade is available
                    char error_msg[512];
                    if (has_load_time) {
                        snprintf(
                            error_msg, sizeof(error_msg),
                            "[Display Commander] ERROR: Multiple Display Commander instances detected! "
                            "Other instance: v%s at %s (loaded at %lld ns), Current instance: v%s (loaded at %lld ns). "
                            "Other instance was loaded first - refusing to load current instance to prevent conflicts.",
                            other_version, narrow_path, other_load_time_ns, current_version, current_load_time_ns);
                    } else {
                        snprintf(error_msg, sizeof(error_msg),
                                 "[Display Commander] ERROR: Multiple Display Commander instances detected! "
                                 "Other instance: v%s at %s, Current instance: v%s. "
                                 "Cannot determine load order - refusing to load to prevent conflicts. Please ensure "
                                 "only one version is loaded.",
                                 other_version, narrow_path, current_version);
                    }

                    // Use reshade::log::message with error level (exception to the rule)
                    // This might not be available yet, but try anyway
                    try {
                        reshade::log::message(reshade::log::level::error, error_msg);
                    } catch (...) {
                        // If ReShade logging isn't available yet, just use debug output
                        OutputDebugStringA(error_msg);
                        OutputDebugStringA("\n");
                    }

                    // Also log to debug output
                    OutputDebugStringA("[DisplayCommander] ERROR: Multiple Display Commander instances detected!\n");
                    snprintf(found_msg, sizeof(found_msg), "[DisplayCommander]   Other instance: v%s at %s\n",
                             other_version, narrow_path);
                    OutputDebugStringA(found_msg);
                    snprintf(found_msg, sizeof(found_msg), "[DisplayCommander]   Current instance: v%s\n",
                             current_version);
                    OutputDebugStringA(found_msg);
                    OutputDebugStringA("[DisplayCommander] Refusing to load to prevent conflicts.\n");
                }
            }
        }
    }

    // Log completion
    char complete_msg[256];
    snprintf(complete_msg, sizeof(complete_msg), "[DisplayCommander] === Display Commander Detection Complete ===\n");
    OutputDebugStringA(complete_msg);
    snprintf(complete_msg, sizeof(complete_msg),
             "[DisplayCommander] Total Display Commander modules found: %d (excluding current)\n", dc_module_count);
    OutputDebugStringA(complete_msg);

    if (should_refuse_load) {
        OutputDebugStringA(
            "[DisplayCommander] WARNING: Multiple Display Commander versions detected! Refusing to load.\n");
        return true;  // Multiple versions detected and we should refuse - refuse to load
    }

    if (dc_module_count > 0) {
        // Other instances found but we were loaded first - allow loading
        OutputDebugStringA(
            "[DisplayCommander] INFO: Multiple Display Commander versions detected, but current instance was loaded "
            "first. Allowing load.\n");
    } else {
        OutputDebugStringA("[DisplayCommander] Single Display Commander instance detected - no conflicts.\n");
    }

    return false;  // No conflicts or we were loaded first - allow loading
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
        const int max_wait_time_ms = 30000;  // Maximum 30 seconds per DLL
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
    settings::g_developerTabSettings.dll_loading_delay_ms.SetValue(
        settings::g_developerTabSettings.dll_loading_delay_ms.GetValue());

    if (safemode_enabled) {
        LogInfo(
            "Safemode enabled - disabling auto-apply settings, continue rendering, FPS limiter, XInput hooks, MinHook "
            "initialization, and "
            "Streamline loading");

        // Set safemode to 0 (force set to 0)
        // settings::g_developerTabSettings.safemode.SetValue(false);
        settings::g_developerTabSettings.prevent_fullscreen.SetValue(false);
        settings::g_developerTabSettings.continue_rendering.SetValue(false);

        settings::g_mainTabSettings.fps_limiter_mode.SetValue((int)FpsLimiterMode::kDisabled);

        // Disable all auto-apply settings
        ui::monitor_settings::g_setting_auto_apply_resolution.SetValue(false);
        ui::monitor_settings::g_setting_auto_apply_refresh.SetValue(false);
        ui::monitor_settings::g_setting_apply_display_settings_at_start.SetValue(false);

        // Disable XInput hooks
        settings::g_hook_suppression_settings.suppress_xinput_hooks.SetValue(true);

        // Enable MinHook suppression
        // settings::g_developerTabSettings.suppress_minhook.SetValue(true);

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

void DoInitializationWithoutHwndSafe(HMODULE h_module) {
    // Initialize config system now (safe to start threads here, after DLLMain)

    // Save entry point to config now that config system is initialized
    // (entry point was detected in DLLMain but couldn't be saved then)
    if (!g_entry_point_to_save.empty()) {
        display_commander::config::set_config_value("DisplayCommander", "EntryPoint", g_entry_point_to_save);
        display_commander::config::save_config("Entry point detection");
        LogInfo("Entry point logged to DisplayCommander.ini: %s", g_entry_point_to_save.c_str());
    }

    // Setup high-resolution timer for maximum precision
    if (utils::setup_high_resolution_timer()) {
        LogInfo("High-resolution timer setup successful");
    } else {
        LogWarn("Failed to setup high-resolution timer");
    }

    LogInfo("DLLMain (DisplayCommander) %lld h_module: 0x%p", utils::get_now_ns(),
            reinterpret_cast<uintptr_t>(h_module));

    // Load all settings at startup
    settings::LoadAllSettingsAtStartup();

    // Log current logging level (always logs, even if logging is disabled)
    LogCurrentLogLevel();

    HandleSafemode();

    // Pin the module to prevent premature unload (unless suppressed by config)
    bool suppress_pin_module = false;
    display_commander::config::get_config_value_ensure_exists("DisplayCommander.Safemode", "SuppressPinModule",
                                                              suppress_pin_module, false);

    if (!suppress_pin_module) {
        HMODULE pinned_module = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(h_module), &pinned_module)
            != 0) {
            LogInfo("Module pinned successfully: 0x%p", pinned_module);
            g_module_pinned.store(true);
        } else {
            DWORD error = GetLastError();
            LogWarn("Failed to pin module: 0x%p, Error: %lu", h_module, error);
            g_module_pinned.store(false);
        }
    } else {
        LogInfo("Module pinning suppressed by config (SuppressPinModule=true)");
        g_module_pinned.store(false);
    }
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

void DoInitializationWithoutHwnd(HMODULE h_module) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
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
    reshade::register_event<reshade::addon_event::finish_present>(OnPresentUpdateAfter);

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
    reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);

    // Register command list/queue lifecycle events
    reshade::register_event<reshade::addon_event::init_command_list>(OnInitCommandList);
    reshade::register_event<reshade::addon_event::destroy_command_list>(OnDestroyCommandList);
    reshade::register_event<reshade::addon_event::init_command_queue>(OnInitCommandQueue);
    reshade::register_event<reshade::addon_event::destroy_command_queue>(OnDestroyCommandQueue);
    reshade::register_event<reshade::addon_event::execute_command_list>(OnExecuteCommandList);

    // Register swapchain/resource lifecycle events
    reshade::register_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
    reshade::register_event<reshade::addon_event::destroy_resource>(OnDestroyResource);

    // Register present completion event
    reshade::register_event<reshade::addon_event::finish_present>(OnFinishPresent);

    // Register ReShade effect rendering events
    reshade::register_event<reshade::addon_event::reshade_begin_effects>(OnReShadeBeginEffects);
    reshade::register_event<reshade::addon_event::reshade_finish_effects>(OnReShadeFinishEffects);
    display_commanderhooks::InstallApiHooks();
}

// Named event name for injection tracking (shared across processes)
// Defined here so it's available in DllMain
constexpr const wchar_t* INJECTION_ACTIVE_EVENT_NAME = L"Local\\DisplayCommander_InjectionActive";
constexpr const wchar_t* INJECTION_STOP_EVENT_NAME = L"Local\\DisplayCommander_InjectionStop";

BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {
    switch (fdw_reason) {
        case DLL_PROCESS_ATTACH: {
            // Record load timestamp as early as possible for conflict resolution
            g_hmodule = h_module;
            g_dll_load_time_ns.store(utils::get_now_ns(), std::memory_order_release);

            // Detect multiple Display Commander instances - refuse to load if multiple versions found
            if (DetectMultipleDisplayCommanderVersions()) {
                // log to reshade log
                reshade::log::message(
                    reshade::log::level::error,
                    "[DisplayCommander] Multiple Display Commander instances detected - refusing to load.");
                OutputDebugStringA(
                    "[DisplayCommander] Multiple Display Commander instances detected - refusing to load.\n");
                g_process_attached.store(true);
                return FALSE;  // Refuse to load
            }
            // Print command line arguments when running with rundll32.exe
            LPSTR command_line = GetCommandLineA();
            if (command_line != nullptr && command_line[0] != '\0') {
                OutputDebugStringA("[DisplayCommander] Command line: ");
                OutputDebugStringA(command_line);
                OutputDebugStringA("\n");
                // run32dll
                if (strstr(command_line, "rundll32") != nullptr) {
                    OutputDebugStringA("Run32DLL command line detected");
                    g_process_attached.store(true);
                    return TRUE;
                }
            } else {
                OutputDebugStringA("[DisplayCommander] Command line: (empty)\n");
            }
            // Don't initialize config system here - it starts a thread which is unsafe during DLLMain
            // Initialize() will be called later in DoInitializationWithoutHwnd
            g_shutdown.store(false);

            /*
            // Print command line arguments when running with rundll32.exe
            LPSTR command_line = GetCommandLineA();
            if (command_line != nullptr && command_line[0] != '\0') {
                OutputDebugStringA("[DisplayCommander] Command line: ");
                OutputDebugStringA(command_line);
                OutputDebugStringA("\n");
                LogInfo("Command line arguments: %s", command_line);
                return TRUE;
            } else {
                OutputDebugStringA("[DisplayCommander] Command line: (empty)\n");
                LogInfo("Command line arguments: (empty)");
            }

            */
            // TODO: if file exists, load it
            // check if reshade is loaded by going through all modules and checking if ReShadeRegisterAddon is
            // present
            HMODULE modules[1024];
            DWORD num_modules_bytes = 0;
            if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &num_modules_bytes) != 0) {
                DWORD num_modules = (std::min<DWORD>)(num_modules_bytes / sizeof(HMODULE),
                                                      static_cast<DWORD>(sizeof(modules) / sizeof(HMODULE)));

                // check for method named ReShadeRegisterAddon
                for (DWORD i = 0; i < num_modules; i++) {
                    if (modules[i] == nullptr) continue;
                    FARPROC register_func = GetProcAddress(modules[i], "ReShadeRegisterAddon");
                    if (register_func != nullptr) {
                        g_reshade_loaded.store(true);
                        OutputDebugStringA("ReShadeRegisterAddon found");
                        break;
                    }
                }
            }
#ifdef _WIN64
            if (!g_reshade_loaded.load()) {
                // Set environment variable to disable ReShade loading check
                SetEnvironmentVariableW(L"RESHADE_DISABLE_LOADING_CHECK", L"1");
                if (LoadLibraryA("Reshade64.dll") != nullptr) {
                    g_reshade_loaded.store(true);
                    OutputDebugStringA("Reshade64.dll loaded successfully");
                } else {
                    // if file exists
                    if (std::filesystem::exists("Reshade64.dll")) {
                        DWORD error = GetLastError();
                        OutputDebugStringA("Reshade64.dll could not be loaded");
                        OutputDebugStringA(std::to_string(error).c_str());
                        std::string error_msg =
                            "Reshade64.dll could not be loaded: Error code: " + std::to_string(error);
                        MessageBoxA(nullptr, error_msg.c_str(), error_msg.c_str(), MB_OK | MB_ICONWARNING | MB_TOPMOST);
                    }
                }
            }
#else
            if (!g_reshade_loaded.load()) {
                // Set environment variable to disable ReShade loading check
                SetEnvironmentVariableW(L"RESHADE_DISABLE_LOADING_CHECK", L"1");
                if (LoadLibraryA("Reshade32.dll") != nullptr) {
                    g_reshade_loaded.store(true);
                    OutputDebugStringA("Reshade32.dll loaded successfully");
                }
            }
#endif
            WCHAR module_path[MAX_PATH];
            std::wstring entry_point = L"addon";
            bool found_proxy = false;
            if (GetModuleFileNameW(h_module, module_path, MAX_PATH) > 0) {
                std::filesystem::path module_file_path(module_path);
                std::wstring module_name = module_file_path.stem().wstring();
                std::wstring module_name_full = module_file_path.filename().wstring();

                // Convert to lowercase for comparison
                std::transform(module_name.begin(), module_name.end(), module_name.begin(), ::towlower);
                std::transform(module_name_full.begin(), module_name_full.end(), module_name_full.begin(), ::towlower);

                // Debug: Print module name to debug output
                int module_utf8_size =
                    WideCharToMultiByte(CP_UTF8, 0, module_name_full.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (module_utf8_size > 0) {
                    std::string module_name_utf8(module_utf8_size - 1, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, module_name_full.c_str(), -1, module_name_utf8.data(),
                                        module_utf8_size, nullptr, nullptr);
                    char debug_msg[512];
                    snprintf(debug_msg, sizeof(debug_msg),
                             "[DisplayCommander] DEBUG: module_name_full='%s', module_name (stem)='%ws'\n",
                             module_name_utf8.c_str(), module_name.c_str());
                    OutputDebugStringA(debug_msg);
                }

                // List of proxy DLL names to check
                struct ProxyDllInfo {
                    const wchar_t* name;
                    const wchar_t* entry_point;
                    const char* debug_msg;
                    const char* log_msg;
                };

                const ProxyDllInfo proxy_dlls[] = {
                    {L"dxgi", L"dxgi.dll", "[DisplayCommander] Entry point detected: dxgi.dll (proxy mode)\n",
                     "Display Commander loaded as dxgi.dll proxy - DXGI functions will be forwarded to system "
                     "dxgi.dll"},
                    {L"d3d11", L"d3d11.dll", "[DisplayCommander] Entry point detected: d3d11.dll (proxy mode)\n",
                     "Display Commander loaded as d3d11.dll proxy - D3D11 functions will be forwarded to system "
                     "d3d11.dll"},
                    {L"d3d12", L"d3d12.dll", "[DisplayCommander] Entry point detected: d3d12.dll (proxy mode)\n",
                     "Display Commander loaded as d3d12.dll proxy - D3D12 functions will be forwarded to system "
                     "d3d12.dll"},
                    {L"version", L"version.dll", "[DisplayCommander] Entry point detected: version.dll (proxy mode)\n",
                     "Display Commander loaded as version.dll proxy - Version functions will be forwarded to "
                     "system "
                     "version.dll"}};

                // Check if we're loaded as any proxy DLL
                for (const auto& proxy : proxy_dlls) {
                    if (_wcsicmp(module_name.c_str(), proxy.name) == 0) {
                        entry_point = proxy.entry_point;
                        OutputDebugStringA(proxy.debug_msg);
                        // LogInfoDirect("%s", proxy.log_msg);
                        found_proxy = true;
                        break;
                    }
                }

                if (!found_proxy) {
                    entry_point = L"addon";
                    // Convert module_name to UTF-8 for debug output
                    int module_utf8_size =
                        WideCharToMultiByte(CP_UTF8, 0, module_name.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    if (module_utf8_size > 0) {
                        std::string module_name_utf8(module_utf8_size - 1, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, module_name.c_str(), -1, module_name_utf8.data(),
                                            module_utf8_size, nullptr, nullptr);
                        char debug_msg[512];
                        snprintf(debug_msg, sizeof(debug_msg),
                                 "[DisplayCommander] Entry point detected: addon (module: %s)\n",
                                 module_name_utf8.c_str());
                        OutputDebugStringA(debug_msg);
                    } else {
                        OutputDebugStringA("[DisplayCommander] Entry point detected: addon\n");
                    }
                    // LogInfoDirect("Display Commander loaded as ReShade addon (module: %ws)", module_name.c_str());
                }
            } else {
                OutputDebugStringA("[DisplayCommander] Entry point detection: Failed to get module filename\n");
            }

            // don't call register_addon if reshade is not loaded to prevent crash
            if (!g_reshade_loaded.load()) {
                OutputDebugStringA("ReShade not loaded");
                // Detect and log platform APIs (Steam, Epic, GOG, etc.)
                display_commander::utils::DetectAndLogPlatformAPIs();

                // Check if any platform was detected
                std::vector<display_commander::utils::PlatformAPI> detected_platforms =
                    display_commander::utils::GetDetectedPlatformAPIs();

                OutputDebugStringA("Detected platforms: ");
                for (size_t i = 0; i < detected_platforms.size(); ++i) {
                    if (i > 0) OutputDebugStringA(", ");
                    OutputDebugStringA(display_commander::utils::GetPlatformAPIName(detected_platforms[i]));
                }
                OutputDebugStringA("\n");

                // Get executable path for whitelist check
                WCHAR executable_path[MAX_PATH] = {0};
                bool whitelist = false;
                if (GetModuleFileNameW(nullptr, executable_path, MAX_PATH) > 0) {
                    OutputDebugStringA("Executable path: ");
                    char executable_path_narrow[MAX_PATH];
                    WideCharToMultiByte(CP_ACP, 0, executable_path, -1, executable_path_narrow, MAX_PATH, nullptr,
                                        nullptr);
                    OutputDebugStringA(executable_path_narrow);
                    OutputDebugStringA("\n");
                    whitelist = display_commander::utils::TestWhitelist(executable_path);
                }

                // Only try Documents folder and show message box if platform detected, proxy found, or whitelisted
                if (!detected_platforms.empty() || found_proxy || whitelist) {
                    // Try to find Reshade DLL in Documents folder
                    wchar_t documents_path[MAX_PATH];

                    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_MYDOCUMENTS, nullptr, SHGFP_TYPE_CURRENT,
                                                   documents_path))) {
                        std::filesystem::path documents_dir(documents_path);
                        std::filesystem::path dc_reshade_dir = documents_dir / L"Display Commander" / L"Reshade";
#ifdef _WIN64
                        std::filesystem::path reshade_path = dc_reshade_dir / L"Reshade64.dll";
                        const char* dll_name = "Reshade64.dll";
#else
                        std::filesystem::path reshade_path = dc_reshade_dir / L"Reshade32.dll";
                        const char* dll_name = "Reshade32.dll";
#endif

                        if (std::filesystem::exists(reshade_path)) {
                            // Get absolute/canonical path
                            std::error_code ec;
                            std::filesystem::path absolute_path = std::filesystem::canonical(reshade_path, ec);
                            if (ec) {
                                // Fallback to absolute path if canonical fails
                                absolute_path = std::filesystem::absolute(reshade_path, ec);
                                if (ec) {
                                    absolute_path = reshade_path;  // Last resort
                                }
                            }

                            // Check if DLL is already loaded (by filename, not full path)
                            std::wstring dll_filename = absolute_path.filename().wstring();
                            HMODULE already_loaded = GetModuleHandleW(dll_filename.c_str());
                            if (already_loaded != nullptr) {
                                g_reshade_loaded.store(true);
                                char msg[512];
                                char path_narrow[MAX_PATH];
                                WideCharToMultiByte(CP_ACP, 0, absolute_path.c_str(), -1, path_narrow, MAX_PATH,
                                                    nullptr, nullptr);
                                snprintf(msg, sizeof(msg), "%s already loaded from Documents folder: %s", dll_name,
                                         path_narrow);
                                OutputDebugStringA(msg);
                            } else {
                                // Set environment variable to disable ReShade loading check
                                SetEnvironmentVariableW(L"RESHADE_DISABLE_LOADING_CHECK", L"1");
                                // Try to load from Documents folder
                                HMODULE reshade_module = LoadLibraryW(absolute_path.c_str());
                                if (reshade_module != nullptr) {
                                    g_reshade_loaded.store(true);
                                    char msg[512];
                                    char path_narrow[MAX_PATH];
                                    WideCharToMultiByte(CP_ACP, 0, absolute_path.c_str(), -1, path_narrow, MAX_PATH,
                                                        nullptr, nullptr);
                                    snprintf(msg, sizeof(msg), "%s loaded successfully from Documents folder: %s",
                                             dll_name, path_narrow);
                                    OutputDebugStringA(msg);
                                } else {
                                    DWORD error = GetLastError();

                                    // Get detailed error message
                                    wchar_t error_msg[512] = {0};
                                    DWORD msg_len =
                                        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                                       nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                                       error_msg, sizeof(error_msg) / sizeof(wchar_t), nullptr);

                                    char msg[1024];
                                    char path_narrow[MAX_PATH];
                                    WideCharToMultiByte(CP_ACP, 0, absolute_path.c_str(), -1, path_narrow, MAX_PATH,
                                                        nullptr, nullptr);

                                    if (msg_len > 0) {
                                        // Remove trailing newlines from error message
                                        while (
                                            msg_len > 0
                                            && (error_msg[msg_len - 1] == L'\n' || error_msg[msg_len - 1] == L'\r')) {
                                            error_msg[--msg_len] = L'\0';
                                        }
                                        char error_msg_narrow[512];
                                        WideCharToMultiByte(CP_ACP, 0, error_msg, -1, error_msg_narrow,
                                                            sizeof(error_msg_narrow), nullptr, nullptr);
                                        snprintf(msg, sizeof(msg),
                                                 "Failed to load %s from Documents folder (error %lu: %s): %s",
                                                 dll_name, error, error_msg_narrow, path_narrow);
                                    } else {
                                        snprintf(msg, sizeof(msg),
                                                 "Failed to load %s from Documents folder (error: %lu): %s", dll_name,
                                                 error, path_narrow);
                                    }
                                    OutputDebugStringA(msg);
                                    MessageBoxA(nullptr, msg, msg, MB_OK | MB_ICONWARNING | MB_TOPMOST);
                                    g_process_attached.store(true);
                                    return FALSE;
                                }
                            }
                        }
                    } else {
                        MessageBoxA(nullptr, "ReShade not found in Documents folder",
                                    "Display Commander - ReShade Not Found", MB_OK | MB_ICONWARNING | MB_TOPMOST);
                        g_process_attached.store(true);
                        return FALSE;
                    }

                    // If still not loaded, show message box
                    if (!g_reshade_loaded.load()) {
                        std::string platform_names;
                        for (size_t i = 0; i < detected_platforms.size(); ++i) {
                            if (i > 0) platform_names += ", ";
                            platform_names += display_commander::utils::GetPlatformAPIName(detected_platforms[i]);
                        }

#ifdef _WIN64
                        const char* dll_name_msg = "ReShade64.dll";
#else
                        const char* dll_name_msg = "Reshade32.dll";
#endif

                        // Get Documents folder path with Display Commander subdirectory
                        std::string documents_path_str = "your Documents folder";
                        wchar_t documents_path[MAX_PATH];
                        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_MYDOCUMENTS, nullptr, SHGFP_TYPE_CURRENT,
                                                       documents_path))) {
                            std::filesystem::path documents_dir(documents_path);
                            std::filesystem::path dc_reshade_dir = documents_dir / L"Display Commander" / L"Reshade";
                            // Convert wide string to narrow string
                            char documents_path_narrow[MAX_PATH];
                            WideCharToMultiByte(CP_ACP, 0, dc_reshade_dir.c_str(), -1, documents_path_narrow, MAX_PATH,
                                                nullptr, nullptr);
                            documents_path_str = documents_path_narrow;
                        }

                        std::string message = "Display Commander detected a game platform (" + platform_names
                                              + ") but ReShade was not found.\n\n";
                        message += "ReShade is required for Display Commander to function.\n\n";
                        message += "Please ensure " + std::string(dll_name_msg) + " is either:\n";
                        message += "1. In the game's installation folder, or\n";
                        message += "2. In " + documents_path_str + "\n\n";
                        message += "Download ReShade from: https://reshade.me/";

                        MessageBoxA(nullptr, message.c_str(), "Display Commander - ReShade Not Found",
                                    MB_OK | MB_ICONWARNING | MB_TOPMOST);
                        g_process_attached.store(true);
                        return FALSE;
                    }
                } else {
                    g_process_attached.store(true);
                    return FALSE;
                }
            }

            if (!reshade::register_addon(h_module)) {
                // Registration failed - likely due to API version mismatch

                // DetectMultipleReShadeVersions();
                // CheckReShadeVersionCompatibility();
                OutputDebugStringA("ReShade 0000000");

                {
                    // log g_module handle
                    char msg[512];
                    snprintf(msg, sizeof(msg), "g_module handle: 0x%p", g_hmodule);
                    reshade::log::message(reshade::log::level::info, msg);
                }
                // list all loaded modules to reshade and g_hmodule
                HMODULE modules[1024];
                DWORD num_modules_bytes = 0;
                if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &num_modules_bytes) != 0) {
                    DWORD num_modules = (std::min<DWORD>)(num_modules_bytes / sizeof(HMODULE),
                                                          static_cast<DWORD>(sizeof(modules) / sizeof(HMODULE)));
                    for (DWORD i = 0; i < num_modules; i++) {
                        char msg[512];
                        // print module handle and name
                        wchar_t module_name[MAX_PATH];
                        if (GetModuleFileNameW(modules[i], module_name, MAX_PATH) > 0) {
                            snprintf(msg, sizeof(msg), "Module %lu: 0x%p %ws", i, modules[i], module_name);
                        } else {
                            snprintf(msg, sizeof(msg), "Module %lu: 0x%p (failed to get name)", i, modules[i]);
                        }
                        reshade::log::message(reshade::log::level::info, msg);
                    }
                }
                g_process_attached.store(true);
                return FALSE;
            }
            display_commander::config::DisplayCommanderConfigManager::GetInstance().Initialize();
            display_commander::config::DisplayCommanderConfigManager::GetInstance().SetAutoFlushLogs(true);
            OutputDebugStringA("ReShade 111111");

            DetectMultipleReShadeVersions();

            // Registration successful - log version compatibility
            LogInfoDirect(
                "Display Commander v%s - ReShade addon registration successful (API version 17 supported)g_hmodule: "
                "0x%p current module: 0x%p",
                DISPLAY_COMMANDER_VERSION_STRING, g_hmodule, GetModuleHandleA(nullptr));

            // Register overlay early so it appears as a tab by default
            reshade::register_overlay("Display Commander", OnRegisterOverlayDisplayCommander);
            LogInfoDirect("Display Commander overlay registered");

            // Detect if we're loaded as a proxy DLL (dxgi.dll, d3d11.dll, d3d12.dll)
            // Similar to how ReShade detects this
            // Log to debug viewer early since log file may not be available yet
            OutputDebugStringA("[DisplayCommander] DllMain: DLL_PROCESS_ATTACH - Starting entry point detection\n");

            // Store entry point for logging later (after config system is initialized)
            // Convert wide string to UTF-8
            std::string entry_point_utf8;
            int utf8_size = WideCharToMultiByte(CP_UTF8, 0, entry_point.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (utf8_size > 0) {
                entry_point_utf8.resize(utf8_size - 1);  // -1 to exclude null terminator
                WideCharToMultiByte(CP_UTF8, 0, entry_point.c_str(), -1, entry_point_utf8.data(), utf8_size, nullptr,
                                    nullptr);
            } else {
                // Fallback to simple conversion if UTF-8 conversion fails
                entry_point_utf8 = std::string(entry_point.begin(), entry_point.end());
            }

            // Store entry point in a global variable to be saved later
            g_entry_point_to_save = entry_point_utf8;

            char debug_msg[512];
            snprintf(debug_msg, sizeof(debug_msg),
                     "[DisplayCommander] Entry point detected: %s (will be saved after initialization)\n",
                     entry_point_utf8.c_str());
            OutputDebugStringA(debug_msg);
            LogInfoDirect("Entry point detected: %s (will be saved after initialization)", entry_point_utf8.c_str());

            // Handle safemode after config system is initialized

            // Detect multiple ReShade versions AFTER successful registration to avoid interference
            // This prevents our module scanning from interfering with ReShade's internal module detection

            // Store module handle for pinning
            // Initialize QPC timing constants based on actual frequency
            utils::initialize_qpc_timing_constants();

            DoInitializationWithoutHwndSafe(h_module);
            DoInitializationWithoutHwnd(h_module);
            // display_commander::config::DisplayCommanderConfigManager::GetInstance().SetAutoFlushLogs(false);

            // Load addons from Plugins directory
            LoadAddonsFromPluginsDirectory();
            g_process_attached.store(true);
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
            if (!g_reshade_loaded.load()) {
                return TRUE;
            }
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

            // Clean up NVAPI instances before shutdown
            if (g_latencyManager) {
                g_latencyManager->Shutdown();
            }

            // Clean up NVAPI fullscreen prevention
            g_nvapiFullscreenPrevention.Cleanup();

            // Clean up fake NVAPI
            nvapi::g_fakeNvapiManager.Cleanup();

            // Clean up PresentMon
            presentmon::g_presentMonManager.StopWorker();

            // Shutdown DisplayCommander logger (must be last to capture all cleanup messages)
            display_commander::logger::Shutdown();

            // Note: reshade::unregister_addon() will automatically unregister all events and overlays
            // registered by this add-on, so manual unregistration is not needed and can cause issues
            // display_restore::RestoreAllIfEnabled(); // restore display settings on exit

            // Unpin the module before unregistration (only if we actually pinned it)
            if (g_module_pinned.load() && g_hmodule != nullptr) {
                if (FreeLibrary(g_hmodule) != 0) {
                    LogInfo("Module unpinned successfully: 0x%p", g_hmodule);
                } else {
                    DWORD error = GetLastError();
                    LogWarn("Failed to unpin module: 0x%p, Error: %lu", g_hmodule, error);
                }
                g_hmodule = nullptr;
                g_module_pinned.store(false);
            }

            reshade::unregister_addon(h_module);

            break;
    }

    return TRUE;
}

// CONTINUOUS RENDERING FUNCTIONS REMOVED - Focus spoofing is now handled by Win32 hooks

// CONTINUOUS RENDERING THREAD REMOVED - Focus spoofing is now handled by Win32 hooks
// This provides a much cleaner and more effective solution

// Auto-injection state
namespace {
HHOOK g_cbt_hook = nullptr;
std::atomic<LONGLONG> g_injection_start_time(0);
std::atomic<bool> g_injection_active(false);
constexpr LONGLONG INJECTION_DURATION_NS = 30LL * 1000000000LL;  // 30 seconds in nanoseconds

// Named event to signal injection is active (shared across processes)
HANDLE g_injection_active_event = nullptr;
HANDLE g_injection_stop_event = nullptr;

// Simple function to check if a PID was injected (by checking for named event)
bool IsPidInjected(DWORD pid) {
    wchar_t pid_event_name[64];
    swprintf_s(pid_event_name, L"Local\\DisplayCommander_Injected_%lu", pid);
    HANDLE hEvent = OpenEventW(SYNCHRONIZE, FALSE, pid_event_name);
    if (hEvent != nullptr) {
        CloseHandle(hEvent);
        return true;
    }
    return false;
}
}  // namespace

// CBT Hook procedure - called when windows are created
// This is just for NOTIFICATION - the actual DLL injection happens automatically
// when SetWindowsHookEx is called with dwThreadId=0 (system-wide hook).
// Windows automatically loads the DLL into target processes when hook events occur.
// This callback runs in the TARGET process after the DLL is already loaded.
LRESULT CALLBACK CBTProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // if nCode is less than 0, return the next hook
    if (nCode < 0) {
        return CallNextHookEx(g_cbt_hook, nCode, wParam, lParam);
    }
    OutputDebugStringA(std::format("CBTProc PID: {}", GetCurrentProcessId()).c_str());

    return CallNextHookEx(g_cbt_hook, nCode, wParam, lParam);
}

// Function to stop injection manually
void StopInjectionInternal() {
    // Signal stop event to notify any running Start process
    HANDLE stop_event = OpenEventW(EVENT_MODIFY_STATE, FALSE, INJECTION_STOP_EVENT_NAME);
    if (stop_event != nullptr) {
        SetEvent(stop_event);
        CloseHandle(stop_event);
    }

    if (g_cbt_hook != nullptr) {
        UnhookWindowsHookEx(g_cbt_hook);
        g_cbt_hook = nullptr;
        g_injection_active.store(false, std::memory_order_release);
        g_injection_start_time.store(0, std::memory_order_release);

        // Signal that injection is no longer active
        if (g_injection_active_event != nullptr) {
            ResetEvent(g_injection_active_event);
            CloseHandle(g_injection_active_event);
            g_injection_active_event = nullptr;
        }

        // Close stop event if we own it
        if (g_injection_stop_event != nullptr) {
            CloseHandle(g_injection_stop_event);
            g_injection_stop_event = nullptr;
        }

        OutputDebugStringA("Auto-injection stopped: CBT hook removed");
    }
}

// Cleanup thread to remove hook after 30 seconds
void StartInjectionCleanupThread() {
    std::thread([]() {
        Sleep(30000);  // 30 seconds
        StopInjectionInternal();
    }).detach();
}

// Internal function to start injection with optional duration limit
void StartInjectionInternal(bool forever) {
    OutputDebugStringA(
        std::format("StartInjectionInternal PID: {}, forever: {}", GetCurrentProcessId(), forever).c_str());

    // Initialize config system for logging
    display_commander::config::DisplayCommanderConfigManager::GetInstance().Initialize();
    display_commander::config::DisplayCommanderConfigManager::GetInstance().SetAutoFlushLogs(true);

    // Check if hook is already active
    if (g_injection_active.load(std::memory_order_acquire)) {
        OutputDebugStringA("Auto-injection already active, restarting timer");
        // Restart the timer
        g_injection_start_time.store(utils::get_now_ns(), std::memory_order_release);
        return;
    }

    // Install global CBT hook (thread ID 0 = system-wide)
    // This automatically injects the DLL into all processes when CBT events occur
    // The hook installation itself IS the injection mechanism - Windows loads the DLL automatically
    g_cbt_hook = SetWindowsHookEx(WH_CBT, CBTProc, g_hmodule, 0);

    if (g_cbt_hook == nullptr) {
        DWORD error = GetLastError();
        OutputDebugStringA(
            std::format("Failed to install CBT hook for auto-injection - Error: {} (0x{:X})", error, error).c_str());
        return;
    }

    // Mark injection as active and record start time
    g_injection_active.store(true, std::memory_order_release);
    g_injection_start_time.store(utils::get_now_ns(), std::memory_order_release);

    // Create named event to signal injection is active (shared across processes)
    g_injection_active_event = CreateEventW(nullptr, TRUE, TRUE, INJECTION_ACTIVE_EVENT_NAME);
    if (g_injection_active_event == nullptr) {
        DWORD error = GetLastError();
        OutputDebugStringA(
            std::format("Failed to create injection active event - Error: {} (0x{:X})", error, error).c_str());
    }

    if (forever) {
        // Create stop event for signaling from other processes
        g_injection_stop_event = CreateEventW(nullptr, TRUE, FALSE, INJECTION_STOP_EVENT_NAME);
        if (g_injection_stop_event == nullptr) {
            DWORD error = GetLastError();
            OutputDebugStringA(
                std::format("Failed to create injection stop event - Error: {} (0x{:X})", error, error).c_str());
        }

        OutputDebugStringA(
            "Auto-injection started: CBT hook installed, will inject into all new processes indefinitely");
        // Keep process alive to maintain the hook
        // The hook will remain active as long as this process is running
        // Check for stop signal periodically
        while (g_injection_active.load(std::memory_order_acquire)) {
            // Check if stop event was signaled (from another process calling Stop)
            if (g_injection_stop_event != nullptr) {
                DWORD wait_result = WaitForSingleObject(g_injection_stop_event, 1000);
                if (wait_result == WAIT_OBJECT_0) {
                    OutputDebugStringA("Stop signal received, stopping injection");
                    StopInjectionInternal();
                    break;
                }
            } else {
                Sleep(1000);  // Sleep in 1-second intervals if stop event creation failed
            }
        }
    } else {
        OutputDebugStringA(
            "Auto-injection started: CBT hook installed, will inject into all new processes for 30 seconds");
        // Start cleanup thread
        StartInjectionCleanupThread();
        Sleep(30000);  // 30 seconds
    }
}

// Helper structure for LoadLibrary injection
struct loading_data {
    WCHAR load_path[MAX_PATH] = L"";
    decltype(&GetLastError) GetLastError = nullptr;
    decltype(&LoadLibraryW) LoadLibraryW = nullptr;
    const WCHAR env_var_name[30] = L"RESHADE_DISABLE_LOADING_CHECK";
    const WCHAR env_var_value[2] = L"1";
    decltype(&SetEnvironmentVariableW) SetEnvironmentVariableW = nullptr;
};

// Loading thread function that runs in the target process
// Sets environment variable and then loads the DLL
static DWORD WINAPI LoadingThreadFunc(loading_data* arg) {
    arg->SetEnvironmentVariableW(arg->env_var_name, arg->env_var_value);
    if (arg->LoadLibraryW(arg->load_path) == NULL) {
        return arg->GetLastError();
    }
    return ERROR_SUCCESS;
}

// Helper function to get ReShade DLL path based on architecture
static std::wstring GetReShadeDllPath(bool is_wow64) {
    wchar_t documents_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_MYDOCUMENTS, nullptr, SHGFP_TYPE_CURRENT, documents_path))) {
        return L"";
    }

    std::filesystem::path documents_dir(documents_path);
    std::filesystem::path dc_reshade_dir = documents_dir / L"Display Commander" / L"Reshade";

    std::filesystem::path reshade_path;
#ifdef _WIN64
    reshade_path = is_wow64 ? (dc_reshade_dir / L"Reshade32.dll") : (dc_reshade_dir / L"Reshade64.dll");
#else
    reshade_path = dc_reshade_dir / L"Reshade32.dll";
#endif

    if (std::filesystem::exists(reshade_path)) {
        std::error_code ec;
        std::filesystem::path absolute_path = std::filesystem::canonical(reshade_path, ec);
        if (ec) {
            absolute_path = std::filesystem::absolute(reshade_path, ec);
            if (ec) {
                absolute_path = reshade_path;
            }
        }
        return absolute_path.wstring();
    }

    return L"";
}

// Helper function to inject ReShade DLL into a process using LoadLibrary
static bool InjectIntoProcess(DWORD pid, const std::wstring& dll_path) {
    // Open target process
    HANDLE remote_process = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION,
        FALSE, pid);

    if (remote_process == nullptr) {
        DWORD error = GetLastError();
        OutputDebugStringA(
            std::format("Failed to open target process (PID {}): Error {} (0x{:X})", pid, error, error).c_str());
        return false;
    }

    // Check process architecture
    BOOL remote_is_wow64 = FALSE;
    IsWow64Process(remote_process, &remote_is_wow64);

#ifdef _WIN64
    if (remote_is_wow64 != FALSE) {
        CloseHandle(remote_process);
        OutputDebugStringA(
            std::format("Process architecture mismatch: 32-bit process, but injector is 64-bit (PID {})", pid).c_str());
        return false;
    }
#else
    if (remote_is_wow64 == FALSE) {
        CloseHandle(remote_process);
        OutputDebugStringA(
            std::format("Process architecture mismatch: 64-bit process, but injector is 32-bit (PID {})", pid).c_str());
        return false;
    }
#endif

    // Setup loading data
    loading_data arg;
    wcscpy_s(arg.load_path, dll_path.c_str());
    arg.GetLastError = GetLastError;
    arg.LoadLibraryW = LoadLibraryW;
    arg.SetEnvironmentVariableW = SetEnvironmentVariableW;

    // Allocate memory in target process
    LPVOID load_param =
        VirtualAllocEx(remote_process, nullptr, sizeof(arg), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (load_param == nullptr) {
        DWORD error = GetLastError();
        OutputDebugStringA(
            std::format("Failed to allocate memory in target process (PID {}): Error {} (0x{:X})", pid, error, error)
                .c_str());
        CloseHandle(remote_process);
        return false;
    }

    // Write loading data to target process
    if (!WriteProcessMemory(remote_process, load_param, &arg, sizeof(arg), nullptr)) {
        DWORD error = GetLastError();
        OutputDebugStringA(
            std::format("Failed to write loading data to target process (PID {}): Error {} (0x{:X})", pid, error, error)
                .c_str());
        VirtualFreeEx(remote_process, load_param, 0, MEM_RELEASE);
        CloseHandle(remote_process);
        return false;
    }

    // Note: To set RESHADE_DISABLE_LOADING_CHECK in target process, we would need to inject
    // the LoadingThreadFunc code. For now, we use direct LoadLibrary injection.
    // The environment variable can be set in the target process environment before starting it.

    // Execute LoadLibrary in target process
    HANDLE load_thread = CreateRemoteThread(
        remote_process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(arg.LoadLibraryW), load_param, 0, nullptr);

    if (load_thread == nullptr) {
        DWORD error = GetLastError();
        OutputDebugStringA(std::format("Failed to create remote thread in target process (PID {}): Error {} (0x{:X})",
                                       pid, error, error)
                               .c_str());
        VirtualFreeEx(remote_process, load_param, 0, MEM_RELEASE);
        CloseHandle(remote_process);
        return false;
    }

    // Wait for loading to finish
    WaitForSingleObject(load_thread, INFINITE);

    // Check if injection was successful
    DWORD exit_code;
    bool success = false;
    if (GetExitCodeThread(load_thread, &exit_code) && exit_code != NULL) {
        success = true;
        OutputDebugStringA(std::format("Successfully injected ReShade into process (PID {})", pid).c_str());
    } else {
        OutputDebugStringA(
            std::format("Failed to inject Display Commander into process (PID {}): LoadLibrary returned NULL", pid)
                .c_str());
    }

    // Cleanup
    CloseHandle(load_thread);
    VirtualFreeEx(remote_process, load_param, 0, MEM_RELEASE);
    CloseHandle(remote_process);

    return success;
}

// Wait for a process with given exe name to start, then inject ReShade DLL
// Waits forever and injects into every new process that starts with the given exe name
// Uses the same injection method as StartAndInject (InjectIntoProcess with LoadLibrary)
static void WaitForProcessAndInject(const std::wstring& exe_name) {
    // Reset stop flag when starting
    g_wait_and_inject_stop.store(false);

    OutputDebugStringA(std::format("Waiting for process: {} (will inject into all new instances)",
                                   std::string(exe_name.begin(), exe_name.end()))
                           .c_str());

    // Use array-based tracking like ReShade does (more efficient than set for PIDs)
    // PIDs are typically in the range 0-65535, so this array covers most cases
    std::array<bool, 65536> process_seen = {};

    // First, enumerate existing processes to mark them as seen
    HANDLE initial_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (initial_snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W process_entry = {};
        process_entry.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(initial_snapshot, &process_entry)) {
            int existing_count = 0;
            do {
                if (_wcsicmp(process_entry.szExeFile, exe_name.c_str()) == 0) {
                    DWORD pid = process_entry.th32ProcessID;
                    if (pid < process_seen.size()) {
                        process_seen[pid] = true;
                        existing_count++;
                    }
                }
            } while (Process32NextW(initial_snapshot, &process_entry));

            if (existing_count > 0) {
                OutputDebugStringA(std::format("Found {} existing process(es) with name '{}', will wait for new ones",
                                               existing_count, std::string(exe_name.begin(), exe_name.end()))
                                       .c_str());
                std::cout << "Found " << existing_count << " existing process(es) with name '"
                          << std::string(exe_name.begin(), exe_name.end()) << "', will wait for new ones" << std::endl;
            }
        }
        CloseHandle(initial_snapshot);
    }

    // Wait forever and inject into every new process
    while (!g_wait_and_inject_stop.load()) {
        // Create process snapshot
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            Sleep(10);  // Very short sleep before retrying (like ReShade)
            continue;
        }

        PROCESSENTRY32W process_entry = {};
        process_entry.dwSize = sizeof(PROCESSENTRY32W);

        // Check all processes to find new ones matching our target
        if (Process32FirstW(snapshot, &process_entry)) {
            do {
                DWORD pid = process_entry.th32ProcessID;
                // Debug: Convert process name to narrow string for logging
                int process_name_size =
                    WideCharToMultiByte(CP_ACP, 0, process_entry.szExeFile, -1, nullptr, 0, nullptr, nullptr);
                if (process_name_size > 0) {
                    std::vector<char> process_name_buf(process_name_size);
                    WideCharToMultiByte(CP_ACP, 0, process_entry.szExeFile, -1, process_name_buf.data(),
                                        process_name_size, nullptr, nullptr);
                    if (!process_seen[pid]) {
                        OutputDebugStringA(std::format("Checking process: {} (PID {})", process_name_buf.data(),
                                                       process_entry.th32ProcessID)
                                               .c_str());
                    }
                }

                // Check if this process matches our target
                if (_wcsicmp(process_entry.szExeFile, exe_name.c_str()) == 0) {
                    // Skip if PID is out of range (shouldn't happen, but be safe)
                    if (pid >= process_seen.size()) {
                        continue;
                    }

                    // Only inject into processes we haven't seen before (new processes)
                    if (!process_seen[pid]) {
                        process_seen[pid] = true;  // Mark as seen immediately to avoid duplicate injection attempts

                        OutputDebugStringA(std::format("Found new process: {} (PID {})",
                                                       std::string(exe_name.begin(), exe_name.end()), pid)
                                               .c_str());
                        std::cout << "Found new process: " << std::string(exe_name.begin(), exe_name.end()) << " (PID "
                                  << pid << ")" << std::endl;

                        // Wait a bit for the process to initialize (same as StartAndInject)
                        // Sleep(500);

                        // Check process architecture (same as StartAndInject)
                        BOOL is_wow64 = FALSE;
                        HANDLE h_process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
                        if (h_process != nullptr) {
                            IsWow64Process(h_process, &is_wow64);
                            CloseHandle(h_process);
                        }

                        // Get ReShade DLL path (same as StartAndInject)
                        std::wstring dll_path = GetReShadeDllPath(is_wow64 != FALSE);
                        if (dll_path.empty()) {
                            OutputDebugStringA(
                                std::format("Failed to find ReShade DLL path for process (PID {}), continuing to wait",
                                            pid)
                                    .c_str());
                            std::cout << "Failed to find ReShade DLL path for process (PID " << pid
                                      << "), continuing to wait" << std::endl;
                            // Continue waiting - don't return on error
                            continue;
                        }

                        // Inject into the process using the same method as StartAndInject
                        bool success = InjectIntoProcess(pid, dll_path);
                        if (success) {
                            OutputDebugStringA(std::format("Successfully injected into process (PID {})", pid).c_str());
                            std::cout << "Successfully injected into process (PID " << pid << ")" << std::endl;
                        } else {
                            OutputDebugStringA(
                                std::format("Failed to inject into process (PID {}), continuing to wait", pid).c_str());
                            std::cout << "Failed to inject into process (PID " << pid << "), continuing to wait"
                                      << std::endl;
                        }
                        // Continue waiting for more processes - never return
                    }
                }
                process_seen[pid] = true;
            } while (Process32NextW(snapshot, &process_entry));
        }

        CloseHandle(snapshot);

        // Don't sleep (or sleep very little) to catch new processes quickly, like ReShade does
        // ReShade's comment: "don't sleep to make sure we catch all new processes"
        Sleep(10);  // Very short sleep to avoid 100% CPU usage
    }

    OutputDebugStringA("WaitForProcessAndInject: Stopped");
    std::cout << "WaitForProcessAndInject: Stopped" << std::endl;
}

// RunDLL entry point for rundll32.exe compatibility
// Allows calling: rundll32.exe zzz_display_commander.addon64,RunDLL_DllMain
// This installs a system-wide CBT hook - Windows automatically loads the DLL into
// all processes when CBT events occur (window creation, etc.). The hook installation
// itself is the injection mechanism. CBTProc is just for notification.
extern "C" __declspec(dllexport) void CALLBACK RunDLL_DllMain(HWND hwnd, HINSTANCE hInst, LPSTR lpszCmdLine,
                                                              int nCmdShow) {
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(hInst);
    UNREFERENCED_PARAMETER(lpszCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    StartInjectionInternal(false);  // 30 seconds
}

// RunDLL entry point for 30-second injection
// Allows calling: rundll32.exe zzz_display_commander.addon64,Service30
extern "C" __declspec(dllexport) void CALLBACK Service30(HWND hwnd, HINSTANCE hInst, LPSTR lpszCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(hInst);
    UNREFERENCED_PARAMETER(lpszCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    StartInjectionInternal(false);  // 30 seconds
}

// RunDLL entry point for indefinite injection service
// Allows calling: rundll32.exe zzz_display_commander.addon64,Start
extern "C" __declspec(dllexport) void CALLBACK Start(HWND hwnd, HINSTANCE hInst, LPSTR lpszCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(hInst);
    UNREFERENCED_PARAMETER(lpszCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    StartInjectionInternal(true);  // Forever
}

// RunDLL entry point to stop injection service
// Allows calling: rundll32.exe zzz_display_commander.addon64,Stop
extern "C" __declspec(dllexport) void CALLBACK Stop(HWND hwnd, HINSTANCE hInst, LPSTR lpszCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(hInst);
    UNREFERENCED_PARAMETER(lpszCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    // Stop WaitAndInject if it's running
    g_wait_and_inject_stop.store(true);
    OutputDebugStringA("Stop: Signaled WaitAndInject to stop");

    StopInjectionInternal();
}

// RunDLL entry point to start a game and inject into it
// Allows calling: rundll32.exe zzz_display_commander.addon64,StartAndInject "C:\Path\To\game.exe"
// The exe path should be passed as a command line argument
extern "C" __declspec(dllexport) void CALLBACK StartAndInject(HWND hwnd, HINSTANCE hInst, LPSTR lpszCmdLine,
                                                              int nCmdShow) {
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(hInst);
    UNREFERENCED_PARAMETER(nCmdShow);

    // Initialize config system for logging
    display_commander::config::DisplayCommanderConfigManager::GetInstance().Initialize();
    display_commander::config::DisplayCommanderConfigManager::GetInstance().SetAutoFlushLogs(true);

    // Parse exe path from command line
    std::string exe_path_ansi;
    if (lpszCmdLine != nullptr && strlen(lpszCmdLine) > 0) {
        // Remove quotes if present
        exe_path_ansi = lpszCmdLine;
        if (exe_path_ansi.length() >= 2 && exe_path_ansi.front() == '"' && exe_path_ansi.back() == '"') {
            exe_path_ansi = exe_path_ansi.substr(1, exe_path_ansi.length() - 2);
        }
    }

    if (exe_path_ansi.empty()) {
        OutputDebugStringA(
            "StartAndInject: No exe path provided. Usage: rundll32.exe zzz_display_commander.addon64,StartAndInject "
            "\"C:\\Path\\To\\game.exe\"");
        return;
    }

    // Convert to wide string
    int size_needed = MultiByteToWideChar(CP_ACP, 0, exe_path_ansi.c_str(), -1, nullptr, 0);
    if (size_needed <= 0) {
        OutputDebugStringA("StartAndInject: Failed to convert exe path to wide string");
        return;
    }

    std::vector<wchar_t> exe_path_wide(size_needed);
    MultiByteToWideChar(CP_ACP, 0, exe_path_ansi.c_str(), -1, exe_path_wide.data(), size_needed);
    std::wstring exe_path(exe_path_wide.data());

    // Check if file exists
    if (!std::filesystem::exists(exe_path)) {
        OutputDebugStringA(std::format("StartAndInject: Exe file not found: {}", exe_path_ansi).c_str());
        return;
    }

    OutputDebugStringA(std::format("StartAndInject: Starting process: {}", exe_path_ansi).c_str());

    // Start the process
    STARTUPINFOW si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);

    std::wstring command_line = L"\"" + exe_path + L"\"";

    if (!CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        DWORD error = GetLastError();
        OutputDebugStringA(
            std::format("StartAndInject: Failed to start process: Error {} (0x{:X})", error, error).c_str());
        return;
    }

    // Close thread handle (we only need process handle)
    CloseHandle(pi.hThread);

    OutputDebugStringA(std::format("StartAndInject: Process started (PID {})", pi.dwProcessId).c_str());

    // Wait a bit for the process to initialize
    Sleep(500);

    // Check process architecture
    BOOL is_wow64 = FALSE;
    IsWow64Process(pi.hProcess, &is_wow64);

    // Get ReShade DLL path
    std::wstring dll_path = GetReShadeDllPath(is_wow64 != FALSE);
    if (dll_path.empty()) {
        OutputDebugStringA("StartAndInject: Failed to find ReShade DLL path");
        CloseHandle(pi.hProcess);
        return;
    }

    // Inject into the process
    bool success = InjectIntoProcess(pi.dwProcessId, dll_path);

    CloseHandle(pi.hProcess);

    if (success) {
        OutputDebugStringA(
            std::format("StartAndInject: Successfully started and injected into process (PID {})", pi.dwProcessId)
                .c_str());
    } else {
        OutputDebugStringA(
            std::format("StartAndInject: Failed to inject into process (PID {})", pi.dwProcessId).c_str());
    }
}

// RunDLL entry point to wait for process and inject
// Allows calling: rundll32.exe zzz_display_commander.addon64,WaitAndInject "game.exe"
// The exe name should be passed as a command line argument
extern "C" __declspec(dllexport) void CALLBACK WaitAndInject(HWND hwnd, HINSTANCE hInst, LPSTR lpszCmdLine,
                                                             int nCmdShow) {
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(hInst);
    UNREFERENCED_PARAMETER(nCmdShow);

    // Initialize config system for logging
    display_commander::config::DisplayCommanderConfigManager::GetInstance().Initialize();
    display_commander::config::DisplayCommanderConfigManager::GetInstance().SetAutoFlushLogs(true);

    // Parse exe name from command line
    std::string exe_name_ansi;
    if (lpszCmdLine != nullptr && strlen(lpszCmdLine) > 0) {
        // Remove quotes if present
        exe_name_ansi = lpszCmdLine;
        if (exe_name_ansi.length() >= 2 && exe_name_ansi.front() == '"' && exe_name_ansi.back() == '"') {
            exe_name_ansi = exe_name_ansi.substr(1, exe_name_ansi.length() - 2);
        }
    }

    if (exe_name_ansi.empty()) {
        OutputDebugStringA(
            "WaitAndInject: No exe name provided. Usage: rundll32.exe zzz_display_commander.addon64,WaitAndInject "
            "\"game.exe\"");
        return;
    }

    // Convert to wide string
    int size_needed = MultiByteToWideChar(CP_ACP, 0, exe_name_ansi.c_str(), -1, nullptr, 0);
    if (size_needed <= 0) {
        OutputDebugStringA("WaitAndInject: Failed to convert exe name to wide string");
        return;
    }

    std::vector<wchar_t> exe_name_wide(size_needed);
    MultiByteToWideChar(CP_ACP, 0, exe_name_ansi.c_str(), -1, exe_name_wide.data(), size_needed);
    std::wstring exe_name(exe_name_wide.data());

    // Extract just the filename if a path was provided (e.g., ".\BPSR_STREAM.exe" -> "BPSR_STREAM.exe")
    std::filesystem::path exe_path(exe_name);
    std::wstring exe_name_only = exe_path.filename().wstring();

    OutputDebugStringA(std::format("WaitAndInject: Waiting for process: {} (comparing against: {})", exe_name_ansi,
                                   std::string(exe_name_only.begin(), exe_name_only.end()))
                           .c_str());
    std::cout << "WaitAndInject: Waiting for process: " << exe_name_ansi
              << " (comparing against: " << std::string(exe_name_only.begin(), exe_name_only.end()) << ")" << std::endl;

    // Wait forever and inject into every new process that starts
    WaitForProcessAndInject(exe_name_only);
}
