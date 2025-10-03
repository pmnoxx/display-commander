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
IDXGISwapChain_Present1_pfn IDXGISwapChain_Present1_Original = nullptr;
IDXGISwapChain_GetDesc_pfn IDXGISwapChain_GetDesc_Original = nullptr;
IDXGISwapChain_GetDesc1_pfn IDXGISwapChain_GetDesc1_Original = nullptr;
IDXGISwapChain_CheckColorSpaceSupport_pfn IDXGISwapChain_CheckColorSpaceSupport_Original = nullptr;
IDXGIFactory_CreateSwapChain_pfn IDXGIFactory_CreateSwapChain_Original = nullptr;

// Additional original function pointers
IDXGISwapChain_GetBuffer_pfn IDXGISwapChain_GetBuffer_Original = nullptr;
IDXGISwapChain_SetFullscreenState_pfn IDXGISwapChain_SetFullscreenState_Original = nullptr;
IDXGISwapChain_GetFullscreenState_pfn IDXGISwapChain_GetFullscreenState_Original = nullptr;
IDXGISwapChain_ResizeBuffers_pfn IDXGISwapChain_ResizeBuffers_Original = nullptr;
IDXGISwapChain_ResizeTarget_pfn IDXGISwapChain_ResizeTarget_Original = nullptr;
IDXGISwapChain_GetContainingOutput_pfn IDXGISwapChain_GetContainingOutput_Original = nullptr;
IDXGISwapChain_GetFrameStatistics_pfn IDXGISwapChain_GetFrameStatistics_Original = nullptr;
IDXGISwapChain_GetLastPresentCount_pfn IDXGISwapChain_GetLastPresentCount_Original = nullptr;

// IDXGISwapChain1 original function pointers
IDXGISwapChain_GetFullscreenDesc_pfn IDXGISwapChain_GetFullscreenDesc_Original = nullptr;
IDXGISwapChain_GetHwnd_pfn IDXGISwapChain_GetHwnd_Original = nullptr;
IDXGISwapChain_GetCoreWindow_pfn IDXGISwapChain_GetCoreWindow_Original = nullptr;
IDXGISwapChain_IsTemporaryMonoSupported_pfn IDXGISwapChain_IsTemporaryMonoSupported_Original = nullptr;
IDXGISwapChain_GetRestrictToOutput_pfn IDXGISwapChain_GetRestrictToOutput_Original = nullptr;
IDXGISwapChain_SetBackgroundColor_pfn IDXGISwapChain_SetBackgroundColor_Original = nullptr;
IDXGISwapChain_GetBackgroundColor_pfn IDXGISwapChain_GetBackgroundColor_Original = nullptr;
IDXGISwapChain_SetRotation_pfn IDXGISwapChain_SetRotation_Original = nullptr;
IDXGISwapChain_GetRotation_pfn IDXGISwapChain_GetRotation_Original = nullptr;

// IDXGISwapChain2 original function pointers
IDXGISwapChain_SetSourceSize_pfn IDXGISwapChain_SetSourceSize_Original = nullptr;
IDXGISwapChain_GetSourceSize_pfn IDXGISwapChain_GetSourceSize_Original = nullptr;
IDXGISwapChain_SetMaximumFrameLatency_pfn IDXGISwapChain_SetMaximumFrameLatency_Original = nullptr;
IDXGISwapChain_GetMaximumFrameLatency_pfn IDXGISwapChain_GetMaximumFrameLatency_Original = nullptr;
IDXGISwapChain_GetFrameLatencyWaitableObject_pfn IDXGISwapChain_GetFrameLatencyWaitableObject_Original = nullptr;
IDXGISwapChain_SetMatrixTransform_pfn IDXGISwapChain_SetMatrixTransform_Original = nullptr;
IDXGISwapChain_GetMatrixTransform_pfn IDXGISwapChain_GetMatrixTransform_Original = nullptr;

// IDXGISwapChain3 original function pointers
IDXGISwapChain_GetCurrentBackBufferIndex_pfn IDXGISwapChain_GetCurrentBackBufferIndex_Original = nullptr;
IDXGISwapChain_SetColorSpace1_pfn IDXGISwapChain_SetColorSpace1_Original = nullptr;
IDXGISwapChain_ResizeBuffers1_pfn IDXGISwapChain_ResizeBuffers1_Original = nullptr;

// Hook state
static std::atomic<bool> g_dxgi_present_hooks_installed{false};
static std::atomic<bool> g_createswapchain_vtable_hooked{false};

// Hooked IDXGISwapChain::Present function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present_Detour(IDXGISwapChain *This, UINT SyncInterval, UINT Flags) {
    // Increment DXGI Present counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_PRESENT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    ::OnPresentFlags2(&Flags);

    // Call original function
    if (IDXGISwapChain_Present_Original != nullptr) {
        return IDXGISwapChain_Present_Original(This, SyncInterval, Flags);
    }

    // Fallback to direct call if hook failed
    return This->Present(SyncInterval, Flags);
}

// Hooked IDXGISwapChain1::Present1 function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present1_Detour(IDXGISwapChain1 *This, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS *pPresentParameters) {
    // Increment DXGI Present1 counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_PRESENT1].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    ::OnPresentFlags2(&PresentFlags);

    // Call original function
    if (IDXGISwapChain_Present1_Original != nullptr) {
        return IDXGISwapChain_Present1_Original(This, SyncInterval, PresentFlags, pPresentParameters);
    }

    // Fallback to direct call if hook failed
    return This->Present1(SyncInterval, PresentFlags, pPresentParameters);
}

