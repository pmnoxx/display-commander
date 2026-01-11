#pragma once

#include <windows.h>
#include <string>

namespace display_commander::utils {

// Platform API types
enum class PlatformAPI {
    None,
    Steam,
    Epic,
    GOG,
    Xbox,
    Origin,
    Uplay,
    BattleNet,
    Bethesda,
    Rockstar,
    Unknown
};

// Convert DLL name to platform API enum
PlatformAPI DetectPlatformAPIFromDLLName(const std::wstring& dll_name);

// Get platform API name as string
const char* GetPlatformAPIName(PlatformAPI api);

// Detect platform APIs from loaded modules and output results
void DetectAndLogPlatformAPIs();

} // namespace display_commander::utils
