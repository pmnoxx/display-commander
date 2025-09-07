#pragma once

#include "custom_fps_limiter.hpp"

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

private:
    CustomFpsLimiter m_fpsLimiter;
};

} // namespace dxgi::fps_limiter
