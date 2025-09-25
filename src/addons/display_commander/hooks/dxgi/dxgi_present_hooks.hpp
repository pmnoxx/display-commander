#pragma once

#include <windows.h>

#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>

namespace renodx::hooks::dxgi {

// Function pointer types for DXGI Present functions
using IDXGISwapChain_Present_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *This, UINT SyncInterval, UINT Flags);

// Original function pointers
extern IDXGISwapChain_Present_pfn IDXGISwapChain_Present_Original;

// Hooked DXGI Present functions
HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present_Detour(IDXGISwapChain *This, UINT SyncInterval, UINT Flags);

// Hook management
bool InstallDxgiPresentHooks();
void UninstallDxgiPresentHooks();
bool AreDxgiPresentHooksInstalled();

// Hook a specific swapchain when it's created
bool HookSwapchain(IDXGISwapChain *swapchain);

} // namespace renodx::hooks::dxgi
