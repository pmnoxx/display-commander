#include "addon.hpp"
#include "swapchain_events_power_saving.hpp"

// Include timing utilities for QPC initialization
#include "../../utils/timing.hpp"

// Include the UI initialization
#include "ui/ui_main.hpp"
#include "ui/new_ui/new_ui_main.hpp"
#include "ui/new_ui/experimental_tab.hpp"
#include "background_tasks/background_task_coordinator.hpp"
#include "reshade_events/fullscreen_prevention.hpp"

// Include input blocking system
#include "input_blocking/input_blocking.hpp"

#include "dxgi/custom_fps_limiter_manager.hpp"
#include "latent_sync/latent_sync_manager.hpp"
#include "dxgi/dxgi_device_info.hpp"
#include "latency/latency_manager.hpp"
#include "nvapi/nvapi_fullscreen_prevention.hpp"
#include "nvapi/nvapi_hdr_monitor.hpp"
#include "nvapi/dlssfg_version_detector.hpp"
#include "nvapi/dlss_preset_detector.hpp"
// Restore display settings on exit if enabled
#include "display_restore.hpp"
#include "process_exit_hooks.hpp"

// Forward declarations for ReShade event handlers
void OnInitEffectRuntime(reshade::api::effect_runtime* runtime);
bool OnReShadeOverlayOpen(reshade::api::effect_runtime* runtime, bool open, reshade::api::input_source source);
namespace {
// Destroy device handler to restore display if needed
void OnDestroyDevice(reshade::api::device* /*device*/) {
  LogInfo("ReShade device destroyed - Attempting to restore display settings");
    display_restore::RestoreAllIfEnabled();
}
} // namespace


// ReShade effect runtime event handler for input blocking
void OnInitEffectRuntime(reshade::api::effect_runtime* runtime) {
    if (runtime != nullptr) {
        g_reshade_runtime.store(runtime);
        LogInfo("ReShade effect runtime initialized - Input blocking now avai`le");

        if (s_fix_hdr10_colorspace.load()) {
          runtime->set_color_space(reshade::api::color_space::hdr10_st2084);
        }
    }
}

// ReShade overlay event handler for input blocking
bool OnReShadeOverlayOpen(reshade::api::effect_runtime* runtime, bool open, reshade::api::input_source source) {
    if (open) {
        LogInfo("ReShade overlay opened - Input blocking active");
        // When ReShade overlay opens, we can also use its input blocking
        if (runtime != nullptr) {
            g_reshade_runtime.store(runtime);
        }
    } else {
        LogInfo("ReShade overlay closed - Input blocking inactive");
    }
    return false; // Don't prevent ReShade from opening/closing the overlay
}


// Direct overlay draw callback (no settings2 indirection)
namespace {
void OnRegisterOverlayDisplayCommander(reshade::api::effect_runtime* runtime) {
    // Draw the new UI
    ui::new_ui::DrawNewUISystem();
}
}  // namespace

bool initialized = false;

BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {


  switch (fdw_reason) {
    case DLL_PROCESS_ATTACH:
    try {
      g_shutdown.store(false);

      if (!reshade::register_addon(h_module)) return FALSE;
      {
        LONGLONG now_ns = utils::get_now_ns();
        std::ostringstream oss;
        oss << "DLLMain(DisplayCommander) " << now_ns << " " << fdw_reason;
        LogInfo(oss.str().c_str());
      }

      if (g_dll_initialization_complete.load()) {
        LogError("DLLMain(DisplayCommander) already initialized");
        return FALSE;
      }
      g_shutdown.store(false);

      // Debug: Log the module handle value
      {
        std::ostringstream oss;
        oss << "DLLMain h_module: 0x" << std::hex << reinterpret_cast<uintptr_t>(h_module);
        LogInfo(oss.str().c_str());
      }



      // Initialize QPC timing constants based on actual frequency
      utils::initialize_qpc_timing_constants();

      reshade::register_overlay("Display Commander", OnRegisterOverlayDisplayCommander);

      // Capture sync interval on swapchain creation for UI
      reshade::register_event<reshade::addon_event::create_swapchain>(OnCreateSwapchainCapture);

      reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);

      // Register ReShade effect runtime events for input blocking
      reshade::register_event<reshade::addon_event::init_effect_runtime>(OnInitEffectRuntime);
      reshade::register_event<reshade::addon_event::reshade_open_overlay>(OnReShadeOverlayOpen);

      // Defer NVAPI init until after settings are loaded below

      // Register our fullscreen prevention event handler
      reshade::register_event<reshade::addon_event::set_fullscreen_state>(
          display_commander::events::OnSetFullscreenState);

      // NVAPI HDR monitor will be started after settings load below if enabled
      // Seed default fps limit snapshot
      // GetFpsLimit removed from proxy, use s_fps_limit directly
      reshade::register_event<reshade::addon_event::present>(OnPresentUpdateBefore);
      reshade::register_event<reshade::addon_event::reshade_present>(OnPresentUpdateBefore2);
      reshade::register_event<reshade::addon_event::finish_present>(OnPresentUpdateAfter);
      reshade::register_event<reshade::addon_event::present_flags>(OnPresentFlags);

      // Register draw event handlers for render timing
      reshade::register_event<reshade::addon_event::draw>(OnDraw);
      reshade::register_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
      reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(OnDrawOrDispatchIndirect);

      // Register power saving event handlers for additional GPU operations
      reshade::register_event<reshade::addon_event::dispatch>(OnDispatch);
      reshade::register_event<reshade::addon_event::dispatch_mesh>(OnDispatchMesh);
      reshade::register_event<reshade::addon_event::dispatch_rays>(OnDispatchRays);
      reshade::register_event<reshade::addon_event::copy_resource>(OnCopyResource);
      reshade::register_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion);
      reshade::register_event<reshade::addon_event::update_buffer_region_command>(OnUpdateBufferRegionCommand);

      // Register buffer resolution upgrade event handlers
      reshade::register_event<reshade::addon_event::create_resource>(OnCreateResource);
      reshade::register_event<reshade::addon_event::create_resource_view>(OnCreateResourceView);
      reshade::register_event<reshade::addon_event::bind_viewports>(OnSetViewport);
      reshade::register_event<reshade::addon_event::bind_scissor_rects>(OnSetScissorRects);
      // Note: bind_resource, map_resource, unmap_resource events don't exist in ReShade API
      // These operations are handled differently in ReShade
      // Register device destroy event for restore-on-exit
      reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);

      // Install process-exit safety hooks to restore display on abnormal exits
      process_exit_hooks::Initialize();

      // Mark DLL initialization as complete - now DXGI calls are safe
      g_dll_initialization_complete.store(true);
      LogInfo("DLL initialization complete - DXGI calls now enabled");

      // Check unsafe calls counter and log if any unsafe calls occurred
      uint32_t unsafe_calls = g_unsafe_calls_cnt.load();
      if (unsafe_calls > 0) {
        LogError("ERROR: %u unsafe Win32 API calls occurred during DLL initialization", unsafe_calls);
      } else {
        LogInfo("No unsafe Win32 API calls occurred during DLL initialization");
      }
    } catch (const std::exception& e) {
      LogError("Error initializing DLL: %s", e.what());
    } catch (...) {
      LogError("Unknown error initializing DLL");
    }
    break;
    case DLL_THREAD_ATTACH: {
      if (!initialized) {
        initialized = true;

        display_cache::g_displayCache.Initialize();

        // Initialize input blocking system
        display_commander::input_blocking::initialize_input_blocking();

        // Register overlay directly
        // Ensure UI system is initialized
        ui::new_ui::InitializeNewUISystem();
        StartContinuousMonitoring();

        // Initialize experimental tab
        std::thread(RunBackgroundAudioMonitor).detach();
        background::StartBackgroundTasks();

        InitializeUISettings();

        // Start NVAPI HDR monitor if enabled
        if (s_nvapi_hdr_logging.load()) {
          std::thread(RunBackgroundNvapiHdrMonitor).detach();
        }

        ui::new_ui::InitExperimentalTab();
      }
      break;
    }
    case DLL_THREAD_DETACH: {
      break;
    }


    case DLL_PROCESS_DETACH:
      g_shutdown.store(true);

      // Clean up input blocking system
      display_commander::input_blocking::cleanup_input_blocking();

      // Clean up continuous monitoring if it's running
      StopContinuousMonitoring();

      // Clean up experimental tab threads
      ui::new_ui::CleanupExperimentalTab();

      // Clean up DXGI Device Info Manager
      g_dxgiDeviceInfoManager.reset();
      // Safety: attempt restore on detach as well (idempotent)
      //display_restore::RestoreAllIfEnabled();
      // Uninstall process-exit hooks
      //process_exit_hooks::Shutdown();


      // Clean up NVAPI instances before shutdown
      if (g_latencyManager) {
        g_latencyManager->Shutdown();
      }

      // Clean up NVAPI fullscreen prevention
      extern NVAPIFullscreenPrevention g_nvapiFullscreenPrevention;
      g_nvapiFullscreenPrevention.Cleanup();

      // Note: reshade::unregister_addon() will automatically unregister all events and overlays
      // registered by this add-on, so manual unregistration is not needed and can cause issues
      //display_restore::RestoreAllIfEnabled(); // restore display settings on exit

      reshade::unregister_addon(h_module);
      break;
  }

  return TRUE;
}

// CONTINUOUS RENDERING FUNCTIONS REMOVED - Focus spoofing is now handled by Win32 hooks

// CONTINUOUS RENDERING THREAD REMOVED - Focus spoofing is now handled by Win32 hooks
// This provides a much cleaner and more effective solution