// Hooked IDXGISwapChain::GetDesc function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetDesc_Detour(IDXGISwapChain *This, DXGI_SWAP_CHAIN_DESC *pDesc) {

    // Increment DXGI GetDesc counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_GETDESC].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Call original function
    if (IDXGISwapChain_GetDesc_Original != nullptr) {
        HRESULT hr = IDXGISwapChain_GetDesc_Original(This, pDesc);
/*
        // Hide HDR capabilities if enabled
        if (SUCCEEDED(hr) && pDesc != nullptr && s_hide_hdr_capabilities.load()) {
            // Check if the format is HDR-capable and hide it
            bool is_hdr_format = false;
            switch (pDesc->BufferDesc.Format) {
                case DXGI_FORMAT_R10G10B10A2_UNORM:     // 10-bit HDR (commonly used for HDR10)
                case DXGI_FORMAT_R11G11B10_FLOAT:        // 11-bit HDR (HDR11)
                case DXGI_FORMAT_R16G16B16A16_FLOAT:    // 16-bit HDR (HDR16)
                case DXGI_FORMAT_R32G32B32A32_FLOAT:    // 32-bit HDR (HDR32)
                case DXGI_FORMAT_R16G16B16A16_UNORM:    // 16-bit HDR (alternative)
                case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:    // 9-bit HDR (shared exponent)
                    is_hdr_format = true;
                    break;
                default:
                    break;
            }

            if (is_hdr_format) {
                // Force to SDR format
                pDesc->BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

                static int hdr_format_hidden_log_count = 0;
                if (hdr_format_hidden_log_count < 3) {
                    LogInfo("HDR hiding: GetDesc - hiding HDR format %d, forcing to R8G8B8A8_UNORM",
                           static_cast<int>(pDesc->BufferDesc.Format));
                    hdr_format_hidden_log_count++;
                }
            }

            // Remove HDR-related flags
            if ((pDesc->Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0) {
                // Keep tearing flag but remove any HDR-specific flags
                pDesc->Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
            } else {
                pDesc->Flags = 0;
            }
        }

        // Log the description if successful (only on first few calls to avoid spam)
        if (SUCCEEDED(hr) && pDesc != nullptr) {
            static int getdesc_log_count = 0;
            if (getdesc_log_count < 3) {
                LogInfo("SwapChain Desc - Width: %u, Height: %u, Format: %u, RefreshRate: %u/%u, BufferCount: %u",
                       pDesc->BufferDesc.Width, pDesc->BufferDesc.Height, pDesc->BufferDesc.Format,
                       pDesc->BufferDesc.RefreshRate.Numerator, pDesc->BufferDesc.RefreshRate.Denominator,
                       pDesc->BufferCount);
                getdesc_log_count++;
            }
        }
*/
        return hr;
    }

    // Fallback to direct call if hook failed
    return This->GetDesc(pDesc);
}

// Hooked IDXGISwapChain1::GetDesc1 function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetDesc1_Detour(IDXGISwapChain1 *This, DXGI_SWAP_CHAIN_DESC1 *pDesc) {
    // Increment DXGI GetDesc1 counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_GETDESC1].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Call original function
    if (IDXGISwapChain_GetDesc1_Original != nullptr) {
        HRESULT hr = IDXGISwapChain_GetDesc1_Original(This, pDesc);
/*
        // Hide HDR capabilities if enabled
        if (SUCCEEDED(hr) && pDesc != nullptr && s_hide_hdr_capabilities.load()) {
            // Check if the format is HDR-capable and hide it
            bool is_hdr_format = false;
            switch (pDesc->Format) {
                case DXGI_FORMAT_R10G10B10A2_UNORM:     // 10-bit HDR (commonly used for HDR10)
                case DXGI_FORMAT_R11G11B10_FLOAT:        // 11-bit HDR (HDR11)
                case DXGI_FORMAT_R16G16B16A16_FLOAT:    // 16-bit HDR (HDR16)
                case DXGI_FORMAT_R32G32B32A32_FLOAT:    // 32-bit HDR (HDR32)
                case DXGI_FORMAT_R16G16B16A16_UNORM:    // 16-bit HDR (alternative)
                case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:    // 9-bit HDR (shared exponent)
                    is_hdr_format = true;
                    break;
                default:
                    break;
            }

            if (is_hdr_format) {
                // Force to SDR format
                pDesc->Format = DXGI_FORMAT_R8G8B8A8_UNORM;

                static int hdr_format_hidden_log_count = 0;
                if (hdr_format_hidden_log_count < 3) {
                    LogInfo("HDR hiding: GetDesc1 - hiding HDR format %d, forcing to R8G8B8A8_UNORM",
                            static_cast<int>(pDesc->Format));
                    hdr_format_hidden_log_count++;
                }
            }

            // Remove HDR-related flags
            if ((pDesc->Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0) {
                // Keep tearing flag but remove any HDR-specific flags
                pDesc->Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
            } else {
                pDesc->Flags = 0;
            }
        }

        // Log the description if successful (only on first few calls to avoid spam)
        if (SUCCEEDED(hr) && pDesc != nullptr) {
            static int getdesc1_log_count = 0;
            if (getdesc1_log_count < 3) {
                LogInfo("SwapChain Desc1 - Width: %u, Height: %u, Format: %u, BufferCount: %u, SwapEffect: %u, Scaling: %u",
                       pDesc->Width, pDesc->Height, pDesc->Format,
                       pDesc->BufferCount, pDesc->SwapEffect, pDesc->Scaling);
                getdesc1_log_count++;
            }
        }
*/
        return hr;
    }

    // Fallback to direct call if hook failed
    return This->GetDesc1(pDesc);
}

