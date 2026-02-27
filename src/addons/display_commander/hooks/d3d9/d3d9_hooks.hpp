#pragma once

#include <windows.h>
#include <atomic>

namespace display_commanderhooks::d3d9 {

// Install D3D9 hooks when d3d9.dll is loaded (called from LoadLibrary detour).
// Similar to InstallOpenGLHooks for opengl32.dll. Sets up state; device vtable hooks
// are installed when we receive a device from ReShade (init_swapchain / init_device).
bool InstallDX9Hooks(HMODULE hModule);
void UninstallDX9Hooks();
bool AreDX9HooksInstalled();

extern std::atomic<bool> g_dx9_hooks_installed;

}  // namespace display_commanderhooks::d3d9
