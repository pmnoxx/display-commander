# InstallDX9Hooks and D3D9 Device VTable Logging – Task Plan

## Overview

Add **InstallDX9Hooks** (similar to **InstallOpenGLHooks**), and hook **IDirect3DDevice9** / **IDirect3DDevice9Ex** vtables to:

- Log every call to **IDirect3DDevice9** methods (e.g. **CreateOffscreenPlainSurface** and all others).
- For **IDirect3DDevice9Ex**: log only the **first call** per function.
- Log whenever a function returns an **error** (separate log).
- Use **CALL_GUARD(utils::get_now_ns())** at the start of each detour.

**References:** `InstallOpenGLHooks` in `hooks/opengl_hooks.cpp`, existing D3D9 present hooks in `hooks/d3d9/d3d9_present_hooks.cpp`, ReShade D3D9 hooking in `external/reshade/source/d3d9/`.

---

## 1. How ReShade Hooks D3D9

- **Module registration:** ReShade calls `reshade::hooks::register_module(get_system_path() / L"d3d9.dll")` in `dll_main.cpp` so that when the game loads `d3d9.dll`, ReShade’s hook manager gets it.
- **Device creation:** ReShade hooks **IDirect3D9::CreateDevice** (vtable index **16** on IDirect3D9) and **IDirect3D9Ex::CreateDeviceEx** (index **20** on IDirect3D9Ex). See `external/reshade/source/d3d9/d3d9.cpp` and `d3d9on12.cpp`.
- **Device lifecycle:** After CreateDevice/CreateDeviceEx, ReShade wraps the device and later invokes addon events **init_device** and **init_swapchain**. The addon can get the native D3D9 device via `device->get_native()` when `api == device_api::d3d9`.

So ReShade does **not** hook `Direct3DCreate9` / `Direct3DCreate9Ex` exports directly; it hooks the **IDirect3D9** object’s vtable (from the module that created it) and intercepts **CreateDevice** / **CreateDeviceEx**.

---

## 2. Current Display Commander D3D9 State

- **Present hooks:** `hooks/d3d9/d3d9_present_hooks.cpp` implements **HookD3D9Present(IDirect3DDevice9* device)** and hooks:
  - **IDirect3DDevice9::Present** at vtable index **17**
  - **IDirect3DDevice9Ex::PresentEx** at vtable index **121**
- **Gap:** **HookD3D9Present** is **never called** anywhere. Only **RecordPresentUpdateDevice** is called from **OnInitSwapchain** when a D3D9 swapchain is initialized. So the D3D9 Present detours are currently **not** installed.
- **Device source:** The only place we get an **IDirect3DDevice9*** is from ReShade’s **init_swapchain** (and device from **init_device**) by querying `device->get_native()`.

---

## 3. InstallDX9Hooks (Similar to InstallOpenGLHooks)

**Goal:** When **d3d9.dll** is loaded, run a single “install D3D9 hooks” step, analogous to **InstallOpenGLHooks** for **opengl32.dll**.

**Options:**

- **A) Module-load only:**
  **InstallDX9Hooks(HMODULE hModule)** is called from the **LoadLibrary** detour when `d3d9.dll` is loaded. It does **not** hook exports yet; it only sets up state (e.g. “D3D9 hooks ready”) and/or hooks that will be applied when we get a device (e.g. in **OnInitDevice** / **OnInitSwapchain**).

- **B) Hook device creation from d3d9.dll:**
  In **InstallDX9Hooks**, get `Direct3DCreate9` and `Direct3DCreate9Ex` from `hModule` and install detours. In the detour, the app gets **IDirect3D9*** / **IDirect3D9Ex***. We would then need to hook **CreateDevice** / **CreateDeviceEx** on that object’s vtable to get **IDirect3DDevice9*** when the device is created, then install our **device** vtable hooks (and call **HookD3D9Present**). This catches every device, including those not yet used for swapchain.

- **C) Rely on ReShade device only:**
  Do **not** hook d3d9.dll exports. In **OnInitDevice(device_api::d3d9)** or when we get the D3D9 device in **OnInitSwapchain**, get `device->get_native()`, call **HookD3D9Present(device)** and install the full **IDirect3DDevice9** / **IDirect3DDevice9Ex** vtable logging on that device. Only the device(s) ReShade gives us are hooked.

