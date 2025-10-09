#include "ngx_hooks.hpp"
#include "../utils.hpp"
#include "../globals.hpp"
#include <MinHook.h>

// NGX type definitions (minimal subset needed for hooks)
#define NVSDK_CONV __cdecl

typedef struct NVSDK_NGX_Parameter NVSDK_NGX_Parameter;

typedef enum NVSDK_NGX_Result
{
    NVSDK_NGX_Result_Success = 0x1,
    NVSDK_NGX_Result_Fail = 0xBAD00000,
    NVSDK_NGX_Result_FAIL_FeatureNotSupported = NVSDK_NGX_Result_Fail | 1,
    NVSDK_NGX_Result_FAIL_PlatformError = NVSDK_NGX_Result_Fail | 2,
    NVSDK_NGX_Result_FAIL_FeatureAlreadyExists = NVSDK_NGX_Result_Fail | 3,
    NVSDK_NGX_Result_FAIL_FeatureNotFound = NVSDK_NGX_Result_Fail | 4,
    NVSDK_NGX_Result_FAIL_InvalidParameter = NVSDK_NGX_Result_Fail | 5,
    NVSDK_NGX_Result_FAIL_ScratchBufferTooSmall = NVSDK_NGX_Result_Fail | 6,
    NVSDK_NGX_Result_FAIL_NotInitialized = NVSDK_NGX_Result_Fail | 7,
    NVSDK_NGX_Result_FAIL_UnsupportedInputFormat = NVSDK_NGX_Result_Fail | 8,
    NVSDK_NGX_Result_FAIL_RWFlagMissing = NVSDK_NGX_Result_Fail | 9,
    NVSDK_NGX_Result_FAIL_MissingInput = NVSDK_NGX_Result_Fail | 10,
    NVSDK_NGX_Result_FAIL_UnableToInitializeFeature = NVSDK_NGX_Result_Fail | 11,
    NVSDK_NGX_Result_FAIL_OutOfDate = NVSDK_NGX_Result_Fail | 12,
    NVSDK_NGX_Result_FAIL_OutOfGPUMemory = NVSDK_NGX_Result_Fail | 13,
    NVSDK_NGX_Result_FAIL_UnsupportedFormat = NVSDK_NGX_Result_Fail | 14,
    NVSDK_NGX_Result_FAIL_UnableToWriteToAppDataPath = NVSDK_NGX_Result_Fail | 15,
    NVSDK_NGX_Result_FAIL_UnsupportedParameter = NVSDK_NGX_Result_Fail | 16,
    NVSDK_NGX_Result_FAIL_Denied = NVSDK_NGX_Result_Fail | 17,
    NVSDK_NGX_Result_FAIL_NotImplemented = NVSDK_NGX_Result_Fail | 18,
} NVSDK_NGX_Result;

#define NVSDK_NGX_SUCCEED(value) (((value) & 0xFFF00000) != NVSDK_NGX_Result_Fail)
#define NVSDK_NGX_FAILED(value) (((value) & 0xFFF00000) == NVSDK_NGX_Result_Fail)

// NGX function pointer type definitions (following Special-K's approach)
using NVSDK_NGX_Parameter_SetF_pfn = void (NVSDK_CONV *)(NVSDK_NGX_Parameter* InParameter, const char* InName, float InValue);
using NVSDK_NGX_Parameter_SetD_pfn = void (NVSDK_CONV *)(NVSDK_NGX_Parameter* InParameter, const char* InName, double InValue);
using NVSDK_NGX_Parameter_SetI_pfn = void (NVSDK_CONV *)(NVSDK_NGX_Parameter* InParameter, const char* InName, int InValue);
using NVSDK_NGX_Parameter_SetUI_pfn = void (NVSDK_CONV *)(NVSDK_NGX_Parameter* InParameter, const char* InName, unsigned int InValue);
using NVSDK_NGX_Parameter_SetULL_pfn = void (NVSDK_CONV *)(NVSDK_NGX_Parameter* InParameter, const char* InName, unsigned long long InValue);

