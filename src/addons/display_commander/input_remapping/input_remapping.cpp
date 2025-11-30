/*
 * Copyright (C) 2024 Display Commander
 * Gamepad to keyboard input remapping system implementation
 */

#include "input_remapping.hpp"
#include "../globals.hpp"
#include "../utils/logging.hpp"
#include "utils/srwlock_wrapper.hpp"
#include "../hooks/timeslowdown_hooks.hpp"
#include "../config/display_commander_config.hpp"
#include "../widgets/xinput_widget/xinput_widget.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../settings/experimental_tab_settings.hpp"
#include "../audio/audio_management.hpp"
#include <reshade.hpp>

namespace display_commander::input_remapping {

// Helper function to get original GetTickCount64 value (unhooked)
static ULONGLONG GetOriginalTickCount64() {
    if (enabled_experimental_features && display_commanderhooks::GetTickCount64_Original) {
        return display_commanderhooks::GetTickCount64_Original();
    } else {
        return GetTickCount64();
    }
}

InputRemapper &InputRemapper::get_instance() {
    static InputRemapper instance;
    return instance;
}

InputRemapper::InputRemapper() {
    // SRWLOCK is statically initialized, no explicit initialization needed

    // Initialize button state tracking
    for (int i = 0; i < XUSER_MAX_COUNT; ++i) {
        _previous_button_states[i].store(0);
        _current_button_states[i].store(0);
        _guide_solo_candidate[i].store(false);
        _guide_other_button_pressed[i].store(false);
    }
}

InputRemapper::~InputRemapper() {
    cleanup();
    // SRWLOCK doesn't need explicit cleanup
}

bool InputRemapper::initialize() {
    if (_initialized.load())
        return true;

    LogInfo("InputRemapper::initialize() - Starting input remapping initialization");

    // Load settings
    load_settings();

    // Add default chords if enabled
    if (settings::g_mainTabSettings.enable_default_chords.GetValue()) {
        add_default_chords();
    }

    _initialized.store(true);
    LogInfo("InputRemapper::initialize() - Input remapping initialization complete");

    return true;
}

void InputRemapper::cleanup() {
    if (!_initialized.load())
        return;

    // Save settings
    save_settings();

    // Clear all remappings
    clear_all_remaps();

    _initialized.store(false);
    LogInfo("InputRemapper::cleanup() - Input remapping cleanup complete");
}

void InputRemapper::process_gamepad_input(DWORD user_index, XINPUT_STATE *state) {
    if (!_remapping_enabled.load() || state == nullptr || user_index >= XUSER_MAX_COUNT) {
        return;
    }

    // Update button states
    update_button_states(user_index, state->Gamepad.wButtons);

    // Process button changes
    WORD previous = _previous_button_states[user_index].load();
    WORD current = _current_button_states[user_index].load();
    WORD changed = previous ^ current;

    // Check each button for changes
    for (int i = 0; i < 16; ++i) {
        WORD button_mask = 1 << i;
        if ((changed & button_mask) != 0) {
            if ((current & button_mask) != 0) {
                handle_button_press(button_mask, user_index, current);
            } else {
                handle_button_release(button_mask, user_index, current);
            }
        }
    }

    // Apply gamepad-to-gamepad remapping (modifies state)
    apply_gamepad_remapping(user_index, state);
}

void InputRemapper::add_default_chord_type(DefaultChordType chord_type) {
    utils::SRWLockExclusive lock(_srwlock);

    WORD button = 0;
    std::string action_name;
    const char* log_name = "";
    bool hold_mode = false;
    bool chord_mode = true;

    // Map chord type to button and action
    switch (chord_type) {
    case DefaultChordType::VolumeUp:
        button = XINPUT_GAMEPAD_DPAD_UP;
        action_name = "increase volume";
        log_name = "Home + D-Pad Up = Increase Volume";
        break;
    case DefaultChordType::VolumeDown:
        button = XINPUT_GAMEPAD_DPAD_DOWN;
        action_name = "decrease volume";
        log_name = "Home + D-Pad Down = Decrease Volume";
        break;
    case DefaultChordType::MuteUnmute:
        button = XINPUT_GAMEPAD_RIGHT_SHOULDER;
        action_name = "mute/unmute audio";
        log_name = "Home + Right Shoulder = Mute/Unmute Audio";
        break;
    case DefaultChordType::PerformanceOverlay:
        button = XINPUT_GAMEPAD_START;
        action_name = "performance overlay toggle";
        log_name = "Home + Menu = Toggle Performance Overlay";
        break;
    case DefaultChordType::Screenshot:
        button = XINPUT_GAMEPAD_BACK;
        action_name = "screenshot";
        log_name = "Home + View = Take Screenshot";
        break;
    case DefaultChordType::IncreaseGameSpeed:
        button = XINPUT_GAMEPAD_DPAD_RIGHT;
        action_name = "increase game speed";
        log_name = "Home + D-Pad Right = Increase Game Speed (10%)";
        break;
    case DefaultChordType::DecreaseGameSpeed:
        button = XINPUT_GAMEPAD_DPAD_LEFT;
        action_name = "decrease game speed";
        log_name = "Home + D-Pad Left = Decrease Game Speed (10%)";
        break;
    case DefaultChordType::DisplayCommanderUI:
        // Guide-alone toggle for Display Commander UI
        button = XINPUT_GAMEPAD_GUIDE;
        action_name = "display commander ui toggle";
        log_name = "Home = Toggle Display Commander UI";
        // For this action we want release-based handling with optional solo requirement
        hold_mode = true;    // ensure release handler runs
        chord_mode = false;  // no Guide chord for Guide itself
        break;
    case DefaultChordType::SystemVolumeUp:
        button = XINPUT_GAMEPAD_RIGHT_THUMB;
        action_name = "increase system volume";
        log_name = "Home + Right Thumbstick = Increase System Volume";
        break;
    case DefaultChordType::SystemVolumeDown:
        button = XINPUT_GAMEPAD_LEFT_THUMB;
        action_name = "decrease system volume";
        log_name = "Home + Left Thumbstick = Decrease System Volume";
        break;
    default:
        return;
    }

    // Check if remapping already exists for this button (don't overwrite user settings)
    auto it = _button_to_remap_index.find(button);
    if (it == _button_to_remap_index.end()) {
        // Add new default chord / action
        ButtonRemap remap(button, action_name, true, hold_mode, chord_mode);
        remap.is_default_chord = true;
        _remappings.push_back(remap);
        _button_to_remap_index[button] = _remappings.size() - 1;
        save_settings();
        LogInfo("InputRemapper::add_default_chord_type() - Added default chord: %s", log_name);
    } else {
        // Check if existing remap is a default chord
        size_t idx = it->second;
        if (idx < _remappings.size() && _remappings[idx].is_default_chord) {
            // Re-enable if it was previously a default chord but disabled
            _remappings[idx].enabled = true;
            save_settings();
            LogInfo("InputRemapper::add_default_chord_type() - Re-enabled default chord: %s", log_name);
        }
    }
}

void InputRemapper::remove_default_chord_type(DefaultChordType chord_type) {
    utils::SRWLockExclusive lock(_srwlock);

    WORD target_button = 0;

    // Map chord type to button
    switch (chord_type) {
    case DefaultChordType::VolumeUp:
        target_button = XINPUT_GAMEPAD_DPAD_UP;
        break;
    case DefaultChordType::VolumeDown:
        target_button = XINPUT_GAMEPAD_DPAD_DOWN;
        break;
    case DefaultChordType::MuteUnmute:
        target_button = XINPUT_GAMEPAD_RIGHT_SHOULDER;
        break;
    case DefaultChordType::PerformanceOverlay:
        target_button = XINPUT_GAMEPAD_START;
        break;
    case DefaultChordType::Screenshot:
        target_button = XINPUT_GAMEPAD_BACK;
        break;
    case DefaultChordType::IncreaseGameSpeed:
        target_button = XINPUT_GAMEPAD_DPAD_RIGHT;
        break;
    case DefaultChordType::DecreaseGameSpeed:
        target_button = XINPUT_GAMEPAD_DPAD_LEFT;
        break;
    case DefaultChordType::DisplayCommanderUI:
        target_button = XINPUT_GAMEPAD_LEFT_SHOULDER;
        break;
    case DefaultChordType::SystemVolumeUp:
        target_button = XINPUT_GAMEPAD_RIGHT_THUMB;
        break;
    case DefaultChordType::SystemVolumeDown:
        target_button = XINPUT_GAMEPAD_LEFT_THUMB;
        break;
    default:
        return;
    }

    // Find and remove the default chord for this button
    auto it = _button_to_remap_index.find(target_button);
    if (it != _button_to_remap_index.end()) {
        size_t idx = it->second;
        if (idx < _remappings.size() && _remappings[idx].is_default_chord) {
            WORD button = _remappings[idx].gamepad_button;

            // Remove from button index map
            _button_to_remap_index.erase(button);

            // Remove from remappings vector
            _remappings.erase(_remappings.begin() + idx);

            // Update indices in button_to_remap_index for all remappings after the removed one
            for (auto &pair : _button_to_remap_index) {
                if (pair.second > idx) {
                    pair.second--;
                }
            }

            save_settings();
            LogInfo("InputRemapper::remove_default_chord_type() - Removed default chord for button 0x%04X", button);
        }
    }
}

void InputRemapper::add_default_chords() {
    // Add all default chord types
    add_default_chord_type(DefaultChordType::VolumeUp);
    add_default_chord_type(DefaultChordType::VolumeDown);
    add_default_chord_type(DefaultChordType::MuteUnmute);
    add_default_chord_type(DefaultChordType::PerformanceOverlay);
    add_default_chord_type(DefaultChordType::Screenshot);
    add_default_chord_type(DefaultChordType::DisplayCommanderUI);
    add_default_chord_type(DefaultChordType::SystemVolumeUp);
    add_default_chord_type(DefaultChordType::SystemVolumeDown);

    // Add experimental game speed chords only if experimental features are enabled
    if (enabled_experimental_features) {
        add_default_chord_type(DefaultChordType::IncreaseGameSpeed);
        add_default_chord_type(DefaultChordType::DecreaseGameSpeed);
    }
}

void InputRemapper::remove_default_chords() {
    // Remove all default chord types
    remove_default_chord_type(DefaultChordType::VolumeUp);
    remove_default_chord_type(DefaultChordType::VolumeDown);
    remove_default_chord_type(DefaultChordType::MuteUnmute);
    remove_default_chord_type(DefaultChordType::PerformanceOverlay);
    remove_default_chord_type(DefaultChordType::Screenshot);
    remove_default_chord_type(DefaultChordType::DisplayCommanderUI);
    remove_default_chord_type(DefaultChordType::SystemVolumeUp);
    remove_default_chord_type(DefaultChordType::SystemVolumeDown);

    // Remove experimental game speed chords (only if they exist)
    remove_default_chord_type(DefaultChordType::IncreaseGameSpeed);
    remove_default_chord_type(DefaultChordType::DecreaseGameSpeed);
}

void InputRemapper::add_button_remap(const ButtonRemap &remap) {
    utils::SRWLockExclusive lock(_srwlock);

    // Check if remap already exists for this button
    auto it = _button_to_remap_index.find(remap.gamepad_button);
    if (it != _button_to_remap_index.end()) {
        // Update existing remap
        _remappings[it->second] = remap;
    } else {
        // Add new remap
        _remappings.push_back(remap);
        _button_to_remap_index[remap.gamepad_button] = _remappings.size() - 1;
    }

    // Auto-save settings when remappings change
    save_settings();

    LogInfo("InputRemapper::add_button_remap() - Added remap for button 0x%04X to key %s", remap.gamepad_button,
            remap.keyboard_name.c_str());
}

void InputRemapper::remove_button_remap(WORD gamepad_button) {
    utils::SRWLockExclusive lock(_srwlock);

    auto it = _button_to_remap_index.find(gamepad_button);
    if (it != _button_to_remap_index.end()) {
        size_t index = it->second;
        _remappings.erase(_remappings.begin() + index);
        _button_to_remap_index.erase(it);

        // Update indices for remaining remaps
        for (auto &pair : _button_to_remap_index) {
            if (pair.second > index) {
                pair.second--;
            }
        }
    }

    // Auto-save settings when remappings change
    save_settings();

    LogInfo("InputRemapper::remove_button_remap() - Removed remap for button 0x%04X", gamepad_button);
}

void InputRemapper::clear_all_remaps() {
    utils::SRWLockExclusive lock(_srwlock);
    _remappings.clear();
    _button_to_remap_index.clear();

    // Auto-save settings when remappings change
    save_settings();

    LogInfo("InputRemapper::clear_all_remaps() - Cleared all remappings");
}

void InputRemapper::set_remapping_enabled(bool enabled) {
    _remapping_enabled.store(enabled);

    // Save the setting to config immediately
    display_commander::config::set_config_value("DisplayCommander.InputRemapping", "Enabled", enabled);
}

void InputRemapper::set_block_input_on_home_button(bool enabled) {
    _block_input_on_home_button.store(enabled);

    // Save the setting to config immediately
    display_commander::config::set_config_value("DisplayCommander.InputRemapping", "BlockInputOnHomeButton", enabled);

    LogInfo("InputRemapper::set_block_input_on_home_button() - Block input on home button %s", enabled ? "enabled" : "disabled");
}

void InputRemapper::set_default_input_method(KeyboardInputMethod method) {
    _default_input_method = method;
    LogInfo("InputRemapper::set_default_input_method() - Set to %s", get_keyboard_input_method_name(method).c_str());
}

const ButtonRemap *InputRemapper::get_button_remap(WORD gamepad_button) const {
    utils::SRWLockShared lock(_srwlock);
    auto it = _button_to_remap_index.find(gamepad_button);
    return (it != _button_to_remap_index.end()) ? &_remappings[it->second] : nullptr;
}

void InputRemapper::update_remap(WORD gamepad_button, int keyboard_vk, const std::string &keyboard_name,
                                 KeyboardInputMethod method, bool hold_mode, bool chord_mode) {
    ButtonRemap remap(gamepad_button, keyboard_vk, keyboard_name, true, method, hold_mode, chord_mode);
    add_button_remap(remap);
}

void InputRemapper::update_remap_keyboard(WORD gamepad_button, int keyboard_vk, const std::string &keyboard_name,
                                         KeyboardInputMethod method, bool hold_mode, bool chord_mode) {
    ButtonRemap remap(gamepad_button, keyboard_vk, keyboard_name, true, method, hold_mode, chord_mode);
    add_button_remap(remap);
}

void InputRemapper::update_remap_gamepad(WORD gamepad_button, WORD target_button, bool hold_mode, bool chord_mode) {
    ButtonRemap remap(gamepad_button, target_button, true, hold_mode, chord_mode);
    add_button_remap(remap);
}

void InputRemapper::update_remap_action(WORD gamepad_button, const std::string &action_name, bool hold_mode, bool chord_mode) {
    ButtonRemap remap(gamepad_button, action_name, true, hold_mode, chord_mode);
    add_button_remap(remap);
}

void InputRemapper::load_settings() {
    // Load remapping enabled state
    bool remapping_enabled = _remapping_enabled.load(); // Get current default value
    display_commander::config::get_config_value("DisplayCommander.InputRemapping", "Enabled", remapping_enabled);
    _remapping_enabled.store(remapping_enabled);

    // Load block input on home button setting
    bool block_input_on_home_button = _block_input_on_home_button.load(); // Get current default value
    display_commander::config::get_config_value("DisplayCommander.InputRemapping", "BlockInputOnHomeButton", block_input_on_home_button);
    _block_input_on_home_button.store(block_input_on_home_button);

    // Load default input method
    int default_method = static_cast<int>(_default_input_method); // Get current default value
    display_commander::config::get_config_value("DisplayCommander.InputRemapping", "DefaultMethod", default_method);
    _default_input_method = static_cast<KeyboardInputMethod>(default_method);

    // Load remappings count
    int remapping_count;
    if (display_commander::config::get_config_value("DisplayCommander.InputRemapping", "Count", remapping_count)) {
        // Load each remapping
        for (int i = 0; i < remapping_count; ++i) {
            std::string key_prefix = "Remapping" + std::to_string(i) + ".";

            int gamepad_button, remap_type_int = 0;
            bool enabled, hold_mode, chord_mode = false;

            // Load common fields
            if (!display_commander::config::get_config_value("DisplayCommander.InputRemapping",
                                          (key_prefix + "GamepadButton").c_str(), gamepad_button) ||
                !display_commander::config::get_config_value("DisplayCommander.InputRemapping",
                                          (key_prefix + "RemapType").c_str(), remap_type_int) ||
                !display_commander::config::get_config_value("DisplayCommander.InputRemapping", (key_prefix + "Enabled").c_str(),
                                          enabled) ||
                !display_commander::config::get_config_value("DisplayCommander.InputRemapping", (key_prefix + "HoldMode").c_str(),
                                          hold_mode)) {
                continue;
            }

            // Load chord_mode (optional, defaults to false for backward compatibility)
            display_commander::config::get_config_value("DisplayCommander.InputRemapping", (key_prefix + "ChordMode").c_str(),
                                      chord_mode);

            // Load is_default_chord (optional, defaults to false for backward compatibility)
            bool is_default_chord = false;
            display_commander::config::get_config_value("DisplayCommander.InputRemapping", (key_prefix + "IsDefaultChord").c_str(),
                                      is_default_chord);

            RemapType remap_type = static_cast<RemapType>(remap_type_int);
            ButtonRemap remap;
            remap.gamepad_button = static_cast<WORD>(gamepad_button);
            remap.remap_type = remap_type;
            remap.enabled = enabled;
            remap.hold_mode = hold_mode;
            remap.chord_mode = chord_mode;
            remap.is_default_chord = is_default_chord;

            // Load type-specific fields
            if (remap_type == RemapType::Keyboard) {
                int keyboard_vk, input_method;
                char keyboard_name[256] = {0};
                size_t keyboard_name_size = sizeof(keyboard_name);

                if (display_commander::config::get_config_value("DisplayCommander.InputRemapping",
                                              (key_prefix + "KeyboardVk").c_str(), keyboard_vk) &&
                    display_commander::config::get_config_value("DisplayCommander.InputRemapping",
                                              (key_prefix + "InputMethod").c_str(), input_method) &&
                    display_commander::config::get_config_value("DisplayCommander.InputRemapping",
                                              (key_prefix + "KeyboardName").c_str(), keyboard_name, &keyboard_name_size)) {
                    remap.keyboard_vk = keyboard_vk;
                    remap.keyboard_name = keyboard_name;
                    remap.input_method = static_cast<KeyboardInputMethod>(input_method);
                    add_button_remap(remap);
                }
            } else if (remap_type == RemapType::Gamepad) {
                int gamepad_target_button;
                if (display_commander::config::get_config_value("DisplayCommander.InputRemapping",
                                              (key_prefix + "GamepadTargetButton").c_str(), gamepad_target_button)) {
                    remap.gamepad_target_button = static_cast<WORD>(gamepad_target_button);
                    add_button_remap(remap);
                }
            } else if (remap_type == RemapType::Action) {
                char action_name[256] = {0};
                size_t action_name_size = sizeof(action_name);
                if (display_commander::config::get_config_value("DisplayCommander.InputRemapping",
                                              (key_prefix + "ActionName").c_str(), action_name, &action_name_size)) {
                    remap.action_name = action_name;
                    add_button_remap(remap);
                }
            }
        }
    } else {
        // Load default remappings if no saved settings
        add_button_remap(ButtonRemap(XINPUT_GAMEPAD_A, VK_SPACE, "Space", true, KeyboardInputMethod::SendInput, true));
        add_button_remap(
            ButtonRemap(XINPUT_GAMEPAD_B, VK_ESCAPE, "Escape", true, KeyboardInputMethod::SendInput, false));
        add_button_remap(ButtonRemap(XINPUT_GAMEPAD_X, VK_F1, "F1", true, KeyboardInputMethod::SendInput, false));
        add_button_remap(ButtonRemap(XINPUT_GAMEPAD_Y, VK_F2, "F2", true, KeyboardInputMethod::SendInput, false));
    }

    LogInfo("InputRemapper::load_settings() - Loaded %zu remappings", _remappings.size());
}

void InputRemapper::save_settings() {
    // Save remapping enabled state
    display_commander::config::set_config_value("DisplayCommander.InputRemapping", "Enabled", _remapping_enabled.load());

    // Save block input on home button setting
    display_commander::config::set_config_value("DisplayCommander.InputRemapping", "BlockInputOnHomeButton", _block_input_on_home_button.load());

    // Save default input method
    display_commander::config::set_config_value("DisplayCommander.InputRemapping", "DefaultMethod",
                              static_cast<int>(_default_input_method));

    // Save remappings count
    display_commander::config::set_config_value("DisplayCommander.InputRemapping", "Count",
                              static_cast<int>(_remappings.size()));

    // Save each remapping
    for (size_t i = 0; i < _remappings.size(); ++i) {
        const auto &remap = _remappings[i];
        std::string key_prefix = "Remapping" + std::to_string(i) + ".";

        // Save common fields
        display_commander::config::set_config_value("DisplayCommander.InputRemapping", (key_prefix + "GamepadButton").c_str(),
                                  static_cast<int>(remap.gamepad_button));
        display_commander::config::set_config_value("DisplayCommander.InputRemapping", (key_prefix + "RemapType").c_str(),
                                  static_cast<int>(remap.remap_type));
        display_commander::config::set_config_value("DisplayCommander.InputRemapping", (key_prefix + "Enabled").c_str(),
                                  remap.enabled);
        display_commander::config::set_config_value("DisplayCommander.InputRemapping", (key_prefix + "HoldMode").c_str(),
                                  remap.hold_mode);
        display_commander::config::set_config_value("DisplayCommander.InputRemapping", (key_prefix + "ChordMode").c_str(),
                                  remap.chord_mode);
        display_commander::config::set_config_value("DisplayCommander.InputRemapping", (key_prefix + "IsDefaultChord").c_str(),
                                  remap.is_default_chord);

        // Save type-specific fields
        if (remap.remap_type == RemapType::Keyboard) {
            display_commander::config::set_config_value("DisplayCommander.InputRemapping", (key_prefix + "KeyboardVk").c_str(),
                                      remap.keyboard_vk);
            display_commander::config::set_config_value("DisplayCommander.InputRemapping", (key_prefix + "InputMethod").c_str(),
                                      static_cast<int>(remap.input_method));
            display_commander::config::set_config_value("DisplayCommander.InputRemapping", (key_prefix + "KeyboardName").c_str(),
                                      remap.keyboard_name.c_str());
        } else if (remap.remap_type == RemapType::Gamepad) {
            display_commander::config::set_config_value("DisplayCommander.InputRemapping", (key_prefix + "GamepadTargetButton").c_str(),
                                      static_cast<int>(remap.gamepad_target_button));
        } else if (remap.remap_type == RemapType::Action) {
            display_commander::config::set_config_value("DisplayCommander.InputRemapping", (key_prefix + "ActionName").c_str(),
                                      remap.action_name.c_str());
        }
    }

    LogInfo("InputRemapper::save_settings() - Saved %zu remappings", _remappings.size());
}

bool InputRemapper::send_keyboard_input_sendinput(int vk_code, bool key_down) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk_code;
    input.ki.dwFlags = key_down ? 0 : KEYEVENTF_KEYUP;
    input.ki.time = 0;
    input.ki.dwExtraInfo = GetMessageExtraInfo();

