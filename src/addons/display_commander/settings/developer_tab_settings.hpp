#pragma once

#include "../ui/new_ui/settings_wrapper.hpp"

#include <vector>

namespace settings {

// Bring setting types into scope
using ui::new_ui::BoolSetting;
using ui::new_ui::BoolSettingRef;
using ui::new_ui::FloatSettingRef;
using ui::new_ui::IntSettingRef;
using ui::new_ui::SettingBase;

// Developer tab settings manager
class DeveloperTabSettings {
   public:
    DeveloperTabSettings();
    ~DeveloperTabSettings() = default;

    // Load all settings from DisplayCommander config
    void LoadAll();

    // Save all settings to DisplayCommander config
    void SaveAll();

    // Developer Settings
    BoolSetting prevent_fullscreen;
    BoolSettingRef continue_rendering;
    BoolSettingRef continuous_monitoring;
    BoolSetting prevent_always_on_top;

    // HDR and Colorspace Settings
    BoolSettingRef hide_hdr_capabilities;
    BoolSettingRef enable_flip_chain;
    BoolSettingRef auto_colorspace;

    // D3D9 to D3D9Ex Upgrade
    // BoolSettingRef enable_d3d9e_upgrade;

    // NVAPI Settings
    BoolSettingRef nvapi_fullscreen_prevention;

    // Keyboard Shortcut Settings (Experimental)
    BoolSettingRef enable_mute_unmute_shortcut;
    BoolSettingRef enable_background_toggle_shortcut;
    BoolSettingRef enable_timeslowdown_shortcut;
    BoolSettingRef enable_adhd_toggle_shortcut;
    BoolSettingRef enable_autoclick_shortcut;

    // Minimal NVIDIA Reflex controls
    BoolSettingRef reflex_auto_configure;
    BoolSettingRef reflex_enable;
    BoolSettingRef reflex_low_latency;
    BoolSettingRef reflex_boost;
    BoolSettingRef reflex_use_markers;
    BoolSettingRef reflex_generate_markers;
    BoolSettingRef reflex_enable_sleep;
    BoolSettingRef reflex_logging;
    BoolSettingRef reflex_supress_native;

    // Safemode setting
    BoolSetting safemode;

    // ReShade LoadFromDllMain setting
    BoolSetting load_from_dll_main;

    // Streamline loading setting
    BoolSetting load_streamline;

    // NGX loading setting
    BoolSetting load_nvngx;

    // NVAPI loading setting
    BoolSetting load_nvapi64;

    // Fake NVAPI setting
    BoolSetting fake_nvapi_enabled;

    // XInput loading setting
    BoolSetting load_xinput;

    // MinHook suppression setting
    BoolSetting suppress_minhook;

    // Debug Layer setting
    BoolSetting debug_layer_enabled;
    BoolSetting debug_break_on_severity;

    // Get all settings for bulk operations
    std::vector<SettingBase*> GetAllSettings();
};

// Global instance
extern DeveloperTabSettings g_developerTabSettings;

}  // namespace settings
