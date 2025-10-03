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

// Hook state
static std::atomic<bool> g_dxgi_present_hooks_installed{false};

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

// Global variables to track hooked swapchains
static std::atomic<IDXGISwapChain *> g_swapchain_hooked{nullptr};
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
    if (g_swapchain_hooked.load() == swapchain) {
        return false;
    }

    // Get the vtable
    void **vtable = *(void ***)swapchain;

    // IDXGISwapChain vtable layout (indices 0-29):
    // [0]  IUnknown::QueryInterface
    // [1]  IUnknown::AddRef
    // [2]  IUnknown::Release
    // [3]  IDXGIObject::SetPrivateData
    // [4]  IDXGIObject::SetPrivateDataInterface
    // [5]  IDXGIObject::GetPrivateData
    // [6]  IDXGIDeviceSubObject::GetDevice
    // [7]  IDXGIDeviceSubObject::GetParent
    // [8]  IDXGISwapChain::Present                    ← Hooked
    // [9]  IDXGISwapChain::GetBuffer
    // [10] IDXGISwapChain::SetFullscreenState
    // [11] IDXGISwapChain::GetFullscreenState
    // [12] IDXGISwapChain::GetDesc                    ← Hooked
    // [13] IDXGISwapChain::ResizeBuffers
    // [14] IDXGISwapChain::ResizeTarget
    // [15] IDXGISwapChain::GetContainingOutput
    // [16] IDXGISwapChain::GetFrameStatistics
    // [17] IDXGISwapChain::GetLastPresentCount
    // [18] IDXGISwapChain1::Present1                  ← Hooked (if IDXGISwapChain1+)
    // [19] IDXGISwapChain1::GetDesc1                  ← Hooked (if IDXGISwapChain1+)
    // [20] IDXGISwapChain1::GetFullscreenDesc
    // [21] IDXGISwapChain1::GetHwnd
    // [22] IDXGISwapChain2::SetSourceSize
    // [23] IDXGISwapChain2::GetSourceSize
    // [24] IDXGISwapChain2::SetMaximumFrameLatency
    // [25] IDXGISwapChain2::GetMaximumFrameLatency
    // [26] IDXGISwapChain3::GetCurrentBackBufferIndex
    // [27] IDXGISwapChain3::CheckColorSpaceSupport    ← Hooked (if IDXGISwapChain3+)
    // [28] IDXGISwapChain3::SetColorSpace1
    // [29] IDXGISwapChain3::ResizeBuffers1

    // Hook Present (index 8 in IDXGISwapChain vtable)
    if (MH_CreateHook(vtable[8], IDXGISwapChain_Present_Detour, (LPVOID *)&IDXGISwapChain_Present_Original) != MH_OK) {
        LogError("Failed to create IDXGISwapChain::Present hook");
        return false;
    }

    // Hook GetDesc (index 12 in IDXGISwapChain vtable)
    if (MH_CreateHook(vtable[12], IDXGISwapChain_GetDesc_Detour, (LPVOID *)&IDXGISwapChain_GetDesc_Original) != MH_OK) {
        LogError("Failed to create IDXGISwapChain::GetDesc hook");
        return false;
    }

    // Hook Present1 (index 18 in IDXGISwapChain1+ vtable) - only if the swapchain supports it
    // Check if this is IDXGISwapChain1 or higher by checking if the method exists
    if (vtable[18] != nullptr) {
        if (MH_CreateHook(vtable[18], IDXGISwapChain_Present1_Detour, (LPVOID *)&IDXGISwapChain_Present1_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain1::Present1 hook");
            return false;
        }
    }

    // Hook GetDesc1 (index 19 in IDXGISwapChain1+ vtable) - only if the swapchain supports it
    // Check if this is IDXGISwapChain1 or higher by checking if the method exists
    if (vtable[19] != nullptr) {
        if (MH_CreateHook(vtable[19], IDXGISwapChain_GetDesc1_Detour, (LPVOID *)&IDXGISwapChain_GetDesc1_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain1::GetDesc1 hook");
            return false;
        }
    }

    // Hook CheckColorSpaceSupport (index 27 in IDXGISwapChain3+ vtable) - only if the swapchain supports it
    // Check if this is IDXGISwapChain3 or higher by checking if the method exists
    if (vtable[27] != nullptr) {
        if (MH_CreateHook(vtable[27], IDXGISwapChain_CheckColorSpaceSupport_Detour, (LPVOID *)&IDXGISwapChain_CheckColorSpaceSupport_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain3::CheckColorSpaceSupport hook");
            return false;
        }
    }

    // Enable the Present hook
    if (MH_EnableHook(vtable[8]) != MH_OK) {
        LogError("Failed to enable IDXGISwapChain::Present hook");
        return false;
    }

    // Enable the GetDesc hook
    if (MH_EnableHook(vtable[12]) != MH_OK) {
        LogError("Failed to enable IDXGISwapChain::GetDesc hook");
        return false;
    }

    // Enable the Present1 hook (if it was created)
    if (vtable[18] != nullptr) {
        if (MH_EnableHook(vtable[18]) != MH_OK) {
            LogError("Failed to enable IDXGISwapChain1::Present1 hook");
            return false;
        }
    }

    // Enable the GetDesc1 hook (if it was created)
    if (vtable[19] != nullptr) {
        if (MH_EnableHook(vtable[19]) != MH_OK) {
            LogError("Failed to enable IDXGISwapChain1::GetDesc1 hook");
            return false;
        }
    }

    // Enable the CheckColorSpaceSupport hook (if it was created)
    if (vtable[27] != nullptr) {
        if (MH_EnableHook(vtable[27]) != MH_OK) {
            LogError("Failed to enable IDXGISwapChain3::CheckColorSpaceSupport hook");
            return false;
        }
    }

    g_hooked_swapchain = swapchain;
    g_swapchain_hooked.store(swapchain);

    // Build success message based on which hooks were installed
    std::string hook_message = "Successfully hooked IDXGISwapChain::Present and GetDesc";
    if (vtable[18] != nullptr) {
        hook_message += ", Present1";
    }
    if (vtable[19] != nullptr) {
        hook_message += ", GetDesc1";
    }
    if (vtable[27] != nullptr) {
        hook_message += ", CheckColorSpaceSupport";
    }
    hook_message += " vtable entries for swapchain: 0x%p";

    LogInfo(hook_message.c_str(), swapchain);

    return true;
}

// Public function to hook a swapchain when it's created
bool HookSwapchain(IDXGISwapChain *swapchain) {
    return HookSwapchainVTable(swapchain);
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
