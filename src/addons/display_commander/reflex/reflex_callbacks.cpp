#include "reflex_callbacks.hpp"
#include "reflex_management.hpp"
#include <atomic>
#include "../globals.hpp"

// Global Reflex manager instance (declared in reflex_management.cpp)
extern std::unique_ptr<ReflexManager> g_reflexManager;

// Swapchain event callbacks - standalone functions that can be called from the main addon
void OnReflexPresentUpdateBefore2(reshade::api::effect_runtime* runtime) {
    (void)runtime;

    if (!s_reflex_enabled.load()) {
        return;
    }

    // Use the last known swapchain (kept up-to-date in swapchain events)
    reshade::api::swapchain* swapchain = g_last_swapchain_ptr.load();
    if (swapchain == nullptr) {
        return;
    }

    // Keep NVAPI sleep mode refreshed
    if (g_reflex_settings_changed.load()) {
        SetReflexSleepMode(swapchain);
        g_reflex_settings_changed.store(false);
    } else {
        // Periodic refresh to help NVIDIA overlay keep state
        static int refresh_counter = 0;
        if ((++refresh_counter % 60) == 0) {
            SetReflexSleepMode(swapchain);
        }
    }

    // Generate full latency markers each frame (SIM/RENDERSUBMIT/INPUT via NVAPI + ETW)
    if (s_reflex_use_markers.load()) {
        SetReflexLatencyMarkers(swapchain);
        // PRESENT markers are sent via NVAPI only to bracket the present
        SetReflexPresentMarkers(swapchain);
    }
}

void OnReflexPresentUpdateAfter(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain) {
    (void)queue;
    (void)swapchain;
    // No-op: Present markers are emitted in the Before phase to properly bracket Present
}

void OnReflexBeginRenderPass(reshade::api::command_list* cmd_list, uint32_t count, const reshade::api::render_pass_render_target_desc* rts, const reshade::api::render_pass_depth_stencil_desc* ds) {
    (void)cmd_list; (void)count; (void)rts; (void)ds;
    // Intentionally left minimal. Games may issue multiple render passes per-frame;
    // marker generation is centralized in PresentUpdateBefore2 to avoid duplication.
}

void OnReflexEndRenderPass(reshade::api::command_list* cmd_list) {
    (void)cmd_list;
    // Intentionally left minimal (see comment in OnReflexBeginRenderPass).
}
