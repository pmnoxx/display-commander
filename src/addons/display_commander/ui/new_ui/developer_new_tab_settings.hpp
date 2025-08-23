#pragma once

#include "settings_wrapper.hpp"
#include <vector>

namespace renodx::ui::new_ui {

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
    renodx::ui::new_ui::BoolSetting prevent_fullscreen;
    renodx::ui::new_ui::BoolSetting spoof_fullscreen_state;
    renodx::ui::new_ui::BoolSetting spoof_window_focus;
    renodx::ui::new_ui::BoolSetting continuous_monitoring;
    renodx::ui::new_ui::BoolSetting prevent_always_on_top;
    renodx::ui::new_ui::BoolSetting remove_top_bar;
    
    // HDR and Colorspace Settings
    renodx::ui::new_ui::BoolSetting fix_hdr10_colorspace;
    
    // NVAPI Settings
    renodx::ui::new_ui::BoolSetting nvapi_fullscreen_prevention;
    renodx::ui::new_ui::BoolSetting nvapi_hdr_logging;
    renodx::ui::new_ui::FloatSetting nvapi_hdr_interval_sec;
    renodx::ui::new_ui::BoolSetting nvapi_force_hdr10;
    
    // Reflex Settings
    renodx::ui::new_ui::BoolSetting reflex_enabled;
    renodx::ui::new_ui::BoolSetting reflex_debug_output;
    
    // Get all settings for bulk operations
    std::vector<renodx::ui::new_ui::SettingBase*> GetAllSettings();
};

// Global instance
extern DeveloperTabSettings g_developerTabSettings;

} // namespace renodx::ui::new_ui
