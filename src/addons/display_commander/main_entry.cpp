#include "addon.hpp"
#include "config/display_commander_config.hpp"
#include "dx11_proxy/dx11_proxy_manager.hpp"
#include "exit_handler.hpp"
#include "globals.hpp"
#include "gpu_completion_monitoring.hpp"
#include "hooks/api_hooks.hpp"
#include "hooks/window_proc_hooks.hpp"
#include "latency/latency_manager.hpp"
#include "nvapi/nvapi_fullscreen_prevention.hpp"
#include "process_exit_hooks.hpp"
#include "settings/main_tab_settings.hpp"
#include "settings/developer_tab_settings.hpp"
#include "swapchain_events.hpp"
#include "swapchain_events_power_saving.hpp"
#include "ui/new_ui/experimental_tab.hpp"
#include "ui/new_ui/main_new_tab.hpp"
#include "widgets/dualsense_widget/dualsense_widget.hpp"
#include "widgets/xinput_widget/xinput_widget.hpp"
#include "hooks/hid_suppression_hooks.hpp"
#include "ui/new_ui/new_ui_main.hpp"
#include "utils/timing.hpp"
#include "version.hpp"
#include "autoclick/autoclick_manager.hpp"

#include <reshade.hpp>
#include <wrl/client.h>
#include <d3d11.h>
#include <psapi.h>
#include <chrono>

// Forward declarations for ReShade event handlers
void OnInitEffectRuntime(reshade::api::effect_runtime *runtime);
bool OnReShadeOverlayOpen(reshade::api::effect_runtime *runtime, bool open, reshade::api::input_source source);

// Forward declaration for ReShade settings override
void OverrideReShadeSettings();

// Forward declaration for version check
bool CheckReShadeVersionCompatibility();

// Forward declaration for multiple ReShade detection
void DetectMultipleReShadeVersions();

// Forward declaration for safemode function
void HandleSafemode();

// Function to parse version string and check if it's 6.5.1 or above
bool IsVersion651OrAbove(const std::string& version_str) {
    if (version_str.empty()) {
        return false;
    }

    // Parse version string in format "major.minor.build.revision"
    // We need to check if version is >= 6.5.1.0
    int major = 0;
    int minor = 0;
    int build = 0;
    int revision = 0;

    if (sscanf_s(version_str.c_str(), "%d.%d.%d.%d", &major, &minor, &build, &revision) >= 2) {
        // Check if version is 6.5.1 or above
        if (major > 6) {
            return true; // Major version > 6
        }
        if (major == 6) {
            if (minor > 5) {
                return true; // 6.x where x > 5
            }
            if (minor == 5) {
                return build >= 1; // 6.5.x where x >= 1
            }
        }
    }

    return false;
}

// Structure to store ReShade module detection debug information
struct ReShadeModuleInfo {
    std::string path;
    std::string version;
    bool has_imgui_support;
    bool is_version_651_or_above;
    HMODULE handle;
};

struct ReShadeDetectionDebugInfo {
    int total_modules_found = 0;
    std::vector<ReShadeModuleInfo> modules;
    bool detection_completed = false;
    std::string error_message;
};

// Global debug information storage
ReShadeDetectionDebugInfo g_reshade_debug_info;
namespace {
void OnRegisterOverlayDisplayCommander(reshade::api::effect_runtime *runtime) {
    // Update UI draw time for auto-click optimization
    autoclick::UpdateLastUIDrawTime();

    ui::new_ui::NewUISystem::GetInstance().Draw();

    // Periodically save config to ensure settings are persisted
    static auto last_save_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_save_time).count() >= 5) {
        display_commander::config::save_config();
        last_save_time = now;
    }
}
} // namespace

