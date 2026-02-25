# Plan: Add opengl32.dll Proxy Support to Display Commander

## Goal
Allow Display Commander to be used as a proxy for **opengl32.dll**, so that OpenGL games load the addon when they load the system OpenGL DLL. This enables ReShade (and Display Commander) to inject into OpenGL titles when the user renames/copies the addon DLL to `opengl32.dll` in the game directory (or uses the ReShade installer’s OpenGL option, if it supports a custom proxy).

## Reference
- **ReShade exports** for opengl32: `external/reshade/res/exports.def` lines 77–438 (all `gl*` and `wgl*` symbols).
- **Existing proxy pattern**: `proxy_dll/ddraw_proxy.cpp`, `d3d9_proxy.cpp`, `dxgi_proxy.cpp` (load real DLL from system directory, forward every export via `GetProcAddress`).

---

## 1. Export list (exports.def)

- **Action**: Add all opengl32 exports to `src/addons/display_commander/proxy_dll/exports.def`.
- **Source**: Copy the opengl32 section from `external/reshade/res/exports.def` (from `; opengl32.dll` through the last `wgl*` export).
- **Content**: ~336 `gl*` and ~22 `wgl*` symbols, each as `SymbolName PRIVATE` (no ordinals needed for OpenGL).
- **Placement**: Add a new `; opengl32.dll exports` section in the Display Commander `exports.def` (e.g. after `ddraw.dll` and before `dxgi.dll`, or at the end before the ReShade loader / addon exports).

### opengl32 exports (checklist)

