#pragma once

#include <d3d9.h>

namespace display_commanderhooks::d3d9 {

// Applies the same D3D9 swap chain upgrades as OnCreateSwapchainCapture2 (ReShade path)
// but to raw D3DPRESENT_PARAMETERS. Used when hooking CreateDevice/CreateDeviceEx in no-ReShade mode.
// - is_create_device_ex: true when called from CreateDeviceEx (FLIPEX and full upgrades apply);
//   false when called from CreateDevice (only back buffer count and prevent fullscreen apply; no FLIPEX).
// Returns true if any parameter was modified.
bool ApplyD3D9PresentParameterUpgrades(D3DPRESENT_PARAMETERS* pp, bool is_create_device_ex);

}  // namespace display_commanderhooks::d3d9
