# main_entry.cpp – Long Functions Analysis and Split Plan

## Overview

`src/addons/display_commander/main_entry.cpp` is **~3261 lines**. This document lists every function **longer than 100 lines**, their approximate extent, and how to split them. Target: no function in this file should exceed ~100 lines after refactor.

**Reference:** Similar split approach used in [draw_display_settings_vsync_tearing_split.md](draw_display_settings_vsync_tearing_split.md).

---

## Summary Table

| Function | Lines | Line range | Priority |
|----------|-------|------------|----------|
| `DllMain` (case DLL_PROCESS_ATTACH) | ~560 | 1843–2402 | High |
| `OverrideReShadeSettings` | 221 | 701–921 | High |
| `DetectMultipleDisplayCommanderVersions` | 210 | 1235–1444 | Medium |
| `DetectMultipleReShadeVersions` | 136 | 1096–1231 | Medium |
| `WaitForProcessAndInject` | 139 | 2806–2944 | Medium |
| `OnInitEffectRuntime` | 124 | 380–503 | Medium |
| `HandleSafemode` | 117 | 1539–1655 | Medium |
| `OnPerformanceOverlay` | 117 | 581–697 | Medium |
| `LoadAddonsFromPluginsDirectory` | 104 | 990–1093 | Low |
| `InjectIntoProcess` | 103 | 2699–2801 | Low |
| `DoInitializationWithoutHwndSafe` | 102 | 1657–1758 | Low |

**Note:** `DllMain` as a whole (1841–2493) includes PROCESS_ATTACH (~560 lines), THREAD_ATTACH, THREAD_DETACH, and PROCESS_DETACH. Only the PROCESS_ATTACH block is >100 lines; the rest are short. The split plan below focuses on extracting PROCESS_ATTACH logic into named helpers.

---

## 1. DllMain – case DLL_PROCESS_ATTACH (~560 lines, L1843–2402)

**Dedicated task:** [main_entry_split_dllmain_process_attach.md](main_entry_split_dllmain_process_attach.md)

**Current structure (logical blocks):**

- Early init: g_hmodule, g_dll_load_time_ns, config init, DetectWine, SpecialK check, multi-DC check, rundll32 early return, g_shutdown.
- ReShade detection: enumerate modules, find ReShadeRegisterAddon.
- Load .dc64/.dc32/.dc/.asi from addon dir.
- GetDisplayCommanderConfigDirectoryW lambda; .NO_RESHADE check.
- Load ReShade from cwd or LocalAppData (Win64/Win32 branches).
- Proxy/entry-point detection: module name, ProxyDllInfo list, entry_point string.
- If ReShade not loaded: platform detection, whitelist, try LocalAppData Reshade path, message boxes.
- No-ReShade mode: minimal init, g_standalone_ui_pending.
- reshade::register_addon; on failure: debug logging, return FALSE.
- Post-registration: config init, DetectMultipleReShadeVersions, overlay register, entry-point logging, utils::initialize_qpc_timing_constants, DoInitializationWithoutHwndSafe, DoInitializationWithoutHwnd, LoadAddonsFromPluginsDirectory.

**Proposed helpers (in same file or `main_entry_dllmain_helpers.cpp`):**

