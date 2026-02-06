# Split DrawDisplaySettings – Task Plan

## Overview

`DrawDisplaySettings(reshade::api::effect_runtime* runtime)` in `src/addons/display_commander/ui/new_ui/main_new_tab.cpp` is **~1520 lines** (lines 2033–3553). This task plans how to split it into smaller, focused helper functions for maintainability and readability.

**Location:** `main_new_tab.cpp` L2033–L3553 (before `DrawAudioSettings()`).

---

## Current Structure (Logical Sections)

| Section | Approx. lines | Description |
|--------|----------------|-------------|
| **1. Display & target** | 2033–2135 | Runtime assert, display cache, selected index, render resolution + bit depth + refresh rate line, Target Display combo |
| **2. Window mode & apply** | 2137–2222 | Window Mode combo, Aspect Ratio / Width / Alignment / Black Curtain (when Aspect Ratio), ADHD Multi-Monitor, Apply Changes button |
| **3. FPS Limiter mode** | 2214–2662 | FPS Limiter Mode combo, OnPresentSync (Reflex checkbox, Display/Input Ratio, Debug window), Reflex status (native/injected), Boost/PCL/Suppress Native/Suppress Sleep, PCL for OnPresent without Reflex, Experimental FG/Safe Mode, Limit Real Frames indicator |
| **4. Latent Sync** | 2664–2662 | Scanline Offset, VBlank Sync Divisor, VBlank Monitor Status (only when Latent Sync) |
| **5. Experimental Limit Real Frames** | 2664–2684 | Checkbox when experimental features enabled |
| **6. FPS & background** | 2685–2737 | FPS Limit slider, No Render in Background, No Present in Background |
| **7. DLSS** | 2738–2745 | CollapsingHeader "DLSS Information" → `DrawDLSSInfo()` (already extracted) |
| **8. VSync & Tearing** | 2746–3552 | Quick FPS Limit Changer, Background FPS Limit, CollapsingHeader "VSync & Tearing": Force VSync ON/OFF, Prevent Tearing, backbuffer count, Flip Chain, D3D9 Flip; restart notice; **Current Present Mode** (D3D9/DXGI present mode, flip state, Discord overlay, PresentMon, surface tooltip, swapchain debug tooltip with window/style/flip explanations, PresentMon ETW subsection, present flags) |

The largest single block is **VSync & Tearing** (especially the “Current Present Mode” and tooltip content), which is a good candidate to extract first.

---

## Proposed Helper Functions

Helper functions should live in `main_new_tab.cpp` (or a dedicated `display_settings_ui.cpp` if we want to move display-setting UI out of the main file). Signatures are `void Foo(...)`; they only need `runtime` or globals/settings already in scope.

| # | Proposed name | Responsibility | Approx. size |
|---|----------------|-----------------|--------------|
| 1 | `DrawDisplaySettings_DisplayAndTarget()` | Display list, selected index, render resolution + bit depth + refresh rate line, Target Display combo. May take `runtime` or nothing (uses globals + display_cache + settings). | ~100 lines |
| 2 | `DrawDisplaySettings_WindowModeAndApply()` | Window Mode, Aspect Ratio, Width, Alignment, Black Curtain, ADHD Multi-Monitor, Apply Changes button. | ~90 lines |
| 3 | `DrawDisplaySettings_FpsLimiterMode()` | FPS Limiter Mode combo and all mode-specific UI: OnPresentSync (Reflex, ratio, debug), Reflex status, Boost/PCL/Suppress, Latent Sync sliders + VBlank status, experimental options, Limit Real Frames indicator. | ~450 lines |
| 4 | `DrawDisplaySettings_FpsAndBackground()` | FPS Limit slider, Quick FPS Limit Changer, Background FPS Limit, No Render/No Present in Background. | ~95 lines |
| 5 | `DrawDisplaySettings_VSyncAndTearing()` | Entire "VSync & Tearing" block: checkboxes (VSync ON/OFF, Prevent Tearing, backbuffer, Flip Chain, D3D9 Flip), restart notice, Current Present Mode display and all tooltips (flip state, PresentMon, surface, swapchain debug, PresentMon ETW). | ~810 lines |

Optional further split of (5):

- `DrawDisplaySettings_VSyncTearingCheckboxes()` – only the checkboxes and restart notice.
- `DrawDisplaySettings_PresentModeAndFlipState()` – “Current Present Mode” label, present mode name/color, flip state, Discord overlay.
- `DrawDisplaySettings_PresentModeTooltip()` – the large `ImGui::BeginTooltip()` content (swapchain info, window rect, flip explanations, PresentMon ETW subsection, present flags).

---

## Recommended Order of Extraction

