#include "debug_layer.hpp"
#include <include/reshade.hpp>
#include <thread>
#include <chrono>
#include <atomic>

namespace d3d_debug_layer {

// Global state for the debug layer addon
static std::atomic<bool> g_initialized{false};
static std::atomic<bool> g_shutdown{false};
static std::thread g_message_processor_thread;

// Forward declarations for ReShade event handlers
void OnInitDevice(reshade::api::device* device);
void OnDestroyDevice(reshade::api::device* device);
void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain, const reshade::api::rect* source_rect, const reshade::api::rect* dest_rect, uint32_t dirty_rect_count, const reshade::api::rect* dirty_rects);

// Helper function to determine if device is D3D12
bool IsD3D12Device(reshade::api::device* device) {
    if (!device) return false;
    
    // Check the device API type
    switch (device->get_api()) {
        case reshade::api::device_api::d3d12:
            return true;
        case reshade::api::device_api::d3d11:
            return false;
        default:
            // For other APIs (OpenGL, Vulkan), we don't support debug layer
            return false;
    }
}

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

// ReShade event handler: Device initialization
void OnInitDevice(reshade::api::device* device) {
    if (!device) {
        reshade::log::message(reshade::log::level::warning, "[D3D Debug] OnInitDevice called with null device");
        return;
    }
    
    reshade::log::message(reshade::log::level::info, "[D3D Debug] Device initialization detected");
    
    // Get the native device handle
    uint64_t native_handle = device->get_native();
    if (!native_handle) {
        reshade::log::message(reshade::log::level::warning, "[D3D Debug] Failed to get native device handle");
        return;
    }
    void* native_device = reinterpret_cast<void*>(static_cast<uintptr_t>(native_handle));
    
    // Determine if this is D3D11 or D3D12
    bool is_d3d12 = IsD3D12Device(device);
    
    reshade::log::message(reshade::log::level::info, 
        ("[D3D Debug] Detected " + std::string(is_d3d12 ? "D3D12" : "D3D11") + " device").c_str());
    
    // Initialize debug layer for this device
    if (DebugLayerManager::GetInstance().InitializeForDevice(native_device, is_d3d12)) {
        g_initialized.store(true);
        reshade::log::message(reshade::log::level::info, "[D3D Debug] Debug layer initialized successfully");
        
        // Start message processor thread if not already running
        if (!g_message_processor_thread.joinable()) {
            g_message_processor_thread = std::thread(MessageProcessorThread);
        }
    } else {
        reshade::log::message(reshade::log::level::warning, "[D3D Debug] Failed to initialize debug layer");
    }
}

// ReShade event handler: Device destruction
void OnDestroyDevice(reshade::api::device* device) {
    if (!device) {
        return;
    }
    
    reshade::log::message(reshade::log::level::info, "[D3D Debug] Device destruction detected");
    
    uint64_t native_handle = device->get_native();
    if (native_handle) {
        void* native_device = reinterpret_cast<void*>(static_cast<uintptr_t>(native_handle));
        DebugLayerManager::GetInstance().CleanupForDevice(native_device);
        g_initialized.store(false);
        reshade::log::message(reshade::log::level::info, "[D3D Debug] Debug layer cleaned up");
    }
}

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

// DLL entry point
BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {
    switch (fdw_reason) {
        case DLL_PROCESS_ATTACH:
            if (!reshade::register_addon(h_module)) {
                return FALSE;
            }
            
            reshade::log::message(reshade::log::level::info, "[D3D Debug] Addon registered successfully");
            
            // Register event handlers
            reshade::register_event<reshade::addon_event::init_device>(d3d_debug_layer::OnInitDevice);
            reshade::register_event<reshade::addon_event::destroy_device>(d3d_debug_layer::OnDestroyDevice);
            reshade::register_event<reshade::addon_event::present>(d3d_debug_layer::OnPresent);
            
            reshade::log::message(reshade::log::level::info, "[D3D Debug] Event handlers registered");
            
            d3d_debug_layer::g_shutdown.store(false);
            break;
            
        case DLL_PROCESS_DETACH:
            reshade::log::message(reshade::log::level::info, "[D3D Debug] Addon shutting down");
            
            // Signal shutdown to message processor thread
            d3d_debug_layer::g_shutdown.store(true);
            
            // Wait for message processor thread to finish
            if (d3d_debug_layer::g_message_processor_thread.joinable()) {
                d3d_debug_layer::g_message_processor_thread.join();
            }
            
            // Unregister event handlers
            reshade::unregister_event<reshade::addon_event::init_device>(d3d_debug_layer::OnInitDevice);
            reshade::unregister_event<reshade::addon_event::destroy_device>(d3d_debug_layer::OnDestroyDevice);
            reshade::unregister_event<reshade::addon_event::present>(d3d_debug_layer::OnPresent);
            
            // Cleanup debug layer
            d3d_debug_layer::DebugLayerManager::GetInstance().CleanupForDevice(nullptr);
            d3d_debug_layer::g_initialized.store(false);
            
            reshade::unregister_addon(h_module);
            reshade::log::message(reshade::log::level::info, "[D3D Debug] Addon unregistered");
            break;
    }
    
    return TRUE;
}