    UINT result = SendInput(1, &input, sizeof(INPUT));
    return result == 1;
}

bool InputRemapper::send_keyboard_input_keybdevent(int vk_code, bool key_down) {
    BYTE scan_code = MapVirtualKey(vk_code, MAPVK_VK_TO_VSC);
    keybd_event(vk_code, scan_code, key_down ? 0 : KEYEVENTF_KEYUP, 0);
    return true;
}

bool InputRemapper::send_keyboard_input_sendmessage(int vk_code, bool key_down) {
    HWND hwnd = get_active_window();
    if (hwnd == nullptr) {
        return false;
    }

    UINT message = key_down ? WM_KEYDOWN : WM_KEYUP;
    WPARAM wParam = vk_code;
    LPARAM lParam = 0; // Could be enhanced with scan code and repeat count

    LRESULT result = SendMessage(hwnd, message, wParam, lParam);
    return result != 0;
}

bool InputRemapper::send_keyboard_input_postmessage(int vk_code, bool key_down) {
    HWND hwnd = get_active_window();
    if (hwnd == nullptr) {
        return false;
    }

    UINT message = key_down ? WM_KEYDOWN : WM_KEYUP;
    WPARAM wParam = vk_code;
    LPARAM lParam = 0; // Could be enhanced with scan code and repeat count

    BOOL result = PostMessage(hwnd, message, wParam, lParam);
    return result != FALSE;
}

