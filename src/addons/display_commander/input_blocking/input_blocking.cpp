/*
 * Copyright (C) 2024 Display Commander
 * Input blocking system implementation
 */

#include "input_blocking.hpp"
#include "../../../external/reshade/include/reshade.hpp"
#include "../globals.hpp"


namespace display_commander::input_blocking
{
    InputBlocker& InputBlocker::get_instance()
    {
        static InputBlocker instance;
        return instance;
    }

    InputBlocker::InputBlocker()
    {
        InitializeCriticalSection(&_cs);
    }

    InputBlocker::~InputBlocker()
    {
        cleanup();
        DeleteCriticalSection(&_cs);
    }

    bool InputBlocker::initialize()
    {
        if (_initialized.load())
            return true;

        // Install keyboard hook
        _keyboard_hook = SetWindowsHookEx(
            WH_KEYBOARD_LL,
            KeyboardHookProc,
            GetModuleHandle(nullptr),
            0
        );

        if (!_keyboard_hook)
        {
            reshade::log::message(reshade::log::level::error,
                "Failed to install keyboard hook");
            return false;
        }

        // Install mouse hook
        _mouse_hook = SetWindowsHookEx(
            WH_MOUSE_LL,
            MouseHookProc,
            GetModuleHandle(nullptr),
            0
        );

        if (!_mouse_hook)
        {
            reshade::log::message(reshade::log::level::error,
                "Failed to install mouse hook");

            // Cleanup keyboard hook if mouse hook failed
            if (_keyboard_hook)
            {
                UnhookWindowsHookEx(_keyboard_hook);
                _keyboard_hook = nullptr;
            }
            return false;
        }

        _initialized.store(true);
        reshade::log::message(reshade::log::level::info,
            "Input blocking hooks installed successfully");

        return true;
    }

    void InputBlocker::cleanup()
    {
        if (!_initialized.load())
            return;

        // Remove hooks
        if (_keyboard_hook)
        {
            UnhookWindowsHookEx(_keyboard_hook);
            _keyboard_hook = nullptr;
        }

        if (_mouse_hook)
        {
            UnhookWindowsHookEx(_mouse_hook);
            _mouse_hook = nullptr;
        }

        // Reset state
        _keyboard_blocking.store(false);
        _mouse_blocking.store(false);
        _global_blocking.store(false);
        _initialized.store(false);

        // Clear blocked keys/buttons
        EnterCriticalSection(&_cs);
        _blocked_keys.clear();
        _blocked_mouse_buttons.clear();
        LeaveCriticalSection(&_cs);

        reshade::log::message(reshade::log::level::info,
            "Input blocking hooks removed");
    }

    void InputBlocker::set_keyboard_blocking(bool enabled)
    {
        _keyboard_blocking.store(enabled);
        reshade::log::message(reshade::log::level::info,
            "Keyboard blocking enabled/disabled");
    }

    void InputBlocker::set_mouse_blocking(bool enabled)
    {
        _mouse_blocking.store(enabled);
        reshade::log::message(reshade::log::level::info,
            "Mouse blocking enabled/disabled");
    }

    void InputBlocker::set_global_blocking(bool enabled)
    {
        _global_blocking.store(enabled);
        reshade::log::message(reshade::log::level::info,
            "Global input blocking enabled/disabled");
    }

    void InputBlocker::block_key(int vk_code)
    {
        EnterCriticalSection(&_cs);
        _blocked_keys.insert(vk_code);
        LeaveCriticalSection(&_cs);

        reshade::log::message(reshade::log::level::info,
            "Blocking key");
    }

    void InputBlocker::unblock_key(int vk_code)
    {
        EnterCriticalSection(&_cs);
        _blocked_keys.erase(vk_code);
        LeaveCriticalSection(&_cs);

        reshade::log::message(reshade::log::level::info,
            "Unblocking key");
    }

    void InputBlocker::block_mouse_button(int button)
    {
        EnterCriticalSection(&_cs);
        _blocked_mouse_buttons.insert(button);
        LeaveCriticalSection(&_cs);

        reshade::log::message(reshade::log::level::info,
            "Blocking mouse button");
    }

    void InputBlocker::unblock_mouse_button(int button)
    {
        EnterCriticalSection(&_cs);
        _blocked_mouse_buttons.erase(button);
        LeaveCriticalSection(&_cs);

        reshade::log::message(reshade::log::level::info,
            "Unblocking mouse button");
    }

