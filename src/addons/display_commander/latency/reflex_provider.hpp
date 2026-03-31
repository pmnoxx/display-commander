#pragma once

#include "../nvapi/reflex_manager.hpp"

// Libraries <standard C++>
#include <cstddef>
#include <cstdint>
#include <vector>

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

    struct NvapiLatencyMetrics {
        double pc_latency_ms = 0.0;
        double gpu_frame_time_ms = 0.0;
        uint64_t frame_id = 0;
    };

    struct NvapiLatencyFrame {
        uint64_t frame_id = 0;
        uint64_t input_sample_time_ns = 0;
        uint64_t sim_start_time_ns = 0;
        uint64_t sim_end_time_ns = 0;
        uint64_t render_submit_start_time_ns = 0;
        uint64_t render_submit_end_time_ns = 0;
        uint64_t present_start_time_ns = 0;
        uint64_t present_end_time_ns = 0;
        uint64_t driver_start_time_ns = 0;
        uint64_t driver_end_time_ns = 0;
        uint64_t os_render_queue_start_time_ns = 0;
        uint64_t os_render_queue_end_time_ns = 0;
        uint64_t gpu_render_start_time_ns = 0;
        uint64_t gpu_render_end_time_ns = 0;
        uint32_t gpu_frame_time_us = 0;
    };

    // Query NVAPI Reflex latency metrics (PC latency and GPU frame time) for the most recent frame.
    // Returns false when Reflex latency reporting is unavailable or on error.
    bool GetLatencyMetrics(NvapiLatencyMetrics& out_metrics);
    bool GetRecentLatencyFrames(std::vector<NvapiLatencyFrame>& out_frames, std::size_t max_frames = 10);

    static void EnsurePCLStatsInitialized();
    static bool IsPCLStatsInitialized();

   private:
    ReflexManager reflex_manager_;
    static bool _is_pcl_initialized;
};