**Recommendation:** Start with **(A) + (C):**

- Add **InstallDX9Hooks(HMODULE)** and call it from the LoadLibrary path when `d3d9.dll` is loaded (same pattern as `opengl32.dll` → **InstallOpenGLHooks**). Inside **InstallDX9Hooks**, only set state / one-time init (e.g. optional hook suppression check); do **not** hook exports.
- When we receive a D3D9 device from ReShade (**OnInitDevice** or **OnInitSwapchain**), call **HookD3D9Present(device)** so Present/PresentEx detours are actually installed, and extend that path to install the **full device vtable logging** (see below).

If we later need to log every device (including those never used for swapchain), we can add **(B)** (hook **Direct3DCreate9** / **CreateDevice** and install vtable hooks on every created device).

**Integration point (LoadLibrary):** In `loadlibrary_hooks.cpp`, where `opengl32.dll` triggers **InstallOpenGLHooks(hModule)**, add a branch for `d3d9.dll` that calls **InstallDX9Hooks(hModule)**.

---

## 4. VTable Hooks for IDirect3DDevice9 / IDirect3DDevice9Exmul

**Goal:**

- **IDirect3DDevice9:** Log **every** call to each method; use **CALL_GUARD(utils::get_now_ns())**; and when the method returns a failure HRESULT (or error), write to a **separate** “error” log.
- **IDirect3DDevice9Ex:** Log only the **first** call per function (e.g. per-method “first call” flag), plus **every** error return in the same separate error log.

**VTable source:** Indices must match the Windows SDK **d3d9.h** declaration order (IUnknown 0–2, then IDirect3DDevice9 methods in interface order). **IDirect3DDevice9Ex** extends the vtable (e.g. PresentEx at index **121** in existing code).

**Known indices (from existing code):**

| Interface / Method              | VTable Index | Notes        |
|---------------------------------|-------------|--------------|
| IDirect3DDevice9::Present        | 17          | Already used |
| IDirect3DDevice9Ex::PresentEx    | 121         | Already used |

**CreateOffscreenPlainSurface** and all other **IDirect3DDevice9** methods should be enumerated from **d3d9.h** (declaration order) and assigned indices 3–82 (approximately; exact count and order must be taken from the SDK header). Example ordering (verify in your SDK):

- 0–2: QueryInterface, AddRef, Release
- 3: TestCooperativeLevel
- 4: GetAvailableTextureMem
- … through GetBackBuffer (16), Present (17), GetRasterStatus (18), …
- CreateDepthStencilSurface, **CreateOffscreenPlainSurface**, … CreateQuery, etc.
- Exact indices must be taken from the Windows SDK **d3d9.h** declaration order.

**Implementation approach:**

- Add a single module (e.g. `hooks/d3d9/d3d9_device_vtable_logging.cpp/.hpp`) that:
  - Defines detour functions for each IDirect3DDevice9 (and, if desired, IDirect3DDevice9Ex) method we care about, or a small set of generic “log + call original” trampolines keyed by index.
  - Each detour: **CALL_GUARD(utils::get_now_ns())**, then call original, then if return value indicates error (e.g. `FAILED(hr)` for HRESULT, or method-specific rules), log to a dedicated “D3D9 error” log.
  - For **IDirect3DDevice9Ex**-only slots: maintain a per-device or global “first call” bit set per method index; log only when that bit is not set, then set it.
- When we install device hooks (in **HookD3D9Present** or a new **InstallD3D9DeviceVtableLogging(IDirect3DDevice9*)**):
  - Resolve vtable from the device (same way as in **HookD3D9Present**).
  - For each index to hook, call **CreateAndEnableHook(vtable[index], detour, &original, "IDirect3DDevice9::MethodName")**.
- Prefer reusing **HookD3D9Present**’s device vtable resolution and extending it to install the extra vtable slots, so Present(17) and PresentEx(121) stay consistent and we add the rest (CreateOffscreenPlainSurface, etc.) in one place.

