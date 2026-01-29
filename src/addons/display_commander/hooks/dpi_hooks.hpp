#pragma once

#include <windows.h>

namespace display_commanderhooks::dpi {

// Function pointer types for DPI APIs
using GetDpiForSystem_pfn = UINT(WINAPI*)();
using GetDpiForWindow_pfn = UINT(WINAPI*)(HWND);
using GetSystemDpiForProcess_pfn = UINT(WINAPI*)(HANDLE);
using GetSystemMetricsForDpi_pfn = int(WINAPI*)(int, UINT);
using AdjustWindowRectExForDpi_pfn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
using EnableNonClientDpiScaling_pfn = BOOL(WINAPI*)(HWND);
using SystemParametersInfoForDpi_pfn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
using SetThreadDpiHostingBehavior_pfn = DPI_HOSTING_BEHAVIOR(WINAPI*)(DPI_HOSTING_BEHAVIOR);
using SetThreadDpiAwarenessContext_pfn = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
using SetProcessDpiAwarenessContext_pfn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);

// Original function pointers
extern GetDpiForSystem_pfn GetDpiForSystem_Original;
extern GetDpiForWindow_pfn GetDpiForWindow_Original;
extern GetSystemDpiForProcess_pfn GetSystemDpiForProcess_Original;
extern GetSystemMetricsForDpi_pfn GetSystemMetricsForDpi_Original;
extern AdjustWindowRectExForDpi_pfn AdjustWindowRectExForDpi_Original;
extern EnableNonClientDpiScaling_pfn EnableNonClientDpiScaling_Original;
extern SystemParametersInfoForDpi_pfn SystemParametersInfoForDpi_Original;
extern SetThreadDpiHostingBehavior_pfn SetThreadDpiHostingBehavior_Original;
extern SetThreadDpiAwarenessContext_pfn SetThreadDpiAwarenessContext_Original;
extern SetProcessDpiAwarenessContext_pfn SetProcessDpiAwarenessContext_Original;

// Hooked functions
UINT WINAPI GetDpiForSystem_Detour();
UINT WINAPI GetDpiForWindow_Detour(HWND hwnd);
UINT WINAPI GetSystemDpiForProcess_Detour(HANDLE hProcess);
int WINAPI GetSystemMetricsForDpi_Detour(int nIndex, UINT dpi);
BOOL WINAPI AdjustWindowRectExForDpi_Detour(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle, UINT dpi);
BOOL WINAPI EnableNonClientDpiScaling_Detour(HWND hwnd);
BOOL WINAPI SystemParametersInfoForDpi_Detour(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni, UINT dpi);
DPI_HOSTING_BEHAVIOR WINAPI SetThreadDpiHostingBehavior_Detour(DPI_HOSTING_BEHAVIOR value);
DPI_AWARENESS_CONTEXT WINAPI SetThreadDpiAwarenessContext_Detour(DPI_AWARENESS_CONTEXT dpiContext);
BOOL WINAPI SetProcessDpiAwarenessContext_Detour(DPI_AWARENESS_CONTEXT value);

// Hook management
bool InstallDpiHooks();
void UninstallDpiHooks();

}  // namespace display_commanderhooks::dpi
