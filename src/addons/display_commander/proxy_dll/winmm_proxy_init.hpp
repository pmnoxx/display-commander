#pragma once

// Preload the real winmm.dll (or winmmHooked.dll from same dir). Safe to call from
// DllMain DLL_PROCESS_ATTACH. When this DLL is used as a winmm proxy, calling this
// in DllMain avoids loading on first winmm call.
void LoadRealWinMMFromDllMain();
