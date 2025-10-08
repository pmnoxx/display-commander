#include "addon.hpp"
#include "display_restore.hpp"
#include "exit_handler.hpp"
#include "hooks/api_hooks.hpp"
#include "hooks/window_proc_hooks.hpp"
#include "latency/latency_manager.hpp"
#include "nvapi/nvapi_fullscreen_prevention.hpp"
#include "process_exit_hooks.hpp"
#include "settings/main_tab_settings.hpp"
#include "swapchain_events.hpp"
#include "swapchain_events_power_saving.hpp"
#include "ui/new_ui/experimental_tab.hpp"
#include "ui/new_ui/new_ui_main.hpp"
#include "utils/timing.hpp"
#include "version.hpp"

#include <reshade.hpp>
#include <wrl/client.h>
#include <d3d11.h>

// Forward declarations for ReShade event handlers
void OnInitEffectRuntime(reshade::api::effect_runtime *runtime);
bool OnReShadeOverlayOpen(reshade::api::effect_runtime *runtime, bool open, reshade::api::input_source source);

// Forward declaration for ReShade settings override
void OverrideReShadeSettings();

// Forward declaration for version check
bool CheckReShadeVersionCompatibility();
namespace {
// Destroy device handler to restore display if needed
void OnDestroyDevice(reshade::api::device * /*device*/) {
    LogInfo("ReShade device destroyed - Attempting to restore display settings");
    display_restore::RestoreAllIfEnabled();
}

void OnRegisterOverlayDisplayCommander(reshade::api::effect_runtime *runtime) {
    ui::new_ui::NewUISystem::GetInstance().Draw();
}
} // namespace

// ReShade effect runtime event handler for input blocking
void OnInitEffectRuntime(reshade::api::effect_runtime *runtime) {
    if (runtime == nullptr)
        return;

    g_reshade_runtime.store(runtime);
    LogInfo("ReShade effect runtime initialized - Input blocking now available");

    if (s_nvapi_fix_hdr10_colorspace.load()) {
        runtime->set_color_space(reshade::api::color_space::hdr10_st2084);
    }
    static bool registered_overlay = false;
    if (!registered_overlay) {

        // Set up window procedure hooks now that we have the runtime
        HWND game_window = static_cast<HWND>(runtime->get_hwnd());
        if (game_window != nullptr && IsWindow(game_window)) {
            LogInfo("Game window detected - HWND: 0x%p", game_window);

            // Initialize if not already done
            DoInitializationWithHwnd(game_window);
        } else {
            LogWarn("ReShade runtime window is not valid - HWND: 0x%p", game_window);
        }
        registered_overlay = true;
        reshade::register_overlay("Display Commander", OnRegisterOverlayDisplayCommander);
    }
}

// ReShade overlay event handler for input blocking
bool OnReShadeOverlayOpen(reshade::api::effect_runtime *runtime, bool open, reshade::api::input_source source) {
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

// Test callback for reshade_overlay event
void OnReShadeOverlayTest(reshade::api::effect_runtime *runtime) {
    // Check the setting from main tab
    if (!settings::g_mainTabSettings.show_test_overlay.GetValue())
        return;

    // Test widget that appears in the main ReShade overlay
    static bool show_test_widget = true;
    if (ImGui::Begin("Display Commander Test Widget", &show_test_widget, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("This is a test widget using reshade_overlay event!");
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                           "Notice: This appears in the main ReShade overlay, not as a separate tab!");
        ImGui::Separator();

        static bool test_checkbox = false;
        ImGui::Checkbox("Test Checkbox", &test_checkbox);

        static float test_slider = 0.5f;
        ImGui::SliderFloat("Test Slider", &test_slider, 0.0f, 1.0f);

        if (ImGui::Button("Test Button")) {
            LogInfo("Test button clicked from reshade_overlay event!");
        }

        ImGui::Separator();
        ImGui::Text("Performance Info:");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Frame Count: %d", ImGui::GetFrameCount());

        ImGui::Separator();
        ImGui::Text("This widget demonstrates the difference between:");
        ImGui::BulletText("register_overlay: Creates separate tabs in ReShade overlay");
        ImGui::BulletText("reshade_overlay: Draws directly in main overlay (this widget)");
    }
    ImGui::End();
}
} // namespace

