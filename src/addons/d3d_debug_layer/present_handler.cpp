#include "event_handlers.hpp"
#include <include/reshade.hpp>
#include <atomic>

namespace d3d_debug_layer {

// ReShade event handler: Present (used for periodic message processing)
void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain, 
               const reshade::api::rect* source_rect, const reshade::api::rect* dest_rect, 
               uint32_t dirty_rect_count, const reshade::api::rect* dirty_rects) {
    // This event handler is lightweight and just ensures we're processing messages
    // The actual processing happens in the dedicated thread
    static std::atomic<uint64_t> frame_count{0};
    frame_count++;
    
    // Log a status message every 1000 frames when debug layer is active
    if (g_initialized.load() && (frame_count % 1000 == 0)) {
        reshade::log::message(reshade::log::level::info, 
            ("[D3D Debug] Debug layer active - frame " + std::to_string(frame_count.load())).c_str());
    }
}

} // namespace d3d_debug_layer
