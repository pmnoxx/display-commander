# Bug Detection Task

## Overview

**Goal:** Systematically find potential bugs in the Display Commander addon. One point per bug. Fix or document each item.

**Scope:** `src/addons/display_commander/` and addon-specific code under `src/`.

---

## Scoring

- **1 point** per distinct bug (or confirmed defect) found and listed below.
- Document location, symptom, and suggested fix (or “confirm if intentional”).

---

## Fixed bugs

| # | Location | Description | Point |
|---|----------|-------------|-------|
| 1 | `api_hooks.cpp` – `CreateDXGIFactory2_Detour` | **riid not printed correctly:** Fixed – `FormatRefIid(REFIID)` helper used for logging in CreateDXGIFactory2_Detour and D3D12CreateDevice_Detour. | 1 |
| 4 | `api_hooks.cpp` – `CreateDXGIFactory2_Detour` | **ppFactory not validated:** Fixed – early return `E_POINTER` when `ppFactory == nullptr`. | 1 |
| 5 | `api_hooks.cpp` – `GetGUIThreadInfo_Detour` | **pgui used without null check when modifying:** Fixed – guard `if (result && pgui != nullptr)` before modifying pgui. | 1 |
| 11 | `window_proc_hooks.cpp` – WM_WINDOWPOSCHANGED | **pWp dereferenced without null check:** Fixed – guard `if (pWp != nullptr && (pWp->flags & SWP_HIDEWINDOW))`. | 1 |
| 34 | `dxgi_present_hooks.cpp` – GetFullscreenState_Detour | **pFullscreen dereferenced without null check:** Fixed – guard `if (pFullscreen != nullptr)` before write. | 1 |
| 36 | `windows_message_hooks.cpp` – GetCursorPos_Detour | **lpPoint passed to original without null check:** Fixed – early return FALSE and SetLastError(ERROR_INVALID_PARAMETER) when `lpPoint == nullptr`. | 1 |
| 38 | `audio/audio_management.cpp` – WStringToUtf8 | **Buffer overflow when size == 1:** Fixed – `if (size <= 1) return std::string();` before allocating result. | 1 |
| 39 | `windows_message_hooks.cpp` – GetKeyboardState_Detour | **lpKeyState passed to original without null check:** Fixed – early return FALSE and SetLastError(ERROR_INVALID_PARAMETER) when `lpKeyState == nullptr`. | 1 |
| 3 | `api_hooks.cpp` – SetWindowLong* detours | **_Original called without null check:** Fixed – use `Original ? Original(...) : SetWindowLong*/(...)` for all four detours. | 1 |
| 10 | `loadlibrary_hooks.cpp` – GetModuleHandleExW/A_Detour | **phModule nullptr passed to original:** Fixed – when `phModule == nullptr` return FALSE and SetLastError(ERROR_INVALID_PARAMETER) without calling original. | 1 |
| 16 | `rand_hooks.cpp` – Rand_s_Detour | **rand_s called with nullptr:** Fixed – when `randomValue == nullptr` return EINVAL without calling original. | 1 |
| 31 | `window_management.cpp` – ApplyWindowChange, GetDC/ReleaseDC | **ReleaseDC called with wrong hWnd:** Fixed – call `ReleaseDC(swapchain_hwnd, hdc)` instead of `ReleaseDC(nullptr, hdc)`. | 1 |
| 18 | `windows_message_hooks.cpp` – SendInput_Detour | **pInputs not validated when nInputs > 0:** Fixed – early return 0 and SetLastError(ERROR_INVALID_PARAMETER) when nInputs > 0 && pInputs == nullptr. | 1 |
| 24 | `streamline_hooks.cpp` – slUpgradeInterface_Detour | **baseInterface dereferenced without null check:** Fixed – guard `if (baseInterface == nullptr) return result;` before dereferencing. | 1 |
| 25 | `d3d9_present_hooks.cpp` – IDirect3DDevice9_Present_Detour | **_Original null check in early-return path:** Fixed – check Present_Original != nullptr in early-return branch; else fall back to This->Present(...). | 1 |
| 27 | `main_entry.cpp` – DEBUG proxy detection block | **snprintf %ws (MSVC-specific):** Fixed – use `%ls` for wide string in narrow printf (portable). | 1 |
| 37 | `resolution_helpers.cpp` – ApplyDisplaySettingsDXGI LogInfo | **Format/argument count mismatch:** Fixed – use 7 specifiers `%u %u %u %u %u %u %u` with cast for Format/ScanlineOrdering/Scaling. | 1 |
| 40 | `hooks/vulkan/nvlowlatencyvk_hooks.cpp` – NvLL_VK_SetLatencyMarker_Detour | **_Original null check in else branch:** Fixed – guard call with `if (NvLL_VK_SetSleepMode_Original != nullptr)` in else branch. | 1 |

