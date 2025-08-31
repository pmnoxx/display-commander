#include "swapchain_events_power_saving.hpp"
#include "globals.hpp"
#include "utils/timing.hpp"
#include <sstream>

// Forward declarations for functions used in the ondraw methods
void HandleRenderStartAndEndTimes();

// Power saving settings - these can be controlled via UI
std::atomic<bool> s_suppress_compute_in_background{true};
std::atomic<bool> s_suppress_copy_in_background{true};
std::atomic<bool> s_suppress_binding_in_background{false}; // Keep binding by default to avoid state issues
std::atomic<bool> s_suppress_memory_ops_in_background{true};

// New power saving settings for frame-specific operations
std::atomic<bool> s_suppress_clear_ops_in_background{true};
std::atomic<bool> s_suppress_mipmap_gen_in_background{true};
std::atomic<bool> s_suppress_blit_ops_in_background{true};
std::atomic<bool> s_suppress_query_ops_in_background{true};

// Helper function to determine if an operation should be suppressed for power saving
bool ShouldBackgroundSuppressOperation() {
    // Check if power saving is enabled and app is in background
    return s_no_render_in_background.load() && g_app_in_background.load(std::memory_order_acquire);
}

// Power saving for compute shader dispatches
bool OnDispatch(reshade::api::command_list* cmd_list, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DISPATCH].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip compute shader dispatches in background
    if (s_suppress_compute_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the dispatch call
    }
    
    return false; // Don't skip the dispatch call
}

// Power saving for mesh shader dispatches
bool OnDispatchMesh(reshade::api::command_list* cmd_list, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DISPATCH_MESH].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip mesh shader dispatches in background
    if (s_suppress_compute_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the dispatch call
    }
    
    return false; // Don't skip the dispatch call
}

// Power saving for ray tracing dispatches
bool OnDispatchRays(reshade::api::command_list* cmd_list, reshade::api::resource raygen, uint64_t raygen_offset, uint64_t raygen_size, reshade::api::resource miss, uint64_t miss_offset, uint64_t miss_size, uint64_t miss_stride, reshade::api::resource hit_group, uint64_t hit_group_offset, uint64_t hit_group_size, uint64_t hit_group_stride, reshade::api::resource callable, uint64_t callable_offset, uint64_t callable_size, uint64_t callable_stride, uint32_t width, uint32_t height, uint32_t depth) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DISPATCH_RAYS].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip ray tracing dispatches in background
    if (s_suppress_compute_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the dispatch call
    }
    
    return false; // Don't skip the dispatch call
}

// Power saving for resource copying
bool OnCopyResource(reshade::api::command_list* cmd_list, reshade::api::resource source, reshade::api::resource dest) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_COPY_RESOURCE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip resource copying in background
    if (s_suppress_copy_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the copy operation
    }
    
    return false; // Don't skip the copy operation
}

// Power saving for buffer updates via device
bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_UPDATE_BUFFER_REGION].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip buffer updates in background
    if (s_suppress_copy_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the buffer update
    }
    
    return false; // Don't skip the buffer update
}

// Power saving for command-based buffer updates
bool OnUpdateBufferRegionCommand(reshade::api::command_list* cmd_list, const void* data, reshade::api::resource dest, uint64_t dest_offset, uint64_t size) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_UPDATE_BUFFER_REGION_COMMAND].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip command-based buffer updates in background
    if (s_suppress_copy_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the buffer update
    }
    
    return false; // Don't skip the buffer update
}

// Power saving for resource binding
bool OnBindResource(reshade::api::command_list* cmd_list, reshade::api::shader_stage stages, reshade::api::descriptor_table table, uint32_t binding, reshade::api::resource_view value) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_BIND_RESOURCE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip resource binding in background
    if (s_suppress_binding_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the resource binding
    }
    
    return false; // Don't skip the resource binding
}

// Power saving for resource mapping
bool OnMapResource(reshade::api::device* device, reshade::api::resource resource, uint32_t subresource, reshade::api::map_access access, reshade::api::subresource_data* data) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_MAP_RESOURCE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip resource mapping in background
    if (s_suppress_memory_ops_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the resource mapping
    }
    
    return false; // Don't skip the resource mapping
}

