#include "nvapi_hooks.hpp"
#include "../utils.hpp"
#include "../globals.hpp"
#include <MinHook.h>
#include "../../../external/nvapi/nvapi_interface.h"

// Original function pointers
NvAPI_Disp_GetHdrCapabilities_pfn NvAPI_Disp_GetHdrCapabilities_Original = nullptr;

// Function to look up NVAPI function ID from interface table
namespace {
    NvU32 GetNvAPIFunctionId(const char* functionName) {
        for (int i = 0; nvapi_interface_table[i].func != nullptr; i++) {
            if (strcmp(nvapi_interface_table[i].func, functionName) == 0) {
                return nvapi_interface_table[i].id;
            }
        }

        LogInfo("NVAPI hooks: Function '%s' not found in interface table", functionName);
        return 0;
    }
}

// Hooked NvAPI_Disp_GetHdrCapabilities function
NvAPI_Status __cdecl NvAPI_Disp_GetHdrCapabilities_Detour(NvU32 displayId, NV_HDR_CAPABILITIES *pHdrCapabilities) {
    // Increment counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_NVAPI_GET_HDR_CAPABILITIES].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 3) {
        LogInfo("NVAPI HDR Capabilities called - DisplayId: %u s_hide_hdr_capabilities: %d", displayId, s_hide_hdr_capabilities.load());
        log_count++;
    }

    // Check if HDR hiding is enabled
    extern std::atomic<bool> s_hide_hdr_capabilities;
    if (s_hide_hdr_capabilities.load()) {
        // Hide HDR capabilities by returning a modified structure
        if (pHdrCapabilities != nullptr) {
            // Call original function first to get the real capabilities
            NvAPI_Status result = NVAPI_OK;
            if (NvAPI_Disp_GetHdrCapabilities_Original != nullptr) {
                result = NvAPI_Disp_GetHdrCapabilities_Original(displayId, pHdrCapabilities);
            } else {
                result = NVAPI_NO_IMPLEMENTATION;
            }

            // If we got valid data, modify it to hide HDR capabilities
            if (result == NVAPI_OK) {
                // Set all HDR-related flags to false
                pHdrCapabilities->isST2084EotfSupported = 0;
                pHdrCapabilities->isTraditionalHdrGammaSupported = 0;
                pHdrCapabilities->isTraditionalSdrGammaSupported = 1; // Keep SDR support
                pHdrCapabilities->isHdr10PlusSupported = 0;
                pHdrCapabilities->isHdr10PlusGamingSupported = 0;
                pHdrCapabilities->isDolbyVisionSupported = 0;

                // Set driver to not expand HDR parameters
                pHdrCapabilities->driverExpandDefaultHdrParameters = 0;

                static int hdr_hidden_count = 0;
                if (hdr_hidden_count < 3) {
                    LogInfo("NVAPI HDR hiding: Modified HDR capabilities for DisplayId: %u", displayId);
                    hdr_hidden_count++;
                }
            }

            return result;
        } else {
            // If pHdrCapabilities is null, just return error
            return NVAPI_NO_IMPLEMENTATION;
        }
    }

    // HDR hiding disabled - call original function normally
    if (NvAPI_Disp_GetHdrCapabilities_Original != nullptr) {
        return NvAPI_Disp_GetHdrCapabilities_Original(displayId, pHdrCapabilities);
    }

    // Fallback - return error if original function not available
    return NVAPI_NO_IMPLEMENTATION;
}

// Install NVAPI hooks
bool InstallNVAPIHooks() {
    // Follow Special-K's approach: get NvAPI_QueryInterface first, then use it to get other functions
    HMODULE nvapi_dll = GetModuleHandleA("nvapi64.dll");
    if (!nvapi_dll) {
        LogInfo("NVAPI hooks: nvapi64.dll not loaded");
        return false;
    }

    // Get NvAPI_QueryInterface function (this is the key function that Special-K uses)
    NvAPI_QueryInterface_pfn queryInterface =
        reinterpret_cast<NvAPI_QueryInterface_pfn>(
            GetProcAddress(nvapi_dll, "nvapi_QueryInterface"));

    if (!queryInterface) {
        LogInfo("NVAPI hooks: Failed to get nvapi_QueryInterface address");
        return false;
    }

    LogInfo("NVAPI hooks: Found nvapi_QueryInterface, getting NvAPI_Disp_GetHdrCapabilities");

    // Get function ID from interface table
    NvU32 functionId = GetNvAPIFunctionId("NvAPI_Disp_GetHdrCapabilities");
    if (functionId == 0) {
        LogInfo("NVAPI hooks: Failed to get NvAPI_Disp_GetHdrCapabilities function ID");
        return false;
    }

    // Use QueryInterface to get the actual function address (same as Special-K)
    NvAPI_Disp_GetHdrCapabilities_pfn original_func =
        reinterpret_cast<NvAPI_Disp_GetHdrCapabilities_pfn>(
            queryInterface(functionId));

    if (!original_func) {
        LogInfo("NVAPI hooks: Failed to get NvAPI_Disp_GetHdrCapabilities via QueryInterface");
        return false;
    }

    LogInfo("NVAPI hooks: Successfully got NvAPI_Disp_GetHdrCapabilities address via QueryInterface");

    // Create hook
    if (MH_CreateHook(original_func, NvAPI_Disp_GetHdrCapabilities_Detour,
                      reinterpret_cast<LPVOID*>(&NvAPI_Disp_GetHdrCapabilities_Original)) != MH_OK) {
        LogInfo("NVAPI hooks: Failed to create NvAPI_Disp_GetHdrCapabilities hook");
        return false;
    }

    // Enable hook
    if (MH_EnableHook(original_func) != MH_OK) {
        LogInfo("NVAPI hooks: Failed to enable NvAPI_Disp_GetHdrCapabilities hook");
        return false;
    }

    LogInfo("NVAPI hooks: Successfully installed NvAPI_Disp_GetHdrCapabilities hook");
    return true;
}

// Uninstall NVAPI hooks
void UninstallNVAPIHooks() {
    if (NvAPI_Disp_GetHdrCapabilities_Original) {
        MH_DisableHook(NvAPI_Disp_GetHdrCapabilities_Original);
        MH_RemoveHook(NvAPI_Disp_GetHdrCapabilities_Original);
        NvAPI_Disp_GetHdrCapabilities_Original = nullptr;
    }
}
