#include "d3d11_sampler_hooks.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/general_utils.hpp"
#include "../../globals.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../hook_suppression_manager.hpp"
#include <MinHook.h>
#include <atomic>

namespace display_commanderhooks {

// Original function pointers
ID3D11Device_CreateSamplerState_pfn ID3D11Device_CreateSamplerState_Original = nullptr;

// Hook state
static std::atomic<bool> g_d3d11_sampler_hooked{false};

// Helper function to check if vtable entry is valid
static bool IsVTableEntryValid(LPVOID* vtable, int index) {
    if (vtable == nullptr) return false;
    if (IsBadReadPtr(vtable, sizeof(LPVOID))) return false;
    if (vtable[index] == nullptr) return false;
    if (IsBadReadPtr(vtable[index], sizeof(LPVOID))) return false;
    return true;
}

// Hooked ID3D11Device::CreateSamplerState function
HRESULT STDMETHODCALLTYPE ID3D11Device_CreateSamplerState_Detour(
    ID3D11Device* This,
    const D3D11_SAMPLER_DESC* pSamplerDesc,
    ID3D11SamplerState** ppSamplerState
) {
    // Increment counter
    g_d3d_sampler_event_counters[D3D_SAMPLER_EVENT_CREATE_SAMPLER_STATE_D3D11].fetch_add(1);

    // Apply overrides if enabled
    if (pSamplerDesc != nullptr && ppSamplerState != nullptr) {
        D3D11_SAMPLER_DESC modified_desc = *pSamplerDesc;
        bool modified = false;

        // Apply mipmap LOD bias override
        if (settings::g_mainTabSettings.force_mipmap_lod_bias.GetValue() != 0.0f) {
            // Only apply if MinLOD != MaxLOD and comparison func is NEVER (non-shadow samplers)
            if (modified_desc.MinLOD != modified_desc.MaxLOD &&
                modified_desc.ComparisonFunc == D3D11_COMPARISON_NEVER) {
                modified_desc.MipLODBias = settings::g_mainTabSettings.force_mipmap_lod_bias.GetValue();
                modified = true;
            }
        }

        // Apply anisotropic filtering override
        if (settings::g_mainTabSettings.force_anisotropic_filtering.GetValue()) {
            // Convert linear filters to anisotropic
            switch (modified_desc.Filter) {
                case D3D11_FILTER_MIN_MAG_MIP_LINEAR:
                    modified_desc.Filter = D3D11_FILTER_ANISOTROPIC;
                    modified = true;
                    break;
                case D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR:
                    modified_desc.Filter = D3D11_FILTER_COMPARISON_ANISOTROPIC;
                    modified = true;
                    break;
                case D3D11_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR:
                    modified_desc.Filter = D3D11_FILTER_MINIMUM_ANISOTROPIC;
                    modified = true;
                    break;
                case D3D11_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR:
                    modified_desc.Filter = D3D11_FILTER_MAXIMUM_ANISOTROPIC;
                    modified = true;
                    break;
                case D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT:
                    modified_desc.Filter = D3D11_FILTER_ANISOTROPIC;
                    modified = true;
                    break;
                case D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT:
                    modified_desc.Filter = D3D11_FILTER_COMPARISON_ANISOTROPIC;
                    modified = true;
                    break;
                default:
                    break;
            }
        }

        // Apply max anisotropy override for anisotropic filters
        if (settings::g_mainTabSettings.max_anisotropy.GetValue() > 0) {
            switch (modified_desc.Filter) {
                case D3D11_FILTER_ANISOTROPIC:
                case D3D11_FILTER_COMPARISON_ANISOTROPIC:
                case D3D11_FILTER_MINIMUM_ANISOTROPIC:
                case D3D11_FILTER_MAXIMUM_ANISOTROPIC:
                    modified_desc.MaxAnisotropy = static_cast<UINT>(settings::g_mainTabSettings.max_anisotropy.GetValue());
                    modified = true;
                    break;
                default:
                    break;
            }
        }

        // Call original function with modified descriptor if needed
        if (modified && ID3D11Device_CreateSamplerState_Original != nullptr) {
            return ID3D11Device_CreateSamplerState_Original(This, &modified_desc, ppSamplerState);
        }
    }

    // Call original function
    if (ID3D11Device_CreateSamplerState_Original != nullptr) {
        return ID3D11Device_CreateSamplerState_Original(This, pSamplerDesc, ppSamplerState);
    }

    // Fallback if original is not set
    return This->CreateSamplerState(pSamplerDesc, ppSamplerState);
}

// Hook a specific D3D11 device sampler creation using vtable hooking
bool HookD3D11DeviceSampler(ID3D11Device* device) {
    if (device == nullptr) {
        LogError("HookD3D11DeviceSampler: device is nullptr");
        return false;
    }

    if (g_d3d11_sampler_hooked.load()) {
        LogInfo("HookD3D11DeviceSampler: Sampler hooks already installed");
        return true;
    }

    // Check if D3D11 hooks should be suppressed
    if (HookSuppressionManager::GetInstance().ShouldSuppressHook(HookType::D3D_DEVICE)) {
        LogInfo("HookD3D11DeviceSampler: installation suppressed by user setting");
        return false;
    }

    // Initialize MinHook
    MH_STATUS init_status = SafeInitializeMinHook(HookType::D3D_DEVICE);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("HookD3D11DeviceSampler: Failed to initialize MinHook - Status: %d", init_status);
        return false;
    }

    // Get the vtable
    LPVOID* vtable = *reinterpret_cast<LPVOID**>(device);

    /*
     * ID3D11Device VTable Layout Reference
     * ======================================
     * CreateSamplerState is at index 23 (after IUnknown and other device methods)
     */

    LogInfo("HookD3D11DeviceSampler: Attempting to hook ID3D11Device::CreateSamplerState vtable at 0x%p", vtable);

    // Hook CreateSamplerState at index 23
    if (IsVTableEntryValid(vtable, 23)) {
        if (MH_CreateHook(vtable[23], ID3D11Device_CreateSamplerState_Detour,
                         (LPVOID*)&ID3D11Device_CreateSamplerState_Original) != MH_OK) {
            LogError("HookD3D11DeviceSampler: Failed to create ID3D11Device::CreateSamplerState hook");
            return false;
        }

        if (MH_EnableHook(vtable[23]) != MH_OK) {
            LogError("HookD3D11DeviceSampler: Failed to enable ID3D11Device::CreateSamplerState hook");
            return false;
        }

        LogInfo("HookD3D11DeviceSampler: ID3D11Device::CreateSamplerState hook created and enabled successfully");
        g_d3d11_sampler_hooked.store(true);
        return true;
    } else {
        LogWarn("HookD3D11DeviceSampler: vtable entry 23 is not valid");
    }

    return false;
}

} // namespace display_commanderhooks

