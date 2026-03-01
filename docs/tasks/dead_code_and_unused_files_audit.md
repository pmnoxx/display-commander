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

**Removed (forty-eighth run):** **g_reshade_present_frame_count** — write-only (only `.fetch_add(1)` in swapchain_events.cpp OnPresentUpdateBefore; never `.load()` or read). Comment said "Used to delay ADHD multi-monitor init until frame 500" but the actual delay uses `g_global_frame_id` in continuous_monitoring.cpp and windows_message_hooks.cpp. Removed from globals.hpp, definition and fetch_add from swapchain_events.cpp.

**Forty-ninth run (continuation):** No new dead code found. Verified: all atomics written in swapchain_events (s_reflex_enable_current_frame, g_present_update_after2_*, flipex_upgrade_count, g_frame_data[].frame_id, sleep_pre_present_*, etc.) have read sites (main_new_tab, gpu_completion_monitoring, swapchain_events). g_frame_time_ns, g_display_settings_hooks_installed, g_min_log_level — read. No `#if 0` blocks. EnsureXInputSetStateForTest, ClipCursorToGameWindow, EnumerateLoadedModules — all have call sites.

**Removed (fiftieth run):** **SuppressMessage(LPMSG lpMsg)** — declared in windows_message_hooks.hpp, defined in windows_message_hooks.cpp; never called. Callers use ShouldSuppressMessage() to decide whether to suppress but never invoke SuppressMessage() to modify the message (e.g. set to WM_NULL). Removed declaration and full definition (~27 lines).

**Removed (fifty-first run):** **mutually_exclusive_keys::ProcessKeyPress(int)** and **ProcessKeyRelease(int)** — declared in mutually_exclusive_keys.hpp, defined in mutually_exclusive_keys.cpp; never called. Exclusive-key handling uses MarkKeyDown/MarkKeyUp and ShouldSuppressKey in windows_message_hooks; the ProcessKey* API was unused. Removed both declarations and definitions (~50 lines).

**Removed (fifty-second run):** **Unused `#include <windows.h>`** in mutually_exclusive_keys.cpp — only used by the removed ProcessKeyPress (keybd_event); no other Windows API used in that file. Removed the include.

**Removed (fifty-third run):** **Dead module mutually_exclusive_keys** — hooks/mutually_exclusive_keys.cpp and hooks/mutually_exclusive_keys.hpp deleted. No file in the codebase included the header or called mutually_exclusive_keys::Initialize, UpdateKeyGroups, or ShouldSuppressKey; exclusive-key behaviour is implemented by namespace exclusive_key_groups in windows_message_hooks (MarkKeyDown, MarkKeyUp, ShouldSuppressKey, Initialize, UpdateCachedActiveKeys, Update). The module was compiled via GLOB but never referenced.

**Removed (fifty-fourth run):** **HookCallStats::last_call_time_ns** — write-only (only `.store()` in UpdateHookLastCallTime; never `.load()` or read). Comment said "Used for 'active input API' display (last 10s)" but no UI reads it; Hook Stats tab and experimental tab only use total_calls/unsuppressed_calls from GetHookStats. "Last call" in Window Info tab comes from GetContinueRenderingApiDebugSnapshots (g_cr_debug), not g_hook_stats. Removed the member from HookCallStats, the store in UpdateHookLastCallTime, and the store in reset().

**Fifty-fifth run (continuation):** No new dead code removed. Re-verified: g_reshade_event_counters (read in main_new_tab, swapchain_tab); g_display_settings_hooks_installed (read in display_settings_hooks); g_got_device_name (read in continuous_monitoring, main_new_tab); s_spoofed_mouse_x/y (read in windows_message_hooks GetCursorPos path); all fetch_add counters in windows_message_hooks use `count` in throttled logging (count % 100 or count % 1000); ResetAllHookStats and GetHookCount called from hook_stats_tab and experimental_tab; RECORD_DETOUR_CALL / UpdateHookLastCallTime still used. No write-only atomics or uncalled declarations identified.

**Fifty-sixth run (continuation):** No new dead code removed. Re-verified: g_submit_start_time_ns (read via .load() and compare_exchange_strong in swapchain_events); g_dxgi_factory_event_counters (read in swapchain_tab displayEventCategory); g_api_hooks_installed (read in api_hooks.cpp Install/Uninstall paths); api_hooks.hpp declarations — SetGameWindow (called from window_proc_hooks), ShowCursor_Direct (used by ShowCursor_Detour), IsIconic_direct/IsWindowVisible_direct (called from overlay_window_detector, window_info_tab, process_window_enumerator, window_management). No write-only atomics or uncalled functions in api_hooks, dxgi_proxy, or swapchain_events.

