#include "reflex_manager.hpp"
#include "../globals.hpp"
#include "../utils.hpp"

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

void ReflexManager::Shutdown() {
    initialized_.store(false, std::memory_order_release);
    d3d_device_ = nullptr;
}

bool ReflexManager::ApplySleepMode(bool low_latency, bool boost, bool use_markers) {
    if (!initialized_.load(std::memory_order_acquire) || d3d_device_ == nullptr) return false;

    NV_SET_SLEEP_MODE_PARAMS params = {};
    params.version = NV_SET_SLEEP_MODE_PARAMS_VER;
    params.bLowLatencyMode = low_latency ? NV_TRUE : NV_FALSE;
    params.bLowLatencyBoost = boost ? NV_TRUE : NV_FALSE;
    params.bUseMarkersToOptimize = use_markers ? NV_TRUE : NV_FALSE;
    params.minimumIntervalUs = 0; // no explicit limiter in minimal integration

    const auto st = NvAPI_D3D_SetSleepMode(d3d_device_, &params);
    if (st != NVAPI_OK) {
        LogWarn("Reflex: NvAPI_D3D_SetSleepMode failed (%d)", (int)st);
        return false;
    }
    return true;
}

bool ReflexManager::SetMarker(NV_LATENCY_MARKER_TYPE marker) {
    if (!initialized_.load(std::memory_order_acquire) || d3d_device_ == nullptr) return false;

    NV_LATENCY_MARKER_PARAMS mp = {};
    mp.version = NV_LATENCY_MARKER_PARAMS_VER;
    mp.markerType = marker;
    mp.frameID = frame_id_.fetch_add(1, std::memory_order_acq_rel);

    const auto st = NvAPI_D3D_SetLatencyMarker(d3d_device_, &mp);
    if (st != NVAPI_OK) {
        // Don't spam logs each frame; minimal warning level
        return false;
    }
    return true;
}

bool ReflexManager::SleepOnce() {
    if (!initialized_.load(std::memory_order_acquire) || d3d_device_ == nullptr) return false;
    const auto st = NvAPI_D3D_Sleep(d3d_device_);
    return st == NVAPI_OK;
}


