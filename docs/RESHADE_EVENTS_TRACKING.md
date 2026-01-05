# ReShade Addon Events Tracking

This document lists all ReShade addon events registered in Display Commander and their tracking status.

## Event Handlers with RECORD_DETOUR_CALL

### Device & Swapchain Events
- ✅ `create_device` → `OnCreateDevice` (swapchain_events.cpp)
- ✅ `destroy_device` → `OnDestroyDevice` (swapchain_events.cpp)
- ✅ `create_swapchain` → `OnCreateSwapchainCapture` (swapchain_events.cpp)
- ✅ `create_swapchain` → `OnCreateSwapchainCapture2` (swapchain_events.cpp, internal)
- ✅ `init_swapchain` → `OnInitSwapchain` (swapchain_events.cpp)

### Effect Runtime Events
- ✅ `init_effect_runtime` → `OnInitEffectRuntime` (main_entry.cpp)
- ✅ `destroy_effect_runtime` → `OnDestroyEffectRuntime` (swapchain_events.cpp)
- ✅ `reshade_open_overlay` → `OnReShadeOverlayOpen` (main_entry.cpp)
- ✅ `reshade_overlay` → `OnReShadeOverlayTest` (main_entry.cpp)
- ✅ `reshade_overlay` → `OnRegisterOverlayDisplayCommander` (main_entry.cpp)

### Present & Rendering Events
- ✅ `present` → `OnPresentUpdateBefore` (swapchain_events.cpp)

### Draw & Dispatch Events
- ✅ `draw` → `OnDraw` (swapchain_events_power_saving.cpp)
- ✅ `draw_indexed` → `OnDrawIndexed` (swapchain_events_power_saving.cpp)
- ✅ `draw_or_dispatch_indirect` → `OnDrawOrDispatchIndirect` (swapchain_events_power_saving.cpp)
- ✅ `dispatch` → `OnDispatch` (swapchain_events_power_saving.cpp)
- ✅ `dispatch_mesh` → `OnDispatchMesh` (swapchain_events_power_saving.cpp)
- ✅ `dispatch_rays` → `OnDispatchRays` (swapchain_events_power_saving.cpp)

### Resource Management Events
- ✅ `create_resource` → `OnCreateResource` (swapchain_events.cpp)
- ✅ `create_resource_view` → `OnCreateResourceView` (swapchain_events.cpp)
- ✅ `create_sampler` → `OnCreateSampler` (swapchain_events.cpp)
- ✅ `copy_resource` → `OnCopyResource` (swapchain_events_power_saving.cpp)
- ✅ `update_buffer_region` → `OnUpdateBufferRegion` (swapchain_events_power_saving.cpp)
- ✅ `update_buffer_region_command` → `OnUpdateBufferRegionCommand` (swapchain_events_power_saving.cpp, commented out in registration)

### Pipeline & State Events
- ✅ `bind_pipeline` → `OnBindPipeline` (swapchain_events.cpp, not registered but has handler)
- ✅ `bind_viewports` → `OnSetViewport` (swapchain_events.cpp)
- ✅ `bind_scissor_rects` → `OnSetScissorRects` (swapchain_events.cpp)

### Memory & Copy Operations (Power Saving)
- ✅ `bind_resource` → `OnBindResource` (swapchain_events_power_saving.cpp, not registered)
- ✅ `map_resource` → `OnMapResource` (swapchain_events_power_saving.cpp, not registered)
- ✅ `unmap_resource` → `OnUnmapResource` (swapchain_events_power_saving.cpp, not registered)
- ✅ `copy_buffer_region` → `OnCopyBufferRegion` (swapchain_events_power_saving.cpp, not registered)
- ✅ `copy_buffer_to_texture` → `OnCopyBufferToTexture` (swapchain_events_power_saving.cpp, not registered)
- ✅ `copy_texture_to_buffer` → `OnCopyTextureToBuffer` (swapchain_events_power_saving.cpp, not registered)
- ✅ `copy_texture_region` → `OnCopyTextureRegion` (swapchain_events_power_saving.cpp, not registered)
- ✅ `resolve_texture_region` → `OnResolveTextureRegion` (swapchain_events_power_saving.cpp, not registered)

## Summary

**Total Event Handlers: 32**
- ✅ All handlers have `RECORD_DETOUR_CALL` added
- All handlers use `utils::get_now_ns()` for timestamp
- All handlers are tracked in the circular buffer for crash reporting

## Notes

- Some handlers exist but are not currently registered (e.g., `OnBindPipeline`, `OnBindResource`, etc.)
- These are still tracked in case they are registered in the future
- The circular buffer stores the last 64 detour calls with timestamps
- Crash handler displays up to 16 most recent calls with time differences

