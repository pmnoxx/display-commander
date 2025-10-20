# Fake NVAPI Feature

## Overview

The Fake NVAPI feature allows Display Commander to spoof NVIDIA GPU detection on non-NVIDIA systems, enabling DLSS and other NVIDIA-specific features to work on AMD and Intel GPUs.

## How It Works

This feature is based on the [fakenvapi project](https://github.com/emoose/fakenvapi) and works by:

1. **Detection**: Automatically detects if a real NVIDIA GPU is present
2. **Override**: If no NVIDIA GPU is detected and the feature is enabled, loads a fake `nvapi64.dll`
3. **Spoofing**: The fake DLL intercepts NVAPI calls and returns fake NVIDIA GPU information
4. **Integration**: Works with games that use DLSS, Reflex, and other NVIDIA features

## Installation

1. Download the `fakenvapi.dll` from the [fakenvapi releases](https://github.com/emoose/fakenvapi/releases)
2. Rename it to `nvapi64.dll`
3. Place it in the same directory as the Display Commander addon
4. Enable "Fake NVAPI" in the Developer tab settings

## Supported Features

- **DLSS (Deep Learning Super Sampling)**: Upscaling technology
- **DLSS-G (Frame Generation)**: AI-powered frame generation
- **NVIDIA Reflex**: Low-latency gaming
- **Anti-Lag 2**: AMD's low-latency technology (when available)
- **LatencyFlex**: Cross-platform low-latency solution

## Configuration

### Settings

- **Enable Fake NVAPI**: Master toggle for the feature (default: enabled)
- **Auto-Detection**: Automatically disables on real NVIDIA systems
- **Status Display**: Shows current state and any errors

### UI Location

The fake NVAPI controls are located in:
- **Tab**: Developer Tab
- **Section**: "Fake NVAPI (Experimental)"

## Warnings and Limitations

### ⚠️ Experimental Feature

This feature is experimental and may cause:

- **Game crashes or instability**
- **Performance issues**
- **Incompatibility with some games**
- **Unexpected behavior in certain scenarios**

### System Requirements

- **Non-NVIDIA GPU**: AMD RDNA1+ or Intel Arc
- **Windows 10/11**: Required for proper DLL loading
- **DirectX 11/12**: Most games using DLSS require these APIs

### Known Issues

1. **Limited Game Support**: Not all games will work with fake NVAPI
2. **Performance Impact**: May introduce slight overhead
3. **Stability Concerns**: Some games may crash or behave unexpectedly
4. **Update Dependencies**: Requires keeping fakenvapi.dll updated

## Troubleshooting

### Common Problems

**"Fake nvapi64.dll not found"**
- Ensure `nvapi64.dll` is in the addon directory
- Check file permissions and antivirus software

**"Real NVIDIA GPU detected - fake NVAPI disabled"**
- This is normal behavior on NVIDIA systems
- Fake NVAPI is not needed on real NVIDIA hardware

**Game crashes or instability**
- Disable fake NVAPI and restart the game
- Check if the game is compatible with fake NVAPI
- Update to the latest fakenvapi.dll version

### Debug Information

The UI provides detailed status information:

- **NVIDIA Detection Status**: Whether a real NVIDIA GPU was found
- **Fake NVAPI Status**: Whether the fake DLL is loaded and active
- **Error Messages**: Detailed error information if something goes wrong
- **Statistics**: Runtime information about the feature state

## Technical Details

### Implementation

The fake NVAPI system consists of:

1. **Detection Module**: Checks for real NVIDIA hardware
2. **Loading System**: Dynamically loads fake nvapi64.dll
3. **Status Tracking**: Monitors and reports system state
4. **Error Handling**: Graceful fallback when issues occur

### Integration Points

- **Initialization**: Loads during addon startup
- **Settings**: Integrated with Display Commander settings system
- **UI**: Real-time status display and configuration
- **Cleanup**: Proper shutdown and resource cleanup

## Related Projects

- **[fakenvapi](https://github.com/emoose/fakenvapi)**: The underlying fake NVAPI implementation
- **[OptiScaler](https://github.com/emoose/OptiScaler)**: Similar functionality with additional features
- **[dxvk-nvapi](https://github.com/jp7677/dxvk-nvapi)**: DXVK-based NVAPI implementation

## Support

If you encounter issues with the fake NVAPI feature:

1. Check the status display in the Developer tab
2. Review the error messages for specific problems
3. Ensure you have the latest fakenvapi.dll version
4. Try disabling the feature if games become unstable
5. Report issues with detailed error information

## Disclaimer

This feature is provided as-is for experimental purposes. Use at your own risk. The Display Commander team is not responsible for any issues, crashes, or data loss that may occur when using this feature.
