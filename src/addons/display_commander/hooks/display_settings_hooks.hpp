#pragma once

#include <windows.h>
#include <atomic>

// Display settings hook function pointer types
typedef LONG (WINAPI *ChangeDisplaySettingsA_pfn)(DEVMODEA *lpDevMode, DWORD dwFlags);
typedef LONG (WINAPI *ChangeDisplaySettingsW_pfn)(DEVMODEW *lpDevMode, DWORD dwFlags);
typedef LONG (WINAPI *ChangeDisplaySettingsExA_pfn)(LPCSTR lpszDeviceName, DEVMODEA *lpDevMode, HWND hWnd, DWORD dwFlags, LPVOID lParam);
typedef LONG (WINAPI *ChangeDisplaySettingsExW_pfn)(LPCWSTR lpszDeviceName, DEVMODEW *lpDevMode, HWND hWnd, DWORD dwFlags, LPVOID lParam);

// Window management hook function pointer types (for OpenGL fullscreen prevention)
typedef BOOL (WINAPI *SetWindowPos_pfn)(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);
typedef BOOL (WINAPI *ShowWindow_pfn)(HWND hWnd, int nCmdShow);
typedef LONG (WINAPI *SetWindowLongA_pfn)(HWND hWnd, int nIndex, LONG dwNewLong);
typedef LONG (WINAPI *SetWindowLongW_pfn)(HWND hWnd, int nIndex, LONG dwNewLong);
typedef LONG_PTR (WINAPI *SetWindowLongPtrA_pfn)(HWND hWnd, int nIndex, LONG_PTR dwNewLong);
typedef LONG_PTR (WINAPI *SetWindowLongPtrW_pfn)(HWND hWnd, int nIndex, LONG_PTR dwNewLong);

// Display settings hook counters (defined in globals.hpp)

// Original function pointers
extern ChangeDisplaySettingsA_pfn ChangeDisplaySettingsA_Original;
extern ChangeDisplaySettingsW_pfn ChangeDisplaySettingsW_Original;
extern ChangeDisplaySettingsExA_pfn ChangeDisplaySettingsExA_Original;
extern ChangeDisplaySettingsExW_pfn ChangeDisplaySettingsExW_Original;

// Window management original function pointers
extern SetWindowPos_pfn SetWindowPos_Original;
extern ShowWindow_pfn ShowWindow_Original;
extern SetWindowLongA_pfn SetWindowLongA_Original;
extern SetWindowLongW_pfn SetWindowLongW_Original;
extern SetWindowLongPtrA_pfn SetWindowLongPtrA_Original;
extern SetWindowLongPtrW_pfn SetWindowLongPtrW_Original;

// Hook installation function
bool InstallDisplaySettingsHooks();

// Hook uninstallation function
void UninstallDisplaySettingsHooks();

// Check if hooks are installed
bool AreDisplaySettingsHooksInstalled();
