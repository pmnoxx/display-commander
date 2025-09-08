#include "dx11_hooks.hpp"
#include "dxgi_factory_hooks.hpp"
#include "factory_detector.hpp"
#include "swapchain_detector.hpp"
#include "../../utils.hpp"
#include <MinHook.h>
#include <unordered_set>
#include <mutex>

namespace renodx::hooks::directx {

// Original function pointers
IDXGISwapChain_Present_pfn IDXGISwapChain_Present_Original = nullptr;
IDXGISwapChain_Present1_pfn IDXGISwapChain_Present1_Original = nullptr;

// Hook state
std::atomic<bool> g_dx11_hooks_installed{false};

// Track hooked swap chains to avoid double-hooking
static std::unordered_set<IDXGISwapChain*> g_hooked_swapchains;
static std::unordered_set<IDXGISwapChain1*> g_hooked_swapchains1;
static std::mutex g_swapchain_mutex;

// Hooked Present function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present_Detour(
    IDXGISwapChain *This, UINT SyncInterval, UINT Flags)
{
    // Log all arguments
    LogInfo("DX11 Present called - SwapChain: 0x%p, SyncInterval: %u, Flags: 0x%x",
            This, SyncInterval, Flags);

    // Call present callback
    OnPresent(This, SyncInterval, Flags);

    // Modify flags if needed
    UINT modifiedFlags = ModifyPresentFlags(Flags);
    if (modifiedFlags != Flags) {
        LogInfo("DX11 Present flags modified: 0x%x -> 0x%x", Flags, modifiedFlags);
    }

    // Call original function
    HRESULT hr = IDXGISwapChain_Present_Original ?
        IDXGISwapChain_Present_Original(This, SyncInterval, modifiedFlags) :
        This->Present(SyncInterval, modifiedFlags);

    // Call finish present callback
    OnFinishPresent(This, hr);

    return hr;
}

// Hooked Present1 function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present1_Detour(
    IDXGISwapChain1 *This, UINT SyncInterval, UINT PresentFlags,
    const DXGI_PRESENT_PARAMETERS *pPresentParameters)
{
    // Log all arguments
    LogInfo("DX11 Present1 called - SwapChain: 0x%p, SyncInterval: %u, PresentFlags: 0x%x, pPresentParameters: 0x%p",
            This, SyncInterval, PresentFlags, pPresentParameters);

    if (pPresentParameters) {
        LogInfo("  PresentParameters - DirtyRects: %u, pDirtyRects: 0x%p, pScrollRect: 0x%p, pScrollOffset: 0x%p",
                pPresentParameters->DirtyRectsCount,
                pPresentParameters->pDirtyRects,
                pPresentParameters->pScrollRect,
                pPresentParameters->pScrollOffset);
    }

    // Call present callback
    OnPresent1(This, SyncInterval, PresentFlags, pPresentParameters);

    // Modify flags if needed
    UINT modifiedFlags = ModifyPresent1Flags(PresentFlags);
    if (modifiedFlags != PresentFlags) {
        LogInfo("DX11 Present1 flags modified: 0x%x -> 0x%x", PresentFlags, modifiedFlags);
    }

    // Call original function
    HRESULT hr = IDXGISwapChain_Present1_Original ?
        IDXGISwapChain_Present1_Original(This, SyncInterval, modifiedFlags, pPresentParameters) :
        This->Present1(SyncInterval, modifiedFlags, pPresentParameters);

    // Call finish present callback
    OnFinishPresent1(This, hr);

    return hr;
}

bool InstallDX11Hooks() {
    if (g_dx11_hooks_installed.load()) {
        LogInfo("DX11 hooks already installed");
        return true;
    }

    // Initialize MinHook if needed
    MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for DX11 hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with DX11 hooks");
    } else {
        LogInfo("MinHook initialized successfully for DX11 hooks");
    }

    // Install DXGI factory hooks to catch swap chain creation
    if (!InstallDXGIFactoryHooks()) {
        LogError("Failed to install DXGI factory hooks");
        return false;
    }

    // Try to detect and hook existing factories
    if (FactoryDetector::HookDetectedFactories()) {
        LogInfo("Successfully hooked detected DXGI factories");
    } else {
        LogInfo("No existing factories detected, will hook new ones as they're created");
    }

    // Start monitoring for new factories
    FactoryDetector::StartMonitoring();

    g_dx11_hooks_installed.store(true);
    LogInfo("DX11 hooks installed successfully");
    return true;
}

void UninstallDX11Hooks() {
    if (!g_dx11_hooks_installed.load()) {
        LogInfo("DX11 hooks not installed");
        return;
    }

    // Stop monitoring
    FactoryDetector::StopMonitoring();

    // Uninstall DXGI factory hooks
    UninstallDXGIFactoryHooks();

    // Unhook all tracked swap chains
    {
        std::lock_guard<std::mutex> lock(g_swapchain_mutex);
        for (auto* swapchain : g_hooked_swapchains) {
            UnhookSwapChainVTable(swapchain);
        }
        g_hooked_swapchains.clear();

        for (auto* swapchain : g_hooked_swapchains1) {
            UnhookSwapChain1VTable(swapchain);
        }
        g_hooked_swapchains1.clear();
    }

    g_dx11_hooks_installed.store(false);
    LogInfo("DX11 hooks uninstalled successfully");
}

bool AreDX11HooksInstalled() {
    return g_dx11_hooks_installed.load();
}

