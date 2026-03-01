# Split OverrideReShadeSettings – Task Plan

## Overview

`OverrideReShadeSettings()` in `src/addons/display_commander/main_entry.cpp` is **221 lines** (L701–921). This task plans how to split it into four smaller helpers so the main function is a short orchestration plus final log.

**Location:** `main_entry.cpp` L701–921.

---

## Current Structure (Logical Sections)

| Section | Approx. lines | Description |
|--------|----------------|-------------|
| Early return & log | 701–705 | if (!g_reshade_loaded) return; LogInfo. |
| Window config | 710–771 | Read OVERLAY::Window (get_config_value twice for size then data); add [Window][DC] and [Window][RenoDX] if not present (comma-to-null conversion); set_config_value if changed. |
| Tutorial & updates | 773–785 | set_config_value TutorialProgress=4, CheckForUpdates=0; if suppress_reshade_clock then ShowClock=0. |
| LoadFromDllMain once | 787–808 | get_config_value LoadFromDllMainSetOnce; if !set: get ReShade LoadFromDllMain (log), set DisplayCommander LoadFromDllMainSetOnce, save_config, log; else log skip. |
| Display Commander paths | 810–918 | SHGetFolderPathW LocalAppData, build shaders_dir/textures_dir; create_directories for both; addPathToSearchPaths lambda (read section/key, parse null-term list, normalize path, case-insensitive compare, append and set_config_value); add EffectSearchPaths and TextureSearchPaths. |
| Final log | 920–921 | LogInfo "ReShade settings override completed successfully". |

---

## Proposed Helper Functions

All helpers are in the same TU as `OverrideReShadeSettings` (or a dedicated `main_entry_reshade_settings.cpp`). They assume `g_reshade_loaded` has already been checked by the caller.

| # | Proposed name | Responsibility |
|---|----------------|----------------|
| 1 | `OverrideReShadeSettings_WindowConfig()` | Read OVERLAY::Window into std::string (size then data); if [Window][DC] missing, append with comma-to-null encoding; if [Window][RenoDX] missing, append same way; if changed, set_config_value. |
| 2 | `OverrideReShadeSettings_TutorialAndUpdates()` | set_config_value OVERLAY::TutorialProgress = 4; GENERAL::CheckForUpdates = 0; if settings::g_reshadeTabSettings.suppress_reshade_clock then OVERLAY::ShowClock = 0. |
| 3 | `OverrideReShadeSettings_LoadFromDllMainOnce()` | get_config_value DisplayCommander::LoadFromDllMainSetOnce; if false: get ReShade ADDON::LoadFromDllMain (log), set DisplayCommander LoadFromDllMainSetOnce = true, save_config, log; else log "already set, skipping". |
| 4 | `OverrideReShadeSettings_AddDisplayCommanderPaths()` | Get LocalAppData; build dc_base_dir, shaders_dir, textures_dir; create_directories for both (with error logging); define addPathToSearchPaths lambda (unchanged logic: read paths, normalize, case-insensitive compare, append + set_config_value); call it for EffectSearchPaths and TextureSearchPaths. |

The lambda `addPathToSearchPaths` (and optionally `normalizeForComparison`) can stay inside `OverrideReShadeSettings_AddDisplayCommanderPaths` to avoid exposing ReShade config format details.

---

## Resulting OverrideReShadeSettings()

```cpp
void OverrideReShadeSettings() {
    if (!g_reshade_loaded.load())
        return;
    LogInfo("Overriding ReShade settings - Setting tutorial as viewed and disabling auto updates");

    OverrideReShadeSettings_WindowConfig();
    OverrideReShadeSettings_TutorialAndUpdates();
    OverrideReShadeSettings_LoadFromDllMainOnce();
    OverrideReShadeSettings_AddDisplayCommanderPaths();

    LogInfo("ReShade settings override completed successfully");
}
```

(~15 lines)

---

## Recommended Order of Extraction

1. **OverrideReShadeSettings_AddDisplayCommanderPaths** – Largest block; move the whole LocalAppData + create_directories + addPathToSearchPaths block. Easiest to test in isolation (run addon, check EffectSearchPaths/TextureSearchPaths in ReShade.ini).
2. **OverrideReShadeSettings_WindowConfig** – Self-contained; verify overlay docking still works.
3. **OverrideReShadeSettings_TutorialAndUpdates** – Trivial.
4. **OverrideReShadeSettings_LoadFromDllMainOnce** – Depends on DisplayCommander config; verify first-run vs subsequent run behavior.

---

## Optional: Move to Separate TU

If you want to shrink main_entry.cpp further, move `OverrideReShadeSettings` and all four helpers to e.g. `main_entry_reshade_settings.cpp` and add a declaration in a header included by main_entry.cpp (e.g. `void OverrideReShadeSettings();` in main_entry.hpp or a small reshade_settings.hpp). Ensure the new TU has access to g_reshade_loaded, settings::g_reshadeTabSettings, and display_commander::config::*.

---

## Verification

- With ReShade loaded: run once, check ReShade.ini for Window (DC/RenoDX), TutorialProgress, CheckForUpdates, ShowClock, EffectSearchPaths, TextureSearchPaths.
- With suppress_reshade_clock on: ShowClock = 0.
- LoadFromDllMainSetOnce: first run sets and saves; second run skips (log message).
- No behavior change to ReShade or Display Commander behavior.
