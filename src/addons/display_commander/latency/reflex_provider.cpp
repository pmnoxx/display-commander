#include "reflex_provider.hpp"

ReflexProvider::ReflexProvider() = default;
ReflexProvider::~ReflexProvider() = default;

bool ReflexProvider::Initialize(reshade::api::device *device) { return reflex_manager_.Initialize(device); }

void ReflexProvider::Shutdown() { reflex_manager_.Shutdown(); }

bool ReflexProvider::IsInitialized() const { return reflex_manager_.IsInitialized(); }

uint64_t ReflexProvider::IncreaseFrameId() { return reflex_manager_.IncreaseFrameId(); }

bool ReflexProvider::SetMarker(LatencyMarkerType marker) {
    if (!IsInitialized())
        return false;

    NV_LATENCY_MARKER_TYPE nv_marker = ConvertMarkerType(marker);
    return reflex_manager_.SetMarker(nv_marker);
}

bool ReflexProvider::ApplySleepMode(bool low_latency, bool boost, bool use_markers) {
    if (!IsInitialized())
        return false;

    return reflex_manager_.ApplySleepMode(low_latency, boost, use_markers);
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