// Hooked IDXGISwapChain3::CheckColorSpaceSupport function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_CheckColorSpaceSupport_Detour(IDXGISwapChain3 *This, DXGI_COLOR_SPACE_TYPE ColorSpace, UINT *pColorSpaceSupport) {
    // Increment DXGI CheckColorSpaceSupport counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_CHECKCOLORSPACESUPPORT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the color space check (only on first few calls to avoid spam)
    static int checkcolorspace_log_count = 0;
    if (checkcolorspace_log_count < 3) {
        LogInfo("CheckColorSpaceSupport called for ColorSpace: %d", static_cast<int>(ColorSpace));
        checkcolorspace_log_count++;
    }

    // Call original function
    if (IDXGISwapChain_CheckColorSpaceSupport_Original != nullptr) {
        HRESULT hr = IDXGISwapChain_CheckColorSpaceSupport_Original(This, ColorSpace, pColorSpaceSupport);

        // Hide HDR capabilities if enabled
        if (SUCCEEDED(hr) && pColorSpaceSupport != nullptr && s_hide_hdr_capabilities.load()) {
            // Check if this is an HDR color space
            bool is_hdr_colorspace = false;
            switch (ColorSpace) {
                case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:      // HDR10
                case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:   // HDR10
                case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020: // HDR10
                case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709:    // HDR10
                case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:      // HDR10
                    is_hdr_colorspace = true;
                    break;
                default:
                    break;
            }

            if (is_hdr_colorspace) {
                // Hide HDR support by setting support to 0
                *pColorSpaceSupport = 0;

                static int hdr_hidden_log_count = 0;
                if (hdr_hidden_log_count < 3) {
                    LogInfo("HDR hiding: CheckColorSpaceSupport for HDR ColorSpace %d - hiding support",
                           static_cast<int>(ColorSpace));
                    hdr_hidden_log_count++;
                }
            }
        }

        // Log the result if successful
        if (SUCCEEDED(hr) && pColorSpaceSupport != nullptr) {
            static int checkcolorspace_result_log_count = 0;
            if (checkcolorspace_result_log_count < 3) {
                LogInfo("CheckColorSpaceSupport result: ColorSpace %d support = 0x%x",
                       static_cast<int>(ColorSpace), *pColorSpaceSupport);
                checkcolorspace_result_log_count++;
            }
        }

        return hr;
    }

    // Fallback to direct call if hook failed
    return This->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport);
}

// Hooked IDXGIFactory::CreateSwapChain function
HRESULT STDMETHODCALLTYPE IDXGIFactory_CreateSwapChain_Detour(IDXGIFactory *This, IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain) {
    // Increment DXGI Factory CreateSwapChain counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_FACTORY_CREATESWAPCHAIN].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the swapchain creation parameters (only on first few calls to avoid spam)
    static int createswapchain_log_count = 0;
    if (createswapchain_log_count < 3 && pDesc != nullptr) {
        LogInfo("IDXGIFactory::CreateSwapChain - Width: %u, Height: %u, Format: %u, BufferCount: %u, SwapEffect: %u, Windowed: %s",
               pDesc->BufferDesc.Width, pDesc->BufferDesc.Height, pDesc->BufferDesc.Format,
               pDesc->BufferCount, pDesc->SwapEffect, pDesc->Windowed ? "true" : "false");
        createswapchain_log_count++;
    }

    // Call original function
    if (IDXGIFactory_CreateSwapChain_Original != nullptr) {
        HRESULT hr = IDXGIFactory_CreateSwapChain_Original(This, pDevice, pDesc, ppSwapChain);

        // If successful, hook the newly created swapchain
        if (SUCCEEDED(hr) && ppSwapChain != nullptr && *ppSwapChain != nullptr) {
            LogInfo("IDXGIFactory::CreateSwapChain succeeded, hooking new swapchain: 0x%p", *ppSwapChain);
            HookSwapchain(*ppSwapChain);
        }

        return hr;
    }

    // Fallback to direct call if hook failed
    return This->CreateSwapChain(pDevice, pDesc, ppSwapChain);
}

// Additional DXGI detour functions
HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetBuffer_Detour(IDXGISwapChain *This, UINT Buffer, REFIID riid, void **ppSurface) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_GETBUFFER].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetBuffer_Original(This, Buffer, riid, ppSurface);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetFullscreenState_Detour(IDXGISwapChain *This, BOOL Fullscreen, IDXGIOutput *pTarget) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_SETFULLSCREENSTATE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_SetFullscreenState_Original(This, Fullscreen, pTarget);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetFullscreenState_Detour(IDXGISwapChain *This, BOOL *pFullscreen, IDXGIOutput **ppTarget) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_GETFULLSCREENSTATE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetFullscreenState_Original(This, pFullscreen, ppTarget);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_ResizeBuffers_Detour(IDXGISwapChain *This, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_RESIZEBUFFERS].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_ResizeBuffers_Original(This, BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_ResizeTarget_Detour(IDXGISwapChain *This, const DXGI_MODE_DESC *pNewTargetParameters) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_RESIZETARGET].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_ResizeTarget_Original(This, pNewTargetParameters);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetContainingOutput_Detour(IDXGISwapChain *This, IDXGIOutput **ppOutput) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_GETCONTAININGOUTPUT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetContainingOutput_Original(This, ppOutput);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetFrameStatistics_Detour(IDXGISwapChain *This, DXGI_FRAME_STATISTICS *pStats) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_GETFRAMESTATISTICS].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetFrameStatistics_Original(This, pStats);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetLastPresentCount_Detour(IDXGISwapChain *This, UINT *pLastPresentCount) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_GETLASTPRESENTCOUNT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetLastPresentCount_Original(This, pLastPresentCount);
}

