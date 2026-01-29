#pragma once

#include <windows.h>

namespace display_commander::display::dpi {

// Check if DPI awareness is set via AppCompat registry
bool IsDPIAwarenessUsingAppCompat();

// Force DPI awareness via AppCompat registry (persistent across restarts)
void ForceDPIAwarenessUsingAppCompat(bool set);

// Set per-monitor DPI awareness for the current process
void SetMonitorDPIAwareness(bool bOnlyIfWin10);

// Disable DPI scaling by making process DPI-aware
// Uses AppCompat for persistence and sets per-monitor awareness
void DisableDPIScaling();

// Enable DPI scaling by removing AppCompat registry entry
// Removes the HIGHDPIAWARE flag from the registry
void EnableDPIScaling();

}  // namespace display_commander::display::dpi
