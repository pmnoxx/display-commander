# Split DrawDisplaySettings_VSyncAndTearing – Task Plan

## Overview

`DrawDisplaySettings_VSyncAndTearing()` in `src/addons/display_commander/ui/new_ui/main_new_tab.cpp` is **~1043 lines** (lines 2824–3866). This task plans how to split it into smaller, focused helper functions for maintainability and readability.

**Location:** `main_new_tab.cpp` L2824–L3866 (called from `DrawDisplaySettings()`).

---

## Current Structure (Logical Sections)

| Section | Approx. lines | Description |
|--------|----------------|-------------|
| **1. FPS sliders (top)** | 2825–2863 | `fps_limit_enabled` check, `DrawQuickFpsLimitChanger()`, Background FPS Limit slider (with disabled state when FPS limit off). |
| **2. CollapsingHeader "VSync & Tearing"** | 2864–3862 | Entire collapsible block. |
| **2a. Checkboxes** | 2865–2984 | Force VSync ON/OFF, Prevent Tearing (when swapchain captured); backbuffer count increase; Flip Chain; D3D9 Flip State; restart notice; separator. |
| **2b. Current Present Mode line** | 2985–3379 | When `desc_ptr`: "Current Present Mode:" label; D3D9 branch (present mode name/color, flip state, Discord overlay); DXGI branch (present mode name/color, flip state, PresentMon ON + surface/surface tooltip, or PresentMon OFF tooltip). Produces `status_hovered` for the status text. |
| **2c. Swapchain debug tooltip** | 3382–3851 | When `status_hovered`: `ImGui::BeginTooltip()` with Swapchain Information (present mode, status), Window Information (Debug), Size Comparison, Display Information, flip-state explanations, Back buffer/Format/Sync/Fullscreen, **PresentMon ETW** (CollapsingHeader: flip state, layer info, debug info, troubleshooting), present flags; `ImGui::EndTooltip()`. |
| **2d. No swapchain** | 3852–3861 | Else: "No swapchain information available" + tooltip. |

The largest blocks are **2b** (Current Present Mode line, ~395 lines) and **2c** (Swapchain debug tooltip, ~470 lines). Splitting by responsibility (checkboxes vs. present-mode display vs. tooltip content) keeps each helper manageable.

---

## Proposed Helper Functions

All helpers live in `main_new_tab.cpp`; add declarations in `main_new_tab.hpp`. Signatures are `void Foo(...)` unless noted. They use globals/settings already in scope (`g_last_swapchain_desc`, `g_last_reshade_device_api`, `settings::g_*`, etc.).

| # | Proposed name | Responsibility | Approx. size |
|---|----------------|----------------|--------------|
| 1 | `DrawDisplaySettings_VSyncAndTearing_FpsSliders()` | Compute `fps_limit_enabled`, draw Quick FPS Limit Changer and Background FPS Limit with disabled state. Called at the very start of `DrawDisplaySettings_VSyncAndTearing()`. | ~40 lines |
| 2 | `DrawDisplaySettings_VSyncAndTearing_Checkboxes()` | Inside the "VSync & Tearing" header: Force VSync ON/OFF, Prevent Tearing, backbuffer count, Flip Chain, D3D9 Flip, restart notice, separator. Caller is responsible for opening the CollapsingHeader and calling this first. | ~125 lines |
| 3 | `DrawDisplaySettings_VSyncAndTearing_PresentModeLine(...)` | Draw "Current Present Mode:" line (D3D9 and DXGI branches: present mode name/color, flip state, Discord/PresentMon/surface). Returns `bool status_hovered` (true when the status part is hovered, for showing the debug tooltip). May take `const SwapchainDesc* desc_ptr` (or use global) and API booleans. | ~400 lines |
| 4 | `DrawDisplaySettings_VSyncAndTearing_SwapchainTooltip(...)` | Draw the full swapchain debug tooltip content only: `ImGui::BeginTooltip()` already assumed by caller. Content: swapchain info, window info, size comparison, display info, flip explanations, back buffer/format/sync, PresentMon ETW subsection, present flags; `ImGui::EndTooltip()`. Parameters: everything needed to avoid re-querying (e.g. `desc`, `flip_state`, `flip_state_str`, `present_mode_name`, or a small struct). | ~470 lines |
| 5 | (optional) `DrawDisplaySettings_VSyncAndTearing_PresentMonETWSubsection(...)` | Only the PresentMon ETW CollapsingHeader block inside the tooltip (flip state, layer info, debug info, troubleshooting). Called from (4) to shrink the tooltip helper. | ~220 lines |

