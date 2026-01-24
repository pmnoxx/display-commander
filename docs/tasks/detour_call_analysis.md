# Detour Call Analysis - Missing RECORD_DETOUR_CALL

## Task
Find all `_Detour` functions and `reshade_addon` event functions that are missing `RECORD_DETOUR_CALL` macro.

## Part 1: ReShade Addon Event Functions Analysis

### Progress
- [x] main_entry.cpp - All event handlers checked
- [x] swapchain_events.cpp - All event handlers checked
- [x] swapchain_events_power_saving.cpp - All event handlers checked
- [x] addon.cpp - Overlay callback checked

### ReShade Event Handlers Verified (All Have RECORD_DETOUR_CALL)

**Note:** Only functions registered via `reshade::register_event` or `reshade::register_overlay` are included. Functions like `OnPresentFlags2`, `OnPresentUpdateAfter2`, `OnCreateSwapchainCapture2`, and `OnBindPipeline` are NOT registered as ReShade events (they are custom functions or not registered).

#### addon.cpp
- `OnRegisterOverlayDisplayCommander` (line 14, implemented in main_entry.cpp line 176) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_overlay("Display Commander", OnRegisterOverlayDisplayCommander)`

#### main_entry.cpp
- `OnReShadeOverlayTest` (line 357) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::reshade_overlay>(OnReShadeOverlayTest)`
- `OnCreateDevice` (line 2098, implemented in swapchain_events.cpp line 70) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::create_device>(OnCreateDevice)`
- `OnCreateSwapchainCapture` (line 2101, implemented in swapchain_events.cpp line 774) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::create_swapchain>(OnCreateSwapchainCapture)`
- `OnInitSwapchain` (line 2103, implemented in swapchain_events.cpp line 817) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain)`
- `OnInitEffectRuntime` (line 2106, implemented in main_entry.cpp line 283) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::init_effect_runtime>(OnInitEffectRuntime)`
- `OnDestroyEffectRuntime` (line 2107, implemented in swapchain_events.cpp line 151) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::destroy_effect_runtime>(OnDestroyEffectRuntime)`
- `OnReShadeOverlayOpen` (line 2108, implemented in main_entry.cpp line 323) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::reshade_open_overlay>(OnReShadeOverlayOpen)`
- `OnPresentUpdateBefore` (line 2119, implemented in swapchain_events.cpp line 1312) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::present>(OnPresentUpdateBefore)`
- `OnPresentUpdateAfter` (line 2120, implemented in swapchain_events.cpp line 878) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::finish_present>(OnPresentUpdateAfter)`
- `OnDraw` (line 2123, implemented in swapchain_events_power_saving.cpp line 258) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::draw>(OnDraw)`
- `OnDrawIndexed` (line 2124, implemented in swapchain_events_power_saving.cpp line 276) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::draw_indexed>(OnDrawIndexed)`
- `OnDrawOrDispatchIndirect` (line 2125, implemented in swapchain_events_power_saving.cpp line 294) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(OnDrawOrDispatchIndirect)`
- `OnDispatch` (line 2128, implemented in swapchain_events_power_saving.cpp line 30) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::dispatch>(OnDispatch)`
- `OnDispatchMesh` (line 2129, implemented in swapchain_events_power_saving.cpp line 46) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::dispatch_mesh>(OnDispatchMesh)`
- `OnDispatchRays` (line 2130, implemented in swapchain_events_power_saving.cpp line 62) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::dispatch_rays>(OnDispatchRays)`
- `OnCopyResource` (line 2131, implemented in swapchain_events_power_saving.cpp line 82) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::copy_resource>(OnCopyResource)`
- `OnUpdateBufferRegion` (line 2132, implemented in swapchain_events_power_saving.cpp line 97) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion)`
- `OnCreateResource` (line 2136, implemented in swapchain_events.cpp line 1520) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::create_resource>(OnCreateResource)`
- `OnCreateResourceView` (line 2137, implemented in swapchain_events.cpp line 1754) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::create_resource_view>(OnCreateResourceView)`
- `OnCreateSampler` (line 2138, implemented in swapchain_events.cpp line 1585) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::create_sampler>(OnCreateSampler)`
- `OnSetViewport` (line 2139, implemented in swapchain_events.cpp line 1804) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::bind_viewports>(OnSetViewport)`
- `OnSetScissorRects` (line 2140, implemented in swapchain_events.cpp line 1842) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::bind_scissor_rects>(OnSetScissorRects)`
- `OnDestroyDevice` (line 2144, implemented in swapchain_events.cpp line 113) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice)`
- `OnInitDevice` (line 2145, implemented in swapchain_events.cpp line 104) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::init_device>(OnInitDevice)`
- `OnInitCommandList` (line 2148, implemented in main_entry.cpp line 208) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::init_command_list>(OnInitCommandList)`
- `OnDestroyCommandList` (line 2149, implemented in main_entry.cpp line 217) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::destroy_command_list>(OnDestroyCommandList)`
- `OnInitCommandQueue` (line 2150, implemented in main_entry.cpp line 226) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::init_command_queue>(OnInitCommandQueue)`
- `OnDestroyCommandQueue` (line 2151, implemented in main_entry.cpp line 235) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::destroy_command_queue>(OnDestroyCommandQueue)`
- `OnExecuteCommandList` (line 2152, implemented in main_entry.cpp line 244) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::execute_command_list>(OnExecuteCommandList)`
- `OnDestroySwapchain` (line 2155, implemented in swapchain_events.cpp line 808) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain)`
- `OnDestroyResource` (line 2156, implemented in swapchain_events.cpp line 1511) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::destroy_resource>(OnDestroyResource)`
- `OnFinishPresent` (line 2159, implemented in main_entry.cpp line 253) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::finish_present>(OnFinishPresent)`
- `OnReShadeBeginEffects` (line 2162, implemented in main_entry.cpp line 262) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::reshade_begin_effects>(OnReShadeBeginEffects)`
- `OnReShadeFinishEffects` (line 2163, implemented in main_entry.cpp line 272) - ✓ Has RECORD_DETOUR_CALL
  - Registered via: `reshade::register_event<reshade::addon_event::reshade_finish_effects>(OnReShadeFinishEffects)`

