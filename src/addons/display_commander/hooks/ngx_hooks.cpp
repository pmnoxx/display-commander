#include "ngx_hooks.hpp"
#include "../utils.hpp"
#include "../globals.hpp"
#include "../settings/swapchain_tab_settings.hpp"
#include <MinHook.h>
#include <algorithm>
#include <string>
#include <vector>

// NGX type definitions (minimal subset needed for hooks)
#define NVSDK_CONV __cdecl

// Forward declarations for DirectX types
struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D11Device;
struct ID3D11DeviceContext;

// NGX types
typedef struct NVSDK_NGX_Parameter NVSDK_NGX_Parameter;
typedef struct NVSDK_NGX_Handle NVSDK_NGX_Handle;
typedef struct NVSDK_NGX_FeatureCommonInfo NVSDK_NGX_FeatureCommonInfo;

// NGX enums
typedef enum NVSDK_NGX_Feature {
    NVSDK_NGX_Feature_SuperSampling = 0,
    NVSDK_NGX_Feature_FrameGeneration = 1,
    NVSDK_NGX_Feature_RayReconstruction = 2
} NVSDK_NGX_Feature;

typedef enum NVSDK_NGX_EngineType {
    NVSDK_NGX_ENGINETYPE_UNREAL = 0,
    NVSDK_NGX_ENGINETYPE_UNITY = 1,
    NVSDK_NGX_ENGINETYPE_OTHER = 2
} NVSDK_NGX_EngineType;

typedef enum NVSDK_NGX_Version {
    NVSDK_NGX_Version_API = 0x00000001
} NVSDK_NGX_Version;

// Progress callback type
typedef void (NVSDK_CONV *PFN_NVSDK_NGX_ProgressCallback)(float InCurrentProgress, bool &OutShouldCancel);

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
using NVSDK_NGX_D3D12_Init_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(unsigned long long InApplicationId, const wchar_t *InApplicationDataPath, ID3D12Device *InDevice, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo, NVSDK_NGX_Version InSDKVersion);
using NVSDK_NGX_D3D12_Init_Ext_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(unsigned long long InApplicationId, const wchar_t *InApplicationDataPath, ID3D12Device *InDevice, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo, void* Unknown5);
using NVSDK_NGX_D3D12_Init_ProjectID_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(const char *InProjectId, NVSDK_NGX_EngineType InEngineType, const char *InEngineVersion, const wchar_t *InApplicationDataPath, ID3D12Device *InDevice, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo, NVSDK_NGX_Version InSDKVersion);
using NVSDK_NGX_D3D12_GetParameters_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter** OutParameters);
using NVSDK_NGX_D3D12_AllocateParameters_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter** OutParameters);
using NVSDK_NGX_D3D12_CreateFeature_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(ID3D12GraphicsCommandList *InCmdList, NVSDK_NGX_Feature InFeatureID, NVSDK_NGX_Parameter *InParameters, NVSDK_NGX_Handle **OutHandle);
using NVSDK_NGX_D3D12_ReleaseFeature_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Handle *InHandle);
using NVSDK_NGX_D3D12_EvaluateFeature_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(ID3D12GraphicsCommandList *InCmdList, const NVSDK_NGX_Handle *InFeatureHandle, const NVSDK_NGX_Parameter *InParameters, PFN_NVSDK_NGX_ProgressCallback InCallback);

