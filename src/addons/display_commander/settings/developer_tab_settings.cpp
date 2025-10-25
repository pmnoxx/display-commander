#include "developer_tab_settings.hpp"
#include "../globals.hpp"

#include <minwindef.h>

#include <atomic>

// Atomic variables for developer tab settings
std::atomic<bool> s_continue_rendering{false};            // Disabled by default
std::atomic<bool> s_continuous_monitoring_enabled{true};  // Enabled by default
std::atomic<bool> s_hide_hdr_capabilities{false};
std::atomic<bool> s_enable_flip_chain{false};
std::atomic<bool> s_auto_colorspace{false};
std::atomic<bool> s_nvapi_fullscreen_prevention{false};  // disabled by default

// Reflex settings
std::atomic<bool> s_reflex_auto_configure{false};  // Disabled by default
std::atomic<bool> s_reflex_enable{false};
std::atomic<bool> s_reflex_enable_current_frame{false};  // Enable NVIDIA Reflex integration for current frame
std::atomic<bool> s_reflex_low_latency{false};
std::atomic<bool> s_reflex_boost{false};
std::atomic<bool> s_reflex_use_markers{false};       // Use markers for optimization
std::atomic<bool> s_reflex_generate_markers{false};  // Generate markers in frame timeline
std::atomic<bool> s_reflex_enable_sleep{false};      // Disabled by default
std::atomic<bool> s_reflex_supress_native{false};    // Disabled by default
std::atomic<bool> s_enable_reflex_logging{false};    // Disabled by default

// Shortcut settings
std::atomic<bool> s_enable_mute_unmute_shortcut{true};
std::atomic<bool> s_enable_background_toggle_shortcut{true};
std::atomic<bool> s_enable_timeslowdown_shortcut{true};
std::atomic<bool> s_enable_adhd_toggle_shortcut{true};
std::atomic<bool> s_enable_autoclick_shortcut{false};

namespace settings {

// Constructor - initialize all settings with proper keys and default values
DeveloperTabSettings::DeveloperTabSettings()
    : prevent_fullscreen("PreventFullscreen", true, "DisplayCommander"),
      continue_rendering("ContinueRendering", s_continue_rendering, s_continue_rendering.load(), "DisplayCommander"),
      continuous_monitoring("ContinuousMonitoring", s_continuous_monitoring_enabled,
                            s_continuous_monitoring_enabled.load(), "DisplayCommander"),
      prevent_always_on_top("PreventAlwaysOnTop", true, "DisplayCommander"),
      hide_hdr_capabilities("HideHDRCapabilities", s_hide_hdr_capabilities, s_hide_hdr_capabilities.load(),
                            "DisplayCommander"),
      enable_flip_chain("EnableFlipChain", s_enable_flip_chain, s_enable_flip_chain.load(), "DisplayCommander"),
      auto_colorspace("AutoColorspace", s_auto_colorspace, s_auto_colorspace.load(), "DisplayCommander"),
      // enable_d3d9e_upgrade("EnableD3D9EUpgrade", s_enable_d3d9e_upgrade, true, "DisplayCommander"),
      nvapi_fullscreen_prevention("NvapiFullscreenPrevention", s_nvapi_fullscreen_prevention,
                                  s_nvapi_fullscreen_prevention.load(), "DisplayCommander"),

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
      safemode("Safemode", false, "DisplayCommander"),
      load_from_dll_main("LoadFromDllMain", true, "DisplayCommander"),
      load_streamline("LoadStreamline", true, "DisplayCommander"),
      load_nvngx("LoadNvngx", true, "DisplayCommander"),
      load_nvapi64("LoadNvapi64", true, "DisplayCommander"),
      fake_nvapi_enabled("FakeNvapiEnabled", true, "DisplayCommander"),
      load_xinput("LoadXInput", true, "DisplayCommander"),
      suppress_minhook("SuppressMinhook", false, "DisplayCommander"),
      debug_layer_enabled("DebugLayerEnabled", false, "DisplayCommander"),
      debug_break_on_severity("DebugBreakOnSeverity", false, "DisplayCommander") {}

void DeveloperTabSettings::LoadAll() {
    prevent_fullscreen.Load();
    continue_rendering.Load();  // This was missing!
    continuous_monitoring.Load();
    prevent_always_on_top.Load();
    hide_hdr_capabilities.Load();
    enable_flip_chain.Load();
    auto_colorspace.Load();
    // enable_d3d9e_upgrade.Load();
    nvapi_fullscreen_prevention.Load();

    reflex_auto_configure.Load();
    reflex_enable.Load();
    reflex_low_latency.Load();
    reflex_boost.Load();
    reflex_use_markers.Load();
    reflex_generate_markers.Load();
    reflex_enable_sleep.Load();
    reflex_logging.Load();
    reflex_supress_native.Load();

    enable_mute_unmute_shortcut.Load();
    enable_background_toggle_shortcut.Load();
    enable_timeslowdown_shortcut.Load();
    enable_adhd_toggle_shortcut.Load();
    enable_autoclick_shortcut.Load();
    safemode.Load();
    load_from_dll_main.Load();
    load_streamline.Load();
    load_nvngx.Load();
    load_nvapi64.Load();
    fake_nvapi_enabled.Load();
    load_xinput.Load();
    suppress_minhook.Load();
    debug_layer_enabled.Load();
    debug_break_on_severity.Load();

    // All Ref classes automatically sync with global variables
}

void DeveloperTabSettings::SaveAll() {
    // Save all settings that don't auto-save
    prevent_fullscreen.Save();
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
    return {&prevent_fullscreen, &continue_rendering, &continuous_monitoring, &prevent_always_on_top,
            &hide_hdr_capabilities, &enable_flip_chain, &auto_colorspace,
            //&enable_d3d9e_upgrade,
            &nvapi_fullscreen_prevention,

            &reflex_auto_configure, &reflex_enable, &reflex_low_latency, &reflex_boost, &reflex_use_markers,
            &reflex_enable_sleep, &reflex_logging, &reflex_supress_native,

            &enable_mute_unmute_shortcut, &enable_background_toggle_shortcut, &enable_timeslowdown_shortcut,
            &enable_adhd_toggle_shortcut, &enable_autoclick_shortcut, &safemode, &load_from_dll_main, &load_streamline,
            &load_nvngx, &load_nvapi64, &fake_nvapi_enabled, &load_xinput, &suppress_minhook, &debug_layer_enabled,
            &debug_break_on_severity};
}

}  // namespace settings
