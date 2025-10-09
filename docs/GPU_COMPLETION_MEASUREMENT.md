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

### Wait for GPU Completion (From Another Thread)
```cpp
// Get the event handle (thread-safe)
HANDLE event = g_gpu_completion_event.load();

if (event != nullptr) {
    // Wait for GPU to complete work (timeout in milliseconds)
    DWORD result = WaitForSingleObject(event, 1000);

    if (result == WAIT_OBJECT_0) {
        // GPU work completed
        LONGLONG completion_time = utils::get_now_ns();

        // Store completion time for UI/monitoring
        g_gpu_completion_time_ns.store(completion_time);

        LogInfo("GPU completed at: %lld ns", completion_time);
    } else if (result == WAIT_TIMEOUT) {
        LogWarn("GPU completion wait timed out");
    } else {
        LogError("GPU completion wait failed");
    }
}
```

### Calculate GPU Time
```cpp
// Measure GPU execution time
LONGLONG present_start = utils::get_now_ns();

// ... Present happens in render thread ...

// In monitoring thread:
HANDLE event = g_gpu_completion_event.load();
if (event != nullptr && WaitForSingleObject(event, 1000) == WAIT_OBJECT_0) {
    LONGLONG present_end = utils::get_now_ns();
    LONGLONG gpu_time_ns = present_end - present_start;

    float gpu_time_ms = gpu_time_ns / 1000000.0f;
    LogInfo("GPU time: %.2f ms", gpu_time_ms);
}
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

GPU duration is automatically calculated in `OnPresentUpdateAfter2()`:
- Uses non-blocking wait (0 timeout) to check if GPU is done
- Calculates duration from Present start to GPU completion
- Smooths the value using exponential moving average (alpha=64)
- Updates `g_gpu_duration_ns` for UI display
- No performance impact when GPU hasn't completed yet

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

