#include "hotkeys_tab_settings.hpp"
#include "../globals.hpp"

namespace settings {

// Constructor - initialize all settings with proper keys and default values
HotkeysTabSettings::HotkeysTabSettings()
    : enable_hotkeys("EnableHotkeys", true, "DisplayCommander"),
      hotkey_mute_unmute("HotkeyMuteUnmute", "ctrl+shift+m", "DisplayCommander"),
      hotkey_background_toggle("HotkeyBackgroundToggle", "", "DisplayCommander"),
      hotkey_timeslowdown("HotkeyTimeslowdown", "", "DisplayCommander"),
      hotkey_adhd_toggle("HotkeyAdhdToggle", "ctrl+shift+d", "DisplayCommander"),
      hotkey_autoclick("HotkeyAutoclick", "", "DisplayCommander"),
      hotkey_input_blocking("HotkeyInputBlocking", "", "DisplayCommander"),
      hotkey_display_commander_ui("HotkeyDisplayCommanderUi", "end", "DisplayCommander"),
      hotkey_performance_overlay("HotkeyPerformanceOverlay", "ctrl+shift+o", "DisplayCommander"),
      hotkey_stopwatch("HotkeyStopwatch", "ctrl+shift+s", "DisplayCommander"),
      hotkey_volume_up("HotkeyVolumeUp", "ctrl+shift+up", "DisplayCommander"),
      hotkey_volume_down("HotkeyVolumeDown", "ctrl+shift+down", "DisplayCommander"),
      hotkey_system_volume_up("HotkeySystemVolumeUp", "ctrl+alt+up", "DisplayCommander"),
      hotkey_system_volume_down("HotkeySystemVolumeDown", "ctrl+alt+down", "DisplayCommander"),
      hotkey_auto_hdr("HotkeyAutoHdr", "", "DisplayCommander"),
      hotkey_brightness_up("HotkeyBrightnessUp", "", "DisplayCommander"),
      hotkey_brightness_down("HotkeyBrightnessDown", "", "DisplayCommander"),
      hotkey_win_down("HotkeyWinDown", "win+down", "DisplayCommander"),
      hotkey_win_up("HotkeyWinUp", "win+up", "DisplayCommander"),
      hotkey_win_left("HotkeyWinLeft", "win+left", "DisplayCommander"),
      hotkey_win_right("HotkeyWinRight", "win+right", "DisplayCommander"),
      exclusive_keys_ad_enabled("ExclusiveKeysADEnabled", false, "DisplayCommander"),
      exclusive_keys_ws_enabled("ExclusiveKeysWSEnabled", false, "DisplayCommander"),
      exclusive_keys_awsd_enabled("ExclusiveKeysAWSDEnabled", false, "DisplayCommander"),
      exclusive_keys_custom_groups("ExclusiveKeysCustomGroups", "", "DisplayCommander") {}

void HotkeysTabSettings::LoadAll() {
    // Get all settings for smart logging
    auto all_settings = GetAllSettings();

    // Use smart logging to show only changed settings
    ui::new_ui::LoadTabSettingsWithSmartLogging(all_settings, "Hotkeys Tab");

    // Update atomic variable when enable_hotkeys is loaded
    s_enable_hotkeys.store(enable_hotkeys.GetValue());
}

void HotkeysTabSettings::SaveAll() {
    // Save all settings
    enable_hotkeys.Save();
    hotkey_mute_unmute.Save();
    hotkey_background_toggle.Save();
    hotkey_timeslowdown.Save();
    hotkey_adhd_toggle.Save();
    hotkey_autoclick.Save();
    hotkey_input_blocking.Save();
    hotkey_display_commander_ui.Save();
    hotkey_performance_overlay.Save();
    hotkey_stopwatch.Save();
    hotkey_volume_up.Save();
    hotkey_volume_down.Save();
    hotkey_system_volume_up.Save();
    hotkey_system_volume_down.Save();
    hotkey_auto_hdr.Save();
    hotkey_brightness_up.Save();
    hotkey_brightness_down.Save();
    hotkey_win_down.Save();
    hotkey_win_up.Save();
    hotkey_win_left.Save();
    hotkey_win_right.Save();
    exclusive_keys_ad_enabled.Save();
    exclusive_keys_ws_enabled.Save();
    exclusive_keys_awsd_enabled.Save();
    exclusive_keys_custom_groups.Save();
}

std::vector<ui::new_ui::SettingBase*> HotkeysTabSettings::GetAllSettings() {
    return {&enable_hotkeys, &hotkey_mute_unmute, &hotkey_background_toggle, &hotkey_timeslowdown,
            &hotkey_adhd_toggle, &hotkey_autoclick, &hotkey_input_blocking, &hotkey_display_commander_ui,
            &hotkey_performance_overlay, &hotkey_stopwatch, &hotkey_volume_up, &hotkey_volume_down,
            &hotkey_system_volume_up, &hotkey_system_volume_down, &hotkey_auto_hdr, &hotkey_brightness_up,
            &hotkey_brightness_down, &hotkey_win_down, &hotkey_win_up, &hotkey_win_left, &hotkey_win_right,
            &exclusive_keys_ad_enabled,
            &exclusive_keys_ws_enabled, &exclusive_keys_awsd_enabled, &exclusive_keys_custom_groups};
}

}  // namespace settings