    bool InputBlocker::should_block_key(int vk_code) const
    {
        // Check global blocking first
        if (_global_blocking.load())
            return true;

        // Check keyboard blocking
        if (_keyboard_blocking.load())
            return true;

        // Check specific key blocking
        EnterCriticalSection(&_cs);
        bool blocked = _blocked_keys.find(vk_code) != _blocked_keys.end();
        LeaveCriticalSection(&_cs);

        return blocked;
    }

    bool InputBlocker::should_block_mouse_action(WPARAM wParam) const
    {
        // Check global blocking first
        if (_global_blocking.load())
            return true;

        // Check mouse blocking
        if (_mouse_blocking.load())
            return true;

        // Map wParam to button codes
        int button = -1;
        switch (wParam)
        {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
            button = VK_LBUTTON;
            break;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
            button = VK_RBUTTON;
            break;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
            button = VK_MBUTTON;
            break;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
            // X buttons would need additional processing
            button = VK_XBUTTON1; // Simplified for now
            break;
        }

        if (button != -1)
        {
            EnterCriticalSection(&_cs);
            bool blocked = _blocked_mouse_buttons.find(button) != _blocked_mouse_buttons.end();
            LeaveCriticalSection(&_cs);
            return blocked;
        }

        return false;
    }

    void InputBlocker::log_blocked_input(const char* type, int code) const
    {
        static std::atomic<int> log_counter{0};
        int count = log_counter.fetch_add(1);

        // Only log every 100th blocked input to avoid spam
        if (count % 100 == 0)
        {
            reshade::log::message(reshade::log::level::debug,
                "Blocked input");
        }
    }

    LRESULT CALLBACK InputBlocker::KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
    {
        if (nCode >= 0)
        {
            KBDLLHOOKSTRUCT* pKeyboard = (KBDLLHOOKSTRUCT*)lParam;
            InputBlocker& blocker = get_instance();

            if (blocker.should_block_key(pKeyboard->vkCode))
            {
                blocker.log_blocked_input("keyboard", pKeyboard->vkCode);
                return 1; // Block the message
            }
        }

        return CallNextHookEx(get_instance()._keyboard_hook, nCode, wParam, lParam);
    }

    LRESULT CALLBACK InputBlocker::MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
    {
        if (nCode >= 0)
        {
            MSLLHOOKSTRUCT* pMouse = (MSLLHOOKSTRUCT*)lParam;
            InputBlocker& blocker = get_instance();

            if (blocker.should_block_mouse_action(wParam))
            {
                blocker.log_blocked_input("mouse", static_cast<int>(wParam));
                return 1; // Block the message
            }
        }

        return CallNextHookEx(get_instance()._mouse_hook, nCode, wParam, lParam);
    }

    // Convenience functions
    void initialize_input_blocking()
    {
        InputBlocker::get_instance().initialize();
    }

    void cleanup_input_blocking()
    {
        InputBlocker::get_instance().cleanup();
    }

    void set_input_blocking(bool keyboard, bool mouse, bool global)
    {
        InputBlocker& blocker = InputBlocker::get_instance();
        blocker.set_keyboard_blocking(keyboard);
        blocker.set_mouse_blocking(mouse);
        blocker.set_global_blocking(global);
    }

    // Example usage functions
    void block_all_input()
    {
        InputBlocker& blocker = InputBlocker::get_instance();
        blocker.set_global_blocking(true);
    }

    void unblock_all_input()
    {
        InputBlocker& blocker = InputBlocker::get_instance();
        blocker.set_global_blocking(false);
        blocker.set_keyboard_blocking(false);
        blocker.set_mouse_blocking(false);
    }

    void block_escape_key()
    {
        InputBlocker& blocker = InputBlocker::get_instance();
        blocker.block_key(VK_ESCAPE);
    }

    void unblock_escape_key()
    {
        InputBlocker& blocker = InputBlocker::get_instance();
        blocker.unblock_key(VK_ESCAPE);
    }

    // Integration with settings
    void update_input_blocking_from_settings()
    {
        if (s_block_input_without_reshade.load())
        {
            // Enable input blocking
            InputBlocker& blocker = InputBlocker::get_instance();
            blocker.set_global_blocking(true);
        }
        else
        {
            // Disable input blocking
            InputBlocker& blocker = InputBlocker::get_instance();
            blocker.set_global_blocking(false);
        }
    }
}
