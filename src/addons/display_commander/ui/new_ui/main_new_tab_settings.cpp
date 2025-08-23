#include "main_new_tab_settings.hpp"
#include "../../addon.hpp"

namespace renodx::ui::new_ui {

// Global instance
MainNewTabSettings g_main_new_tab_settings;

MainNewTabSettings::MainNewTabSettings()
    : window_mode("window_mode", 0, {"Borderless Windowed (Aspect Ratio)", "Borderless Windowed (Width/Height)", "Borderless Fullscreen"}, "renodx_main_tab"),
      window_width("window_width", 0, {"Current Display", "1280", "1366", "1600", "1920", "2560", "3440", "3840"}, "renodx_main_tab"),
      window_height("window_height", 0, {"Current Display", "720", "900", "1080", "1200", "1440", "1600", "2160"}, "renodx_main_tab"),
      aspect_index("aspect_index", 3, {"3:2", "4:3", "16:10", "16:9", "19:9", "19.5:9", "21:9", "32:9"}, "renodx_main_tab"), // Default to 16:9
      target_monitor_index("target_monitor_index", 0, {"Auto", "Monitor 1", "Monitor 2", "Monitor 3", "Monitor 4", "Monitor 5", "Monitor 6", "Monitor 7", "Monitor 8", "Monitor 9", "Monitor 10"}, "renodx_main_tab"),
      background_feature("background_feature", false, "renodx_main_tab"),
      alignment("alignment", 0, {"None", "Top Left", "Top Right", "Bottom Left", "Bottom Right", "Center"}, "renodx_main_tab"),
      fps_limit("fps_limit", 0.0f, 0.0f, 240.0f, "renodx_main_tab"),
      fps_limit_background("fps_limit_background", 0.0f, 0.0f, 240.0f, "renodx_main_tab"),
      audio_volume_percent("audio_volume_percent", 100.0f, 0.0f, 100.0f, "renodx_main_tab"),
      audio_mute("audio_mute", false, "renodx_main_tab"),
      mute_in_background("mute_in_background", false, "renodx_main_tab"),
      mute_in_background_if_other_audio("mute_in_background_if_other_audio", true, "renodx_main_tab"),
      audio_volume_auto_apply("audio_volume_auto_apply", true, "renodx_main_tab"),
      reflex_enabled("reflex_enabled", false, "renodx_main_tab") {
    
    // Initialize the all_settings_ vector
    all_settings_ = {
        &window_mode,
        &window_width,
        &window_height,
        &aspect_index,
        &target_monitor_index,
        &background_feature,
        &alignment,
        &fps_limit,
        &fps_limit_background,
        &audio_volume_percent,
        &audio_mute,
        &mute_in_background,
        &mute_in_background_if_other_audio,
        &audio_volume_auto_apply,
        &reflex_enabled
    };
}

void MainNewTabSettings::LoadSettings() {
    LoadTabSettings(all_settings_);
}

std::vector<SettingBase*> MainNewTabSettings::GetAllSettings() {
    return all_settings_;
}

} // namespace renodx::ui::new_ui
