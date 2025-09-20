# Screensaver Disabling Implementation Analysis

## Overview

This document analyzes how Special-K implements screensaver disabling functionality and provides an abstracted approach for implementing similar functionality in ReShade Display Commander.

## Windows APIs Used

### Primary APIs

1. **SetThreadExecutionState** - Core API for preventing system sleep and screensaver
   - `ES_CONTINUOUS` - Continuous execution state
   - `ES_DISPLAY_REQUIRED` - System required for display
   - `ES_SYSTEM_REQUIRED` - System required for operation

2. **SystemParametersInfo** - System parameter management
   - `SPI_GETSCREENSAVERRUNNING` - Check if screensaver is currently active
   - `SPI_GETSCREENSAVETIMEOUT` - Get screensaver timeout value

3. **Window Message Handling**
   - `WM_SYSCOMMAND` with `SC_SCREENSAVE` - Screensaver activation message
   - `WM_SYSCOMMAND` with `SC_MONITORPOWER` - Monitor power management

4. **Process Management**
   - `TerminateProcess` - Kill screensaver process (`scrnsave.scr`)

## Implementation Strategy

### 1. Configuration Options

```cpp
struct ScreensaverConfig {
    bool disable_screensaver = false;        // Global screensaver disable
    bool fullscreen_no_saver = false;        // Disable in fullscreen only
    bool manage_screensaver = false;         // Take full control from game
    bool gamepad_blocks_screensaver = true;  // Gamepad input blocks screensaver
    BOOL screensaver_active = FALSE;         // Current state (read-only)
};
```

### 2. Core Implementation Components

#### A. Thread Execution State Management

```cpp
// Hook SetThreadExecutionState to prevent games from overriding
EXECUTION_STATE WINAPI SetThreadExecutionState_Detour(EXECUTION_STATE esFlags) {
    if (config.window.manage_screensaver) {
        // Reset any continuous state to allow micromanagement
        SetThreadExecutionState_Original(ES_CONTINUOUS);
        return 0x0; // Block game's attempt
    }
    return SetThreadExecutionState_Original(esFlags);
}
```

#### B. Window Message Interception

```cpp
// Handle WM_SYSCOMMAND messages for screensaver activation
case WM_SYSCOMMAND:
    switch (LOWORD(wParam & 0xFFF0)) {
        case SC_SCREENSAVE:
        case SC_MONITORPOWER:
            if (config.window.disable_screensaver) {
                if (lParam != -1) // -1 = Monitor Power On, don't block
                    return TRUE;   // Block screensaver activation
            }
            break;
    }
    break;
```

#### C. Screensaver State Detection

```cpp
// Check if screensaver is currently active
bool CheckScreensaverActive() {
    static DWORD dwLastCheck = 0;
    static BOOL bScreensaverActive = FALSE;

    if (dwLastCheck < GetTickCount() - 125) { // Check every 125ms
        dwLastCheck = GetTickCount();
        SystemParametersInfoA(SPI_GETSCREENSAVETIMEOUT, 0, &bScreensaverActive, 0);
    }

    return bScreensaverActive != FALSE;
}
```

#### D. Screensaver Termination

```cpp
// Kill active screensaver process
void KillScreensaver() {
    // Terminate screensaver process
    SK_TerminateProcesses(L"scrnsave.scr", true);

    // Reset execution state to prevent immediate reactivation
    SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);
}
```

### 3. Input-Based Screensaver Management

#### Gamepad Input Detection

```cpp
// Track last gamepad activity timestamp
extern DWORD LastGamepadActivity;

bool IsGamepadActive() {
    DWORD dwTimeoutInSeconds = 0;
    SystemParametersInfoA(SPI_GETSCREENSAVETIMEOUT, 0, &dwTimeoutInSeconds, 0);

    return LastGamepadActivity >= (GetTickCount() - (dwTimeoutInSeconds * 1000));
}
```

#### Mouse/Keyboard Activity

```cpp
// Update activity timestamps on input
void OnInputActivity() {
    LastInputActivity = GetTickCount();

    if (config.window.screensaver_active && config.input.blocks_screensaver) {
        KillScreensaver();
    }
}
```

