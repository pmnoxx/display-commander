#pragma once

#include "../ui/new_ui/settings_wrapper.hpp"

#include <vector>

namespace settings {

// Bring setting types into scope
using ui::new_ui::SettingBase;

// Settings manager for the reshade tab
class ReShadeTabSettings {
  public:
    ReShadeTabSettings();
    ~ReShadeTabSettings() = default;

    // Load all settings from ReShade config
    void LoadAll();

    // Get all settings for loading
    std::vector<SettingBase*> GetAllSettings();

  private:
    std::vector<SettingBase*> all_settings_;
};

// Global instance
extern ReShadeTabSettings g_reshadeTabSettings;

}  // namespace settings
