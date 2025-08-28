#pragma once

#include <include/reshade.hpp>

// Forward declaration
class ReflexManager;

// Swapchain event callbacks - these are standalone functions that can be called from the main addon
void OnReflexPresentUpdateBefore2(reshade::api::effect_runtime* runtime);
void OnReflexPresentUpdateAfter(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain);
void OnReflexBeginRenderPass(reshade::api::command_list* cmd_list, uint32_t count, const reshade::api::render_pass_render_target_desc* rts, const reshade::api::render_pass_depth_stencil_desc* ds);
void OnReflexEndRenderPass(reshade::api::command_list* cmd_list);
