// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "reshade_event_registration.hpp"

#include "reshade_addon_handlers.hpp"
#include "swapchain_events.hpp"
#include "ui/new_ui/controls/performance_overlay/reshade_overlay_event.hpp"
#include "utils/detour_call_tracker.hpp"

// Libraries <ReShade> / <imgui>
#include <reshade.hpp>

void RegisterReShadeEvents(HMODULE h_module) {
    (void)h_module;
    CALL_GUARD_NO_TS();
    // Register reshade_overlay event for test code
    reshade::register_event<reshade::addon_event::reshade_overlay>(OnPerformanceOverlay);

    // Register device creation event for D3D9 to D3D9Ex upgrade
    reshade::register_event<reshade::addon_event::create_device>(OnCreateDevice);

    // Capture sync interval on swapchain creation for UI
    reshade::register_event<reshade::addon_event::create_swapchain>(OnCreateSwapchainCapture);

    reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);

    // Register ReShade effect runtime events for input blocking
    reshade::register_event<reshade::addon_event::init_effect_runtime>(OnInitEffectRuntime);
    reshade::register_event<reshade::addon_event::destroy_effect_runtime>(OnDestroyEffectRuntime);
    reshade::register_event<reshade::addon_event::reshade_open_overlay>(OnReShadeOverlayOpen);

    // Defer NVAPI init until after settings are loaded below

    // Register our fullscreen prevention event handler
    // NOTE: Fullscreen prevention is now handled directly in IDXGISwapChain_SetFullscreenState_Detour
    // reshade::register_event<reshade::addon_event::set_fullscreen_state>(OnSetFullscreenState);

    // NVAPI HDR monitor will be started after settings load below if enabled
    // Seed default fps limit snapshot
    // GetFpsLimit removed from proxy, use s_fps_limit directly
    reshade::register_event<reshade::addon_event::present>(OnPresentUpdateBefore);
    reshade::register_event<reshade::addon_event::finish_present>(OnPresentUpdateAfter);

    reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
    reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);

    // Register command list/queue lifecycle events
    reshade::register_event<reshade::addon_event::init_command_list>(OnInitCommandList);
    reshade::register_event<reshade::addon_event::destroy_command_list>(OnDestroyCommandList);
    reshade::register_event<reshade::addon_event::init_command_queue>(OnInitCommandQueue);
    reshade::register_event<reshade::addon_event::destroy_command_queue>(OnDestroyCommandQueue);
    reshade::register_event<reshade::addon_event::execute_command_list>(OnExecuteCommandList);

    // Register swapchain/resource lifecycle events
    reshade::register_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);

    // Register present completion event
    reshade::register_event<reshade::addon_event::finish_present>(OnFinishPresent);

    // Register ReShade effect rendering events
    reshade::register_event<reshade::addon_event::reshade_begin_effects>(OnReShadeBeginEffects);
    reshade::register_event<reshade::addon_event::reshade_finish_effects>(OnReShadeFinishEffects);
    reshade::register_event<reshade::addon_event::reshade_present>(OnReShadePresent);
}
