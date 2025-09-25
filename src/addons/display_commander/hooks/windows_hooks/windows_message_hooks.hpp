#pragma once

#include <atomic>
#include <windows.h>


namespace renodx::hooks {

// Hook call statistics structure
struct HookCallStats {
    std::atomic<uint64_t> total_calls{0};
    std::atomic<uint64_t> unsuppressed_calls{0};

    void increment_total() { total_calls.fetch_add(1); }
    void increment_unsuppressed() { unsuppressed_calls.fetch_add(1); }
    void reset() {
        total_calls.store(0);
        unsuppressed_calls.store(0);
    }
};

// Hook call statistics
enum HookIndex {
    HOOK_GetMessageA = 0,
    HOOK_GetMessageW,
    HOOK_PeekMessageA,
    HOOK_PeekMessageW,
    HOOK_PostMessageA,
    HOOK_PostMessageW,
    HOOK_GetKeyboardState,
    HOOK_ClipCursor,
    HOOK_GetCursorPos,
    HOOK_SetCursorPos,
    HOOK_GetKeyState,
    HOOK_GetAsyncKeyState,
    HOOK_SetWindowsHookExA,
    HOOK_SetWindowsHookExW,
    HOOK_UnhookWindowsHookEx,
    HOOK_GetRawInputBuffer,
    HOOK_TranslateMessage,
    HOOK_DispatchMessageA,
    HOOK_DispatchMessageW,
    HOOK_GetRawInputData,
    HOOK_RegisterRawInputDevices,
    HOOK_VkKeyScan,
    HOOK_VkKeyScanEx,
    HOOK_ToAscii,
    HOOK_ToAsciiEx,
    HOOK_ToUnicode,
    HOOK_ToUnicodeEx,
    HOOK_GetKeyNameTextA,
    HOOK_GetKeyNameTextW,
    HOOK_SendInput,
    HOOK_keybd_event,
    HOOK_mouse_event,
    HOOK_MapVirtualKey,
    HOOK_MapVirtualKeyEx,
    HOOK_XInputGetState,
    HOOK_XInputGetStateEx,
    HOOK_DirectInputCreateA,
    HOOK_DirectInputCreateW,
    HOOK_DirectInputCreateEx,
    HOOK_DirectInput8Create,
    HOOK_Sleep,
    HOOK_SleepEx,
    HOOK_WaitForSingleObject,
    HOOK_WaitForMultipleObjects,
    HOOK_COUNT
};

// Function pointer types for Windows message functions
using GetMessageA_pfn = BOOL(WINAPI *)(LPMSG, HWND, UINT, UINT);
using GetMessageW_pfn = BOOL(WINAPI *)(LPMSG, HWND, UINT, UINT);
using PeekMessageA_pfn = BOOL(WINAPI *)(LPMSG, HWND, UINT, UINT, UINT);
using PeekMessageW_pfn = BOOL(WINAPI *)(LPMSG, HWND, UINT, UINT, UINT);
using PostMessageA_pfn = BOOL(WINAPI *)(HWND, UINT, WPARAM, LPARAM);
using PostMessageW_pfn = BOOL(WINAPI *)(HWND, UINT, WPARAM, LPARAM);
using GetKeyboardState_pfn = BOOL(WINAPI *)(PBYTE);
using ClipCursor_pfn = BOOL(WINAPI *)(const RECT *);
using GetCursorPos_pfn = BOOL(WINAPI *)(LPPOINT);
using SetCursorPos_pfn = BOOL(WINAPI *)(int, int);
using GetKeyState_pfn = SHORT(WINAPI *)(int);
using GetAsyncKeyState_pfn = SHORT(WINAPI *)(int);
using SetWindowsHookExA_pfn = HHOOK(WINAPI *)(int, HOOKPROC, HINSTANCE, DWORD);
using SetWindowsHookExW_pfn = HHOOK(WINAPI *)(int, HOOKPROC, HINSTANCE, DWORD);
using UnhookWindowsHookEx_pfn = BOOL(WINAPI *)(HHOOK);
using GetRawInputBuffer_pfn = UINT(WINAPI *)(PRAWINPUT, PUINT, UINT);
using TranslateMessage_pfn = BOOL(WINAPI *)(const MSG *);
using DispatchMessageA_pfn = LRESULT(WINAPI *)(const MSG *);
using DispatchMessageW_pfn = LRESULT(WINAPI *)(const MSG *);
using GetRawInputData_pfn = UINT(WINAPI *)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
using RegisterRawInputDevices_pfn = BOOL(WINAPI *)(PCRAWINPUTDEVICE, UINT, UINT);
using VkKeyScan_pfn = SHORT(WINAPI *)(CHAR);
using VkKeyScanEx_pfn = SHORT(WINAPI *)(CHAR, HKL);
using ToAscii_pfn = int(WINAPI *)(UINT, UINT, const BYTE *, LPWORD, UINT);
using ToAsciiEx_pfn = int(WINAPI *)(UINT, UINT, const BYTE *, LPWORD, UINT, HKL);
using ToUnicode_pfn = int(WINAPI *)(UINT, UINT, const BYTE *, LPWSTR, int, UINT);
using ToUnicodeEx_pfn = int(WINAPI *)(UINT, UINT, const BYTE *, LPWSTR, int, UINT, HKL);
using GetKeyNameTextA_pfn = int(WINAPI *)(LONG, LPSTR, int);
using GetKeyNameTextW_pfn = int(WINAPI *)(LONG, LPWSTR, int);
using SendInput_pfn = UINT(WINAPI *)(UINT, LPINPUT, int);
using keybd_event_pfn = void(WINAPI *)(BYTE, BYTE, DWORD, ULONG_PTR);
using mouse_event_pfn = void(WINAPI *)(DWORD, DWORD, DWORD, DWORD, ULONG_PTR);
using MapVirtualKey_pfn = UINT(WINAPI *)(UINT, UINT);
using MapVirtualKeyEx_pfn = UINT(WINAPI *)(UINT, UINT, HKL);

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
extern VkKeyScan_pfn VkKeyScan_Original;
extern VkKeyScanEx_pfn VkKeyScanEx_Original;
extern ToAscii_pfn ToAscii_Original;
extern ToAsciiEx_pfn ToAsciiEx_Original;
extern ToUnicode_pfn ToUnicode_Original;
extern ToUnicodeEx_pfn ToUnicodeEx_Original;
extern GetKeyNameTextA_pfn GetKeyNameTextA_Original;
extern GetKeyNameTextW_pfn GetKeyNameTextW_Original;
extern SendInput_pfn SendInput_Original;
extern keybd_event_pfn keybd_event_Original;
extern mouse_event_pfn mouse_event_Original;
extern MapVirtualKey_pfn MapVirtualKey_Original;
extern MapVirtualKeyEx_pfn MapVirtualKeyEx_Original;

// Hooked message functions
BOOL WINAPI GetMessageA_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
BOOL WINAPI GetMessageW_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
BOOL WINAPI PeekMessageA_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
BOOL WINAPI PeekMessageW_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
BOOL WINAPI PostMessageA_Detour(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL WINAPI PostMessageW_Detour(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL WINAPI GetKeyboardState_Detour(PBYTE lpKeyState);
BOOL WINAPI ClipCursor_Detour(const RECT *lpRect);
BOOL WINAPI GetCursorPos_Detour(LPPOINT lpPoint);
BOOL WINAPI SetCursorPos_Detour(int X, int Y);
SHORT WINAPI GetKeyState_Detour(int vKey);
SHORT WINAPI GetAsyncKeyState_Detour(int vKey);
HHOOK WINAPI SetWindowsHookExA_Detour(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId);
HHOOK WINAPI SetWindowsHookExW_Detour(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId);
BOOL WINAPI UnhookWindowsHookEx_Detour(HHOOK hhk);
UINT WINAPI GetRawInputBuffer_Detour(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader);
BOOL WINAPI TranslateMessage_Detour(const MSG *lpMsg);
LRESULT WINAPI DispatchMessageA_Detour(const MSG *lpMsg);
LRESULT WINAPI DispatchMessageW_Detour(const MSG *lpMsg);
UINT WINAPI GetRawInputData_Detour(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader);
BOOL WINAPI RegisterRawInputDevices_Detour(PCRAWINPUTDEVICE pRawInputDevices, UINT uiNumDevices, UINT cbSize);
SHORT WINAPI VkKeyScan_Detour(CHAR ch);
SHORT WINAPI VkKeyScanEx_Detour(CHAR ch, HKL dwhkl);
int WINAPI ToAscii_Detour(UINT uVirtKey, UINT uScanCode, const BYTE *lpKeyState, LPWORD lpChar, UINT uFlags);
int WINAPI ToAsciiEx_Detour(UINT uVirtKey, UINT uScanCode, const BYTE *lpKeyState, LPWORD lpChar, UINT uFlags,
                            HKL dwhkl);
int WINAPI ToUnicode_Detour(UINT wVirtKey, UINT wScanCode, const BYTE *lpKeyState, LPWSTR pwszBuff, int cchBuff,
                            UINT wFlags);
int WINAPI ToUnicodeEx_Detour(UINT wVirtKey, UINT wScanCode, const BYTE *lpKeyState, LPWSTR pwszBuff, int cchBuff,
                              UINT wFlags, HKL dwhkl);
int WINAPI GetKeyNameTextA_Detour(LONG lParam, LPSTR lpString, int cchSize);
int WINAPI GetKeyNameTextW_Detour(LONG lParam, LPWSTR lpString, int cchSize);
UINT WINAPI SendInput_Detour(UINT nInputs, LPINPUT pInputs, int cbSize);
void WINAPI keybd_event_Detour(BYTE bVk, BYTE bScan, DWORD dwFlags, ULONG_PTR dwExtraInfo);
void WINAPI mouse_event_Detour(DWORD dwFlags, DWORD dx, DWORD dy, DWORD dwData, ULONG_PTR dwExtraInfo);
UINT WINAPI MapVirtualKey_Detour(UINT uCode, UINT uMapType);
UINT WINAPI MapVirtualKeyEx_Detour(UINT uCode, UINT uMapType, HKL dwhkl);

// Hook management
bool InstallWindowsMessageHooks();
void UninstallWindowsMessageHooks();
bool AreWindowsMessageHooksInstalled();

// Helper functions
bool ShouldInterceptMessage(HWND hWnd, UINT uMsg);
void ProcessInterceptedMessage(LPMSG lpMsg);
bool ShouldSuppressMessage(HWND hWnd, UINT uMsg);
void SuppressMessage(LPMSG lpMsg);

// Hook call statistics
extern HookCallStats g_hook_stats[];

// Hook statistics access functions
const HookCallStats &GetHookStats(int hook_index);
void ResetAllHookStats();
int GetHookCount();
const char *GetHookName(int hook_index);

} // namespace renodx::hooks
