#pragma once

#include <windows.h>
#include <wingdi.h>  // For DISPLAYCONFIG_* structures
#include <array>
#include <atomic>
#include "../../globals.hpp"  // For InputBlockingMode enum

namespace display_commanderhooks {

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

// DLL group enumeration
enum class DllGroup { USER32, XINPUT1_4, KERNEL32, DINPUT8, DINPUT, OPENGL, DISPLAY_SETTINGS, HID_API, COUNT };

// Hook information structure
struct HookInfo {
    const char* name;
    DllGroup dll_group;
};

// Hook call statistics
enum HookIndex {
    // user32.dll hooks (0-34)
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
    HOOK_DefRawInputProc,
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
    HOOK_SetCapture,
    HOOK_ReleaseCapture,
    HOOK_MapVirtualKey,
    HOOK_MapVirtualKeyEx,
    HOOK_DisplayConfigGetDeviceInfo,

    // xinput1_4.dll hooks (35-37)
    HOOK_XInputGetState,
    HOOK_XInputGetStateEx,
    HOOK_XInputSetState,
    HOOK_XInputGetCapabilities,

    // kernel32.dll hooks (38-44)
    HOOK_Sleep,
    HOOK_SleepEx,
    HOOK_WaitForSingleObject,
    HOOK_WaitForMultipleObjects,
    HOOK_SetUnhandledExceptionFilter,
    HOOK_IsDebuggerPresent,
    HOOK_SetThreadExecutionState,

    // dinput8.dll hooks (44)
    HOOK_DInput8CreateDevice,

    // dinput.dll hooks (45)
    HOOK_DInputCreateDevice,

    // OpenGL/WGL hooks (46-60)
    HOOK_wglSwapBuffers,
    HOOK_wglMakeCurrent,
    HOOK_wglCreateContext,
    HOOK_wglDeleteContext,
    HOOK_wglChoosePixelFormat,
    HOOK_wglSetPixelFormat,
    HOOK_wglGetPixelFormat,
    HOOK_wglDescribePixelFormat,
    HOOK_wglCreateContextAttribsARB,
    HOOK_wglChoosePixelFormatARB,
    HOOK_wglGetPixelFormatAttribivARB,
    HOOK_wglGetPixelFormatAttribfvARB,
    HOOK_wglGetProcAddress,
    HOOK_wglSwapIntervalEXT,
    HOOK_wglGetSwapIntervalEXT,

    // Display Settings hooks (61-70)
    HOOK_ChangeDisplaySettingsA,
    HOOK_ChangeDisplaySettingsW,
    HOOK_ChangeDisplaySettingsExA,
    HOOK_ChangeDisplaySettingsExW,
    HOOK_SetWindowPos,
    HOOK_ShowWindow,
    HOOK_SetWindowLongA,
    HOOK_SetWindowLongW,
    HOOK_SetWindowLongPtrA,
    HOOK_SetWindowLongPtrW,

    // HID API hooks (71-87)
    HOOK_HID_CreateFileA,
    HOOK_HID_CreateFileW,
    HOOK_HID_ReadFile,
    HOOK_HID_WriteFile,
    HOOK_HID_DeviceIoControl,
    HOOK_HIDD_GetInputReport,
    HOOK_HIDD_GetAttributes,
    HOOK_HIDD_GetPreparsedData,
    HOOK_HIDD_FreePreparsedData,
    HOOK_HIDD_GetCaps,
    HOOK_HIDD_GetManufacturerString,
    HOOK_HIDD_GetProductString,
    HOOK_HIDD_GetSerialNumberString,
    HOOK_HIDD_GetNumInputBuffers,
    HOOK_HIDD_SetNumInputBuffers,
    HOOK_HIDD_GetFeature,
    HOOK_HIDD_SetFeature,