Optional further split of (3): separate **D3D9** vs **DXGI** present-mode line drawing into `_PresentModeLineD3D9` and `_PresentModeLineDxgi` if (3) is still too long after extracting (4).

---

## Recommended Order of Extraction

1. **Swapchain tooltip content** → `DrawDisplaySettings_VSyncAndTearing_SwapchainTooltip(...)`  
   - Extract the `if (status_hovered) { BeginTooltip(); ... EndTooltip(); }` body into a function that receives `desc`, flip state, present mode name, etc. (or a context struct). Reduces the main function by ~470 lines and isolates the heaviest UI block.

2. **PresentMon ETW subsection** (optional) → `DrawDisplaySettings_VSyncAndTearing_PresentMonETWSubsection(...)`  
   - Extract the `CollapsingHeader("PresentMon ETW Flip State & Debug Info")` block from the tooltip helper. Shrinks the tooltip helper and keeps ETW debug UI in one place.

3. **Present mode line** → `DrawDisplaySettings_VSyncAndTearing_PresentModeLine(...)`  
   - Extract the "Current Present Mode:" block (D3D9 + DXGI branches). Function returns `bool status_hovered`. After drawing, caller does `if (status_hovered) { BeginTooltip(); DrawDisplaySettings_VSyncAndTearing_SwapchainTooltip(...); EndTooltip(); }`.

4. **Checkboxes** → `DrawDisplaySettings_VSyncAndTearing_Checkboxes()`  
   - Extract all checkboxes, restart notice, and separator. Clear, self-contained block.

5. **FPS sliders** → `DrawDisplaySettings_VSyncAndTearing_FpsSliders()`  
   - Extract the top FPS limit + background FPS block. Alternatively, consider moving this block into `DrawDisplaySettings_FpsAndBackground()` so VSyncAndTearing only handles VSync/tearing/present mode (see **Notes**).

After all extractions, `DrawDisplaySettings_VSyncAndTearing()` would look like:

```cpp
void DrawDisplaySettings_VSyncAndTearing() {
    DrawDisplaySettings_VSyncAndTearing_FpsSliders();

    ImGui::Spacing();

    if (ImGui::CollapsingHeader("VSync & Tearing", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawDisplaySettings_VSyncAndTearing_Checkboxes();

        // Current Present Mode + tooltip
        bool status_hovered = DrawDisplaySettings_VSyncAndTearing_PresentModeLine(...);
        if (status_hovered) {
            ImGui::BeginTooltip();
            DrawDisplaySettings_VSyncAndTearing_SwapchainTooltip(...);
            ImGui::EndTooltip();
        }

        // No swapchain case (or fold into PresentModeLine and return a small enum: NoDesc / LineDrawn / LineDrawnHovered)
        if (!desc_ptr) {
            ImGui::TextColored(...); // "No swapchain information available"
            ...
        }
    }
}
```

(Exact signature and parameter passing for `_PresentModeLine` and `_SwapchainTooltip` to be decided when implementing: e.g. pass `desc`, API flags, and strings/state that are already computed in the caller to avoid duplicated logic.)

---

## Dependencies and State

