# Hook Statistics Manager

## Overview

**Rule (storage):** All hooks for a given file (e.g. `kernel32.dll`) are stored in **one array** — one contiguous block of raw data per file. The Hook Statistics Manager has one container (array) per file.

**Rule (display):** It is OK to show **separate categories/sections** in the UI within that file — e.g. "HID section" of kernel32.dll, "Display settings" section of user32.dll — so the user can still see logical groupings (HID API, display settings, kernel, etc.) while the underlying data is one array per file.

**Rule (installation):** There should be **one `InstallXXXHooks(...)` per DLL** — one install function per file (e.g. `InstallKernel32Hooks()`, `InstallUser32Hooks()`, `InstallHidHooks()`). This simplifies hooking, avoids hooking the same DLL twice from different places, and keeps all hooks for that module in one place.

**Goal:** Introduce a **Hook Statistics Manager** that owns many **containers** (one array per file). The UI iterates over files; for each file it can draw one table or split the table into subsections by category (HID, display_settings, etc.).

## Current State

- **Storage:** `g_hook_stats[hook_index]` in `windows_message_hooks.cpp` — one `HookCallStats` (total_calls, unsuppressed_calls) per hook index.
- **Metadata:** `g_hook_info[hook_index]` = `{ name, dll_group }`. DllGroup is an enum: USER32, XINPUT1_4, KERNEL32, DINPUT8, DINPUT, OPENGL, DISPLAY_SETTINGS, HID_API.
- **Display:** `GetDllGroupName(DllGroup)` returns display strings:
  - USER32 → `"user32.dll"`
  - KERNEL32 → `"kernel32.dll"`
  - HID_API → `"kernel32.dll (hid_api)"`
  - DISPLAY_SETTINGS → `"user32.dll (display_settings)"`
  - etc.
- **UI:** `hook_stats_tab.cpp` iterates over a fixed list of DllGroups. For each group it builds one collapsible section and **one table** with rows for all hooks in that group. So we currently have **one table per DllGroup** (8 sections), not one table per actual DLL file.

**Problem:** Two groups can map to the same DLL (e.g. KERNEL32 and HID_API both kernel32; USER32 and DISPLAY_SETTINGS both user32). The rule we want is **one table per file**, so e.g. one "kernel32.dll" table containing Sleep, ReadFile, CreateFileA, etc., and one "user32.dll" table containing GetMessage, ClipCursor, ChangeDisplaySettings, etc.

## Target Design

### 1. Notion of "file"

- **File** = canonical module name used as the key for one table (e.g. `kernel32.dll`, `user32.dll`, `hid.dll`, `xinput1_4.dll`, `dinput8.dll`, `dinput.dll`, `opengl32.dll`).
- Hooks that live in the same DLL are grouped under that one file, regardless of current DllGroup (so KERNEL32 and HID_API → one file `kernel32.dll`; USER32 and DISPLAY_SETTINGS → one file `user32.dll`).

### 2. Hook Statistics Manager — storage: one array per file

- **Raw data:** For each file (e.g. `kernel32.dll`), the manager has **one array** of hook stats per file. All kernel32 hooks live in one kernel32 array; all user32 in one user32 array.
- **Responsibility:** Provide a single place that:
  - Defines **which files (modules) exist**.
  - For each file, provides the **list of hook entries** (name + stats) to show in that file’s table.
- **Containers:** One container per file. Each container holds:
  - File (module) name (e.g. `"kernel32.dll"`).
  - List of hook entries: `(hook_display_name, total_calls, unsuppressed_calls)` — or references into existing `g_hook_stats` + `g_hook_info` so we don’t duplicate storage.
- **Options:**
  - **View-only manager:** No new storage. Manager has a mapping DllGroup → canonical module name; UI asks manager for “all module names” and “all (hook_index, stats) for this module”. Existing `g_hook_stats` and `g_hook_info` remain the source of truth.
  - **Owning manager:** Manager owns `map<module_name, vector<HookEntry>>` with its own counters. Hooks call into the manager to record (e.g. `HookStatisticsManager::RecordCall(module_name, hook_name, suppressed)`). Allows hooks from other modules (e.g. `hid.dll`) that are not in the central `g_hook_info` to participate without touching `windows_message_hooks`.

Recommendation: Move to **owning** manager with **one array per file** as the source of truth; use category (DllGroup or display section) only when rendering the UI.

### 3. Canonical module name mapping

