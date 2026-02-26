#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace display_commanderhooks {

// Last call timestamps (nanoseconds, utils::get_now_ns()) for present detours.
// Updated by IDXGISwapChain_Present_Detour, IDirect3DDevice9_Present_Detour, wglSwapBuffers_Detour.
extern std::atomic<uint64_t> g_last_dxgi_present_time_ns;
extern std::atomic<uint64_t> g_last_d3d9_present_time_ns;
extern std::atomic<uint64_t> g_last_opengl_swapbuffers_time_ns;

// Returns comma-separated list of API names that had present traffic in the last 1 second
// (e.g. "DXGI", "D3D9", "OpenGL32"). Empty string if none.
std::string GetPresentTrafficApisString();

}  // namespace display_commanderhooks
