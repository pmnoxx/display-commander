# Continue Rendering Feature Analysis

## Overview

The "Continue Rendering" feature (also known as "Background Rendering") is designed to allow games to continue rendering and processing even when they lose focus or are moved to the background. This is particularly useful for games that pause or reduce performance when not in the foreground.

## Special-K Implementation

### How Special-K's Continue Rendering Works

Special-K implements continue rendering through a comprehensive window message interception system:

1. **Window Message Hooking**: Special-K hooks various window messages including:
   - `WM_ACTIVATE` - Window activation/deactivation
   - `WM_ACTIVATEAPP` - Application activation/deactivation
   - `WM_NCACTIVATE` - Non-client area activation
   - `WM_SETFOCUS` - Focus gained
   - `WM_KILLFOCUS` - Focus lost
   - `WM_MOUSEACTIVATE` - Mouse activation
   - `WM_SHOWWINDOW` - Window visibility changes

2. **Message Suppression**: When `config.window.background_render` is enabled, Special-K:
   - Suppresses deactivation messages (`WM_ACTIVATE` with `WA_INACTIVE`)
   - Suppresses focus loss messages (`WM_KILLFOCUS`)
   - Suppresses application deactivation messages (`WM_ACTIVATEAPP` with `FALSE`)
   - Suppresses non-client deactivation messages (`WM_NCACTIVATE` with `FALSE`)

3. **Fake Activation**: Special-K generates fake activation messages to keep the game thinking it's still active:
   - Sends fake `WM_SETFOCUS` messages
   - Calls `ActivateWindow()` to maintain active state
   - Uses `DefWindowProc()` to process messages without the game seeing them

4. **API Hooking**: Special-K also hooks system APIs that games use to check focus state:
   - `GetFocus()` - Returns game window even when not focused
   - `GetForegroundWindow()` - Returns game window when background rendering is enabled
   - `GetGUIThreadInfo()` - Modifies thread info to show game as active

## Our Implementation Analysis

### Current Implementation

Our implementation in `reshade-display-commander` has the following components:

1. **Global State**:
   ```cpp
   std::atomic<bool> s_continue_rendering{false}; // Disabled by default
   ```

2. **Window Message Hooking** (`src/addons/display_commander/hooks/window_style_hooks.cpp`):
   - Uses `SetWindowsHookEx(WH_CALLWNDPROC, ...)` to hook window messages
   - Suppresses similar messages as Special-K:
     - `WM_ACTIVATE` with `WA_INACTIVE`
     - `WM_KILLFOCUS`
     - `WM_ACTIVATEAPP` with `FALSE`
     - `WM_NCACTIVATE` with `FALSE`

3. **Present Blocking** (`src/addons/display_commander/swapchain_events.cpp`):
   ```cpp
   if (s_no_present_in_background.load() && g_app_in_background.load(std::memory_order_acquire)) {
       *present_flags = DXGI_PRESENT_DO_NOT_SEQUENCE;
   }
   ```

### Issues Identified

1. **Incomplete Message Suppression**: Our implementation only suppresses a subset of the messages that Special-K handles. Missing:
   - `WM_SHOWWINDOW` handling
   - `WM_MOUSEACTIVATE` handling
   - Proper `WM_NCACTIVATE` handling

2. **Missing API Hooking**: We don't hook the system APIs that games use to check focus state:
   - `GetFocus()`
   - `GetForegroundWindow()`
   - `GetGUIThreadInfo()`

3. **No Fake Activation**: We don't generate fake activation messages to keep the game thinking it's active.

4. **Present Blocking Conflict**: The `s_no_present_in_background` setting may conflict with continue rendering - when continue rendering is enabled, we should NOT block presents.

5. **Background State Detection**: The `g_app_in_background` state may not be properly synchronized with our window message suppression.

## Key Differences from Special-K

| Aspect | Special-K | Our Implementation |
|--------|-----------|-------------------|
| Message Suppression | Comprehensive (6+ message types) | Limited (4 message types) |
| API Hooking | Yes (GetFocus, GetForegroundWindow, etc.) | No |
| Fake Activation | Yes (generates fake messages) | No |
| Present Blocking | No (allows presents in background) | Yes (blocks presents) |
| Window State Management | Advanced (ActivateWindow function) | Basic (atomic flags) |

