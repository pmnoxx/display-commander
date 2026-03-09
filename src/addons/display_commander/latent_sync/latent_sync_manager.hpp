#pragma once

#include "latent_sync_limiter.hpp"

namespace dxgi::latent_sync {

class LatentSyncManager {
  public:
    LatentSyncManager();
    ~LatentSyncManager() = default;

    // Get the latent sync limiter instance
    dxgi::fps_limiter::LatentSyncLimiter &GetLatentLimiter() { return m_latentLimiter; }

  private:
    dxgi::fps_limiter::LatentSyncLimiter m_latentLimiter;
};

} // namespace dxgi::latent_sync