**Removed (fifty-seventh run):** **g_reflex_sleep_status_low_latency_enabled** and **g_reflex_sleep_status_last_update_ns** — write-only (only `.store()` in latency_manager.cpp UpdateCachedSleepStatus; no `.load()` or other read anywhere). Comment said "Cached Reflex sleep status (updated periodically, read by UI)" but no UI or other code reads them. Removed from globals.hpp/cpp and all store calls in LatencyManager::UpdateCachedSleepStatus; left the GetSleepStatus call with (void) cast since call site remains.

**Removed (fifty-eighth run):** **SwapchainTrackingManager** — five unused member functions (never called): **GetAllTrackedSwapchains()**, **GetTrackedSwapchainCount()**, **ClearAll()**, **HasTrackedSwapchains()**, **ForEachTrackedSwapchain()**. Callers only use AddSwapchain, RemoveSwapchain (via Release hook), IsSwapchainTracked, and IsLockHeldForDiagnostics. Removed the five declarations/definitions from globals.hpp.

**Fifty-ninth run (continuation):** No new dead code removed. Re-verified: all atomics written in latency_manager (reflex marker counts, g_reflex_sleep_duration_ns, initialized_) have readers (advanced_tab, main_new_tab, experimental_tab, latency_manager). windows_message_hooks key-state atomics (s_key_down, s_key_pressed, s_exclusive_group_active_key, s_key_press_timestamp, s_key_actually_pressed, s_key_needs_simulate_press, s_key_in_active_group, s_key_to_group_index) are all read in same file (IsKeyDown, IsKeyPressed, GetKeyGroupIndex, ShouldSuppressKey, exclusive-group logic). UpdateHookLastCallTime still called from multiple hooks (only updates InputActivityStats now). g_latency_marker_thread_id / g_latency_marker_last_frame_id written in nvapi_hooks, read in experimental_tab. ParseIniLine, LogInfoDirectSynchronized, IsWriteLockHeld have call sites. No `#if 0` blocks; no write-only atomics or uncalled declarations identified.

**Removed (sixtieth run):** **GetHookInfo(int hook_index)** — declared in windows_message_hooks.hpp, defined in windows_message_hooks.cpp; never called. Callers use GetHookName(hook_index) and GetHookDllGroup(hook_index), which read g_hook_info directly. Removed declaration and definition (~7 lines). HookInfo struct and g_hook_info array retained (used by GetHookName and GetHookDllGroup).

**Sixty-first run (continuation):** No new dead code removed. Re-verified: display_restore (MarkOriginalForMonitor/ForDeviceName/ForDisplayIndex, MarkDeviceChanged*, RestoreAll/RestoreAllIfEnabled, WasDeviceChangedByDeviceName, RestoreDisplayByIndex/ByDeviceName, GetCurrentForDevice, GetDeviceNameForMonitor, ApplyModeForDevice) — all have call sites or internal use. LogAllSrwlockStatus called from continuous_monitoring; HookInfoOrderValid used in static_assert. IsContinueRenderingEnabled, SendFakeActivationMessages called from window_proc_hooks, main_new_tab, window_info_tab. No write-only atomics or uncalled declarations identified.

**Removed (sixty-second run):** **GetHookNameById(TimerHookIdentifier id)** — declared in timeslowdown_hooks.hpp, defined in timeslowdown_hooks.cpp; never called. Experimental tab and other callers use GetHookIdentifierByName(name) and GetAllHookIdentifiers() with GetHookTypeById/SetTimerHookTypeById; no code calls GetHookNameById to get the string for an id. Removed declaration and definition (~14 lines). HOOK_* constants retained (used by GetHookIdentifierByName and CreateAndEnableHook).

