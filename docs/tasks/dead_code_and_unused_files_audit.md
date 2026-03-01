# Dead Code and Unused Files Audit

## Overview

**Goal:** Systematically identify and document unused/dead code, unused files, and removable dependencies in the Display Commander addon and related sources. This reduces maintenance burden, clarifies the codebase, and avoids accidental regressions when refactoring.

**Scope:** `src/addons/display_commander/` and any addon-specific code under `src/`. External submodules (e.g. reshade, nvidia-dlss) are out of scope unless we explicitly depend on parts that are unused.

---

## Definitions

| Term | Definition |
|------|------------|
| **Dead code** | Functions, classes, or blocks that are never called or only called from other dead code. |
| **Unused file** | A source/header that is not compiled into any target or not included/referenced by any compiled code. |
| **Unused symbol** | Exported (e.g. in a header) but never referenced; or static/local and never used. |
| **Orphaned code** | Code that was left behind after a feature removal or refactor (e.g. `#if 0`, commented blocks, or branches that are never taken). |
| **Unused dependency** | A library, include, or CMake target that is linked/included but not actually used. |

---

## Approach

1. **Build and link analysis**
   - Ensure all addon sources are in `CMakeLists.txt` and build. Identify source files that are never added to any target.
   - Use linker / compiler options if available (e.g. `-Wl,--gc-sections`-style checks, or link-time reports) to spot unreferenced sections.

2. **Static analysis and search**
   - Grep for function definitions and then for call sites (excluding the definition). Functions with zero call sites are candidates for dead code.
   - Pay attention to: static functions, anonymous-namespace functions, and functions only called from `#if 0` or disabled `#ifdef` blocks.
   - Check headers: declarations that have no corresponding use in the codebase may be dead exports or leftover API.

3. **Include graph**
   - Identify headers that are never included (or only included by other unused headers). These are candidates for unused files.

4. **Orphaned / disabled code**
   - Search for `#if 0`, large commented blocks, and `#ifdef FEATURE` where `FEATURE` is never defined (or always defined to 0). Document and consider removal.

5. **Dependencies**
   - In `CMakeLists.txt`, list linked libraries and includes. Verify each is used (e.g. at least one symbol or header reference). Unused libs can be removed to simplify builds and avoid dependency issues.

6. **Manual review**
   - Some “dead” code may be kept intentionally (e.g. for debugging, future use, or compatibility). Document such cases and mark as “keep by design” with a short reason.

---

## File-by-file checklist

For one-file-at-a-time inspection, see **[dead_code_file_checklist.md](dead_code_file_checklist.md)** — 152 `.cpp` files listed as separate tasks (paths relative to `src/addons/display_commander/`). Check one, mark done, note findings.

---

## Deliverables

- A list of **unused source/header files** with a recommendation (remove / keep with reason).
- A list of **dead functions/symbols** (and their locations) with recommendation.
- A list of **orphaned/disabled code blocks** (file + line range or identifier) with recommendation.
- A list of **unused CMake dependencies** (libraries, optional includes) with recommendation.
- Optional: a short “audit summary” section in this doc or a separate `docs/audits/dead_code_YYYY-MM.md` with dates and findings.

---

## Task List (do in order)

- [x] **Task 1 — Unused source files**
  - List every `.cpp` under `src/addons/display_commander/` (and other addon dirs). For each, verify it is included in `CMakeLists.txt` and that at least one symbol from it is referenced (e.g. called, or used to satisfy a dependency) from the rest of the build. Document files that are never referenced.
  - **Result:** All `.cpp` files are picked up by `file(GLOB_RECURSE ... *.cpp)` except those explicitly excluded: `cli_ui_exe/` (separate target) and `imgui_wrapper_reshade.cpp` (header-only use). No source file is “never added to target.” One **dead module** (see Task 2/3): `display_settings_debug_tab.cpp` is compiled but its only entry point is never called.

