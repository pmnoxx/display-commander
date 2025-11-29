#include "ngx_hooks.hpp"
#include <MinHook.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "../../../external/nvidia-dlss/include/nvsdk_ngx_defs.h"
#include "../globals.hpp"
#include "../settings/developer_tab_settings.hpp"
#include "../settings/swapchain_tab_settings.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "../utils/srwlock_wrapper.hpp"
#include "hook_suppression_manager.hpp"

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

// Using official NVIDIA NGX enums from nvsdk_ngx_defs.h

// Helper function to get feature name from enum
const char* GetNGXFeatureName(NVSDK_NGX_Feature feature) {
    switch (feature) {
        case NVSDK_NGX_Feature_Reserved0:             return "Reserved0";
        case NVSDK_NGX_Feature_SuperSampling:         return "SuperSampling (DLSS)";
        case NVSDK_NGX_Feature_InPainting:            return "InPainting";
        case NVSDK_NGX_Feature_ImageSuperResolution:  return "ImageSuperResolution";
        case NVSDK_NGX_Feature_SlowMotion:            return "SlowMotion";
        case NVSDK_NGX_Feature_VideoSuperResolution:  return "VideoSuperResolution";
        case NVSDK_NGX_Feature_Reserved1:             return "Reserved1";
        case NVSDK_NGX_Feature_Reserved2:             return "Reserved2";
        case NVSDK_NGX_Feature_Reserved3:             return "Reserved3";
        case NVSDK_NGX_Feature_ImageSignalProcessing: return "ImageSignalProcessing";
        case NVSDK_NGX_Feature_DeepResolve:           return "DeepResolve";
        case NVSDK_NGX_Feature_FrameGeneration:       return "FrameGeneration (DLSS-G)";
        case NVSDK_NGX_Feature_DeepDVC:               return "DeepDVC";
        case NVSDK_NGX_Feature_RayReconstruction:     return "RayReconstruction";
        case NVSDK_NGX_Feature_Reserved14:            return "Reserved14";
        case NVSDK_NGX_Feature_Reserved15:            return "Reserved15";
        case NVSDK_NGX_Feature_Reserved16:            return "Reserved16";
        case NVSDK_NGX_Feature_Reserved_SDK:          return "Reserved SDK";
        case NVSDK_NGX_Feature_Reserved_Core:         return "Reserved Core";
        case NVSDK_NGX_Feature_Reserved_Unknown:      return "Reserved Unknown";
        default:                                      return "Unknown Feature";
    }
}

// Helper function to get engine type name from enum
const char* GetNGXEngineTypeName(NVSDK_NGX_EngineType engineType) {
    switch (engineType) {
        case NVSDK_NGX_ENGINE_TYPE_CUSTOM:    return "Custom";
        case NVSDK_NGX_ENGINE_TYPE_UNREAL:    return "Unreal Engine";
        case NVSDK_NGX_ENGINE_TYPE_UNITY:     return "Unity";
        case NVSDK_NGX_ENGINE_TYPE_OMNIVERSE: return "Omniverse";
        default:                              return "Unknown Engine";
    }
}

// Progress callback type
typedef void(NVSDK_CONV* PFN_NVSDK_NGX_ProgressCallback)(float InCurrentProgress, bool& OutShouldCancel);

// Handle tracking for NGX features
static std::map<NVSDK_NGX_Handle*, NVSDK_NGX_Feature> g_ngx_handle_map;
static SRWLOCK g_ngx_handle_mutex = SRWLOCK_INIT;

// Using official NVIDIA NGX enums from nvsdk_ngx_defs.h

#define NVSDK_NGX_SUCCEED(value) (((value) & 0xFFF00000) != NVSDK_NGX_Result_Fail)
#define NVSDK_NGX_FAILED(value)  (((value) & 0xFFF00000) == NVSDK_NGX_Result_Fail)

// Helper functions for handle tracking
static void TrackNGXHandle(NVSDK_NGX_Handle* handle, NVSDK_NGX_Feature feature) {
    if (handle == nullptr) return;

    utils::SRWLockExclusive lock(g_ngx_handle_mutex);
    g_ngx_handle_map[handle] = feature;

    // Update global tracking variables
    switch (feature) {
        case NVSDK_NGX_Feature_SuperSampling:
            g_dlss_enabled.store(true);
            LogInfo("NGX DLSS Super Resolution enabled");
            break;
        case NVSDK_NGX_Feature_FrameGeneration:
            g_dlssg_enabled.store(true);
            LogInfo("NGX DLSS Frame Generation enabled");
            break;
        case NVSDK_NGX_Feature_RayReconstruction:
            g_ray_reconstruction_enabled.store(true);
            LogInfo("NGX Ray Reconstruction enabled");
            break;
    }
}

static void UntrackNGXHandle(NVSDK_NGX_Handle* handle) {
    if (handle == nullptr) return;

    utils::SRWLockExclusive lock(g_ngx_handle_mutex);
    auto it = g_ngx_handle_map.find(handle);
    if (it != g_ngx_handle_map.end()) {
        NVSDK_NGX_Feature feature = it->second;
        g_ngx_handle_map.erase(it);

        // Update global tracking variables
        switch (feature) {
            case NVSDK_NGX_Feature_SuperSampling:
                g_dlss_enabled.store(false);
                LogInfo("NGX DLSS Super Resolution disabled");
                break;
            case NVSDK_NGX_Feature_FrameGeneration:
                g_dlssg_enabled.store(false);
                LogInfo("NGX DLSS Frame Generation disabled");
                break;
            case NVSDK_NGX_Feature_RayReconstruction:
                g_ray_reconstruction_enabled.store(false);
                LogInfo("NGX Ray Reconstruction disabled");
                break;
        }
    }
}

static NVSDK_NGX_Feature GetFeatureFromHandle(NVSDK_NGX_Handle* handle) {
    if (handle == nullptr) return static_cast<NVSDK_NGX_Feature>(-1);

    utils::SRWLockExclusive lock(g_ngx_handle_mutex);
    auto it = g_ngx_handle_map.find(handle);
    return (it != g_ngx_handle_map.end()) ? it->second : static_cast<NVSDK_NGX_Feature>(-1);
}

