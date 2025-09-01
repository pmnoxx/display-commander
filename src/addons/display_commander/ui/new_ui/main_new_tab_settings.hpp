#pragma once

#include "settings_wrapper.hpp"
#include <vector>
#include <atomic>

namespace ui::new_ui {

// Settings manager for the main new tab
class MainNewTabSettings {
public:
    MainNewTabSettings();
    ~MainNewTabSettings() = default;
    
    // Load all settings from Reshade config
    void LoadSettings();
    
    // Get all settings for loading
    std::vector<SettingBase*> GetAllSettings();
    
    // Display Settings
    ComboSetting window_mode;
    ComboSetting window_width;
    ComboSetting window_height;
    ComboSetting aspect_index;
    ComboSetting target_monitor_index;
    BoolSetting background_feature;
    ComboSetting alignment;
    
    // FPS Settings
    ComboSetting fps_limiter_mode;
    IntSetting scanline_offset;
    IntSetting vblank_sync_divisor;
    IntSettingRef fps_limiter_injection;
    FloatSettingRef fps_limit;
    FloatSettingRef fps_limit_background;
    
    // VSync & Tearing
    BoolSettingRef force_vsync_on;
    BoolSettingRef force_vsync_off;
    BoolSettingRef prevent_tearing;

    
    // Audio Settings
    FloatSetting audio_volume_percent;
    BoolSettingRef audio_mute;
    BoolSettingRef mute_in_background;
    BoolSettingRef mute_in_background_if_other_audio;
    BoolSetting audio_volume_auto_apply;
    
    // Input Blocking (Background) Settings
    BoolSetting block_input_in_background;
    
    // Render Blocking (Background) Settings
    BoolSettingRef no_render_in_background;
    BoolSettingRef no_present_in_background;

private:
    std::vector<SettingBase*> all_settings_;
};

// Global instance
extern MainNewTabSettings g_main_new_tab_settings;

} // namespace ui::new_ui
