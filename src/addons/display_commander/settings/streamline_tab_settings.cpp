// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "streamline_tab_settings.hpp"
#include "../utils/logging.hpp"

// Libraries <ReShade> / <imgui>

// Libraries <standard C++>

// Libraries <Windows.h> — before other Windows headers

// Libraries <Windows>

namespace settings {

StreamlineTabSettings::StreamlineTabSettings()
    : dlss_override_enabled("DlssOverride.Enabled", false, "DisplayCommander"),
      dlss_override_subfolder("DlssOverride.DlssSubfolder", "", "DisplayCommander"),
      dlss_override_subfolder_dlssd("DlssOverride.DlssdSubfolder", "", "DisplayCommander"),
      dlss_override_subfolder_dlssg("DlssOverride.DlssgSubfolder", "", "DisplayCommander"),
      dlss_override_dlss("DlssOverride.DlssEnabled", false, "DisplayCommander"),
      dlss_override_dlss_fg("DlssOverride.DlssgEnabled", false, "DisplayCommander"),
      dlss_override_dlss_rr("DlssOverride.DlssdEnabled", false, "DisplayCommander") {
    all_settings_ = {
        &dlss_override_enabled, &dlss_override_subfolder, &dlss_override_subfolder_dlssd, &dlss_override_subfolder_dlssg,
        &dlss_override_dlss,    &dlss_override_dlss_fg,  &dlss_override_dlss_rr,
    };
}

void StreamlineTabSettings::LoadAll() {
    LogInfo("StreamlineTabSettings::LoadAll() called");
    LoadTabSettingsWithSmartLogging(all_settings_, "Streamline Tab");
    LogInfo("StreamlineTabSettings::LoadAll() completed");
}

std::vector<ui::new_ui::SettingBase*> StreamlineTabSettings::GetAllSettings() { return all_settings_; }

}  // namespace settings
