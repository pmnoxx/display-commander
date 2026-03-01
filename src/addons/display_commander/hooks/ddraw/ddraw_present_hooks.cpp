#include "ddraw_present_hooks.hpp"
#include "../../globals.hpp"
#include "../../gpu_completion_monitoring.hpp"
#include "../../performance_types.hpp"
#include "../../swapchain_events.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"
#include "../present_traffic_tracking.hpp"
#include "../dxgi/dxgi_present_hooks.hpp"

#include <MinHook.h>
#include <atomic>
#include <ddraw.h>

extern void OnPresentFlags2(bool from_present_detour, bool from_wrapper);

namespace {

// IDirectDraw::CreateSurface vtable index (IUnknown 0-2, Compact 3, CreateClipper 4, CreatePalette 5, CreateSurface 6)
constexpr int kIDirectDraw_CreateSurface_Index = 6;
// IDirectDrawSurface::Flip vtable index (IUnknown 0-2, GetCaps 3, GetPixelFormat 4, GetSurfaceDesc 5,
// Lock 6, Unlock 7, GetDC 8, ReleaseDC 9, Flip 10)
constexpr int kIDirectDrawSurface_Flip_Index = 10;

typedef HRESULT(WINAPI* DirectDrawCreate_pfn)(GUID* lpGUID, LPDIRECTDRAW* lplpDD, IUnknown* pUnkOuter);
typedef HRESULT(WINAPI* DirectDrawCreateEx_pfn)(GUID* lpGuid, LPVOID* lplpDD, REFIID iid, IUnknown* pUnkOuter);
typedef HRESULT(STDMETHODCALLTYPE* IDirectDraw_CreateSurface_pfn)(LPDIRECTDRAW This, LPDDSURFACEDESC lpDDSurfaceDesc,
                                                                  LPDIRECTDRAWSURFACE* lplpDDSurface,
                                                                  IUnknown* pUnkOuter);

DirectDrawCreate_pfn DirectDrawCreate_Original = nullptr;
DirectDrawCreateEx_pfn DirectDrawCreateEx_Original = nullptr;
IDirectDraw_CreateSurface_pfn IDirectDraw_CreateSurface_Original = nullptr;
display_commanderhooks::ddraw::IDirectDrawSurface_Flip_pfn IDirectDrawSurface_Flip_Original = nullptr;

std::atomic<bool> g_ddraw_create_surface_hooked{false};
std::atomic<bool> g_ddraw_flip_hooked{false};

HRESULT STDMETHODCALLTYPE IDirectDraw_CreateSurface_Detour(LPDIRECTDRAW This, LPDDSURFACEDESC lpDDSurfaceDesc,
                                                           LPDIRECTDRAWSURFACE* lplpDDSurface, IUnknown* pUnkOuter) {
    HRESULT hr =
        IDirectDraw_CreateSurface_Original(This, lpDDSurfaceDesc, lplpDDSurface, pUnkOuter);
    if (FAILED(hr) || lplpDDSurface == nullptr || *lplpDDSurface == nullptr) {
        return hr;
    }
    // Install Flip hook on the first surface we see (all surfaces share the same vtable)
    if (!g_ddraw_flip_hooked.exchange(true)) {
        void** surface_vtable = *reinterpret_cast<void***>(*lplpDDSurface);
        void* flip_target = surface_vtable[kIDirectDrawSurface_Flip_Index];
        if (CreateAndEnableHook(flip_target,
                                reinterpret_cast<LPVOID>(&display_commanderhooks::ddraw::IDirectDrawSurface_Flip_Detour),
                                reinterpret_cast<LPVOID*>(&IDirectDrawSurface_Flip_Original),
                                "IDirectDrawSurface::Flip")) {
            LogInfo("DDraw: IDirectDrawSurface::Flip hook installed");
        } else {
            g_ddraw_flip_hooked.store(false);
            LogWarn("DDraw: failed to install Flip hook");
        }
    }
    return hr;
}

HRESULT WINAPI DirectDrawCreate_Detour(GUID* lpGUID, LPDIRECTDRAW* lplpDD, IUnknown* pUnkOuter) {
    HRESULT hr = DirectDrawCreate_Original(lpGUID, lplpDD, pUnkOuter);
    if (FAILED(hr) || lplpDD == nullptr || *lplpDD == nullptr) {
        return hr;
    }
    if (!g_ddraw_create_surface_hooked.exchange(true)) {
        void** dd_vtable = *reinterpret_cast<void***>(*lplpDD);
        void* create_surface_target = dd_vtable[kIDirectDraw_CreateSurface_Index];
        if (CreateAndEnableHook(create_surface_target,
                               reinterpret_cast<LPVOID>(&IDirectDraw_CreateSurface_Detour),
                               reinterpret_cast<LPVOID*>(&IDirectDraw_CreateSurface_Original),
                               "IDirectDraw::CreateSurface")) {
            LogInfo("DDraw: IDirectDraw::CreateSurface hook installed");
        } else {
            g_ddraw_create_surface_hooked.store(false);
            LogWarn("DDraw: failed to install CreateSurface hook");
        }
    }
    return hr;
}

HRESULT WINAPI DirectDrawCreateEx_Detour(GUID* lpGuid, LPVOID* lplpDD, REFIID iid, IUnknown* pUnkOuter) {
    HRESULT hr = DirectDrawCreateEx_Original(lpGuid, lplpDD, iid, pUnkOuter);
    if (FAILED(hr) || lplpDD == nullptr || *lplpDD == nullptr) {
        return hr;
    }
    if (!g_ddraw_create_surface_hooked.exchange(true)) {
        // IDirectDraw7 has same base vtable layout for CreateSurface
        void** dd_vtable = *reinterpret_cast<void***>(*lplpDD);
        void* create_surface_target = dd_vtable[kIDirectDraw_CreateSurface_Index];
        if (CreateAndEnableHook(create_surface_target,
                               reinterpret_cast<LPVOID>(&IDirectDraw_CreateSurface_Detour),
                               reinterpret_cast<LPVOID*>(&IDirectDraw_CreateSurface_Original),
                               "IDirectDraw::CreateSurface (Ex)")) {
            LogInfo("DDraw: IDirectDraw::CreateSurface (Ex) hook installed");
        } else {
            g_ddraw_create_surface_hooked.store(false);
            LogWarn("DDraw: failed to install CreateSurface (Ex) hook");
        }
    }
    return hr;
}

}  // namespace