- Add a function that maps (DllGroup or hook_index) → **canonical module name**:
  - USER32, DISPLAY_SETTINGS → `"user32.dll"`
  - KERNEL32, HID_API → `"kernel32.dll"`
  - XINPUT1_4 → `"xinput1_4.dll"`
  - DINPUT8 → `"dinput8.dll"`
  - DINPUT → `"dinput.dll"`
  - OPENGL → `"opengl32.dll"`
- Future: `hid.dll` when we have hooks that report as that module.

### 4. Display: one file, optional categories/sections

- **Current:** Loop over DllGroups → for each group, one collapsible header and one table.
- **Target:** Loop over **canonical module names** (one per file). For each file:
  - One collapsible header (e.g. "kernel32.dll", "user32.dll").
  - **Display is allowed to split into categories/sections** within that file, e.g.:
    - **kernel32.dll:** "Kernel" section (Sleep, SleepEx, …), "HID" section (ReadFile, CreateFileA, CreateFileW, …).
    - **user32.dll:** "Input" section (GetMessage, GetCursorPos, …), "Display settings" section (ChangeDisplaySettings, SetWindowPos, …).
  - So the user still sees logical groupings (HID, display_settings, etc.), but the **underlying storage** is one array per file.
- Each section can be a sub-header + table, or one table with section labels; implementation choice.
- Optional: In the header for a module, show aggregated totals (sum of total_calls, sum of unsuppressed_calls, suppression %) for the whole file.

### 5. One InstallXXXHooks per DLL

- **Convention:** For each DLL (file) that we hook, there is **exactly one** install function, e.g. `InstallKernel32Hooks()`, `InstallUser32Hooks()`, `InstallHidHooks()`, `InstallXInputHooks()`, etc.
- **Benefits:**
  - **Single place:** All hooks for that DLL (Sleep, ReadFile, CreateFileA, CreateFileW for kernel32; GetMessage, ClipCursor, ChangeDisplaySettings for user32) are installed from one function. No scattered `InstallWindowsMessageHooks()` + `InstallHIDSuppressionHooks()` both touching kernel32.
  - **Avoid double hooking:** The install function can guard with “already installed for this DLL” and ensure MinHook (or similar) is used once per target; no risk of two call sites both installing hooks into the same module.
  - **Simpler call graph:** Addon init (or the central hook coordinator) calls one function per DLL instead of multiple feature-specific installers that each hook several DLLs.
- **Migration:** Today we have e.g. `InstallWindowsMessageHooks()` (user32 + more), `InstallHIDSuppressionHooks()` (kernel32 ReadFile/CreateFile). Target: `InstallUser32Hooks()`, `InstallKernel32Hooks()`, etc., where each installs every hook for that DLL and registers with the Hook Statistics Manager (one array per file). Feature logic (HID suppression, sleep hook, etc.) stays in their own files; only the **installation** is centralized per DLL.

### 6. Registration (optional, for owning manager)

- If we move to an owning manager:
  - `RegisterModule("hid.dll")` — ensure a container exists for that file.
  - `RegisterHook("hid.dll", "HidD_GetInputReport", &stats)` or `RecordCall("hid.dll", "HidD_GetInputReport", suppressed)` so hooks outside `g_hook_info` can contribute.
- HID suppression (and similar) would then update the manager instead of (or in addition to) `g_hook_stats` when we add hooks for `hid.dll`.

## Implementation Plan

### Phase 1: One table per file (view-only)

1. **Canonical module name**
   - Add `GetCanonicalModuleName(DllGroup)` or `GetCanonicalModuleName(int hook_index)` returning e.g. `"kernel32.dll"`, `"user32.dll"`, etc., using the mapping above.
   - Add `GetUniqueModuleNames()` (or equivalent) that returns the list of canonical module names that have at least one hook (for stable UI order, use a fixed order: user32, kernel32, xinput1_4, dinput8, dinput, opengl32; extend with hid.dll when needed).

2. **Hook Statistics Manager (thin layer)**
   - New file(s): e.g. `hook_statistics_manager.hpp` / `hook_statistics_manager.cpp` in a suitable place (e.g. under `hooks/` or `ui/`).
   - Manager exposes:
     - `GetModuleNames()` → list of canonical module names (one per file).
     - `GetHooksForModule(const char* module_name)` → list of (hook_index or name + total_calls, unsuppressed_calls) for that module. Implementation: iterate hook indices, filter by `GetCanonicalModuleName(GetHookDllGroup(i)) == module_name`, return references to existing `g_hook_stats` / `g_hook_info`.

