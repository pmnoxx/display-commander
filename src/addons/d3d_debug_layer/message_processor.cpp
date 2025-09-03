#include "debug_layer.hpp"
#include "event_handlers.hpp"
#include <include/reshade.hpp>
#include <thread>
#include <chrono>

namespace d3d_debug_layer {

// Message processing thread function
void MessageProcessorThread() {
    reshade::log::message(reshade::log::level::info, "[D3D Debug] Message processor thread started");
    
    while (!g_shutdown.load()) {
        if (g_initialized.load()) {
            try {
                DebugLayerManager::GetInstance().ProcessDebugMessages();
            } catch (const std::exception& e) {
                reshade::log::message(reshade::log::level::error, 
                    ("[D3D Debug] Exception in message processor: " + std::string(e.what())).c_str());
            } catch (...) {
                reshade::log::message(reshade::log::level::error, 
                    "[D3D Debug] Unknown exception in message processor");
            }
        }
        
        // Process messages every 16ms (roughly 60 FPS)
        // This provides a good balance between responsiveness and performance
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    
    reshade::log::message(reshade::log::level::info, "[D3D Debug] Message processor thread stopped");
}

} // namespace d3d_debug_layer
