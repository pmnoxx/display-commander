#include "swapchain_tab_settings.hpp"
#include "../globals.hpp"

namespace settings {

SwapchainTabSettings::SwapchainTabSettings()
    : dlss_preset_override_enabled("DLSSPresetOverrideEnabled", false, "DisplayCommander.Swapchain")
    , dlss_sr_preset_override("DLSSSRPresetOverride", 0, {
        "Game Default",
        "Preset A",
        "Preset B",
        "Preset C",
        "Preset D",
        "Preset E",
        "Preset F",
        "Preset G",
        "Preset H",
        "Preset I",
        "Preset J",
        "Preset K",
        "Preset L",
        "Preset M",
        "Preset N",
        "Preset O"
    }, "DisplayCommander.Swapchain")
    , dlss_rr_preset_override("DLSSRRPresetOverride", 0, {
        "Game Default",
        "Preset A",
        "Preset B",
        "Preset C",
        "Preset D",
        "Preset E",
        "Preset F",
        "Preset G",
        "Preset H",
        "Preset I",
        "Preset J",
        "Preset K",
        "Preset L",
        "Preset M",
        "Preset N",
        "Preset O"
    }, "DisplayCommander.Swapchain")
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
