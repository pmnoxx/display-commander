# Known Bugs

This document tracks known issues and bugs in the Display Commander addon. These are confirmed problems that need to be addressed in future releases.

## Critical Issues

### 1. Auto-restore on Exit Not Working
**Status:** Confirmed
**Severity:** Medium
**Description:** The "Auto-restore on exit" feature is not functioning properly. When exiting a game, the display resolution remains at 4K instead of being restored to the original resolution.
**Expected Behavior:** Display should automatically restore to the original resolution when the game exits.
**Current Behavior:** Display stays at 4K resolution after game exit.

### 2. Auto-apply Changes Options Not Working
**Status:** Confirmed
**Severity:** Medium
**Description:** While the "Auto-apply changes" feature itself works, two specific options within this feature are not functioning properly. The resolution changes do work correctly.
**Expected Behavior:** All auto-apply options should work when enabled.
**Current Behavior:** Some auto-apply options are not being applied automatically.

### 3. FPS Option Buttons Missing
**Status:** Confirmed
**Severity:** Medium
**Description:** In recent builds, the FPS option buttons are missing from the interface. This appears to be a UI rendering issue.
**Workaround:** Navigate to the "Window Info" tab and then back to the "Main" tab to restore the FPS option buttons.
**Expected Behavior:** FPS option buttons should be visible and functional on the Main tab.
**Current Behavior:** FPS option buttons are not visible until the workaround is applied.

## Bug Reporting

If you encounter additional bugs or issues, please report them with the following information:
- Description of the bug
- Steps to reproduce
- Expected behavior
- Actual behavior
- System information (if relevant)

## Fix Priority

1. **High Priority:** Auto-restore on exit and auto-apply options (affects core functionality)
2. **Medium Priority:** FPS option buttons UI issue (affects user experience)
