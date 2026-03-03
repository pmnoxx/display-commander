#pragma once

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

#include <d3d11.h>

namespace display_commanderhooks::d3d11 {

/** Hook the D3D11 device (e.g. install vtable hooks for CreateTexture2D and other methods).
 *  Called from init_swapchain when API is D3D11, with device from swapchain->get_device()->get_native().
 *  Returns true if hooks were installed or device was already hooked; false on failure.
 *  Tracks hooked devices so each device is only hooked once. */
bool HookD3D11Device(ID3D11Device* device);

}  // namespace display_commanderhooks::d3d11
