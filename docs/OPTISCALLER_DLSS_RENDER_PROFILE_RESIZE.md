# OptiScaler: How DLSS Render Profile Change Relates to Resolution / Swapchain

Summary of how **OptiScaler** (external-src/optiscaller) handles **DLSS Render Profile** changes and whether it triggers resolution change or swapchain recreation.

## Finding: OptiScaler does **not** trigger swapchain resize when only DLSS Render Profile changes

### 1. Where the preset is applied

- **DLSS Render Profile** (preset) is applied only at **DLSS feature creation**, in `DLSSFeature::ProcessInitParams()` (`OptiScaler/upscalers/dlss/DLSSFeature.cpp`).
- Preset values (e.g. `NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality`, etc.) are read from config (`RenderPresetForAll`, `RenderPresetQuality`, …) and set on the NGX init parameters there (lines ~113–182).
- There is no runtime API in NGX to “change render preset” on an existing feature; the preset is fixed at **NGX feature creation**.

### 2. What happens when the user changes the preset in the overlay

- The menu only updates **config** (`RenderPresetOverride`, `RenderPresetForAll`, per-quality presets) via `AddDLSSRenderPreset` in `menu/menu_common.cpp` (~2965, 3004–3012).
- The UI text says **“Press apply after enable/disable”** and **“Requires a game restart”** for preset changes.
- **No** `changeBackend` is set when only the preset is changed.
- **No** call to `ResizeBuffers` or `ResizeTarget` is made by OptiScaler when the user changes only the DLSS Render Profile.

So: changing only the DLSS Render Profile does **not** trigger resolution change or swapchain recreation in OptiScaler.

### 3. When the new preset actually takes effect

The new preset is used the **next time** the DLSS feature is created, i.e. when:

1. **Game restart** (as indicated by the “Requires a game restart” note), or  
2. **User changes upscaler** (e.g. DLSS → FSR and back to DLSS): that sets `changeBackend` and runs `FeatureProvider_Dx12::ChangeFeature`, which destroys the current feature and creates a new one, so `ProcessInitParams` runs again with the new config, or  
3. **The game itself** calls `ResizeBuffers` / `ResizeTarget` and then re-initializes DLSS (many games re-init NGX on resize/fullscreen change). In that case the **game** triggers the resize; OptiScaler just forwards it and on the next NGX init it applies the current config (including the new preset).

### 4. Swapchain resize handling in OptiScaler (for reference)

- **ResizeBuffers / ResizeBuffers1** are implemented in `OptiScaler/wrapped/wrapped_swapchain.cpp` (lines ~555, ~861).
- OptiScaler **hooks** the swapchain; it does **not** call `ResizeBuffers` from its own code when the user changes preset or other DLSS options.
- On resize, it:
  - Takes mutex if FG/swapchain mutex is used.
  - Sets `State::Instance().SCchanged = true`, cleans overlay render targets (`MenuOverlayDx::CleanupRenderTarget`), optionally overrides VSync/tearing flags and HDR.
  - Calls the **real** `_real->ResizeBuffers` / `_real3->ResizeBuffers1` with the arguments provided by the **game** (it does not invent new width/height to “force” a resize for preset).

So resolution change / swapchain recreation is always initiated by the **game** (or by the user changing backend, which recreates the feature but does not call ResizeBuffers).

### 5. If you want “apply preset without restart” in another addon

To get new DLSS render preset applied without a full game restart, you would need one of:

- **Force feature recreation** (similar to OptiScaler’s “change backend”): set a “recreate DLSS” flag so the next frame the current DLSS feature is destroyed and re-created with the new preset in init params; no swapchain resize required, but the addon must own or hook the NGX create/destroy path.
- **Force a swapchain resize** so the game re-inits DLSS: call `IDXGISwapChain::ResizeBuffers(..., currentWidth, currentHeight, ...)` (same size). That only works for games that re-initialize NGX on resize; it can cause a brief flicker and is game-dependent.

OptiScaler does neither of these when only the DLSS Render Profile is changed; it relies on “apply + restart” or the user changing backend.

---

## Can Display Commander do the “recreate DLSS” flag?

**No.** Display Commander cannot implement the “recreate DLSS” approach (destroy + re-create the DLSS feature so the new preset is applied without restart) for these reasons:

1. **We don’t own the NGX feature**  
   The **game** creates the NGX feature (NGX_CreateFeature) and holds the handle. We only **hook** NGX (Parameter_Set*, CreateFeature, etc.) and tweak parameters before the game creates. We never create or own the handle. Calling NGX_ReleaseFeature(handle) ourselves would release the game’s handle; the game would then use a dangling handle and crash.

2. **OptiScaler can do it because it owns the lifecycle**  
   OptiScaler has its own upscaler layer: when you “change backend” it **replaces** the active feature by destroying its own wrapper and creating a new one with new init params. The game is still the one that created the original NGX feature; OptiScaler’s “recreate” is within its layer, not us calling NGX_Release on the game’s handle.

3. **We have no way to make the game re-create**  
   We can’t “tell” the game to destroy and re-create DLSS. We could only try to **trigger a resize** (e.g. call ResizeBuffers with the same size from a present callback) so that games that re-init NGX on resize do so—but that’s game-dependent, can cause flicker, and we don’t currently hold a safe swapchain reference to call ResizeBuffers from the UI.

**What we do instead:**  
Preset overrides are applied in our NGX Parameter_Set* hooks. They take effect the **next time** the game creates (or re-creates) the DLSS feature—e.g. on **game restart** or when the game itself re-initializes DLSS (e.g. after a real resize or mode change). We already have “Takes effect when DLSS feature is (re)created” in the UI; we do not add a “recreate DLSS” button because we cannot safely force that from the addon.
