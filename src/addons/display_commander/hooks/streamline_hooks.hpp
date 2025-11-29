#pragma once

#include <windows.h>
#include <cstdint>

// Streamline hook functions
bool InstallStreamlineHooks(HMODULE streamline_module = nullptr);

// Get last SDK version from slInit calls
uint64_t GetLastStreamlineSDKVersion();