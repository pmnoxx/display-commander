#include "main_tab_settings.hpp"
#include "../adhd_multi_monitor/adhd_simple_api.hpp"
#include "../config/display_commander_config.hpp"
#include "../hooks/windows_hooks/api_hooks.hpp"
#include "../utils.hpp"
#include "../utils/logging.hpp"

#include <windows.h>

std::atomic<float> s_system_volume_percent{100.f};
std::atomic<float> s_game_volume_percent{100.f};

namespace settings {

MainTabSettings::MainTabSettings()
    : window_mode(
          "WindowMode", static_cast<int>(WindowMode::kNoChanges),
          // Labels in WindowMode order (the stored value is the enum value).
          {"No changes", "Borderless fullscreen", "Borderless windowed", "Prevent exclusive fullscreen / no resize"},
          "DisplayCommander"),
      aspect_index("AspectRatio", 3, {"3:2", "4:3", "16:10", "16:9", "19:9", "19.5:9", "21:9", "21.5:9", "32:9"},
                   "DisplayCommander"),  // Default to 16:9
      window_aspect_width("WindowAspectWidth", 0,
                          {"Display Width", "3840", "2560", "1920", "1600", "1280", "1080", "900", "720"},
                          "DisplayCommander"),
      alignment("Alignment", 0, {"Center", "Top Left", "Top Right", "Bottom Left", "Bottom Right"}, "DisplayCommander"),
      fps_limiter_enabled("FpsLimiter.Enabled", true, "DisplayCommander"),
      fps_limiter_mode("FpsLimiter.Mode", 0, {"Default", "Reflex (low latency)"}, "DisplayCommander"),
      fps_limit("FpsLimiter.TargetFps", 0.0f, 0.0f, 1000.0f, "DisplayCommander", true),
      fps_limit_background("FpsLimiter.BackgroundTargetFps", 60.0f, 0.0f, 1000.0f, "DisplayCommander", true),
      background_fps_enabled("FpsLimiter.BackgroundLimitEnabled", false, "DisplayCommander"),
      fps_limiter_fg2_enabled("FpsLimiter.Fg2Enabled", false, "DisplayCommander"),
      fps_limiter_fg2_target_boost_percent("FpsLimiter.Fg2TargetBoostPercent", 0.3f, -3.0f, 3.0f,
                                           "DisplayCommander"),
      suppress_reflex_sleep("Reflex.SuppressSleep", false, "DisplayCommander"),
      inject_reflex("Reflex.InjectionEnabled", false, "DisplayCommander"),
      onpresent_sync_low_latency_ratio(
          "FpsLimiter.OnPresentSyncLowLatencyRatio", 0,
          {"100% Display / 0% Input (default)", "87.5% Display / 12.5% Input", "75% Display / 25% Input",
           "62.5% Display / 37.5% Input", "50% Display / 50% Input", "37.5% Display / 62.5% Input",
           "25% Display / 75% Input", "12.5% Display / 87.5% Input", "0% Display / 100% Input"},
          "DisplayCommander"),  // Default to 100% Display / 0% Input (current behavior)
      onpresent_reflex_mode("Reflex.OnPresentSyncMode", static_cast<int>(OnPresentReflexMode::kLowLatency),
                            {"Low latency", "Low Latency + boost (can cause issues)", "Off", "Game Defaults"}, "DisplayCommander"),
      reflex_limiter_reflex_mode("Reflex.ReflexLimiterMode", static_cast<int>(OnPresentReflexMode::kLowLatency),
                                 {"Low latency", "Low Latency + boost (can cause issues)", "Off", "Game Defaults"}, "DisplayCommander"),
      reflex_disabled_limiter_mode("Reflex.LimiterOffMode", static_cast<int>(OnPresentReflexMode::kOff),
                                   {"Low latency", "Low Latency + boost (can cause issues)", "Off", "Game Defaults"}, "DisplayCommander"),
      pcl_stats_enabled("Reflex.PclStatsEnabled", true, "DisplayCommander"),
      use_reflex_markers_as_fps_limiter("FpsLimiter.ReflexMarkersEnabled", true, "DisplayCommander"),
      reflex_fps_limiter_max_queued_frames("FpsLimiter.ReflexMaxQueuedFrames", 2,
                                           {"Game default", "1", "2", "3", "4", "5", "6",}, "DisplayCommander"),
      native_reflex_fps_preset("FpsLimiter.NativeReflexPreset", static_cast<int>(FpsLimiterPreset::kLowLatencyNativePacing),
                               {"Low latency mode - pace game simulation thread only (bad frame pacing)",
                                "DCPaceLock(q=1) (recommended, low latency, good frame pacing)",
                                "DCPaceLock(q=2) (default, +1 frame time latency penalty, use if q=1 is not good enough)",
                                "DCPaceLock(q=3) (+2 frame time latency penalty, use if q=2 is not good enough)",
                                "Pace native rendered frames only (pre-FG, stable at latency penalty)",
                                "Pace generated Frames",
                                "Custom (configure manually)",
                                "Low latency mode - experimental",},
                               "DisplayCommander"),
      use_streamline_proxy_fps_limiter("FpsLimiter.StreamlineProxyEnabled", false, "DisplayCommander"),
      native_pacing_sim_start_only("FpsLimiter.NativePacingSimStartOnly", false, "DisplayCommander"),
      delay_present_start_after_sim_enabled("FpsLimiter.DelayPresentStartAfterSimEnabled", false, "DisplayCommander"),
      delay_present_start_frames("FpsLimiter.DelayPresentStartFrames", 1.0f, 0.0f, 3.0f, "DisplayCommander"),
      safe_mode_fps_limiter("FpsLimiter.SafeModeEnabled", false, "DisplayCommander"),
      selected_reshade_runtime_index("ReShade.SelectedRuntimeIndex", 0, 0, 31, "DisplayCommander"),
      prevent_tearing("PreventTearing", false, "DisplayCommander"),
      audio_mute("Mute", false, "DisplayCommander"),
      mute_in_background("MuteInBackground", false, "DisplayCommander"),
      mute_in_background_if_other_audio("MuteInBackgroundIfOtherAudio", false, "DisplayCommander"),
      enable_default_chords("DefaultChordsEnabled", true, "DisplayCommander"),
      guide_button_solo_ui_toggle_only("GuideButtonSoloUiToggleOnly", true, "DisplayCommander"),
      keyboard_input_blocking("KeyboardBlockingMode", static_cast<int>(InputBlockingMode::kEnabledInBackground),
                              {"Off", "Always", "When game is not focused"}, "DisplayCommander"),
      mouse_input_blocking("MouseBlockingMode", static_cast<int>(InputBlockingMode::kEnabledInBackground),
                           {"Off", "Always", "When game is not focused", "While a controller is connected"},
                           "DisplayCommander"),
      gamepad_input_blocking("GamepadBlockingMode", static_cast<int>(InputBlockingMode::kDisabled),
                             {"Off", "Always", "When game is not focused"}, "DisplayCommander"),
      clip_cursor_enabled("ClipCursorEnabled", false, "DisplayCommander"),
      unclip_cursor_enabled("UnclipCursorEnabled", false, "DisplayCommander"),
      show_performance_overlay("ShowPerformanceOverlay", false, "DisplayCommander"),
      show_fps_counter("Overlay.ShowFpsCounter", true, "DisplayCommander"),
      show_native_fps("Overlay.ShowNativeFps", false, "DisplayCommander"),
      show_refresh_rate("Overlay.ShowRefreshRate", false, "DisplayCommander"),
      show_flip_status("Overlay.ShowFlipStatus", false, "DisplayCommander"),
      show_vrr_status("Overlay.ShowVrrStatus", false, "DisplayCommander"),
      vrr_debug_mode("Overlay.ShowVrrDebug", false, "DisplayCommander"),
      show_display_commander_ui("ShowDisplayCommanderUi", false, "DisplayCommander"),
      display_commander_ui_window_x("Window.PositionXPx", 100.0f, 0.0f, 10000.0f, "DisplayCommander"),
      display_commander_ui_window_y("Window.PositionYPx", 100.0f, 0.0f, 10000.0f, "DisplayCommander"),
      overlay_label_mode("Overlay.LabelMode", static_cast<int>(OverlayLabelMode::kFull),
                         {"No labels", "Short labels", "Full labels"}, "DisplayCommander"),
      show_clock("Overlay.ShowClock", false, "DisplayCommander"),
      show_frame_time_graph("Overlay.ShowFrameTimeGraph", true, "DisplayCommander"),
      show_frame_time_stats("Overlay.ShowFrameTimeStats", false, "DisplayCommander"),
      show_native_frame_time_graph("Overlay.ShowNativeFrameTimeGraph", false, "DisplayCommander"),
      show_cpu_usage("Overlay.ShowCpuUsage", false, "DisplayCommander"),
      show_overlay_cpu_process_load("Overlay.ShowCpuProcessLoad", false, "DisplayCommander"),
      show_overlay_cpu_system_load("Overlay.ShowCpuSystemLoad", false, "DisplayCommander"),
      show_nvapi_latency_stats("ShowNvapiLatencyStats", true, "DisplayCommander"),
      show_overlay_nvapi_sim_duration("Overlay.ShowNvapiSimDuration", false, "DisplayCommander"),
      show_overlay_nvapi_sim_end_to_rs_start("Overlay.ShowNvapiSimEndToRenderSubmitStart", false, "DisplayCommander"),
      show_overlay_nvapi_rs_submit_duration("Overlay.ShowNvapiRenderSubmitDuration", false, "DisplayCommander"),
      show_overlay_nvapi_rs_start_to_present_start("Overlay.ShowNvapiRenderSubmitStartToPresentStart", false,
                                                   "DisplayCommander"),
      show_overlay_nvapi_rs_end_to_present_start("Overlay.ShowNvapiRenderSubmitEndToPresentStart", false,
                                                 "DisplayCommander"),
      show_overlay_nvapi_present_phase_duration("Overlay.ShowNvapiPresentPhaseDuration", false, "DisplayCommander"),
      show_overlay_nvapi_present_end_to_rs_end("Overlay.ShowNvapiPresentEndToRenderSubmitEnd", false, "DisplayCommander"),
      show_overlay_nvapi_gpu_active_ms("Overlay.ShowNvapiGpuActiveTime", false, "DisplayCommander"),
      show_overlay_nvapi_latency_jitter_abs("Overlay.ShowNvapiAbsoluteLatencyJitter", true, "DisplayCommander"),
      show_overlay_nvapi_setlatencymarker_threads("Overlay.ShowNvapiSetLatencyMarkerThreads", false,
                                                  "DisplayCommander"),
      show_fg_mode("Overlay.ShowFgMode", false, "DisplayCommander"),
      show_overlay_resolution("Overlay.ShowDlssInternalResolution", false, "DisplayCommander"),
      show_dlss_status("Overlay.ShowDlssStatus", false, "DisplayCommander"),
      show_dlss_quality_preset("Overlay.ShowDlssQualityPreset", false, "DisplayCommander"),
      show_driver_dlss_sr_preset("Overlay.ShowDriverDlssSuperResolutionPreset", false, "DisplayCommander"),
      show_driver_dlss_rr_preset("Overlay.ShowDriverDlssRayReconstructionPreset", false, "DisplayCommander"),
      show_fps_limiter_src("Overlay.ShowFpsLimiterSource", false, "DisplayCommander"),
      show_fps_limiter_late_frames_pct("Overlay.ShowFpsLimiterLateFramesPercent", false, "DisplayCommander"),
      show_playtime("Overlay.ShowPlaytime", false, "DisplayCommander"),
      show_overlay_vu_bars("Overlay.ShowVuBars", false, "DisplayCommander"),
      show_overlay_vram("Overlay.ShowVram", false, "DisplayCommander"),
      show_overlay_ram("Overlay.ShowRam", false, "DisplayCommander"),
      overlay_background_alpha("Overlay.BackgroundAlpha", 0.6f, 0.0f, 1.0f, "DisplayCommander"),
      overlay_chart_alpha("Overlay.ChartAlpha", 0.0f, 0.0f, 1.0f, "DisplayCommander"),
      overlay_graph_scale("Overlay.GraphScale", 2.0f, 0.5f, 10.0f, "DisplayCommander"),
      overlay_graph_max_scale("Overlay.GraphMaxScale", 4.0f, 2.0f, 10.0f, "DisplayCommander"),
      overlay_vertical_spacing("Overlay.VerticalSpacingPx", 0.0f, 0.0f,
                               static_cast<float>(GetSystemMetrics(SM_CYSCREEN)), "DisplayCommander"),
      overlay_horizontal_spacing("Overlay.HorizontalSpacingPx", 0.0f, 0.0f,
                                 static_cast<float>(GetSystemMetrics(SM_CXSCREEN)), "DisplayCommander"),
      gpu_measurement_enabled("GpuMeasurementEnabled", 1, 0, 1, "DisplayCommander"),
      target_extended_display_device_id("TargetExtendedDisplayDeviceId", "", "DisplayCommander"),
      game_window_extended_display_device_id("GameWindowExtendedDisplayDeviceId", "", "DisplayCommander"),
      selected_extended_display_device_id("SelectedExtendedDisplayDeviceId", "", "DisplayCommander"),
      adhd_single_monitor_enabled_for_game_display("AdhdSingleMonitorEnabledForGameDisplay", false,
                                                   "DisplayCommander"),
      adhd_multi_monitor_enabled("AdhdMultiMonitorEnabled", false, "DisplayCommander"),
      screensaver_mode("ScreensaverMode", static_cast<int>(ScreensaverMode::kDefault),
                       {"Default", "In foreground", "Always"}, "DisplayCommander"),
      frame_time_mode("FrameTimeMode", static_cast<int>(FrameTimeMode::kPresent),
                      {"Frame Present Time", "Frame Start Time (input)",
                       "Frame Display Time later (Present or GPU Completion whichever comes later)"},
                      "DisplayCommander"),
      advanced_settings_enabled("AdvancedSettingsEnabled", false, "DisplayCommander"),
      log_level("LogLevel", 0, {"Log everything", "Info", "Warning", "Error Only"}, "DisplayCommander"),
      show_hotkeys_tab("ShowHotkeysTab", true, "DisplayCommander"),
      show_important_info_tab("ShowImportantInfoTab", false, "DisplayCommander"),
      show_controller_tab("ShowControllerTab", false, "DisplayCommander"),
      show_reshade_tab("ShowReshadeTab", false, "DisplayCommander"),
      vulkan_nvll_hooks_enabled("Vulkan.NvLowLatencyHooksEnabled", true, "DisplayCommander"),
      vulkan_vk_loader_hooks_enabled("Vulkan.LoaderHooksEnabled", true, "DisplayCommander"),
      vulkan_injected_reflex_enabled("VulkanInjectedReflexEnabled", false, "DisplayCommander"),
      vulkan_inject_extensions_enabled("VulkanInjectExtensionsEnabled", false, "DisplayCommander"),
      auto_reshade_config_backup("ReShade.ConfigBackupEnabled", false, "DisplayCommander") {
    // Initialize the all_settings_ vector
    all_settings_ = {
        &window_mode,
        &aspect_index,
        &window_aspect_width,
        &alignment,
        &fps_limiter_enabled,
        &fps_limiter_mode,
        &fps_limit,
        &fps_limit_background,
        &background_fps_enabled,
        &fps_limiter_fg2_enabled,
        &fps_limiter_fg2_target_boost_percent,
        &suppress_reflex_sleep,
        &inject_reflex,
        &onpresent_sync_low_latency_ratio,
        &onpresent_reflex_mode,
        &reflex_limiter_reflex_mode,
        &reflex_disabled_limiter_mode,
        &pcl_stats_enabled,
        &use_reflex_markers_as_fps_limiter,
        &reflex_fps_limiter_max_queued_frames,
        &native_reflex_fps_preset,
        &use_streamline_proxy_fps_limiter,
        &native_pacing_sim_start_only,
        &delay_present_start_after_sim_enabled,
        &delay_present_start_frames,
        &safe_mode_fps_limiter,
        &selected_reshade_runtime_index,
        &prevent_tearing,
        &audio_mute,
        &mute_in_background,
        &mute_in_background_if_other_audio,
        &enable_default_chords,
        &guide_button_solo_ui_toggle_only,
        &keyboard_input_blocking,
        &mouse_input_blocking,
        &gamepad_input_blocking,
        &clip_cursor_enabled,
        &unclip_cursor_enabled,
        &show_performance_overlay,
        &show_fps_counter,
        &show_native_fps,
        &show_refresh_rate,
        &show_flip_status,
        &show_vrr_status,
        &vrr_debug_mode,
        &show_display_commander_ui,
        &display_commander_ui_window_x,
        &display_commander_ui_window_y,
        &overlay_label_mode,
        &show_clock,
        &show_frame_time_graph,
        &show_frame_time_stats,
        &show_native_frame_time_graph,
        &show_cpu_usage,
        &show_overlay_cpu_process_load,
        &show_overlay_cpu_system_load,
        &show_nvapi_latency_stats,
        &show_overlay_nvapi_sim_duration,
        &show_overlay_nvapi_sim_end_to_rs_start,
        &show_overlay_nvapi_rs_submit_duration,
        &show_overlay_nvapi_rs_start_to_present_start,
        &show_overlay_nvapi_rs_end_to_present_start,
        &show_overlay_nvapi_present_phase_duration,
        &show_overlay_nvapi_present_end_to_rs_end,
        &show_overlay_nvapi_gpu_active_ms,
        &show_overlay_nvapi_latency_jitter_abs,
        &show_overlay_nvapi_setlatencymarker_threads,
        &show_fg_mode,
        &show_overlay_resolution,
        &show_dlss_status,
        &show_dlss_quality_preset,
        &show_driver_dlss_sr_preset,
        &show_driver_dlss_rr_preset,
        &show_fps_limiter_src,
        &show_fps_limiter_late_frames_pct,
        &show_playtime,
        &show_overlay_vu_bars,
        &show_overlay_vram,
        &show_overlay_ram,
        &overlay_background_alpha,
        &overlay_chart_alpha,
        &overlay_graph_scale,
        &overlay_graph_max_scale,
        &overlay_vertical_spacing,
        &overlay_horizontal_spacing,
        &gpu_measurement_enabled,
        &frame_time_mode,
        &target_extended_display_device_id,
        &game_window_extended_display_device_id,
        &selected_extended_display_device_id,
        &adhd_single_monitor_enabled_for_game_display,
        &adhd_multi_monitor_enabled,
        &screensaver_mode,
        &advanced_settings_enabled,
        &log_level,
        &show_hotkeys_tab,
        &show_important_info_tab,
        &show_controller_tab,
        &show_reshade_tab,
        &vulkan_nvll_hooks_enabled,
        &vulkan_vk_loader_hooks_enabled,
        &vulkan_injected_reflex_enabled,
        &vulkan_inject_extensions_enabled,
        &auto_reshade_config_backup,
    };
}

void GetNativeReflexPresetOverrides(FpsLimiterPreset preset, NativeReflexPresetOverrides& out) {
    switch (preset) {
        case FpsLimiterPreset::kLowLatencyNativePacing:
            out.use_reflex_markers_as_fps_limiter = true;
            out.reflex_fps_limiter_max_queued_frames = 0;
            out.use_streamline_proxy_fps_limiter = false;
            out.native_pacing_sim_start_only = true;
            out.delay_present_start_after_sim_enabled = false;
            out.safe_mode_fps_limiter = false;
            out.fps_limiter_fg2_enabled = false;
            break;
        case FpsLimiterPreset::kDCPaceLockQ1:
            out.use_reflex_markers_as_fps_limiter = true;
            out.reflex_fps_limiter_max_queued_frames = 1;
            out.use_streamline_proxy_fps_limiter = false;
            out.native_pacing_sim_start_only = false;
            out.delay_present_start_after_sim_enabled = false;
            out.safe_mode_fps_limiter = false;
            out.fps_limiter_fg2_enabled = false;
            break;
        case FpsLimiterPreset::kDCPaceLockQ2:
            out.use_reflex_markers_as_fps_limiter = true;
            out.reflex_fps_limiter_max_queued_frames = 2;
            out.use_streamline_proxy_fps_limiter = false;
            out.native_pacing_sim_start_only = false;
            out.delay_present_start_after_sim_enabled = false;
            out.safe_mode_fps_limiter = false;
            out.fps_limiter_fg2_enabled = false;
            break;
        case FpsLimiterPreset::kDCPaceLockQ3:
            out.use_reflex_markers_as_fps_limiter = true;
            out.reflex_fps_limiter_max_queued_frames = 3;
            out.use_streamline_proxy_fps_limiter = false;
            out.native_pacing_sim_start_only = false;
            out.delay_present_start_after_sim_enabled = false;
            out.safe_mode_fps_limiter = false;
            out.fps_limiter_fg2_enabled = false;
            break;
        case FpsLimiterPreset::kPaceGenerated:
            out.use_reflex_markers_as_fps_limiter = false;
            out.reflex_fps_limiter_max_queued_frames = 0;
            out.use_streamline_proxy_fps_limiter = false;
            out.native_pacing_sim_start_only = false;
            out.delay_present_start_after_sim_enabled = false;
            out.safe_mode_fps_limiter = false;
            out.fps_limiter_fg2_enabled = false;
            break;
        case FpsLimiterPreset::kPaceGeneratedSafe:
            out.use_reflex_markers_as_fps_limiter = false;
            out.reflex_fps_limiter_max_queued_frames = 0;
            out.use_streamline_proxy_fps_limiter = false;
            out.native_pacing_sim_start_only = false;
            out.delay_present_start_after_sim_enabled = false;
            out.safe_mode_fps_limiter = true;
            out.fps_limiter_fg2_enabled = false;
            break;
        case FpsLimiterPreset::kCustom:
            // No overrides; caller should use config values
            break;
        case FpsLimiterPreset::kLowLatencyNativePacingV2:
            out.use_reflex_markers_as_fps_limiter = true;
            out.reflex_fps_limiter_max_queued_frames = 0;
            out.use_streamline_proxy_fps_limiter = false;
            out.native_pacing_sim_start_only = true;
            out.delay_present_start_after_sim_enabled = false;
            out.safe_mode_fps_limiter = false;
            out.fps_limiter_fg2_enabled = true;
            break;

    }
}

// TODO add initialization of other settings
void MainTabSettings::LoadSettings() {
    LogInfo("MainTabSettings::LoadSettings() called");
    LoadTabSettingsWithSmartLogging(all_settings_, "Main Tab");

    // Legacy config key was show_test_overlay; migrate if the new key is absent.
    int new_key_value = 0;
    if (!display_commander::config::get_config_value("DisplayCommander", "ShowPerformanceOverlay", new_key_value)) {
        int legacy_value = 0;
        if (display_commander::config::get_config_value("DisplayCommander", "show_test_overlay", legacy_value)
            && (legacy_value == 0 || legacy_value == 1)) {
            show_performance_overlay.SetValue(legacy_value != 0);
        }
    }

    // Legacy bool show_labels -> overlay_label_mode (0=None, 2=Full). New key takes precedence when present.
    int overlay_mode_probe = 0;
    if (!display_commander::config::get_config_value("DisplayCommander", "Overlay.LabelMode", overlay_mode_probe)) {
        bool legacy_show_labels = true;
        if (display_commander::config::get_config_value("DisplayCommander", "show_labels", legacy_show_labels)) {
            overlay_label_mode.SetValue(legacy_show_labels ? static_cast<int>(OverlayLabelMode::kFull)
                                                           : static_cast<int>(OverlayLabelMode::kNone));
        }
    }

    if (clip_cursor_enabled.GetValue() && unclip_cursor_enabled.GetValue()) {
        LogInfo("Main tab: clip_cursor_enabled and unclip_cursor_enabled both true; disabling unclip_cursor_enabled.");
        unclip_cursor_enabled.SetValue(false);
    }

    // Update overlay spacing maximums based on screen dimensions
    UpdateOverlaySpacingMaximums();

    LogInfo("MainTabSettings::LoadSettings() completed");
}

// Helper function to convert wstring to string
std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();

    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return std::string();

    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

// Returns the extended display device ID for the monitor containing the window.
std::string GetExtendedDisplayDeviceIdFromWindow(HWND hwnd) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return "No Window";
    }

    // Get the monitor that contains the window
    HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (hmon == nullptr) {
        return "No Monitor";
    }

    // Use the display cache function to get the extended device ID
    return display_cache::g_displayCache.GetExtendedDeviceIdFromMonitor(hmon);
}