**Error logging:** Use a dedicated log helper or channel (e.g. “D3D9 error” or “D3D9 HRESULT”) so error returns are easy to grep and don’t flood the main call log.

---

## 5. CALL_GUARD and Timing

- Every detour must call **CALL_GUARD(utils::get_now_ns())** at entry (same pattern as OpenGL and existing D3D9 Present detours).
- Include `utils/detour_call_tracker.hpp` and `utils/timing.hpp` where needed; use **utils::get_now_ns()** for the timestamp.

---

## 6. Suggested Implementation Order

1. **Add InstallDX9Hooks(HMODULE)** and **UninstallDX9Hooks()** / **AreDX9HooksInstalled()** in a new or existing D3D9 hook file (e.g. `hooks/d3d9/d3d9_hooks.cpp` or extend `d3d9_present_hooks`). Call **InstallDX9Hooks** from the LoadLibrary detour when `d3d9.dll` is loaded.
2. **Wire HookD3D9Present:** From **OnInitDevice(D3D9)** or **OnInitSwapchain(D3D9)**, get the native device and call **HookD3D9Present(device)** so Present/PresentEx are actually installed.
3. **Build IDirect3DDevice9 vtable index table:** From Windows SDK **d3d9.h**, list all IDirect3DDevice9 (and IDirect3DDevice9Ex) methods in order and document their vtable indices in a small reference (e.g. in this doc or a `docs/D3D9_VTABLE_INDICES.md`).
4. **Add device vtable logging:** Implement detours for at least **CreateOffscreenPlainSurface** and a few other methods, with **CALL_GUARD**, call original, and error log on failure. Then extend to all IDirect3DDevice9 methods.
5. **Add IDirect3DDevice9Ex “first call” logic:** For Ex-only indices, log only the first call per method (and all error returns).
6. **Optional:** Hook **Direct3DCreate9** / **CreateDevice** in **InstallDX9Hooks** to install vtable logging on every created device, not only the one provided by ReShade.

---

## 7. Files to Touch

| File / Area | Change |
|-------------|--------|
| `hooks/d3d9/d3d9_present_hooks.cpp` | Keep Present/PresentEx; optionally extend to call a shared “install device vtable logging” routine. |
| New: `hooks/d3d9/d3d9_hooks.cpp` (or similar) | **InstallDX9Hooks**, **UninstallDX9Hooks**, **AreDX9HooksInstalled**; optionally **InstallD3D9DeviceVtableLogging(device)**. |
| `hooks/loadlibrary_hooks.cpp` | When `d3d9.dll` is loaded, call **InstallDX9Hooks(hModule)**. |
| `swapchain_events.cpp` or device init path | When D3D9 device is available, call **HookD3D9Present(device)** and, if implemented, **InstallD3D9DeviceVtableLogging(device)**. |
| New: `docs/D3D9_VTABLE_INDICES.md` (optional) | Table of IDirect3DDevice9 / IDirect3DDevice9Ex vtable indices from d3d9.h. |

---

## 8. Summary

- **InstallDX9Hooks**: Entry point when **d3d9.dll** is loaded (like InstallOpenGLHooks for opengl32); start with state/init only, then optionally hook device creation.
- **Device vtable:** Hook **IDirect3DDevice9** (and **IDirect3DDevice9Ex**) vtable to log **CreateOffscreenPlainSurface** and all other methods; **CALL_GUARD(utils::get_now_ns())** in every detour; separate log for error returns; for Ex, log only first call per function.
- **Fix:** Ensure **HookD3D9Present** is actually invoked when a D3D9 device is available (e.g. from **OnInitDevice** / **OnInitSwapchain**).

---

**Status:** Implemented (initial).
**Implemented:** InstallDX9Hooks (d3d9_hooks.cpp) called from LoadLibrary when d3d9.dll loads; HookD3D9Present and InstallD3D9DeviceVtableLogging called from OnInitSwapchain when D3D9 device is available. Vtable logging hooks CreateOffscreenPlainSurface (28), CreateRenderTarget (26), CreateDepthStencilSurface (27) with CALL_GUARD and separate error log. More IDirect3DDevice9 methods and IDirect3DDevice9Ex first-call-only can be added incrementally.
**Last updated:** 2025-02.
