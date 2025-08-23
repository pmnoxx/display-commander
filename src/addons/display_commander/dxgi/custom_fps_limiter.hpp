#pragma once

#include <chrono>
#include <deque>
#include <thread>

namespace renodx::dxgi::fps_limiter {

class CustomFpsLimiter {
public:
    CustomFpsLimiter();
    ~CustomFpsLimiter() = default;

    // Main FPS limiting function - call this in OnPresent
    void LimitFrameRate();
    
    // Frame lifecycle hooks - call these to get accurate timing
    void OnFrameBegin();  // Call in OnBeginRenderPass
    void OnFrameEnd();    // Call in OnEndRenderPass
    
    // Basic configuration
    void SetTargetFps(float fps);
    float GetTargetFps() const { return m_target_fps; }
    
    // Enable/disable
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

private:
    // Internal timing functions (cloned from RenoDX)
    void UpdateTiming();
    void ApplySleepAndSpinLock();
    
    // Configuration
    float m_target_fps;
    bool m_enabled;
    
    // Frame lifecycle timing (NEW - matches RenoDX approach)
    std::chrono::high_resolution_clock::time_point m_frame_start_time;
    std::chrono::high_resolution_clock::time_point m_frame_end_time;
    std::chrono::high_resolution_clock::time_point m_last_present_time;
    
    // Timing state (exact RenoDX variable names)
    std::chrono::high_resolution_clock::time_point last_time_point;
    std::chrono::nanoseconds spin_lock_duration{0};
    
    // Sleep latency tracking (exact RenoDX variable names)
    std::deque<std::chrono::nanoseconds> sleep_latency_history;
    static constexpr size_t MAX_LATENCY_HISTORY_SIZE = 1000;  // Match RenoDX exactly
    
    // Failure tracking (exact RenoDX variable names)
    std::uint32_t spin_lock_failures = 0;
    
    // Last FPS limit for reset detection (exact RenoDX variable name)
    float last_fps_limit = 0.0f;
    
    // Frame state tracking
    bool m_frame_in_progress = false;
};

} // namespace renodx::dxgi::fps_limiter
