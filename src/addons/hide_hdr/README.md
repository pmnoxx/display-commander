# Hide HDR Addon

Hides HDR capabilities from games to prevent HDR mode detection and force SDR rendering.

## Features

This addon provides:

- **HDR Capability Hiding** - Prevents games from detecting HDR display capabilities
- **SDR Force Mode** - Forces games to use SDR rendering instead of HDR
- **Display Mode Override** - Overrides HDR display modes to SDR equivalents
- **DirectX/Vulkan Support** - Works with both DirectX and Vulkan games
- **ImGui Interface** - User-friendly configuration interface
- **Settings Management** - Persistent configuration using ReShade's config system
- **Event Handling** - ReShade event registration and handling
- **Logging** - Proper logging integration with ReShade
- **CMake Build System** - Complete build configuration

## Files

- `addon.hpp` - Header file with declarations and namespace
- `addon.cpp` - Addon metadata exports
- `main_entry.cpp` - Main implementation with ReShade event handlers and UI
- `CMakeLists.txt` - Build configuration
- `README.md` - This documentation

## UI Components

The addon includes three tabs:

1. **Main Tab** - Primary functionality with controls and status
2. **Settings Tab** - Configuration options and current values
3. **About Tab** - Information about the addon

## Usage

1. Install the addon by placing the `.addon64` file in your ReShade addons directory
2. Launch a game with ReShade enabled
3. Open the ReShade overlay (usually Home key)
4. Navigate to the "Hide HDR" addon
5. Enable the addon and configure settings as needed

## Building

The hide_hdr addon is automatically included in the main build system. Simply build the project as usual:

```bash
# Windows
build_display_commander.ps1

# Or use CMake directly
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

## Use Cases

This addon is useful when:

- Games incorrectly detect HDR capabilities on your display
- You want to force SDR rendering for better compatibility
- HDR mode causes visual issues or performance problems
- You need to override HDR display modes for testing purposes
- Games have broken HDR implementation

## Technical Details

The addon works by:

1. Intercepting DirectX/Vulkan API calls related to display mode enumeration
2. Filtering out HDR modes from available display modes
3. Forcing SDR mode selection when HDR modes are requested
4. Overriding HDR capability detection in graphics APIs

## Configuration

The addon supports various configuration options:

- **Enable/Disable** - Toggle the addon functionality
- **UI Visibility** - Show/hide the addon interface
- **Display Mode Filtering** - Control which modes are hidden
- **API Hooking** - Configure which graphics APIs to hook

## Notes

- The addon uses atomic variables for thread-safe state management
- Settings are automatically saved when changed
- All logging goes through ReShade's logging system
- The addon follows ReShade's naming conventions (zzz_ prefix for alphabetical ordering)
- Compatible with Windows 10/11 and modern graphics drivers

## Troubleshooting

If the addon doesn't work as expected:

1. Check that ReShade is properly installed and working
2. Verify the addon is loaded in the ReShade overlay
3. Check the ReShade log for error messages
4. Ensure your graphics drivers are up to date
5. Try disabling other addons that might conflict

## Development

To modify or extend the addon:

1. Edit the source files in `src/addons/hide_hdr/`
2. Update the CMakeLists.txt if adding new dependencies
3. Rebuild the project
4. Test with your target games

## License

This addon is part of the ReShade Display Commander project and follows the same license terms.
