#include "hotkeys_tab.hpp"
#include "../imgui_wrapper_base.hpp"
#include "../../adhd_multi_monitor/adhd_simple_api.hpp"
#include "../../audio/audio_management.hpp"
#include "../../autoclick/autoclick_manager.hpp"
#include "display/display_cache.hpp"
#include "../../globals.hpp"
#include "../../hooks/api_hooks.hpp"
#include "../../hooks/display_settings_hooks.hpp"
#include "../../hooks/window_proc_hooks.hpp"
#include "../../hooks/windows_hooks/windows_message_hooks.hpp"
#include "../../input_remapping/input_remapping.hpp"
#include "../../res/forkawesome.h"
#include "../../res/ui_colors.hpp"
#include "../../settings/advanced_tab_settings.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../settings/hotkeys_tab_settings.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"
#include "imgui.h"
#include "settings_wrapper.hpp"
#include "utils/timing.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <vector>

namespace ui::new_ui {

namespace {
// VK code (0-255) -> list of string names; first is the primary display form. 256 elements.
std::array<std::vector<std::string>, 256> g_vk_to_strings;

void InitVkToStringsTable() {
    auto& t = g_vk_to_strings;
    t[VK_BACK] = {"backspace"};
    t[VK_TAB] = {"tab"};
    t[VK_RETURN] = {"enter", "return"};
    t[VK_ESCAPE] = {"escape", "esc"};
    t[VK_SPACE] = {"space"};
    t[0x2D] = {"insert", "ins"};      // VK_INSERT
    t[0x2E] = {"delete", "del"};      // VK_DELETE
    t[0x21] = {"pageup", "pgup"};      // VK_PRIOR
    t[0x22] = {"pagedown", "pgdn"};    // VK_NEXT
    t[0x23] = {"end"};
    t[0x24] = {"home"};
    t[0x25] = {"left"};
    t[0x26] = {"up"};
    t[0x27] = {"right"};
    t[0x28] = {"down"};
    for (int i = 0; i <= 9; ++i) {
        t[0x30 + i] = {std::string(1, '0' + i)};  // VK_0..VK_9
    }
    for (int i = 0; i < 26; ++i) {
        t[0x41 + i] = {std::string(1, 'a' + i)};  // VK_A..VK_Z
    }
    t[0x60] = {"numpad0", "num0"};   // VK_NUMPAD0
    t[0x61] = {"numpad1", "num1"};
    t[0x62] = {"numpad2", "num2"};
    t[0x63] = {"numpad3", "num3"};
    t[0x64] = {"numpad4", "num4"};
    t[0x65] = {"numpad5", "num5"};
    t[0x66] = {"numpad6", "num6"};
    t[0x67] = {"numpad7", "num7"};
    t[0x68] = {"numpad8", "num8"};
    t[0x69] = {"numpad9", "num9"};
    t[0x6A] = {"numpad*", "numpadmultiply", "nummultiply"};   // VK_MULTIPLY
    t[0x6B] = {"numpad+", "numpadadd", "numadd"};            // VK_ADD
    t[0x6C] = {"numpadsep"};                                  // VK_SEPARATOR
    t[0x6D] = {"numpad-", "numpadsubtract", "numsubtract"};  // VK_SUBTRACT
    t[0x6E] = {"numpad.", "numpaddecimal", "numdecimal"};    // VK_DECIMAL
    t[0x6F] = {"numpad/", "numpaddivide", "numdivide"};      // VK_DIVIDE
    for (int i = 1; i <= 24; ++i) {
        t[0x70 - 1 + i] = {"f" + std::to_string(i)};  // VK_F1..VK_F24
    }
    t[0xC0] = {"`", "backtick", "grave"};   // VK_OEM_3
    t[0xBF] = {"/", "slash"};                // VK_OEM_2
    t[0xDC] = {"\\", "backslash"};           // VK_OEM_5
}

// Primary display name for a VK (for readable config format)
static std::string VkToReadableName(int vk) {
    if (vk >= 0 && vk < 256 && !g_vk_to_strings[vk].empty()) {
        return g_vk_to_strings[vk][0];
    }
    return "key" + std::to_string(vk);
}

// Parse readable space-separated format: "ctrl a", "alt numpad+", "numpad+"
static ParsedHotkey ParseReadableHotkeyString(const std::string& value) {
    ParsedHotkey p;
    if (value.empty()) return p;
    std::istringstream iss(value);
    std::string token;
    std::vector<std::string> tokens;
    while (iss >> token) {
        tokens.push_back(token);
    }
    if (tokens.empty()) return p;
    for (const std::string& raw : tokens) {
        std::string t = raw;
        std::transform(t.begin(), t.end(), t.begin(), ::tolower);
        if (t == "ctrl" || t == "control") {
            p.ctrl = true;
        } else if (t == "shift") {
            p.shift = true;
        } else if (t == "alt") {
            p.alt = true;
        } else if (t == "win" || t == "windows") {
            p.win = true;
        } else {
            // Key: look up in VK table (case-insensitive)
            for (int vk = 0; vk < 256; ++vk) {
                for (const std::string& s : g_vk_to_strings[vk]) {
                    std::string sl = s;
                    std::transform(sl.begin(), sl.end(), sl.begin(), ::tolower);
                    if (sl == t) {
                        p.key_code = vk;
                        break;
                    }
                }
                if (p.key_code != 0) break;
            }
            if (p.key_code == 0 && t.size() >= 4 && t.substr(0, 3) == "key") {
                try {
                    int vk = std::stoi(t.substr(3));
                    if (vk >= 1 && vk <= 255) p.key_code = vk;
                } catch (...) {}
            }
        }
    }
    return p;
}

std::string SerializeHotkeyToConfigString(const ParsedHotkey& p) {
    if (!p.IsValid()) return "";
    std::ostringstream oss;
    bool first = true;
    if (p.ctrl) { if (!first) oss << " "; oss << "ctrl"; first = false; }
    if (p.shift) { if (!first) oss << " "; oss << "shift"; first = false; }
    if (p.alt) { if (!first) oss << " "; oss << "alt"; first = false; }
    if (p.win) { if (!first) oss << " "; oss << "win"; first = false; }
    if (!first) oss << " ";
    oss << VkToReadableName(p.key_code);
    return oss.str();
}

ParsedHotkey DeserializeHotkeyFromConfigString(const std::string& value) {
    ParsedHotkey p;
    if (value.empty()) return p;
    // Readable format: "ctrl a", "alt numpad+", "numpad+"
    p = ParseReadableHotkeyString(value);
    if (p.IsValid()) return p;
    // Plus-separated: "ctrl+shift+m"
    return ParseHotkeyString(value);
}

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

// Index of the hotkey row currently capturing a key (-1 = none). When set, ProcessHotkeys runs capture logic instead of normal hotkeys.
static int s_capturing_hotkey_index = -1;
// Capture result from ProcessHotkeys (same thread as Update): UI applies when it next draws.
static bool s_capture_pending = false;
static int s_captured_for_index = -1;
static ParsedHotkey s_captured_parsed;

// Returns true if vk is a modifier-only key (we use it as modifier state, not as main key when capturing).
static bool IsModifierVKey(int vk) {
    return vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN;
}

// Returns true if the current keyboard state (key just pressed + modifiers) matches the given parsed hotkey.
static bool HotkeyMatchesCurrentState(const ParsedHotkey& p) {
    if (!p.IsValid()) return false;
    if (!display_commanderhooks::keyboard_tracker::IsKeyPressed(p.key_code)) return false;
    if (p.ctrl != display_commanderhooks::keyboard_tracker::IsKeyDown(VK_CONTROL)) return false;
    if (p.shift != display_commanderhooks::keyboard_tracker::IsKeyDown(VK_SHIFT)) return false;
    if (p.alt != display_commanderhooks::keyboard_tracker::IsKeyDown(VK_MENU)) return false;
    bool win_down = display_commanderhooks::keyboard_tracker::IsKeyDown(VK_LWIN)
                    || display_commanderhooks::keyboard_tracker::IsKeyDown(VK_RWIN);
    if (p.win != win_down) return false;
    return true;
}

// Draw a single hotkey entry in the table. Display string is derived from def.parsed (no parsing).
void DrawHotkeyEntry(display_commander::ui::IImGuiWrapper& imgui, HotkeyDefinition& def,
                    ui::new_ui::StringSetting& setting, int index) {
    imgui.TableNextRow();

    // Source of truth is def.parsed (numeric); display from formatted string
    std::string display_value = FormatHotkeyString(def.parsed);
    const bool this_row_capturing = (s_capturing_hotkey_index == index);

    // Hotkey Name
    imgui.TableNextColumn();
    imgui.Text("%s", def.name.c_str());
    if (imgui.IsItemHovered() && !def.description.empty()) {
        imgui.SetTooltip("%s", def.description.c_str());
    }

    // Shortcut Input + Capture
    imgui.TableNextColumn();
    // Apply pending capture from ProcessHotkeys (runs in same thread as keyboard_tracker::Update)
    if (s_capture_pending && s_captured_for_index == index) {
        setting.SetValue(SerializeHotkeyToConfigString(s_captured_parsed));
        def.parsed = s_captured_parsed;
        def.parsed.original_string = FormatHotkeyString(s_captured_parsed);
        s_capture_pending = false;
        s_captured_for_index = -1;
    }

    if (this_row_capturing) {
        imgui.TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "Press key... (Esc to cancel)");
    } else {
        char buffer[256] = {0};
        strncpy_s(buffer, sizeof(buffer), display_value.c_str(), _TRUNCATE);

        imgui.SetNextItemWidth(-1);
        if (imgui.InputText(("##HotkeyInput" + std::to_string(index)).c_str(), buffer, sizeof(buffer))) {
            std::string new_value(buffer);
            setting.SetValue(new_value);
            def.parsed = DeserializeHotkeyFromConfigString(new_value);
        }
    }