// NGX parameter getter function pointer types
using NVSDK_NGX_Parameter_GetI_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter* InParameter, const char* InName, int* OutValue);
using NVSDK_NGX_Parameter_GetUI_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter* InParameter, const char* InName, unsigned int* OutValue);
using NVSDK_NGX_Parameter_GetULL_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter* InParameter, const char* InName, unsigned long long* OutValue);
using NVSDK_NGX_Parameter_GetVoidPointer_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter* InParameter, const char* InName, void** OutValue);

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
NVSDK_NGX_Parameter_SetULL_pfn NVSDK_NGX_Parameter_SetULL_Original = nullptr;

// NGX parameter getter original function pointers
NVSDK_NGX_Parameter_GetI_pfn NVSDK_NGX_Parameter_GetI_Original = nullptr;
NVSDK_NGX_Parameter_GetUI_pfn NVSDK_NGX_Parameter_GetUI_Original = nullptr;
NVSDK_NGX_Parameter_GetULL_pfn NVSDK_NGX_Parameter_GetULL_Original = nullptr;
NVSDK_NGX_Parameter_GetVoidPointer_pfn NVSDK_NGX_Parameter_GetVoidPointer_Original = nullptr;

// NGX initialization function originals
NVSDK_NGX_D3D12_GetParameters_pfn NVSDK_NGX_D3D12_GetParameters_Original = nullptr;
NVSDK_NGX_D3D12_AllocateParameters_pfn NVSDK_NGX_D3D12_AllocateParameters_Original = nullptr;
NVSDK_NGX_D3D11_GetParameters_pfn NVSDK_NGX_D3D11_GetParameters_Original = nullptr;
NVSDK_NGX_D3D11_AllocateParameters_pfn NVSDK_NGX_D3D11_AllocateParameters_Original = nullptr;

// Global flag to track if vtable hooks are installed
static bool g_ngx_vtable_hooks_installed = false;

// Hooked NVSDK_NGX_Parameter_SetF function
void NVSDK_CONV NVSDK_NGX_Parameter_SetF_Detour(NVSDK_NGX_Parameter* InParameter, const char* InName, float InValue) {
    // Increment counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_NGX_PARAMETER_SETF].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

        // Store parameter in thread-safe storage
        if (InName != nullptr) {
            g_ngx_parameters.update_float(std::string(InName), InValue);
        }

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

        // Store parameter in thread-safe storage
        if (InName != nullptr) {
            g_ngx_parameters.update_double(std::string(InName), InValue);
        }

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

        // Store parameter in thread-safe storage
        if (InName != nullptr) {
            g_ngx_parameters.update_int(std::string(InName), InValue);
        }

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

        // Store parameter in thread-safe storage
        if (InName != nullptr) {
            g_ngx_parameters.update_uint(std::string(InName), InValue);
        }

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

        // Store parameter in thread-safe storage
        if (InName != nullptr) {
            g_ngx_parameters.update_ull(std::string(InName), InValue);
        }

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

// Hooked NVSDK_NGX_Parameter_GetI function
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetI_Detour(NVSDK_NGX_Parameter* InParameter, const char* InName, int* OutValue) {
    // Increment counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_NGX_PARAMETER_GETI].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 60) {
        LogInfo("NGX Parameter GetI called - Name: %s", InName ? InName : "null");
        log_count++;
    }

    // Call original function
    if (NVSDK_NGX_Parameter_GetI_Original != nullptr) {
        auto res = NVSDK_NGX_Parameter_GetI_Original(InParameter, InName, OutValue);

        if (res == NVSDK_NGX_Result_Success && OutValue != nullptr) {
            g_ngx_parameters.update_int(std::string(InName), *OutValue);
        }

        return res;
    }

    return NVSDK_NGX_Result_Fail;
}

// Hooked NVSDK_NGX_Parameter_GetUI function
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetUI_Detour(NVSDK_NGX_Parameter* InParameter, const char* InName, unsigned int* OutValue) {
    // Increment counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_NGX_PARAMETER_GETUI].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 60) {
        LogInfo("NGX Parameter GetUI called - Name: %s", InName ? InName : "null");
        log_count++;
    }

    // Call original function
    if (NVSDK_NGX_Parameter_GetUI_Original != nullptr) {
        auto res = NVSDK_NGX_Parameter_GetUI_Original(InParameter, InName, OutValue);

        if (res == NVSDK_NGX_Result_Success && OutValue != nullptr) {
            g_ngx_parameters.update_uint(std::string(InName), *OutValue);
        }

        return res;
    }

    return NVSDK_NGX_Result_Fail;
}

