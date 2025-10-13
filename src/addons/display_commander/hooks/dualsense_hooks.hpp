#pragma once

#include <windows.h>
#include <XInput.h>
#include <hidsdi.h>

namespace display_commander::hooks {

// Function pointer types for direct HID calls (bypassing suppression)
using ReadFile_pfn = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using HidD_GetInputReport_pfn = BOOLEAN(__stdcall*)(HANDLE, PVOID, ULONG);
using HidD_GetAttributes_pfn = BOOLEAN(__stdcall*)(HANDLE, PHIDD_ATTRIBUTES);

// Direct function pointers for bypassing HID suppression
extern ReadFile_pfn ReadFile_Direct;
extern HidD_GetInputReport_pfn HidD_GetInputReport_Direct;
extern HidD_GetAttributes_pfn HidD_GetAttributes_Direct;

// Direct HID function initialization
void InitializeDirectHIDFunctions();

// DualSense to XInput conversion functions
bool InitializeDualSenseSupport();
void CleanupDualSenseSupport();
bool IsDualSenseAvailable();
bool ConvertDualSenseToXInput(DWORD user_index, XINPUT_STATE* pState);

// DualSense state structure (simplified version based on Special-K)
struct DualSenseState {
    // Button states (using XInput button mapping)
    WORD buttons;

    // Analog sticks (0-255 range, converted to XInput -32768 to 32767)
    SHORT left_stick_x;
    SHORT left_stick_y;
    SHORT right_stick_x;
    SHORT right_stick_y;

    // Triggers (0-255 range, converted to XInput 0-255)
    BYTE left_trigger;
    BYTE right_trigger;

    // Connection state
    bool connected;

    // Packet number for change detection
    DWORD packet_number;
};

// Global DualSense state for all controllers
extern DualSenseState g_dualsense_states[XUSER_MAX_COUNT];

} // namespace display_commander::hooks
