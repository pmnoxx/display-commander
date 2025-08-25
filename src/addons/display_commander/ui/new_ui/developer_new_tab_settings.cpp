#include "developer_new_tab_settings.hpp"
#include "../../renodx/settings.hpp"
#include <minwindef.h>
#include <atomic>

// External declarations for the global variables
extern float s_prevent_fullscreen;
extern float s_spoof_fullscreen_state;
extern float s_spoof_window_focus;
extern std::atomic<bool> s_continuous_monitoring_enabled;
extern std::atomic<bool> s_prevent_always_on_top;
extern std::atomic<bool> s_background_feature_enabled;
extern float s_fix_hdr10_colorspace;
extern float s_nvapi_fullscreen_prevention;
extern float s_nvapi_hdr_logging;
extern float s_nvapi_hdr_interval_sec;
extern float s_nvapi_force_hdr10;
extern std::atomic<bool> s_reflex_enabled;
extern std::atomic<bool> s_reflex_debug_output;
extern float s_remove_top_bar;
extern std::atomic<bool> s_enable_unstable_reshade_features;
extern float s_enable_resolution_override;
extern float s_override_resolution_width;
extern float s_override_resolution_height;
extern float s_enable_mute_unmute_shortcut;
extern float s_fps_extra_wait_ms;

namespace ui::new_ui {

// Constructor - initialize all settings with proper keys and default values
DeveloperTabSettings::DeveloperTabSettings()
    : prevent_fullscreen("PreventFullscreen", true, "DisplayCommanderNew")
    , spoof_fullscreen_state("SpoofFullscreenState", false, "DisplayCommanderNew")
    , spoof_window_focus("SpoofWindowFocus", false, "DisplayCommanderNew")
    , continuous_monitoring("ContinuousMonitoring", true, "DisplayCommanderNew")
    , prevent_always_on_top("PreventAlwaysOnTop", true, "DisplayCommanderNew")
    , remove_top_bar("RemoveTopBar", false, "DisplayCommanderNew")
    , fix_hdr10_colorspace("FixHDR10Colorspace", false, "DisplayCommanderNew")
    , nvapi_fullscreen_prevention("NvapiFullscreenPrevention", false, "DisplayCommanderNew")
    , nvapi_hdr_logging("NvapiHDRLogging", false, "DisplayCommanderNew")
    , nvapi_hdr_interval_sec("NvapiHDRInterval", 5.0f, 1.0f, 60.0f, "DisplayCommanderNew")
    , nvapi_force_hdr10("NvapiForceHDR10", false, "DisplayCommanderNew")
    , reflex_enabled("ReflexEnabled", false, "DisplayCommanderNew")
    , reflex_debug_output("ReflexDebugOutput", false, "DisplayCommanderNew")
    , fps_extra_wait_ms("FpsLimiterExtraWaitMs", 0.0f, 0.0f, 10.0f, "DisplayCommanderNew")
    , enable_unstable_reshade_features("EnableUnstableReShadeFeatures", false, "DisplayCommanderNew")
    , enable_resolution_override("EnableResolutionOverride", false, "DisplayCommanderNew")
    , override_resolution_width("OverrideResolutionWidth", 1920, 1, 7680, "DisplayCommanderNew")
    , override_resolution_height("OverrideResolutionHeight", 1080, 1, 4320, "DisplayCommanderNew")
    , enable_mute_unmute_shortcut("EnableMuteUnmuteShortcut", false, "DisplayCommanderNew")
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
    nvapi_force_hdr10.Load();
    reflex_enabled.Load();
    reflex_debug_output.Load();
    fps_extra_wait_ms.Load();
    enable_unstable_reshade_features.Load();
    enable_resolution_override.Load();
    override_resolution_width.Load();
    override_resolution_height.Load();
    enable_mute_unmute_shortcut.Load();
    
    // Update global variables to maintain compatibility
    s_prevent_fullscreen = prevent_fullscreen.GetValue() ? 1.0f : 0.0f;
    s_spoof_fullscreen_state = spoof_fullscreen_state.GetValue() ? 1.0f : 0.0f;
    s_spoof_window_focus = spoof_window_focus.GetValue() ? 1.0f : 0.0f;
    s_continuous_monitoring_enabled.store(continuous_monitoring.GetValue());
    s_prevent_always_on_top.store(prevent_always_on_top.GetValue());
    s_remove_top_bar = remove_top_bar.GetValue() ? 1.0f : 0.0f;
    s_fix_hdr10_colorspace = fix_hdr10_colorspace.GetValue() ? 1.0f : 0.0f;
    s_nvapi_fullscreen_prevention = nvapi_fullscreen_prevention.GetValue() ? 1.0f : 0.0f;
    s_nvapi_hdr_logging = nvapi_hdr_logging.GetValue() ? 1.0f : 0.0f;
    s_nvapi_hdr_interval_sec = nvapi_hdr_interval_sec.GetValue();
    s_nvapi_force_hdr10 = nvapi_force_hdr10.GetValue() ? 1.0f : 0.0f;
    s_reflex_enabled.store(reflex_enabled.GetValue());
    s_reflex_debug_output = reflex_debug_output.GetValue() ? 1.0f : 0.0f;
    s_fps_extra_wait_ms = fps_extra_wait_ms.GetValue();
    s_enable_unstable_reshade_features = enable_unstable_reshade_features.GetValue() ? 1.0f : 0.0f;
    s_enable_resolution_override = enable_resolution_override.GetValue() ? 1.0f : 0.0f;
    s_override_resolution_width = static_cast<float>(override_resolution_width.GetValue());
    s_override_resolution_height = static_cast<float>(override_resolution_height.GetValue());
    s_enable_mute_unmute_shortcut = enable_mute_unmute_shortcut.GetValue() ? 1.0f : 0.0f;
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
    nvapi_force_hdr10.Save();
    reflex_enabled.Save();
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
        &nvapi_force_hdr10,
        &reflex_enabled,
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
