#pragma once

#include <windows.h>

namespace display_commanderhooks {

// Install WinMM joystick hooks (joyGetPos, joyGetPosEx) when winmm.dll is loaded.
// Returns true if hooks were installed; false if suppressed, already installed, or module is null.
bool InstallWinMMJoystickHooks(HMODULE hWinMM);

}  // namespace display_commanderhooks
