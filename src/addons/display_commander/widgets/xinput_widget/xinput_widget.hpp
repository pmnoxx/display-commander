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

    // Chord detection
    struct Chord {
        WORD buttons;
        std::string name;
        std::string action;
        bool enabled{true};
        std::atomic<bool> is_pressed{false};
        std::atomic<ULONGLONG> last_press_time{0};

        // Default constructor
        Chord() = default;

        // Copy constructor
        Chord(const Chord &other)
            : buttons(other.buttons), name(other.name), action(other.action), enabled(other.enabled),
              is_pressed(other.is_pressed.load()), last_press_time(other.last_press_time.load()) {}

        // Assignment operator
        Chord &operator=(const Chord &other) {
            if (this != &other) {
                buttons = other.buttons;
                name = other.name;
                action = other.action;
                enabled = other.enabled;
                is_pressed.store(other.is_pressed.load());
                last_press_time.store(other.last_press_time.load());
            }
            return *this;
        }
    };

    std::vector<Chord> chords;
    std::atomic<WORD> current_button_state{0};
    std::atomic<bool> suppress_input{false};
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
        std::atomic<uint64_t> last_fire_frame_id; // Last frame when this button was toggled
        std::atomic<bool> current_state;          // Current autofire state (on/off)

        AutofireButton() : button_mask(0), last_fire_frame_id(0), current_state(false) {}
        AutofireButton(WORD mask) : button_mask(mask), last_fire_frame_id(0), current_state(false) {}

        // Copy constructor (needed for vector operations)
        AutofireButton(const AutofireButton& other)
            : button_mask(other.button_mask),
              last_fire_frame_id(other.last_fire_frame_id.load()),
              current_state(other.current_state.load()) {}

        // Assignment operator
        AutofireButton& operator=(const AutofireButton& other) {
            if (this != &other) {
                button_mask = other.button_mask;
                last_fire_frame_id.store(other.last_fire_frame_id.load());
                current_state.store(other.current_state.load());
            }
            return *this;
        }
    };

    std::atomic<bool> autofire_enabled{false};              // Master enable/disable for autofire
    std::atomic<uint32_t> autofire_frame_interval{1};        // Frames between toggles (1 = every frame, 2 = every other frame, etc.)
    std::vector<AutofireButton> autofire_buttons;            // List of buttons with autofire enabled
    mutable SRWLOCK autofire_lock = SRWLOCK_INIT;           // Thread safety for autofire_buttons vector access
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

    // Chord detection functions
    void DrawChordSettings();
    void InitializeDefaultChords();
    void ProcessChordDetection(DWORD user_index, WORD button_state);
    void ExecuteChordAction(const XInputSharedState::Chord &chord, DWORD user_index);
    std::string GetChordButtonNames(WORD buttons) const;

    // Autofire functions
    void DrawAutofireSettings();
    void AddAutofireButton(WORD button_mask);
    void RemoveAutofireButton(WORD button_mask);
    bool IsAutofireButton(WORD button_mask) const;

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
void ProcessChordDetection(DWORD user_index, WORD button_state);
void CheckAndHandleScreenshot();
void ProcessAutofire(DWORD user_index, XINPUT_STATE *pState);

// Recenter calibration functions for hooks
void ProcessRecenterData(SHORT left_x, SHORT left_y, SHORT right_x, SHORT right_y);

} // namespace display_commander::widgets::xinput_widget
