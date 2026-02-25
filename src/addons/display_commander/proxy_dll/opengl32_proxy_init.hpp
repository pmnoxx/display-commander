#pragma once

// Preload the real system opengl32.dll. Safe to call from DllMain DLL_PROCESS_ATTACH
// (uses GetSystemDirectoryW + LoadLibraryW with full path). When this DLL is used
// as an opengl32 proxy, calling this in DllMain avoids loading on first gl/wgl call.
void LoadRealOpenGL32FromDllMain();
