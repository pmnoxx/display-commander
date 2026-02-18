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

    // Load all settings from hotkeys.toml (Display Commander folder in Local App Data; shared across games)
    void LoadAll();

    // Save all settings to hotkeys.toml
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

    // Exclusive key groups - predefined groups
    BoolSetting exclusive_keys_ad_enabled;      // A and D keys
    BoolSetting exclusive_keys_ws_enabled;      // W and S keys
    BoolSetting exclusive_keys_awsd_enabled;   // A, W, S, D keys

    // Custom exclusive key groups (stored as JSON array of groups, each group is comma-separated keys)
    // Format: "A,S|W,S|Q,E" where | separates groups and , separates keys within a group
    StringSetting exclusive_keys_custom_groups;

    // Get all settings for bulk operations
    std::vector<SettingBase*> GetAllSettings();
};

// Global instance
extern HotkeysTabSettings g_hotkeysTabSettings;

}  // namespace settings

