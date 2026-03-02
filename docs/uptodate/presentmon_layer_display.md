# PresentMon Layer Display Feature

## Overview
Display PresentMon status and layer information associated with the current game HWND in the main tab.

## Requirements

1. **PresentMon Status Indicator**
   - Show "PresentMon: ON" in the main tab when PresentMon ETW tracing is enabled (checked in Advanced tab)
   - Display should be visible and clear

2. **Layer Information Display**
   - Monitor PresentMon events for information about the current game HWND
   - When a layer/surface is found associated with the game HWND, display:
     - Layer flip compatibility information
     - FlipState information from PresentMon
   - Update dynamically as PresentMon receives new events

3. **Implementation Details**
   - Use `g_last_swapchain_hwnd` to get the current game window handle
   - Use `presentmon::g_presentMonManager.GetRecentFlipCompatibilitySurfaces()` to find surfaces matching the HWND
   - Display flip state from `presentmon::g_presentMonManager.GetFlipState()`
   - Check `settings::g_advancedTabSettings.enable_presentmon_tracing` to determine if PresentMon is enabled

## Technical Approach

### PresentMon Integration
- PresentMon already tracks surfaces with HWND mapping via `UpdateSurfaceWindowMappingFromEvent()`
- Surface cache stores HWND information in `PresentMonSurfaceCompatibilitySummary`
- Use `GetRecentFlipCompatibilitySurfaces()` with appropriate time window (e.g., 10 seconds) to find recent surfaces

### Display Location
- Add PresentMon status indicator near other status indicators in the main tab
- Display layer information in the existing PresentMon section (around line 2997-3133)
- Show layer info when a matching surface is found for the current game HWND

## Implementation Steps

1. ✅ Create task document
2. ✅ Add PresentMon status indicator ("PresentMon: ON") in main tab
3. ✅ Implement function to find layer/surface for current game HWND
4. ✅ Display layer flip compatibility information
5. ✅ Display FlipState information from PresentMon
6. ⏳ Test and verify implementation

## Implementation Details

### PresentMon Status Indicator
- Added "PresentMon: ON" indicator next to the flip state status (line ~2805)
- Only displays when:
  - `settings::g_advancedTabSettings.enable_presentmon_tracing.GetValue()` is true
  - `presentmon::g_presentMonManager.IsRunning()` returns true
- Shows in green color (`ui::colors::TEXT_SUCCESS`)
- Includes tooltip explaining PresentMon is active

### Layer Information Display
- Added new section "Layer Information (Game HWND: 0x%p)" in PresentMon ETW section (line ~3068)
- Uses `g_last_swapchain_hwnd` to get current game window handle
- Calls `GetRecentFlipCompatibilitySurfaces()` with 10 second window to find recent surfaces
- Searches for surface matching the game HWND
- When found, displays:
  - Surface LUID
  - Surface size (width x height)
  - Pixel format (if available)
  - Color space (if available)
  - Flip compatibility flags:
    - Direct Flip Compatible
    - Advanced Direct Flip Compatible
    - Overlay Compatible
    - Overlay Required
    - No Overlapping Content
  - Last update time (age of data)
  - Event count
- Shows appropriate messages when no layer is found or game window is not available

### Code Location
- PresentMon status indicator: `main_new_tab.cpp` line ~2799-2809
- Layer information display: `main_new_tab.cpp` line ~3063-3145

## Code References

- PresentMon Manager: `src/addons/display_commander/presentmon/presentmon_manager.hpp`
- Main Tab UI: `src/addons/display_commander/ui/new_ui/main_new_tab.cpp` (around line 2997)
- Advanced Tab Settings: `src/addons/display_commander/settings/advanced_tab_settings.hpp`
- Game HWND: `g_last_swapchain_hwnd` in `globals.hpp`

## Notes

- PresentMon surface cache uses lock-free hash table with HWND mapping
- Surface compatibility information includes:
  - `is_direct_flip_compatible`
  - `is_advanced_direct_flip_compatible`
  - `is_overlay_compatible`
  - `is_overlay_required`
  - `no_overlapping_content`
- FlipState information includes:
  - `flip_mode` (DxgiBypassMode enum)
  - `present_mode_str` (string description)
  - `debug_info` (additional debug information)