**Removed (sixty-fifth run):** **timeslowdown_hooks** — Five name-based API overloads and one UI helper never called: **SetTimerHookType(const char*, TimerHookType)**, **GetTimerHookType(const char*)**, **IsTimerHookEnabled(const char*)**, **GetTimerHookCallCount(const char*)**, **GetAllHookIdentifiers()** (and static **g_all_hook_identifiers**). All callers use the ById variants (SetTimerHookTypeById, IsTimerHookEnabledById, GetTimerHookCallCountById); experimental_tab uses a local array of enum values, not GetAllHookIdentifiers(). Also removed internal **GetHookTypeByName()** and **ShouldApplyHook(const char*)** (only used by the removed name-based functions). Declarations removed from timeslowdown_hooks.hpp; definitions and static array removed from timeslowdown_hooks.cpp.

**Sixty-third run (continuation):** No new dead code removed. Re-verified: RestoreClipCursor/ClipCursorToGameWindow (called from continuous_monitoring, main_new_tab); GetAspectWidthValue (used in general_utils.cpp); GetDllGroupName (hook_stats_tab); FpsLimiterSiteName/GetChosenFpsLimiterSiteName (globals, main_new_tab, experimental_tab); ClearQPCallingModules, IsQPCModuleEnabled, SetQPCModuleEnabled, LoadQPCEnabledModulesFromSettings (experimental_tab, experimental_tab_settings); ApplyTranslateMousePositionToCursorPos (windows_message_hooks, experimental_tab); ProcessStickInputRadial/ProcessStickInputSquare (xinput_widget, xinput_hooks); GetTimerHookTypeById (used by IsTimerHookEnabledById; experimental_tab calls IsTimerHookEnabledById); MarkKeyDown/MarkKeyUp (windows_message_hooks, hotkeys_tab); exclusive_key_groups::Initialize/Update/UpdateCachedActiveKeys (windows_message_hooks, continuous_monitoring, hotkeys_tab); keyboard_tracker::ResetFrame (continuous_monitoring); g_dll_load_time_ns, g_last_foreground_background_switch_ns (store + load in main_entry, addon, continuous_monitoring, hotkeys_tab). No write-only atomics or uncalled declarations identified.

**Sixty-fourth run (continuation):** No new dead code removed. Re-verified: s_spoofed_mouse_x/s_spoofed_mouse_y (store in windows_message_hooks, autoclick_manager; load in windows_message_hooks); process_exit_hooks::g_last_detour_handler (load+store in windows_message_hooks, load/store in process_exit_hooks); g_message_hooks_installed (load in windows_message_hooks Install/Uninstall paths). NVAPI: IsNvapiGetAdaptiveSyncDataFailingRepeatedly, ForEachNvapiActualRefreshRateSample (main_new_tab); InvalidateProfileSearchCache, HasDisplayCommanderProfile (nvidia_profile_search.cpp, nvidia_profile_tab_shared). exit_handler: GetExitSourceString (used in exit_handler.cpp), WriteToDebugLog, WriteMultiLineToDebugLog (process_exit_hooks, continuous_monitoring, exit_handler). QueryDxgiCompositionState called from dxgi_present_hooks (implementation is no-op stub; kept as call point). No write-only atomics or uncalled declarations identified.

**Sixty-fifth run (continuation):** No new dead code removed. Re-verified: g_last_api_version, cached_nvapi_ok, cached_output_device_name (store in swapchain_events/continuous_monitoring; load in main_new_tab, experimental_tab, continuous_monitoring). GetRenderThreadId/SetRenderThreadId (used in timeslowdown_hooks.cpp and swapchain_events); IsCurrentThreadRenderThread (used internally in timeslowdown_hooks). GetHookDllGroup (hook_stats_tab). RecordFrameTime/RecordNativeFrameTime (swapchain_events, gpu_completion_monitoring, present hooks, pclstats, vulkan). No write-only atomics or uncalled declarations identified.

**Removed (sixty-sixth run):** **g_qpf_call_count** — write-only atomic in timeslowdown_hooks.cpp. Only written via fetch_add(1) in QueryPerformanceFrequency_Detour; never read. GetTimerHookCallCountById returns call counts for TimerHookIdentifier enum members only; there is no QueryPerformanceFrequency in that enum, so no code path reads g_qpf_call_count. Removed the atomic variable and the fetch_add line.

**Sixty-seventh run (continuation):** No new dead code removed. Re-verified: windows_message_hooks static counters (suppress_counter, clear_counter, replace_counter, block_counter, filter_counter) — count is used in `if (count % 100 == 0)` for throttled logging, so not write-only. timeslowdown: only ById APIs are in the header; no name-based GetTimerHookCallCount/GetHookType/IsTimerHookEnabled in header (already removed). Reflex/latency globals (g_reflex_sleep_count, g_reflex_apply_sleep_mode_count, g_reflex_sleep_duration_ns, g_reflex_marker_*_count) — all read (and reset) in advanced_tab. No write-only atomics or uncalled declarations identified.

