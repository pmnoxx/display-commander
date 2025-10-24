#include "latent_sync_manager.hpp"
#include "../utils/logging.hpp"

namespace dxgi::latent_sync {

LatentSyncManager::LatentSyncManager() {}

bool LatentSyncManager::InitializeLatentSyncSystem() {
    LogWarn("Latent Sync Manager initialized successfully");
    return true;
}

void LatentSyncManager::ShutdownLatentSyncSystem() { LogWarn("Latent Sync Manager shutdown"); }

} // namespace dxgi::latent_sync
