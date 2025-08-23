# DeveloperNew Tab Implementation

## Overview
The DeveloperNew tab is a complete migration of the old developer functionality from the settings-based UI system to the new ImGui-based UI system. This tab provides all the advanced developer features and debugging capabilities that were previously available in the old Developer tab.

## Features Implemented

### 1. Developer Settings Section
- **Prevent Fullscreen**: Checkbox to prevent exclusive fullscreen mode
- **Spoof Fullscreen State**: Combo box with options for Disabled, Spoof as Fullscreen, Spoof as Windowed
- **Spoof Window Focus**: Combo box with options for Disabled, Spoof as Focused, Spoof as Unfocused
- **Continuous Monitoring**: Checkbox to enable/disable continuous window monitoring and automatic fixes
- **Prevent Always On Top**: Checkbox to prevent windows from becoming always on top
- **Background Feature**: Checkbox to enable/disable background window creation behind games

### 2. HDR and Colorspace Settings Section
- **Fix NVAPI HDR10 Colorspace**: Checkbox to automatically fix HDR10 colorspace when swapchain format is RGB10A2

### 3. NVAPI Settings Section
- **NVAPI Fullscreen Prevention**: Checkbox to use NVAPI for fullscreen prevention at driver level
- **NVAPI HDR Logging**: Checkbox to enable HDR monitor information logging via NVAPI
- **HDR Logging Interval**: Slider to control the interval between HDR logging (1-60 seconds)
- **Force HDR10**: Checkbox to force HDR10 mode using NVAPI
- **NVAPI Debug Information**: Comprehensive display of NVAPI status including:
  - Library loading status
  - Driver version information
  - Hardware detection
  - Fullscreen prevention status
  - Function availability
  - Error information and troubleshooting tips

### 4. Reflex Settings Section
- **Enable NVIDIA Reflex**: Checkbox to enable/disable NVIDIA Reflex for reduced latency
- **Reflex Low Latency Mode**: Disabled checkbox (currently not implemented)
- **Reflex Low Latency Boost**: Disabled checkbox (currently not implemented)
- **Reflex Use Markers**: Disabled checkbox (currently not implemented)
- **Reflex Debug Output**: Checkbox to enable/disable Reflex debug messages in logs

### 5. Latency Display Section
- **Current Latency**: Real-time display of current frame latency in milliseconds
- **PCL AV Latency**: Display of PCL Average Latency (30-frame average) - matches NVIDIA overlay
- **Reflex Status**: Current Reflex activation status and frame count
- **Debug Button**: Button to log all current latency values for debugging purposes

## Implementation Details

### File Structure
- `developer_new_tab.hpp`: Header file with function declarations
- `developer_new_tab.cpp`: Implementation file with all UI drawing functions
- Integrated into `new_ui_tabs.cpp` as "DeveloperNew" tab

### Dependencies
- ImGui for UI rendering
- External variables from `addon.hpp` for settings
- NVAPI classes for fullscreen prevention functionality
- Reflex management functions for hook installation/removal
- Logging functions for debug output

### Migration Approach
Instead of using the old `renodx::utils::settings2::Setting` system, this implementation:
1. Directly manipulates the external variables (e.g., `s_prevent_fullscreen`, `s_spoof_fullscreen_state`)
2. Uses ImGui widgets (checkboxes, combo boxes, sliders) for user interaction
3. Maintains the same functionality and behavior as the original settings
4. Provides immediate visual feedback and real-time updates

### Key Differences from Old System
1. **Immediate Updates**: Changes take effect immediately without needing to apply settings
2. **Better Visual Feedback**: ImGui provides more responsive and modern UI elements
3. **Real-time Display**: Latency information updates in real-time
4. **Consistent Styling**: Follows the new UI system's visual design patterns

## Usage
The DeveloperNew tab is accessible through the new UI system alongside other tabs like Main, Device Info, Window Info, and Swapchain. Users can:

1. Navigate to the "DeveloperNew" tab
2. Modify developer settings using the various controls
3. View real-time NVAPI status and debug information
4. Monitor latency performance metrics
5. Configure Reflex settings for optimal performance

## Future Considerations
- The old Developer tab can be removed once this new implementation is verified to work correctly
- Additional developer features can be easily added to this tab
- The modular design allows for easy maintenance and extension
- Consider adding more advanced debugging tools and performance monitoring features

## Testing
The implementation has been tested with a successful build and should provide the same functionality as the original developer tab while offering a more modern and responsive user experience.
