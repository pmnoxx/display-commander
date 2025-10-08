#include "ngx_hooks.hpp"
#include "../utils.hpp"
#include "../globals.hpp"
#include <MinHook.h>
#include "../../../../external-src/SpecialK/include/SpecialK/render/ngx/ngx_defs.h"

// NGX function pointer type definitions (following Special-K's approach)
using NVSDK_NGX_Parameter_SetF_pfn = void (NVSDK_CONV *)(NVSDK_NGX_Parameter* InParameter, const char* InName, float InValue);
using NVSDK_NGX_Parameter_SetD_pfn = void (NVSDK_CONV *)(NVSDK_NGX_Parameter* InParameter, const char* InName, double InValue);
using NVSDK_NGX_Parameter_SetI_pfn = void (NVSDK_CONV *)(NVSDK_NGX_Parameter* InParameter, const char* InName, int InValue);
using NVSDK_NGX_Parameter_SetUI_pfn = void (NVSDK_CONV *)(NVSDK_NGX_Parameter* InParameter, const char* InName, unsigned int InValue);
using NVSDK_NGX_Parameter_SetULL_pfn = void (NVSDK_CONV *)(NVSDK_NGX_Parameter* InParameter, const char* InName, unsigned long long InValue);

// NGX initialization function pointer types
using NVSDK_NGX_D3D12_GetParameters_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter** OutParameters);
using NVSDK_NGX_D3D12_AllocateParameters_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter** OutParameters);
using NVSDK_NGX_D3D11_GetParameters_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter** OutParameters);
using NVSDK_NGX_D3D11_AllocateParameters_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter** OutParameters);

// Original function pointers
NVSDK_NGX_Parameter_SetF_pfn NVSDK_NGX_Parameter_SetF_Original = nullptr;
NVSDK_NGX_Parameter_SetD_pfn NVSDK_NGX_Parameter_SetD_Original = nullptr;
NVSDK_NGX_Parameter_SetI_pfn NVSDK_NGX_Parameter_SetI_Original = nullptr;
NVSDK_NGX_Parameter_SetUI_pfn NVSDK_NGX_Parameter_SetUI_Original = nullptr;

// NGX initialization function originals
NVSDK_NGX_D3D12_GetParameters_pfn NVSDK_NGX_D3D12_GetParameters_Original = nullptr;
NVSDK_NGX_D3D12_AllocateParameters_pfn NVSDK_NGX_D3D12_AllocateParameters_Original = nullptr;
NVSDK_NGX_D3D11_GetParameters_pfn NVSDK_NGX_D3D11_GetParameters_Original = nullptr;
NVSDK_NGX_D3D11_AllocateParameters_pfn NVSDK_NGX_D3D11_AllocateParameters_Original = nullptr;

// Global flag to track if vtable hooks are installed
static bool g_ngx_vtable_hooks_installed = false;
NVSDK_NGX_Parameter_SetULL_pfn NVSDK_NGX_Parameter_SetULL_Original = nullptr;

// Hooked NVSDK_NGX_Parameter_SetF function
void NVSDK_CONV NVSDK_NGX_Parameter_SetF_Detour(NVSDK_NGX_Parameter* InParameter, const char* InName, float InValue) {
    // Increment counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_NGX_PARAMETER_SETF].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 60) {
        LogInfo("NGX Parameter SetF called - Name: %s, Value: %f", InName ? InName : "null", InValue);
        log_count++;
    }

    // Call original function
    if (NVSDK_NGX_Parameter_SetF_Original != nullptr) {
        NVSDK_NGX_Parameter_SetF_Original(InParameter, InName, InValue);
    }
}

// Hooked NVSDK_NGX_Parameter_SetD function
void NVSDK_CONV NVSDK_NGX_Parameter_SetD_Detour(NVSDK_NGX_Parameter* InParameter, const char* InName, double InValue) {
    // Increment counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_NGX_PARAMETER_SETD].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 60) {
        LogInfo("NGX Parameter SetD called - Name: %s, Value: %f", InName ? InName : "null", InValue);
        log_count++;
    }

    // Call original function
    if (NVSDK_NGX_Parameter_SetD_Original != nullptr) {
        NVSDK_NGX_Parameter_SetD_Original(InParameter, InName, InValue);
    }
}

