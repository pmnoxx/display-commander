#pragma once

#include <atomic>
#include <string>
#include "../ui/new_ui/settings_wrapper.hpp"

namespace settings {

// DLSS Override Settings
class StreamlineTabSettings {
   public:
    StreamlineTabSettings();
    ~StreamlineTabSettings() = default;

    // Load all settings from DisplayCommander config
    void LoadAll();

    // Get all settings for loading
    std::vector<ui::new_ui::SettingBase*> GetAllSettings();

    // DLSS Override Settings (base path is always Display Commander\dlss_override)
    ui::new_ui::BoolSetting dlss_override_enabled;
    ui::new_ui::StringSetting dlss_override_subfolder;        // nvngx_dlss.dll subfolder e.g. 310.5.2
    ui::new_ui::StringSetting dlss_override_subfolder_dlssd;  // nvngx_dlssd.dll (D = denoiser / RR) subfolder
    ui::new_ui::StringSetting dlss_override_subfolder_dlssg;  // nvngx_dlssg.dll (G = generation / FG) subfolder
    ui::new_ui::BoolSettingRef dlss_override_dlss;            // nvngx_dlss.dll
    ui::new_ui::BoolSettingRef dlss_override_dlss_fg;         // nvngx_dlssg.dll (G = generation / FG)
    ui::new_ui::BoolSettingRef dlss_override_dlss_rr;         // nvngx_dlssd.dll (D = denoiser / RR)

   private:
    std::vector<ui::new_ui::SettingBase*> all_settings_;
};

}  // namespace settings