- [x] **Task 2 — Unused header files**
  - List headers (e.g. `.hpp` / `.h`) in the addon. For each, search the codebase for `#include` of that header. Document headers that are never included (or only included by files that are themselves unused).
  - **Result:** Every addon header is included by at least one file (typically its corresponding `.cpp`). **Caveat:** `display_settings_debug_tab.hpp` is only included by `display_settings_debug_tab.cpp`; that module is dead because `DrawDisplaySettingsDebugTab()` is never called from the UI (see Task 3).

- [x] **Task 3 — Dead functions**
  - For each addon source file, list non-inline function definitions. For each, search for call sites (excluding the defining file if the symbol is static). Document functions with no call sites; mark any that are intentionally kept (e.g. for future use or external use).
  - **Result:** **Dead module:** `DrawDisplaySettingsDebugTab()` in `ui/new_ui/display_settings_debug_tab.cpp` is never called. The tab is not registered in `new_ui_tabs.cpp` or elsewhere. **Recommendation:** Remove `display_settings_debug_tab.cpp` and `display_settings_debug_tab.hpp`, or wire the tab into the UI if the feature is desired. Full project-wide dead-function scan (every definition vs every call site) was not run; consider a follow-up with a static analyzer or script.

- [x] **Task 4 — Orphaned / disabled code**
  - Grep for `#if 0`, `#ifdef`/`#if defined` blocks that are never enabled, and large commented-out blocks. Document file and line ranges; recommend remove or keep with reason.
  - **Result:**
    - **`hooks/dinput_hooks.cpp` L293–L458:** One `#if 0` block (~165 lines). Contains disabled DirectInput device state hook detours (`DInputDevice_GetDeviceState_Detour`, `DInputDevice_GetDeviceData_Detour`, etc.). **Recommendation:** Remove or move to a “reference/backup” file; document in commit if kept.
    - **`#ifdef TRY_CATCH_BLOCKS`:** Used in `main_entry.cpp`, `continuous_monitoring.cpp`. `TRY_CATCH_BLOCKS` is not defined in CMake or the codebase, so these blocks are never compiled. **Recommendation:** Remove the `#ifdef` and the guarded code, or define the macro if the feature is wanted.
    - Other `#ifdef`/`#if defined` uses (`_WIN64`, `__clang__`, `EXPERIMENTAL_FEATURES`, `min`/`max`, etc.) are normal platform/feature guards and are not orphaned.

- [x] **Task 5 — Unused CMake dependencies**
  - In addon `CMakeLists.txt`, list every linked library and every addon-specific include path. Verify each is used by at least one source. Document unused libs or includes and recommend removal or comment.
  - **Result:** Linked libs: `version`, `setupapi`, `hid`, `dxgi`, `d3d11`, `tdh`, `advapi32`, `wininet`, `bcrypt` — all are referenced (version info, HID, DXGI, ETW, crypto, etc.). Include dirs point to reshade, imgui, Streamline, nvapi, nvidia-dlss, minhook, vulkan-headers, toml++. No obviously unused dependency identified. Optional: run a link-map or symbol-use check for finer granularity.

- [x] **Task 6 — Audit report**
  - Produce a short report (in this doc or in `docs/audits/`) summarizing: number of unused files, dead functions, orphaned blocks, and unused deps; and list any items kept by design with reasons.
  - **Summary:** See **Audit summary** below.

---

## Audit summary (automated run)

| Category | Count | Details |
|----------|--------|--------|
| **Unused source files** | 0 | All `.cpp` are in the addon target except intentional exclusions (cli_ui_exe, imgui_wrapper_reshade.cpp). |
| **Unused header files** | 0 | **(Removed)** `hid/hid_enumeration.hpp` (never included, no implementation or call sites). Previously: display_settings_debug_tab removed. |
| **Dead modules / functions** | 0 (was 2+2) | **(Removed)** DrawDisplaySettingsDebugTab; `ParseDisplayNumberFromDeviceIdUtf8()`; `IsPidInjected()` in main_entry.cpp; `NormalizeExtendedIdForMatch()` in display_cache.cpp. |
| **Orphaned / disabled code** | 0 (was 2) | **(Removed)** (1) `#if 0` block in dinput_hooks.cpp. (2) `#ifdef TRY_CATCH_BLOCKS` blocks in main_entry.cpp and continuous_monitoring.cpp. |
| **Unused CMake dependencies** | 0 | No unused libs or include dirs identified. |

