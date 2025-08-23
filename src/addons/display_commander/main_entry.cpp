#include "addon.hpp"

// Include the UI initialization
#include "ui/ui_main.hpp"
#include "ui/new_ui/new_ui_main.hpp"
#include "background_tasks/background_task_coordinator.hpp"
#include "reshade_events/fullscreen_prevention.hpp"

#include "dxgi/custom_fps_limiter_manager.hpp"
#include "dxgi/dxgi_device_info.hpp"
#include "nvapi/nvapi_fullscreen_prevention.hpp"
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
    renodx::display_restore::RestoreAllIfEnabled();
}
} // namespace

// Forward declarations for frame lifecycle hooks
void OnBeginRenderPass(reshade::api::command_list* cmd_list);

// ReShade effect runtime event handler for input blocking
void OnInitEffectRuntime(reshade::api::effect_runtime* runtime) {
    if (runtime != nullptr) {
        g_reshade_runtime.store(runtime);
        LogInfo("ReShade effect runtime initialized - Input blocking now available");
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
    renodx::ui::new_ui::DrawNewUISystem();
}
}  // namespace

BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {
  switch (fdw_reason) {
    case DLL_PROCESS_ATTACH:
      if (!reshade::register_addon(h_module)) return FALSE;
      // Ensure UI system is initialized
      renodx::ui::new_ui::InitializeNewUISystem();
      // Install process-exit safety hooks to restore display on abnormal exits
      renodx::process_exit_hooks::Initialize();
      
      // Capture sync interval on swapchain creation for UI
#if RESHADE_API_VERSION >= 17
      reshade::register_event<reshade::addon_event::create_swapchain>(OnCreateSwapchainCapture);
#else
      reshade::register_event<reshade::addon_event::create_swapchain>(OnCreateSwapchainCapture);
#endif

      reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
      
      // Register ReShade effect runtime events for input blocking
      reshade::register_event<reshade::addon_event::init_effect_runtime>(OnInitEffectRuntime);
      reshade::register_event<reshade::addon_event::reshade_open_overlay>(OnReShadeOverlayOpen);
      
      // Defer NVAPI / Reflex init until after settings are loaded below
      
      // Register our fullscreen prevention event handler
      reshade::register_event<reshade::addon_event::set_fullscreen_state>(
          renodx::display_commander::events::OnSetFullscreenState);

      g_attach_time = std::chrono::steady_clock::now();
      g_shutdown.store(false);
      std::thread(RunBackgroundAudioMonitor).detach();
      renodx::background::StartBackgroundTasks();
      
      // NVAPI HDR monitor will be started after settings load below if enabled
      // Seed default fps limit snapshot
      // GetFpsLimit removed from proxy, use s_fps_limit directly
      g_default_fps_limit.store(s_fps_limit);
      reshade::register_event<reshade::addon_event::present>(OnPresentUpdate);
      
      // Register frame lifecycle hooks for custom FPS limiter
      reshade::register_event<reshade::addon_event::begin_render_pass>(OnBeginRenderPass);
      reshade::register_event<reshade::addon_event::end_render_pass>(OnEndRenderPass);
      // Register overlay directly
      reshade::register_overlay("Display Commander", OnRegisterOverlayDisplayCommander);
      // Register device destroy event for restore-on-exit
      reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
      break;
    case DLL_PROCESS_DETACH:
      // Safety: attempt restore on detach as well (idempotent)
      renodx::display_restore::RestoreAllIfEnabled();
      // Uninstall process-exit hooks
      renodx::process_exit_hooks::Shutdown();
      
            // Clean up continuous monitoring if it's running
      StopContinuousMonitoring();
      
      // Clean up Reflex hooks if they're installed
      UninstallReflexHooks();
      
      // Clean up DXGI Device Info Manager
      g_dxgiDeviceInfoManager.reset();
      
      reshade::unregister_event<reshade::addon_event::present>(OnPresentUpdate);
      
      // Unregister frame lifecycle hooks for custom FPS limiter
      reshade::unregister_event<reshade::addon_event::begin_render_pass>(OnBeginRenderPass);
      reshade::unregister_event<reshade::addon_event::end_render_pass>(OnEndRenderPass);
      // Unregister overlay
      reshade::unregister_overlay("###settings", OnRegisterOverlayDisplayCommander);
      
      reshade::unregister_event<reshade::addon_event::set_fullscreen_state>(
          renodx::display_commander::events::OnSetFullscreenState);
      reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
      reshade::unregister_event<reshade::addon_event::create_swapchain>(OnCreateSwapchainCapture);
      renodx::display_restore::RestoreAllIfEnabled(); // restore display settings on exit
      reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
      reshade::unregister_addon(h_module);
      g_shutdown.store(true);
      break;
  }


  // Initialize hooks if settings are enabled on startup (after settings are loaded)
  if (fdw_reason == DLL_PROCESS_ATTACH) {


    // Initialize UI system before first draw
    InitializeUISettings();
    // InitializeSwapchain removed from proxy
    
    // Check if continuous monitoring should be enabled
    if (s_continuous_monitoring_enabled >= 0.5f) {
      StartContinuousMonitoring();
      LogInfo("Continuous monitoring started proactively");
    }

    
    // Initialize NVAPI fullscreen prevention if enabled
    if (s_nvapi_fullscreen_prevention >= 0.5f) {
      if (::g_nvapiFullscreenPrevention.Initialize()) {
        LogInfo("NVAPI initialized proactively for fullscreen prevention");
        if (::g_nvapiFullscreenPrevention.SetFullscreenPrevention(true)) {
          LogInfo("NVAPI fullscreen prevention applied proactively");
        } else {
          LogWarn("Failed to apply NVAPI fullscreen prevention proactively");
        }
      } else {
        LogWarn("Failed to initialize NVAPI proactively");
      }
    }

    // Initialize Reflex hooks if enabled
    if (s_reflex_enabled >= 0.5f) {
      if (InstallReflexHooks()) {
        LogInfo("Reflex hooks initialized proactively");
      } else {
        LogWarn("Failed to initialize Reflex hooks proactively");
      }
    }

    // Start NVAPI HDR monitor if enabled
    if (s_nvapi_hdr_logging >= 0.5f) {
      std::thread(RunBackgroundNvapiHdrMonitor).detach();
    }

    // Initialize Custom FPS Limiter system if any FPS limits are set
    if (s_fps_limit > 0.0f || s_fps_limit_background > 0.0f) {
      if (renodx::dxgi::fps_limiter::g_customFpsLimiterManager && renodx::dxgi::fps_limiter::g_customFpsLimiterManager->InitializeCustomFpsLimiterSystem()) {
        LogInfo("Custom FPS Limiter system auto-initialized at startup (FPS limits detected)");
      } else {
        LogWarn("Failed to initialize Custom FPS Limiter system at startup");
      }
    }

    // Initialize DXGI Device Info Manager
    g_dxgiDeviceInfoManager = std::make_unique<DXGIDeviceInfoManager>();
    if (g_dxgiDeviceInfoManager->Initialize()) {
      LogInfo("DXGI Device Info Manager initialized successfully");
    } else {
      LogWarn("Failed to initialize DXGI Device Info Manager");
    }
  }

  return TRUE;
}

// CONTINUOUS RENDERING FUNCTIONS REMOVED - Focus spoofing is now handled by Win32 hooks

// CONTINUOUS RENDERING THREAD REMOVED - Focus spoofing is now handled by Win32 hooks
// This provides a much cleaner and more effective solution


