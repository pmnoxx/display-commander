#include "reflex_provider.hpp"
#include "../../../../external/Streamline/source/plugins/sl.pcl/pclstats.h"
#include "../globals.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../utils/logging.hpp"

// Static member initialization
bool ReflexProvider::_is_pcl_initialized = false;

ReflexProvider::ReflexProvider() = default;
ReflexProvider::~ReflexProvider() = default;

bool ReflexProvider::Initialize(reshade::api::device* device) { return reflex_manager_.Initialize(device); }

bool ReflexProvider::InitializeNative(void* native_device, DeviceTypeDC device_type) {
    return reflex_manager_.InitializeNative(native_device, device_type);
}

void ReflexProvider::Shutdown() {
    // Only shutdown PCLStats if it was initialized
    if (_is_pcl_initialized) {
        PCLSTATS_SHUTDOWN();
        _is_pcl_initialized = false;
    }
    reflex_manager_.Shutdown();
}

void ReflexProvider::EnsurePCLStatsInitialized() {
    // Only initialize if feature is enabled and not already initialized
    if (!_is_pcl_initialized && settings::g_mainTabSettings.pcl_stats_enabled.GetValue()) {
        PCLSTATS_INIT(0);
        _is_pcl_initialized = true;
    }
}

bool ReflexProvider::IsInitialized() const { return reflex_manager_.IsInitialized(); }

bool ReflexProvider::SetMarker(LatencyMarkerType marker) {
    if (!IsInitialized()) return false;
    static bool first_call = true;
    if (first_call) {
        first_call = false;
        LogInfo("ReflexProvider::SetMarker: First call");
    }

    // LatencyMarkerType is now NV_LATENCY_MARKER_TYPE, so no conversion needed
    return reflex_manager_.SetMarker(marker);
}

bool ReflexProvider::ApplySleepMode(bool low_latency, bool boost, bool use_markers, float fps_limit) {
    if (!IsInitialized()) return false;

    return reflex_manager_.ApplySleepMode(low_latency, boost, use_markers, fps_limit);
}

bool ReflexProvider::Sleep() {
    if (!IsInitialized()) return false;

    return reflex_manager_.Sleep();
}

bool ReflexProvider::GetSleepStatus(NV_GET_SLEEP_STATUS_PARAMS* status_params,
                                    SleepStatusUnavailableReason* out_reason) {
    if (!IsInitialized() || status_params == nullptr) return false;

    return reflex_manager_.GetSleepStatus(status_params, out_reason);
}
