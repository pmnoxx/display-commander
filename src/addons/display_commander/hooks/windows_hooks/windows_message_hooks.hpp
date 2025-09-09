#pragma once

#include <windows.h>

namespace renodx::hooks {

// Function pointer types for Windows message functions
using GetMessageA_pfn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT);
using GetMessageW_pfn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT);
using PeekMessageA_pfn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using PeekMessageW_pfn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using PostMessageA_pfn = BOOL(WINAPI*)(HWND, UINT, WPARAM, LPARAM);
using PostMessageW_pfn = BOOL(WINAPI*)(HWND, UINT, WPARAM, LPARAM);
using GetKeyboardState_pfn = BOOL(WINAPI*)(PBYTE);

// Original function pointers
extern GetMessageA_pfn GetMessageA_Original;
extern GetMessageW_pfn GetMessageW_Original;
extern PeekMessageA_pfn PeekMessageA_Original;
extern PeekMessageW_pfn PeekMessageW_Original;
extern PostMessageA_pfn PostMessageA_Original;
extern PostMessageW_pfn PostMessageW_Original;
extern GetKeyboardState_pfn GetKeyboardState_Original;

// Hooked message functions
BOOL WINAPI GetMessageA_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
BOOL WINAPI GetMessageW_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
BOOL WINAPI PeekMessageA_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
BOOL WINAPI PeekMessageW_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
BOOL WINAPI PostMessageA_Detour(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL WINAPI PostMessageW_Detour(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL WINAPI GetKeyboardState_Detour(PBYTE lpKeyState);

// Hook management
bool InstallWindowsMessageHooks();
void UninstallWindowsMessageHooks();
bool AreWindowsMessageHooksInstalled();

// Helper functions
bool ShouldInterceptMessage(HWND hWnd, UINT uMsg);
void ProcessInterceptedMessage(LPMSG lpMsg);
bool ShouldSuppressMessage(HWND hWnd, UINT uMsg);
void SuppressMessage(LPMSG lpMsg);

} // namespace renodx::hooks
