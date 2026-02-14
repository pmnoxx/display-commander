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

    // DLSS Override Settings
    ui::new_ui::BoolSetting dlss_override_enabled;
    ui::new_ui::StringSetting dlss_override_folder;
    ui::new_ui::StringSetting dlss_override_subfolder;  // "" = Default Folder, else subfolder name under dlss_override
    ui::new_ui::BoolSettingRef dlss_override_dlss;      // nvngx_dlss.dll
    ui::new_ui::BoolSettingRef dlss_override_dlss_fg;  // nvngx_dlssd.dll (DLSS-FG)
    ui::new_ui::BoolSettingRef dlss_override_dlss_rr;  // nvngx_dlssg.dll (DLSS-RR)

   private:
    std::vector<ui::new_ui::SettingBase*> all_settings_;
};

}  // namespace settings