namespace display_commanderhooks::ddraw {

std::atomic<bool> g_ddraw_present_hooks_installed{false};

HRESULT STDMETHODCALLTYPE IDirectDrawSurface_Flip_Detour(LPDIRECTDRAWSURFACE This,
                                                        LPDIRECTDRAWSURFACE lpDDSurfaceTargetOverride, DWORD dwFlags) {
    const LONGLONG now_ns = utils::get_now_ns();
    display_commanderhooks::g_last_ddraw_flip_time_ns.store(static_cast<uint64_t>(now_ns), std::memory_order_relaxed);
    RECORD_DETOUR_CALL(now_ns);

    ChooseFpsLimiter(static_cast<uint64_t>(now_ns), FpsLimiterCallSite::ddraw_flip);
    bool use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::ddraw_flip);
    if (use_fps_limiter) {
        OnPresentFlags2(true, false);
        RecordNativeFrameTime();
    }
    if (GetChosenFrameTimeLocation() == FpsLimiterCallSite::ddraw_flip) {
        RecordFrameTime(FrameTimeMode::kPresent);
    }

    HRESULT res = IDirectDrawSurface_Flip_Original(This, lpDDSurfaceTargetOverride, dwFlags);
    if (FAILED(res)) {
        LogError("[DDraw error] IDirectDrawSurface::Flip returned 0x%08X", static_cast<unsigned>(res));
    }
    if (use_fps_limiter) {
        dxgi::HandlePresentAfter(false);
    }
    HandleOpenGLGPUCompletion();
    OnPresentUpdateAfter2(false);
    return res;
}

bool InstallDDrawHooks(HMODULE hModule) {
    if (g_ddraw_present_hooks_installed.load()) {
        LogInfo("InstallDDrawHooks: DDraw hooks already installed");
        return true;
    }
    if (g_shutdown.load()) {
        LogInfo("InstallDDrawHooks: shutdown in progress, skipping");
        return false;
    }
    if (hModule == nullptr) {
        LogWarn("InstallDDrawHooks: null module handle");
        return false;
    }
    // When we are used as ddraw proxy, the loaded "ddraw.dll" is us – do not hook our own exports
    if (g_hmodule != nullptr && hModule == g_hmodule) {
        LogInfo("InstallDDrawHooks: skipping (module is ourselves, proxy mode)");
        return true;
    }

    auto* pDirectDrawCreate =
        reinterpret_cast<LPVOID>(GetProcAddress(hModule, "DirectDrawCreate"));
    auto* pDirectDrawCreateEx =
        reinterpret_cast<LPVOID>(GetProcAddress(hModule, "DirectDrawCreateEx"));
    if (pDirectDrawCreate == nullptr) {
        LogWarn("InstallDDrawHooks: DirectDrawCreate not found");
        return false;
    }

    if (!CreateAndEnableHook(pDirectDrawCreate, reinterpret_cast<LPVOID>(&DirectDrawCreate_Detour),
                             reinterpret_cast<LPVOID*>(&DirectDrawCreate_Original), "DirectDrawCreate")) {
        LogWarn("InstallDDrawHooks: failed to hook DirectDrawCreate");
        return false;
    }
    if (pDirectDrawCreateEx &&
        !CreateAndEnableHook(pDirectDrawCreateEx, reinterpret_cast<LPVOID>(&DirectDrawCreateEx_Detour),
                             reinterpret_cast<LPVOID*>(&DirectDrawCreateEx_Original), "DirectDrawCreateEx")) {
        LogWarn("InstallDDrawHooks: failed to hook DirectDrawCreateEx (non-fatal)");
    }

    g_ddraw_present_hooks_installed.store(true);
    LogInfo("InstallDDrawHooks: DDraw hooks installed (Flip will hook on first CreateSurface)");
    return true;
}

void UninstallDDrawHooks() {
    if (!g_ddraw_present_hooks_installed.load()) {
        return;
    }
    // MinHook: disable/remove in reverse order; we don't track each hook target here, so just clear state
    g_ddraw_present_hooks_installed.store(false);
    g_ddraw_create_surface_hooked.store(false);
    g_ddraw_flip_hooked.store(false);
    DirectDrawCreate_Original = nullptr;
    DirectDrawCreateEx_Original = nullptr;
    IDirectDraw_CreateSurface_Original = nullptr;
    IDirectDrawSurface_Flip_Original = nullptr;
    LogInfo("UninstallDDrawHooks: DDraw hook state cleared");
}

}  // namespace display_commanderhooks::ddraw
