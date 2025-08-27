# Display Timing Query

This module provides functionality to query detailed display timing information using the Windows Display Configuration API, similar to how Special-K retrieves and logs display information.

## Overview

The `QueryDisplayTimingInfo()` function retrieves the same timing data that Special-K uses to generate its display logs:

```
/24/2025 03:26:48.043: [RenderBase]  (    DEL AW3423DW ) :: PixelClock= 799.8 MHz, vSyncFreq=143.975 Hz, hSyncFreq=222.153 kHz, activeSize=(3440x1440), totalSize=(3600x1543), Standard=N/A
```

## Key Features

- **Pixel Clock**: Display pixel clock in MHz
- **VSync Frequency**: Vertical refresh rate in Hz
- **HSync Frequency**: Horizontal sync frequency in kHz
- **Active Size**: Visible display resolution (e.g., 3440x1440)
- **Total Size**: Total scan area including blanking (e.g., 3600x1543)
- **Video Standard**: Display timing standard

## Usage

### Basic Usage

```cpp
#include "display/query_display.hpp"

// Get timing info for all active displays
auto timing_info = QueryDisplayTimingInfo();

for (const auto& timing : timing_info) {
    // Get formatted string similar to Special-K log format
    std::wstring formatted = timing.GetFormattedString();
    
    // Or access individual values
    double pixel_clock_mhz = timing.GetPixelClockMHz();
    double vsync_hz = timing.GetVSyncFreqHz();
    double hsync_khz = timing.GetHSyncFreqKHz();
    
    // Log the information
    LogInfo(formatted.c_str());
}
```

### Query Specific Monitor

```cpp
HMONITOR monitor = GetPrimaryMonitor(); // Get monitor handle
auto* timing = QueryDisplayTimingInfoForMonitor(monitor);

if (timing) {
    std::wstring formatted = timing->GetFormattedString();
    LogInfo(formatted.c_str());
}
```

### Demonstration Function

```cpp
// Log all display timing information (similar to Special-K)
LogAllDisplayTimingInfo();
```

## Data Source

The timing information comes directly from the Windows Display Configuration API:

- `GetDisplayConfigBufferSizes()` - Gets required buffer sizes
- `QueryDisplayConfig()` - Retrieves display configuration data
- `DISPLAYCONFIG_MODE_INFO.targetMode.targetVideoSignalInfo` - Contains timing data

This is the same API that Special-K uses, ensuring accuracy and consistency.

## Structure Members

```cpp
struct DisplayTimingInfo {
    std::wstring device_name;           // Device identifier
    std::wstring friendly_name;         // Human-readable name
    uint32_t pixel_clock_hz;           // Pixel clock in Hz
    uint32_t hsync_freq_numerator;     // HSync frequency numerator
    uint32_t hsync_freq_denominator;   // HSync frequency denominator
    uint32_t vsync_freq_numerator;     // VSync frequency numerator
    uint32_t vsync_freq_denominator;   // VSync frequency denominator
    uint32_t active_width;             // Active display width
    uint32_t active_height;            // Active display height
    uint32_t total_width;              // Total scan width
    uint32_t total_height;             // Total scan height
    uint32_t video_standard;           // Video timing standard
};
```

## Helper Methods

- `GetPixelClockMHz()` - Returns pixel clock in MHz
- `GetHSyncFreqHz()` - Returns horizontal sync frequency in Hz
- `GetHSyncFreqKHz()` - Returns horizontal sync frequency in kHz
- `GetVSyncFreqHz()` - Returns vertical sync frequency in Hz
- `GetFormattedString()` - Returns formatted string matching Special-K log format

## Integration with Special-K

This module provides the same display timing data that Special-K uses for:

- Frame rate limiting calculations
- Latent sync timing
- Display mode validation
- Performance monitoring
- Debug logging

## Notes

- The current implementation focuses on timing data retrieval
- Device name and friendly name matching with DXGI outputs could be enhanced
- Monitor-specific queries could be improved with better DISPLAYCONFIG-to-DXGI mapping
- All timing data comes directly from Windows graphics drivers via DISPLAYCONFIG API
