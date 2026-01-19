# PCL Latency Markers Reporting

This document describes how PCL (PC Latency) latency markers reporting works in Display Commander, including the ETW (Event Tracing for Windows) mechanism used to communicate with NVIDIA's overlay and other consumers.

## Overview

PCL Stats is part of NVIDIA Reflex, enabling measurement of key latency components in the rendering pipeline. Display Commander implements a Special-K compatible PCLStats ETW provider that emits latency markers to consumers like NVIDIA's overlay, FrameView, and other diagnostic tools.

## Architecture

### Dual Reporting Path

Display Commander reports latency markers through two parallel paths:

1. **NVAPI Path**: Direct reporting to NVIDIA driver via `NvAPI_D3D_SetLatencyMarker`
   - Used by NVIDIA driver for internal latency calculations
   - Required for Reflex low latency mode functionality
   - Located in: `src/addons/display_commander/nvapi/reflex_manager.cpp`

2. **ETW Path**: Event Tracing for Windows provider for external consumers
   - Used by NVIDIA overlay, FrameView, and other diagnostic tools
   - Special-K compatible implementation
   - Located in: `src/addons/display_commander/latency/pclstats_etw.cpp`

### Integration Flow

```
Game Frame Loop
    ↓
swapchain_events.cpp (OnPresentBefore/After)
    ↓
LatencyManager::SetMarker()
    ↓
ReflexProvider::SetMarker()
    ↓
ReflexManager::SetMarker()
    ├─→ NvAPI_D3D_SetLatencyMarker_Direct()  [NVAPI Path]
    └─→ pclstats_etw::EmitMarker()            [ETW Path]
```

## ETW Provider Details

### Provider Registration

- **Provider Name**: `"PCLStatsTraceLoggingProvider"`
- **Provider GUID**: `{0D216F06-82A6-4D49-BC4F-8F38AE56EFAB}`
- **Registration**: Uses `TraceLoggingRegisterEx()` with enable/disable callback

### Event Schema

#### Primary Marker Event: `PCLStatsEvent`

```cpp
Event Name: "PCLStatsEvent"
Fields:
  - Marker: UInt32    // Marker type ID (see Marker IDs section)
  - FrameID: UInt64   // Frame counter (monotonically increasing)
```

#### Lifecycle Events

- **`PCLStatsInit`**: Emitted when provider is registered and when ETW consumer enables the provider
  - Helps NVIDIA overlay discover the provider
  - Re-emitted on provider enable for discovery

- **`PCLStatsShutdown`**: Emitted when provider is unregistered
  - Signals clean shutdown to consumers

- **`PCLStatsFlags`**: Emitted on `EVENT_CONTROL_CODE_CAPTURE_STATE`
  - Used for state capture by consumers
  - Currently emits with Flags=0

### ETW Consumer Control

The provider is controlled by ETW consumers through control codes:

- **`EVENT_CONTROL_CODE_ENABLE_PROVIDER`**: Consumer enables provider
  - Sets `g_etw_enabled = true`
  - Re-emits `PCLStatsInit` event for discovery
  - Provider only emits markers when enabled

- **`EVENT_CONTROL_CODE_DISABLE_PROVIDER`**: Consumer disables provider
  - Sets `g_etw_enabled = false`
  - Stops marker emission (reduces overhead)

- **`EVENT_CONTROL_CODE_CAPTURE_STATE`**: Consumer requests state capture
  - Emits `PCLStatsFlags` event with current flags

## Marker IDs and Semantics

Markers follow NVIDIA's `NV_LATENCY_MARKER_TYPE` enumeration:

| ID | Name | Description | When Emitted |
|----|------|-------------|--------------|
| 0 | `SIMULATION_START` | Start of game simulation/logic | Beginning of frame processing |
| 1 | `SIMULATION_END` | End of game simulation/logic | After simulation completes |
| 2 | `RENDERSUBMIT_START` | Start of render command submission | Before draw calls |
| 3 | `RENDERSUBMIT_END` | End of render command submission | After all draw calls |
| 4 | `PRESENT_START` | Start of present operation | Before `Present()` call |
| 5 | `PRESENT_END` | End of present operation | After `Present()` completes |
| 7 | `TRIGGER_FLASH` | Flash trigger marker | Special trigger events |
| 8 | `PC_LATENCY_PING` | Periodic ping marker | Injected periodically (100-300ms) |
| 9-12 | Out-of-band variants | Extended marker types | Special use cases |
| 13 | `CONTROLLER_INPUT_SAMPLE` | Input sample marker | Input polling events |

### Frame ID

The `FrameID` is a monotonically increasing counter stored in `g_global_frame_id`:
- Incremented once per frame in `swapchain_events.cpp` (OnPresentBefore)
- Shared between NVAPI and ETW paths for consistency
- All markers for a given frame use the same `FrameID`

