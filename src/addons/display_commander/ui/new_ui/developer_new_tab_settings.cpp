#include "developer_new_tab_settings.hpp"
#include <minwindef.h>
#include <atomic>

// External declarations for the global variables
extern std::atomic<bool> s_prevent_fullscreen;
extern std::atomic<int> s_spoof_fullscreen_state;
extern std::atomic<int> s_spoof_window_focus;
extern std::atomic<bool> s_continuous_monitoring_enabled;
extern std::atomic<bool> s_prevent_always_on_top;
extern std::atomic<bool> s_background_feature_enabled;
extern std::atomic<bool> s_fix_hdr10_colorspace;
extern std::atomic<bool> s_nvapi_fullscreen_prevention;
extern std::atomic<bool> s_nvapi_hdr_logging;
extern std::atomic<float> s_nvapi_hdr_interval_sec;
extern std::atomic<bool> s_reflex_enable;
extern std::atomic<bool> s_reflex_low_latency;
extern std::atomic<bool> s_reflex_boost;
extern std::atomic<bool> s_reflex_use_markers;
extern std::atomic<bool> s_enable_reflex_logging;

extern std::atomic<bool> s_enable_unstable_reshade_features;

extern std::atomic<bool> s_enable_mute_unmute_shortcut;

extern std::atomic<bool> g_flush_before_present;

namespace ui::new_ui {

// Constructor - initialize all settings with proper keys and default values
DeveloperTabSettings::DeveloperTabSettings()
    : prevent_fullscreen("PreventFullscreen", s_prevent_fullscreen, true, "DisplayCommander")
    , spoof_fullscreen_state("SpoofFullscreenState", s_spoof_fullscreen_state, 0, 0, 2, "DisplayCommander")
    , spoof_window_focus("SpoofWindowFocus", s_spoof_window_focus, 0, 0, 2, "DisplayCommander")
    , continuous_monitoring("ContinuousMonitoring", s_continuous_monitoring_enabled, false, "DisplayCommander")
    , prevent_always_on_top("PreventAlwaysOnTop", s_prevent_always_on_top, true, "DisplayCommander")
    , fix_hdr10_colorspace("FixHDR10Colorspace", s_fix_hdr10_colorspace, false, "DisplayCommander")
    , nvapi_fullscreen_prevention("NvapiFullscreenPrevention", s_nvapi_fullscreen_prevention, false, "DisplayCommander")
    , nvapi_hdr_logging("NvapiHDRLogging", s_nvapi_hdr_logging, false, "DisplayCommander")
    , nvapi_hdr_interval_sec("NvapiHDRInterval", s_nvapi_hdr_interval_sec, 5.0f, 1.0f, 60.0f, "DisplayCommander")

    , enable_unstable_reshade_features("EnableUnstableReShadeFeatures", s_enable_unstable_reshade_features, false, "DisplayCommander")

    // Minimal NVIDIA Reflex controls
    , reflex_enable("ReflexEnable", s_reflex_enable, false, "DisplayCommander")
    , reflex_low_latency("ReflexLowLatency", s_reflex_low_latency, false, "DisplayCommander")
    , reflex_boost("ReflexBoost", s_reflex_boost, false, "DisplayCommander")
    , reflex_use_markers("ReflexUseMarkers", s_reflex_use_markers, true, "DisplayCommander")
    , reflex_logging("ReflexLogging", s_enable_reflex_logging, false, "DisplayCommander")

    , enable_mute_unmute_shortcut("EnableMuteUnmuteShortcut", s_enable_mute_unmute_shortcut, false, "DisplayCommander")
    , flush_before_present("FlushBeforePresent", g_flush_before_present, true, "DisplayCommander")
{
}

void DeveloperTabSettings::LoadAll() {
    prevent_fullscreen.Load();
    spoof_fullscreen_state.Load();
    spoof_window_focus.Load();
    continuous_monitoring.Load();
    prevent_always_on_top.Load();
    fix_hdr10_colorspace.Load();
    nvapi_fullscreen_prevention.Load();
    nvapi_hdr_logging.Load();
    nvapi_hdr_interval_sec.Load();

    enable_unstable_reshade_features.Load();

    enable_mute_unmute_shortcut.Load();
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

std::vector<ui::new_ui::SettingBase*> DeveloperTabSettings::GetAllSettings() {
    return {
        &prevent_fullscreen,
        &spoof_fullscreen_state,
        &spoof_window_focus,
        &continuous_monitoring,
        &prevent_always_on_top,
        &fix_hdr10_colorspace,
        &nvapi_fullscreen_prevention,
        &nvapi_hdr_logging,
        &nvapi_hdr_interval_sec,

        &reflex_enable,
        &reflex_low_latency,
        &reflex_boost,
        &reflex_use_markers,
        &reflex_logging,

        &enable_unstable_reshade_features,

        &enable_mute_unmute_shortcut,
        &flush_before_present
    };
}

// Global instance
DeveloperTabSettings g_developerTabSettings;

// Legacy function for backward compatibility - now uses the new settings wrapper
void AddDeveloperNewTabSettings(std::vector<void*>& settings) {
    // This function is now deprecated in favor of the settings wrapper system
    // The settings are now managed by DeveloperTabSettings class
    // This function is kept for backward compatibility but doesn't add any new settings
}

} // namespace ui::new_ui
