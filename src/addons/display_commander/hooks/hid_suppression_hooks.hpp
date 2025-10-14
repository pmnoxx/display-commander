#pragma once

#include <windows.h>
#include <hidsdi.h>

namespace renodx::hooks {

// Function pointer types for HID functions
using ReadFile_pfn = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using HidD_GetInputReport_pfn = BOOLEAN(__stdcall*)(HANDLE, PVOID, ULONG);
using HidD_GetAttributes_pfn = BOOLEAN(__stdcall*)(HANDLE, PHIDD_ATTRIBUTES);
using CreateFileA_pfn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileW_pfn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

// HID suppression hook function pointers
extern ReadFile_pfn ReadFile_Original;
extern HidD_GetInputReport_pfn HidD_GetInputReport_Original;
extern HidD_GetAttributes_pfn HidD_GetAttributes_Original;
extern CreateFileA_pfn CreateFileA_Original;
extern CreateFileW_pfn CreateFileW_Original;

// Hooked HID functions
BOOL WINAPI ReadFile_Direct(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
BOOL WINAPI ReadFile_Detour(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
BOOLEAN __stdcall HidD_GetInputReport_Direct(HANDLE HidDeviceObject, PVOID ReportBuffer, ULONG ReportBufferLength);
BOOLEAN __stdcall HidD_GetInputReport_Detour(HANDLE HidDeviceObject, PVOID ReportBuffer, ULONG ReportBufferLength);
BOOLEAN __stdcall HidD_GetAttributes_Direct(HANDLE HidDeviceObject, PHIDD_ATTRIBUTES Attributes);
BOOLEAN __stdcall HidD_GetAttributes_Detour(HANDLE HidDeviceObject, PHIDD_ATTRIBUTES Attributes);
HANDLE WINAPI CreateFileA_Direct(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
HANDLE WINAPI CreateFileA_Detour(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
HANDLE WINAPI CreateFileW_Direct(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
HANDLE WINAPI CreateFileW_Detour(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);

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