    // Status (from parsed state only; no string parsing)
    imgui.TableNextColumn();
    if (this_row_capturing) {
        ui::colors::PushIconColor(&imgui, ui::colors::ICON_WARNING);
        imgui.Text(ICON_FK_PENCIL " Capturing");
        ui::colors::PopIconColor(&imgui);
    } else if (def.parsed.IsValid()) {
        ui::colors::PushIconColor(&imgui, ui::colors::ICON_SUCCESS);
        imgui.Text(ICON_FK_OK " Active");
        ui::colors::PopIconColor(&imgui);
    } else if (!def.parsed.IsEmpty()) {
        ui::colors::PushIconColor(&imgui, ui::colors::ICON_WARNING);
        imgui.Text(ICON_FK_WARNING " Invalid");
        ui::colors::PopIconColor(&imgui);
    } else {
        ui::colors::PushIconColor(&imgui, ui::colors::ICON_DISABLED);
        imgui.Text(ICON_FK_MINUS " Disabled");
        ui::colors::PopIconColor(&imgui);
    }

    // Actions: Capture + Reset
    imgui.TableNextColumn();
    if (this_row_capturing) {
        if (imgui.Button(("Cancel##" + def.id).c_str())) {
            s_capturing_hotkey_index = -1;
        }
    } else {
        if (imgui.Button(("...##Capture" + def.id).c_str())) {
            s_capturing_hotkey_index = index;
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("Capture shortcut (press key combination including numpad)");
        }
        imgui.SameLine();
        if (imgui.Button(("Reset##" + def.id).c_str())) {
            setting.SetValue(def.default_shortcut);
            def.parsed = DeserializeHotkeyFromConfigString(def.default_shortcut);
        }
    }
}
}  // namespace

// Numeric array representation for storage/persistence
std::array<int, kHotkeyArraySize> ParsedHotkey::ToArray() const {
    return {{key_code, ctrl ? 1 : 0, shift ? 1 : 0, alt ? 1 : 0, win ? 1 : 0}};
}

ParsedHotkey ParsedHotkey::FromArray(const std::array<int, kHotkeyArraySize>& arr) {
    ParsedHotkey p;
    p.key_code = arr[0];
    p.ctrl = (arr[1] != 0);
    p.shift = (arr[2] != 0);
    p.alt = (arr[3] != 0);
    p.win = (arr[4] != 0);
    return p;
}

std::string GetHotkeyDisplayString(const HotkeyDefinition& def) {
    return FormatHotkeyString(def.parsed);
}

