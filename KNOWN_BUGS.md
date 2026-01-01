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
DxgiSwapchainHooks=1 # most likely to cause issues
D3DDeviceHooks=1 # 2nd ost likely
WindowApiHooks=1
ProcessExitHooks=1
SleepHooks=1
LoadLibraryHooks=1
DInputHooks=1
OpenGLHooks=1
DisplaySettingsHooks=1
NvapiHooks=1
StreamlineHooks=1
NGXHooks=1
XInputHooks=1
HidHooks=1
TimeslowdownHooks=1
```

Setting these values to 1 suppresses the corresponding hooks, which can help avoid compatibility issues with this game.

###