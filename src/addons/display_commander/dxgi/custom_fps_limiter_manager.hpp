#pragma once

#include "custom_fps_limiter.hpp"
#include "../latent_sync/latent_sync_limiter.hpp"

namespace dxgi::fps_limiter {

class CustomFpsLimiterManager {
public:
    CustomFpsLimiterManager();
    ~CustomFpsLimiterManager() = default;

    // Initialize the custom FPS limiter system
    bool InitializeCustomFpsLimiterSystem();
    
    // Shutdown the custom FPS limiter system
    void ShutdownCustomFpsLimiterSystem();
    
    // Get the FPS limiter instance
    CustomFpsLimiter& GetFpsLimiter() { return m_fpsLimiter; }

    // Get the VBlank Scanline Sync (VBlank) limiter instance
    LatentSyncLimiter& GetLatentLimiter() { return m_latentLimiter; }

private:
    CustomFpsLimiter m_fpsLimiter;
    LatentSyncLimiter m_latentLimiter;
};

} // namespace dxgi::fps_limiter
