#pragma once

#include "../../globals.hpp"

#include <reshade.hpp>

#include <vector>

#include <Windows.h>

#include <atomic>

#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>
#include <dxgi1_5.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

/*
 * IDXGISwapChain hooks — only methods where we change behavior or capture state needed for DC:
 * Present / Present1 (FPS limiter, timing),
 * fullscreen state (exclusive fullscreen prevention),
 * ResizeBuffers / ResizeTarget / ResizeBuffers1 (render resolution),
 * CheckColorSpaceSupport. No stats-only / logging-only detours.
 */

namespace display_commanderhooks::dxgi {

/** Present / Present1 nesting depth shared with Streamline proxy swapchain detours (`features/streamline`). */
extern std::atomic<int> g_dxgi_present_nested_depth;

using IDXGISwapChain_Present_pfn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* This, UINT SyncInterval, UINT Flags);

using IDXGISwapChain_Present1_pfn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1* This, UINT SyncInterval,
                                                                UINT PresentFlags,
                                                                const DXGI_PRESENT_PARAMETERS* pPresentParameters);

using IDXGISwapChain_CheckColorSpaceSupport_pfn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3* This,
                                                                              DXGI_COLOR_SPACE_TYPE ColorSpace,
                                                                              UINT* pColorSpaceSupport);

using IDXGISwapChain_SetFullscreenState_pfn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* This, BOOL Fullscreen,
                                                                          IDXGIOutput* pTarget);
using IDXGISwapChain_GetFullscreenState_pfn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* This, BOOL* pFullscreen,
                                                                          IDXGIOutput** ppTarget);
using IDXGISwapChain_ResizeBuffers_pfn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* This, UINT BufferCount, UINT Width,
                                                                     UINT Height, DXGI_FORMAT NewFormat,
                                                                     UINT SwapChainFlags);
using IDXGISwapChain_ResizeTarget_pfn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* This,
                                                                    const DXGI_MODE_DESC* pNewTargetParameters);

using IDXGISwapChain_ResizeBuffers1_pfn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3* This, UINT BufferCount,
                                                                      UINT Width, UINT Height, DXGI_FORMAT Format,
                                                                      UINT SwapChainFlags,
                                                                      const UINT* pCreationNodeMask,
                                                                      IUnknown* const* ppPresentQueue);

extern IDXGISwapChain_Present_pfn IDXGISwapChain_Present_Original;
extern IDXGISwapChain_Present1_pfn IDXGISwapChain_Present1_Original;
extern IDXGISwapChain_CheckColorSpaceSupport_pfn IDXGISwapChain_CheckColorSpaceSupport_Original;

extern IDXGISwapChain_SetFullscreenState_pfn IDXGISwapChain_SetFullscreenState_Original;
extern IDXGISwapChain_GetFullscreenState_pfn IDXGISwapChain_GetFullscreenState_Original;
extern IDXGISwapChain_ResizeBuffers_pfn IDXGISwapChain_ResizeBuffers_Original;
extern IDXGISwapChain_ResizeTarget_pfn IDXGISwapChain_ResizeTarget_Original;
extern IDXGISwapChain_ResizeBuffers1_pfn IDXGISwapChain_ResizeBuffers1_Original;

HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present_Detour(IDXGISwapChain* This, UINT SyncInterval, UINT Flags);

HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present1_Detour(IDXGISwapChain1* This, UINT SyncInterval, UINT PresentFlags,
                                                         const DXGI_PRESENT_PARAMETERS* pPresentParameters);

HRESULT STDMETHODCALLTYPE IDXGISwapChain_CheckColorSpaceSupport_Detour(IDXGISwapChain3* This,
                                                                       DXGI_COLOR_SPACE_TYPE ColorSpace,
                                                                       UINT* pColorSpaceSupport);

bool HookSwapchain(IDXGISwapChain* swapchain);

struct DCDxgiSwapchainData {
    IDXGISwapChain* dxgi_swapchain{nullptr};
    reshade::api::swapchain* swapchain{nullptr};
    reshade::api::command_queue* command_queue{nullptr};
    reshade::api::device_api device_api{};
    /** Set when device_api == d3d9. Not refcounted. Cast to IDirect3DDevice9* when device_api == d3d9. */
    void* d3d9_device{nullptr};
    /** True after AutoSetColorSpace has been run once for this swapchain (run at most once per swapchain). */
    bool auto_colorspace_applied{false};
};

bool LoadDCDxgiSwapchainData(IDXGISwapChain* swapchain, DCDxgiSwapchainData* out);

void SaveDCDxgiSwapchainData(IDXGISwapChain* swapchain, const DCDxgiSwapchainData* data);

extern std::atomic<std::shared_ptr<DCDxgiSwapchainData>> g_last_d3d9_dc_swapchain_data;

void SaveDCDxgiSwapchainDataForD3D9(const DCDxgiSwapchainData* data);

void CleanupGPUMeasurementFences();

void HandlePresentAfter(bool frame_generation_aware);
}  // namespace display_commanderhooks::dxgi
