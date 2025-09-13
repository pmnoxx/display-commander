#pragma once

#include <deps/imgui/imgui.h>
#include <memory>
#include <atomic>
#include <string>
#include <array>
#include <vector>
#include <windows.h>
#include <xinput.h>

// Guide button constant (not defined in standard XInput headers)
#ifndef XINPUT_GAMEPAD_GUIDE
#define XINPUT_GAMEPAD_GUIDE 0x0400
#endif

namespace display_commander::widgets::xinput_widget {

// Thread-safe shared state for XInput data
struct XInputSharedState {
    // Controller states for all 4 possible controllers
    std::array<XINPUT_STATE, XUSER_MAX_COUNT> controller_states;
    std::array<bool, XUSER_MAX_COUNT> controller_connected;
    std::array<DWORD, XUSER_MAX_COUNT> last_packet_numbers;

    // Event counters
    std::atomic<uint64_t> total_events{0};
    std::atomic<uint64_t> button_events{0};
    std::atomic<uint64_t> stick_events{0};
    std::atomic<uint64_t> trigger_events{0};

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
        Chord(const Chord& other)
            : buttons(other.buttons), name(other.name), action(other.action),
              enabled(other.enabled), is_pressed(other.is_pressed.load()),
              last_press_time(other.last_press_time.load()) {}

        // Assignment operator
        Chord& operator=(const Chord& other) {
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
    std::atomic<bool> swap_a_b_buttons{false};
    std::atomic<float> left_stick_sensitivity{1.0f}; // Left stick sensitivity multiplier
    std::atomic<float> right_stick_sensitivity{1.0f}; // Right stick sensitivity multiplier
    std::atomic<float> left_stick_deadzone{0.0f}; // Left stick deadzone percentage (-100.0 = -100%, 100.0 = 100%)
    std::atomic<float> right_stick_deadzone{0.0f}; // Right stick deadzone percentage (-100.0 = -100%, 100.0 = 100%)

    // Last update time for each controller
    std::array<std::atomic<uint64_t>, XUSER_MAX_COUNT> last_update_times;

    // Thread safety
    mutable std::atomic<bool> is_updating{false};
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
    void DrawButtonStates(const XINPUT_GAMEPAD& gamepad);
    void DrawStickStates(const XINPUT_GAMEPAD& gamepad);
    void DrawTriggerStates(const XINPUT_GAMEPAD& gamepad);

    // Helper functions
    std::string GetButtonName(WORD button) const;
    std::string GetControllerStatus(int controller_index) const;
    float ApplyDeadzone(float value, float deadzone) const;
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
    void ExecuteChordAction(const XInputSharedState::Chord& chord, DWORD user_index);
    std::string GetChordButtonNames(WORD buttons) const;

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
void UpdateXInputState(DWORD user_index, const XINPUT_STATE* state);
void IncrementEventCounter(const std::string& event_type);
void ProcessChordDetection(DWORD user_index, WORD button_state);
void CheckAndHandleScreenshot();

} // namespace display_commander::widgets::xinput_widget