// Initialize hotkey definitions with default values
void InitializeHotkeyDefinitions() {
    g_hotkey_definitions = {
        {"mute_unmute", "Mute/Unmute Audio", "ctrl shift m", "Toggle audio mute state",
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
        {"adhd_toggle", "ADHD Multi-Monitor Mode", "ctrl shift d", "Toggle ADHD Multi-Monitor Mode",
         []() {
             bool current_state = settings::g_mainTabSettings.adhd_multi_monitor_enabled.GetValue();
             bool new_state = !current_state;
             settings::g_mainTabSettings.adhd_multi_monitor_enabled.SetValue(new_state);
             bool game_display = settings::g_mainTabSettings.adhd_single_monitor_enabled_for_game_display.GetValue();
             adhd_multi_monitor::api::SetEnabled(game_display, new_state);
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
        {"performance_overlay", "Performance Overlay Toggle", "ctrl shift o", "Toggle the performance overlay",
         []() {
             bool current_state = settings::g_mainTabSettings.show_test_overlay.GetValue();
             bool new_state = !current_state;
             settings::g_mainTabSettings.show_test_overlay.SetValue(new_state);
             std::ostringstream oss;
             oss << "Performance overlay " << (new_state ? "enabled" : "disabled") << " via hotkey";
             LogInfo(oss.str().c_str());
         }},
        {"stopwatch", "Stopwatch Start/Pause", "ctrl shift s", "Start or pause the stopwatch (2-state toggle)",
         []() { display_commander::input_remapping::ToggleStopwatch(); }},
        {"volume_up", "Volume Up", "ctrl shift up", "Increase audio volume (percentage-based, min 1%)",
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
        {"volume_down", "Volume Down", "ctrl shift down", "Decrease audio volume (percentage-based, min 1%)",
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
        {"system_volume_up", "System Volume Up", "ctrl alt up",
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
        {"system_volume_down", "System Volume Down", "ctrl alt down",
         "Decrease system master volume (percentage-based, min 1%)",
         []() {
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
         }},
        {"auto_hdr", "AutoHDR Toggle", "", "Toggle AutoHDR (DisplayCommander_PerceptualBoost SDR-to-HDR effect)",
         []() {
             bool current_state = settings::g_mainTabSettings.auto_hdr.GetValue();
             bool new_state = !current_state;
             settings::g_mainTabSettings.auto_hdr.SetValue(new_state);
             std::ostringstream oss;
             oss << "AutoHDR " << (new_state ? "enabled" : "disabled") << " via hotkey";
             LogInfo(oss.str().c_str());
         }},
        {"brightness_down", "Brightness Down", "",
         "Decrease Display Commander brightness (0-200%%, step configurable below, 100%% = neutral)",
         []() {
             float step = static_cast<float>(settings::g_hotkeysTabSettings.brightness_hotkey_step_percent.GetValue());
             constexpr float min_percent = 0.0f;
             float current = settings::g_mainTabSettings.brightness_percent.GetValue();
             float next = (std::max)(min_percent, current - step);
             settings::g_mainTabSettings.brightness_percent.SetValue(next);
             std::ostringstream oss;
             oss << "Brightness decreased to " << std::fixed << std::setprecision(0) << next << "%% via hotkey";
             LogInfo(oss.str().c_str());
         }},
        {"brightness_up", "Brightness Up", "",
         "Increase Display Commander brightness (0-200%%, step configurable below, 100%% = neutral)",
         []() {
             float step = static_cast<float>(settings::g_hotkeysTabSettings.brightness_hotkey_step_percent.GetValue());
             constexpr float max_percent = 200.0f;
             float current = settings::g_mainTabSettings.brightness_percent.GetValue();
             float next = (std::min)(max_percent, current + step);
             settings::g_mainTabSettings.brightness_percent.SetValue(next);
             std::ostringstream oss;
             oss << "Brightness increased to " << std::fixed << std::setprecision(0) << next << "%% via hotkey";
             LogInfo(oss.str().c_str());
         }},
        {"win_down", "Win+Down (Minimize)", "win down",
         "Minimize borderless game window (Special-K style). Only when game is in foreground.",
         []() {
             HWND game_hwnd = g_last_swapchain_hwnd.load();
             if (!game_hwnd) return;
             if (display_commanderhooks::GetForegroundWindow_Direct() != game_hwnd) return;
             if (display_commanderhooks::WindowHasBorder(game_hwnd)) return;
             LogInfo("Hotkey Win+Down: minimizing game window HWND 0x%p", game_hwnd);
             ShowWindow_Direct(game_hwnd, SW_MINIMIZE);
         }},
        {"win_up", "Win+Up (Restore)", "win up",
         "Restore minimized borderless game. Works in foreground or within grace period (Advanced tab).",
         []() {
             HWND game_hwnd = g_last_swapchain_hwnd.load();
             if (!game_hwnd) return;
             if (display_commanderhooks::WindowHasBorder(game_hwnd)) return;
             HWND fg = display_commanderhooks::GetForegroundWindow_Direct();
             bool is_fg = (fg == game_hwnd);
             int grace_sec = settings::g_advancedTabSettings.win_up_grace_seconds.GetValue();
             LONGLONG last_ns = g_last_foreground_background_switch_ns.load(std::memory_order_acquire);
             LONGLONG now_ns = utils::get_now_ns();
             bool grace_ok = (grace_sec >= 61)
                             || (grace_sec > 0 && last_ns != 0
                                 && (now_ns - last_ns <= static_cast<LONGLONG>(grace_sec) * utils::SEC_TO_NS));
             if (is_fg || grace_ok) {
                 ShowWindow_Direct(game_hwnd, SW_RESTORE);
             }
         }},
        {"win_left", "Win+Left (Previous display)", "win left",
         "Set target display to previous monitor. Only when game is in foreground.",
         []() {
             if (display_commanderhooks::GetForegroundWindow_Direct() != g_last_swapchain_hwnd.load()) return;
             if (!display_cache::g_displayCache.IsInitialized()) return;
             HWND game_hwnd = g_last_swapchain_hwnd.load();
             std::string current_id = settings::g_mainTabSettings.selected_extended_display_device_id.GetValue();
             if (current_id.empty())
                 current_id = settings::g_mainTabSettings.target_extended_display_device_id.GetValue();
             if (current_id.empty() && game_hwnd)
                 current_id = settings::GetExtendedDisplayDeviceIdFromWindow(game_hwnd);
             if (current_id.empty() || current_id == "No Window" || current_id == "No Monitor"
                 || current_id == "Monitor Info Failed")
                 return;
             std::string new_id = display_cache::g_displayCache.GetAdjacentDisplayDeviceId(current_id, true);
             if (new_id.empty()) {
                 LogInfo("Win+Left: no adjacent display.");
                 return;
             }
             const WindowMode mode = s_window_mode.load();
             bool switched_mode = (mode == WindowMode::kNoChanges || mode == WindowMode::kPreventFullscreenNoResize);
             if (switched_mode) {
                 settings::g_mainTabSettings.window_mode.SetValue(static_cast<int>(WindowMode::kFullscreen));
                 s_window_mode.store(WindowMode::kFullscreen);
                 settings::g_mainTabSettings.window_mode.Save();
             }
             settings::g_mainTabSettings.selected_extended_display_device_id.SetValue(new_id);
             settings::g_mainTabSettings.target_extended_display_device_id.SetValue(new_id);
             LogInfo("Win+Left: target display set to \"%s\".", new_id.c_str());
         }},
        {"win_right", "Win+Right (Next display)", "win right",
         "Set target display to next monitor. Only when game is in foreground.", []() {
             if (display_commanderhooks::GetForegroundWindow_Direct() != g_last_swapchain_hwnd.load()) return;
             if (!display_cache::g_displayCache.IsInitialized()) return;
             HWND game_hwnd = g_last_swapchain_hwnd.load();
             std::string current_id = settings::g_mainTabSettings.selected_extended_display_device_id.GetValue();
             if (current_id.empty())
                 current_id = settings::g_mainTabSettings.target_extended_display_device_id.GetValue();
             if (current_id.empty() && game_hwnd)
                 current_id = settings::GetExtendedDisplayDeviceIdFromWindow(game_hwnd);
             if (current_id.empty() || current_id == "No Window" || current_id == "No Monitor"
                 || current_id == "Monitor Info Failed")
                 return;
             std::string new_id = display_cache::g_displayCache.GetAdjacentDisplayDeviceId(current_id, false);
             if (new_id.empty()) {
                 LogInfo("Win+Right: no adjacent display.");
                 return;
             }
             const WindowMode mode = s_window_mode.load();
             bool switched_mode = (mode == WindowMode::kNoChanges || mode == WindowMode::kPreventFullscreenNoResize);
             if (switched_mode) {
                 settings::g_mainTabSettings.window_mode.SetValue(static_cast<int>(WindowMode::kFullscreen));
                 s_window_mode.store(WindowMode::kFullscreen);
                 settings::g_mainTabSettings.window_mode.Save();
             }
             settings::g_mainTabSettings.selected_extended_display_device_id.SetValue(new_id);
             settings::g_mainTabSettings.target_extended_display_device_id.SetValue(new_id);
             LogInfo("Win+Right: target display set to \"%s\".", new_id.c_str());
         }},
        {"move_to_primary", "Move to primary monitor", "numpad+",
         "Set target display to primary monitor. Only when game is in foreground.",
         []() {
             if (display_commanderhooks::GetForegroundWindow_Direct() != g_last_swapchain_hwnd.load()) return;
             if (!display_cache::g_displayCache.IsInitialized()) return;
             auto display_info = display_cache::g_displayCache.GetDisplayInfoForUI();
             std::string primary_id;
             for (const auto& info : display_info) {
                 if (info.is_primary) {
                     primary_id = info.extended_device_id;
                     break;
                 }
             }
             if (primary_id.empty()) {
                 LogInfo("Move to primary: no primary display found.");
                 return;
             }
             const WindowMode mode = s_window_mode.load();
             bool switched_mode = (mode == WindowMode::kNoChanges || mode == WindowMode::kPreventFullscreenNoResize);
             if (switched_mode) {
                 settings::g_mainTabSettings.window_mode.SetValue(static_cast<int>(WindowMode::kFullscreen));
                 s_window_mode.store(WindowMode::kFullscreen);
                 settings::g_mainTabSettings.window_mode.Save();
             }
             settings::g_mainTabSettings.selected_extended_display_device_id.SetValue(primary_id);
             settings::g_mainTabSettings.target_extended_display_device_id.SetValue(primary_id);
             LogInfo("Move to primary: target display set to primary monitor.");
         }},
        {"move_to_secondary", "Move to secondary monitor", "numpad-",
         "Set target display to the first non-primary monitor. Only when game is in foreground.",
         []() {
             if (display_commanderhooks::GetForegroundWindow_Direct() != g_last_swapchain_hwnd.load()) return;
             if (!display_cache::g_displayCache.IsInitialized()) return;
             auto display_info = display_cache::g_displayCache.GetDisplayInfoForUI();
             std::string secondary_id;
             for (const auto& info : display_info) {
                 if (!info.is_primary) {
                     secondary_id = info.extended_device_id;
                     break;
                 }
             }
             if (secondary_id.empty()) {
                 LogInfo("Move to secondary: no non-primary display found.");
                 return;
             }
             const WindowMode mode = s_window_mode.load();
             bool switched_mode = (mode == WindowMode::kNoChanges || mode == WindowMode::kPreventFullscreenNoResize);
             if (switched_mode) {
                 settings::g_mainTabSettings.window_mode.SetValue(static_cast<int>(WindowMode::kFullscreen));
                 s_window_mode.store(WindowMode::kFullscreen);
                 settings::g_mainTabSettings.window_mode.Save();
             }
             settings::g_mainTabSettings.selected_extended_display_device_id.SetValue(secondary_id);
             settings::g_mainTabSettings.target_extended_display_device_id.SetValue(secondary_id);
             LogInfo("Move to secondary: target display set to secondary monitor.");
         }}};

    // Map settings to definitions
    auto& settings = settings::g_hotkeysTabSettings;
    if (g_hotkey_definitions.size() >= kHotkeyDefinitionCount) {
        // Load from config: "0x11;0x10;0x65" or legacy "ctrl+shift+m"
        g_hotkey_definitions[0].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_mute_unmute.GetValue());
        g_hotkey_definitions[1].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_background_toggle.GetValue());
        if (enabled_experimental_features) {
            g_hotkey_definitions[2].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_timeslowdown.GetValue());
            g_hotkey_definitions[4].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_autoclick.GetValue());
        }
        g_hotkey_definitions[3].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_adhd_toggle.GetValue());
        g_hotkey_definitions[5].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_input_blocking.GetValue());
        g_hotkey_definitions[6].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_display_commander_ui.GetValue());
        g_hotkey_definitions[7].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_performance_overlay.GetValue());
        g_hotkey_definitions[8].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_stopwatch.GetValue());
        g_hotkey_definitions[9].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_volume_up.GetValue());
        g_hotkey_definitions[10].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_volume_down.GetValue());
        g_hotkey_definitions[11].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_system_volume_up.GetValue());
        g_hotkey_definitions[12].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_system_volume_down.GetValue());
        g_hotkey_definitions[13].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_auto_hdr.GetValue());
        g_hotkey_definitions[14].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_brightness_down.GetValue());
        g_hotkey_definitions[15].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_brightness_up.GetValue());
        g_hotkey_definitions[16].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_win_down.GetValue());
        g_hotkey_definitions[17].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_win_up.GetValue());
        g_hotkey_definitions[18].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_win_left.GetValue());
        g_hotkey_definitions[19].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_win_right.GetValue());
        g_hotkey_definitions[20].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_move_to_primary.GetValue());
        g_hotkey_definitions[21].parsed = DeserializeHotkeyFromConfigString(settings.hotkey_move_to_secondary.GetValue());
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
        } else if (tokens[i] == "win" || tokens[i] == "windows") {
            result.win = true;
        }
    }

    // Last token is the key
    std::string key_str = tokens.back();

    // key<N> fallback for any VK code
    if (key_str.size() >= 4 && key_str.substr(0, 3) == "key") {
        try {
            int vk = std::stoi(key_str.substr(3));
            if (vk >= 1 && vk <= 255) {
                result.key_code = vk;
            }
        } catch (...) {
            // Invalid key number
        }
        return result;
    }

    // Look up in VK -> list of strings table (256 elements)
    for (int vk = 0; vk < 256; ++vk) {
        for (const std::string& s : g_vk_to_strings[vk]) {
            if (s == key_str) {
                result.key_code = vk;
                return result;
            }
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
    if (hotkey.win) {
        if (!first) oss << "+";
        oss << "win";
        first = false;
    }

    if (!first) oss << "+";

    // Format key name from VK -> strings table (first string is display form)
    if (hotkey.key_code >= 0 && hotkey.key_code < 256 && !g_vk_to_strings[hotkey.key_code].empty()) {
        oss << g_vk_to_strings[hotkey.key_code][0];
    } else {
        oss << "key" << hotkey.key_code;
    }

    return oss.str();
}

void InitHotkeysTab() {
    static bool settings_loaded = false;
    if (!settings_loaded) {
        InitVkToStringsTable();
        settings::g_hotkeysTabSettings.LoadAll();
        InitializeHotkeyDefinitions();
        settings_loaded = true;
    }
}

void SyncHotkeySettingsFromParsed() {
    if (g_hotkey_definitions.size() < kHotkeyDefinitionCount) {
        return;
    }
    auto& s = settings::g_hotkeysTabSettings;
    // Store in config as "0x11;0x10;0x65" (VK list: modifiers then key)
    s.hotkey_mute_unmute.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[0].parsed));
    s.hotkey_background_toggle.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[1].parsed));
    if (enabled_experimental_features) {
        s.hotkey_timeslowdown.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[2].parsed));
        s.hotkey_autoclick.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[4].parsed));
    }
    s.hotkey_adhd_toggle.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[3].parsed));
    s.hotkey_input_blocking.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[5].parsed));
    s.hotkey_display_commander_ui.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[6].parsed));
    s.hotkey_performance_overlay.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[7].parsed));
    s.hotkey_stopwatch.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[8].parsed));
    s.hotkey_volume_up.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[9].parsed));
    s.hotkey_volume_down.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[10].parsed));
    s.hotkey_system_volume_up.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[11].parsed));
    s.hotkey_system_volume_down.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[12].parsed));
    s.hotkey_auto_hdr.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[13].parsed));
    s.hotkey_brightness_down.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[14].parsed));
    s.hotkey_brightness_up.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[15].parsed));
    s.hotkey_win_down.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[16].parsed));
    s.hotkey_win_up.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[17].parsed));
    s.hotkey_win_left.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[18].parsed));
    s.hotkey_win_right.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[19].parsed));
    s.hotkey_move_to_primary.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[20].parsed));
    s.hotkey_move_to_secondary.SetValue(SerializeHotkeyToConfigString(g_hotkey_definitions[21].parsed));
}

