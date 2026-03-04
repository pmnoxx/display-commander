#pragma once

#include <cstdint>
#include <string>

#include <windows.h>

namespace display_commander {

// Runs RunDLL_NvAPI_SetDWORD via rundll32.exe with "runas" (elevated) so the setting
// is applied with admin privileges. Use when SetProfileSetting fails with
// NVAPI_INVALID_USER_PRIVILEGE. exePath is the full path to the game executable (e.g. "C:\\Games\\game.exe").
// Returns true if the elevated process was started (UAC may still cancel); false if
// building or launching the command failed.
// If outProcess is non-null and the function returns true, *outProcess receives the
// process handle (caller must CloseHandle). Use this to wait for the process to exit
// and then refresh profile data.
// If outError is non-null and the function returns false, *outError receives a short
// reason (e.g. "Could not get DLL path" or "ShellExecute failed: Access is denied").
// If resultFilePath is non-empty, it is passed to the elevated process; that process
// writes "OK" or "ERROR: <message>" to the file so the caller can read the outcome.
bool RunNvApiSetDwordAsAdmin(std::uint32_t settingId, std::uint32_t value,
                             const std::wstring& exeName, HANDLE* outProcess = nullptr,
                             std::string* outError = nullptr,
                             const std::wstring* resultFilePath = nullptr);

}  // namespace display_commander