### 4. Window State Management

#### Fullscreen Detection

```cpp
bool IsFullscreen() {
    RECT windowRect, displayRect;
    GetWindowRect(hWnd, &windowRect);
    GetClientRect(GetDesktopWindow(), &displayRect);

    return (windowRect.left == displayRect.left &&
            windowRect.right == displayRect.right &&
            windowRect.top == displayRect.top &&
            windowRect.bottom == displayRect.bottom);
}
```

#### Window Focus Detection

```cpp
bool IsWindowActive() {
    return (GetForegroundWindow() == hWnd) &&
           (GetFocus() == hWnd);
}
```

## Implementation Phases

### Phase 1: Basic Screensaver Disabling
1. Hook `SetThreadExecutionState` to prevent game overrides
2. Intercept `WM_SYSCOMMAND` messages for `SC_SCREENSAVE`
3. Implement basic configuration options
4. Add screensaver state detection

### Phase 2: Advanced Management
1. Add input-based screensaver blocking
2. Implement fullscreen-specific behavior
3. Add screensaver termination functionality
4. Create UI controls for configuration

### Phase 3: Integration
1. Integrate with existing ReShade settings system
2. Add per-game configuration support
3. Implement proper cleanup on exit
4. Add user notifications and feedback

## Key Considerations

### Thread Safety
- Use atomic operations for configuration flags
- Protect shared state with mutexes where needed
- Ensure thread-safe screensaver state detection

### Performance
- Limit screensaver state checks to reasonable intervals (125ms)
- Cache results when possible
- Avoid blocking operations in critical paths

### Compatibility
- Handle different game behaviors gracefully
- Provide fallback mechanisms for edge cases
- Ensure proper cleanup on DLL unload

### User Experience
- Provide clear configuration options
- Show appropriate notifications
- Allow fine-grained control over behavior

## API Dependencies

### Required Windows APIs
- `kernel32.dll`: SetThreadExecutionState, GetTickCount
- `user32.dll`: SystemParametersInfo, GetForegroundWindow, GetFocus
- `psapi.dll`: EnumProcesses (for screensaver process detection)

### Optional APIs
- `powrprof.dll`: SetSuspendState (for advanced power management)
- `winmm.dll`: timeGetTime (for high-resolution timing)

## Error Handling

### Common Failure Modes
1. **API Hook Failure**: Fallback to direct API calls
2. **Process Termination Failure**: Log error, continue operation
3. **Configuration Load Failure**: Use safe defaults
4. **Thread Creation Failure**: Disable advanced features

### Recovery Strategies
- Graceful degradation when features fail
- Automatic retry mechanisms for transient failures
- User notification for critical failures
- Fallback to basic functionality

## Testing Considerations

### Test Scenarios
1. **Basic Disable**: Verify screensaver doesn't activate
2. **Input Blocking**: Test mouse/keyboard/gamepad input
3. **Fullscreen Mode**: Verify fullscreen-specific behavior
4. **Window Focus**: Test focus change scenarios
5. **Process Termination**: Test screensaver killing
6. **Configuration Changes**: Test runtime configuration updates

### Edge Cases
- Multiple monitor setups
- Remote desktop scenarios
- Power management conflicts
- Game-specific screensaver handling
- System sleep vs. screensaver differences

## Integration Points

### ReShade Display Commander Integration
1. **Settings System**: Use existing configuration framework
2. **UI Integration**: Add to appropriate settings tabs
3. **Event System**: Hook into existing window management
4. **Logging**: Use existing logging infrastructure
5. **Threading**: Integrate with existing thread management

### Configuration Storage
- Store settings in ReShade INI format
- Support per-game overrides
- Provide sensible defaults
- Enable runtime configuration changes

This implementation provides a comprehensive approach to screensaver management that can be integrated into ReShade Display Commander while maintaining compatibility and user control.

## Special-K's Specific API Usage

Based on analysis of the Special-K source code, here's how each API is actually implemented:

### 1. SetThreadExecutionState - Hooked for Game Override Prevention

