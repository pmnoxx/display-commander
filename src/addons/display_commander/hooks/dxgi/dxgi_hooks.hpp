#pragma once

#include <Windows.h>

namespace display_commanderhooks {

bool InstallDxgiFactoryHooks(HMODULE dxgi_module);
bool InstallD3D11DeviceHooks(HMODULE d3d11_module);
bool InstallD3D12DeviceHooks(HMODULE d3d12_module);

}  // namespace display_commanderhooks
