#pragma once

#include <cstdint>
#include <string>

#include <windows.h>

namespace display_commander {

// Runs RunDLL_NvAPI_SetDWORD via rundll32.exe with "runas" (elevated) so the setting
// is applied with admin privileges. Use when SetProfileSetting fails with
// NVAPI_INVALID_USER_PRIVILEGE. exeName is the game executable name (e.g. "game.exe").
// Returns true if the elevated process was started (UAC may still cancel); false if
// building or launching the command failed.
// If outProcess is non-null and the function returns true, *outProcess receives the
// process handle (caller must CloseHandle). Use this to wait for the process to exit
// and then refresh profile data.
bool RunNvApiSetDwordAsAdmin(std::uint32_t settingId, std::uint32_t value,
                             const std::wstring& exeName, HANDLE* outProcess = nullptr);

}  // namespace display_commander
