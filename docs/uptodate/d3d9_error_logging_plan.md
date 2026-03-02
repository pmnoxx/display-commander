# D3D9 device error logging – hook plan

**Goal:** Hook every IDirect3DDevice9 / IDirect3DDevice9Ex method that returns HRESULT and log failures (with first-failure full-arg dump where useful), so D3D9 issues are visible without the debug layer (unsupported on Windows 11).

## Status

- **Done (Batch 0 – existing):** Reset, BeginScene, EndScene, Clear, CreateAdditionalSwapChain, GetBackBuffer, CreateTexture, CreateVolumeTexture, CreateCubeTexture, CreateVertexBuffer, CreateIndexBuffer, CreateRenderTarget, CreateDepthStencilSurface, CreateOffscreenPlainSurface, SetRenderTarget, SetDepthStencilSurface, CreateStateBlock, EndStateBlock, CreateVertexDeclaration, CreateVertexShader, SetStreamSource, SetIndices, CreatePixelShader.
- **Done (Batch 1):** TestCooperativeLevel, GetSwapChain, UpdateSurface, UpdateTexture, GetRenderTargetData, GetFrontBufferData, StretchRect, ColorFill, BeginStateBlock, CreateQuery.
- **Done (Batch 2):** DrawPrimitive, DrawIndexedPrimitive, DrawPrimitiveUP, DrawIndexedPrimitiveUP, ProcessVertices, SetVertexDeclaration, SetFVF, SetStreamSourceFreq.
- **Done (Batch 3):** GetRenderTarget, GetDepthStencilSurface, SetViewport, SetTransform, SetRenderState, GetTexture, SetTexture, SetVertexShader, SetPixelShader.
- **Done (Batch 4 – Device9Ex):** CreateRenderTargetEx, CreateOffscreenPlainSurfaceEx, CreateDepthStencilSurfaceEx, ResetEx, GetDisplayModeEx, CheckDeviceState.

**Total: 59 HRESULT-returning device methods hooked** (Present/PresentEx are in d3d9_present_hooks.cpp).

## Optional future (lower priority)

Remaining HRESULT methods not yet hooked: GetDeviceCaps, GetDisplayMode, GetCreationParameters, EvictManagedResources, GetDirect3D, SetCursorProperties, SetDialogBoxMode, GetRasterStatus, GetTransform, MultiplyTransform, GetViewport, SetMaterial, GetMaterial, SetLight, GetLight, LightEnable, GetLightEnable, SetClipPlane, GetClipPlane, GetRenderState, SetClipStatus, GetClipStatus, SetTextureStageState, GetSamplerState, SetSamplerState, ValidateDevice, SetPaletteEntries, GetPaletteEntries, SetCurrentTexturePalette, GetCurrentTexturePalette, SetScissorRect, GetScissorRect, SetSoftwareVertexProcessing, SetNPatchMode, DrawRectPatch, DrawTriPatch, GetVertexDeclaration, GetFVF, GetVertexShader, GetStreamSource, GetStreamSourceFreq, GetIndices, GetPixelShader, and Ex: SetConvolutionMonoKernel, ComposeRects, GetGPUThreadPriority, SetGPUThreadPriority, WaitForVBlank, CheckResourceResidency, SetMaximumFrameLatency, GetMaximumFrameLatency.

## Not hooked (by design)

- **Present / PresentEx** – hooked in `d3d9_present_hooks.cpp`.
- **QueryInterface, AddRef, Release** – not logged as D3D9 “errors” in this addon.
- **Methods that do not return HRESULT** – e.g. GetAvailableTextureMem, ShowCursor, SetCursorPosition, GetNumberOfSwapChains, SetGammaRamp, GetGammaRamp, GetMaterial, GetLight, GetLightEnable, GetRenderState, GetTextureStageState, GetSamplerState, GetPaletteEntries, GetCurrentTexturePalette, GetScissorRect, GetSoftwareVertexProcessing, GetNPatchMode, GetFVF, GetVertexShaderConstantF, etc.

## Pattern per hook

1. Type alias and `*_Original` for the method.
2. `g_first_<Method>_error` atomic (true).
3. Detour: `RECORD_DETOUR_CALL`, call original, if `FAILED(hr)` then `LogD3D9Error` and, on first failure, `LogD3D9FirstFailure` + optional one-line key args.
4. `CreateAndEnableHook` in `InstallD3D9DeviceVtableLogging`.
5. Update final `LogInfo` list when adding new hooks.