3. **UI**
   - In `hook_stats_tab.cpp`: replace “loop over DLL_GROUPS” with “loop over HookStatisticsManager::GetModuleNames()”. For each module name, create **one** collapsible section and **one** table; rows = `GetHooksForModule(module_name)`.
   - Optional: header line for each section showing module-wide total calls and suppression %.

4. **Reset**
   - “Reset All Statistics” continues to call existing `ResetAllHookStats()` (no change in behavior).

5. **One Install per DLL (migration)**
   Introduce per-DLL install functions (e.g. `InstallKernel32Hooks()`, `InstallUser32Hooks()`); each installs every hook for that DLL and guards against double install. Refactor call sites so the central coordinator calls one install function per DLL instead of multiple feature-based installers that touch the same module.

### Phase 2 (optional): Owning manager and extra modules

1. **Storage in manager — one array per file**
   - Manager holds **one array per file** (e.g. `map<string, vector<HookCallStats>>` or one `vector`/`array` per module). All hooks for kernel32.dll live in that one kernel32 array; categories (HID, kernel, …) are display-only (e.g. each entry has a `category` or `dll_group` field for UI grouping). Hooks record into the manager by (module_name, index or name); we migrate from `g_hook_stats` to this layout so the single source of truth is per-file arrays.

2. **Registration API**
   - `RegisterModule(module_name)` — ensure container exists.
   - `RecordCall(module_name, hook_name, bool suppressed)` or per-hook stats handle — so e.g. `hid.dll` hooks can register and be shown in a dedicated "hid.dll" table without being in `g_hook_info`.

3. **hid.dll**
   - When adding hooks for `hid.dll`, register the module and hooks with the manager so "hid.dll" appears as one table with one row per hooked function.

## Files to Touch (Phase 1)

- Add: `src/addons/display_commander/hooks/hook_statistics_manager.hpp` (and optionally `.cpp`) — canonical module mapping and GetModuleNames / GetHooksForModule.
- Change: `src/addons/display_commander/ui/new_ui/hook_stats_tab.cpp` — iterate by module name, one table per module.
- Optionally change: `windows_message_hooks.cpp` / `.hpp` — only if we move the canonical mapping there; otherwise keep it in the new manager.

## Summary

- **Storage:** All hooks for a file (e.g. kernel32.dll) are stored in **one array** (raw data). Manager has one container/array per file.
- **Display:** OK to show **separate categories/sections** (e.g. HID section of kernel32.dll, display_settings section of user32.dll) in the UI; categories are for presentation only, not for splitting storage.
- **Installation:** **One `InstallXXXHooks()` per DLL** — e.g. `InstallKernel32Hooks()`, `InstallUser32Hooks()`. Simplifies hooking and avoids hooking the same DLL twice; all hooks for that module are installed from a single function.
- **Manager:** One array per file; optional category/section metadata per entry for display grouping.
- **UI:** One file → one collapsible block; inside it, one table or multiple sub-sections (HID, display_settings, kernel, etc.) as needed.

This keeps storage simple (one array per file), installation clear (one install function per DLL), and UI flexible (categories/sections per file).

---

## Task list (do in order)

- [x] **Task 1 — Canonical module name**
  Add `GetCanonicalModuleName(DllGroup)` (and optionally `GetCanonicalModuleName(int hook_index)`). Map USER32/DISPLAY_SETTINGS → `"user32.dll"`, KERNEL32/HID_API → `"kernel32.dll"`, XINPUT1_4 → `"xinput1_4.dll"`, DINPUT8 → `"dinput8.dll"`, DINPUT → `"dinput.dll"`, OPENGL → `"opengl32.dll"`. Place in a new file or in `windows_message_hooks` (decide and add there).

- [x] **Task 2 — Hook Statistics Manager (view-only)**
  Add `hook_statistics_manager.hpp` (and `.cpp` if needed). Expose `GetModuleNames()` (fixed order: user32, kernel32, xinput1_4, dinput8, dinput, opengl32) and `GetHooksForModule(module_name)` returning hook entries (name + total_calls, unsuppressed_calls) for that module by iterating hook indices and filtering with `GetCanonicalModuleName(GetHookDllGroup(i)) == module_name`. Use existing `g_hook_stats` / `g_hook_info`; no new storage yet.

