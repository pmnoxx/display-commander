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
    ui::new_ui::BoolSettingRef remove_top_bar;
    
    // HDR and Colorspace Settings
    ui::new_ui::BoolSettingRef fix_hdr10_colorspace;
    
    // NVAPI Settings
    ui::new_ui::BoolSettingRef nvapi_fullscreen_prevention;
    ui::new_ui::BoolSettingRef nvapi_hdr_logging;
    ui::new_ui::FloatSettingRef nvapi_hdr_interval_sec;
    
    // Reflex Settings
    ui::new_ui::BoolSettingRef reflex_enabled;
    ui::new_ui::BoolSettingRef reflex_low_latency_mode;
    ui::new_ui::BoolSettingRef reflex_low_latency_boost;
    ui::new_ui::BoolSettingRef reflex_use_markers;
    ui::new_ui::BoolSettingRef reflex_debug_output;
    // FPS limiter: extra wait before SIMULATION_START (ms)
    ui::new_ui::FloatSettingRef fps_extra_wait_ms;
    
    // Experimental/Unstable features toggle
    ui::new_ui::BoolSettingRef enable_unstable_reshade_features;
    

    
    // Keyboard Shortcut Settings (Experimental)
    ui::new_ui::BoolSettingRef enable_mute_unmute_shortcut;
    
    // Performance optimization settings
    ui::new_ui::BoolSettingRef flush_before_present;
    
    // Get all settings for bulk operations
    std::vector<ui::new_ui::SettingBase*> GetAllSettings();
};

// Global instance
extern DeveloperTabSettings g_developerTabSettings;

} // namespace ui::new_ui
