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
// Removed: s_enable_resolution_override is now handled by BoolSetting in developer tab settings
extern std::atomic<int> s_override_resolution_width;
extern std::atomic<int> s_override_resolution_height;
extern std::atomic<bool> s_enable_mute_unmute_shortcut;
extern std::atomic<float> s_fps_extra_wait_ms;

namespace ui::new_ui {

// Constructor - initialize all settings with proper keys and default values
DeveloperTabSettings::DeveloperTabSettings()
    : prevent_fullscreen("PreventFullscreen", true, "DisplayCommander")
    , spoof_fullscreen_state("SpoofFullscreenState", false, "DisplayCommander")
    , spoof_window_focus("SpoofWindowFocus", false, "DisplayCommander")
    , continuous_monitoring("ContinuousMonitoring", true, "DisplayCommander")
    , prevent_always_on_top("PreventAlwaysOnTop", true, "DisplayCommander")
    , remove_top_bar("RemoveTopBar", false, "DisplayCommander")
    , fix_hdr10_colorspace("FixHDR10Colorspace", false, "DisplayCommander")
    , nvapi_fullscreen_prevention("NvapiFullscreenPrevention", false, "DisplayCommander")
    , nvapi_hdr_logging("NvapiHDRLogging", false, "DisplayCommander")
    , nvapi_hdr_interval_sec("NvapiHDRInterval", 5.0f, 1.0f, 60.0f, "DisplayCommander")
    , reflex_enabled("ReflexEnabled", false, "DisplayCommander")
    , reflex_low_latency_mode("ReflexLowLatencyMode", false, "DisplayCommander")
    , reflex_low_latency_boost("ReflexLowLatencyBoost", false, "DisplayCommander")
    , reflex_use_markers("ReflexUseMarkers", false, "DisplayCommander")
    , reflex_debug_output("ReflexDebugOutput", false, "DisplayCommander")
    , fps_extra_wait_ms("FpsLimiterExtraWaitMs", 0.0f, 0.0f, 10.0f, "DisplayCommander")
    , enable_unstable_reshade_features("EnableUnstableReShadeFeatures", false, "DisplayCommander")
    , enable_resolution_override("EnableResolutionOverride", false, "DisplayCommander")
    , override_resolution_width("OverrideResolutionWidth", 1920, 1, 7680, "DisplayCommander")
    , override_resolution_height("OverrideResolutionHeight", 1080, 1, 4320, "DisplayCommander")
    , enable_mute_unmute_shortcut("EnableMuteUnmuteShortcut", false, "DisplayCommander")
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
    
    // Update global variables to maintain compatibility
    s_prevent_fullscreen.store(prevent_fullscreen.GetValue());
    s_spoof_fullscreen_state.store(spoof_fullscreen_state.GetValue());
    s_spoof_window_focus.store(spoof_window_focus.GetValue());
    s_continuous_monitoring_enabled.store(continuous_monitoring.GetValue());
    s_prevent_always_on_top.store(prevent_always_on_top.GetValue());
    s_remove_top_bar.store(remove_top_bar.GetValue());
    s_fix_hdr10_colorspace.store(fix_hdr10_colorspace.GetValue());
    s_nvapi_fullscreen_prevention.store(nvapi_fullscreen_prevention.GetValue());
    s_nvapi_hdr_logging.store(nvapi_hdr_logging.GetValue());
    s_nvapi_hdr_interval_sec.store(nvapi_hdr_interval_sec.GetValue());
    s_reflex_enabled.store(reflex_enabled.GetValue());
    s_reflex_low_latency_mode.store(reflex_low_latency_mode.GetValue());
    s_reflex_low_latency_boost.store(reflex_low_latency_boost.GetValue());
    s_reflex_use_markers.store(reflex_use_markers.GetValue());
    s_reflex_debug_output.store(reflex_debug_output.GetValue());
    s_fps_extra_wait_ms.store(fps_extra_wait_ms.GetValue());
    s_enable_unstable_reshade_features.store(enable_unstable_reshade_features.GetValue());
    // Removed: s_enable_resolution_override is now handled by BoolSetting in developer tab settings
    s_override_resolution_width.store(override_resolution_width.GetValue());
    s_override_resolution_height.store(override_resolution_height.GetValue());
    s_enable_mute_unmute_shortcut.store(enable_mute_unmute_shortcut.GetValue());
}

void DeveloperTabSettings::SaveAll() {
    prevent_fullscreen.Save();
    spoof_fullscreen_state.Save();
    spoof_window_focus.Save();
    continuous_monitoring.Save();
    prevent_always_on_top.Save();
    remove_top_bar.Save();
    fix_hdr10_colorspace.Save();
    nvapi_hdr_logging.Save();
    nvapi_hdr_interval_sec.Save();
    reflex_enabled.Save();
    reflex_low_latency_mode.Save();
    reflex_low_latency_boost.Save();
    reflex_use_markers.Save();
    reflex_debug_output.Save();
    fps_extra_wait_ms.Save();
    enable_unstable_reshade_features.Save();
    enable_resolution_override.Save();
    override_resolution_width.Save();
    override_resolution_height.Save();
    enable_mute_unmute_shortcut.Save();
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
