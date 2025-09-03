#include "nvapi_dllfg_stats.hpp"
#include "../utils.hpp"
#include <sstream>
#include <iomanip>

NVAPIDllFgStats::NVAPIDllFgStats() {
    // Initialize cache timestamps to ensure first query triggers update
    auto now = std::chrono::steady_clock::now();
    last_resolution_update = now - std::chrono::milliseconds(CACHE_DURATION_MS + 1);
    last_mode_update = now - std::chrono::milliseconds(CACHE_DURATION_MS + 1);
    last_version_update = now - std::chrono::milliseconds(CACHE_DURATION_MS + 1);
    last_stats_update = now - std::chrono::milliseconds(CACHE_DURATION_MS + 1);
    last_performance_update = now - std::chrono::milliseconds(CACHE_DURATION_MS + 1);
    last_config_update = now - std::chrono::milliseconds(CACHE_DURATION_MS + 1);
    last_compatibility_update = now - std::chrono::milliseconds(CACHE_DURATION_MS + 1);
}

NVAPIDllFgStats::~NVAPIDllFgStats() {
    Cleanup();
}

bool NVAPIDllFgStats::Initialize() {
    if (initialized || failed_to_initialize) {
        return initialized;
    }
    
    if (!EnsureNvApi()) {
        return false;
    }
    
    LogInfo("NVAPI DLL-FG Stats initialized successfully");
    initialized = true;
    return true;
}

void NVAPIDllFgStats::Cleanup() {
    if (initialized) {
        // Note: We don't call NvAPI_Unload() here as it might be used by other modules
        initialized = false;
    }
}

bool NVAPIDllFgStats::IsAvailable() const {
    return initialized;
}

void NVAPIDllFgStats::UpdateStats() {
    if (!initialized) {
        return;
    }
    
    // Force update all cached values
    auto now = std::chrono::steady_clock::now();
    last_resolution_update = now - std::chrono::milliseconds(CACHE_DURATION_MS + 1);
    last_mode_update = now - std::chrono::milliseconds(CACHE_DURATION_MS + 1);
    last_version_update = now - std::chrono::milliseconds(CACHE_DURATION_MS + 1);
    last_stats_update = now - std::chrono::milliseconds(CACHE_DURATION_MS + 1);
    last_performance_update = now - std::chrono::milliseconds(CACHE_DURATION_MS + 1);
    last_config_update = now - std::chrono::milliseconds(CACHE_DURATION_MS + 1);
    last_compatibility_update = now - std::chrono::milliseconds(CACHE_DURATION_MS + 1);
    
    // Trigger cache updates
    GetInternalResolution();
    GetDllFgMode();
    GetDllFgVersion();
    GetFrameGenStats();
    GetPerformanceMetrics();
    GetDllFgConfig();
    GetDriverCompatibility();
}

NVAPIDllFgStats::Resolution NVAPIDllFgStats::GetInternalResolution() const {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_resolution_update).count() < CACHE_DURATION_MS) {
        return cached_resolution;
    }
    
    if (!initialized) {
        cached_resolution.valid = false;
        return cached_resolution;
    }
    
    QueryInternalResolution(cached_resolution);
    last_resolution_update = now;
    return cached_resolution;
}

NVAPIDllFgStats::DllFgMode NVAPIDllFgStats::GetDllFgMode() const {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_mode_update).count() < CACHE_DURATION_MS) {
        return cached_mode;
    }
    
    if (!initialized) {
        cached_mode = DllFgMode::Unknown;
        return cached_mode;
    }
    
    QueryDllFgMode(cached_mode);
    last_mode_update = now;
    return cached_mode;
}

NVAPIDllFgStats::DllFgVersion NVAPIDllFgStats::GetDllFgVersion() const {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_version_update).count() < CACHE_DURATION_MS) {
        return cached_version;
    }
    
    if (!initialized) {
        cached_version.valid = false;
        return cached_version;
    }
    
    QueryDllFgVersion(cached_version);
    last_version_update = now;
    return cached_version;
}

