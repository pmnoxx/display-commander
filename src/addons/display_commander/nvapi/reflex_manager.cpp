#include "reflex_manager.hpp"
#include "../globals.hpp"
#include "../hooks/hook_suppression_manager.hpp"
#include "../hooks/nvapi_hooks.hpp"
#include "../hooks/pclstats_etw_hooks.hpp"
#include "../latency/reflex_provider.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../swapchain_events.hpp"
#include "../utils.hpp"
#include "../utils/logging.hpp"
#include "nvapi_init.hpp"
#include "utils/timing.hpp"

// Include Streamline PCLStats header for PCLSTATS_MARKER macro
// Path is relative to src/addons/display_commander from external/Streamline
#include "../../../../external/Streamline/source/plugins/sl.pcl/pclstats.h"
// Minimal helper to pull the native D3D device pointer from ReShade device
static IUnknown* GetNativeD3DDeviceFromReshade(reshade::api::device* device) {
    if (device == nullptr) return nullptr;
    const uint64_t native = device->get_native();
    return reinterpret_cast<IUnknown*>(native);
}

bool ReflexManager::EnsureNvApi() {
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::NVAPI)) {
        return false;
    }
    static std::atomic<bool> g_nvapi_inited{false};
    if (!g_nvapi_inited.load(std::memory_order_acquire)) {
        if (!nvapi::EnsureNvApiInitialized()) {
            return false;
        }
        g_nvapi_inited.store(true, std::memory_order_release);
    }
    return true;
}

bool ReflexManager::Initialize(reshade::api::device* device) {
    if (initialized_.load(std::memory_order_acquire)) return true;
    if (!EnsureNvApi()) return false;

    d3d_device_ = GetNativeD3DDeviceFromReshade(device);
    if (d3d_device_ == nullptr) {
        LogWarn("Reflex: failed to get native D3D device");
        return false;
    }

    initialized_.store(true, std::memory_order_release);
    return true;
}

bool ReflexManager::InitializeNative(void* native_device, DeviceTypeDC device_type) {
    if (initialized_.load(std::memory_order_acquire)) return true;
    if (!EnsureNvApi()) return false;

    // Only support D3D11 and D3D12 for Reflex
    if (device_type != DeviceTypeDC::DX11 && device_type != DeviceTypeDC::DX12) {
        // Only log this warning once per session to avoid spam
        static std::atomic<bool> g_unsupported_device_type_warned{false};
        if (!g_unsupported_device_type_warned.exchange(true, std::memory_order_acq_rel)) {
            LogWarn("Reflex: Only D3D11 and D3D12 are supported, got device type %d", static_cast<int>(device_type));
        }
        return false;
    }

    d3d_device_ = static_cast<IUnknown*>(native_device);
    if (d3d_device_ == nullptr) {
        // Only log this warning once per session to avoid spam
        static std::atomic<bool> g_null_device_warned{false};
        if (!g_null_device_warned.exchange(true, std::memory_order_acq_rel)) {
            LogWarn("Reflex: native device is null");
        }
        return false;
    }

    initialized_.store(true, std::memory_order_release);
    return true;
}

void ReflexManager::Shutdown() {
    if (!initialized_.exchange(false, std::memory_order_release)) return;

    if (d3d_device_ != nullptr && nvapi::EnsureNvApiInitialized()) {
        // Disable sleep mode by setting all parameters to false/disabled
        NV_SET_SLEEP_MODE_PARAMS params = {};
        params.version = NV_SET_SLEEP_MODE_PARAMS_VER;
        params.bLowLatencyMode = NV_FALSE;
        params.bLowLatencyBoost = NV_FALSE;
        params.bUseMarkersToOptimize = NV_FALSE;
        params.minimumIntervalUs = 0;  // No frame rate limit

        NvAPI_D3D_SetSleepMode_Direct(d3d_device_, &params);
    }
    d3d_device_ = nullptr;
}

bool ReflexManager::ApplySleepMode(bool low_latency, bool boost, bool use_markers, float fps_limit) {
    if (g_global_frame_id.load(std::memory_order_acquire) < 500) {
        return true;
    }
    if (!initialized_.load(std::memory_order_acquire) || d3d_device_ == nullptr) return false;
    if (!EnsureNvApi()) return false;

    {
        static bool first_call = true;
        if (first_call) {
            first_call = false;
            LogInfo("ReflexManager::ApplySleepMode: First call for frame %llu",
                    g_global_frame_id.load(std::memory_order_acquire));
        }
    }

    NV_SET_SLEEP_MODE_PARAMS params = {};
    params.version = NV_SET_SLEEP_MODE_PARAMS_VER;
    params.bLowLatencyMode = low_latency ? NV_TRUE : NV_FALSE;
    params.bLowLatencyBoost = boost ? NV_TRUE : NV_FALSE;
    params.bUseMarkersToOptimize = use_markers ? NV_TRUE : NV_FALSE;
    params.minimumIntervalUs =
        fps_limit > 0.0f && ShouldUseReflexAsFpsLimiter() ? (UINT)(round(1000000.0 / fps_limit)) : 0;

    const auto st = NvAPI_D3D_SetSleepMode_Direct(d3d_device_, &params);
    if (st != NVAPI_OK) {
        LogWarn("Reflex: NvAPI_D3D_SetSleepMode_Direct failed (%d)", (int)st);
        return false;
    }
    g_last_reflex_params_set_by_addon.store(std::make_shared<NV_SET_SLEEP_MODE_PARAMS>(params));
    return true;
}

