# Split DllMain PROCESS_ATTACH – Task Plan

## Status: **Done** (helpers in main_entry.cpp anonymous namespace; case body replaced with short sequence)

## Overview

The `case DLL_PROCESS_ATTACH:` block in `DllMain()` in `src/addons/display_commander/main_entry.cpp` was **~560 lines**. It has been split into smaller, DllMain-safe helpers so that the switch case is now a short, readable sequence of steps.

**Location:** `main_entry.cpp` L1841–2493 (`DllMain`); the block to split is L1843–2402.

**Constraint:** Helpers called from `DLL_PROCESS_ATTACH` must not start threads, acquire locks that might trigger loader lock, or call into DLLs that do. Keep only simple logic, config reads that don’t start threads, and LoadLibrary of known DLLs (ReShade, etc.). Any helper that might do heavy I/O or start threads should be called only after the addon is registered and we are effectively in “post-DllMain” init (e.g. `DoInitializationWithoutHwndSafe`).

---

## Current Structure (Logical Sections)

| Section | Approx. lines | Description |
|--------|----------------|-------------|
| Early init & checks | 1843–1895 | g_hmodule, g_dll_load_time_ns, config init, DetectWine, SpecialK check, DetectMultipleDisplayCommanderVersions, command line / rundll32 early return, g_shutdown. |
| ReShade in modules | 1912–1931 | K32EnumProcessModules, loop GetProcAddress(ReShadeRegisterAddon), set g_reshade_loaded. |
| Load .dc64/.dc32/.dc/.asi | 1932–1962 | GetModuleFileNameW, addon_dir, directory_iterator, LoadLibraryW for matching extensions. |
| Config dir & .NO_RESHADE | 1963–1986 | GetDisplayCommanderConfigDirectoryW lambda; check .NO_RESHADE/.NORESHADE, set g_no_reshade_mode. |
| Load ReShade from cwd | 1987–2025 | #ifdef _WIN64 Load Reshade64.dll else Reshade32.dll; set env RESHADE_BASE_PATH_OVERRIDE, RESHADE_DISABLE_LOADING_CHECK. |
| Entry point detection | 2026–2111 | GetModuleFileNameW, ProxyDllInfo array, compare module name, set entry_point and found_proxy. |
| ReShade not loaded path | 2113–2298 | Platform detection, whitelist, LocalAppData Reshade path, LoadLibrary or MessageBox, multiple return FALSE branches. |
| No-ReShade mode init | 2300–2311 | g_standalone_ui_pending, DoInitializationWithoutHwndSafe, g_process_attached, break. |
| register_addon & failure | 2313–2346 | reshade::register_addon; on failure: debug logs, module list to reshade::log, return FALSE. |
| Post-registration init | 2347–2402 | Config init, DetectMultipleReShadeVersions, register_overlay, entry_point logging, utils::initialize_qpc_timing_constants, DoInitializationWithoutHwndSafe, DoInitializationWithoutHwnd, LoadAddonsFromPluginsDirectory, g_process_attached, break. |

---

## Proposed Helper Functions

All helpers must be callable from `DLL_PROCESS_ATTACH` without violating loader lock or starting threads. Prefer a separate TU (e.g. `main_entry_dllmain_helpers.cpp`) so `main_entry.cpp` only contains the switch and a few short calls.

| # | Proposed name | Responsibility | DllMain-safe? |
|---|----------------|-----------------|----------------|
| 1 | `ProcessAttach_EarlyChecksAndInit(HMODULE h_module)` | Set g_hmodule, g_dll_load_time_ns; config init (if safe); DetectWine; SpecialK check; DetectMultipleDisplayCommanderVersions; command line / rundll32 check and early return. Returns `bool`: false = refuse load (caller returns FALSE). | Yes (config init may start thread – verify; if so, move config init to after register_addon). |
| 2 | `ProcessAttach_DetectReShadeInModules()` | Enumerate modules, set g_reshade_loaded if ReShadeRegisterAddon found. | Yes |
| 3 | `ProcessAttach_LoadLocalAddonDlls(HMODULE h_module)` | Get addon dir, iterate .dc64/.dc32/.dc/.asi, LoadLibraryW. | Yes (LoadLibrary is allowed) |
| 4 | `ProcessAttach_GetConfigDirectoryW()` | Return GetModuleFileNameW(nullptr) parent path as wstring. Replaces lambda. | Yes |
| 5 | `ProcessAttach_CheckNoReShadeMode()` | Use ProcessAttach_GetConfigDirectoryW, check .NO_RESHADE/.NORESHADE, set g_no_reshade_mode. | Yes |
| 6 | `ProcessAttach_TryLoadReShadeFromCwd()` | Set RESHADE_BASE_PATH_OVERRIDE, RESHADE_DISABLE_LOADING_CHECK, LoadLibrary Reshade64/32, set g_reshade_loaded. | Yes |
| 7 | `ProcessAttach_DetectEntryPoint(HMODULE h_module, std::wstring& entry_point, bool& found_proxy)` | Get module path, stem/filename, ProxyDllInfo loop, set entry_point and found_proxy. | Yes |
| 8 | `ProcessAttach_TryLoadReShadeWhenNotLoaded()` | Platform detection, whitelist, LocalAppData path, LoadLibrary or MessageBox; set g_reshade_loaded or return false. May show UI. | Yes (MessageBox is allowed) |
| 9 | `ProcessAttach_NoReShadeModeInit(HMODULE h_module)` | g_standalone_ui_pending = true, DoInitializationWithoutHwndSafe(h_module), g_process_attached = true. | DoInitializationWithoutHwndSafe starts threads – only call when we’re intentionally leaving “pure” DllMain path (no-ReShade mode). |
| 10 | `ProcessAttach_RegisterAndPostInit(HMODULE h_module, const std::wstring& entry_point)` | register_addon; config init; DetectMultipleReShadeVersions; register_overlay; logging; utils::initialize_qpc_timing_constants; DoInitializationWithoutHwndSafe; DoInitializationWithoutHwnd; LoadAddonsFromPluginsDirectory; g_process_attached = true. | Post register_addon, ReShade is up; threads in DoInitializationWithoutHwndSafe are acceptable. |

