#include "hotkeys_tab_settings.hpp"
#include "../globals.hpp"

namespace settings {

// Constructor - initialize all settings with proper keys and default values
HotkeysTabSettings::HotkeysTabSettings()
    : enable_hotkeys("EnableHotkeys", true, "DisplayCommander"),
      hotkey_mute_unmute("HotkeyMuteUnmute", "ctrl+m", "DisplayCommander"),
      hotkey_background_toggle("HotkeyBackgroundToggle", "", "DisplayCommander"),
      hotkey_timeslowdown("HotkeyTimeslowdown", "", "DisplayCommander"),
      hotkey_adhd_toggle("HotkeyAdhdToggle", "ctrl+d", "DisplayCommander"),
      hotkey_autoclick("HotkeyAutoclick", "", "DisplayCommander"),
      hotkey_input_blocking("HotkeyInputBlocking", "", "DisplayCommander"),
      hotkey_display_commander_ui("HotkeyDisplayCommanderUi", "ctrl+shift+backspace", "DisplayCommander"),
      hotkey_performance_overlay("HotkeyPerformanceOverlay", "ctrl+o", "DisplayCommander"),
      hotkey_stopwatch("HotkeyStopwatch", "ctrl+s", "DisplayCommander") {}

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
}

std::vector<ui::new_ui::SettingBase*> HotkeysTabSettings::GetAllSettings() {
    return {&enable_hotkeys, &hotkey_mute_unmute, &hotkey_background_toggle, &hotkey_timeslowdown,
            &hotkey_adhd_toggle, &hotkey_autoclick, &hotkey_input_blocking, &hotkey_display_commander_ui,
            &hotkey_performance_overlay, &hotkey_stopwatch};
}

}  // namespace settings

