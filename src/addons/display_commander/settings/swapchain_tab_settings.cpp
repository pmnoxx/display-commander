#include "swapchain_tab_settings.hpp"
#include "../globals.hpp"

namespace settings {

SwapchainTabSettings::SwapchainTabSettings()
    : dlss_preset_override_enabled("DLSSPresetOverrideEnabled", false, "DisplayCommander.Swapchain")
    , dlss_sr_preset_override("DLSSSRPresetOverride", "Game Default", "DisplayCommander.Swapchain")
    , dlss_rr_preset_override("DLSSRRPresetOverride", "Game Default", "DisplayCommander.Swapchain")
    , dlss_forced_auto_exposure("DLSSForcedAutoExposure", "Game Default", "DisplayCommander.Swapchain")
{
    // Initialize the all_settings_ vector
    all_settings_ = {
        &dlss_preset_override_enabled, &dlss_sr_preset_override, &dlss_rr_preset_override,
        &dlss_forced_auto_exposure,
    };
}

void SwapchainTabSettings::LoadAll() {
    LoadTabSettingsWithSmartLogging(all_settings_, "Swapchain Tab");
}

std::vector<SettingBase *> SwapchainTabSettings::GetAllSettings() {
    return all_settings_;
}

} // namespace settings
