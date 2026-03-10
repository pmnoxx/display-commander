#include "main_tab_settings.hpp"
#include "../adhd_multi_monitor/adhd_simple_api.hpp"
#include "../hooks/api_hooks.hpp"
#include "../utils.hpp"
#include "../utils/logging.hpp"

#include <windows.h>

std::atomic<float> s_system_volume_percent{100.f};

namespace settings {

MainTabSettings::MainTabSettings()
    : window_mode(
          "window_mode", static_cast<int>(WindowMode::kNoChanges),
          {"No changes", "Prevent exclusive fullscreen / no resize", "Borderless fullscreen", "Borderless windowed"},
          "DisplayCommander"),
      aspect_index("aspect_index", 3, {"3:2", "4:3", "16:10", "16:9", "19:9", "19.5:9", "21:9", "21.5:9", "32:9"},
                   "DisplayCommander"),  // Default to 16:9
      window_aspect_width("aspect_width", 0,
                          {"Display Width", "3840", "2560", "1920", "1600", "1280", "1080", "900", "720"},
                          "DisplayCommander"),
      alignment("alignment", 0, {"Center", "Top Left", "Top Right", "Bottom Left", "Bottom Right"}, "DisplayCommander"),
      fps_limiter_enabled("fps_limiter_enabled", true, "DisplayCommander"),
      fps_limiter_mode(
          "fps_limiter_mode", 0,
          {"Default", "Reflex (low latency)", "Sync to Display Refresh Rate (fraction of monitor refresh rate)"},
          "DisplayCommander"),
      scanline_offset("scanline_offset", 0, -1000, 1000, "DisplayCommander"),
      vblank_sync_divisor("vblank_sync_divisor", 1, 0, 8, "DisplayCommander"),
      fps_limit("fps_limit", 0.0f, 0.0f, 240.0f, "DisplayCommander"),
      fps_limit_background("fps_limit_background", 60.0f, 0.0f, 240.0f, "DisplayCommander"),
      background_fps_enabled("background_fps_enabled", false, "DisplayCommander"),
      suppress_reflex_sleep("suppress_reflex_sleep", false, "DisplayCommander"),
      inject_reflex("inject_reflex", false, "DisplayCommander"),
      onpresent_sync_low_latency_ratio(
          "onpresent_sync_low_latency_ratio", 0,
          {"100% Display / 0% Input (default)", "87.5% Display / 12.5% Input", "75% Display / 25% Input",
           "62.5% Display / 37.5% Input", "50% Display / 50% Input", "37.5% Display / 62.5% Input",
           "25% Display / 75% Input", "12.5% Display / 87.5% Input", "0% Display / 100% Input"},
          "DisplayCommander"),  // Default to 100% Display / 0% Input (current behavior)
      onpresent_reflex_mode("onpresent_reflex_mode", static_cast<int>(OnPresentReflexMode::kLowLatency),
                            {"Low latency", "Low Latency + boost", "Off", "Game Defaults"}, "DisplayCommander"),
      reflex_limiter_reflex_mode("reflex_limiter_reflex_mode", static_cast<int>(OnPresentReflexMode::kLowLatency),
                                 {"Low latency", "Low Latency + boost", "Off", "Game Defaults"}, "DisplayCommander"),
      reflex_disabled_limiter_mode("reflex_disabled_limiter_mode", static_cast<int>(OnPresentReflexMode::kOff),
                                   {"Low latency", "Low Latency + boost", "Off", "Game Defaults"}, "DisplayCommander"),
      pcl_stats_enabled("pcl_stats_enabled.disabled2", false, "DisplayCommander"),
      use_reflex_markers_as_fps_limiter("use_reflex_markers_as_fps_limiter", true, "DisplayCommander"),
      reflex_fps_limiter_max_queued_frames("reflex_fps_limiter_max_queued_frames", 2,
                                           {"Game default", "1", "2", "3", "4", "5", "6"}, "DisplayCommander"),
      native_reflex_fps_preset(
          "native_reflex_fps_preset", 0,
          {"Pace real frames Balanced (Use Reflex Latency Markers, max queued=2)",
           "Pace real frames Stability (Use Reflex Latency Markers, max queued=3)",
           "Pace real frames Low-latency (Use Reflex Latency Markers, max queued=1)",
           "Pace real frames Low-latency (Use native frame pacing)",
           "Pace generated frames (FPS limiter on generated frames)",
           "Pace generated (safe) - Use Reshade APIs as fallback",
           "Custom (configure manually)"},
          "DisplayCommander"),
      use_streamline_proxy_fps_limiter("use_streamline_proxy_fps_limiter", false, "DisplayCommander"),
      native_pacing_sim_start_only("native_pacing_sim_start_only_doff", false, "DisplayCommander"),
      delay_present_start_after_sim_enabled("delay_present_start_after_sim_enabled_doff", false, "DisplayCommander"),
      delay_present_start_frames("delay_present_start_frames", 1.0f, 0.0f, 3.0f, "DisplayCommander"),
      safe_mode_fps_limiter("safe_mode_fps_limiter", false, "DisplayCommander"),
      selected_reshade_runtime_index("selected_reshade_runtime_index", 0, 0, 31, "DisplayCommander"),
      vsync_override("vsync_override", 0,
                     {"No override", "Force ON", "FORCED 1/2 (NO VRR)", "FORCED 1/3 (NO VRR)", "FORCED 1/4 (NO VRR)",
                      "FORCED OFF"},
                     "DisplayCommander"),
      max_frame_latency_override(
          "max_frame_latency_override", 0,
          {"No override", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"},
          "DisplayCommander"),
      force_vsync_on("force_vsync_on", false, "DisplayCommander"),
      force_vsync_off("force_vsync_off", false, "DisplayCommander"),
      prevent_tearing("prevent_tearing", false, "DisplayCommander"),
      limit_real_frames("limit_real_frames", true, "DisplayCommander"),
      backbuffer_count_override("backbuffer_count_override", 0, {"No override", "1", "2", "3", "4"},
                                "DisplayCommander"),
      force_flip_discard_upgrade("ForceFlipDiscardUpgrade", false, "DisplayCommander"),
      audio_volume_percent("audio_volume_percent", 100.0f, 0.0f, 100.0f, "DisplayCommander"),
      audio_mute("audio_mute", false, "DisplayCommander"),
      mute_in_background("mute_in_background", false, "DisplayCommander"),
      mute_in_background_if_other_audio("mute_in_background_if_other_audio", false, "DisplayCommander"),
      audio_volume_auto_apply("audio_volume_auto_apply", true, "DisplayCommander"),
      enable_default_chords("enable_default_chords", true, "DisplayCommander"),
      guide_button_solo_ui_toggle_only("guide_button_solo_ui_toggle_only", true, "DisplayCommander"),
      keyboard_input_blocking("keyboard_input_blocking", static_cast<int>(InputBlockingMode::kEnabledInBackground),
                              {"Disabled", "Enabled", "Enabled (in background)"}, "DisplayCommander"),
      mouse_input_blocking("mouse_input_blocking", static_cast<int>(InputBlockingMode::kEnabledInBackground),
                           {"Disabled", "Enabled", "Enabled (in background)", "Enabled (when XInput detected)"},
                           "DisplayCommander"),
      gamepad_input_blocking("gamepad_input_blocking", static_cast<int>(InputBlockingMode::kDisabled),
                             {"Disabled", "Enabled", "Enabled (in background)"}, "DisplayCommander"),
      clip_cursor_enabled("clip_cursor_enabled", false, "DisplayCommander"),
      no_render_in_background("no_render_in_background", false, "DisplayCommander"),
      no_present_in_background("no_present_in_background", false, "DisplayCommander"),
      cpu_cores("cpu_cores", 0, 0, 64,
                "DisplayCommander"),  // Max will be set dynamically based on CPU count
      show_test_overlay("show_test_overlay", false, "DisplayCommander"),
      show_fps_counter("show_fps_counter", true, "DisplayCommander"),
      show_native_fps("show_native_fps", false, "DisplayCommander"),
      show_refresh_rate("show_refresh_rate", false, "DisplayCommander"),
      show_vrr_status("show_vrr_status", false, "DisplayCommander"),
      show_actual_refresh_rate("show_actual_refresh_rate", false, "DisplayCommander"),
      vrr_debug_mode("vrr_debug_mode", false, "DisplayCommander"),
      show_flip_status("show_flip_status", false, "DisplayCommander"),
      show_display_commander_ui("show_display_commander_ui", false, "DisplayCommander"),
      show_independent_window("show_independent_window", false, "DisplayCommander"),
      display_commander_ui_window_x("display_commander_ui_window_x", 100.0f, 0.0f, 10000.0f, "DisplayCommander"),
      display_commander_ui_window_y("display_commander_ui_window_y", 100.0f, 0.0f, 10000.0f, "DisplayCommander"),
      show_labels("show_labels", true, "DisplayCommander"),
      show_clock("show_clock", false, "DisplayCommander"),
      show_frame_time_graph("show_frame_time_graph", true, "DisplayCommander"),
      show_frame_time_stats("show_frame_time_stats", false, "DisplayCommander"),
      show_native_frame_time_graph("show_native_frame_time_graph", false, "DisplayCommander"),
      show_frame_timeline_bar("show_frame_timeline_bar", false, "DisplayCommander"),
      show_refresh_rate_frame_times("show_refresh_rate_frame_times", true, "DisplayCommander"),
      refresh_rate_monitor_poll_ms("refresh_rate_monitor_poll_ms", 1, 1, 500, "DisplayCommander"),
      show_refresh_rate_frame_time_stats("show_refresh_rate_frame_time_stats", false, "DisplayCommander"),
      show_dxgi_vrr_status("show_dxgi_vrr_status", false, "DisplayCommander"),
      show_dxgi_refresh_rate("show_dxgi_refresh_rate", false, "DisplayCommander"),
      show_cpu_usage("show_cpu_usage", false, "DisplayCommander"),
      show_cpu_fps("show_cpu_fps", false, "DisplayCommander"),
      show_fg_mode("show_fg_mode", false, "DisplayCommander"),
      show_dlss_internal_resolution("show_dlss_internal_resolution", false, "DisplayCommander"),
      show_dlss_status("show_dlss_status", false, "DisplayCommander"),
      show_dlss_quality_preset("show_dlss_quality_preset", false, "DisplayCommander"),
      show_dlss_render_preset("show_dlss_render_preset", false, "DisplayCommander"),
      show_stopwatch("show_stopwatch", false, "DisplayCommander"),
      show_playtime("show_playtime", false, "DisplayCommander"),
      show_overlay_vu_bars("show_overlay_vu_bars", false, "DisplayCommander"),
      show_overlay_vram("show_overlay_vram", false, "DisplayCommander"),
      show_overlay_texture_stats("show_overlay_texture_stats", false, "DisplayCommander"),
      overlay_background_alpha("overlay_background_alpha", 0.3f, 0.0f, 1.0f, "DisplayCommander"),
      overlay_chart_alpha("overlay_chart_alpha", 0.0f, 0.0f, 1.0f, "DisplayCommander"),
      overlay_graph_scale("overlay_graph_scale", 1.0f, 0.5f, 10.0f, "DisplayCommander"),
      overlay_graph_max_scale("overlay_graph_max_scale", 4.0f, 2.0f, 10.0f, "DisplayCommander"),
      overlay_vertical_spacing("overlay_vertical_spacing", 0.0f, 0.0f,
                               static_cast<float>(GetSystemMetrics(SM_CYSCREEN)), "DisplayCommander"),
      overlay_horizontal_spacing("overlay_horizontal_spacing", 0.0f, 0.0f,
                                 static_cast<float>(GetSystemMetrics(SM_CXSCREEN)), "DisplayCommander"),
      gpu_measurement_enabled("gpu_measurement_enabled", 1, 0, 1, "DisplayCommander"),
      target_extended_display_device_id("target_extended_display_device_id", "", "DisplayCommander"),
      game_window_extended_display_device_id("game_window_display_device_id", "", "DisplayCommander"),
      selected_extended_display_device_id("selected_extended_display_device_id", "", "DisplayCommander"),
      adhd_single_monitor_enabled_for_game_display("adhd_single_monitor_enabled_for_game_display", false,
                                                   "DisplayCommander"),
      adhd_multi_monitor_enabled("adhd_multi_monitor_enabled", false, "DisplayCommander"),
      screensaver_mode("screensaver_mode", static_cast<int>(ScreensaverMode::kDefault),
                       {"Default (no change)", "Disable when Focused", "Disable"}, "DisplayCommander"),
      taskbar_hide_mode("taskbar_hide_mode", static_cast<int>(TaskbarHideMode::kNoChanges),
                        {"No changes", "In foreground", "Always"}, "DisplayCommander"),
      frame_time_mode("frame_time_mode", static_cast<int>(FrameTimeMode::kPresent),
                      {"Frame Present Time", "Frame Start Time (input)",
                       "Frame Display Time later (Present or GPU Completion whichever comes later)"},
                      "DisplayCommander"),
      advanced_settings_enabled("advanced_settings_enabled", false, "DisplayCommander"),
      log_level("log_level", 0, {"Log everything", "Info", "Warning", "Error Only"}, "DisplayCommander"),
      show_advanced_tab("show_advanced_tab", false, "DisplayCommander"),
      show_window_info_tab("show_window_info_tab", false, "DisplayCommander"),
      show_swapchain_tab("show_swapchain_tab", false, "DisplayCommander"),
      show_important_info_tab("show_important_info_tab", false, "DisplayCommander"),
      show_controller_tab("show_controller_tab", false, "DisplayCommander"),
      show_hook_stats_tab("show_hook_stats_tab", false, "DisplayCommander"),
      show_streamline_tab("show_streamline_tab", false, "DisplayCommander"),
      show_experimental_tab("show_experimental_tab", false, "DisplayCommander"),
      show_reshade_tab("show_reshade_tab", false, "DisplayCommander"),
      show_performance_tab("show_performance_tab", false, "DisplayCommander"),
      show_vulkan_tab("show_vulkan_tab", false, "DisplayCommander"),
      vulkan_nvll_hooks_enabled("vulkan_nvll_hooks_enabled_don", true, "DisplayCommander"),
      vulkan_vk_loader_hooks_enabled("vulkan_vk_loader_hooks_enabled", false, "DisplayCommander"),
      vulkan_append_reflex_extensions("vulkan_append_reflex_extensions", false, "DisplayCommander"),
      skip_ansel_loading("skip_ansel_loading", false, "DisplayCommander"),
      force_anisotropic_filtering("force_anisotropic_filtering", false, "DisplayCommander"),
      upgrade_min_mag_mip_linear("upgrade_min_mag_mip_linear", true, "DisplayCommander"),
      upgrade_compare_min_mag_mip_linear("upgrade_compare_min_mag_mip_linear", false, "DisplayCommander"),
      upgrade_min_mag_linear_mip_point("upgrade_min_mag_linear_mip_point", true, "DisplayCommander"),
      upgrade_compare_min_mag_linear_mip_point("upgrade_compare_min_mag_linear_mip_point", false, "DisplayCommander"),
      max_anisotropy("max_anisotropy", 0, 0, 16, "DisplayCommander"),
      force_mipmap_lod_bias("force_mipmap_lod_bias", 0.0f, -5.0f, 5.0f, "DisplayCommander"),
      brightness_autohdr_section_enabled("brightness_autohdr_section_enabled_don", true, "DisplayCommander"),
      brightness_percent("brightness_percent", 100.0f, 0.0f, 500.0f, "DisplayCommander"),
      swapchain_colorspace("swapchain_colorspace", 0, {"Auto", "scRGB(default)", "HDR10", "sRGB", "Gamma 2.2", "None"},
                           "DisplayCommander"),
      brightness_colorspace("brightness_colorspace2", 0,
                            {"Auto", "scRGB(default)", "HDR10", "sRGB", "Gamma 2.2", "None"}, "DisplayCommander"),
      gamma_value("gamma_value", 1.0f, 0.5f, 2.0f, "DisplayCommander"),
      contrast_value("contrast_value", 1.0f, 0.0f, 2.0f, "DisplayCommander"),
      saturation_value("saturation_value", 1.0f, 0.0f, 2.0f, "DisplayCommander"),
      hue_degrees("hue_degrees", 0.0f, -15.0f, 15.0f, "DisplayCommander"),
      swapchain_hdr_upgrade("swapchain_hdr_upgrade", false, "DisplayCommander"),
      swapchain_hdr_upgrade_mode("swapchain_hdr_upgrade_mode", 0, {"scRGB (default)", "HDR10"}, "DisplayCommander"),
      auto_hdr("auto_hdr", false, "DisplayCommander"),
      auto_hdr_strength("auto_hdr_strength", 1.0f, 0.0f, 2.0f, "DisplayCommander"),
      auto_enable_disable_hdr("auto_enable_disable_hdr", false, "DisplayCommander"),
      auto_apply_maxmdl_1000_hdr_metadata("auto_apply_maxmdl_1000_hdr_metadata", false, "DisplayCommander") {
    // Initialize the all_settings_ vector
    all_settings_ = {
        &window_mode,
        &aspect_index,
        &window_aspect_width,
        &alignment,
        &fps_limiter_enabled,
        &fps_limiter_mode,
        &scanline_offset,
        &vblank_sync_divisor,
        &fps_limit,
        &fps_limit_background,
        &background_fps_enabled,
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
        &vsync_override,
        &max_frame_latency_override,
        &force_vsync_on,
        &force_vsync_off,
        &prevent_tearing,
        &limit_real_frames,
        &backbuffer_count_override,
        &force_flip_discard_upgrade,
        &audio_volume_percent,
        &audio_mute,
        &mute_in_background,
        &mute_in_background_if_other_audio,
        &audio_volume_auto_apply,
        &enable_default_chords,
        &guide_button_solo_ui_toggle_only,
        &keyboard_input_blocking,
        &mouse_input_blocking,
        &gamepad_input_blocking,
        &clip_cursor_enabled,
        &no_render_in_background,
        &no_present_in_background,
        &show_test_overlay,
        &show_fps_counter,
        &show_native_fps,
        &show_refresh_rate,
        &show_vrr_status,
        &show_actual_refresh_rate,
        &vrr_debug_mode,
        &show_flip_status,
        &show_display_commander_ui,
        &show_independent_window,
        &display_commander_ui_window_x,
        &display_commander_ui_window_y,
        &show_labels,
        &show_clock,
        &show_frame_time_graph,
        &show_frame_time_stats,
        &show_native_frame_time_graph,
        &show_frame_timeline_bar,
        &show_refresh_rate_frame_times,
        &refresh_rate_monitor_poll_ms,
        &show_refresh_rate_frame_time_stats,
        &show_dxgi_vrr_status,
        &show_dxgi_refresh_rate,
        &show_cpu_usage,
        &show_cpu_fps,
        &show_fg_mode,
        &show_dlss_internal_resolution,
        &show_dlss_status,
        &show_dlss_quality_preset,
        &show_dlss_render_preset,
        &show_stopwatch,
        &show_playtime,
        &show_overlay_vu_bars,
        &show_overlay_vram,
        &show_overlay_texture_stats,
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
        &taskbar_hide_mode,
        &advanced_settings_enabled,
        &log_level,
        &show_advanced_tab,
        &show_window_info_tab,
        &show_swapchain_tab,
        &show_important_info_tab,
        &show_controller_tab,
        &show_hook_stats_tab,
        &show_streamline_tab,
        &show_experimental_tab,
        &show_reshade_tab,
        &show_performance_tab,
        &show_vulkan_tab,
        &vulkan_nvll_hooks_enabled,
        &vulkan_vk_loader_hooks_enabled,
        &vulkan_append_reflex_extensions,
        &skip_ansel_loading,
        &force_anisotropic_filtering,
        &upgrade_min_mag_mip_linear,
        &upgrade_compare_min_mag_mip_linear,
        &upgrade_min_mag_linear_mip_point,
        &upgrade_compare_min_mag_linear_mip_point,
        &max_anisotropy,
        &force_mipmap_lod_bias,
        &brightness_autohdr_section_enabled,
        &brightness_percent,
        &swapchain_colorspace,
        &brightness_colorspace,
        &gamma_value,
        &contrast_value,
        &saturation_value,
        &hue_degrees,
        &swapchain_hdr_upgrade,
        &swapchain_hdr_upgrade_mode,
        &auto_hdr,
        &auto_hdr_strength,
        &auto_enable_disable_hdr,
        &auto_apply_maxmdl_1000_hdr_metadata,
    };
}

void ApplyNativeReflexPreset(int preset) {
    switch (preset) {
        case 0:  // Pace real frames Balanced
            g_mainTabSettings.limit_real_frames.SetValue(true);
            g_mainTabSettings.use_reflex_markers_as_fps_limiter.SetValue(true);
            g_mainTabSettings.reflex_fps_limiter_max_queued_frames.SetValue(2);
            g_mainTabSettings.use_streamline_proxy_fps_limiter.SetValue(false);
            g_mainTabSettings.native_pacing_sim_start_only.SetValue(false);
            g_mainTabSettings.delay_present_start_after_sim_enabled.SetValue(false);
            g_mainTabSettings.safe_mode_fps_limiter.SetValue(false);
            break;
        case 1:  // Pace real frames Stability
            g_mainTabSettings.limit_real_frames.SetValue(true);
            g_mainTabSettings.use_reflex_markers_as_fps_limiter.SetValue(true);
            g_mainTabSettings.reflex_fps_limiter_max_queued_frames.SetValue(3);
            g_mainTabSettings.use_streamline_proxy_fps_limiter.SetValue(false);
            g_mainTabSettings.native_pacing_sim_start_only.SetValue(false);
            g_mainTabSettings.delay_present_start_after_sim_enabled.SetValue(false);
            g_mainTabSettings.safe_mode_fps_limiter.SetValue(false);
            break;
        case 2:  // Pace real frames Low-latency (Reflex markers, max queued=1)
            g_mainTabSettings.limit_real_frames.SetValue(true);
            g_mainTabSettings.use_reflex_markers_as_fps_limiter.SetValue(true);
            g_mainTabSettings.reflex_fps_limiter_max_queued_frames.SetValue(1);
            g_mainTabSettings.use_streamline_proxy_fps_limiter.SetValue(false);
            g_mainTabSettings.native_pacing_sim_start_only.SetValue(false);
            g_mainTabSettings.delay_present_start_after_sim_enabled.SetValue(false);
            g_mainTabSettings.safe_mode_fps_limiter.SetValue(false);
            break;
        case 3:  // Pace real frames Low-latency (Use native frame pacing)
            g_mainTabSettings.limit_real_frames.SetValue(true);
            g_mainTabSettings.use_reflex_markers_as_fps_limiter.SetValue(false);
            g_mainTabSettings.reflex_fps_limiter_max_queued_frames.SetValue(0);
            g_mainTabSettings.use_streamline_proxy_fps_limiter.SetValue(false);
            g_mainTabSettings.native_pacing_sim_start_only.SetValue(true);
            g_mainTabSettings.delay_present_start_after_sim_enabled.SetValue(false);
            g_mainTabSettings.safe_mode_fps_limiter.SetValue(false);
            break;
        case 4:  // Pace generated frames
            g_mainTabSettings.limit_real_frames.SetValue(false);
            g_mainTabSettings.use_reflex_markers_as_fps_limiter.SetValue(false);
            g_mainTabSettings.reflex_fps_limiter_max_queued_frames.SetValue(0);
            g_mainTabSettings.use_streamline_proxy_fps_limiter.SetValue(false);
            g_mainTabSettings.native_pacing_sim_start_only.SetValue(false);
            g_mainTabSettings.delay_present_start_after_sim_enabled.SetValue(false);
            g_mainTabSettings.safe_mode_fps_limiter.SetValue(false);
            break;
        case 5:  // Pace generated (safe) - Use Reshade APIs as fallback
            g_mainTabSettings.limit_real_frames.SetValue(false);
            g_mainTabSettings.use_reflex_markers_as_fps_limiter.SetValue(false);
            g_mainTabSettings.reflex_fps_limiter_max_queued_frames.SetValue(0);
            g_mainTabSettings.use_streamline_proxy_fps_limiter.SetValue(false);
            g_mainTabSettings.native_pacing_sim_start_only.SetValue(false);
            g_mainTabSettings.delay_present_start_after_sim_enabled.SetValue(false);
            g_mainTabSettings.safe_mode_fps_limiter.SetValue(true);
            break;
        default:  // Custom (6) - no auto-apply
            break;
    }
}

// TODO add initialization of other settings
void MainTabSettings::LoadSettings() {
    LogInfo("MainTabSettings::LoadSettings() called");
    LoadTabSettingsWithSmartLogging(all_settings_, "Main Tab");

    // Apply Native Reflex preset when not Custom (preset 6)
    int preset = native_reflex_fps_preset.GetValue();
    if (preset >= 0 && preset < 6) {
        ApplyNativeReflexPreset(preset);
    }

    // Update CPU cores maximum based on system CPU count
    UpdateCpuCoresMaximum();

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

    // Update the maximum values for FPS limit settings
    // Add some buffer (e.g., 10%) to allow for slightly higher FPS than max refresh rate
    float max_fps = max(60.f, static_cast<float>(max_refresh_rate));

    // Update the maximum values
    if (g_mainTabSettings.fps_limit.GetMax() != max_fps) {
        auto old_fps = g_mainTabSettings.fps_limit.GetMax();
        g_mainTabSettings.fps_limit.SetMax(max_fps);
        g_mainTabSettings.fps_limit_background.SetMax(max_fps);

        LogInfo("Updated FPS limit maximum %.1f->%.1f FPS (based on max monitor refresh rate of %.1f Hz)", old_fps,
                max_fps, max_refresh_rate);
    }
}

// Function to get CPU core count and update CPU cores setting maximum
void UpdateCpuCoresMaximum() {
    SYSTEM_INFO sys_info = {};
    GetSystemInfo(&sys_info);
    DWORD cpu_count = sys_info.dwNumberOfProcessors;

    // Update the maximum value for CPU cores setting
    if (g_mainTabSettings.cpu_cores.GetMax() != static_cast<int>(cpu_count)) {
        g_mainTabSettings.cpu_cores.SetMax(static_cast<int>(cpu_count));
        LogInfo("Updated CPU cores maximum to %lu cores", cpu_count);
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
