#pragma once

#include "globals.hpp"
#include "performance_types.hpp"

#include <reshade.hpp>

#include <atomic>

// ============================================================================
// API TYPE ENUM
// ============================================================================


// Forward declaration for OnPresentUpdateAfter2
void OnPresentUpdateAfter2(void* native_device, DeviceTypeDC device_type, bool from_wrapper);

// ============================================================================
// SWAPCHAIN EVENT HANDLERS
// ============================================================================

// Pipeline and resource binding hooks
bool OnBindPipeline(reshade::api::command_list *cmd_list, reshade::api::pipeline_stage stages,
                    reshade::api::pipeline pipeline);

// Device lifecycle hooks
bool OnCreateDevice(reshade::api::device_api api, uint32_t& api_version);
void OnDestroyDevice(reshade::api::device *device);
void OnInitDevice(reshade::api::device *device);

// Effect runtime lifecycle hooks
void OnDestroyEffectRuntime(reshade::api::effect_runtime *runtime);

// Swapchain lifecycle hooks
void OnInitSwapchain(reshade::api::swapchain *swapchain, bool resize);
bool OnCreateSwapchainCapture(reshade::api::device_api api, reshade::api::swapchain_desc &desc, void *hwnd);
void OnDestroySwapchain(reshade::api::swapchain *swapchain, bool resize);

// Centralized initialization method
void DoInitializationWithHwnd(HWND hwnd);

// Present event handlers
void OnPresentUpdateBefore(reshade::api::command_queue *queue, reshade::api::swapchain *swapchain,
                           const reshade::api::rect *source_rect, const reshade::api::rect *dest_rect,
                           uint32_t dirty_rect_count, const reshade::api::rect *dirty_rects);
void OnPresentUpdateAfter2(void* native_device, DeviceTypeDC device_type, bool from_wrapper = false);
void OnPresentFlags2(uint32_t *present_flags, DeviceTypeDC device_type, bool from_present_detour = true, bool from_wrapper = false);

// Auto color space helper
void AutoSetColorSpace(reshade::api::swapchain *swapchain);

// Buffer resolution upgrade event handlers
bool OnCreateResource(reshade::api::device *device, reshade::api::resource_desc &desc,
                      reshade::api::subresource_data *initial_data, reshade::api::resource_usage usage);
bool OnCreateResourceView(reshade::api::device *device, reshade::api::resource resource,
                          reshade::api::resource_usage usage_type, reshade::api::resource_view_desc &desc);
bool OnCreateSampler(reshade::api::device *device, reshade::api::sampler_desc &desc);
void OnDestroyResource(reshade::api::device *device, reshade::api::resource resource);
void OnSetViewport(reshade::api::command_list *cmd_list, uint32_t first, uint32_t count,
                   const reshade::api::viewport *viewports);
void OnSetScissorRects(reshade::api::command_list *cmd_list, uint32_t first, uint32_t count,
                       const reshade::api::rect *rects);

// ============================================================================
// POWER SAVING EVENT HANDLERS
// ============================================================================

// Compute shader power saving
bool OnDispatch(reshade::api::command_list *cmd_list, uint32_t group_count_x, uint32_t group_count_y,
                uint32_t group_count_z);
bool OnDispatchMesh(reshade::api::command_list *cmd_list, uint32_t group_count_x, uint32_t group_count_y,
                    uint32_t group_count_z);
bool OnDispatchRays(reshade::api::command_list *cmd_list, reshade::api::resource raygen, uint64_t raygen_offset,
                    uint64_t raygen_size, reshade::api::resource miss, uint64_t miss_offset, uint64_t miss_size,
                    uint64_t miss_stride, reshade::api::resource hit_group, uint64_t hit_group_offset,
                    uint64_t hit_group_size, uint64_t hit_group_stride, reshade::api::resource callable,
                    uint64_t callable_offset, uint64_t callable_size, uint64_t callable_stride, uint32_t width,
                    uint32_t height, uint32_t depth);

// Resource operation power saving
bool OnCopyResource(reshade::api::command_list *cmd_list, reshade::api::resource source, reshade::api::resource dest);
bool OnUpdateBufferRegion(reshade::api::device *device, const void *data, reshade::api::resource resource,
                          uint64_t offset, uint64_t size);
bool OnUpdateBufferRegionCommand(reshade::api::command_list *cmd_list, const void *data, reshade::api::resource dest,
                                 uint64_t dest_offset, uint64_t size);

// Resource binding and memory operation power saving
bool OnBindResource(reshade::api::command_list *cmd_list, reshade::api::shader_stage stages,
                    reshade::api::descriptor_table table, uint32_t binding, reshade::api::resource_view value);
bool OnMapResource(reshade::api::device *device, reshade::api::resource resource, uint32_t subresource,
                   reshade::api::map_access access, reshade::api::subresource_data *data);
void OnUnmapResource(reshade::api::device *device, reshade::api::resource resource, uint32_t subresource);

// ============================================================================
// POWER SAVING HELPER FUNCTIONS
// ============================================================================

// Helper function to determine if an operation should be suppressed for power saving
bool ShouldBackgroundSuppressOperation();

// ============================================================================
// POWER SAVING SETTINGS
// ============================================================================

// Power saving configuration flags
extern std::atomic<bool> s_suppress_compute_in_background;
extern std::atomic<bool> s_suppress_copy_in_background;
extern std::atomic<bool> s_suppress_binding_in_background;
extern std::atomic<bool> s_suppress_memory_ops_in_background;

// ============================================================================
// SWAPCHAIN UTILITY FUNCTIONS
// ============================================================================

// Get target FPS based on background state
float GetTargetFps();

// DXGI composition state utilities
DxgiBypassMode GetIndependentFlipState(IDXGISwapChain *dxgi_swapchain);
const char *DxgiBypassModeToString(DxgiBypassMode mode);

// Query DXGI composition state - should only be called from DXGI present hooks
void QueryDxgiCompositionState(IDXGISwapChain *dxgi_swapchain);

// ============================================================================
// PERFORMANCE MONITORING FUNCTIONS
// ============================================================================

// Record per-frame FPS sample for background aggregation
void RecordFrameTime(FrameTimeMode reason = FrameTimeMode::kPresent);

// Record native frame time (for frames shown to display via native swapchain Present)
// Similar to RecordFrameTime but specifically for native frames when native_frame_pacing is enabled
void RecordNativeFrameTime();

// ============================================================================
// TIMING VARIABLES
// ============================================================================

// Present after end time tracking (simulation start time)
extern std::atomic<LONGLONG> g_frame_time_ns;
extern std::atomic<LONGLONG> g_sim_start_ns;

// ============================================================================
// INITIALIZATION STATE
// ============================================================================

// Global initialization state
extern std::atomic<bool> g_initialized_with_hwnd;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================


// OnSetFullscreenState function removed - fullscreen prevention now handled directly in IDXGISwapChain_SetFullscreenState_Detour