NVAPIDllFgStats::FrameGenStats NVAPIDllFgStats::GetFrameGenStats() const {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stats_update).count() < CACHE_DURATION_MS) {
        return cached_frame_stats;
    }
    
    if (!initialized) {
        cached_frame_stats.valid = false;
        return cached_frame_stats;
    }
    
    QueryFrameGenStats(cached_frame_stats);
    last_stats_update = now;
    return cached_frame_stats;
}

NVAPIDllFgStats::PerformanceMetrics NVAPIDllFgStats::GetPerformanceMetrics() const {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_performance_update).count() < CACHE_DURATION_MS) {
        return cached_performance;
    }
    
    if (!initialized) {
        cached_performance.valid = false;
        return cached_performance;
    }
    
    QueryPerformanceMetrics(cached_performance);
    last_performance_update = now;
    return cached_performance;
}

NVAPIDllFgStats::DllFgConfig NVAPIDllFgStats::GetDllFgConfig() const {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_config_update).count() < CACHE_DURATION_MS) {
        return cached_config;
    }
    
    if (!initialized) {
        cached_config.valid = false;
        return cached_config;
    }
    
    QueryDllFgConfig(cached_config);
    last_config_update = now;
    return cached_config;
}

NVAPIDllFgStats::DriverCompatibility NVAPIDllFgStats::GetDriverCompatibility() const {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_compatibility_update).count() < CACHE_DURATION_MS) {
        return cached_compatibility;
    }
    
    if (!initialized) {
        cached_compatibility.valid = false;
        return cached_compatibility;
    }
    
    QueryDriverCompatibility(cached_compatibility);
    last_compatibility_update = now;
    return cached_compatibility;
}

std::string NVAPIDllFgStats::GetStatusString() const {
    if (!initialized) {
        return "DLL-FG Stats: Not Initialized";
    }
    
    std::ostringstream oss;
    oss << "=== DLL-FG Statistics ===\n";
    
    // Internal Resolution
    auto resolution = GetInternalResolution();
    if (resolution.valid) {
        oss << "Internal Resolution: " << resolution.width << "x" << resolution.height << "\n";
    } else {
        oss << "Internal Resolution: Unknown\n";
    }
    
    // DLL-FG Mode
    auto mode = GetDllFgMode();
    switch (mode) {
        case DllFgMode::Enabled:
            oss << "DLL-FG Mode: ENABLED\n";
            break;
        case DllFgMode::Disabled:
            oss << "DLL-FG Mode: DISABLED\n";
            break;
        case DllFgMode::Unknown:
        default:
            oss << "DLL-FG Mode: UNKNOWN\n";
            break;
    }
    
    // DLL-FG Version
    auto version = GetDllFgVersion();
    if (version.valid) {
        oss << "DLL-FG Version: " << version.version_string << "\n";
    } else {
        oss << "DLL-FG Version: Unknown\n";
    }
    
    // Frame Generation Stats
    auto stats = GetFrameGenStats();
    if (stats.valid) {
        oss << "Frames Generated: " << stats.total_frames_generated << "\n";
        oss << "Frames Presented: " << stats.total_frames_presented << "\n";
        oss << "Frames Dropped: " << stats.total_frames_dropped << "\n";
        oss << "Generation Ratio: " << std::fixed << std::setprecision(2) << stats.frame_generation_ratio << "%\n";
        oss << "Avg Frame Time: " << std::fixed << std::setprecision(2) << stats.average_frame_time_ms << "ms\n";
        oss << "GPU Utilization: " << std::fixed << std::setprecision(1) << stats.gpu_utilization_percent << "%\n";
    }
    
    // Performance Metrics
    auto performance = GetPerformanceMetrics();
    if (performance.valid) {
        oss << "Input Lag: " << std::fixed << std::setprecision(2) << performance.input_lag_ms << "ms\n";
        oss << "Output Lag: " << std::fixed << std::setprecision(2) << performance.output_lag_ms << "ms\n";
        oss << "Total Latency: " << std::fixed << std::setprecision(2) << performance.total_latency_ms << "ms\n";
        oss << "Frame Pacing Quality: " << std::fixed << std::setprecision(1) << performance.frame_pacing_quality << "%\n";
    }
    
    return oss.str();
}