std::string InputRemapper::get_button_name(WORD button) const {
    switch (button) {
    case XINPUT_GAMEPAD_DPAD_UP:
        return "D-Pad Up";
    case XINPUT_GAMEPAD_DPAD_DOWN:
        return "D-Pad Down";
    case XINPUT_GAMEPAD_DPAD_LEFT:
        return "D-Pad Left";
    case XINPUT_GAMEPAD_DPAD_RIGHT:
        return "D-Pad Right";
    case XINPUT_GAMEPAD_START:
        return "Menu";
    case XINPUT_GAMEPAD_BACK:
        return "View";
    case XINPUT_GAMEPAD_LEFT_THUMB:
        return "Left Stick";
    case XINPUT_GAMEPAD_RIGHT_THUMB:
        return "Right Stick";
    case XINPUT_GAMEPAD_LEFT_SHOULDER:
        return "Left Bumper";
    case XINPUT_GAMEPAD_RIGHT_SHOULDER:
        return "Right Bumper";
    case XINPUT_GAMEPAD_A:
        return "A";
    case XINPUT_GAMEPAD_B:
        return "B";
    case XINPUT_GAMEPAD_X:
        return "X";
    case XINPUT_GAMEPAD_Y:
        return "Y";
    case XINPUT_GAMEPAD_GUIDE:
        return "Home";
    default:
        return "Unknown";
    }
}