void DrawHotkeysTab(display_commander::ui::IImGuiWrapper& imgui) {
    auto& settings = settings::g_hotkeysTabSettings;

    // Enable Hotkeys Master Toggle
    if (CheckboxSetting(settings.enable_hotkeys, "Enable Hotkeys", imgui)) {
        s_enable_hotkeys.store(settings.enable_hotkeys.GetValue());
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Master toggle for all keyboard shortcuts. When disabled, all hotkeys will not work.");
    }

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    // Only show individual hotkey settings if hotkeys are enabled.
    // Source of truth is def.parsed (numeric); display via FormatHotkeyString; no per-frame parsing.
    if (settings.enable_hotkeys.GetValue()) {
        // Create a table for hotkeys
        const int table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable;
        if (imgui.BeginTable("HotkeysTable", 4, table_flags)) {
            imgui.TableSetupColumn("Hotkey Name", ImGuiTableColumnFlags_WidthStretch);
            imgui.TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 250.0f);
            imgui.TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            imgui.TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            imgui.TableHeadersRow();

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
                    case 13: setting_ptr = &settings.hotkey_auto_hdr; break;
                    case 14: setting_ptr = &settings.hotkey_brightness_down; break;
                    case 15: setting_ptr = &settings.hotkey_brightness_up; break;
                    case 16: setting_ptr = &settings.hotkey_win_down; break;
                    case 17: setting_ptr = &settings.hotkey_win_up; break;
                    case 18: setting_ptr = &settings.hotkey_win_left; break;
                    case 19: setting_ptr = &settings.hotkey_win_right; break;
                    case 20: setting_ptr = &settings.hotkey_move_to_primary; break;
                    case 21: setting_ptr = &settings.hotkey_move_to_secondary; break;
                    default: setting_ptr = nullptr; break;
                }

                if (setting_ptr == nullptr) continue;

                ui::new_ui::StringSetting& setting = *setting_ptr;
                DrawHotkeyEntry(imgui, def, setting, static_cast<int>(i));
            }

            imgui.EndTable();
        }

        imgui.Spacing();
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Format: ctrl+shift+key");
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Empty string = disabled");
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Example: \"ctrl a\", \"alt numpad+\"");
    }

    // Exclusive Keys Section
    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    if (imgui.CollapsingHeader("Exclusive Keys", ImGuiTreeNodeFlags_DefaultOpen)) {
        imgui.TextWrapped(
            "Exclusive key groups ensure that when one key in a group is pressed, other keys in the same group are "
            "automatically released. Useful for strafe macros and preventing conflicting key states.");
        imgui.Spacing();

        // Predefined groups
        imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Predefined Groups:");
        imgui.Spacing();

        CheckboxSetting(settings.exclusive_keys_ad_enabled, "AD Group (A and D keys)", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("When A is pressed, D is released. When D is pressed, A is released.");
        }

        CheckboxSetting(settings.exclusive_keys_ws_enabled, "WS Group (W and S keys)", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("When W is pressed, S is released. When S is pressed, W is released.");
        }

        CheckboxSetting(settings.exclusive_keys_awsd_enabled, "AWSD Group (A, W, S, D keys)", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("When any key (A, W, S, or D) is pressed, all other keys in this group are released.");
        }

        imgui.Spacing();
        imgui.Separator();
        imgui.Spacing();

        // Custom groups
        imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Custom Groups:");
        imgui.Spacing();
        imgui.TextWrapped("Format: Groups separated by |, keys within groups separated by commas");
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Example: \"A,S|Q,E|Left,Right\"");
        imgui.Spacing();

        // Parse and display custom groups as individual entries
        std::string custom_groups = settings.exclusive_keys_custom_groups.GetValue();
        std::vector<std::string> group_list;
        if (!custom_groups.empty()) {
            std::istringstream iss(custom_groups);
            std::string group_str;
            while (std::getline(iss, group_str, '|')) {
                if (!group_str.empty()) {
                    group_list.push_back(group_str);
                }
            }
        }

        // Display existing groups with delete buttons
        for (size_t i = 0; i < group_list.size(); ++i) {
            imgui.PushID(static_cast<int>(i));
            ImVec2 avail = imgui.GetContentRegionAvail();
            imgui.SetNextItemWidth(avail.x - 80);
            char group_buffer[256] = {0};
            strncpy_s(group_buffer, sizeof(group_buffer), group_list[i].c_str(), _TRUNCATE);
            if (imgui.InputText("##GroupEdit", group_buffer, sizeof(group_buffer))) {
                group_list[i] = std::string(group_buffer);
                // Rebuild custom groups string
                std::ostringstream oss;
                for (size_t j = 0; j < group_list.size(); ++j) {
                    if (j > 0) oss << "|";
                    oss << group_list[j];
                }
                settings.exclusive_keys_custom_groups.SetValue(oss.str());
            }
            imgui.SameLine();
            if (imgui.Button("Delete")) {
                group_list.erase(group_list.begin() + i);
                // Rebuild custom groups string
                std::ostringstream oss;
                for (size_t j = 0; j < group_list.size(); ++j) {
                    if (j > 0) oss << "|";
                    oss << group_list[j];
                }
                settings.exclusive_keys_custom_groups.SetValue(oss.str());
                imgui.PopID();
                break;  // Exit loop after deletion
            }
            imgui.PopID();
        }

        // Add new group button
        if (imgui.Button("Add Group")) {
            group_list.push_back("Key1,Key2");
            // Rebuild custom groups string
            std::ostringstream oss;
            for (size_t j = 0; j < group_list.size(); ++j) {
                if (j > 0) oss << "|";
                oss << group_list[j];
            }
            settings.exclusive_keys_custom_groups.SetValue(oss.str());
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("Add a new custom exclusive key group");
        }
    }

    // Debug Information Section
    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    if (imgui.CollapsingHeader("Debug Information", ImGuiTreeNodeFlags_None)) {
        LONGLONG now_ns = utils::get_now_ns();
        LONGLONG last_call_age_ns = now_ns - g_hotkey_debug_info.last_call_time_ns;
        double last_call_age_ms = static_cast<double>(last_call_age_ns) / 1000000.0;

        // Last call time
        if (g_hotkey_debug_info.last_call_time_ns > 0) {
            imgui.Text("Last ProcessHotkeys() call: %.2f ms ago", last_call_age_ms);
            if (last_call_age_ms > 1000.0) {
                imgui.SameLine();
                ui::colors::PushIconColor(&imgui, ui::colors::ICON_WARNING);
                imgui.Text(ICON_FK_WARNING);
                ui::colors::PopIconColor(&imgui);
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltip(
                        "ProcessHotkeys hasn't been called recently - check continuous monitoring thread");
                }
            }
        } else {
            imgui.TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Last ProcessHotkeys() call: Never");
        }

        // Last successful call time
        if (g_hotkey_debug_info.last_successful_call_time_ns > 0) {
            LONGLONG last_success_age_ns = now_ns - g_hotkey_debug_info.last_successful_call_time_ns;
            double last_success_age_ms = static_cast<double>(last_success_age_ns) / 1000000.0;
            imgui.Text("Last successful call: %.2f ms ago", last_success_age_ms);
        } else {
            imgui.TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Last successful call: Never");
        }

        imgui.Spacing();

        // Current state checks
        imgui.Text("Current State:");
        imgui.Indent();

        // Hotkeys enabled
        if (g_hotkey_debug_info.hotkeys_enabled) {
            ui::colors::PushIconColor(&imgui, ui::colors::ICON_SUCCESS);
            imgui.Text(ICON_FK_OK " Hotkeys enabled: Yes");
            ui::colors::PopIconColor(&imgui);
        } else {
            ui::colors::PushIconColor(&imgui, ui::colors::ICON_WARNING);
            imgui.Text(ICON_FK_CANCEL " Hotkeys enabled: No");
            ui::colors::PopIconColor(&imgui);
        }

        // Game HWND
        if (g_hotkey_debug_info.game_hwnd_valid) {
            ui::colors::PushIconColor(&imgui, ui::colors::ICON_SUCCESS);
            imgui.Text(ICON_FK_OK " Game window: Valid (0x%p)", g_hotkey_debug_info.game_hwnd);
            ui::colors::PopIconColor(&imgui);
        } else {
            ui::colors::PushIconColor(&imgui, ui::colors::ICON_WARNING);
            imgui.Text(ICON_FK_CANCEL " Game window: Invalid (null)");
            ui::colors::PopIconColor(&imgui);
        }

        // Game in foreground
        if (g_hotkey_debug_info.game_in_foreground) {
            ui::colors::PushIconColor(&imgui, ui::colors::ICON_SUCCESS);
            imgui.Text(ICON_FK_OK " Game in foreground: Yes");
            ui::colors::PopIconColor(&imgui);
        } else {
            ui::colors::PushIconColor(&imgui, ui::colors::ICON_DISABLED);
            imgui.Text(ICON_FK_MINUS " Game in foreground: No");
            ui::colors::PopIconColor(&imgui);
            if (g_hotkey_debug_info.current_foreground_hwnd != nullptr) {
                imgui.Text("  Foreground window: 0x%p", g_hotkey_debug_info.current_foreground_hwnd);
            }
        }

        // UI open
        if (g_hotkey_debug_info.ui_open) {
            ui::colors::PushIconColor(&imgui, ui::colors::ICON_SUCCESS);
            imgui.Text(ICON_FK_OK " Display Commander UI: Open");
            ui::colors::PopIconColor(&imgui);
        } else {
            ui::colors::PushIconColor(&imgui, ui::colors::ICON_DISABLED);
            imgui.Text(ICON_FK_MINUS " Display Commander UI: Closed");
            ui::colors::PopIconColor(&imgui);
        }

        imgui.Unindent();

        // Block reason
        if (!g_hotkey_debug_info.last_block_reason.empty()) {
            imgui.Spacing();
            imgui.TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Last block reason:");
            imgui.SameLine();
            imgui.Text("%s", g_hotkey_debug_info.last_block_reason.c_str());
        } else if (g_hotkey_debug_info.last_call_time_ns > 0) {
            imgui.Spacing();
            ui::colors::PushIconColor(&imgui, ui::colors::ICON_SUCCESS);
            imgui.Text(ICON_FK_OK " No blocking conditions - hotkeys should work");
            ui::colors::PopIconColor(&imgui);
        }
    }

    // Brightness hotkey step (at bottom of tab)
    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    SliderIntSetting(settings.brightness_hotkey_step_percent, "Brightness hotkey step (%)", "%d%%", imgui);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Step size for Brightness Up/Down hotkeys (0-200%%, 100%% = neutral). Default 5%%.");
    }
}