1. **VSync & Tearing** → `DrawDisplaySettings_VSyncAndTearing()` (and optionally the sub-helpers above).  
   - Reduces `DrawDisplaySettings` by the most lines and isolates the most complex UI (PresentMon, flip state, tooltips).

2. **FPS Limiter mode** → `DrawDisplaySettings_FpsLimiterMode()`.  
   - Second-largest block; self-contained (mode combo + all conditional UI).

3. **Display & target** → `DrawDisplaySettings_DisplayAndTarget()`.  
   - Clear boundary at the top; uses display_cache and settings.

4. **Window mode & apply** → `DrawDisplaySettings_WindowModeAndApply()`.  
   - Small, cohesive block.

5. **FPS & background** → `DrawDisplaySettings_FpsAndBackground()`.  
   - FPS sliders and background checkboxes in one place.

After all extractions, `DrawDisplaySettings` would look like:

```cpp
void DrawDisplaySettings(reshade::api::effect_runtime* runtime) {
    assert(runtime != nullptr);
    DrawDisplaySettings_DisplayAndTarget();
    DrawDisplaySettings_WindowModeAndApply();
    DrawDisplaySettings_FpsLimiterMode();
    DrawDisplaySettings_FpsAndBackground();

    if (ImGui::CollapsingHeader("DLSS Information", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();
        DrawDLSSInfo();
        ImGui::Unindent();
    }

    DrawDisplaySettings_VSyncAndTearing();
}
```

---

## Dependencies and State

- **Parameters:** Only `runtime` is passed in; it’s used for the assert and `native_device` (latter may be unused in the current body—verify before/after split).
- **Globals / statics used in DrawDisplaySettings:**  
  `g_game_render_width`, `g_game_render_height`, `g_last_swapchain_desc`, `g_last_reshade_device_api`, `g_last_swapchain_hwnd`, `g_swapchain_wrapper_present_called`, `g_window_state`, `g_onpresent_sync_*`, `late_amount_ns`, `s_*` (e.g. `s_window_mode`, `s_fps_limiter_mode`, `s_restart_needed_vsync_tearing`, `s_enable_flip_chain`), `enabled_experimental_features`, `display_cache::g_displayCache`, `settings::g_mainTabSettings`, `settings::g_advancedTabSettings`, `settings::g_experimentalTabSettings`, NVAPI/presentmon/dxgi namespaces.
- **Existing helpers already called:** `DrawAdhdMultiMonitorControls`, `DrawDLSSInfo`, `DrawQuickFpsLimitChanger`, `GetChosenFpsLimiterSiteName`, `IsNativeReflexActive`, `GetFlipStateForAPI`, `DxgiBypassModeToString`, etc.
- **Declarations:** Add new function declarations in `main_new_tab.hpp` (or the header for the new file if we introduce `display_settings_ui.cpp`).

---

## File Layout Options

- **Option A (minimal):** Keep everything in `main_new_tab.cpp`; add the new `DrawDisplaySettings_*` declarations in `main_new_tab.hpp`. No new files.
- **Option B:** Add `display_settings_ui.cpp` / `display_settings_ui.hpp` (or `display_settings_sections.cpp`) and move all `DrawDisplaySettings_*` implementations and their helpers there. Reduces `main_new_tab.cpp` size; may require moving or sharing some statics/helpers.

Recommendation: start with **Option A**; move to Option B only if `main_new_tab.cpp` remains too large or display-setting UI is to be reused elsewhere.

---

## Checklist (when implementing)

- [x] Extract `DrawDisplaySettings_VSyncAndTearing()` (and optional sub-helpers).
- [x] Extract `DrawDisplaySettings_FpsLimiterMode()`.
- [x] Extract `DrawDisplaySettings_DisplayAndTarget()`.
- [x] Extract `DrawDisplaySettings_WindowModeAndApply()`.
- [x] Extract `DrawDisplaySettings_FpsAndBackground()`.
- [x] Add declarations in `main_new_tab.hpp` (or new header).
- [x] Replace extracted blocks in `DrawDisplaySettings` with calls to the new functions.
- [x] Ensure `enabled_experimental_features` and any other shared state are visible where needed (same TU or passed as parameter).
- [x] Build and run UI; verify Display Settings tab behavior (display list, window mode, FPS limiter, VSync & Tearing, Present Mode tooltips, PresentMon section).

---

## Notes

- The **Delay Bias Debug** window (OnPresentSync) is a small popup inside the FPS Limiter section; it can stay inside `DrawDisplaySettings_FpsLimiterMode()`.
- **PresentMon** and **flip state** logic is tightly coupled to the VSync & Tearing section; keep it in `DrawDisplaySettings_VSyncAndTearing()` (or in `DrawDisplaySettings_PresentModeTooltip()` if that sub-helper is created).
- Line numbers above are approximate; re-check boundaries when editing (e.g. brace matching for the VSync collapsing header).
