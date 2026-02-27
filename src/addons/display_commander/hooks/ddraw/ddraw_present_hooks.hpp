#pragma once

#include <ddraw.h>
#include <atomic>

namespace display_commanderhooks::ddraw {

// IDirectDrawSurface::Flip - present equivalent for DirectDraw
typedef HRESULT(STDMETHODCALLTYPE* IDirectDrawSurface_Flip_pfn)(LPDIRECTDRAWSURFACE This,
                                                                LPDIRECTDRAWSURFACE lpDDSurfaceTargetOverride,
                                                                DWORD dwFlags);

HRESULT STDMETHODCALLTYPE IDirectDrawSurface_Flip_Detour(LPDIRECTDRAWSURFACE This,
                                                        LPDIRECTDRAWSURFACE lpDDSurfaceTargetOverride, DWORD dwFlags);

// Install hooks when ddraw.dll is loaded (DirectDrawCreate/CreateEx -> CreateSurface -> Flip).
// Skips if hModule is our own (proxy mode).
bool InstallDDrawHooks(HMODULE hModule);
void UninstallDDrawHooks();
bool AreDDrawHooksInstalled();

extern std::atomic<bool> g_ddraw_present_hooks_installed;

}  // namespace display_commanderhooks::ddraw
