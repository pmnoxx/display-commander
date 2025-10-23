#include "developer_tab_settings.hpp"
#include "../globals.hpp"

#include <minwindef.h>

#include <atomic>

// External declarations for the global variables
extern std::atomic<bool> s_background_feature_enabled;
extern std::atomic<bool> s_reflex_generate_markers;

namespace settings {

// Constructor - initialize all settings with proper keys and default values
DeveloperTabSettings::DeveloperTabSettings()
    : prevent_fullscreen("PreventFullscreen", true, "DisplayCommander"),
      continue_rendering("ContinueRendering", s_continue_rendering, false, "DisplayCommander"),
      continuous_monitoring("ContinuousMonitoring", s_continuous_monitoring_enabled, true, "DisplayCommander"),
      prevent_always_on_top("PreventAlwaysOnTop", s_prevent_always_on_top, true, "DisplayCommander"),
      hide_hdr_capabilities("HideHDRCapabilities", s_hide_hdr_capabilities, false, "DisplayCommander"),
      enable_flip_chain("EnableFlipChain", s_enable_flip_chain, false, "DisplayCommander"),
      auto_colorspace("AutoColorspace", s_auto_colorspace, false, "DisplayCommander"),
      //enable_d3d9e_upgrade("EnableD3D9EUpgrade", s_enable_d3d9e_upgrade, true, "DisplayCommander"),
      nvapi_fullscreen_prevention("NvapiFullscreenPrevention", s_nvapi_fullscreen_prevention, false,
                                  "DisplayCommander"),

      // Minimal NVIDIA Reflex controls
      reflex_auto_configure("ReflexAutoConfigure", s_reflex_auto_configure, false, "DisplayCommander"),
      reflex_enable("ReflexEnable", s_reflex_enable, false, "DisplayCommander"),
      reflex_low_latency("ReflexLowLatency", s_reflex_low_latency, false, "DisplayCommander"),
      reflex_boost("ReflexBoost", s_reflex_boost, false, "DisplayCommander"),
      reflex_use_markers("ReflexUseMarkers", s_reflex_use_markers, true, "DisplayCommander"),
      reflex_generate_markers("ReflexGenerateMarkers", s_reflex_generate_markers, true, "DisplayCommander"),
      reflex_enable_sleep("ReflexEnableSleep", s_reflex_enable_sleep, false, "DisplayCommander"),
      reflex_logging("ReflexLogging", s_enable_reflex_logging, false, "DisplayCommander"),
      reflex_supress_native("ReflexSupressNative", s_reflex_supress_native, false, "DisplayCommander"),

      enable_mute_unmute_shortcut("EnableMuteUnmuteShortcut", s_enable_mute_unmute_shortcut, true, "DisplayCommander"),
      enable_background_toggle_shortcut("EnableBackgroundToggleShortcut", s_enable_background_toggle_shortcut, true,
                                        "DisplayCommander"),
      enable_timeslowdown_shortcut("EnableTimeslowdownShortcut", s_enable_timeslowdown_shortcut, true, "DisplayCommander"),
      enable_adhd_toggle_shortcut("EnableAdhdToggleShortcut", s_enable_adhd_toggle_shortcut, true, "DisplayCommander"),
      enable_autoclick_shortcut("EnableAutoclickShortcut", s_enable_autoclick_shortcut, false, "DisplayCommander"),
      safemode("Safemode", false, "DisplayCommander"),
      load_from_dll_main("LoadFromDllMain", true, "DisplayCommander"),
      load_streamline("LoadStreamline", true, "DisplayCommander"),
      load_nvngx("LoadNvngx", true, "DisplayCommander"),
      load_nvapi64("LoadNvapi64", true, "DisplayCommander"),
      fake_nvapi_enabled("FakeNvapiEnabled", true, "DisplayCommander"),
      load_xinput("LoadXInput", true, "DisplayCommander") {}

void DeveloperTabSettings::LoadAll() {
    prevent_fullscreen.Load();
    continue_rendering.Load(); // This was missing!
    continuous_monitoring.Load();
    prevent_always_on_top.Load();
    hide_hdr_capabilities.Load();
    enable_flip_chain.Load();
    auto_colorspace.Load();
    //enable_d3d9e_upgrade.Load();
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

    // All Ref classes automatically save when values change
}

std::vector<ui::new_ui::SettingBase *> DeveloperTabSettings::GetAllSettings() {
    return {&prevent_fullscreen,
            &continue_rendering,
            &continuous_monitoring,
            &prevent_always_on_top,
            &hide_hdr_capabilities,
            &enable_flip_chain,
            &auto_colorspace,
            //&enable_d3d9e_upgrade,
            &nvapi_fullscreen_prevention,

            &reflex_auto_configure,
            &reflex_enable,
            &reflex_low_latency,
            &reflex_boost,
            &reflex_use_markers,
            &reflex_enable_sleep,
            &reflex_logging,
            &reflex_supress_native,

            &enable_mute_unmute_shortcut,
            &enable_background_toggle_shortcut,
            &enable_timeslowdown_shortcut,
            &enable_adhd_toggle_shortcut,
            &enable_autoclick_shortcut,
            &safemode,
            &load_from_dll_main,
            &load_streamline,
            &load_nvngx,
            &load_nvapi64,
            &fake_nvapi_enabled,
            &load_xinput};
}

} // namespace settings