// IDXGISwapChain1 detour functions
HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetFullscreenDesc_Detour(IDXGISwapChain1 *This, DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pDesc) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_GETFULLSCREENDESC].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetFullscreenDesc_Original(This, pDesc);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetHwnd_Detour(IDXGISwapChain1 *This, HWND *pHwnd) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_GETHWND].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetHwnd_Original(This, pHwnd);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetCoreWindow_Detour(IDXGISwapChain1 *This, REFIID refiid, void **ppUnk) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_GETCOREWINDOW].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetCoreWindow_Original(This, refiid, ppUnk);
}

BOOL STDMETHODCALLTYPE IDXGISwapChain_IsTemporaryMonoSupported_Detour(IDXGISwapChain1 *This) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_ISTEMPORARYMONOSUPPORTED].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_IsTemporaryMonoSupported_Original(This);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetRestrictToOutput_Detour(IDXGISwapChain1 *This, IDXGIOutput **ppRestrictToOutput) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_GETRESTRICTTOOUTPUT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetRestrictToOutput_Original(This, ppRestrictToOutput);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetBackgroundColor_Detour(IDXGISwapChain1 *This, const DXGI_RGBA *pColor) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_SETBACKGROUNDCOLOR].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_SetBackgroundColor_Original(This, pColor);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetBackgroundColor_Detour(IDXGISwapChain1 *This, DXGI_RGBA *pColor) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_GETBACKGROUNDCOLOR].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetBackgroundColor_Original(This, pColor);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetRotation_Detour(IDXGISwapChain1 *This, DXGI_MODE_ROTATION Rotation) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_SETROTATION].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_SetRotation_Original(This, Rotation);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetRotation_Detour(IDXGISwapChain1 *This, DXGI_MODE_ROTATION *pRotation) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_GETROTATION].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetRotation_Original(This, pRotation);
}

// IDXGISwapChain2 detour functions
HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetSourceSize_Detour(IDXGISwapChain2 *This, UINT Width, UINT Height) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_SETSOURCESIZE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_SetSourceSize_Original(This, Width, Height);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetSourceSize_Detour(IDXGISwapChain2 *This, UINT *pWidth, UINT *pHeight) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_GETSOURCESIZE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetSourceSize_Original(This, pWidth, pHeight);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetMaximumFrameLatency_Detour(IDXGISwapChain2 *This, UINT MaxLatency) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_SETMAXIMUMFRAMELATENCY].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_SetMaximumFrameLatency_Original(This, MaxLatency);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetMaximumFrameLatency_Detour(IDXGISwapChain2 *This, UINT *pMaxLatency) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_GETMAXIMUMFRAMELATENCY].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetMaximumFrameLatency_Original(This, pMaxLatency);
}

HANDLE STDMETHODCALLTYPE IDXGISwapChain_GetFrameLatencyWaitableObject_Detour(IDXGISwapChain2 *This) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_GETFRAMELATENCYWAIABLEOBJECT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetFrameLatencyWaitableObject_Original(This);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetMatrixTransform_Detour(IDXGISwapChain2 *This, const DXGI_MATRIX_3X2_F *pMatrix) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_SETMATRIXTRANSFORM].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_SetMatrixTransform_Original(This, pMatrix);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetMatrixTransform_Detour(IDXGISwapChain2 *This, DXGI_MATRIX_3X2_F *pMatrix) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_GETMATRIXTRANSFORM].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetMatrixTransform_Original(This, pMatrix);
}

// IDXGISwapChain3 detour functions
UINT STDMETHODCALLTYPE IDXGISwapChain_GetCurrentBackBufferIndex_Detour(IDXGISwapChain3 *This) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_GETCURRENTBACKBUFFERINDEX].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetCurrentBackBufferIndex_Original(This);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetColorSpace1_Detour(IDXGISwapChain3 *This, DXGI_COLOR_SPACE_TYPE ColorSpace) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_SETCOLORSPACE1].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_SetColorSpace1_Original(This, ColorSpace);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_ResizeBuffers1_Detour(IDXGISwapChain3 *This, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags, const UINT *pCreationNodeMask, IUnknown *const *ppPresentQueue) {
    g_swapchain_event_counters[SWAPCHAIN_EVENT_DXGI_RESIZEBUFFERS1].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_ResizeBuffers1_Original(This, BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask, ppPresentQueue);
}

// Global variables to track hooked swapchains
static std::atomic<IDXGISwapChain *> g_swapchain_hooked{nullptr};
static IDXGISwapChain *g_hooked_swapchain = nullptr;

// VTable hooking functions
bool HookSwapchainVTable(IDXGISwapChain *swapchain);
bool HookFactoryVTable(IDXGIFactory *factory);

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

    g_hooked_swapchain = swapchain;
    g_swapchain_hooked.store(swapchain);

    // Get the vtable
    void **vtable = *(void ***)swapchain;