void OnPresent(IDXGISwapChain* swapChain, UINT SyncInterval, UINT Flags) {
    // Your present handling logic here
    LogInfo("DX11 OnPresent callback - SwapChain: 0x%p, SyncInterval: %u, Flags: 0x%x",
            swapChain, SyncInterval, Flags);

    // You can access the back buffer, perform post-processing, etc.
    // This is where you'd implement your display management features
}

void OnPresent1(IDXGISwapChain1* swapChain, UINT SyncInterval, UINT PresentFlags,
                const DXGI_PRESENT_PARAMETERS *pPresentParameters) {
    // Your present1 handling logic here
    LogInfo("DX11 OnPresent1 callback - SwapChain: 0x%p, SyncInterval: %u, PresentFlags: 0x%x",
            swapChain, SyncInterval, PresentFlags);
}

void OnFinishPresent(IDXGISwapChain* swapChain, HRESULT hr) {
    // Post-present handling
    if (FAILED(hr)) {
        LogError("DX11 Present failed - SwapChain: 0x%p, HRESULT: 0x%x", swapChain, hr);
    } else {
        LogInfo("DX11 Present succeeded - SwapChain: 0x%p", swapChain);
    }
}

void OnFinishPresent1(IDXGISwapChain1* swapChain, HRESULT hr) {
    // Post-present1 handling
    if (FAILED(hr)) {
        LogError("DX11 Present1 failed - SwapChain: 0x%p, HRESULT: 0x%x", swapChain, hr);
    } else {
        LogInfo("DX11 Present1 succeeded - SwapChain: 0x%p", swapChain);
    }
}

UINT ModifyPresentFlags(UINT originalFlags) {
    // Modify present flags here based on your display management needs
    UINT modifiedFlags = originalFlags;

    // Example: Force certain flags based on your requirements
    // if (s_continue_rendering.load()) {
    //     modifiedFlags |= DXGI_PRESENT_DO_NOT_WAIT;  // Don't wait for vsync
    // }

    return modifiedFlags;
}

UINT ModifyPresent1Flags(UINT originalFlags) {
    // Modify present1 flags here based on your display management needs
    UINT modifiedFlags = originalFlags;

    // Example: Force certain flags based on your requirements
    // if (s_continue_rendering.load()) {
    //     modifiedFlags |= DXGI_PRESENT_DO_NOT_WAIT;  // Don't wait for vsync
    // }

    return modifiedFlags;
}

bool HookSwapChainVTable(IDXGISwapChain* swapChain) {
    if (!swapChain) return false;

    std::lock_guard<std::mutex> lock(g_swapchain_mutex);

    // Check if already hooked
    if (g_hooked_swapchains.find(swapChain) != g_hooked_swapchains.end()) {
        return true;
    }

    // Get vtable
    void** vtable = *(void***)swapChain;

    // Hook Present method (index 8 in IDXGISwapChain vtable)
    if (MH_CreateHook(vtable[8], IDXGISwapChain_Present_Detour,
                      (LPVOID*)&IDXGISwapChain_Present_Original) != MH_OK) {
        LogError("Failed to create IDXGISwapChain::Present hook");
        return false;
    }

    if (MH_EnableHook(vtable[8]) != MH_OK) {
        LogError("Failed to enable IDXGISwapChain::Present hook");
        MH_RemoveHook(vtable[8]);
        return false;
    }

    g_hooked_swapchains.insert(swapChain);
    LogInfo("Successfully hooked IDXGISwapChain::Present - SwapChain: 0x%p", swapChain);
    return true;
}

bool HookSwapChain1VTable(IDXGISwapChain1* swapChain) {
    if (!swapChain) return false;

    std::lock_guard<std::mutex> lock(g_swapchain_mutex);

    // Check if already hooked
    if (g_hooked_swapchains1.find(swapChain) != g_hooked_swapchains1.end()) {
        return true;
    }

    // Get vtable
    void** vtable = *(void***)swapChain;

    // Hook Present1 method (index 22 in IDXGISwapChain1 vtable)
    if (MH_CreateHook(vtable[22], IDXGISwapChain_Present1_Detour,
                      (LPVOID*)&IDXGISwapChain_Present1_Original) != MH_OK) {
        LogError("Failed to create IDXGISwapChain1::Present1 hook");
        return false;
    }

    if (MH_EnableHook(vtable[22]) != MH_OK) {
        LogError("Failed to enable IDXGISwapChain1::Present1 hook");
        MH_RemoveHook(vtable[22]);
        return false;
    }

    g_hooked_swapchains1.insert(swapChain);
    LogInfo("Successfully hooked IDXGISwapChain1::Present1 - SwapChain: 0x%p", swapChain);
    return true;
}

void UnhookSwapChainVTable(IDXGISwapChain* swapChain) {
    if (!swapChain) return;

    void** vtable = *(void***)swapChain;

    // Disable and remove hook
    MH_DisableHook(vtable[8]);
    MH_RemoveHook(vtable[8]);

    LogInfo("Unhooked IDXGISwapChain::Present - SwapChain: 0x%p", swapChain);
}

void UnhookSwapChain1VTable(IDXGISwapChain1* swapChain) {
    if (!swapChain) return;

    void** vtable = *(void***)swapChain;

    // Disable and remove hook
    MH_DisableHook(vtable[22]);
    MH_RemoveHook(vtable[22]);

    LogInfo("Unhooked IDXGISwapChain1::Present1 - SwapChain: 0x%p", swapChain);
}

} // namespace renodx::hooks::directx