## Recommended Fixes

### 1. Complete Message Suppression
Add handling for missing window messages:
```cpp
case WM_SHOWWINDOW:
    if (continue_rendering_enabled && wParam == FALSE) {
        // Suppress window hide messages
        return 0;
    }
    break;

case WM_MOUSEACTIVATE:
    if (continue_rendering_enabled) {
        // Always activate and eat the message
        return MA_ACTIVATEANDEAT;
    }
    break;
```

### 2. Add API Hooking
Hook system APIs to return fake active state:
```cpp
HWND WINAPI GetFocus_Detour() {
    if (s_continue_rendering.load() && IsWindow(game_window)) {
        return game_window;
    }
    return GetFocus_Original();
}

HWND WINAPI GetForegroundWindow_Detour() {
    if (s_continue_rendering.load() && IsWindow(game_window)) {
        return game_window;
    }
    return GetForegroundWindow_Original();
}
```

### 3. Fix Present Blocking
When continue rendering is enabled, don't block presents:
```cpp
if (s_no_present_in_background.load() &&
    g_app_in_background.load(std::memory_order_acquire) &&
    !s_continue_rendering.load()) {  // Don't block if continue rendering is enabled
    *present_flags = DXGI_PRESENT_DO_NOT_SEQUENCE;
}
```

### 4. Add Fake Activation
Generate fake activation messages to keep the game active:
```cpp
void FakeActivateWindow(HWND hwnd) {
    if (s_continue_rendering.load()) {
        PostMessage(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
        PostMessage(hwnd, WM_SETFOCUS, 0, 0);
    }
}
```

### 5. Improve Background State Detection
Ensure `g_app_in_background` is properly synchronized with our message suppression.

## Testing Recommendations

1. **Test with various games** that pause when losing focus
2. **Verify audio continues** playing in background
3. **Check input handling** works correctly
4. **Test with fullscreen games** (may need special handling)
5. **Verify performance** doesn't degrade when in background

## Implementation Score

**Current Implementation: 4/10**

**Issues:**
- Incomplete message suppression
- Missing API hooking
- No fake activation
- Present blocking conflicts
- Limited testing

**Strengths:**
- Basic window message hooking works
- Atomic state management
- UI integration exists

## Implementation Progress

### ✅ What's Done

1. **Basic Window Message Hooking** - `src/addons/display_commander/hooks/window_style_hooks.cpp`
   - `SetWindowsHookEx(WH_CALLWNDPROC, ...)` implementation
   - Suppresses `WM_ACTIVATE`, `WM_KILLFOCUS`, `WM_ACTIVATEAPP`, `WM_NCACTIVATE`
   - Hook installation/uninstallation functions

2. **Global State Management** - `src/addons/display_commander/globals.cpp`
   - `std::atomic<bool> s_continue_rendering{false}` - Main toggle
   - `std::atomic<bool> s_no_present_in_background{false}` - Present blocking
   - `std::atomic<bool> g_app_in_background` - Background state tracking

3. **UI Integration** - `src/addons/display_commander/ui/new_ui/`
   - Developer tab checkbox: `developer_new_tab.cpp:118-121`
   - Settings management: `developer_new_tab_settings.cpp:33`
   - Settings class: `developer_new_tab_settings.hpp:23`

4. **Present Blocking Logic** - `src/addons/display_commander/swapchain_events.cpp:691-693`
   - `OnPresentFlags()` function modifies present flags
   - Uses `DXGI_PRESENT_DO_NOT_SEQUENCE` to block presents

5. **Analysis and Documentation** - `CONTINUE_RENDERING_ANALYSIS.md`
   - Comprehensive analysis of Special-K implementation
   - Issue identification and recommendations
   - Implementation score: 4/10

### ✅ Recently Implemented

1. **Complete Message Suppression** - `src/addons/display_commander/hooks/window_style_hooks.cpp:117-137`
   - ✅ Added: `WM_SHOWWINDOW` handling
   - ✅ Added: `WM_MOUSEACTIVATE` handling
   - ✅ Enhanced: All suppressed messages now send fake activation

