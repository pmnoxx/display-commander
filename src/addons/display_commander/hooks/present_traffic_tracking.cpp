#include "present_traffic_tracking.hpp"
#include "../utils/timing.hpp"

#include <string>

namespace display_commanderhooks {

std::atomic<uint64_t> g_last_dxgi_present_time_ns{0};
std::atomic<uint64_t> g_last_d3d9_present_time_ns{0};
std::atomic<uint64_t> g_last_opengl_swapbuffers_time_ns{0};
std::atomic<uint64_t> g_last_ddraw_flip_time_ns{0};

std::string GetPresentTrafficApisString() {
    const uint64_t now_ns = static_cast<uint64_t>(utils::get_now_ns());
    const uint64_t one_sec_ns = static_cast<uint64_t>(utils::SEC_TO_NS);
    std::string result;
    auto add = [&result](const char* name) {
        if (!result.empty()) result += ", ";
        result += name;
    };
    const uint64_t dxgi = g_last_dxgi_present_time_ns.load(std::memory_order_relaxed);
    if (dxgi != 0 && (now_ns - dxgi) < one_sec_ns) add("DXGI");
    const uint64_t d3d9 = g_last_d3d9_present_time_ns.load(std::memory_order_relaxed);
    if (d3d9 != 0 && (now_ns - d3d9) < one_sec_ns) add("D3D9");
    const uint64_t ogl = g_last_opengl_swapbuffers_time_ns.load(std::memory_order_relaxed);
    if (ogl != 0 && (now_ns - ogl) < one_sec_ns) add("OpenGL32");
    const uint64_t ddraw_ts = g_last_ddraw_flip_time_ns.load(std::memory_order_relaxed);
    if (ddraw_ts != 0 && (now_ns - ddraw_ts) < one_sec_ns) add("DDraw");
    return result;
}

}  // namespace display_commanderhooks
