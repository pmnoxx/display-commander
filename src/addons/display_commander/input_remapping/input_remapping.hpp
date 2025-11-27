/*
 * Copyright (C) 2024 Display Commander
 * Gamepad to keyboard input remapping system
 */

#pragma once

#include <Windows.h>
#include <array>
#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>
#include <xinput.h>


// Guide button constant (not defined in standard XInput headers)
#ifndef XINPUT_GAMEPAD_GUIDE
#define XINPUT_GAMEPAD_GUIDE 0x0400
#endif

namespace display_commander::input_remapping {
// Default chord type enum
enum class DefaultChordType : int {
    VolumeUp = 0,
    VolumeDown = 1,
    MuteUnmute = 2,
    PerformanceOverlay = 3,
    Screenshot = 4,
    IncreaseGameSpeed = 5,
    DecreaseGameSpeed = 6,
    DisplayCommanderUI = 7,
    Count
};

// Remap type enum
enum class RemapType : int {
    Keyboard = 0, // Map to keyboard key
    Gamepad = 1,  // Map to gamepad button
    Action = 2,   // Map to action (e.g., screenshot)
    Count
};

// Keyboard input methods
enum class KeyboardInputMethod : int {
    SendInput = 0,   // Modern SendInput API
    KeybdEvent = 1,  // Legacy keybd_event API
    SendMessage = 2, // SendMessage to active window
    PostMessage = 3, // PostMessage to active window
    Count
};

// Remapping configuration for a single button
struct ButtonRemap {
    WORD gamepad_button;                       // XInput button constant (source)
    RemapType remap_type;                     // Type of remapping (Keyboard, Gamepad, Action)

    // Keyboard remapping fields (used when remap_type == Keyboard)
    int keyboard_vk;                           // Virtual key code
    std::string keyboard_name;                 // Human-readable key name
    KeyboardInputMethod input_method;          // Method to use for input

    // Gamepad remapping fields (used when remap_type == Gamepad)
    WORD gamepad_target_button;                // Target XInput button constant

    // Action remapping fields (used when remap_type == Action)
    std::string action_name;                   // Action name (e.g., "screenshot")

    bool enabled;                              // Whether this remap is active
    bool hold_mode;                            // If true, holds key/button while button pressed
    bool chord_mode;                           // If true, remapping only works when home button is also pressed
    bool is_default_chord;                     // If true, this remap was added by default chords feature
    std::atomic<bool> is_pressed{false};       // Current press state
    std::atomic<ULONGLONG> last_press_time{0}; // Last press timestamp
    std::atomic<uint64_t> trigger_count{0};    // Number of times this remapping was triggered

    ButtonRemap() = default;

    // Constructor for keyboard remapping (backward compatible)
    ButtonRemap(WORD btn, int vk, const std::string &name, bool en = true,
                KeyboardInputMethod method = KeyboardInputMethod::SendInput, bool hold = true, bool chord = false)
        : gamepad_button(btn), remap_type(RemapType::Keyboard), keyboard_vk(vk), keyboard_name(name),
          gamepad_target_button(0), action_name(""), enabled(en), input_method(method), hold_mode(hold), chord_mode(chord), is_default_chord(false) {}

    // Constructor for gamepad remapping
    ButtonRemap(WORD btn, WORD target_btn, bool en = true, bool hold = true, bool chord = false)
        : gamepad_button(btn), remap_type(RemapType::Gamepad), keyboard_vk(0), keyboard_name(""),
          gamepad_target_button(target_btn), action_name(""), enabled(en),
          input_method(KeyboardInputMethod::SendInput), hold_mode(hold), chord_mode(chord), is_default_chord(false) {}

    // Constructor for action remapping
    ButtonRemap(WORD btn, const std::string &action, bool en = true, bool hold = false, bool chord = false)
        : gamepad_button(btn), remap_type(RemapType::Action), keyboard_vk(0), keyboard_name(""),
          gamepad_target_button(0), action_name(action), enabled(en),
          input_method(KeyboardInputMethod::SendInput), hold_mode(hold), chord_mode(chord), is_default_chord(false) {}

    // Copy constructor
    ButtonRemap(const ButtonRemap &other)
        : gamepad_button(other.gamepad_button), remap_type(other.remap_type),
          keyboard_vk(other.keyboard_vk), keyboard_name(other.keyboard_name),
          gamepad_target_button(other.gamepad_target_button), action_name(other.action_name),
          enabled(other.enabled), input_method(other.input_method), hold_mode(other.hold_mode),
          chord_mode(other.chord_mode), is_default_chord(other.is_default_chord),
          is_pressed(other.is_pressed.load()), last_press_time(other.last_press_time.load()),
          trigger_count(other.trigger_count.load()) {}

    // Assignment operator
    ButtonRemap &operator=(const ButtonRemap &other) {
        if (this != &other) {
            gamepad_button = other.gamepad_button;
            remap_type = other.remap_type;
            keyboard_vk = other.keyboard_vk;
            keyboard_name = other.keyboard_name;
            gamepad_target_button = other.gamepad_target_button;
            action_name = other.action_name;
            enabled = other.enabled;
            input_method = other.input_method;
            hold_mode = other.hold_mode;
            chord_mode = other.chord_mode;
            is_default_chord = other.is_default_chord;
            is_pressed.store(other.is_pressed.load());
            last_press_time.store(other.last_press_time.load());
            trigger_count.store(other.trigger_count.load());
        }
        return *this;
    }
};

// Main remapping manager class
class InputRemapper {
  public:
    InputRemapper();
    ~InputRemapper();

