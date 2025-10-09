#include "d3d9_present_hooks.hpp"

#include <d3d9.h>
#include <MinHook.h>
#include <atomic>

// Forward declarations to avoid including headers that cause redefinition
enum class PresentApiType {
    DX9,
    DXGI
};
extern void OnPresentFlags2(uint32_t *present_flags, PresentApiType api_type);
extern std::atomic<uint32_t> g_swapchain_event_counters[];
extern std::atomic<uint32_t> g_swapchain_event_total_count;
extern void LogInfo(const char *format, ...);
extern void LogWarn(const char *format, ...);

// Event counter index
constexpr int SWAPCHAIN_EVENT_DX9_PRESENT = 73;

namespace display_commanderhooks::d3d9 {

// Original function pointers
IDirect3DDevice9_Present_pfn IDirect3DDevice9_Present_Original = nullptr;
IDirect3DDevice9_PresentEx_pfn IDirect3DDevice9_PresentEx_Original = nullptr;

// Hook state
std::atomic<bool> g_d3d9_present_hooks_installed{false};

// Hooked IDirect3DDevice9::Present function
HRESULT STDMETHODCALLTYPE IDirect3DDevice9_Present_Detour(
    IDirect3DDevice9 *This,
    const RECT *pSourceRect,
    const RECT *pDestRect,
    HWND hDestWindowOverride,
    const RGNDATA *pDirtyRegion)
{
    // Increment DX9 Present counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DX9_PRESENT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Call OnPresentFlags2 with flags = 0 (no flags for regular Present)
    uint32_t present_flags = 0;
    OnPresentFlags2(&present_flags, PresentApiType::DX9);

    // Call original function
    if (IDirect3DDevice9_Present_Original != nullptr) {
        return IDirect3DDevice9_Present_Original(This, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
    }

    // Fallback to direct call if hook failed
    return This->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

// Hooked IDirect3DDevice9::PresentEx function
HRESULT STDMETHODCALLTYPE IDirect3DDevice9_PresentEx_Detour(
    IDirect3DDevice9 *This,
    const RECT *pSourceRect,
    const RECT *pDestRect,
    HWND hDestWindowOverride,
    const RGNDATA *pDirtyRegion,
    DWORD dwFlags)
{
    // Increment DX9 Present counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DX9_PRESENT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Call OnPresentFlags with the actual flags
    uint32_t present_flags = static_cast<uint32_t>(dwFlags);
    OnPresentFlags2(&present_flags, PresentApiType::DX9);

    // Call original function
    if (IDirect3DDevice9_PresentEx_Original != nullptr) {
        return IDirect3DDevice9_PresentEx_Original(This, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
    }

    // Fallback to direct call if hook failed
    // Note: PresentEx is only available on IDirect3DDevice9Ex, so we need to cast
    if (auto *deviceEx = static_cast<IDirect3DDevice9Ex *>(This)) {
        return deviceEx->PresentEx(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
    }
    return D3DERR_INVALIDCALL;
}

bool HookD3D9Present(IDirect3DDevice9 *device) {
    if (device == nullptr) {
        LogWarn("HookD3D9Present: device is nullptr");
        return false;
    }

    if (g_d3d9_present_hooks_installed.load()) {
        LogInfo("HookD3D9Present: hooks already installed");
        return true;
    }

    // Get the vtable from the device
    void **vtable = *reinterpret_cast<void ***>(device);
    if (vtable == nullptr) {
        LogWarn("HookD3D9Present: failed to get vtable from device");
        return false;
    }

    // Hook Present (vtable index 17 for IDirect3DDevice9::Present)
    if (MH_CreateHook(vtable[17], &IDirect3DDevice9_Present_Detour,
                      reinterpret_cast<LPVOID *>(&IDirect3DDevice9_Present_Original)) != MH_OK) {
        LogWarn("HookD3D9Present: failed to create Present hook");
        return false;
    }

    if (MH_EnableHook(vtable[17]) != MH_OK) {
        LogWarn("HookD3D9Present: failed to enable Present hook");
        return false;
    }

    // Hook PresentEx (vtable index 121 for IDirect3DDevice9Ex::PresentEx)
    // Note: PresentEx is only available on IDirect3DDevice9Ex, so we need to check if it exists
    if (MH_CreateHook(vtable[121], &IDirect3DDevice9_PresentEx_Detour,
                      reinterpret_cast<LPVOID *>(&IDirect3DDevice9_PresentEx_Original)) != MH_OK) {
        LogInfo("HookD3D9Present: PresentEx hook not available (device may not be IDirect3DDevice9Ex)");
        // This is not a fatal error, PresentEx is optional
    } else {
        if (MH_EnableHook(vtable[121]) != MH_OK) {
            LogWarn("HookD3D9Present: failed to enable PresentEx hook");
            // This is not fatal, we can continue with just Present
        } else {
            LogInfo("HookD3D9Present: PresentEx hook enabled successfully");
        }
    }

    g_d3d9_present_hooks_installed.store(true);
    LogInfo("HookD3D9Present: hooks installed successfully for device: 0x%p", device);
    return true;
}

void UnhookD3D9Present() {
    if (!g_d3d9_present_hooks_installed.load()) {
        return;
    }

    if (IDirect3DDevice9_Present_Original != nullptr) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_RemoveHook(MH_ALL_HOOKS);
        IDirect3DDevice9_Present_Original = nullptr;
        IDirect3DDevice9_PresentEx_Original = nullptr;
    }

    g_d3d9_present_hooks_installed.store(false);
    LogInfo("UnhookD3D9Present: hooks removed");
}

} // namespace display_commanderhooks::d3d9