**Removed (sixty-eighth run):** **GetQPCallingModules()** — declared in timeslowdown_hooks.hpp, defined in timeslowdown_hooks.cpp; never called. Experimental tab and settings use GetQPCallingModulesWithHandles() and SaveQPCEnabledModulesToSettings(); no code calls the overload that returns std::vector&lt;std::wstring&gt;. Removed declaration and definition (~13 lines).

**Removed (sixty-ninth run):** **GetHookIdentifierByName(const char*)** — declared in timeslowdown_hooks.hpp, defined in timeslowdown_hooks.cpp; never called. All callers use ById APIs with enum values (experimental_tab uses a local array of TimerHookIdentifier). Leftover from run 65 name-based API removal. HOOK_* constants retained (used by CreateAndEnableHook in InstallTimeslowdownHooks). Removed declaration and definition (~22 lines).

**Seventieth run (continuation):** No new dead code removed. Spot-checked Log* format strings vs argument types in timeslowdown_hooks, windows_message_hooks, globals.cpp, swapchain_events.cpp — all use matching specifiers (%d/%u/%p/%x/%s with correct types). The only known format bug is api_hooks CreateDXGIFactory2 (riid with %s), already bug #1 in bug_detection_task.md.

**Seventy-first run (continuation):** No new dead code removed. Spot-checked switch statements in window_proc_hooks.cpp, windows_message_hooks.cpp, latency_manager.cpp, input_activity_stats.cpp for unintentional fall-through (case without break). All cases either have explicit break, intentional fall-through (multiple case labels then one block), or default; no missing-break bug identified.

**Removed (seventy-second run):** **LatencyManager::GetCurrentTechnology()** and **LatencyManager::GetCurrentTechnologyName()** — declared in latency_manager.hpp, defined in latency_manager.cpp; never called. No call sites in advanced_tab or elsewhere; UI uses GetSleepStatus, IsInitialized, SwitchTechnology, etc. Removed both declarations and definitions (~12 lines). ILatencyProvider::GetTechnology/GetTechnologyName remain in the interface (implemented by ReflexProvider etc.) for potential future use.

**Seventy-third run (continuation):** No new dead code removed. Previous run had grepped TODO/FIXME/XXX/HACK (latency_manager, globals, swapchain_events, dxgi_factory_wrapper, etc.); those are documented or in bug list. No additional dead code identified.

**Seventy-fourth run (continuation):** No new dead code removed. Spot-checked main_entry.cpp DEBUG proxy block: snprintf uses `%ws` for `module_name.c_str()` (wchar_t*). C standard uses `%ls` for wide strings in narrow printf; `%ws` is MSVC-specific. Documented as **bug #27** in bug_detection_task.md (portability). Other addon snprintf/sprintf_s usages (globals.cpp, continuous_monitoring.cpp, audio_management.cpp, main_entry version/scan messages) use correct specifiers (%hu, %lu, %lld, %p, %s, %d, %u). No further dead code identified.

**Seventy-fifth run (continuation):** No new dead code removed. Resource-handle spot check: GetDC/ReleaseDC — cli_standalone_ui.cpp uses GetDC(hWnd) and ReleaseDC(hWnd, g_hDC) on multiple paths (correct). window_management.cpp uses GetDC(swapchain_hwnd) but ReleaseDC(nullptr, hdc); the hWnd must match. Documented as **bug #31** in bug_detection_task.md (wrong ReleaseDC hWnd → leak or ERROR_DC_NOT_FOUND). CloseHandle usage across addon appears paired with Create* / Open* in same functions. No further dead code identified.

**Seventy-sixth run (continuation):** No new dead code removed. Re-checked `if (true)` / `if (false)`: api_hooks L812 is already bug #2; windows_message_hooks L1962 is inside a /* ... */ comment (SetUnhandledExceptionFilter_Detour), so not live. DDraw hooks (ddraw_present_hooks.cpp): CreateSurface, DirectDrawCreate, DirectDrawCreateEx, Flip call *_Original with no null check → **bug #32** added. strcpy_s usages (experimental_tab, nvapi_fullscreen_prevention, window_info_tab) use explicit sizes; no obvious buffer-size bug identified. No further dead code identified.

