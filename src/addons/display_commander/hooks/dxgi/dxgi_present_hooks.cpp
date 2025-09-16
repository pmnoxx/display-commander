#include "dxgi_present_hooks.hpp"
#include "../../utils.hpp"
#include "../../swapchain_events.hpp"
#include <MinHook.h>
#include <sstream>
#include <iomanip>
#include <string>

namespace renodx::hooks::dxgi {

// Original function pointers
IDXGISwapChain_Present_pfn IDXGISwapChain_Present_Original = nullptr;
IDXGISwapChain1_Present1_pfn IDXGISwapChain1_Present1_Original = nullptr;

// Hook state
static std::atomic<bool> g_dxgi_present_hooks_installed{false};

// Helper function to decode Present flags
std::string DecodePresentFlags(UINT flags) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::setfill('0') << std::setw(8) << flags;

    if (flags == 0) {
        oss << " (NONE)";
    } else {
        oss << " (";
        bool first = true;

        if (flags & DXGI_PRESENT_TEST) {
            if (!first) oss << " | ";
            oss << "TEST";
            first = false;
        }
        if (flags & DXGI_PRESENT_DO_NOT_SEQUENCE) {
            if (!first) oss << " | ";
            oss << "DO_NOT_SEQUENCE";
            first = false;
        }
        if (flags & DXGI_PRESENT_RESTART) {
            if (!first) oss << " | ";
            oss << "RESTART";
            first = false;
        }
        if (flags & DXGI_PRESENT_DO_NOT_WAIT) {
            if (!first) oss << " | ";
            oss << "DO_NOT_WAIT";
            first = false;
        }
        if (flags & DXGI_PRESENT_STEREO_PREFER_RIGHT) {
            if (!first) oss << " | ";
            oss << "STEREO_PREFER_RIGHT";
            first = false;
        }
        if (flags & DXGI_PRESENT_STEREO_TEMPORARY_MONO) {
            if (!first) oss << " | ";
            oss << "STEREO_TEMPORARY_MONO";
            first = false;
        }
        if (flags & DXGI_PRESENT_RESTRICT_TO_OUTPUT) {
            if (!first) oss << " | ";
            oss << "RESTRICT_TO_OUTPUT";
            first = false;
        }
        if (flags & DXGI_PRESENT_USE_DURATION) {
            if (!first) oss << " | ";
            oss << "USE_DURATION";
            first = false;
        }
        if (flags & DXGI_PRESENT_ALLOW_TEARING) {
            if (!first) oss << " | ";
            oss << "ALLOW_TEARING";
            first = false;
        }

        oss << ")";
    }

    return oss.str();
}

// Helper function to decode Present1 flags
std::string DecodePresent1Flags(UINT flags) {
    return DecodePresentFlags(flags); // Same flags as Present
}


// Hooked IDXGISwapChain::Present function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present_Detour(IDXGISwapChain* This, UINT SyncInterval, UINT Flags) {
    // Log the Present call with all arguments
    std::ostringstream oss;
    oss << "DXGI Present called - SwapChain: 0x" << std::hex << This
        << ", SyncInterval: " << std::dec << SyncInterval
        << ", Flags: " << DecodePresentFlags(Flags);
    LogInfo("%s", oss.str().c_str());

    ::OnPresentFlags2(&Flags);

    // Call original function
    if (IDXGISwapChain_Present_Original) {
        return IDXGISwapChain_Present_Original(This, SyncInterval, Flags);
    }

    // Fallback to direct call if hook failed
    return This->Present(SyncInterval, Flags);
}

// Hooked IDXGISwapChain1::Present1 function
HRESULT STDMETHODCALLTYPE IDXGISwapChain1_Present1_Detour(IDXGISwapChain1* This, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters) {
    // Log the Present1 call with all arguments
    std::ostringstream oss;
    oss << "DXGI Present1 called - SwapChain1: 0x" << std::hex << This
        << ", SyncInterval: " << std::dec << SyncInterval
        << ", PresentFlags: " << DecodePresent1Flags(PresentFlags);

    if (pPresentParameters) {
        oss << ", PresentParameters: DirtyRects=" << pPresentParameters->DirtyRectsCount;
    } else {
        oss << ", PresentParameters: NULL";
    }

    LogInfo("%s", oss.str().c_str());

    // Call original function
    if (IDXGISwapChain1_Present1_Original) {
        return IDXGISwapChain1_Present1_Original(This, SyncInterval, PresentFlags, pPresentParameters);
    }

    // Fallback to direct call if hook failed
    return This->Present1(SyncInterval, PresentFlags, pPresentParameters);
}

// Global variables to track hooked swapchains
static std::atomic<bool> g_swapchain_hooked{false};
static IDXGISwapChain* g_hooked_swapchain = nullptr;
static IDXGISwapChain1* g_hooked_swapchain1 = nullptr;

// VTable hooking functions
bool HookSwapchainVTable(IDXGISwapChain* swapchain);
bool HookSwapchain1VTable(IDXGISwapChain1* swapchain1);

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
bool HookSwapchainVTable(IDXGISwapChain* swapchain) {
    if (!swapchain || g_swapchain_hooked.load()) {
        return false;
    }

    // Get the vtable
    void** vtable = *(void***)swapchain;

    // Hook Present (index 8 in IDXGISwapChain vtable)
    if (MH_CreateHook(vtable[8], IDXGISwapChain_Present_Detour,
                      (LPVOID*)&IDXGISwapChain_Present_Original) != MH_OK) {
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

// Hook a specific swapchain1's vtable
bool HookSwapchain1VTable(IDXGISwapChain1* swapchain1) {
    if (!swapchain1) {
        return false;
    }

    // Get the vtable
    void** vtable = *(void***)swapchain1;

    // Hook Present1 (index 22 in IDXGISwapChain1 vtable)
    if (MH_CreateHook(vtable[22], IDXGISwapChain1_Present1_Detour,
                      (LPVOID*)&IDXGISwapChain1_Present1_Original) != MH_OK) {
        LogError("Failed to create IDXGISwapChain1::Present1 hook");
        return false;
    }

    // Enable the hook
    if (MH_EnableHook(vtable[22]) != MH_OK) {
        LogError("Failed to enable IDXGISwapChain1::Present1 hook");
        return false;
    }

    g_hooked_swapchain1 = swapchain1;
    LogInfo("Successfully hooked IDXGISwapChain1::Present1 vtable");

    return true;
}

// Public function to hook a swapchain when it's created
bool HookSwapchain(IDXGISwapChain* swapchain) {
    if (!g_dxgi_present_hooks_installed.load()) {
        LogError("DXGI Present hooks not installed, cannot hook swapchain");
        return false;
    }

    bool success = false;

    // Try to hook as IDXGISwapChain
    if (HookSwapchainVTable(swapchain)) {
        success = true;
    }

    // Try to hook as IDXGISwapChain1
    IDXGISwapChain1* swapchain1 = nullptr;
    if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain1)))) {
        if (HookSwapchain1VTable(swapchain1)) {
            success = true;
        }
        swapchain1->Release();
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
bool AreDxgiPresentHooksInstalled() {
    return g_dxgi_present_hooks_installed.load();
}

} // namespace renodx::hooks::dxgi