// ReShade effect runtime event handler for input blocking
void OnInitEffectRuntime(reshade::api::effect_runtime *runtime) {
    if (runtime == nullptr) {
        return;
    }
    AddReShadeRuntime(runtime);
    LogInfo("ReShade effect runtime initialized - Input blocking now available");

    if (s_nvapi_fix_hdr10_colorspace.load()) {
        runtime->set_color_space(reshade::api::color_space::hdr10_st2084);
    }
    static bool registered_overlay = false;
    if (!registered_overlay) {

        // Set up window procedure hooks now that we have the runtime
        HWND game_window = static_cast<HWND>(runtime->get_hwnd());
        if (game_window != nullptr && IsWindow(game_window) != 0) {
            LogInfo("Game window detected - HWND: 0x%p", game_window);

            // Initialize if not already done
            DoInitializationWithHwnd(game_window);
        } else {
            LogWarn("ReShade runtime window is not valid - HWND: 0x%p", game_window);
        }
        registered_overlay = true;
        reshade::register_overlay("Display Commander", OnRegisterOverlayDisplayCommander);

        // Start the auto-click thread (always running, sleeps when disabled)
        autoclick::StartAutoClickThread();
    }
}

// ReShade overlay event handler for input blocking
bool OnReShadeOverlayOpen(reshade::api::effect_runtime *runtime, bool open, reshade::api::input_source source) {
    if (open) {
        LogInfo("ReShade overlay opened - Input blocking active");
        // When ReShade overlay opens, we can also use its input blocking
        if (runtime != nullptr) {
            AddReShadeRuntime(runtime);
        }
    } else {
        LogInfo("ReShade overlay closed - Input blocking inactive");
    }

    // Update auto-click UI state for optimization
    autoclick::UpdateUIOverlayState(open);

    return false; // Don't prevent ReShade from opening/closing the overlay
}

// Direct overlay draw callback (no settings2 indirection)
namespace {

// Test callback for reshade_overlay event
void OnReShadeOverlayTest(reshade::api::effect_runtime *runtime) {
    // Check the setting from main tab
    if (!settings::g_mainTabSettings.show_test_overlay.GetValue()) {
        return;
    }

    // Test widget that appears in the main ReShade overlay
    ui::new_ui::DrawFrameTimeGraph();
}
} // namespace

bool initialized = false;

// Override ReShade settings to set tutorial as viewed and disable auto updates
void OverrideReShadeSettings() {
    LogInfo("Overriding ReShade settings - Setting tutorial as viewed and disabling auto updates");

    // Set tutorial progress to 4 (fully viewed)
    reshade::set_config_value(nullptr, "OVERLAY", "TutorialProgress", 4);
    //LogInfo("ReShade settings override - TutorialProgress set to 4 (viewed)");

    // Disable auto updates
    reshade::set_config_value(nullptr, "GENERAL", "CheckForUpdates", 0);
    LogInfo("ReShade settings override - CheckForUpdates set to 0 (disabled)");

    // Read LoadFromDllMain value from DisplayCommander.ini
    int32_t load_from_dll_main_from_display_commander = 1; // Default to 1 if not found
    bool found_in_display_commander = display_commander::config::get_config_value("DisplayCommander", "LoadFromDllMain", load_from_dll_main_from_display_commander);

    if (found_in_display_commander) {
        LogInfo("ReShade settings override - LoadFromDllMain value from DisplayCommander.ini: %d", load_from_dll_main_from_display_commander);
    } else {
        LogInfo("ReShade settings override - LoadFromDllMain not found in DisplayCommander.ini, using default value: %d", load_from_dll_main_from_display_commander);
    }

    // Get current value from ReShade.ini for logging
    int32_t current_reshade_value = 0;
    reshade::get_config_value(nullptr, "ADDON", "LoadFromDllMain", current_reshade_value);
    LogInfo("ReShade settings override - LoadFromDllMain current ReShade value: %d", current_reshade_value);

    // Set LoadFromDllMain to the value from DisplayCommander.ini
    reshade::set_config_value(nullptr, "ADDON", "LoadFromDllMain", load_from_dll_main_from_display_commander);
    LogInfo("ReShade settings override - LoadFromDllMain set to %d (from DisplayCommander.ini)", load_from_dll_main_from_display_commander);

    LogInfo("ReShade settings override completed successfully");
}

