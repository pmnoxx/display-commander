#pragma once

#include "../nvapi/reflex_manager.hpp"

// NVIDIA Reflex provider (single-technology; no LatencyManager abstraction).
class ReflexProvider {
   public:
    ReflexProvider();
    ~ReflexProvider();

    bool Initialize(reshade::api::device* device);
    bool InitializeNative(void* native_device, DeviceTypeDC device_type);
    void Shutdown();
    bool IsInitialized() const;

    bool SetMarker(NV_LATENCY_MARKER_TYPE marker);
    bool ApplySleepMode(bool low_latency, bool boost, bool use_markers, float fps_limit);
    bool Sleep();

    void UpdateCachedSleepStatus();
    bool GetSleepStatus(NV_GET_SLEEP_STATUS_PARAMS* status_params,
                       SleepStatusUnavailableReason* out_reason = nullptr);

    static void EnsurePCLStatsInitialized();
    static bool IsPCLStatsInitialized();

   private:
    ReflexManager reflex_manager_;
    static bool _is_pcl_initialized;
};
