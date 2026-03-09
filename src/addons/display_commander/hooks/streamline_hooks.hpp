#pragma once

// Source Code <Display Commander>
#include "../utils/dlss_fix_api_state.hpp"

#include <cstdint>
#include <vector>

#include <windows.h>

// Streamline hook functions
bool InstallStreamlineHooks(HMODULE streamline_module = nullptr);

// Get last SDK version from slInit calls
uint64_t GetLastStreamlineSDKVersion();

// DLSS-fix: fill entries for the 6 Streamline APIs that need proxy→native conversion (hooked + call count)
void GetDLSSFixStreamlineAPIEntries(std::vector<display_commander::DLSSFixAPIEntry>& out);
