#include "advanced_tab_settings.hpp"
#include "../globals.hpp"

#include <minwindef.h>

#include <atomic>

// Reflex: enable for current frame only (not a persisted setting)
std::atomic<bool> s_reflex_enable_current_frame{false};

// Hotkeys: synced from Hotkeys tab for fast read path
std::atomic<bool> s_enable_hotkeys{true};

// Input blocking toggle state (controlled by Ctrl+I)
std::atomic<bool> s_input_blocking_toggle{false};

namespace settings {

// Constructor - initialize all settings with proper keys and default values
AdvancedTabSettings::AdvancedTabSettings()
    : continue_rendering("ContinueRendering", false, "DisplayCommander"),
      prevent_always_on_top("PreventAlwaysOnTop", true, "DisplayCommander"),
      prevent_minimize("PreventMinimize", true, "DisplayCommander"),
      hide_hdr_capabilities("HideHDRCapabilities", false, "DisplayCommander"),
      enable_flip_chain("EnableFlipChain", false, "DisplayCommander"),
      auto_colorspace("AutoColorspace2", true, "DisplayCommander"),

      // Minimal NVIDIA Reflex controls
      reflex_auto_configure("ReflexAutoConfigure", false, "DisplayCommander"),
      reflex_enable("ReflexEnable", false, "DisplayCommander"),
      reflex_delay_first_500_frames("ReflexDelayFirst500Frames", true, "DisplayCommander"),
      reflex_low_latency("ReflexLowLatency", true, "DisplayCommander"),
      reflex_boost("ReflexBoost", false, "DisplayCommander"),
      reflex_use_markers("ReflexUseMarkers", false, "DisplayCommander"),
      reflex_generate_markers("ReflexGenerateMarkers", false, "DisplayCommander"),
      reflex_enable_sleep("ReflexEnableSleep", false, "DisplayCommander"),
      reflex_logging("ReflexLogging", false, "DisplayCommander"),
      reflex_supress_native("ReflexSupressNative", false, "DisplayCommander"),

