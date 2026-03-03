#pragma once

#include "../ui/new_ui/settings_wrapper.hpp"

#include <vector>

namespace settings {

// Bring setting types into scope
using ui::new_ui::BoolSetting;
using ui::new_ui::BoolSettingRef;
using ui::new_ui::FloatSettingRef;
using ui::new_ui::IntSetting;
using ui::new_ui::IntSettingRef;
using ui::new_ui::SettingBase;
using ui::new_ui::StringSetting;

// Advanced tab settings manager
class AdvancedTabSettings {
   public:
    AdvancedTabSettings();
    ~AdvancedTabSettings() = default;

    // Load all settings from DisplayCommander config
    void LoadAll();

    // Save all settings to DisplayCommander config
    void SaveAll();

    // Developer Settings
    BoolSetting continue_rendering;
    BoolSetting prevent_always_on_top;
    BoolSetting prevent_minimize;

    // HDR and Colorspace Settings
    BoolSetting hide_hdr_capabilities;
    BoolSetting enable_flip_chain;
    BoolSetting auto_colorspace;

    // D3D9 to D3D9Ex Upgrade
    // BoolSettingRef enable_d3d9e_upgrade;

    // Keyboard Shortcut Settings (Experimental)
    BoolSetting enable_hotkeys;
    BoolSettingRef enable_mute_unmute_shortcut;
    BoolSettingRef enable_background_toggle_shortcut;
    BoolSettingRef enable_timeslowdown_shortcut;
    BoolSettingRef enable_adhd_toggle_shortcut;
    BoolSettingRef enable_autoclick_shortcut;
    BoolSettingRef enable_input_blocking_shortcut;
    BoolSettingRef enable_display_commander_ui_shortcut;
    BoolSettingRef enable_performance_overlay_shortcut;

    // Minimal NVIDIA Reflex controls
    BoolSettingRef reflex_auto_configure;
    BoolSetting reflex_enable;
    BoolSetting reflex_delay_first_500_frames;
    BoolSetting reflex_low_latency;
    BoolSetting reflex_boost;
    BoolSetting reflex_use_markers;
    BoolSetting reflex_generate_markers;
    BoolSetting reflex_enable_sleep;
    BoolSettingRef reflex_logging;
    BoolSettingRef reflex_supress_native;

    // Safemode setting
    BoolSetting safemode;

    // DLL loading delay setting (milliseconds)
    IntSetting dll_loading_delay_ms;

    // DLLs to load before Display Commander (comma-separated list)
    StringSetting dlls_to_load_before;

    // Fake NVAPI setting
    BoolSetting fake_nvapi_enabled;

    // MinHook suppression setting
    BoolSetting suppress_minhook;

    /** Only visible when UnityPlayer.dll is loaded. When true, suppress WGI for Unity games. Default false. */
    BoolSetting suppress_wgi_for_unity;
    /** Only visible when UnityPlayer.dll is not loaded. When true, suppress WGI for non-Unity games. Default true. */
    BoolSetting suppress_wgi_for_non_unity_games;

    // Debug Layer setting
    BoolSetting debug_layer_enabled;
    BoolSetting debug_break_on_severity;

    // Discord Overlay auto-hide setting
    BoolSetting auto_hide_discord_overlay;

    // Window management compatibility setting
    BoolSetting suppress_window_changes;

    // Win+Up grace: seconds after leaving foreground when Win+Up (restore) still works. 0=disabled, 1-60=seconds,
    // 61=forever.
    IntSetting win_up_grace_seconds;

    // PresentMon ETW tracing setting
    BoolSetting enable_presentmon_tracing;
    // Which ETW providers to subscribe to (take effect on next PresentMon start)
    BoolSetting presentmon_provider_dxgkrnl;
    BoolSetting presentmon_provider_dxgi;
    BoolSetting presentmon_provider_dwm;
    BoolSetting presentmon_provider_d3d9;

    // DPI scaling disable setting
    BoolSetting disable_dpi_scaling;

    // Get all settings for bulk operations
    std::vector<SettingBase*> GetAllSettings();
};

// Global instance
extern AdvancedTabSettings g_advancedTabSettings;

}  // namespace settings
