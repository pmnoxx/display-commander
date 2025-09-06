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
    ui::new_ui::BoolSettingRef prevent_fullscreen;
    ui::new_ui::IntSettingRef spoof_fullscreen_state;
    ui::new_ui::IntSettingRef spoof_window_focus;
    ui::new_ui::BoolSettingRef continuous_monitoring;
    ui::new_ui::BoolSettingRef prevent_always_on_top;

    // HDR and Colorspace Settings
    ui::new_ui::BoolSettingRef fix_hdr10_colorspace;

    // NVAPI Settings
    ui::new_ui::BoolSettingRef nvapi_fullscreen_prevention;
    ui::new_ui::BoolSettingRef nvapi_hdr_logging;
    ui::new_ui::FloatSettingRef nvapi_hdr_interval_sec;

    // Keyboard Shortcut Settings (Experimental)
    ui::new_ui::BoolSettingRef enable_mute_unmute_shortcut;

    // Performance optimization settings
    ui::new_ui::BoolSettingRef flush_before_present;

    // Minimal NVIDIA Reflex controls
    ui::new_ui::BoolSettingRef reflex_enable;
    ui::new_ui::BoolSettingRef reflex_low_latency;
    ui::new_ui::BoolSettingRef reflex_boost;
    ui::new_ui::BoolSettingRef reflex_use_markers;
    ui::new_ui::BoolSettingRef reflex_logging;

    // Get all settings for bulk operations
    std::vector<ui::new_ui::SettingBase*> GetAllSettings();
};

// Global instance
extern DeveloperTabSettings g_developerTabSettings;

} // namespace ui::new_ui
