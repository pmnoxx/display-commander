#pragma once

#include <Windows.h>
#include <Xinput.h>
#include <memory>
#include <atomic>
#include <string>
#include <array>

#pragma comment(lib, "Xinput.lib")

namespace display_commander::xinput {

// Controller state structure
struct ControllerState {
    bool connected = false;
    bool was_connected = false;
    DWORD controller_index = 0;
    XINPUT_STATE state = {};
    XINPUT_VIBRATION vibration = {};

    // Button states for toggle detection
    std::array<bool, 20> previous_pressed = {};
    std::array<bool, 20> toggle_states = {};

    // Raw input values (normalized)
    std::array<float, 20> raw_values = {};
    std::array<float, 20> toggle_values = {};

    // Stick range settings
    static constexpr float MAX_STICK = 32767.0f;
};

// XInput Controller Manager
class XInputControllerManager {
public:
    XInputControllerManager();
    ~XInputControllerManager();

    // Initialize/Shutdown
    bool Initialize();
    void Shutdown();

    // Controller management
    void UpdateControllers();
    bool IsControllerConnected(DWORD index) const;
    const ControllerState& GetControllerState(DWORD index) const;

    // Vibration control
    bool SetVibration(DWORD index, float left_motor, float right_motor);
    bool StopVibration(DWORD index);
    void StopAllVibration();


    // Input state access
    const std::array<float, 20>& GetRawValues(DWORD index) const;
    const std::array<float, 20>& GetToggleValues(DWORD index) const;

    // Controller info
    std::string GetControllerInfo(DWORD index) const;
    DWORD GetConnectedControllerCount() const;

private:
    // Controller states (max 4 controllers)
    std::array<ControllerState, 4> controllers_;

    void UpdateControllerState(ControllerState& controller);
    void GetGamepadState(ControllerState& controller);

    // State management
    std::atomic<bool> initialized_{false};
    std::atomic<bool> shutdown_{false};
};

// Global instance
extern std::unique_ptr<XInputControllerManager> g_xinput_manager;

// Initialize XInput system
bool InitializeXInput();

// Shutdown XInput system
void ShutdownXInput();

// Get global XInput manager
XInputControllerManager* GetXInputManager();

} // namespace display_commander::xinput
