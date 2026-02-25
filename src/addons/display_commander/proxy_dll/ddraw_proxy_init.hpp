#pragma once

// Preload the real system ddraw.dll. Safe to call from DllMain DLL_PROCESS_ATTACH
// (uses GetSystemDirectoryW + LoadLibraryW with full path). When this DLL is used
// as a ddraw proxy, calling this in DllMain avoids loading on first DirectDrawCreate.
void LoadRealDDrawFromDllMain();
