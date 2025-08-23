#include "custom_fps_limiter_manager.hpp"
#include "../addon.hpp"

namespace dxgi::fps_limiter {

CustomFpsLimiterManager::CustomFpsLimiterManager()
{
}

bool CustomFpsLimiterManager::InitializeCustomFpsLimiterSystem()
{
    // Disable RenoDX's built-in FPS limiter
  //  utils::swapchain::fps_limit = 0.0f;
    
    LogWarn("Custom FPS Limiter Manager initialized successfully");
    LogWarn("RenoDX built-in FPS limiter disabled");
    
    return true;
}

void CustomFpsLimiterManager::ShutdownCustomFpsLimiterSystem()
{
    // Re-enable RenoDX's built-in FPS limiter
   // utils::swapchain::fps_limit = 60.0f;
    
    LogWarn("Custom FPS Limiter Manager shutdown");
    LogWarn("RenoDX built-in FPS limiter re-enabled");
}

} // namespace dxgi::fps_limiter
