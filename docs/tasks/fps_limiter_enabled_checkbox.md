# FPS Limiter: Replace "Disabled" Mode with Enable Checkbox

## Overview

Replace the **Disabled** option in the FPS limiter mode combo with a separate **FPS limiter enabled** checkbox (default **enabled**). The combo will only control *which* limiter mode is used (Default / Reflex / Latent Sync); the checkbox controls *whether* any limiter is active.

- **No migration**: Existing configs are not migrated. Users who had "Disabled" (saved as 2) will need to use the new checkbox after the change.
- **kDisabled is removed** from `FpsLimiterMode`; the enum will have three values only.

---

## Current State

- **Enum** (`globals.hpp`): `FpsLimiterMode::kOnPresentSync = 0`, `kReflex = 1`, `kDisabled = 2`, `kLatentSync = 3`.
- **UI** (`main_new_tab.cpp`): Combo with 4 items: "Default", "NVIDIA Reflex...", "Disabled", "Sync to Display Refresh Rate...".
- **Settings** (`main_tab_settings`): `fps_limiter_mode` (ComboSetting, default 0). Settings constructor uses 5 labels (separate from UI); the UI uses its own 4-item array.
- **Logic**: `s_fps_limiter_mode` is read in `swapchain_events.cpp`, `main_new_tab.cpp`, `main_entry.cpp`, `continuous_monitoring.cpp`, `nvapi_hooks.cpp`, etc. "Disabled" is treated as "no limiting"; Reflex config when disabled uses `reflex_disabled_limiter_mode`.

---

## Target State

1. **New setting**: `fps_limiter_enabled` (bool, default **true**). Persisted; synced to an atomic for hot-path reads if desired.
2. **Enum**: Remove `kDisabled`. Renumber so `kLatentSync = 2`:
   - `FpsLimiterMode::kOnPresentSync = 0`, `kReflex = 1`, `kLatentSync = 2`.
3. **Combo**: Three options only (Default, Reflex, Sync to Display Refresh Rate). Stored value 0, 1, or 2.
4. **Config value clamp** (no full migration): When loading `fps_limiter_mode`, if value is `3` (old LatentSync), treat as `2` so existing LatentSync users keep working.
5. **Logic**: Everywhere that currently checks `mode == FpsLimiterMode::kDisabled` instead checks "FPS limiter is disabled" via the new checkbox (e.g. `!s_fps_limiter_enabled` or a helper `IsFpsLimiterEnabled()`).

---

## Implementation Plan (Order of Work)

### 1. Add setting and atomic (no behavior change yet)

- **main_tab_settings.hpp**: Add `ui::new_ui::BoolSetting fps_limiter_enabled;`.
- **main_tab_settings.cpp**:
  - Add `fps_limiter_enabled("fps_limiter_enabled", true, "DisplayCommander")` (default **true**).
  - Add to `all_settings_` so it loads/saves.
- **globals.hpp**: Add `extern std::atomic<bool> s_fps_limiter_enabled;` (or equivalent).
- **globals.cpp**: Define `std::atomic<bool> s_fps_limiter_enabled{true};`.
- **main_new_tab.cpp** (settings load): When loading settings, set `s_fps_limiter_enabled.store(settings::g_mainTabSettings.fps_limiter_enabled.GetValue())`.
- No UI yet; no enum change yet.

### 2. Remove kDisabled from enum and renumber

- **globals.hpp**: Change enum to `FpsLimiterMode : std::uint8_t { kOnPresentSync = 0, kReflex = 1, kLatentSync = 2 }`. Remove `kDisabled`.
- **globals.cpp**: Default `s_fps_limiter_mode` to `FpsLimiterMode::kOnPresentSync` (already 0). Update comment for FPS limiter mode.

### 3. Clamp stored combo value when loading

- When reading `fps_limiter_mode` from settings (in main_new_tab load and anywhere else that applies the value): if value is `3`, use `2` (LatentSync) so old configs with LatentSync still work. Optionally clamp to `[0, 2]` so any stray value is safe.

### 4. Update FPS limiter mode combo in settings

- **main_tab_settings.cpp**: Update `fps_limiter_mode` constructor to **3** options only (remove "Disabled"), e.g. "Default" / "Reflex (low latency)" / "Sync to Display Refresh Rate..." (align labels with current UI style). Default remains 0.

### 5. UI: checkbox + 3-item combo

- **main_new_tab.cpp** – `DrawDisplaySettings_FpsLimiterMode()`:
  - Add a **checkbox** before the combo: label e.g. "FPS limiter enabled", bound to `fps_limiter_enabled`. On change, update setting and `s_fps_limiter_enabled`.
  - Change combo to **3 items**: "Default", "NVIDIA Reflex (DX11/DX12 only, Vulkan requires native reflex)", "Sync to Display Refresh Rate (fraction of monitor refresh rate) Non-VRR". Remove "Disabled".
  - Combo `current_item` is 0, 1, or 2. On change, set `fps_limiter_mode` and `s_fps_limiter_mode` (store as `static_cast<FpsLimiterMode>(current_item)`).
  - Remove the branch that logs "FPS Limiter: Disabled (no limiting)" when mode was kDisabled; when checkbox is unchecked, log or show that limiter is disabled there if desired.
  - Tooltip: update to describe that the checkbox enables/disables the limiter and the combo selects the mode.