bool ReflexManager::SetMarker(NV_LATENCY_MARKER_TYPE marker) {
    if (g_global_frame_id.load(std::memory_order_acquire) < 500) {
        return true;
    }
    if (!initialized_.load(std::memory_order_acquire) || d3d_device_ == nullptr) return false;
    if (!EnsureNvApi()) return false;
    {
        static bool first_call = true;
        if (first_call) {
            first_call = false;
            LogInfo("ReflexManager::SetMarker: First call for frame %llu",
                    g_global_frame_id.load(std::memory_order_acquire));
        }
    }

    if (s_enable_reflex_logging.load()) {
        std::ostringstream oss;
        oss << utils::get_now_ns() % utils::SEC_TO_NS << " Reflex: SetMarker " << marker << " frame_id "
            << g_global_frame_id.load(std::memory_order_acquire);
        LogInfo(oss.str().c_str());
    }

    // Initialize structure: zero-initialize all fields, then set required values
    // This matches NVAPI best practices and Special-K behavior
    NV_LATENCY_MARKER_PARAMS mp = {};
    mp.version = NV_LATENCY_MARKER_PARAMS_VER;
    mp.frameID = g_global_frame_id.load(std::memory_order_acquire);
    mp.markerType = marker;
    // Reserved fields (rsvd0 and rsvd[56]) are zero-initialized by = {}
    // Explicitly zero rsvd0 for clarity (though = {} already handles it)
    mp.rsvd0 = 0;
    if (PCLStatsReportingEnabled()) {
        // Ensure PCLSTATS is initialized if setting is enabled (lazy initialization)
        ReflexProvider::EnsurePCLStatsInitialized();
        RecordPCLStatsMarkerCall();
        PCLSTATS_MARKER(static_cast<PCLSTATS_LATENCY_MARKER_TYPE>(marker), static_cast<uint64_t>(mp.frameID));
    }
    if (marker == PC_LATENCY_PING) {
        return true;
    }

    const auto st = NvAPI_D3D_SetLatencyMarker_Direct(d3d_device_, &mp);
    if (st != NVAPI_OK) {
        // Don't spam logs each frame; minimal warning level
        return false;
    }

    // Emit PCLStats marker (ETW) using the same marker type / frame id we sent to NVAPI.
    // Only emit if PCL stats reporting is enabled
    return true;
}

bool ReflexManager::Sleep() {
    if (g_global_frame_id.load(std::memory_order_acquire) < 500) {
        return true;
    }
    if (!initialized_.load(std::memory_order_acquire) || d3d_device_ == nullptr) return false;
    if (!EnsureNvApi()) return false;
    {
        static bool first_call = true;
        if (first_call) {
            first_call = false;
            LogInfo("ReflexManager::Sleep: First call for frame %llu",
                    g_global_frame_id.load(std::memory_order_acquire));
        }
    }

    // Check if Reflex sleep suppression is enabled
    if (settings::g_mainTabSettings.suppress_reflex_sleep.GetValue()) {
        return true;  // Return success without actually calling sleep
    }

    const auto st = NvAPI_D3D_Sleep_Direct(d3d_device_);
    return st == NVAPI_OK;
}

bool ReflexManager::GetSleepStatus(NV_GET_SLEEP_STATUS_PARAMS* status_params,
                                   SleepStatusUnavailableReason* out_reason) {
    if (status_params == nullptr) {
        return false;
    }
    if (!initialized_.load(std::memory_order_acquire)) {
        if (out_reason) *out_reason = SleepStatusUnavailableReason::kReflexNotInitialized;
        return false;
    }
    if (d3d_device_ == nullptr) {
        if (out_reason) *out_reason = SleepStatusUnavailableReason::kNoD3DDevice;
        return false;
    }
    if (!EnsureNvApi()) {
        if (out_reason) *out_reason = SleepStatusUnavailableReason::kNvApiError;
        return false;
    }

    // Initialize the structure
    *status_params = {};
    status_params->version = NV_GET_SLEEP_STATUS_PARAMS_VER;

    const auto st = NvAPI_D3D_GetSleepStatus_Direct(d3d_device_, status_params);
    if (st == NVAPI_OK) {
        return true;
    }
    if (out_reason) {
        *out_reason = (st == NVAPI_NO_IMPLEMENTATION) ? SleepStatusUnavailableReason::kNvApiFunctionUnavailable
                                                      : SleepStatusUnavailableReason::kNvApiError;
    }
    return false;
}

// params may be nullptr if no parameters were stored
void ReflexManager::RestoreSleepMode(IUnknown* d3d_device_, NV_SET_SLEEP_MODE_PARAMS* params) {
    if (g_global_frame_id.load(std::memory_order_acquire) < 500) {
        return;
    }
    if (d3d_device_ == nullptr) {
        return;  // No device to restore on (e.g. game never called SetSleepMode). Calling NVAPI with null device
                 // crashes in dxgi.
    }
    if (!nvapi::EnsureNvApiInitialized()) {
        return;
    }
    NV_SET_SLEEP_MODE_PARAMS default_params = {};
    if (params == nullptr) {
        default_params.version = NV_SET_SLEEP_MODE_PARAMS_VER;
        default_params.bLowLatencyMode = NV_FALSE;
        default_params.bLowLatencyBoost = NV_FALSE;
        default_params.bUseMarkersToOptimize = NV_FALSE;
        default_params.minimumIntervalUs = 0;
        params = &default_params;
    }
    NvAPI_D3D_SetSleepMode_Direct(d3d_device_, params);
}
