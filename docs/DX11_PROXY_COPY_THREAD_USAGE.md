# DX11 Proxy Copy Thread and Test Window Usage

## Overview

The DX11ProxyManager now includes two new features:

1. **Copy Thread**: A background thread that copies frames from a source swapchain to the proxy swapchain once per second
2. **Test Window Creation**: Ability to create test windows at 4K resolution (3840x2160)

## Copy Thread Feature

### Purpose
The copy thread allows you to periodically copy frames from an existing (source) swapchain to the proxy swapchain. This runs in a background thread and copies at a rate of 1 frame per second.

### Usage Example

```cpp
#include "dx11_proxy/dx11_proxy_manager.hpp"

using namespace dx11_proxy;

// Get the manager instance
auto& proxy_manager = DX11ProxyManager::GetInstance();

// Initialize the proxy device with swapchain
HWND game_hwnd = /* your game window */;
bool success = proxy_manager.Initialize(game_hwnd, 1920, 1080, true);

if (success) {
    // Get your source swapchain (e.g., from the game)
    IDXGISwapChain* source_swapchain = /* your source swapchain */;

    // Start the copy thread
    proxy_manager.StartCopyThread(source_swapchain);

    // The thread will now copy frames once per second

    // Check if thread is running
    if (proxy_manager.IsCopyThreadRunning()) {
        LogInfo("Copy thread is running!");
    }

    // Get statistics
    auto stats = proxy_manager.GetStats();
    LogInfo("Frames copied: %llu", stats.frames_copied);

    // Stop the copy thread when done
    proxy_manager.StopCopyThread();
}

// Cleanup
proxy_manager.Shutdown();
```

### How It Works

1. **Thread Creation**: `StartCopyThread()` creates a background thread that runs in a loop
2. **Frame Copying**: Every second, the thread:
   - Gets the back buffer from the source swapchain
   - Gets the back buffer from the destination (proxy) swapchain
   - Copies the content using `CopyResource` (if dimensions match) or `CopySubresourceRegion` (if they differ)
   - Presents the copied frame
3. **Thread Safety**: The copy operation uses thread-safe atomic operations and proper resource management
4. **Automatic Cleanup**: The thread is automatically stopped during `Shutdown()` or `CleanupResources()`

### Important Notes

- The source swapchain must remain valid while the copy thread is running
- The copy thread automatically handles dimension mismatches between swapchains
- The copy rate is fixed at 1 frame per second (1000ms sleep between copies)
- Copy statistics are tracked in `Stats::frames_copied`

## Test Window Creation

### Purpose
Create test windows at specific resolutions to test the proxy device functionality, particularly useful for testing 4K rendering.

### Usage Example

```cpp
#include "dx11_proxy/dx11_proxy_manager.hpp"

using namespace dx11_proxy;

// Get the manager instance
auto& proxy_manager = DX11ProxyManager::GetInstance();

// Create a 4K test window (3840x2160)
HWND test_window = proxy_manager.CreateTestWindow4K();

if (test_window) {
    LogInfo("Created 4K test window: 0x%p", test_window);

    // Now you can initialize the proxy with this window
    bool success = proxy_manager.Initialize(test_window, 3840, 2160, true);

    if (success) {
        // Use the proxy device with the 4K window
        // ... your rendering code here ...
    }

    // Destroy the test window when done
    proxy_manager.DestroyTestWindow(test_window);
}
```

### Window Specifications

- **Resolution**: 3840x2160 (4K UHD)
- **Window Style**: `WS_OVERLAPPEDWINDOW` (standard window with title bar, borders, and controls)
- **Position**: Centered on the primary monitor (or clamped to screen if too large)
- **Window Class**: `DX11ProxyTestWindow4K`
- **Title**: "DX11 Proxy Test Window - 4K (3840x2160)"
- **Background**: Black

### Multiple Test Windows

The manager tracks all created test windows internally, so you can create multiple test windows:

