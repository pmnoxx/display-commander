#include "reflex_manager.hpp"
#include "../globals.hpp"
#include "../hooks/nvapi_hooks.hpp"
#include "../latency/pclstats_etw.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../utils.hpp"
#include "../utils/logging.hpp"
#include "utils/timing.hpp"

// Minimal helper to pull the native D3D device pointer from ReShade device
static IUnknown* GetNativeD3DDeviceFromReshade(reshade::api::device* device) {
    if (device == nullptr) return nullptr;
    const uint64_t native = device->get_native();
    return reinterpret_cast<IUnknown*>(native);
}

bool ReflexManager::EnsureNvApi() {
    static std::atomic<bool> g_nvapi_inited{false};
    if (!g_nvapi_inited.load(std::memory_order_acquire)) {
        if (NvAPI_Initialize() != NVAPI_OK) {
            LogWarn("NVAPI Initialize failed for Reflex");
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
        LogWarn("Reflex: Only D3D11 and D3D12 are supported, got device type %d", static_cast<int>(device_type));
        return false;
    }

    d3d_device_ = static_cast<IUnknown*>(native_device);
    if (d3d_device_ == nullptr) {
        LogWarn("Reflex: native device is null");
        return false;
    }

    initialized_.store(true, std::memory_order_release);
    return true;
}

void ReflexManager::Shutdown() {
    if (!initialized_.exchange(false, std::memory_order_release)) return;

    // Disable sleep mode by setting all parameters to false/disabled
    NV_SET_SLEEP_MODE_PARAMS params = {};
    params.version = NV_SET_SLEEP_MODE_PARAMS_VER;
    params.bLowLatencyMode = NV_FALSE;
    params.bLowLatencyBoost = NV_FALSE;
    params.bUseMarkersToOptimize = NV_FALSE;
    params.minimumIntervalUs = 0;  // No frame rate limit

    NvAPI_D3D_SetSleepMode_Direct(d3d_device_, &params);
    d3d_device_ = nullptr;
}

bool ReflexManager::ApplySleepMode(bool low_latency, bool boost, bool use_markers, float fps_limit) {
    if (!initialized_.load(std::memory_order_acquire) || d3d_device_ == nullptr) return false;

    NV_SET_SLEEP_MODE_PARAMS params = {};
    params.version = NV_SET_SLEEP_MODE_PARAMS_VER;
    params.bLowLatencyMode = low_latency ? NV_TRUE : NV_FALSE;
    params.bLowLatencyBoost = boost ? NV_TRUE : NV_FALSE;
    params.bUseMarkersToOptimize = NV_FALSE;
    params.minimumIntervalUs = fps_limit > 0.0f ? (UINT)(round(1000000.0 / fps_limit)) : 0;

    const auto st = NvAPI_D3D_SetSleepMode_Direct(d3d_device_, &params);
    if (st != NVAPI_OK) {
        LogWarn("Reflex: NvAPI_D3D_SetSleepMode_Direct failed (%d)", (int)st);
        return false;
    }
    return true;
}

bool ReflexManager::SetMarker(NV_LATENCY_MARKER_TYPE marker) {
    if (!initialized_.load(std::memory_order_acquire) || d3d_device_ == nullptr) return false;

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

    const auto st = NvAPI_D3D_SetLatencyMarker_Direct(d3d_device_, &mp);
    if (st != NVAPI_OK) {
        // Don't spam logs each frame; minimal warning level
        return false;
    }

    // Emit PCLStats marker (ETW) using the same marker type / frame id we sent to NVAPI.
    latency::pclstats_etw::EmitMarker(static_cast<uint32_t>(marker), static_cast<uint64_t>(mp.frameID));
    return true;
}

bool ReflexManager::Sleep() {
    if (!initialized_.load(std::memory_order_acquire) || d3d_device_ == nullptr) return false;

    // Check if Reflex sleep suppression is enabled
    if (settings::g_mainTabSettings.suppress_reflex_sleep.GetValue()) {
        return true;  // Return success without actually calling sleep
    }

    const auto st = NvAPI_D3D_Sleep_Direct(d3d_device_);
    return st == NVAPI_OK;
}

// params may be nullptr if no parameters were stored
void ReflexManager::RestoreSleepMode(IUnknown* d3d_device_, NV_SET_SLEEP_MODE_PARAMS* params) {
    // unsted for params == nullptr
    NvAPI_D3D_SetSleepMode_Direct(d3d_device_, params);
}