// Cleanup function to clear handle map and reset tracking variables
static void CleanupNGXHandleTracking() {
    utils::SRWLockExclusive lock(g_ngx_handle_mutex);
    g_ngx_handle_map.clear();

    // Reset all tracking variables
    g_dlss_enabled.store(false);
    g_dlssg_enabled.store(false);
    g_ray_reconstruction_enabled.store(false);

    LogInfo("NGX handle tracking cleaned up");
}

// NGX function pointer type definitions (following Special-K's approach)
using NVSDK_NGX_Parameter_SetF_pfn = void(NVSDK_CONV*)(NVSDK_NGX_Parameter* InParameter, const char* InName,
                                                       float InValue);
using NVSDK_NGX_Parameter_SetD_pfn = void(NVSDK_CONV*)(NVSDK_NGX_Parameter* InParameter, const char* InName,
                                                       double InValue);
using NVSDK_NGX_Parameter_SetI_pfn = void(NVSDK_CONV*)(NVSDK_NGX_Parameter* InParameter, const char* InName,
                                                       int InValue);
using NVSDK_NGX_Parameter_SetUI_pfn = void(NVSDK_CONV*)(NVSDK_NGX_Parameter* InParameter, const char* InName,
                                                        unsigned int InValue);
using NVSDK_NGX_Parameter_SetULL_pfn = void(NVSDK_CONV*)(NVSDK_NGX_Parameter* InParameter, const char* InName,
                                                         unsigned long long InValue);

// NGX parameter getter function pointer types
using NVSDK_NGX_Parameter_GetI_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter* InParameter, const char* InName,
                                                                   int* OutValue);
using NVSDK_NGX_Parameter_GetUI_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter* InParameter,
                                                                    const char* InName, unsigned int* OutValue);
using NVSDK_NGX_Parameter_GetULL_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter* InParameter,
                                                                     const char* InName, unsigned long long* OutValue);
using NVSDK_NGX_Parameter_GetVoidPointer_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter* InParameter,
                                                                             const char* InName, void** OutValue);

// NGX initialization function pointer types
using NVSDK_NGX_D3D12_Init_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(unsigned long long InApplicationId,
                                                               const wchar_t* InApplicationDataPath,
                                                               ID3D12Device* InDevice,
                                                               const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                                               NVSDK_NGX_Version InSDKVersion);
using NVSDK_NGX_D3D12_Init_Ext_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(unsigned long long InApplicationId,
                                                                   const wchar_t* InApplicationDataPath,
                                                                   ID3D12Device* InDevice,
                                                                   const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                                                   void* Unknown5);
using NVSDK_NGX_D3D12_Init_ProjectID_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(
    const char* InProjectId, NVSDK_NGX_EngineType InEngineType, const char* InEngineVersion,
    const wchar_t* InApplicationDataPath, ID3D12Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
    NVSDK_NGX_Version InSDKVersion);
using NVSDK_NGX_D3D12_GetParameters_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter** OutParameters);
using NVSDK_NGX_D3D12_AllocateParameters_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter** OutParameters);
using NVSDK_NGX_D3D12_CreateFeature_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList* InCmdList,
                                                                        NVSDK_NGX_Feature InFeatureID,
                                                                        NVSDK_NGX_Parameter* InParameters,
                                                                        NVSDK_NGX_Handle** OutHandle);
using NVSDK_NGX_D3D12_ReleaseFeature_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle* InHandle);
using NVSDK_NGX_D3D12_EvaluateFeature_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList* InCmdList,
                                                                          const NVSDK_NGX_Handle* InFeatureHandle,
                                                                          const NVSDK_NGX_Parameter* InParameters,
                                                                          PFN_NVSDK_NGX_ProgressCallback InCallback);

using NVSDK_NGX_D3D11_Init_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(unsigned long long InApplicationId,
                                                               const wchar_t* InApplicationDataPath,
                                                               ID3D11Device* InDevice,
                                                               const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                                               NVSDK_NGX_Version InSDKVersion);
using NVSDK_NGX_D3D11_Init_Ext_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(unsigned long long InApplicationId,
                                                                   const wchar_t* InApplicationDataPath,
                                                                   ID3D11Device* InDevice,
                                                                   const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                                                   void* Unknown5);
using NVSDK_NGX_D3D11_Init_ProjectID_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(
    const char* InProjectId, NVSDK_NGX_EngineType InEngineType, const char* InEngineVersion,
    const wchar_t* InApplicationDataPath, ID3D11Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
    NVSDK_NGX_Version InSDKVersion);
using NVSDK_NGX_D3D11_GetParameters_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter** OutParameters);
using NVSDK_NGX_D3D11_AllocateParameters_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter** OutParameters);
using NVSDK_NGX_D3D11_CreateFeature_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D11DeviceContext* InDevCtx,
                                                                        NVSDK_NGX_Feature InFeatureID,
                                                                        NVSDK_NGX_Parameter* InParameters,
                                                                        NVSDK_NGX_Handle** OutHandle);
using NVSDK_NGX_D3D11_ReleaseFeature_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle* InHandle);
using NVSDK_NGX_D3D11_EvaluateFeature_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D11DeviceContext* InDevCtx,
                                                                          const NVSDK_NGX_Handle* InFeatureHandle,
                                                                          const NVSDK_NGX_Parameter* InParameters,
                                                                          PFN_NVSDK_NGX_ProgressCallback InCallback);

// UpdateFeature function pointer type
using NVSDK_NGX_UpdateFeature_pfn = NVSDK_NGX_Result(NVSDK_CONV*)(const NVSDK_NGX_Application_Identifier* ApplicationId,
                                                                  const NVSDK_NGX_Feature FeatureID);

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

// UpdateFeature original function pointer
NVSDK_NGX_UpdateFeature_pfn NVSDK_NGX_UpdateFeature_Original = nullptr;

// Global flag to track if vtable hooks are installed
static bool g_ngx_vtable_hooks_installed = false;

