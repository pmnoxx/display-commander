#include "swapchain_tab_settings.hpp"
#include "../globals.hpp"

namespace settings {

SwapchainTabSettings::SwapchainTabSettings()
    : dlss_preset_override_enabled("DLSSPresetOverrideEnabled", true, "DisplayCommander.Swapchain")
    , dlss_sr_preset_override("DLSSSRPresetOverride", "Game Default", "DisplayCommander.Swapchain")
    , dlss_rr_preset_override("DLSSRRPresetOverride", "Game Default", "DisplayCommander.Swapchain")
{
    // Initialize the all_settings_ vector
    all_settings_ = {
        &dlss_preset_override_enabled, &dlss_sr_preset_override, &dlss_rr_preset_override,
    };
}

void SwapchainTabSettings::LoadAll() {
    LoadTabSettings(all_settings_);
}

std::vector<SettingBase *> SwapchainTabSettings::GetAllSettings() {
    return all_settings_;
}

} // namespace settings
