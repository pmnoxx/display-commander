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

// Helper functions for thumbstick processing
void ApplyThumbstickProcessing(XINPUT_STATE* pState, float left_max_input, float right_max_input,
                            float left_min_output, float right_min_output,
                            float left_deadzone, float right_deadzone);

} // namespace renodx::hooks
