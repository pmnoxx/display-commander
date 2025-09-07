# Input Blocking System

This module provides a Windows hook-based input blocking system that allows you to block mouse and keyboard input independently of Reshade's input system.

## Features

- **Global Input Blocking**: Block all keyboard and mouse input
- **Selective Blocking**: Block only keyboard or only mouse input
- **Key-Specific Blocking**: Block individual keys or mouse buttons
- **Thread-Safe**: Uses critical sections for thread safety
- **Automatic Cleanup**: Properly uninstalls hooks on DLL unload

## Usage

### Basic Usage

```cpp
#include "input_blocking/input_blocking.hpp"

// Block all input
display_commander::input_blocking::block_all_input();

// Unblock all input
display_commander::input_blocking::unblock_all_input();

// Block only keyboard input
display_commander::input_blocking::set_input_blocking(true, false);

// Block only mouse input
display_commander::input_blocking::set_input_blocking(false, true);

// Block specific key (Escape)
display_commander::input_blocking::block_escape_key();
display_commander::input_blocking::unblock_escape_key();
```

### Advanced Usage

```cpp
// Get the input blocker instance
auto& blocker = display_commander::input_blocking::InputBlocker::get_instance();

// Block specific keys
blocker.block_key(VK_F1);
blocker.block_key(VK_F2);
blocker.unblock_key(VK_F1);

// Block specific mouse buttons
blocker.block_mouse_button(VK_LBUTTON);
blocker.block_mouse_button(VK_RBUTTON);

// Check blocking status
if (blocker.is_keyboard_blocked()) {
    // Keyboard input is being blocked
}

if (blocker.is_mouse_blocked()) {
    // Mouse input is being blocked
}
```

## How It Works

The system uses Windows low-level hooks (`WH_KEYBOARD_LL` and `WH_MOUSE_LL`) to intercept input messages before they reach applications. When a message should be blocked, the hook procedure returns `1` to prevent the message from being processed.

### Hook Installation

Hooks are automatically installed in `DLL_THREAD_ATTACH` and cleaned up in `DLL_THREAD_DETACH` and `DLL_PROCESS_DETACH`.

### Message Types Blocked

**Keyboard Messages:**
- `WM_KEYDOWN` / `WM_KEYUP`
- `WM_SYSKEYDOWN` / `WM_SYSKEYUP`
- All keys in the range `WM_KEYFIRST` to `WM_KEYLAST`

**Mouse Messages:**
- `WM_LBUTTONDOWN` / `WM_LBUTTONUP`
- `WM_RBUTTONDOWN` / `WM_RBUTTONUP`
- `WM_MBUTTONDOWN` / `WM_MBUTTONUP`
- `WM_XBUTTONDOWN` / `WM_XBUTTONUP`
- `WM_MOUSEWHEEL`
- All messages in the range `WM_MOUSEFIRST` to `WM_MOUSELAST`

## Thread Safety

The system is thread-safe and uses:
- `std::atomic<bool>` for blocking state flags
- `CRITICAL_SECTION` for protecting the blocked keys/buttons sets
- Proper hook cleanup to prevent crashes

## Performance Considerations

- Low-level hooks can impact system performance if not used carefully
- The system logs blocked input every 100th occurrence to avoid log spam
- Hooks are only active when blocking is enabled

## Integration with Reshade

This system works independently of Reshade's input blocking system. You can use both systems together if needed:

```cpp
// Use Reshade's system for overlay input blocking
runtime->block_input_next_frame();

// Use our system for background input blocking
display_commander::input_blocking::block_all_input();
```

## Error Handling

The system logs errors to Reshade's log system:
- Hook installation failures
- Hook removal failures
- Blocked input events (throttled)

## Limitations

- Only works on Windows
- Requires appropriate privileges for low-level hooks
- May not work with some security software
- Global hooks can be detected by some applications
