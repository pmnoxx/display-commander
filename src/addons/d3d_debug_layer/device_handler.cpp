#include "debug_layer.hpp"
#include "event_handlers.hpp"
#include <reshade.hpp>

namespace d3d_debug_layer {

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

} // namespace d3d_debug_layer
