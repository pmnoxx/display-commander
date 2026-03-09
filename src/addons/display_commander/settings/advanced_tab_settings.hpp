#pragma once

#include "../ui/new_ui/settings_wrapper.hpp"

#include <vector>

namespace settings {

// Bring setting types into scope
using ui::new_ui::BoolSetting;
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
    BoolSetting enable_mute_unmute_shortcut;
    BoolSetting enable_background_toggle_shortcut;
    BoolSetting enable_timeslowdown_shortcut;
    BoolSetting enable_adhd_toggle_shortcut;
    BoolSetting enable_autoclick_shortcut;
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
    BoolSetting reflex_supress_native;

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

    /** Global WGI suppression (global_settings.toml). When true, WGI suppression is on for all games. Default false. */
    BoolSetting suppress_wgi_globally;
    /** Master switch for WGI suppression (Controller tab). When false, WGI is never suppressed. Default false. */
    BoolSetting suppress_wgi_enabled;
    /** Only visible when UnityPlayer.dll is loaded. When true, suppress WGI for Unity games. Default false. */
    BoolSetting suppress_wgi_for_unity;
    /** Only visible when UnityPlayer.dll is not loaded. When true, suppress WGI for non-Unity games. Default false. */
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

    // Enable D3D11 device vtable hooks (HookD3D11DeviceVTable). Required for track loaded texture size. Off by default.
    BoolSetting enable_dx11_vtable_hooks;
    // Optional texture memory tracking (tracks loaded texture size and hooks IUnknown::Release). Off by default.
    BoolSetting texture_tracking_enabled;
    // D3D11 texture caching: cache CreateTexture2D results by content hash; no eviction, no size limit. Off by default.
    BoolSetting d3d11_texture_caching_enabled;
    // Same for CreateTexture1D / CreateTexture3D. Off by default.
    BoolSetting d3d11_texture_caching_1d_enabled;
    BoolSetting d3d11_texture_caching_3d_enabled;
    // Max bytes of initial data to include in content hash (in KB). Default 64 = 64 KB. Range 1..1048576 (1 GB).
    IntSetting texture_cache_content_hash_cap_kb;
    // When enabled, dump D3D11 CreateTexture* initial data to dumped_textures folder as .dds (only when pInitialData
    // is provided). Off by default.
    BoolSetting dump_textures_enabled;

    // Steam achievement overlay: show notifications even when performance overlay is off. Off by default.
    BoolSetting show_steam_achievement_notifications;
    // When enabled, show a message whenever the achievement unlocked count increases. Off by default.
    BoolSetting show_steam_achievement_counter_increased;
    // When enabled, play a system sound when a new Steam achievement is unlocked. Off by default.
    BoolSetting play_sound_on_achievement;

    // Get all settings for bulk operations
    std::vector<SettingBase*> GetAllSettings();
};

// Global instance
extern AdvancedTabSettings g_advancedTabSettings;

}  // namespace settings
