# DirectComposition Refresh Rate Monitoring Feature

## Overview

Add an optional refresh rate monitoring feature using DirectComposition (DComp) frame statistics. When enabled, create an `IDCompositionDevice` when the ReShade effect runtime is created, call `GetFrameStatistics` to read `DCOMPOSITION_FRAME_STATISTICS.currentCompositionRate` (composition engine rate in FPS), and display it in the UI. The feature is **off by default** and controlled by a checkbox in the **Advanced** tab.

## Workplan (Todo)

- [x] **1. Settings** – Add DComp monitoring setting (default off)
  - [x] Add `BoolSetting enable_dcomposition_refresh_rate_monitoring` in `advanced_tab_settings.hpp`
  - [x] In `advanced_tab_settings.cpp`: init with key `"EnableDCompositionRefreshRateMonitoring"`, default `false`; add to Save/Load and `GetAllSettings`
- [x] **2. DComp module** – New module for device + frame stats
  - [x] Add source/header (e.g. `dcomposition_refresh_rate_monitor.hpp` / `.cpp`) under display_commander
  - [x] Implement create/release of `IDCompositionDevice` (D3D11 or DXGI path, link `dcomp.lib`, include `dcomp.h` / `dcomptypes.h`)
  - [x] Implement `GetDCompCompositionRateHz()` (GetFrameStatistics → currentCompositionRate, handle errors)
  - [x] Implement `StartDCompRefreshRateMonitoring()`, `StopDCompRefreshRateMonitoring()`, `IsDCompRefreshRateMonitoringActive()`
- [x] **3. Lifecycle** – Tie to runtime and setting
  - [x] In `OnInitEffectRuntime`: if setting on, call StartDCompRefreshRateMonitoring()
  - [x] In `OnDestroyEffectRuntime`: if monitoring active, call StopDCompRefreshRateMonitoring()
  - [x] When Advanced checkbox turned off: call StopDCompRefreshRateMonitoring()
  - [x] When Advanced checkbox turned on: if runtime exists, call StartDCompRefreshRateMonitoring()
- [x] **4. Advanced tab UI** – Checkbox and display
  - [x] In `DrawAdvancedTabSettingsSection()` (or suitable subsection): checkbox for `enable_dcomposition_refresh_rate_monitoring` with tooltip
  - [x] On checkbox change: start/stop DComp monitoring as in step 3
  - [x] When monitoring active: show composition rate (e.g. "Composition rate: XX.XX Hz" or "DComp: XX.XX Hz") in Advanced tab (or main tab refresh section)
- [x] **5. InitAdvancedTab** – Start when setting was saved on
  - [x] In `InitAdvancedTab()`: if `enable_dcomposition_refresh_rate_monitoring` is true and a runtime is present, call StartDCompRefreshRateMonitoring()
- [x] **6. CMake / build** – Wire new sources
  - [x] Add new `.cpp` to CMakeLists / addon target so it compiles and links `dcomp.lib`
- [ ] **7. Testing**
  - [ ] Default off; toggle on/off; restart addon and confirm setting persists
  - [ ] Enable with runtime: device created; disable: device released
  - [ ] destroy_effect_runtime with feature on: device released
  - [ ] GetFrameStatistics failure or invalid rational: no crash, UI fallback

## Requirements

1. **Setting (default off)**
   - New boolean setting: enable DirectComposition refresh rate monitoring.
   - Stored in Advanced tab settings; default value `false`.
   - When toggled on: create DComp device when a ReShade runtime exists (or on next `init_effect_runtime`).
   - When toggled off: destroy DComp device and stop sampling.

2. **Advanced tab checkbox**
   - Checkbox in the Advanced tab (e.g. in "Advanced Settings" or a dedicated "Display / Refresh Rate" subsection).
   - Label and tooltip explaining that this uses DComp frame statistics (composition engine rate, not necessarily the physical display rate).

3. **Lifecycle tied to ReShade runtime**
   - Create DComp device when:
     - Setting is on, and
     - ReShade fires `init_effect_runtime` (first or any runtime).
   - Destroy DComp device when:
     - Setting is turned off, or
     - ReShade fires `destroy_effect_runtime` and we are cleaning up that runtime (last runtime when setting is on).

4. **Refresh rate value**
   - Use `IDCompositionDevice::GetFrameStatistics(DCOMPOSITION_FRAME_STATISTICS*)`.
   - Read `currentCompositionRate` (DXGI_RATIONAL: Numerator/Denominator = Hz).
   - Expose current composition rate (e.g. for overlay or Advanced tab).
   - Handle GetFrameStatistics failure or invalid rational (e.g. denominator 0) without crashing.

5. **Display**
   - Show current DComp composition rate somewhere consistent with existing UI (e.g. main tab performance/refresh section, or Advanced tab near the checkbox).
   - Optionally show "DComp: XX.XX Hz" or "Composition rate: XX.XX Hz" when the feature is enabled and data is valid.

## Technical Approach

### DirectComposition API