// Function to save the extended display device ID for the game window
void SaveGameWindowDisplayDeviceId(HWND hwnd) {
    std::string device_id = GetExtendedDisplayDeviceIdFromWindow(hwnd);
    settings::g_mainTabSettings.game_window_extended_display_device_id.SetValue(device_id);

    std::ostringstream oss;
    oss << "Saved game window display device ID: " << device_id;
    LogInfo(oss.str().c_str());
}

// Function to update the target display setting with current game window
void UpdateTargetDisplayFromGameWindow() {
    // Get the game window from the API hooks
    HWND game_window = display_commanderhooks::GetGameWindow();

    std::string display_id = GetExtendedDisplayDeviceIdFromWindow(game_window);
    settings::g_mainTabSettings.target_extended_display_device_id.SetValue(display_id);
}

void UpdateFpsLimitMaximums() {
    // Only update if display cache is initialized
    if (!display_cache::g_displayCache.IsInitialized()) {
        return;
    }

    // Get the maximum refresh rate across all monitors
    double max_refresh_rate = display_cache::g_displayCache.GetMaxRefreshRateAcrossAllMonitors();

    // Base cap from monitor capabilities (at least 60 FPS); slider allows up to 2× for headroom above refresh.
    const float base_max_fps = max(60.f, static_cast<float>(max_refresh_rate));
    const float max_fps = base_max_fps * 2.f;

    // Update the maximum values
    if (g_mainTabSettings.fps_limit.GetMax() != max_fps) {
        auto old_fps = g_mainTabSettings.fps_limit.GetMax();
        g_mainTabSettings.fps_limit.SetMax(max_fps);
        g_mainTabSettings.fps_limit_background.SetMax(max_fps);

        LogInfo("Updated FPS limit maximum %.1f->%.1f FPS (2× base cap %.1f, max monitor refresh %.1f Hz)", old_fps,
                max_fps, base_max_fps, max_refresh_rate);
    }
}

// Function to update overlay spacing maximums based on screen dimensions
void UpdateOverlaySpacingMaximums() {
    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int screen_height = GetSystemMetrics(SM_CYSCREEN);

    float max_width = static_cast<float>(screen_width);
    float max_height = static_cast<float>(screen_height);

    // Update the maximum values for overlay spacing settings
    if (g_mainTabSettings.overlay_horizontal_spacing.GetMax() != max_width) {
        g_mainTabSettings.overlay_horizontal_spacing.SetMax(max_width);
        LogInfo("Updated overlay horizontal spacing maximum to %.0f px (screen width)", max_width);
    }

    if (g_mainTabSettings.overlay_vertical_spacing.GetMax() != max_height) {
        g_mainTabSettings.overlay_vertical_spacing.SetMax(max_height);
        LogInfo("Updated overlay vertical spacing maximum to %.0f px (screen height)", max_height);
    }
}

}  // namespace settings

WindowMode GetCurrentWindowMode() {
    return static_cast<WindowMode>(settings::g_mainTabSettings.window_mode.GetValue());
}