- [ ] glAccum
- [ ] glAlphaFunc
- [ ] glAreTexturesResident
- [ ] glArrayElement
- [ ] glBegin
- [ ] glBindTexture
- [ ] glBitmap
- [ ] glBlendFunc
- [ ] glCallList
- [ ] glCallLists
- [ ] glClear
- [ ] glClearAccum
- [ ] glClearColor
- [ ] glClearDepth
- [ ] glClearIndex
- [ ] glClearStencil
- [ ] glClipPlane
- [ ] glColor3b
- [ ] glColor3bv
- [ ] glColor3d
- [ ] glColor3dv
- [ ] glColor3f
- [ ] glColor3fv
- [ ] glColor3i
- [ ] glColor3iv
- [ ] glColor3s
- [ ] glColor3sv
- [ ] glColor3ub
- [ ] glColor3ubv
- [ ] glColor3ui
- [ ] glColor3uiv
- [ ] glColor3us
- [ ] glColor3usv
- [ ] glColor4b
- [ ] glColor4bv
- [ ] glColor4d
- [ ] glColor4dv
- [ ] glColor4f
- [ ] glColor4fv
- [ ] glColor4i
- [ ] glColor4iv
- [ ] glColor4s
- [ ] glColor4sv
- [ ] glColor4ub
- [ ] glColor4ubv
- [ ] glColor4ui
- [ ] glColor4uiv
- [ ] glColor4us
- [ ] glColor4usv
- [ ] glColorMask
- [ ] glColorMaterial
- [ ] glColorPointer
- [ ] glCopyPixels
- [ ] glCopyTexImage1D
- [ ] glCopyTexImage2D
- [ ] glCopyTexSubImage1D
- [ ] glCopyTexSubImage2D
- [ ] glCullFace
- [ ] glDeleteLists
- [ ] glDeleteTextures
- [ ] glDepthFunc
- [ ] glDepthMask
- [ ] glDepthRange
- [ ] glDisable
- [ ] glDisableClientState
- [ ] glDrawArrays
- [ ] glDrawBuffer
- [ ] glDrawElements
- [ ] glDrawPixels
- [ ] glEdgeFlag
- [ ] glEdgeFlagPointer
- [ ] glEdgeFlagv
- [ ] glEnable
- [ ] glEnableClientState
- [ ] glEnd
- [ ] glEndList
- [ ] glEvalCoord1d
- [ ] glEvalCoord1dv
- [ ] glEvalCoord1f
- [ ] glEvalCoord1fv
- [ ] glEvalCoord2d
- [ ] glEvalCoord2dv
- [ ] glEvalCoord2f
- [ ] glEvalCoord2fv
- [ ] glEvalMesh1
- [ ] glEvalMesh2
- [ ] glEvalPoint1
- [ ] glEvalPoint2
- [ ] glFeedbackBuffer
- [ ] glFinish
- [ ] glFlush
- [ ] glFogf
- [ ] glFogfv
- [ ] glFogi
- [ ] glFogiv
- [ ] glFrontFace
- [ ] glFrustum
- [ ] glGenLists
- [ ] glGenTextures
- [ ] glGetBooleanv
- [ ] glGetClipPlane
- [ ] glGetDoublev
- [ ] glGetError
- [ ] glGetFloatv
- [ ] glGetIntegerv
- [ ] glGetLightfv
- [ ] glGetLightiv
- [ ] glGetMapdv
- [ ] glGetMapfv
- [ ] glGetMapiv
- [ ] glGetMaterialfv
- [ ] glGetMaterialiv
- [ ] glGetPixelMapfv
- [ ] glGetPixelMapuiv
- [ ] glGetPixelMapusv
- [ ] glGetPointerv
- [ ] glGetPolygonStipple
- [ ] glGetString
- [ ] glGetTexEnvfv
- [ ] glGetTexEnviv
- [ ] glGetTexGendv
- [ ] glGetTexGenfv
- [ ] glGetTexGeniv
- [ ] glGetTexImage
- [ ] glGetTexLevelParameterfv
- [ ] glGetTexLevelParameteriv
- [ ] glGetTexParameterfv
- [ ] glGetTexParameteriv
- [ ] glHint
- [ ] glIndexMask
- [ ] glIndexPointer
- [ ] glIndexd
- [ ] glIndexdv
- [ ] glIndexf
- [ ] glIndexfv
- [ ] glIndexi
- [ ] glIndexiv
- [ ] glIndexs
- [ ] glIndexsv
- [ ] glIndexub
- [ ] glIndexubv
- [ ] glInitNames
- [ ] glInterleavedArrays
- [ ] glIsEnabled
- [ ] glIsList
- [ ] glIsTexture
- [ ] glLightModelf
- [ ] glLightModelfv
- [ ] glLightModeli
- [ ] glLightModeliv
- [ ] glLightf
- [ ] glLightfv
- [ ] glLighti
- [ ] glLightiv
- [ ] glLineStipple
- [ ] glLineWidth
- [ ] glListBase
- [ ] glLoadIdentity
- [ ] glLoadMatrixd
- [ ] glLoadMatrixf
- [ ] glLoadName
- [ ] glLogicOp
- [ ] glMap1d
- [ ] glMap1f
- [ ] glMap2d
- [ ] glMap2f
- [ ] glMapGrid1d
- [ ] glMapGrid1f
- [ ] glMapGrid2d
- [ ] glMapGrid2f
- [ ] glMaterialf
- [ ] glMaterialfv
- [ ] glMateriali
- [ ] glMaterialiv
- [ ] glMatrixMode
- [ ] glMultMatrixd
- [ ] glMultMatrixf
- [ ] glNewList
- [ ] glNormal3b
- [ ] glNormal3bv
- [ ] glNormal3d
- [ ] glNormal3dv
- [ ] glNormal3f
- [ ] glNormal3fv
- [ ] glNormal3i
- [ ] glNormal3iv
- [ ] glNormal3s
- [ ] glNormal3sv
- [ ] glNormalPointer
- [ ] glOrtho
- [ ] glPassThrough
- [ ] glPixelMapfv
- [ ] glPixelMapuiv
- [ ] glPixelMapusv
- [ ] glPixelStoref
- [ ] glPixelStorei
- [ ] glPixelTransferf
- [ ] glPixelTransferi
- [ ] glPixelZoom
- [ ] glPointSize
- [ ] glPolygonMode
- [ ] glPolygonOffset
- [ ] glPolygonStipple
- [ ] glPopAttrib
- [ ] glPopClientAttrib
- [ ] glPopMatrix
- [ ] glPopName
- [ ] glPrioritizeTextures
- [ ] glPushAttrib
- [ ] glPushClientAttrib
- [ ] glPushMatrix
- [ ] glPushName
- [ ] glRasterPos2d
- [ ] glRasterPos2dv
- [ ] glRasterPos2f
- [ ] glRasterPos2fv
- [ ] glRasterPos2i
- [ ] glRasterPos2iv
- [ ] glRasterPos2s
- [ ] glRasterPos2sv
- [ ] glRasterPos3d
- [ ] glRasterPos3dv
- [ ] glRasterPos3f
- [ ] glRasterPos3fv
- [ ] glRasterPos3i
- [ ] glRasterPos3iv
- [ ] glRasterPos3s
- [ ] glRasterPos3sv
- [ ] glRasterPos4d
- [ ] glRasterPos4dv
- [ ] glRasterPos4f
- [ ] glRasterPos4fv
- [ ] glRasterPos4i
- [ ] glRasterPos4iv
- [ ] glRasterPos4s
- [ ] glRasterPos4sv
- [ ] glReadBuffer
- [ ] glReadPixels
- [ ] glRectd
- [ ] glRectdv
- [ ] glRectf
- [ ] glRectfv
- [ ] glRecti
- [ ] glRectiv
- [ ] glRects
- [ ] glRectsv
- [ ] glRenderMode
- [ ] glRotated
- [ ] glRotatef
- [ ] glScaled
- [ ] glScalef
- [ ] glScissor
- [ ] glSelectBuffer
- [ ] glShadeModel
- [ ] glStencilFunc
- [ ] glStencilMask
- [ ] glStencilOp
- [ ] glTexCoord1d
- [ ] glTexCoord1dv
- [ ] glTexCoord1f
- [ ] glTexCoord1fv
- [ ] glTexCoord1i
- [ ] glTexCoord1iv
- [ ] glTexCoord1s
- [ ] glTexCoord1sv
- [ ] glTexCoord2d
- [ ] glTexCoord2dv
- [ ] glTexCoord2f
- [ ] glTexCoord2fv
- [ ] glTexCoord2i
- [ ] glTexCoord2iv
- [ ] glTexCoord2s
- [ ] glTexCoord2sv
- [ ] glTexCoord3d
- [ ] glTexCoord3dv
- [ ] glTexCoord3f
- [ ] glTexCoord3fv
- [ ] glTexCoord3i
- [ ] glTexCoord3iv
- [ ] glTexCoord3s
- [ ] glTexCoord3sv
- [ ] glTexCoord4d
- [ ] glTexCoord4dv
- [ ] glTexCoord4f
- [ ] glTexCoord4fv
- [ ] glTexCoord4i
- [ ] glTexCoord4iv
- [ ] glTexCoord4s
- [ ] glTexCoord4sv
- [ ] glTexCoordPointer
- [ ] glTexEnvf
- [ ] glTexEnvfv
- [ ] glTexEnvi
- [ ] glTexEnviv
- [ ] glTexGend
- [ ] glTexGendv
- [ ] glTexGenf
- [ ] glTexGenfv
- [ ] glTexGeni
- [ ] glTexGeniv
- [ ] glTexImage1D
- [ ] glTexImage2D
- [ ] glTexParameterf
- [ ] glTexParameterfv
- [ ] glTexParameteri
- [ ] glTexParameteriv
- [ ] glTexSubImage1D
- [ ] glTexSubImage2D
- [ ] glTranslated
- [ ] glTranslatef
- [ ] glVertex2d
- [ ] glVertex2dv
- [ ] glVertex2f
- [ ] glVertex2fv
- [ ] glVertex2i
- [ ] glVertex2iv
- [ ] glVertex2s
- [ ] glVertex2sv
- [ ] glVertex3d
- [ ] glVertex3dv
- [ ] glVertex3f
- [ ] glVertex3fv
- [ ] glVertex3i
- [ ] glVertex3iv
- [ ] glVertex3s
- [ ] glVertex3sv
- [ ] glVertex4d
- [ ] glVertex4dv
- [ ] glVertex4f
- [ ] glVertex4fv
- [ ] glVertex4i
- [ ] glVertex4iv
- [ ] glVertex4s
- [ ] glVertex4sv
- [ ] glVertexPointer
- [ ] glViewport
- [ ] wglChoosePixelFormat
- [ ] wglCopyContext
- [ ] wglCreateContext
- [ ] wglCreateLayerContext
- [ ] wglDeleteContext
- [ ] wglDescribeLayerPlane
- [ ] wglDescribePixelFormat
- [ ] wglGetCurrentContext
- [ ] wglGetCurrentDC
- [ ] wglGetLayerPaletteEntries
- [ ] wglGetPixelFormat
- [ ] wglGetProcAddress
- [ ] wglMakeCurrent
- [ ] wglRealizeLayerPalette
- [ ] wglSetLayerPaletteEntries
- [ ] wglSetPixelFormat
- [ ] wglShareLists
- [ ] wglSwapBuffers
- [ ] wglSwapLayerBuffers
- [ ] wglSwapMultipleBuffers
- [ ] wglUseFontBitmapsA
- [ ] wglUseFontBitmapsW
- [ ] wglUseFontOutlinesA
- [ ] wglUseFontOutlinesW

