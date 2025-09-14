# DualSense to XInput Integration TODO

## Overview
Integrate XInput_HID submodule to enable DualSense controller mapping to XInput in display_commander.

## Core Integration Tasks

- [ ] **Add XInput_HID build integration to CMakeLists.txt**
  - Add external/xinput_hid as subdirectory in CMakeLists.txt
  - Link XInput_HID library to display_commander addon
  - Include necessary XInput_HID headers in build

- [ ] **Create DualSense HID wrapper module**
  - Create src/addons/display_commander/dualsense/ directory
  - Implement dualsense_hid_wrapper.cpp/hpp to interface with XInput_HID
  - Expose simplified API: InitializeDualSenseHID(), GetDualSenseState(), ShutdownDualSenseHID()

- [ ] **Integrate DualSense detection into existing XInput hooks**
  - Modify src/addons/display_commander/hooks/xinput_hooks.cpp
  - Add DualSense device enumeration before calling original XInput functions
  - Implement fallback: if no XInput controllers found, check for DualSense controllers
  - Map DualSense input to XINPUT_STATE structure using XInput_HID mapping

- [ ] **Add DualSense-specific input processing**
  - Create dualsense_input_processor.cpp to handle DualSense-specific features
  - Implement adaptive trigger support (if needed for display_commander features)
  - Add touchpad input mapping (map to additional buttons or mouse input)
  - Handle DualSense Edge controller variants (additional buttons/paddles)

## API Integration Points

- [ ] **XInput_HID Core APIs to call:**
  - `SK_HID_SetupCompatibleControllers()` - Initialize HID device enumeration
  - `SK_DualSense_GetInputReportUSB()` - Get USB DualSense input data
  - `SK_DualSense_GetInputReportBt()` - Get Bluetooth DualSense input data
  - `hid_devices` global vector - Access enumerated DualSense devices
  - `hid_device_file_s::GetInputReport()` - Poll individual device for input

- [ ] **Required XInput_HID parameters:**
  - Device enumeration: `GUID_DEVINTERFACE_HID` for HID device detection
  - Vendor ID filtering: `0x054c` (Sony) for DualSense controllers
  - Product ID filtering: `0x0DF2` (DualSense), `0x0CE6` (DualSense Edge)
  - Connection type detection: USB vs Bluetooth via device path string matching

## Settings Integration

- [ ] **Add DualSense settings to experimental tab**
  - Create dualsense_settings.hpp/cpp in settings/ directory
  - Add enable/disable DualSense mapping toggle
  - Add DualSense-specific sensitivity/deadzone settings
  - Add adaptive trigger enable/disable option
  - Add touchpad input mapping configuration

- [ ] **Integrate with existing input remapping system**
  - Extend src/addons/display_commander/input_remapping/ to support DualSense buttons
  - Add DualSense-specific button constants (touchpad, mute, etc.)
  - Map DualSense buttons to XInput button equivalents for remapping compatibility

## UI Integration

- [ ] **Add DualSense status display to XInput widget**
  - Modify src/addons/display_commander/widgets/xinput_widget/ to show DualSense status
  - Display connection type (USB/Bluetooth) and battery level
  - Show DualSense-specific features (adaptive triggers, touchpad) status

- [ ] **Add DualSense configuration UI**
  - Create dualsense_config_widget.cpp in widgets/ directory
  - Add settings for sensitivity, deadzone, adaptive triggers
  - Add button mapping configuration for DualSense-specific buttons

## Threading and Performance

- [ ] **Implement proper threading for HID input polling**
  - Use XInput_HID's existing thread management (SK_HID_SpawnInputReportThread)
  - Ensure thread-safe access to DualSense input data
  - Implement proper cleanup on addon shutdown

- [ ] **Optimize input processing pipeline**
  - Minimize latency between DualSense input and XInput state updates
  - Use XInput_HID's existing input timestamp tracking
  - Implement efficient state change detection

## Error Handling and Diagnostics

- [ ] **Add comprehensive error handling**
  - Handle DualSense device connection/disconnection events
  - Implement fallback to original XInput if DualSense detection fails
  - Add logging for DualSense-specific errors and status changes

- [ ] **Add diagnostic functions**
  - Create dualsense_diagnostics.cpp for troubleshooting
  - Add device enumeration logging
  - Add input state debugging and validation

## Testing and Validation

- [ ] **Create DualSense test functions**
  - Add test_dualsense.cpp for manual testing
  - Implement input validation against expected DualSense behavior
  - Add performance benchmarking for input latency

- [ ] **Integration testing with existing features**
  - Test DualSense input with existing input remapping
  - Verify compatibility with A/B button swapping
  - Test with sensitivity/deadzone processing

## Minimum Viable Implementation

The absolute minimum work needed to map DualSense to XInput:

1. **Add XInput_HID to build system** (CMakeLists.txt changes)
2. **Create basic DualSense wrapper** (dualsense_hid_wrapper.cpp with InitializeDualSenseHID, GetDualSenseState, ShutdownDualSenseHID functions)
3. **Modify XInput hooks** to call DualSense wrapper when no XInput controllers found
4. **Map DualSense input to XINPUT_STATE** using existing XInput_HID mapping functions
5. **Add basic settings toggle** to enable/disable DualSense mapping

This minimal implementation would provide basic DualSense to XInput mapping without advanced features like adaptive triggers or touchpad support.
