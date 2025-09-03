#pragma once

#include <windows.h>
#include <nvapi.h>
#include <string>
#include <chrono>

// NVAPI DLL-FG (Frame Generation) Statistics Module
// Provides comprehensive frame generation statistics and monitoring
class NVAPIDllFgStats {
public:
    // Constructor
    NVAPIDllFgStats();
    
    // Destructor
    ~NVAPIDllFgStats();
    
    // Initialize NVAPI and DLL-FG monitoring
    bool Initialize();
    
    // Cleanup resources
    void Cleanup();
    
    // Check if DLL-FG stats are available
    bool IsAvailable() const;
    
    // Update frame generation statistics
    void UpdateStats();
    
    // Get current internal resolution
    struct Resolution {
        uint32_t width = 0;
        uint32_t height = 0;
        bool valid = false;
    };
    Resolution GetInternalResolution() const;
    
    // Get DLL-FG mode status
    enum class DllFgMode {
        Disabled = 0,
        Enabled = 1,
        Unknown = 2
    };
    DllFgMode GetDllFgMode() const;
    
    // Get DLL-FG version information
    struct DllFgVersion {
        std::string version_string;
        uint32_t major_version = 0;
        uint32_t minor_version = 0;
        uint32_t build_number = 0;
        bool valid = false;
    };
    DllFgVersion GetDllFgVersion() const;
    
    // Get frame generation statistics
    struct FrameGenStats {
        uint64_t total_frames_generated = 0;
        uint64_t total_frames_presented = 0;
        uint64_t total_frames_dropped = 0;
        double frame_generation_ratio = 0.0;
        double average_frame_time_ms = 0.0;
        double gpu_utilization_percent = 0.0;
        bool valid = false;
    };
    FrameGenStats GetFrameGenStats() const;
    
    // Get performance metrics
    struct PerformanceMetrics {
        double input_lag_ms = 0.0;
        double output_lag_ms = 0.0;
        double total_latency_ms = 0.0;
        double frame_pacing_quality = 0.0;
        bool valid = false;
    };
    PerformanceMetrics GetPerformanceMetrics() const;
    
    // Get DLL-FG configuration
    struct DllFgConfig {
        bool auto_mode_enabled = false;
        bool quality_mode_enabled = false;
        bool performance_mode_enabled = false;
        uint32_t target_fps = 0;
        bool vsync_enabled = false;
        bool gsync_enabled = false;
        bool valid = false;
    };
    DllFgConfig GetDllFgConfig() const;
    
    // Get comprehensive status string
    std::string GetStatusString() const;
    
    // Get detailed debug information
    std::string GetDebugInfo() const;
    
    // Get last error message
    std::string GetLastError() const;
    
    // Check if NVIDIA hardware supports DLL-FG
    bool IsDllFgSupported() const;
    
    // Get driver compatibility info
    struct DriverCompatibility {
        bool is_supported = false;
        std::string min_required_version;
        std::string current_version;
        std::string compatibility_status;
        bool valid = false;
    };
    DriverCompatibility GetDriverCompatibility() const;

private:
    bool initialized = false;
    bool failed_to_initialize = false;
    mutable std::string last_error;
    
    // Cached statistics (updated periodically)
    mutable Resolution cached_resolution;
    mutable DllFgMode cached_mode = DllFgMode::Unknown;
    mutable DllFgVersion cached_version;
    mutable FrameGenStats cached_frame_stats;
    mutable PerformanceMetrics cached_performance;
    mutable DllFgConfig cached_config;
    mutable DriverCompatibility cached_compatibility;
    
    // Update timestamps
    mutable std::chrono::steady_clock::time_point last_resolution_update;
    mutable std::chrono::steady_clock::time_point last_mode_update;
    mutable std::chrono::steady_clock::time_point last_version_update;
    mutable std::chrono::steady_clock::time_point last_stats_update;
    mutable std::chrono::steady_clock::time_point last_performance_update;
    mutable std::chrono::steady_clock::time_point last_config_update;
    mutable std::chrono::steady_clock::time_point last_compatibility_update;
    
    // Cache duration (in milliseconds)
    static constexpr int CACHE_DURATION_MS = 1000; // 1 second cache
    
    // Helper methods
    bool EnsureNvApi() const;
    bool QueryInternalResolution(Resolution& resolution) const;
    bool QueryDllFgMode(DllFgMode& mode) const;
    bool QueryDllFgVersion(DllFgVersion& version) const;
    bool QueryFrameGenStats(FrameGenStats& stats) const;
    bool QueryPerformanceMetrics(PerformanceMetrics& metrics) const;
    bool QueryDllFgConfig(DllFgConfig& config) const;
    bool QueryDriverCompatibility(DriverCompatibility& compatibility) const;
    
    // Disable copy constructor and assignment
    NVAPIDllFgStats(const NVAPIDllFgStats&) = delete;
    NVAPIDllFgStats& operator=(const NVAPIDllFgStats&) = delete;
};

// Global instance
extern NVAPIDllFgStats g_nvapiDllFgStats;