// Parse a key name string to virtual key code
int ParseKeyNameToVKey(const std::string& key_name) {
    std::string lower = key_name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Trim whitespace
    lower.erase(0, lower.find_first_not_of(" \t"));
    lower.erase(lower.find_last_not_of(" \t") + 1);

    if (lower.length() == 1 && lower[0] >= 'a' && lower[0] <= 'z') {
        return std::toupper(static_cast<unsigned char>(lower[0]));
    } else if (lower == "left") {
        return VK_LEFT;
    } else if (lower == "right") {
        return VK_RIGHT;
    } else if (lower == "up") {
        return VK_UP;
    } else if (lower == "down") {
        return VK_DOWN;
    }

    return 0;  // Invalid key
}

// Process exclusive key groups - automatically release other keys in a group when one is pressed
void ProcessExclusiveKeyGroups() {
    // Check if hotkeys are enabled globally (exclusive keys are part of hotkeys system)
    bool hotkeys_enabled = s_enable_hotkeys.load();
    if (!hotkeys_enabled) {
        return;
    }

    // Check if game is in foreground OR UI is open (same conditions as ProcessHotkeys)
    HWND game_hwnd = g_last_swapchain_hwnd.load();
    HWND foreground_hwnd = display_commanderhooks::GetForegroundWindow_Direct();
    bool is_game_in_foreground = (game_hwnd != nullptr && foreground_hwnd == game_hwnd);
    bool is_ui_open = settings::g_mainTabSettings.show_display_commander_ui.GetValue();

    if (!is_game_in_foreground && !is_ui_open) {
        return;
    }

    auto& settings = settings::g_hotkeysTabSettings;

    // Build list of active exclusive groups
    std::vector<std::vector<int>> active_groups;

    // Predefined AD group
    if (settings.exclusive_keys_ad_enabled.GetValue()) {
        active_groups.push_back({'A', 'D'});
    }

    // Predefined WS group
    if (settings.exclusive_keys_ws_enabled.GetValue()) {
        active_groups.push_back({'W', 'S'});
    }

    // Predefined AWSD group
    if (settings.exclusive_keys_awsd_enabled.GetValue()) {
        active_groups.push_back({'A', 'W', 'S', 'D'});
    }

    // Parse custom groups
    std::string custom_groups_str = settings.exclusive_keys_custom_groups.GetValue();
    if (!custom_groups_str.empty()) {
        // Split by | to get individual groups
        std::istringstream iss(custom_groups_str);
        std::string group_str;
        while (std::getline(iss, group_str, '|')) {
            // Split by comma to get keys in this group
            std::istringstream group_iss(group_str);
            std::string key_str;
            std::vector<int> group_keys;
            while (std::getline(group_iss, key_str, ',')) {
                int vkey = ParseKeyNameToVKey(key_str);
                if (vkey > 0) {
                    group_keys.push_back(vkey);
                }
            }
            if (group_keys.size() >= 2) {  // At least 2 keys needed for exclusivity
                active_groups.push_back(group_keys);
            }
        }
    }

    // Process each active group
    for (const auto& group : active_groups) {
        // Check if any key in this group was just pressed
        for (int pressed_key : group) {
            if (display_commanderhooks::keyboard_tracker::IsKeyPressed(pressed_key)) {
                // This key was just pressed - release all other keys in the group
                INPUT inputs[256];
                int input_count = 0;

                for (int other_key : group) {
                    if (other_key != pressed_key) {
                        // Check if the other key is currently down
                        if (display_commanderhooks::keyboard_tracker::IsKeyDown(other_key)) {
                            // Send key up event for this key
                            inputs[input_count].type = INPUT_KEYBOARD;
                            inputs[input_count].ki.wVk = static_cast<WORD>(other_key);
                            inputs[input_count].ki.wScan = 0;
                            inputs[input_count].ki.dwFlags = KEYEVENTF_KEYUP;
                            inputs[input_count].ki.time = 0;
                            inputs[input_count].ki.dwExtraInfo = 0;
                            input_count++;
                        }
                    }
                }

                // Send all key up events at once
                if (input_count > 0) {
                    SendInput(static_cast<UINT>(input_count), inputs, sizeof(INPUT));
                    LogDebug("Exclusive keys: Released %d keys when %c was pressed", input_count, pressed_key);
                }

                // Mark the pressed key as active in exclusive groups
                // This records the timestamp and updates the active key to be the most recently pressed one
                display_commanderhooks::exclusive_key_groups::MarkKeyDown(pressed_key);

                // Only process one key press per group per frame
                break;
            }
        }

        // Check for keys that were released (not simulated releases, but actual releases)
        // MarkKeyUp will be called from the hooks when keys are actually released,
        // which will automatically update the active key to be the most recently pressed one
        // that's still actually pressed
    }
}