---

## Bug List (open)

| # | Location | Description | Point |
|---|----------|-------------|-------|
| 2 | `api_hooks.cpp` – `InstallDxgiFactoryHooks` | **DXGI factory hooks never installed:** Function starts with `if (true) { return true; }`, so the rest of the function is dead. CreateDXGIFactory/CreateDXGIFactory1/CreateDXGIFactory2 detours are never installed. Either remove the early return (if hooks should run) or document that DXGI factory hook installation is intentionally disabled. | 1 |
| 6 | `windows_message_hooks.cpp` – GetMessageA/W, PeekMessageA/W detours | **lpMsg not validated before first call:** The detours call `GetMessage*_Original(lpMsg, ...)` or `PeekMessage*_Original(lpMsg, ...)` at the start of the loop without checking `lpMsg != nullptr`. If the caller passes nullptr, the Windows API is invoked with a null MSG pointer (undefined behavior / crash). Add an early check at the top: e.g. `if (lpMsg == nullptr) return -1;` (GetMessage) or `return 0;` (PeekMessage). | 1 |
| 7 | `windows_message_hooks.cpp` – TranslateMessage_Detour, DispatchMessageA_Detour, DispatchMessageW_Detour | **lpMsg passed to original without null check:** TranslateMessage_Detour calls `TranslateMessage_Original(lpMsg)` (or TranslateMessage(lpMsg)) without validating lpMsg. DispatchMessageA/W check lpMsg before dereferencing but still call the original with lpMsg at the end; if lpMsg is nullptr that is undefined behavior. Guard: if (lpMsg == nullptr) return appropriate value (FALSE / 0) and skip calling the original. | 1 |
| 8 | `latency_manager.cpp` – `LatencyManager::Shutdown()` | **Shutdown is a no-op:** The entire body of `Shutdown()` is commented out (lines 104–115). The provider is never shut down, `initialized_` is never cleared, and config is not reset. This can leave the latency provider in an inconsistent state and may leak or double-initialize on reinit. Uncomment and run the shutdown logic, or document that shutdown is intentionally disabled. | 1 |
| 9 | `timeslowdown_hooks.cpp` – Timer API detours | **Output pointers passed to original without null check:** QueryPerformanceCounter_Detour, QueryPerformanceFrequency_Detour, GetSystemTime_Detour, GetSystemTimeAsFileTime_Detour, GetSystemTimePreciseAsFileTime_Detour, GetLocalTime_Detour, and NtQuerySystemTime_Detour call the original API with `lpPerformanceCount`, `lpFrequency`, `lpSystemTime`, `lpSystemTimeAsFileTime`, or `SystemTime` without validating they are non-null. Passing nullptr to these Windows APIs is undefined behavior. Add an early return (e.g. `if (lpPerformanceCount == nullptr) return FALSE;`) or guard before calling the original. | 1 |
| 12 | `dxgi_present_hooks.cpp` – IDXGISwapChain_GetDesc_Detour, IDXGISwapChain_GetDesc1_Detour, IDXGISwapChain_CheckColorSpaceSupport_Detour | **Output pointers passed to original without null check:** GetDesc/GetDesc1 call the original with `pDesc` and CheckColorSpaceSupport with `pColorSpaceSupport` without validating they are non-null. These DXGI methods require valid output pointers; passing nullptr is undefined behavior. Add an early return (e.g. E_POINTER) when the output pointer is nullptr before calling the original. | 1 |
| 13 | `dinput_hooks.cpp` – DirectInput8Create_Detour, DirectInputCreateA_Detour, DirectInputCreateW_Detour | **_Original called without null check:** All three detours call `DirectInput8Create_Original(...)`, `DirectInputCreateA_Original(...)`, or `DirectInputCreateW_Original(...)` with no null check. If the hook is installed but the Original pointer was never set (e.g. MinHook edge case or install failure), this can crash. Add `Original ? Original(...) : SystemApi(...)` (or return E_FAIL) when Original is null. | 1 |
| 14 | `display_settings_hooks.cpp` – ChangeDisplaySettingsA/W, ChangeDisplaySettingsExA/W, ShowWindow detours | **_Original called without null check:** The detours call `ChangeDisplaySettingsA_Original`, `ChangeDisplaySettingsW_Original`, `ChangeDisplaySettingsExA_Original`, `ChangeDisplaySettingsExW_Original`, and `ShowWindow_Original` (lines 41, 55, 70, 85, 100, 112, 116) with no null check. Same pattern as api_hooks SetWindowLong* and dinput_hooks: add null check and fallback to the system API when Original is null. | 1 |
| 15 | `opengl_hooks.cpp` – all wgl* detours | **_Original called without null check:** Every wgl*_Detour (wglSwapBuffers, wglMakeCurrent, wglCreateContext, wglDeleteContext, wglChoosePixelFormat, wglSetPixelFormat, wglGetPixelFormat, wglDescribePixelFormat, wglCreateContextAttribsARB, wglChoosePixelFormatARB, wglGetPixelFormatAttribivARB, wglGetPixelFormatAttribfvARB, wglGetProcAddress, wglSwapIntervalEXT, wglGetSwapIntervalEXT) calls the corresponding wgl*_Original with no null check. If a hook is installed but Original was never set, this can crash. Add null check and fallback to the system wgl API (or return a safe error) when Original is null. | 1 |
| 17 | `windows_gaming_input_hooks.cpp` – RoGetActivationFactory_Detour | **_Original called without null check:** Both call sites (blocked-iid path and forward path) call `RoGetActivationFactory_Original(activatableClassId, iid, factory)` with no null check. If the hook is installed but Original was never set, this can crash. Add null check and fallback to the system RoGetActivationFactory (or return E_FAIL) when Original is null. Also consider validating `factory != nullptr` before calling the original, since RoGetActivationFactory requires a valid output pointer. | 1 |
| 19 | `dxgi_factory_wrapper.cpp` – `CreateOutputWrapper` | **Memory leak (in-code TODO):** Comment at L809 states "this is wrong way to do it, there is memory leak here". The function creates/returns an `IDXGIOutput6*` wrapper; if callers do not release it or the wrapper is not ref-counted correctly, resources leak. Fix: implement proper ownership (e.g. ComPtr, or document that the caller must Release the returned pointer) and ensure all code paths that create the wrapper eventually release it. | 1 |
| 20 | `ngx_hooks.cpp` – DLSSOptimalSettingsCallback_Proxy | **GetUI_Original called without null check:** Lines 275–276 call `NVSDK_NGX_Parameter_GetUI_Original(InParams, ...)` for Width/Height with no check that `NVSDK_NGX_Parameter_GetUI_Original` is non-null. SetUI at 289–290 is guarded; GetUI is not. If the Parameter GetUI hook is installed but Original was never set, this can crash. Add null check and early fallback (e.g. call `g_ngx_dlss_optimal_settings_callback_original(InParams)`) when GetUI_Original is null. | 1 |
| 21 | `ngx_hooks.cpp` – NVSDK_NGX_Parameter_GetVoidPointer_Detour | **OutValue not validated before calling original:** The detour calls `NVSDK_NGX_Parameter_GetVoidPointer_Original(InParameter, InName, OutValue)` without checking `OutValue != nullptr`. The API writes through OutValue; passing nullptr is undefined behavior. Add early return (e.g. `NVSDK_NGX_Result_Fail`) when `OutValue == nullptr` before calling the original. | 1 |
| 22 | `streamline_hooks.cpp` – slDLSSGetOptimalSettings_Detour | **_Original called without null check:** Line 319 calls `slDLSSGetOptimalSettings_Original(modified_options, settings)` with no null check. If the hook is installed but Original was never set, this can crash. Add null check and return error (e.g. -1) when Original is null. | 1 |
| 23 | `streamline_hooks.cpp` – slDLSSSetOptions_Detour | **_Original called without null check:** Line 412 calls `slDLSSSetOptions_Original(viewport, modified_options)` with no null check. Same pattern as #22: add null check and return error when Original is null. | 1 |
| 26 | `d3d9_hooks.cpp` – CreateDevice_Detour | **_Original called without null check:** CreateDevice_Detour calls `CreateDevice_Original(...)` at line 171 with no null check. CreateDeviceEx_Detour correctly checks and returns D3DERR_INVALIDCALL. Add the same check at the start of CreateDevice_Detour and return D3DERR_INVALIDCALL when CreateDevice_Original is null. | 1 |
| 28 | `pclstats_etw_hooks.cpp` – EventRegister_Detour | **_Original called without null check; RegHandle output not validated:** EventRegister_Detour calls `EventRegister_Original(ProviderId, EnableCallback, CallbackContext, RegHandle)` at line 91 with no check that EventRegister_Original is non-null. If the hook is installed but Original was never set, this can crash. Also, EventRegister requires a valid PREGHANDLE output when the caller expects a handle; if RegHandle is nullptr, calling the original may be invalid. Add null check for Original and consider returning ERROR_INVALID_PARAMETER when RegHandle == nullptr without calling the original. | 1 |
| 29 | `pclstats_etw_hooks.cpp` – EventWriteTransfer_Detour | **_Original called without null check:** Both call sites (lines 115 and 184) call `EventWriteTransfer_Original(...)` with no check that EventWriteTransfer_Original is non-null. If the hook is installed but Original was never set, this can crash. Add null check and return an appropriate error (e.g. non-zero ULONG) when Original is null. | 1 |
| 30 | `d3d9_device_vtable_logging.cpp` – Create* / GetBackBuffer / CreateAdditionalSwapChain detours | **Output pointers passed to original without null check:** CreateTexture_Detour, CreateVolumeTexture_Detour, CreateCubeTexture_Detour, CreateVertexBuffer_Detour, CreateIndexBuffer_Detour, CreateOffscreenPlainSurface_Detour, CreateRenderTarget_Detour, CreateDepthStencilSurface_Detour, CreateAdditionalSwapChain_Detour, and GetBackBuffer_Detour call the original with output pointers (ppTexture, ppVolumeTexture, ppCubeTexture, ppVertexBuffer, ppIndexBuffer, ppSurface, ppBackBuffer, ppSwapChain) without validating they are non-null. D3D9 APIs require valid output pointers; passing nullptr is undefined behavior. Add early return (e.g. D3DERR_INVALIDCALL) when the relevant output pointer is nullptr before calling the original. | 1 |
| 32 | `ddraw_present_hooks.cpp` – CreateSurface, DirectDrawCreate, DirectDrawCreateEx, Flip detours | **_Original called without null check:** IDirectDraw_CreateSurface_Detour (L44), DirectDrawCreate_Detour (L66), DirectDrawCreateEx_Detour (L87), and IDirectDrawSurface_Flip_Detour (L130) call the corresponding *_Original with no null check. If a hook is installed but Original was never set, this can crash. Add null check and fallback to the system API (or return a safe HRESULT such as E_FAIL) when Original is null. | 1 |
| 33 | `dxgi_present_hooks.cpp` – IDXGISwapChain_Present_Detour, IDXGISwapChain_Present1_Detour | **_Original called without null check in early-return paths:** Present_Detour at lines 404 and 413 returns `IDXGISwapChain_Present_Original(...)` when in_present_call > 0 or when swapchain doesn't match; Present1_Detour at line 461 returns `IDXGISwapChain_Present1_Original(...)` when swapchain doesn't match. None of these paths check that the corresponding _Original is non-null. The main code paths do check and fall back to This->Present / This->Present1. Add null check in each early-return branch and fall back to This->Present(...) / This->Present1(...) when Original is null. | 1 |
| 35 | `dxgi_present_hooks.cpp` – multiple swapchain detours | **_Original called without null check:** GetBuffer_Detour, SetFullscreenState_Detour, GetFullscreenState_Detour (call to Original at 924), ResizeBuffers_Detour, ResizeTarget_Detour, GetContainingOutput_Detour, GetFrameStatistics_Detour, GetLastPresentCount_Detour, GetFullscreenDesc_Detour, GetHwnd_Detour, GetCoreWindow_Detour, IsTemporaryMonoSupported_Detour, and GetRestrictToOutput_Detour all call their respective _Original with no null check. Add null check and return a safe error (e.g. E_FAIL) or fall back to calling the method on This when possible when Original is null. | 1 |