```cpp
// Special-K hooks SetThreadExecutionState to prevent games from using it
EXECUTION_STATE WINAPI SetThreadExecutionState_Detour(EXECUTION_STATE esFlags) {
    // Reset any continuous state so we can micromanage screensaver activation
    if (config.window.manage_screensaver) {
        SetThreadExecutionState_Original(ES_CONTINUOUS);
        return 0x0; // Block game's attempt to control execution state
    }
    return SetThreadExecutionState_Original(esFlags);
}
```

**Key Points:**
- Special-K doesn't directly call `SetThreadExecutionState` for screensaver prevention
- Instead, it hooks the API to prevent games from overriding screensaver management
- When `manage_screensaver` is enabled, it blocks all game attempts to use this API
- This allows Special-K to have full control over execution state management

### 2. SystemParametersInfo - State Detection and Timeout Management

```cpp
// Check if screensaver is currently running (called every 125ms)
static DWORD dwLastScreensaverCheck = 0;
static BOOL bScreensaverActive = FALSE;

if (dwLastScreensaverCheck < current_time - 125) {
    dwLastScreensaverCheck = current_time;
    SystemParametersInfoA(SPI_GETSCREENSAVERRUNNING, 0, &bScreensaverActive, 0);
}

// Get screensaver timeout for gamepad activity calculations
DWORD dwTimeoutInSeconds = 0;
SystemParametersInfoA(SPI_GETSCREENSAVETIMEOUT, 0, &dwTimeoutInSeconds, 0);
```

**Key Points:**
- Uses `SPI_GETSCREENSAVERRUNNING` to detect if screensaver is currently active
- Uses `SPI_GETSCREENSAVETIMEOUT` to get system screensaver timeout
- Called every 125ms for real-time state detection
- Used for gamepad activity timeout calculations

### 3. Window Message Interception - Blocking Screensaver Activation

```cpp
// Handle WM_SYSCOMMAND messages in window procedure
case WM_SYSCOMMAND:
    switch (LOWORD(wParam & 0xFFF0)) {
        case SC_SCREENSAVE:
        case SC_MONITORPOWER:
            if (config.window.disable_screensaver) {
                if (lParam != -1) // -1 = Monitor Power On, don't block
                    return TRUE;   // Block screensaver activation
            }

            // Additional logic for fullscreen and gamepad activity
            if (SK_IsGameWindowActive() || bTopMostOnMonitor ||
                (bGamepadActive && config.input.gamepad.blocks_screensaver)) {
                if (LOWORD(wParam & 0xFFF0) == SC_SCREENSAVE) {
                    // Block screensaver in fullscreen or when gamepad is active
                    return TRUE;
                }
            }
            break;
    }
    break;
```

**Key Points:**
- Intercepts `WM_SYSCOMMAND` messages with `SC_SCREENSAVE` and `SC_MONITORPOWER`
- Blocks these messages to prevent screensaver activation
- Has special handling for fullscreen mode and gamepad activity
- Uses `lParam != -1` check to avoid blocking monitor power-on events

### 4. Process Management - Active Screensaver Termination

```cpp
// Custom process termination function
BOOL WINAPI SK_TerminateProcesses(const wchar_t* wszProcName, bool all) {
    BOOL killed = FALSE;
    PROCESSENTRY32W pe32 = {};

    SK_AutoHandle hProcSnap(
        CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    );

    if ((intptr_t)hProcSnap.m_h <= 0) return false;

    pe32.dwSize = sizeof(PROCESSENTRY32W);

    if (!Process32FirstW(hProcSnap, &pe32)) return false;

    do {
        if (!SK_Path_wcsicmp(wszProcName, pe32.szExeFile)) {
            killed |= SK_TerminatePID(pe32.th32ProcessID, 0x0);
            if ((!all) && killed) return TRUE;
        }
    } while (Process32NextW(hProcSnap, &pe32));

    return killed != FALSE;
}

// Screensaver termination in background thread
void SK_ImGui_KillScreensaver(void) {
    static HANDLE hScreensaverTerminator = SK_Thread_CreateEx([](LPVOID)->DWORD {
        const HANDLE events[] = {
            __SK_DLL_TeardownEvent, hSignalScreensaver
        };

        while (WAIT_OBJECT_0 != WaitForMultipleObjects(2, events, FALSE, INFINITE)) {
            SK_TerminateProcesses(L"scrnsave.scr", true);
        }

        SK_Thread_CloseSelf();
        return 0;
    }, L"[SK] Screensaver Terminator");

    SetEvent(hSignalScreensaver);
}
```