### Functions NOT Registered as ReShade Events (Excluded from Analysis)
- `OnBindPipeline` - Function exists but NOT registered via `register_event` (no `bind_pipeline` event registration found)
- `OnPresentFlags2` - Custom function called from detours, NOT a ReShade event
- `OnPresentUpdateAfter2` - Custom function called from detours, NOT a ReShade event
- `OnCreateSwapchainCapture2` - Helper function called from `OnCreateSwapchainCapture`, NOT directly registered
- `OnUpdateBufferRegionCommand` - Commented out in registration (line 2133), NOT registered

### Summary - ReShade Event Handlers
- Total registered event handlers checked: 35
- Event handlers with RECORD_DETOUR_CALL: 35
- Event handlers missing RECORD_DETOUR_CALL: 0
- Overlay callbacks checked: 1 (OnRegisterOverlayDisplayCommander)
- Overlay callbacks with RECORD_DETOUR_CALL: 1

## Part 2: _Detour Functions Analysis

## Task
Find all `_Detour` functions that are missing `RECORD_DETOUR_CALL` macro.

## Progress
- [x] api_hooks.cpp - All verified
- [x] ngx_hooks.cpp - All verified
- [x] loadlibrary_hooks.cpp - All verified
- [x] windows_message_hooks.cpp - All verified
- [x] nvapi_hooks.cpp - All verified
- [x] window_proc_hooks.cpp - Fixed ✓
- [x] dxgi_present_hooks.cpp - All verified
- [x] vulkan_hooks.cpp - Fixed ✓
- [ ] sleep_hooks.cpp
- [x] streamline_hooks.cpp - All verified
- [ ] debug_output_hooks.cpp
- [x] opengl_hooks.cpp - Fixed ✓
- [x] d3d9_present_hooks.cpp - Fixed ✓
- [x] dinput_hooks.cpp - All verified
- [ ] timeslowdown_hooks.cpp
- [x] display_settings_hooks.cpp - Fixed ✓
- [ ] windows_gaming_input_hooks.cpp
- [ ] rand_hooks.cpp
- [ ] hid_suppression_hooks.cpp
- [ ] hid_additional_hooks.cpp
- [ ] process_exit_hooks.cpp
- [x] xinput_hooks.cpp - Fixed ✓

## Results

### Files with missing RECORD_DETOUR_CALL

#### process_exit_hooks.cpp
- `ExitProcess_Detour` (line 22) - Missing RECORD_DETOUR_CALL
- `TerminateProcess_Detour` (line 36) - Missing RECORD_DETOUR_CALL

#### sleep_hooks.cpp
- `Sleep_Detour` (line 31) - Missing RECORD_DETOUR_CALL
- `SleepEx_Detour` (line 81) - Missing RECORD_DETOUR_CALL
- `WaitForSingleObject_Detour` (line 131) - Missing RECORD_DETOUR_CALL
- `WaitForMultipleObjects_Detour` (line 182) - Missing RECORD_DETOUR_CALL

#### timeslowdown_hooks.cpp
- `QueryPerformanceCounter_Detour` (line 311) - Missing RECORD_DETOUR_CALL
- `QueryPerformanceFrequency_Detour` (line 403) - Missing RECORD_DETOUR_CALL
- `GetTickCount_Detour` (line 416) - Missing RECORD_DETOUR_CALL
- `GetTickCount64_Detour` (line 435) - Missing RECORD_DETOUR_CALL
- `timeGetTime_Detour` (line 473) - Missing RECORD_DETOUR_CALL
- `GetSystemTime_Detour` (line 499) - Missing RECORD_DETOUR_CALL
- `GetSystemTimeAsFileTime_Detour` (line 538) - Missing RECORD_DETOUR_CALL
- `GetSystemTimePreciseAsFileTime_Detour` (line 571) - Missing RECORD_DETOUR_CALL
- `GetLocalTime_Detour` (line 604) - Missing RECORD_DETOUR_CALL
- `NtQuerySystemTime_Detour` (line 643) - Missing RECORD_DETOUR_CALL

