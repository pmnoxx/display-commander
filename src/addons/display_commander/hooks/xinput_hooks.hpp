#pragma once

#include <windows.h>
#include <XInput.h>

namespace renodx::hooks {

// Function pointer types
using XInputGetState_pfn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using XInputGetStateEx_pfn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

// XInput hook function pointers
extern XInputGetState_pfn XInputGetState_Original;
extern XInputGetStateEx_pfn XInputGetStateEx_Original;

// Hooked XInput functions
DWORD WINAPI XInputGetState_Detour(DWORD dwUserIndex, XINPUT_STATE* pState);
DWORD WINAPI XInputGetStateEx_Detour(DWORD dwUserIndex, XINPUT_STATE* pState);

// Hook management
bool InstallXInputHooks();
void UninstallXInputHooks();
bool AreXInputHooksInstalled();

// Test function
void TestXInputState();

// Diagnostic function
void DiagnoseXInputModules();

} // namespace renodx::hooks
