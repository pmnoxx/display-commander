#pragma once

#include <windows.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>
#include <atomic>
#include <string>

namespace renodx::hooks::dxgi {

// Function pointer types for DXGI Present functions
using IDXGISwapChain_Present_pfn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* This, UINT SyncInterval, UINT Flags);
using IDXGISwapChain1_Present1_pfn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1* This, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters);

// Original function pointers
extern IDXGISwapChain_Present_pfn IDXGISwapChain_Present_Original;
extern IDXGISwapChain1_Present1_pfn IDXGISwapChain1_Present1_Original;

// Hooked DXGI Present functions
HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present_Detour(IDXGISwapChain* This, UINT SyncInterval, UINT Flags);
HRESULT STDMETHODCALLTYPE IDXGISwapChain1_Present1_Detour(IDXGISwapChain1* This, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters);

// Hook management
bool InstallDxgiPresentHooks();
void UninstallDxgiPresentHooks();
bool AreDxgiPresentHooksInstalled();

// Hook a specific swapchain when it's created
bool HookSwapchain(IDXGISwapChain* swapchain);

// Helper functions
std::string DecodePresentFlags(UINT flags);
std::string DecodePresent1Flags(UINT flags);

} // namespace renodx::hooks::dxgi
