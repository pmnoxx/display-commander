# DX11 Proxy UI - Quick Test Guide

## Overview

The DX11 Proxy UI now includes convenient one-click buttons for testing the proxy device functionality, including:
- Quick initialization with 4K test window
- Copy thread controls
- Real-time statistics display

## New UI Features

### 1. Quick Test Button

**Location**: Experimental Tab → DX11 Proxy Device → Manual Controls

**Button**: "Quick Test: Enable + Create 4K Window"

**What it does**:
1. Enables the DX11 proxy setting
2. Enables swapchain creation
3. Creates a 3840x2160 test window
4. Initializes the DX11 proxy device with the test window

**Usage**:
```
1. Open ReShade overlay (typically Home key)
2. Go to "Add-ons" tab
3. Navigate to "Experimental Tab"
4. Expand "DX11 Proxy Device (DX9 to DX11)" section
5. Scroll down to "Manual Controls"
6. Click "Quick Test: Enable + Create 4K Window"
```

**Result**:
- A new 3840x2160 window will appear
- The DX11 proxy device will be initialized
- You can see the initialization status in the "Status" section
- Statistics will show device details and frame counters

### 2. Copy Thread Controls

**Location**: Same section, below the Quick Test button

**Buttons**:
- "Start Copy Thread" - Starts the background copy thread
- "Stop Copy Thread" - Stops the copy thread

**What it does**:
- Starts a background thread that copies frames from source swapchain to destination
- Runs at 1 frame per second (1000ms interval)
- Useful for testing frame copying functionality

**Requirements**:
- DX11 proxy must be initialized
- A swapchain must be created
- Button is disabled if requirements aren't met

**Usage**:
```
1. First initialize the proxy (use Quick Test button or Test Initialize)
2. Enable "Show Statistics" to see copy thread status
3. Click "Start Copy Thread"
4. Watch the statistics - "Frames Copied" will increment once per second
5. Click "Stop Copy Thread" when done testing
```

### 3. Enhanced Statistics Display

**Location**: Enable "Show Statistics" checkbox in the Status section

**New Information Displayed**:
- **Frames Copied**: Number of frames copied by the copy thread
- **Copy Thread Status**: Shows if thread is running or stopped
- **Active Indicator**: Green checkmark when thread is active, shows "Active (1 fps)"

**Example Statistics Panel**:
```
Statistics:
───────────────────────────────

Device:
  Mode: Device + Swapchain
  Swapchain: 3840x2160
  Format: 24

Frames:
  Generated: 0
  Presented: 0
  Copied: 10

Copy Thread:
  Status: Running
  ✓ Active (1 fps)

Lifecycle:
  Initializations: 1
  Shutdowns: 0
```

## Complete Testing Workflow

### Quick Test Workflow

1. **Open ReShade Overlay**: Press Home (or your configured key)
2. **Navigate to DX11 Proxy**: Add-ons → Experimental Tab → DX11 Proxy Device
3. **One-Click Test**: Click "Quick Test: Enable + Create 4K Window"
4. **Verify**: Check that the 4K window appeared and status shows "Initialized"
5. **Enable Stats**: Check "Show Statistics" to see details
6. **Test Copy**: Click "Start Copy Thread" and watch frames being copied
7. **Monitor**: The "Frames Copied" counter increments once per second
8. **Stop**: Click "Stop Copy Thread" when done
9. **Cleanup**: Click "Shutdown" to clean up resources

### Manual Test Workflow

If you want more control:

1. **Enable Settings**:
   - Check "Enable DX11 Proxy Device"
   - Check "Create Own Swapchain"

2. **Configure** (optional):
   - Choose "Swapchain Format" (default: HDR 10-bit)
   - Adjust "Buffer Count" (default: 2)
   - Enable "Allow Tearing (VRR)" if you have a VRR display

3. **Initialize**:
   - Click "Test Initialize" to use current game window
   - OR click "Quick Test: Enable + Create 4K Window" for 4K test

4. **Test Copy Thread**:
   - Click "Start Copy Thread"
   - Monitor statistics

5. **Cleanup**:
   - Click "Stop Copy Thread"
   - Click "Shutdown"

## UI Layout

