#pragma once

#include "../ui/new_ui/settings_wrapper.hpp"

#include <vector>

namespace settings {

// Bring setting types into scope
using ui::new_ui::BoolSetting;
using ui::new_ui::IntSetting;
using ui::new_ui::OverrideBoolSetting;
using ui::new_ui::SettingBase;

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

    /** When true (default), DXGI Present detours flush the command queue before FPS limiter sleep (DX11/DX12). Reduces
     * input-to-display latency when limiter is active. */
    BoolSetting flush_command_queue_before_sleep;
    /** When true (default), enqueue GPU completion measurement from recorded present-update state (DX11/DX12). Used for
     * latency/GPU timing. */
    BoolSetting enqueue_gpu_completion;

    // HDR and Colorspace Settings
    BoolSetting hide_hdr_capabilities;
    BoolSetting enable_flip_chain;
    BoolSetting auto_colorspace;

    // D3D9 to D3D9Ex Upgrade
    // BoolSettingRef enable_d3d9e_upgrade;

    // Keyboard Shortcut Settings (Experimental)
    BoolSetting enable_hotkeys;
    BoolSetting enable_mute_unmute_shortcut;
    BoolSetting enable_background_toggle_shortcut;
    BoolSetting enable_timeslowdown_shortcut;
    BoolSetting enable_adhd_toggle_shortcut;
    BoolSetting enable_input_blocking_shortcut;
    BoolSetting enable_display_commander_ui_shortcut;
    BoolSetting enable_performance_overlay_shortcut;

    // Minimal NVIDIA Reflex controls
    BoolSetting reflex_auto_configure;
    BoolSetting reflex_enable;
    BoolSetting reflex_delay_first_500_frames;
    BoolSetting reflex_low_latency;
    BoolSetting reflex_boost;
    BoolSetting reflex_use_markers;
    BoolSetting reflex_generate_markers;
    BoolSetting reflex_enable_sleep;
    BoolSetting reflex_logging;
    //   BoolSetting reflex_supress_native;

    // Fake NVAPI setting
    BoolSetting fake_nvapi_enabled;

    /** Global WGI suppression (global_overrides.toml). When true, WGI suppression is on for all games. Default false.
     */
    OverrideBoolSetting suppress_wgi_globally;
    /** Master switch for WGI suppression (Controller tab). When false, WGI is never suppressed. Default false. */
    BoolSetting suppress_wgi_enabled;
    /** Only visible when UnityPlayer.dll is loaded. When true, suppress WGI for Unity games. Default false. */
    BoolSetting suppress_wgi_for_unity;
    /** Only visible when UnityPlayer.dll is not loaded. When true, suppress WGI for non-Unity games. Default false. */
    BoolSetting suppress_wgi_for_non_unity_games;

    // Debug break on severity setting
    BoolSetting debug_break_on_severity;

    // Window management compatibility setting
    BoolSetting suppress_window_changes;

    /** When true, DXGI Present detours signal the refresh rate monitor (SignalRefreshRateMonitor) for DXGI-based
     * refresh rate / VRR detection. Default false. */
    BoolSetting enable_dxgi_refresh_rate_vrr_detection;

    // Win+Up grace: seconds after leaving foreground when Win+Up (restore) still works. 0=disabled, 1-60=seconds,
    // 61=forever.
    IntSetting win_up_grace_seconds;

    // DPI scaling disable setting
    BoolSetting disable_dpi_scaling;

    // All settings (for load, UI, etc.)
    std::vector<SettingBase*> GetAllSettings();

    // Subset of settings that are persisted on SaveAll (excludes e.g. reflex, shortcut toggles, some dev options)
    std::vector<SettingBase*> GetSettingsToSave();
};

// Global instance
extern AdvancedTabSettings g_advancedTabSettings;

}  // namespace settings
