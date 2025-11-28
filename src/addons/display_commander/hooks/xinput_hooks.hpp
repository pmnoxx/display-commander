#pragma once

#include <windows.h>

#include <XInput.h>

namespace display_commanderhooks {

// Function pointer types
using XInputGetState_pfn = DWORD(WINAPI *)(DWORD, XINPUT_STATE *);
using XInputGetStateEx_pfn = DWORD(WINAPI *)(DWORD, XINPUT_STATE *);
using XInputSetState_pfn = DWORD(WINAPI *)(DWORD, XINPUT_VIBRATION *);
using XInputGetBatteryInformation_pfn = DWORD(WINAPI *)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION *);
using XInputGetCapabilities_pfn = DWORD(WINAPI *)(DWORD, DWORD, XINPUT_CAPABILITIES *);


// XInput function pointers for direct calls
extern XInputGetState_pfn XInputGetState_Direct;
extern XInputGetStateEx_pfn XInputGetStateEx_Direct;
extern XInputSetState_pfn XInputSetState_Direct;
extern XInputGetBatteryInformation_pfn XInputGetBatteryInformation_Direct;
extern XInputGetCapabilities_pfn XInputGetCapabilities_Direct;

// Hooked XInput functions
DWORD WINAPI XInputGetState_Detour(DWORD dwUserIndex, XINPUT_STATE *pState);
DWORD WINAPI XInputGetStateEx_Detour(DWORD dwUserIndex, XINPUT_STATE *pState);
DWORD WINAPI XInputSetState_Detour(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration);
DWORD WINAPI XInputGetCapabilities_Detour(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES *pCapabilities);

// Hook management
bool InstallXInputHooks();

// Helper functions for thumbstick processing
void ApplyThumbstickProcessing(XINPUT_STATE *pState, float left_max_input, float right_max_input, float left_min_output,
                               float right_min_output, float left_deadzone, float right_deadzone,
                               float left_center_x, float left_center_y, float right_center_x, float right_center_y,
                               bool left_circular, bool right_circular);

} // namespace display_commanderhooks
