# Known Bugs

## Game Compatibility Issues

### Silent Hill 2 Remake
**Current Behavior:** Game crashes on launch or during gameplay when Display Commander addon is active.

## Workarounds

### Prevent Window Resizing
To prevent Display Commander from resizing or moving the game window, add or modify in the `[DisplayCommander]` section:
```ini
[DisplayCommander]
SuppressWindowChanges=1
```

### Crashes
Setting hook suppression settings to 1 may help prevent crashes. Add or modify the following section:
```ini
[DisplayCommander.HookSuppression]
DxgiSwapchainHooks=1 # DXGI swapchain present hooks for FPS limiting and display management
D3DDeviceHooks=1 # DirectX 11/12 device creation hooks
WindowApiHooks=1 # Window focus/foreground spoofing and window management
ProcessExitHooks=1 # ExitProcess/TerminateProcess hooks for cleanup on exit
SleepHooks=1 # Sleep/SleepEx hooks for time slowdown features
LoadLibraryHooks=1 # DLL loading hooks to install other hooks when modules load
DInputHooks=1 # DirectInput hooks for input remapping
OpenGLHooks=1 # OpenGL swap buffer hooks for OpenGL games
DisplaySettingsHooks=1 # ChangeDisplaySettings hooks for fullscreen prevention
StreamlineHooks=1 # Streamline (DLSS-FG) hooks for frame generation
NvapiHooks=1 # NVAPI hooks for HDR capabilities and Reflex latency markers
NGXHooks=1 # NGX (DLSS) hooks for DLSS integration
XInputHooks=1 # XInput controller hooks for input remapping
HidHooks=1 # HID input suppression hooks for DualSense controllers
```

Setting these values to 1 suppresses the corresponding hooks, which can help avoid compatibility issues with this game.

###