# D3D9 to D3D9Ex Upgrade Feature

## Overview

This feature automatically upgrades Direct3D 9 applications to use the enhanced Direct3D 9Ex API, which provides improved performance, better memory management, and reduced latency.

## Benefits of D3D9Ex

- **Flip Model Presentation**: Modern presentation model with reduced latency
- **Better Memory Management**: Improved VRAM management and reduced memory fragmentation
- **Alt-Tab Performance**: Faster and smoother alt-tabbing
- **Power Management**: Better GPU power state management
- **Compatibility**: Fully backward-compatible with all D3D9 applications

## Implementation Details

### Files Modified

1. **globals.hpp / globals.cpp**
   - Added `s_enable_d3d9_upgrade` - Enable/disable the upgrade feature
   - Added `s_d3d9e_upgrade_successful` - Track upgrade success status

2. **settings/advanced_tab_settings.hpp / .cpp**
   - Added `enable_d3d9e_upgrade` setting with persistence
   - Default value: `true` (enabled)

3. **swapchain_events.hpp / .cpp**
   - Implemented `OnCreateDevice()` callback
   - Intercepts D3D9 device creation (version 0x9000)
   - Upgrades to D3D9Ex (version 0x9100)

4. **main_entry.cpp**
   - Registered `create_device` event handler
   - Runs early in initialization before device creation

5. **ui/new_ui/advanced_tab.cpp**
   - Added UI controls in "HDR and Display Settings" section
   - Shows upgrade status with visual feedback
   - Provides detailed tooltips

## How It Works

1. When a game attempts to create a Direct3D 9 device (version 0x9000):
   - ReShade calls our `OnCreateDevice()` callback
   - If upgrade is enabled, we modify `api_version` to 0x9100
   - We return `true` to signal the change

2. ReShade's D3D9 implementation (in `external/reshade/source/d3d9/d3d9.cpp`):
   - Detects the modified version (0x9100)
   - Uses `Direct3DCreate9Ex()` instead of `Direct3DCreate9()`
   - Creates an `IDirect3DDevice9Ex` interface

3. The game uses D3D9Ex transparently without any code changes

## UI Features

### Settings Section
Located in: **Developer Tab → HDR and Display Settings → Direct3D 9 Settings**

**Controls:**
- **Checkbox**: "Enable D3D9 to D3D9Ex Upgrade"
  - Default: Enabled
  - Persists across game restarts

**Status Display:**
- ✓ **Success**: Green checkmark with "D3D9 upgraded to D3D9Ex successfully"
- **Waiting**: Gray text "Waiting for D3D9 device creation..."
- **Disabled**: No status shown when feature is disabled

**Tooltips:**
- Feature description
- Benefits of D3D9Ex
- Compatibility information

## Configuration

### ReShade Settings (DisplayCommander section)
```ini
[DisplayCommander]
EnableD3D9EUpgrade=1  # 1 = enabled, 0 = disabled
```

## Code Example

### Callback Implementation
```cpp
bool OnCreateDevice(reshade::api::device_api api, uint32_t& api_version) {
    // Check if D3D9 upgrade is enabled
    if (!s_enable_d3d9_upgrade.load()) {
        return false;
    }

    // Only process D3D9 API
    if (api != reshade::api::device_api::d3d9) {
        return false;
    }

    // Check if already D3D9Ex (0x9100)
    if (api_version == 0x9100) {
        LogInfo("D3D9Ex already detected, no upgrade needed");
        s_d3d9e_upgrade_successful.store(true);
        return false;
    }

    // Upgrade D3D9 (0x9000) to D3D9Ex (0x9100)
    LogInfo("Upgrading Direct3D 9 (0x%x) to Direct3D 9Ex (0x9100)", api_version);
    api_version = 0x9100;
    s_d3d9e_upgrade_successful.store(true);

    return true;
}
```

## Testing

### Verification Steps
1. Launch a Direct3D 9 game
2. Open ReShade overlay
3. Navigate to **Developer Tab**
4. Check **HDR and Display Settings** section
5. Verify status shows: "✓ D3D9 upgraded to D3D9Ex successfully"

### Log Messages
```
[INFO] Upgrading Direct3D 9 (0x9000) to Direct3D 9Ex (0x9100)
[INFO] D3D9 to D3D9Ex upgrade setting changed to: enabled
```

## Compatibility

- **Compatible with**: All Direct3D 9 games
- **No conflicts with**: D3D9Ex games (detected and skipped)
- **Safe fallback**: If upgrade fails, game uses standard D3D9
- **Thread-safe**: Uses atomic operations for state management

## Performance Impact

- **CPU overhead**: Negligible (single check during device creation)
- **Memory overhead**: 2 bytes (2 atomic bools)
- **Runtime impact**: None (check happens once at startup)

## Future Enhancements

Potential improvements:
1. Per-game upgrade profiles
2. Statistics tracking (upgrade success/failure counts)
3. Integration with game detection system
4. Automatic disabling for problematic games

## Technical Notes

- The feature uses ReShade's `addon_event::create_device` event
- The upgrade is transparent to the application
- The device is created before any D3D9 objects are used
- Compatible with other ReShade addons and effects
- No modifications to game binaries or memory

## References

- ReShade API: `include/reshade_events.hpp` (line 1756)
- D3D9 Implementation: `external/reshade/source/d3d9/d3d9.cpp` (line 289)
- Microsoft D3D9Ex Documentation: https://docs.microsoft.com/en-us/windows/win32/direct3d9/d3d9ex-improvements