### 6. Replace all "disabled" behavior with checkbox

- **swapchain_events.cpp**:
  - `GetEffectiveReflexMode()`: Use `reflex_disabled_limiter_mode` when **limiter is disabled** (checkbox off) **or** mode is `kLatentSync`. So: if `!s_fps_limiter_enabled.load()` or `s_fps_limiter_mode.load() == FpsLimiterMode::kLatentSync`, use `reflex_disabled_limiter_mode`; else use mode-specific Reflex setting.
  - `HandleFpsLimiterPre()` switch: Remove `case FpsLimiterMode::kDisabled`. At the start of the function (or before the switch), if `!s_fps_limiter_enabled.load()` then skip limiting (same behavior as current kDisabled) and return/break as appropriate.
- **main_new_tab.cpp**:
  - Replace every `current_item == static_cast<int>(FpsLimiterMode::kDisabled)` with a check that the limiter is disabled (e.g. `!s_fps_limiter_enabled.load()`). In particular, the "Reflex config when FPS limiter is Disabled or LatentSync" block should show when **checkbox is off** OR mode is LatentSync.
  - Replace `s_fps_limiter_mode.load() == FpsLimiterMode::kDisabled` with `!s_fps_limiter_enabled.load()` where the intent is "limiter off".
  - `fps_limit_enabled` (overlay / logic): `fps_limit_enabled = s_fps_limiter_enabled.load() && (s_fps_limiter_mode.load() != FpsLimiterMode::kLatentSync)` (or equivalent: enabled and not LatentSync for that specific overlay meaning, if that’s the current semantics). Confirm semantics: currently `fps_limit_enabled` is false for kDisabled and kLatentSync; so new logic: false when checkbox off or mode is kLatentSync.
- **main_entry.cpp**: The safe-mode / Streamline loading block that sets `fps_limiter_mode` to `kDisabled`: instead set **fps_limiter_enabled** to false (and optionally leave mode as 0). So: `settings::g_mainTabSettings.fps_limiter_enabled.SetValue(false)` and sync `s_fps_limiter_enabled` if used; remove the `fps_limiter_mode.SetValue((int)FpsLimiterMode::kDisabled)` line.

### 7. Other references

- **continuous_monitoring.cpp**: Uses `fps_limiter_mode.GetValue() == FpsLimiterMode::kReflex`; no kDisabled change needed, only ensure enum value for kReflex remains 1.
- **nvapi_hooks.cpp**: Compares `fps_limiter_mode.GetValue() == static_cast<int>(FpsLimiterMode::kReflex)`; same.
- **standalone_ui_settings_bridge.cpp**: Returns `fps_limiter_mode.GetValue()`; no kDisabled, just ensure valid 0/1/2.
- **main_tab_settings.hpp** comment: Update "Used when FPS limiter is Disabled or LatentSync" to "Used when FPS limiter is off (checkbox unchecked) or mode is LatentSync".

### 8. Comments and tooltips

- Update any comments that still say "0 = Disabled" or list kDisabled (e.g. in globals.hpp/cpp, main_new_tab.cpp tooltips).
- Ensure "Frame Time: Not set (FPS limiter disabled?)" and similar messages still make sense (they refer to limiter being off, which is now the checkbox).

---

## Files to Touch (Summary)

| File | Changes |
|------|--------|
| `globals.hpp` | Remove `kDisabled`, set `kLatentSync = 2`; add `s_fps_limiter_enabled` if used. |
| `globals.cpp` | Define `s_fps_limiter_enabled`; default `s_fps_limiter_mode` to kOnPresentSync; update comment. |
| `settings/main_tab_settings.hpp` | Add `fps_limiter_enabled`; update comment for `reflex_disabled_limiter_mode`. |
| `settings/main_tab_settings.cpp` | Add `fps_limiter_enabled` ctor + `all_settings_`; `fps_limiter_mode` 3 options only. |
| `ui/new_ui/main_new_tab.cpp` | Checkbox + 3-item combo; clamp mode 3→2 on load; replace kDisabled checks with checkbox; sync `s_fps_limiter_enabled` on load and on checkbox change. |
| `swapchain_events.cpp` | GetEffectiveReflexMode use checkbox; HandleFpsLimiterPre skip when checkbox off, remove kDisabled case. |
| `main_entry.cpp` | Set `fps_limiter_enabled` to false instead of `fps_limiter_mode` to kDisabled. |

---

## Verification

- With checkbox **on** and mode Default/Reflex/LatentSync: behavior unchanged from current non-Disabled modes.
- With checkbox **off**: no FPS limiting (same as current Disabled); Reflex when "limiter off" uses `reflex_disabled_limiter_mode`.
- New installs: checkbox default on, mode default 0 (OnPresentSync).
- Existing config with `fps_limiter_mode == 3`: after load, treated as 2 (LatentSync).
- Existing config with `fps_limiter_mode == 2` (old Disabled): no migration; user sees mode 2 = LatentSync until they uncheck the new checkbox (acceptable per "don't migrate").
