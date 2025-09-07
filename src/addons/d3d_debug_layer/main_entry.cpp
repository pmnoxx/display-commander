#include "event_handlers.hpp"
#include "debug_layer.hpp"
#include <include/reshade.hpp>
#include <thread>
#include <atomic>

namespace d3d_debug_layer {

// Global state for the debug layer addon
std::atomic<bool> g_initialized{false};
std::atomic<bool> g_shutdown{false};
std::thread g_message_processor_thread;

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
