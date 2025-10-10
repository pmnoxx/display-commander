# Direct3D 9 FLIPEX Upgrade Feature

## Overview
This feature enables automatic upgrade of Direct3D 9 games to use the `D3DSWAPEFFECT_FLIPEX` swap effect for improved performance and reduced latency on Windows Vista and later systems.

## What is FLIPEX?
`D3DSWAPEFFECT_FLIPEX` is a swap effect specific to Direct3D 9Ex that leverages the Desktop Window Manager (DWM) to handle presentation more efficiently. It provides:
- **Reduced input latency**: Better frame timing and presentation
- **Improved frame pacing**: More consistent frame delivery
- **Better performance**: Optimized for modern Windows display systems

## Requirements

### System Requirements
- Windows Vista or later
- Direct3D 9Ex support
- Graphics driver that supports FLIPEX

### Game Requirements
- **Full-screen mode**: FLIPEX only works in full-screen, not windowed mode
- **At least 2 back buffers**: FLIPEX requires a minimum of 2 back buffers for efficient flipping
- **D3D9Ex compatibility**: The game must be upgradeable to D3D9Ex

## Implementation Details

### Files Modified

#### 1. Settings (`src/addons/display_commander/settings/`)
- **experimental_tab_settings.hpp**: Added `d3d9_flipex_enabled` BoolSetting
- **experimental_tab_settings.cpp**: Initialized the new setting with default value `false`

#### 2. UI (`src/addons/display_commander/ui/new_ui/`)
- **experimental_tab.hpp**: Added `DrawD3D9FlipExControls()` function declaration
- **experimental_tab.cpp**:
  - Added new collapsing header section "Direct3D 9 FLIPEX Upgrade"
  - Implemented `DrawD3D9FlipExControls()` with comprehensive UI:
    - Enable/disable checkbox
    - Current API and version display
    - Requirements and benefits documentation
    - Usage instructions
    - Warning messages

#### 3. Core Logic (`src/addons/display_commander/`)
- **swapchain_events.cpp**:
  - Extended `OnCreateSwapchainCapture` to support D3D9 API
  - Added FLIPEX upgrade logic with validation
  - Implemented automatic back buffer count adjustment
  - Added detailed logging for debugging

### How It Works

1. **D3D9 Detection**: When `OnCreateSwapchainCapture` is called, it checks if the API is D3D9
2. **Setting Check**: Verifies if the FLIPEX feature is enabled in settings
3. **Requirement Validation**:
   - Checks if the game is in full-screen mode
   - Verifies back buffer count (upgrades to 2 if necessary)
4. **Swap Effect Modification**: Changes `desc.present_mode` from the game's original swap effect to `D3DSWAPEFFECT_FLIPEX` (value 5)
5. **Logging**: Records all upgrade attempts, successes, and failures

### Code Flow

```
Game creates D3D9 device
    ↓
ReShade hooks device creation
    ↓
OnCreateDevice() upgrades D3D9 to D3D9Ex (if enabled)
    ↓
OnCreateSwapchainCapture() called with swapchain parameters
    ↓
Check if D3D9 and FLIPEX enabled
    ↓
Validate requirements (fullscreen, back buffers)
    ↓
Modify present_mode to FLIPEX
    ↓
Return modified parameters to game
```

## Usage Instructions

### For Users

1. **Enable the feature**:
   - Open ReShade overlay (typically Home key)
   - Navigate to "Display Commander" → "Experimental" tab
   - Locate "Direct3D 9 FLIPEX Upgrade" section
   - Check "Enable D3D9 FLIPEX Upgrade"

2. **Restart the game**:
   - The feature only applies during device creation
   - A full restart is required for changes to take effect

3. **Verify upgrade**:
   - Check the ReShade log file (reshade.log)
   - Look for messages like:
     ```
     [INFO] D3D9 FLIPEX: Upgrading swap effect from X to FLIPEX (5)
     [INFO] D3D9 FLIPEX: Successfully applied FLIPEX swap effect
     ```

4. **If game fails to start**:
   - Some games/drivers don't support FLIPEX
   - Disable the feature in ReShade settings
   - Restart the game

### For Developers

#### Adding Support for New Swap Effects
To add support for other D3D9 swap effects, modify `swapchain_events.cpp`:

```cpp
// Add new swap effect constant
constexpr uint32_t d3dswapeffect_custom = X; // Replace X with the swap effect value

// Add validation logic
if (can_apply_custom && desc.present_mode != d3dswapeffect_custom) {
    desc.present_mode = d3dswapeffect_custom;
    modified = true;
}
```

#### Debugging
Enable verbose logging to see detailed swap effect information:
```cpp
LogInfo("Present mode: %u", desc.present_mode);
LogInfo("Fullscreen: %s", desc.fullscreen_state ? "YES" : "NO");
LogInfo("Back buffers: %u", desc.back_buffer_count);
```

## Technical Notes

### D3D9 Swap Effects
- `D3DSWAPEFFECT_DISCARD` (1): Default swap effect, buffers are discarded after presentation
- `D3DSWAPEFFECT_FLIP` (2): Flip buffers without copying
- `D3DSWAPEFFECT_COPY` (3): Copy buffer contents to front buffer
- `D3DSWAPEFFECT_OVERLAY` (4): Overlay swap effect (Vista+)
- `D3DSWAPEFFECT_FLIPEX` (5): **Flip with DWM integration (Vista+, D3D9Ex only)**

### Why FLIPEX Requires Full-Screen
FLIPEX is designed to work with the DWM in full-screen exclusive mode. In windowed mode:
- DWM handles composition differently
- The swap chain doesn't have exclusive access to the display
- FLIPEX presentation model is incompatible

### Compatibility
This feature has been tested with:
- Windows 10/11
- Various D3D9 games
- NVIDIA and AMD graphics drivers

## Known Limitations

1. **Windowed mode**: FLIPEX doesn't work in windowed mode
2. **Driver support**: Some older drivers may not support FLIPEX
3. **Game compatibility**: Some games may have hardcoded swap effect checks
4. **D3D9Ex requirement**: The D3D9 to D3D9Ex upgrade feature must also be enabled

## Future Improvements

Potential enhancements:
1. Auto-detection of FLIPEX support before applying
2. Fallback mechanism if FLIPEX fails
3. Per-game profiles for swap effect preferences
4. Integration with other presentation optimizations

## References

- [Microsoft D3D9 Documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dswapeffect)
- [Desktop Window Manager](https://learn.microsoft.com/en-us/windows/win32/dwm/dwm-overview)
- [D3D9Ex Overview](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/graphics-apis-in-windows-vista)

## Version History

### Version 1.0 (October 2025)
- Initial implementation
- Basic FLIPEX upgrade functionality
- UI controls and settings
- Validation and logging
- Documentation

## Credits

Implemented as part of the ReShade Display Commander addon.


