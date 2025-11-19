#pragma once

#include <array>
#include <atomic>
#include <imgui.h>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>
#include <xinput.h>
#include "../../dualsense/dualsense_hid_wrapper.hpp"


// Guide button constant (not defined in standard XInput headers)
#ifndef XINPUT_GAMEPAD_GUIDE
#define XINPUT_GAMEPAD_GUIDE 0x0400
#endif

namespace display_commander::widgets::xinput_widget {

// Controller connection states
enum class ControllerState {
    Uninitialized,  // Controller state has not been checked yet
    Connected,      // Controller is connected and working
    Unconnected     // Controller is not connected or failed
};

// Thread-safe shared state for XInput data
struct XInputSharedState {
    // Controller states for all 4 possible controllers
    std::array<XINPUT_STATE, XUSER_MAX_COUNT> controller_states;
    std::array<ControllerState, XUSER_MAX_COUNT> controller_connected;
    std::array<DWORD, XUSER_MAX_COUNT> last_packet_numbers;

    // Event counters
    std::atomic<uint64_t> total_events{0};
    std::atomic<uint64_t> button_events{0};
    std::atomic<uint64_t> stick_events{0};
    std::atomic<uint64_t> trigger_events{0};

    // HID CreateFile counters
    std::atomic<uint64_t> hid_createfile_total{0};
    std::atomic<uint64_t> hid_createfile_dualsense{0};

    std::atomic<bool> trigger_screenshot{false};
    std::atomic<bool> ui_overlay_open{false};

    // Settings
    std::atomic<bool> enable_xinput_hooks{true}; // Enable XInput hooks (off by default)
    std::atomic<bool> swap_a_b_buttons{false};
    std::atomic<bool> enable_dualsense_xinput{false}; // Enable DualSense to XInput conversion
    std::atomic<float> left_stick_max_input{
        1.0f}; // Left stick sensitivity (max input) - 0.7 = 70% stick movement = 100% output
    std::atomic<float> right_stick_max_input{
        1.0f}; // Right stick sensitivity (max input) - 0.7 = 70% stick movement = 100% output
    std::atomic<float> left_stick_min_output{
        0.0f}; // Left stick remove game's deadzone (min output) - 0.3 = eliminates small movements
    std::atomic<float> right_stick_min_output{
        0.0f}; // Right stick remove game's deadzone (min output) - 0.3 = eliminates small movements
    std::atomic<float> left_stick_deadzone{
        0.0f}; // Left stick dead zone (min input) - 0.0 = no deadzone, 15.0 = ignores small movements
    std::atomic<float> right_stick_deadzone{
        0.0f}; // Right stick dead zone (min input) - 0.0 = no deadzone, 15.0 = ignores small movements
    std::atomic<bool> left_stick_circular{true};  // Left stick processing mode: true = circular (radial), false = square (separate axes)
    std::atomic<bool> right_stick_circular{true}; // Right stick processing mode: true = circular (radial), false = square (separate axes)

    // Stick center calibration
    std::atomic<float> left_stick_center_x{0.0f};  // Left stick X center offset
    std::atomic<float> left_stick_center_y{0.0f};  // Left stick Y center offset
    std::atomic<float> right_stick_center_x{0.0f}; // Right stick X center offset
    std::atomic<float> right_stick_center_y{0.0f}; // Right stick Y center offset

    // Gamepad input override state
    // Values of INFINITY mean "not overridden" - use original input
    // For sticks: -1.0 to 1.0 range (will be converted to SHORT)
    // For buttons: mask of buttons to press (0 = no override)
    struct OverrideState {
        std::atomic<float> left_stick_x{INFINITY};   // INF when not overridden
        std::atomic<float> left_stick_y{INFINITY};   // INF when not overridden
        std::atomic<float> right_stick_x{INFINITY};   // INF when not overridden
        std::atomic<float> right_stick_y{INFINITY};   // INF when not overridden
        std::atomic<WORD> buttons_pressed_mask{0};    // Mask 0 = do nothing
    };

    OverrideState override_state;

