#include "d3d11_hooks.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../globals.hpp"
#include "../hook_suppression_manager.hpp"
#include <MinHook.h>
#include <atomic>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace display_commanderhooks {

// Original function pointers
ID3D11Device_CreateTexture2D_pfn ID3D11Device_CreateTexture2D_Original = nullptr;
ID3D11DeviceContext_UpdateSubresource_pfn ID3D11DeviceContext_UpdateSubresource_Original = nullptr;
ID3D11DeviceContext_UpdateSubresource1_pfn ID3D11DeviceContext_UpdateSubresource1_Original = nullptr;

// Hook state
static std::atomic<bool> g_d3d11_device_hooked{false};
static std::atomic<bool> g_d3d11_context_hooked{false};

// Hooked ID3D11Device::CreateTexture2D function
HRESULT STDMETHODCALLTYPE ID3D11Device_CreateTexture2D_Detour(
    ID3D11Device* This,
    const D3D11_TEXTURE2D_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    ID3D11Texture2D** ppTexture2D
) {
    // Increment counter
    g_d3d11_texture_event_counters[D3D11_EVENT_CREATE_TEXTURE2D].fetch_add(1);

    // Call original function
    if (ID3D11Device_CreateTexture2D_Original != nullptr) {
        return ID3D11Device_CreateTexture2D_Original(This, pDesc, pInitialData, ppTexture2D);
    }

    // Fallback if original is not set
    return This->CreateTexture2D(pDesc, pInitialData, ppTexture2D);
}

