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
| **Unused header files** | 0 | Every header is included somewhere. One header belongs to a dead module (display_settings_debug_tab). |
| **Dead modules / functions** | 0 (was 2) | **(Removed)** DrawDisplaySettingsDebugTab module; dead `ParseDisplayNumberFromDeviceIdUtf8()` in display_cache.cpp. |
| **Orphaned / disabled code** | 0 (was 2) | **(Removed)** (1) `#if 0` block in dinput_hooks.cpp. (2) `#ifdef TRY_CATCH_BLOCKS` blocks in main_entry.cpp and continuous_monitoring.cpp. |
| **Unused CMake dependencies** | 0 | No unused libs or include dirs identified. |

**Done (follow-up run):** (1) Display Settings Debug tab removed; (2) `#if 0` block in dinput_hooks.cpp removed; (3) TRY_CATCH_BLOCKS blocks removed (macro never defined); (4) dead `ParseDisplayNumberFromDeviceIdUtf8()` in display_cache.cpp removed. **Kept by design:** Excluded sources (cli_ui_exe, imgui_wrapper_reshade.cpp) and platform/feature `#ifdef`s (_WIN64, EXPERIMENTAL_FEATURES, etc.).

---

## Notes

- **Safety:** Before removing any file or symbol, run a full build and (if available) tests. Prefer one PR per category (e.g. “remove unused headers”) to simplify review.
- **Project rules:** Respect `.cursor/rules` (e.g. no `std::mutex`, Reshade/ImGui patterns). Do not remove code that is required for Special-K compatibility or documented optional features unless explicitly agreed.
- **Submodules:** Do not treat external submodule code (e.g. reshade) as “dead” just because we don’t call it; only consider our addon’s own sources and our use of external APIs.