using NVSDK_NGX_D3D11_Init_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(unsigned long long InApplicationId, const wchar_t *InApplicationDataPath, ID3D11Device *InDevice, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo, NVSDK_NGX_Version InSDKVersion);
using NVSDK_NGX_D3D11_Init_Ext_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(unsigned long long InApplicationId, const wchar_t *InApplicationDataPath, ID3D11Device *InDevice, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo, void* Unknown5);
using NVSDK_NGX_D3D11_Init_ProjectID_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(const char *InProjectId, NVSDK_NGX_EngineType InEngineType, const char *InEngineVersion, const wchar_t *InApplicationDataPath, ID3D11Device *InDevice, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo, NVSDK_NGX_Version InSDKVersion);
using NVSDK_NGX_D3D11_GetParameters_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter** OutParameters);
using NVSDK_NGX_D3D11_AllocateParameters_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter** OutParameters);
using NVSDK_NGX_D3D11_CreateFeature_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(ID3D11DeviceContext *InDevCtx, NVSDK_NGX_Feature InFeatureID, NVSDK_NGX_Parameter *InParameters, NVSDK_NGX_Handle **OutHandle);
using NVSDK_NGX_D3D11_ReleaseFeature_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Handle *InHandle);
using NVSDK_NGX_D3D11_EvaluateFeature_pfn = NVSDK_NGX_Result (NVSDK_CONV *)(ID3D11DeviceContext *InDevCtx, const NVSDK_NGX_Handle *InFeatureHandle, const NVSDK_NGX_Parameter *InParameters, PFN_NVSDK_NGX_ProgressCallback InCallback);

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
NVSDK_NGX_D3D12_Init_pfn NVSDK_NGX_D3D12_Init_Original = nullptr;
NVSDK_NGX_D3D12_Init_Ext_pfn NVSDK_NGX_D3D12_Init_Ext_Original = nullptr;
NVSDK_NGX_D3D12_Init_ProjectID_pfn NVSDK_NGX_D3D12_Init_ProjectID_Original = nullptr;
NVSDK_NGX_D3D12_GetParameters_pfn NVSDK_NGX_D3D12_GetParameters_Original = nullptr;
NVSDK_NGX_D3D12_AllocateParameters_pfn NVSDK_NGX_D3D12_AllocateParameters_Original = nullptr;
NVSDK_NGX_D3D12_CreateFeature_pfn NVSDK_NGX_D3D12_CreateFeature_Original = nullptr;
NVSDK_NGX_D3D12_ReleaseFeature_pfn NVSDK_NGX_D3D12_ReleaseFeature_Original = nullptr;
NVSDK_NGX_D3D12_EvaluateFeature_pfn NVSDK_NGX_D3D12_EvaluateFeature_Original = nullptr;

NVSDK_NGX_D3D11_Init_pfn NVSDK_NGX_D3D11_Init_Original = nullptr;
NVSDK_NGX_D3D11_Init_Ext_pfn NVSDK_NGX_D3D11_Init_Ext_Original = nullptr;
NVSDK_NGX_D3D11_Init_ProjectID_pfn NVSDK_NGX_D3D11_Init_ProjectID_Original = nullptr;
NVSDK_NGX_D3D11_GetParameters_pfn NVSDK_NGX_D3D11_GetParameters_Original = nullptr;
NVSDK_NGX_D3D11_AllocateParameters_pfn NVSDK_NGX_D3D11_AllocateParameters_Original = nullptr;
NVSDK_NGX_D3D11_CreateFeature_pfn NVSDK_NGX_D3D11_CreateFeature_Original = nullptr;
NVSDK_NGX_D3D11_ReleaseFeature_pfn NVSDK_NGX_D3D11_ReleaseFeature_Original = nullptr;
NVSDK_NGX_D3D11_EvaluateFeature_pfn NVSDK_NGX_D3D11_EvaluateFeature_Original = nullptr;

// Global flag to track if vtable hooks are installed
static bool g_ngx_vtable_hooks_installed = false;

// DLSS preset parameter names arrays
static const std::vector<std::string> g_dlss_sr_preset_params = {
    "DLSS.Hint.Render.Preset.Quality",
    "DLSS.Hint.Render.Preset.Balanced",
    "DLSS.Hint.Render.Preset.Performance",
    "DLSS.Hint.Render.Preset.UltraPerformance",
    "DLSS.Hint.Render.Preset.UltraQuality",
    "DLSS.Hint.Render.Preset.DLAA"
};

static const std::vector<std::string> g_dlss_rr_preset_params = {
    "RayReconstruction.Hint.Render.Preset.Quality",
    "RayReconstruction.Hint.Render.Preset.Balanced",
    "RayReconstruction.Hint.Render.Preset.Performance",
    "RayReconstruction.Hint.Render.Preset.UltraPerformance",
    "RayReconstruction.Hint.Render.Preset.UltraQuality",
    "RayReconstruction.Hint.Render.Preset.DLAA"
};

// Helper function to check if a parameter name is in the DLSS preset array
static bool IsDLSSPresetParameter(const std::string& param_name, const std::vector<std::string>& preset_params) {
    return std::find(preset_params.begin(), preset_params.end(), param_name) != preset_params.end();
}

