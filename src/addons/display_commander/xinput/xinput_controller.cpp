#include "xinput_controller.hpp"
#include "../addon.hpp"
#include <cmath>
#include <sstream>

namespace display_commander::xinput {

// Global instance
std::unique_ptr<XInputControllerManager> g_xinput_manager = nullptr;

XInputControllerManager::XInputControllerManager() {
    // Initialize all controller states
    for (DWORD i = 0; i < 4; ++i) {
        controllers_[i].controller_index = i;
    }
}

XInputControllerManager::~XInputControllerManager() {
    Shutdown();
}

bool XInputControllerManager::Initialize() {
    if (initialized_.load()) {
        return true;
    }

    LogInfo("Initializing XInput Controller Manager");

    // Test XInput availability
    XINPUT_STATE test_state;
    DWORD result = XInputGetState(0, &test_state);
    if (result != ERROR_SUCCESS && result != ERROR_DEVICE_NOT_CONNECTED) {
        LogError("XInput not available: %lu", result);
        return false;
    }

    initialized_.store(true);
    LogInfo("XInput Controller Manager initialized successfully");
    return true;
}

void XInputControllerManager::Shutdown() {
    if (!initialized_.load() || shutdown_.load()) {
        return;
    }

    LogInfo("Shutting down XInput Controller Manager");

    // Stop all vibration
    StopAllVibration();

    shutdown_.store(true);
    initialized_.store(false);

    LogInfo("XInput Controller Manager shutdown complete");
}

void XInputControllerManager::UpdateControllers() {
    if (!initialized_.load() || shutdown_.load()) {
        return;
    }

    for (DWORD i = 0; i < 4; ++i) {
        UpdateControllerState(controllers_[i]);
    }
}

bool XInputControllerManager::IsControllerConnected(DWORD index) const {
    if (index >= 4) return false;
    return controllers_[index].connected;
}

const ControllerState& XInputControllerManager::GetControllerState(DWORD index) const {
    static ControllerState empty_state;
    if (index >= 4) return empty_state;
    return controllers_[index];
}

bool XInputControllerManager::SetVibration(DWORD index, float left_motor, float right_motor) {
    if (!initialized_.load() || index >= 4) {
        return false;
    }

    // Clamp values to valid range
    left_motor = (left_motor < 0.0f) ? 0.0f : (left_motor > 1.0f) ? 1.0f : left_motor;
    right_motor = (right_motor < 0.0f) ? 0.0f : (right_motor > 1.0f) ? 1.0f : right_motor;

    // Convert to XInput range (0-65535)
    WORD left_speed = static_cast<WORD>(left_motor * 65535.0f);
    WORD right_speed = static_cast<WORD>(right_motor * 65535.0f);

    XINPUT_VIBRATION vibration = {};
    vibration.wLeftMotorSpeed = left_speed;
    vibration.wRightMotorSpeed = right_speed;

    DWORD result = XInputSetState(index, &vibration);
    if (result == ERROR_SUCCESS) {
        controllers_[index].vibration = vibration;
        return true;
    }

    return false;
}

bool XInputControllerManager::StopVibration(DWORD index) {
    return SetVibration(index, 0.0f, 0.0f);
}

void XInputControllerManager::StopAllVibration() {
    for (DWORD i = 0; i < 4; ++i) {
        StopVibration(i);
    }
}



const std::array<float, 20>& XInputControllerManager::GetRawValues(DWORD index) const {
    static const std::array<float, 20> empty_values = {};
    if (index >= 4) return empty_values;
    return controllers_[index].raw_values;
}

const std::array<float, 20>& XInputControllerManager::GetToggleValues(DWORD index) const {
    static const std::array<float, 20> empty_values = {};
    if (index >= 4) return empty_values;
    return controllers_[index].toggle_values;
}

std::string XInputControllerManager::GetControllerInfo(DWORD index) const {
    if (index >= 4 || !controllers_[index].connected) {
        return "Not Connected";
    }

    const auto& state = controllers_[index].state;
    std::ostringstream oss;

    oss << "Controller " << index << " - ";
    oss << "Battery: ";

    // Get battery information
    XINPUT_BATTERY_INFORMATION battery_info;
    DWORD battery_result = XInputGetBatteryInformation(index, BATTERY_DEVTYPE_GAMEPAD, &battery_info);
    if (battery_result == ERROR_SUCCESS) {
        switch (battery_info.BatteryType) {
            case BATTERY_TYPE_DISCONNECTED:
                oss << "Disconnected";
                break;
            case BATTERY_TYPE_WIRED:
                oss << "Wired";
                break;
            case BATTERY_TYPE_ALKALINE:
            case BATTERY_TYPE_NIMH:
                oss << "Battery: ";
                switch (battery_info.BatteryLevel) {
                    case BATTERY_LEVEL_EMPTY: oss << "Empty"; break;
                    case BATTERY_LEVEL_LOW: oss << "Low"; break;
                    case BATTERY_LEVEL_MEDIUM: oss << "Medium"; break;
                    case BATTERY_LEVEL_FULL: oss << "Full"; break;
                    default: oss << "Unknown"; break;
                }
                break;
            default:
                oss << "Unknown";
                break;
        }
    } else {
        oss << "Unknown";
    }

    return oss.str();
}

DWORD XInputControllerManager::GetConnectedControllerCount() const {
    DWORD count = 0;
    for (const auto& controller : controllers_) {
        if (controller.connected) {
            count++;
        }
    }
    return count;
}


void XInputControllerManager::UpdateControllerState(ControllerState& controller) {
    // Store previous connection state
    controller.was_connected = controller.connected;

    // Get current state
    DWORD result = XInputGetState(controller.controller_index, &controller.state);
    controller.connected = (result == ERROR_SUCCESS);

    if (controller.connected) {
        // Update input values
        GetGamepadState(controller);

        // Update toggle states for buttons and triggers
        for (int i = 0; i < 20; ++i) {
            // Treat triggers (indices 4 and 5) and all buttons (index >= 6) as toggle candidates
            bool currently_pressed = (i >= 4 && controller.raw_values[i] > 0.1f);

            if (currently_pressed && !controller.previous_pressed[i]) {
                controller.toggle_states[i] = !controller.toggle_states[i];
            }
            controller.previous_pressed[i] = currently_pressed;

            // Update toggle values
            if (i < 4) {
                // Sticks: raw analog values for both raw and toggle arrays
                controller.toggle_values[i] = controller.raw_values[i];
            } else {
                // Triggers and buttons: raw is direct analog/press state, toggle is latched bool
                controller.toggle_values[i] = controller.toggle_states[i] ? 1.0f : 0.0f;
            }
        }
    } else {
        // Clear values when disconnected
        std::fill(controller.raw_values.begin(), controller.raw_values.end(), 0.0f);
        std::fill(controller.toggle_values.begin(), controller.toggle_values.end(), 0.0f);
    }
}

void XInputControllerManager::GetGamepadState(ControllerState& controller) {
    // Clear all values
    std::fill(controller.raw_values.begin(), controller.raw_values.end(), 0.0f);

    if (!controller.connected) {
        return;
    }

    const XINPUT_GAMEPAD& gamepad = controller.state.Gamepad;

    // Raw stick values (normalized to -1.0 to 1.0)
    controller.raw_values[0] = static_cast<float>(gamepad.sThumbLX) / ControllerState::MAX_STICK;
    controller.raw_values[1] = static_cast<float>(gamepad.sThumbLY) / ControllerState::MAX_STICK;
    controller.raw_values[2] = static_cast<float>(gamepad.sThumbRX) / ControllerState::MAX_STICK;
    controller.raw_values[3] = static_cast<float>(gamepad.sThumbRY) / ControllerState::MAX_STICK;

    // Triggers (normalized to 0-1)
    controller.raw_values[4] = gamepad.bLeftTrigger / 255.0f;
    controller.raw_values[5] = gamepad.bRightTrigger / 255.0f;

    // Buttons
    WORD buttons = gamepad.wButtons;
    controller.raw_values[6] = (buttons & XINPUT_GAMEPAD_A) ? 1.0f : 0.0f;
    controller.raw_values[7] = (buttons & XINPUT_GAMEPAD_B) ? 1.0f : 0.0f;
    controller.raw_values[8] = (buttons & XINPUT_GAMEPAD_X) ? 1.0f : 0.0f;
    controller.raw_values[9] = (buttons & XINPUT_GAMEPAD_Y) ? 1.0f : 0.0f;
    controller.raw_values[10] = (buttons & XINPUT_GAMEPAD_START) ? 1.0f : 0.0f;
    controller.raw_values[11] = (buttons & XINPUT_GAMEPAD_BACK) ? 1.0f : 0.0f;
    controller.raw_values[12] = (buttons & XINPUT_GAMEPAD_DPAD_UP) ? 1.0f : 0.0f;
    controller.raw_values[13] = (buttons & XINPUT_GAMEPAD_DPAD_DOWN) ? 1.0f : 0.0f;
    controller.raw_values[14] = (buttons & XINPUT_GAMEPAD_DPAD_LEFT) ? 1.0f : 0.0f;
    controller.raw_values[15] = (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) ? 1.0f : 0.0f;
    controller.raw_values[16] = (buttons & XINPUT_GAMEPAD_LEFT_THUMB) ? 1.0f : 0.0f;
    controller.raw_values[17] = (buttons & XINPUT_GAMEPAD_RIGHT_THUMB) ? 1.0f : 0.0f;
    controller.raw_values[18] = (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) ? 1.0f : 0.0f;
    controller.raw_values[19] = (buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? 1.0f : 0.0f;
}

// Global functions
bool InitializeXInput() {
    if (g_xinput_manager) {
        return true;
    }

    g_xinput_manager = std::make_unique<XInputControllerManager>();
    return g_xinput_manager->Initialize();
}

void ShutdownXInput() {
    if (g_xinput_manager) {
        g_xinput_manager->Shutdown();
        g_xinput_manager.reset();
    }
}

XInputControllerManager* GetXInputManager() {
    return g_xinput_manager.get();
}

} // namespace display_commander::xinput
