#pragma once

#include <d3d9.h>

namespace display_commanderhooks::d3d9 {

// Install vtable hooks on the D3D9 device to log calls and errors.
// Called when we receive a device from ReShade (OnInitSwapchain).
// IDirect3DDevice9: logs every call (CALL_GUARD) and logs failures to a separate channel.
// IDirect3DDevice9Ex-only slots: log first call per function only, plus all errors.
// Safe to call multiple times; installs hooks only once per process.
void InstallD3D9DeviceVtableLogging(IDirect3DDevice9* device);

}  // namespace display_commanderhooks::d3d9
