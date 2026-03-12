#pragma once

#include <stdint.h>
#include <windows.h>

namespace display_commanderhooks {

// Function pointer type for rand
using Rand_pfn = int(__cdecl *)();

// Function pointer type for rand_s
using Rand_s_pfn = errno_t(__cdecl *)(unsigned int*);

// Original function pointers
extern Rand_pfn Rand_Original;
extern Rand_s_pfn Rand_s_Original;

// Hooked rand function
int __cdecl Rand_Detour();

// Hooked rand_s function
errno_t __cdecl Rand_s_Detour(unsigned int* randomValue);

// Hook management
bool InstallRandHooks();
void UninstallRandHooks();
bool AreRandHooksInstalled();

// Statistics
uint64_t GetRandCallCount();
uint64_t GetRand_sCallCount();

} // namespace display_commanderhooks

