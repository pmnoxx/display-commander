#pragma once

#include "../ui/new_ui/settings_wrapper.hpp"

#include <vector>

namespace settings {

// Bring setting types into scope
using ui::new_ui::StringSetting;
using ui::new_ui::BoolSetting;
using ui::new_ui::SettingBase;

// Hotkeys tab settings manager
class HotkeysTabSettings {
   public:
    HotkeysTabSettings();
    ~HotkeysTabSettings() = default;

    // Load all settings from DisplayCommander config
    void LoadAll();

    // Save all settings to DisplayCommander config
    void SaveAll();

    // Master toggle
    BoolSetting enable_hotkeys;

    // Individual hotkey shortcut strings (empty = disabled)
    StringSetting hotkey_mute_unmute;
    StringSetting hotkey_background_toggle;
    StringSetting hotkey_timeslowdown;
    StringSetting hotkey_adhd_toggle;
    StringSetting hotkey_autoclick;
    StringSetting hotkey_input_blocking;
    StringSetting hotkey_display_commander_ui;
    StringSetting hotkey_performance_overlay;
    StringSetting hotkey_stopwatch;
    StringSetting hotkey_volume_up;
    StringSetting hotkey_volume_down;
    StringSetting hotkey_system_volume_up;
    StringSetting hotkey_system_volume_down;

    // Get all settings for bulk operations
    std::vector<SettingBase*> GetAllSettings();
};

// Global instance
extern HotkeysTabSettings g_hotkeysTabSettings;

}  // namespace settings