std::string InputRemapper::get_keyboard_name(int vk_code) const {
    char key_name[256];
    int result = GetKeyNameTextA(MapVirtualKey(vk_code, MAPVK_VK_TO_VSC) << 16, key_name, sizeof(key_name));
    if (result > 0) {
        return std::string(key_name);
    }
    return "Unknown";
}

int InputRemapper::get_vk_code_from_name(const std::string &name) const {
    // Simple mapping for common keys
    if (name == "Space")
        return VK_SPACE;
    if (name == "Enter")
        return VK_RETURN;
    if (name == "Escape")
        return VK_ESCAPE;
    if (name == "Tab")
        return VK_TAB;
    if (name == "Shift")
        return VK_SHIFT;
    if (name == "Ctrl")
        return VK_CONTROL;
    if (name == "Alt")
        return VK_MENU;
    if (name == "F1")
        return VK_F1;
    if (name == "F2")
        return VK_F2;
    if (name == "F3")
        return VK_F3;
    if (name == "F4")
        return VK_F4;
    if (name == "F5")
        return VK_F5;
    if (name == "F6")
        return VK_F6;
    if (name == "F7")
        return VK_F7;
    if (name == "F8")
        return VK_F8;
    if (name == "F9")
        return VK_F9;
    if (name == "F10")
        return VK_F10;
    if (name == "F11")
        return VK_F11;
    if (name == "F12")
        return VK_F12;
    if (name == "~")
        return VK_OEM_3; // Tilde key
    if (name == "A")
        return 'A';
    if (name == "B")
        return 'B';
    if (name == "C")
        return 'C';
    if (name == "D")
        return 'D';
    if (name == "E")
        return 'E';
    if (name == "F")
        return 'F';
    if (name == "G")
        return 'G';
    if (name == "H")
        return 'H';
    if (name == "I")
        return 'I';
    if (name == "J")
        return 'J';
    if (name == "K")
        return 'K';
    if (name == "L")
        return 'L';
    if (name == "M")
        return 'M';
    if (name == "N")
        return 'N';
    if (name == "O")
        return 'O';
    if (name == "P")
        return 'P';
    if (name == "Q")
        return 'Q';
    if (name == "R")
        return 'R';
    if (name == "S")
        return 'S';
    if (name == "T")
        return 'T';
    if (name == "U")
        return 'U';
    if (name == "V")
        return 'V';
    if (name == "W")
        return 'W';
    if (name == "X")
        return 'X';
    if (name == "Y")
        return 'Y';
    if (name == "Z")
        return 'Z';
    return 0;
}