| Helper | Responsibility |
|--------|----------------|
| `ProcessAttach_EarlyChecksAndInit()` | g_hmodule, load time, config init, DetectWine, SpecialK check, DetectMultipleDisplayCommanderVersions, rundll32 return, g_shutdown. Returns false if load should be refused. |
| `ProcessAttach_DetectReShadeInModules()` | Enumerate modules, set g_reshade_loaded if ReShadeRegisterAddon found. |
| `ProcessAttach_LoadLocalAddonDlls(HMODULE)` | Load .dc64/.dc32/.dc/.asi from addon directory. |
| `ProcessAttach_CheckNoReShadeMode()` | GetDisplayCommanderConfigDirectoryW, .NO_RESHADE/.NORESHADE, set g_no_reshade_mode. |
| `ProcessAttach_TryLoadReShadeFromCwd()` | #ifdef _WIN64 / #else LoadLibrary Reshade64/32 from cwd, set g_reshade_loaded. |
| `ProcessAttach_DetectEntryPoint(HMODULE, std::wstring& entry_point, bool& found_proxy)` | Get module filename, ProxyDllInfo loop, set entry_point and found_proxy. |
| `ProcessAttach_TryLoadReShadeWhenNotLoaded(HMODULE)` | Platform detection, whitelist, LocalAppData Reshade path, LoadLibrary or MessageBox; return false to refuse load. |
| `ProcessAttach_NoReShadeModeInit(HMODULE)` | g_standalone_ui_pending, DoInitializationWithoutHwndSafe, etc. |
| `ProcessAttach_RegisterAndPostInit(HMODULE, const std::wstring& entry_point)` | register_addon, config init, DetectMultipleReShadeVersions, register_overlay, logging, utils::initialize_qpc_timing_constants, DoInitializationWithoutHwndSafe, DoInitializationWithoutHwnd, LoadAddonsFromPluginsDirectory. |

After split, `case DLL_PROCESS_ATTACH:` becomes a short sequence of calls to these helpers.

---

## 2. OverrideReShadeSettings (221 lines, L701–921)

**Dedicated task:** [main_entry_split_override_reshade_settings.md](main_entry_split_override_reshade_settings.md)

**Current structure:**

- Early return if !g_reshade_loaded.
- Block 1: Read/parse ReShade OVERLAY::Window config; add [Window][DC] and [Window][RenoDX] if missing; write back.
- Block 2: TutorialProgress = 4, CheckForUpdates = 0, ShowClock (if suppress_reshade_clock).
- Block 3: LoadFromDllMainSetOnce: get/set config, first-time set and save.
- Block 4: LocalAppData paths, create Shaders/Textures dirs, addPathToSearchPaths lambda, add EffectSearchPaths and TextureSearchPaths.

**Proposed helpers:**

| Helper | Responsibility |
|--------|----------------|
| `OverrideReShadeSettings_WindowConfig()` | Read OVERLAY::Window, add DC and RenoDX window entries if absent, set_config_value. |
| `OverrideReShadeSettings_TutorialAndUpdates()` | Set TutorialProgress, CheckForUpdates, ShowClock. |
| `OverrideReShadeSettings_LoadFromDllMainOnce()` | LoadFromDllMainSetOnce logic and save. |
| `OverrideReShadeSettings_AddDisplayCommanderPaths()` | LocalAppData dirs, create_directories, addPathToSearchPaths for EffectSearchPaths and TextureSearchPaths. |

`OverrideReShadeSettings()` then becomes: early return + call these four helpers + final LogInfo.

---

## 3. DetectMultipleDisplayCommanderVersions (210 lines, L1235–1444)

**Current structure:** Enumerate modules, skip self, find GetDisplayCommanderVersion; for each other DC module: path, version, LoadedNs, conflict resolution (refuse if other loaded first), notify other instance, set g_other_dc_version_detected.

**Proposed split:**

- `DetectMultipleDisplayCommanderVersions_CollectOtherModules(std::vector<DisplayCommanderModuleInfo>& out)` – enumerate, fill `out` with handle, path, version, load_time_ns, has_load_time.
- `DetectMultipleDisplayCommanderVersions_ResolveConflicts(const std::vector<DisplayCommanderModuleInfo>&, bool& should_refuse)` – compare load times, notify other, set g_other_dc_version_detected and should_refuse.

Keep `DetectMultipleDisplayCommanderVersions()` as a short wrapper: call Collect, call Resolve, return should_refuse. Define a small struct `DisplayCommanderModuleInfo` (handle, path, version, load_time_ns, has_load_time) in anonymous namespace or header.

