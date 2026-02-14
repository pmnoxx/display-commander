#include "streamline_tab_settings.hpp"
#include "../utils.hpp"
#include "../utils/logging.hpp"

// Atomic variables for DLSS override settings
std::atomic<bool> s_dlss_override_enabled{false};
std::atomic<bool> s_dlss_override_dlss{false};
std::atomic<bool> s_dlss_override_dlss_fg{false};
std::atomic<bool> s_dlss_override_dlss_rr{false};

namespace settings {

StreamlineTabSettings::StreamlineTabSettings()
    : dlss_override_enabled("dlss_override_enabled", false, "DisplayCommander"),
      dlss_override_subfolder("dlss_override_subfolder", "", "DisplayCommander"),
      dlss_override_subfolder_dlssd("dlss_override_subfolder_dlssd", "", "DisplayCommander"),
      dlss_override_subfolder_dlssg("dlss_override_subfolder_dlssg", "", "DisplayCommander"),
      dlss_override_dlss("dlss_override_dlss", s_dlss_override_dlss, s_dlss_override_dlss.load(), "DisplayCommander"),
      dlss_override_dlss_fg("dlss_override_dlss_fg", s_dlss_override_dlss_fg, s_dlss_override_dlss_fg.load(),
                            "DisplayCommander"),
      dlss_override_dlss_rr("dlss_override_dlss_rr", s_dlss_override_dlss_rr, s_dlss_override_dlss_rr.load(),
                            "DisplayCommander") {
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