// Function to detect multiple ReShade versions by scanning all modules
void DetectMultipleReShadeVersions() {
    LogInfo("=== ReShade Module Detection ===");

    // Reset debug info
    g_reshade_debug_info = ReShadeDetectionDebugInfo();

    HMODULE modules[1024];
    DWORD num_modules = 0;

    // Use K32EnumProcessModules for safe enumeration from DllMain
    if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &num_modules) == 0) {
        DWORD error = GetLastError();
        LogWarn("Failed to enumerate process modules: %lu", error);
        g_reshade_debug_info.error_message = "Failed to enumerate process modules: " + std::to_string(error);
        g_reshade_debug_info.detection_completed = true;
        return;
    }

    if (num_modules > sizeof(modules)) {
        num_modules = static_cast<DWORD>(sizeof(modules));
    }

    int reshade_module_count = 0;
    std::vector<HMODULE> reshade_modules;

    LogInfo("Scanning %lu modules for ReShade...", num_modules / sizeof(HMODULE));

    for (DWORD i = 0; i < num_modules / sizeof(HMODULE); ++i) {
        HMODULE module = modules[i];
        if (module == nullptr) continue;

        // Check if this module has ReShadeRegisterAddon
        FARPROC register_func = GetProcAddress(module, "ReShadeRegisterAddon");
        FARPROC unregister_func = GetProcAddress(module, "ReShadeUnregisterAddon");

        if (register_func != nullptr && unregister_func != nullptr) {
            reshade_module_count++;
            reshade_modules.push_back(module);

            // Create module info for debug storage
            ReShadeModuleInfo module_info;
            module_info.handle = module;

            // Get module file path for detailed logging
            wchar_t module_path[MAX_PATH];
            DWORD path_length = GetModuleFileNameW(module, module_path, MAX_PATH);

            if (path_length > 0) {
                // Convert wide string to narrow string for logging
                char narrow_path[MAX_PATH];
                WideCharToMultiByte(CP_UTF8, 0, module_path, -1, narrow_path, MAX_PATH, nullptr, nullptr);
                module_info.path = narrow_path;

                LogInfo("Found ReShade module #%d: 0x%p - %s", reshade_module_count, module, narrow_path);

                // Try to get version information
                DWORD version_dummy = 0;
                DWORD version_size = GetFileVersionInfoSizeW(module_path, &version_dummy);
                if (version_size > 0) {
                    std::vector<uint8_t> version_data(version_size);
                    if (GetFileVersionInfoW(module_path, version_dummy, version_size, version_data.data()) != 0) {
                VS_FIXEDFILEINFO *version_info = nullptr;
                UINT version_info_size = 0;
                if (VerQueryValueW(version_data.data(), L"\\", reinterpret_cast<LPVOID*>(&version_info), &version_info_size) != 0 &&
                    version_info != nullptr) {
                            char version_str[64];
                            snprintf(version_str, sizeof(version_str), "%hu.%hu.%hu.%hu",
                                HIWORD(version_info->dwFileVersionMS),
                                LOWORD(version_info->dwFileVersionMS),
                                HIWORD(version_info->dwFileVersionLS),
                                LOWORD(version_info->dwFileVersionLS));
                            module_info.version = version_str;
                            module_info.is_version_651_or_above = IsVersion651OrAbove(version_str);
                            LogInfo("  Version: %s", version_str);
                            LogInfo("  Version 6.5.1+: %s", module_info.is_version_651_or_above ? "Yes" : "No");
                        }
                    }
                }

                // Check if this module also has ReShadeGetImGuiFunctionTable
                FARPROC imgui_func = GetProcAddress(module, "ReShadeGetImGuiFunctionTable");
                module_info.has_imgui_support = (imgui_func != nullptr);
                LogInfo("  ImGui Support: %s", imgui_func != nullptr ? "Yes" : "No");

                // If version extraction failed, set compatibility to false
                if (module_info.version.empty()) {
                    module_info.is_version_651_or_above = false;
                    LogInfo("  Version 6.5.1+: No (version unknown)");
                }

            } else {
                module_info.path = "(path unavailable)";
                LogInfo("Found ReShade module #%d: 0x%p - (path unavailable)", reshade_module_count, module);
            }

            // Store module info for debug display
            g_reshade_debug_info.modules.push_back(module_info);
        }
    }

    LogInfo("=== ReShade Detection Complete ===");
    LogInfo("Total ReShade modules found: %d", reshade_module_count);

    // Check if any module meets version requirements
    bool has_compatible_version = false;
    for (const auto& module : g_reshade_debug_info.modules) {
        if (module.is_version_651_or_above) {
            has_compatible_version = true;
            LogInfo("Found compatible ReShade version: %s", module.version.c_str());
            break;
        }
    }

    if (!has_compatible_version && !g_reshade_debug_info.modules.empty()) {
        LogWarn("No ReShade modules found with version 6.5.1 or above");
    }

    // Update debug info
    g_reshade_debug_info.total_modules_found = reshade_module_count;
    g_reshade_debug_info.detection_completed = true;

    if (reshade_module_count > 1) {
        LogWarn("WARNING: Multiple ReShade versions detected! This may cause conflicts.");
        LogWarn("Found %d ReShade modules - only the first one will be used for registration.", reshade_module_count);

        // Log warning about potential conflicts
        for (size_t i = 0; i < reshade_modules.size(); ++i) {
            LogWarn("  ReShade module %zu: 0x%p", i + 1, reshade_modules[i]);
        }
    } else if (reshade_module_count == 1) {
        LogInfo("Single ReShade module detected - proceeding with registration.");
    } else {
        LogError("No ReShade modules found! Registration will likely fail.");
        g_reshade_debug_info.error_message = "No ReShade modules found! Registration will likely fail.";
    }
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

    // Build debug information string
    std::string debug_info = "ERROR DETAILS:\n";
    debug_info += "• Required API Version: 17 (ReShade 6.5.1+)\n";

    // Check if we have version information
    bool has_version_info = false;
    bool has_compatible_version = false;
    std::string detected_versions;

    if (g_reshade_debug_info.detection_completed && !g_reshade_debug_info.modules.empty()) {
        for (const auto& module : g_reshade_debug_info.modules) {
            if (!module.version.empty()) {
                has_version_info = true;
                if (!detected_versions.empty()) {
                    detected_versions += ", ";
                }
                detected_versions += module.version;

                if (module.is_version_651_or_above) {
                    has_compatible_version = true;
                }
            }
        }
    }

    if (has_version_info) {
        debug_info += "• Detected ReShade Versions: " + detected_versions + "\n";
        debug_info += "• Version 6.5.1+ Compatible: " + std::string(has_compatible_version ? "Yes" : "No") + "\n";
    } else {
        debug_info += "• Your ReShade Version: Unknown (version detection failed)\n";
    }
    debug_info += "• Status: Incompatible\n\n";

    // Add module detection debug information
    if (g_reshade_debug_info.detection_completed) {
        debug_info += "MODULE DETECTION RESULTS:\n";
        debug_info += "• Total ReShade modules found: " + std::to_string(g_reshade_debug_info.total_modules_found) + "\n";

        if (!g_reshade_debug_info.error_message.empty()) {
            debug_info += "• Error: " + g_reshade_debug_info.error_message + "\n";
        }

        if (!g_reshade_debug_info.modules.empty()) {
            debug_info += "• Detected modules:\n";
            for (size_t i = 0; i < g_reshade_debug_info.modules.size(); ++i) {
                const auto& module = g_reshade_debug_info.modules[i];
                debug_info += "  " + std::to_string(i + 1) + ". " + module.path + "\n";
                if (!module.version.empty()) {
                    debug_info += "     Version: " + module.version + "\n";
                    debug_info += "     Version 6.5.1+: " + std::string(module.is_version_651_or_above ? "Yes" : "No") + "\n";
                } else {
                    debug_info += "     Version: Unknown\n";
                    debug_info += "     Version 6.5.1+: No (version unknown)\n";
                }
                debug_info += "     ImGui Support: " + std::string(module.has_imgui_support ? "Yes" : "No") + "\n";
                debug_info += "     Handle: 0x" + std::to_string(reinterpret_cast<uintptr_t>(module.handle)) + "\n";
            }
        } else {
            debug_info += "• No ReShade modules detected\n";
        }
        debug_info += "\n";
    } else {
        debug_info += "MODULE DETECTION:\n";
        debug_info += "• Detection not completed or failed\n\n";
    }

    debug_info += "SOLUTION:\n";
    debug_info += "1. Download the latest ReShade from: https://reshade.me/\n";
    debug_info += "2. Install ReShade 6.5.1 or newer\n";
    debug_info += "3. Restart your game to load the updated ReShade\n\n";
    debug_info += "This addon uses advanced features that require the newer ReShade API.";

    // Display detailed error message to user
    MessageBoxA(nullptr,
        debug_info.c_str(),
        "ReShade Version Incompatible - Update Required",
        MB_OK | MB_ICONERROR | MB_TOPMOST);

    return false;
}