---

## 2. Proxy implementation (opengl32_proxy.cpp + init)

- **New file**: `proxy_dll/opengl32_proxy.cpp`
  - Load real **opengl32.dll** from the system directory (`GetSystemDirectoryW` + `LoadLibraryW`) on first use, same pattern as `ddraw_proxy.cpp` / `d3d9_proxy.cpp`.
  - Implement one **forwarding stub per export**: `extern "C" ... SymbolName(...) { LoadRealOpenGL32(); auto fn = (PFN)GetProcAddress(g_opengl32_module, "SymbolName"); return fn(...); }`.
- **New header**: `proxy_dll/opengl32_proxy_init.hpp`
  - Declare `void LoadRealOpenGL32FromDllMain();` for preload from `DllMain` (safe: system path only).
- **Number of symbols**: ~358. Options:
  - **A) Code generation (recommended)**  
    - Script (e.g. Python or CMake) that parses the opengl32 section of ReShade’s `exports.def` and generates `opengl32_proxy.cpp` with correct signatures.  
    - Signatures can be taken from a single canonical list (e.g. Windows `GL/gl.h` + `GL/wglext.h` for the base 1.1 + WGL set), or from ReShade’s headers if available.
  - **B) Macro-based one-liners**  
    - Define macros for common return/arg patterns (e.g. `GLAPI void GLAPIENTRY glX(...)`, `GLAPI GLuint GLAPIENTRY glY(...)`) and generate 358 lines of macro invocations.  
    - Still requires correct types (GLenum, GLsizei, etc.) from a minimal GL header or local typedefs.
