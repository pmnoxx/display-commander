# Dead Code Audit — File-by-File Checklist

**Purpose:** Inspect one file at a time for dead/unused code. Paths are relative to `src/addons/display_commander/`. For each file: look for uncalled functions, `#if 0` blocks, `#ifdef` guards that are never defined, large commented blocks, and unused static/helpers.

**How to use:** Pick one task, open the file, search for definitions and call sites (or use grep), then mark the task done and note any findings (remove / keep with reason).

---

## Root

- [x] addon.cpp — no dead code (all statics/exports used; RunCommandLine→CommandLine, DetectExeForPath/RunStandaloneUI from cli_standalone_ui)
- [x] continuous_monitoring.cpp — no dead code (all functions called); **removed** orphaned comment `// #define TRY_CATCH_BLOCKS`
- [x] display_cache.cpp — no dead code (GetMonitorFriendlyName, EnumerateDisplayModes, ParseDisplayNumberFromDeviceName all used)
- [x] display_initial_state.cpp — **removed** dead `GetInitialStateForDisplayId()` (never called)
- [x] display_restore.cpp — **removed:** Clear(), HasAnyChanges() (never called)
- [x] exit_handler.cpp — no dead code (WriteToDebugLog, WriteMultiLineToDebugLog, OnHandleExit, GetExitSourceString all used)
- [x] globals.cpp — no dead code (spot-checked; DetectWine, FPS/Reflex/DLSS/ReShade helpers, lock diagnostics all referenced)
- [x] gpu_completion_monitoring.cpp — no dead code (Start/StopGPUCompletionMonitoring, HandleOpenGLGPUCompletion all used)
- [x] main_entry.cpp — no dead code (ReShade callbacks, static helpers IsAddonEnabledForLoading/IsVersion662OrAbove/InjectIntoProcess/WaitForProcessAndInject used; RunStandaloneSettingsUI forward-decl, impl in cli_standalone_ui)
- [x] process_exit_hooks.cpp — no dead code (anon helpers used by LogCrashReport/AtExitHandler; Initialize/Shutdown from main)
- [x] resolution_helpers.cpp — no dead code (ApplyDisplaySettingsDXGI used from resolution_widget, monitor_settings)
- [x] swapchain_events.cpp — **removed:** ApplyHdr1000MetadataToCurrentSwapchain(), GetManualColorSpaceDXGIAsInt(); no remaining dead code
- [x] swapchain_events_power_saving.cpp — no dead code (ShouldBackgroundSuppressOperation + On* ReShade callbacks used)

## config

- [x] config/chords_file.cpp — no dead code (ParseTomlLine, TryMigrateFromGameConfig, IsChordConfigKey, Load/Save/Get/SetChordValue all used; display_commander_config delegates chord keys here)
- [x] config/display_commander_config.cpp — no dead code (IniFile/TomlFile + get/set/save_config, get_config_value_ensure_exists used widely; chord/hotkey delegation to chords_file/hotkeys_file)
- [x] config/hotkeys_file.cpp — no dead code (ParseTomlLine, MigrateHotkeyKeysFromMap used by TryMigrateFromGameIni; Load/Save/Get/SetHotkeyValue, IsHotkeyConfigKey used by display_commander_config)

## hid

- [x] hid/ — **removed:** `hid/hid_enumeration.hpp` (never included, no implementation or call sites). `hid/` may be empty; no .cpp in this dir.

## display

- [x] display/dpi_management.cpp — no dead code (static helpers used by Disable/EnableDPIScaling; public API used from main_entry, advanced_tab)
- [x] display/hdr_control.cpp — no dead code (FindPathForMonitor used by Get/SetHdr; public API used from swapchain_events, resolution_widget)
- [x] display/query_display.cpp — no dead code (QueryDisplayTimingInfo, GetCurrentDisplaySettingsQueryConfig, DisplayTimingInfo used from display_cache, vblank_monitor)

## dxgi

- [x] dxgi/custom_fps_limiter.cpp — **removed:** file and custom_fps_limiter.hpp deleted; g_customFpsLimiter, g_custom_fps_limiter_callback removed from globals (FPS limiting uses LatentSyncLimiter)
- [x] dxgi/vram_info.cpp — no dead code (GetOrCreateVramAdapter, ClearVramAdapterCache used by GetVramInfo; GetVramInfo used from main_new_tab)

## hooks (root)