HWND InputRemapper::get_active_window() const { return GetForegroundWindow(); }

void InputRemapper::update_button_states(DWORD user_index, WORD button_state) {
    if (user_index >= XUSER_MAX_COUNT)
        return;

    _previous_button_states[user_index].store(_current_button_states[user_index].load());
    _current_button_states[user_index].store(button_state);
}

void InputRemapper::handle_button_press(WORD gamepad_button, DWORD user_index, WORD current_button_state) {
    ButtonRemap *remap = const_cast<ButtonRemap *>(get_button_remap(gamepad_button));
    if (!remap || !remap->enabled)
        return;

    // If Home-based Display Commander UI solo toggle is pending, any other button press cancels "solo" state
    if (gamepad_button != XINPUT_GAMEPAD_GUIDE && user_index < XUSER_MAX_COUNT) {
        if (_guide_solo_candidate[user_index].load()) {
            _guide_other_button_pressed[user_index].store(true);
        }
    }

    // Special handling for Home button mapped to Display Commander UI toggle:
    // - We want to trigger the action on Home RELEASE, optionally only if no other buttons were pressed.
    // - So we do NOT execute the action here on press; we just start tracking a potential solo press.
    if (remap->remap_type == RemapType::Action &&
        remap->action_name == "display commander ui toggle" &&
        gamepad_button == XINPUT_GAMEPAD_GUIDE &&
        user_index < XUSER_MAX_COUNT) {
        _guide_solo_candidate[user_index].store(true);

        // If any other button is currently held down, this cannot be a "solo" press
        const WORD guide_mask = XINPUT_GAMEPAD_GUIDE;
        WORD other_down = current_button_state & static_cast<WORD>(~guide_mask);
        _guide_other_button_pressed[user_index].store(other_down != 0);
        return;
    }

    // Check chord mode: if enabled, home button must also be pressed
    if (remap->chord_mode) {
        if ((current_button_state & XINPUT_GAMEPAD_GUIDE) == 0) {
            // Home button not pressed, ignore this remapping
            return;
        }
    }

    remap->is_pressed.store(true);
    remap->last_press_time.store(GetOriginalTickCount64());

    bool success = false;

    // Handle different remap types
    switch (remap->remap_type) {
    case RemapType::Keyboard: {
        // Send keyboard input
        switch (remap->input_method) {
        case KeyboardInputMethod::SendInput:
            success = send_keyboard_input_sendinput(remap->keyboard_vk, true);
            break;
        case KeyboardInputMethod::KeybdEvent:
            success = send_keyboard_input_keybdevent(remap->keyboard_vk, true);
            break;
        case KeyboardInputMethod::SendMessage:
            success = send_keyboard_input_sendmessage(remap->keyboard_vk, true);
            break;
        case KeyboardInputMethod::PostMessage:
            success = send_keyboard_input_postmessage(remap->keyboard_vk, true);
            break;
        case KeyboardInputMethod::Count:
            // Should never happen
            break;
        }

        if (success) {
            LogInfo("InputRemapper::handle_button_press() - Mapped %s to keyboard %s (Controller %lu)",
                    get_button_name(gamepad_button).c_str(), remap->keyboard_name.c_str(), user_index);
        } else {
            LogError("InputRemapper::handle_button_press() - Failed to send keyboard input for %s",
                     remap->keyboard_name.c_str());
        }
        break;
    }
    case RemapType::Gamepad:
        // Gamepad remapping is handled in apply_gamepad_remapping
        // Just mark as success for logging
        success = true;
        LogInfo("InputRemapper::handle_button_press() - Mapped %s to gamepad %s (Controller %lu)",
                get_button_name(gamepad_button).c_str(), get_button_name(remap->gamepad_target_button).c_str(), user_index);
        break;
    case RemapType::Action:
        // Execute action
        execute_action(remap->action_name);
        success = true;
        LogInfo("InputRemapper::handle_button_press() - Mapped %s to action %s (Controller %lu)",
                get_button_name(gamepad_button).c_str(), remap->action_name.c_str(), user_index);
        break;
    case RemapType::Count:
        // Should never happen
        break;
    }

    if (success) {
        // Increment trigger counter
        increment_trigger_count(gamepad_button);
    }
}

