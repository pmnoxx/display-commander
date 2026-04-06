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
        uint32_t gpu_active_render_time_us = 0;
    };

    // Query NVAPI Reflex latency metrics (PC latency and GPU frame time) for the most recent frame.
    // Returns false when Reflex latency reporting is unavailable or on error.
    bool GetLatencyMetrics(NvapiLatencyMetrics& out_metrics);
    bool GetRecentLatencyFrames(std::vector<NvapiLatencyFrame>& out_frames, std::size_t max_frames = 10);

    /** Fills raw NVAPI latency buffer (one NvAPI_D3D_GetLatency call). */
    bool GetLatencyParamsV1(NV_LATENCY_RESULT_PARAMS_V1& out_params);
    /** PC latency + GPU frame time averages from an already-fetched NVAPI buffer (no extra driver call). */
    static bool MetricsFromLatencyParams(const NV_LATENCY_RESULT_PARAMS_V1& params, NvapiLatencyMetrics& out_metrics);
    /** Newest frame by frameID: sim duration, GPU active time, OSD latency estimate (matches overlay Lat. + FG). */
    struct NvapiReflexNewestFrameDerived {
        uint64_t frame_id = 0;
        bool sim_duration_valid = false;
        double sim_duration_ms = 0.0;
        /** renderSubmitStartTime − simEndTime on newest frame (µs domain → ms). */
        bool sim_end_to_render_submit_start_valid = false;
        double sim_end_to_render_submit_start_ms = 0.0;
        /** renderSubmitEndTime − renderSubmitStartTime (µs → ms). */
        bool render_submit_phase_valid = false;
        double render_submit_phase_ms = 0.0;
        /** presentStartTime − renderSubmitEndTime (µs → ms). */
        bool rs_end_to_present_start_valid = false;
        double rs_end_to_present_start_ms = 0.0;
        /** presentEndTime − presentStartTime (µs → ms). */
        bool present_phase_valid = false;
        double present_phase_ms = 0.0;
        bool gpu_active_valid = false;
        double gpu_active_render_ms = 0.0;
        bool osd_latency_valid = false;
        double osd_latency_estimate_ms = 0.0;
    };
    static bool FillNewestFrameDerivedForOverlay(const NV_LATENCY_RESULT_PARAMS_V1& params, int dlss_fg_mode,
                                                 NvapiReflexNewestFrameDerived& out);

    static void EnsurePCLStatsInitialized();
    static bool IsPCLStatsInitialized();

    /** Writes PCLStatsEvent (ETW) and bumps g_pclstats_etw_* counters only after PCLSTATS_INIT (inject reflex + PCL stats on, g_global_frame_id > 500, no foreign PCLStats init). No-op otherwise. */
    static void EmitPclStatsMarker(uint32_t marker, uint64_t frame_id);

   private:
    ReflexManager reflex_manager_;
    static bool _is_pcl_initialized;
};
