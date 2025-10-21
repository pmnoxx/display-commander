#pragma once

#include <windows.h>
#include <hidsdi.h>

namespace display_commanderhooks {

// Function pointer types for additional HID functions
using WriteFile_pfn = BOOL(WINAPI*)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
using DeviceIoControl_pfn = BOOL(WINAPI*)(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using HidD_GetPreparsedData_pfn = BOOLEAN(__stdcall*)(HANDLE, PHIDP_PREPARSED_DATA*);
using HidD_FreePreparsedData_pfn = BOOLEAN(__stdcall*)(PHIDP_PREPARSED_DATA);
using HidP_GetCaps_pfn = BOOLEAN(__stdcall*)(PHIDP_PREPARSED_DATA, PHIDP_CAPS);
using HidD_GetManufacturerString_pfn = BOOLEAN(__stdcall*)(HANDLE, PVOID, ULONG);
using HidD_GetProductString_pfn = BOOLEAN(__stdcall*)(HANDLE, PVOID, ULONG);
using HidD_GetSerialNumberString_pfn = BOOLEAN(__stdcall*)(HANDLE, PVOID, ULONG);
using HidD_GetNumInputBuffers_pfn = BOOLEAN(__stdcall*)(HANDLE, PULONG);
using HidD_SetNumInputBuffers_pfn = BOOLEAN(__stdcall*)(HANDLE, ULONG);
using HidD_GetFeature_pfn = BOOLEAN(__stdcall*)(HANDLE, PVOID, ULONG);
using HidD_SetFeature_pfn = BOOLEAN(__stdcall*)(HANDLE, PVOID, ULONG);

// Additional HID hook function pointers
extern WriteFile_pfn WriteFile_Original;
extern DeviceIoControl_pfn DeviceIoControl_Original;
extern HidD_GetPreparsedData_pfn HidD_GetPreparsedData_Original;
extern HidD_FreePreparsedData_pfn HidD_FreePreparsedData_Original;
extern HidP_GetCaps_pfn HidP_GetCaps_Original;
extern HidD_GetManufacturerString_pfn HidD_GetManufacturerString_Original;
extern HidD_GetProductString_pfn HidD_GetProductString_Original;
extern HidD_GetSerialNumberString_pfn HidD_GetSerialNumberString_Original;
extern HidD_GetNumInputBuffers_pfn HidD_GetNumInputBuffers_Original;
extern HidD_SetNumInputBuffers_pfn HidD_SetNumInputBuffers_Original;
extern HidD_GetFeature_pfn HidD_GetFeature_Original;
extern HidD_SetFeature_pfn HidD_SetFeature_Original;

// Hooked additional HID functions
BOOL WINAPI WriteFile_Detour(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped);
BOOL WINAPI DeviceIoControl_Detour(HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped);
BOOLEAN __stdcall HidD_GetPreparsedData_Detour(HANDLE HidDeviceObject, PHIDP_PREPARSED_DATA* PreparsedData);
BOOLEAN __stdcall HidD_FreePreparsedData_Detour(PHIDP_PREPARSED_DATA PreparsedData);
BOOLEAN __stdcall HidP_GetCaps_Detour(PHIDP_PREPARSED_DATA PreparsedData, PHIDP_CAPS Capabilities);
BOOLEAN __stdcall HidD_GetManufacturerString_Detour(HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength);
BOOLEAN __stdcall HidD_GetProductString_Detour(HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength);
BOOLEAN __stdcall HidD_GetSerialNumberString_Detour(HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength);
BOOLEAN __stdcall HidD_GetNumInputBuffers_Detour(HANDLE HidDeviceObject, PULONG NumberBuffers);
BOOLEAN __stdcall HidD_SetNumInputBuffers_Detour(HANDLE HidDeviceObject, ULONG NumberBuffers);
BOOLEAN __stdcall HidD_GetFeature_Detour(HANDLE HidDeviceObject, PVOID ReportBuffer, ULONG ReportBufferLength);
BOOLEAN __stdcall HidD_SetFeature_Detour(HANDLE HidDeviceObject, PVOID ReportBuffer, ULONG ReportBufferLength);

// Hook management
bool InstallAdditionalHIDHooks();
void UninstallAdditionalHIDHooks();

} // namespace display_commanderhooks