void InputRemapper::handle_button_release(WORD gamepad_button, DWORD user_index, WORD current_button_state) {
    ButtonRemap *remap = const_cast<ButtonRemap *>(get_button_remap(gamepad_button));
    if (!remap || !remap->enabled || !remap->hold_mode)
        return;

    // Check chord mode: if enabled, home button must also be pressed (or was pressed when button was released)
    // For release, we check if home button is still pressed or if it was pressed when the button was held
    if (remap->chord_mode) {
        if ((current_button_state & XINPUT_GAMEPAD_GUIDE) == 0) {
            // Home button not pressed, ignore this remapping release
            return;
        }
    }

    remap->is_pressed.store(false);

    bool success = false;

    // Handle different remap types
    switch (remap->remap_type) {
    case RemapType::Keyboard: {
        // Send keyboard release
        switch (remap->input_method) {
        case KeyboardInputMethod::SendInput:
            success = send_keyboard_input_sendinput(remap->keyboard_vk, false);
            break;
        case KeyboardInputMethod::KeybdEvent:
            success = send_keyboard_input_keybdevent(remap->keyboard_vk, false);
            break;
        case KeyboardInputMethod::SendMessage:
            success = send_keyboard_input_sendmessage(remap->keyboard_vk, false);
            break;
        case KeyboardInputMethod::PostMessage:
            success = send_keyboard_input_postmessage(remap->keyboard_vk, false);
            break;
        case KeyboardInputMethod::Count:
            // Should never happen
            break;
        }

        if (success) {
            LogInfo("InputRemapper::handle_button_release() - Released keyboard %s (Controller %lu)",
                    remap->keyboard_name.c_str(), user_index);
        }
        break;
    }
    case RemapType::Gamepad:
        // Gamepad remapping release is handled in apply_gamepad_remapping
        // Just mark as success for logging
        success = true;
        LogInfo("InputRemapper::handle_button_release() - Released gamepad %s (Controller %lu)",
                get_button_name(remap->gamepad_target_button).c_str(), user_index);
        break;
    case RemapType::Action:
        // Special handling for Home-based Display Commander UI toggle:
        // - Trigger on Home RELEASE
        // - Optionally require that no other buttons were pressed while Home was held
        if (remap->action_name == "display commander ui toggle" &&
            gamepad_button == XINPUT_GAMEPAD_GUIDE &&
            user_index < XUSER_MAX_COUNT) {
            bool candidate_active = _guide_solo_candidate[user_index].load();
            bool other_pressed = _guide_other_button_pressed[user_index].load();

            // Clear tracking state
            _guide_solo_candidate[user_index].store(false);
            _guide_other_button_pressed[user_index].store(false);

            if (candidate_active) {
                bool require_solo =
                    settings::g_mainTabSettings.guide_button_solo_ui_toggle_only.GetValue();

                if (!require_solo || !other_pressed) {
                    execute_action(remap->action_name);
                    success = true;
                    LogInfo("InputRemapper::handle_button_release() - Home solo Display Commander UI toggle (Controller %lu)",
                            user_index);
                }
            }
        } else {
            // Other actions typically don't need release handling
            success = true;
        }
        break;
    case RemapType::Count:
        // Should never happen
        break;
    }
}

// Global functions
void initialize_input_remapping() { InputRemapper::get_instance().initialize(); }

void cleanup_input_remapping() { InputRemapper::get_instance().cleanup(); }

void process_gamepad_input_for_remapping(DWORD user_index, XINPUT_STATE *state) {
    InputRemapper::get_instance().process_gamepad_input(user_index, state);
}

// Utility functions
std::string get_keyboard_input_method_name(KeyboardInputMethod method) {
    switch (method) {
    case KeyboardInputMethod::SendInput:
        return "SendInput";
    case KeyboardInputMethod::KeybdEvent:
        return "keybd_event";
    case KeyboardInputMethod::SendMessage:
        return "SendMessage";
    case KeyboardInputMethod::PostMessage:
        return "PostMessage";
    default:
        return "Unknown";
    }
}

std::vector<std::string> get_available_keyboard_input_methods() {
    return {"SendInput", "keybd_event", "SendMessage", "PostMessage"};
}

std::vector<std::string> get_available_gamepad_buttons() {
    return {"A",     "B",    "X",     "Y",          "D-Pad Up",    "D-Pad Down",  "D-Pad Left",  "D-Pad Right",
            "Menu", "View", "Home", "Left Stick", "Right Stick", "Left Bumper", "Right Bumper"};
}

std::vector<std::string> get_available_keyboard_keys() {
    return {"Space", "Enter", "Escape", "Tab", "Shift", "Ctrl", "Alt", "F1",  "F2", "F3",
            "F4",    "F5",    "F6",     "F7",  "F8",    "F9",   "F10", "F11", "F12",
            "~",     "A",     "B",      "C",   "D",     "E",    "F",   "G",   "H",
            "I",     "J",     "K",      "L",   "M",     "N",    "O",   "P",   "Q",
            "R",     "S",     "T",      "U",   "V",     "W",    "X",   "Y",   "Z"};
}

void InputRemapper::increment_trigger_count(WORD gamepad_button) {
    ButtonRemap *remap = const_cast<ButtonRemap *>(get_button_remap(gamepad_button));
    if (remap) {
        remap->trigger_count.fetch_add(1);
    }
}