bool IsCapturingHotkey() {
    return s_capturing_hotkey_index >= 0;
}

// Process all hotkeys (call from continuous monitoring loop, same thread as keyboard_tracker::Update)
void ProcessHotkeys() {
    if (IsCapturingHotkey()) {
        // Prime all keys 0-255 so keyboard_tracker::Update() will track them next run
        for (int vk = 0; vk < 256; ++vk) {
            display_commanderhooks::keyboard_tracker::IsKeyDown(vk);
        }
        bool ctrl = display_commanderhooks::keyboard_tracker::IsKeyDown(VK_CONTROL);
        bool shift = display_commanderhooks::keyboard_tracker::IsKeyDown(VK_SHIFT);
        bool alt = display_commanderhooks::keyboard_tracker::IsKeyDown(VK_MENU);
        bool win = display_commanderhooks::keyboard_tracker::IsKeyDown(VK_LWIN)
                   || display_commanderhooks::keyboard_tracker::IsKeyDown(VK_RWIN);
        if (display_commanderhooks::keyboard_tracker::IsKeyPressed(VK_ESCAPE)) {
            s_capturing_hotkey_index = -1;
            return;
        }
        for (int vk = 0; vk < 256; ++vk) {
            if (IsModifierVKey(vk)) continue;
            if (display_commanderhooks::keyboard_tracker::IsKeyPressed(vk)) {
                s_captured_parsed.key_code = vk;
                s_captured_parsed.ctrl = ctrl;
                s_captured_parsed.shift = shift;
                s_captured_parsed.alt = alt;
                s_captured_parsed.win = win;
                s_captured_for_index = s_capturing_hotkey_index;
                s_capture_pending = true;
                s_capturing_hotkey_index = -1;
                return;
            }
        }
        return;
    }

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
    display_commanderhooks::keyboard_tracker::IsKeyDown(VK_LWIN);
    display_commanderhooks::keyboard_tracker::IsKeyDown(VK_RWIN);
    display_commanderhooks::keyboard_tracker::IsKeyDown(VK_DOWN);
    display_commanderhooks::keyboard_tracker::IsKeyDown(VK_UP);
    display_commanderhooks::keyboard_tracker::IsKeyDown(VK_LEFT);
    display_commanderhooks::keyboard_tracker::IsKeyDown(VK_RIGHT);

    // Handle keyboard shortcuts (when game is in foreground OR when Display Commander UI is open)
    // When the UI is open, it becomes the foreground window, but we still want hotkeys to work
    HWND game_hwnd = g_last_swapchain_hwnd.load();
    HWND foreground_hwnd = display_commanderhooks::GetForegroundWindow_Direct();
    bool is_game_in_foreground = (game_hwnd != nullptr && foreground_hwnd == game_hwnd);
    bool is_ui_open = settings::g_mainTabSettings.show_display_commander_ui.GetValue();

    // Update debug info
    g_hotkey_debug_info.game_hwnd = game_hwnd;
    g_hotkey_debug_info.game_hwnd_valid = (game_hwnd != nullptr);
    g_hotkey_debug_info.current_foreground_hwnd = foreground_hwnd;
    g_hotkey_debug_info.game_in_foreground = is_game_in_foreground;
    g_hotkey_debug_info.ui_open = is_ui_open;

    static auto last_foreground_time_ns = utils::get_now_ns();
    if (is_game_in_foreground) {
        last_foreground_time_ns = now_ns;
    }

    // Successfully passed all checks - update successful call time
    g_hotkey_debug_info.last_successful_call_time_ns = now_ns;
    g_hotkey_debug_info.last_block_reason = "";

    // Win+Down / Win+Up / Win+Left / Win+Right are handled by configurable hotkeys (see definitions with id win_down,
    // win_up, win_left, win_right) and processed in the generic loop below.

    // Process exclusive key groups first (before hotkeys)
    ProcessExclusiveKeyGroups();

    // Update exclusive key groups - simulate key presses for keys that became active
    display_commanderhooks::exclusive_key_groups::Update();
    // Allow hotkeys if game is in foreground OR if UI is open (UI is an overlay, so hotkeys should work)
    if (!is_game_in_foreground) {
        // Win+Up (restore) grace: allow for a short time after leaving foreground, or forever if setting is 61.
        // Use the configured Win+Up hotkey (may be remapped in Hotkeys tab).
        int grace_sec = settings::g_advancedTabSettings.win_up_grace_seconds.GetValue();
        bool grace_ok =
            (grace_sec >= 61)
            || (grace_sec > 0 && last_foreground_time_ns != 0
                && (now_ns - last_foreground_time_ns <= static_cast<LONGLONG>(grace_sec) * utils::SEC_TO_NS));
        for (const auto& def : g_hotkey_definitions) {
            if (def.id == "win_up" && def.parsed.IsValid() && HotkeyMatchesCurrentState(def.parsed) && grace_ok
                && game_hwnd != nullptr && !display_commanderhooks::WindowHasBorder(game_hwnd)) {
                ShowWindow_Direct(game_hwnd, SW_RESTORE);
                break;
            }
        }
        if (game_hwnd == nullptr) {
            g_hotkey_debug_info.last_block_reason = "No game window detected (swapchain not initialized)";
        } else {
            g_hotkey_debug_info.last_block_reason = "Game not in foreground and UI not open";
        }
        return;
    }

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
        bool win_down = display_commanderhooks::keyboard_tracker::IsKeyDown(VK_LWIN)
                        || display_commanderhooks::keyboard_tracker::IsKeyDown(VK_RWIN);

        // Check if modifiers match exactly
        if (hotkey.ctrl != ctrl_down) continue;
        if (hotkey.shift != shift_down) continue;
        if (hotkey.alt != alt_down) continue;
        if (hotkey.win != win_down) continue;

        // All conditions met - execute the action
        if (def.action) {
            def.action();
        }
    }
}

}  // namespace ui::new_ui