// Power saving for resource unmapping
void OnUnmapResource(reshade::api::device* device, reshade::api::resource resource, uint32_t subresource) {
    // Note: Unmap operations are typically required for cleanup, so we don't suppress them
    // This function is provided for consistency but always allows the operation
    // You could add logic here if needed for specific cases
}

// Power saving for buffer region copying (frame-specific)
bool OnCopyBufferRegion(reshade::api::command_list* cmd_list, reshade::api::resource source, uint64_t source_offset, reshade::api::resource dest, uint64_t dest_offset, uint64_t size) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_COPY_BUFFER_REGION].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip buffer region copying in background
    if (s_suppress_copy_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the copy operation
    }
    
    return false; // Don't skip the copy operation
}

// Power saving for buffer to texture copying (very GPU intensive, frame-specific)
bool OnCopyBufferToTexture(reshade::api::command_list* cmd_list, reshade::api::resource source, uint64_t source_offset, uint32_t row_length, uint32_t slice_height, reshade::api::resource dest, uint32_t dest_subresource, const reshade::api::subresource_box* dest_box) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_COPY_BUFFER_TO_TEXTURE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip buffer to texture copying in background
    if (s_suppress_copy_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the copy operation
    }
    
    return false; // Don't skip the copy operation
}

// Power saving for texture to buffer copying (frame-specific)
bool OnCopyTextureToBuffer(reshade::api::command_list* cmd_list, reshade::api::resource source, uint32_t source_subresource, const reshade::api::subresource_box* source_box, reshade::api::resource dest, uint64_t dest_offset, uint32_t row_length, uint32_t slice_height) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_COPY_TEXTURE_TO_BUFFER].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip texture to buffer copying in background
    if (s_suppress_copy_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the copy operation
    }
    
    return false; // Don't skip the copy operation
}

// Power saving for texture region copying (frame-specific)
bool OnCopyTextureRegion(reshade::api::command_list* cmd_list, reshade::api::resource source, uint32_t source_subresource, const reshade::api::subresource_box* source_box, reshade::api::resource dest, uint32_t dest_subresource, const reshade::api::subresource_box* dest_box, reshade::api::filter_mode filter) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_COPY_TEXTURE_REGION].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip texture region copying in background
    if (s_suppress_copy_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the copy operation
    }
    
    return false; // Don't skip the copy operation
}

// Power saving for texture region resolving (MSAA resolve, very GPU intensive, frame-specific)
bool OnResolveTextureRegion(reshade::api::command_list* cmd_list, reshade::api::resource source, uint32_t source_subresource, const reshade::api::subresource_box* source_box, reshade::api::resource dest, uint32_t dest_subresource, uint32_t dest_x, uint32_t dest_y, uint32_t dest_z, reshade::api::format format) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_RESOLVE_TEXTURE_REGION].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip texture region resolving in background
    if (s_suppress_copy_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the resolve operation
    }
    
    return false; // Don't skip the resolve operation
}

// Power saving for render target clearing (frame-specific, safe to suspend)
bool OnClearRenderTargetView(reshade::api::command_list* cmd_list, reshade::api::resource_view rtv, const float color[4], uint32_t rect_count, const reshade::api::rect* rects) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_CLEAR_RENDER_TARGET_VIEW].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip render target clearing in background
    if (s_suppress_clear_ops_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the clear operation
    }
    
    return false; // Don't skip the clear operation
}

// Power saving for depth stencil clearing (frame-specific, safe to suspend)
bool OnClearDepthStencilView(reshade::api::command_list* cmd_list, reshade::api::resource_view dsv, const float* depth, const uint8_t* stencil, uint32_t rect_count, const reshade::api::rect* rects) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_CLEAR_DEPTH_STENCIL_VIEW].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip depth stencil clearing in background
    if (s_suppress_clear_ops_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the clear operation
    }
    
    return false; // Don't skip the clear operation
}

