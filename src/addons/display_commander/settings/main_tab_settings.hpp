#pragma once

#include "../ui/new_ui/settings_wrapper.hpp"
#include "../performance_types.hpp"
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
extern std::atomic<bool> s_audio_mute;
extern std::atomic<bool> s_mute_in_background;
extern std::atomic<bool> s_mute_in_background_if_other_audio;
extern std::atomic<InputBlockingMode> s_keyboard_input_blocking;
extern std::atomic<InputBlockingMode> s_mouse_input_blocking;
extern std::atomic<InputBlockingMode> s_gamepad_input_blocking;
extern std::atomic<bool> s_no_render_in_background;
extern std::atomic<bool> s_no_present_in_background;

namespace settings {

// Settings manager for the main tab
class MainTabSettings {
  public:
    MainTabSettings();
    ~MainTabSettings() = default;

    // Load all settings from DisplayCommander config
    void LoadSettings();

    // Get all settings for loading
    std::vector<ui::new_ui::SettingBase *> GetAllSettings();

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

    // VSync & Tearing
    ui::new_ui::BoolSettingRef force_vsync_on;
    ui::new_ui::BoolSettingRef force_vsync_off;
    ui::new_ui::BoolSettingRef prevent_tearing;

    // Audio Settings
    ui::new_ui::FloatSettingRef audio_volume_percent;
    ui::new_ui::BoolSettingRef audio_mute;
    ui::new_ui::BoolSettingRef mute_in_background;
    ui::new_ui::BoolSettingRef mute_in_background_if_other_audio;
    ui::new_ui::BoolSetting audio_volume_auto_apply;

    // Input Blocking Settings
    ui::new_ui::ComboSettingEnumRef<InputBlockingMode> keyboard_input_blocking;
    ui::new_ui::ComboSettingEnumRef<InputBlockingMode> mouse_input_blocking;
    ui::new_ui::ComboSettingEnumRef<InputBlockingMode> gamepad_input_blocking;

    // Render Blocking (Background) Settings
    ui::new_ui::BoolSettingRef no_render_in_background;
    ui::new_ui::BoolSettingRef no_present_in_background;

    // Test Overlay Settings
    ui::new_ui::BoolSetting show_test_overlay;

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
    ui::new_ui::BoolSetting show_xinput_tab;

    // Ansel Control
    ui::new_ui::BoolSetting skip_ansel_loading;

  private:
    std::vector<ui::new_ui::SettingBase *> all_settings_;
};

// Global instance
extern MainTabSettings g_mainTabSettings;

// Utility functions
std::string GetDisplayDeviceIdFromWindow(HWND hwnd);
void SaveGameWindowDisplayDeviceId(HWND hwnd);
void UpdateTargetDisplayFromGameWindow();
void UpdateFpsLimitMaximums();

} // namespace settings
