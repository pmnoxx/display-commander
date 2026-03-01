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

/** Manual color space when auto_colorspace is off. Index 0 = No changes; 1..MANUAL_COLOR_SPACE_MAX_INDEX = DXGI types
 * (see GetManualColorSpaceDisplayName). */
constexpr int MANUAL_COLOR_SPACE_MAX_INDEX = 22;

// Legacy enum: 0..4 map to first five entries (No changes, sRGB, scRGB, HDR10 ST2084, HDR10 HLG).
enum class ManualColorSpace : int {
    NoChanges = 0,
    sRGB = 1,
    scRGB = 2,
    HDR10_ST2084 = 3,
    HDR10_HLG = 4,
};

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
    BoolSetting prevent_fullscreen;
    BoolSetting continue_rendering;
    BoolSetting prevent_always_on_top;
    BoolSetting prevent_minimize;

    // HDR and Colorspace Settings
    BoolSetting hide_hdr_capabilities;
    BoolSetting enable_flip_chain;
    BoolSetting auto_colorspace;
    IntSetting manual_colorspace;  // persisted as int 0..MANUAL_COLOR_SPACE_MAX_INDEX; use Get/SetManualColorSpaceIndex

    int GetManualColorSpaceIndex() const;
    void SetManualColorSpaceIndex(int index);

    // D3D9 to D3D9Ex Upgrade
    // BoolSettingRef enable_d3d9e_upgrade;

    // NVAPI Settings
    BoolSetting nvapi_auto_enable_enabled;

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

    // DPI scaling disable setting
    BoolSetting disable_dpi_scaling;

    // Continuous monitoring: trigger toggles and intervals
    BoolSetting monitor_high_freq_enabled;
    IntSetting monitor_high_freq_interval_ms;
    BoolSetting monitor_per_second_enabled;
    IntSetting monitor_per_second_interval_sec;
    BoolSetting monitor_screensaver;
    BoolSetting monitor_fps_aggregate;
    BoolSetting monitor_volume;
    BoolSetting monitor_refresh_rate;
    BoolSetting monitor_vrr_status;
    BoolSetting monitor_exclusive_key_groups;
    BoolSetting monitor_discord_overlay;
    BoolSetting monitor_reflex_auto_configure;
    BoolSetting monitor_auto_apply_trigger;
    BoolSetting monitor_display_cache;
    IntSetting monitor_display_cache_interval_sec;

    // Get all settings for bulk operations
    std::vector<SettingBase*> GetAllSettings();
};

// Global instance
extern AdvancedTabSettings g_advancedTabSettings;

}  // namespace settings