    // Vibration amplification
    std::atomic<float> vibration_amplification{1.0f}; // Vibration amplification multiplier (1.0 = normal, 2.0 = double)

    // Last update time for each controller
    std::array<std::atomic<uint64_t>, XUSER_MAX_COUNT> last_update_times;

    // Battery information for each controller
    std::array<XINPUT_BATTERY_INFORMATION, XUSER_MAX_COUNT> battery_info;
    std::array<std::atomic<uint64_t>, XUSER_MAX_COUNT> last_battery_update_times;
    std::array<std::atomic<bool>, XUSER_MAX_COUNT> battery_info_valid;

    // Recenter calibration data
    struct RecenterData {
        // Raw min/max values for each axis (range -32768 to 32767)
        std::atomic<SHORT> left_stick_x_min{32767};
        std::atomic<SHORT> left_stick_x_max{-32768};
        std::atomic<SHORT> left_stick_y_min{32767};
        std::atomic<SHORT> left_stick_y_max{-32768};
        std::atomic<SHORT> right_stick_x_min{32767};
        std::atomic<SHORT> right_stick_x_max{-32768};
        std::atomic<SHORT> right_stick_y_min{32767};
        std::atomic<SHORT> right_stick_y_max{-32768};

        // Computed center values
        std::atomic<SHORT> left_stick_x_center{0};
        std::atomic<SHORT> left_stick_y_center{0};
        std::atomic<SHORT> right_stick_x_center{0};
        std::atomic<SHORT> right_stick_y_center{0};

        // Recording state
        std::atomic<bool> is_recording{false};
        std::atomic<bool> has_data{false};
    };

    RecenterData recenter_data;

    // Thread safety
    mutable SRWLOCK state_lock = SRWLOCK_INIT;

    // XInput call timing tracking for smooth rate calculation
    std::atomic<uint64_t> last_xinput_call_time_ns{0};
    std::atomic<uint64_t> xinput_getstate_update_ns{0};
    std::atomic<uint64_t> xinput_getstateex_update_ns{0};

    // Autofire settings
    struct AutofireButton {
        WORD button_mask;
        std::atomic<uint64_t> phase_start_frame_id; // Frame when current phase (hold down/up) started
        std::atomic<bool> is_holding_down;          // true = holding down phase, false = holding up phase

        AutofireButton() : button_mask(0), phase_start_frame_id(0), is_holding_down(true) {}
        AutofireButton(WORD mask) : button_mask(mask), phase_start_frame_id(0), is_holding_down(true) {}

        // Copy constructor (needed for vector operations)
        AutofireButton(const AutofireButton& other)
            : button_mask(other.button_mask),
              phase_start_frame_id(other.phase_start_frame_id.load()),
              is_holding_down(other.is_holding_down.load()) {}

        // Assignment operator
        AutofireButton& operator=(const AutofireButton& other) {
            if (this != &other) {
                button_mask = other.button_mask;
                phase_start_frame_id.store(other.phase_start_frame_id.load());
                is_holding_down.store(other.is_holding_down.load());
            }
            return *this;
        }
    };

    // Autofire trigger settings
    enum class TriggerType {
        LeftTrigger,  // LT
        RightTrigger  // RT
    };

    struct AutofireTrigger {
        TriggerType trigger_type;
        std::atomic<uint64_t> phase_start_frame_id; // Frame when current phase (hold down/up) started
        std::atomic<bool> is_holding_down;          // true = holding down phase, false = holding up phase

        AutofireTrigger() : trigger_type(TriggerType::LeftTrigger), phase_start_frame_id(0), is_holding_down(true) {}
        AutofireTrigger(TriggerType type) : trigger_type(type), phase_start_frame_id(0), is_holding_down(true) {}

        // Copy constructor (needed for vector operations)
        AutofireTrigger(const AutofireTrigger& other)
            : trigger_type(other.trigger_type),
              phase_start_frame_id(other.phase_start_frame_id.load()),
              is_holding_down(other.is_holding_down.load()) {}

