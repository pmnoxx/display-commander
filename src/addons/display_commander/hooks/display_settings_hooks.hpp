#pragma once

#include <windows.h>
#include <atomic>

// Display settings hook function pointer types
typedef LONG(WINAPI* ChangeDisplaySettingsA_pfn)(DEVMODEA* lpDevMode, DWORD dwFlags);
typedef LONG(WINAPI* ChangeDisplaySettingsW_pfn)(DEVMODEW* lpDevMode, DWORD dwFlags);
typedef LONG(WINAPI* ChangeDisplaySettingsExA_pfn)(LPCSTR lpszDeviceName, DEVMODEA* lpDevMode, HWND hWnd, DWORD dwFlags,
                                                   LPVOID lParam);
typedef LONG(WINAPI* ChangeDisplaySettingsExW_pfn)(LPCWSTR lpszDeviceName, DEVMODEW* lpDevMode, HWND hWnd,
                                                   DWORD dwFlags, LPVOID lParam);

// Window management hook function pointer types (for OpenGL fullscreen prevention)
// SetWindowPos_pfn moved to api_hooks.hpp to avoid duplicate declaration
typedef BOOL(WINAPI* ShowWindow_pfn)(HWND hWnd, int nCmdShow);

// Display settings hook counters (defined in globals.hpp)

// Original function pointers
extern ChangeDisplaySettingsA_pfn ChangeDisplaySettingsA_Original;
extern ChangeDisplaySettingsW_pfn ChangeDisplaySettingsW_Original;
extern ChangeDisplaySettingsExA_pfn ChangeDisplaySettingsExA_Original;
extern ChangeDisplaySettingsExW_pfn ChangeDisplaySettingsExW_Original;

// Window management original function pointers
// SetWindowPos_Original moved to api_hooks.hpp to avoid duplicate declaration
extern ShowWindow_pfn ShowWindow_Original;

// Hook installation function
bool InstallDisplaySettingsHooks();

// Hook uninstallation function
void UninstallDisplaySettingsHooks();

// Check if hooks are installed
bool AreDisplaySettingsHooksInstalled();

// Direct function that bypasses hooks - use this when we want to change resolution ourselves
LONG ChangeDisplaySettingsExW_Direct(LPCWSTR lpszDeviceName, DEVMODEW* lpDevMode, HWND hWnd, DWORD dwFlags,
                                     LPVOID lParam);
