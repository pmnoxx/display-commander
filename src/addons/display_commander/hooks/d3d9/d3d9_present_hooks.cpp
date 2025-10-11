#include "d3d9_present_hooks.hpp"
#include "../../dx11_proxy/dx11_proxy_manager.hpp"
#include "../../dx11_proxy/dx11_proxy_settings.hpp"
#include "../../dx11_proxy/dx11_proxy_shared_resources.hpp"
#include "../../globals.hpp"
#include "../../performance_types.hpp"
#include "../../swapchain_events.hpp"

#include <d3d9.h>
#include <MinHook.h>
#include <atomic>
#include <array>

// Forward declarations to avoid including headers that cause redefinition
extern void OnPresentFlags2(uint32_t *present_flags, PresentApiType api_type);
extern void LogInfo(const char *format, ...);
extern void LogWarn(const char *format, ...);

namespace display_commanderhooks::d3d9 {

// Original function pointers
IDirect3DDevice9_Present_pfn IDirect3DDevice9_Present_Original = nullptr;
IDirect3DDevice9_PresentEx_pfn IDirect3DDevice9_PresentEx_Original = nullptr;

// Hook state and device tracking
namespace {
    std::atomic<bool> g_d3d9_present_hooks_installed_internal{false};

    // Track the last D3D9 device used in OnPresentUpdateBefore
    std::atomic<IDirect3DDevice9*> g_last_present_update_device{nullptr};
} // namespace

std::atomic<bool> g_d3d9_present_hooks_installed{false};

// Hooked IDirect3DDevice9::Present function
HRESULT STDMETHODCALLTYPE IDirect3DDevice9_Present_Detour(
    IDirect3DDevice9 *This,
    const RECT *pSourceRect,
    const RECT *pDestRect,
    HWND hDestWindowOverride,
    const RGNDATA *pDirtyRegion)
{
    // Skip if this is not the device used by OnPresentUpdateBefore
    IDirect3DDevice9* expected_device = g_last_present_update_device.load();
    if (expected_device != nullptr && This != expected_device) {
        return IDirect3DDevice9_Present_Original(This, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
    }

    // Increment DX9 Present counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DX9_PRESENT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Call OnPresentFlags2 with flags = 0 (no flags for regular Present)
    uint32_t present_flags = 0;
    OnPresentFlags2(&present_flags, PresentApiType::DX9);

    // Record per-frame FPS sample for background aggregation
    RecordFrameTime(FrameTimeMode::kPresent);

    // DX11 Proxy: Process frame through proxy device if enabled
    if (dx11_proxy::g_dx11ProxySettings.enabled.load()) {
        auto& proxy_manager = dx11_proxy::DX11ProxyManager::GetInstance();
        auto& shared_resources = dx11_proxy::SharedResourceManager::GetInstance();

        if (proxy_manager.IsInitialized()) {
            // Initialize shared resources if not already done
            if (!shared_resources.IsInitialized()) {
                // Get backbuffer to determine format and size
                IDirect3DSurface9* backbuffer = nullptr;
                if (SUCCEEDED(This->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer))) {
                    D3DSURFACE_DESC desc;
                    if (SUCCEEDED(backbuffer->GetDesc(&desc))) {
                        shared_resources.Initialize(
                            This,
                            proxy_manager.GetDevice(),
                            desc.Width,
                            desc.Height,
                            desc.Format
                        );
                    }
                    backbuffer->Release();
                }
            }

            // Transfer frame through shared resources
            if (shared_resources.IsInitialized()) {
                IDirect3DSurface9* backbuffer = nullptr;
                if (SUCCEEDED(This->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer))) {
                    if (shared_resources.TransferFrame(This, backbuffer)) {
                        // Frame transferred successfully
                        proxy_manager.IncrementFrameGenerated();
                    }
                    backbuffer->Release();
                }
            }
        }
    }

    // Call original function
    if (IDirect3DDevice9_Present_Original != nullptr) {
        auto res= IDirect3DDevice9_Present_Original(This, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
        ::OnPresentUpdateAfter2();
        return res;
    }

    // Fallback to direct call if hook failed
    auto res= This->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
    ::OnPresentUpdateAfter2();
    return res;
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
    // Skip if this is not the device used by OnPresentUpdateBefore
    IDirect3DDevice9* expected_device = g_last_present_update_device.load();
    if (expected_device != nullptr && This != expected_device) {
        if (IDirect3DDevice9_PresentEx_Original != nullptr) {
            return IDirect3DDevice9_PresentEx_Original(This, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
        }
        if (auto *deviceEx = static_cast<IDirect3DDevice9Ex *>(This)) {
            return deviceEx->PresentEx(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
        }
        return D3DERR_INVALIDCALL;
    }

    // Increment DX9 Present counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DX9_PRESENT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Call OnPresentFlags with the actual flags
    uint32_t present_flags = static_cast<uint32_t>(dwFlags);
    OnPresentFlags2(&present_flags, PresentApiType::DX9);

    // Record per-frame FPS sample for background aggregation
    RecordFrameTime(FrameTimeMode::kPresent);

    // DX11 Proxy: Process frame through proxy device if enabled
    if (dx11_proxy::g_dx11ProxySettings.enabled.load()) {
        auto& proxy_manager = dx11_proxy::DX11ProxyManager::GetInstance();
        auto& shared_resources = dx11_proxy::SharedResourceManager::GetInstance();

        if (proxy_manager.IsInitialized()) {
            // Initialize shared resources if not already done
            if (!shared_resources.IsInitialized()) {
                // Get backbuffer to determine format and size
                IDirect3DSurface9* backbuffer = nullptr;
                if (SUCCEEDED(This->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer))) {
                    D3DSURFACE_DESC desc;
                    if (SUCCEEDED(backbuffer->GetDesc(&desc))) {
                        shared_resources.Initialize(
                            This,
                            proxy_manager.GetDevice(),
                            desc.Width,
                            desc.Height,
                            desc.Format
                        );
                    }
                    backbuffer->Release();
                }
            }

            // Transfer frame through shared resources
            if (shared_resources.IsInitialized()) {
                IDirect3DSurface9* backbuffer = nullptr;
                if (SUCCEEDED(This->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer))) {
                    if (shared_resources.TransferFrame(This, backbuffer)) {
                        // Frame transferred successfully
                        proxy_manager.IncrementFrameGenerated();
                    }
                    backbuffer->Release();
                }
            }
        }
    }

    // Call original function
    if (IDirect3DDevice9_PresentEx_Original != nullptr) {
        auto res= IDirect3DDevice9_PresentEx_Original(This, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
        ::OnPresentUpdateAfter2();
        return res;
    }

    // Fallback to direct call if hook failed
    // Note: PresentEx is only available on IDirect3DDevice9Ex, so we need to cast
    if (auto *deviceEx = static_cast<IDirect3DDevice9Ex *>(This)) {
        auto res= deviceEx->PresentEx(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
        ::OnPresentUpdateAfter2();
        return res;
    }
    ::OnPresentUpdateAfter2();
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

// Record the D3D9 device used in OnPresentUpdateBefore
void RecordPresentUpdateDevice(IDirect3DDevice9 *device) {
    g_last_present_update_device.store(device);
}

} // namespace display_commanderhooks::d3d9