// Hooked NVSDK_NGX_Parameter_SetI function
void NVSDK_CONV NVSDK_NGX_Parameter_SetI_Detour(NVSDK_NGX_Parameter* InParameter, const char* InName, int InValue) {
    // Increment counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_NGX_PARAMETER_SETI].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 60) {
        LogInfo("NGX Parameter SetI called - Name: %s, Value: %d", InName ? InName : "null", InValue);
        log_count++;
    }

    // Call original function
    if (NVSDK_NGX_Parameter_SetI_Original != nullptr) {
        NVSDK_NGX_Parameter_SetI_Original(InParameter, InName, InValue);
    }
}

// Hooked NVSDK_NGX_Parameter_SetUI function
void NVSDK_CONV NVSDK_NGX_Parameter_SetUI_Detour(NVSDK_NGX_Parameter* InParameter, const char* InName, unsigned int InValue) {
    // Increment counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_NGX_PARAMETER_SETUI].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 60) {
        LogInfo("NGX Parameter SetUI called - Name: %s, Value: %u", InName ? InName : "null", InValue);
        log_count++;
    }

    // Call original function
    if (NVSDK_NGX_Parameter_SetUI_Original != nullptr) {
        NVSDK_NGX_Parameter_SetUI_Original(InParameter, InName, InValue);
    }
}

// Hooked NVSDK_NGX_Parameter_SetULL function
void NVSDK_CONV NVSDK_NGX_Parameter_SetULL_Detour(NVSDK_NGX_Parameter* InParameter, const char* InName, unsigned long long InValue) {
    // Increment counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_NGX_PARAMETER_SETULL].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 60) {
        LogInfo("NGX Parameter SetULL called - Name: %s, Value: %llu", InName ? InName : "null", InValue);
        log_count++;
    }

    // Call original function
    if (NVSDK_NGX_Parameter_SetULL_Original != nullptr) {
        NVSDK_NGX_Parameter_SetULL_Original(InParameter, InName, InValue);
    }
}

// Function to hook NGX Parameter vtable (following Special-K's approach)
bool HookNGXParameterVTable(NVSDK_NGX_Parameter* Params) {
    if (Params == nullptr) {
        return false;
    }

    if (g_ngx_vtable_hooks_installed) {
        return true;
    }

    // Extract vtable from parameter object
    void** vftable = *(void***)*&Params;

    // VTable layout (following Special-K's comments):
    // [ 0] void* SetVoidPointer;
    // [ 1] void* SetD3d12Resource;
    // [ 2] void *SetD3d11Resource;
    // [ 3] void* SetI;
    // [ 4] void* SetUI;
    // [ 5] void* SetD;
    // [ 6] void* SetF;
    // [ 7] void* SetULL;
    // [ 8] void* GetVoidPointer;
    // [ 9] void* GetD3d12Resource;
    // [10] void* GetD3d11Resource;
    // [11] void *GetI;
    // [12] void *GetUI;
    // [13] void* GetD;
    // [14] void* GetF;
    // [15] void* GetULL;
    // [16] void* Reset;

    LogInfo("Installing NGX Parameter vtable hooks...");

    // Hook SetI (vtable index 3)
    if (MH_CreateHook(vftable[3], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_SetI_Detour),
                      reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_SetI_Original)) != MH_OK) {
        LogInfo("NGX hooks: Failed to hook NVSDK_NGX_Parameter_SetI vtable");
    }

    // Hook SetUI (vtable index 4)
    if (MH_CreateHook(vftable[4], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_SetUI_Detour),
                      reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_SetUI_Original)) != MH_OK) {
        LogInfo("NGX hooks: Failed to hook NVSDK_NGX_Parameter_SetUI vtable");
    }

    // Hook SetD (vtable index 5)
    if (MH_CreateHook(vftable[5], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_SetD_Detour),
                      reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_SetD_Original)) != MH_OK) {
        LogInfo("NGX hooks: Failed to hook NVSDK_NGX_Parameter_SetD vtable");
    }

    // Hook SetF (vtable index 6)
    if (MH_CreateHook(vftable[6], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_SetF_Detour),
                      reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_SetF_Original)) != MH_OK) {
        LogInfo("NGX hooks: Failed to hook NVSDK_NGX_Parameter_SetF vtable");
    }

    // Hook SetULL (vtable index 7)
    if (MH_CreateHook(vftable[7], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_SetULL_Detour),
                      reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_SetULL_Original)) != MH_OK) {
        LogInfo("NGX hooks: Failed to hook NVSDK_NGX_Parameter_SetULL vtable");
    }

    // Enable hooks
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        LogError("Failed to enable NGX vtable hooks");
        return false;
    }

    g_ngx_vtable_hooks_installed = true;
    LogInfo("NGX Parameter vtable hooks installed successfully");
    return true;
}

