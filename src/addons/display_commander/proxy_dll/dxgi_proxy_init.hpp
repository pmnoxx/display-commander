#pragma once

// Preload the real system dxgi.dll. Safe to call from DllMain DLL_PROCESS_ATTACH
// (uses GetSystemDirectoryW + LoadLibraryW with full path). When this DLL is used
// as a dxgi proxy, calling this in DllMain avoids loading on first CreateDXGIFactory.
void LoadRealDXGIFromDllMain();

// Install MinHook on real dxgi.dll CreateDXGIFactory2. Call from addon init (not DllMain).
// No-op if real DXGI not loaded, hook suppression enabled, or already installed.
void InstallRealDXGIMinHookHooks();
