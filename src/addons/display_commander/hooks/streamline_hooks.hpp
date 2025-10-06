#pragma once

#include <cstdint>

// Streamline hook functions
bool InstallStreamlineHooks();
void UninstallStreamlineHooks();

// Get last SDK version from slInit calls
uint64_t GetLastStreamlineSDKVersion();