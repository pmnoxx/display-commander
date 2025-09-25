#pragma once

#include <chrono>
#include <deque>
#include <thread>
#include <windows.h>

namespace dxgi::fps_limiter {

class CustomFpsLimiter {
  public:
    CustomFpsLimiter();
    ~CustomFpsLimiter() = default;

    // Main FPS limiting function - call this in OnPresent
    void LimitFrameRate();
    void LimitFrameRate(double fps);

  private:
    // QPC for sleep
    LONGLONG last_time_point_ns = 0;
};

} // namespace dxgi::fps_limiter
