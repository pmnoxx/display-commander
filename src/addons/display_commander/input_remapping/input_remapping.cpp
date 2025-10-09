/*
 * Copyright (C) 2024 Display Commander
 * Gamepad to keyboard input remapping system implementation
 */

#include "input_remapping.hpp"
#include "../utils.hpp"
#include "utils/srwlock_wrapper.hpp"
#include "../hooks/timeslowdown_hooks.hpp"
#include <reshade.hpp>

namespace display_commander::input_remapping {

// Helper function to get original GetTickCount64 value (unhooked)
static ULONGLONG GetOriginalTickCount64() {
    if (display_commanderhooks::GetTickCount64_Original) {
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

void InputRemapper::process_gamepad_input(DWORD user_index, const XINPUT_STATE *state) {
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
                handle_button_press(button_mask, user_index);
            } else {
                handle_button_release(button_mask, user_index);
            }
        }
    }
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
    LogInfo("InputRemapper::set_remapping_enabled() - Remapping %s", enabled ? "enabled" : "disabled");
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
                                 KeyboardInputMethod method, bool hold_mode) {
    ButtonRemap remap(gamepad_button, keyboard_vk, keyboard_name, true, method, hold_mode);
    add_button_remap(remap);
}

void InputRemapper::load_settings() {
    // Load remapping enabled state
    bool remapping_enabled;
    if (reshade::get_config_value(nullptr, "DisplayCommander.InputRemapping", "Enabled", remapping_enabled)) {
        _remapping_enabled.store(remapping_enabled);
    }

    // Load default input method
    int default_method;
    if (reshade::get_config_value(nullptr, "DisplayCommander.InputRemapping", "DefaultMethod", default_method)) {
        _default_input_method = static_cast<KeyboardInputMethod>(default_method);
    }

    // Load remappings count
    int remapping_count;
    if (reshade::get_config_value(nullptr, "DisplayCommander.InputRemapping", "Count", remapping_count)) {
        // Load each remapping
        for (int i = 0; i < remapping_count; ++i) {
            std::string key_prefix = "Remapping" + std::to_string(i) + ".";

            int gamepad_button, keyboard_vk, input_method;
            bool enabled, hold_mode;
            char keyboard_name[256] = {0};
            size_t keyboard_name_size = sizeof(keyboard_name);

            if (reshade::get_config_value(nullptr, "DisplayCommander.InputRemapping",
                                          (key_prefix + "GamepadButton").c_str(), gamepad_button) &&
                reshade::get_config_value(nullptr, "DisplayCommander.InputRemapping",
                                          (key_prefix + "KeyboardVk").c_str(), keyboard_vk) &&
                reshade::get_config_value(nullptr, "DisplayCommander.InputRemapping",
                                          (key_prefix + "InputMethod").c_str(), input_method) &&
                reshade::get_config_value(nullptr, "DisplayCommander.InputRemapping", (key_prefix + "Enabled").c_str(),
                                          enabled) &&
                reshade::get_config_value(nullptr, "DisplayCommander.InputRemapping", (key_prefix + "HoldMode").c_str(),
                                          hold_mode) &&
                reshade::get_config_value(nullptr, "DisplayCommander.InputRemapping",
                                          (key_prefix + "KeyboardName").c_str(), keyboard_name, &keyboard_name_size)) {

                ButtonRemap remap(static_cast<WORD>(gamepad_button), keyboard_vk, keyboard_name, enabled,
                                  static_cast<KeyboardInputMethod>(input_method), hold_mode);
                add_button_remap(remap);
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
    reshade::set_config_value(nullptr, "DisplayCommander.InputRemapping", "Enabled", _remapping_enabled.load());

    // Save default input method
    reshade::set_config_value(nullptr, "DisplayCommander.InputRemapping", "DefaultMethod",
                              static_cast<int>(_default_input_method));

    // Save remappings count
    reshade::set_config_value(nullptr, "DisplayCommander.InputRemapping", "Count",
                              static_cast<int>(_remappings.size()));

    // Save each remapping
    for (size_t i = 0; i < _remappings.size(); ++i) {
        const auto &remap = _remappings[i];
        std::string key_prefix = "Remapping" + std::to_string(i) + ".";

        reshade::set_config_value(nullptr, "DisplayCommander.InputRemapping", (key_prefix + "GamepadButton").c_str(),
                                  static_cast<int>(remap.gamepad_button));
        reshade::set_config_value(nullptr, "DisplayCommander.InputRemapping", (key_prefix + "KeyboardVk").c_str(),
                                  remap.keyboard_vk);
        reshade::set_config_value(nullptr, "DisplayCommander.InputRemapping", (key_prefix + "InputMethod").c_str(),
                                  static_cast<int>(remap.input_method));
        reshade::set_config_value(nullptr, "DisplayCommander.InputRemapping", (key_prefix + "Enabled").c_str(),
                                  remap.enabled);
        reshade::set_config_value(nullptr, "DisplayCommander.InputRemapping", (key_prefix + "HoldMode").c_str(),
                                  remap.hold_mode);
        reshade::set_config_value(nullptr, "DisplayCommander.InputRemapping", (key_prefix + "KeyboardName").c_str(),
                                  remap.keyboard_name.c_str());
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
        return "Start";
    case XINPUT_GAMEPAD_BACK:
        return "Back";
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
        return "Guide";
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
    return 0;
}

HWND InputRemapper::get_active_window() const { return GetForegroundWindow(); }

void InputRemapper::update_button_states(DWORD user_index, WORD button_state) {
    if (user_index >= XUSER_MAX_COUNT)
        return;

    _previous_button_states[user_index].store(_current_button_states[user_index].load());
    _current_button_states[user_index].store(button_state);
}

void InputRemapper::handle_button_press(WORD gamepad_button, DWORD user_index) {
    ButtonRemap *remap = const_cast<ButtonRemap *>(get_button_remap(gamepad_button));
    if (!remap || !remap->enabled)
        return;

    remap->is_pressed.store(true);
    remap->last_press_time.store(GetOriginalTickCount64());

    // Send keyboard input
    bool success = false;
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
        // Increment trigger counter
        increment_trigger_count(gamepad_button);

        LogInfo("InputRemapper::handle_button_press() - Mapped %s to %s (Controller %lu)",
                get_button_name(gamepad_button).c_str(), remap->keyboard_name.c_str(), user_index);
    } else {
        LogError("InputRemapper::handle_button_press() - Failed to send keyboard input for %s",
                 remap->keyboard_name.c_str());
    }
}

void InputRemapper::handle_button_release(WORD gamepad_button, DWORD user_index) {
    ButtonRemap *remap = const_cast<ButtonRemap *>(get_button_remap(gamepad_button));
    if (!remap || !remap->enabled || !remap->hold_mode)
        return;

    remap->is_pressed.store(false);

    // Send keyboard release
    bool success = false;
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
        LogInfo("InputRemapper::handle_button_release() - Released %s (Controller %lu)", remap->keyboard_name.c_str(),
                user_index);
    }
}

// Global functions
void initialize_input_remapping() { InputRemapper::get_instance().initialize(); }

void cleanup_input_remapping() { InputRemapper::get_instance().cleanup(); }

void process_gamepad_input_for_remapping(DWORD user_index, const XINPUT_STATE *state) {
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
            "Start", "Back", "Guide", "Left Stick", "Right Stick", "Left Bumper", "Right Bumper"};
}

std::vector<std::string> get_available_keyboard_keys() {
    return {"Space", "Enter", "Escape", "Tab", "Shift", "Ctrl", "Alt", "F1",  "F2", "F3",
            "F4",    "F5",    "F6",     "F7",  "F8",    "F9",   "F10", "F11", "F12"};
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
} // namespace display_commander::input_remapping
