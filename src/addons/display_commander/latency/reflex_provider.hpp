#pragma once

#include "../nvapi/reflex_manager.hpp"
#include "latency_manager.hpp"

// NVIDIA Reflex implementation of ILatencyProvider
class ReflexProvider : public ILatencyProvider {
   public:
    ReflexProvider();
    ~ReflexProvider() override;

    // ILatencyProvider interface
    bool Initialize(reshade::api::device* device) override;
    bool InitializeNative(void* native_device, DeviceTypeDC device_type) override;
    void Shutdown() override;
    bool IsInitialized() const override;

    bool SetMarker(LatencyMarkerType marker) override;
    bool ApplySleepMode(bool low_latency, bool boost, bool use_markers, float fps_limit) override;
    bool Sleep() override;
    bool GetSleepStatus(NV_GET_SLEEP_STATUS_PARAMS* status_params) override;

    LatencyTechnology GetTechnology() const override { return LatencyTechnology::NVIDIA_Reflex; }
    const char* GetTechnologyName() const override { return "NVIDIA Reflex"; }

    // PCLStats initialization helper (lazy initialization)
    static void EnsurePCLStatsInitialized();

   private:
    ReflexManager reflex_manager_;
    static bool _is_pcl_initialized;
};