/*
| Index | Interface | Method | Description |
|-------|-----------|--------|-------------|
| 0-2 | IUnknown | QueryInterface, AddRef, Release | Base COM interface methods |
| 3-5 | IDXGIObject | SetPrivateData, SetPrivateDataInterface, GetPrivateData | Object private data management |
| 6 | IDXGIObject | GetParent | Get parent object |
| 7 | IDXGIDeviceSubObject | GetDevice | Get associated device |
| 8 | IDXGISwapChain | Present | Present frame to screen |
| 9 | IDXGISwapChain | GetBuffer | Get back buffer |
| 10 | IDXGISwapChain | SetFullscreenState | Set fullscreen state |
| 11 | IDXGISwapChain | GetFullscreenState | Get fullscreen state |
| 12 | IDXGISwapChain | GetDesc | Get swapchain description |
| 13 | IDXGISwapChain | ResizeBuffers | Resize back buffers |
| 14 | IDXGISwapChain | ResizeTarget | Resize target window |
| 15 | IDXGISwapChain | GetContainingOutput | Get containing output |
| 16 | IDXGISwapChain | GetFrameStatistics | Get frame statistics |
| 17 | IDXGISwapChain | GetLastPresentCount | Get last present count | // IDXGISwapChain up to index 17
| 18 | IDXGISwapChain1 | GetDesc1 | Get swapchain description (v1) | // IDXGISwapChain1 18
| 19 | IDXGISwapChain1 | GetFullscreenDesc | Get fullscreen description |
| 20 | IDXGISwapChain1 | GetHwnd | Get window handle |
| 21 | IDXGISwapChain1 | GetCoreWindow | Get core window |
| 22 | IDXGISwapChain1 | Present1 | Present with parameters | // ok
| 23 | IDXGISwapChain1 | IsTemporaryMonoSupported | Check mono support |
| 24 | IDXGISwapChain1 | GetRestrictToOutput | Get restricted output |
| 25 | IDXGISwapChain1 | SetBackgroundColor | Set background color |
| 26 | IDXGISwapChain1 | GetBackgroundColor | Get background color |
| 27 | IDXGISwapChain1 | SetRotation | Set rotation |
| 28 | IDXGISwapChain1 | GetRotation | Get rotation | // 28 IDXGISwapChain1
| 29 | IDXGISwapChain2 | SetSourceSize | Set source size | // 29 IDXGISwapChain2
| 30 | IDXGISwapChain2 | GetSourceSize | Get source size |
| 31 | IDXGISwapChain2 | SetMaximumFrameLatency | Set max frame latency |
| 32 | IDXGISwapChain2 | GetMaximumFrameLatency | Get max frame latency |
| 33 | IDXGISwapChain2 | GetFrameLatencyWaitableObject | Get latency waitable object |
| 34 | IDXGISwapChain2 | SetMatrixTransform | Set matrix transform |
| 35 | IDXGISwapChain2 | GetMatrixTransform | Get matrix transform | // 35 IDXGISwapChain2
| **36** | **IDXGISwapChain3** | **GetCurrentBackBufferIndex** | **Get current back buffer index** |
| **37** | **IDXGISwapChain3** | **CheckColorSpaceSupport** | **Check color space support** ⭐ |
| **38** | **IDXGISwapChain3** | **SetColorSpace1** | **Set color space** |
| **39** | **IDXGISwapChain3** | **ResizeBuffers1** | **Resize buffers with parameters** |
*/

    // Hook Present (index 8 in IDXGISwapChain vtable)
    if (MH_CreateHook(vtable[8], IDXGISwapChain_Present_Detour, (LPVOID *)&IDXGISwapChain_Present_Original) != MH_OK) {
        LogError("Failed to create IDXGISwapChain::Present hook");
        return false;
    }

    // Hook GetDesc (index 12 in IDXGISwapChain vtable)

    // Hook GetDesc1 (index 18 in IDXGISwapChain1+ vtable) - only if the swapchain supports it
    // Check if this is IDXGISwapChain1 or higher by checking if the method exists

    // Hook Present1 (index 22 in IDXGISwapChain1+ vtable) - only if the swapchain supports it
    // Check if this is IDXGISwapChain1 or higher by checking if the method exists



    // Hook CheckColorSpaceSupport (index 37 in IDXGISwapChain3+ vtable) - only if the swapchain supports it
    // Check if this is IDXGISwapChain3 or higher by checking if the method exists
    // Reference: Windows SDK dxgi1_4.h - IDXGISwapChain3 interface definition
    // Documentation: docs/DXGI_VTABLE_INDICES.md - Complete vtable index reference


    // Hook all additional DXGI methods (indices 9-17, 19-21, 23-39)
    // IDXGISwapChain methods (9-17)
    if (vtable[9] != nullptr) {
        if (MH_CreateHook(vtable[9], IDXGISwapChain_GetBuffer_Detour, (LPVOID *)&IDXGISwapChain_GetBuffer_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain::GetBuffer hook");
        }
    }
    if (vtable[10] != nullptr) {
        if (MH_CreateHook(vtable[10], IDXGISwapChain_SetFullscreenState_Detour, (LPVOID *)&IDXGISwapChain_SetFullscreenState_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain::SetFullscreenState hook");
        }
    }
    if (vtable[11] != nullptr) {
        if (MH_CreateHook(vtable[11], IDXGISwapChain_GetFullscreenState_Detour, (LPVOID *)&IDXGISwapChain_GetFullscreenState_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain::GetFullscreenState hook");
        }
    }
    if (MH_CreateHook(vtable[12], IDXGISwapChain_GetDesc_Detour, (LPVOID *)&IDXGISwapChain_GetDesc_Original) != MH_OK) {
        LogError("Failed to create IDXGISwapChain::GetDesc hook");
       // return false;
    }
    if (vtable[13] != nullptr) {
        if (MH_CreateHook(vtable[13], IDXGISwapChain_ResizeBuffers_Detour, (LPVOID *)&IDXGISwapChain_ResizeBuffers_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain::ResizeBuffers hook");
        }
    }
    if (vtable[14] != nullptr) {
        if (MH_CreateHook(vtable[14], IDXGISwapChain_ResizeTarget_Detour, (LPVOID *)&IDXGISwapChain_ResizeTarget_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain::ResizeTarget hook");
        }
    }
    if (vtable[15] != nullptr) {
        if (MH_CreateHook(vtable[15], IDXGISwapChain_GetContainingOutput_Detour, (LPVOID *)&IDXGISwapChain_GetContainingOutput_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain::GetContainingOutput hook");
        }
    }
    if (vtable[16] != nullptr) {
        if (MH_CreateHook(vtable[16], IDXGISwapChain_GetFrameStatistics_Detour, (LPVOID *)&IDXGISwapChain_GetFrameStatistics_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain::GetFrameStatistics hook");
        }
    }
    if (vtable[17] != nullptr) {
        if (MH_CreateHook(vtable[17], IDXGISwapChain_GetLastPresentCount_Detour, (LPVOID *)&IDXGISwapChain_GetLastPresentCount_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain::GetLastPresentCount hook");
        }
    }
    if (vtable[18] != nullptr) {
        if (MH_CreateHook(vtable[18], IDXGISwapChain_GetDesc1_Detour, (LPVOID *)&IDXGISwapChain_GetDesc1_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain1::GetDesc1 hook");
            return false;
        }
    }
    // IDXGISwapChain1 methods (19-21, 23-28)
    if (vtable[19] != nullptr) {
        if (MH_CreateHook(vtable[19], IDXGISwapChain_GetFullscreenDesc_Detour, (LPVOID *)&IDXGISwapChain_GetFullscreenDesc_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain1::GetFullscreenDesc hook");
        }
    }
    if (vtable[20] != nullptr) {
        if (MH_CreateHook(vtable[20], IDXGISwapChain_GetHwnd_Detour, (LPVOID *)&IDXGISwapChain_GetHwnd_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain1::GetHwnd hook");
        }
    }
    if (vtable[21] != nullptr) {
        if (MH_CreateHook(vtable[21], IDXGISwapChain_GetCoreWindow_Detour, (LPVOID *)&IDXGISwapChain_GetCoreWindow_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain1::GetCoreWindow hook");
        }
    }
    if (vtable[22] != nullptr) {
        if (MH_CreateHook(vtable[22], IDXGISwapChain_Present1_Detour, (LPVOID *)&IDXGISwapChain_Present1_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain1::Present1 hook");
            return false;
        }
    }
    if (vtable[23] != nullptr) {
        if (MH_CreateHook(vtable[23], IDXGISwapChain_IsTemporaryMonoSupported_Detour, (LPVOID *)&IDXGISwapChain_IsTemporaryMonoSupported_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain1::IsTemporaryMonoSupported hook");
        }
    }
    if (vtable[24] != nullptr) {
        if (MH_CreateHook(vtable[24], IDXGISwapChain_GetRestrictToOutput_Detour, (LPVOID *)&IDXGISwapChain_GetRestrictToOutput_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain1::GetRestrictToOutput hook");
        }
    }
    if (vtable[25] != nullptr) {
        if (MH_CreateHook(vtable[25], IDXGISwapChain_SetBackgroundColor_Detour, (LPVOID *)&IDXGISwapChain_SetBackgroundColor_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain1::SetBackgroundColor hook");
        }
    }
    if (vtable[26] != nullptr) {
        if (MH_CreateHook(vtable[26], IDXGISwapChain_GetBackgroundColor_Detour, (LPVOID *)&IDXGISwapChain_GetBackgroundColor_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain1::GetBackgroundColor hook");
        }
    }
    if (vtable[27] != nullptr) {
        if (MH_CreateHook(vtable[27], IDXGISwapChain_SetRotation_Detour, (LPVOID *)&IDXGISwapChain_SetRotation_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain1::SetRotation hook");
        }
    }
    if (vtable[28] != nullptr) {
        if (MH_CreateHook(vtable[28], IDXGISwapChain_GetRotation_Detour, (LPVOID *)&IDXGISwapChain_GetRotation_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain1::GetRotation hook");
        }
    }

    // IDXGISwapChain2 methods (29-35)
    if (vtable[29] != nullptr) {
        if (MH_CreateHook(vtable[29], IDXGISwapChain_SetSourceSize_Detour, (LPVOID *)&IDXGISwapChain_SetSourceSize_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain2::SetSourceSize hook");
        }
    }
    if (vtable[30] != nullptr) {
        if (MH_CreateHook(vtable[30], IDXGISwapChain_GetSourceSize_Detour, (LPVOID *)&IDXGISwapChain_GetSourceSize_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain2::GetSourceSize hook");
        }
    }
    if (vtable[31] != nullptr) {
        if (MH_CreateHook(vtable[31], IDXGISwapChain_SetMaximumFrameLatency_Detour, (LPVOID *)&IDXGISwapChain_SetMaximumFrameLatency_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain2::SetMaximumFrameLatency hook");
        }
    }
    if (vtable[32] != nullptr) {
        if (MH_CreateHook(vtable[32], IDXGISwapChain_GetMaximumFrameLatency_Detour, (LPVOID *)&IDXGISwapChain_GetMaximumFrameLatency_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain2::GetMaximumFrameLatency hook");
        }
    }
    if (vtable[33] != nullptr) {
        if (MH_CreateHook(vtable[33], IDXGISwapChain_GetFrameLatencyWaitableObject_Detour, (LPVOID *)&IDXGISwapChain_GetFrameLatencyWaitableObject_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain2::GetFrameLatencyWaitableObject hook");
        }
    }
    if (vtable[34] != nullptr) {
        if (MH_CreateHook(vtable[34], IDXGISwapChain_SetMatrixTransform_Detour, (LPVOID *)&IDXGISwapChain_SetMatrixTransform_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain2::SetMatrixTransform hook");
        }
    }
    if (vtable[35] != nullptr) {
        if (MH_CreateHook(vtable[35], IDXGISwapChain_GetMatrixTransform_Detour, (LPVOID *)&IDXGISwapChain_GetMatrixTransform_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain2::GetMatrixTransform hook");
        }
    }

    // IDXGISwapChain3 methods (36, 38-39)
    if (vtable[36] != nullptr) {
        if (MH_CreateHook(vtable[36], IDXGISwapChain_GetCurrentBackBufferIndex_Detour, (LPVOID *)&IDXGISwapChain_GetCurrentBackBufferIndex_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain3::GetCurrentBackBufferIndex hook");
        }
    }
    if (vtable[37] != nullptr) {
        if (MH_CreateHook(vtable[37], IDXGISwapChain_CheckColorSpaceSupport_Detour, (LPVOID *)&IDXGISwapChain_CheckColorSpaceSupport_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain3::CheckColorSpaceSupport hook");
            //return false;
        }
    }
    if (vtable[38] != nullptr) {
        if (MH_CreateHook(vtable[38], IDXGISwapChain_SetColorSpace1_Detour, (LPVOID *)&IDXGISwapChain_SetColorSpace1_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain3::SetColorSpace1 hook");
        }
    }
    if (vtable[39] != nullptr) {
        if (MH_CreateHook(vtable[39], IDXGISwapChain_ResizeBuffers1_Detour, (LPVOID *)&IDXGISwapChain_ResizeBuffers1_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain3::ResizeBuffers1 hook");
        }
    }

    // Enable the Present hook
    if (MH_EnableHook(vtable[8]) != MH_OK) {
        LogError("Failed to enable IDXGISwapChain::Present hook");
      //  return false;
    }


    // Enable all additional DXGI hooks
    // IDXGISwapChain methods (9-17)
    if (vtable[9] != nullptr) MH_EnableHook(vtable[9]);
    if (vtable[10] != nullptr) MH_EnableHook(vtable[10]);
    if (vtable[11] != nullptr) MH_EnableHook(vtable[11]);
    if (vtable[12] != nullptr) MH_EnableHook(vtable[12]);
    if (vtable[13] != nullptr) MH_EnableHook(vtable[13]);
    if (vtable[14] != nullptr) MH_EnableHook(vtable[14]);
    if (vtable[15] != nullptr) MH_EnableHook(vtable[15]);
    if (vtable[16] != nullptr) MH_EnableHook(vtable[16]);
    if (vtable[17] != nullptr) MH_EnableHook(vtable[17]);

    // IDXGISwapChain1 methods (18-28)
    if (vtable[18] != nullptr) MH_EnableHook(vtable[18]);
    if (vtable[19] != nullptr) MH_EnableHook(vtable[19]);
    if (vtable[20] != nullptr) MH_EnableHook(vtable[20]);
    if (vtable[21] != nullptr) MH_EnableHook(vtable[21]);
    if (vtable[22] != nullptr) MH_EnableHook(vtable[22]);
    if (vtable[23] != nullptr) MH_EnableHook(vtable[23]);
    if (vtable[24] != nullptr) MH_EnableHook(vtable[24]);
    if (vtable[25] != nullptr) MH_EnableHook(vtable[25]);
    if (vtable[26] != nullptr) MH_EnableHook(vtable[26]);
    if (vtable[27] != nullptr) MH_EnableHook(vtable[27]);
    if (vtable[28] != nullptr) MH_EnableHook(vtable[28]);

    // IDXGISwapChain2 methods (29-35)
    if (vtable[29] != nullptr) MH_EnableHook(vtable[29]);
    if (vtable[30] != nullptr) MH_EnableHook(vtable[30]);
    if (vtable[31] != nullptr) MH_EnableHook(vtable[31]);
    if (vtable[32] != nullptr) MH_EnableHook(vtable[32]);
    if (vtable[33] != nullptr) MH_EnableHook(vtable[33]);
    if (vtable[34] != nullptr) MH_EnableHook(vtable[34]);
    if (vtable[35] != nullptr) MH_EnableHook(vtable[35]);

    // IDXGISwapChain3 methods (36, 38-39)
    if (vtable[36] != nullptr) MH_EnableHook(vtable[36]);
    if (vtable[37] != nullptr) MH_EnableHook(vtable[37]);
    if (vtable[38] != nullptr) MH_EnableHook(vtable[38]);
    if (vtable[39] != nullptr) MH_EnableHook(vtable[39]);

    // Build success message based on which hooks were installed
    std::string hook_message = "Successfully hooked DXGI methods: Present, GetDesc";
    if (vtable[9] != nullptr) hook_message += ", GetBuffer";
    if (vtable[10] != nullptr) hook_message += ", SetFullscreenState";
    if (vtable[11] != nullptr) hook_message += ", GetFullscreenState";
    if (vtable[13] != nullptr) hook_message += ", ResizeBuffers";
    if (vtable[14] != nullptr) hook_message += ", ResizeTarget";
    if (vtable[15] != nullptr) hook_message += ", GetContainingOutput";
    if (vtable[16] != nullptr) hook_message += ", GetFrameStatistics";
    if (vtable[17] != nullptr) hook_message += ", GetLastPresentCount";
    if (vtable[18] != nullptr) hook_message += ", GetDesc1";
    if (vtable[19] != nullptr) hook_message += ", GetFullscreenDesc";
    if (vtable[20] != nullptr) hook_message += ", GetHwnd";
    if (vtable[21] != nullptr) hook_message += ", GetCoreWindow";
    if (vtable[22] != nullptr) hook_message += ", Present1";
    if (vtable[23] != nullptr) hook_message += ", IsTemporaryMonoSupported";
    if (vtable[24] != nullptr) hook_message += ", GetRestrictToOutput";
    if (vtable[25] != nullptr) hook_message += ", SetBackgroundColor";
    if (vtable[26] != nullptr) hook_message += ", GetBackgroundColor";
    if (vtable[27] != nullptr) hook_message += ", SetRotation";
    if (vtable[28] != nullptr) hook_message += ", GetRotation";
    if (vtable[29] != nullptr) hook_message += ", SetSourceSize";
    if (vtable[30] != nullptr) hook_message += ", GetSourceSize";
    if (vtable[31] != nullptr) hook_message += ", SetMaximumFrameLatency";
    if (vtable[32] != nullptr) hook_message += ", GetMaximumFrameLatency";
    if (vtable[33] != nullptr) hook_message += ", GetFrameLatencyWaitableObject";
    if (vtable[34] != nullptr) hook_message += ", SetMatrixTransform";
    if (vtable[35] != nullptr) hook_message += ", GetMatrixTransform";
    if (vtable[36] != nullptr) hook_message += ", GetCurrentBackBufferIndex";
    if (vtable[37] != nullptr) hook_message += ", CheckColorSpaceSupport";
    if (vtable[38] != nullptr) hook_message += ", SetColorSpace1";
    if (vtable[39] != nullptr) hook_message += ", ResizeBuffers1";
    hook_message += " for swapchain: 0x%p";

    LogInfo(hook_message.c_str(), swapchain);

    return true;
}

// Hook a specific factory's vtable
bool HookFactoryVTable(IDXGIFactory *factory) {
    if (factory == nullptr) {
        return false;
    }

    // Check if we've already hooked the CreateSwapChain vtable
    if (g_createswapchain_vtable_hooked.load()) {
        LogInfo("IDXGIFactory::CreateSwapChain vtable already hooked, skipping");
        return true;
    }

    // Get the vtable
    void **vtable = *(void ***)factory;

    /*
     * IDXGIFactory VTable Layout Reference
     * ===================================
     *
     * IDXGIFactory inherits from IDXGIObject, which inherits from IUnknown.
     * The vtable contains all methods from the inheritance chain.
     *
     * VTable Indices 0-5:
     * [0-2]   IUnknown methods (QueryInterface, AddRef, Release)
     * [3-5]   IDXGIObject methods (SetPrivateData, SetPrivateDataInterface, GetPrivateData)
     * [6]     IDXGIObject methods (GetParent)
     * [7-11]  IDXGIFactory methods (EnumAdapters, MakeWindowAssociation, GetWindowAssociation, CreateSwapChain, CreateSoftwareAdapter)
     *
     * CreateSwapChain is at index 10 in the IDXGIFactory vtable.
     */

    // Hook CreateSwapChain (index 10 in IDXGIFactory vtable)
    if (vtable[10] != nullptr) {
        LogInfo("Attempting to hook IDXGIFactory::CreateSwapChain at vtable[10] = 0x%p", vtable[10]);

        // Check if we've already set the original function pointer (indicates already hooked)
        if (IDXGIFactory_CreateSwapChain_Original != nullptr) {
            LogWarn("IDXGIFactory::CreateSwapChain already hooked, skipping");
            g_createswapchain_vtable_hooked.store(true);
            return true; // Consider this success since it's already hooked
        }

        MH_STATUS hook_status = MH_CreateHook(vtable[10], IDXGIFactory_CreateSwapChain_Detour, (LPVOID *)&IDXGIFactory_CreateSwapChain_Original);
        if (hook_status != MH_OK) {
            LogError("Failed to create IDXGIFactory::CreateSwapChain hook - Status: %d (0x%x)", hook_status, hook_status);

            // Check if MinHook is initialized
            MH_STATUS init_status = MH_Initialize();
            LogInfo("MinHook initialization status: %d (0x%x)", init_status, init_status);

            return false;
        }

        // Enable the CreateSwapChain hook
        if (MH_EnableHook(vtable[10]) != MH_OK) {
            LogError("Failed to enable IDXGIFactory::CreateSwapChain hook");
            return false;
        }

        LogInfo("Successfully hooked IDXGIFactory::CreateSwapChain for factory: 0x%p", factory);
        g_createswapchain_vtable_hooked.store(true);
        return true;
    }

    LogError("IDXGIFactory::CreateSwapChain method not found in vtable");
    return false;
}

// Public function to hook a swapchain when it's created
bool HookSwapchain(IDXGISwapChain *swapchain) {
    return HookSwapchainVTable(swapchain);
}

// Public function to hook a factory when it's created
bool HookFactory(IDXGIFactory *factory) {
    return HookFactoryVTable(factory);
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