## Ping Mechanism

Special-K style periodic ping injection:

1. **Ping Thread**: Background thread runs when provider is initialized
   - Wakes at randomized intervals (100-300ms)
   - Sets edge-triggered signal when provider is enabled

2. **Ping Signal**: Atomic flag (`g_ping_signal`)
   - Set by ping thread when interval elapses
   - Consumed once per ping via `ConsumePingSignal()`
   - Cleared atomically when consumed

3. **Ping Injection**: On `SIMULATION_START`
   - Checks `ConsumePingSignal()`
   - If signaled, injects `PC_LATENCY_PING` marker (ID=8)
   - Uses same frame ID as current frame

## Multiple Listeners Support

**Yes, ETW supports multiple listeners!**

ETW (Event Tracing for Windows) is designed to support multiple consumers simultaneously. When a TraceLogging provider emits an event:

1. **Multiple Consumers**: Any number of ETW sessions can subscribe to the same provider
2. **Independent Sessions**: Each consumer runs in its own ETW session
3. **No Interference**: Consumers don't interfere with each other
4. **Provider Enable/Disable**: Each consumer can independently enable/disable the provider
   - Provider emits events if **any** consumer has it enabled
   - Provider callback receives enable/disable notifications from all consumers

### Practical Implications

- **NVIDIA Overlay**: Can listen to PCLStats events
- **FrameView**: Can simultaneously listen to the same events
- **Custom Loggers**: Can add additional listeners (like Display Commander's log file feature)
- **All Active Simultaneously**: All listeners receive the same events

### Implementation Note

Display Commander's ETW listener implementation:
- Uses `TraceLoggingRegisterEx()` to register as a provider (emitter)
- Can also use ETW consumer APIs to listen to its own events (for logging)
- However, it's more efficient to log directly in `EmitMarker()` rather than consuming via ETW

## Marker Emission Points

Markers are emitted at specific points in the rendering pipeline:

### OnPresentBefore (Frame Start)

```cpp
// swapchain_events.cpp - OnPresentBefore
g_global_frame_id.fetch_add(1);  // Increment frame counter

if (reflex_enabled && generate_markers) {
    g_latencyManager->SetMarker(LatencyMarkerType::SIMULATION_START);
}
```

### During Frame Processing

```cpp
// swapchain_events.cpp - Various points
g_latencyManager->SetMarker(LatencyMarkerType::SIMULATION_END);
g_latencyManager->SetMarker(LatencyMarkerType::RENDERSUBMIT_START);
g_latencyManager->SetMarker(LatencyMarkerType::RENDERSUBMIT_END);
```

### OnPresentAfter (Frame End)

```cpp
// swapchain_events.cpp - OnPresentAfter
g_latencyManager->SetMarker(LatencyMarkerType::PRESENT_START);
g_latencyManager->SetMarker(LatencyMarkerType::PRESENT_END);
```

## Native Reflex Detection

Display Commander detects when games use native Reflex:

- **Detection**: Hooks `NvAPI_D3D_SetLatencyMarker` and detects game calls
- **Behavior**: When native Reflex is detected:
  - PCLStats ETW reporting is automatically disabled
  - Prevents duplicate/conflicting marker streams
  - Mirrors Special-K's behavior

## Log File Feature

Display Commander includes a toggleable log file feature (default: OFF) that logs all PCLStats events:

- **Location**: `DisplayCommander_PCLStats.log` in addon directory
- **Format**: CSV-like format with timestamp, marker ID, frame ID
- **Filtering**: Can filter by marker ID (matches ID used for NVIDIA overlay)
- **Performance**: Minimal overhead when disabled (no file I/O)

See `src/addons/display_commander/latency/pclstats_logger.cpp` for implementation.

## Debugging and Diagnostics

### Statistics Tracking

The implementation tracks:
- Total events emitted
- Per-marker-type counts
- Last marker type and frame ID
- Provider registration status
- ETW enable/disable state
- Ping signal generation/consumption

### Marker History

First 100 markers are stored in memory for debugging:
- Marker type, frame ID, timestamp
- Accessible via `GetMarkerHistory()` API
- Displayed in Experimental tab UI

### Test Functions

- **`EmitTestMarker()`**: Manually emit a test marker (SIMULATION_START)
- **`ReEmitInitEvent()`**: Re-emit PCLStatsInit for overlay discovery

## References

- Special-K Implementation: `docs/SPECIALK_PCLSTATS_REPORTING.md`
- NVIDIA Reflex Documentation: https://developer.nvidia.com/reflex
- ETW TraceLogging: https://docs.microsoft.com/en-us/windows/win32/etw/tracelogging
- PCLStats Header: NVIDIA Reflex SDK
