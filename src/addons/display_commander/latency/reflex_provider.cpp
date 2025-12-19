#include "reflex_provider.hpp"
#include "../utils/logging.hpp"
#include "../globals.hpp"

ReflexProvider::ReflexProvider() = default;
ReflexProvider::~ReflexProvider() = default;

bool ReflexProvider::Initialize(reshade::api::device *device) { return reflex_manager_.Initialize(device); }

bool ReflexProvider::InitializeNative(void* native_device, DeviceTypeDC device_type) {
    return reflex_manager_.InitializeNative(native_device, device_type);
}

void ReflexProvider::Shutdown() { reflex_manager_.Shutdown(); }

bool ReflexProvider::IsInitialized() const { return reflex_manager_.IsInitialized(); }


bool ReflexProvider::SetMarker(LatencyMarkerType marker) {
    if (!IsInitialized())
        return false;
    static bool first_call = true;
    if (first_call) {
        first_call = false;
        LogInfo("ReflexProvider::SetMarker: First call");
    }

    NV_LATENCY_MARKER_TYPE nv_marker = ConvertMarkerType(marker);
    return reflex_manager_.SetMarker(nv_marker);
}

bool ReflexProvider::ApplySleepMode(bool low_latency, bool boost, bool use_markers, float fps_limit) {
    if (!IsInitialized())
        return false;

    return reflex_manager_.ApplySleepMode(low_latency, boost, use_markers, fps_limit);
}

bool ReflexProvider::Sleep() {
    if (!IsInitialized())
        return false;

    return reflex_manager_.Sleep();
}

NV_LATENCY_MARKER_TYPE ReflexProvider::ConvertMarkerType(LatencyMarkerType marker) const {
    switch (marker) {
    case LatencyMarkerType::SIMULATION_START:
        return SIMULATION_START;
    case LatencyMarkerType::SIMULATION_END:
        return SIMULATION_END;
    case LatencyMarkerType::RENDERSUBMIT_START:
        return RENDERSUBMIT_START;
    case LatencyMarkerType::RENDERSUBMIT_END:
        return RENDERSUBMIT_END;
    case LatencyMarkerType::PRESENT_START:
        return PRESENT_START;
    case LatencyMarkerType::PRESENT_END:
        return PRESENT_END;
    case LatencyMarkerType::INPUT_SAMPLE:
        return INPUT_SAMPLE;
    default:
        return SIMULATION_START; // Fallback
    }
}