- [x] hooks/api_hooks.cpp — **removed:** RestoreSetCursor/RestoreShowCursor (were only in commented-out code); commented-out calls removed from continuous_monitoring
- [x] hooks/dbghelp_hooks.cpp — no dead code (LogCollectedStackWalk used by hook; InstallDbgHelpHooks from loadlibrary_hooks)
- [x] hooks/debug_output_hooks.cpp — no dead code (LogDebugOutput used by detours; Install/Uninstall from api_hooks)
- [x] hooks/dinput_hooks.cpp — no dead code (prior #if 0 block removed in earlier audit)
- [x] hooks/display_settings_hooks.cpp — **removed:** AreDisplaySettingsHooksInstalled() (never called); Install/Uninstall used from api_hooks
- [x] hooks/dpi_hooks.cpp — no dead code (EnsureDpiAwarenessContext used by hook; Install/Uninstall from api_hooks)
- [x] hooks/dualsense_hooks.cpp — **removed:** CleanupDualSenseSupport(); no remaining dead code
- [x] hooks/dxgi_factory_wrapper.cpp — no dead code (FlushCommandQueueFromSwapchain, TrackPresentStatistics used from swapchain_events and internally)
- [x] hooks/game_input_hooks.cpp — no dead code (InstallGameInputHooks from loadlibrary_hooks)
- [x] hooks/hid_additional_hooks.cpp — **removed:** UninstallAdditionalHIDHooks(); no remaining dead code
- [x] hooks/hid_hooks_install.cpp — no dead code (InstallHIDKernel32Hooks, InstallHIDDHooks from loadlibrary_hooks)
- [x] hooks/hid_statistics.cpp — **removed:** ResetAllHIDStats(); no remaining dead code
- [x] hooks/hid_suppression_hooks.cpp — no dead code (AreHIDSuppressionHooksInstalled, Mark* used; Uninstall from main_entry)
- [x] hooks/hook_suppression_manager.cpp — **removed:** WasHookInstalled(); no remaining dead code
- [x] hooks/input_activity_stats.cpp — no dead code (GetInstance, MarkActive, MarkActiveByHookIndex etc. used)
- [x] hooks/loadlibrary_hooks.cpp — **removed:** IsModuleSrwlockHeld(), IsBlockedDllsSrwlockHeld(); no remaining dead code
- [x] hooks/mutually_exclusive_keys.cpp — **removed:** file deleted (dead module; exclusive_key_groups in windows_message_hooks is used instead)
- [x] hooks/ngx_hooks.cpp — no dead code (static helpers used internally; InstallNGXHooks, CleanupNGXHooks, AreNGXParameterVTableHooksInstalled, IsDLSS*, ResetNGXPresetInitialization, ApplyNGXParameterOverride used)
- [x] hooks/nvapi_hooks.cpp — no dead code (Install/Uninstall from loadlibrary; IsNvapiLockHeld from globals/diagnostics)
- [x] hooks/opengl_hooks.cpp — **removed:** AreOpenGLHooksInstalled(); no remaining dead code
- [x] hooks/pclstats_etw_hooks.cpp — no dead code (Install/Uninstall, Are*Installed, PCLStats*, Get/Reset counts used from reflex_manager, UI)
- [x] hooks/present_traffic_tracking.cpp — no dead code (GetPresentTrafficApisString used from main_new_tab; atomics written by dxgi/d3d9/opengl/ddraw present hooks)
- [x] hooks/process_exit_hooks.cpp — no dead code (Install/UninstallProcessExitHooks from api_hooks)
- [x] hooks/rand_hooks.cpp — no dead code (Install/Uninstall from api_hooks; AreRandHooksInstalled from experimental_tab)
- [x] hooks/sleep_hooks.cpp — no dead code (Install/Uninstall from api_hooks)
- [x] hooks/streamline_hooks.cpp — no dead code (static helpers + detours; InstallStreamlineHooks, InitializePreventSLUpgradeInterface from loadlibrary)
- [x] hooks/timeslowdown_hooks.cpp — **removed (run 62):** GetHookNameById; **removed (run 65):** SetTimerHookType(name), GetTimerHookType(name), IsTimerHookEnabled(name), GetTimerHookCallCount(name), GetAllHookIdentifiers(), GetHookTypeByName(), ShouldApplyHook(name); **removed (run 66):** write-only g_qpf_call_count; **removed (run 68):** GetQPCallingModules(); **removed (run 69):** GetHookIdentifierByName(); no remaining dead code (Install/Uninstall, Are*Installed, ById APIs, GetQPCallingModulesWithHandles, HOOK_* used from UI and hooks)
- [x] hooks/window_proc_hooks.cpp — no dead code (Install/Uninstall, ProcessWindowMessage, DetourWindowMessage, IsContinueRenderingEnabled, SendFakeActivationMessages, WindowHasBorder used)
- [x] hooks/windows_gaming_input_hooks.cpp — no dead code (Install/Uninstall from loadlibrary)
- [x] hooks/winmm_joystick_hooks.cpp — **removed:** file deleted (WinMM joystick hooks removed)
- [x] hooks/xinput_hooks.cpp — no dead code (InstallXInputHooks from loadlibrary; EnsureXInputSetStateForTest from xinput_widget; IsXInputHooksInstalled, ApplyThumbstickProcessing used)

## hooks/d3d9

- [x] hooks/d3d9/d3d9_device_vtable_logging.cpp — no dead code (static helpers used; InstallD3D9DeviceVtableLogging from d3d9_hooks)
- [x] hooks/d3d9/d3d9_hooks.cpp — **removed:** AreDX9HooksInstalled(); no remaining dead code
- [x] hooks/d3d9/d3d9_pool_upgrade.cpp — no dead code (IsD3D9FixCreateTextureDimensionsEnabled used from d3d9_device_vtable_logging)
- [x] hooks/d3d9/d3d9_present_hooks.cpp — no dead code (HookD3D9Present, UnhookD3D9Present, RecordPresentUpdateDevice used from d3d9_hooks, swapchain)
- [x] hooks/d3d9/d3d9_present_params_upgrade.cpp — no dead code (ApplyD3D9PresentParameterUpgrades used from d3d9_hooks)

## hooks/ddraw

- [x] hooks/ddraw/ddraw_present_hooks.cpp — **removed:** AreDDrawHooksInstalled(); no remaining dead code

## hooks/dxgi

- [x] hooks/dxgi/dxgi_present_hooks.cpp — **removed:** HasTrackedSwapchains(), ClearAllTrackedSwapchains(), GetAllTrackedSwapchains(), GetTrackedSwapchainCount(), IsSwapchainTracked(), AddSwapchainToTracking(), RemoveSwapchainFromTracking() free fns, HookSwapchainNative(); no remaining dead code

## hooks/vulkan

- [x] hooks/vulkan/nvlowlatencyvk_hooks.cpp — no dead code (Install/Uninstall, Are*Installed, Get* debug/counts used from loadlibrary, UI)
- [x] hooks/vulkan/vulkan_loader_hooks.cpp — no dead code (Install/Uninstall, Are*Installed, Get* used from loadlibrary, UI)

## hooks/wgi

- [x] hooks/wgi/corewindow_proxy.cpp — no dead code (CoreWindowProxy COM impl; used by windows_gaming_input when wrapping ICoreWindow)

## hooks/windows_hooks

- [x] hooks/windows_hooks/windows_message_hooks.cpp — **removed:** IsKeyActuallyPressed(), IsUIOpenedRecently(); no remaining dead code

## ui

- [x] ui/cli_standalone_ui.cpp — no dead code (RunStandaloneSettingsUI, CLI entry; no #if 0)
- [x] ui/imgui_wrapper_standalone.cpp — no dead code (standalone ImGui impl; no #if 0)
- [x] ui/nvidia_profile_tab_shared.cpp — no dead code (shared Nvidia profile UI; no #if 0)
- [x] ui/standalone_ui_settings_bridge.cpp — no dead code (bridge to settings; no #if 0)
- [x] ui/ui_display_tab.cpp — no dead code (legacy display tab; no #if 0)

## ui/monitor_settings

- [x] ui/monitor_settings/monitor_settings.cpp — no dead code (ApplyDisplaySettingsDXGI, monitor UI; no #if 0)

## ui/new_ui

- [x] ui/new_ui/addons_tab.cpp — no dead code (tab + config; no #if 0)
- [x] ui/new_ui/advanced_tab.cpp — no dead code (DPI, Vulkan, etc.; no #if 0)
- [x] ui/new_ui/experimental_tab.cpp — no dead code (hooks/rand/HID UI; no #if 0)
- [x] ui/new_ui/hook_stats_tab.cpp — no dead code (hook stats UI; no #if 0)
- [x] ui/new_ui/hotkeys_tab.cpp — no dead code (hotkeys UI; no #if 0)
- [x] ui/new_ui/main_new_tab.cpp — no dead code (main tab, traffic APIs, VRAM, etc.; no #if 0)
- [x] ui/new_ui/new_ui_main.cpp — no dead code (new UI entry; no #if 0)
- [x] ui/new_ui/new_ui_tabs.cpp — no dead code (tab registration; no #if 0)
- [x] ui/new_ui/new_ui_wrapper.cpp — no dead code (wrapper; no #if 0)
- [x] ui/new_ui/performance_tab.cpp — no dead code (perf metrics; no #if 0)
- [x] ui/new_ui/settings_wrapper.cpp — **removed:** LoadTabSettings(vector), ButtonSetting, TextSetting, SeparatorSetting, SpacingSetting; no remaining dead code
- [x] ui/new_ui/streamline_tab.cpp — no dead code (Streamline tab; no #if 0)
- [x] ui/new_ui/swapchain_tab.cpp — no dead code (swapchain tab; no #if 0)
- [x] ui/new_ui/updates_tab.cpp — no dead code (updates tab; no #if 0)
- [x] ui/new_ui/vulkan_tab.cpp — no dead code (Vulkan/NVLL UI; no #if 0)
- [x] ui/new_ui/window_info_tab.cpp — no dead code (window info, CR debug; no #if 0)

## utils

- [x] utils/detour_call_tracker.cpp — no dead code (RECORD_DETOUR_CALL, stats; no #if 0)
- [x] utils/display_commander_logger.cpp — **removed:** ScopedForceAutoFlush, IncrementForceAutoFlush, DecrementForceAutoFlush; no remaining dead code
- [x] utils/file_sha256.cpp — no dead code (hashing; no #if 0)
- [x] utils/game_launcher_registry.cpp — no dead code (registry; no #if 0)
- [x] utils/general_utils.cpp — **removed:** TestDLSSPresetSupport() (debug/test function, never called)
- [x] utils/logging.cpp — no dead code (LogInfo etc.; no #if 0)
- [x] utils/mpo_registry.cpp — no dead code (MPO; no #if 0)
- [x] utils/overlay_window_detector.cpp — no dead code (overlay detection; no #if 0)
- [x] utils/perf_measurement.cpp — no dead code (metrics; no #if 0)
- [x] utils/platform_api_detector.cpp — no dead code (platform detection; no #if 0)
- [x] utils/process_window_enumerator.cpp — no dead code (window enum; no #if 0)
- [x] utils/reshade_global_config.cpp — **removed:** SetLoadFromDllMain() (never called)
- [x] utils/reshade_sha256_database.cpp — no dead code (SHA DB; no #if 0)
- [x] utils/srwlock_registry.cpp — no dead code (TryIsSRWLockHeld etc.; no #if 0)
- [x] utils/stack_trace.cpp — no dead code (stack trace; no #if 0)
- [x] utils/steam_library.cpp — no dead code (Steam libs; no #if 0)
- [x] utils/timing.cpp — **removed:** supports_mwaitx(); no remaining dead code
- [x] utils/version_check.cpp — no dead code (version check; no #if 0)

## Other dirs

- [x] adhd_multi_monitor/adhd_multi_monitor.cpp — no dead code (ADHD API; no #if 0)
- [x] adhd_multi_monitor/adhd_simple_api.cpp — **removed:** api::Shutdown(); no remaining dead code
- [x] audio/audio_channel_volume.cpp — no dead code (audio; no #if 0)
- [x] audio/audio_management.cpp — no dead code (audio; no #if 0)
- [x] autoclick/autoclick_action.cpp — no dead code (autoclick; no #if 0)
- [x] autoclick/autoclick_manager.cpp — **removed:** SendKeyDown(), SendKeyUp(); no remaining dead code
- [x] dbghelp_loader.cpp — no dead code (DbgHelp load; no #if 0)
- [x] dlss/dlss_indicator_manager.cpp — no dead code (DLSS indicator; no #if 0)
- [x] dualsense/dualsense_hid_wrapper.cpp — no dead code (HID wrapper; no #if 0)
- [x] input_remapping/input_remapping.cpp — **removed:** cleanup_input_remapping(); no remaining dead code
- [x] latency/latency_manager.cpp — **removed (run 72):** GetCurrentTechnology(), GetCurrentTechnologyName(); **removed (run 74):** SetConfig(), GetConfig(), extern GetTargetFps(); **removed (run 78):** IncreaseFrameId() declaration (no def, no call sites); no remaining dead code (latency; no #if 0)
- [x] latency/reflex_provider.cpp — no dead code (Reflex; no #if 0)
- [x] latent_sync/latent_sync_limiter.cpp — no dead code (LimitFrameRate etc.; no #if 0)
- [x] latent_sync/latent_sync_manager.cpp — no dead code (manager; no #if 0)
- [x] latent_sync/refresh_rate_monitor.cpp — no dead code (refresh monitor; no #if 0)
- [x] latent_sync/refresh_rate_monitor_integration.cpp — **removed:** IsRefreshRateMonitoringActive(), ProcessFrameStatistics() free fn, GetRefreshRateStatusString(); **header:** ForEachRefreshRateSample(); no remaining dead code
- [x] latent_sync/vblank_monitor.cpp — no dead code (vblank; no #if 0)
- [x] latent_sync/vblank_monitor_integration.cpp — **removed:** BindVBlankMonitorToWindow(); no remaining dead code
- [x] nvapi/nvapi_actual_refresh_rate_monitor.cpp — no dead code (refresh monitor; no #if 0)
- [x] nvapi/nvapi_fullscreen_prevention.cpp — no dead code (fullscreen prevention; no #if 0)
- [x] nvapi/nvidia_profile_search.cpp — no dead code (profile search; no #if 0)
- [x] nvapi/nvpi_reference.cpp — no dead code (NvPI ref; no #if 0)
- [x] nvapi/reflex_manager.cpp — **removed (run 78):** IncreaseFrameId() declaration (no def, no call sites); no remaining dead code (Reflex; no #if 0)
- [x] nvapi/vrr_status.cpp — no dead code (VRR; no #if 0)
- [x] presentmon/presentmon_manager.cpp — no dead code (PresentMon; no #if 0)
- [x] proxy_dll/d3d11_proxy.cpp — no dead code (proxy; no #if 0)
- [x] proxy_dll/d3d12_proxy.cpp — no dead code (proxy; no #if 0)
- [x] proxy_dll/d3d9_proxy.cpp — no dead code (proxy; no #if 0)
- [x] proxy_dll/ddraw_proxy.cpp — no dead code (proxy; no #if 0)
- [x] proxy_dll/dxgi_proxy.cpp — no dead code (proxy; no #if 0)
- [x] proxy_dll/opengl32_proxy.cpp — no dead code (proxy; no #if 0)
- [x] proxy_dll/reshade_loader.cpp — no dead code (loader; no #if 0)
- [x] proxy_dll/version_proxy.cpp — no dead code (proxy; no #if 0)
- [x] proxy_dll/winmm_proxy.cpp — no dead code (proxy; no #if 0)
- [x] settings/advanced_tab_settings.cpp — **removed:** SetManualColorSpace(ManualColorSpace), GetManualColorSpace() (UI uses SetManualColorSpaceIndex/GetManualColorSpaceIndex)
- [x] settings/display_settings.cpp — no dead code (settings; no #if 0)
- [x] settings/experimental_tab_settings.cpp — no dead code (settings; no #if 0)
- [x] settings/hook_suppression_settings.cpp — no dead code (settings; no #if 0)
- [x] settings/hotkeys_tab_settings.cpp — no dead code (settings; no #if 0)
- [x] settings/main_tab_settings.cpp — no dead code (settings; no #if 0)
- [x] settings/reshade_tab_settings.cpp — no dead code (settings; no #if 0)
- [x] settings/streamline_tab_settings.cpp — no dead code (settings; no #if 0)
- [x] settings/swapchain_tab_settings.cpp — no dead code (settings; no #if 0)
- [x] widgets/dualsense_widget/dualsense_widget.cpp — no dead code (widget; no #if 0)
- [x] widgets/remapping_widget/remapping_widget.cpp — no dead code (widget; no #if 0)
- [x] widgets/resolution_widget/resolution_settings.cpp — **removed:** ResolutionSettingsManager::ResetAllDirty(); no remaining dead code
- [x] widgets/resolution_widget/resolution_widget.cpp — no dead code (widget; no #if 0)
- [x] widgets/xinput_widget/xinput_widget.cpp — no dead code (widget; no #if 0)
- [x] window_management/window_management.cpp — no dead code (window mgmt; no #if 0)

---

**Total:** 150 `.cpp` files (was 151; mutually_exclusive_keys.cpp deleted; was 152 before winmm_joystick_hooks.cpp deleted). One header-only unused module: `hid/hid_enumeration.hpp`. Check one at a time; document findings in this file or in the main audit doc.
