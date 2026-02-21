#include "advanced_tab_settings.hpp"
#include "../globals.hpp"

#include <minwindef.h>

#include <atomic>

// Reflex settings
std::atomic<bool> s_reflex_auto_configure{false};        // Disabled by default
std::atomic<bool> s_reflex_enable_current_frame{false};  // Enable NVIDIA Reflex integration for current frame
std::atomic<bool> s_reflex_supress_native{false};        // Disabled by default
std::atomic<bool> s_enable_reflex_logging{false};        // Disabled by default

// Shortcut settings
std::atomic<bool> s_enable_hotkeys{true};  // Enable hotkeys by default
std::atomic<bool> s_enable_mute_unmute_shortcut{true};
std::atomic<bool> s_enable_background_toggle_shortcut{false};
std::atomic<bool> s_enable_timeslowdown_shortcut{false};
std::atomic<bool> s_enable_adhd_toggle_shortcut{true};
std::atomic<bool> s_enable_autoclick_shortcut{false};
std::atomic<bool> s_enable_input_blocking_shortcut{false};
std::atomic<bool> s_enable_display_commander_ui_shortcut{true};
std::atomic<bool> s_enable_performance_overlay_shortcut{true};

// Input blocking toggle state (controlled by Ctrl+I)
std::atomic<bool> s_input_blocking_toggle{false};

namespace settings {

// Constructor - initialize all settings with proper keys and default values
AdvancedTabSettings::AdvancedTabSettings()
    : prevent_fullscreen("PreventFullscreen", true, "DisplayCommander"),
      continue_rendering("ContinueRendering", false, "DisplayCommander"),
      prevent_always_on_top("PreventAlwaysOnTop", true, "DisplayCommander"),
      prevent_minimize("PreventMinimize", false, "DisplayCommander"),
      hide_hdr_capabilities("HideHDRCapabilities", false, "DisplayCommander"),
      enable_flip_chain("EnableFlipChain", false, "DisplayCommander"),
      auto_colorspace("AutoColorspace", false, "DisplayCommander"),
      // enable_d3d9e_upgrade("EnableD3D9EUpgrade", s_enable_d3d9e_upgrade, true, "DisplayCommander"),
      nvapi_auto_enable_enabled("NvapiAutoEnableEnabled", true, "DisplayCommander"),

      // Minimal NVIDIA Reflex controls
      reflex_auto_configure("ReflexAutoConfigure", s_reflex_auto_configure, s_reflex_auto_configure.load(),
                            "DisplayCommander"),
      reflex_enable("ReflexEnable", false, "DisplayCommander"),
      reflex_delay_first_500_frames("ReflexDelayFirst500Frames", true, "DisplayCommander"),
      reflex_low_latency("ReflexLowLatency", true, "DisplayCommander"),
      reflex_boost("ReflexBoost", false, "DisplayCommander"),
      reflex_use_markers("ReflexUseMarkers", false, "DisplayCommander"),
      reflex_generate_markers("ReflexGenerateMarkers", false, "DisplayCommander"),
      reflex_enable_sleep("ReflexEnableSleep", false, "DisplayCommander"),
      reflex_logging("ReflexLogging", s_enable_reflex_logging, s_enable_reflex_logging.load(), "DisplayCommander"),
      reflex_supress_native("ReflexSupressNative", s_reflex_supress_native, s_reflex_supress_native.load(),
                            "DisplayCommander"),

      enable_hotkeys("EnableHotkeys", true, "DisplayCommander"),
      enable_mute_unmute_shortcut("EnableMuteUnmuteShortcut", s_enable_mute_unmute_shortcut,
                                  s_enable_mute_unmute_shortcut.load(), "DisplayCommander"),
      enable_background_toggle_shortcut("EnableBackgroundToggleShortcut", s_enable_background_toggle_shortcut,
                                        s_enable_background_toggle_shortcut.load(), "DisplayCommander"),
      enable_timeslowdown_shortcut("EnableTimeslowdownShortcut", s_enable_timeslowdown_shortcut,
                                   s_enable_timeslowdown_shortcut.load(), "DisplayCommander"),
      enable_adhd_toggle_shortcut("EnableAdhdToggleShortcut", s_enable_adhd_toggle_shortcut,
                                  s_enable_adhd_toggle_shortcut.load(), "DisplayCommander"),
      enable_autoclick_shortcut("EnableAutoclickShortcut", s_enable_autoclick_shortcut,
                                s_enable_autoclick_shortcut.load(), "DisplayCommander"),
      enable_input_blocking_shortcut("EnableInputBlockingShortcut", s_enable_input_blocking_shortcut,
                                     s_enable_input_blocking_shortcut.load(), "DisplayCommander"),
      enable_display_commander_ui_shortcut("EnableDisplayCommanderUiShortcut", s_enable_display_commander_ui_shortcut,
                                           s_enable_display_commander_ui_shortcut.load(), "DisplayCommander"),
      enable_performance_overlay_shortcut("EnablePerformanceOverlayShortcut", s_enable_performance_overlay_shortcut,
                                          s_enable_performance_overlay_shortcut.load(), "DisplayCommander"),
      safemode("Safemode", false, "DisplayCommander.Safemode"),
      dll_loading_delay_ms("DllLoadingDelayMs", 0, 0, 10000, "DisplayCommander"),
      dlls_to_load_before("DllsToLoadBefore", "", "DisplayCommander"),
      fake_nvapi_enabled("FakeNvapiEnabled", false, "DisplayCommander"),
      suppress_minhook("SuppressMinhook", false, "DisplayCommander.Safemode"),
      suppress_windows_gaming_input("SuppressWindowsGamingInput", true, "DisplayCommander"),
      debug_layer_enabled("DebugLayerEnabled", false, "DisplayCommander"),
      debug_break_on_severity("DebugBreakOnSeverity", false, "DisplayCommander"),
      auto_hide_discord_overlay("AutoHideDiscordOverlay", true, "DisplayCommander"),
      suppress_window_changes("SuppressWindowChanges", false, "DisplayCommander.Safemode"),
      enable_presentmon_tracing("EnablePresentMonTracing", true, "DisplayCommander"),
      disable_dpi_scaling("DisableDpiScaling", true, "DisplayCommander"),

