#pragma once

#include <windows.h>

namespace renodx::hooks {

// Function pointer types for timeslowdown hooks
using QueryPerformanceCounter_pfn = BOOL(WINAPI*)(LARGE_INTEGER* lpPerformanceCount);
using QueryPerformanceFrequency_pfn = BOOL(WINAPI*)(LARGE_INTEGER* lpFrequency);

// Timeslowdown hook function pointers
extern QueryPerformanceCounter_pfn QueryPerformanceCounter_Original;
extern QueryPerformanceFrequency_pfn QueryPerformanceFrequency_Original;

// Hooked API functions
BOOL WINAPI QueryPerformanceCounter_Detour(LARGE_INTEGER* lpPerformanceCount);
BOOL WINAPI QueryPerformanceFrequency_Detour(LARGE_INTEGER* lpFrequency);

// Hook management
bool InstallTimeslowdownHooks();
void UninstallTimeslowdownHooks();
bool AreTimeslowdownHooksInstalled();

// Timeslowdown configuration
void SetTimeslowdownMultiplier(double multiplier);
double GetTimeslowdownMultiplier();
bool IsTimeslowdownEnabled();
void SetTimeslowdownEnabled(bool enabled);

} // namespace renodx::hooks
