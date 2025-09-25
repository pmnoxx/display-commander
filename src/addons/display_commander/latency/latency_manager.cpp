#include "latency_manager.hpp"
#include "../globals.hpp"
#include "../utils.hpp"
#include "reflex_provider.hpp"


LatencyManager::LatencyManager() = default;

LatencyManager::~LatencyManager() { Shutdown(); }

bool LatencyManager::Initialize(reshade::api::device *device, LatencyTechnology technology) {
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

void LatencyManager::Shutdown() {
    if (!initialized_.exchange(false, std::memory_order_release)) {
        return; // Already shutdown
    }

    if (provider_) {
        provider_->Shutdown();
        provider_.reset();
    }

    config_ = LatencyConfig{}; // Reset config
    LogInfo("LatencyManager: Shutdown complete");
}

bool LatencyManager::IsInitialized() const {
    return initialized_.load(std::memory_order_acquire) && provider_ && provider_->IsInitialized();
}

uint64_t LatencyManager::IncreaseFrameId() {
    if (!IsInitialized())
        return 0;
    return provider_->IncreaseFrameId();
}

bool LatencyManager::SetMarker(LatencyMarkerType marker) {
    if (!IsInitialized())
        return false;
    return provider_->SetMarker(marker);
}

bool LatencyManager::ApplySleepMode(bool low_latency, bool boost, bool use_markers) {
    if (!IsInitialized())
        return false;
    return provider_->ApplySleepMode(low_latency, boost, use_markers);
}

bool LatencyManager::Sleep() {
    if (!IsInitialized())
        return false;
    return provider_->Sleep();
}

void LatencyManager::SetConfig(const LatencyConfig &config) {
    config_ = config;

    // Apply configuration if initialized
    if (IsInitialized()) {
        ApplySleepMode(config.low_latency_mode, config.boost_mode, config.use_markers);
    }
}

LatencyConfig LatencyManager::GetConfig() const { return config_; }

LatencyTechnology LatencyManager::GetCurrentTechnology() const {
    if (!IsInitialized())
        return LatencyTechnology::None;
    return provider_->GetTechnology();
}

const char *LatencyManager::GetCurrentTechnologyName() const {
    if (!IsInitialized())
        return "None";
    return provider_->GetTechnologyName();
}

bool LatencyManager::SwitchTechnology(LatencyTechnology technology, reshade::api::device *device) {
    if (technology == config_.technology && IsInitialized()) {
        return true; // Already using this technology
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

std::unique_ptr<ILatencyProvider> LatencyManager::CreateProvider(LatencyTechnology technology) {
    switch (technology) {
    case LatencyTechnology::NVIDIA_Reflex:
        return std::make_unique<ReflexProvider>();

    case LatencyTechnology::AMD_AntiLag2:
        // TODO: Implement AMD Anti-Lag 2 provider
        LogWarn("LatencyManager: AMD Anti-Lag 2 not yet implemented");
        return nullptr;

    case LatencyTechnology::Intel_XeSS:
        // TODO: Implement Intel XeSS provider
        LogWarn("LatencyManager: Intel XeSS not yet implemented");
        return nullptr;

    case LatencyTechnology::None:
    default:
        LogWarn("LatencyManager: No latency technology specified");
        return nullptr;
    }
}