---

## 4. DetectMultipleReShadeVersions (136 lines, L1096–1231)

**Current structure:** Reset g_reshade_debug_info, enumerate modules, for each with ReShadeRegisterAddon + ReShadeUnregisterAddon: path, version (GetFileVersionInfo), ImGui support, push to g_reshade_debug_info.modules; then check has_compatible_version, set total_modules_found and detection_completed.

**Proposed split:**

- `DetectMultipleReShadeVersions_EnumerateModules(std::vector<ReShadeModuleInfo>& out)` – enumerate, fill `out` (path, version, has_imgui_support, is_version_662_or_above, handle). ReShadeModuleInfo already exists; use it for the vector (or a duplicate type in anon namespace).
- Keep `DetectMultipleReShadeVersions()` as: reset g_reshade_debug_info, call EnumerateModules(g_reshade_debug_info.modules), set total_modules_found, has_compatible_version, detection_completed, error_message, and existing warning logs.

---

## 5. WaitForProcessAndInject (139 lines, L2806–2944)

**Current structure:** Reset g_wait_and_inject_stop; mark existing PIDs in process_seen; loop: snapshot, Process32First/Next, for each new matching exe call GetReShadeDllPath and InjectIntoProcess; Sleep(10).

**Proposed split:**

- `WaitForProcessAndInject_MarkExistingProcesses(const std::wstring& exe_name, std::array<bool, 65536>& process_seen)` – CreateToolhelp32Snapshot, Process32FirstW/NextW, set process_seen for matching PIDs.
- `WaitForProcessAndInject_ProcessSnapshot(const std::wstring& exe_name, std::array<bool, 65536>& process_seen)` – one snapshot iteration: find new PIDs matching exe_name, for each call GetReShadeDllPath + InjectIntoProcess, mark process_seen. Returns void.

Then `WaitForProcessAndInject()`: reset stop flag, MarkExistingProcesses, while (!g_wait_and_inject_stop) { WaitForProcessAndInject_ProcessSnapshot(...); Sleep(10); }.

---

## 6. OnInitEffectRuntime (124 lines, L380–503)

**Current structure:** Null check, AddReShadeRuntime; one-time shader extract (extract_resource lambda, multiple paths); refresh rate monitoring start if settings; static initialized_with_hwnd: get_hwnd, DoInitializationWithHwnd, autoclick threads.

**Proposed split:**

- `OnInitEffectRuntime_ExtractShadersOnce()` – entire static shader_extract_done block (extract_resource, IDR_* calls). Call from OnInitEffectRuntime.
- `OnInitEffectRuntime_StartRefreshRateMonitoringIfNeeded()` – check show_actual_refresh_rate / show_refresh_rate_frame_times, call StartNvapiActualRefreshRateMonitoring.
- `OnInitEffectRuntime_InitWithHwndOnce(reshade::api::effect_runtime* runtime)` – static initialized_with_hwnd, get_hwnd, DoInitializationWithHwnd, autoclick start. Call from OnInitEffectRuntime.

OnInitEffectRuntime becomes: null check, AddReShadeRuntime, ExtractShadersOnce, StartRefreshRateMonitoringIfNeeded, InitWithHwndOnce, log exit.

---

## 7. HandleSafemode (117 lines, L1539–1655)

**Current structure:** Read safemode and dlls_to_load_before; wait for DLLs (parse list, GetModuleHandleW loop with timeout); DLL loading delay; if safemode_enabled: disable window mode, continue_rendering, fps_limiter, auto-apply, XInput, SaveAll; else: set safemode to 0, d3d9_flipex toggles, SaveAll.

**Proposed split:**