    HOOK_COUNT
};

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
using DefRawInputProc_pfn = LRESULT(WINAPI*)(PRAWINPUT, INT, UINT);
using VkKeyScan_pfn = SHORT(WINAPI*)(CHAR);
using VkKeyScanEx_pfn = SHORT(WINAPI*)(CHAR, HKL);
using ToAscii_pfn = int(WINAPI*)(UINT, UINT, const BYTE*, LPWORD, UINT);
using ToAsciiEx_pfn = int(WINAPI*)(UINT, UINT, const BYTE*, LPWORD, UINT, HKL);
using ToUnicode_pfn = int(WINAPI*)(UINT, UINT, const BYTE*, LPWSTR, int, UINT);
using ToUnicodeEx_pfn = int(WINAPI*)(UINT, UINT, const BYTE*, LPWSTR, int, UINT, HKL);
using GetKeyNameTextA_pfn = int(WINAPI*)(LONG, LPSTR, int);
using GetKeyNameTextW_pfn = int(WINAPI*)(LONG, LPWSTR, int);
using SendInput_pfn = UINT(WINAPI*)(UINT, LPINPUT, int);
using keybd_event_pfn = void(WINAPI*)(BYTE, BYTE, DWORD, ULONG_PTR);
using mouse_event_pfn = void(WINAPI*)(DWORD, DWORD, DWORD, DWORD, ULONG_PTR);
using SetCapture_pfn = HWND(WINAPI*)(HWND);
using ReleaseCapture_pfn = BOOL(WINAPI*)();
using MapVirtualKey_pfn = UINT(WINAPI*)(UINT, UINT);
using MapVirtualKeyEx_pfn = UINT(WINAPI*)(UINT, UINT, HKL);
using DisplayConfigGetDeviceInfo_pfn = LONG(WINAPI*)(DISPLAYCONFIG_DEVICE_INFO_HEADER*);
using SetUnhandledExceptionFilter_pfn = LPTOP_LEVEL_EXCEPTION_FILTER(WINAPI*)(LPTOP_LEVEL_EXCEPTION_FILTER);
using IsDebuggerPresent_pfn = BOOL(WINAPI*)();

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
extern DefRawInputProc_pfn DefRawInputProc_Original;
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
extern SetCapture_pfn SetCapture_Original;
extern ReleaseCapture_pfn ReleaseCapture_Original;
extern MapVirtualKey_pfn MapVirtualKey_Original;
extern MapVirtualKeyEx_pfn MapVirtualKeyEx_Original;
extern DisplayConfigGetDeviceInfo_pfn DisplayConfigGetDeviceInfo_Original;
extern SetUnhandledExceptionFilter_pfn SetUnhandledExceptionFilter_Original;
extern IsDebuggerPresent_pfn IsDebuggerPresent_Original;

// Hooked message functions
BOOL WINAPI GetMessageA_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
BOOL WINAPI GetMessageW_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
BOOL WINAPI PeekMessageA_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
BOOL WINAPI PeekMessageW_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
BOOL WINAPI PostMessageA_Detour(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL WINAPI PostMessageW_Detour(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL WINAPI GetKeyboardState_Detour(PBYTE lpKeyState);
BOOL WINAPI ClipCursor_Detour(const RECT* lpRect);
BOOL ClipCursor_Direct(const RECT* lpRect);
void RestoreClipCursor();
void ClipCursorToGameWindow();
BOOL WINAPI GetCursorPos_Detour(LPPOINT lpPoint);
// Spoof lpPoint (screen coords) to render-space screen coords when translate-mouse-position is on
void ApplyTranslateMousePositionToCursorPos(LPPOINT lpPoint);
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
LRESULT WINAPI DefRawInputProc_Detour(PRAWINPUT paRawInput, INT nInput, UINT cbSizeHeader);
SHORT WINAPI VkKeyScan_Detour(CHAR ch);
SHORT WINAPI VkKeyScanEx_Detour(CHAR ch, HKL dwhkl);
int WINAPI ToAscii_Detour(UINT uVirtKey, UINT uScanCode, const BYTE* lpKeyState, LPWORD lpChar, UINT uFlags);
int WINAPI ToAsciiEx_Detour(UINT uVirtKey, UINT uScanCode, const BYTE* lpKeyState, LPWORD lpChar, UINT uFlags,
                            HKL dwhkl);
int WINAPI ToUnicode_Detour(UINT wVirtKey, UINT wScanCode, const BYTE* lpKeyState, LPWSTR pwszBuff, int cchBuff,
                            UINT wFlags);
int WINAPI ToUnicodeEx_Detour(UINT wVirtKey, UINT wScanCode, const BYTE* lpKeyState, LPWSTR pwszBuff, int cchBuff,
                              UINT wFlags, HKL dwhkl);
int WINAPI GetKeyNameTextA_Detour(LONG lParam, LPSTR lpString, int cchSize);
int WINAPI GetKeyNameTextW_Detour(LONG lParam, LPWSTR lpString, int cchSize);
UINT WINAPI SendInput_Detour(UINT nInputs, LPINPUT pInputs, int cbSize);
void WINAPI keybd_event_Detour(BYTE bVk, BYTE bScan, DWORD dwFlags, ULONG_PTR dwExtraInfo);
void WINAPI mouse_event_Detour(DWORD dwFlags, DWORD dx, DWORD dy, DWORD dwData, ULONG_PTR dwExtraInfo);
HWND WINAPI SetCapture_Detour(HWND hWnd);
BOOL WINAPI ReleaseCapture_Detour();
UINT WINAPI MapVirtualKey_Detour(UINT uCode, UINT uMapType);
UINT WINAPI MapVirtualKeyEx_Detour(UINT uCode, UINT uMapType, HKL dwhkl);
LONG WINAPI DisplayConfigGetDeviceInfo_Detour(DISPLAYCONFIG_DEVICE_INFO_HEADER* requestPacket);
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI
SetUnhandledExceptionFilter_Detour(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter);
BOOL WINAPI IsDebuggerPresent_Detour();

// Hook management
bool InstallWindowsMessageHooks();
void UninstallWindowsMessageHooks();

// Helper functions
bool ShouldSuppressMessage(HWND hWnd, UINT uMsg);
void SuppressMessage(LPMSG lpMsg);

// Debug: suppress all GetMessage/PeekMessage (default off, not saved). Use to test if we forgot to spoof some message type for continue rendering.
bool GetDebugSuppressAllGetMessage();
void SetDebugSuppressAllGetMessage(bool enable);

// Input blocking helper functions
bool ShouldBlockKeyboardInput(bool assume_foreground);
bool ShouldBlockMouseInput(bool assume_foreground);
bool ShouldBlockGamepadInput();

// Hook call statistics
extern std::array<HookCallStats, HOOK_COUNT> g_hook_stats;

// Hook statistics access functions
const HookCallStats& GetHookStats(int hook_index);
void ResetAllHookStats();
int GetHookCount();
const char* GetHookName(int hook_index);

// DLL group helper functions
const char* GetDllGroupName(DllGroup group);
DllGroup GetHookDllGroup(int hook_index);
const HookInfo& GetHookInfo(int hook_index);

// Keyboard state tracking
namespace keyboard_tracker {

// Initialize keyboard tracking
void Initialize();

// Update keyboard state (call each frame)
void Update();

// Reset frame states (call after processing shortcuts)
void ResetFrame();

// Check if a key is currently down
bool IsKeyDown(int vKey);

// Check if a key was just pressed this frame
bool IsKeyPressed(int vKey);

}  // namespace keyboard_tracker

// Exclusive key groups management
namespace exclusive_key_groups {

// Check if a key should be suppressed due to exclusive key groups
// Returns true if the key should be suppressed (another key in its group is down)
bool ShouldSuppressKey(int vKey);

// Update tracked state - mark a key as down in its exclusive group
// Records the timestamp when the key was pressed
void MarkKeyDown(int vKey);

// Update tracked state - mark a key as up in its exclusive group
// When a key is released, checks which keys are still actually pressed and keeps only the most recently pressed one
void MarkKeyUp(int vKey);

// Check if a key is actually pressed (not simulated)
bool IsKeyActuallyPressed(int vKey);

// Initialize exclusive key groups system
void Initialize();

// Update exclusive key groups state (call each frame from ProcessHotkeys)
// Simulates key presses for keys that became active but were previously suppressed
void Update();

// Update cached list of keys belonging to active groups (call once per second from continuous monitoring)
void UpdateCachedActiveKeys();

}  // namespace exclusive_key_groups

}  // namespace display_commanderhooks
