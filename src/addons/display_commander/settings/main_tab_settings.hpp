#pragma once

#include "../performance_types.hpp"
#include "../ui/new_ui/settings_wrapper.hpp"
#include "globals.hpp"

#include <atomic>
#include <vector>

// Forward declarations for atomic variables used by main tab settings
extern std::atomic<bool> s_background_feature_enabled;
extern std::atomic<int> s_scanline_offset;
extern std::atomic<int> s_vblank_sync_divisor;
extern std::atomic<float> s_fps_limit;
extern std::atomic<float> s_fps_limit_background;
extern std::atomic<float> s_present_pacing_delay_percentage;
extern std::atomic<bool> s_force_vsync_on;
extern std::atomic<bool> s_force_vsync_off;
extern std::atomic<bool> s_prevent_tearing;
extern std::atomic<float> s_audio_volume_percent;
extern std::atomic<float> s_system_volume_percent;
extern std::atomic<bool> s_audio_mute;
extern std::atomic<bool> s_mute_in_background;
extern std::atomic<bool> s_mute_in_background_if_other_audio;
extern std::atomic<InputBlockingMode> s_keyboard_input_blocking;
extern std::atomic<InputBlockingMode> s_mouse_input_blocking;
extern std::atomic<InputBlockingMode> s_gamepad_input_blocking;
extern std::atomic<bool> s_no_render_in_background;
extern std::atomic<bool> s_no_present_in_background;
extern std::atomic<int> s_cpu_cores;

namespace settings {

// Settings manager for the main tab
class MainTabSettings {
   public:
    MainTabSettings();
    ~MainTabSettings() = default;

    // Load all settings from DisplayCommander config
    void LoadSettings();

    // Get all settings for loading
    std::vector<ui::new_ui::SettingBase*> GetAllSettings();

    // Display Settings
    ui::new_ui::ComboSettingEnumRef<WindowMode> window_mode;
    ui::new_ui::ComboSetting aspect_index;
    ui::new_ui::ComboSettingRef window_aspect_width;
    ui::new_ui::BoolSettingRef background_feature;
    ui::new_ui::ComboSetting alignment;

    // ADHD Multi-Monitor Mode Settings
    ui::new_ui::BoolSetting adhd_multi_monitor_enabled;

    // FPS Settings
    ui::new_ui::ComboSetting fps_limiter_mode;
    ui::new_ui::IntSettingRef scanline_offset;
    ui::new_ui::IntSettingRef vblank_sync_divisor;
    ui::new_ui::FloatSettingRef fps_limit;
    ui::new_ui::FloatSettingRef fps_limit_background;
    ui::new_ui::FloatSettingRef present_pacing_delay_percentage;
    ui::new_ui::BoolSetting onpresent_sync_enable_reflex;
    ui::new_ui::BoolSetting suppress_reflex_sleep;
    ui::new_ui::ComboSetting onpresent_sync_low_latency_ratio;

    // VSync & Tearing
    ui::new_ui::BoolSettingRef force_vsync_on;
    ui::new_ui::BoolSettingRef force_vsync_off;
    ui::new_ui::BoolSettingRef prevent_tearing;
    ui::new_ui::BoolSetting limit_real_frames;

    // Audio Settings
    ui::new_ui::FloatSettingRef audio_volume_percent;
    ui::new_ui::BoolSettingRef audio_mute;
    ui::new_ui::BoolSettingRef mute_in_background;
    ui::new_ui::BoolSettingRef mute_in_background_if_other_audio;
    ui::new_ui::BoolSetting audio_volume_auto_apply;

    // Input Remapping Settings
    ui::new_ui::BoolSetting enable_default_chords;
    ui::new_ui::BoolSetting guide_button_solo_ui_toggle_only;

    // Input Blocking Settings
    ui::new_ui::ComboSettingEnumRef<InputBlockingMode> keyboard_input_blocking;
    ui::new_ui::ComboSettingEnumRef<InputBlockingMode> mouse_input_blocking;
    ui::new_ui::ComboSettingEnumRef<InputBlockingMode> gamepad_input_blocking;

    // Render Blocking (Background) Settings
    ui::new_ui::BoolSettingRef no_render_in_background;
    ui::new_ui::BoolSettingRef no_present_in_background;

