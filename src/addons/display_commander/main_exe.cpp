// Source Code <Display Commander>
#include "ui/cli_standalone_ui.hpp"

// Libraries <Windows.h>
#include <Windows.h>

// Standalone .exe entry. Uses the same init path as the DLL in no-ReShade mode (ProcessAttach_NoReShadeModeInit)
// then runs the standalone settings UI on the main thread. Build with DISPLAY_COMMANDER_BUILD_EXE defined.
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInst;
    (void)lpCmdLine;
    (void)nCmdShow;
    RunDisplayCommanderStandalone(hInst);
    return 0;
}
