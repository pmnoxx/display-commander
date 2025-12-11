# ReShade Version Check Implementation

## Overview

Display Commander now includes a comprehensive version check system that detects when ReShade is too old to support the required API version.

## How It Works

### API Version Requirements
- **Required API Version**: 17 (ReShade 6.6.2+)
- **Minimum ReShade Version**: 6.6.2
- **Detection Method**: Attempts to register with ReShade using API version 17

### Error Handling Flow

1. **Registration Attempt**: The addon attempts to register with ReShade using `reshade::register_addon()`
2. **Failure Detection**: If registration fails, it's likely due to API version mismatch
3. **User Notification**: A detailed error dialog is displayed to the user
4. **Graceful Exit**: The addon exits cleanly without crashing

### Error Message Details

When ReShade version is incompatible, users see:

```
Display Commander requires ReShade 6.6.2 or newer.

ERROR DETAILS:
• Required API Version: 17 (ReShade 6.6.2+)
• Your ReShade Version: 16 or older
• Status: Incompatible

SOLUTION:
1. Download the latest ReShade from: https://reshade.me/
2. Install ReShade 6.6.2 or newer
3. Restart your game to load the updated ReShade

This addon uses advanced features that require the newer ReShade API.
```

### Logging

The system provides detailed logging:

- **Success**: `"ReShade addon registration successful - API version 17 supported (ReShade 6.6.2+)"`
- **Failure**: `"ReShade addon registration failed - this usually indicates an API version mismatch"`
- **Details**: `"Display Commander requires ReShade 6.6.2+ (API version 17) but detected older version"`

## Implementation Details

### Files Modified
- `src/addons/display_commander/main_entry.cpp`

### Key Functions
- `CheckReShadeVersionCompatibility()`: Displays error dialog and logs failure
- Modified `DllMain()`: Calls version check when registration fails

### Benefits
1. **User-Friendly**: Clear error message with solution steps
2. **Non-Crashing**: Graceful exit instead of undefined behavior
3. **Informative**: Detailed logging for debugging
4. **Actionable**: Direct link to download updated ReShade

## Testing

To test the version check:

1. **With Compatible ReShade**: Addon loads normally with success message
2. **With Incompatible ReShade**: Error dialog appears, addon exits cleanly

## Future Enhancements

- Could add runtime version detection for more specific error messages
- Could add automatic ReShade update checking
- Could add fallback to older API versions if available
