# D3D Debug Layer Addon

A minimalistic ReShade addon that provides D3D debug layer message logging functionality for both D3D11 and D3D12 applications.

## Overview

This addon automatically detects D3D11 and D3D12 devices and enables debug layer message logging when available. It provides real-time logging of debug messages with appropriate severity levels.

## Features

- **Automatic Detection**: Automatically detects D3D11 vs D3D12 devices
- **Real-time Logging**: Processes debug messages in a dedicated thread
- **Severity Mapping**: Maps D3D debug message severities to appropriate log levels
- **Performance Conscious**: Limits message processing to maintain performance
- **Zero Configuration**: Works out of the box with no configuration required

## Requirements

- ReShade 6.5.1 or later
- D3D Debug Layer enabled (requires D3D SDK or Visual Studio)
- Windows 10/11 with D3D11 or D3D12 applications

## Enabling D3D Debug Layer

For the addon to function properly, the D3D debug layer must be enabled:

### Method 1: Graphics Tools Feature
1. Open Settings → Apps → Optional features
2. Add "Graphics Tools" feature
3. Restart the application

### Method 2: Visual Studio
1. Install Visual Studio with Graphics Tools component
2. The debug layer will be available system-wide

### Method 3: Windows SDK
1. Install Windows SDK with debugging tools
2. Debug layer will be enabled for development scenarios

## Log Output Format

Debug messages are logged in the following format:
```
[D3D Debug] D3D11 [SEVERITY] [CATEGORY] ID:123 - Message description
[D3D Debug] D3D12 [WARNING] [EXECUTION] ID:456 - Resource binding issue
```

### Severity Levels:
- **CORRUPTION**: Critical errors (logged as ERROR)
- **ERROR**: Runtime errors (logged as ERROR)  
- **WARNING**: Performance/usage warnings (logged as WARNING)
- **INFO**: Informational messages (logged as INFO)
- **MESSAGE**: General messages (logged as INFO)

### Categories:
- APPLICATION, INITIALIZATION, CLEANUP, COMPILATION
- STATE_CREATION, STATE_SETTING, STATE_GETTING
- RESOURCE_MANIPULATION, EXECUTION, SHADER
- And others depending on D3D version

## Performance Considerations

- Messages are processed in a separate thread at ~60 FPS
- Limited to 50 messages per frame to prevent performance impact
- Excess messages are skipped with a warning logged
- Debug layer has inherent performance overhead regardless of this addon

## Technical Details

### Architecture:
- `DebugLayerManager`: Singleton managing debug handlers
- `D3D11DebugHandler`: Handles ID3D11InfoQueue integration
- `D3D12DebugHandler`: Handles ID3D12InfoQueue integration
- Dedicated message processor thread with 16ms polling interval

### ReShade Integration:
- Uses `init_device` event for device detection and initialization
- Uses `destroy_device` event for cleanup
- Uses `present` event for status monitoring
- Thread-safe implementation with proper synchronization

## Building

The addon is built as part of the main CMake configuration:

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

Output: `d3d_debug_layer.addon64` (or `.addon32` for 32-bit builds)

## Installation

1. Copy the compiled `.addon64` file to your ReShade addons directory
2. Ensure D3D debug layer is enabled (see requirements above)
3. Launch your D3D11/D3D12 application with ReShade
4. Debug messages will automatically appear in ReShade logs

## Troubleshooting

### "InfoQueue interface not available"
- Debug layer is not enabled - follow enabling instructions above
- Application was not compiled with debug information
- Running release build without debug layer support

### No debug messages appearing
- Verify debug layer is enabled using DirectX Control Panel or Graphics Tools
- Check if application is actually triggering D3D validation warnings
- Ensure ReShade logging is enabled and visible

### Performance issues
- Debug layer itself has significant overhead (not related to this addon)
- Consider disabling debug layer for production use
- The addon limits processing to maintain acceptable performance

## Limitations

- Only works with D3D11 and D3D12 (no OpenGL/Vulkan support)
- Requires debug layer to be enabled system-wide or per-application
- Debug layer availability depends on installed development tools
- Some newer D3D12 message categories may not be available on older systems

## Dependencies

- ReShade API headers
- Windows D3D11/D3D12 headers
- Microsoft WRL (Windows Runtime Library)
- Standard C++ libraries (thread, atomic, mutex)
