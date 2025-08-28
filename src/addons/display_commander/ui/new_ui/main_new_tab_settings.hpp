#pragma once

#include "settings_wrapper.hpp"
#include <vector>

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
    FloatSetting fps_limit;
    FloatSetting fps_limit_background;
    
    // VSync & Tearing
    BoolSetting force_vsync_on;
    BoolSetting force_vsync_off;
    BoolSetting allow_tearing;
    BoolSetting prevent_tearing;

    
    // Audio Settings
    FloatSetting audio_volume_percent;
    BoolSetting audio_mute;
    BoolSetting mute_in_background;
    BoolSetting mute_in_background_if_other_audio;
    BoolSetting audio_volume_auto_apply;
    
    // Input Blocking (Background) Settings
    BoolSetting block_input_in_background;

private:
    std::vector<SettingBase*> all_settings_;
};

// Global instance
extern MainNewTabSettings g_main_new_tab_settings;

} // namespace ui::new_ui
