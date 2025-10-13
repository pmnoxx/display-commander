# DualSense to XInput Conversion Feature

## Overview

This feature allows Display Commander to convert DualSense (PlayStation 5) controller input to XInput format, enabling DualSense controllers to work with games that only support XInput controllers.

## Requirements

- Special-K must be installed and running
- DualSense controller must be connected
- XInput hooks must be enabled in Display Commander

## How to Use

1. Open Display Commander's XInput widget
2. Enable "XInput Hooks" if not already enabled
3. Check the "DualSense to XInput" checkbox
4. The feature will automatically detect and convert DualSense input

## Technical Details

### Implementation

The feature works by:

1. **Detection**: Checks if Special-K is loaded and has DualSense support
2. **Direct HID Access**: Uses direct HID function pointers to bypass HID suppression hooks
3. **Polling**: Continuously reads DualSense controller state in a background thread
4. **Conversion**: Converts DualSense button/stick data to XInput format
5. **Integration**: Intercepts XInputGetState calls and returns converted data when enabled

### HID Suppression Bypass

The implementation includes direct function pointers that bypass HID suppression hooks:

- `ReadFile_Direct`: Direct ReadFile calls that bypass suppression
- `HidD_GetInputReport_Direct`: Direct input report reading
- `HidD_GetAttributes_Direct`: Direct device attribute reading

This allows the DualSense conversion to work even when HID suppression is enabled for games.

### Button Mapping

The following DualSense buttons are mapped to XInput:

- **DualSense** → **XInput**
- Cross → A
- Circle → B
- Square → X
- Triangle → Y
- L1 → Left Shoulder
- R1 → Right Shoulder
- L2 → Left Trigger
- R2 → Right Trigger
- L3 → Left Thumbstick
- R3 → Right Thumbstick
- Options → Start
- Share → Back
- PlayStation → Guide

### Stick Conversion

- DualSense analog sticks (0-255 range) are converted to XInput format (-32768 to 32767)
- Triggers (0-255 range) are converted to XInput format (0-255)

## Limitations

- Currently requires Special-K for DualSense support
- Only works with games that use XInput API
- May not support all DualSense features (haptic feedback, adaptive triggers, etc.)
- Experimental feature - may have compatibility issues

## Troubleshooting

### Feature Not Working

1. Ensure Special-K is installed and running
2. Check that XInput hooks are enabled
3. Verify DualSense controller is connected
4. Check Display Commander logs for error messages

### Controller Not Detected

1. Make sure DualSense is connected via USB or Bluetooth
2. Verify Special-K can detect the controller
3. Try disconnecting and reconnecting the controller

## Future Improvements

- Direct HID support without Special-K dependency
- Support for DualSense-specific features
- Better error handling and diagnostics
- Configuration options for button mapping
