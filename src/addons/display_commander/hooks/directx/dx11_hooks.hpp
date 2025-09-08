#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <MinHook.h>
#include <atomic>

// Forward declarations
namespace renodx::hooks::directx {
    class SwapChainDetector;
}

namespace renodx::hooks::directx {

// Function pointer types for IDXGISwapChain vtable
typedef HRESULT (STDMETHODCALLTYPE *IDXGISwapChain_Present_pfn)(
    IDXGISwapChain *This, UINT SyncInterval, UINT Flags);

typedef HRESULT (STDMETHODCALLTYPE *IDXGISwapChain_Present1_pfn)(
    IDXGISwapChain1 *This, UINT SyncInterval, UINT PresentFlags,
    const DXGI_PRESENT_PARAMETERS *pPresentParameters);

// Original function pointers
extern IDXGISwapChain_Present_pfn IDXGISwapChain_Present_Original;
extern IDXGISwapChain_Present1_pfn IDXGISwapChain_Present1_Original;

// Hook state
extern std::atomic<bool> g_dx11_hooks_installed;

// Hook functions
bool InstallDX11Hooks();
void UninstallDX11Hooks();
bool AreDX11HooksInstalled();

// Present callbacks
void OnPresent(IDXGISwapChain* swapChain, UINT SyncInterval, UINT Flags);
void OnPresent1(IDXGISwapChain1* swapChain, UINT SyncInterval, UINT PresentFlags,
                const DXGI_PRESENT_PARAMETERS *pPresentParameters);
void OnFinishPresent(IDXGISwapChain* swapChain, HRESULT hr);
void OnFinishPresent1(IDXGISwapChain1* swapChain, HRESULT hr);

// Flag modification functions
UINT ModifyPresentFlags(UINT originalFlags);
UINT ModifyPresent1Flags(UINT originalFlags);

// VTable hooking utilities
bool HookSwapChainVTable(IDXGISwapChain* swapChain);
bool HookSwapChain1VTable(IDXGISwapChain1* swapChain);
void UnhookSwapChainVTable(IDXGISwapChain* swapChain);
void UnhookSwapChain1VTable(IDXGISwapChain1* swapChain);

} // namespace renodx::hooks::directx