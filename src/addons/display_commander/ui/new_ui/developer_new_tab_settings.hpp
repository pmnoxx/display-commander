#pragma once

#include "settings_wrapper.hpp"
#include <vector>

namespace ui::new_ui {

// Developer tab settings manager
class DeveloperTabSettings {
public:
    DeveloperTabSettings();
    ~DeveloperTabSettings() = default;
    
    // Load all settings from ReShade config
    void LoadAll();
    
    // Save all settings to ReShade config
    void SaveAll();
    
    // Developer Settings
    ui::new_ui::BoolSetting prevent_fullscreen;
    ui::new_ui::BoolSetting spoof_fullscreen_state;
    ui::new_ui::BoolSetting spoof_window_focus;
    ui::new_ui::BoolSetting continuous_monitoring;
    ui::new_ui::BoolSetting prevent_always_on_top;
    ui::new_ui::BoolSetting remove_top_bar;
    
    // HDR and Colorspace Settings
    ui::new_ui::BoolSetting fix_hdr10_colorspace;
    
    // NVAPI Settings
    ui::new_ui::BoolSetting nvapi_fullscreen_prevention;
    ui::new_ui::BoolSetting nvapi_hdr_logging;
    ui::new_ui::FloatSetting nvapi_hdr_interval_sec;
    ui::new_ui::BoolSetting nvapi_force_hdr10;
    
    // Reflex Settings
    ui::new_ui::BoolSetting reflex_enabled;
    ui::new_ui::BoolSetting reflex_debug_output;
    
    // Sync Interval Settings
    ui::new_ui::ComboSetting sync_interval;
    
    // Get all settings for bulk operations
    std::vector<ui::new_ui::SettingBase*> GetAllSettings();
};

// Global instance
extern DeveloperTabSettings g_developerTabSettings;

} // namespace ui::new_ui