- `HandleSafemode_WaitForDlls(const std::string& dlls_to_load)` – parse comma/semicolon list, trim, wait for each with GetModuleHandleW + timeout. No settings write.
- `HandleSafemode_ApplySafemodeSettings()` – all settings changes when safemode is enabled (window_mode, continue_rendering, fps_limiter, auto-apply, suppress_xinput_hooks, SaveAll).
- `HandleSafemode_ApplyNonSafemodeSettings()` – set safemode to 0, d3d9_flipex toggles, SaveAll.

HandleSafemode: read safemode and dlls_to_load_before; if !dlls_to_load.empty() call WaitForDlls; apply delay; if safemode_enabled call ApplySafemodeSettings else ApplyNonSafemodeSettings.

---

## 8. OnPerformanceOverlay (117 lines, L581–697)

**Current structure:** show_display_commander_ui branch (window pos/size, "Display Commander" window, NewUISystem::Draw, DrawCustomCursor); read many overlay toggles (show_fps_counter, show_vrr_status, …); NVAPI refresh rate start/stop; if !show_test_overlay return; "Test Window" with DrawPerformanceOverlayContent.

**Proposed split:**

- `OnPerformanceOverlay_DisplayCommanderWindow(reshade::api::effect_runtime* runtime)` – entire block for show_display_commander_ui: window pos/size, Begin("Display Commander"), block_input_next_frame, position save, NewUISystem::Draw, show_display_commander_ui.SetValue(false), DrawCustomCursor. Call when show_display_commander_ui.
- `OnPerformanceOverlay_TestWindow(reshade::api::effect_runtime* runtime, bool show_tooltips)` – SetNextWindowPos/Size/BgAlpha, Begin("Test Window"), DrawPerformanceOverlayContent, End. Call when show_test_overlay.

Keep in OnPerformanceOverlay: reading all overlay toggles, NVAPI refresh rate start/stop, early return if !show_test_overlay, then call TestWindow. Call DisplayCommanderWindow at the start when show_display_commander_ui. Toggle reading can stay inline or move to a small struct populated at top.

---

## 9. LoadAddonsFromPluginsDirectory (104 lines, L990–1093)

**Current structure:** Get LocalAppData, build addons_dir, create_directories; if !g_reshade_loaded return; directory_iterator over addons_dir, filter .addon64/.addon32 by arch, IsAddonEnabledForLoading, LoadLibraryExW, count loaded/failed/skipped.

**Proposed split:**

- `LoadAddonsFromPluginsDirectory_GetAddonsDir(std::filesystem::path& out)` – SHGetFolderPathW, build path, create_directories, return success. Optional: return path so caller can iterate.
- `LoadAddonsFromPluginsDirectory_TryLoadOne(const std::filesystem::path& path, int& loaded, int& failed)` – check extension by arch, IsAddonEnabledForLoading; if enabled, LoadLibraryExW and increment loaded or failed. (Skipped count can be inferred or passed by ref.)

LoadAddonsFromPluginsDirectory: GetAddonsDir; if !g_reshade_loaded return; loop over directory_iterator, call TryLoadOne for each file; log summary. This keeps the function just over 50 lines.

---

## 10. InjectIntoProcess (103 lines, L2699–2801)

**Current structure:** OpenProcess; IsWow64Process and arch check; fill loading_data; VirtualAllocEx; WriteProcessMemory; CreateRemoteThread(LoadLibraryW); WaitForSingleObject; GetExitCodeThread; cleanup, return success.

**Proposed split:**

- `InjectIntoProcess_OpenTarget(DWORD pid, HANDLE& out_process, bool& out_is_wow64)` – OpenProcess, IsWow64Process, arch check. Returns false on failure (caller closes handle if needed). Or return HANDLE and let caller check null.
- `InjectIntoProcess_DoRemoteLoadLibrary(HANDLE process, const std::wstring& dll_path, DWORD& out_exit_code)` – VirtualAllocEx, WriteProcessMemory (loading_data with dll_path only, or pass path and build in helper), CreateRemoteThread(LoadLibraryW), WaitForSingleObject, GetExitCodeThread, VirtualFreeEx, CloseHandle(thread). Returns true if exit_code != 0. Caller closes process handle.

