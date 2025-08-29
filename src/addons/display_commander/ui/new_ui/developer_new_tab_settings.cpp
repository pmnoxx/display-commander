#include "developer_new_tab_settings.hpp"
#include "../../renodx/settings.hpp"
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
extern std::atomic<bool> s_reflex_enabled;
extern std::atomic<bool> s_reflex_low_latency_mode;
extern std::atomic<bool> s_reflex_low_latency_boost;
extern std::atomic<bool> s_reflex_use_markers;
extern std::atomic<bool> s_reflex_debug_output;
extern std::atomic<bool> s_remove_top_bar;
extern std::atomic<bool> s_enable_unstable_reshade_features;
extern std::atomic<bool> s_enable_resolution_override;
extern std::atomic<int> s_override_resolution_width;
extern std::atomic<int> s_override_resolution_height;
extern std::atomic<bool> s_enable_mute_unmute_shortcut;
extern std::atomic<float> s_fps_extra_wait_ms;

namespace ui::new_ui {

// Constructor - initialize all settings with proper keys and default values
DeveloperTabSettings::DeveloperTabSettings()
    : prevent_fullscreen("PreventFullscreen", s_prevent_fullscreen, true, "DisplayCommander")
    , spoof_fullscreen_state("SpoofFullscreenState", s_spoof_fullscreen_state, 0, 0, 2, "DisplayCommander")
    , spoof_window_focus("SpoofWindowFocus", s_spoof_window_focus, 0, 0, 2, "DisplayCommander")
    , continuous_monitoring("ContinuousMonitoring", s_continuous_monitoring_enabled, true, "DisplayCommander")
    , prevent_always_on_top("PreventAlwaysOnTop", s_prevent_always_on_top, true, "DisplayCommander")
    , remove_top_bar("RemoveTopBar", s_remove_top_bar, false, "DisplayCommander")
    , fix_hdr10_colorspace("FixHDR10Colorspace", s_fix_hdr10_colorspace, false, "DisplayCommander")
    , nvapi_fullscreen_prevention("NvapiFullscreenPrevention", s_nvapi_fullscreen_prevention, false, "DisplayCommander")
    , nvapi_hdr_logging("NvapiHDRLogging", s_nvapi_hdr_logging, false, "DisplayCommander")
    , nvapi_hdr_interval_sec("NvapiHDRInterval", s_nvapi_hdr_interval_sec, 5.0f, 1.0f, 60.0f, "DisplayCommander")
    , reflex_enabled("ReflexEnabled", s_reflex_enabled, false, "DisplayCommander")
    , reflex_low_latency_mode("ReflexLowLatencyMode", s_reflex_low_latency_mode, false, "DisplayCommander")
    , reflex_low_latency_boost("ReflexLowLatencyBoost", s_reflex_low_latency_boost, false, "DisplayCommander")
    , reflex_use_markers("ReflexUseMarkers", s_reflex_use_markers, false, "DisplayCommander")
    , reflex_debug_output("ReflexDebugOutput", s_reflex_debug_output, false, "DisplayCommander")
    , fps_extra_wait_ms("FpsLimiterExtraWaitMs", s_fps_extra_wait_ms, 0.0f, 0.0f, 10.0f, "DisplayCommander")
    , enable_unstable_reshade_features("EnableUnstableReShadeFeatures", s_enable_unstable_reshade_features, false, "DisplayCommander")
    , enable_resolution_override("EnableResolutionOverride", s_enable_resolution_override, false, "DisplayCommander")
    , override_resolution_width("OverrideResolutionWidth", s_override_resolution_width, 1920, 1, 7680, "DisplayCommander")
    , override_resolution_height("OverrideResolutionHeight", s_override_resolution_height, 1080, 1, 4320, "DisplayCommander")
    , enable_mute_unmute_shortcut("EnableMuteUnmuteShortcut", s_enable_mute_unmute_shortcut, false, "DisplayCommander")
{
}

void DeveloperTabSettings::LoadAll() {
    prevent_fullscreen.Load();
    spoof_fullscreen_state.Load();
    spoof_window_focus.Load();
    continuous_monitoring.Load();
    prevent_always_on_top.Load();
    remove_top_bar.Load();
    fix_hdr10_colorspace.Load();
    nvapi_fullscreen_prevention.Load();
    nvapi_hdr_logging.Load();
    nvapi_hdr_interval_sec.Load();
    reflex_enabled.Load();
    reflex_low_latency_mode.Load();
    reflex_low_latency_boost.Load();
    reflex_use_markers.Load();
    reflex_debug_output.Load();
    fps_extra_wait_ms.Load();
    enable_unstable_reshade_features.Load();
    enable_resolution_override.Load();
    override_resolution_width.Load();
    override_resolution_height.Load();
    enable_mute_unmute_shortcut.Load();
    
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
        &remove_top_bar,
        &fix_hdr10_colorspace,
        &nvapi_fullscreen_prevention,
        &nvapi_hdr_logging,
        &nvapi_hdr_interval_sec,
        &reflex_enabled,
        &reflex_low_latency_mode,
        &reflex_low_latency_boost,
        &reflex_use_markers,
        &reflex_debug_output,
        &fps_extra_wait_ms,
        &enable_unstable_reshade_features,
        &enable_resolution_override,
        &override_resolution_width,
        &override_resolution_height,
        &enable_mute_unmute_shortcut
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