// Function to automatically set DLSS preset parameters during initialization
static void ApplyDLSSPresetParameters(NVSDK_NGX_Parameter* InParameters) {
    if (InParameters == nullptr) {
        return;
    }

    // Check if presets have already been applied
    if (g_ngx_presets_initialized.load()) {
        return;
    }

    // Check if preset override is enabled
    if (!settings::g_swapchainTabSettings.dlss_preset_override_enabled.GetValue()) {
        return;
    }

    LogInfo("Applying DLSS preset parameters during NGX initialization...");

    // Get preset values
    int sr_preset = settings::g_swapchainTabSettings.dlss_sr_preset_override.GetValue();
    int rr_preset = settings::g_swapchainTabSettings.dlss_rr_preset_override.GetValue();

    // Apply DLSS Super Resolution preset parameters
    if (sr_preset > 0) { // 0 = Game Default, 1+ = Preset A+
        for (const auto& param_name : g_dlss_sr_preset_params) {
            if (NVSDK_NGX_Parameter_SetI_Original != nullptr) {
                NVSDK_NGX_Parameter_SetI_Original(InParameters, param_name.c_str(), sr_preset);
                g_ngx_parameters.update_int(param_name, sr_preset);
                LogInfo("Applied DLSS SR preset: %s -> %d (Preset %c)",
                       param_name.c_str(), sr_preset, 'A' + sr_preset - 1);
            }
        }
    }

    // Apply DLSS Ray Reconstruction preset parameters
    if (rr_preset > 0) { // 0 = Game Default, 1+ = Preset A+
        for (const auto& param_name : g_dlss_rr_preset_params) {
            if (NVSDK_NGX_Parameter_SetI_Original != nullptr) {
                NVSDK_NGX_Parameter_SetI_Original(InParameters, param_name.c_str(), rr_preset);
                g_ngx_parameters.update_int(param_name, rr_preset);
                LogInfo("Applied DLSS RR preset: %s -> %d (Preset %c)",
                       param_name.c_str(), rr_preset, 'A' + rr_preset - 1);
            }
        }
    }

    // Mark as initialized
    g_ngx_presets_initialized.store(true);
    LogInfo("DLSS preset parameters applied successfully");
}

// Function to reset NGX preset initialization flag
void ResetNGXPresetInitialization() {
    g_ngx_presets_initialized.store(false);
    LogInfo("NGX preset initialization flag reset - presets will be reapplied on next initialization");
}

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

    // DLSS preset override logic
    if (InName != nullptr && settings::g_swapchainTabSettings.dlss_preset_override_enabled.GetValue()) {
        std::string param_name = std::string(InName);

        // Check for DLSS Super Resolution preset parameters
        if (IsDLSSPresetParameter(param_name, g_dlss_sr_preset_params)) {
            int sr_preset = settings::g_swapchainTabSettings.dlss_sr_preset_override.GetValue();
            if (sr_preset > 0) { // 0 = Game Default, 1+ = Preset A+
                InValue = sr_preset;
                LogInfo("DLSS SR preset override: %s -> %d (Preset %c)", param_name.c_str(), InValue, 'A' + sr_preset - 1);
            }
        }

        // Check for DLSS Ray Reconstruction preset parameters
        if (IsDLSSPresetParameter(param_name, g_dlss_rr_preset_params)) {
            int rr_preset = settings::g_swapchainTabSettings.dlss_rr_preset_override.GetValue();
            if (rr_preset > 0) { // 0 = Game Default, 1+ = Preset A+
                InValue = rr_preset;
                LogInfo("DLSS RR preset override: %s -> %d (Preset %c)", param_name.c_str(), InValue, 'A' + rr_preset - 1);
            }
        }
    }

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

    // DLSS preset override logic
    if (InName != nullptr && settings::g_swapchainTabSettings.dlss_preset_override_enabled.GetValue()) {
        std::string param_name = std::string(InName);

        // Check for DLSS Super Resolution preset parameters
        if (IsDLSSPresetParameter(param_name, g_dlss_sr_preset_params)) {
            int sr_preset = settings::g_swapchainTabSettings.dlss_sr_preset_override.GetValue();
            if (sr_preset > 0) { // 0 = Game Default, 1+ = Preset A+
                InValue = static_cast<unsigned int>(sr_preset);
                LogInfo("DLSS SR preset override: %s -> %u (Preset %c)", param_name.c_str(), InValue, 'A' + sr_preset - 1);
            }
        }

        // Check for DLSS Ray Reconstruction preset parameters
        if (IsDLSSPresetParameter(param_name, g_dlss_rr_preset_params)) {
            int rr_preset = settings::g_swapchainTabSettings.dlss_rr_preset_override.GetValue();
            if (rr_preset > 0) { // 0 = Game Default, 1+ = Preset A+
                InValue = static_cast<unsigned int>(rr_preset);
                LogInfo("DLSS RR preset override: %s -> %u (Preset %c)", param_name.c_str(), InValue, 'A' + rr_preset - 1);
            }
        }
    }

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

