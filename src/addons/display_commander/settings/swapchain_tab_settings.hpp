#pragma once

#include "../ui/new_ui/settings_wrapper.hpp"

#include <vector>

namespace settings {

// Bring setting types into scope
using ui::new_ui::BoolSetting;
using ui::new_ui::BoolSettingRef;
using ui::new_ui::ComboSetting;
using ui::new_ui::FixedIntArraySetting;
using ui::new_ui::FloatSetting;
using ui::new_ui::FloatSettingRef;
using ui::new_ui::IntSetting;
using ui::new_ui::SettingBase;
using ui::new_ui::StringSetting;

// Settings manager for the swapchain tab
class SwapchainTabSettings {
  public:
    SwapchainTabSettings();
    ~SwapchainTabSettings() = default;

    // Load all settings from ReShade config
    void LoadAll();

    // Get all settings for loading
    std::vector<SettingBase *> GetAllSettings();

    // DLSS preset override settings
    BoolSetting dlss_preset_override_enabled;
    StringSetting dlss_sr_preset_override;
    StringSetting dlss_rr_preset_override;
    // DLSS auto-exposure override: "Game Default", "Force Off", "Force On"
    StringSetting dlss_forced_auto_exposure;


  private:
    std::vector<SettingBase *> all_settings_;
};

} // namespace settings
