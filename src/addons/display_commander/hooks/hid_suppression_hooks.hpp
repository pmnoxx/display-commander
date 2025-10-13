#pragma once

#include <windows.h>
#include <hidsdi.h>

namespace renodx::hooks {

// Function pointer types for HID functions
using ReadFile_pfn = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using HidD_GetInputReport_pfn = BOOLEAN(__stdcall*)(HANDLE, PVOID, ULONG);
using HidD_GetAttributes_pfn = BOOLEAN(__stdcall*)(HANDLE, PHIDD_ATTRIBUTES);

// HID suppression hook function pointers
extern ReadFile_pfn ReadFile_Original;
extern HidD_GetInputReport_pfn HidD_GetInputReport_Original;
extern HidD_GetAttributes_pfn HidD_GetAttributes_Original;

// Hooked HID functions
BOOL WINAPI ReadFile_Detour(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
BOOLEAN __stdcall HidD_GetInputReport_Detour(HANDLE HidDeviceObject, PVOID ReportBuffer, ULONG ReportBufferLength);
BOOLEAN __stdcall HidD_GetAttributes_Detour(HANDLE HidDeviceObject, PHIDD_ATTRIBUTES Attributes);

// Hook management
bool InstallHIDSuppressionHooks();
void UninstallHIDSuppressionHooks();
bool AreHIDSuppressionHooksInstalled();

// Helper functions
bool IsDualSenseDevice(USHORT vendorId, USHORT productId);
bool ShouldSuppressHIDInput();
void SetHIDSuppressionEnabled(bool enabled);
bool IsHIDSuppressionEnabled();

} // namespace renodx::hooks

