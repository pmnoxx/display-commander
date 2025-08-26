# VBlank Monitor

The VBlank Monitor is a separate thread that continuously monitors the display's vertical blank (vblank) state and tracks time spent in both vblank and active display periods.

## What it does

- **Monitors display state**: Continuously polls the display to detect when it's in vblank vs. active state
- **Tracks timing**: Measures exactly how long the display spends in each state
- **Logs transitions**: Prints detailed information every time the state changes from active to vblank or vice versa
- **Provides statistics**: Offers comprehensive timing statistics and percentages

## Key Features

### State Detection
- **Active State**: When the display is actively drawing pixels (scanning)
- **VBlank State**: When the display is in vertical blank (between frames)

### Timing Measurements
- **Active → VBlank**: Time spent in active state before entering vblank
- **VBlank → Active**: Time spent in vblank before becoming active again
- **Total percentages**: What percentage of time is spent in each state
- **Average durations**: Mean time spent in each state

### Logging Output
The monitor logs every state transition with precise timing:

```
Active -> VBlank: spent 12.34 ms in active
VBlank -> Active: spent 2.45 ms in vblank
Active -> VBlank: spent 12.33 ms in active
VBlank -> Active: spent 2.46 ms in vblank
```

## Usage

### Basic Usage

```cpp
#include "vblank_monitor.hpp"

using namespace dxgi::fps_limiter;

// Create monitor instance
VBlankMonitor monitor;

// Start monitoring (automatically binds to foreground window)
monitor.StartMonitoring();

// ... let it run ...

// Get statistics
double vblank_percent = monitor.GetVBlankPercentage();
auto avg_vblank = monitor.GetAverageVBlankDuration();
auto avg_active = monitor.GetAverageActiveDuration();

// Stop monitoring
monitor.StopMonitoring();
```

### Binding to Specific Window

```cpp
// Bind to a specific window
HWND hwnd = GetForegroundWindow();
if (monitor.BindToDisplay(hwnd)) {
    monitor.StartMonitoring();
    // ... monitoring ...
    monitor.StopMonitoring();
}
```

### Getting Statistics

```cpp
// Basic stats
uint64_t vblank_count = monitor.GetVBlankCount();
uint64_t state_changes = monitor.GetStateChangeCount();
double vblank_percentage = monitor.GetVBlankPercentage();

// Detailed timing
auto avg_vblank_ms = monitor.GetAverageVBlankDuration();
auto avg_active_ms = monitor.GetAverageActiveDuration();

// Comprehensive statistics
std::string detailed_stats = monitor.GetDetailedStatsString();
std::string last_transition = monitor.GetLastTransitionInfo();
```

## Technical Details

### How it works
1. **Display Binding**: Uses D3DKMT APIs to bind to a specific display adapter
2. **Continuous Polling**: Polls `D3DKMTGetScanLine` every 100 microseconds
3. **State Detection**: Monitors the `InVerticalBlank` flag for state changes
4. **Timing**: Uses high-resolution steady clock for precise measurements
5. **Thread Safety**: All statistics are protected with atomic operations and mutexes

### Dependencies
- Windows D3DKMT APIs (via gdi32.dll)
- C++11 standard library (threads, chrono, atomic)
- No external dependencies beyond Windows system libraries

### Performance
- **CPU Usage**: Minimal - polls every 100 microseconds with small sleep
- **Memory**: Very low - only stores timing statistics and state
- **Accuracy**: High precision timing using steady_clock

## Integration with Existing Code

The VBlankMonitor is designed to be completely independent and can be used alongside the existing `LatentSyncLimiter` class. It provides complementary information:

- **LatentSyncLimiter**: Controls frame timing and pacing
- **VBlankMonitor**: Monitors and reports actual display timing

## Example Output

When running, you'll see output like:

```
VBlank monitoring thread started
VBlank monitor bound to display: 15 characters
VBlank monitoring thread: entering main loop
Active -> VBlank: spent 12.34 ms in active
VBlank -> Active: spent 2.45 ms in vblank
Active -> VBlank: spent 12.33 ms in active
VBlank -> Active: spent 2.46 ms in vblank
...
```

## Use Cases

- **Display timing analysis**: Understand exactly how your display behaves
- **Frame pacing optimization**: Fine-tune frame timing based on actual vblank periods
- **Performance debugging**: Identify display-related performance issues
- **Research and development**: Study display behavior for academic or development purposes

## Notes

- The monitor automatically binds to the foreground window if no specific binding is provided
- All timing is done using `std::chrono::steady_clock` for maximum precision
- The monitoring thread can be started/stopped multiple times safely
- Statistics are cumulative across all monitoring sessions
