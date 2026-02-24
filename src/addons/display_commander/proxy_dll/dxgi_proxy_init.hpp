#pragma once

// Preload the real system dxgi.dll. Safe to call from DllMain DLL_PROCESS_ATTACH
// (uses GetSystemDirectoryW + LoadLibraryW with full path). When this DLL is used
// as a dxgi proxy, calling this in DllMain avoids loading on first CreateDXGIFactory.
void LoadRealDXGIFromDllMain();
