# Auto Adjust Colorspace – Improvements (Name, Default, Tooltips)

## Overview

Improve the **Auto color space** feature with a clearer name, default-on behavior, and better tooltips. The feature automatically sets both **DXGI swap chain** color space (`IDXGISwapChain3::SetColorSpace1`) and **ReShade runtime** color space (`runtime->set_color_space`) to values that match the back-buffer format, so ReShade and the game use consistent color handling.

- **Rename**: "Auto color space" → **"Auto adjust color space"** (checkbox label and related UI text; two words for consistency with the original label and general English).
- **Default**: Change default from **false** to **true** so new users get correct SDR/HDR behavior by default.
- **Tooltips / copy**: Clarify that both DXGI and ReShade are updated; keep format mapping clear.

---

## Current State

- **Setting**: `settings::g_advancedTabSettings.auto_colorspace` (`BoolSetting`), config key `"AutoColorspace"`, default **false**.
- **UI**: Advanced tab, DXGI section – checkbox **"Auto color space"**; manual **"Color space"** combo used when auto is off.
- **Behavior**: When enabled and manual selector is "No changes", `AutoSetColorSpace()` in `presentBefore` path:
  - Detects back-buffer format and picks DXGI + ReShade color space (e.g. R10G10B10A2 → HDR10/ST2084, R16G16B16A16 → scRGB, R8G8B8A8 → sRGB).
  - Calls `SetSwapChainColorSpace()` which does both `swapchain3->SetColorSpace1(...)` and `runtime->set_color_space(...)`.
- **Tooltip**: Describes format mapping and "Applied automatically in presentBefore"; does not explicitly say "ReShade and DXGI".

---

## Target State

1. **Display name**: Checkbox label **"Auto adjust color space"** (and any references in tooltips/combo text to "Auto color space" updated to "Auto adjust color space" where it’s user-facing).
2. **Default**: `auto_colorspace` default **true** in `AdvancedTabSettings` constructor. No config migration: existing users keep their saved value; only new configs get true by default.
3. **Tooltip (checkbox)**:
   - State clearly that both **DXGI swap chain** and **ReShade** color space are set to matching values.
   - Keep the format mapping (HDR10 / scRGB / sRGB) and "DirectX 11/12" and "presentBefore" as-is or slightly tightened.
4. **Manual selector tooltip**: Replace "when \"Auto color space\" is off" with "when \"Auto adjust color space\" is off".
5. **Config key**: Keep **"AutoColorspace"** for backward compatibility (no renames in config).
6. **Comments**: Update in-code comments that say "Auto color space" to "Auto adjust color space" where they refer to the feature name (e.g. in `advanced_tab.cpp`, `swapchain_events.hpp`, `globals.hpp`, `advanced_tab_settings.hpp`).

---

## Implementation Plan (Order of Work)

### 1. Change default to true

- **advanced_tab_settings.cpp**: In `AdvancedTabSettings` constructor, change  
  `auto_colorspace("AutoColorspace", false, "DisplayCommander")`  
  to  
  `auto_colorspace("AutoColorspace", true, "DisplayCommander")`.

### 2. Rename UI label and tooltips

- **advanced_tab.cpp** (Advanced tab, DXGI section):
  - Checkbox: change label from `"Auto color space"` to `"Auto adjust color space"`.
  - Checkbox tooltip: rewrite to state that both **DXGI swap chain** and **ReShade** color space are set to the correct values for the current format; keep format bullets (HDR10/scRGB/sRGB) and "DirectX 11/12" / "presentBefore".
  - Manual "Color space" combo tooltip: change `"Auto color space"` to `"Auto adjust color space"` in the sentence "when \"Auto color space\" is off".
  - Any other comment in that block that says "Auto color space" (e.g. "used when Auto color space is off") → "Auto adjust color space".

### 3. Update comments elsewhere

- **swapchain_events.hpp**: Comment for auto colorspace helper → "Auto adjust color space helper" (or equivalent).
- **globals.hpp**: Comment "Auto color space setting" → "Auto adjust color space setting".
- **advanced_tab_settings.hpp**: Comment for manual color space / auto_colorspace → use "Auto adjust color space" where it describes the feature.

### 4. Optional: settings vector / display name

- If `advanced_tab_settings` has a list of display names for the settings UI, add or update the string for this option to "Auto adjust color space" so it’s consistent everywhere.

---

## Suggested Tooltip Text (Checkbox)

Short form (can be used as-is or adapted):

```
Automatically sets DXGI swap chain and ReShade color space to the correct values for the current back-buffer format:
• HDR10 (R10G10B10A2) → HDR10 (ST2084)
• FP16 (R16G16B16A16) → scRGB (Linear)
• SDR (R8G8B8A8) → sRGB (Non-linear)
Only applies to DirectX 11/12. Applied in presentBefore.
```

---

## Files to Touch (Summary)

| File | Changes |
|------|--------|
| `settings/advanced_tab_settings.cpp` | Default `true` for `auto_colorspace`. |
| `ui/new_ui/advanced_tab.cpp` | Label "Auto adjust color space"; checkbox + manual combo tooltips; inline comments. |
| `swapchain_events.hpp` | Comment "Auto adjust color space helper". |
| `globals.hpp` | Comment "Auto adjust color space setting". |
| `settings/advanced_tab_settings.hpp` | Comment for manual/auto colorspace. |

Config key **"AutoColorspace"** unchanged for compatibility.

---

## Verification

- New install / clean config: checkbox "Auto adjust color space" is **on** by default.
- Existing config with `AutoColorspace` saved: behavior unchanged (loads saved value).
- With auto on: verify in-game that swap chain and ReShade report correct color space for SDR/HDR formats.
- Tooltips and combo text reference "Auto adjust color space" consistently.
