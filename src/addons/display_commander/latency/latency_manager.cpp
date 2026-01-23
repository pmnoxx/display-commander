#include "latency_manager.hpp"
#include "../globals.hpp"
#include "../utils.hpp"
#include "../utils/detour_call_tracker.hpp"
#include "../utils/logging.hpp"
#include "../utils/timing.hpp"
#include "reflex_provider.hpp"

// Include Streamline PCLStats header for PCLSTATS_MARKER macro
// Path is relative to src/addons/display_commander from external/Streamline
#include "../../../../external/Streamline/source/plugins/sl.pcl/pclstats.h"

// Define the PCLStats provider (must be in a .cpp file, not header)
PCLSTATS_DEFINE()

// Forward declaration to avoid circular dependency
extern float GetTargetFps();

// Namespace alias for cleaner code
namespace timing = utils;

LatencyManager::LatencyManager() = default;

LatencyManager::~LatencyManager() { Shutdown(); }

bool LatencyManager::Initialize(reshade::api::device* device, LatencyTechnology technology) {
    if (initialized_.load(std::memory_order_acquire)) {
        // Already initialized, check if we need to switch technology
        if (config_.technology != technology) {
            return SwitchTechnology(technology, device);
        }
        return true;
    }

    // Create provider for the requested technology
    provider_ = CreateProvider(technology);
    if (!provider_) {
        LogWarn("LatencyManager: Failed to create provider for technology");
        return false;
    }

    // Initialize the provider
    if (!provider_->Initialize(device)) {
        LogWarn("LatencyManager: Failed to initialize provider");
        provider_.reset();
        return false;
    }

    config_.technology = technology;
    initialized_.store(true, std::memory_order_release);

    std::ostringstream oss;
    oss << "LatencyManager: Initialized with " << provider_->GetTechnologyName();
    LogInfo(oss.str().c_str());

    return true;
}

bool LatencyManager::Initialize(void* native_device, DeviceTypeDC device_type, LatencyTechnology technology) {
    if (initialized_.load(std::memory_order_acquire)) {
        // Already initialized, check if we need to switch technology
        if (config_.technology != technology) {
            return SwitchTechnologyNative(technology, native_device, device_type);
        }
        return true;
    }

    // Create provider for the requested technology
    provider_ = CreateProvider(technology);
    if (!provider_) {
        LogWarn("LatencyManager: Failed to create provider for technology");
        return false;
    }

    // Initialize the provider with native device
    if (!provider_->InitializeNative(native_device, device_type)) {
        LogWarn("LatencyManager: Failed to initialize provider with native device");
        provider_.reset();
        return false;
    }

    config_.technology = technology;
    initialized_.store(true, std::memory_order_release);

    std::ostringstream oss;
    oss << "LatencyManager: Initialized with " << provider_->GetTechnologyName() << " (native device)";
    LogInfo(oss.str().c_str());

    return true;
}

void LatencyManager::Shutdown() {
    if (!initialized_.exchange(false, std::memory_order_release)) {
        return;  // Already shutdown
    }

    if (provider_) {
        provider_->Shutdown();
        provider_.reset();
    }

    config_ = LatencyConfig{};  // Reset config
    LogInfo("LatencyManager: Shutdown complete");
}

bool LatencyManager::IsInitialized() const {
    return initialized_.load(std::memory_order_acquire) && provider_ && provider_->IsInitialized();
}

bool LatencyManager::SetMarker(LatencyMarkerType marker) {
    if (!IsInitialized()) {
        return false;
    }
    RECORD_DETOUR_CALL(utils::get_now_ns());

    auto result = provider_->SetMarker(marker);
    if (!result) {
        return result;
    }
    RECORD_DETOUR_CALL(utils::get_now_ns());

    // Special-K style: on SIMULATION_START, optionally inject PC_LATENCY_PING when signaled.
    // This is gated inside pclstats_etw (user enable + ETW provider enable + not Reflex-native).
    // CRITICAL: Special K sends PC_LATENCY_PING through NVAPI first, which then gets emitted via ETW.
    // This ensures proper correlation and timing for PCL/AV stats calculation.
    if (marker == SIMULATION_START) {
        PCLSTATS_MARKER(PC_LATENCY_PING, g_global_frame_id.load(std::memory_order_acquire));
    }
    RECORD_DETOUR_CALL(utils::get_now_ns());

    switch (marker) {
        case SIMULATION_START: g_reflex_marker_simulation_start_count.fetch_add(1, std::memory_order_relaxed); break;
        case SIMULATION_END:   g_reflex_marker_simulation_end_count.fetch_add(1, std::memory_order_relaxed); break;
        case RENDERSUBMIT_START:
            g_reflex_marker_rendersubmit_start_count.fetch_add(1, std::memory_order_relaxed);
            break;
        case RENDERSUBMIT_END: g_reflex_marker_rendersubmit_end_count.fetch_add(1, std::memory_order_relaxed); break;
        case PRESENT_START:    g_reflex_marker_present_start_count.fetch_add(1, std::memory_order_relaxed); break;
        case PRESENT_END:      g_reflex_marker_present_end_count.fetch_add(1, std::memory_order_relaxed); break;
        case INPUT_SAMPLE:     g_reflex_marker_input_sample_count.fetch_add(1, std::memory_order_relaxed); break;
        case PC_LATENCY_PING:  break;
        default:               break;
    }
    RECORD_DETOUR_CALL(utils::get_now_ns());
    return result;
}

bool LatencyManager::ApplySleepMode(bool low_latency, bool boost, bool use_markers, float fps_limit) {
    if (!IsInitialized()) return false;

    // Increment debug counter
    extern std::atomic<uint32_t> g_reflex_apply_sleep_mode_count;
    g_reflex_apply_sleep_mode_count.fetch_add(1, std::memory_order_relaxed);

    return provider_->ApplySleepMode(low_latency, boost, use_markers, fps_limit);
}

