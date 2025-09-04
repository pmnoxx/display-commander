#pragma once

#include "addon.hpp"
#include <atomic>

// ============================================================================
// SWAPCHAIN EVENT HANDLERS
// ============================================================================


// Draw event handlers for render timing and power saving
// These are now implemented in swapchain_events_power_saving.cpp

// Command list and queue lifecycle hooks
//void OnInitCommandList(reshade::api::command_list* cmd_list);
//void OnInitCommandQueue(reshade::api::command_queue* queue);//
//void OnResetCommandList(reshade::api::command_list* cmd_list);
//void OnExecuteCommandList(reshade::api::command_queue* queue, reshade::api::command_list* cmd_list);

// Pipeline and resource binding hooks
bool OnBindPipeline(reshade::api::command_list* cmd_list, reshade::api::pipeline_stage stages, reshade::api::pipeline pipeline);

// Swapchain lifecycle hooks
void OnInitSwapchain(reshade::api::swapchain* swapchain, bool resize);
bool OnCreateSwapchainCapture(reshade::api::device_api api, reshade::api::swapchain_desc& desc, void* hwnd);

// Present event handlers
void OnPresentUpdateBefore(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain, const reshade::api::rect* source_rect, const reshade::api::rect* dest_rect, uint32_t dirty_rect_count, const reshade::api::rect* dirty_rects);
void OnPresentUpdateBefore2(reshade::api::effect_runtime* runtime);
void OnPresentUpdateAfter(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain);
void OnPresentFlags(uint32_t* present_flags, reshade::api::swapchain* swapchain);

// Resource view creation event handler to fix format mismatches
bool OnCreateResourceView(reshade::api::device* device, reshade::api::resource resource, reshade::api::resource_usage usage_type, reshade::api::resource_view_desc& desc);

// ============================================================================
// POWER SAVING EVENT HANDLERS
// ============================================================================

// Compute shader power saving
bool OnDispatch(reshade::api::command_list* cmd_list, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);
bool OnDispatchMesh(reshade::api::command_list* cmd_list, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);
bool OnDispatchRays(reshade::api::command_list* cmd_list, reshade::api::resource raygen, uint64_t raygen_offset, uint64_t raygen_size, reshade::api::resource miss, uint64_t miss_offset, uint64_t miss_size, uint64_t miss_stride, reshade::api::resource hit_group, uint64_t hit_group_offset, uint64_t hit_group_size, uint64_t hit_group_stride, reshade::api::resource callable, uint64_t callable_offset, uint64_t callable_size, uint64_t callable_stride, uint32_t width, uint32_t height, uint32_t depth);

// Resource operation power saving
bool OnCopyResource(reshade::api::command_list* cmd_list, reshade::api::resource source, reshade::api::resource dest);
bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size);
bool OnUpdateBufferRegionCommand(reshade::api::command_list* cmd_list, const void* data, reshade::api::resource dest, uint64_t dest_offset, uint64_t size);

// Resource binding and memory operation power saving
bool OnBindResource(reshade::api::command_list* cmd_list, reshade::api::shader_stage stages, reshade::api::descriptor_table table, uint32_t binding, reshade::api::resource_view value);
bool OnMapResource(reshade::api::device* device, reshade::api::resource resource, uint32_t subresource, reshade::api::map_access access, reshade::api::subresource_data* data);
void OnUnmapResource(reshade::api::device* device, reshade::api::resource resource, uint32_t subresource);

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


// DXGI composition state utilities
DxgiBypassMode GetIndependentFlipState(reshade::api::swapchain* swapchain);
bool SetIndependentFlipState(reshade::api::swapchain* swapchain);
const char* DxgiBypassModeToString(DxgiBypassMode mode);

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

// Forward declarations for types used in this header
namespace dxgi::fps_limiter {
    class CustomFpsLimiterManager;
}


