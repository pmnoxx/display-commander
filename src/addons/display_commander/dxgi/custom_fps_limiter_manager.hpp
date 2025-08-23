#pragma once

#include "custom_fps_limiter.hpp"

namespace renodx::dxgi::fps_limiter {

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

private:
    CustomFpsLimiter m_fpsLimiter;
};

// Global instance
extern std::unique_ptr<CustomFpsLimiterManager> g_customFpsLimiterManager;

} // namespace renodx::dxgi::fps_limiter