        // Assignment operator
        AutofireTrigger& operator=(const AutofireTrigger& other) {
            if (this != &other) {
                trigger_type = other.trigger_type;
                phase_start_frame_id.store(other.phase_start_frame_id.load());
                is_holding_down.store(other.is_holding_down.load());
            }
            return *this;
        }
    };

    std::atomic<bool> autofire_enabled{false};              // Master enable/disable for autofire
    std::atomic<uint32_t> autofire_hold_down_frames{1};    // Frames to hold button down
    std::atomic<uint32_t> autofire_hold_up_frames{1};      // Frames to hold button up
    std::vector<AutofireButton> autofire_buttons;            // List of buttons with autofire enabled
    std::vector<AutofireTrigger> autofire_triggers;          // List of triggers with autofire enabled
    mutable SRWLOCK autofire_lock = SRWLOCK_INIT;           // Thread safety for autofire_buttons and autofire_triggers vector access
};

// XInput widget class
class XInputWidget {
  public:
    XInputWidget();
    ~XInputWidget() = default;

    // Main draw function - call this from the main tab
    void OnDraw();

    // Initialize the widget (call once at startup)
    void Initialize();

    // Cleanup the widget (call at shutdown)
    void Cleanup();

    // Get the shared state (thread-safe)
    static std::shared_ptr<XInputSharedState> GetSharedState();

  private:
    // UI state
    bool is_initialized_ = false;
    int selected_controller_ = 0;

    // UI helper functions
    void DrawControllerSelector();
    void DrawControllerState();
    void DrawSettings();
    void DrawEventCounters();
    void DrawVibrationTest();
    void DrawButtonStates(const XINPUT_GAMEPAD &gamepad);
    void DrawStickStates(const XINPUT_GAMEPAD &gamepad);
    void DrawStickStatesExtended(float left_deadzone, float left_max_input, float left_min_output, float right_deadzone,
                                 float right_max_input, float right_min_output);
    void DrawTriggerStates(const XINPUT_GAMEPAD &gamepad);
    void DrawBatteryStatus(int controller_index);
    void DrawDualSenseReport(int controller_index);

    // Helper functions
    std::string GetButtonName(WORD button) const;
    std::string GetControllerStatus(int controller_index) const;
    bool IsButtonPressed(WORD buttons, WORD button) const;

    // Settings management
    void LoadSettings();
    void SaveSettings();

    // Vibration test functions
    void TestLeftMotor();
    void TestRightMotor();
    void StopVibration();

    // Autofire functions
    void DrawAutofireSettings();
    void AddAutofireButton(WORD button_mask);
    void RemoveAutofireButton(WORD button_mask);
    bool IsAutofireButton(WORD button_mask) const;
    void AddAutofireTrigger(XInputSharedState::TriggerType trigger_type);
    void RemoveAutofireTrigger(XInputSharedState::TriggerType trigger_type);
    bool IsAutofireTrigger(XInputSharedState::TriggerType trigger_type) const;

     // Recenter calibration functions
    void DrawRecenterSettings();
    void ClearRecenterData();
    void StartRecenterRecording();
    void StopRecenterRecording();
    void ProcessRecenterData(SHORT left_x, SHORT left_y, SHORT right_x, SHORT right_y);
    void ApplyRecenterCalibration(SHORT &left_x, SHORT &left_y, SHORT &right_x, SHORT &right_y);

    // Global shared state
    static std::shared_ptr<XInputSharedState> g_shared_state;
};

// Global widget instance
extern std::unique_ptr<XInputWidget> g_xinput_widget;

// Global functions for integration
void InitializeXInputWidget();
void CleanupXInputWidget();
void DrawXInputWidget();

// Global functions for hooks to use
void UpdateXInputState(DWORD user_index, const XINPUT_STATE *state);
void UpdateBatteryStatus(DWORD user_index);
void IncrementEventCounter(const std::string &event_type);
void CheckAndHandleScreenshot();
void ProcessAutofire(DWORD user_index, XINPUT_STATE *pState);

// Recenter calibration functions for hooks
void ProcessRecenterData(SHORT left_x, SHORT left_y, SHORT right_x, SHORT right_y);

} // namespace display_commander::widgets::xinput_widget