uint64_t InputRemapper::get_trigger_count(WORD gamepad_button) const {
    const ButtonRemap *remap = get_button_remap(gamepad_button);
    return remap ? remap->trigger_count.load() : 0;
}

void InputRemapper::apply_gamepad_remapping(DWORD user_index, XINPUT_STATE *state) {
    if (!state || user_index >= XUSER_MAX_COUNT) {
        return;
    }

    utils::SRWLockShared lock(_srwlock);

    // Apply all gamepad-to-gamepad remappings
    for (const auto &remap : _remappings) {
        if (!remap.enabled || remap.remap_type != RemapType::Gamepad) {
            continue;
        }

        // Check chord mode: if enabled, home button must also be pressed
        if (remap.chord_mode) {
            if ((state->Gamepad.wButtons & XINPUT_GAMEPAD_GUIDE) == 0) {
                // Home button not pressed, skip this remapping
                continue;
            }
        }

        // Check if source button is pressed
        if ((state->Gamepad.wButtons & remap.gamepad_button) != 0) {
            // Add target button to state
            state->Gamepad.wButtons |= remap.gamepad_target_button;

            // If hold_mode is false, remove source button (one-time press)
            if (!remap.hold_mode) {
                state->Gamepad.wButtons &= ~remap.gamepad_button;
            }
        } else if (remap.hold_mode) {
            // If hold_mode is true and source button is released, remove target button
            // (This is handled by button state tracking, but we ensure consistency here)
            // Note: We can't easily detect release here without state tracking
            // The release will be handled by handle_button_release
        }
    }
}

