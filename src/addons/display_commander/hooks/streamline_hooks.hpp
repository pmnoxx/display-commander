#pragma once

// Source Code <Display Commander>

#include <cstdint>

#include <windows.h>

// Streamline hook functions
bool InstallStreamlineHooks(HMODULE streamline_module = nullptr);

// Get last SDK version from slInit calls
uint64_t GetLastStreamlineSDKVersion();
