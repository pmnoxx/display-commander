# GPU Completion Measurement

## Overview

GPU completion measurement has been added to the DXGI Present hooks to allow measuring when the GPU has finished rendering work from another thread. This is useful for accurate latency measurement and performance monitoring.

## How It Works

### D3D11
- Creates an `ID3D11Fence` on first use (requires Windows 10+ with D3D11.3)
- Signals the fence from the GPU after each Present call using `ID3D11DeviceContext4::Signal()`
- Sets a Windows event to be triggered when the fence completes using `SetEventOnCompletion()`
- The event handle is stored in `g_gpu_completion_event` for external threads to wait on

### D3D12
- Creates an `ID3D12Fence` on first use
- Sets a Windows event to be triggered when the fence completes
- **Note**: Currently only sets up infrastructure; full implementation requires command queue access during swapchain initialization

## Implementation

### Dedicated Monitoring Thread

The GPU completion measurement now uses a dedicated background thread (`gpu_completion_monitoring.cpp`) that:
- Waits on the GPU completion event in a blocking manner (100ms timeout per iteration)
- Captures the exact moment the GPU completes its work
- Calculates and smooths the GPU duration using exponential moving average (alpha = 64 frames)
- Automatically starts/stops with the addon lifecycle

This provides **accurate GPU completion timestamps** without the polling delay that would occur if checking during the next frame's Present call.

### Thread Management

The monitoring thread:
- Starts automatically when the addon initializes (via `StartGPUCompletionMonitoring()`)
- Stops automatically when the addon unloads (via `StopGPUCompletionMonitoring()`)
- Only actively waits when `g_gpu_measurement_enabled` is true
- Uses a 100ms timeout on `WaitForSingleObject` to allow responsive shutdown

## Usage

### Enable Measurement

#### Via UI
1. Open the ReShade overlay
2. Navigate to the "Main" tab in Display Commander
3. Open the "Frame Time Graph" section
4. Check "Enable GPU Completion Measurement"
5. The "GPU Duration" metric will appear in the timing list below

#### Via Code
```cpp
// Enable GPU completion measurement
g_gpu_measurement_enabled.store(true);
```

### Access GPU Completion Data (Thread-Safe)
```cpp
// Get the last measured GPU completion time
LONGLONG completion_time_ns = g_gpu_completion_time_ns.load();

// Get the smoothed GPU duration
LONGLONG gpu_duration_ns = g_gpu_duration_ns.load();

// Check if measurement is enabled
if (g_gpu_measurement_enabled.load()) {
    LogInfo("GPU measurement is active");
}
```

### GPU Duration Calculation

The dedicated monitoring thread automatically calculates GPU duration:
```cpp
// The monitoring thread handles this automatically:
// 1. Waits for g_gpu_completion_event to be signaled
// 2. Captures completion time with utils::get_now_ns()
// 3. Calculates duration: completion_time - g_present_start_time_ns
// 4. Smooths with exponential moving average (alpha = 64)
// 5. Stores result in g_gpu_duration_ns

// To access the smoothed GPU duration from any thread:
LONGLONG gpu_duration_ns = g_gpu_duration_ns.load();
float gpu_time_ms = gpu_duration_ns / 1000000.0f;
LogInfo("GPU time: %.2f ms", gpu_time_ms);
```

## Global Variables

- `g_gpu_measurement_enabled` - Enable/disable GPU completion measurement
- `g_gpu_completion_event` - Windows event handle that is signaled when GPU completes
- `g_gpu_completion_time_ns` - Last measured GPU completion time (for tracking)
- `g_gpu_duration_ns` - Smoothed GPU duration from Present to completion (displayed in UI)

## Integration Points

### Automatic GPU Completion Tracking

The GPU completion measurement is automatically enqueued after each Present call in:
- `IDXGISwapChain::Present` (Direct3D 9, 10, 11, 12)
- `IDXGISwapChain1::Present1` (Direct3D 10.1+, 11, 12)

GPU duration is automatically calculated by the dedicated monitoring thread (`gpu_completion_monitoring.cpp`):
- Uses blocking wait with 100ms timeout on the GPU completion event
- Captures the **exact moment** the GPU finishes work (no polling delay)
- Calculates duration from Present start to GPU completion
- Smooths the value using exponential moving average (alpha=64)
- Updates `g_gpu_duration_ns` for UI display
- Minimal performance impact - thread sleeps when measurement is disabled

## Requirements

- **D3D11**: Windows 10+ with D3D11.3+ (ID3D11Device5, ID3D11DeviceContext4)
- **D3D12**: Windows 10+ (ID3D12Device, ID3D12Fence)

## Limitations

- D3D12 implementation currently only sets up event infrastructure; command queue signaling needs to be added
- Falls back gracefully if device doesn't support fences
- Only works with DXGI-based APIs (D3D10+, not D3D9)

## Thread Safety

All atomic operations ensure thread-safe access:
- Fence creation is protected by atomic initialization flag
- Event handle is stored atomically
- External threads can safely wait on the event handle