**Key Points:**
- Uses `CreateToolhelp32Snapshot` to enumerate running processes
- Searches for `scrnsave.scr` (Windows screensaver process)
- Uses `OpenProcess` + `TerminateProcess` to kill the screensaver process
- Runs in a background thread that continuously monitors and terminates screensaver
- More aggressive approach than just preventing activation

### Special-K's Unique Approach

**Key Differences from Basic Implementation:**

1. **Hooking Strategy**: Instead of directly calling `SetThreadExecutionState`, Special-K hooks it to prevent games from interfering
2. **State Detection**: Uses `SystemParametersInfo` for detection rather than prevention
3. **Message Blocking**: Intercepts window messages at the system level
4. **Process Termination**: Actively kills the screensaver process rather than just preventing it
5. **Background Monitoring**: Runs continuous background threads for screensaver management

**Why This Approach Works:**
- Prevents games from overriding screensaver settings
- Provides real-time state detection
- Handles edge cases like fullscreen mode and gamepad input
- Ensures screensaver stays disabled even if other methods fail
- Gives users fine-grained control over screensaver behavior

This sophisticated approach allows Special-K to provide reliable screensaver management that works across different games and scenarios.

## Current Implementation

The screensaver disabling functionality has been implemented using a more sophisticated approach than the originally proposed minimal implementation:

### Current Implementation Approach

The current implementation uses a combination of Windows API hooks and continuous monitoring to provide flexible screensaver management:

#### 1. **SetThreadExecutionState Hook**
- Hooks the `SetThreadExecutionState` API to intercept and control execution state requests
- Captures the original execution state on first call to preserve game's intended behavior
- Provides three modes of operation:
  - **No Change**: Preserves original game behavior
  - **Disable in Foreground**: Only disables screensaver when game window is focused
  - **Always Disable**: Always disables screensaver while game is running

#### 2. **Window State Monitoring**
- Continuously monitors the game window's foreground state
- Uses `GetForegroundWindow` to determine if the game window is currently active
- Updates screensaver state based on window focus and selected mode

#### 3. **Execution State Management**
- Uses `ES_CONTINUOUS | ES_DISPLAY_REQUIRED` to prevent screensaver activation
- Restores original execution state when screensaver should be allowed
- Maintains state tracking to avoid unnecessary API calls

#### 4. **User Interface Integration**
- Provides a dropdown menu in the main settings tab with three options:
  - "No Change" - Don't modify screensaver behavior
  - "Disable in Foreground" - Disable when game window is focused
  - "Always Disable" - Always disable while game is running
- Includes helpful tooltips explaining each mode
- Settings are persisted and loaded from ReShade configuration

### Key Benefits of Current Approach

1. **Non-Intrusive**: Uses Windows APIs as intended rather than process termination
2. **Flexible**: Provides multiple modes to suit different use cases
3. **Efficient**: Only updates state when necessary, avoiding constant polling
4. **Compatible**: Works with games that manage their own execution state
5. **User-Friendly**: Simple UI with clear explanations

### Implementation Details

**Core Components:**
- `SetThreadExecutionState_Detour()` in `api_hooks.cpp` - Main hook function
- `UpdateScreensaverState()` in `continuous_monitoring.cpp` - State management
- `ScreensaverMode` enum in `main_tab_settings.hpp` - Configuration options
- UI controls in `main_new_tab.cpp` - User interface

**State Management:**
- Tracks last screensaver state and window foreground state
- Only calls `SetThreadExecutionState` when state changes
- Preserves original game execution state for restoration
