# Plan: Migrate All Tooltips to SetTooltipEx

## Goal
Replace every `imgui.SetTooltip(...)` call with `imgui.SetTooltipEx(...)` so all tooltips use a default max width (word-wrap). This improves readability by preventing very wide single-line tooltips.

## Current State
- **SetTooltip**: ImGui wrapper method; no wrap width → tooltips can be arbitrarily wide.
- **SetTooltipEx**: Already implemented in `imgui_wrapper_base.hpp` with:
  - `SetTooltipEx(const char* fmt, ...)` → uses **default wrap width 800px**.
  - `SetTooltipEx(float wrap_width, const char* fmt, ...)` → optional custom width.
- ReShade/standalone implementations use `PushTextWrapPos(wrap_width)` + `TextV` in `SetTooltipExV`.

## Change Required
- **Code change**: Replace `SetTooltip(` with `SetTooltipEx(` everywhere. No argument changes needed; the `(fmt, ...)` signature is identical. Default 800px will apply for all migrated calls.
- **Wrapper**: No changes to `imgui_wrapper_base.hpp`, `imgui_wrapper_reshade.hpp`, or `imgui_wrapper_standalone.cpp` — API already supports the migration.

## Files to Update (by number of `SetTooltip` calls)

| File | Count | Notes |
|------|-------|--------|
| `ui/new_ui/main_new_tab.cpp` | 269 | Already has many SetTooltipEx; convert remaining SetTooltip |
| `ui/new_ui/experimental_tab.cpp` | 120 | |
| `ui/new_ui/advanced_tab.cpp` | 102 | One SetTooltipEx already at 984 |
| `widgets/xinput_widget/xinput_widget.cpp` | 41 | Some SetTooltipEx already |
| `ui/nvidia_profile_tab_shared.cpp` | 21 | |
| `ui/new_ui/games_tab.cpp` | 18 | |
| `ui/new_ui/addons_tab.cpp` | 15 | |
| `widgets/resolution_widget/resolution_widget.cpp` | 13 | |
| `ui/new_ui/swapchain_tab.cpp` | 12 | |
| `autoclick/autoclick_manager.cpp` | 10 | |
| `widgets/dualsense_widget/dualsense_widget.cpp` | 10 | |
| `ui/new_ui/vulkan_tab.cpp` | 9 | |
| `ui/new_ui/hotkeys_tab.cpp` | 9 | |
| `widgets/remapping_widget/remapping_widget.cpp` | 7 | |
| `ui/new_ui/updates_tab.cpp` | 6 | |
| `ui/cli_standalone_ui.cpp` | 5 | |
| `ui/new_ui/settings_wrapper.cpp` | 5 | |
| `ui/new_ui/window_info_tab.cpp` | 4 | |
| `ui/new_ui/performance_tab.cpp` | 3 | |
| `ui/monitor_settings/monitor_settings.cpp` | 3 | |
| `ui/new_ui/streamline_tab.cpp` | 1 | |

**Total:** ~673 call sites across 21 files (all under `src/addons/display_commander/`).

## Execution Strategy

1. **Per-file replace**  
   In each file: replace `imgui.SetTooltip(` with `imgui.SetTooltipEx(`.  
   - No need to add a width argument unless a specific tooltip should use a different max width (then use `SetTooltipEx(width, fmt, ...)`).

2. **Order of work (optional batching)**  
   - **Batch 1 (high count):** `main_new_tab.cpp`, `experimental_tab.cpp`, `advanced_tab.cpp`.  
   - **Batch 2:** Remaining `ui/new_ui/*.cpp`, then `ui/*.cpp`, then widgets and autoclick.

3. **Verification**  
   - After migration: `grep -r "\.SetTooltip(" src/addons/display_commander --include="*.cpp"` should return no matches (only `SetTooltipEx` and possibly base class `SetTooltip` declaration in headers).  
   - Build and quick UI smoke test: open tabs that use tooltips and hover a few controls to confirm tooltips show and wrap at ~800px.

4. **Optional follow-up**  
   - If desired, deprecate or remove `SetTooltip` from the wrapper so new code cannot use it; not required for this migration.

## Risk / Notes
- **Low risk:** Same variadic signature; only the function name changes.  
- **Edge cases:** Any dynamic or very long tooltip text will now wrap at 800px instead of one long line — intended.  
- **Custom width:** For any tooltip that should be narrower/wider, keep using `SetTooltipEx(wrap_width, fmt, ...)` (already used in a few places).

## Changelog
- Add an entry under the next version, e.g.: **Tooltips:** All UI tooltips now use `SetTooltipEx` with a default max width for better readability (word-wrap at 800px). Details: migrated ~673 `SetTooltip` calls across 21 files.

---

## Execution completed
- **Done:** All 21 files updated; every `imgui.SetTooltip(` replaced with `imgui.SetTooltipEx(` (~673 calls). Grep on `\.SetTooltip\(` in `*.cpp` returns no matches.
- **CHANGELOG:** Entry added under v0.12.404 (unreleased).
- **Build:** Verify with a build from Visual Studio or a Developer Command Prompt (include paths must resolve); then do a quick UI smoke test (hover tooltips to confirm wrap at ~800px).
