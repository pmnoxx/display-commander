#pragma once

#include <reshade.hpp>
#include <atomic>

// Power saving event handlers for additional GPU operations
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

bool OnCopyResource(reshade::api::command_list *cmd_list, reshade::api::resource source, reshade::api::resource dest);
bool OnUpdateBufferRegion(reshade::api::device *device, const void *data, reshade::api::resource resource,
                          uint64_t offset, uint64_t size);
bool OnUpdateBufferRegionCommand(reshade::api::command_list *cmd_list, const void *data, reshade::api::resource dest,
                                 uint64_t dest_offset, uint64_t size);

bool OnBindResource(reshade::api::command_list *cmd_list, reshade::api::shader_stage stages,
                    reshade::api::descriptor_table table, uint32_t binding, reshade::api::resource_view value);
// Note: OnBindPipeline is already implemented in swapchain_events.cpp

bool OnMapResource(reshade::api::device *device, reshade::api::resource resource, uint32_t subresource,
                   reshade::api::map_access access, reshade::api::subresource_data *data);
void OnUnmapResource(reshade::api::device *device, reshade::api::resource resource, uint32_t subresource);

// New power saving event handlers for frame-specific GPU operations
bool OnCopyBufferRegion(reshade::api::command_list *cmd_list, reshade::api::resource source, uint64_t source_offset,
                        reshade::api::resource dest, uint64_t dest_offset, uint64_t size);
bool OnCopyBufferToTexture(reshade::api::command_list *cmd_list, reshade::api::resource source, uint64_t source_offset,
                           uint32_t row_length, uint32_t slice_height, reshade::api::resource dest,
                           uint32_t dest_subresource, const reshade::api::subresource_box *dest_box);
bool OnCopyTextureToBuffer(reshade::api::command_list *cmd_list, reshade::api::resource source,
                           uint32_t source_subresource, const reshade::api::subresource_box *source_box,
                           reshade::api::resource dest, uint64_t dest_offset, uint32_t row_length,
                           uint32_t slice_height);
bool OnCopyTextureRegion(reshade::api::command_list *cmd_list, reshade::api::resource source,
                         uint32_t source_subresource, const reshade::api::subresource_box *source_box,
                         reshade::api::resource dest, uint32_t dest_subresource,
                         const reshade::api::subresource_box *dest_box, reshade::api::filter_mode filter);
bool OnResolveTextureRegion(reshade::api::command_list *cmd_list, reshade::api::resource source,
                            uint32_t source_subresource, const reshade::api::subresource_box *source_box,
                            reshade::api::resource dest, uint32_t dest_subresource, uint32_t dest_x, uint32_t dest_y,
                            uint32_t dest_z, reshade::api::format format);


// Helper function to determine if an operation should be suppressed for power saving
bool ShouldSuppressOperation();

// Power saving for draw calls
bool OnDraw(reshade::api::command_list *cmd_list, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex,
            uint32_t first_instance);
bool OnDrawIndexed(reshade::api::command_list *cmd_list, uint32_t index_count, uint32_t instance_count,
                   uint32_t first_index, int32_t vertex_offset, uint32_t first_instance);
bool OnDrawOrDispatchIndirect(reshade::api::command_list *cmd_list, reshade::api::indirect_command type,
                              reshade::api::resource buffer, uint64_t offset, uint32_t draw_count, uint32_t stride);

// Power saving settings
extern std::atomic<bool> s_suppress_compute_in_background;
extern std::atomic<bool> s_suppress_copy_in_background;
extern std::atomic<bool> s_suppress_binding_in_background;
extern std::atomic<bool> s_suppress_memory_ops_in_background;

// New power saving settings for frame-specific operations
extern std::atomic<bool> s_suppress_clear_ops_in_background;
extern std::atomic<bool> s_suppress_mipmap_gen_in_background;
extern std::atomic<bool> s_suppress_blit_ops_in_background;
extern std::atomic<bool> s_suppress_query_ops_in_background;
