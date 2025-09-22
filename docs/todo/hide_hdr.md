# HDR Hiding Implementation Plan

## Overview
This document outlines the implementation plan for adding HDR hiding functionality to ReShade Display Commander. The feature will hook DXGI APIs directly to prevent games from detecting HDR capabilities, forcing them to use SDR rendering.

## Background
- **Problem**: Some games automatically enable HDR when available, which can cause issues with ReShade effects or user preferences
- **Solution**: Hook DXGI APIs to hide HDR capabilities from games while maintaining HDR support for the display
- **Architecture**: Use direct DXGI hooks (not through ReShade) for better control and independence

## Technical Approach

### 1. DXGI APIs to Hook
The following DXGI APIs need to be hooked to hide HDR capabilities:

#### Primary HDR Detection APIs:
- `IDXGIOutput::GetDisplayModeList()` - Returns supported display modes
- `IDXGIOutput::FindClosestMatchingMode()` - Finds best matching display mode
- `IDXGIOutput1::GetDisplayModeList1()` - Returns HDR-capable display modes
- `IDXGIOutput1::FindClosestMatchingMode1()` - Finds best matching HDR mode
- `IDXGIOutput6::GetDesc1()` - Returns HDR metadata in output description

#### Secondary HDR Detection APIs:
- `IDXGIFactory::EnumAdapters()` - Enumerates adapters
- `IDXGIFactory1::EnumAdapters1()` - Enumerates adapters with HDR info
- `IDXGIAdapter::EnumOutputs()` - Enumerates outputs
- `IDXGIAdapter1::EnumOutputs()` - Enumerates outputs with HDR info

### 2. Implementation Strategy

#### Phase 1: Core HDR Hiding Infrastructure
1. **Create HDR Hiding Module**
   - Location: `src/addons/display_commander/hooks/dxgi/hdr_hiding/`
   - Files:
     - `hdr_hiding_hooks.hpp` - Hook declarations and function pointers
     - `hdr_hiding_hooks.cpp` - Hook implementations
     - `hdr_hiding_manager.hpp` - HDR hiding state management
     - `hdr_hiding_manager.cpp` - HDR hiding logic

2. **Hook Implementation Pattern**
   ```cpp
   // Example for IDXGIOutput::GetDisplayModeList
   HRESULT STDMETHODCALLTYPE IDXGIOutput_GetDisplayModeList_Detour(
       IDXGIOutput* This,
       DXGI_FORMAT EnumFormat,
       UINT Flags,
       UINT* pNumModes,
       DXGI_MODE_DESC* pDesc
   ) {
       // Call original function
       HRESULT hr = IDXGIOutput_GetDisplayModeList_Original(This, EnumFormat, Flags, pNumModes, pDesc);

       // If HDR hiding is enabled, filter out HDR modes
       if (SUCCEEDED(hr) && g_hdr_hiding_enabled && pDesc) {
           FilterHDRModes(pDesc, pNumModes);
       }

       return hr;
   }
   ```