// NGX D3D12 GetParameters detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_GetParameters_Detour(NVSDK_NGX_Parameter** InParameters) {
    NVSDK_NGX_Result ret = NVSDK_NGX_Result_Fail;

    if (NVSDK_NGX_D3D12_GetParameters_Original != nullptr) {
        ret = NVSDK_NGX_D3D12_GetParameters_Original(InParameters);
    }

    if (ret == NVSDK_NGX_Result_Success && InParameters != nullptr && *InParameters != nullptr) {
        HookNGXParameterVTable(*InParameters);
    }

    return ret;
}

// NGX D3D12 AllocateParameters detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_AllocateParameters_Detour(NVSDK_NGX_Parameter** InParameters) {
    NVSDK_NGX_Result ret = NVSDK_NGX_Result_Fail;

    if (NVSDK_NGX_D3D12_AllocateParameters_Original != nullptr) {
        ret = NVSDK_NGX_D3D12_AllocateParameters_Original(InParameters);
    }

    if (ret == NVSDK_NGX_Result_Success && InParameters != nullptr && *InParameters != nullptr) {
        HookNGXParameterVTable(*InParameters);
    }

    return ret;
}

// NGX D3D11 GetParameters detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D11_GetParameters_Detour(NVSDK_NGX_Parameter** InParameters) {
    NVSDK_NGX_Result ret = NVSDK_NGX_Result_Fail;

    if (NVSDK_NGX_D3D11_GetParameters_Original != nullptr) {
        ret = NVSDK_NGX_D3D11_GetParameters_Original(InParameters);
    }

    if (ret == NVSDK_NGX_Result_Success && InParameters != nullptr && *InParameters != nullptr) {
        HookNGXParameterVTable(*InParameters);
    }

    return ret;
}

// NGX D3D11 AllocateParameters detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D11_AllocateParameters_Detour(NVSDK_NGX_Parameter** InParameters) {
    NVSDK_NGX_Result ret = NVSDK_NGX_Result_Fail;

    if (NVSDK_NGX_D3D11_AllocateParameters_Original != nullptr) {
        ret = NVSDK_NGX_D3D11_AllocateParameters_Original(InParameters);
    }

    if (ret == NVSDK_NGX_Result_Success && InParameters != nullptr && *InParameters != nullptr) {
        HookNGXParameterVTable(*InParameters);
    }

    return ret;
}

