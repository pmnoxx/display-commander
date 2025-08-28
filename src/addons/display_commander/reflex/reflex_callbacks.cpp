#include "reflex_callbacks.hpp"
#include "reflex_management.hpp"
#include "../utils.hpp"

// Global Reflex manager instance (declared in reflex_management.cpp)
extern std::unique_ptr<ReflexManager> g_reflexManager;

// Swapchain event callbacks - standalone functions that can be called from the main addon
void OnReflexPresentUpdateBefore2(reshade::api::effect_runtime* runtime) {
    // Empty callback for swapchain event
}

void OnReflexPresentUpdateAfter(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain) {
    // Empty callback for swapchain event
}

void OnReflexBeginRenderPass(reshade::api::command_list* cmd_list, uint32_t count, const reshade::api::render_pass_render_target_desc* rts, const reshade::api::render_pass_depth_stencil_desc* ds) {
    // Empty callback for swapchain event
}

void OnReflexEndRenderPass(reshade::api::command_list* cmd_list) {
    // Empty callback for swapchain event
}