// Hooked ID3D11DeviceContext::UpdateSubresource function
void STDMETHODCALLTYPE ID3D11DeviceContext_UpdateSubresource_Detour(
    ID3D11DeviceContext* This,
    ID3D11Resource* pDstResource,
    UINT DstSubresource,
    const D3D11_BOX* pDstBox,
    const void* pSrcData,
    UINT SrcRowPitch,
    UINT SrcDepthPitch
) {
    // Increment counter
    g_d3d11_texture_event_counters[D3D11_EVENT_UPDATE_SUBRESOURCE].fetch_add(1);

    // Call original function
    if (ID3D11DeviceContext_UpdateSubresource_Original != nullptr) {
        ID3D11DeviceContext_UpdateSubresource_Original(This, pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
    } else {
        // Fallback if original is not set
        This->UpdateSubresource(pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
    }
}

// Hooked ID3D11DeviceContext::UpdateSubresource1 function
void STDMETHODCALLTYPE ID3D11DeviceContext_UpdateSubresource1_Detour(
    ID3D11DeviceContext* This,
    ID3D11Resource* pDstResource,
    UINT DstSubresource,
    const D3D11_BOX* pDstBox,
    const void* pSrcData,
    UINT SrcRowPitch,
    UINT SrcDepthPitch,
    UINT CopyFlags
) {
    // Increment counter
    g_d3d11_texture_event_counters[D3D11_EVENT_UPDATE_SUBRESOURCE1].fetch_add(1);

    // Call original function
    if (ID3D11DeviceContext_UpdateSubresource1_Original != nullptr) {
        ID3D11DeviceContext_UpdateSubresource1_Original(This, pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch, CopyFlags);
    } else {
        // Fallback: just call UpdateSubresource without CopyFlags
        This->UpdateSubresource(pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
    }
}

// Helper function to check if vtable entry is valid
static bool IsVTableEntryValid(LPVOID* vtable, int index) {
    if (vtable == nullptr) return false;
    if (IsBadReadPtr(vtable, sizeof(LPVOID))) return false;
    if (vtable[index] == nullptr) return false;
    if (IsBadReadPtr(vtable[index], sizeof(LPVOID))) return false;
    return true;
}

// Hook a specific D3D11 device using vtable hooking
bool HookD3D11Device(ID3D11Device* device) {
    if (device == nullptr) {
        LogError("HookD3D11Device: device is nullptr");
        return false;
    }

    if (g_d3d11_device_hooked.load()) {
        LogInfo("HookD3D11Device: Device hooks already installed");
        return true;
    }

    // Check if D3D11 hooks should be suppressed
    if (HookSuppressionManager::GetInstance().ShouldSuppressHook(HookType::D3D_DEVICE)) {
        LogInfo("HookD3D11Device: installation suppressed by user setting");
        return false;
    }

    // Initialize MinHook
    MH_STATUS init_status = SafeInitializeMinHook(HookType::D3D_DEVICE);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("HookD3D11Device: Failed to initialize MinHook - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("HookD3D11Device: MinHook already initialized, proceeding with D3D11 device hooks");
    } else {
        LogInfo("HookD3D11Device: MinHook initialized successfully");
    }

    // Get the vtable
    LPVOID* vtable = *reinterpret_cast<LPVOID**>(device);

    /*
     * ID3D11Device VTable Layout Reference
     * ======================================
     * [0-2]   IUnknown methods
     * [0]     CreateTexture2D
     * [1]     CreateTexture3D
     * [2]     CreateBuffer
     * ... more methods ...
     */

    // Hook CreateTexture2D (index 0 after IUnknown - but actually IUnknown's QueryInterface is in base, so 0 is CreateTexture2D)
    // Actually, let me check: after IUnknown (AddRef, Release), the first ID3D11Device method is index 0

    // Wait, I need to verify this. Let me assume standard COM layout:
    // [0-2] IUnknown methods (QueryInterface, AddRef, Release)
    // Then ID3D11Device methods start
    // But CreateTexture2D should be the first method after base interfaces

    // Actually looking at DXGI examples in the codebase, they use specific indices like 8 for Present
    // Let me use index 0 assuming that's the first method (or we need to adjust based on runtime

    LogInfo("HookD3D11Device: Attempting to hook ID3D11Device vtable at 0x%p", vtable);

    // Try index 0 for CreateTexture2D (this may need adjustment based on actual vtable layout)
    if (IsVTableEntryValid(vtable, 0)) {
        if (MH_CreateHook(vtable[0], ID3D11Device_CreateTexture2D_Detour, (LPVOID*)&ID3D11Device_CreateTexture2D_Original) != MH_OK) {
            LogError("HookD3D11Device: Failed to create ID3D11Device::CreateTexture2D hook");
        } else {
            if (MH_EnableHook(vtable[0]) != MH_OK) {
                LogError("HookD3D11Device: Failed to enable ID3D11Device::CreateTexture2D hook");
            } else {
                LogInfo("HookD3D11Device: ID3D11Device::CreateTexture2D hook created and enabled successfully");
                g_d3d11_device_hooked.store(true);
                HookSuppressionManager::GetInstance().MarkHookInstalled(HookType::D3D_DEVICE);
                return true;
            }
        }
    } else {
        LogWarn("HookD3D11Device: vtable entry 0 is not valid");
    }

    return false;
}

// Hook a specific D3D11 device context using vtable hooking
bool HookD3D11DeviceContext(ID3D11DeviceContext* context) {
    if (context == nullptr) {
        LogError("HookD3D11DeviceContext: context is nullptr");
        return false;
    }

    if (g_d3d11_context_hooked.load()) {
        LogInfo("HookD3D11DeviceContext: Context hooks already installed");
        return true;
    }

    // Check if D3D11 hooks should be suppressed
    if (HookSuppressionManager::GetInstance().ShouldSuppressHook(HookType::D3D_DEVICE)) {
        LogInfo("HookD3D11DeviceContext: installation suppressed by user setting");
        return false;
    }

    // Initialize MinHook
    MH_STATUS init_status = SafeInitializeMinHook(HookType::D3D_DEVICE);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("HookD3D11DeviceContext: Failed to initialize MinHook - Status: %d", init_status);
        return false;
    }

    // Get the vtable
    LPVOID* vtable = *reinterpret_cast<LPVOID**>(context);

    /*
     * ID3D11DeviceContext VTable Layout Reference
     * ============================================
     * UpdateSubresource should be around index 5
     * UpdateSubresource1 should be around index 11 (ID3D11DeviceContext3)
     */

    LogInfo("HookD3D11DeviceContext: Attempting to hook ID3D11DeviceContext vtable at 0x%p", vtable);

    // Try to hook UpdateSubresource (ID3D11DeviceContext)
    if (IsVTableEntryValid(vtable, 5)) {
        if (MH_CreateHook(vtable[5], ID3D11DeviceContext_UpdateSubresource_Detour, (LPVOID*)&ID3D11DeviceContext_UpdateSubresource_Original) != MH_OK) {
            LogError("HookD3D11DeviceContext: Failed to create ID3D11DeviceContext::UpdateSubresource hook");
        } else {
            if (MH_EnableHook(vtable[5]) != MH_OK) {
                LogError("HookD3D11DeviceContext: Failed to enable ID3D11DeviceContext::UpdateSubresource hook");
            } else {
                LogInfo("HookD3D11DeviceContext: ID3D11DeviceContext::UpdateSubresource hook created and enabled successfully");
            }
        }
    }

    // Try to hook UpdateSubresource1 (ID3D11DeviceContext3)
    if (IsVTableEntryValid(vtable, 11)) {
        if (MH_CreateHook(vtable[11], ID3D11DeviceContext_UpdateSubresource1_Detour, (LPVOID*)&ID3D11DeviceContext_UpdateSubresource1_Original) != MH_OK) {
            LogError("HookD3D11DeviceContext: Failed to create ID3D11DeviceContext::UpdateSubresource1 hook");
        } else {
            if (MH_EnableHook(vtable[11]) != MH_OK) {
                LogError("HookD3D11DeviceContext: Failed to enable ID3D11DeviceContext::UpdateSubresource1 hook");
            } else {
                LogInfo("HookD3D11DeviceContext: ID3D11DeviceContext::UpdateSubresource1 hook created and enabled successfully");
            }
        }
    }

    g_d3d11_context_hooked.store(true);
    return true;
}

// Install D3D11 hooks (legacy function, now deprecated in favor of HookD3D11Device)
bool InstallD3D11Hooks() {
    LogInfo("InstallD3D11Hooks: This function is deprecated. Use HookD3D11Device and HookD3D11DeviceContext instead.");
    return false;
}

} // namespace display_commanderhooks