// Install NGX hooks
bool InstallNGXHooks() {
    // Check if NGX DLLs are loaded
    HMODULE ngx_dll = GetModuleHandleA("_nvngx.dll");
    if (!ngx_dll) {
        LogInfo("NGX hooks: _nvngx.dll not loaded");
        return false;
    }

    static bool g_ngx_hooks_installed = false;
    if (g_ngx_hooks_installed) {
        LogInfo("NGX hooks already installed");
        return true;
    }
    g_ngx_hooks_installed = true;

    LogInfo("Installing NGX initialization hooks...");

    // Hook NGX initialization functions to get Parameter objects
    // These functions are exported from _nvngx.dll and return Parameter objects
    // We'll hook their vtables when they're called

    // Hook D3D12 GetParameters
    if (MH_CreateHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D12_GetParameters"),
                      reinterpret_cast<LPVOID>(NVSDK_NGX_D3D12_GetParameters_Detour),
                      reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D12_GetParameters_Original)) != MH_OK) {
        LogInfo("NGX hooks: Failed to hook NVSDK_NGX_D3D12_GetParameters");
    }

    // Hook D3D12 AllocateParameters
    if (MH_CreateHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D12_AllocateParameters"),
                      reinterpret_cast<LPVOID>(NVSDK_NGX_D3D12_AllocateParameters_Detour),
                      reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D12_AllocateParameters_Original)) != MH_OK) {
        LogInfo("NGX hooks: Failed to hook NVSDK_NGX_D3D12_AllocateParameters");
    }

    // Hook D3D11 GetParameters
    if (MH_CreateHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D11_GetParameters"),
                      reinterpret_cast<LPVOID>(NVSDK_NGX_D3D11_GetParameters_Detour),
                      reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D11_GetParameters_Original)) != MH_OK) {
        LogInfo("NGX hooks: Failed to hook NVSDK_NGX_D3D11_GetParameters");
    }

    // Hook D3D11 AllocateParameters
    if (MH_CreateHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D11_AllocateParameters"),
                      reinterpret_cast<LPVOID>(NVSDK_NGX_D3D11_AllocateParameters_Detour),
                      reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D11_AllocateParameters_Original)) != MH_OK) {
        LogInfo("NGX hooks: Failed to hook NVSDK_NGX_D3D11_AllocateParameters");
    }

    // Enable hooks
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        LogError("Failed to enable NGX initialization hooks");
        return false;
    }

    LogInfo("NGX initialization hooks installed successfully");
    LogInfo("NGX Parameter vtable hooks will be installed when Parameter objects are created");
    return true;
}

// Uninstall NGX hooks
void UninstallNGXHooks() {
    // Disable hooks
    MH_DisableHook(MH_ALL_HOOKS);

    // Remove hooks
    if (NVSDK_NGX_Parameter_SetF_Original != nullptr) {
        MH_RemoveHook(GetProcAddress(GetModuleHandleA("_nvngx.dll"), "NVSDK_NGX_Parameter_SetF"));
        NVSDK_NGX_Parameter_SetF_Original = nullptr;
    }

    if (NVSDK_NGX_Parameter_SetD_Original != nullptr) {
        MH_RemoveHook(GetProcAddress(GetModuleHandleA("_nvngx.dll"), "NVSDK_NGX_Parameter_SetD"));
        NVSDK_NGX_Parameter_SetD_Original = nullptr;
    }

    if (NVSDK_NGX_Parameter_SetI_Original != nullptr) {
        MH_RemoveHook(GetProcAddress(GetModuleHandleA("_nvngx.dll"), "NVSDK_NGX_Parameter_SetI"));
        NVSDK_NGX_Parameter_SetI_Original = nullptr;
    }

    if (NVSDK_NGX_Parameter_SetUI_Original != nullptr) {
        MH_RemoveHook(GetProcAddress(GetModuleHandleA("_nvngx.dll"), "NVSDK_NGX_Parameter_SetUI"));
        NVSDK_NGX_Parameter_SetUI_Original = nullptr;
    }

    if (NVSDK_NGX_Parameter_SetULL_Original != nullptr) {
        MH_RemoveHook(GetProcAddress(GetModuleHandleA("_nvngx.dll"), "NVSDK_NGX_Parameter_SetULL"));
        NVSDK_NGX_Parameter_SetULL_Original = nullptr;
    }

    LogInfo("NGX hooks uninstalled");
}

// Get NGX hook statistics
uint64_t GetNGXHookCount(int event_type) {
    if (event_type >= SWAPCHAIN_EVENT_NGX_PARAMETER_SETF &&
        event_type <= SWAPCHAIN_EVENT_NGX_PARAMETER_SETULL) {
        return g_swapchain_event_counters[event_type].load();
    }
    return 0;
}

uint64_t GetTotalNGXHookCount() {
    uint64_t total = 0;
    for (int i = SWAPCHAIN_EVENT_NGX_PARAMETER_SETF; i <= SWAPCHAIN_EVENT_NGX_PARAMETER_SETULL; i++) {
        total += g_swapchain_event_counters[i].load();
    }
    return total;
}
