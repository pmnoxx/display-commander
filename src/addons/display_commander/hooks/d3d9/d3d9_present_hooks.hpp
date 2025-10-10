#pragma once

#include <d3d9.h>
#include <atomic>

namespace display_commanderhooks::d3d9 {

// Function pointer types for original DX9 Present functions
typedef HRESULT (STDMETHODCALLTYPE *IDirect3DDevice9_Present_pfn)(
    IDirect3DDevice9 *This,
    const RECT *pSourceRect,
    const RECT *pDestRect,
    HWND hDestWindowOverride,
    const RGNDATA *pDirtyRegion
);

typedef HRESULT (STDMETHODCALLTYPE *IDirect3DDevice9_PresentEx_pfn)(
    IDirect3DDevice9 *This,
    const RECT *pSourceRect,
    const RECT *pDestRect,
    HWND hDestWindowOverride,
    const RGNDATA *pDirtyRegion,
    DWORD dwFlags
);

// Hooked Present functions
HRESULT STDMETHODCALLTYPE IDirect3DDevice9_Present_Detour(
    IDirect3DDevice9 *This,
    const RECT *pSourceRect,
    const RECT *pDestRect,
    HWND hDestWindowOverride,
    const RGNDATA *pDirtyRegion
);

HRESULT STDMETHODCALLTYPE IDirect3DDevice9_PresentEx_Detour(
    IDirect3DDevice9 *This,
    const RECT *pSourceRect,
    const RECT *pDestRect,
    HWND hDestWindowOverride,
    const RGNDATA *pDirtyRegion,
    DWORD dwFlags
);

// Hook management
bool HookD3D9Present(IDirect3DDevice9 *device);
void UnhookD3D9Present();

// Record the D3D9 device used in OnPresentUpdateBefore
void RecordPresentUpdateDevice(IDirect3DDevice9 *device);

// Hook state
extern std::atomic<bool> g_d3d9_present_hooks_installed;

} // namespace display_commanderhooks::d3d9