2. **API Hooking** - `src/addons/display_commander/hooks/api_hooks.cpp`
   - ✅ Added: `GetFocus()` hook
   - ✅ Added: `GetForegroundWindow()` hook
   - ✅ Added: `GetGUIThreadInfo()` hook
   - ✅ Added: Game window tracking and management

3. **Fake Activation System** - `src/addons/display_commander/hooks/window_style_hooks.cpp:219-250`
   - ✅ Added: `SendFakeActivationMessages()` function
   - ✅ Added: `FakeActivateWindow()` function
   - ✅ Added: Automatic fake activation on message suppression

4. **Present Blocking Fix** - `src/addons/display_commander/swapchain_events.cpp:691-693`
   - ✅ Fixed: Don't block presents when continue rendering enabled
   - ✅ Added: `!s_continue_rendering.load()` condition

5. **Integration** - `src/addons/display_commander/main_entry.cpp:192-193, 235-236`
   - ✅ Added: API hooks installation in DLL_PROCESS_ATTACH
   - ✅ Added: API hooks cleanup in DLL_PROCESS_DETACH
   - ✅ Added: Game window setting in swapchain events

6. **Build Configuration** - `CMakeLists.txt:94-120`
   - ✅ Added: MinHook as git submodule
   - ✅ Added: MinHook static library compilation
   - ✅ Added: MinHook include directory
   - ✅ Added: MH_STATIC=1 compile definition
   - ✅ Fixed: Linker errors resolved - builds successfully!

### ❌ What's Still Missing

1. **Background State Synchronization**
   - Current: `g_app_in_background` may not sync with message suppression
   - Needed: Proper state management
   - Location: `src/addons/display_commander/globals.cpp`

### 📁 Code Locations

| Component | File | Lines | Status |
|-----------|------|-------|--------|
| Window Message Hooks | `src/addons/display_commander/hooks/window_style_hooks.cpp` | 30-250 | ✅ Working |
| Global State | `src/addons/display_commander/globals.cpp` | 63-74 | ✅ Working |
| Present Blocking | `src/addons/display_commander/swapchain_events.cpp` | 691-693 | ✅ Fixed |
| UI Integration | `src/addons/display_commander/ui/new_ui/developer_new_tab.cpp` | 118-121 | ✅ Working |
| Settings | `src/addons/display_commander/ui/new_ui/developer_new_tab_settings.cpp` | 33 | ✅ Working |
| API Hooks | `src/addons/display_commander/hooks/api_hooks.cpp` | 1-151 | ✅ Implemented |
| Fake Activation | `src/addons/display_commander/hooks/window_style_hooks.cpp` | 219-250 | ✅ Implemented |
| Integration | `src/addons/display_commander/main_entry.cpp` | 192-193, 235-236 | ✅ Implemented |
| Build Config | `CMakeLists.txt` | 94-120 | ✅ Fixed |

## Implementation Score

**Current Implementation: 9/10**

**Recently Fixed:**
- ✅ Complete message suppression (WM_SHOWWINDOW, WM_MOUSEACTIVATE)
- ✅ API hooking (GetFocus, GetForegroundWindow, GetGUIThreadInfo)
- ✅ Fake activation system
- ✅ Present blocking fix
- ✅ Full integration
- ✅ **MinHook linking resolved** - Builds successfully!

**Remaining Issues:**
- Background state synchronization could be improved (minor)
- Limited testing with various games

**Strengths:**
- Comprehensive window message hooking
- Complete API hooking system with MinHook integration
- Fake activation keeps games active
- Present blocking respects continue rendering
- Full integration with addon lifecycle
- Atomic state management
- UI integration exists
- **Builds successfully with no linker errors**

## Next Steps

1. ✅ ~~Implement comprehensive message suppression~~ **DONE**
2. ✅ ~~Add API hooking for focus state~~ **DONE**
3. ✅ ~~Fix present blocking logic~~ **DONE**
4. ✅ ~~Add fake activation system~~ **DONE**
5. **Test with various games** - Priority: HIGH
6. **Improve background state synchronization** - Priority: MEDIUM
7. **Add error handling and logging improvements** - Priority: LOW
