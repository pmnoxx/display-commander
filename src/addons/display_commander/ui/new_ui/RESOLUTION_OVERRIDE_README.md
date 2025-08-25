# Resolution Override Feature (Experimental)

## Overview
The Resolution Override feature allows users to force a specific backbuffer resolution during swapchain creation, similar to ReShade's `ForceResolution` setting. This feature is experimental and requires enabling "unstable ReShade features" in the Developer tab.

## How It Works
When enabled, the resolution override intercepts the `create_swapchain` event and modifies the `swapchain_desc.back_buffer.texture.width` and `swapchain_desc.back_buffer.texture.height` values before the swapchain is created. This effectively forces the game to render at the specified resolution.

## Implementation Details

### Settings
- **Enable Resolution Override**: Master toggle for the feature
- **Override Width**: Target width in pixels (1-7680)
- **Override Height**: Target height in pixels (1-4320)

### Logic
The override only applies when:
1. The feature is enabled (`s_enable_resolution_override >= 0.5f`)
2. Both width and height are greater than 0
3. The swapchain creation event is triggered

### Code Location
- **Settings**: `developer_new_tab_settings.hpp/cpp`
- **UI**: `developer_new_tab.cpp` - `DrawResolutionOverrideSettings()`
- **Logic**: `swapchain_events.cpp` - `OnCreateSwapchainCapture()`

## Usage

### Enabling the Feature
1. Go to the Developer tab in the UI
2. Enable "Enable unstable ReShade features"
3. Scroll down to the "Resolution Override (Experimental)" section
4. Check "Enable Resolution Override"
5. Set desired width and height values

### Requirements
- Must have "unstable ReShade features" enabled
- Both width and height must be > 0 for the override to take effect
- Changes take effect on the next swapchain creation (usually game restart)

## Technical Notes

### Same as ReShade ForceResolution
This implementation follows the exact same logic as ReShade's `ForceResolution` setting:
- Only applies when both values are non-zero
- Modifies the swapchain description during creation
- Logs the override for debugging purposes

### Safety Features
- Feature is disabled by default
- Requires explicit enabling of unstable features
- Input validation (width/height ranges)
- Logging for debugging and monitoring

### Performance Impact
- Minimal overhead - only runs during swapchain creation
- No runtime performance impact
- Memory usage scales with the target resolution

## Example Configuration
```ini
[DisplayCommanderNew]
EnableUnstableReShadeFeatures=1
EnableResolutionOverride=1
OverrideResolutionWidth=2560
OverrideResolutionHeight=1440
```

## Troubleshooting

### Override Not Working
1. Ensure "unstable ReShade features" is enabled
2. Verify both width and height are > 0
3. Check that the feature is enabled
4. Restart the game (swapchain recreation required)
5. Check logs for "Resolution Override applied" message

### Common Issues
- **Feature disabled**: Enable "unstable ReShade features" first
- **Invalid resolution**: Ensure width and height are within valid ranges
- **No effect**: Resolution changes require game restart in most cases
- **Performance issues**: Very high resolutions may impact performance

## Future Enhancements
- Add preset resolutions (1080p, 1440p, 4K, etc.)
- Support for aspect ratio preservation
- Dynamic resolution scaling
- Per-game resolution profiles
- Integration with display mode detection