- [x] **Task 3 — UI: one table per file**
  In `hook_stats_tab.cpp`, replace the loop over `DLL_GROUPS` with a loop over `GetModuleNames()`. For each module name, one collapsible header and one table; rows from `GetHooksForModule(module_name)`. Keep "Reset All Statistics" calling `ResetAllHookStats()`. Verify kernel32 shows one table (Sleep, ReadFile, CreateFileA, …) and user32 one table (GetMessage, …, ChangeDisplaySettings, …).

- [x] **Task 4 — Optional: display categories per file**
  Within each file’s section, group rows by category (e.g. kernel32: "Kernel" vs "HID"; user32: "Input" vs "Display settings") using DllGroup or a category field. Sub-headers or one table with section labels — implementation choice.

- [x] **Task 5 — One Install per DLL: kernel32**
  Add `InstallKernel32Hooks()` that installs all kernel32 hooks (Sleep, SleepEx, … from windows_message_hooks + ReadFile, CreateFileA, CreateFileW from HID suppression). Guard with "already installed" (static/atomic). Do not remove existing detour code; only centralize the **call** to create/enable hooks for kernel32 in this one function. Find current call sites that install kernel32 hooks and make them call `InstallKernel32Hooks()` instead (or have the central coordinator call it once).

- [x] **Task 6 — One Install per DLL: user32**
  Add `InstallUser32Hooks()` that installs all user32 hooks (messages, input, display settings — everything currently in `InstallWindowsMessageHooks()` that targets user32). Guard against double install. Refactor so user32 is only installed from this function.

- [x] **Task 7 — One Install per DLL: remaining modules**
  Add `InstallXInputHooks()`, `InstallDInput8Hooks()`, `InstallDInputHooks()`, `InstallOpenGLHooks()` (or equivalent names). Each installs every hook for that DLL and guards against double install. Refactor call sites so each DLL is installed from exactly one function. (Already satisfied: `InstallXInputHooks`, `InstallDirectInput8Hooks`, `InstallDirectInputHooks`, `InstallOpenGLHooks` exist with guards; installed via main_entry/loadlibrary/swapchain/widget and OnModuleLoaded respectively.)

- [x] **Task 8 — Central hook coordinator**
  Ensure addon init (or the single place that installs hooks) calls exactly one install function per DLL: `InstallUser32Hooks()`, `InstallKernel32Hooks()`, `InstallXInputHooks()`, etc. Remove or deprecate broad installers that span multiple DLLs (e.g. `InstallWindowsMessageHooks()` as the only entry for user32; kernel32 only via `InstallKernel32Hooks()`). Coordinator in api_hooks now calls `InstallUser32Hooks()` and `InstallKernel32Hooks()`; kernel32 also installs exception hooks via `InstallKernel32ExceptionHooks()`. `InstallWindowsMessageHooks()` kept for legacy but only installs user32; XInput/DInput/OpenGL remain on-demand via loadlibrary/main_entry.

- [x] **Task 9 — Phase 2 (optional): owning manager**
  Move stats storage to the manager: one array per file (e.g. `map<string, vector<HookCallStats>>`). Hooks record via manager (e.g. by module_name + slot). Migrate from `g_hook_stats` to this layout; Reset updates manager. UI continues to use manager API. Done: `hook_call_stats.hpp` for shared type; manager owns `g_module_stats` (map module → vector<HookCallStats>), lazy init, RecordHookTotal/RecordHookUnsuppressed/GetHookStats/ResetAllHookStats; all detours use manager; GetHookStats/ResetAllHookStats moved to manager.

- [x] **Task 10 — Phase 2 (optional): hid.dll and registration**
  Add `RegisterModule("hid.dll")` and a way for hid.dll hooks to record stats. Show "hid.dll" in the Hook Statistics UI when that module is registered and has hooks. Done: manager has `RegisterModule(module_name, hook_names)`, `RecordHookTotal(module_name, slot)`, `RecordHookUnsuppressed(module_name, slot)`, and `IsRegisteredModule` / `GetRegisteredModuleSlotCount` / `GetRegisteredModuleSlotName` / `GetRegisteredModuleSlotStats`. `InstallHIDSuppressionHooks()` calls `RegisterModule("hid.dll", {"HidD_GetInputReport", "HidD_GetAttributes"})`. HidD_* detours record stats by slot. UI shows registered modules in the same table layout; summary includes registered module stats. Reset resets registered stats.