// DLSS preset parameter names arrays
static const std::vector<std::string> g_dlss_sr_preset_params = {
    "DLSS.Hint.Render.Preset.Quality",      "DLSS.Hint.Render.Preset.Balanced",
    "DLSS.Hint.Render.Preset.Performance",  "DLSS.Hint.Render.Preset.UltraPerformance",
    "DLSS.Hint.Render.Preset.UltraQuality", "DLSS.Hint.Render.Preset.DLAA"};

static const std::vector<std::string> g_dlss_rr_preset_params = {
    "RayReconstruction.Hint.Render.Preset.Quality",      "RayReconstruction.Hint.Render.Preset.Balanced",
    "RayReconstruction.Hint.Render.Preset.Performance",  "RayReconstruction.Hint.Render.Preset.UltraPerformance",
    "RayReconstruction.Hint.Render.Preset.UltraQuality", "RayReconstruction.Hint.Render.Preset.DLAA"};

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
    int sr_preset = GetDLSSPresetValue(settings::g_swapchainTabSettings.dlss_sr_preset_override.GetValue());
    int rr_preset = GetDLSSPresetValue(settings::g_swapchainTabSettings.dlss_rr_preset_override.GetValue());

    // Apply DLSS Super Resolution preset parameters
    if (sr_preset >= 0) {  // -1 = Game Default (no override), 0 = DLSS Default, 1+ = Preset A+
        for (const auto& param_name : g_dlss_sr_preset_params) {
            if (NVSDK_NGX_Parameter_SetI_Original != nullptr) {
                NVSDK_NGX_Parameter_SetI_Original(InParameters, param_name.c_str(), sr_preset);
                g_ngx_parameters.update_int(param_name, sr_preset);
                if (sr_preset == 0) {
                    LogInfo("Applied DLSS SR preset: %s -> %d (DLSS Default)", param_name.c_str(), sr_preset);
                } else {
                    LogInfo("Applied DLSS SR preset: %s -> %d (Preset %c)", param_name.c_str(), sr_preset,
                            'A' + sr_preset - 1);
                }
            }
        }
    }

    // Apply DLSS Ray Reconstruction preset parameters
    if (rr_preset >= 0) {  // -1 = Game Default (no override), 0 = DLSS Default, 1+ = Preset A+
        for (const auto& param_name : g_dlss_rr_preset_params) {
            if (NVSDK_NGX_Parameter_SetI_Original != nullptr) {
                NVSDK_NGX_Parameter_SetI_Original(InParameters, param_name.c_str(), rr_preset);
                g_ngx_parameters.update_int(param_name, rr_preset);
                if (rr_preset == 0) {
                    LogInfo("Applied DLSS RR preset: %s -> %d (DLSS Default)", param_name.c_str(), rr_preset);
                } else {
                    LogInfo("Applied DLSS RR preset: %s -> %d (Preset %c)", param_name.c_str(), rr_preset,
                            'A' + rr_preset - 1);
                }
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
    // Increment NGX counters
    g_ngx_counters.parameter_setf_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

    // Store parameter in thread-safe storage (store original game value)
    if (InName != nullptr) {
        g_ngx_parameters.update_float(std::string(InName), InValue);
    }

    // Check for override value
    float override_value = InValue;
    bool has_override = false;
    if (InName != nullptr) {
        float override_val;
        if (g_ngx_parameter_overrides.get_as_float(std::string(InName), override_val)) {
            override_value = override_val;
            has_override = true;
        }
    }

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 60) {
        if (has_override) {
            LogInfo("NGX Parameter SetF called - Name: %s, Game Value: %f, Override: %f", InName ? InName : "null", InValue, override_value);
        } else {
            LogInfo("NGX Parameter SetF called - Name: %s, Value: %f", InName ? InName : "null", InValue);
        }
        log_count++;
    }

    // Call original function with override value if present
    if (NVSDK_NGX_Parameter_SetF_Original != nullptr) {
        NVSDK_NGX_Parameter_SetF_Original(InParameter, InName, override_value);
    }
}

// Hooked NVSDK_NGX_Parameter_SetD function
void NVSDK_CONV NVSDK_NGX_Parameter_SetD_Detour(NVSDK_NGX_Parameter* InParameter, const char* InName, double InValue) {
    // Increment NGX counters
    g_ngx_counters.parameter_setd_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

    // Store parameter in thread-safe storage (store original game value)
    if (InName != nullptr) {
        g_ngx_parameters.update_double(std::string(InName), InValue);
    }

    // Check for override value
    double override_value = InValue;
    bool has_override = false;
    if (InName != nullptr) {
        double override_val;
        if (g_ngx_parameter_overrides.get_as_double(std::string(InName), override_val)) {
            override_value = override_val;
            has_override = true;
        }
    }

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 60) {
        if (has_override) {
            LogInfo("NGX Parameter SetD called - Name: %s, Game Value: %f, Override: %f", InName ? InName : "null", InValue, override_value);
        } else {
            LogInfo("NGX Parameter SetD called - Name: %s, Value: %f", InName ? InName : "null", InValue);
        }
        log_count++;
    }

    // Call original function with override value if present
    if (NVSDK_NGX_Parameter_SetD_Original != nullptr) {
        NVSDK_NGX_Parameter_SetD_Original(InParameter, InName, override_value);
    }
}