    // Initialize the remapping system
    bool initialize();

    // Cleanup the remapping system
    void cleanup();

    // Process gamepad input and apply remappings
    void process_gamepad_input(DWORD user_index, XINPUT_STATE *state);

    // Add/remove button remappings
    void add_button_remap(const ButtonRemap &remap);
    void remove_button_remap(WORD gamepad_button);
    void clear_all_remaps();

    // Enable/disable remapping system
    void set_remapping_enabled(bool enabled);
    bool is_remapping_enabled() const { return _remapping_enabled.load(); }

    // Block gamepad input to game when home button is pressed (prevents accidental shortcuts)
    void set_block_input_on_home_button(bool enabled);
    bool is_block_input_on_home_button() const { return _block_input_on_home_button.load(); }

    // Set default input method
    void set_default_input_method(KeyboardInputMethod method);
    KeyboardInputMethod get_default_input_method() const { return _default_input_method; }

    // Get all current remappings
    const std::vector<ButtonRemap> &get_remappings() const { return _remappings; }

    // Get remapping for specific button
    const ButtonRemap *get_button_remap(WORD gamepad_button) const;

    // Update remapping settings
    void update_remap(WORD gamepad_button, int keyboard_vk, const std::string &keyboard_name,
                      KeyboardInputMethod method, bool hold_mode, bool chord_mode = false);

    // Update remapping settings (overloaded for different remap types)
    void update_remap_keyboard(WORD gamepad_button, int keyboard_vk, const std::string &keyboard_name,
                               KeyboardInputMethod method, bool hold_mode, bool chord_mode = false);
    void update_remap_gamepad(WORD gamepad_button, WORD target_button, bool hold_mode, bool chord_mode = false);
    void update_remap_action(WORD gamepad_button, const std::string &action_name, bool hold_mode, bool chord_mode = false);

    // Settings management
    void load_settings();
    void save_settings();

    // Default chords initialization (generic system)
    void add_default_chords();
    void remove_default_chords();
    void add_default_chord_type(DefaultChordType chord_type);
    void remove_default_chord_type(DefaultChordType chord_type);

    // Get singleton instance
    static InputRemapper &get_instance();

  private:
    // Keyboard input methods
    bool send_keyboard_input_sendinput(int vk_code, bool key_down);
    bool send_keyboard_input_keybdevent(int vk_code, bool key_down);
    bool send_keyboard_input_sendmessage(int vk_code, bool key_down);
    bool send_keyboard_input_postmessage(int vk_code, bool key_down);

    // Helper functions
    std::string get_button_name(WORD button) const;
    std::string get_keyboard_name(int vk_code) const;
    int get_vk_code_from_name(const std::string &name) const;
    HWND get_active_window() const;

    // Trigger counter functions
    void increment_trigger_count(WORD gamepad_button);
    uint64_t get_trigger_count(WORD gamepad_button) const;

    // Button state tracking
    void update_button_states(DWORD user_index, WORD button_state);
    void handle_button_press(WORD gamepad_button, DWORD user_index, WORD current_button_state);
    void handle_button_release(WORD gamepad_button, DWORD user_index, WORD current_button_state);

    // Gamepad remapping - modify XINPUT_STATE
    void apply_gamepad_remapping(DWORD user_index, XINPUT_STATE *state);

    // Action execution
    void execute_action(const std::string &action_name);

    // Settings
    std::atomic<bool> _remapping_enabled{false};
    std::atomic<bool> _block_input_on_home_button{false}; // Block gamepad input to game when home button is pressed
    std::atomic<bool> _initialized{false};
    KeyboardInputMethod _default_input_method{KeyboardInputMethod::SendInput};

    // Remapping data
    std::vector<ButtonRemap> _remappings;
    std::unordered_map<WORD, size_t> _button_to_remap_index;

    // Button state tracking for each controller
    std::array<std::atomic<WORD>, XUSER_MAX_COUNT> _previous_button_states;
    std::array<std::atomic<WORD>, XUSER_MAX_COUNT> _current_button_states;

    // Guide solo-press tracking for Display Commander UI toggle
    std::array<std::atomic<bool>, XUSER_MAX_COUNT> _guide_solo_candidate;
    std::array<std::atomic<bool>, XUSER_MAX_COUNT> _guide_other_button_pressed;

    // Thread safety
    mutable SRWLOCK _srwlock = SRWLOCK_INIT;
};

// Global functions for integration
void initialize_input_remapping();
void cleanup_input_remapping();
void process_gamepad_input_for_remapping(DWORD user_index, XINPUT_STATE *state);

// Utility functions
std::string get_keyboard_input_method_name(KeyboardInputMethod method);
std::string get_remap_type_name(RemapType type);
std::vector<std::string> get_available_keyboard_input_methods();
std::vector<std::string> get_available_gamepad_buttons();
std::vector<std::string> get_available_keyboard_keys();
std::vector<std::string> get_available_actions();
} // namespace display_commander::input_remapping
