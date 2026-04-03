// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "dll_process_attach.hpp"

// Libraries <Windows.h>
#include <Windows.h>

#if !defined(DISPLAY_COMMANDER_BUILD_EXE)
BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {
    (void)lpv_reserved;
    switch (fdw_reason) {
        case DLL_PROCESS_ATTACH:
            display_commander::dll_main::OnProcessAttach(h_module);
            break;
        case DLL_PROCESS_DETACH:
            display_commander::dll_main::OnProcessDetach(h_module);
            break;
    }

    return TRUE;
}
#endif  // !DISPLAY_COMMANDER_BUILD_EXE
