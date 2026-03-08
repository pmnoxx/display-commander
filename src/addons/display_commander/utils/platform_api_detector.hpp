#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace display_commander::utils {

// Platform API types
enum class PlatformAPI { None, Steam, Epic, GOG, Xbox, Origin, Uplay, BattleNet, Bethesda, Rockstar, Unknown };

// Convert DLL name to platform API enum
PlatformAPI DetectPlatformAPIFromDLLName(const std::wstring& dll_name);

// Get platform API name as string
const char* GetPlatformAPIName(PlatformAPI api);

// Detect platform APIs from loaded modules and output results
void DetectAndLogPlatformAPIs();

// Get list of detected platform APIs from currently loaded modules
std::vector<PlatformAPI> GetDetectedPlatformAPIs();

// Check if executable path matches whitelist patterns
bool TestWhitelist(const std::wstring& executable_path);

// Steam API DLL (steam_api64.dll / steam_api.dll) — path and export presence.
// Only reports the already-loaded module; does not load or search for the DLL.
// GetSteamDLLPath: writes full path of the loaded Steam API module to dest, returns true if loaded.
bool GetSteamDLLPath(wchar_t* dest, size_t max_size);
std::wstring GetSteamDLLPath();
// True if steam_api64.dll (64-bit) or steam_api.dll (32-bit) is loaded in this process.
bool IsSteamAPIModuleLoaded();
// True if the loaded Steam API module exports SteamAPI_Init (undefined if module not loaded).
bool IsSteamAPIInitExportPresent();
// True if the loaded Steam API module exports the given symbol (e.g. "SteamUser", "SteamUserStats").
bool IsSteamAPIExportPresent(const char* export_name);

}  // namespace display_commander::utils
