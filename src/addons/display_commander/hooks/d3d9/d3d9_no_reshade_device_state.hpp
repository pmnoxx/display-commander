#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

namespace display_commanderhooks::d3d9 {

// Snapshot of the last D3D9 device creation in no-ReShade path (CreateDevice or CreateDeviceEx detour).
// Used by the UI to show "Last D3D9 (no-ReShade): CreateDeviceEx, FLIPEX, 3 buffers, ...".
struct D3D9NoReShadeDeviceSnapshot {
    bool created_with_ex{false};   // true = CreateDeviceEx, false = CreateDevice
    uint32_t back_buffer_count{0};
    uint32_t swap_effect{0};        // D3DSWAPEFFECT_* (e.g. 5 = FLIPEX)
    uint32_t presentation_interval{0};  // D3DPRESENT_INTERVAL_*
    int windowed{0};                // 0 = fullscreen, 1 = windowed
};

// Updated from CreateDevice_Detour / CreateDeviceEx_Detour on success. UI loads and displays.
extern std::atomic<std::shared_ptr<const D3D9NoReShadeDeviceSnapshot>> g_last_d3d9_no_reshade_device_snapshot;

}  // namespace display_commanderhooks::d3d9
