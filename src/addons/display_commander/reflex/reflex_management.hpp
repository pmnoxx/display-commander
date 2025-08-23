#pragma once

#include <windows.h>
#include <string>
#include <atomic>
#include <memory>
#include <chrono>
#include <deque>
#include <include/reshade.hpp>

// ETW tracing support for NVIDIA overlay compatibility
#include <TraceLoggingProvider.h>
#include <evntrace.h>

// Declare the ETW provider (defined in the implementation)
TRACELOGGING_DECLARE_PROVIDER(g_hPCLStatsProvider);

// Include NVAPI types for the implementation
#include <nvapi.h>

// PCLStats marker types (same as Special-K)
enum PCLSTATS_LATENCY_MARKER_TYPE {
    PCLSTATS_SIMULATION_START               = 0,
    PCLSTATS_SIMULATION_END                 = 1,
    PCLSTATS_RENDERSUBMIT_START             = 2,
    PCLSTATS_RENDERSUBMIT_END               = 3,
    PCLSTATS_PRESENT_START                  = 4,
    PCLSTATS_PRESENT_END                    = 5,
    // PCLSTATS_INPUT_SAMPLE = 6, Deprecated
    PCLSTATS_TRIGGER_FLASH                  = 7,
    PCLSTATS_PC_LATENCY_PING                = 8,
    PCLSTATS_OUT_OF_BAND_RENDERSUBMIT_START = 9,
    PCLSTATS_OUT_OF_BAND_RENDERSUBMIT_END   = 10,
    PCLSTATS_OUT_OF_BAND_PRESENT_START      = 11,
    PCLSTATS_OUT_OF_BAND_PRESENT_END        = 12,
    PCLSTATS_CONTROLLER_INPUT_SAMPLE        = 13,
};

class ReflexManager {
public:
    ReflexManager();
    ~ReflexManager();

    bool Initialize();
    void Shutdown();
    bool IsAvailable() const { return is_initialized_; }

    // Reflex control functions
    bool SetSleepMode(reshade::api::swapchain* swapchain);
    bool SetLatencyMarkers(reshade::api::swapchain* swapchain);
    bool CallSleep(reshade::api::swapchain* swapchain);
    bool SetPresentMarkers(reshade::api::swapchain* swapchain);

    // PCLStats ETW tracing for NVIDIA overlay compatibility
    void PCLStatsInit();
    void PCLStatsShutdown();
    void PCLStatsMarker(uint32_t marker_type, uint64_t frame_id);
    void PCLStatsMarkerV2(uint32_t marker_type, uint64_t frame_id);
    bool PCLStatsIsSignaled() const;

    // Latency display methods for our UI (atomic, no mutex needed)
    float GetCurrentLatencyMs() const { return current_latency_ms_.load(); }
    uint64_t GetCurrentFrame() const { return current_frame_.load(); }
    bool IsReflexActive() const { return is_active_.load(); }
    std::string GetReflexStatus() const;
    float GetAverageLatencyMs() const { return average_latency_ms_.load(); }
    float GetMinLatencyMs() const { return min_latency_ms_.load(); }
    float GetMaxLatencyMs() const { return max_latency_ms_.load(); }
    float GetPCLLatencyMs() const { return pcl_latency_ms_.load(); } // PCL AV latency

    // Safe batch access method for UI (reduces pointer access risk)
    struct LatencyData {
        float current_latency_ms;
        float average_latency_ms;
        float min_latency_ms;
        float max_latency_ms;
        float pcl_latency_ms; // PCL AV latency
        uint64_t current_frame;
        bool is_active;
        std::string status;
    };
    
    LatencyData GetLatencyDataSafe() const;

    // Internal latency tracking update
    void UpdateLatencyTracking();

    std::string GetLastError() const { return last_error_; }

private:
    // NVAPI function pointers
    using NvAPI_D3D_SetLatencyMarker_pfn = NvAPI_Status (__cdecl *)(IUnknown* pDev, NV_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams);
    using NvAPI_D3D_SetSleepMode_pfn = NvAPI_Status (__cdecl *)(IUnknown* pDev, NV_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams);
    using NvAPI_D3D_Sleep_pfn = NvAPI_Status (__cdecl *)(IUnknown* pDev);
    
    NvAPI_D3D_SetLatencyMarker_pfn set_latency_marker_;
    NvAPI_D3D_SetSleepMode_pfn set_sleep_mode_;
    NvAPI_D3D_Sleep_pfn sleep_;

    bool is_initialized_;
    std::atomic<uint64_t> frame_id_;
    std::string last_error_;

    // PCLStats ETW tracing members
    HANDLE pcl_stats_quit_event_;
    HANDLE pcl_stats_ping_thread_;
    bool pcl_stats_enable_;
    std::atomic<ULONG> pcl_stats_signal_;
    DWORD pcl_stats_id_thread_;
    UINT pcl_stats_window_message_; // Window message for PCLStats ping

    // Atomic variables for latency tracking
    std::atomic<float> current_latency_ms_;
    std::atomic<uint64_t> current_frame_;
    std::atomic<bool> is_active_;
    std::atomic<float> average_latency_ms_;
    std::atomic<float> min_latency_ms_;
    std::atomic<float> max_latency_ms_;
    std::atomic<float> pcl_latency_ms_; // PCL AV latency

    // Internal tracking members
    std::deque<float> latency_history_;
    std::chrono::high_resolution_clock::time_point last_frame_time_;
    static constexpr size_t MAX_LATENCY_HISTORY = 60; // Keep last 60 frames

    static DWORD WINAPI PCLStatsPingThreadProc(LPVOID param);
    void PCLStatsPingThread();
};

// Global Reflex manager instance
extern std::unique_ptr<ReflexManager> g_reflexManager;

// Reflex management functions
bool InstallReflexHooks();
void UninstallReflexHooks();
void SetReflexLatencyMarkers(reshade::api::swapchain* swapchain);
void SetReflexSleepMode(reshade::api::swapchain* swapchain);
void SetReflexPresentMarkers(reshade::api::swapchain* swapchain);
