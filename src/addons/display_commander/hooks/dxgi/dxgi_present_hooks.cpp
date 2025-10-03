#include "dxgi_present_hooks.hpp"
#include "../../swapchain_events.hpp"
#include "../../utils.hpp"
#include "../../globals.hpp"

#include <MinHook.h>

#include <string>

/*
 * IDXGISwapChain VTable Layout Documentation
 * ==========================================
 *
 * IDXGISwapChain inherits from IDXGIDeviceSubObject, which inherits from IDXGIObject,
 * which inherits from IUnknown. The vtable contains all methods from the inheritance chain.
 *
 * VTable Indices 0-18:
 * [0-2]   IUnknown methods (QueryInterface, AddRef, Release)
 * [3-5]   IDXGIObject methods (SetPrivateData, SetPrivateDataInterface, GetPrivateData)
 * [6-7]   IDXGIDeviceSubObject methods (GetDevice, GetParent)
 * [8-18]  IDXGISwapChain methods (Present, GetBuffer, SetFullscreenState, etc.)
 *
 * Note: Index 18 (GetDesc1) is only available in IDXGISwapChain1 and later versions.
 * The base IDXGISwapChain interface only goes up to index 17.
 */

namespace display_commanderhooks::dxgi {

// Original function pointers
IDXGISwapChain_Present_pfn IDXGISwapChain_Present_Original = nullptr;
IDXGISwapChain_GetDesc_pfn IDXGISwapChain_GetDesc_Original = nullptr;

// Hook state
static std::atomic<bool> g_dxgi_present_hooks_installed{false};

// Hooked IDXGISwapChain::Present function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present_Detour(IDXGISwapChain *This, UINT SyncInterval, UINT Flags) {
    // Increment DXGI Present counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_PRESENT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    ::OnPresentFlags2(&Flags);

    // Call original function
    if (IDXGISwapChain_Present_Original) {
        return IDXGISwapChain_Present_Original(This, SyncInterval, Flags);
    }

    // Fallback to direct call if hook failed
    return This->Present(SyncInterval, Flags);
}

// Hooked IDXGISwapChain::GetDesc function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetDesc_Detour(IDXGISwapChain *This, DXGI_SWAP_CHAIN_DESC *pDesc) {
    // Increment DXGI GetDesc counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_GETDESC].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Call original function
    if (IDXGISwapChain_GetDesc_Original) {
        HRESULT hr = IDXGISwapChain_GetDesc_Original(This, pDesc);

        // Log the description if successful (only on first few calls to avoid spam)
        if (SUCCEEDED(hr) && pDesc) {
            static int getdesc_log_count = 0;
            if (getdesc_log_count < 3) {
                LogInfo("SwapChain Desc - Width: %u, Height: %u, Format: %u, RefreshRate: %u/%u, BufferCount: %u",
                       pDesc->BufferDesc.Width, pDesc->BufferDesc.Height, pDesc->BufferDesc.Format,
                       pDesc->BufferDesc.RefreshRate.Numerator, pDesc->BufferDesc.RefreshRate.Denominator,
                       pDesc->BufferCount);
                getdesc_log_count++;
            }
        }

        return hr;
    }

    // Fallback to direct call if hook failed
    return This->GetDesc(pDesc);
}

// Global variables to track hooked swapchains
static std::atomic<IDXGISwapChain *> g_swapchain_hooked{nullptr};
static IDXGISwapChain *g_hooked_swapchain = nullptr;

// VTable hooking functions
bool HookSwapchainVTable(IDXGISwapChain *swapchain);

// Install DXGI Present hooks
bool InstallDxgiPresentHooks() {
    if (g_dxgi_present_hooks_installed.load()) {
        LogInfo("DXGI Present hooks already installed");
        return true;
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for DXGI Present hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with DXGI Present hooks");
    } else {
        LogInfo("MinHook initialized successfully for DXGI Present hooks");
    }

    g_dxgi_present_hooks_installed.store(true);
    LogInfo("DXGI Present hooks installed successfully - will hook swapchains when they are created");

    return true;
}

// Hook a specific swapchain's vtable
bool HookSwapchainVTable(IDXGISwapChain *swapchain) {
    if (g_swapchain_hooked.load() == swapchain) {
        return false;
    }

    // Get the vtable
    void **vtable = *(void ***)swapchain;

    // IDXGISwapChain vtable layout (indices 0-18):
    // [0]  IUnknown::QueryInterface
    // [1]  IUnknown::AddRef
    // [2]  IUnknown::Release
    // [3]  IDXGIObject::SetPrivateData
    // [4]  IDXGIObject::SetPrivateDataInterface
    // [5]  IDXGIObject::GetPrivateData
    // [6]  IDXGIDeviceSubObject::GetDevice
    // [7]  IDXGIDeviceSubObject::GetParent
    // [8]  IDXGISwapChain::Present                    ← Hooked
    // [9]  IDXGISwapChain::GetBuffer
    // [10] IDXGISwapChain::SetFullscreenState
    // [11] IDXGISwapChain::GetFullscreenState
    // [12] IDXGISwapChain::GetDesc                    ← Hooked
    // [13] IDXGISwapChain::ResizeBuffers
    // [14] IDXGISwapChain::ResizeTarget
    // [15] IDXGISwapChain::GetContainingOutput
    // [16] IDXGISwapChain::GetFrameStatistics
    // [17] IDXGISwapChain::GetLastPresentCount
    // [18] IDXGISwapChain::GetDesc1 (IDXGISwapChain1+)

    // Hook Present (index 8 in IDXGISwapChain vtable)
    if (MH_CreateHook(vtable[8], IDXGISwapChain_Present_Detour, (LPVOID *)&IDXGISwapChain_Present_Original) != MH_OK) {
        LogError("Failed to create IDXGISwapChain::Present hook");
        return false;
    }

    // Hook GetDesc (index 12 in IDXGISwapChain vtable)
    if (MH_CreateHook(vtable[12], IDXGISwapChain_GetDesc_Detour, (LPVOID *)&IDXGISwapChain_GetDesc_Original) != MH_OK) {
        LogError("Failed to create IDXGISwapChain::GetDesc hook");
        return false;
    }

    // Enable the Present hook
    if (MH_EnableHook(vtable[8]) != MH_OK) {
        LogError("Failed to enable IDXGISwapChain::Present hook");
        return false;
    }

    // Enable the GetDesc hook
    if (MH_EnableHook(vtable[12]) != MH_OK) {
        LogError("Failed to enable IDXGISwapChain::GetDesc hook");
        return false;
    }

    g_hooked_swapchain = swapchain;
    g_swapchain_hooked.store(swapchain);
    LogInfo("Successfully hooked IDXGISwapChain::Present and GetDesc vtable entries for swapchain: 0x%p", swapchain);

    return true;
}

// Public function to hook a swapchain when it's created
bool HookSwapchain(IDXGISwapChain *swapchain) {
    return HookSwapchainVTable(swapchain);
}

// Uninstall DXGI Present hooks
void UninstallDxgiPresentHooks() {
    if (!g_dxgi_present_hooks_installed.load()) {
        return;
    }

    // Disable hooks
    MH_DisableHook(MH_ALL_HOOKS);

    // Remove hooks
    MH_RemoveHook(MH_ALL_HOOKS);

    g_dxgi_present_hooks_installed.store(false);
    LogInfo("DXGI Present hooks uninstalled");
}

// Check if DXGI Present hooks are installed
bool AreDxgiPresentHooksInstalled() { return g_dxgi_present_hooks_installed.load(); }

} // namespace display_commanderhooks::dxgi