#### rand_hooks.cpp
- `Rand_Detour` (line 25) - Missing RECORD_DETOUR_CALL
- `Rand_s_Detour` (line 45) - Missing RECORD_DETOUR_CALL


#### debug_output_hooks.cpp
- `OutputDebugStringA_Detour` (line 52) - Missing RECORD_DETOUR_CALL
- `OutputDebugStringW_Detour` (line 74) - Missing RECORD_DETOUR_CALL

#### windows_gaming_input_hooks.cpp
- `RoGetActivationFactory_Detour` (line 45) - Missing RECORD_DETOUR_CALL

#### hid_suppression_hooks.cpp
- `ReadFile_Detour` (line 62) - Missing RECORD_DETOUR_CALL
- `HidD_GetInputReport_Detour` (line 111) - Missing RECORD_DETOUR_CALL
- `HidD_GetAttributes_Detour` (line 136) - Missing RECORD_DETOUR_CALL
- `CreateFileA_Detour` (line 216) - Missing RECORD_DETOUR_CALL
- `CreateFileW_Detour` (line 283) - Missing RECORD_DETOUR_CALL

#### hid_additional_hooks.cpp
- `WriteFile_Detour` (line 30) - Missing RECORD_DETOUR_CALL
- `DeviceIoControl_Detour` (line 51) - Missing RECORD_DETOUR_CALL
- `HidD_GetPreparsedData_Detour` (line 72) - Missing RECORD_DETOUR_CALL
- `HidD_FreePreparsedData_Detour` (line 93) - Missing RECORD_DETOUR_CALL
- `HidP_GetCaps_Detour` (line 114) - Missing RECORD_DETOUR_CALL
- `HidD_GetManufacturerString_Detour` (line 135) - Missing RECORD_DETOUR_CALL
- `HidD_GetProductString_Detour` (line 156) - Missing RECORD_DETOUR_CALL
- `HidD_GetSerialNumberString_Detour` (line 177) - Missing RECORD_DETOUR_CALL
- `HidD_GetNumInputBuffers_Detour` (line 198) - Missing RECORD_DETOUR_CALL
- `HidD_SetNumInputBuffers_Detour` (line 219) - Missing RECORD_DETOUR_CALL
- `HidD_GetFeature_Detour` (line 240) - Missing RECORD_DETOUR_CALL
- `HidD_SetFeature_Detour` (line 261) - Missing RECORD_DETOUR_CALL

## Summary
- Total files checked: 15
- Files with missing RECORD_DETOUR_CALL: 9
- Total functions missing RECORD_DETOUR_CALL: 36
- Functions fixed: 26 (window_proc_hooks: 1, display_settings_hooks: 5, d3d9_present_hooks: 2, vulkan_hooks: 7, opengl_hooks: 15, xinput_hooks: 4)

## Files Verified (All Have RECORD_DETOUR_CALL)
- api_hooks.cpp - All 20 _Detour functions have RECORD_DETOUR_CALL
- ngx_hooks.cpp - All 26 _Detour functions have RECORD_DETOUR_CALL
- loadlibrary_hooks.cpp - All 6 _Detour functions have RECORD_DETOUR_CALL
- windows_message_hooks.cpp - All 39 _Detour functions have RECORD_DETOUR_CALL
- nvapi_hooks.cpp - All 5 _Detour functions have RECORD_DETOUR_CALL
- dxgi_present_hooks.cpp - All 38 _Detour functions have RECORD_DETOUR_CALL
- dinput_hooks.cpp - All 5 _Detour functions have RECORD_DETOUR_CALL
- streamline_hooks.cpp - All 4 _Detour functions have RECORD_DETOUR_CALL
- window_proc_hooks.cpp - All 1 _Detour functions have RECORD_DETOUR_CALL ✓ Fixed
- display_settings_hooks.cpp - All 5 _Detour functions have RECORD_DETOUR_CALL ✓ Fixed
- d3d9_present_hooks.cpp - All 2 _Detour functions have RECORD_DETOUR_CALL ✓ Fixed
- vulkan_hooks.cpp - All 7 _Detour functions have RECORD_DETOUR_CALL ✓ Fixed
- opengl_hooks.cpp - All 15 _Detour functions have RECORD_DETOUR_CALL ✓ Fixed
- xinput_hooks.cpp - All 4 _Detour_Impl functions have RECORD_DETOUR_CALL ✓ Fixed