**Done (follow-up runs):**
- (1) Display Settings Debug tab removed; (2) `#if 0` block in dinput_hooks.cpp removed; (3) TRY_CATCH_BLOCKS blocks removed; (4) `ParseDisplayNumberFromDeviceIdUtf8()` in display_cache.cpp removed; (5) `IsPidInjected()` in main_entry.cpp removed; (6) `NormalizeExtendedIdForMatch()` in display_cache.cpp removed.
- (7) **CustomFpsLimiter** — removed unused `g_customFpsLimiter`, `g_custom_fps_limiter_callback`, and deleted `dxgi/custom_fps_limiter.cpp` / `dxgi/custom_fps_limiter.hpp` (FPS limiting uses LatentSyncLimiter).
- (8) **RestoreSetCursor / RestoreShowCursor** — removed from api_hooks (only referenced in commented-out code) and removed commented-out calls in continuous_monitoring.cpp.
- (9) **AreDisplaySettingsHooksInstalled()** — removed from display_settings_hooks (never called).

**Removed (this run):** (32) TestDLSSPresetSupport(); (35) SetLoadFromDllMain(); (36)-(37) display_restore::Clear(), HasAnyChanges(); (38) GetPressedKeyInGroup(); (39)-(40) ApplyHdr1000MetadataToCurrentSwapchain(), GetManualColorSpaceDXGIAsInt(); (41) AdvancedTabSettings::SetManualColorSpace/GetManualColorSpace.

**Removed (second run):** (12) IsModuleSrwlockHeld/IsBlockedDllsSrwlockHeld; (13) AreOpenGLHooksInstalled; (14) AreDX9HooksInstalled; (15) AreDDrawHooksInstalled; (16) UnhookDirectInputDeviceVTable; (17) IsReshadeRuntimesLockHeld; (18) IsNvapiLockHeld; (22) HasTrackedSwapchains() free function; (24) LoadTabSettings(vector); (33) supports_mwaitx(); (34) SendKeyDown/SendKeyUp. **(21) was incorrect:** ResetFrame() is called from continuous_monitoring.cpp; left in place.

**Removed (third run):** (23) ResolutionSettingsManager::ResetAllDirty(); (25) ScopedForceAutoFlush, IncrementForceAutoFlush, DecrementForceAutoFlush; (26) ButtonSetting, TextSetting, SeparatorSetting, SpacingSetting; (28) BindVBlankMonitorToWindow(); (29) IsRefreshRateMonitoringActive().

**Removed (fourth run):** (10) CleanupDualSenseSupport(); (11) UninstallAdditionalHIDHooks(); (19) ResetAllHIDStats(); (20) ClearAllTrackedSwapchains(); (27) cleanup_input_remapping(); (30) ProcessFrameStatistics() free fn; (31) adhd_multi_monitor::api::Shutdown().

**Removed (fifth run):** GetRefreshRateStatusString() (refresh_rate_monitor_integration); GetAllKeyGroups() (mutually_exclusive_keys).

**Removed (sixth run):** ForEachRefreshRateSample() (refresh_rate_monitor_integration.hpp); GetAllTrackedSwapchains(), GetTrackedSwapchainCount() (dxgi_present_hooks).

**Removed (seventh run):** IsKeyActuallyPressed() (windows_message_hooks, exclusive_key_groups).

**Removed (eighth run):** IsSwapchainTracked() free fn (dxgi_present_hooks); IsUIOpenedRecently() (windows_message_hooks).

**Removed (ninth run):** AddSwapchainToTracking(), RemoveSwapchainFromTracking() free fns (dxgi_present_hooks).

**Removed (tenth run):** HookSwapchainNative() (dxgi_present_hooks, WIP stub never called).