// Safemode function - handles safemode logic
void HandleSafemode() {
    // Developer settings already loaded at startup
    bool safemode_enabled = settings::g_developerTabSettings.safemode.GetValue();

    if (safemode_enabled) {
        LogInfo("Safemode enabled - disabling auto-apply settings, continue rendering, FPS limiter, and XInput hooks");

        // Set safemode to 0 (force set to 0)
        settings::g_developerTabSettings.safemode.SetValue(false);

        // Disable all auto-apply settings
        s_auto_apply_resolution_change.store(false);
        s_auto_apply_refresh_rate_change.store(false);
        s_apply_display_settings_at_start.store(false);

        // Disable continue rendering
        s_continue_rendering.store(false);

        // Set FPS limiter mode to 0 (Disabled)
        s_fps_limiter_mode.store(FpsLimiterMode::kDisabled);

        // Disable XInput hooks
        auto xinput_shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
        if (xinput_shared_state) {
            xinput_shared_state->enable_xinput_hooks.store(false);
            // Save XInput setting to config
            display_commander::config::set_config_value("DisplayCommander.XInputWidget", "EnableXInputHooks", false);
        }

        // Save the changes
        settings::g_developerTabSettings.SaveAll();

        LogInfo("Safemode applied - auto-apply settings disabled, continue rendering disabled, FPS limiter set to disabled, XInput hooks disabled");
    } else {
        // If unset, force set to 0 so it appears in config
        settings::g_developerTabSettings.safemode.SetValue(false);
        settings::g_developerTabSettings.SaveAll();

        LogInfo("Safemode not enabled - setting to 0 for config visibility");
    }
}