- **Types**: Use a minimal set of OpenGL types for the proxy (e.g. include `GL/gl.h` and `GL/wglext.h` only for the base 1.1 + WGL exports, or equivalent from ReShade’s deps). Do **not** link `opengl32.lib` in the proxy build (see §5).
- **Critical export**: `wglGetProcAddress` must forward to the real opengl32 so extension function pointers (e.g. `glCreateShader`) are returned by the real driver.

---

## 3. Proxy detection and entry-point wiring

- **proxy_detection.hpp**
  - In `IsProxyDllMode()`, add: `module_name == L"opengl32.dll"` so the addon recognizes when it is loaded as opengl32.
- **main_entry.cpp**
  - In `DLL_PROCESS_ATTACH`, call `LoadRealOpenGL32FromDllMain();` together with the existing `LoadRealDXGIFromDllMain`, `LoadRealDDrawFromDllMain`, `LoadRealD3D9FromDllMain()`.
  - In the `ProxyDllInfo` list, add:
    - `{ L"opengl32", L"opengl32.dll", "[DisplayCommander] Entry point detected: opengl32.dll (proxy mode)\n", "Display Commander loaded as opengl32.dll proxy - OpenGL/WGL functions will be forwarded to system opengl32.dll" }`
  - Include `proxy_dll/opengl32_proxy_init.hpp` with the other proxy init headers.

---

## 4. Build system

- **CMakeLists.txt**
  - Add `proxy_dll/opengl32_proxy.cpp` to the target sources for `zzz_display_commander`.
  - No new link libraries for the proxy itself (forwarding only).

---

## 5. Link conflict: opengl32.lib

