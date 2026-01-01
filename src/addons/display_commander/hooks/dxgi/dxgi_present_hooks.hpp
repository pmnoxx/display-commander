#pragma once

#include <windows.h>

#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>
#include <dxgi1_5.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <vector>
#include "../../globals.hpp"

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

// Function pointer types for DXGI Factory CreateSwapChain functions
using IDXGIFactory_CreateSwapChain_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGIFactory *This, IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain);

// Additional function pointer types for DXGI methods
using IDXGISwapChain_GetBuffer_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *This, UINT Buffer, REFIID riid, void **ppSurface);
using IDXGISwapChain_SetFullscreenState_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *This, BOOL Fullscreen, IDXGIOutput *pTarget);
using IDXGISwapChain_GetFullscreenState_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *This, BOOL *pFullscreen, IDXGIOutput **ppTarget);
using IDXGISwapChain_ResizeBuffers_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *This, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
using IDXGISwapChain_ResizeTarget_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *This, const DXGI_MODE_DESC *pNewTargetParameters);
using IDXGISwapChain_GetContainingOutput_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *This, IDXGIOutput **ppOutput);
using IDXGISwapChain_GetFrameStatistics_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *This, DXGI_FRAME_STATISTICS *pStats);
using IDXGISwapChain_GetLastPresentCount_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *This, UINT *pLastPresentCount);

// IDXGISwapChain1 function pointer types
using IDXGISwapChain_GetFullscreenDesc_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain1 *This, DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pDesc);
using IDXGISwapChain_GetHwnd_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain1 *This, HWND *pHwnd);
using IDXGISwapChain_GetCoreWindow_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain1 *This, REFIID refiid, void **ppUnk);
using IDXGISwapChain_IsTemporaryMonoSupported_pfn = BOOL(STDMETHODCALLTYPE *)(IDXGISwapChain1 *This);
using IDXGISwapChain_GetRestrictToOutput_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain1 *This, IDXGIOutput **ppRestrictToOutput);
using IDXGISwapChain_SetBackgroundColor_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain1 *This, const DXGI_RGBA *pColor);
using IDXGISwapChain_GetBackgroundColor_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain1 *This, DXGI_RGBA *pColor);
using IDXGISwapChain_SetRotation_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain1 *This, DXGI_MODE_ROTATION Rotation);
using IDXGISwapChain_GetRotation_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain1 *This, DXGI_MODE_ROTATION *pRotation);

// IDXGISwapChain2 function pointer types
using IDXGISwapChain_SetSourceSize_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain2 *This, UINT Width, UINT Height);
using IDXGISwapChain_GetSourceSize_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain2 *This, UINT *pWidth, UINT *pHeight);
using IDXGISwapChain_SetMaximumFrameLatency_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain2 *This, UINT MaxLatency);
using IDXGISwapChain_GetMaximumFrameLatency_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain2 *This, UINT *pMaxLatency);
using IDXGISwapChain_GetFrameLatencyWaitableObject_pfn = HANDLE(STDMETHODCALLTYPE *)(IDXGISwapChain2 *This);
using IDXGISwapChain_SetMatrixTransform_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain2 *This, const DXGI_MATRIX_3X2_F *pMatrix);
using IDXGISwapChain_GetMatrixTransform_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain2 *This, DXGI_MATRIX_3X2_F *pMatrix);

// IDXGISwapChain3 function pointer types
using IDXGISwapChain_GetCurrentBackBufferIndex_pfn = UINT(STDMETHODCALLTYPE *)(IDXGISwapChain3 *This);
using IDXGISwapChain_SetColorSpace1_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain3 *This, DXGI_COLOR_SPACE_TYPE ColorSpace);
using IDXGISwapChain_ResizeBuffers1_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain3 *This, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags, const UINT *pCreationNodeMask, IUnknown *const *ppPresentQueue);

// IDXGISwapChain4 function pointer types
using IDXGISwapChain_SetHDRMetaData_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain4 *This, DXGI_HDR_METADATA_TYPE Type, UINT Size, void *pMetaData);

