/*
 * Example usage of the input blocking system
 * This file demonstrates how to use the input blocking functionality
 */

#include "input_blocking.hpp"

namespace display_commander::input_blocking::examples
{
    // Example 1: Block all input when overlay is open
    void block_input_for_overlay()
    {
        // Block all input globally
        block_all_input();

        // Or block specific types
        set_input_blocking(true, true, false); // keyboard + mouse, not global
    }

    // Example 2: Block only specific keys
    void block_specific_keys()
    {
        auto& blocker = InputBlocker::get_instance();

        // Block function keys
        blocker.block_key(VK_F1);
        blocker.block_key(VK_F2);
        blocker.block_key(VK_F3);

        // Block escape key
        blocker.block_key(VK_ESCAPE);

        // Block arrow keys
        blocker.block_key(VK_LEFT);
        blocker.block_key(VK_RIGHT);
        blocker.block_key(VK_UP);
        blocker.block_key(VK_DOWN);
    }

    // Example 3: Block mouse buttons only
    void block_mouse_buttons()
    {
        auto& blocker = InputBlocker::get_instance();

        // Block left and right mouse buttons
        blocker.block_mouse_button(VK_LBUTTON);
        blocker.block_mouse_button(VK_RBUTTON);

        // Or block all mouse input
        blocker.set_mouse_blocking(true);
    }

    // Example 4: Conditional blocking based on application state
    void conditional_blocking_example()
    {
        auto& blocker = InputBlocker::get_instance();

        // Check if we should block input
        bool should_block = true; // Your condition here

        if (should_block)
        {
            // Block keyboard but allow mouse
            blocker.set_keyboard_blocking(true);
            blocker.set_mouse_blocking(false);
        }
        else
        {
            // Unblock everything
            blocker.set_keyboard_blocking(false);
            blocker.set_mouse_blocking(false);
        }
    }

    // Example 5: Toggle blocking on/off
    void toggle_input_blocking()
    {
        static bool blocked = false;

        if (blocked)
        {
            unblock_all_input();
            blocked = false;
        }
        else
        {
            block_all_input();
            blocked = true;
        }
    }

    // Example 6: Block input for a specific duration
    void temporary_input_blocking()
    {
        // Block all input
        block_all_input();

        // In a real scenario, you would use a timer or async operation
        // For this example, we'll just show the concept
        // std::this_thread::sleep_for(std::chrono::seconds(5));

        // Unblock after duration
        unblock_all_input();
    }

    // Example 7: Check blocking status
    void check_blocking_status()
    {
        auto& blocker = InputBlocker::get_instance();

        if (blocker.is_global_blocked())
        {
            // All input is blocked
        }
        else if (blocker.is_keyboard_blocked())
        {
            // Only keyboard is blocked
        }
        else if (blocker.is_mouse_blocked())
        {
            // Only mouse is blocked
        }
        else
        {
            // No input is blocked
        }
    }
}
