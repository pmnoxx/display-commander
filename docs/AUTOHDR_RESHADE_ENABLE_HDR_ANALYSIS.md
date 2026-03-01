# AutoHDR-ReShade "Enable HDR" — ReShade Addon Events Analysis

This document describes how the **Enable HDR** feature in [AutoHDR-ReShade](https://github.com/EndlesslyFlowering/AutoHDR-ReShade) (EndlesslyFlowering modified version) works in terms of **ReShade addon events**: which events it uses, what it modifies, and in what order. The analysis is based on the addon’s `dllmain.cpp` (branch `mine`).

---

## Overview

AutoHDR-ReShade turns an SDR game into an HDR output by:

1. **Changing the swap chain** to an HDR format (scRGB 16-bit float or HDR10 10-bit) and setting the DXGI color space.
2. **Telling ReShade** which color space the back buffer uses via `effect_runtime::set_color_space()`.
3. **Keeping resource views** for the back buffer in the correct HDR format when ReShade or the game create views.

All of this is done from ReShade addon event callbacks; no present-time hooks are used.

---

## ReShade Events Used

| ReShade addon event        | Handler                  | Purpose |
|---------------------------|--------------------------|--------|
| `init_device`             | `on_init_device`         | Load config, detect API (D3D10/11/12/Vulkan). |
| `destroy_device`          | `on_destroy_device`      | Clear device/API state. |
| **`create_swapchain`**    | `on_create_swapchain`    | **Modify** swap chain description before creation (HDR format, buffer count, present mode). |
| **`init_swapchain`**      | `on_init_swapchain`     | After creation: check display HDR support, optionally **ResizeBuffers** + **SetColorSpace1**, call **set_color_space**. |
| `destroy_swapchain`       | `on_destroy_swapchain`  | Untrack back buffer handles. |
| **`create_resource_view`**| `on_create_resource_view` | **Override** view format for back-buffer views so they stay R10G10B10A2 or R16G16B16A16_FLOAT. |
| `init_effect_runtime`     | `on_init_effect_runtime`  | Store runtime, call `set_reshade_colour_space()`. |
| `destroy_effect_runtime`  | `on_destroy_effect_runtime` | Clear runtime pointer. |

Plus **`register_overlay`** for the settings UI (Enable HDR / Use HDR10 checkboxes); that is not an addon event but is required for the feature to be configurable.

---

## What Each Event Modifies (Details)

### 1. `init_device` → `on_init_device`

- **Modifies:** Addon state only (no ReShade/DXGI parameters).
- **Actions:**
  - Stores `device` in `g_device`.
  - Sets `g_is_supported_api` for D3D10/11/12/Vulkan.
  - Sets `g_is_vulkan_api` for Vulkan.
  - Loads config: `reshade::get_config_value(runtime, "HDR", "EnableHDR", g_hdr_enable)` and `"UseHDR10"` → `g_use_hdr10`.

Note: Config is read with `g_runtime`; at init_device time runtime may be null, so the addon may rely on config being read again when runtime is available or on defaults.

---

### 2. `create_swapchain` → `on_create_swapchain`

- **Signature (conceptually):**  
  `bool on_create_swapchain(device_api api, swapchain_desc& swapchain_desc, void* hwnd)`
- **Modifies:** **`swapchain_desc`** (in-out). ReShade uses this description to create the swap chain.
- **Actions when `g_is_supported_api`:**
  - **Back buffer format:**  
    `swapchain_desc.back_buffer.texture.format = r16g16b16a16_float` (scRGB).  
    If `g_use_hdr10`: `r10g10b10a2_unorm`.
  - **Buffer count:** If `back_buffer_count < 2`, set to `2`.
  - **Present mode:**  
    `swapchain_desc.present_mode = FLIP_DISCARD` (DXGI swap effect).  
    `swapchain_desc.present_flags |= ALLOW_TEARING`.
- **Return:** `true` so creation proceeds with the modified desc.

So the **first** place HDR is applied is by changing the **requested** swap chain format and present options before the swap chain exists.

---

### 3. `init_swapchain` → `on_init_swapchain`

- **Modifies:**  
  - Addon state (back buffer set, `g_original_format`, `g_colour_space`).  
  - **Native DXGI swap chain** (via `IDXGISwapChain4`), and **ReShade effect runtime** (via `set_color_space`).
- **Actions:**
  1. **Track back buffers:** For each back buffer, `swapchain->get_back_buffer(i)` → store handle in `g_back_buffers` (used later in `create_resource_view`).
  2. **Get native swap chain:** `swapchain->get_native()` → `IDXGISwapChain*` → `QueryInterface` to `IDXGISwapChain4`.
  3. **Display HDR support (once):** If `g_hdr_support` is still false:
     - Get factory from swap chain, then `dxgi_check_display_hdr_support(factory, hwnd)`.
     - Uses `IDXGIOutput6::GetDesc1()` for the output that best contains the window; sets `g_hdr_support = (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)` (i.e. display reports HDR10).
  4. **If HDR enabled and display supports HDR:**
     - `GetDesc1()` on swap chain.
     - Optionally store `g_original_format` on first run.
     - **Target format/space:**  
       scRGB: `R16G16B16A16_FLOAT` + `DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709`.  
       HDR10: `R10G10B10A2_UNORM` + `DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020`.
     - If current format/color space differs from target:  
       **`IDXGISwapChain4::ResizeBuffers`** with `new_swapchain_format` (same size/count).
     - **`dxgi_swapchain_color_space(swapchain4, new_colour_space)`:**  
       - `CheckColorSpaceSupport` → if supported, **`SetColorSpace1(target_colour_space)`**.  
       - Then **`set_reshade_colour_space()`** (see below).
  5. **If HDR disabled:**  
     ResizeBuffers back to `g_original_format`, set color space to SDR (`G22_NONE_P709`), and call `set_reshade_colour_space()`.

So **init_swapchain** is where the addon:
- Ensures the **DXGI swap chain** actually uses an HDR format and color space (including ResizeBuffers if the game created an SDR swap chain).
- Tells **ReShade** the back buffer color space via **`effect_runtime::set_color_space()`**.

---

### 4. `set_reshade_colour_space()` (helper, called from init_swapchain and init_effect_runtime)

- **Modifies:** **ReShade effect runtime** only.
- **Actions:**  
  Maps `g_colour_space` (DXGI) to `reshade::api::color_space` and calls **`g_runtime->set_color_space(reshade_colour_space)`**:
  - `DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709` → `extended_srgb_linear` (scRGB).
  - `DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020` → `hdr10_st2084`.
  - Default → `srgb_nonlinear`.

So ReShade’s tonemapping and effects use the correct color space for the back buffer.

---

### 5. `destroy_swapchain` → `on_destroy_swapchain`

- **Modifies:** Addon state only.
- **Actions:** Removes all back buffer handles for this swap chain from `g_back_buffers` (so `create_resource_view` no longer treats them as back buffers).

---

### 6. `create_resource_view` → `on_create_resource_view`

- **Signature (conceptually):**  
  `bool on_create_resource_view(device*, resource, usage_type, resource_view_desc& desc)`
- **Modifies:** **`resource_view_desc`** (in-out). ReShade uses this to create the view; returning `true` means “use this modified desc”.
- **Actions when `g_is_supported_api`:**
  - If the **resource** is in `g_back_buffers` and the resource’s format is `r10g10b10a2_unorm` or `r16g16b16a16_float`, set **`desc.format`** to that same format and **return true**.
- **Otherwise:** Return `false` (no change).

So when ReShade (or the game) creates a view on the back buffer, the addon can **force the view format** to match the HDR back buffer format, avoiding mismatches or fallbacks to SDR views.

---

### 7. `init_effect_runtime` / `destroy_effect_runtime`

- **init_effect_runtime:** Stores `g_runtime = runtime`, then calls **`set_reshade_colour_space()`** so the runtime’s color space is correct as soon as the runtime exists.
- **destroy_effect_runtime:** Sets `g_runtime = nullptr`.

No ReShade API parameters are modified beyond `set_color_space` as above.

---

## Order of Operations (Typical Flow)

1. **init_device** — Load Enable HDR / Use HDR10, detect API.
2. **create_swapchain** — ReShade (or the game) is about to create a swap chain; addon **changes the requested format** to HDR (and buffer count / present mode).
3. ReShade creates the swap chain with the modified desc.
4. **init_swapchain** — Addon gets the created swap chain, checks display HDR support, may **ResizeBuffers** and **SetColorSpace1**, then **set_color_space** on the runtime.
5. **init_effect_runtime** — Addon stores runtime and calls **set_reshade_colour_space()** again so ReShade’s color space is correct.
6. Whenever a **create_resource_view** runs for a back buffer resource, addon **overrides the view format** to the HDR format and returns true.
7. **destroy_swapchain** / **destroy_effect_runtime** / **destroy_device** — Clean up state.

---

## Summary Table: What Is Modified Per Event

| Event               | ReShade API / object modified     | What is changed |
|---------------------|------------------------------------|------------------|
| `create_swapchain`  | `swapchain_desc` (in-out)         | Back buffer format (HDR), buffer count, present mode/flags. |
| `init_swapchain`    | Native `IDXGISwapChain4`           | Optional ResizeBuffers; SetColorSpace1(DXGI color space). |
| `init_swapchain`    | `effect_runtime`                  | `set_color_space(reshade::api::color_space)`. |
| `create_resource_view` | `resource_view_desc` (in-out) | Format of views that target the back buffer. |
| `init_effect_runtime` | `effect_runtime`                | `set_color_space(...)` (again for consistency). |

---

## Comparison with Display Commander

Display Commander’s **auto color space** (and manual color space) behavior is similar in outcome but implemented differently:

- **Display Commander** typically applies color space from **present** (e.g. in `present` / `presentBefore`-style logic) or from swapchain init paths, and uses **SetSwapChainColorSpace** which calls both **IDXGISwapChain3::SetColorSpace1** and **effect_runtime::set_color_space**. It does **not** modify **create_swapchain** (swapchain_desc) or **create_resource_view**.
- **AutoHDR-ReShade**:
  - **Intercepts creation:** Uses **create_swapchain** to request an HDR swap chain from the start, and **init_swapchain** to fix format/color space if needed (including ResizeBuffers).
  - **Keeps views consistent:** Uses **create_resource_view** so any view on the back buffer keeps the HDR format.
  - **Syncs ReShade:** Uses **init_effect_runtime** and **init_swapchain** to call **set_color_space** so ReShade’s pipeline matches the DXGI color space.

So AutoHDR’s “Enable HDR” is more invasive in the creation path (swap chain desc + resource views) and does not rely on present-time color space application only.

---

## References

- [AutoHDR-ReShade](https://github.com/EndlesslyFlowering/AutoHDR-ReShade) (EndlesslyFlowering modified version, branch `mine`).
- ReShade addon events: `create_swapchain`, `init_swapchain`, `destroy_swapchain`, `create_resource_view`, `init_effect_runtime`, `destroy_effect_runtime`, `init_device`, `destroy_device` (see ReShade `addon_manager.hpp` and addon API).
- Display Commander: `docs/RESHADE_EVENTS_TRACKING.md`, `swapchain_events.cpp` (e.g. `SetSwapChainColorSpace`, present handling).
