#include "reshade_tab_settings.hpp"

namespace settings {

ReShadeTabSettings::ReShadeTabSettings()
    : suppress_reshade_clock("SuppressReShadeClock", true, "DisplayCommander.ReShade") {
    // Initialize all_settings_ vector
    all_settings_ = {
        &suppress_reshade_clock,
    };
}

void ReShadeTabSettings::LoadAll() {
    for (auto* setting : all_settings_) {
        setting->Load();
    }
}

std::vector<SettingBase*> ReShadeTabSettings::GetAllSettings() { return all_settings_; }

}  // namespace settings
