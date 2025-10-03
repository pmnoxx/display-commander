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

// Function pointer types for DXGI Present1 functions (IDXGISwapChain1)
using IDXGISwapChain_Present1_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain1 *This, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS *pPresentParameters);

// Function pointer types for DXGI GetDesc functions
using IDXGISwapChain_GetDesc_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *This, DXGI_SWAP_CHAIN_DESC *pDesc);

// Function pointer types for DXGI GetDesc1 functions (IDXGISwapChain1)
using IDXGISwapChain_GetDesc1_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain1 *This, DXGI_SWAP_CHAIN_DESC1 *pDesc);

// Function pointer types for DXGI CheckColorSpaceSupport functions (IDXGISwapChain3)
using IDXGISwapChain_CheckColorSpaceSupport_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain3 *This, DXGI_COLOR_SPACE_TYPE ColorSpace, UINT *pColorSpaceSupport);

// Original function pointers
extern IDXGISwapChain_Present_pfn IDXGISwapChain_Present_Original;
extern IDXGISwapChain_Present1_pfn IDXGISwapChain_Present1_Original;
extern IDXGISwapChain_GetDesc_pfn IDXGISwapChain_GetDesc_Original;
extern IDXGISwapChain_GetDesc1_pfn IDXGISwapChain_GetDesc1_Original;
extern IDXGISwapChain_CheckColorSpaceSupport_pfn IDXGISwapChain_CheckColorSpaceSupport_Original;

// Hooked DXGI Present functions
HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present_Detour(IDXGISwapChain *This, UINT SyncInterval, UINT Flags);

// Hooked DXGI Present1 functions (IDXGISwapChain1)
HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present1_Detour(IDXGISwapChain1 *This, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS *pPresentParameters);

// Hooked DXGI GetDesc functions
HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetDesc_Detour(IDXGISwapChain *This, DXGI_SWAP_CHAIN_DESC *pDesc);

// Hooked DXGI GetDesc1 functions (IDXGISwapChain1)
HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetDesc1_Detour(IDXGISwapChain1 *This, DXGI_SWAP_CHAIN_DESC1 *pDesc);

// Hooked DXGI CheckColorSpaceSupport functions (IDXGISwapChain3)
HRESULT STDMETHODCALLTYPE IDXGISwapChain_CheckColorSpaceSupport_Detour(IDXGISwapChain3 *This, DXGI_COLOR_SPACE_TYPE ColorSpace, UINT *pColorSpaceSupport);

// Hook management
bool InstallDxgiPresentHooks();
void UninstallDxgiPresentHooks();
bool AreDxgiPresentHooksInstalled();

// Hook a specific swapchain when it's created
bool HookSwapchain(IDXGISwapChain *swapchain);

} // namespace display_commanderhooks::dxgi
