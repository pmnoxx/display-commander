# HDR10 Color Fix Simplification (Auto Adjust Color Space → HDR10-only)

## Overview

Simplify the **Auto adjust color space** feature so it acts only as an **HDR10 color fix**: apply color space correction only when the back buffer is **HDR10** (10-bit, `R10G10B10A2_UNORM`). Do **not** apply for 8-bit (R8G8B8A8) or 16-bit (R16G16B16A16) buffers—the apply function does less and stays out of SDR/scRGB paths.

---

## Current State

- **Setting**: `settings::g_advancedTabSettings.auto_colorspace` (config key `AutoColorspace2`), default **true**.
- **UI**: Main tab → Brightness and AutoHDR (DXGI only), checkbox **"Auto adjust color space"**.
- **Behavior** (`AutoSetColorSpace` in `swapchain_events.cpp`):
  - When enabled and API is D3D11/D3D12, reads back-buffer format and:
    - **R10G10B10A2_UNORM** → sets DXGI + ReShade to HDR10 (ST2084)
    - **R16G16B16A16_FLOAT** → sets DXGI + ReShade to scRGB (Linear)
    - **R8G8B8A8_UNORM** → sets DXGI + ReShade to sRGB
  - Any other format → logs error and returns.
- **Apply path**: `OnPresentUpdateBefore` → `AutoSetColorSpace(swapchain)` → `SetSwapChainColorSpace(...)` for all three format branches.

---

## Target State

1. **Scope**: Feature applies **only when HDR10 is detected** (back-buffer format `r10g10b10a2_unorm`).
2. **No-op for other formats**: For `r8g8b8a8_unorm` and `r16g16b16a16_float` (and any other format), do **nothing**—no `SetSwapChainColorSpace` call, no error log. Leave swap chain and ReShade color space as-is.
3. **Apply function does less**: `AutoSetColorSpace` only contains the HDR10 branch; no scRGB/sRGB branches.
4. **Naming / UI**: Rename to **"HDR10 color fix"** (or keep label and clarify in tooltip). Tooltip should state that it only corrects color space for HDR10 (10-bit) swap chains; 8-bit and 16-bit are unchanged.
5. **Config**: Keep existing setting key and variable name for backward compatibility; only behavior and optionally label/tooltip change.

---

## Implementation Plan (Order of Work)

### 1. Simplify `AutoSetColorSpace` (swapchain_events.cpp)

- After the existing guards (API check, setting check, `desc_ptr`), get `format = desc.back_buffer.texture.format`.
- **If** `format != reshade::api::format::r10g10b10a2_unorm`: **return** immediately (no-op). Do not log for 8-bit/16-bit; they are valid, we simply do not touch them.
- **Else** (HDR10): set `color_space = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020`, `reshade_color_space = reshade::api::color_space::hdr10_st2084`, then call `SetSwapChainColorSpace(swapchain, color_space, reshade_color_space)`.
- **Remove**: The `else if` branches for `r16g16b16a16_float` and `r8g8b8a8_unorm`, and the `else { LogError("AutoSetColorSpace: Unsupported format ..."); return; }` (no longer needed; other formats are intentionally no-op).

### 2. Update comments

- **swapchain_events.cpp**: Comment above `AutoSetColorSpace` → e.g. "Helper: set color space to HDR10 (ST2084) when HDR10 color fix is enabled and back buffer is R10G10B10A2. No-op for 8-bit/16-bit."
- **swapchain_events.hpp**: Comment for `AutoSetColorSpace` → e.g. "HDR10 color fix: set DXGI + ReShade color space to HDR10 when back buffer is 10-bit; no-op otherwise."

### 3. UI: label and tooltip (main_new_tab.cpp) ✅

- **Checkbox label**: **"HDR10 color fix"** (done).
- **Tooltip**: Replace with something like (done):
  - "When the game uses an HDR10 back buffer (R10G10B10A2), sets DXGI swap chain and ReShade color space to HDR10 (ST2084). Does not change color space for 8-bit or 16-bit buffers. DirectX 11/12 only. Applied in presentBefore."

### 4. Optional: setting display name / comments elsewhere

- **advanced_tab_settings.hpp/cpp**: If there is a display name for this setting, update to "HDR10 color fix".
- **globals.hpp**: Comment "Auto color space setting" → "HDR10 color fix setting" (or similar).

### 5. CHANGELOG

- Add entry: e.g. "**HDR10 color fix (simplified)** – 'Auto adjust color space' now only applies when HDR10 is detected (R10G10B10A2 back buffer). Renamed to 'HDR10 color fix'; 8-bit and 16-bit buffers are no longer modified."

---

## Files to Touch (Summary)

| File | Changes |
|------|--------|
| `swapchain_events.cpp` | Restrict `AutoSetColorSpace` to HDR10 only; no-op for other formats; remove scRGB/sRGB branches and unsupported-format error; update comment. |
| `swapchain_events.hpp` | Update comment for `AutoSetColorSpace`. |
| `ui/new_ui/main_new_tab.cpp` | Checkbox label → "HDR10 color fix"; tooltip updated to describe HDR10-only behavior. |
| `globals.hpp` | Optional: comment for setting. |
| `settings/advanced_tab_settings.*` | Optional: display name / comments. |
| `CHANGELOG.md` | Add entry for HDR10-only simplification. |

---

## Verification

- **HDR10 game** (R10G10B10A2): With "HDR10 color fix" on, DXGI and ReShade color space are set to HDR10 (ST2084). With it off, no change from addon.
- **SDR game** (R8G8B8A8): Addon never calls `SetSwapChainColorSpace`; no effect.
- **scRGB / FP16 game** (R16G16B16A16): Addon never calls `SetSwapChainColorSpace`; no effect.
- **Config**: Existing `AutoColorspace2` value is still read; no migration.

---

## Rationale

- **Less surface area**: Avoid touching sRGB/scRGB paths that games may already set correctly; focus on the HDR10 case where misconfiguration is common.
- **Clearer intent**: "HDR10 color fix" describes exactly when the feature applies.
- **Apply function does less**: Single branch, no branching for 8/16-bit, no error log for other formats.
