#include "reflex_provider.hpp"
#include "../../../../external/Streamline/source/plugins/sl.pcl/pclstats.h"
#include "../globals.hpp"
#include "../utils/logging.hpp"

ReflexProvider::ReflexProvider() = default;
ReflexProvider::~ReflexProvider() = default;

bool ReflexProvider::Initialize(reshade::api::device* device) {
    PCLSTATS_INIT(0);
    return reflex_manager_.Initialize(device);
}

bool ReflexProvider::InitializeNative(void* native_device, DeviceTypeDC device_type) {
    PCLSTATS_INIT(0);
    return reflex_manager_.InitializeNative(native_device, device_type);
}

void ReflexProvider::Shutdown() {
    PCLSTATS_SHUTDOWN();
    reflex_manager_.Shutdown();
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

bool ReflexProvider::GetSleepStatus(NV_GET_SLEEP_STATUS_PARAMS* status_params) {
    if (!IsInitialized() || status_params == nullptr) return false;

    return reflex_manager_.GetSleepStatus(status_params);
}