```cpp
HWND test_window1 = proxy_manager.CreateTestWindow4K();
HWND test_window2 = proxy_manager.CreateTestWindow4K();

// Use both windows...

// Cleanup
proxy_manager.DestroyTestWindow(test_window1);
proxy_manager.DestroyTestWindow(test_window2);
```

## Statistics and Monitoring

You can monitor the proxy manager's activity through the `Stats` structure:

```cpp
auto stats = proxy_manager.GetStats();

// Check initialization status
if (stats.is_initialized) {
    LogInfo("Proxy device is initialized");
}

// Check copy thread status
if (stats.copy_thread_running) {
    LogInfo("Copy thread is active");
}

// Get frame counts
LogInfo("Frames generated: %llu", stats.frames_generated);
LogInfo("Frames copied: %llu", stats.frames_copied);

// Get swapchain info
LogInfo("Swapchain: %dx%d", stats.swapchain_width, stats.swapchain_height);
LogInfo("Format: %d", stats.swapchain_format);
```

## Thread Safety

All public methods are thread-safe:

- `StartCopyThread()` / `StopCopyThread()` - Use atomic flags and proper thread joining
- `CreateTestWindow4K()` / `DestroyTestWindow()` - Track windows in a vector
- `GetStats()` - Returns atomic values safely

## Best Practices

1. **Always check return values**: Both `Initialize()` and `CreateTestWindow4K()` can fail
2. **Clean up resources**: Always call `StopCopyThread()` and `DestroyTestWindow()` when done
3. **Shutdown properly**: Call `Shutdown()` to clean up all resources (automatically stops threads)
4. **Monitor statistics**: Use `GetStats()` to verify the copy thread is working as expected
5. **Handle errors**: Check for null pointers and failed initialization

## Example: Complete Workflow

```cpp
#include "dx11_proxy/dx11_proxy_manager.hpp"

using namespace dx11_proxy;

void TestDX11Proxy() {
    auto& proxy = DX11ProxyManager::GetInstance();

    // Step 1: Create test window
    HWND test_hwnd = proxy.CreateTestWindow4K();
    if (!test_hwnd) {
        LogError("Failed to create test window");
        return;
    }

    // Step 2: Initialize proxy with test window
    if (!proxy.Initialize(test_hwnd, 3840, 2160, true)) {
        LogError("Failed to initialize proxy");
        proxy.DestroyTestWindow(test_hwnd);
        return;
    }

    // Step 3: Get source swapchain (from game/application)
    IDXGISwapChain* source_swapchain = /* your source */;

    // Step 4: Start copy thread
    proxy.StartCopyThread(source_swapchain);

    // Step 5: Let it run for a while (e.g., 10 seconds)
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // Step 6: Check results
    auto stats = proxy.GetStats();
    LogInfo("Copied %llu frames in 10 seconds", stats.frames_copied);

    // Step 7: Cleanup
    proxy.StopCopyThread();
    proxy.DestroyTestWindow(test_hwnd);
    proxy.Shutdown();
}
```

## Future Enhancements

Possible improvements for the copy thread:

1. **Configurable copy rate**: Allow customizing the 1-second interval
2. **Copy on demand**: Add a method to copy a single frame on request
3. **Format conversion**: Support automatic format conversion between swapchains
4. **Multi-threaded copying**: Support multiple copy threads for different swapchains
5. **Error recovery**: Better handling of device lost scenarios

## Troubleshooting

### Copy Thread Not Working

- Verify both swapchains are valid and not null
- Check that the proxy device is initialized with `GetStats().is_initialized`
- Ensure the source swapchain hasn't been destroyed
- Check for D3D11 errors in the log

### Test Window Not Showing

- Verify the window was created successfully (not null)
- Check if the window is off-screen on multi-monitor setups
- Ensure no other application is blocking window creation
- Look for Windows API errors in the log

### Performance Issues

- The 1-second copy rate is intentionally slow; adjust if needed
- Copying large textures (4K) can be expensive
- Consider using device-only mode if swapchain isn't needed

