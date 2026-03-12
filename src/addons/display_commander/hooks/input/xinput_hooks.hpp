#pragma once

#include <windows.h>
#include <cstdint>
#include <memory>

#include <XInput.h>

namespace display_commander {
namespace widgets {
namespace xinput_widget {
struct XInputSharedState;
}
}
}

namespace display_commanderhooks {

// Function pointer types
using XInputGetState_pfn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using XInputGetStateEx_pfn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using XInputSetState_pfn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
using XInputGetBatteryInformation_pfn = DWORD(WINAPI*)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
using XInputGetCapabilities_pfn = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);

// XInput function pointers for direct calls
extern XInputGetState_pfn XInputGetState_Direct;
extern XInputGetStateEx_pfn XInputGetStateEx_Direct;
extern XInputSetState_pfn XInputSetState_Direct;
extern XInputGetBatteryInformation_pfn XInputGetBatteryInformation_Direct;
extern XInputGetCapabilities_pfn XInputGetCapabilities_Direct;

// Hooked XInput functions
DWORD WINAPI XInputGetState_Detour(DWORD dwUserIndex, XINPUT_STATE* pState);
DWORD WINAPI XInputGetStateEx_Detour(DWORD dwUserIndex, XINPUT_STATE* pState);
DWORD WINAPI XInputSetState_Detour(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration);
DWORD WINAPI XInputGetCapabilities_Detour(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities);

// Hook management
bool InstallXInputHooks(HMODULE xinput_module = nullptr);

// Ensures XInputSetState_Direct is set (for vibration test). Loads an XInput DLL if the game has not loaded one yet.
void EnsureXInputSetStateForTest();

// True if at least one XInput module has been hooked (e.g. xinput1_3.dll)
bool IsXInputHooksInstalled();

// Number of game calls to XInputGetState(dwUserIndex=0) seen by the detour (0 = game may use WGI/other API)
std::uint64_t GetXInputGetStateUserIndexZeroCallCount();

// Last duration (ns) of XInputGetState_Detour_Impl when dwUserIndex=0; 0 if not yet measured
std::uint64_t GetXInputGetStateUserIndexZeroLastDurationNs();

// Helper: apply stick mapping and center calibration (reads all params from shared_state)
void ApplyThumbstickProcessing(
    XINPUT_STATE* pState,
    const std::shared_ptr<display_commander::widgets::xinput_widget::XInputSharedState>& shared_state);

}  // namespace display_commanderhooks