      // Continuous monitoring
      monitor_high_freq_enabled("MonitorHighFreqEnabled", true, "DisplayCommander"),
      monitor_high_freq_interval_ms("MonitorHighFreqIntervalMs", 8, 5, 100, "DisplayCommander"),
      monitor_per_second_enabled("MonitorPerSecondEnabled", true, "DisplayCommander"),
      monitor_per_second_interval_sec("MonitorPerSecondIntervalSec", 1, 1, 60, "DisplayCommander"),
      monitor_screensaver("MonitorScreensaver", true, "DisplayCommander"),
      monitor_fps_aggregate("MonitorFpsAggregate", true, "DisplayCommander"),
      monitor_volume("MonitorVolume", true, "DisplayCommander"),
      monitor_refresh_rate("MonitorRefreshRate", true, "DisplayCommander"),
      monitor_vrr_status("MonitorVrrStatus", true, "DisplayCommander"),
      monitor_exclusive_key_groups("MonitorExclusiveKeyGroups", true, "DisplayCommander"),
      monitor_discord_overlay("MonitorDiscordOverlay", true, "DisplayCommander"),
      monitor_reflex_auto_configure("MonitorReflexAutoConfigure", true, "DisplayCommander"),
      monitor_auto_apply_trigger("MonitorAutoApplyTrigger", true, "DisplayCommander"),
      monitor_display_cache("MonitorDisplayCache", true, "DisplayCommander"),
      monitor_display_cache_interval_sec("MonitorDisplayCacheIntervalSec", 2, 1, 60, "DisplayCommander") {}

void AdvancedTabSettings::LoadAll() {
    // Get all settings for smart logging
    auto all_settings = GetAllSettings();

    // Use smart logging to show only changed settings
    ui::new_ui::LoadTabSettingsWithSmartLogging(all_settings, "Advanced Tab");

    // All Ref classes automatically sync with global variables
}

void AdvancedTabSettings::SaveAll() {
    // Save all settings that don't auto-save
    prevent_fullscreen.Save();
    continue_rendering.Save();
    hide_hdr_capabilities.Save();
    enable_flip_chain.Save();
    auto_colorspace.Save();
    nvapi_auto_enable_enabled.Save();
    enable_hotkeys.Save();
    safemode.Save();
    fake_nvapi_enabled.Save();
    suppress_minhook.Save();
    suppress_windows_gaming_input.Save();
    debug_layer_enabled.Save();
    debug_break_on_severity.Save();
    auto_hide_discord_overlay.Save();
    suppress_window_changes.Save();
    enable_presentmon_tracing.Save();
    disable_dpi_scaling.Save();

    monitor_high_freq_enabled.Save();
    monitor_high_freq_interval_ms.Save();
    monitor_per_second_enabled.Save();
    monitor_per_second_interval_sec.Save();
    monitor_screensaver.Save();
    monitor_fps_aggregate.Save();
    monitor_volume.Save();
    monitor_refresh_rate.Save();
    monitor_vrr_status.Save();
    monitor_exclusive_key_groups.Save();
    monitor_discord_overlay.Save();
    monitor_reflex_auto_configure.Save();
    monitor_auto_apply_trigger.Save();
    monitor_display_cache.Save();
    monitor_display_cache_interval_sec.Save();

    // All Ref classes automatically save when values change
}

std::vector<ui::new_ui::SettingBase*> AdvancedTabSettings::GetAllSettings() {
    return {&prevent_fullscreen, &continue_rendering, &prevent_always_on_top, &prevent_minimize, &hide_hdr_capabilities,
            &enable_flip_chain, &auto_colorspace,
            //&enable_d3d9e_upgrade,
            &nvapi_auto_enable_enabled,

            &reflex_auto_configure, &reflex_enable, &reflex_delay_first_500_frames, &reflex_low_latency, &reflex_boost,
            &reflex_use_markers, &reflex_generate_markers, &reflex_enable_sleep, &reflex_logging,
            &reflex_supress_native,

            &enable_hotkeys, &enable_mute_unmute_shortcut, &enable_background_toggle_shortcut,
            &enable_timeslowdown_shortcut, &enable_adhd_toggle_shortcut, &enable_autoclick_shortcut,
            &enable_input_blocking_shortcut, &enable_display_commander_ui_shortcut,
            &enable_performance_overlay_shortcut, &safemode, &dll_loading_delay_ms, &dlls_to_load_before,
            &fake_nvapi_enabled, &suppress_minhook, &suppress_windows_gaming_input, &debug_layer_enabled, &debug_break_on_severity,
            &auto_hide_discord_overlay, &suppress_window_changes, &enable_presentmon_tracing, &disable_dpi_scaling,

            &monitor_high_freq_enabled, &monitor_high_freq_interval_ms, &monitor_per_second_enabled,
            &monitor_per_second_interval_sec, &monitor_screensaver, &monitor_fps_aggregate, &monitor_volume,
            &monitor_refresh_rate, &monitor_vrr_status, &monitor_exclusive_key_groups, &monitor_discord_overlay,
            &monitor_reflex_auto_configure, &monitor_auto_apply_trigger, &monitor_display_cache,
            &monitor_display_cache_interval_sec};
}

}  // namespace settings
