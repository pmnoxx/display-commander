# OpenGL (WGL) Hooks and FPS Limiter – Task Documentation

## Overview

Display Commander hooks **opengl32.dll** (WGL/OpenGL) to:

- Use **wglSwapBuffers** as an FPS limiter call site (same pattern as D3D9 Present).
- Track present traffic, GPU completion, and swapchain events for OpenGL games.
- Hook all **context-creation** entry points so we see every OpenGL context created by the application.

**References:** `hooks/opengl_hooks.cpp`, `hooks/opengl_hooks.hpp`, `hooks/loadlibrary_hooks.cpp` (opengl32.dll branch), `docs/tasks/install_dx9_hooks_and_vtable_logging.md` (similar pattern for D3D9).

---

## 1. When Are OpenGL Hooks Installed?

- **Trigger:** When the LoadLibrary detour sees **opengl32.dll** being loaded (by the game or by ReShade), it calls **InstallOpenGLHooks(hModule)** with the module handle of the loaded opengl32.dll.
- **Single installation:** Hooks are installed once per process (guarded by `g_opengl_hooks_installed`). Hook suppression (e.g. user setting) and shutdown state are checked before installing.
- **No separate “CreateDevice” DLL:** Unlike D3D9 (d3d9.dll) or DXGI, OpenGL on Windows does not have a separate “device creation” DLL. The same **opengl32.dll** exports both context-creation (wglCreateContext, etc.) and present (wglSwapBuffers). So hooking **opengl32.dll** at load time is the single point where we attach to both context creation and present.

---

## 2. OpenGL “Create Context” / “Create Device” Equivalents – All Hooked

On Windows, an OpenGL “device” is represented by an **HGLRC** (OpenGL rendering context). Contexts are created via:

| Entry point | Hooked? | Notes |
|-------------|---------|--------|
| **wglCreateContext** | Yes | Core WGL; always present. Hooked via `GetProcAddress(opengl_module, "wglCreateContext")` and MinHook on that pointer. |
| **wglCreateContextAttribsARB** | Yes (if available) | Extension for modern GL (e.g. 3.3+). Obtained at install time via `wglGetProcAddress("wglCreateContextAttribsARB")` and hooked if non-null. |

There is no other standard WGL API that creates a new context. So we **do** hook all OpenGL “create context” / “create device” entry points:

- **wglCreateContext** – hooked for every opengl32.dll load.
- **wglCreateContextAttribsARB** – hooked when the extension is reported by the driver (same process, same module).

Additional WGL hooks (for completeness and stats):

- **wglMakeCurrent**, **wglDeleteContext** – context lifecycle.
- **wglChoosePixelFormat**, **wglSetPixelFormat**, **wglGetPixelFormat**, **wglDescribePixelFormat** – pixel format selection (needed before context creation).
- **wglChoosePixelFormatARB**, **wglGetPixelFormatAttribivARB**, **wglGetPixelFormatAttribfvARB** – ARB extensions for pixel format.
- **wglGetProcAddress** – used to resolve extensions (including wglCreateContextAttribsARB).
- **wglSwapIntervalEXT**, **wglGetSwapIntervalEXT** – vsync control.

---

## 3. Present (Swap) – wglSwapBuffers

- **wglSwapBuffers** is the single present/flip entry point for WGL. It is hooked and used as an FPS limiter call site, analogous to **dx9_present** / **dx9_presentex** for D3D9.
- In **wglSwapBuffers_Detour** we:
  - Update timestamps and counters (`g_last_opengl_swapbuffers_time_ns`, `g_opengl_hook_counters`, `g_swapchain_event_total_count`).
  - Call **ChooseFpsLimiter(..., FpsLimiterCallSite::opengl_swapbuffers)** and **GetChosenFpsLimiter(opengl_swapbuffers)** to participate in the global FPS limiter source selection.
  - When chosen: **OnPresentFlags2(true, false)**, **RecordNativeFrameTime()**, and after the original call **HandlePresentAfter(false)**.
  - When **GetChosenFrameTimeLocation() == opengl_swapbuffers**, call **RecordFrameTime(FrameTimeMode::kPresent)**.
  - Call original **wglSwapBuffers_Original(hdc)**, then **HandleOpenGLGPUCompletion()** and **OnPresentUpdateAfter2(false)**.

---

## 4. FPS Limiter Call Site: opengl_swapbuffers

- **FpsLimiterCallSite::opengl_swapbuffers** is added in `globals.hpp` and included in:
  - **kFpsLimiterPriorityOrder** (after dx9_presentex, before dxgi_factory_wrapper and reshade_addon_event).
  - **GetChosenFrameTimeLocation()** as a valid present-path site (so frame time can be recorded from OpenGL present).
  - **FpsLimiterSiteName()** for UI/logging ("opengl_swapbuffers").

So OpenGL games can use the same custom FPS limiter and frame-time tracking as D3D9 and DXGI, with the source chosen automatically based on which path is active.

---

## 5. Files Touched

| File | Change |
|------|--------|
| `globals.hpp` | Add **opengl_swapbuffers** to **FpsLimiterCallSite** enum. |
| `globals.cpp` | Add **opengl_swapbuffers** to priority order, **FpsLimiterSiteName**, and **GetChosenFrameTimeLocation** valid-site list. |
| `hooks/opengl_hooks.cpp` | In **wglSwapBuffers_Detour**: FPS limiter (ChooseFpsLimiter, GetChosenFpsLimiter, OnPresentFlags2, RecordNativeFrameTime, RecordFrameTime when chosen, HandlePresentAfter); **g_swapchain_event_total_count**; include **dxgi/dxgi_present_hooks.hpp** for HandlePresentAfter. |
| `hooks/loadlibrary_hooks.cpp` | When **opengl32.dll** is loaded, call **InstallOpenGLHooks(hModule)** (already present). |

---

## 6. Summary

- **All OpenGL “create context” entry points are hooked:** **wglCreateContext** (always) and **wglCreateContextAttribsARB** (when available), via **InstallOpenGLHooks** when **opengl32.dll** is loaded.
- **Present:** **wglSwapBuffers** is hooked and used as the FPS limiter call site **opengl_swapbuffers**, with the same pattern as **dx9_present** / **dx9_presentex** (ChooseFpsLimiter, GetChosenFpsLimiter, OnPresentFlags2, RecordNativeFrameTime, RecordFrameTime, HandlePresentAfter).
- **Task doc:** This file records that we hook all OpenGL create-context and present entry points and that the FPS limiter is integrated for OpenGL the same way as for D3D9.

---

**Status:** Implemented.  
**Last updated:** 2025-02.
