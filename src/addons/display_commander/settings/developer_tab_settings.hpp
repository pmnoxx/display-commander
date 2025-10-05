#pragma once

#include "../ui/new_ui/settings_wrapper.hpp"

#include <vector>

namespace settings {

// Bring setting types into scope
using ui::new_ui::BoolSettingRef;
using ui::new_ui::FloatSettingRef;
using ui::new_ui::IntSettingRef;
using ui::new_ui::SettingBase;

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
    BoolSettingRef prevent_fullscreen;
    IntSettingRef spoof_fullscreen_state;
    BoolSettingRef continue_rendering;
    BoolSettingRef continuous_monitoring;
    BoolSettingRef prevent_always_on_top;

    // HDR and Colorspace Settings
    BoolSettingRef nvapi_fix_hdr10_colorspace;
    BoolSettingRef hide_hdr_capabilities;

    // NVAPI Settings
    BoolSettingRef nvapi_fullscreen_prevention;
    BoolSettingRef nvapi_auto_enable;
    FloatSettingRef nvapi_hdr_interval_sec;

    // Keyboard Shortcut Settings (Experimental)
    BoolSettingRef enable_mute_unmute_shortcut;
    BoolSettingRef enable_background_toggle_shortcut;
    BoolSettingRef enable_timeslowdown_shortcut;

    // Performance optimization settings
    BoolSettingRef flush_before_present;

    // Minimal NVIDIA Reflex controls
    BoolSettingRef reflex_enable;
    BoolSettingRef reflex_low_latency;
    BoolSettingRef reflex_boost;
    BoolSettingRef reflex_use_markers;
    BoolSettingRef reflex_logging;

    // Get all settings for bulk operations
    std::vector<SettingBase *> GetAllSettings();
};

// Global instance
extern DeveloperTabSettings g_developerTabSettings;

} // namespace settings