**Note:** Config manager init (DisplayCommanderConfigManager::Initialize) may start a thread. If it does, ensure it is not called in the “early” path that runs before we know we’re not doing rundll32 early return. Today it’s called at the very start of PROCESS_ATTACH; if that’s required for logging, keep it there but document that this is the only thread start acceptable before register_addon.

---

## Recommended Order of Extraction

1. **ProcessAttach_GetConfigDirectoryW** – Extract the lambda to a function; use it in CheckNoReShadeMode and in TryLoadReShade paths that need the config dir.
2. **ProcessAttach_CheckNoReShadeMode** – Small, uses GetConfigDirectoryW.
3. **ProcessAttach_DetectReShadeInModules** – Isolated module enumeration.
4. **ProcessAttach_LoadLocalAddonDlls** – Isolated directory iteration and LoadLibrary.
5. **ProcessAttach_TryLoadReShadeFromCwd** – Win64/Win32 branches, env vars, LoadLibrary.
6. **ProcessAttach_DetectEntryPoint** – Proxy list and entry_point/found_proxy out-params.
7. **ProcessAttach_TryLoadReShadeWhenNotLoaded** – Platform, whitelist, LocalAppData, message boxes. Returns bool (true = loaded or already loaded, false = refuse load).
8. **ProcessAttach_EarlyChecksAndInit** – SpecialK, multi-DC, rundll32. Return bool (false = refuse). Consider moving config init to after register_addon if it starts a thread.
9. **ProcessAttach_NoReShadeModeInit** – No-ReShade branch body.
10. **ProcessAttach_RegisterAndPostInit** – Everything after successful register_addon.

Then `case DLL_PROCESS_ATTACH:` becomes:

```cpp
case DLL_PROCESS_ATTACH: {
    if (!ProcessAttach_EarlyChecksAndInit(h_module))
        return FALSE;

    ProcessAttach_DetectReShadeInModules();
    ProcessAttach_LoadLocalAddonDlls(h_module);
    ProcessAttach_CheckNoReShadeMode();

    if (!g_reshade_loaded.load() && !g_no_reshade_mode.load()) {
        ProcessAttach_TryLoadReShadeFromCwd();
        if (!g_reshade_loaded.load())
            if (!ProcessAttach_TryLoadReShadeWhenNotLoaded(h_module))
                return FALSE;
    }

    if (g_no_reshade_mode.load()) {
        ProcessAttach_NoReShadeModeInit(h_module);
        break;
    }

    std::wstring entry_point;
    bool found_proxy = false;
    ProcessAttach_DetectEntryPoint(h_module, entry_point, found_proxy);

    if (!reshade::register_addon(h_module)) {
        // ... existing failure logging ...
        return FALSE;
    }

    ProcessAttach_RegisterAndPostInit(h_module, entry_point);
    break;
}
```

(Adjust order and branch logic to match current behavior exactly; e.g. LocalAppData try is inside “platform detected or proxy or whitelist” in current code.)

---

## File Layout

- **Option A:** Keep all helpers in `main_entry.cpp` in an anonymous namespace or static. No new TU.
- **Option B:** New file `main_entry_dllmain_helpers.cpp` with declarations in `main_entry.hpp` or a small `main_entry_dllmain_helpers.hpp` included only by main_entry.cpp. Implement ProcessAttach_* there; DllMain in main_entry.cpp calls them.

Option B keeps main_entry.cpp shorter and groups all PROCESS_ATTACH logic in one place. Ensure no circular includes and that globals (g_hmodule, g_reshade_loaded, g_no_reshade_mode, etc.) are declared in a shared header or in main_entry.cpp with extern in the helpers’ TU.

---

## Verification

- Build and run: load addon as ReShade addon, as proxy, and with .NO_RESHADE.
- Confirm no new threads or lock acquisitions during the “early” part of PROCESS_ATTACH before register_addon (except any single config/log thread that already exists).
- ReShade registration and overlay registration behavior unchanged.
- Load order and refusal (SpecialK, multi-DC, rundll32, missing ReShade) unchanged.
