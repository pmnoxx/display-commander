#include "hotkeys_tab.hpp"
#include "../../adhd_multi_monitor/adhd_simple_api.hpp"
#include "../../audio/audio_management.hpp"
#include "../../autoclick/autoclick_manager.hpp"
#include "../../globals.hpp"
#include "../../hooks/windows_hooks/windows_message_hooks.hpp"
#include "../../input_remapping/input_remapping.hpp"
#include "../../res/forkawesome.h"
#include "../../res/ui_colors.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../settings/hotkeys_tab_settings.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"
#include "imgui.h"
#include "settings_wrapper.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <vector>

namespace ui::new_ui {

namespace {
// Hotkey definitions array (data-driven approach)
std::vector<HotkeyDefinition> g_hotkey_definitions;

// Debug tracking for ProcessHotkeys
struct HotkeyDebugInfo {
    LONGLONG last_call_time_ns = 0;
    LONGLONG last_successful_call_time_ns = 0;
    std::string last_block_reason;
    bool hotkeys_enabled = false;
    bool game_in_foreground = false;
    bool ui_open = false;
    bool game_hwnd_valid = false;
    HWND current_foreground_hwnd = nullptr;
    HWND game_hwnd = nullptr;
};
HotkeyDebugInfo g_hotkey_debug_info;

// Draw a single hotkey entry in the table
void DrawHotkeyEntry(HotkeyDefinition& def, ui::new_ui::StringSetting& setting, int index) {
    ImGui::TableNextRow();

    // Hotkey Name
    ImGui::TableNextColumn();
    ImGui::Text("%s", def.name.c_str());
    if (ImGui::IsItemHovered() && !def.description.empty()) {
        ImGui::SetTooltip("%s", def.description.c_str());
    }

    // Shortcut Input
    ImGui::TableNextColumn();
    std::string current_value = setting.GetValue();
    char buffer[256] = {0};
    strncpy_s(buffer, sizeof(buffer), current_value.c_str(), _TRUNCATE);

    ImGui::SetNextItemWidth(-1);  // Use full column width
    if (ImGui::InputText(("##HotkeyInput" + std::to_string(index)).c_str(), buffer, sizeof(buffer))) {
        std::string new_value(buffer);
        setting.SetValue(new_value);
        def.parsed = ParseHotkeyString(new_value);
    }

    // Status
    ImGui::TableNextColumn();
    if (def.parsed.IsValid()) {
        ui::colors::PushIconColor(ui::colors::ICON_SUCCESS);
        ImGui::Text(ICON_FK_OK " Active");
        ui::colors::PopIconColor();
    } else if (!current_value.empty()) {
        ui::colors::PushIconColor(ui::colors::ICON_WARNING);
        ImGui::Text(ICON_FK_WARNING " Invalid");
        ui::colors::PopIconColor();
    } else {
        ui::colors::PushIconColor(ui::colors::ICON_DISABLED);
        ImGui::Text(ICON_FK_MINUS " Disabled");
        ui::colors::PopIconColor();
    }

    // Reset to default button
    ImGui::TableNextColumn();
    if (ImGui::Button(("Reset##" + def.id).c_str())) {
        setting.SetValue(def.default_shortcut);
        def.parsed = ParseHotkeyString(def.default_shortcut);
    }
}
}  // namespace

// Initialize hotkey definitions with default values
void InitializeHotkeyDefinitions() {
    g_hotkey_definitions = {
        {"mute_unmute", "Mute/Unmute Audio", "ctrl+shift+m", "Toggle audio mute state",
         []() {
             bool new_mute_state = !settings::g_mainTabSettings.audio_mute.GetValue();
             settings::g_mainTabSettings.audio_mute.SetValue(new_mute_state);
             if (SetMuteForCurrentProcess(new_mute_state)) {
                 g_muted_applied.store(new_mute_state);
                 std::ostringstream oss;
                 oss << "Audio " << (new_mute_state ? "muted" : "unmuted") << " via hotkey";
                 LogInfo(oss.str().c_str());
             }
         }},
        {"background_toggle", "Background Toggle", "",
         "Toggle both 'No Render in Background' and 'No Present in Background' settings",
         []() {
             bool new_render_state = !s_no_render_in_background.load();
             bool new_present_state = new_render_state;
             s_no_render_in_background.store(new_render_state);
             s_no_present_in_background.store(new_present_state);
             settings::g_mainTabSettings.no_render_in_background.SetValue(new_render_state);
             settings::g_mainTabSettings.no_present_in_background.SetValue(new_present_state);
             std::ostringstream oss;
             oss << "Background settings toggled via hotkey - Both Render and Present: "
                 << (new_render_state ? "disabled" : "enabled");
             LogInfo(oss.str().c_str());
         }},
        {"timeslowdown", "Time Slowdown Toggle", "", "Toggle Time Slowdown feature",
         []() {
             if (!enabled_experimental_features) return;
             bool current_state = settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue();
             bool new_state = !current_state;
             settings::g_experimentalTabSettings.timeslowdown_enabled.SetValue(new_state);
             std::ostringstream oss;
             oss << "Time Slowdown " << (new_state ? "enabled" : "disabled") << " via hotkey";
             LogInfo(oss.str().c_str());
         }},
        {"adhd_toggle", "ADHD Multi-Monitor Mode", "ctrl+shift+d", "Toggle ADHD Multi-Monitor Mode",
         []() {
             bool current_state = settings::g_mainTabSettings.adhd_multi_monitor_enabled.GetValue();
             bool new_state = !current_state;
             settings::g_mainTabSettings.adhd_multi_monitor_enabled.SetValue(new_state);
             adhd_multi_monitor::api::SetEnabled(new_state);
             std::ostringstream oss;
             oss << "ADHD Multi-Monitor Mode " << (new_state ? "enabled" : "disabled") << " via hotkey";
             LogInfo(oss.str().c_str());
         }},
        {"autoclick", "Auto-Click Toggle", "", "Toggle Auto-Click sequences (requires experimental features)",
         []() {
             if (!enabled_experimental_features) return;
             LogInfo("Auto-Click hotkey detected - toggling auto-click");
             autoclick::ToggleAutoClickEnabled();
         }},
        {"input_blocking", "Input Blocking Toggle", "", "Toggle input blocking",
         []() {
             bool current_state = s_input_blocking_toggle.load();
             bool new_state = !current_state;
             s_input_blocking_toggle.store(new_state);
             std::ostringstream oss;
             oss << "Input Blocking " << (new_state ? "enabled" : "disabled") << " via hotkey";
             LogInfo(oss.str().c_str());
         }},
        {"display_commander_ui", "Display Commander UI Toggle", "end", "Toggle the Display Commander UI overlay",
         []() {
             bool current_state = settings::g_mainTabSettings.show_display_commander_ui.GetValue();
             bool new_state = !current_state;
             settings::g_mainTabSettings.show_display_commander_ui.SetValue(new_state);
             std::ostringstream oss;
             oss << "Display Commander UI " << (new_state ? "enabled" : "disabled") << " via hotkey";
             LogInfo(oss.str().c_str());
         }},
        {"performance_overlay", "Performance Overlay Toggle", "ctrl+shift+o", "Toggle the performance overlay",
         []() {
             bool current_state = settings::g_mainTabSettings.show_test_overlay.GetValue();
             bool new_state = !current_state;
             settings::g_mainTabSettings.show_test_overlay.SetValue(new_state);
             std::ostringstream oss;
             oss << "Performance overlay " << (new_state ? "enabled" : "disabled") << " via hotkey";
             LogInfo(oss.str().c_str());
         }},
        {"stopwatch", "Stopwatch Start/Pause", "ctrl+shift+s", "Start or pause the stopwatch (2-state toggle)",
         []() { display_commander::input_remapping::ToggleStopwatch(); }},
        {"volume_up", "Volume Up", "ctrl+shift+up", "Increase audio volume (percentage-based, min 1%)",
         []() {
             float current_volume = 0.0f;
             if (!GetVolumeForCurrentProcess(&current_volume)) {
                 current_volume = s_audio_volume_percent.load();
             }

             // Calculate percentage-based step: 20% of current volume, minimum 1%
             float step = 0.0f;
             if (current_volume <= 0.0f) {
                 // Special case: if at 0%, jump to 1%
                 step = 1.0f;
             } else {
                 // 20% of current volume, with minimum of 1%
                 step = (std::max)(1.0f, current_volume * 0.20f);
             }

             if (AdjustVolumeForCurrentProcess(step)) {
                 std::ostringstream oss;
                 oss << "Volume increased by " << std::fixed << std::setprecision(1) << step << "% via hotkey";
                 LogInfo(oss.str().c_str());
             } else {
                 LogWarn("Failed to increase volume via hotkey");
             }
         }},
        {"volume_down", "Volume Down", "ctrl+shift+down", "Decrease audio volume (percentage-based, min 1%)",
         []() {
             float current_volume = 0.0f;
             if (!GetVolumeForCurrentProcess(&current_volume)) {
                 current_volume = s_audio_volume_percent.load();
             }

             // Calculate percentage-based step: 20% of current volume, minimum 1%
             if (current_volume <= 0.0f) {
                 // Already at 0%, can't go lower
                 return;
             }

             // 20% of current volume, with minimum of 1%
             float step = (std::max)(1.0f, current_volume * 0.20f);

             if (AdjustVolumeForCurrentProcess(-step)) {
                 std::ostringstream oss;
                 oss << "Volume decreased by " << std::fixed << std::setprecision(1) << step << "% via hotkey";
                 LogInfo(oss.str().c_str());
             } else {
                 LogWarn("Failed to decrease volume via hotkey");
             }
         }},
        {"system_volume_up", "System Volume Up", "ctrl+alt+up",
         "Increase system master volume (percentage-based, min 1%)",
         []() {
             float current_volume = 0.0f;
             if (!GetSystemVolume(&current_volume)) {
                 current_volume = s_system_volume_percent.load();
             }

             // Calculate percentage-based step: 20% of current volume, minimum 1%
             float step = 0.0f;
             if (current_volume <= 0.0f) {
                 // Special case: if at 0%, jump to 1%
                 step = 1.0f;
             } else {
                 // 20% of current volume, with minimum of 1%
                 step = (std::max)(1.0f, current_volume * 0.20f);
             }

             if (AdjustSystemVolume(step)) {
                 std::ostringstream oss;
                 oss << "System volume increased by " << std::fixed << std::setprecision(1) << step << "% via hotkey";
                 LogInfo(oss.str().c_str());
             } else {
                 LogWarn("Failed to increase system volume via hotkey");
             }
         }},
        {"system_volume_down", "System Volume Down", "ctrl+alt+down",
         "Decrease system master volume (percentage-based, min 1%)", []() {
             float current_volume = 0.0f;
             if (!GetSystemVolume(&current_volume)) {
                 current_volume = s_system_volume_percent.load();
             }

             // Calculate percentage-based step: 20% of current volume, minimum 1%
             if (current_volume <= 0.0f) {
                 // Already at 0%, can't go lower
                 return;
             }

             // 20% of current volume, with minimum of 1%
             float step = (std::max)(1.0f, current_volume * 0.20f);

             if (AdjustSystemVolume(-step)) {
                 std::ostringstream oss;
                 oss << "System volume decreased by " << std::fixed << std::setprecision(1) << step << "% via hotkey";
                 LogInfo(oss.str().c_str());
             } else {
                 LogWarn("Failed to decrease system volume via hotkey");
             }
         }}};

    // Map settings to definitions
    auto& settings = settings::g_hotkeysTabSettings;
    if (g_hotkey_definitions.size() >= 13) {
        // Load parsed shortcuts from settings
        g_hotkey_definitions[0].parsed = ParseHotkeyString(settings.hotkey_mute_unmute.GetValue());
        g_hotkey_definitions[1].parsed = ParseHotkeyString(settings.hotkey_background_toggle.GetValue());
        if (enabled_experimental_features) {
            g_hotkey_definitions[2].parsed = ParseHotkeyString(settings.hotkey_timeslowdown.GetValue());
            g_hotkey_definitions[4].parsed = ParseHotkeyString(settings.hotkey_autoclick.GetValue());
        }
        g_hotkey_definitions[3].parsed = ParseHotkeyString(settings.hotkey_adhd_toggle.GetValue());
        g_hotkey_definitions[5].parsed = ParseHotkeyString(settings.hotkey_input_blocking.GetValue());
        g_hotkey_definitions[6].parsed = ParseHotkeyString(settings.hotkey_display_commander_ui.GetValue());
        g_hotkey_definitions[7].parsed = ParseHotkeyString(settings.hotkey_performance_overlay.GetValue());
        g_hotkey_definitions[8].parsed = ParseHotkeyString(settings.hotkey_stopwatch.GetValue());
        g_hotkey_definitions[9].parsed = ParseHotkeyString(settings.hotkey_volume_up.GetValue());
        g_hotkey_definitions[10].parsed = ParseHotkeyString(settings.hotkey_volume_down.GetValue());
        g_hotkey_definitions[11].parsed = ParseHotkeyString(settings.hotkey_system_volume_up.GetValue());
        g_hotkey_definitions[12].parsed = ParseHotkeyString(settings.hotkey_system_volume_down.GetValue());
    }
}

// Parse a shortcut string like "ctrl+t" or "ctrl+shift+backspace"
ParsedHotkey ParseHotkeyString(const std::string& shortcut) {
    ParsedHotkey result;
    result.original_string = shortcut;

    if (shortcut.empty()) {
        return result;
    }

    // Convert to lowercase for case-insensitive parsing
    std::string lower = shortcut;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Split by '+' and process each token
    std::istringstream iss(lower);
    std::string token;
    std::vector<std::string> tokens;

    while (std::getline(iss, token, '+')) {
        // Trim whitespace
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }

    if (tokens.empty()) {
        return result;
    }

    // Process modifiers
    for (size_t i = 0; i < tokens.size() - 1; ++i) {
        if (tokens[i] == "ctrl" || tokens[i] == "control") {
            result.ctrl = true;
        } else if (tokens[i] == "shift") {
            result.shift = true;
        } else if (tokens[i] == "alt") {
            result.alt = true;
        }
    }

    // Last token is the key
    std::string key_str = tokens.back();

    // Map common key names to virtual key codes
    if (key_str.length() == 1 && key_str[0] >= 'a' && key_str[0] <= 'z') {
        // Single letter key
        result.key_code = std::toupper(static_cast<unsigned char>(key_str[0]));
    } else if (key_str == "`" || key_str == "backtick" || key_str == "grave") {
        result.key_code = VK_OEM_3;
    } else if (key_str == "backspace") {
        result.key_code = VK_BACK;
    } else if (key_str == "tab") {
        result.key_code = VK_TAB;
    } else if (key_str == "enter" || key_str == "return") {
        result.key_code = VK_RETURN;
    } else if (key_str == "escape" || key_str == "esc") {
        result.key_code = VK_ESCAPE;
    } else if (key_str == "space") {
        result.key_code = VK_SPACE;
    } else if (key_str == "delete" || key_str == "del") {
        result.key_code = VK_DELETE;
    } else if (key_str == "insert" || key_str == "ins") {
        result.key_code = VK_INSERT;
    } else if (key_str == "home") {
        result.key_code = VK_HOME;
    } else if (key_str == "end") {
        result.key_code = VK_END;
    } else if (key_str == "pageup" || key_str == "pgup") {
        result.key_code = VK_PRIOR;
    } else if (key_str == "pagedown" || key_str == "pgdn") {
        result.key_code = VK_NEXT;
    } else if (key_str == "up") {
        result.key_code = VK_UP;
    } else if (key_str == "down") {
        result.key_code = VK_DOWN;
    } else if (key_str == "left") {
        result.key_code = VK_LEFT;
    } else if (key_str == "right") {
        result.key_code = VK_RIGHT;
    } else if (key_str.length() >= 2 && (key_str[0] == 'f' || key_str[0] == 'F')) {
        // Handle F followed by numbers (e.g., "f1", "f24")
        try {
            int fn_num = std::stoi(key_str.substr(1));
            if (fn_num >= 1 && fn_num <= 24) {
                result.key_code = VK_F1 + (fn_num - 1);
            }
        } catch (...) {
            // Invalid function key number
        }
    }

    return result;
}

// Format a parsed hotkey back to a string
std::string FormatHotkeyString(const ParsedHotkey& hotkey) {
    if (!hotkey.IsValid()) {
        return "";
    }

    std::ostringstream oss;
    bool first = true;

    if (hotkey.ctrl) {
        if (!first) oss << "+";
        oss << "ctrl";
        first = false;
    }
    if (hotkey.shift) {
        if (!first) oss << "+";
        oss << "shift";
        first = false;
    }
    if (hotkey.alt) {
        if (!first) oss << "+";
        oss << "alt";
        first = false;
    }

    if (!first) oss << "+";

    // Format key name
    if (hotkey.key_code >= 'A' && hotkey.key_code <= 'Z') {
        oss << static_cast<char>(std::tolower(hotkey.key_code));
    } else if (hotkey.key_code == VK_OEM_3) {
        oss << "`";
    } else if (hotkey.key_code == VK_BACK) {
        oss << "backspace";
    } else if (hotkey.key_code >= VK_F1 && hotkey.key_code <= VK_F24) {
        oss << "f" << (hotkey.key_code - VK_F1 + 1);
    } else {
        oss << "key" << hotkey.key_code;
    }

    return oss.str();
}

void InitHotkeysTab() {
    // Ensure settings are loaded
    static bool settings_loaded = false;
    if (!settings_loaded) {
        settings::g_hotkeysTabSettings.LoadAll();
        InitializeHotkeyDefinitions();
        settings_loaded = true;
    }
}

void DrawHotkeysTab() {
    auto& settings = settings::g_hotkeysTabSettings;

    // Enable Hotkeys Master Toggle
    if (CheckboxSetting(settings.enable_hotkeys, "Enable Hotkeys")) {
        s_enable_hotkeys.store(settings.enable_hotkeys.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Master toggle for all keyboard shortcuts. When disabled, all hotkeys will not work.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Only show individual hotkey settings if hotkeys are enabled
    if (settings.enable_hotkeys.GetValue()) {
        // Update parsed shortcuts from settings
        g_hotkey_definitions[0].parsed = ParseHotkeyString(settings.hotkey_mute_unmute.GetValue());
        g_hotkey_definitions[1].parsed = ParseHotkeyString(settings.hotkey_background_toggle.GetValue());
        if (enabled_experimental_features) {
            g_hotkey_definitions[2].parsed = ParseHotkeyString(settings.hotkey_timeslowdown.GetValue());
            g_hotkey_definitions[4].parsed = ParseHotkeyString(settings.hotkey_autoclick.GetValue());
        }
        g_hotkey_definitions[3].parsed = ParseHotkeyString(settings.hotkey_adhd_toggle.GetValue());
        g_hotkey_definitions[5].parsed = ParseHotkeyString(settings.hotkey_input_blocking.GetValue());
        g_hotkey_definitions[6].parsed = ParseHotkeyString(settings.hotkey_display_commander_ui.GetValue());
        g_hotkey_definitions[7].parsed = ParseHotkeyString(settings.hotkey_performance_overlay.GetValue());
        g_hotkey_definitions[8].parsed = ParseHotkeyString(settings.hotkey_stopwatch.GetValue());
        g_hotkey_definitions[9].parsed = ParseHotkeyString(settings.hotkey_volume_up.GetValue());
        g_hotkey_definitions[10].parsed = ParseHotkeyString(settings.hotkey_volume_down.GetValue());
        g_hotkey_definitions[11].parsed = ParseHotkeyString(settings.hotkey_system_volume_up.GetValue());
        g_hotkey_definitions[12].parsed = ParseHotkeyString(settings.hotkey_system_volume_down.GetValue());

        // Create a table for hotkeys
        if (ImGui::BeginTable("HotkeysTable", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Hotkey Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 250.0f);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            // Draw each hotkey configuration
            for (size_t i = 0; i < g_hotkey_definitions.size(); ++i) {
                auto& def = g_hotkey_definitions[i];

                // Get corresponding setting
                settings::StringSetting* setting_ptr = nullptr;
                switch (i) {
                    case 0: setting_ptr = &settings.hotkey_mute_unmute; break;
                    case 1: setting_ptr = &settings.hotkey_background_toggle; break;
                    case 2:
                        if (enabled_experimental_features) setting_ptr = &settings.hotkey_timeslowdown;
                        break;
                    case 3: setting_ptr = &settings.hotkey_adhd_toggle; break;
                    case 4:
                        if (enabled_experimental_features) setting_ptr = &settings.hotkey_autoclick;
                        break;
                    case 5:  setting_ptr = &settings.hotkey_input_blocking; break;
                    case 6:  setting_ptr = &settings.hotkey_display_commander_ui; break;
                    case 7:  setting_ptr = &settings.hotkey_performance_overlay; break;
                    case 8:  setting_ptr = &settings.hotkey_stopwatch; break;
                    case 9:  setting_ptr = &settings.hotkey_volume_up; break;
                    case 10: setting_ptr = &settings.hotkey_volume_down; break;
                    case 11: setting_ptr = &settings.hotkey_system_volume_up; break;
                    case 12: setting_ptr = &settings.hotkey_system_volume_down; break;
                    default: setting_ptr = nullptr; break;
                }

                if (setting_ptr == nullptr) continue;

                ui::new_ui::StringSetting& setting = *setting_ptr;
                DrawHotkeyEntry(def, setting, static_cast<int>(i));
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Format: ctrl+shift+key");
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Empty string = disabled");
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Example: \"ctrl+t\", \"ctrl+shift+backspace\"");
    }

    // Debug Information Section
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Debug Information", ImGuiTreeNodeFlags_None)) {
        LONGLONG now_ns = utils::get_now_ns();
        LONGLONG last_call_age_ns = now_ns - g_hotkey_debug_info.last_call_time_ns;
        double last_call_age_ms = static_cast<double>(last_call_age_ns) / 1000000.0;

        // Last call time
        if (g_hotkey_debug_info.last_call_time_ns > 0) {
            ImGui::Text("Last ProcessHotkeys() call: %.2f ms ago", last_call_age_ms);
            if (last_call_age_ms > 1000.0) {
                ImGui::SameLine();
                ui::colors::PushIconColor(ui::colors::ICON_WARNING);
                ImGui::Text(ICON_FK_WARNING);
                ui::colors::PopIconColor();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "ProcessHotkeys hasn't been called recently - check continuous monitoring thread");
                }
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Last ProcessHotkeys() call: Never");
        }

        // Last successful call time
        if (g_hotkey_debug_info.last_successful_call_time_ns > 0) {
            LONGLONG last_success_age_ns = now_ns - g_hotkey_debug_info.last_successful_call_time_ns;
            double last_success_age_ms = static_cast<double>(last_success_age_ns) / 1000000.0;
            ImGui::Text("Last successful call: %.2f ms ago", last_success_age_ms);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Last successful call: Never");
        }

        ImGui::Spacing();

        // Current state checks
        ImGui::Text("Current State:");
        ImGui::Indent();

        // Hotkeys enabled
        if (g_hotkey_debug_info.hotkeys_enabled) {
            ui::colors::PushIconColor(ui::colors::ICON_SUCCESS);
            ImGui::Text(ICON_FK_OK " Hotkeys enabled: Yes");
            ui::colors::PopIconColor();
        } else {
            ui::colors::PushIconColor(ui::colors::ICON_WARNING);
            ImGui::Text(ICON_FK_CANCEL " Hotkeys enabled: No");
            ui::colors::PopIconColor();
        }

        // Game HWND
        if (g_hotkey_debug_info.game_hwnd_valid) {
            ui::colors::PushIconColor(ui::colors::ICON_SUCCESS);
            ImGui::Text(ICON_FK_OK " Game window: Valid (0x%p)", g_hotkey_debug_info.game_hwnd);
            ui::colors::PopIconColor();
        } else {
            ui::colors::PushIconColor(ui::colors::ICON_WARNING);
            ImGui::Text(ICON_FK_CANCEL " Game window: Invalid (null)");
            ui::colors::PopIconColor();
        }

        // Game in foreground
        if (g_hotkey_debug_info.game_in_foreground) {
            ui::colors::PushIconColor(ui::colors::ICON_SUCCESS);
            ImGui::Text(ICON_FK_OK " Game in foreground: Yes");
            ui::colors::PopIconColor();
        } else {
            ui::colors::PushIconColor(ui::colors::ICON_DISABLED);
            ImGui::Text(ICON_FK_MINUS " Game in foreground: No");
            ui::colors::PopIconColor();
            if (g_hotkey_debug_info.current_foreground_hwnd != nullptr) {
                ImGui::Text("  Foreground window: 0x%p", g_hotkey_debug_info.current_foreground_hwnd);
            }
        }

        // UI open
        if (g_hotkey_debug_info.ui_open) {
            ui::colors::PushIconColor(ui::colors::ICON_SUCCESS);
            ImGui::Text(ICON_FK_OK " Display Commander UI: Open");
            ui::colors::PopIconColor();
        } else {
            ui::colors::PushIconColor(ui::colors::ICON_DISABLED);
            ImGui::Text(ICON_FK_MINUS " Display Commander UI: Closed");
            ui::colors::PopIconColor();
        }

        ImGui::Unindent();

        // Block reason
        if (!g_hotkey_debug_info.last_block_reason.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Last block reason:");
            ImGui::SameLine();
            ImGui::Text("%s", g_hotkey_debug_info.last_block_reason.c_str());
        } else if (g_hotkey_debug_info.last_call_time_ns > 0) {
            ImGui::Spacing();
            ui::colors::PushIconColor(ui::colors::ICON_SUCCESS);
            ImGui::Text(ICON_FK_OK " No blocking conditions - hotkeys should work");
            ui::colors::PopIconColor();
        }
    }
}

// Process all hotkeys (call from continuous monitoring loop)
void ProcessHotkeys() {
    // Update debug info - always track when this function is called
    LONGLONG now_ns = utils::get_now_ns();
    g_hotkey_debug_info.last_call_time_ns = now_ns;

    // Check if hotkeys are enabled globally
    bool hotkeys_enabled = s_enable_hotkeys.load();
    g_hotkey_debug_info.hotkeys_enabled = hotkeys_enabled;
    if (!hotkeys_enabled) {
        g_hotkey_debug_info.last_block_reason = "Hotkeys disabled (master toggle off)";
        return;
    }

    // Ensure all keys are being tracked
    display_commanderhooks::keyboard_tracker::IsKeyDown(VK_CONTROL);
    display_commanderhooks::keyboard_tracker::IsKeyDown(VK_SHIFT);
    display_commanderhooks::keyboard_tracker::IsKeyDown(VK_MENU);

    // Handle keyboard shortcuts (when game is in foreground OR when Display Commander UI is open)
    // When the UI is open, it becomes the foreground window, but we still want hotkeys to work
    HWND game_hwnd = g_last_swapchain_hwnd.load();
    HWND foreground_hwnd = GetForegroundWindow();
    bool is_game_in_foreground = (game_hwnd != nullptr && foreground_hwnd == game_hwnd);
    bool is_ui_open = settings::g_mainTabSettings.show_display_commander_ui.GetValue();

    // Update debug info
    g_hotkey_debug_info.game_hwnd = game_hwnd;
    g_hotkey_debug_info.game_hwnd_valid = (game_hwnd != nullptr);
    g_hotkey_debug_info.current_foreground_hwnd = foreground_hwnd;
    g_hotkey_debug_info.game_in_foreground = is_game_in_foreground;
    g_hotkey_debug_info.ui_open = is_ui_open;

    // Allow hotkeys if game is in foreground OR if UI is open (UI is an overlay, so hotkeys should work)
    if (!is_game_in_foreground && !is_ui_open) {
        if (game_hwnd == nullptr) {
            g_hotkey_debug_info.last_block_reason = "No game window detected (swapchain not initialized)";
        } else {
            g_hotkey_debug_info.last_block_reason = "Game not in foreground and UI not open";
        }
        return;
    }

    // Successfully passed all checks - update successful call time
    g_hotkey_debug_info.last_successful_call_time_ns = now_ns;
    g_hotkey_debug_info.last_block_reason = "";

    // Process each hotkey definition
    for (auto& def : g_hotkey_definitions) {
        if (!def.parsed.IsValid() || def.parsed.IsEmpty()) {
            continue;
        }

        const auto& hotkey = def.parsed;

        // Check if the key was pressed
        bool key_pressed = display_commanderhooks::keyboard_tracker::IsKeyPressed(hotkey.key_code);
        if (!key_pressed) {
            continue;
        }

        // Check modifier states (must match exactly - if hotkey requires modifier, it must be pressed; if not, it must
        // not be pressed)
        bool ctrl_down = display_commanderhooks::keyboard_tracker::IsKeyDown(VK_CONTROL);
        bool shift_down = display_commanderhooks::keyboard_tracker::IsKeyDown(VK_SHIFT);
        bool alt_down = display_commanderhooks::keyboard_tracker::IsKeyDown(VK_MENU);

        // Check if modifiers match exactly
        if (hotkey.ctrl != ctrl_down) continue;
        if (hotkey.shift != shift_down) continue;
        if (hotkey.alt != alt_down) continue;

        // All conditions met - execute the action
        if (def.action) {
            def.action();
        }
    }
}

}  // namespace ui::new_ui