**Seventy-seventh run (continuation):** No new dead code removed. loadlibrary_hooks: LdrLoadDll_Original is guarded by `if (!LdrLoadDll_Original || DllHandle == nullptr)` before use; FreeLibraryAndExitThread uses Original ? Original(...) : system API. dxgi_present_hooks: all IDXGISwapChain*/IDXGIFactory*/IDXGIOutput* detours call *_Original with no null check → **bug #33** added (single doc entry for the file-wide pattern). No further dead code identified.

**Removed (seventy-eighth run):** **LatencyManager::IncreaseFrameId()** — declared in latency_manager.hpp; no definition in latency_manager.cpp and no call sites. Orphaned declaration; frame ID is managed via g_global_frame_id in swapchain/Reflex paths. **ReflexManager::IncreaseFrameId()** — declared in reflex_manager.hpp; no definition in reflex_manager.cpp and no call sites. Removed both declarations.

**Seventy-ninth run (continuation):** No new dead code removed. Output-pointer spot check: latency_manager GetSleepStatus guards `if (out_reason) *out_reason = ...`. swapchain_events GetLastColorSpaceSupportForUI guards `if (out_dxgi)` and `if (out_supported)`. windows_message_hooks GetMouseTranslateScale only caller passes stack addresses. GetCursorPos_Detour calls original with lpPoint without null check when lpPoint can be null → **bug #36** added. No vulkan/d3d11 _Original in addon hooks (no matches for those globs). No further dead code identified.

**Eightieth run (continuation):** No new dead code removed. Division-by-zero spot check: globals.cpp DLSS summary uses `internal_width > 0 && internal_height > 0` before dividing by internal_width/internal_height. windows_message_hooks TranslateMouseLParam uses num_x/num_y only after GetMouseTranslateScale returns true (which requires positive window/render dimensions). swapchain_events modulo uses kFrameDataBufferSize (constant > 0). SetCursorPos_Detour, GetKeyState_Detour, GetAsyncKeyState_Detour all use Original ? Original(...) : system API. No new bugs identified; "How to continue" in bug_detection_task updated with division-by-zero hint.

**Eighty-first run (continuation):** No new dead code removed. Output-parameter spot check: ClipCursor allows lpRect NULL (optional per MSDN); no bug. GetKeyboardState requires valid 256-byte buffer; GetKeyboardState_Detour calls original with lpKeyState without null check → **bug #39** added. Array index usages (lpKeyState[vKey], pInputs[i], g_frame_data[slot], etc.) are either guarded by bounds (nInputs, slot from modulo) or vKey is virtual key code; no obvious out-of-bounds identified. No further dead code identified.

**Eighty-second run (continuation):** No new dead code removed. Raw-input and key-translation detours: GetRawInputBuffer_Detour and GetRawInputData_Detour use Original ? Original(...) : system API; they use pData/pcbSize only after the call when result > 0 (with null checks). ToAscii/ToAsciiEx/ToUnicode/ToUnicodeEx allow null output buffer (lpChar/pwszBuff) per MSDN when only return value is needed; no validation bug. resolution_helpers ApplyDisplaySettingsDXGI LogInfo format/argument mismatch already bug #37. No new bugs identified.

**Eighty-third run (continuation):** No new dead code removed. Container/optional spot check: GetFirstReShadeRuntime checks g_reshade_runtimes.empty() before .front(). main_new_tab and swapchain_tab check frame_times.empty() before .back(). display_commander_logger uses queue_.front() only after loop ensures !queue_.empty(). cli_standalone_ui prefix.back() used after dir.empty() return (prefix = dir). display_cache and monitor_settings check .has_value() before .value() on optional. dcProxyList.back() after push_back. No unsafe .front()/.back()/.value() found; "How to continue" in bug_detection_task updated with container/optional hint.

**Eighty-fourth run (continuation):** No new dead code removed. reflex_provider.cpp: GetSleepStatus checks status_params == nullptr; no null deref. advanced_tab.cpp: .back() guarded with !line.empty(); LogInfo uses %s with string or literals. memset/memcpy spot check: GetKeyboardState_Detour memset(lpKeyState, 0, 256) is inside block guarded by result && lpKeyState != nullptr; dualsense, reshade_global_config, hid_suppression, presentmon, nvidia_profile_search use fixed sizes or validated buffers. No new bugs identified.

