#pragma once

#include "../nvapi/reflex_manager.hpp"
#include "latency_manager.hpp"


// NVIDIA Reflex implementation of ILatencyProvider
class ReflexProvider : public ILatencyProvider {
  public:
    ReflexProvider();
    ~ReflexProvider() override;

    // ILatencyProvider interface
    bool Initialize(reshade::api::device *device) override;
    void Shutdown() override;
    bool IsInitialized() const override;

    uint64_t IncreaseFrameId() override;
    bool SetMarker(LatencyMarkerType marker) override;
    bool ApplySleepMode(bool low_latency, bool boost, bool use_markers) override;
    bool Sleep() override;

    LatencyTechnology GetTechnology() const override { return LatencyTechnology::NVIDIA_Reflex; }
    const char *GetTechnologyName() const override { return "NVIDIA Reflex"; }

  private:
    ReflexManager reflex_manager_;

    // Convert LatencyMarkerType to NV_LATENCY_MARKER_TYPE
    NV_LATENCY_MARKER_TYPE ConvertMarkerType(LatencyMarkerType marker) const;
};