// Hooked NVSDK_NGX_Parameter_SetI function
void NVSDK_CONV NVSDK_NGX_Parameter_SetI_Detour(NVSDK_NGX_Parameter* InParameter, const char* InName, int InValue) {
    // Increment NGX counters
    g_ngx_counters.parameter_seti_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

    // DLSS preset override logic
    if (InName != nullptr && settings::g_swapchainTabSettings.dlss_preset_override_enabled.GetValue()) {
        std::string param_name = std::string(InName);

        // Check for DLSS Super Resolution preset parameters
        if (IsDLSSPresetParameter(param_name, g_dlss_sr_preset_params)) {
            int sr_preset = GetDLSSPresetValue(settings::g_swapchainTabSettings.dlss_sr_preset_override.GetValue());
            if (sr_preset >= 0) {  // -1 = Game Default (no override), 0 = DLSS Default, 1+ = Preset A+
                InValue = sr_preset;
                if (sr_preset == 0) {
                    LogInfo("DLSS SR preset override: %s -> %d (DLSS Default)", param_name.c_str(), InValue);
                } else {
                    LogInfo("DLSS SR preset override: %s -> %d (Preset %c)", param_name.c_str(), InValue,
                            'A' + sr_preset - 1);
                }
            }
        }

        // Check for DLSS Ray Reconstruction preset parameters
        if (IsDLSSPresetParameter(param_name, g_dlss_rr_preset_params)) {
            int rr_preset = GetDLSSPresetValue(settings::g_swapchainTabSettings.dlss_rr_preset_override.GetValue());
            if (rr_preset >= 0) {  // -1 = Game Default (no override), 0 = DLSS Default, 1+ = Preset A+
                InValue = rr_preset;
                if (rr_preset == 0) {
                    LogInfo("DLSS RR preset override: %s -> %d (DLSS Default)", param_name.c_str(), InValue);
                } else {
                    LogInfo("DLSS RR preset override: %s -> %d (Preset %c)", param_name.c_str(), InValue,
                            'A' + rr_preset - 1);
                }
            }
        }
    }

    // Store parameter in thread-safe storage (store original game value before override check)
    int original_value = InValue;
    if (InName != nullptr) {
        g_ngx_parameters.update_int(std::string(InName), original_value);
    }

    // Check for override value (after DLSS preset override logic)
    int override_value = InValue;
    bool has_override = false;
    if (InName != nullptr) {
        int override_val;
        if (g_ngx_parameter_overrides.get_as_int(std::string(InName), override_val)) {
            override_value = override_val;
            has_override = true;
        }
    }

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 60) {
        if (has_override) {
            LogInfo("NGX Parameter SetI called - Name: %s, Game Value: %d, Override: %d", InName ? InName : "null", original_value, override_value);
        } else {
            LogInfo("NGX Parameter SetI called - Name: %s, Value: %d", InName ? InName : "null", InValue);
        }
        log_count++;
    }

    // Call original function with override value if present
    if (NVSDK_NGX_Parameter_SetI_Original != nullptr) {
        NVSDK_NGX_Parameter_SetI_Original(InParameter, InName, override_value);
    }
}

// Hooked NVSDK_NGX_Parameter_SetUI function
void NVSDK_CONV NVSDK_NGX_Parameter_SetUI_Detour(NVSDK_NGX_Parameter* InParameter, const char* InName,
                                                 unsigned int InValue) {
    // Increment NGX counters
    g_ngx_counters.parameter_setui_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

    // DLSS preset override logic
    if (InName != nullptr && settings::g_swapchainTabSettings.dlss_preset_override_enabled.GetValue()) {
        std::string param_name = std::string(InName);

        // Check for DLSS Super Resolution preset parameters
        if (IsDLSSPresetParameter(param_name, g_dlss_sr_preset_params)) {
            int sr_preset = GetDLSSPresetValue(settings::g_swapchainTabSettings.dlss_sr_preset_override.GetValue());
            if (sr_preset >= 0) {  // -1 = Game Default (no override), 0 = DLSS Default, 1+ = Preset A+
                InValue = static_cast<unsigned int>(sr_preset);
                if (sr_preset == 0) {
                    LogInfo("DLSS SR preset override: %s -> %u (DLSS Default)", param_name.c_str(), InValue);
                } else {
                    LogInfo("DLSS SR preset override: %s -> %u (Preset %c)", param_name.c_str(), InValue,
                            'A' + sr_preset - 1);
                }
            }
        }

        // Check for DLSS Ray Reconstruction preset parameters
        if (IsDLSSPresetParameter(param_name, g_dlss_rr_preset_params)) {
            int rr_preset = GetDLSSPresetValue(settings::g_swapchainTabSettings.dlss_rr_preset_override.GetValue());
            if (rr_preset >= 0) {  // -1 = Game Default (no override), 0 = DLSS Default, 1+ = Preset A+
                InValue = static_cast<unsigned int>(rr_preset);
                if (rr_preset == 0) {
                    LogInfo("DLSS RR preset override: %s -> %u (DLSS Default)", param_name.c_str(), InValue);
                } else {
                    LogInfo("DLSS RR preset override: %s -> %u (Preset %c)", param_name.c_str(), InValue,
                            'A' + rr_preset - 1);
                }
            }
        }
    }

    // Store parameter in thread-safe storage (store original game value before override check)
    unsigned int original_value = InValue;
    if (InName != nullptr) {
        g_ngx_parameters.update_uint(std::string(InName), original_value);
    }

    // Check for override value (after DLSS preset override logic)
    unsigned int override_value = InValue;
    bool has_override = false;
    if (InName != nullptr) {
        unsigned int override_val;
        if (g_ngx_parameter_overrides.get_as_uint(std::string(InName), override_val)) {
            override_value = override_val;
            has_override = true;
        }
    }

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 60) {
        if (has_override) {
            LogInfo("NGX Parameter SetUI called - Name: %s, Game Value: %u, Override: %u", InName ? InName : "null", original_value, override_value);
        } else {
            LogInfo("NGX Parameter SetUI called - Name: %s, Value: %u", InName ? InName : "null", InValue);
        }
        log_count++;
    }

    // Call original function with override value if present
    if (NVSDK_NGX_Parameter_SetUI_Original != nullptr) {
        NVSDK_NGX_Parameter_SetUI_Original(InParameter, InName, override_value);
    }
}