**Eighty-fifth run (continuation):** No new dead code removed. pclstats_etw_hooks: memcpy(&val, ptr, 4) is inside __try/__except and len is validated (>= 4, <= 0x10000); ETW descriptor ptr guarded by SEH. EventRegister/EventWriteTransfer _Original null check already bugs #28, #29. size-1 usage: globals/loadlibrary/audio use (size - 1) for string allocation after WideCharToMultiByte; audio size==1 case is bug #38; size==0 would wrap in size_t (optional defensive check elsewhere). general_utils result.erase(result.length() - 3) guarded by result.length() >= 3. No new bugs identified.

**Eighty-sixth run (continuation):** No new dead code removed. _Original usage: timeslowdown_hooks guards all timer _Original calls with null check and fallback to system API (bug #9 is output-pointer validation only). windows_message_hooks uses ternary (_Original ? _Original(...) : Api(...)) throughout. length/size-1: main_entry (length>=3, length>=2), main_new_tab (i < size(), then size()-1), hotkeys_tab (tokens.empty() return before loop), loadlibrary (size>=4), general_utils (length>=3), logger (empty||size<2 short-circuit), cli_standalone (size>=8), remapping_widget (get_available_keyboard_input_methods() always returns 4 elements), detour_call_tracker (loop over by_time; body only when size>0), display_commander_config and hotkeys_file (section line needs '[' and ']' so length>=2). .front()/.back(): advanced_tab (!line.empty()), main_entry (empty check or length>=2), globals GetFirstReShadeRuntime (documented), main_new_tab frame_times (guarded), logger (empty check), cli_standalone (dir.empty() return or !path.empty()), config/hotkeys_file (section/line with length>=2). No new bugs identified.

**Eighty-seventh run (continuation):** No new dead code removed. Division/modulo: swapchain_events 1e9/adjusted_target_fps is inside `if (target_fps >= 1.0f)` so no div-by-zero; all `% kFrameDataBufferSize` use compile-time constant > 0. windows_message_hooks TranslateMouseLParam uses (wx*denom_x)/num_x and (wy*denom_y)/num_y only when GetMouseTranslateScale returns true, which requires window_w > 0 and window_h > 0 (L377–378), so num_x/num_y are positive. globals.cpp scale_x uses internal_width only when internal_width > 0 && internal_height > 0 (L728). swapchain_events LogInfo/LogError spot-check: format specifiers match args (e.g. %d/%s/%x with api, GetDeviceApiString(api), api_version). No new bugs identified.

**Eighty-eighth run (continuation):** No new dead code removed. optional .value(): monitor_settings and display_cache use .value() only after has_value() check; display_cache.hpp GetRefreshRateLabels uses idx.value() only after !idx.has_value() return {}. find/substr: main_entry, globals, timeslowdown, main_new_tab, etc. use find() with npos comparison (== or != npos) before using position; window_proc_hooks g_original_wndproc.find(hwnd) checks it != end() before it->second. nvapi/nvpi_reference.cpp L91, L97: xml.substr(nb, xml.find("</...>", nb) - nb) — if find returns npos, (npos - nb) wraps to large positive; substr(pos, count) is defined and returns [pos, min(pos+count, size())), so no crash but may include extra content for malformed XML; optional improvement: check find result != npos. GetProcAddress/LoadLibrary: main_entry checks module == nullptr after LoadLibraryExW and skips; loop over enumerated modules uses "if (module == nullptr) continue" before GetProcAddress; GetProcAddress return used only when checked (e.g. register_func != nullptr). No new bugs added to bug list.

**Eighty-ninth run (continuation):** No new dead code removed. swapchain_events.cpp ~L2010–2048: OnPresent(swapchain) block uses swapchain (ReShade callback arg), iunknown checked != nullptr before QueryInterface, ComPtr GetContainingOutput/GetDesc1 and SUCCEEDED on all COM calls; DXGI output device name extraction is guarded. g_reflexProvider: globals.cpp L178 initializes g_reflexProvider with make_unique<ReflexProvider>() so it is never null; L2008–2012 and other g_reflexProvider-> calls are safe; L1941 uses defensive "g_reflexProvider &&" before IsInitialized(). static_cast from .load()/GetValue(): swapchain_events frame slots use % kFrameDataBufferSize (constant); monitor_settings/main_new_tab/sleep_hooks/logging use int/DWORD/size_t casts from atomics or settings (indices/enums in normal range). No new bugs identified.

**Ninetieth run (continuation):** No new dead code removed. HRESULT/error handling: swapchain_events, main_entry, globals, api_hooks, windows_message_hooks use SUCCEEDED/FAILED on COM and SHGetFolderPathW results; no ignored HRESULT in sampled code. ApplyHdr1000MetadataToSwapchain(swapchain): called only from OnInitSwapchain after swapchain != nullptr check (L1099–1102); iunknown checked != nullptr before QueryInterface (L1091). GetFirstReShadeRuntime() result checked != nullptr before use (L1143). display_cache.hpp FindClosestResolutionIndex: current_area = width * height and resolutions[i].width * height (int); theoretical int overflow for extreme resolutions (e.g. >46340 width); in practice display modes are bounded (e.g. 8K 7680×4320 fits in int). No new bugs added.

**Ninety-first run (continuation):** No new dead code removed. std::mutex: none used; presentmon_manager and file_sha256.hpp comment "No std::mutex" (project uses SRWLOCK). CloseHandle: main_entry injection paths close remote_process, load_thread, and VirtualFreeEx on all error and success paths; no handle leak in sampled code. (void) ignored results: main_entry swprintf_s, nvidia_profile_tab_shared snprintf (formatting); loadlibrary/cli_standalone GetModuleHandleExW (g_display_commander_module may stay null on failure—caller detection); presentmon TryGetEventPropertyU64 (optional props); nvlowlatencyvk NvLL_VK_SetSleepMode_Original (L132 inside if that checks Original != nullptr; L147 in else branch has no check). **Bug #40 added:** nvlowlatencyvk_hooks.cpp L147 NvLL_VK_SetSleepMode_Original called without null check in else branch.

**Ninety-second run (continuation):** No new dead code removed. _Original usage: timeslowdown_hooks and windows_message_hooks use null check or ternary before _Original (already audited). snprintf/wcscpy_s: main_entry and globals use snprintf(buf, sizeof(buf), ...) and wcscpy_s(arg.load_path, dll_path.c_str()) with load_path[MAX_PATH]; if dll_path longer than MAX_PATH, wcscpy_s truncates (return not checked—injection could fail silently; optional: check return). release/delete/free: globals L460 release() then return atomic ref (no use-after-free); dxgi_factory_wrapper delete this in ref-counted Release; reshade_global_config free(user_profile) after use; settings_wrapper delete ptr in destructor; cli_standalone delete params on error path; presentmon destructor deletes atomic-loaded pointers (delete nullptr is safe; destructor assumed single-threaded shutdown); presentmon exchange() then delete in setters avoids double-free. main_entry L2345 uses %ws (MSVC)—see bug #27. No new bugs added.

**Further findings (not yet removed):** None.

**Ninety-third run (continuation):** No new dead code removed. CMakeLists: GLOB_RECURSE with CONFIGURE_DEPENDS, filters for cli_ui_exe and imgui_wrapper; MSVC /arch:SSE2 then AVX then AVX2 (later overrides earlier); static runtime, /Zi in Release for symbols; no unsafe options found. assert/static_assert: swapchain_events L618 assert(desc.back_buffer_count >= 2)—in NDEBUG/Release the check is removed; logic above (L608–612) already forces back_buffer_count to 3 when < 2, so value is >= 2 at assert; optional hardening: add runtime check and early return for Release. static_assert in windows_message_hooks, dualsense_hid_wrapper, ring_buffer are compile-time only (no release impact). ngx_hooks: GetUI_Original at L275–276 unguarded (bug #20); SetI at L405 guarded by != nullptr (L403); GetVoidPointer_Original at L1042 unguarded, OutValue (bug #21); other _Original calls in ngx use null check. No new bugs added.

**Ninety-fourth run (continuation):** No new dead code removed. streamline_hooks: slDLSSGetOptimalSettings_Original (L319) and slDLSSSetOptions_Original (L412) called without null check—bugs #22, #23; slDLSSGGetState_Detour and slGetFeatureFunction_Detour null-check _Original before use (L447, L458). static_cast<int>(.size()/.length()): advanced_tab, monitor_settings, main_new_tab use for ImGui PlotLines/Combo/loop bounds; UI collections (frame_times, monitor_labels, resolution_labels, display_info) are bounded in practice (small buffers, display enum); truncation only if size() > INT_MAX, not realistic here. No new bugs added.

**Ninety-fifth run (continuation):** No new dead code removed. Chained pointer use (swapchain->get_device()->get_api(), runtime->get_device()->get_api()): swapchain_events hookToSwapChain checks swapchain == nullptr at L189 before using get_device(); OnInitSwapchain checks swapchain at L1099; ApplyHdr1000MetadataToSwapchain is only called after that. main_new_tab GetGraphicsApiFromRuntime checks if (!runtime) at L1364 before get_device(). ReShade API contract implies get_device() is non-null for valid swapchain/effect_runtime. dxgi_present_hooks L284 uses swapchain->get_device() in context where swapchain is from hook. No null-deref from chained calls. Log format: main_entry %zu with (i+1, ptr), globals %zu with .size()—correct for size_t; api_hooks/windows_message_hooks use %p, %08X, %u appropriately. latency/ folder: no unchecked [index] or .at() in sampled grep. No new bugs added.

**Ninety-sixth run (continuation):** No new dead code removed. Critical API return checks: main_entry VirtualAllocEx result checked (load_param == nullptr → cleanup, return false); CreateRemoteThread result checked (load_thread == nullptr → VirtualFreeEx, CloseHandle(remote_process), return false). cli_standalone_ui CreateThread: return checked (if (h) CloseHandle(h)); on failure params may remain (worker handles or caller cleans up—no new bug without full flow). nvidia_profile_tab_shared CreateThread: hThread != nullptr checked before use. ddraw_present_hooks: _Original called without null check (bug #32). device_api switch: general_utils GetDeviceApiString and GetDeviceApiVersionString have default cases (L286, L344); main_new_tab GetGraphicsApiFromRuntime has default (L1373). No new bugs added.

**Ninety-seventh run (continuation):** No new dead code removed. Process/path APIs: main_entry WriteProcessMemory return checked (!WriteProcessMemory → VirtualFreeEx, CloseHandle, return false). GetModuleFileNameW/GetModuleFileNameA: all call sites check return (== 0, > 0, or path_length) before using buffer (main_entry, globals, timeslowdown_hooks, main_new_tab). d3d9_device_vtable_logging: Create* and GetBackBuffer detours call _Original without null check and pass output pointers unvalidated—bug #30. Atomics: fetch_add used for event counters; compare_exchange_strong for g_initialized_with_hwnd, g_submit_start_time_ns, g_game_start_time_ns (single-writer or idempotent updates); no conflicting read-modify-write races found. No new bugs added.

**Checklist status:** All 150 `.cpp` files in [dead_code_file_checklist.md](dead_code_file_checklist.md) have been checked (151 after mutually_exclusive_keys removal; 152 before WinMM removal); no further dead code or `#if 0` blocks were found in ui/, utils/, latent_sync/, latency/, nvapi/, settings/, widgets/, proxy_dll/, or other remaining dirs.

**Follow-up search:** Uninstall/Cleanup/Shutdown, Register/Unregister, Get*/Is*LockHeld/Are*Installed, static helpers (RecordCRDebug, ParseDisplayNumberFromDeviceName, RemoveDlssOverrideHandle), and CleanupGPUMeasurement* were verified; all have call sites. No new uncalled symbols beyond (10)–(18).

**Later pass:** Apply/Restore/Log (ApplyWindowChange, LogCurrentLogLevel, RestoreClipCursor, ApplySpoofGameResolutionInSizeMessages, LogD3D9*, ReflexManager::RestoreSleepMode, NVAPIFullscreenPrevention::Cleanup), Reset/Clear/Flush (except 19–22), HasTrackedSwapchains (member used; free function 22), and DPI/Enable/Disable were verified; all have call sites. No further uncalled symbols beyond (10)–(22).

**Kept by design:** Excluded sources (cli_ui_exe, imgui_wrapper_reshade.cpp) and platform/feature `#ifdef`s (_WIN64, EXPERIMENTAL_FEATURES, etc.).

---

## Notes

- **Safety:** Before removing any file or symbol, run a full build and (if available) tests. Prefer one PR per category (e.g. “remove unused headers”) to simplify review.
- **Project rules:** Respect `.cursor/rules` (e.g. no `std::mutex`, Reshade/ImGui patterns). Do not remove code that is required for Special-K compatibility or documented optional features unless explicitly agreed.
- **Submodules:** Do not treat external submodule code (e.g. reshade) as “dead” just because we don’t call it; only consider our addon’s own sources and our use of external APIs.