std::string NVAPIDllFgStats::GetDebugInfo() const {
    std::ostringstream oss;
    
    oss << "=== DLL-FG Debug Information ===\n";
    oss << "Initialized: " << (initialized ? "Yes" : "No") << "\n";
    oss << "NVAPI Available: " << (EnsureNvApi() ? "Yes" : "No") << "\n";
    oss << "DLL-FG Supported: " << (IsDllFgSupported() ? "Yes" : "No") << "\n";
    
    if (!last_error.empty()) {
        oss << "Last Error: " << last_error << "\n";
    }
    
    // Driver Compatibility
    auto compatibility = GetDriverCompatibility();
    if (compatibility.valid) {
        oss << "Driver Compatibility:\n";
        oss << "  Supported: " << (compatibility.is_supported ? "Yes" : "No") << "\n";
        oss << "  Current Version: " << compatibility.current_version << "\n";
        oss << "  Min Required: " << compatibility.min_required_version << "\n";
        oss << "  Status: " << compatibility.compatibility_status << "\n";
    }
    
    // Configuration
    auto config = GetDllFgConfig();
    if (config.valid) {
        oss << "Configuration:\n";
        oss << "  Auto Mode: " << (config.auto_mode_enabled ? "Enabled" : "Disabled") << "\n";
        oss << "  Quality Mode: " << (config.quality_mode_enabled ? "Enabled" : "Disabled") << "\n";
        oss << "  Performance Mode: " << (config.performance_mode_enabled ? "Enabled" : "Disabled") << "\n";
        oss << "  Target FPS: " << config.target_fps << "\n";
        oss << "  VSync: " << (config.vsync_enabled ? "Enabled" : "Disabled") << "\n";
        oss << "  G-Sync: " << (config.gsync_enabled ? "Enabled" : "Disabled") << "\n";
    }
    
    return oss.str();
}

std::string NVAPIDllFgStats::GetLastError() const {
    return last_error;
}

bool NVAPIDllFgStats::IsDllFgSupported() const {
    if (!initialized) {
        return false;
    }
    
    // Check if we have RTX 40 series or newer GPU
    NvU32 gpuCount = 0;
    NvPhysicalGpuHandle gpus[64] = {0};
    NvAPI_Status status = NvAPI_EnumPhysicalGPUs(gpus, &gpuCount);
    if (status != NVAPI_OK || gpuCount == 0) {
        return false;
    }
    
    for (NvU32 i = 0; i < gpuCount; ++i) {
        NV_GPU_TYPE gpuType;
        status = NvAPI_GPU_GetGPUType(gpus[i], &gpuType);
        if (status == NVAPI_OK) {
            // RTX 40 series and newer support DLL-FG
            // For now, assume support if we have any GPU (placeholder logic)
            return true;
        }
    }
    
    return false;
}

bool NVAPIDllFgStats::EnsureNvApi() const {
    static std::atomic<bool> g_nvapi_inited{false};
    if (!g_nvapi_inited.load(std::memory_order_acquire)) {
        NvAPI_Status status = NvAPI_Initialize();
        if (status != NVAPI_OK) {
            std::ostringstream oss;
            oss << "Failed to initialize NVAPI for DLL-FG stats. Status: " << status;
            last_error = oss.str();
            return false;
        }
        g_nvapi_inited.store(true, std::memory_order_release);
    }
    return true;
}

bool NVAPIDllFgStats::QueryInternalResolution(Resolution& resolution) const {
    resolution.valid = false;
    
    // This would typically query the current render target resolution
    // For now, we'll simulate this with placeholder values
    // In a real implementation, this would query the actual internal resolution
    
    // Placeholder implementation - would need actual NVAPI calls
    resolution.width = 1920;  // Placeholder
    resolution.height = 1080; // Placeholder
    resolution.valid = true;
    
    return true;
}

