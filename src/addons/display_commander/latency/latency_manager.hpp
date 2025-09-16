#pragma once

#include <atomic>
#include <memory>
#include <reshade.hpp>

// Forward declarations
class ReflexManager;

// Latency marker types for different technologies
enum class LatencyMarkerType {
    SIMULATION_START,
    SIMULATION_END,
    RENDERSUBMIT_START,
    RENDERSUBMIT_END,
    PRESENT_START,
    PRESENT_END,
    INPUT_SAMPLE
};

// Latency technology types
enum class LatencyTechnology {
    None,
    NVIDIA_Reflex,
    AMD_AntiLag2,
    Intel_XeSS
};

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
    virtual void Shutdown() = 0;
    virtual bool IsInitialized() const = 0;

    // Frame management
    virtual uint64_t IncreaseFrameId() = 0;

    // Markers for frame timing
    virtual bool SetMarker(LatencyMarkerType marker) = 0;

    // Sleep/limiting functionality
    virtual bool ApplySleepMode(bool low_latency, bool boost, bool use_markers) = 0;
    virtual bool Sleep() = 0;

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

    // Shutdown current provider
    void Shutdown();

    // Check if any latency technology is active
    bool IsInitialized() const;

    // Frame management
    uint64_t IncreaseFrameId();

    // Marker operations
    bool SetMarker(LatencyMarkerType marker);

    // Sleep mode configuration
    bool ApplySleepMode(bool low_latency, bool boost, bool use_markers);
    bool Sleep();

    // Configuration
    void SetConfig(const LatencyConfig& config);
    LatencyConfig GetConfig() const;

    // Technology info
    LatencyTechnology GetCurrentTechnology() const;
    const char* GetCurrentTechnologyName() const;

    // Switch between technologies at runtime
    bool SwitchTechnology(LatencyTechnology technology, reshade::api::device* device);

private:
    std::unique_ptr<ILatencyProvider> provider_;
    LatencyConfig config_;
    std::atomic<bool> initialized_{false};

    // Create provider for specific technology
    std::unique_ptr<ILatencyProvider> CreateProvider(LatencyTechnology technology);
};