3. **HDR Mode Filtering Logic**
   - Remove modes with HDR color spaces (DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
   - Remove modes with HDR formats (R10G10B10A2_UNORM, R16G16B16A16_FLOAT)
   - Keep only standard SDR modes (R8G8B8A8_UNORM, etc.)

#### Phase 2: Settings Integration
1. **Add HDR Hiding Settings**
   - Location: `src/addons/display_commander/settings/experimental_tab_settings.hpp`
   - Add new setting:
     ```cpp
     // HDR hiding settings
     BoolSetting hdr_hiding_enabled;
     ComboSetting hdr_hiding_mode; // None, Hide HDR Modes, Force SDR
     ```

2. **UI Integration**
   - Location: `src/addons/display_commander/ui/new_ui/experimental_tab.cpp`
   - Add HDR hiding section with:
     - Enable/disable checkbox
     - Mode selection dropdown
     - Status display
     - Warning about experimental nature

#### Phase 3: Hook Management
1. **Hook Installation**
   - Install hooks during addon initialization
   - Hook both factory and output creation functions
   - Ensure hooks are applied to all created DXGI objects

2. **Hook Lifecycle**
   - Install: During `OnInitDevice` or `OnInitSwapchain`
   - Uninstall: During `OnDestroyDevice` or addon cleanup
   - Reinstall: When new DXGI objects are created

### 3. File Structure

```
src/addons/display_commander/hooks/dxgi/hdr_hiding/
├── hdr_hiding_hooks.hpp          # Hook declarations
├── hdr_hiding_hooks.cpp          # Hook implementations
├── hdr_hiding_manager.hpp        # State management
├── hdr_hiding_manager.cpp        # HDR hiding logic
└── hdr_hiding_utils.hpp          # Utility functions
```

### 4. Key Implementation Details

#### HDR Mode Detection
```cpp
bool IsHDRMode(const DXGI_MODE_DESC& mode) {
    // Check for HDR color space
    if (mode.Format == DXGI_FORMAT_R10G10B10A2_UNORM ||
        mode.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        return true;
    }

    // Check for HDR color space in mode description
    // This would require additional logic based on DXGI version
    return false;
}
```

#### Mode Filtering
```cpp
void FilterHDRModes(DXGI_MODE_DESC* modes, UINT* numModes) {
    if (!modes || !numModes) return;

    UINT originalCount = *numModes;
    UINT filteredCount = 0;

    for (UINT i = 0; i < originalCount; i++) {
        if (!IsHDRMode(modes[i])) {
            if (filteredCount != i) {
                modes[filteredCount] = modes[i];
            }
            filteredCount++;
        }
    }

    *numModes = filteredCount;
}
```

### 5. Integration Points

#### Settings Integration
- Add to `ExperimentalTabSettings` class
- Use existing settings wrapper system
- Follow same pattern as other experimental features

#### UI Integration
- Add new section in experimental tab
- Use existing UI helper functions
- Include proper tooltips and warnings

#### Hook Integration
- Integrate with existing DXGI hook system
- Use MinHook for API hooking
- Follow existing hook management patterns

### 6. Testing Strategy

#### Test Cases
1. **Basic HDR Hiding**
   - Game with HDR support should not detect HDR
   - Game should render in SDR mode
   - No crashes or rendering issues

2. **Mode Enumeration**
   - Verify HDR modes are filtered out
   - Verify SDR modes are preserved
   - Verify mode count is updated correctly

3. **Different Games**
   - Test with various game engines
   - Test with different DXGI versions
   - Test with different HDR implementations

#### Test Games
- Games known to auto-enable HDR
- Games with HDR settings menus
- Games that check HDR capabilities at startup

### 7. Potential Issues and Solutions

#### Issue: Game Crashes
- **Cause**: Incorrect mode filtering or hook implementation
- **Solution**: Careful validation of mode data, proper error handling

#### Issue: HDR Still Detected
- **Cause**: Game uses different HDR detection methods
- **Solution**: Hook additional APIs, implement more comprehensive filtering

#### Issue: Performance Impact
- **Cause**: Excessive hooking or inefficient filtering
- **Solution**: Optimize filtering logic, minimize hook overhead

### 8. Future Enhancements

#### Advanced HDR Hiding
- Hook additional HDR detection APIs
- Support for different HDR standards (HDR10, HDR10+, Dolby Vision)
- Per-game HDR hiding profiles

#### HDR Metadata Manipulation
- Modify HDR metadata instead of hiding it
- Support for HDR tone mapping
- HDR to SDR conversion options

### 9. Implementation Timeline

#### Week 1: Core Infrastructure
- Create HDR hiding module structure
- Implement basic hook framework
- Add settings integration

#### Week 2: Hook Implementation
- Implement primary HDR detection hooks
- Add mode filtering logic
- Test with basic scenarios

#### Week 3: UI and Integration
- Add UI controls
- Integrate with existing systems
- Test with real games

#### Week 4: Testing and Refinement
- Comprehensive testing
- Bug fixes and optimizations
- Documentation updates

### 10. Success Criteria

- [ ] Games do not detect HDR when feature is enabled
- [ ] No crashes or rendering issues
- [ ] Feature can be toggled on/off
- [ ] Works with multiple game engines
- [ ] Minimal performance impact
- [ ] Proper error handling and logging

## Conclusion

This implementation plan provides a comprehensive approach to hiding HDR from games through direct DXGI API hooking. The solution is designed to be robust, performant, and well-integrated with the existing ReShade Display Commander architecture.

The key success factors are:
1. Comprehensive API hooking coverage
2. Proper mode filtering logic
3. Seamless integration with existing systems
4. Thorough testing with real games
5. Clear user interface and documentation