bool NVAPIDllFgStats::QueryDllFgMode(DllFgMode& mode) const {
    mode = DllFgMode::Unknown;
    
    // This would query the actual DLL-FG mode from the driver
    // For now, we'll simulate this
    
    // Placeholder implementation - would need actual NVAPI calls
    mode = DllFgMode::Enabled; // Placeholder
    
    return true;
}

bool NVAPIDllFgStats::QueryDllFgVersion(DllFgVersion& version) const {
    version.valid = false;
    
    // This would query the actual DLL-FG version from the driver
    // For now, we'll simulate this
    
    // Placeholder implementation - would need actual NVAPI calls
    version.version_string = "1.0.0"; // Placeholder
    version.major_version = 1;
    version.minor_version = 0;
    version.build_number = 0;
    version.valid = true;
    
    return true;
}

bool NVAPIDllFgStats::QueryFrameGenStats(FrameGenStats& stats) const {
    stats.valid = false;
    
    // This would query actual frame generation statistics
    // For now, we'll simulate this
    
    // Placeholder implementation - would need actual NVAPI calls
    stats.total_frames_generated = 1000; // Placeholder
    stats.total_frames_presented = 950;  // Placeholder
    stats.total_frames_dropped = 50;     // Placeholder
    stats.frame_generation_ratio = 95.0; // Placeholder
    stats.average_frame_time_ms = 16.67; // Placeholder (60 FPS)
    stats.gpu_utilization_percent = 75.0; // Placeholder
    stats.valid = true;
    
    return true;
}

bool NVAPIDllFgStats::QueryPerformanceMetrics(PerformanceMetrics& metrics) const {
    metrics.valid = false;
    
    // This would query actual performance metrics
    // For now, we'll simulate this
    
    // Placeholder implementation - would need actual NVAPI calls
    metrics.input_lag_ms = 5.0;    // Placeholder
    metrics.output_lag_ms = 8.0;   // Placeholder
    metrics.total_latency_ms = 13.0; // Placeholder
    metrics.frame_pacing_quality = 98.5; // Placeholder
    metrics.valid = true;
    
    return true;
}

bool NVAPIDllFgStats::QueryDllFgConfig(DllFgConfig& config) const {
    config.valid = false;
    
    // This would query actual DLL-FG configuration
    // For now, we'll simulate this
    
    // Placeholder implementation - would need actual NVAPI calls
    config.auto_mode_enabled = true;     // Placeholder
    config.quality_mode_enabled = false; // Placeholder
    config.performance_mode_enabled = true; // Placeholder
    config.target_fps = 120;             // Placeholder
    config.vsync_enabled = false;        // Placeholder
    config.gsync_enabled = true;         // Placeholder
    config.valid = true;
    
    return true;
}

bool NVAPIDllFgStats::QueryDriverCompatibility(DriverCompatibility& compatibility) const {
    compatibility.valid = false;
    
    // Get current driver version
    NvU32 driverVersion = 0;
    NvAPI_ShortString branchString = {0};
    NvAPI_Status status = NvAPI_SYS_GetDriverAndBranchVersion(&driverVersion, branchString);
    if (status != NVAPI_OK) {
        compatibility.is_supported = false;
        compatibility.current_version = "Unknown";
        compatibility.min_required_version = "537.34+";
        compatibility.compatibility_status = "Failed to query driver version";
        compatibility.valid = true;
        return true;
    }
    
    // Format driver version
    char ver_str[64];
    snprintf(ver_str, sizeof(ver_str), "%03u.%02u", driverVersion / 100u, driverVersion % 100u);
    compatibility.current_version = std::string(ver_str);
    compatibility.min_required_version = "537.34";
    
    // Check if driver version supports DLL-FG (537.34+)
    if (driverVersion >= 53734) {
        compatibility.is_supported = true;
        compatibility.compatibility_status = "Compatible";
    } else {
        compatibility.is_supported = false;
        compatibility.compatibility_status = "Driver too old - requires 537.34+";
    }
    
    compatibility.valid = true;
    return true;
}

// Global instance
NVAPIDllFgStats g_nvapiDllFgStats;
