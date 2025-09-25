#include "dxgi_present_hooks.hpp"
#include "../../swapchain_events.hpp"
#include "../../utils.hpp"

#include <MinHook.h>

#include <string>

namespace display_commanderhooks::dxgi {

// Original function pointers
IDXGISwapChain_Present_pfn IDXGISwapChain_Present_Original = nullptr;

// Hook state
static std::atomic<bool> g_dxgi_present_hooks_installed{false};

// Hooked IDXGISwapChain::Present function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present_Detour(IDXGISwapChain *This, UINT SyncInterval, UINT Flags) {
    ::OnPresentFlags2(&Flags);

    // Call original function
    if (IDXGISwapChain_Present_Original) {
        return IDXGISwapChain_Present_Original(This, SyncInterval, Flags);
    }

    // Fallback to direct call if hook failed
    return This->Present(SyncInterval, Flags);
}

// Global variables to track hooked swapchains
static std::atomic<bool> g_swapchain_hooked{false};
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
    if (!swapchain || g_swapchain_hooked.load()) {
        return false;
    }

    // Get the vtable
    void **vtable = *(void ***)swapchain;

    // Hook Present (index 8 in IDXGISwapChain vtable)
    if (MH_CreateHook(vtable[8], IDXGISwapChain_Present_Detour, (LPVOID *)&IDXGISwapChain_Present_Original) != MH_OK) {
        LogError("Failed to create IDXGISwapChain::Present hook");
        return false;
    }

    // Enable the hook
    if (MH_EnableHook(vtable[8]) != MH_OK) {
        LogError("Failed to enable IDXGISwapChain::Present hook");
        return false;
    }

    g_hooked_swapchain = swapchain;
    g_swapchain_hooked.store(true);
    LogInfo("Successfully hooked IDXGISwapChain::Present vtable");

    return true;
}

// Public function to hook a swapchain when it's created
bool HookSwapchain(IDXGISwapChain *swapchain) {
    if (!g_dxgi_present_hooks_installed.load()) {
        LogError("DXGI Present hooks not installed, cannot hook swapchain");
        return false;
    }

    bool success = false;

    // Try to hook as IDXGISwapChain
    if (HookSwapchainVTable(swapchain)) {
        success = true;
    }

    return success;
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
