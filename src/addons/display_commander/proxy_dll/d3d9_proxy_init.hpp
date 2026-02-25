#pragma once

// Preload the real system d3d9.dll. Safe to call from DllMain DLL_PROCESS_ATTACH
// (uses GetSystemDirectoryW + LoadLibraryW with full path). When this DLL is used
// as a d3d9 proxy, calling this in DllMain avoids loading on first Direct3DCreate9.
void LoadRealD3D9FromDllMain();