// Power saving for unordered access view clearing with uint values (frame-specific)
bool OnClearUnorderedAccessViewUint(reshade::api::command_list* cmd_list, reshade::api::resource_view uav, const uint32_t values[4], uint32_t rect_count, const reshade::api::rect* rects) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_CLEAR_UNORDERED_ACCESS_VIEW_UINT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip UAV clearing in background
    if (s_suppress_clear_ops_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the clear operation
    }
    
    return false; // Don't skip the clear operation
}

// Power saving for unordered access view clearing with float values (frame-specific)
bool OnClearUnorderedAccessViewFloat(reshade::api::command_list* cmd_list, reshade::api::resource_view uav, const float values[4], uint32_t rect_count, const reshade::api::rect* rects) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_CLEAR_UNORDERED_ACCESS_VIEW_FLOAT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip UAV clearing in background
    if (s_suppress_clear_ops_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the clear operation
    }
    
    return false; // Don't skip the clear operation
}

// Power saving for mipmap generation (very GPU intensive, frame-specific)
bool OnGenerateMipmaps(reshade::api::command_list* cmd_list, reshade::api::resource_view srv) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_GENERATE_MIPMAPS].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip mipmap generation in background
    if (s_suppress_mipmap_gen_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the mipmap generation
    }
    
    return false; // Don't skip the mipmap generation
}

// Power saving for blit operations (frame-specific image processing)
bool OnBlit(reshade::api::command_list* cmd_list, reshade::api::resource source, uint32_t source_subresource, const reshade::api::subresource_box* source_box, reshade::api::resource dest, uint32_t dest_subresource, const reshade::api::subresource_box* dest_box, reshade::api::filter_mode filter) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_BLIT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip blit operations in background
    if (s_suppress_blit_ops_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the blit operation
    }
    
    return false; // Don't skip the blit operation
}

// Power saving for query begin operations (frame-specific statistics)
bool OnBeginQuery(reshade::api::command_list* cmd_list, reshade::api::query_heap heap, reshade::api::query_type type, uint32_t index) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_BEGIN_QUERY].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip query operations in background
    if (s_suppress_query_ops_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the query operation
    }
    
    return false; // Don't skip the query operation
}

// Power saving for query end operations (frame-specific statistics)
bool OnEndQuery(reshade::api::command_list* cmd_list, reshade::api::query_heap heap, reshade::api::query_type type, uint32_t index) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_END_QUERY].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip query operations in background
    if (s_suppress_query_ops_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the query operation
    }
    
    return false; // Don't skip the query operation
}

// Power saving for query data resolution (frame-specific statistics)
bool OnResolveQueryData(reshade::api::command_list* cmd_list, reshade::api::query_heap heap, reshade::api::query_type type, uint32_t first, uint32_t count, reshade::api::resource dest, uint64_t dest_offset) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_RESOLVE_QUERY_DATA].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Power saving: skip query data resolution in background
    if (s_suppress_query_ops_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the query data resolution
    }
    
    return false; // Don't skip the query data resolution
}

// Power saving for draw calls
bool OnDraw(reshade::api::command_list* cmd_list, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DRAW].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Set render start time if it's 0 (first draw call of the frame)
    HandleRenderStartAndEndTimes();

    if (ShouldBackgroundSuppressOperation()) {
      return true; // Skip the draw call
    }
    
    return false; // Don't skip the draw call
}

// Power saving for indexed draw calls
bool OnDrawIndexed(reshade::api::command_list* cmd_list, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DRAW_INDEXED].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Set render start time if it's 0 (first draw call of the frame)
    HandleRenderStartAndEndTimes();

    if (ShouldBackgroundSuppressOperation()) {
      return true; // Skip the draw call
    }
    
    return false; // Don't skip the draw call
}

// Power saving for indirect draw calls
bool OnDrawOrDispatchIndirect(reshade::api::command_list* cmd_list, reshade::api::indirect_command type, reshade::api::resource buffer, uint64_t offset, uint32_t draw_count, uint32_t stride) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DRAW_OR_DISPATCH_INDIRECT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Set render start time if it's 0 (first draw call of the frame)
    HandleRenderStartAndEndTimes();

    if (ShouldBackgroundSuppressOperation()) {
      return true; // Skip the draw call
    }

    return false; // Don't skip the draw call
}