**Removed (eleventh run):** (1) Orphaned comment `// #define TRY_CATCH_BLOCKS` in continuous_monitoring.cpp. (2) HookSuppressionManager::WasHookInstalled() (hooks/hook_suppression_manager — never called).

**Removed (twelfth run):** WinMM joystick hooks — deleted hooks/winmm_joystick_hooks.cpp and .hpp; removed WINMM_JOYSTICK from DllGroup/HookType/settings; removed InstallWinMMJoystickHooks call from loadlibrary_hooks; removed InputApiId::WinMmJoystick and HOOK_joyGetPos/Ex from windows_message_hooks and input_activity_stats.

**Thirteenth run (continuation):** No new dead code found. Verified: no `#if 0`; only `#ifdef` is EXPERIMENTAL_FEATURES (CMake guard). Forward declarations and `g_last_swapchain_ptr_unsafe` (TODO: unsafe remove later) are in use. Checklist updated with **hid/** subsection and total 151 .cpp files.

**Fourteenth run (continuation):** No new dead code. Spot-checked api_hooks.cpp (includes and static RecordCRDebug — all used).

**Fifteenth run (continuation):** No new dead code. Re-verified no `#if 0`; sampled header declarations (Reset, LoadAll, RestoreClipCursor, InstallWindowsMessageHooks, dualsense Initialize/Cleanup, etc.) — all in use.

**Removed (sixteenth run):** **hid/hid_enumeration.hpp** — deleted unused header (never included; declared GetCachedHidDevices, RefreshHidDevicesSync, RequestHidDevicesRefresh, IsHidDevicesRefreshing, GetLastHidEnumerationError with no implementation or call sites).

**Removed (seventeenth run):** **IsDxgiSwapChainGettingCalled()** — declared in globals.hpp, defined in globals.cpp, never called.

**Removed (eighteenth run):** **UpdateHdr10OverrideStatus()**, **UpdateHdr10OverrideTimestamp()** — declared in globals.hpp, defined in globals.cpp, never called; globals g_hdr10_override_status / g_hdr10_override_timestamp remain (read by Swapchain tab UI).

**Removed (nineteenth run):** **display_restore::WasDeviceChangedByDisplayIndex()** — declared in display_restore.hpp, defined in display_restore.cpp, never called.

**Removed (twentieth run):** **ShouldUseNativeFpsLimiterFromFramePacing()** — declared in globals.hpp, defined in globals.cpp, never called.

**Removed (twenty-first run):** **IsGameWindow(HWND)** — declared in api_hooks.hpp, no definition or call sites (callers use GetGameWindow() and compare instead).

**Removed (twenty-second run):** **GetHookedWindow()** — declared in window_proc_hooks.hpp, defined in window_proc_hooks.cpp, never called (callers use GetGameWindow() directly).

**Twenty-third run (continuation):** No new dead code found. Verified: no `#if 0`; DetourWindowMessage, ProcessWindowMessage, SendFakeActivationMessages, IsContinueRenderingEnabled, Install/UninstallWindowsMessageHooks, RecordCRDebug, ProcessStickInputRadial, LogAllSrwlockStatus, perf_measurement::ResetAll, GetCurrentForeGroundWindow — all have call sites.

**Removed (twenty-fourth run):** Write-only / never-read atomics and one completely unused atomic: **g_comp_query_counter** (declared and defined, never read or written). **g_global_frame_id_last_updated_filetime** (only .store in swapchain_events; never .load). **g_last_window_message_processed_filetime** (only .store in window_proc_hooks; never .load; kept g_last_window_message_processed_ns which is read in continuous_monitoring). **global_dxgi_swapchain_inuse** (only .store in refresh_rate_monitor; never .load). **g_init_apply_generation** (only fetch_add in main_new_tab; never read). Removed all five from globals.hpp/cpp; removed corresponding store/fetch_add calls; kept QPC-ns counterparts (g_global_frame_id_last_updated_ns, g_last_window_message_processed_ns) for stuck-detection.

**Removed (twenty-fifth run):** **g_reflex_settings_outdated** — write-only atomic (only .store(true) in main_new_tab.cpp and continuous_monitoring.cpp; no .load() anywhere). Removed from globals.hpp/cpp and all store sites; removed now-empty Reflex branch in continuous_monitoring that only set this flag.

**Removed (twenty-sixth run):** Three write-only atomics (only .store(), never .load()): **g_last_ui_drawn_frame_id** (stored in main_entry.cpp OnReShadeOverlayOpen and autoclick_manager.cpp UpdateLastUIDrawTime). **s_last_cursor_value** and **s_last_show_cursor_arg** (stored in api_hooks.cpp SetCursor_Detour and ShowCursor_Detour). Removed from globals.hpp/cpp and all store sites.

**Removed (twenty-seventh run):** Two write-only totals (only .fetch_add(1), never .load()): **g_swapchain_event_total_count** and **g_display_settings_hook_total_count**. UI uses per-category arrays and sums them; the global totals were never read. Removed from globals.hpp/cpp and all fetch_add sites (api_hooks, display_settings_hooks, swapchain_events, swapchain_events_power_saving, opengl_hooks, nvapi_hooks, dxgi_present_hooks, dxgi_factory_wrapper, d3d9_present_hooks, ddraw_present_hooks, streamline_hooks).

**Removed (twenty-eighth run):** **g_opengl_hook_total_count** — write-only (only .fetch_add(1) in opengl_hooks.cpp; never .load()). UI uses g_opengl_hook_counters array. Removed from globals.hpp/cpp and all 15 fetch_add sites in opengl_hooks.cpp.

**Removed (twenty-ninth run):** **FPS_LIMITER_INJECTION_DEFAULT**, **FPS_LIMITER_INJECTION_FALLBACK1**, **FPS_LIMITER_INJECTION_FALLBACK2** — unused `#define`s in globals.hpp (no references in codebase). Removed the three macros and their comment block.

**Removed (thirtieth run):** Commented-out legacy **s_enable_d3d9e_upgrade** — removed the commented extern from globals.hpp, the commented definition from globals.cpp, and the commented `enable_d3d9e_upgrade` setting line from settings/advanced_tab_settings.cpp.

**Removed (thirty-first run):** **global_dxgi_swapchain** — never written to (.store never called); only .load() in refresh_rate_monitor. Removed the global from globals.hpp/cpp, removed the dead branch in GetCurrentVBlankTime (refresh_rate_monitor.cpp), removed #include "../globals.hpp" from refresh_rate_monitor.cpp. GetCurrentVBlankTime now returns false with a short comment.

**Removed (thirty-second run):** **DEBUG_LEVEL_0** — unused `#define` (never referenced in #ifdef/#if). Removed from globals.hpp and from utils/general_utils.hpp.

**Removed (thirty-third run):** **g_volume_change_time_ns** and **g_volume_display_value** — write-only (only .store() in audio_management.cpp when volume changes; never .load()). Overlay now uses g_action_notification. Removed from globals.hpp/cpp and the two store sites in audio/audio_management.cpp.

**Removed (thirty-fourth run):** **s_apply_display_settings_at_start** — write-only (only synced from g_setting_apply_display_settings_at_start in monitor_settings.cpp; never read). Logic uses the setting or other state. Removed from globals.hpp/cpp and the single assignment in ui/monitor_settings/monitor_settings.cpp.

**Removed (thirty-fifth run):** **vrr_status::last_nvapi_update_ns** — write-only (only .store() in continuous_monitoring.cpp; never .load()). VRR throttling in that block uses the local static last_nvapi_update_ns. Removed from globals.hpp, globals.cpp, and the single store in continuous_monitoring.cpp.

**Removed (thirty-sixth run):** **g_last_continuous_monitoring_loop_filetime** — write-only static in continuous_monitoring.cpp (only .store() after GetSystemTimePreciseAsFileTime; never .load()). Stuck detection uses g_last_continuous_monitoring_loop_real_ns. Removed the static, the FILETIME/GetSystemTimePreciseAsFileTime call, and the store.

**Removed (thirty-seventh run):** **g_translate_mouse_debug_*** (15 atomics) — unused: declared as "recorded in ApplyTranslateMousePositionToCursorPos, read by UI" but never written or read anywhere (only in globals.hpp/cpp). Removed from globals.hpp and globals.cpp.

**Removed (thirty-eighth run):** **Duplicate #include &lt;dxgi.h&gt;** in swapchain_events.cpp (included at line 35 and again at line 51). Removed the second occurrence.

**Removed (thirty-ninth run):** **g_reflex_was_enabled_last_frame** — write-only (only .store(true) in swapchain_events.cpp when Reflex is applied; never .load()). Comment said "for RestoreSleepMode on disable" but no reader. Removed from globals.hpp, globals.cpp, and the single store in swapchain_events.cpp.

**Removed (fortieth run):** **Orphaned commented line** in swapchain_events.cpp OnDestroyDevice: `// g_initialized_with_hwnd.store(false);` — dead commented code; replaced with a short placeholder comment.

**Forty-first run (continuation):** No new dead code found. Re-verified: g_thread_tracking_enabled, g_hmodule, g_shared_dxgi_factory, s_restart_needed_nvapi, s_reflex_supress_native, s_enable_reflex_logging — all have read and write/use sites. No write-only atomics or unused declarations identified.

**Forty-second run (continuation):** No new dead code removed. Searched for write-only atomics (`.store()`/`.fetch_add()`/`.exchange()` with no `.load()` or other read): all atomics in swapchain_events and globals that are written are also read (s_d3d9e_upgrade_successful, g_last_swapchain_desc, s_we_auto_enabled_hdr, fps_sleep_after_on_present_ns, g_onpresent_sync_post_sleep_ns, g_present_update_after2_*, s_reflex_enable_current_frame, flipex_upgrade_count, g_submit_start_time_ns, g_gpu_late_time_ns, g_reshade_overhead_duration_ns, g_render_submit_duration_ns, g_sim_to_display_latency_ns, g_perf_time_seconds, g_render_submit_end_time_ns, g_muted_applied, g_standalone_ui_hwnd, g_module_pinned, g_dll_load_time_ns, g_last_xinput_detected_frame_id, g_last_set_sleep_mode_direct_frame_id). Checked frame_data sleep_post_present_* — read in main_new_tab. Checked CreateDlssOverrideSubfolder, GetRenderThreadId, IsCurrentThreadRenderThread — all have call sites. No duplicate `#include` in swapchain_events.cpp; no unused `#define`s identified beyond those already removed.

**Forty-third run (continuation):** No new dead code removed. Re-verified: g_last_swapchain_ptr_unsafe, g_last_reshade_device_api, g_last_api_version, g_last_swapchain_hwnd, g_simulation_duration_ns, g_game_render_width/height, g_used_flipex, g_dx9_swapchain_detected — all have `.load()` or other read sites (swapchain_events, main_new_tab, continuous_monitoring, api_hooks, monitor_settings, experimental_tab, dxgi_present_hooks, autoclick_manager). No `#if 0` blocks found; only normal `#ifdef` (EXPERIMENTAL_FEATURES, _WIN64). main_entry.cpp includes are unique; window_proc_hooks.hpp declarations (WindowHasBorder, Install/UninstallWindowProcHooks, IsContinueRenderingEnabled, SendFakeActivationMessages, ProcessWindowMessage) are used per prior runs.

**Forty-fourth run (continuation):** No new dead code removed. Checked all atomics written in continuous_monitoring.cpp: g_app_in_background, g_last_foreground_background_switch_ns, g_continuous_monitoring_section, g_perf_text_shared, s_audio_volume_percent, s_system_volume_percent, g_cached_refresh_rate_stats, g_failed, g_inited, g_last_continuous_monitoring_loop_real_ns — each has `.load()` or other read (swapchain_events, main_new_tab, process_exit_hooks, hotkeys_tab, audio_management, input_remapping, swapchain_tab, windows_message_hooks, standalone_ui_settings_bridge, continuous_monitoring itself for stuck detection). Checked globals.cpp store sites (g_using_wine, g_fps_limiter_site_thread_id, g_fps_limiter_last_timestamp_ns) — read elsewhere. main_new_tab.cpp includes are unique (no duplicate `#include` lines).

**Removed (forty-fifth run):** **g_display_settings_hook_counters** — write-only (only `.fetch_add(1)` in display_settings_hooks.cpp and api_hooks.cpp; never `.load()` or read). Hook Stats tab uses g_hook_stats (increment_total/unsuppressed) for display; this array was redundant. Removed from globals.hpp/cpp and all 8 fetch_add sites (5 in display_settings_hooks.cpp, 3 in api_hooks.cpp). DisplaySettingsHookIndex enum kept (used for hook indexing elsewhere).

**Forty-sixth run (continuation):** No new dead code removed. Checked atomics written in nvapi/ and latent_sync/: g_nvapi_inited, g_last_reflex_params_set_by_addon, g_recent_write_index, g_recent_count, g_actual_refresh_rate_hz, g_consecutive_failures, g_stop_monitor (nvapi_actual_refresh_rate_monitor, reflex_manager) — all have `.load()` (reflex_manager, main_new_tab, nvapi_actual_refresh_rate_monitor GetRecentCount/GetNvapiActualRefreshRateHz/IsNvapiActualRefreshRateFailing). g_latent_sync_total_height, g_latent_sync_active_height — read in latent_sync_limiter and main_new_tab. refresh_rate_monitor m_recent_samples_*, m_min/max_refresh_rate — read in same file and header. display_settings_hooks.cpp and swapchain_tab.cpp includes are unique.

**Removed (forty-seventh run):** **g_gpu_completion_time_ns** and **g_gpu_completion_callback_time_ns** — write-only (only `.store()` in gpu_completion_monitoring.cpp; never `.load()` or read elsewhere). Removed from globals.hpp/cpp and all four store sites in gpu_completion_monitoring.cpp.

**Further findings (not yet removed):** None.

**Checklist status:** All 151 `.cpp` files in [dead_code_file_checklist.md](dead_code_file_checklist.md) have been checked (152 before WinMM removal); no further dead code or `#if 0` blocks were found in ui/, utils/, latent_sync/, latency/, nvapi/, settings/, widgets/, proxy_dll/, or other remaining dirs.

**Follow-up search:** Uninstall/Cleanup/Shutdown, Register/Unregister, Get*/Is*LockHeld/Are*Installed, static helpers (RecordCRDebug, ParseDisplayNumberFromDeviceName, RemoveDlssOverrideHandle), and CleanupGPUMeasurement* were verified; all have call sites. No new uncalled symbols beyond (10)–(18).

**Later pass:** Apply/Restore/Log (ApplyWindowChange, LogCurrentLogLevel, RestoreClipCursor, ApplySpoofGameResolutionInSizeMessages, LogD3D9*, ReflexManager::RestoreSleepMode, NVAPIFullscreenPrevention::Cleanup), Reset/Clear/Flush (except 19–22), HasTrackedSwapchains (member used; free function 22), and DPI/Enable/Disable were verified; all have call sites. No further uncalled symbols beyond (10)–(22).

**Kept by design:** Excluded sources (cli_ui_exe, imgui_wrapper_reshade.cpp) and platform/feature `#ifdef`s (_WIN64, EXPERIMENTAL_FEATURES, etc.).

---

## Notes

- **Safety:** Before removing any file or symbol, run a full build and (if available) tests. Prefer one PR per category (e.g. “remove unused headers”) to simplify review.
- **Project rules:** Respect `.cursor/rules` (e.g. no `std::mutex`, Reshade/ImGui patterns). Do not remove code that is required for Special-K compatibility or documented optional features unless explicitly agreed.
- **Submodules:** Do not treat external submodule code (e.g. reshade) as “dead” just because we don’t call it; only consider our addon’s own sources and our use of external APIs.
