#pragma once

#include <windows.h>

#include <XInput.h>

namespace display_commanderhooks {

// Function pointer types
using XInputGetState_pfn = DWORD(WINAPI *)(DWORD, XINPUT_STATE *);
using XInputGetStateEx_pfn = DWORD(WINAPI *)(DWORD, XINPUT_STATE *);
using XInputSetState_pfn = DWORD(WINAPI *)(DWORD, XINPUT_VIBRATION *);
using XInputGetBatteryInformation_pfn = DWORD(WINAPI *)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION *);


// XInput function pointers for direct calls
extern XInputGetStateEx_pfn XInputGetStateEx_Direct;
extern XInputSetState_pfn XInputSetState_Direct;
extern XInputGetBatteryInformation_pfn XInputGetBatteryInformation_Direct;

// Hooked XInput functions
DWORD WINAPI XInputGetState_Detour(DWORD dwUserIndex, XINPUT_STATE *pState);
DWORD WINAPI XInputGetStateEx_Detour(DWORD dwUserIndex, XINPUT_STATE *pState);

// Hook management
bool InstallXInputHooks();
void UninstallXInputHooks();
bool AreXInputHooksInstalled();

// Helper functions for thumbstick processing
void ApplyThumbstickProcessing(XINPUT_STATE *pState, float left_max_input, float right_max_input, float left_min_output,
                               float right_min_output, float left_deadzone, float right_deadzone);

} // namespace display_commanderhooks