- **Globals / statics used:** `g_last_swapchain_desc`, `g_last_reshade_device_api`, `g_last_swapchain_hwnd`, `g_reshade_event_counters`, `s_restart_needed_vsync_tearing`, `s_enable_flip_chain`, `settings::g_mainTabSettings`, `settings::g_advancedTabSettings`, `settings::g_experimentalTabSettings`, `presentmon::g_presentMonManager`, `display_commanderhooks::GetGameWindow()`, `GetFlipStateForAPI`, `DxgiBypassModeToString`, `ui::colors::*`, etc.
- **Existing helpers called:** `DrawQuickFpsLimitChanger`, `SliderFloatSettingRef`, `GetFlipStateForAPI`, `DxgiBypassModeToString`, `display_commander::utils::IsWindowWithTitleVisible`.
- **Declarations:** Add new function declarations in `main_new_tab.hpp`.

---

## Parameter Passing for Tooltip Helper

To keep `DrawDisplaySettings_VSyncAndTearing_SwapchainTooltip` self-contained and avoid re-querying inside the tooltip, consider a small struct, e.g.:

```cpp
struct VSyncTearingTooltipContext {
    const SwapchainDesc* desc;           // or full copy
    DxgiBypassMode flip_state;
    const char* flip_state_str;
    std::string present_mode_name;
    int current_api;                     // for D3D9 vs DXGI branching inside tooltip if needed
};
```

Caller (or `_PresentModeLine`) fills this when drawing the present mode line; when `status_hovered`, call `DrawDisplaySettings_VSyncAndTearing_SwapchainTooltip(ctx)`.

---

## File Layout

- **Option A (recommended):** Keep all new helpers in `main_new_tab.cpp`; add declarations in `main_new_tab.hpp`. No new files.
- **Option B:** If `main_new_tab.cpp` is still too large, introduce `display_settings_vsync_tearing.cpp` / `.hpp` and move `DrawDisplaySettings_VSyncAndTearing` and all `_VSyncAndTearing_*` helpers there. Shared globals must remain visible (same TU or declared in header).

---

## Checklist (when implementing)

- [x] Introduce `DrawDisplaySettings_VSyncAndTearing_SwapchainTooltip(...)` and move tooltip body into it.
- [x] (Optional) Introduce `DrawDisplaySettings_VSyncAndTearing_PresentMonETWSubsection(...)`.
- [x] Introduce `DrawDisplaySettings_VSyncAndTearing_PresentModeLine(...)` returning `bool status_hovered`; wire tooltip call when hovered.
- [x] Introduce `DrawDisplaySettings_VSyncAndTearing_Checkboxes()`.
- [x] Introduce `DrawDisplaySettings_VSyncAndTearing_FpsSliders()` (or move FPS sliders to `DrawDisplaySettings_FpsAndBackground()` and remove from VSyncAndTearing).
- [ ] Add declarations in `main_new_tab.hpp` (optional; helpers are static in .cpp).
- [x] Refactor `DrawDisplaySettings_VSyncAndTearing()` to call the new helpers in order.
- [x] Build and run; verify Display Settings → VSync & Tearing (checkboxes, present mode line, hover tooltip, PresentMon ETW subsection, no swapchain message).

---

## Notes

- **FPS sliders:** The top block (Quick FPS Limit Changer + Background FPS Limit) is logically FPS-related. The existing `draw_display_settings_split.md` assigns "FPS Limit slider, Quick FPS Limit Changer, Background FPS Limit" to `DrawDisplaySettings_FpsAndBackground()`. If that function already draws those, then this block might be duplicate; if not, consider moving it into `DrawDisplaySettings_FpsAndBackground()` and removing it from `DrawDisplaySettings_VSyncAndTearing()` so VSyncAndTearing only handles VSync/tearing and present mode.
- **Line numbers** in this doc are approximate; re-check brace boundaries (e.g. CollapsingHeader, `if (desc_ptr)`, `if (status_hovered)`) when editing.
- **D3D9 vs DXGI:** Present mode line and tooltip both branch on API; keep branching in one place per helper to avoid duplication.