- **Issue**: The addon currently links `opengl32` (for standalone ImGui OpenGL backend in `cli_standalone_ui.cpp` and `imgui_impl_opengl3.cpp`). If we **export** all `gl*` and `wgl*` from the same DLL, we cannot also **import** them via `opengl32.lib` (same symbol names would be both exported and imported → link or runtime conflict).
- **Conclusion**: To support a single-DLL opengl32 proxy build, we must **stop linking opengl32.lib** and load OpenGL at runtime where needed.
- **Actions**:
  1. **Remove** `opengl32` from `target_link_libraries(zzz_display_commander ...)` in `CMakeLists.txt`.
  2. **Remove** `#pragma comment(lib, "opengl32.lib")` from `ui/cli_standalone_ui.cpp` (if present).
  3. **Standalone UI OpenGL usage**: Replace direct `gl*` / `wgl*` calls with a small **runtime OpenGL loader**:
     - When the DLL is loaded **as opengl32.dll** (proxy mode): the “OpenGL” module is ourselves; our exports forward to the real opengl32. The standalone UI can call its own exports (which forward), or use `GetModuleHandleW(L"opengl32")` and get our forwarding stubs. So it can keep calling `wglCreateContext`, `wglMakeCurrent`, `glClear`, etc. as long as those symbols resolve to our proxy (they will when the process loads “opengl32” = us).
     - When the DLL is loaded **as addon** (e.g. `zzz_display_commander.dll`): there is no opengl32 in process yet. The standalone UI must explicitly load the **real** opengl32 from the system directory (`GetSystemDirectoryW` + `LoadLibraryW("opengl32.dll")`) and use `GetProcAddress` for every `gl*` / `wgl*` it needs (or use a small table of function pointers filled at init). So:
       - Add a tiny “OpenGL loader” helper used only by the standalone UI: load system `opengl32.dll` when not in opengl32-proxy mode; when in proxy mode, use `GetModuleHandle("opengl32")` (our DLL) and `GetProcAddress` for the same names (our exports). Then the standalone UI calls through this helper instead of linking to opengl32.
  4. **ImGui OpenGL backend** (`imgui_impl_opengl3.cpp`): It expects function pointers or linked symbols. Either:
     - Build a small “gl_load” layer that provides the backend with function pointers (from the loader above), and use a custom/patched backend that uses that layer, or
     - Keep the backend as-is and ensure the process’s “opengl32” is always the right one: when we’re addon we load system opengl32 early so that `GetProcAddress(GetModuleHandle("opengl32"), "glClear")` etc. return the real functions. When we’re proxy, our DLL is opengl32 and our exports forward. So the key is: **never link opengl32.lib**; always resolve `gl*`/`wgl*` via GetProcAddress from the module that is “opengl32” (us in proxy case, or the system DLL we loaded in addon case).

---

## 6. ReShade loader (opengl32 proxy)

- When the addon is loaded as **opengl32.dll**, the existing ReShade loading path (e.g. `LoadReShadeDll()` from `reshade_loader.cpp`) should still run so that ReShade gets loaded. ReShade’s OpenGL layer will then call “opengl32” (our DLL); our proxy forwards to the real opengl32, so ReShade sits between the game and the real driver. No change to the loader is required unless ReShade’s OpenGL build expects a different DLL name or entry point; confirm with ReShade’s OpenGL documentation/source.

---

## 7. Testing and deployment

- **Build**: Build the addon; ensure no `opengl32.lib` link and that all opengl32 exports are satisfied by `opengl32_proxy.cpp`.
- **Rename/copy**: User renames (or copies) the built addon DLL to `opengl32.dll` and places it in the game directory (or uses the ReShade installer’s OpenGL option if it supports a custom proxy path).
- **Smoke test**: Run an OpenGL game that loads opengl32; confirm the game runs and ReShade overlay works if applicable.
- **Standalone UI**: Run the standalone settings UI (e.g. via rundll32) both when the DLL is loaded as addon and (if possible) as opengl32; confirm the OpenGL-backed window still works.

---

## 8. Optional follow-ups

- **d3d9 / ddraw in ProxyDllInfo**: The current `proxy_dlls` array in `main_entry.cpp` does not include `d3d9` or `ddraw`; only dxgi, d3d11, d3d12, version are listed. Consider adding d3d9 and ddraw to that list for consistent entry-point and logging behavior when loaded as those proxies.
- **Documentation**: Update user-facing docs (e.g. README or installer text) to state that Display Commander can be used as an opengl32 proxy and how to install it for OpenGL games.
- **CHANGELOG**: Add an entry for “opengl32.dll proxy support”.

---

## 9. Summary checklist

| Step | Item |
|------|------|
| 1 | Add all gl* and wgl* exports to `proxy_dll/exports.def` (from ReShade’s exports.def). |
| 2 | Implement `opengl32_proxy.cpp` (forward each export to real opengl32) and `opengl32_proxy_init.hpp`. Prefer code generation from the export list. |
| 3 | Update `proxy_detection.hpp` (add opengl32.dll). |
| 4 | Update `main_entry.cpp`: preload real opengl32 in DllMain, add opengl32 to ProxyDllInfo, include opengl32_proxy_init.hpp. |
| 5 | Remove `opengl32` from `target_link_libraries` and from `#pragma comment(lib)` in standalone UI. |
| 6 | Add runtime OpenGL loader for standalone UI (resolve gl*/wgl* via GetProcAddress from system opengl32 when addon, or from “opengl32” when proxy). |
| 7 | Add `opengl32_proxy.cpp` to CMake target sources. |
| 8 | Test as addon and as opengl32 proxy; update docs/CHANGELOG as needed. |

---

## Implementation score (after implementation)

To be filled after implementation: score in (-∞, +∞) with brief justification (code quality, maintainability, correctness, adherence to project patterns, completeness).
