#include "custom_fps_limiter.hpp"
#include "../addon.hpp"
#include <algorithm>
#include "../../utils/timing.hpp"

namespace dxgi::fps_limiter {
    HANDLE m_timer_handle = nullptr;

CustomFpsLimiter::CustomFpsLimiter()
    : last_time_point_qpc(0)
{
}

void CustomFpsLimiter::LimitFrameRate(double fps)
{
    LONGLONG wait_target_qpc = last_time_point_qpc + (QPC_PER_SECOND / fps);
    utils::wait_until_qpc(wait_target_qpc, m_timer_handle);
    last_time_point_qpc = get_now_qpc();
}
} // namespace dxgi::fps_limiter