// IDXGIOutput function pointer types
using IDXGIOutput_SetGammaControl_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGIOutput *This, const DXGI_GAMMA_CONTROL *pArray);
using IDXGIOutput_GetGammaControl_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGIOutput *This, DXGI_GAMMA_CONTROL *pArray);
using IDXGIOutput_GetDesc_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGIOutput *This, DXGI_OUTPUT_DESC *pDesc);

// IDXGIOutput6 function pointer types
using IDXGIOutput6_GetDesc1_pfn = HRESULT(STDMETHODCALLTYPE *)(IDXGIOutput6 *This, DXGI_OUTPUT_DESC1 *pDesc);

// Original function pointers
extern IDXGISwapChain_Present_pfn IDXGISwapChain_Present_Original;
extern IDXGISwapChain_Present1_pfn IDXGISwapChain_Present1_Original;
extern IDXGISwapChain_GetDesc_pfn IDXGISwapChain_GetDesc_Original;
extern IDXGISwapChain_GetDesc1_pfn IDXGISwapChain_GetDesc1_Original;
extern IDXGISwapChain_CheckColorSpaceSupport_pfn IDXGISwapChain_CheckColorSpaceSupport_Original;
extern IDXGIFactory_CreateSwapChain_pfn IDXGIFactory_CreateSwapChain_Original;

// Additional original function pointers
extern IDXGISwapChain_GetBuffer_pfn IDXGISwapChain_GetBuffer_Original;
extern IDXGISwapChain_SetFullscreenState_pfn IDXGISwapChain_SetFullscreenState_Original;
extern IDXGISwapChain_GetFullscreenState_pfn IDXGISwapChain_GetFullscreenState_Original;
extern IDXGISwapChain_ResizeBuffers_pfn IDXGISwapChain_ResizeBuffers_Original;
extern IDXGISwapChain_ResizeTarget_pfn IDXGISwapChain_ResizeTarget_Original;
extern IDXGISwapChain_GetContainingOutput_pfn IDXGISwapChain_GetContainingOutput_Original;
extern IDXGISwapChain_GetFrameStatistics_pfn IDXGISwapChain_GetFrameStatistics_Original;
extern IDXGISwapChain_GetLastPresentCount_pfn IDXGISwapChain_GetLastPresentCount_Original;

// IDXGISwapChain1 original function pointers
extern IDXGISwapChain_GetFullscreenDesc_pfn IDXGISwapChain_GetFullscreenDesc_Original;
extern IDXGISwapChain_GetHwnd_pfn IDXGISwapChain_GetHwnd_Original;
extern IDXGISwapChain_GetCoreWindow_pfn IDXGISwapChain_GetCoreWindow_Original;
extern IDXGISwapChain_IsTemporaryMonoSupported_pfn IDXGISwapChain_IsTemporaryMonoSupported_Original;
extern IDXGISwapChain_GetRestrictToOutput_pfn IDXGISwapChain_GetRestrictToOutput_Original;
extern IDXGISwapChain_SetBackgroundColor_pfn IDXGISwapChain_SetBackgroundColor_Original;
extern IDXGISwapChain_GetBackgroundColor_pfn IDXGISwapChain_GetBackgroundColor_Original;
extern IDXGISwapChain_SetRotation_pfn IDXGISwapChain_SetRotation_Original;
extern IDXGISwapChain_GetRotation_pfn IDXGISwapChain_GetRotation_Original;

// IDXGISwapChain2 original function pointers
extern IDXGISwapChain_SetSourceSize_pfn IDXGISwapChain_SetSourceSize_Original;
extern IDXGISwapChain_GetSourceSize_pfn IDXGISwapChain_GetSourceSize_Original;
extern IDXGISwapChain_SetMaximumFrameLatency_pfn IDXGISwapChain_SetMaximumFrameLatency_Original;
extern IDXGISwapChain_GetMaximumFrameLatency_pfn IDXGISwapChain_GetMaximumFrameLatency_Original;
extern IDXGISwapChain_GetFrameLatencyWaitableObject_pfn IDXGISwapChain_GetFrameLatencyWaitableObject_Original;
extern IDXGISwapChain_SetMatrixTransform_pfn IDXGISwapChain_SetMatrixTransform_Original;
extern IDXGISwapChain_GetMatrixTransform_pfn IDXGISwapChain_GetMatrixTransform_Original;