    // CPU Settings
    ui::new_ui::IntSettingRef cpu_cores;

    // Test Overlay Settings
    ui::new_ui::BoolSetting show_test_overlay;
    ui::new_ui::BoolSetting show_fps_counter;
    ui::new_ui::BoolSetting show_native_fps;
    ui::new_ui::BoolSetting show_refresh_rate;
    ui::new_ui::BoolSetting show_vrr_status;
    ui::new_ui::BoolSetting vrr_debug_mode;
    ui::new_ui::BoolSetting show_flip_status;
    ui::new_ui::BoolSetting show_display_commander_ui;
    ui::new_ui::FloatSetting display_commander_ui_window_x;
    ui::new_ui::FloatSetting display_commander_ui_window_y;
    ui::new_ui::BoolSetting show_labels;
    ui::new_ui::BoolSetting show_clock;
    ui::new_ui::BoolSetting show_frame_time_graph;
    ui::new_ui::BoolSetting show_native_frame_time_graph;
    ui::new_ui::BoolSetting show_refresh_rate_frame_times;
    ui::new_ui::BoolSetting show_volume;
    ui::new_ui::BoolSetting show_cpu_usage;
    ui::new_ui::BoolSetting show_fg_mode;
    ui::new_ui::BoolSetting show_stopwatch;
    ui::new_ui::BoolSetting show_playtime;
    ui::new_ui::FloatSetting overlay_background_alpha;
    ui::new_ui::FloatSetting overlay_chart_alpha;
    ui::new_ui::FloatSetting overlay_graph_scale;
    ui::new_ui::FloatSetting overlay_graph_max_scale;

    // GPU Measurement Settings
    ui::new_ui::IntSetting gpu_measurement_enabled;

    // Frame Time Graph Settings
    ui::new_ui::ComboSettingEnumRef<FrameTimeMode> frame_time_mode;

    // Display Information
    ui::new_ui::StringSetting target_display;
    ui::new_ui::StringSetting game_window_display_device_id;
    ui::new_ui::StringSetting selected_extended_display_device_id;

    // Screensaver Control
    ui::new_ui::ComboSettingEnumRef<ScreensaverMode> screensaver_mode;

    // Advanced Settings
    ui::new_ui::BoolSetting advanced_settings_enabled;

    // Logging Level
    ui::new_ui::ComboSettingEnumRef<LogLevel> log_level;

    // Individual Tab Visibility Settings
    ui::new_ui::BoolSetting show_developer_tab;
    ui::new_ui::BoolSetting show_window_info_tab;
    ui::new_ui::BoolSetting show_swapchain_tab;
    ui::new_ui::BoolSetting show_important_info_tab;
    ui::new_ui::BoolSetting show_xinput_tab;
    ui::new_ui::BoolSetting show_remapping_tab;
    ui::new_ui::BoolSetting show_hook_stats_tab;
    ui::new_ui::BoolSetting show_streamline_tab;
    ui::new_ui::BoolSetting show_experimental_tab;
    ui::new_ui::BoolSetting show_reshade_tab;

    // Ansel Control
    ui::new_ui::BoolSetting skip_ansel_loading;

    // Sampler State Override Settings
    ui::new_ui::BoolSetting force_anisotropic_filtering;
    ui::new_ui::BoolSetting upgrade_min_mag_mip_linear;
    ui::new_ui::BoolSetting upgrade_compare_min_mag_mip_linear;
    ui::new_ui::BoolSetting upgrade_min_mag_linear_mip_point;
    ui::new_ui::BoolSetting upgrade_compare_min_mag_linear_mip_point;
    ui::new_ui::IntSetting max_anisotropy;
    ui::new_ui::FloatSetting force_mipmap_lod_bias;

   private:
    std::vector<ui::new_ui::SettingBase*> all_settings_;
};

// Global instance
extern MainTabSettings g_mainTabSettings;

// Utility functions
std::string GetDisplayDeviceIdFromWindow(HWND hwnd);
void SaveGameWindowDisplayDeviceId(HWND hwnd);
void UpdateTargetDisplayFromGameWindow();
void UpdateFpsLimitMaximums();
void UpdateCpuCoresMaximum();

}  // namespace settings
