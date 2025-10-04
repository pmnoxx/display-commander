#include "developer_tab_settings.hpp"
#include "../globals.hpp"

#include <minwindef.h>

#include <atomic>

// External declarations for the global variables
extern std::atomic<bool> s_prevent_fullscreen;
extern std::atomic<int> s_spoof_fullscreen_state;
extern std::atomic<bool> s_continue_rendering;
extern std::atomic<bool> s_continuous_monitoring_enabled;
extern std::atomic<bool> s_prevent_always_on_top;
extern std::atomic<bool> s_background_feature_enabled;
extern std::atomic<bool> s_nvapi_fix_hdr10_colorspace;
extern std::atomic<bool> s_hide_hdr_capabilities;
extern std::atomic<bool> s_nvapi_fullscreen_prevention;
extern std::atomic<bool> s_nvapi_auto_enable;
extern std::atomic<bool> s_nvapi_hdr_logging;
extern std::atomic<float> s_nvapi_hdr_interval_sec;
extern std::atomic<bool> s_reflex_enable;
extern std::atomic<bool> s_reflex_low_latency;
extern std::atomic<bool> s_reflex_boost;
extern std::atomic<bool> s_reflex_use_markers;
extern std::atomic<bool> s_enable_reflex_logging;

extern std::atomic<bool> s_enable_mute_unmute_shortcut;
extern std::atomic<bool> s_enable_background_toggle_shortcut;
extern std::atomic<bool> s_enable_timeslowdown_shortcut;

extern std::atomic<bool> g_flush_before_present;

namespace settings {

// Constructor - initialize all settings with proper keys and default values
DeveloperTabSettings::DeveloperTabSettings()
    : prevent_fullscreen("PreventFullscreen", s_prevent_fullscreen, true, "DisplayCommander"),
      spoof_fullscreen_state("SpoofFullscreenState", s_spoof_fullscreen_state, 0, 0, 2, "DisplayCommander"),
      continue_rendering("ContinueRendering", s_continue_rendering, false, "DisplayCommander"),
      continuous_monitoring("ContinuousMonitoring", s_continuous_monitoring_enabled, true, "DisplayCommander"),
      prevent_always_on_top("PreventAlwaysOnTop", s_prevent_always_on_top, true, "DisplayCommander"),
      nvapi_fix_hdr10_colorspace("NvapiFixHDR10Colorspace", s_nvapi_fix_hdr10_colorspace, false, "DisplayCommander"),
      hide_hdr_capabilities("HideHDRCapabilities", s_hide_hdr_capabilities, false, "DisplayCommander"),
      nvapi_fullscreen_prevention("NvapiFullscreenPrevention", s_nvapi_fullscreen_prevention, false,
                                  "DisplayCommander"),
      nvapi_auto_enable("NvapiAutoEnable", s_nvapi_auto_enable, true, "DisplayCommander"),
      nvapi_hdr_logging("NvapiHDRLogging", s_nvapi_hdr_logging, false, "DisplayCommander"),
      nvapi_hdr_interval_sec("NvapiHDRInterval", s_nvapi_hdr_interval_sec, 5.0f, 1.0f, 60.0f, "DisplayCommander")

      // Minimal NVIDIA Reflex controls
      ,
      reflex_enable("ReflexEnable", s_reflex_enable, false, "DisplayCommander"),
      reflex_low_latency("ReflexLowLatency", s_reflex_low_latency, false, "DisplayCommander"),
      reflex_boost("ReflexBoost", s_reflex_boost, false, "DisplayCommander"),
      reflex_use_markers("ReflexUseMarkers", s_reflex_use_markers, true, "DisplayCommander"),
      reflex_logging("ReflexLogging", s_enable_reflex_logging, false, "DisplayCommander")

      ,
      enable_mute_unmute_shortcut("EnableMuteUnmuteShortcut", s_enable_mute_unmute_shortcut, true, "DisplayCommander"),
      enable_background_toggle_shortcut("EnableBackgroundToggleShortcut", s_enable_background_toggle_shortcut, true,
                                        "DisplayCommander"),
      enable_timeslowdown_shortcut("EnableTimeslowdownShortcut", s_enable_timeslowdown_shortcut, true, "DisplayCommander"),
      flush_before_present("FlushBeforePresent", g_flush_before_present, true, "DisplayCommander") {}

void DeveloperTabSettings::LoadAll() {
    prevent_fullscreen.Load();
    spoof_fullscreen_state.Load();
    continue_rendering.Load(); // This was missing!
    continuous_monitoring.Load();
    prevent_always_on_top.Load();
    nvapi_fix_hdr10_colorspace.Load();
    hide_hdr_capabilities.Load();
    nvapi_fullscreen_prevention.Load();
    nvapi_auto_enable.Load();
    nvapi_hdr_logging.Load();
    nvapi_hdr_interval_sec.Load();

    enable_mute_unmute_shortcut.Load();
    enable_background_toggle_shortcut.Load();
    enable_timeslowdown_shortcut.Load();
    flush_before_present.Load();
    reflex_enable.Load();
    reflex_low_latency.Load();
    reflex_boost.Load();
    reflex_use_markers.Load();
    reflex_logging.Load();

    // All Ref classes automatically sync with global variables
}

void DeveloperTabSettings::SaveAll() {
    // All Ref classes automatically save when values change
}

std::vector<ui::new_ui::SettingBase *> DeveloperTabSettings::GetAllSettings() {
    return {&prevent_fullscreen,
            &spoof_fullscreen_state,
            &continue_rendering,
            &continuous_monitoring,
            &prevent_always_on_top,
            &nvapi_fix_hdr10_colorspace,
            &hide_hdr_capabilities,
            &nvapi_fullscreen_prevention,
            &nvapi_auto_enable,
            &nvapi_hdr_logging,
            &nvapi_hdr_interval_sec,

            &reflex_enable,
            &reflex_low_latency,
            &reflex_boost,
            &reflex_use_markers,
            &reflex_logging,

            &enable_mute_unmute_shortcut,
            &enable_background_toggle_shortcut,
            &enable_timeslowdown_shortcut,
            &flush_before_present};
}

// Global instance is now defined in globals.cpp

// Legacy function for backward compatibility - now uses the new settings wrapper
void AddDeveloperNewTabSettings(std::vector<void *> &settings) {
    // This function is now deprecated in favor of the settings wrapper system
    // The settings are now managed by DeveloperTabSettings class
    // This function is kept for backward compatibility but doesn't add any new settings
}

} // namespace settings
