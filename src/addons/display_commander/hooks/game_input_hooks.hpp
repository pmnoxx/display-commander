#pragma once

#include <windows.h>

namespace display_commanderhooks {

/**
 * Install hook on GameInputCreate from GameInput.dll.
 * IGameInput is the Windows Game Input API (GameInputCreate returns IGameInput**).
 * The API is exported from GameInput.dll (Microsoft Game Input redist).
 * When the game calls GameInputCreate, we mark "GameInput" as active for the Controller tab.
 *
 * @param h_module Module handle of GameInput.dll (from LoadLibrary).
 * @return true if the hook was installed.
 */
bool InstallGameInputHooks(HMODULE h_module);

}  // namespace display_commanderhooks
