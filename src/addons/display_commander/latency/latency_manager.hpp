#pragma once

#include <atomic>
#include <memory>
#include <reshade.hpp>
#include "../globals.hpp"

// Forward declarations
class ReflexManager;

// Use NVAPI marker type directly to avoid conversion bugs
// Since only NVIDIA Reflex is implemented, we use NV_LATENCY_MARKER_TYPE directly
using LatencyMarkerType = NV_LATENCY_MARKER_TYPE;

// Latency technology types
enum class LatencyTechnology { None, NVIDIA_Reflex, AMD_AntiLag2, Intel_XeSS };

// Configuration for latency technologies
struct LatencyConfig {
    bool enabled = false;
    bool low_latency_mode = false;
    bool boost_mode = false;
    bool use_markers = false;
    float target_fps = 0.0f;
    LatencyTechnology technology = LatencyTechnology::None;
};

// Abstract base class for latency management
class ILatencyProvider {
   public:
    virtual ~ILatencyProvider() = default;

    // Core lifecycle
    virtual bool Initialize(reshade::api::device* device) = 0;
    virtual bool InitializeNative(void* native_device, DeviceTypeDC device_type) = 0;
    virtual void Shutdown() = 0;
    virtual bool IsInitialized() const = 0;

    // Markers for frame timing
    virtual bool SetMarker(LatencyMarkerType marker) = 0;

    // Sleep/limiting functionality
    virtual bool ApplySleepMode(bool low_latency, bool boost, bool use_markers, float fps_limit) = 0;
    virtual bool Sleep() = 0;

    // Get sleep status (optional, returns false if not supported).
    // When returning false, out_reason may be set to explain why (if non-null).
    virtual bool GetSleepStatus(NV_GET_SLEEP_STATUS_PARAMS* status_params,
                                SleepStatusUnavailableReason* out_reason = nullptr) {
        (void)status_params;
        if (out_reason) *out_reason = SleepStatusUnavailableReason::kProviderDoesNotSupport;
        return false;
    }

    // Technology-specific info
    virtual LatencyTechnology GetTechnology() const = 0;
    virtual const char* GetTechnologyName() const = 0;
};

// Main latency manager that abstracts different technologies
class LatencyManager {
   public:
    LatencyManager();
    ~LatencyManager();

    // Initialize with a specific technology
    bool Initialize(reshade::api::device* device, LatencyTechnology technology = LatencyTechnology::NVIDIA_Reflex);

    // Initialize with native device (alternative to ReShade device)
    bool Initialize(void* native_device, DeviceTypeDC device_type,
                    LatencyTechnology technology = LatencyTechnology::NVIDIA_Reflex);

    // Shutdown current provider
    void Shutdown();

    // Check if any latency technology is active
    bool IsInitialized() const;

    // Frame management
    uint64_t IncreaseFrameId();

    // Marker operations
    bool SetMarker(LatencyMarkerType marker);

    // Sleep mode configuration
    bool ApplySleepMode(bool low_latency, bool boost, bool use_markers, float fps_limit);
    bool Sleep();

    // Configuration
    void SetConfig(const LatencyConfig& config);
    LatencyConfig GetConfig() const;

    // Technology info
    LatencyTechnology GetCurrentTechnology() const;
    const char* GetCurrentTechnologyName() const;

    // Update cached sleep status (called periodically)
    void UpdateCachedSleepStatus();

    // Get full sleep status (for UI display).
    // When returning false, out_reason may be set to explain why (if non-null).
    bool GetSleepStatus(NV_GET_SLEEP_STATUS_PARAMS* status_params, SleepStatusUnavailableReason* out_reason = nullptr);

    // Switch between technologies at runtime
    bool SwitchTechnology(LatencyTechnology technology, reshade::api::device* device);
    bool SwitchTechnologyNative(LatencyTechnology technology, void* native_device, DeviceTypeDC device_type);

   private:
    std::unique_ptr<ILatencyProvider> provider_;
    LatencyConfig config_;
    std::atomic<bool> initialized_{false};

    // Create provider for specific technology
    std::unique_ptr<ILatencyProvider> CreateProvider(LatencyTechnology technology);
};

// Human-readable reason for sleep status being unavailable (for UI)
inline const char* SleepStatusUnavailableReasonToString(SleepStatusUnavailableReason r) {
    switch (r) {
        case SleepStatusUnavailableReason::kNone:             return "Available";
        case SleepStatusUnavailableReason::kNoLatencyManager: return "Latency manager not created";
        case SleepStatusUnavailableReason::kLatencyManagerNotInitialized:
            return "Latency manager not initialized (no D3D device yet)";
        case SleepStatusUnavailableReason::kProviderDoesNotSupport:
            return "Current latency provider does not support sleep status";
        case SleepStatusUnavailableReason::kReflexNotInitialized: return "Reflex manager not initialized";
        case SleepStatusUnavailableReason::kNoD3DDevice:          return "No D3D device (device lost or not set)";
        case SleepStatusUnavailableReason::kNvApiFunctionUnavailable:
            return "NvAPI_D3D_GetSleepStatus not found in nvapi64";
        case SleepStatusUnavailableReason::kNvApiError: return "NvAPI GetSleepStatus returned an error";
        default:                                        return "Unknown";
    }
}
