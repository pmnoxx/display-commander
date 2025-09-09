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
using ClipCursor_pfn = BOOL(WINAPI*)(const RECT*);
using GetCursorPos_pfn = BOOL(WINAPI*)(LPPOINT);
using SetCursorPos_pfn = BOOL(WINAPI*)(int, int);
using GetKeyState_pfn = SHORT(WINAPI*)(int);
using GetAsyncKeyState_pfn = SHORT(WINAPI*)(int);
using SetWindowsHookExA_pfn = HHOOK(WINAPI*)(int, HOOKPROC, HINSTANCE, DWORD);
using SetWindowsHookExW_pfn = HHOOK(WINAPI*)(int, HOOKPROC, HINSTANCE, DWORD);
using UnhookWindowsHookEx_pfn = BOOL(WINAPI*)(HHOOK);
using GetRawInputBuffer_pfn = UINT(WINAPI*)(PRAWINPUT, PUINT, UINT);
using TranslateMessage_pfn = BOOL(WINAPI*)(const MSG*);
using DispatchMessageA_pfn = LRESULT(WINAPI*)(const MSG*);
using DispatchMessageW_pfn = LRESULT(WINAPI*)(const MSG*);
using GetRawInputData_pfn = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
using RegisterRawInputDevices_pfn = BOOL(WINAPI*)(PCRAWINPUTDEVICE, UINT, UINT);

// Original function pointers
extern GetMessageA_pfn GetMessageA_Original;
extern GetMessageW_pfn GetMessageW_Original;
extern PeekMessageA_pfn PeekMessageA_Original;
extern PeekMessageW_pfn PeekMessageW_Original;
extern PostMessageA_pfn PostMessageA_Original;
extern PostMessageW_pfn PostMessageW_Original;
extern GetKeyboardState_pfn GetKeyboardState_Original;
extern ClipCursor_pfn ClipCursor_Original;
extern GetCursorPos_pfn GetCursorPos_Original;
extern SetCursorPos_pfn SetCursorPos_Original;
extern GetKeyState_pfn GetKeyState_Original;
extern GetAsyncKeyState_pfn GetAsyncKeyState_Original;
extern SetWindowsHookExA_pfn SetWindowsHookExA_Original;
extern SetWindowsHookExW_pfn SetWindowsHookExW_Original;
extern UnhookWindowsHookEx_pfn UnhookWindowsHookEx_Original;
extern GetRawInputBuffer_pfn GetRawInputBuffer_Original;
extern TranslateMessage_pfn TranslateMessage_Original;
extern DispatchMessageA_pfn DispatchMessageA_Original;
extern DispatchMessageW_pfn DispatchMessageW_Original;
extern GetRawInputData_pfn GetRawInputData_Original;
extern RegisterRawInputDevices_pfn RegisterRawInputDevices_Original;

// Hooked message functions
BOOL WINAPI GetMessageA_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
BOOL WINAPI GetMessageW_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
BOOL WINAPI PeekMessageA_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
BOOL WINAPI PeekMessageW_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
BOOL WINAPI PostMessageA_Detour(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL WINAPI PostMessageW_Detour(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL WINAPI GetKeyboardState_Detour(PBYTE lpKeyState);
BOOL WINAPI ClipCursor_Detour(const RECT* lpRect);
BOOL WINAPI GetCursorPos_Detour(LPPOINT lpPoint);
BOOL WINAPI SetCursorPos_Detour(int X, int Y);
SHORT WINAPI GetKeyState_Detour(int vKey);
SHORT WINAPI GetAsyncKeyState_Detour(int vKey);
HHOOK WINAPI SetWindowsHookExA_Detour(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId);
HHOOK WINAPI SetWindowsHookExW_Detour(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId);
BOOL WINAPI UnhookWindowsHookEx_Detour(HHOOK hhk);
UINT WINAPI GetRawInputBuffer_Detour(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader);
BOOL WINAPI TranslateMessage_Detour(const MSG* lpMsg);
LRESULT WINAPI DispatchMessageA_Detour(const MSG* lpMsg);
LRESULT WINAPI DispatchMessageW_Detour(const MSG* lpMsg);
UINT WINAPI GetRawInputData_Detour(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader);
BOOL WINAPI RegisterRawInputDevices_Detour(PCRAWINPUTDEVICE pRawInputDevices, UINT uiNumDevices, UINT cbSize);

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
