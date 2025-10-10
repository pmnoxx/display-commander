#include "reflex_manager.hpp"
#include "../globals.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../utils.hpp"
#include "utils/timing.hpp"

// Minimal helper to pull the native D3D device pointer from ReShade device
static IUnknown *GetNativeD3DDeviceFromReshade(reshade::api::device *device) {
    if (device == nullptr)
        return nullptr;
    const uint64_t native = device->get_native();
    return reinterpret_cast<IUnknown *>(native);
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

bool ReflexManager::Initialize(reshade::api::device *device) {
    if (initialized_.load(std::memory_order_acquire))
        return true;
    if (!EnsureNvApi())
        return false;

    d3d_device_ = GetNativeD3DDeviceFromReshade(device);
    if (d3d_device_ == nullptr) {
        LogWarn("Reflex: failed to get native D3D device");
        return false;
    }

    initialized_.store(true, std::memory_order_release);
    return true;
}

void ReflexManager::Shutdown() {
    if (!initialized_.exchange(false, std::memory_order_release))
        return;

    // Check if shutdown is in progress to avoid NVAPI calls during DLL unload
    extern std::atomic<bool> g_shutdown;
    if (g_shutdown.load()) {
        LogInfo("ReflexManager shutdown skipped - shutdown in progress");
        d3d_device_ = nullptr;
        return;
    }

    // Disable sleep mode by setting all parameters to false/disabled
    NV_SET_SLEEP_MODE_PARAMS params = {};
    params.version = NV_SET_SLEEP_MODE_PARAMS_VER;
    params.bLowLatencyMode = NV_FALSE;
    params.bLowLatencyBoost = NV_FALSE;
    params.bUseMarkersToOptimize = NV_FALSE;
    params.minimumIntervalUs = 0; // No frame rate limit

    NvAPI_D3D_SetSleepMode(d3d_device_, &params);
    d3d_device_ = nullptr;
}

bool ReflexManager::ApplySleepMode(bool low_latency, bool boost, bool use_markers) {
    if (!initialized_.load(std::memory_order_acquire) || d3d_device_ == nullptr)
        return false;

    // Check if shutdown is in progress to avoid NVAPI calls during DLL unload
    extern std::atomic<bool> g_shutdown;
    if (g_shutdown.load())
        return false;

    NV_SET_SLEEP_MODE_PARAMS params = {};
    params.version = NV_SET_SLEEP_MODE_PARAMS_VER;
    params.bLowLatencyMode = low_latency ? NV_TRUE : NV_FALSE;
    params.bLowLatencyBoost = boost ? NV_TRUE : NV_FALSE;
    params.bUseMarkersToOptimize = use_markers ? NV_TRUE : NV_FALSE;
    //  params.minimumIntervalUs = 0; // no explicit limiter in minimal integration
    double target_fps_limit = s_fps_limit.load();
    params.minimumIntervalUs =
        target_fps_limit > 0.0f ? (UINT)(round(1000000.0 / target_fps_limit)) : 0; // + (__SK_ForceDLSSGPacing ? 6 : 0);

    const auto st = NvAPI_D3D_SetSleepMode(d3d_device_, &params);
    if (st != NVAPI_OK) {
        LogWarn("Reflex: NvAPI_D3D_SetSleepMode failed (%d)", (int)st);
        return false;
    }
    return true;
}

NvU64 ReflexManager::IncreaseFrameId() { return frame_id_.fetch_add(1, std::memory_order_acq_rel); }

bool ReflexManager::SetMarker(NV_LATENCY_MARKER_TYPE marker) {
    if (!initialized_.load(std::memory_order_acquire) || d3d_device_ == nullptr)
        return false;

    // Check if shutdown is in progress to avoid NVAPI calls during DLL unload
    extern std::atomic<bool> g_shutdown;
    if (g_shutdown.load())
        return false;

    if (s_enable_reflex_logging.load()) {
        std::ostringstream oss;
        oss << utils::get_now_ns() % utils::SEC_TO_NS << " Reflex: SetMarker " << marker << " frame_id "
            << frame_id_.load(std::memory_order_acquire);
        LogInfo(oss.str().c_str());
    }

    NV_LATENCY_MARKER_PARAMS mp = {};
    mp.version = NV_LATENCY_MARKER_PARAMS_VER;
    mp.markerType = marker;
    mp.frameID = frame_id_.load(std::memory_order_acquire);

    const auto st = NvAPI_D3D_SetLatencyMarker(d3d_device_, &mp);
    if (st != NVAPI_OK) {
        // Don't spam logs each frame; minimal warning level
        return false;
    }
    return true;
}

bool ReflexManager::Sleep() {
    if (!initialized_.load(std::memory_order_acquire) || d3d_device_ == nullptr)
        return false;

    // Check if shutdown is in progress to avoid NVAPI calls during DLL unload
    extern std::atomic<bool> g_shutdown;
    if (g_shutdown.load())
        return false;

    const auto st = NvAPI_D3D_Sleep(d3d_device_);
    return st == NVAPI_OK;
}