void InputRemapper::execute_action(const std::string &action_name) {
    // Helper function to trigger generic action notification
    auto trigger_action_notification = [](const std::string &name) {
        ActionNotification notification = {};
        notification.type = ActionNotificationType::GenericAction;
        notification.timestamp_ns = utils::get_now_ns();
        notification.float_value = 0.0f;
        notification.bool_value = false;
        size_t copy_len = (std::min)(name.length(), sizeof(notification.action_name) - 1);
        name.copy(notification.action_name, copy_len);
        notification.action_name[copy_len] = '\0';
        g_action_notification.store(notification);
    };

    if (action_name == "screenshot") {
        // Use ReShade 6.6.2+ runtime screenshot API
        reshade::api::effect_runtime* runtime = GetFirstReShadeRuntime();
        if (runtime != nullptr) {
            runtime->save_screenshot();
            trigger_action_notification("Screenshot");
            LogInfo("InputRemapper::execute_action() - Screenshot taken via ReShade runtime API");
        } else {
            LogWarn("InputRemapper::execute_action() - ReShade runtime not available for screenshot");
            // Fallback to old mechanism if runtime is not available
            auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
            if (shared_state) {
                shared_state->trigger_screenshot.store(true);
                trigger_action_notification("Screenshot");
                LogInfo("InputRemapper::execute_action() - Screenshot triggered via fallback mechanism");
            } else {
                LogError("InputRemapper::execute_action() - No screenshot mechanism available");
            }
        }
    } else if (action_name == "time slowdown toggle") {
        // Toggle time slowdown enabled state
        if (!enabled_experimental_features) {
            LogWarn("InputRemapper::execute_action() - Time slowdown toggle requires experimental features");
            return;
        }
        bool current_state = settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue();
        bool new_state = !current_state;
        settings::g_experimentalTabSettings.timeslowdown_enabled.SetValue(new_state);
        display_commanderhooks::SetTimeslowdownEnabled(new_state);
        trigger_action_notification("Time Slowdown " + std::string(new_state ? "On" : "Off"));
        LogInfo("InputRemapper::execute_action() - Time slowdown %s via action", new_state ? "enabled" : "disabled");
    } else if (action_name == "performance overlay toggle") {
        // Toggle performance overlay
        bool current_state = settings::g_mainTabSettings.show_test_overlay.GetValue();
        bool new_state = !current_state;
        settings::g_mainTabSettings.show_test_overlay.SetValue(new_state);
        trigger_action_notification("Performance Overlay " + std::string(new_state ? "On" : "Off"));
        LogInfo("InputRemapper::execute_action() - Performance overlay %s via action", new_state ? "enabled" : "disabled");
    } else if (action_name == "mute/unmute audio") {
        // Toggle audio mute state
        bool current_state = settings::g_mainTabSettings.audio_mute.GetValue();
        bool new_state = !current_state;
        settings::g_mainTabSettings.audio_mute.SetValue(new_state);

        // Apply the mute state immediately
        if (SetMuteForCurrentProcess(new_state)) {
            ::g_muted_applied.store(new_state);
            LogInfo("InputRemapper::execute_action() - Audio %s via action", new_state ? "muted" : "unmuted");
        } else {
            LogError("InputRemapper::execute_action() - Failed to %s audio", new_state ? "mute" : "unmute");
        }
    } else if (action_name == "increase volume") {
        // Increase volume by relative 20% (multiply by 1.2), minimum 1% change
        float current_volume = 0.0f;
        if (!GetVolumeForCurrentProcess(&current_volume)) {
            // If we can't get current volume, use stored value or default
            current_volume = ::s_audio_volume_percent.load();
        }

        float percent_change = 0.0f;
        if (current_volume <= 0.0f) {
            // Special case: if at 0%, jump to 1%
            percent_change = 1.0f;
        } else {
            // Calculate relative 20% increase (multiply by 1.2) for stability
            float new_volume = current_volume * 1.2f;
            // Ensure minimum 1% absolute change
            float min_new_volume = current_volume + 1.0f;
            if (new_volume < min_new_volume) {
                new_volume = min_new_volume;
            }
            percent_change = new_volume - current_volume;
        }

        // Use AdjustVolumeForCurrentProcess which handles system volume when game volume is at 100%
        if (AdjustVolumeForCurrentProcess(percent_change)) {
            float new_volume = ::s_audio_volume_percent.load();
            LogInfo("InputRemapper::execute_action() - Volume increased from %.1f%% to %.1f%% (change: +%.1f%%)",
                   current_volume, new_volume, percent_change);
        } else {
            LogError("InputRemapper::execute_action() - Failed to increase volume");
        }
    } else if (action_name == "decrease volume") {
        // Decrease volume by relative 20% (divide by 1.2), minimum 1% change
        float current_volume = 0.0f;
        if (!GetVolumeForCurrentProcess(&current_volume)) {
            // If we can't get current volume, use stored value or default
            current_volume = ::s_audio_volume_percent.load();
        }

        if (current_volume <= 0.0f) {
            // Already at 0%, can't go lower
            return;
        }

        // Calculate relative 20% decrease (divide by 1.2) for stability
        float new_volume = current_volume / 1.2f;
        // Ensure minimum 1% absolute change
        float max_new_volume = current_volume - 1.0f;
        if (new_volume > max_new_volume) {
            new_volume = max_new_volume;
        }
        float percent_change = new_volume - current_volume;

        // Use AdjustVolumeForCurrentProcess which handles system volume when game volume is at 100%
        if (AdjustVolumeForCurrentProcess(percent_change)) {
            float final_volume = ::s_audio_volume_percent.load();
            LogInfo("InputRemapper::execute_action() - Volume decreased from %.1f%% to %.1f%% (change: %.1f%%)",
                   current_volume, final_volume, percent_change);
        } else {
            LogError("InputRemapper::execute_action() - Failed to decrease volume");
        }
    } else if (action_name == "increase game speed") {
        // Increase game speed by 10% (multiply multiplier by 1.1)
        if (!enabled_experimental_features) {
            LogWarn("InputRemapper::execute_action() - Increase game speed requires experimental features");
            return;
        }
        float current_multiplier = display_commanderhooks::GetTimeslowdownMultiplier();
        float new_multiplier = current_multiplier * 1.1f;
        float max_multiplier = settings::g_experimentalTabSettings.timeslowdown_max_multiplier.GetValue();
        if (new_multiplier > max_multiplier) {
            new_multiplier = max_multiplier;
        }
        display_commanderhooks::SetTimeslowdownMultiplier(new_multiplier);
        trigger_action_notification("Game Speed: " + std::to_string(new_multiplier) + "x");
        LogInfo("InputRemapper::execute_action() - Game speed increased from %.2fx to %.2fx", current_multiplier, new_multiplier);
    } else if (action_name == "decrease game speed") {
        // Decrease game speed by 10% (divide multiplier by 1.1)
        if (!enabled_experimental_features) {
            LogWarn("InputRemapper::execute_action() - Decrease game speed requires experimental features");
            return;
        }
        float current_multiplier = display_commanderhooks::GetTimeslowdownMultiplier();
        float new_multiplier = current_multiplier / 1.1f;
        float min_multiplier = 0.1f; // Minimum multiplier
        if (new_multiplier < min_multiplier) {
            new_multiplier = min_multiplier;
        }
        display_commanderhooks::SetTimeslowdownMultiplier(new_multiplier);
        trigger_action_notification("Game Speed: " + std::to_string(new_multiplier) + "x");
        LogInfo("InputRemapper::execute_action() - Game speed decreased from %.2fx to %.2fx", current_multiplier, new_multiplier);
    } else if (action_name == "display commander ui toggle") {
        // Toggle Display Commander UI
        bool current_state = settings::g_mainTabSettings.show_display_commander_ui.GetValue();
        bool new_state = !current_state;
        settings::g_mainTabSettings.show_display_commander_ui.SetValue(new_state);
        trigger_action_notification("Display Commander UI " + std::string(new_state ? "On" : "Off"));
        LogInfo("InputRemapper::execute_action() - Display Commander UI %s via action", new_state ? "enabled" : "disabled");
    } else if (action_name == "increase system volume") {
        // Increase system volume by relative 20% (multiply by 1.2), minimum 1% change
        float current_volume = 0.0f;
        if (!GetSystemVolume(&current_volume)) {
            // If we can't get current system volume, use stored value or default
            current_volume = ::s_system_volume_percent.load();
        }

        float percent_change = 0.0f;
        if (current_volume <= 0.0f) {
            // Special case: if at 0%, jump to 1%
            percent_change = 1.0f;
        } else {
            // Calculate relative 20% increase (multiply by 1.2) for stability
            float new_volume = current_volume * 1.2f;
            // Ensure minimum 1% absolute change
            float min_new_volume = current_volume + 1.0f;
            if (new_volume < min_new_volume) {
                new_volume = min_new_volume;
            }
            percent_change = new_volume - current_volume;
        }

        if (AdjustSystemVolume(percent_change)) {
            float new_volume = 0.0f;
            GetSystemVolume(&new_volume);
            LogInfo("InputRemapper::execute_action() - System volume increased from %.1f%% to %.1f%% (change: +%.1f%%)",
                   current_volume, new_volume, percent_change);
        } else {
            LogError("InputRemapper::execute_action() - Failed to increase system volume");
        }
    } else if (action_name == "decrease system volume") {
        // Decrease system volume by relative 20% (divide by 1.2), minimum 1% change
        float current_volume = 0.0f;
        if (!GetSystemVolume(&current_volume)) {
            // If we can't get current system volume, use stored value or default
            current_volume = ::s_system_volume_percent.load();
        }

        if (current_volume <= 0.0f) {
            // Already at 0%, can't go lower
            return;
        }

        // Calculate relative 20% decrease (divide by 1.2) for stability
        float new_volume = current_volume / 1.2f;
        // Ensure minimum 1% absolute change
        float max_new_volume = current_volume - 1.0f;
        if (new_volume > max_new_volume) {
            new_volume = max_new_volume;
        }
        float percent_change = new_volume - current_volume;

        if (AdjustSystemVolume(percent_change)) {
            float final_volume = 0.0f;
            GetSystemVolume(&final_volume);
            LogInfo("InputRemapper::execute_action() - System volume decreased from %.1f%% to %.1f%% (change: %.1f%%)",
                   current_volume, final_volume, percent_change);
        } else {
            LogError("InputRemapper::execute_action() - Failed to decrease system volume");
        }
    } else {
        LogError("InputRemapper::execute_action() - Unknown action: %s", action_name.c_str());
    }
}

std::string get_remap_type_name(RemapType type) {
    switch (type) {
    case RemapType::Keyboard:
        return "Keyboard";
    case RemapType::Gamepad:
        return "Gamepad";
    case RemapType::Action:
        return "Action";
    case RemapType::Count:
        return "Unknown";
    }
    return "Unknown";
}

std::vector<std::string> get_available_actions() {
    return {"screenshot", "time slowdown toggle", "performance overlay toggle", "mute/unmute audio", "increase volume", "decrease volume", "increase system volume", "decrease system volume", "increase game speed", "decrease game speed", "display commander ui toggle"};
}
} // namespace display_commander::input_remapping
