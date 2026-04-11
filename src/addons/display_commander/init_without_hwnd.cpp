// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "init_without_hwnd.hpp"

#include "config/display_commander_config.hpp"
#include "config/override_reshade_settings.hpp"
#include "display/display_initial_state.hpp"
#include "features/auto_windows_hdr/auto_windows_hdr.hpp"
#include "features/dpi/dpi_management.hpp"
#include "dll_boot_logging.hpp"
#include "globals.hpp"
#include "hooks/loadlibrary_hooks.hpp"
#include "hooks/windows_hooks/api_hooks.hpp"
#include "hooks/windows_hooks/windows_message_hooks.hpp"
#include "latency/gpu_completion_monitoring.hpp"
#include "latent_sync/refresh_rate_monitor_integration.hpp"
#include "process_exit_hooks.hpp"
#include "proxy_dll/dxgi_proxy_init.hpp"
#include "settings/advanced_tab_settings.hpp"
#include "settings/main_tab_settings.hpp"
#include "ui/new_ui/new_ui_main.hpp"
#include "utils/logging.hpp"
#include "utils/timing.hpp"

// Libraries <Windows.h>
#include <Windows.h>

namespace {
void DoInitializationWithoutHwndSafe_Early(HMODULE h_module) {
    LogBootInitWithoutHwndStage("Early enter");
    if (!IsDisplayCommanderHookingInstance()) {
        LogBootInitWithoutHwndStage("Early return (not hooking instance)");
        return;
    }
    if (utils::setup_high_resolution_timer()) {
        LogInfo("High-resolution timer setup successful");
    } else {
        LogWarn("Failed to setup high-resolution timer");
    }
    LogInfo("DLLMain (DisplayCommander) %lld h_module: 0x%p", utils::get_now_ns(),
            reinterpret_cast<uintptr_t>(h_module));
    settings::LoadAllSettingsAtStartup();
    // docs/spec/features/auto_enable_windows_hdr.md — early DLL init (DllMain stack)
    display_commander::features::auto_windows_hdr::OnEarlyInitTryAutoEnableWindowsHdr();
    LogBootInitWithoutHwndStage("Early after LoadAllSettingsAtStartup");
    display_commanderhooks::InstallLoadLibraryHooks();
    LogBootInitWithoutHwndStage("Early after InstallLoadLibraryHooks");
    LogCurrentLogLevel();
    if (settings::g_advancedTabSettings.disable_dpi_scaling.GetValue()) {
        display_commander::display::dpi::DisableDPIScaling();
        LogInfo("DPI scaling disabled - process is now DPI-aware");
    }

    bool suppress_pin_module = false;
    (void)display_commander::config::get_config_value("DisplayCommander.Safemode", "SuppressPinModule",
                                                      suppress_pin_module);
    if (!suppress_pin_module) {
        HMODULE pinned_module = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(h_module), &pinned_module)
            != 0) {
            LogInfo("Module pinned successfully: 0x%p", pinned_module);
            g_module_pinned.store(true);
        } else {
            DWORD error = GetLastError();
            LogWarn("Failed to pin module: 0x%p, Error: %lu", h_module, error);
            g_module_pinned.store(false);
        }
    } else {
        LogInfo("Module pinning suppressed by config (SuppressPinModule=true)");
        g_module_pinned.store(false);
    }

    process_exit_hooks::Initialize();
    LogBootInitWithoutHwndStage("Early after process_exit_hooks::Initialize");
    LogInfo("DLL initialization complete - DXGI calls now enabled");
    LogInfo("DLL_THREAD_ATTACH: Installing API hooks...");
    LogBootInitWithoutHwndStage("Early before InstallApiHooks");
    display_commanderhooks::InstallApiHooks();
    LogBootInitWithoutHwndStage("Early after InstallApiHooks");
    InstallRealDXGIMinHookHooks();
    LogBootInitWithoutHwndStage("Early after InstallRealDXGIMinHookHooks");
    OverrideReShadeSettings(nullptr);
    LogBootInitWithoutHwndStage("Early after OverrideReShadeSettings");
}

void DoInitializationWithoutHwndSafe_Late() {
    LogBootInitWithoutHwndStage("Late enter");
    if (!IsDisplayCommanderHookingInstance()) {
        LogBootInitWithoutHwndStage("Late return (not hooking instance)");
        return;
    }

    display_cache::g_displayCache.Initialize();
    display_initial_state::g_initialDisplayState.CaptureInitialState();
    LogBootInitWithoutHwndStage("Late after display cache and initial state");
    ui::new_ui::InitializeNewUISystem();
    LogBootInitWithoutHwndStage("Late after InitializeNewUISystem");
    StartContinuousMonitoring();
    StartGPUCompletionMonitoring();
    dxgi::fps_limiter::StartRefreshRateMonitoring();
    LogBootInitWithoutHwndStage("Late after start monitoring (continuous, GPU, refresh rate)");
    display_commanderhooks::keyboard_tracker::Initialize();
    LogInfo("Keyboard tracking system initialized");
    LogBootInitWithoutHwndStage("Late after keyboard_tracker::Initialize");
    LogBootInitWithoutHwndStage("Late complete");
}
}  // namespace

void DoInitializationWithoutHwndSafe(HMODULE h_module) {
    LogBootInitWithoutHwndStage("enter");
    DoInitializationWithoutHwndSafe_Early(h_module);
    LogBootInitWithoutHwndStage("after Early");
    DoInitializationWithoutHwndSafe_Late();
    LogBootInitWithoutHwndStage("complete");
}
