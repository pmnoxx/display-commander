# Exclusive Key Groups Implementation Tasks

## Overview
Implement exclusive key groups functionality across all keyboard-related APIs to ensure consistent behavior regardless of which API the game uses to query keyboard state.

**Note:** Predefined groups are:
- AD Group: A and D keys (left/right strafe)
- WS Group: W and S keys (forward/backward)
- AWSD Group: A, W, S, D keys (all movement keys)

## Current Status
- ✅ Settings added (hotkeys_tab_settings)
- ✅ UI implemented (hotkeys_tab.cpp)
- ✅ Basic logic in ProcessHotkeys() - sends key up events
- ✅ Shared exclusive_key_groups namespace created
- ✅ Implemented in all keyboard API hooks
- ✅ Timestamp tracking for key presses
- ✅ Most recently pressed key selection logic
- ✅ Key simulation when active key changes
- ✅ Performance optimization: Cached active groups (updated once per second)

## Keyboard APIs Implementation Status

### 1. GetKeyState ✅ COMPLETE
- **File**: `hooks/windows_hooks/windows_message_hooks.cpp`
- **Function**: `GetKeyState_Detour`
- **Status**: ✅ Implemented
- **Implementation**: 
  - Checks `exclusive_key_groups::ShouldSuppressKey()` before returning result
  - Returns 0 if key should be suppressed
  - Updates exclusive group state when key is down/up

### 2. GetAsyncKeyState ✅ COMPLETE
- **File**: `hooks/windows_hooks/windows_message_hooks.cpp`
- **Function**: `GetAsyncKeyState_Detour`
- **Status**: ✅ Implemented
- **Implementation**: 
  - Checks `exclusive_key_groups::ShouldSuppressKey()` before returning result
  - Returns 0 if key should be suppressed
  - Updates exclusive group state when key is down/up

### 3. GetKeyboardState ✅ COMPLETE
- **File**: `hooks/windows_hooks/windows_message_hooks.cpp`
- **Function**: `GetKeyboardState_Detour`
- **Status**: ✅ Implemented
- **Implementation**: 
  - After getting keyboard state, clears keys that should be suppressed
  - Updates exclusive group state for all keys

### 4. Windows Messages (WM_KEYDOWN, WM_KEYUP, WM_CHAR, etc.) ✅ COMPLETE
- **File**: `hooks/windows_hooks/windows_message_hooks.cpp`
- **Functions**: `GetMessageA_Detour`, `GetMessageW_Detour`, `PeekMessageA_Detour`, `PeekMessageW_Detour`
- **Status**: ✅ Implemented
- **Implementation**: 
  - Suppresses WM_KEYDOWN messages for keys that should be suppressed
  - Updates exclusive group state for keyboard messages
  - Also implemented in `TranslateMessage_Detour` and `DispatchMessageA_Detour`/`DispatchMessageW_Detour`

### 5. Raw Input (GetRawInputData) ✅ COMPLETE
- **File**: `hooks/windows_hooks/windows_message_hooks.cpp`
- **Function**: `GetRawInputData_Detour`
- **Status**: ✅ Implemented
- **Implementation**: 
  - Filters out keyboard DOWN events for keys that should be suppressed
  - Replaces suppressed key events with neutral data
  - Updates exclusive group state for key down/up events

### 6. Raw Input Buffer (GetRawInputBuffer) ✅ COMPLETE
- **File**: `hooks/windows_hooks/windows_message_hooks.cpp`
- **Function**: `GetRawInputBuffer_Detour`
- **Status**: ✅ Implemented
- **Implementation**: 
  - Filters out keyboard DOWN events for keys that should be suppressed
  - Replaces suppressed key events with neutral data
  - Updates exclusive group state for key down/up events

### 7. keybd_event ✅ COMPLETE
- **File**: `hooks/windows_hooks/windows_message_hooks.cpp`
- **Function**: `keybd_event_Detour`
- **Status**: ✅ Implemented
- **Implementation**: 
  - Blocks key DOWN events for keys that should be suppressed
  - Updates exclusive group state for key down/up events

### 8. SendInput ✅ COMPLETE
- **File**: `hooks/windows_hooks/windows_message_hooks.cpp`
- **Function**: `SendInput_Detour`
- **Status**: ✅ Implemented
- **Implementation**: 
  - Filters out keyboard DOWN events for keys that should be suppressed
  - Updates exclusive group state for key down/up events

## Implementation Strategy

### Core Logic Function ✅ IMPLEMENTED
Created `exclusive_key_groups::ShouldSuppressKey(int vKey)` that:
1. ✅ Checks if exclusive keys feature is enabled (via hotkeys enabled toggle)
2. ✅ Checks if game is in foreground or UI is open
3. ✅ Checks if the key belongs to an active exclusive group
4. ✅ Checks if another key in the same group is currently down
5. ✅ Returns true if the key should be suppressed

### Key State Tracking ✅ IMPLEMENTED
- ✅ Track which keys are currently down in exclusive groups using `s_exclusive_group_active_key` array
- ✅ Update state when keys are pressed/released via `MarkKeyDown()` and `MarkKeyUp()`
- ✅ Use atomic operations for thread safety (std::atomic<int>)

### Integration Points ✅ IMPLEMENTED
- ✅ All keyboard API hooks call `exclusive_key_groups::ShouldSuppressKey()`
- ✅ ProcessExclusiveKeyGroups() updates the tracked state via `MarkKeyDown()`
- ✅ State is checked before returning key state to the game
- ✅ State is updated when keys are detected as down/up in all hooks

## Testing Checklist
- [ ] Test with GetKeyState
- [ ] Test with GetAsyncKeyState
- [ ] Test with GetKeyboardState
- [ ] Test with Windows Messages (GetMessage/PeekMessage)
- [ ] Test with Raw Input (GetRawInputData/GetRawInputBuffer)
- [ ] Test with keybd_event
- [ ] Test with SendInput
- [ ] Test multiple exclusive groups simultaneously
- [ ] Test custom groups
- [ ] Test edge cases (rapid key presses, overlapping groups)
- [ ] Test that state persists correctly across API calls
- [ ] Test that key releases properly clear exclusive group state

## Performance Optimization ✅ IMPLEMENTED
- **Cached Active Groups**: Added `UpdateCachedActiveKeys()` function that precomputes which keys belong to active groups
- **Update Frequency**: Cache is updated once per second in `continuous_monitoring.cpp` (in the 1-second update section)
- **Hot Path Optimization**: `ShouldSuppressKey()`, `MarkKeyDown()`, and `MarkKeyUp()` now use cached data instead of calling `GetActiveGroups()` every time
- **Cache Structure**:
  - `s_key_in_active_group[]` - Boolean array indicating if a key belongs to an active group
  - `s_key_to_group_index[]` - Map from key to group index for quick lookup
  - `s_cached_active_groups` - Cached vector of active groups
- **Initialization**: Cache is initialized on startup via `keyboard_tracker::Initialize()`

## Notes
- Exclusive key groups should work independently of input blocking
- Should respect hotkeys enabled toggle
- Should only work when game is in foreground or UI is open
- Need to handle thread safety for state tracking
- Performance: Active groups are cached and updated once per second to avoid repeated parsing/checking in hot paths