void DoInitializationWithoutHwnd(HMODULE h_module, DWORD fdw_reason) {

    // Initialize QPC timing constants based on actual frequency
    utils::initialize_qpc_timing_constants();

    // Setup high-resolution timer for maximum precision
    if (utils::setup_high_resolution_timer()) {
        LogInfo("High-resolution timer setup successful");
    } else {
        LogWarn("Failed to setup high-resolution timer");
    }


    LogInfo("DLLMain (DisplayCommander) %lld %d h_module: 0x%p", utils::get_now_ns(), fdw_reason,
            reinterpret_cast<uintptr_t>(h_module));

    // Load all settings at startup
    settings::LoadAllSettingsAtStartup();

    HandleSafemode();

    // Pin the module to prevent premature unload
    HMODULE pinned_module = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(h_module), &pinned_module) != 0) {
        LogInfo("Module pinned successfully: 0x%p", pinned_module);
    } else {
        DWORD error = GetLastError();
        LogWarn("Failed to pin module: 0x%p, Error: %lu", h_module, error);
    }

    // Register reshade_overlay event for test code
    reshade::register_event<reshade::addon_event::reshade_overlay>(OnReShadeOverlayTest);

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
    //reshade::register_event<reshade::addon_event::finish_present>(OnPresentUpdateAfter);

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
   // reshade::register_event<reshade::addon_event::update_buffer_region_command>(OnUpdateBufferRegionCommand);

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
    // Override ReShade settings early to set tutorial as viewed and disable auto updates
    OverrideReShadeSettings();
}

BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {
    // Early logging to detect crash timing
    OutputDebugStringA("DisplayCommander: DllMain called\n");

    switch (fdw_reason) {
    case DLL_PROCESS_ATTACH: {
        OutputDebugStringA("DisplayCommander: DLL_PROCESS_ATTACH\n");
        g_shutdown.store(false);

        if (g_dll_initialization_complete.load()) {
            LogError("DLLMain(DisplayCommander) already initialized");
            return FALSE;
        }

        OutputDebugStringA("DisplayCommander: About to register addon\n");
        if (!reshade::register_addon(h_module)) {
            // Registration failed - likely due to API version mismatch
            OutputDebugStringA("DisplayCommander: ReShade addon registration FAILED\n");
            LogError("ReShade addon registration failed - this usually indicates an API version mismatch");
            LogError("Display Commander requires ReShade 6.5.1+ (API version 17) but detected older version");

            DetectMultipleReShadeVersions();
            CheckReShadeVersionCompatibility();
            return FALSE;
        }

        DetectMultipleReShadeVersions();
        OutputDebugStringA("DisplayCommander: ReShade addon registration SUCCESS\n");

        // Registration successful - log version compatibility
        LogInfo("Display Commander v%s - ReShade addon registration successful (API version 17 supported)", DISPLAY_COMMANDER_VERSION_STRING);

        // Initialize DisplayCommander config system before handling safemode
        display_commander::config::DisplayCommanderConfigManager::GetInstance().Initialize();
        LogInfo("DisplayCommander config system initialized");


        // Handle safemode after config system is initialized

        // Detect multiple ReShade versions AFTER successful registration to avoid interference
        // This prevents our module scanning from interfering with ReShade's internal module detection
        OutputDebugStringA("DisplayCommander: About to detect ReShade modules\n");

        // Store module handle for pinning
        g_hmodule = h_module;

        OutputDebugStringA("DisplayCommander: About to call DoInitializationWithoutHwnd\n");
        __try {
            DoInitializationWithoutHwnd(h_module, fdw_reason);
            OutputDebugStringA("DisplayCommander: DoInitializationWithoutHwnd completed\n");
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            OutputDebugStringA("DisplayCommander: EXCEPTION in DoInitializationWithoutHwnd\n");
            LogError("Exception occurred during initialization: 0x%x", GetExceptionCode());
            return FALSE;
        }

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
        StopGPUCompletionMonitoring();

        // Clean up experimental tab threads
        ui::new_ui::CleanupExperimentalTab();

        // Clean up DualSense support
        display_commander::widgets::dualsense_widget::CleanupDualSenseWidget();

        // Clean up HID suppression hooks
        renodx::hooks::UninstallHIDSuppressionHooks();

        // Clean up DX11 proxy device
        dx11_proxy::DX11ProxyManager::GetInstance().Shutdown();

        // Clean up NVAPI instances before shutdown
        if (g_latencyManager) {
            g_latencyManager->Shutdown();
        }

        // Clean up NVAPI fullscreen prevention
        g_nvapiFullscreenPrevention.Cleanup();

        // Note: reshade::unregister_addon() will automatically unregister all events and overlays
        // registered by this add-on, so manual unregistration is not needed and can cause issues
        // display_restore::RestoreAllIfEnabled(); // restore display settings on exit

        // Unpin the module before unregistration
        if (g_hmodule != nullptr) {
            if (FreeLibrary(g_hmodule) != 0) {
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