```
┌─ DX11 Proxy Device (DX9 to DX11) ──────────────────────┐
│                                                         │
│ [WARNING: EXPERIMENTAL]                                 │
│                                                         │
│ Benefits:                                              │
│ • Enable HDR for DX9 games                            │
│ • Modern flip model presentation                       │
│ • Better VRR/G-Sync support                           │
│ • Tearing support for lower latency                    │
│                                                         │
│ ─────────────────────────────────────────────────────  │
│                                                         │
│ [✓] Enable DX11 Proxy Device                          │
│ [✓] Auto-Initialize on DX9 Detection                  │
│ [✓] Create Own Swapchain                              │
│                                                         │
│ ─────────────────────────────────────────────────────  │
│                                                         │
│ Configuration                                          │
│ Swapchain Format: [R10G10B10A2 (HDR 10-bit)  ▼]      │
│ Buffer Count:     [────●────] 2                        │
│ [✓] Allow Tearing (VRR)                               │
│ [ ] Debug Mode                                         │
│                                                         │
│ ─────────────────────────────────────────────────────  │
│                                                         │
│ Status                                                 │
│ ✓ Initialized                                          │
│                                                         │
│ [✓] Show Statistics                                    │
│                                                         │
│ ┌─ Statistics ─────────────────────────────────────┐  │
│ │ Device:                                          │  │
│ │   Mode: Device + Swapchain                       │  │
│ │   Swapchain: 3840x2160                          │  │
│ │   Format: 24                                     │  │
│ │                                                  │  │
│ │ Frames:                                          │  │
│ │   Generated: 0                                   │  │
│ │   Presented: 0                                   │  │
│ │   Copied: 10                                     │  │
│ │                                                  │  │
│ │ Copy Thread:                                     │  │
│ │   Status: Running                                │  │
│ │   ✓ Active (1 fps)                              │  │
│ │                                                  │  │
│ │ Lifecycle:                                       │  │
│ │   Initializations: 1                             │  │
│ │   Shutdowns: 0                                   │  │
│ └──────────────────────────────────────────────────┘  │
│                                                         │
│ ─────────────────────────────────────────────────────  │
│                                                         │
│ Manual Controls                                        │
│                                                         │
│ [Test Initialize]  [Shutdown]                          │
│                                                         │
│ [Quick Test: Enable + Create 4K Window]               │
│                                                         │
│ [Start Copy Thread]                                    │
│                                                         │
│ [Test Frame Generation (+1)]                           │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

## Tooltips

Hover over the `(?)` icons next to each control for detailed help:

- **Quick Test Button**: Explains the 3-step process
- **Start Copy Thread**: Explains background thread operation
- **Other controls**: Detailed explanations for each setting

## Expected Results

### Successful Initialization
- Status indicator shows green checkmark: "✓ Initialized"
- Statistics show device details
- Test window (if created) is visible at 3840x2160

### Successful Copy Thread
- Copy Thread status shows "Running"
- Green indicator: "✓ Active (1 fps)"
- "Frames Copied" counter increments every second

### Test Window
- Title: "DX11 Proxy Test Window - 4K (3840x2160)"
- Size: 3840x2160 client area (plus borders)
- Position: Centered on primary monitor
- Background: Black

## Troubleshooting

### Quick Test Button Does Nothing
- Check ReShade log for error messages
- Verify system supports 3840x2160 windows
- Try "Test Initialize" with game window instead

### Copy Thread Won't Start
- Ensure device is initialized first
- Verify swapchain was created (check statistics)
- Look for error messages in log

### Statistics Not Updating
- Ensure "Show Statistics" is checked
- Refresh the ReShade overlay (close and reopen)
- Check that device is initialized

### Window Not Visible
- Check if window is off-screen (multi-monitor)
- Verify window creation in log
- Try Alt+Tab to find the window

## Technical Details

### Button Implementation
- Single-click operation performs multiple steps
- Automatic error handling and cleanup
- Detailed logging for debugging

### Copy Thread
- Background thread using `std::thread`
- Fixed 1-second interval (1000ms sleep)
- Thread-safe with atomic operations
- Automatically stopped on shutdown

### 4K Test Window
- Standard Windows window class
- Uses `CreateWindowExA` API
- Tracked internally for cleanup
- Can create multiple test windows

## Notes

- The Quick Test button is intended for testing/debugging
- In production, initialization would happen automatically
- Copy thread demonstration copies swapchain to itself (for testing)
- All operations log detailed messages for debugging

## See Also

- `DX11_PROXY_COPY_THREAD_USAGE.md` - Detailed API documentation
- `DX11_PROXY_IMPLEMENTATION.md` - Implementation details
- ReShade log file - For debugging and error messages

