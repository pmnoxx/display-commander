#include "reshade_tab_settings.hpp"

namespace settings {

ReShadeTabSettings::ReShadeTabSettings() = default;

void ReShadeTabSettings::LoadAll() {
    for (auto* setting : all_settings_) {
        setting->Load();
    }
}

std::vector<SettingBase*> ReShadeTabSettings::GetAllSettings() { return all_settings_; }

}  // namespace settings