InjectIntoProcess: call OpenTarget; if !ok return false; call DoRemoteLoadLibrary; CloseHandle(process); return success. Reduces main function to ~25 lines.

---

## 11. DoInitializationWithoutHwndSafe (102 lines, L1657–1758)

**Current structure:** Timer setup; LogInfo; LoadAllSettingsAtStartup; InstallLoadLibraryHooks; LogCurrentLogLevel; disable DPI if setting; HandleSafemode; module pinning; process_exit_hooks::Initialize; InstallApiHooks; InstallRealDXGIMinHookHooks; g_dll_initialization_complete; OverrideReShadeSettings; XInput hooks; display cache; display_initial_state; input_remapping; InitializeNewUISystem; StartContinuousMonitoring; StartGPUCompletionMonitoring; refresh rate monitoring; RunBackgroundAudioMonitor thread; g_nvapiFullscreenPrevention.CheckAndAutoEnable; InitExperimentalTab; DualSense init; keyboard_tracker::Initialize.

**Proposed split:**

- `DoInitializationWithoutHwndSafe_Early(HMODULE h_module)` – timer setup, LogInfo, LoadAllSettingsAtStartup, InstallLoadLibraryHooks, LogCurrentLogLevel, DPI disable, HandleSafemode, module pinning, process_exit_hooks::Initialize, InstallApiHooks, InstallRealDXGIMinHookHooks, g_dll_initialization_complete, OverrideReShadeSettings.
- `DoInitializationWithoutHwndSafe_Late()` – XInput hooks, display_cache, display_initial_state, input_remapping, InitializeNewUISystem, StartContinuousMonitoring, StartGPUCompletionMonitoring, refresh rate monitoring, RunBackgroundAudioMonitor, CheckAndAutoEnable, InitExperimentalTab, DualSense, keyboard_tracker::Initialize.

DoInitializationWithoutHwndSafe: Early(h_module); Late();. Optionally group Late into "display & input" vs "monitoring & UI" if you want three helpers.

---

## Recommended Order of Implementation

1. **OverrideReShadeSettings** – clear blocks, no DllMain sensitivity.
2. **HandleSafemode** – small, self-contained.
3. **OnInitEffectRuntime** – shader extract and init-with-hwnd are natural cuts.
4. **DoInitializationWithoutHwndSafe** – two clear phases.
5. **DetectMultipleReShadeVersions** / **DetectMultipleDisplayCommanderVersions** – reduce duplication and clarify conflict logic.
6. **OnPerformanceOverlay** – two windows, two helpers.
7. **LoadAddonsFromPluginsDirectory** / **InjectIntoProcess** / **WaitForProcessAndInject** – straightforward extractions.
8. **DllMain PROCESS_ATTACH** – do last; move helpers to `main_entry_dllmain_helpers.cpp` or keep in main_entry with clear naming to avoid DllMain complexity.

---

## File Layout After Split

- **main_entry.cpp** – Keep DllMain, ReShade event handlers, RunDLL exports, and either inline helpers or includes. Prefer moving PROCESS_ATTACH helpers to a separate TU (e.g. `main_entry_dllmain_helpers.cpp`) so DllMain stays in one file and only calls into the new TU (no new threads/locks in DllMain; helpers must stay DllMain-safe where they are called from PROCESS_ATTACH).
- **main_entry_reshade_settings.cpp** (optional) – OverrideReShadeSettings and its four helpers, if you want to keep main_entry smaller.
- **main_entry_injection.cpp** (optional) – GetReShadeDllPath, InjectIntoProcess, WaitForProcessAndInject and their helpers (already static/local to main_entry; moving to a TU would require exposing or passing globals like g_wait_and_inject_stop).

Keep all new helpers in the same namespace/visibility as today (static or in anonymous namespace where appropriate) to avoid breaking call sites.
