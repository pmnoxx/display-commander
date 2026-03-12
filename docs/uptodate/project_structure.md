# Display Commander project structure

Full file list and conventions. The Cursor rule that references this lives in `.cursor/rules/project_structure.mdc` (folders-only); keep this doc updated when adding or moving files.

---

## Reorganization plan (what to move)

Goal: keep addon **root** for cross-cutting pieces only; move feature-specific modules into **existing** subdirs. Do not create new folders for a single file—only move into folders that already exist and fit.

### Folder-by-folder move plan (execute in order)

| Folder | Files to move here (from root) | Why it fits |
|--------|--------------------------------|-------------|
| **latency/** | ~~`gpu_completion_monitoring.cpp`, `gpu_completion_monitoring.hpp`~~ ✅ done | GPU completion time is a latency/performance metric; `reflex_provider` and `hooks/dxgi/dxgi_gpu_completion.hpp` already live in this domain. |
| **display/** | ~~`display_initial_state`, `display_cache`, `display_restore` (6 files)~~ ✅ done | Display state/cache/restore next to existing `query_display`, `hdr_control`, `dpi_management`. |
| **utils/** | ~~`rundll_injection`, `rundll_injection_helpers.hpp`, `dbghelp_loader` (4 files)~~ ✅ done | Loaders/injection helpers; `utils/` already has many helpers (stack_trace, logging, etc.). |

**Not moving (for now):** Root `process_exit_hooks` and `exit_handler`. `hooks/process_exit_hooks.cpp` is the MinHook detour for ExitProcess/TerminateProcess; the root `process_exit_hooks` is the exception-handler/orchestration logic that uses `exit_handler` and `dbghelp_loader`. Moving would require renaming to avoid a name clash and many include updates; defer unless we consolidate later.

**Order:** Do **latency/** first (2 files), then **display/** (6 files), then **utils/** (4 files). After each folder’s move, update all `#include` paths and fix any same-folder includes in the moved files.

---

## addons/display_commander (root)

- **main_entry.cpp**: DLL entry point and ReShade event registration
- **addon.cpp**, **addon.hpp**: Addon metadata exports
- **globals.cpp**, **globals.hpp**: Global variables and atomic state management
- **utils.hpp**: Shared utility declarations (implementation in utils/)
- **continuous_monitoring.cpp**: Background monitoring thread for app state
- **swapchain_events.cpp**, **swapchain_events.hpp**: ReShade swapchain event handlers and FPS limiting
- **swapchain_events_power_saving.cpp**, **swapchain_events_power_saving.hpp**: Power saving optimizations
- **resolution_helpers.cpp**, **resolution_helpers.hpp**: Resolution calculation utilities
- _(display_restore, display_cache, display_initial_state moved to display/)_
- _(gpu_completion_monitoring moved to latency/)_
- **process_exit_hooks.cpp**, **process_exit_hooks.hpp**: Process exit safety hooks (root copy; see hooks/ for duplicate)
- **exit_handler.cpp**, **exit_handler.hpp**: Exit/cleanup handler (candidate → hooks/)
- _(rundll_injection, dbghelp_loader moved to utils/)_
- **version.hpp**, **performance_types.hpp**: Version and shared types

## addons/display_commander/audio

- **audio_management.cpp**, **audio_management.hpp**: Audio session control and volume management
- **audio_channel_volume.cpp**: Per-channel volume
- **audio_device_policy.hpp**: Device policy (header-only)

## addons/display_commander/autoclick

- **autoclick_manager.cpp**, **autoclick_manager.hpp**: Autoclick feature manager
- **autoclick_action.cpp**, **autoclick_action.hpp**: Autoclick actions

## addons/display_commander/config

- **display_commander_config.cpp**, **display_commander_config.hpp**: Main addon config
- **chords_file.cpp**, **chords_file.hpp**: Chord hotkeys file
- **hotkeys_file.cpp**, **hotkeys_file.hpp**: Hotkeys file

## addons/display_commander/display

- **display_cache.cpp**, **display_cache.hpp**: Display enumeration and resolution management
- **display_initial_state.cpp**, **display_initial_state.hpp**: Initial display state capture
- **display_restore.cpp**, **display_restore.hpp**: Display settings restoration on exit
- **query_display.cpp**, **query_display.hpp**: Display query utilities
- **hdr_control.cpp**, **hdr_control.hpp**: HDR control
- **dpi_management.cpp**, **dpi_management.hpp**: DPI management

## addons/display_commander/dlss

- **dlss_indicator_manager.cpp**, **dlss_indicator_manager.hpp**: DLSS indicator UI/state

## addons/display_commander/dualsense

- **dualsense_hid_wrapper.cpp**, **dualsense_hid_wrapper.hpp**: DualSense HID wrapper

## addons/display_commander/dxgi

- **vram_info.cpp**, **vram_info.hpp**: VRAM info (DXGI-related)

## addons/display_commander/hooks

- **api_hooks.cpp**, **api_hooks.hpp**: Windows API hooks (focus/foreground spoofing)
- **window_proc_hooks.cpp**, **window_proc_hooks.hpp**: Window procedure hooks
- **loadlibrary_hooks.cpp**, **loadlibrary_hooks.hpp**: DLL loading hooks
- **xinput_hooks.cpp**, **xinput_hooks.hpp**: XInput controller hooks and remapping
- **windows_gaming_input_hooks.cpp**, **windows_gaming_input_hooks.hpp**: Windows.Gaming.Input hooks
- **display_settings_hooks.cpp**, **display_settings_hooks.hpp**: Display settings hooks
- **dxgi_factory_wrapper.cpp**, **dxgi_factory_wrapper.hpp**: DXGI factory wrapper
- **rand_hooks.cpp**, **rand_hooks.hpp**: Rand hooks
- **sleep_hooks.cpp**, **sleep_hooks.hpp**: Sleep hooks
- **timeslowdown_hooks.cpp**, **timeslowdown_hooks.hpp**: Time slowdown hooks
- **dinput_hooks.cpp**, **dinput_hooks.hpp**: DirectInput hooks
- **dualsense_hooks.cpp**, **dualsense_hooks.hpp**: DualSense hooks
- **game_input_hooks.cpp**, **game_input_hooks.hpp**: Game input hooks
- **process_exit_hooks.cpp**, **process_exit_hooks.hpp**: Process exit hooks (hooks copy)
- **debug_output_hooks.cpp**, **debug_output_hooks.hpp**: Debug output hooks
- **dbghelp_hooks.cpp**, **dbghelp_hooks.hpp**: DbgHelp hooks
- **dpi_hooks.cpp**, **dpi_hooks.hpp**: DPI hooks
- **hook_suppression_manager.cpp**, **hook_suppression_manager.hpp**: Hook suppression
- **input_activity_stats.cpp**, **input_activity_stats.hpp**: Input activity stats
- **present_traffic_tracking.cpp**, **present_traffic_tracking.hpp**: Present traffic tracking
- **hid_hooks_install.cpp**, **hid_hooks_install.hpp**: HID hooks installation
- **hid_additional_hooks.cpp**, **hid_additional_hooks.hpp**: HID additional hooks
- **hid_suppression_hooks.cpp**, **hid_suppression_hooks.hpp**: HID suppression
- **hid_statistics.cpp**, **hid_statistics.hpp**: HID statistics
- **hook_call_stats.hpp**: Hook call stats (header-only)
- **windows_hooks/windows_message_hooks.cpp**, **.hpp**: Windows message hooks
- **d3d9/d3d9_hooks.cpp**, **.hpp**, **d3d9_present_hooks**, **d3d9_present_params_upgrade**, **d3d9_pool_upgrade**, **d3d9_device_vtable_logging**, **d3d9_no_reshade_device_state.hpp**, **d3d9_vtable_indices.hpp**
- **dxgi/dxgi_present_hooks.cpp**, **.hpp**, **dxgi_gpu_completion.hpp**
- **ddraw/ddraw_present_hooks.cpp**, **.hpp**
- **opengl/opengl_hooks.cpp**, **.hpp**: OpenGL (WGL) hooks
- **nvidia/ngx_hooks.cpp**, **.hpp**: NGX (DLSS) hooks
- **nvidia/nvapi_hooks.cpp**, **.hpp**: NVAPI hooks
- **nvidia/streamline_hooks.cpp**, **.hpp**: Streamline hooks
- **nvidia/pclstats_etw_hooks.cpp**, **.hpp**: PCLStats ETW reporting
- **vulkan/nvlowlatencyvk_hooks.cpp**, **.hpp**, **vulkan_loader_hooks.cpp**, **.hpp**

## addons/display_commander/input_remapping

- **input_remapping.cpp**, **input_remapping.hpp**: Input remapping system

## addons/display_commander/latency

- **gpu_completion_monitoring.cpp**, **gpu_completion_monitoring.hpp**: GPU completion time tracking (thread + OpenGL callback)
- **reflex_provider.cpp**, **reflex_provider.hpp**: Reflex provider implementation

## addons/display_commander/latent_sync

- **vblank_monitor.cpp**, **vblank_monitor.hpp**: VBlank timing monitor
- **refresh_rate_monitor.cpp**, **refresh_rate_monitor.hpp**: Refresh rate monitor
- **refresh_rate_monitor_integration.cpp**, **.hpp**: Refresh rate integration
- **latent_sync_manager.cpp**, **latent_sync_manager.hpp**: Latent sync manager
- **latent_sync_limiter.cpp**, **latent_sync_limiter.hpp**: Latent sync FPS limiter

## addons/display_commander/nvapi

- **reflex_manager.cpp**, **reflex_manager.hpp**: NVIDIA Reflex integration
- **nvidia_profile_search.cpp**, **.hpp**: NVIDIA profile search
- **nvapi_actual_refresh_rate_monitor.cpp**, **.hpp**: Actual refresh rate via NVAPI
- **vrr_status.cpp**, **vrr_status.hpp**: VRR status (NVAPI)
- **nvpi_reference.cpp**, **nvpi_reference.hpp**: NvPI reference
- **dlss_preset_manager.hpp**: DLSS preset manager (header-only)
- **run_nvapi_setdword_as_admin.hpp**: Admin helper (header-only)

## addons/display_commander/hdr_upgrade

- **hdr_upgrade.cpp**, **hdr_upgrade.hpp**: HDR upgrade handling

## addons/display_commander/presentmon

- **presentmon_manager.cpp**, **presentmon_manager.hpp**: PresentMon integration

## addons/display_commander/proxy_dll

- **dxgi_proxy.cpp**, **dxgi_proxy_init.hpp**, **d3d11_proxy.cpp**, **d3d12_proxy.cpp**, **d3d9_proxy.cpp**, **d3d9_proxy_init.hpp**, **ddraw_proxy.cpp**, **ddraw_proxy_init.hpp**, **opengl32_proxy.cpp**, **opengl32_proxy_init.hpp**, **winmm_proxy.cpp**, **winmm_proxy_init.hpp**, **version_proxy.cpp**, **reshade_loader.cpp**, **.hpp**, **proxy_detection.hpp**

## addons/display_commander/settings

- **main_tab_settings.cpp**, **.hpp**
- **advanced_tab_settings.cpp**, **.hpp**
- **experimental_tab_settings.cpp**, **.hpp**
- **hotkeys_tab_settings.cpp**, **.hpp**
- **hook_suppression_settings.cpp**, **.hpp**
- **display_settings.cpp**, **.hpp**
- **reshade_tab_settings.cpp**, **.hpp**
- **streamline_tab_settings.cpp**, **.hpp**
- **swapchain_tab_settings.cpp**, **.hpp**

## addons/display_commander/ui

- **ui_display_tab.cpp**, **ui_display_tab.hpp**: Legacy display tab
- **cli_standalone_ui.cpp**, **.hpp**, **cli_detect_exe.hpp**: CLI/standalone UI
- **imgui_wrapper_base.hpp**, **imgui_wrapper_reshade.hpp**, **imgui_wrapper_standalone.cpp**, **.hpp**: ImGui wrappers
- **nvidia_profile_tab_shared.cpp**, **.hpp**, **standalone_ui_settings_bridge.cpp**, **.hpp**
- **new_ui/new_ui_main.cpp**, **.hpp**, **new_ui_wrapper.cpp**, **.hpp**, **new_ui_tabs.cpp**, **.hpp**: New UI system
- **new_ui/main_new_tab.cpp**, **.hpp**, **main_new_tab_standalone.hpp**
- **new_ui/advanced_tab.cpp**, **.hpp**
- **new_ui/experimental_tab.cpp**, **.hpp**
- **new_ui/hotkeys_tab.cpp**, **.hpp**
- **new_ui/swapchain_tab.cpp**, **.hpp**
- **new_ui/hook_stats_tab.cpp**, **.hpp**
- **new_ui/window_info_tab.cpp**, **.hpp**
- **new_ui/streamline_tab.cpp**, **.hpp**
- **new_ui/vulkan_tab.cpp**, **.hpp**
- **new_ui/addons_tab.cpp**, **.hpp**
- **new_ui/updates_tab.cpp**, **.hpp**
- **new_ui/performance_tab.cpp**, **.hpp**
- **new_ui/settings_wrapper.cpp**, **.hpp**
- **monitor_settings/monitor_settings.cpp**, **.hpp**: Monitor configuration UI

## addons/display_commander/widgets

- **remapping_widget/remapping_widget.cpp**, **.hpp**: Input remapping widget
- **xinput_widget/xinput_widget.cpp**, **.hpp**: XInput controller widget
- **resolution_widget/resolution_widget.cpp**, **.hpp**, **resolution_settings.cpp**, **.hpp**: Resolution widget and settings
- **dualsense_widget/dualsense_widget.cpp**, **.hpp**: DualSense widget

## addons/display_commander/window_management

- **window_management.cpp**, **window_management.hpp**: Window positioning and sizing

## addons/display_commander/utils

- **rundll_injection.cpp**, **rundll_injection_helpers.hpp**: RunDLL injection entry points and helpers
- **dbghelp_loader.cpp**, **dbghelp_loader.hpp**: DbgHelp dynamic loader (stack traces, symbols)
- **general_utils.cpp**, **.hpp**
- **timing.cpp**, **timing.hpp**: High-precision timing
- **display_commander_logger.cpp**, **.hpp**
- **reshade_global_config.cpp**, **.hpp**
- **srwlock_registry.cpp**, **.hpp**, **srwlock_wrapper.hpp**
- **logging.cpp**, **logging.hpp**
- **perf_measurement.cpp**, **perf_measurement.hpp**
- **detour_call_tracker.cpp**, **.hpp**
- **texture_tracker.cpp**, **.hpp**: Thread-safe tracking of loaded D3D11 texture size and count; used by optional Advanced tab texture tracking feature and IUnknown::Release hooks
- **file_sha256.cpp**, **.hpp**
- **mpo_registry.cpp**, **.hpp**
- **no_inject_windows.cpp**, **.hpp**: Windows to skip for addon overlay/UI injection (e.g. standalone independent UI); used in OnPerformanceOverlay, OnReShadeOverlayOpen, OnInitEffectRuntime
- **overlay_window_detector.cpp**, **.hpp**
- **platform_api_detector.cpp**, **.hpp**
- **process_window_enumerator.cpp**, **.hpp**
- **reshade_sha256_database.cpp**, **.hpp**
- **reshade_load_path.cpp**, **.hpp**: ReShade load source (Local / Shared path / Specific version), path resolution from DC config, used at ProcessAttach and ReShade tab
- **reshade_version_download.cpp**, **.hpp**: Download ReShade Addon by version (tar.exe extract), status for ReShade tab
- **stack_trace.cpp**, **.hpp**
- **steam_library.cpp**, **.hpp**: Enumerate Steam libraries and installed games; LaunchSteamGame via steam://run
- **steam_launch_history.cpp**, **.hpp**: Persist Steam game launch timestamps (HKCU) for "most recent" ordering in Games tab
- **version_check.cpp**, **.hpp**
- **ring_buffer.hpp**: Ring buffer (header-only)

## addons/display_commander/res

- **ui_colors.hpp**: Centralized UI color definitions

## addons/display_commander/adhd_multi_monitor

- **adhd_multi_monitor.cpp**, **.hpp**, **adhd_simple_api.cpp**, **.hpp**: ADHD multi-monitor feature

---

## Conventions

### includes

- Use ImGui from ReShade: `<imgui.h>`, `<reshade.hpp>`
- Use MinHook for hooks

### not allowed

- std locks are not allowed (use SRWLOCK / project patterns)

### CMake

- Sources are collected with `file(GLOB_RECURSE ... ${CMAKE_CURRENT_LIST_DIR}/*.cpp` and `.../utils/*.cpp`; moving files into existing subdirs does not require CMake changes.
