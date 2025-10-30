#include "developer_tab_settings.hpp"
#include "../globals.hpp"

#include <minwindef.h>

#include <atomic>

// Atomic variables for developer tab settings
std::atomic<bool> s_continue_rendering{false};            // Disabled by default
std::atomic<bool> s_hide_hdr_capabilities{false};
std::atomic<bool> s_enable_flip_chain{false};
std::atomic<bool> s_auto_colorspace{false};
std::atomic<bool> s_nvapi_auto_enable_enabled{true};    // enabled by default

// Reflex settings
std::atomic<bool> s_reflex_auto_configure{false};  // Disabled by default
std::atomic<bool> s_reflex_enable{false};
std::atomic<bool> s_reflex_enable_current_frame{false};  // Enable NVIDIA Reflex integration for current frame
std::atomic<bool> s_reflex_low_latency{true};
std::atomic<bool> s_reflex_boost{true};
std::atomic<bool> s_reflex_use_markers{false};       // Use markers for optimization
std::atomic<bool> s_reflex_generate_markers{false};  // Generate markers in frame timeline
std::atomic<bool> s_reflex_enable_sleep{false};      // Disabled by default
std::atomic<bool> s_reflex_supress_native{false};    // Disabled by default
std::atomic<bool> s_enable_reflex_logging{false};    // Disabled by default

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
DeveloperTabSettings::DeveloperTabSettings()
    : prevent_fullscreen("PreventFullscreen", true, "DisplayCommander"),
      continue_rendering("ContinueRendering", s_continue_rendering, s_continue_rendering.load(), "DisplayCommander"),
      prevent_always_on_top("PreventAlwaysOnTop", true, "DisplayCommander"),
      hide_hdr_capabilities("HideHDRCapabilities", s_hide_hdr_capabilities, s_hide_hdr_capabilities.load(),
                            "DisplayCommander"),
      enable_flip_chain("EnableFlipChain", s_enable_flip_chain, s_enable_flip_chain.load(), "DisplayCommander"),
      auto_colorspace("AutoColorspace", s_auto_colorspace, s_auto_colorspace.load(), "DisplayCommander"),
      // enable_d3d9e_upgrade("EnableD3D9EUpgrade", s_enable_d3d9e_upgrade, true, "DisplayCommander"),
      nvapi_auto_enable_enabled("NvapiAutoEnableEnabled", s_nvapi_auto_enable_enabled,
                                s_nvapi_auto_enable_enabled.load(), "DisplayCommander"),

      // Minimal NVIDIA Reflex controls
      reflex_auto_configure("ReflexAutoConfigure", s_reflex_auto_configure, s_reflex_auto_configure.load(),
                            "DisplayCommander"),
      reflex_enable("ReflexEnable", s_reflex_enable, s_reflex_enable.load(), "DisplayCommander"),
      reflex_low_latency("ReflexLowLatency", s_reflex_low_latency, s_reflex_low_latency.load(), "DisplayCommander"),
      reflex_boost("ReflexBoost", s_reflex_boost, s_reflex_boost.load(), "DisplayCommander"),
      reflex_use_markers("ReflexUseMarkers", s_reflex_use_markers, s_reflex_use_markers.load(), "DisplayCommander"),
      reflex_generate_markers("ReflexGenerateMarkers", s_reflex_generate_markers, s_reflex_generate_markers.load(),
                              "DisplayCommander"),
      reflex_enable_sleep("ReflexEnableSleep", s_reflex_enable_sleep, s_reflex_enable_sleep.load(), "DisplayCommander"),
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
      load_from_dll_main("LoadFromDllMain", true, "DisplayCommander"),
      load_streamline("LoadStreamline", true, "DisplayCommander"),
      load_nvngx("LoadNvngx", true, "DisplayCommander"),
      load_nvapi64("LoadNvapi64", true, "DisplayCommander"),
      fake_nvapi_enabled("FakeNvapiEnabled", false, "DisplayCommander"),
      load_xinput("LoadXInput", true, "DisplayCommander"),
      suppress_minhook("SuppressMinhook", false, "DisplayCommander"),
      debug_layer_enabled("DebugLayerEnabled", false, "DisplayCommander"),
      debug_break_on_severity("DebugBreakOnSeverity", false, "DisplayCommander") {}

void DeveloperTabSettings::LoadAll() {
    // Get all settings for smart logging
    auto all_settings = GetAllSettings();

    // Use smart logging to show only changed settings
    ui::new_ui::LoadTabSettingsWithSmartLogging(all_settings, "Developer Tab");

    // All Ref classes automatically sync with global variables
}

void DeveloperTabSettings::SaveAll() {
    // Save all settings that don't auto-save
    prevent_fullscreen.Save();
    enable_hotkeys.Save();
    safemode.Save();
    load_from_dll_main.Save();
    load_streamline.Save();
    load_nvngx.Save();
    load_nvapi64.Save();
    fake_nvapi_enabled.Save();
    load_xinput.Save();
    suppress_minhook.Save();
    debug_layer_enabled.Save();
    debug_break_on_severity.Save();

    // All Ref classes automatically save when values change
}

std::vector<ui::new_ui::SettingBase*> DeveloperTabSettings::GetAllSettings() {
    return {&prevent_fullscreen, &continue_rendering, &prevent_always_on_top,
            &hide_hdr_capabilities, &enable_flip_chain, &auto_colorspace,
            //&enable_d3d9e_upgrade,
            &nvapi_auto_enable_enabled,

            &reflex_auto_configure, &reflex_enable, &reflex_low_latency, &reflex_boost, &reflex_use_markers,
            &reflex_enable_sleep, &reflex_logging, &reflex_supress_native,

            &enable_hotkeys, &enable_mute_unmute_shortcut, &enable_background_toggle_shortcut, &enable_timeslowdown_shortcut,
            &enable_adhd_toggle_shortcut, &enable_autoclick_shortcut, &enable_input_blocking_shortcut, &enable_display_commander_ui_shortcut, &enable_performance_overlay_shortcut, &safemode, &load_from_dll_main, &load_streamline,
            &load_nvngx, &load_nvapi64, &fake_nvapi_enabled, &load_xinput, &suppress_minhook, &debug_layer_enabled,
            &debug_break_on_severity};
}

}  // namespace settings
