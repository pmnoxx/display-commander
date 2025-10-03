#pragma once

#include <windows.h>

#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>

/*
 * IDXGISwapChain VTable Layout Reference
 * ======================================
 *
 * This file contains hooks for IDXGISwapChain vtable methods.
 * The vtable layout follows COM inheritance hierarchy:
 *
 * [0-2]   IUnknown methods
 * [3-5]   IDXGIObject methods
 * [6-7]   IDXGIDeviceSubObject methods
 * [8-17]  IDXGISwapChain methods (base interface)
 * [18+]   IDXGISwapChain1+ methods (extended interfaces)
 *
 * Currently hooked methods:
 * - [8]  Present: Main rendering/presentation method
 * - [12] GetDesc: Retrieves swapchain description
 */

namespace display_commanderhooks::dxgi {

// Function pointer types for DXGI Present functions
using IDXGISwapChain_Present_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *This, UINT SyncInterval, UINT Flags);

// Function pointer types for DXGI GetDesc functions
using IDXGISwapChain_GetDesc_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *This, DXGI_SWAP_CHAIN_DESC *pDesc);

// Original function pointers
extern IDXGISwapChain_Present_pfn IDXGISwapChain_Present_Original;
extern IDXGISwapChain_GetDesc_pfn IDXGISwapChain_GetDesc_Original;

// Hooked DXGI Present functions
HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present_Detour(IDXGISwapChain *This, UINT SyncInterval, UINT Flags);

// Hooked DXGI GetDesc functions
HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetDesc_Detour(IDXGISwapChain *This, DXGI_SWAP_CHAIN_DESC *pDesc);

// Hook management
bool InstallDxgiPresentHooks();
void UninstallDxgiPresentHooks();
bool AreDxgiPresentHooksInstalled();

// Hook a specific swapchain when it's created
bool HookSwapchain(IDXGISwapChain *swapchain);

} // namespace display_commanderhooks::dxgi
