# PresentMon Status in All API Modes (DX9, DXGI, Vulkan, OpenGL)

## Overview

Show `settings::g_advancedTabSettings.enable_presentmon_tracing.GetValue()` and PresentMon ON/OFF status in **all** graphics API modes (dx9, dxgi, vulkan, opengl), not only when DXGI is active. Previously this data was drawn only inside the DXGI branch of `DrawDisplaySettings_VSyncAndTearing_PresentModeLine`.

## Goals

1. **Show PresentMon status in all modes**: In Display Settings → VSync & Tearing, the "PresentMon: ON" / "PresentMon: OFF" line (and setting-based tooltip) is visible for:
   - **DX9** (D3D9)
   - **DXGI** (D3D10/11/12)
   - **Vulkan**
   - **OpenGL** (and any other "Unsupported API" fallback)

2. **Surface information for all APIs**: When PresentMon is ON, surface LUID, flip-from-surface, and surface tooltip are shown whenever available — for DX9, DXGI, Vulkan, and OpenGL (not DXGI-only).

3. **Split the PresentMon drawing**: Extract the PresentMon status block into a dedicated helper so it can be called from every API branch without duplication.

## Current Behavior

- **DXGI**: Full present mode line + flip state + PresentMon ON/OFF + (when ON) surface LUID, flip-from-surface, and surface tooltip.
- **D3D9**: Present mode line + flip state + Discord overlay; **no** PresentMon line.
- **Vulkan/OpenGL** ("Unsupported API"): Only "Unsupported API (WIP)"; **no** PresentMon line.

## Target Behavior

- **DXGI**: Full PresentMon block including surface LUID, flip-from-surface, and surface tooltip when PresentMon is ON and data is available.
- **D3D9**: Present mode line **plus** PresentMon status line (ON/OFF + surface details when available).
- **Vulkan/OpenGL**: "Unsupported API (WIP)" **plus** PresentMon status line (ON/OFF + surface details when available). Surface information is available for all APIs when PresentMon is running.

## Implementation Plan

### 1. New helper: `DrawDisplaySettings_VSyncAndTearing_PresentMonStatusLine()`

- **Location**: `main_new_tab.cpp`, static, near other `_VSyncAndTearing_*` helpers.
- **Responsibility**:
  - If `settings::g_advancedTabSettings.enable_presentmon_tracing.GetValue()` and `presentmon::g_presentMonManager.IsRunning()`:
    - Draw "PresentMon: ON" + short tooltip.
    - When a game HWND and surface are available: draw surface LUID, flip-from-surface, and surface tooltip (all APIs: DX9, DXGI, Vulkan, OpenGL).
  - Else:
    - Draw "PresentMon: OFF (not enabled by default)" + tooltip explaining how to enable in Advanced tab.
- **Parameters**: None (surface details shown for all APIs when PresentMon is ON and data is available).

### 2. Call sites in `DrawDisplaySettings_VSyncAndTearing_PresentModeLine`

| Branch              | Action |
|---------------------|--------|
| **D3D9**            | After Discord overlay block, before filling `out_ctx` and returning: call `DrawDisplaySettings_VSyncAndTearing_PresentMonStatusLine()`. |
| **DXGI**            | Replace the inline PresentMon block with `DrawDisplaySettings_VSyncAndTearing_PresentMonStatusLine()`. |
| **Unsupported API** | After drawing "Unsupported API (WIP)", call `DrawDisplaySettings_VSyncAndTearing_PresentMonStatusLine()`; then fill `out_ctx` and return. |

### 3. No changes to

- Advanced tab checkbox or PresentMon ETW subsection in the tooltip.
- `DrawDisplaySettings_VSyncAndTearing_SwapchainTooltip` or `DrawDisplaySettings_VSyncAndTearing_PresentMonETWSubsection`.

## Checklist

- [x] Add `DrawDisplaySettings_VSyncAndTearing_PresentMonStatusLine()` and implement ON/OFF + surface block for all APIs when available.
- [x] In PresentModeLine D3D9 branch: call helper with `false` before return.
- [x] In PresentModeLine DXGI branch: replace inline PresentMon block with helper call with `true`.
- [x] In PresentModeLine Unsupported API branch: call helper with `false` after "Unsupported API (WIP)".
- [ ] Build and verify in DX9, DXGI, Vulkan, OpenGL (or Unsupported API) that PresentMon status line appears in all cases; DXGI still shows full surface details.