---

## Summary

- **Total so far:** 40 points (40 bugs).
- Add new rows as more bugs are found; keep one point per bug.

---

## How to continue

- Grep for format specifiers vs types (e.g. `%s` with GUID, `%d` with pointer).
- Search for `_Original(` without a preceding null check.
- Look for output parameters (e.g. `void** ppFactory`) used without null checks before calling the original API.
- Look for `if (true)` / `if (false)` that disable whole blocks (dead code or accidental disable).
- In detours that modify structs (e.g. `LPGUITHREADINFO`), add defensive `ptr != nullptr` before dereference.
- In message hooks (GetMessage, PeekMessage, TranslateMessage, DispatchMessage): validate `LPMSG` / `const MSG*` before calling the original API.
- Look for commented-out critical logic (e.g. `Shutdown()` body) that leaves the object in an inconsistent state.
- In timer/output-parameter detours (QPC, GetSystemTime, GetSystemTimeAsFileTime, etc.): validate output pointers before calling the original API.
- In window message handlers (e.g. WM_WINDOWPOSCHANGED): validate lParam-derived pointers (e.g. WINDOWPOS*) before dereferencing.
- In DXGI vtable detours (GetDesc, GetDesc1, CheckColorSpaceSupport): validate output pointers before calling the original.
- In GetModuleHandleEx and similar “out parameter” APIs: do not call the original with a null output pointer; return an error instead.
- In display_settings_hooks (ChangeDisplaySettings*, ShowWindow): guard `*_Original` with null check and fallback to system API (same pattern as api_hooks SetWindowLong* and dinput_hooks).
- Scan other Log*/printf-style calls for format/argument mismatch (e.g. pointer with %d, GUID with %s); bug #1 is the only one documented so far.
- In narrow printf/snprintf, use `%ls` for wide strings (wchar_t*) for portability; `%ws` is MSVC-specific (bug #27).
- Grep for TODO/FIXME that mention "memory leak", "wrong way", or "fixme" and verify whether they describe real bugs (e.g. dxgi_factory_wrapper CreateOutputWrapper → bug #19).
- GetDC/ReleaseDC: the hWnd passed to ReleaseDC must match the hWnd passed to GetDC (bug #31).
- DDraw hooks (ddraw_present_hooks): guard *_Original with null check (bug #32). The `if (true)` in windows_message_hooks at L1962 is inside a comment block (dead comment), not live code.
- DXGI present hooks (dxgi_present_hooks.cpp): guard all *_Original calls with null check (bug #33). loadlibrary_hooks: LdrLoadDll_Original is guarded at L825; FreeLibraryAndExitThread has Original ? Original : system API.
- GetCursorPos: validate lpPoint != nullptr before calling the original (bug #36). GetMouseTranslateScale output pointers are only used by one caller that passes stack addresses; optional defensive check.
- GetKeyboardState: validate lpKeyState != nullptr before calling the original (bug #39). ClipCursor allows lpRect NULL (optional per MSDN); no check needed.
- ToAscii/ToUnicode: lpChar/pwszBuff can be NULL per MSDN (return value only); no validation bug. GetRawInputBuffer/GetRawInputData: pData NULL is valid for size query; detours use result and null-check before deref.
- Container/optional: .front()/.back() on vector/string — ensure !empty() before use (GetFirstReShadeRuntime, main_new_tab frame_times, swapchain_tab, logger queue are guarded). optional .value() — ensure has_value() before use (display_cache, monitor_settings checked).
- size-1 for buffer allocation: when size comes from WideCharToMultiByte etc., ensure size > 1 (or handle size <= 1) before (size - 1) to avoid size_t wrap when size == 0; bug #38 covers size==1 in audio WStringToUtf8. general_utils erase(length()-3) guarded by length() >= 3. pclstats_etw memcpy guarded by len >= 4 and __try/__except.
- Division by variable: ensure divisor is validated (e.g. > 0 or != 0) before use. swapchain_events 1e9/adjusted_target_fps is inside target_fps >= 1.0f. windows_message_hooks TranslateMouseLParam divides by num_x/num_y only when GetMouseTranslateScale returns true (which requires window_w > 0, window_h > 0). globals scale_x uses internal_width only when internal_width > 0 && internal_height > 0.
- find() used in substr or arithmetic: if find returns npos, (npos - pos) wraps in unsigned; substr(pos, npos - pos) is defined but may include unintended content. Prefer checking find result != npos before using in substr (e.g. nvapi/nvpi_reference.cpp ParseSettingValuesBlock). GetProcAddress/LoadLibrary: validate module != nullptr and check GetProcAddress return before calling.
- HRESULT/COM: always check SUCCEEDED(hr) or FAILED(hr) before using output parameters or continuing; swapchain_events and api_hooks follow this. Callback pointer args (e.g. swapchain): validate non-null at entry when caller can pass null (OnInitSwapchain checks; ApplyHdr1000MetadataToSwapchain is only called after that check). Integer overflow: width*height or count*sizeof in int/size_t can overflow for extreme values; display_cache area comparison uses int (bounded in practice).
- Buffer/path size: wcscpy_s/strcpy_s return errno_t; if path or string exceeds buffer (e.g. load_path[MAX_PATH]), truncation or ERANGE; optional to check return (e.g. main_entry injection). delete/free: presentmon destructor deletes atomic-loaded pointers (delete nullptr safe); use exchange() before delete when another thread may replace pointer to avoid double-free.
- assert() in release: with NDEBUG, assert is removed; any invariant that must hold in Release should use a runtime check and early return (e.g. swapchain_events L618 back_buffer_count >= 2; logic above already enforces it). static_assert is compile-time only, no release impact.

---

## Notes

- For format-string issues (e.g. GUID with `%s`), prefer a small helper or inline GUID format so all REFIID logging is consistent.
- For “intentional” early returns (e.g. `if (true) return true;`), add a comment or a named constant (e.g. `SkipDxgiFactoryHooks`) so the intent is clear.
