#pragma once

// Preload the real system dinput8.dll. Safe to call from DllMain DLL_PROCESS_ATTACH
// (uses GetSystemDirectoryW + LoadLibraryW with full path). When this DLL is used
// as a dinput8 proxy, calling this in DllMain avoids loading on first DirectInput8Create.
void LoadRealDinput8FromDllMain();