// Hooked NVSDK_NGX_Parameter_SetULL function
void NVSDK_CONV NVSDK_NGX_Parameter_SetULL_Detour(NVSDK_NGX_Parameter* InParameter, const char* InName,
                                                  unsigned long long InValue) {
    // Increment NGX counters
    g_ngx_counters.parameter_setull_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

    // Store parameter in thread-safe storage (store original game value)
    if (InName != nullptr) {
        g_ngx_parameters.update_ull(std::string(InName), InValue);
    }

    // Check for override value
    unsigned long long override_value = InValue;
    bool has_override = false;
    if (InName != nullptr) {
        uint64_t override_val;
        if (g_ngx_parameter_overrides.get_as_ull(std::string(InName), override_val)) {
            override_value = override_val;
            has_override = true;
        }
    }

    // Log the call (first few times only)
    static int log_count = 0;
    if (log_count < 60) {
        if (has_override) {
            LogInfo("NGX Parameter SetULL called - Name: %s, Game Value: %llu, Override: %llu", InName ? InName : "null", InValue, override_value);
        } else {
            LogInfo("NGX Parameter SetULL called - Name: %s, Value: %llu", InName ? InName : "null", InValue);
        }
        log_count++;
    }

    // Call original function with override value if present
    if (NVSDK_NGX_Parameter_SetULL_Original != nullptr) {
        NVSDK_NGX_Parameter_SetULL_Original(InParameter, InName, override_value);
    }
}