// D3D12 Init detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_Init_Detour(unsigned long long InApplicationId, const wchar_t *InApplicationDataPath, ID3D12Device *InDevice, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo, NVSDK_NGX_Version InSDKVersion) {
    LogInfo("NGX D3D12 Init called - AppId: %llu", InApplicationId);

    if (NVSDK_NGX_D3D12_Init_Original != nullptr) {
        return NVSDK_NGX_D3D12_Init_Original(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion);
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D12 Init Ext detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_Init_Ext_Detour(unsigned long long InApplicationId, const wchar_t *InApplicationDataPath, ID3D12Device *InDevice, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo, void* Unknown5) {
    LogInfo("NGX D3D12 Init Ext called - AppId: %llu", InApplicationId);

    if (NVSDK_NGX_D3D12_Init_Ext_Original != nullptr) {
        return NVSDK_NGX_D3D12_Init_Ext_Original(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo, Unknown5);
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D12 Init ProjectID detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_Init_ProjectID_Detour(const char *InProjectId, NVSDK_NGX_EngineType InEngineType, const char *InEngineVersion, const wchar_t *InApplicationDataPath, ID3D12Device *InDevice, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo, NVSDK_NGX_Version InSDKVersion) {
    LogInfo("NGX D3D12 Init ProjectID called - ProjectId: %s", InProjectId ? InProjectId : "null");

    if (NVSDK_NGX_D3D12_Init_ProjectID_Original != nullptr) {
        return NVSDK_NGX_D3D12_Init_ProjectID_Original(InProjectId, InEngineType, InEngineVersion, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion);
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D12 CreateFeature detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_CreateFeature_Detour(ID3D12GraphicsCommandList *InCmdList, NVSDK_NGX_Feature InFeatureID, NVSDK_NGX_Parameter *InParameters, NVSDK_NGX_Handle **OutHandle) {
    LogInfo("NGX D3D12 CreateFeature called - FeatureID: %d", InFeatureID);

    // Track enabled features based on FeatureID
    switch (InFeatureID) {
        case NVSDK_NGX_Feature_SuperSampling:
            LogInfo("DLSS Super Resolution feature being created");
            g_ngx_parameters.update_int("Feature.DLSS.Enabled", 1);
            break;
        case NVSDK_NGX_Feature_FrameGeneration:
            LogInfo("DLSS Frame Generation feature being created");
            g_ngx_parameters.update_int("Feature.DLSSG.Enabled", 1);
            break;
        case NVSDK_NGX_Feature_RayReconstruction:
            LogInfo("Ray Reconstruction feature being created");
            g_ngx_parameters.update_int("Feature.RayReconstruction.Enabled", 1);
            break;
        default:
            LogInfo("Unknown NGX feature being created - FeatureID: %d", InFeatureID);
            break;
    }

    // Hook the parameter vtable if we have parameters
    if (InParameters != nullptr) {
        HookNGXParameterVTable(InParameters);
    }

    if (NVSDK_NGX_D3D12_CreateFeature_Original != nullptr) {
        return NVSDK_NGX_D3D12_CreateFeature_Original(InCmdList, InFeatureID, InParameters, OutHandle);
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D12 ReleaseFeature detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_ReleaseFeature_Detour(NVSDK_NGX_Handle *InHandle) {
    LogInfo("NGX D3D12 ReleaseFeature called");

    if (NVSDK_NGX_D3D12_ReleaseFeature_Original != nullptr) {
        return NVSDK_NGX_D3D12_ReleaseFeature_Original(InHandle);
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D12 EvaluateFeature detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_EvaluateFeature_Detour(ID3D12GraphicsCommandList *InCmdList, const NVSDK_NGX_Handle *InFeatureHandle, const NVSDK_NGX_Parameter *InParameters, PFN_NVSDK_NGX_ProgressCallback InCallback) {
   // LogInfo("NGX D3D12 EvaluateFeature called");

    // Hook the parameter vtable if we have parameters
    if (InParameters != nullptr) {
        HookNGXParameterVTable((NVSDK_NGX_Parameter*)InParameters);
    }

    if (NVSDK_NGX_D3D12_EvaluateFeature_Original != nullptr) {
        return NVSDK_NGX_D3D12_EvaluateFeature_Original(InCmdList, InFeatureHandle, InParameters, InCallback);
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D11 Init detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D11_Init_Detour(unsigned long long InApplicationId, const wchar_t *InApplicationDataPath, ID3D11Device *InDevice, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo, NVSDK_NGX_Version InSDKVersion) {
    LogInfo("NGX D3D11 Init called - AppId: %llu", InApplicationId);

    if (NVSDK_NGX_D3D11_Init_Original != nullptr) {
        return NVSDK_NGX_D3D11_Init_Original(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion);
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D11 Init Ext detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D11_Init_Ext_Detour(unsigned long long InApplicationId, const wchar_t *InApplicationDataPath, ID3D11Device *InDevice, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo, void* Unknown5) {
    LogInfo("NGX D3D11 Init Ext called - AppId: %llu", InApplicationId);

    if (NVSDK_NGX_D3D11_Init_Ext_Original != nullptr) {
        return NVSDK_NGX_D3D11_Init_Ext_Original(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo, Unknown5);
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D11 Init ProjectID detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D11_Init_ProjectID_Detour(const char *InProjectId, NVSDK_NGX_EngineType InEngineType, const char *InEngineVersion, const wchar_t *InApplicationDataPath, ID3D11Device *InDevice, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo, NVSDK_NGX_Version InSDKVersion) {
    LogInfo("NGX D3D11 Init ProjectID called - ProjectId: %s", InProjectId ? InProjectId : "null");

    if (NVSDK_NGX_D3D11_Init_ProjectID_Original != nullptr) {
        return NVSDK_NGX_D3D11_Init_ProjectID_Original(InProjectId, InEngineType, InEngineVersion, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion);
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D11 CreateFeature detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D11_CreateFeature_Detour(ID3D11DeviceContext *InDevCtx, NVSDK_NGX_Feature InFeatureID, NVSDK_NGX_Parameter *InParameters, NVSDK_NGX_Handle **OutHandle) {
    LogInfo("NGX D3D11 CreateFeature called - FeatureID: %d", InFeatureID);

    // Track enabled features based on FeatureID
    switch (InFeatureID) {
        case NVSDK_NGX_Feature_SuperSampling:
            LogInfo("DLSS Super Resolution feature being created (D3D11)");
            g_ngx_parameters.update_int("Feature.DLSS.Enabled", 1);
            break;
        case NVSDK_NGX_Feature_FrameGeneration:
            LogInfo("DLSS Frame Generation feature being created (D3D11)");
            g_ngx_parameters.update_int("Feature.DLSSG.Enabled", 1);
            break;
        case NVSDK_NGX_Feature_RayReconstruction:
            LogInfo("Ray Reconstruction feature being created (D3D11)");
            g_ngx_parameters.update_int("Feature.RayReconstruction.Enabled", 1);
            break;
        default:
            LogInfo("Unknown NGX feature being created (D3D11) - FeatureID: %d", InFeatureID);
            break;
    }

    // Hook the parameter vtable if we have parameters
    if (InParameters != nullptr) {
        HookNGXParameterVTable(InParameters);
    }

    if (NVSDK_NGX_D3D11_CreateFeature_Original != nullptr) {
        return NVSDK_NGX_D3D11_CreateFeature_Original(InDevCtx, InFeatureID, InParameters, OutHandle);
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D11 ReleaseFeature detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D11_ReleaseFeature_Detour(NVSDK_NGX_Handle *InHandle) {
    LogInfo("NGX D3D11 ReleaseFeature called");

    if (NVSDK_NGX_D3D11_ReleaseFeature_Original != nullptr) {
        return NVSDK_NGX_D3D11_ReleaseFeature_Original(InHandle);
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D11 EvaluateFeature detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D11_EvaluateFeature_Detour(ID3D11DeviceContext *InDevCtx, const NVSDK_NGX_Handle *InFeatureHandle, const NVSDK_NGX_Parameter *InParameters, PFN_NVSDK_NGX_ProgressCallback InCallback) {
    LogInfo("NGX D3D11 EvaluateFeature called");

    // Hook the parameter vtable if we have parameters
    if (InParameters != nullptr) {
        HookNGXParameterVTable((NVSDK_NGX_Parameter*)InParameters);
    }

    if (NVSDK_NGX_D3D11_EvaluateFeature_Original != nullptr) {
        return NVSDK_NGX_D3D11_EvaluateFeature_Original(InDevCtx, InFeatureHandle, InParameters, InCallback);
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
    CreateAndEnableHook(vftable[3], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_SetI_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_SetI_Original),
                        "NVSDK_NGX_Parameter_SetI");

    // Hook SetUI (vtable index 4)
    CreateAndEnableHook(vftable[4], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_SetUI_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_SetUI_Original),
                        "NVSDK_NGX_Parameter_SetUI");
    // Hook SetD (vtable index 5)
    CreateAndEnableHook(vftable[5], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_SetD_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_SetD_Original),
                        "NVSDK_NGX_Parameter_SetD");

    // Hook SetF (vtable index 6)
    CreateAndEnableHook(vftable[6], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_SetF_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_SetF_Original),
                        "NVSDK_NGX_Parameter_SetF");

    // Hook SetULL (vtable index 7)
    CreateAndEnableHook(vftable[7], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_SetULL_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_SetULL_Original),
                        "NVSDK_NGX_Parameter_SetULL");
    // Hook GetVoidPointer (vtable index 8) - Special-K uses index 8
    CreateAndEnableHook(vftable[8], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_GetVoidPointer_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_GetVoidPointer_Original),
                        "NVSDK_NGX_Parameter_GetVoidPointer");
    // Hook GetI (vtable index 11) - Special-K uses index 11
    CreateAndEnableHook(vftable[11], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_GetI_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_GetI_Original),
                        "NVSDK_NGX_Parameter_GetI");
    // Hook GetUI (vtable index 12) - Special-K uses index 12
    CreateAndEnableHook(vftable[12], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_GetUI_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_GetUI_Original),
                        "NVSDK_NGX_Parameter_GetUI");
    // Hook GetULL (vtable index 15) - Special-K uses index 15
    CreateAndEnableHook(vftable[15], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_GetULL_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_GetULL_Original),
                        "NVSDK_NGX_Parameter_GetULL");



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
        // Apply DLSS preset parameters during initialization
        ApplyDLSSPresetParameters(*InParameters);
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
        // Apply DLSS preset parameters during initialization
        ApplyDLSSPresetParameters(*InParameters);
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
        // Apply DLSS preset parameters during initialization
        ApplyDLSSPresetParameters(*InParameters);
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
        // Apply DLSS preset parameters during initialization
        ApplyDLSSPresetParameters(*InParameters);
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

    // Hook NGX initialization functions
    // D3D12 Init hooks
    CreateAndEnableHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D12_Init"),
                        reinterpret_cast<LPVOID>(NVSDK_NGX_D3D12_Init_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D12_Init_Original),
                        "NVSDK_NGX_D3D12_Init");

    CreateAndEnableHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D12_Init_Ext"),
                        reinterpret_cast<LPVOID>(NVSDK_NGX_D3D12_Init_Ext_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D12_Init_Ext_Original),
                        "NVSDK_NGX_D3D12_Init_Ext");

    CreateAndEnableHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D12_Init_ProjectID"),
                        reinterpret_cast<LPVOID>(NVSDK_NGX_D3D12_Init_ProjectID_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D12_Init_ProjectID_Original),
                        "NVSDK_NGX_D3D12_Init_ProjectID");

    CreateAndEnableHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D12_CreateFeature"),
                        reinterpret_cast<LPVOID>(NVSDK_NGX_D3D12_CreateFeature_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D12_CreateFeature_Original),
                        "NVSDK_NGX_D3D12_CreateFeature");

    CreateAndEnableHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D12_ReleaseFeature"),
                        reinterpret_cast<LPVOID>(NVSDK_NGX_D3D12_ReleaseFeature_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D12_ReleaseFeature_Original),
                        "NVSDK_NGX_D3D12_ReleaseFeature");

    CreateAndEnableHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D12_EvaluateFeature"),
                        reinterpret_cast<LPVOID>(NVSDK_NGX_D3D12_EvaluateFeature_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D12_EvaluateFeature_Original),
                        "NVSDK_NGX_D3D12_EvaluateFeature");

    // D3D11 Init hooks
    CreateAndEnableHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D11_Init"),
                        reinterpret_cast<LPVOID>(NVSDK_NGX_D3D11_Init_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D11_Init_Original),
                        "NVSDK_NGX_D3D11_Init");

    CreateAndEnableHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D11_Init_Ext"),
                        reinterpret_cast<LPVOID>(NVSDK_NGX_D3D11_Init_Ext_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D11_Init_Ext_Original),
                        "NVSDK_NGX_D3D11_Init_Ext");

    CreateAndEnableHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D11_Init_ProjectID"),
                        reinterpret_cast<LPVOID>(NVSDK_NGX_D3D11_Init_ProjectID_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D11_Init_ProjectID_Original),
                        "NVSDK_NGX_D3D11_Init_ProjectID");

    CreateAndEnableHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D11_CreateFeature"),
                        reinterpret_cast<LPVOID>(NVSDK_NGX_D3D11_CreateFeature_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D11_CreateFeature_Original),
                        "NVSDK_NGX_D3D11_CreateFeature");

    CreateAndEnableHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D11_ReleaseFeature"),
                        reinterpret_cast<LPVOID>(NVSDK_NGX_D3D11_ReleaseFeature_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D11_ReleaseFeature_Original),
                        "NVSDK_NGX_D3D11_ReleaseFeature");

    CreateAndEnableHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D11_EvaluateFeature"),
                        reinterpret_cast<LPVOID>(NVSDK_NGX_D3D11_EvaluateFeature_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D11_EvaluateFeature_Original),
                        "NVSDK_NGX_D3D11_EvaluateFeature");

    // Hook NGX parameter functions to get Parameter objects
    // These functions are exported from _nvngx.dll and return Parameter objects
    // We'll hook their vtables when they're called

    // Hook D3D12 GetParameters
    CreateAndEnableHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D12_GetParameters"),
                        reinterpret_cast<LPVOID>(NVSDK_NGX_D3D12_GetParameters_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D12_GetParameters_Original),
                        "NVSDK_NGX_D3D12_GetParameters");

    // Hook D3D12 AllocateParameters
    CreateAndEnableHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D12_AllocateParameters"),
                        reinterpret_cast<LPVOID>(NVSDK_NGX_D3D12_AllocateParameters_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D12_AllocateParameters_Original),
                        "NVSDK_NGX_D3D12_AllocateParameters");

    // Hook D3D11 GetParameters
    CreateAndEnableHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D11_GetParameters"),
                        reinterpret_cast<LPVOID>(NVSDK_NGX_D3D11_GetParameters_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D11_GetParameters_Original),
                        "NVSDK_NGX_D3D11_GetParameters");

    // Hook D3D11 AllocateParameters
    CreateAndEnableHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D11_AllocateParameters"),
                        reinterpret_cast<LPVOID>(NVSDK_NGX_D3D11_AllocateParameters_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D11_AllocateParameters_Original),
                        "NVSDK_NGX_D3D11_AllocateParameters");

    LogInfo("NGX initialization hooks installed successfully");
    LogInfo("NGX Parameter vtable hooks will be installed when Parameter objects are created");
    LogInfo("NGX Init, CreateFeature, and EvaluateFeature hooks are now active");
    LogInfo("VTable hooks are called automatically inside detour functions");
    return true;
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

// Feature status checking functions
bool IsDLSSEnabled() {
    int enabled;
    return g_ngx_parameters.get_as_int("Feature.DLSS.Enabled", enabled) && enabled == 1;
}

bool IsDLSSGEnabled() {
    int enabled;
    return g_ngx_parameters.get_as_int("Feature.DLSSG.Enabled", enabled) && enabled == 1;
}

bool IsRayReconstructionEnabled() {
    int enabled;
    return g_ngx_parameters.get_as_int("Feature.RayReconstruction.Enabled", enabled) && enabled == 1;
}

std::string GetEnabledFeaturesSummary() {
    std::vector<std::string> enabled_features;

    if (IsDLSSEnabled()) {
        enabled_features.push_back("DLSS");
    }
    if (IsDLSSGEnabled()) {
        enabled_features.push_back("DLSS-G");
    }
    if (IsRayReconstructionEnabled()) {
        enabled_features.push_back("Ray Reconstruction");
    }

    if (enabled_features.empty()) {
        return "No NGX features detected";
    }

    std::string result;
    for (size_t i = 0; i < enabled_features.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += enabled_features[i];
    }

    return result;
}