// IDXGISwapChain3 original function pointers
extern IDXGISwapChain_GetCurrentBackBufferIndex_pfn IDXGISwapChain_GetCurrentBackBufferIndex_Original;
extern IDXGISwapChain_SetColorSpace1_pfn IDXGISwapChain_SetColorSpace1_Original;
extern IDXGISwapChain_ResizeBuffers1_pfn IDXGISwapChain_ResizeBuffers1_Original;

// IDXGISwapChain4 original function pointers
extern IDXGISwapChain_SetHDRMetaData_pfn IDXGISwapChain_SetHDRMetaData_Original;

// IDXGIOutput original function pointers
extern IDXGIOutput_SetGammaControl_pfn IDXGIOutput_SetGammaControl_Original;
extern IDXGIOutput_GetGammaControl_pfn IDXGIOutput_GetGammaControl_Original;
extern IDXGIOutput_GetDesc_pfn IDXGIOutput_GetDesc_Original;

// IDXGIOutput6 original function pointers
extern IDXGIOutput6_GetDesc1_pfn IDXGIOutput6_GetDesc1_Original;

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

// Hooked DXGI SetHDRMetaData functions (IDXGISwapChain4)
HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetHDRMetaData_Detour(IDXGISwapChain4 *This, DXGI_HDR_METADATA_TYPE Type, UINT Size, void *pMetaData);

// Hooked DXGI Factory CreateSwapChain functions
HRESULT STDMETHODCALLTYPE IDXGIFactory_CreateSwapChain_Detour(IDXGIFactory *This, IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain);

// Hook a specific swapchain when it's created
bool HookSwapchain(IDXGISwapChain *swapchain);

// Hook a specific factory when it's created
bool HookFactory(IDXGIFactory *factory);

// Record the native swapchain used in OnPresentUpdateBefore
void RecordPresentUpdateSwapchain(IDXGISwapChain *swapchain);

// Hooked IDXGIOutput functions
HRESULT STDMETHODCALLTYPE IDXGIOutput_SetGammaControl_Detour(IDXGIOutput *This, const DXGI_GAMMA_CONTROL *pArray);
HRESULT STDMETHODCALLTYPE IDXGIOutput_GetGammaControl_Detour(IDXGIOutput *This, DXGI_GAMMA_CONTROL *pArray);
HRESULT STDMETHODCALLTYPE IDXGIOutput_GetDesc_Detour(IDXGIOutput *This, DXGI_OUTPUT_DESC *pDesc);

// Hooked IDXGIOutput6 functions
HRESULT STDMETHODCALLTYPE IDXGIOutput6_GetDesc1_Detour(IDXGIOutput6 *This, DXGI_OUTPUT_DESC1 *pDesc);


// Swapchain tracking management functions
bool IsSwapchainTracked(IDXGISwapChain *swapchain);
bool AddSwapchainToTracking(IDXGISwapChain *swapchain);
bool RemoveSwapchainFromTracking(IDXGISwapChain *swapchain);
std::vector<IDXGISwapChain*> GetAllTrackedSwapchains();
size_t GetTrackedSwapchainCount();
void ClearAllTrackedSwapchains();
bool HasTrackedSwapchains();

bool HookSwapchainNative(IDXGISwapChain *swapchain);

// Cleanup GPU measurement fences when device is destroyed
void CleanupGPUMeasurementFences();

// Helper structure to hold common present logic state
struct PresentCommonState {
    DeviceTypeDC device_type = DeviceTypeDC::DX10;
    Microsoft::WRL::ComPtr<IUnknown> device;
    IDXGISwapChain* base_swapchain = nullptr;
};

// Helper function for common Present/Present1 logic before calling original
template<typename SwapChainType>
PresentCommonState HandlePresentBefore(
    SwapChainType* This,
    IDXGISwapChain* baseSwapChain,
    bool checkD3D10);

template<typename SwapChainType>
void HandlePresentBefore2(SwapChainType* This);

// Helper function for common Present/Present1 logic after calling original
void HandlePresentAfter(
    IDXGISwapChain* baseSwapChain,
    const PresentCommonState& state);
} // namespace display_commanderhooks::dxgi