- **Create device:** `DCompositionCreateDevice` (e.g. with a D3D11 device or `DCompositionCreateDevice2` with DXGI device). Alternatively `DCompositionCreateDevice` with `IDXGIObject` if available; otherwise use minimal D3D11 device for DComp only.
- **Frame stats:** `IDCompositionDevice::GetFrameStatistics(DCOMPOSITION_FRAME_STATISTICS* pStats)`.
- **Reference:** [DCOMPOSITION_FRAME_STATISTICS](https://learn.microsoft.com/en-us/windows/win32/api/DcompTypes/ns-dcomptypes-dcomposition_frame_statistics) – `currentCompositionRate` is the composition engine rate in FPS (DXGI_RATIONAL).

### Lifecycle

- **On init_effect_runtime:** If setting is on and we don’t yet have a DComp device, create it (and optionally start a timer or hook to sample GetFrameStatistics periodically).
- **On destroy_effect_runtime:** If we have a DComp device and this is the last runtime (or we always tear down per-runtime), release the device and clear state.
- **On setting off:** If we have a DComp device, release it and stop sampling.

### Threading / sampling

- Call `GetFrameStatistics` from a single thread (e.g. main/render thread or a dedicated timer). No need to call every frame; e.g. once per second or when the overlay/tab is drawn is enough for a "current composition rate" display.

## Implementation Steps

1. **Create task document** (this file).
2. **Add setting**
   - In `AdvancedTabSettings`: add `BoolSetting enable_dcomposition_refresh_rate_monitoring` (default `false`).
   - In `advanced_tab_settings.cpp`: register key (e.g. `"EnableDCompositionRefreshRateMonitoring"`), default `false`, include in Save/Load and `GetAllSettings` if needed.
3. **Implement DComp refresh rate module**
   - New files (e.g. under `src/addons/display_commander/` or a `dcomposition/` subfolder):  
     - Create/release `IDCompositionDevice` (using D3D11 device or appropriate factory).  
     - Function to get current composition rate (call `GetFrameStatistics`, return Hz from `currentCompositionRate`).  
     - Functions: `StartDCompRefreshRateMonitoring()`, `StopDCompRefreshRateMonitoring()`, `IsDCompRefreshRateMonitoringActive()`, `GetDCompCompositionRateHz()`.
   - Start/stop this module when the setting is toggled and when runtime is created/destroyed (see step 4).
4. **Tie lifecycle to runtime and setting**
   - In `OnInitEffectRuntime`: if setting is on, call start for DComp monitoring (create device if needed).
   - In `OnDestroyEffectRuntime`: if DComp monitoring is active, call stop (release device).
   - When Advanced tab checkbox is turned off: call stop (release device).
   - When Advanced tab checkbox is turned on: if a runtime already exists, call start (create device).
5. **Advanced tab checkbox**
   - In `DrawAdvancedTabSettingsSection()` (or appropriate subsection in `advanced_tab.cpp`): add checkbox bound to `enable_dcomposition_refresh_rate_monitoring`, with tooltip explaining DComp composition rate. On change, start/stop DComp monitoring as above.
6. **Display composition rate**
   - Where appropriate (e.g. main tab performance/refresh area, or Advanced tab under the checkbox): when DComp monitoring is active and rate is valid, show "Composition rate: XX.XX Hz" (or "DComp: XX.XX Hz").
7. **InitAdvancedTab**
   - If setting is loaded as true and a runtime is already present, start DComp monitoring (same as PresentMon pattern in `InitAdvancedTab`).
8. **Testing**
   - Verify checkbox defaults off and survives restart.
   - Verify enabling creates device after runtime init; disabling destroys it.
   - Verify destroy_effect_runtime tears down device when feature was on.
   - Verify GetFrameStatistics failure/invalid rational doesn’t crash and UI shows a sensible fallback.

## Implementation Details (to fill during implementation)

### Setting

- **Key:** `EnableDCompositionRefreshRateMonitoring` (or similar).
- **Default:** `false`.
- **Location:** `settings/advanced_tab_settings.hpp` / `.cpp`.

### DComp module

- **Create device:** Use `DCompositionCreateDevice` with a minimal D3D11 device (or follow ReShade/OS compatibility). Requires linking `dcomp.lib` and including `dcomp.h`, `dcomptypes.h`.
- **GetFrameStatistics:** Call once per second or when drawing the relevant UI; cache last valid rate and timestamp for display.

### Code locations

- **Settings:** `src/addons/display_commander/settings/advanced_tab_settings.hpp`, `advanced_tab_settings.cpp`
- **Advanced tab UI:** `src/addons/display_commander/ui/new_ui/advanced_tab.cpp` (checkbox + optional status/rate display)
- **Runtime lifecycle:** `src/addons/display_commander/main_entry.cpp` (`OnInitEffectRuntime`), `src/addons/display_commander/swapchain_events.cpp` (`OnDestroyEffectRuntime`)
- **Optional main tab display:** `src/addons/display_commander/ui/new_ui/main_new_tab.cpp` (if showing DComp rate in performance/refresh section)

## Code References

- DComp frame statistics: [DCOMPOSITION_FRAME_STATISTICS (dcomptypes.h)](https://learn.microsoft.com/en-us/windows/win32/api/DcompTypes/ns-dcomptypes-dcomposition_frame_statistics)
- IDCompositionDevice: [IDCompositionDevice::GetFrameStatistics](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositiondevice-getframestatistics)
- PresentMon checkbox pattern: `advanced_tab.cpp` (e.g. `enable_presentmon_tracing`), `advanced_tab_settings.hpp`, `InitAdvancedTab()`.

## Notes

- DComp composition rate reflects the **composition engine** (DWM) rate, not necessarily the physical display refresh rate. It can vary with load. Existing WaitForVBlank/DXGI-based refresh rate monitoring remains the source for display-rate measurement.
- DComp requires Windows 8+; ensure runtime checks or OS version checks if supporting older systems.
- Keep DComp device creation/destruction on the same thread as ReShade runtime callbacks if required by DComp threading model.