// Hooked NVSDK_NGX_Parameter_GetI function
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetI_Detour(NVSDK_NGX_Parameter* InParameter, const char* InName,
                                                            int* OutValue) {
    // Increment NGX counters
    g_ngx_counters.parameter_geti_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

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
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetUI_Detour(NVSDK_NGX_Parameter* InParameter, const char* InName,
                                                             unsigned int* OutValue) {
    // Increment NGX counters
    g_ngx_counters.parameter_getui_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

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
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetULL_Detour(NVSDK_NGX_Parameter* InParameter, const char* InName,
                                                              unsigned long long* OutValue) {
    // Increment NGX counters
    g_ngx_counters.parameter_getull_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

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
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetVoidPointer_Detour(NVSDK_NGX_Parameter* InParameter,
                                                                      const char* InName, void** OutValue) {
    // Increment NGX counters
    g_ngx_counters.parameter_getvoidpointer_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

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
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_Init_Detour(unsigned long long InApplicationId,
                                                        const wchar_t* InApplicationDataPath, ID3D12Device* InDevice,
                                                        const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                                        NVSDK_NGX_Version InSDKVersion) {
    // Increment NGX counters
    g_ngx_counters.d3d12_init_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

    LogInfo("NGX D3D12 Init called - AppId: %llu", InApplicationId);

    if (NVSDK_NGX_D3D12_Init_Original != nullptr) {
        return NVSDK_NGX_D3D12_Init_Original(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo,
                                             InSDKVersion);
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D12 Init Ext detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_Init_Ext_Detour(unsigned long long InApplicationId,
                                                            const wchar_t* InApplicationDataPath,
                                                            ID3D12Device* InDevice,
                                                            const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                                            void* Unknown5) {
    // Increment NGX counters
    g_ngx_counters.d3d12_init_ext_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

    LogInfo("NGX D3D12 Init Ext called - AppId: %llu", InApplicationId);

    if (NVSDK_NGX_D3D12_Init_Ext_Original != nullptr) {
        return NVSDK_NGX_D3D12_Init_Ext_Original(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo,
                                                 Unknown5);
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D12 Init ProjectID detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_Init_ProjectID_Detour(
    const char* InProjectId, NVSDK_NGX_EngineType InEngineType, const char* InEngineVersion,
    const wchar_t* InApplicationDataPath, ID3D12Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
    NVSDK_NGX_Version InSDKVersion) {
    // Increment NGX counters
    g_ngx_counters.d3d12_init_projectid_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

    LogInfo("NGX D3D12 Init ProjectID called - ProjectId: %s", InProjectId ? InProjectId : "null");

    if (NVSDK_NGX_D3D12_Init_ProjectID_Original != nullptr) {
        return NVSDK_NGX_D3D12_Init_ProjectID_Original(InProjectId, InEngineType, InEngineVersion,
                                                       InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion);
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D12 CreateFeature detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_CreateFeature_Detour(ID3D12GraphicsCommandList* InCmdList,
                                                                 NVSDK_NGX_Feature InFeatureID,
                                                                 NVSDK_NGX_Parameter* InParameters,
                                                                 NVSDK_NGX_Handle** OutHandle) {
    // Increment NGX counters
    g_ngx_counters.d3d12_createfeature_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

    LogInfo("NGX D3D12 CreateFeature called - FeatureID: %d", InFeatureID);

    // Hook the parameter vtable if we have parameters
    if (InParameters != nullptr) {
        HookNGXParameterVTable(InParameters);
    }

    if (NVSDK_NGX_D3D12_CreateFeature_Original != nullptr) {
        auto res = NVSDK_NGX_D3D12_CreateFeature_Original(InCmdList, InFeatureID, InParameters, OutHandle);

        // Track the handle and feature type if creation was successful
        if (res == NVSDK_NGX_Result_Success && OutHandle != nullptr && *OutHandle != nullptr) {
            TrackNGXHandle(*OutHandle, InFeatureID);
        }
        return res;
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D12 ReleaseFeature detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_ReleaseFeature_Detour(NVSDK_NGX_Handle* InHandle) {
    // Increment NGX counters
    g_ngx_counters.d3d12_releasefeature_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

    // Get feature type before releasing to log which feature is being released
    NVSDK_NGX_Feature feature = GetFeatureFromHandle(InHandle);
    if (feature != static_cast<NVSDK_NGX_Feature>(-1)) {
        const char* featureName = "Unknown";
        switch (feature) {
            case NVSDK_NGX_Feature_SuperSampling:     featureName = "DLSS Super Resolution"; break;
            case NVSDK_NGX_Feature_FrameGeneration:   featureName = "DLSS Frame Generation"; break;
            case NVSDK_NGX_Feature_RayReconstruction: featureName = "Ray Reconstruction"; break;
        }
        LogInfo("NGX D3D12 ReleaseFeature called - Releasing %s", featureName);
    } else {
        LogInfo("NGX D3D12 ReleaseFeature called - Unknown feature handle");
    }

    if (NVSDK_NGX_D3D12_ReleaseFeature_Original != nullptr) {
        auto result = NVSDK_NGX_D3D12_ReleaseFeature_Original(InHandle);

        // Untrack the handle after successful release
        if (result == NVSDK_NGX_Result_Success) {
            UntrackNGXHandle(InHandle);
        }

        return result;
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D12 EvaluateFeature detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_EvaluateFeature_Detour(ID3D12GraphicsCommandList* InCmdList,
                                                                   const NVSDK_NGX_Handle* InFeatureHandle,
                                                                   const NVSDK_NGX_Parameter* InParameters,
                                                                   PFN_NVSDK_NGX_ProgressCallback InCallback) {
    // Increment NGX counters
    g_ngx_counters.d3d12_evaluatefeature_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

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
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D11_Init_Detour(unsigned long long InApplicationId,
                                                        const wchar_t* InApplicationDataPath, ID3D11Device* InDevice,
                                                        const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                                        NVSDK_NGX_Version InSDKVersion) {
    // Increment NGX counters
    g_ngx_counters.d3d11_init_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

    LogInfo("NGX D3D11 Init called - AppId: %llu", InApplicationId);

    if (NVSDK_NGX_D3D11_Init_Original != nullptr) {
        return NVSDK_NGX_D3D11_Init_Original(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo,
                                             InSDKVersion);
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D11 Init Ext detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D11_Init_Ext_Detour(unsigned long long InApplicationId,
                                                            const wchar_t* InApplicationDataPath,
                                                            ID3D11Device* InDevice,
                                                            const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                                            void* Unknown5) {
    // Increment NGX counters
    g_ngx_counters.d3d11_init_ext_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

    LogInfo("NGX D3D11 Init Ext called - AppId: %llu", InApplicationId);

    if (NVSDK_NGX_D3D11_Init_Ext_Original != nullptr) {
        return NVSDK_NGX_D3D11_Init_Ext_Original(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo,
                                                 Unknown5);
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D11 Init ProjectID detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D11_Init_ProjectID_Detour(
    const char* InProjectId, NVSDK_NGX_EngineType InEngineType, const char* InEngineVersion,
    const wchar_t* InApplicationDataPath, ID3D11Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
    NVSDK_NGX_Version InSDKVersion) {
    // Increment NGX counters
    g_ngx_counters.d3d11_init_projectid_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

    LogInfo("NGX D3D11 Init ProjectID called - ProjectId: %s", InProjectId ? InProjectId : "null");

    if (NVSDK_NGX_D3D11_Init_ProjectID_Original != nullptr) {
        return NVSDK_NGX_D3D11_Init_ProjectID_Original(InProjectId, InEngineType, InEngineVersion,
                                                       InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion);
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D11 CreateFeature detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D11_CreateFeature_Detour(ID3D11DeviceContext* InDevCtx,
                                                                 NVSDK_NGX_Feature InFeatureID,
                                                                 NVSDK_NGX_Parameter* InParameters,
                                                                 NVSDK_NGX_Handle** OutHandle) {
    // Increment NGX counters
    g_ngx_counters.d3d11_createfeature_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

    LogInfo("NGX D3D11 CreateFeature called - FeatureID: %d", InFeatureID);

    // Hook the parameter vtable if we have parameters
    if (InParameters != nullptr) {
        HookNGXParameterVTable(InParameters);
    }

    if (NVSDK_NGX_D3D11_CreateFeature_Original != nullptr) {
        auto res = NVSDK_NGX_D3D11_CreateFeature_Original(InDevCtx, InFeatureID, InParameters, OutHandle);

        // Track the handle and feature type if creation was successful
        if (res == NVSDK_NGX_Result_Success && OutHandle != nullptr && *OutHandle != nullptr) {
            TrackNGXHandle(*OutHandle, InFeatureID);
        }

        return res;
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D11 ReleaseFeature detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D11_ReleaseFeature_Detour(NVSDK_NGX_Handle* InHandle) {
    // Increment NGX counters
    g_ngx_counters.d3d11_releasefeature_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

    // Get feature type before releasing to log which feature is being released
    NVSDK_NGX_Feature feature = GetFeatureFromHandle(InHandle);
    if (feature != static_cast<NVSDK_NGX_Feature>(-1)) {
        const char* featureName = "Unknown";
        switch (feature) {
            case NVSDK_NGX_Feature_SuperSampling:     featureName = "DLSS Super Resolution"; break;
            case NVSDK_NGX_Feature_FrameGeneration:   featureName = "DLSS Frame Generation"; break;
            case NVSDK_NGX_Feature_RayReconstruction: featureName = "Ray Reconstruction"; break;
        }
        LogInfo("NGX D3D11 ReleaseFeature called - Releasing %s", featureName);
    } else {
        LogInfo("NGX D3D11 ReleaseFeature called - Unknown feature handle");
    }

    if (NVSDK_NGX_D3D11_ReleaseFeature_Original != nullptr) {
        auto result = NVSDK_NGX_D3D11_ReleaseFeature_Original(InHandle);

        // Untrack the handle after successful release
        if (result == NVSDK_NGX_Result_Success) {
            UntrackNGXHandle(InHandle);
        }

        return result;
    }

    return NVSDK_NGX_Result_Fail;
}

// D3D11 EvaluateFeature detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D11_EvaluateFeature_Detour(ID3D11DeviceContext* InDevCtx,
                                                                   const NVSDK_NGX_Handle* InFeatureHandle,
                                                                   const NVSDK_NGX_Parameter* InParameters,
                                                                   PFN_NVSDK_NGX_ProgressCallback InCallback) {
    // Increment NGX counters
    g_ngx_counters.d3d11_evaluatefeature_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);


    //LogInfo("NGX D3D11 EvaluateFeature called");

    // Hook the parameter vtable if we have parameters
    if (InParameters != nullptr) {
        HookNGXParameterVTable((NVSDK_NGX_Parameter*)InParameters);
    }

    if (NVSDK_NGX_D3D11_EvaluateFeature_Original != nullptr) {
        return NVSDK_NGX_D3D11_EvaluateFeature_Original(InDevCtx, InFeatureHandle, InParameters, InCallback);
    }

    return NVSDK_NGX_Result_Fail;
}

// UpdateFeature detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_UpdateFeature_Detour(const NVSDK_NGX_Application_Identifier* ApplicationId,
                                                           const NVSDK_NGX_Feature FeatureID) {
    // Increment NGX counters
    g_ngx_counters.total_count.fetch_add(1);

    // Log the UpdateFeature call with feature details
    const char* featureName = GetNGXFeatureName(FeatureID);
    LogInfo("NGX UpdateFeature called - FeatureID: %d (%s)", FeatureID, featureName);

    // Log application ID if available
    if (ApplicationId != nullptr) {
        if (ApplicationId->IdentifierType == NVSDK_NGX_Application_Identifier_Type_Application_Id) {
            LogInfo("NGX UpdateFeature - ApplicationId: %llu", ApplicationId->v.ApplicationId);
        } else if (ApplicationId->IdentifierType == NVSDK_NGX_Application_Identifier_Type_Project_Id) {
            LogInfo("NGX UpdateFeature - ProjectId: %s", ApplicationId->v.ProjectDesc.ProjectId);
        }
    }

    // TODO: Implement UpdateFeature suppression on crash
    // This function can cause crashes in some games, consider adding:
    // 1. Crash detection around the call
    // 2. Automatic suppression if crashes are detected
    // 3. User-configurable option to disable UpdateFeature calls
    LogInfo("TODO: Consider implementing UpdateFeature suppression on crash detection");

    if (NVSDK_NGX_UpdateFeature_Original != nullptr) {
        return NVSDK_NGX_UpdateFeature_Original(ApplicationId, FeatureID);
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
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_SetI_Original), "NVSDK_NGX_Parameter_SetI");

    // Hook SetUI (vtable index 4)
    CreateAndEnableHook(vftable[4], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_SetUI_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_SetUI_Original), "NVSDK_NGX_Parameter_SetUI");
    // Hook SetD (vtable index 5)
    CreateAndEnableHook(vftable[5], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_SetD_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_SetD_Original), "NVSDK_NGX_Parameter_SetD");

    // Hook SetF (vtable index 6)
    CreateAndEnableHook(vftable[6], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_SetF_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_SetF_Original), "NVSDK_NGX_Parameter_SetF");

    // Hook SetULL (vtable index 7)
    CreateAndEnableHook(vftable[7], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_SetULL_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_SetULL_Original), "NVSDK_NGX_Parameter_SetULL");
    // Hook GetVoidPointer (vtable index 8) - Special-K uses index 8
    CreateAndEnableHook(vftable[8], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_GetVoidPointer_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_GetVoidPointer_Original),
                        "NVSDK_NGX_Parameter_GetVoidPointer");
    // Hook GetI (vtable index 11) - Special-K uses index 11
    CreateAndEnableHook(vftable[11], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_GetI_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_GetI_Original), "NVSDK_NGX_Parameter_GetI");
    // Hook GetUI (vtable index 12) - Special-K uses index 12
    CreateAndEnableHook(vftable[12], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_GetUI_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_GetUI_Original), "NVSDK_NGX_Parameter_GetUI");
    // Hook GetULL (vtable index 15) - Special-K uses index 15
    CreateAndEnableHook(vftable[15], reinterpret_cast<LPVOID>(NVSDK_NGX_Parameter_GetULL_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_Parameter_GetULL_Original), "NVSDK_NGX_Parameter_GetULL");

    g_ngx_vtable_hooks_installed = true;
    LogInfo("NGX Parameter vtable hooks installed successfully");
    return true;
}

// NGX D3D12 GetParameters detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_GetParameters_Detour(NVSDK_NGX_Parameter** InParameters) {
    // Increment NGX counters
    g_ngx_counters.d3d12_getparameters_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

    NVSDK_NGX_Result ret = NVSDK_NGX_Result_Fail;

    if (NVSDK_NGX_D3D12_GetParameters_Original != nullptr) {
        ret = NVSDK_NGX_D3D12_GetParameters_Original(InParameters);
    }

    if (ret == NVSDK_NGX_Result_Success && InParameters != nullptr && *InParameters != nullptr) {
        HookNGXParameterVTable(*InParameters);
        // Store parameter object for direct API calls
        g_last_ngx_parameter.store(*InParameters);
        // Apply DLSS preset parameters during initialization
        ApplyDLSSPresetParameters(*InParameters);
    }

    return ret;
}

// NGX D3D12 AllocateParameters detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_AllocateParameters_Detour(NVSDK_NGX_Parameter** InParameters) {
    // Increment NGX counters
    g_ngx_counters.d3d12_allocateparameters_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

    NVSDK_NGX_Result ret = NVSDK_NGX_Result_Fail;

    if (NVSDK_NGX_D3D12_AllocateParameters_Original != nullptr) {
        ret = NVSDK_NGX_D3D12_AllocateParameters_Original(InParameters);
    }

    if (ret == NVSDK_NGX_Result_Success && InParameters != nullptr && *InParameters != nullptr) {
        HookNGXParameterVTable(*InParameters);
        // Store parameter object for direct API calls
        g_last_ngx_parameter.store(*InParameters);
        // Apply DLSS preset parameters during initialization
        ApplyDLSSPresetParameters(*InParameters);
    }

    return ret;
}

// NGX D3D11 GetParameters detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D11_GetParameters_Detour(NVSDK_NGX_Parameter** InParameters) {
    // Increment NGX counters
    g_ngx_counters.d3d11_getparameters_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

    NVSDK_NGX_Result ret = NVSDK_NGX_Result_Fail;

    if (NVSDK_NGX_D3D11_GetParameters_Original != nullptr) {
        ret = NVSDK_NGX_D3D11_GetParameters_Original(InParameters);
    }

    if (ret == NVSDK_NGX_Result_Success && InParameters != nullptr && *InParameters != nullptr) {
        HookNGXParameterVTable(*InParameters);
        // Store parameter object for direct API calls
        g_last_ngx_parameter.store(*InParameters);
        // Apply DLSS preset parameters during initialization
        ApplyDLSSPresetParameters(*InParameters);
    }

    return ret;
}

// NGX D3D11 AllocateParameters detour
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D11_AllocateParameters_Detour(NVSDK_NGX_Parameter** InParameters) {
    // Increment NGX counters
    g_ngx_counters.d3d11_allocateparameters_count.fetch_add(1);
    g_ngx_counters.total_count.fetch_add(1);

    NVSDK_NGX_Result ret = NVSDK_NGX_Result_Fail;

    if (NVSDK_NGX_D3D11_AllocateParameters_Original != nullptr) {
        ret = NVSDK_NGX_D3D11_AllocateParameters_Original(InParameters);
    }

    if (ret == NVSDK_NGX_Result_Success && InParameters != nullptr && *InParameters != nullptr) {
        HookNGXParameterVTable(*InParameters);
        // Store parameter object for direct API calls
        g_last_ngx_parameter.store(*InParameters);
        // Apply DLSS preset parameters during initialization
        ApplyDLSSPresetParameters(*InParameters);
    }

    return ret;
}

// Install NGX hooks
bool InstallNGXHooks(HMODULE ngx_module) {
    if (!settings::g_developerTabSettings.load_nvngx.GetValue()) {
        LogInfo("NGX hooks not installed - load_nvngx is disabled");
        return false;
    }

    // Check if NGX hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::NGX)) {
        LogInfo("NGX hooks installation suppressed by user setting");
        return false;
    }

    // Check if NGX DLLs are loaded
    HMODULE ngx_dll = ngx_module;
    if (ngx_dll == nullptr) {
        ngx_dll = GetModuleHandleA("_nvngx.dll");
        if (!ngx_dll) {
            LogInfo("NGX hooks: _nvngx.dll not loaded");
            return false;
        }
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
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D12_Init_Original), "NVSDK_NGX_D3D12_Init");

    CreateAndEnableHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D12_Init_Ext"),
                        reinterpret_cast<LPVOID>(NVSDK_NGX_D3D12_Init_Ext_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D12_Init_Ext_Original), "NVSDK_NGX_D3D12_Init_Ext");

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
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D11_Init_Original), "NVSDK_NGX_D3D11_Init");

    CreateAndEnableHook(GetProcAddress(ngx_dll, "NVSDK_NGX_D3D11_Init_Ext"),
                        reinterpret_cast<LPVOID>(NVSDK_NGX_D3D11_Init_Ext_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_D3D11_Init_Ext_Original), "NVSDK_NGX_D3D11_Init_Ext");

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

    // Hook UpdateFeature function
    CreateAndEnableHook(GetProcAddress(ngx_dll, "NVSDK_NGX_UpdateFeature"),
                        reinterpret_cast<LPVOID>(NVSDK_NGX_UpdateFeature_Detour),
                        reinterpret_cast<LPVOID*>(&NVSDK_NGX_UpdateFeature_Original), "NVSDK_NGX_UpdateFeature");

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

    // Mark NGX hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::NGX);

    return true;
}

// Cleanup NGX hooks and reset tracking
void CleanupNGXHooks() {
    LogInfo("Cleaning up NGX hooks and handle tracking");
    CleanupNGXHandleTracking();
}

// Get NGX hook statistics - now using dedicated NGX counters
uint64_t GetNGXHookCount(int event_type) {
    // This function is deprecated - use g_ngx_counters directly
    return 0;
}

uint64_t GetTotalNGXHookCount() {
    // This function is deprecated - use g_ngx_counters.total_count directly
    return g_ngx_counters.total_count.load();
}

// Feature status checking functions
bool IsDLSSEnabled() { return g_dlss_enabled.load(); }

bool IsDLSSGEnabled() { return g_dlssg_enabled.load(); }

bool IsRayReconstructionEnabled() { return g_ray_reconstruction_enabled.load(); }

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

// Force apply NGX parameter override via API call
bool ApplyNGXParameterOverride(const char* param_name, const char* param_type) {
    if (param_name == nullptr || param_type == nullptr) {
        return false;
    }

    NVSDK_NGX_Parameter* param_obj = g_last_ngx_parameter.load();
    if (param_obj == nullptr) {
        return false;
    }

    ParameterValue current_override;
    if (!g_ngx_parameter_overrides.get(std::string(param_name), current_override)) {
        return false;
    }

    std::string type_str = param_type;
    if (type_str == "float") {
        if (NVSDK_NGX_Parameter_SetF_Original != nullptr) {
            NVSDK_NGX_Parameter_SetF_Original(param_obj, param_name, current_override.get_as_float());
            LogInfo("NGX Parameter Applied via API: %s = %f", param_name, current_override.get_as_float());
            return true;
        }
    } else if (type_str == "double") {
        if (NVSDK_NGX_Parameter_SetD_Original != nullptr) {
            NVSDK_NGX_Parameter_SetD_Original(param_obj, param_name, current_override.get_as_double());
            LogInfo("NGX Parameter Applied via API: %s = %f", param_name, current_override.get_as_double());
            return true;
        }
    } else if (type_str == "int") {
        if (NVSDK_NGX_Parameter_SetI_Original != nullptr) {
            NVSDK_NGX_Parameter_SetI_Original(param_obj, param_name, current_override.get_as_int());
            LogInfo("NGX Parameter Applied via API: %s = %d", param_name, current_override.get_as_int());
            return true;
        }
    } else if (type_str == "uint") {
        if (NVSDK_NGX_Parameter_SetUI_Original != nullptr) {
            NVSDK_NGX_Parameter_SetUI_Original(param_obj, param_name, current_override.get_as_uint());
            LogInfo("NGX Parameter Applied via API: %s = %u", param_name, current_override.get_as_uint());
            return true;
        }
    } else if (type_str == "ull") {
        if (NVSDK_NGX_Parameter_SetULL_Original != nullptr) {
            NVSDK_NGX_Parameter_SetULL_Original(param_obj, param_name, current_override.get_as_ull());
            LogInfo("NGX Parameter Applied via API: %s = %llu", param_name, current_override.get_as_ull());
            return true;
        }
    }

    return false;
}
