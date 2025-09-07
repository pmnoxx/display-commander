/*
 * Copyright (C) 2024 Display Commander
 * Input blocking system using Windows hooks
 */

#pragma once

#include <Windows.h>
#include <atomic>
#include <unordered_set>

namespace display_commander::input_blocking
{
    class InputBlocker
    {
    public:
        InputBlocker();
        ~InputBlocker();

        // Initialize the input blocking system
        bool initialize();

        // Cleanup the input blocking system
        void cleanup();

        // Control input blocking
        void set_keyboard_blocking(bool enabled);
        void set_mouse_blocking(bool enabled);
        void set_global_blocking(bool enabled);

        // Block specific keys/mouse buttons
        void block_key(int vk_code);
        void unblock_key(int vk_code);
        void block_mouse_button(int button);
        void unblock_mouse_button(int button);

        // Check if input is currently blocked
        bool is_keyboard_blocked() const { return _keyboard_blocking.load(); }
        bool is_mouse_blocked() const { return _mouse_blocking.load(); }
        bool is_global_blocked() const { return _global_blocking.load(); }

        // Get singleton instance
        static InputBlocker& get_instance();

    private:
        // Hook procedures
        static LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);

        // Helper functions
        bool should_block_key(int vk_code) const;
        bool should_block_mouse_action(WPARAM wParam) const;
        void log_blocked_input(const char* type, int code) const;

        // Hook handles
        HHOOK _keyboard_hook = nullptr;
        HHOOK _mouse_hook = nullptr;

        // Blocking state
        std::atomic<bool> _keyboard_blocking{false};
        std::atomic<bool> _mouse_blocking{false};
        std::atomic<bool> _global_blocking{false};
        std::atomic<bool> _initialized{false};

        // Specific key/button blocking
        std::unordered_set<int> _blocked_keys;
        std::unordered_set<int> _blocked_mouse_buttons;

        // Thread safety
        mutable CRITICAL_SECTION _cs;
    };

    // Convenience functions
    void initialize_input_blocking();
    void cleanup_input_blocking();
    void set_input_blocking(bool keyboard, bool mouse, bool global = false);

    // Example usage functions
    void block_all_input();
    void unblock_all_input();
    void block_escape_key();
    void unblock_escape_key();

    // Integration with settings
    void update_input_blocking_from_settings();
}
