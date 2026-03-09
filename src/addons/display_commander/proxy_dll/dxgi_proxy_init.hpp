#pragma once

// Install MinHook on real dxgi.dll CreateDXGIFactory2. Call from addon init (not DllMain).
// No-op if real DXGI not loaded, hook suppression enabled, or already installed.
void InstallRealDXGIMinHookHooks();
