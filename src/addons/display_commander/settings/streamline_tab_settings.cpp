// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "streamline_tab_settings.hpp"
#include "../utils/logging.hpp"

// Libraries <ReShade> / <imgui>

// Libraries <standard C++>

// Libraries <Windows.h> — before other Windows headers

// Libraries <Windows>

namespace settings {

StreamlineTabSettings::StreamlineTabSettings()
    : dlss_override_enabled("dlss_override_enabled", false, "DisplayCommander"),
      dlss_override_subfolder("dlss_override_subfolder", "", "DisplayCommander"),
      dlss_override_subfolder_dlssd("dlss_override_subfolder_dlssd", "", "DisplayCommander"),
      dlss_override_subfolder_dlssg("dlss_override_subfolder_dlssg", "", "DisplayCommander"),
      dlss_override_dlss("dlss_override_dlss", false, "DisplayCommander"),
      dlss_override_dlss_fg("dlss_override_dlss_fg", false, "DisplayCommander"),
      dlss_override_dlss_rr("dlss_override_dlss_rr", false, "DisplayCommander") {
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
