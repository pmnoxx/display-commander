#pragma once

#include <windows.h>

namespace renodx::hooks {

// Function pointer types
using GetFocus_pfn = HWND(WINAPI*)();
using GetForegroundWindow_pfn = HWND(WINAPI*)();
using GetActiveWindow_pfn = HWND(WINAPI*)();
using GetGUIThreadInfo_pfn = BOOL(WINAPI*)(DWORD, PGUITHREADINFO);

// API hook function pointers
extern GetFocus_pfn GetFocus_Original;
extern GetForegroundWindow_pfn GetForegroundWindow_Original;
extern GetActiveWindow_pfn GetActiveWindow_Original;
extern GetGUIThreadInfo_pfn GetGUIThreadInfo_Original;

// Hooked API functions
HWND WINAPI GetFocus_Detour();
HWND WINAPI GetForegroundWindow_Detour();
HWND WINAPI GetActiveWindow_Detour();
BOOL WINAPI GetGUIThreadInfo_Detour(DWORD idThread, PGUITHREADINFO pgui);

// Hook management
bool InstallApiHooks();
void UninstallApiHooks();
bool AreApiHooksInstalled();

// Helper functions
HWND GetGameWindow();
bool IsGameWindow(HWND hwnd);
void SetGameWindow(HWND hwnd);

} // namespace renodx::hooks