      enable_hotkeys("EnableHotkeys", true, "DisplayCommander"),
      enable_mute_unmute_shortcut("EnableMuteUnmuteShortcut", true, "DisplayCommander"),
      enable_background_toggle_shortcut("EnableBackgroundToggleShortcut", false, "DisplayCommander"),
      enable_timeslowdown_shortcut("EnableTimeslowdownShortcut", false, "DisplayCommander"),
      enable_adhd_toggle_shortcut("EnableAdhdToggleShortcut", true, "DisplayCommander"),
      enable_autoclick_shortcut("EnableAutoclickShortcut", false, "DisplayCommander"),
      enable_input_blocking_shortcut("EnableInputBlockingShortcut", false, "DisplayCommander"),
      enable_display_commander_ui_shortcut("EnableDisplayCommanderUiShortcut", true, "DisplayCommander"),
      enable_performance_overlay_shortcut("EnablePerformanceOverlayShortcut", true, "DisplayCommander"),
      safemode("Safemode", false, "DisplayCommander.Safemode"),
      dll_loading_delay_ms("DllLoadingDelayMs", 0, 0, 10000, "DisplayCommander"),
      dlls_to_load_before("DllsToLoadBefore", "", "DisplayCommander"),
      fake_nvapi_enabled("FakeNvapiEnabled", false, "DisplayCommander"),
      suppress_minhook("SuppressMinhook", false, "DisplayCommander.Safemode"),
      suppress_wgi_for_unity("SuppressWgiForUnity", true, "DisplayCommander"),
      suppress_wgi_for_non_unity_games("SuppressWgiForNonUnityGames", false, "DisplayCommander"),
      debug_layer_enabled("DebugLayerEnabled", false, "DisplayCommander"),
      debug_break_on_severity("DebugBreakOnSeverity", false, "DisplayCommander"),
      auto_hide_discord_overlay("AutoHideDiscordOverlay", true, "DisplayCommander"),
      suppress_window_changes("SuppressWindowChanges", false, "DisplayCommander.Safemode"),
      win_up_grace_seconds("WinUpGraceSeconds", 1, 0, 61, "DisplayCommander"),
      enable_presentmon_tracing("EnablePresentMonTracing", false, "DisplayCommander"),
      presentmon_provider_dxgkrnl("PresentMonProviderDxgKrnl", false, "DisplayCommander"),
      presentmon_provider_dxgi("PresentMonProviderDXGI", false, "DisplayCommander"),
      presentmon_provider_dwm("PresentMonProviderDwm", true, "DisplayCommander"),
      presentmon_provider_d3d9("PresentMonProviderD3D9", false, "DisplayCommander"),
      disable_dpi_scaling("DisableDpiScaling", true, "DisplayCommander"),
      enable_dx11_vtable_hooks("EnableDx11VtableHooks", false, "DisplayCommander"),
      texture_tracking_enabled("TextureTrackingEnabled", false, "DisplayCommander"),
      d3d11_texture_caching_enabled("D3D11TextureCachingEnabled", false, "DisplayCommander"),
      d3d11_texture_caching_1d_enabled("D3D11TextureCaching1DEnabled", false, "DisplayCommander"),
      d3d11_texture_caching_3d_enabled("D3D11TextureCaching3DEnabled", false, "DisplayCommander"),
      texture_cache_content_hash_cap_kb("TextureCacheContentHashCapKb", 64, 1, 1048576, "DisplayCommander"),
      dump_textures_enabled("DumpTexturesEnabled", false, "DisplayCommander"),
      show_steam_achievement_notifications("ShowSteamAchievementNotifications", false, "DisplayCommander"),
      show_steam_achievement_counter_increased("ShowSteamAchievementCounterIncreased", false, "DisplayCommander") {}

void AdvancedTabSettings::LoadAll() {
    // Get all settings for smart logging
    auto all_settings = GetAllSettings();

    // Use smart logging to show only changed settings
    ui::new_ui::LoadTabSettingsWithSmartLogging(all_settings, "Advanced Tab");
}

void AdvancedTabSettings::SaveAll() {
    // Save all settings that don't auto-save
    continue_rendering.Save();
    hide_hdr_capabilities.Save();
    enable_flip_chain.Save();
    auto_colorspace.Save();
    enable_hotkeys.Save();
    safemode.Save();
    fake_nvapi_enabled.Save();
    suppress_minhook.Save();
    suppress_wgi_for_unity.Save();
    suppress_wgi_for_non_unity_games.Save();
    debug_layer_enabled.Save();
    debug_break_on_severity.Save();
    auto_hide_discord_overlay.Save();
    suppress_window_changes.Save();
    enable_presentmon_tracing.Save();
    presentmon_provider_dxgkrnl.Save();
    presentmon_provider_dxgi.Save();
    presentmon_provider_dwm.Save();
    presentmon_provider_d3d9.Save();
    disable_dpi_scaling.Save();
    enable_dx11_vtable_hooks.Save();
    texture_tracking_enabled.Save();
    d3d11_texture_caching_enabled.Save();
    d3d11_texture_caching_1d_enabled.Save();
    d3d11_texture_caching_3d_enabled.Save();
    texture_cache_content_hash_cap_kb.Save();
    dump_textures_enabled.Save();
    show_steam_achievement_notifications.Save();
    show_steam_achievement_counter_increased.Save();
}

std::vector<ui::new_ui::SettingBase*> AdvancedTabSettings::GetAllSettings() {
    return {
        &continue_rendering, &prevent_always_on_top, &prevent_minimize, &hide_hdr_capabilities, &enable_flip_chain,
        &auto_colorspace,
        //&enable_d3d9e_upgrade,

        &reflex_auto_configure, &reflex_enable, &reflex_delay_first_500_frames, &reflex_low_latency, &reflex_boost,
        &reflex_use_markers, &reflex_generate_markers, &reflex_enable_sleep, &reflex_logging, &reflex_supress_native,

        &enable_hotkeys, &enable_mute_unmute_shortcut, &enable_background_toggle_shortcut,
        &enable_timeslowdown_shortcut, &enable_adhd_toggle_shortcut, &enable_autoclick_shortcut,
        &enable_input_blocking_shortcut, &enable_display_commander_ui_shortcut, &enable_performance_overlay_shortcut,
        &safemode, &dll_loading_delay_ms, &dlls_to_load_before, &fake_nvapi_enabled, &suppress_minhook,
        &suppress_wgi_for_unity, &suppress_wgi_for_non_unity_games, &debug_layer_enabled, &debug_break_on_severity,
        &auto_hide_discord_overlay, &suppress_window_changes, &win_up_grace_seconds, &enable_presentmon_tracing,
        &presentmon_provider_dxgkrnl, &presentmon_provider_dxgi, &presentmon_provider_dwm, &presentmon_provider_d3d9,
        &disable_dpi_scaling, &enable_dx11_vtable_hooks, &texture_tracking_enabled, &d3d11_texture_caching_enabled,
        &d3d11_texture_caching_1d_enabled, &d3d11_texture_caching_3d_enabled, &texture_cache_content_hash_cap_kb,
        &dump_textures_enabled, &show_steam_achievement_notifications, &show_steam_achievement_counter_increased};
}

}  // namespace settings