bool initialized = false;

// Override ReShade settings to set tutorial as viewed and disable auto updates
void OverrideReShadeSettings() {
    LogInfo("Overriding ReShade settings - Setting tutorial as viewed and disabling auto updates");

    // Set tutorial progress to 4 (fully viewed)
    reshade::set_config_value(nullptr, "GENERAL", "TutorialProgress", 4);
    LogInfo("ReShade settings override - TutorialProgress set to 4 (viewed)");

    // Disable auto updates
    reshade::set_config_value(nullptr, "GENERAL", "CheckForUpdates", 0);
    LogInfo("ReShade settings override - CheckForUpdates set to 0 (disabled)");

    // Set LoadFromDllMain to 1 and log previous value
    int32_t load_from_dll_main = 0;
    reshade::get_config_value(nullptr, "ADDON", "LoadFromDllMain", load_from_dll_main);
    LogInfo("ReShade settings override - LoadFromDllMain previous value: %d", load_from_dll_main);
    reshade::set_config_value(nullptr, "ADDON", "LoadFromDllMain", 1);
    LogInfo("ReShade settings override - LoadFromDllMain set to 1");

    LogInfo("ReShade settings override completed successfully");
}

// Version compatibility check function
bool CheckReShadeVersionCompatibility() {
    static bool first_time = true;
    if (!first_time) {
        return false;
    }
    first_time = false;
    // This function will be called after registration fails
    // We'll display a helpful error message to the user
    LogError("ReShade addon registration failed - API version not supported");

    // Display detailed error message to user
    MessageBoxA(nullptr,
        "Display Commander requires ReShade 6.6.0 or newer.\n\n"
        "ERROR DETAILS:\n"
        "• Required API Version: 18 (ReShade 6.6.0+)\n"
        "• Your ReShade Version: 17 or older\n"
        "• Status: Incompatible\n\n"
        "SOLUTION:\n"
        "1. Download the latest ReShade from: https://reshade.me/\n"
        "2. Install ReShade 6.6.0 or newer\n"
        "3. Restart your game to load the updated ReShade\n\n"
        "This addon uses advanced features that require the newer ReShade API.",
        "ReShade Version Incompatible - Update Required",
        MB_OK | MB_ICONERROR | MB_TOPMOST);

    return false;
}

void DoInitializationWithoutHwnd(HMODULE h_module, DWORD fdw_reason) {

    // Initialize QPC timing constants based on actual frequency
    utils::initialize_qpc_timing_constants();

    // Override ReShade settings early to set tutorial as viewed and disable auto updates
    OverrideReShadeSettings();

    LogInfo("DLLMain (DisplayCommander) %lld %d h_module: 0x%p", utils::get_now_ns(), fdw_reason,
            reinterpret_cast<uintptr_t>(h_module));

    // Pin the module to prevent premature unload
    HMODULE pinned_module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(h_module), &pinned_module)) {
        LogInfo("Module pinned successfully: 0x%p", pinned_module);
    } else {
        DWORD error = GetLastError();
        LogWarn("Failed to pin module: 0x%p, Error: %lu", h_module, error);
    }

    // Register reshade_overlay event for test code
    reshade::register_event<reshade::addon_event::reshade_overlay>(OnReShadeOverlayTest);

    // Capture sync interval on swapchain creation for UI
    reshade::register_event<reshade::addon_event::create_swapchain>(OnCreateSwapchainCapture);

    reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);

    // Register ReShade effect runtime events for input blocking
    reshade::register_event<reshade::addon_event::init_effect_runtime>(OnInitEffectRuntime);
    reshade::register_event<reshade::addon_event::reshade_open_overlay>(OnReShadeOverlayOpen);

    // Defer NVAPI init until after settings are loaded below

    // Register our fullscreen prevention event handler
    reshade::register_event<reshade::addon_event::set_fullscreen_state>(OnSetFullscreenState);

    // NVAPI HDR monitor will be started after settings load below if enabled
    // Seed default fps limit snapshot
    // GetFpsLimit removed from proxy, use s_fps_limit directly
    reshade::register_event<reshade::addon_event::present>(OnPresentUpdateBefore);
    reshade::register_event<reshade::addon_event::reshade_present>(OnPresentUpdateBefore2);
    reshade::register_event<reshade::addon_event::finish_present>(OnPresentUpdateAfter);
    // reshade::register_event<reshade::addon_event::present_flags>(OnPresentFlags); // Not available in current ReShade
    // API

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

    LogInfo("DLL initialization complete - DXGI calls now enabled");

    // Install API hooks for continue rendering
    LogInfo("DLL_THREAD_ATTACH: Installing API hooks...");
    display_commanderhooks::InstallApiHooks();

    g_dll_initialization_complete.store(true);
}

BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {
    switch (fdw_reason) {
    case DLL_PROCESS_ATTACH: {
        g_shutdown.store(false);

        if (g_dll_initialization_complete.load()) {
            LogError("DLLMain(DisplayCommander) already initialized");
            return FALSE;
        }

        if (!reshade::register_addon(h_module)) {
            // Registration failed - likely due to API version mismatch
            LogError("ReShade addon registration failed - this usually indicates an API version mismatch");
            LogError("Display Commander requires ReShade 6.6.0+ (API version 18) but detected older version");
            CheckReShadeVersionCompatibility();
            return FALSE;
        }

        // Registration successful - log version compatibility
        LogInfo("Display Commander v%s - ReShade addon registration successful (API version 18 supported)", DISPLAY_COMMANDER_VERSION_STRING);

        // Store module handle for pinning
        g_hmodule = h_module;

        DoInitializationWithoutHwnd(h_module, fdw_reason);

        break;
    }
    case DLL_THREAD_ATTACH: {
        break;
    }
    case DLL_THREAD_DETACH: {
        // Log exit detection
        // exit_handler::OnHandleExit(exit_handler::ExitSource::DLL_THREAD_DETACH_EVENT, "DLL thread detach");
        break;
    }

    case DLL_PROCESS_DETACH:
        LogInfo("DLL_PROCESS_DETACH: DLL process detach");
        g_shutdown.store(true);

        // Log exit detection
        exit_handler::OnHandleExit(exit_handler::ExitSource::DLL_PROCESS_DETACH_EVENT, "DLL process detach");

        // Clean up input blocking system
        // Input blocking cleanup is now handled by Windows message hooks

        // Clean up window procedure hooks
        display_commanderhooks::UninstallWindowProcHooks();

        // Clean up API hooks
        display_commanderhooks::UninstallApiHooks();

        // Clean up continuous monitoring if it's running
        StopContinuousMonitoring();

        // Clean up experimental tab threads
        ui::new_ui::CleanupExperimentalTab();

        // Clean up NVAPI instances before shutdown
        if (g_latencyManager) {
            g_latencyManager->Shutdown();
        }

        // Clean up NVAPI fullscreen prevention
        extern NVAPIFullscreenPrevention g_nvapiFullscreenPrevention;
        g_nvapiFullscreenPrevention.Cleanup();

        // Note: reshade::unregister_addon() will automatically unregister all events and overlays
        // registered by this add-on, so manual unregistration is not needed and can cause issues
        // display_restore::RestoreAllIfEnabled(); // restore display settings on exit

        // Unpin the module before unregistration
        if (g_hmodule != nullptr) {
            if (FreeLibrary(g_hmodule)) {
                LogInfo("Module unpinned successfully: 0x%p", g_hmodule);
            } else {
                DWORD error = GetLastError();
                LogWarn("Failed to unpin module: 0x%p, Error: %lu", g_hmodule, error);
            }
            g_hmodule = nullptr;
        }

        reshade::unregister_addon(h_module);

        break;
    }

    return TRUE;
}

// CONTINUOUS RENDERING FUNCTIONS REMOVED - Focus spoofing is now handled by Win32 hooks

// CONTINUOUS RENDERING THREAD REMOVED - Focus spoofing is now handled by Win32 hooks
// This provides a much cleaner and more effective solution
