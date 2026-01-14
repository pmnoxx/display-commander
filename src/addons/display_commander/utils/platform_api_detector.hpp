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

}  // namespace display_commander::utils
