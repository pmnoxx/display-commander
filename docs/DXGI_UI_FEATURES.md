# DXGI UI Features Documentation

## Overview

The Swapchain Tab UI has been enhanced to display comprehensive DXGI hook statistics with organized grouping and detailed monitoring capabilities.

## UI Organization

### Event Grouping

The UI now organizes events into collapsible groups for better readability:

1. **ReShade Events (0-37)**: Original ReShade addon events
   - Color: Light Blue
   - Includes: Render passes, command lists, GPU operations, power saving events

2. **DXGI Core Methods (38-47)**: Basic IDXGISwapChain methods
   - Color: Light Green
   - Includes: Present, GetBuffer, SetFullscreenState, GetFullscreenState, GetDesc, ResizeBuffers, ResizeTarget, GetContainingOutput, GetFrameStatistics, GetLastPresentCount

3. **DXGI SwapChain1 Methods (48-58)**: IDXGISwapChain1 interface methods
   - Color: Light Red
   - Includes: GetDesc1, GetFullscreenDesc, GetHwnd, GetCoreWindow, Present1, IsTemporaryMonoSupported, GetRestrictToOutput, SetBackgroundColor, GetBackgroundColor, SetRotation, GetRotation

4. **DXGI SwapChain2 Methods (59-65)**: IDXGISwapChain2 interface methods
   - Color: Light Yellow
   - Includes: SetSourceSize, GetSourceSize, SetMaximumFrameLatency, GetMaximumFrameLatency, GetFrameLatencyWaitableObject, SetMatrixTransform, GetMatrixTransform

5. **DXGI SwapChain3 Methods (66-69)**: IDXGISwapChain3 interface methods
   - Color: Light Cyan
   - Includes: GetCurrentBackBufferIndex, CheckColorSpaceSupport, SetColorSpace1, ResizeBuffers1

## Visual Indicators

### Color Coding
- **Green Text**: Method has been called (count > 0) - Hook is working
- **Red Text**: Method has not been called (count = 0) - Hook may not be working or method not used

### Status Messages
- **✓ DXGI hooks are working correctly**: When DXGI methods are being called
- **⚠ No DXGI method calls detected**: When no DXGI methods have been called
- **Status: Swapchain events are working correctly**: Overall system status

## Summary Statistics

### DXGI Hooks Summary
- **Total DXGI Calls**: Sum of all DXGI method calls
- **Active DXGI Methods**: Number of DXGI methods that have been called (out of 32 total)

### Overall Statistics
- **Total Events (Visible)**: Sum of all visible event counters
- **Hidden Events**: Number of events set to invisible
- **Suppression Rate**: Percentage of suppressed vs total calls (for power saving)

## Event Visibility Control

Each event can be individually hidden by setting `event_visibility[i] = false` in the code. This allows you to focus on specific event types during debugging.

## Monitoring Capabilities

### Real-time Monitoring
- All counters update in real-time as methods are called
- Color changes immediately when methods start being used
- No performance impact on the application

### Debugging Features
- Clear visual indication of which DXGI methods are being used
- Easy identification of unused methods
- Grouped display for better organization
- Summary statistics for quick overview

## Usage Tips

1. **Check DXGI Hooks Summary first** to see if any DXGI methods are being called
2. **Expand specific groups** to see detailed method usage
3. **Look for red text** to identify methods that might not be working
4. **Use the collapsible headers** to focus on specific interface versions
5. **Monitor the counters** during different application states (fullscreen, windowed, etc.)

## Technical Details

- **Total Events**: 70 (38 ReShade + 32 DXGI)
- **DXGI Methods Monitored**: 32 methods across 4 interface versions
- **VTable Indices Covered**: 8-39 (all major DXGI swapchain methods)
- **Thread Safety**: All counters use atomic operations
- **Performance**: Minimal overhead per hook call

---

**Last Updated**: December 2024
**Status**: ✅ Complete and Ready for Use