// Hooked NVSDK_NGX_Parameter_GetULL function
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetULL_Detour(NVSDK_NGX_Parameter* InParameter, const char* InName, unsigned long long* OutValue) {
    // Increment counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_NGX_PARAMETER_GETULL].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 60) {
        LogInfo("NGX Parameter GetULL called - Name: %s", InName ? InName : "null");
        log_count++;
    }

    // Call original function
    if (NVSDK_NGX_Parameter_GetULL_Original != nullptr) {
        auto res = NVSDK_NGX_Parameter_GetULL_Original(InParameter, InName, OutValue);

        if (res == NVSDK_NGX_Result_Success && OutValue != nullptr) {
            g_ngx_parameters.update_ull(std::string(InName), *OutValue);
        }

        return res;
    }

    return NVSDK_NGX_Result_Fail;
}

// Hooked NVSDK_NGX_Parameter_GetVoidPointer function
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetVoidPointer_Detour(NVSDK_NGX_Parameter* InParameter, const char* InName, void** OutValue) {
    // Increment counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_NGX_PARAMETER_GETVOIDPOINTER].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 60) {
        LogInfo("NGX Parameter GetVoidPointer called - Name: %s", InName ? InName : "null");
        log_count++;
    }

    // Call original function
    if (NVSDK_NGX_Parameter_GetVoidPointer_Original != nullptr) {
        return NVSDK_NGX_Parameter_GetVoidPointer_Original(InParameter, InName, OutValue);
    }

    return NVSDK_NGX_Result_Fail;
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

    // Hook GetVoidPointer (vtable index 8) - Special-K uses index 8
    if (MH_CreateHook(vftable[8], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_GetVoidPointer_Detour),
                      reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_GetVoidPointer_Original)) != MH_OK) {
        LogInfo("NGX hooks: Failed to hook NVSDK_NGX_Parameter_GetVoidPointer vtable");
    }

    // Hook GetI (vtable index 11) - Special-K uses index 11
    if (MH_CreateHook(vftable[11], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_GetI_Detour),
                      reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_GetI_Original)) != MH_OK) {
        LogInfo("NGX hooks: Failed to hook NVSDK_NGX_Parameter_GetI vtable");
    }

    // Hook GetUI (vtable index 12) - Special-K uses index 12
    if (MH_CreateHook(vftable[12], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_GetUI_Detour),
                      reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_GetUI_Original)) != MH_OK) {
        LogInfo("NGX hooks: Failed to hook NVSDK_NGX_Parameter_GetUI vtable");
    }

    // Hook GetULL (vtable index 15) - Special-K uses index 15
    if (MH_CreateHook(vftable[15], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_GetULL_Detour),
                      reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_GetULL_Original)) != MH_OK) {
        LogInfo("NGX hooks: Failed to hook NVSDK_NGX_Parameter_GetULL vtable");
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

    if (NVSDK_NGX_Parameter_GetI_Original != nullptr) {
        MH_RemoveHook(GetProcAddress(GetModuleHandleA("_nvngx.dll"), "NVSDK_NGX_Parameter_GetI"));
        NVSDK_NGX_Parameter_GetI_Original = nullptr;
    }

    if (NVSDK_NGX_Parameter_GetUI_Original != nullptr) {
        MH_RemoveHook(GetProcAddress(GetModuleHandleA("_nvngx.dll"), "NVSDK_NGX_Parameter_GetUI"));
        NVSDK_NGX_Parameter_GetUI_Original = nullptr;
    }

    if (NVSDK_NGX_Parameter_GetULL_Original != nullptr) {
        MH_RemoveHook(GetProcAddress(GetModuleHandleA("_nvngx.dll"), "NVSDK_NGX_Parameter_GetULL"));
        NVSDK_NGX_Parameter_GetULL_Original = nullptr;
    }

    if (NVSDK_NGX_Parameter_GetVoidPointer_Original != nullptr) {
        MH_RemoveHook(GetProcAddress(GetModuleHandleA("_nvngx.dll"), "NVSDK_NGX_Parameter_GetVoidPointer"));
        NVSDK_NGX_Parameter_GetVoidPointer_Original = nullptr;
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