bool LatencyManager::Sleep() {
    if (!IsInitialized()) return false;

    // Increment debug counter
    extern std::atomic<uint32_t> g_reflex_sleep_count;
    extern std::atomic<LONGLONG> g_reflex_sleep_duration_ns;
    g_reflex_sleep_count.fetch_add(1, std::memory_order_relaxed);

    // Measure sleep duration
    LONGLONG sleep_start_ns = timing::get_now_ns();
    bool result = provider_->Sleep();
    LONGLONG sleep_end_ns = timing::get_now_ns();

    // Update rolling average sleep duration
    LONGLONG sleep_duration_ns = sleep_end_ns - sleep_start_ns;
    LONGLONG old_duration = g_reflex_sleep_duration_ns.load();
    LONGLONG smoothed_duration = UpdateRollingAverage(sleep_duration_ns, old_duration);
    g_reflex_sleep_duration_ns.store(smoothed_duration);

    return result;
}

void LatencyManager::SetConfig(const LatencyConfig& config) {
    config_ = config;

    // Apply configuration if initialized
    if (IsInitialized()) {
        // Use config.target_fps if available, otherwise get current target FPS
        float fps_limit = GetTargetFps();
        ApplySleepMode(config.low_latency_mode, config.boost_mode, config.use_markers, fps_limit);
    }
}

LatencyConfig LatencyManager::GetConfig() const { return config_; }

LatencyTechnology LatencyManager::GetCurrentTechnology() const {
    if (!IsInitialized()) return LatencyTechnology::None;
    return provider_->GetTechnology();
}

const char* LatencyManager::GetCurrentTechnologyName() const {
    if (!IsInitialized()) return "None";
    return provider_->GetTechnologyName();
}

bool LatencyManager::SwitchTechnology(LatencyTechnology technology, reshade::api::device* device) {
    if (technology == config_.technology && IsInitialized()) {
        return true;  // Already using this technology
    }

    // Shutdown current provider
    if (provider_) {
        provider_->Shutdown();
        provider_.reset();
    }

    // Create new provider
    provider_ = CreateProvider(technology);
    if (!provider_) {
        LogWarn("LatencyManager: Failed to create provider for technology switch");
        return false;
    }

    // Initialize new provider
    if (!provider_->Initialize(device)) {
        LogWarn("LatencyManager: Failed to initialize new provider");
        provider_.reset();
        return false;
    }

    config_.technology = technology;

    std::ostringstream oss;
    oss << "LatencyManager: Switched to " << provider_->GetTechnologyName();
    LogInfo(oss.str().c_str());

    return true;
}

bool LatencyManager::SwitchTechnologyNative(LatencyTechnology technology, void* native_device,
                                            DeviceTypeDC device_type) {
    if (technology == config_.technology && IsInitialized()) {
        return true;  // Already using this technology
    }

    // Shutdown current provider
    if (provider_) {
        provider_->Shutdown();
        provider_.reset();
    }

    // Create new provider
    provider_ = CreateProvider(technology);
    if (!provider_) {
        LogWarn("LatencyManager: Failed to create provider for technology switch");
        return false;
    }

    // Initialize new provider with native device
    if (!provider_->InitializeNative(native_device, device_type)) {
        LogWarn("LatencyManager: Failed to initialize new provider with native device");
        provider_.reset();
        return false;
    }

    config_.technology = technology;

    std::ostringstream oss;
    oss << "LatencyManager: Switched to " << provider_->GetTechnologyName() << " (native device)";
    LogInfo(oss.str().c_str());

    return true;
}

std::unique_ptr<ILatencyProvider> LatencyManager::CreateProvider(LatencyTechnology technology) {
    switch (technology) {
        case LatencyTechnology::NVIDIA_Reflex: return std::make_unique<ReflexProvider>();

        case LatencyTechnology::AMD_AntiLag2:
            // TODO: Implement AMD Anti-Lag 2 provider
            LogWarn("LatencyManager: AMD Anti-Lag 2 not yet implemented");
            return nullptr;

        case LatencyTechnology::Intel_XeSS:
            // TODO: Implement Intel XeSS provider
            LogWarn("LatencyManager: Intel XeSS not yet implemented");
            return nullptr;

        case LatencyTechnology::None:
        default:                      LogWarn("LatencyManager: No latency technology specified"); return nullptr;
    }
}

void LatencyManager::UpdateCachedSleepStatus() {
    if (!IsInitialized() || !provider_) {
        g_reflex_sleep_status_low_latency_enabled.store(false, std::memory_order_release);
        return;
    }

    NV_GET_SLEEP_STATUS_PARAMS sleep_status = {};
    sleep_status.version = NV_GET_SLEEP_STATUS_PARAMS_VER;

    if (provider_->GetSleepStatus(&sleep_status)) {
        bool low_latency_enabled = (sleep_status.bLowLatencyMode == NV_TRUE);
        g_reflex_sleep_status_low_latency_enabled.store(low_latency_enabled, std::memory_order_release);
        g_reflex_sleep_status_last_update_ns.store(utils::get_now_ns(), std::memory_order_release);
    } else {
        g_reflex_sleep_status_low_latency_enabled.store(false, std::memory_order_release);
    }
}

bool LatencyManager::GetSleepStatus(NV_GET_SLEEP_STATUS_PARAMS* status_params) {
    if (!IsInitialized() || !provider_ || status_params == nullptr) {
        return false;
    }

    return provider_->GetSleepStatus(status_params);
}
