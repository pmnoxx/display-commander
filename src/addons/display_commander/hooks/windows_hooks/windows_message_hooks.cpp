#include "windows_message_hooks.hpp"
#include "../api_hooks.hpp"  // For GetGameWindow and other functions
#include "../../globals.hpp"    // For s_continue_rendering
#include "../../utils.hpp"
#include <MinHook.h>

namespace renodx::hooks {

// Original function pointers
GetMessageA_pfn GetMessageA_Original = nullptr;
GetMessageW_pfn GetMessageW_Original = nullptr;
PeekMessageA_pfn PeekMessageA_Original = nullptr;
PeekMessageW_pfn PeekMessageW_Original = nullptr;
PostMessageA_pfn PostMessageA_Original = nullptr;
PostMessageW_pfn PostMessageW_Original = nullptr;
GetKeyboardState_pfn GetKeyboardState_Original = nullptr;
ClipCursor_pfn ClipCursor_Original = nullptr;
GetCursorPos_pfn GetCursorPos_Original = nullptr;
SetCursorPos_pfn SetCursorPos_Original = nullptr;
GetKeyState_pfn GetKeyState_Original = nullptr;
GetAsyncKeyState_pfn GetAsyncKeyState_Original = nullptr;
SetWindowsHookExA_pfn SetWindowsHookExA_Original = nullptr;
SetWindowsHookExW_pfn SetWindowsHookExW_Original = nullptr;
UnhookWindowsHookEx_pfn UnhookWindowsHookEx_Original = nullptr;
GetRawInputBuffer_pfn GetRawInputBuffer_Original = nullptr;
TranslateMessage_pfn TranslateMessage_Original = nullptr;
DispatchMessageA_pfn DispatchMessageA_Original = nullptr;
DispatchMessageW_pfn DispatchMessageW_Original = nullptr;
GetRawInputData_pfn GetRawInputData_Original = nullptr;
RegisterRawInputDevices_pfn RegisterRawInputDevices_Original = nullptr;
VkKeyScan_pfn VkKeyScan_Original = nullptr;
VkKeyScanEx_pfn VkKeyScanEx_Original = nullptr;
ToAscii_pfn ToAscii_Original = nullptr;
ToAsciiEx_pfn ToAsciiEx_Original = nullptr;
ToUnicode_pfn ToUnicode_Original = nullptr;
ToUnicodeEx_pfn ToUnicodeEx_Original = nullptr;
GetKeyNameTextA_pfn GetKeyNameTextA_Original = nullptr;
GetKeyNameTextW_pfn GetKeyNameTextW_Original = nullptr;
SendInput_pfn SendInput_Original = nullptr;
keybd_event_pfn keybd_event_Original = nullptr;
mouse_event_pfn mouse_event_Original = nullptr;
MapVirtualKey_pfn MapVirtualKey_Original = nullptr;
MapVirtualKeyEx_pfn MapVirtualKeyEx_Original = nullptr;

// Hook state
static std::atomic<bool> g_message_hooks_installed{false};

// Cursor position tracking (similar to Reshade)
static POINT s_last_cursor_position = {};
static RECT s_last_clip_cursor = {};

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
    HOOK_COUNT
};

// Hook statistics array
HookCallStats g_hook_stats[HOOK_COUNT];

// Hook names for display
static const char* g_hook_names[HOOK_COUNT] = {
    "GetMessageA",
    "GetMessageW",
    "PeekMessageA",
    "PeekMessageW",
    "PostMessageA",
    "PostMessageW",
    "GetKeyboardState",
    "ClipCursor",
    "GetCursorPos",
    "SetCursorPos",
    "GetKeyState",
    "GetAsyncKeyState",
    "SetWindowsHookExA",
    "SetWindowsHookExW",
    "UnhookWindowsHookEx",
    "GetRawInputBuffer",
    "TranslateMessage",
    "DispatchMessageA",
    "DispatchMessageW",
    "GetRawInputData",
    "RegisterRawInputDevices",
    "VkKeyScan",
    "VkKeyScanEx",
    "ToAscii",
    "ToAsciiEx",
    "ToUnicode",
    "ToUnicodeEx",
    "GetKeyNameTextA",
    "GetKeyNameTextW",
    "SendInput",
    "keybd_event",
    "mouse_event",
    "MapVirtualKey",
    "MapVirtualKeyEx"
};

// Helper function to determine if we should intercept messages
bool ShouldInterceptMessage(HWND hWnd, UINT uMsg) {
    // Only intercept if continue rendering is enabled
    if (!s_continue_rendering.load()) {
        return false;
    }

    // Get the game window from API hooks
    HWND gameWindow = GetGameWindow();
    if (gameWindow == nullptr) {
        return false;
    }

    // Check if the message is for the game window or its children
    if (hWnd == nullptr || hWnd == gameWindow || IsChild(gameWindow, hWnd)) {
        return true;
    }

    return false;
}

// Process intercepted messages
void ProcessInterceptedMessage(LPMSG lpMsg) {
    if (lpMsg == nullptr) {
        return;
    }

    // Log message details for debugging
    LogInfo("Intercepted message: HWND=0x%p, Msg=0x%04X, WParam=0x%08X, LParam=0x%08X",
            lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);

    // Handle specific messages if needed
    switch (lpMsg->message) {
        case WM_ACTIVATE:
        case WM_ACTIVATEAPP:
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            // These messages might affect rendering, so we intercept them
            LogInfo("Intercepted focus/activation message: 0x%04X", lpMsg->message);
            break;
        default:
            // For other messages, we just log them
            break;
    }
}

// Check if we should suppress a message (for input blocking)
bool ShouldSuppressMessage(HWND hWnd, UINT uMsg) {
    // Only suppress if input blocking without reshade is enabled
    if (!s_block_input_without_reshade.load()) {
        return false;
    }

    // Get the game window from API hooks
    HWND gameWindow = GetGameWindow();
    if (gameWindow == nullptr) {
        return false;
    }

    // Check if the message is for the game window or its children
    if (hWnd == nullptr || hWnd == gameWindow || IsChild(gameWindow, hWnd)) {
        // Check if it's an input message that should be blocked
        switch (uMsg) {
            // Keyboard messages
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_CHAR:
            case WM_SYSCHAR:
            case WM_DEADCHAR:
            case WM_SYSDEADCHAR:
            // Mouse messages
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_MOUSEMOVE:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
            // Cursor messages
            case WM_SETCURSOR:
                return true;
            default:
                return false;
        }
    }

    return false;
}

// Suppress a message by modifying it
void SuppressMessage(LPMSG lpMsg) {
    if (lpMsg == nullptr) {
        return;
    }

    // Log suppressed input for debugging
    static std::atomic<int> suppress_counter{0};
    int count = suppress_counter.fetch_add(1);

    // Only log every 100th suppressed message to avoid spam
    if (count % 100 == 0) {
        LogInfo("Suppressed input message: HWND=0x%p, Msg=0x%04X, WParam=0x%08X, LParam=0x%08X",
                lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    }

    // For input messages, we can either:
    // 1. Clear the message (set to WM_NULL)
    // 2. Modify the parameters to make it harmless
    // 3. Change the message type to something that won't be processed

    // For now, we'll clear the message by setting it to WM_NULL
    // This effectively removes the input from the message queue
    lpMsg->message = WM_NULL;
    lpMsg->wParam = 0;
    lpMsg->lParam = 0;
}

// Hooked GetMessageA function
BOOL WINAPI GetMessageA_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    // Track total calls
    g_hook_stats[HOOK_GetMessageA].increment_total();

    // Call original function first
    BOOL result = GetMessageA_Original ?
        GetMessageA_Original(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax) :
        GetMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);

    // If we got a message
    if (result > 0 && lpMsg != nullptr) {
        // Check if we should suppress this message (input blocking)
        if (ShouldSuppressMessage(hWnd, lpMsg->message)) {
            SuppressMessage(lpMsg);
        }
        // Check if we should intercept it for other purposes
        else if (ShouldInterceptMessage(hWnd, lpMsg->message)) {
            ProcessInterceptedMessage(lpMsg);
            // Track unsuppressed calls
            g_hook_stats[HOOK_GetMessageA].increment_unsuppressed();
        }
    }

    return result;
}

// Hooked GetMessageW function
BOOL WINAPI GetMessageW_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    // Track total calls
    g_hook_stats[HOOK_GetMessageW].increment_total();

    // Call original function first
    BOOL result = GetMessageW_Original ?
        GetMessageW_Original(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax) :
        GetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);

    // If we got a message
    if (result > 0 && lpMsg != nullptr) {
        // Check if we should suppress this message (input blocking)
        if (ShouldSuppressMessage(hWnd, lpMsg->message)) {
            SuppressMessage(lpMsg);
        }
        // Check if we should intercept it for other purposes
        else if (ShouldInterceptMessage(hWnd, lpMsg->message)) {
            ProcessInterceptedMessage(lpMsg);
            // Track unsuppressed calls
            g_hook_stats[HOOK_GetMessageW].increment_unsuppressed();
        }
    }

    return result;
}

// Hooked PeekMessageA function
BOOL WINAPI PeekMessageA_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    // Call original function first
    BOOL result = PeekMessageA_Original ?
        PeekMessageA_Original(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg) :
        PeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);

    // If we got a message
    if (result && lpMsg != nullptr) {
        // Check if we should suppress this message (input blocking)
        if (ShouldSuppressMessage(hWnd, lpMsg->message)) {
            SuppressMessage(lpMsg);
        }
        // Check if we should intercept it for other purposes
        else if (ShouldInterceptMessage(hWnd, lpMsg->message)) {
            ProcessInterceptedMessage(lpMsg);
        }
    }

    return result;
}

// Hooked PeekMessageW function
BOOL WINAPI PeekMessageW_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    // Call original function first
    BOOL result = PeekMessageW_Original ?
        PeekMessageW_Original(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg) :
        PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);

    // If we got a message
    if (result && lpMsg != nullptr) {
        // Check if we should suppress this message (input blocking)
        if (ShouldSuppressMessage(hWnd, lpMsg->message)) {
            SuppressMessage(lpMsg);
        }
        // Check if we should intercept it for other purposes
        else if (ShouldInterceptMessage(hWnd, lpMsg->message)) {
            ProcessInterceptedMessage(lpMsg);
        }
    }

    return result;
}

// Hooked PostMessageA function
BOOL WINAPI PostMessageA_Detour(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    // Check if we should suppress this message (input blocking)
    if (ShouldSuppressMessage(hWnd, Msg)) {
        // Log suppressed input for debugging
        static std::atomic<int> suppress_counter{0};
        int count = suppress_counter.fetch_add(1);

        // Only log every 100th suppressed message to avoid spam
        if (count % 100 == 0) {
            LogInfo("Suppressed PostMessageA: HWND=0x%p, Msg=0x%04X, WParam=0x%08X, LParam=0x%08X",
                    hWnd, Msg, wParam, lParam);
        }

        // Return TRUE to indicate the message was "processed" (blocked)
        return TRUE;
    }

    // Call original function
    return PostMessageA_Original ?
        PostMessageA_Original(hWnd, Msg, wParam, lParam) :
        PostMessageA(hWnd, Msg, wParam, lParam);
}

// Hooked PostMessageW function
BOOL WINAPI PostMessageW_Detour(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    // Check if we should suppress this message (input blocking)
    if (ShouldSuppressMessage(hWnd, Msg)) {
        // Log suppressed input for debugging
        static std::atomic<int> suppress_counter{0};
        int count = suppress_counter.fetch_add(1);

        // Only log every 100th suppressed message to avoid spam
        if (count % 100 == 0) {
            LogInfo("Suppressed PostMessageW: HWND=0x%p, Msg=0x%04X, WParam=0x%08X, LParam=0x%08X",
                    hWnd, Msg, wParam, lParam);
        }

        // Return TRUE to indicate the message was "processed" (blocked)
        return TRUE;
    }

    // Call original function
    return PostMessageW_Original ?
        PostMessageW_Original(hWnd, Msg, wParam, lParam) :
        PostMessageW(hWnd, Msg, wParam, lParam);
}

// Hooked GetKeyboardState function
BOOL WINAPI GetKeyboardState_Detour(PBYTE lpKeyState) {
    // Call original function first
    BOOL result = GetKeyboardState_Original ?
        GetKeyboardState_Original(lpKeyState) :
        GetKeyboardState(lpKeyState);

    // If input blocking is enabled and we got valid key state data
    if (result && lpKeyState != nullptr && s_block_input_without_reshade.load()) {
        // Clear all key states to simulate no keys being pressed
        // This effectively blocks keyboard input at the state level
        memset(lpKeyState, 0, 256); // 256 bytes for all virtual keys

        // Log occasionally for debugging
        static std::atomic<int> clear_counter{0};
        int count = clear_counter.fetch_add(1);
        if (count % 1000 == 0) {
            LogInfo("Cleared keyboard state for input blocking");
        }
    }

    return result;
}

// Hooked ClipCursor function
BOOL WINAPI ClipCursor_Detour(const RECT* lpRect) {
    // Store the clip rectangle for reference
    s_last_clip_cursor = (lpRect != nullptr) ? *lpRect : RECT{};

    // If input blocking is enabled, disable cursor clipping
    if (s_block_input_without_reshade.load()) {
        // Disable cursor clipping when input is blocked
        lpRect = nullptr;
    }

    // Call original function
    return ClipCursor_Original ?
        ClipCursor_Original(lpRect) :
        ClipCursor(lpRect);
}

// Hooked GetCursorPos function
BOOL WINAPI GetCursorPos_Detour(LPPOINT lpPoint) {
    // If input blocking is enabled, return last known cursor position
    if (s_block_input_without_reshade.load() && lpPoint != nullptr) {
        *lpPoint = s_last_cursor_position;
        return TRUE;
    }

    // Call original function
    BOOL result = GetCursorPos_Original ?
        GetCursorPos_Original(lpPoint) :
        GetCursorPos(lpPoint);

    // Update last known cursor position
    if (result && lpPoint != nullptr) {
        s_last_cursor_position = *lpPoint;
    }

    return result;
}

// Hooked SetCursorPos function
BOOL WINAPI SetCursorPos_Detour(int X, int Y) {
    // Update last known cursor position
    s_last_cursor_position.x = X;
    s_last_cursor_position.y = Y;

    // If input blocking is enabled, block cursor position changes
    if (s_block_input_without_reshade.load()) {
        return TRUE; // Block the cursor position change
    }

    // Call original function
    return SetCursorPos_Original ?
        SetCursorPos_Original(X, Y) :
        SetCursorPos(X, Y);
}

// Hooked GetKeyState function
SHORT WINAPI GetKeyState_Detour(int vKey) {
    // Track total calls
    g_hook_stats[HOOK_GetKeyState].increment_total();

    // If input blocking is enabled, return 0 for all keys
    if (s_block_input_without_reshade.load()) {
        // Block all keyboard keys (0x08-0xFF) and mouse buttons
        if ((vKey >= 0x08 && vKey <= 0xFF) ||
            (vKey >= VK_LBUTTON && vKey <= VK_XBUTTON2)) {
            return 0; // Block input
        }
    }

    // Track unsuppressed calls
    g_hook_stats[HOOK_GetKeyState].increment_unsuppressed();

    // Call original function
    return GetKeyState_Original ?
        GetKeyState_Original(vKey) :
        GetKeyState(vKey);
}

// Hooked GetAsyncKeyState function
SHORT WINAPI GetAsyncKeyState_Detour(int vKey) {
    // Track total calls
    g_hook_stats[HOOK_GetAsyncKeyState].increment_total();

    // If input blocking is enabled, return 0 for all keys
    if (s_block_input_without_reshade.load()) {
        // Block all keyboard keys (0x08-0xFF) and mouse buttons
        if ((vKey >= 0x08 && vKey <= 0xFF) ||
            (vKey >= VK_LBUTTON && vKey <= VK_XBUTTON2)) {
            return 0; // Block input
        }
    }

    // Track unsuppressed calls
    g_hook_stats[HOOK_GetAsyncKeyState].increment_unsuppressed();

    // Call original function
    return GetAsyncKeyState_Original ?
        GetAsyncKeyState_Original(vKey) :
        GetAsyncKeyState(vKey);
}

// Hooked SetWindowsHookExA function
HHOOK WINAPI SetWindowsHookExA_Detour(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId) {
    // Call original function first
    HHOOK result = SetWindowsHookExA_Original ?
        SetWindowsHookExA_Original(idHook, lpfn, hmod, dwThreadId) :
        SetWindowsHookExA(idHook, lpfn, hmod, dwThreadId);

    // Log hook installation for debugging
    if (result != nullptr) {
        LogInfo("SetWindowsHookExA installed: idHook=%d, hmod=0x%p, dwThreadId=%lu", idHook, hmod, dwThreadId);
    }

    return result;
}

// Hooked SetWindowsHookExW function
HHOOK WINAPI SetWindowsHookExW_Detour(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId) {
    // Call original function first
    HHOOK result = SetWindowsHookExW_Original ?
        SetWindowsHookExW_Original(idHook, lpfn, hmod, dwThreadId) :
        SetWindowsHookExW(idHook, lpfn, hmod, dwThreadId);

    // Log hook installation for debugging
    if (result != nullptr) {
        LogInfo("SetWindowsHookExW installed: idHook=%d, hmod=0x%p, dwThreadId=%lu", idHook, hmod, dwThreadId);
    }

    return result;
}

// Hooked UnhookWindowsHookEx function
BOOL WINAPI UnhookWindowsHookEx_Detour(HHOOK hhk) {
    // Log hook removal for debugging
    LogInfo("UnhookWindowsHookEx called: hhk=0x%p", hhk);

    // Call original function
    return UnhookWindowsHookEx_Original ?
        UnhookWindowsHookEx_Original(hhk) :
        UnhookWindowsHookEx(hhk);
}

// Hooked GetRawInputBuffer function
UINT WINAPI GetRawInputBuffer_Detour(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader) {
    // Call original function first
    UINT result = GetRawInputBuffer_Original ?
        GetRawInputBuffer_Original(pData, pcbSize, cbSizeHeader) :
        GetRawInputBuffer(pData, pcbSize, cbSizeHeader);

    // If input blocking is enabled and we got data, filter it
    if (result > 0 && pData != nullptr && pcbSize != nullptr && s_block_input_without_reshade.load()) {
        // Filter out blocked input data
        PRAWINPUT current = pData;
        UINT filtered_count = 0;

        for (UINT i = 0; i < result; ++i) {
            // Check if this input should be blocked
            bool should_block = false;

            if (current->header.dwType == RIM_TYPEKEYBOARD) {
                should_block = true; // Block all keyboard input
            } else if (current->header.dwType == RIM_TYPEMOUSE) {
                should_block = true; // Block all mouse input
            }

            // If not blocked, keep the data
            if (!should_block) {
                if (current != pData + filtered_count) {
                    memmove(pData + filtered_count, current, current->header.dwSize);
                }
                filtered_count++;
            }

            current = (PRAWINPUT)((PBYTE)current + current->header.dwSize);
        }

        // Update the count
        *pcbSize = filtered_count * cbSizeHeader;
        result = filtered_count;

        // Log occasionally for debugging
        static std::atomic<int> filter_counter{0};
        int count = filter_counter.fetch_add(1);
        if (count % 1000 == 0) {
            LogInfo("Filtered raw input buffer: %u -> %u", result, filtered_count);
        }
    }

    return result;
}

// Hooked TranslateMessage function
BOOL WINAPI TranslateMessage_Detour(const MSG* lpMsg) {
    // If input blocking is enabled, don't translate messages that should be blocked
    if (s_block_input_without_reshade.load() && lpMsg != nullptr) {
        // Check if this message should be suppressed
        if (ShouldSuppressMessage(lpMsg->hwnd, lpMsg->message)) {
            // Don't translate blocked messages
            return FALSE;
        }
    }

    // Call original function
    return TranslateMessage_Original ?
        TranslateMessage_Original(lpMsg) :
        TranslateMessage(lpMsg);
}

// Hooked DispatchMessageA function
LRESULT WINAPI DispatchMessageA_Detour(const MSG* lpMsg) {
    // If input blocking is enabled, don't dispatch messages that should be blocked
    if (s_block_input_without_reshade.load() && lpMsg != nullptr) {
        // Check if this message should be suppressed
        if (ShouldSuppressMessage(lpMsg->hwnd, lpMsg->message)) {
            // Return 0 to indicate the message was "processed" (blocked)
            return 0;
        }
    }

    // Call original function
    return DispatchMessageA_Original ?
        DispatchMessageA_Original(lpMsg) :
        DispatchMessageA(lpMsg);
}

// Hooked DispatchMessageW function
LRESULT WINAPI DispatchMessageW_Detour(const MSG* lpMsg) {
    // If input blocking is enabled, don't dispatch messages that should be blocked
    if (s_block_input_without_reshade.load() && lpMsg != nullptr) {
        // Check if this message should be suppressed
        if (ShouldSuppressMessage(lpMsg->hwnd, lpMsg->message)) {
            // Return 0 to indicate the message was "processed" (blocked)
            return 0;
        }
    }

    // Call original function
    return DispatchMessageW_Original ?
        DispatchMessageW_Original(lpMsg) :
        DispatchMessageW(lpMsg);
}

// Hooked GetRawInputData function
UINT WINAPI GetRawInputData_Detour(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader) {
    // Call original function first
    UINT result = GetRawInputData_Original ?
        GetRawInputData_Original(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader) :
        GetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);

    // If input blocking is enabled and we got data, filter it
    if (result != UINT(-1) && pData != nullptr && pcbSize != nullptr && s_block_input_without_reshade.load()) {
        // For raw input data, we need to check if it's keyboard or mouse input
        if (uiCommand == RID_INPUT) {
            RAWINPUT* rawInput = static_cast<RAWINPUT*>(pData);

            // Block keyboard and mouse input
            if (rawInput->header.dwType == RIM_TYPEKEYBOARD || rawInput->header.dwType == RIM_TYPEMOUSE) {
                // Return 0 to indicate no data available (effectively blocking the input)
                *pcbSize = 0;
                return 0;
            }
        }
    }

    return result;
}

// Hooked RegisterRawInputDevices function
BOOL WINAPI RegisterRawInputDevices_Detour(PCRAWINPUTDEVICE pRawInputDevices, UINT uiNumDevices, UINT cbSize) {
    // Log raw input device registration for debugging
    if (pRawInputDevices != nullptr && uiNumDevices > 0) {
        LogInfo("RegisterRawInputDevices called: %u devices", uiNumDevices);
        for (UINT i = 0; i < uiNumDevices; ++i) {
            LogInfo("  Device %u: UsagePage=0x%04X, Usage=0x%04X, Flags=0x%08X, hwndTarget=0x%p",
                    i, pRawInputDevices[i].usUsagePage, pRawInputDevices[i].usUsage,
                    pRawInputDevices[i].dwFlags, pRawInputDevices[i].hwndTarget);
        }
    }

    // Call original function
    return RegisterRawInputDevices_Original ?
        RegisterRawInputDevices_Original(pRawInputDevices, uiNumDevices, cbSize) :
        RegisterRawInputDevices(pRawInputDevices, uiNumDevices, cbSize);
}

// Hooked VkKeyScan function
SHORT WINAPI VkKeyScan_Detour(CHAR ch) {
    // If input blocking is enabled, return -1 to indicate no virtual key found
    if (s_block_input_without_reshade.load()) {
        return -1; // No virtual key found
    }

    // Call original function
    return VkKeyScan_Original ?
        VkKeyScan_Original(ch) :
        VkKeyScan(ch);
}

// Hooked VkKeyScanEx function
SHORT WINAPI VkKeyScanEx_Detour(CHAR ch, HKL dwhkl) {
    // If input blocking is enabled, return -1 to indicate no virtual key found
    if (s_block_input_without_reshade.load()) {
        return -1; // No virtual key found
    }

    // Call original function
    return VkKeyScanEx_Original ?
        VkKeyScanEx_Original(ch, dwhkl) :
        VkKeyScanEx(ch, dwhkl);
}

// Hooked ToAscii function
int WINAPI ToAscii_Detour(UINT uVirtKey, UINT uScanCode, const BYTE* lpKeyState, LPWORD lpChar, UINT uFlags) {
    // If input blocking is enabled, return 0 to indicate no character generated
    if (s_block_input_without_reshade.load()) {
        return 0; // No character generated
    }

    // Call original function
    return ToAscii_Original ?
        ToAscii_Original(uVirtKey, uScanCode, lpKeyState, lpChar, uFlags) :
        ToAscii(uVirtKey, uScanCode, lpKeyState, lpChar, uFlags);
}

// Hooked ToAsciiEx function
int WINAPI ToAsciiEx_Detour(UINT uVirtKey, UINT uScanCode, const BYTE* lpKeyState, LPWORD lpChar, UINT uFlags, HKL dwhkl) {
    // If input blocking is enabled, return 0 to indicate no character generated
    if (s_block_input_without_reshade.load()) {
        return 0; // No character generated
    }

    // Call original function
    return ToAsciiEx_Original ?
        ToAsciiEx_Original(uVirtKey, uScanCode, lpKeyState, lpChar, uFlags, dwhkl) :
        ToAsciiEx(uVirtKey, uScanCode, lpKeyState, lpChar, uFlags, dwhkl);
}

// Hooked ToUnicode function
int WINAPI ToUnicode_Detour(UINT wVirtKey, UINT wScanCode, const BYTE* lpKeyState, LPWSTR pwszBuff, int cchBuff, UINT wFlags) {
    // If input blocking is enabled, return 0 to indicate no character generated
    if (s_block_input_without_reshade.load()) {
        return 0; // No character generated
    }

    // Call original function
    return ToUnicode_Original ?
        ToUnicode_Original(wVirtKey, wScanCode, lpKeyState, pwszBuff, cchBuff, wFlags) :
        ToUnicode(wVirtKey, wScanCode, lpKeyState, pwszBuff, cchBuff, wFlags);
}

// Hooked ToUnicodeEx function
int WINAPI ToUnicodeEx_Detour(UINT wVirtKey, UINT wScanCode, const BYTE* lpKeyState, LPWSTR pwszBuff, int cchBuff, UINT wFlags, HKL dwhkl) {
    // If input blocking is enabled, return 0 to indicate no character generated
    if (s_block_input_without_reshade.load()) {
        return 0; // No character generated
    }

    // Call original function
    return ToUnicodeEx_Original ?
        ToUnicodeEx_Original(wVirtKey, wScanCode, lpKeyState, pwszBuff, cchBuff, wFlags, dwhkl) :
        ToUnicodeEx(wVirtKey, wScanCode, lpKeyState, pwszBuff, cchBuff, wFlags, dwhkl);
}

// Hooked GetKeyNameTextA function
int WINAPI GetKeyNameTextA_Detour(LONG lParam, LPSTR lpString, int cchSize) {
    // If input blocking is enabled, return 0 to indicate no key name
    if (s_block_input_without_reshade.load()) {
        HWND gameWindow = GetGameWindow();
        if (gameWindow != nullptr) {
            if (lpString != nullptr && cchSize > 0) {
                lpString[0] = '\0'; // Empty string
            }
            return 0; // No key name
        }
    }

    // Call original function
    return GetKeyNameTextA_Original ?
        GetKeyNameTextA_Original(lParam, lpString, cchSize) :
        GetKeyNameTextA(lParam, lpString, cchSize);
}

// Hooked GetKeyNameTextW function
int WINAPI GetKeyNameTextW_Detour(LONG lParam, LPWSTR lpString, int cchSize) {
    // If input blocking is enabled, return 0 to indicate no key name
    if (s_block_input_without_reshade.load()) {
        if (lpString != nullptr && cchSize > 0) {
            lpString[0] = L'\0'; // Empty string
        }
        return 0; // No key name
    }

    // Call original function
    return GetKeyNameTextW_Original ?
        GetKeyNameTextW_Original(lParam, lpString, cchSize) :
        GetKeyNameTextW(lParam, lpString, cchSize);
}

// Hooked SendInput function
UINT WINAPI SendInput_Detour(UINT nInputs, LPINPUT pInputs, int cbSize) {
    // If input blocking is enabled, block all input generation
    if (s_block_input_without_reshade.load()) {
        // Log blocked input for debugging
        static std::atomic<int> block_counter{0};
        int count = block_counter.fetch_add(1);
        if (count % 100 == 0) {
            LogInfo("Blocked SendInput: nInputs=%u", nInputs);
        }
        return 0; // Block input generation
    }

    // Call original function
    return SendInput_Original ?
        SendInput_Original(nInputs, pInputs, cbSize) :
        SendInput(nInputs, pInputs, cbSize);
}

// Hooked keybd_event function
void WINAPI keybd_event_Detour(BYTE bVk, BYTE bScan, DWORD dwFlags, ULONG_PTR dwExtraInfo) {
    // If input blocking is enabled, block keyboard event generation
    if (s_block_input_without_reshade.load()) {
        // Log blocked input for debugging
        static std::atomic<int> block_counter{0};
        int count = block_counter.fetch_add(1);
        if (count % 100 == 0) {
            LogInfo("Blocked keybd_event: bVk=0x%02X, dwFlags=0x%08X", bVk, dwFlags);
        }
        return; // Block keyboard event generation
    }

    // Call original function
    if (keybd_event_Original) {
        keybd_event_Original(bVk, bScan, dwFlags, dwExtraInfo);
    } else {
        keybd_event(bVk, bScan, dwFlags, dwExtraInfo);
    }
}

// Hooked mouse_event function
void WINAPI mouse_event_Detour(DWORD dwFlags, DWORD dx, DWORD dy, DWORD dwData, ULONG_PTR dwExtraInfo) {
    // If input blocking is enabled, block mouse event generation
    if (s_block_input_without_reshade.load()) {
        // Log blocked input for debugging
        static std::atomic<int> block_counter{0};
        int count = block_counter.fetch_add(1);
        if (count % 100 == 0) {
            LogInfo("Blocked mouse_event: dwFlags=0x%08X, dx=%u, dy=%u", dwFlags, dx, dy);
        }
        return; // Block mouse event generation
    }

    // Call original function
    if (mouse_event_Original) {
        mouse_event_Original(dwFlags, dx, dy, dwData, dwExtraInfo);
    } else {
        mouse_event(dwFlags, dx, dy, dwData, dwExtraInfo);
    }
}

// Hooked MapVirtualKey function
UINT WINAPI MapVirtualKey_Detour(UINT uCode, UINT uMapType) {
    // If input blocking is enabled, return 0 for virtual key mapping
    if (s_block_input_without_reshade.load()) {
        return 0; // Block virtual key mapping
    }

    // Call original function
    return MapVirtualKey_Original ?
        MapVirtualKey_Original(uCode, uMapType) :
        MapVirtualKey(uCode, uMapType);
}

// Hooked MapVirtualKeyEx function
UINT WINAPI MapVirtualKeyEx_Detour(UINT uCode, UINT uMapType, HKL dwhkl) {
    // If input blocking is enabled, return 0 for virtual key mapping
    if (s_block_input_without_reshade.load()) {
        return 0; // Block virtual key mapping
    }

    // Call original function
    return MapVirtualKeyEx_Original ?
        MapVirtualKeyEx_Original(uCode, uMapType, dwhkl) :
        MapVirtualKeyEx(uCode, uMapType, dwhkl);
}

// Install Windows message hooks
bool InstallWindowsMessageHooks() {
    if (g_message_hooks_installed.load()) {
        LogInfo("Windows message hooks already installed");
        return true;
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for Windows message hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with Windows message hooks");
    } else {
        LogInfo("MinHook initialized successfully for Windows message hooks");
    }

    // Hook GetMessageA
    if (MH_CreateHook(GetMessageA, GetMessageA_Detour, (LPVOID*)&GetMessageA_Original) != MH_OK) {
        LogError("Failed to create GetMessageA hook");
        return false;
    }

    // Hook GetMessageW
    if (MH_CreateHook(GetMessageW, GetMessageW_Detour, (LPVOID*)&GetMessageW_Original) != MH_OK) {
        LogError("Failed to create GetMessageW hook");
        return false;
    }

    // Hook PeekMessageA
    if (MH_CreateHook(PeekMessageA, PeekMessageA_Detour, (LPVOID*)&PeekMessageA_Original) != MH_OK) {
        LogError("Failed to create PeekMessageA hook");
        return false;
    }

    // Hook PeekMessageW
    if (MH_CreateHook(PeekMessageW, PeekMessageW_Detour, (LPVOID*)&PeekMessageW_Original) != MH_OK) {
        LogError("Failed to create PeekMessageW hook");
        return false;
    }

    // Hook PostMessageA
    if (MH_CreateHook(PostMessageA, PostMessageA_Detour, (LPVOID*)&PostMessageA_Original) != MH_OK) {
        LogError("Failed to create PostMessageA hook");
        return false;
    }

    // Hook PostMessageW
    if (MH_CreateHook(PostMessageW, PostMessageW_Detour, (LPVOID*)&PostMessageW_Original) != MH_OK) {
        LogError("Failed to create PostMessageW hook");
        return false;
    }

    // Hook GetKeyboardState
    if (MH_CreateHook(GetKeyboardState, GetKeyboardState_Detour, (LPVOID*)&GetKeyboardState_Original) != MH_OK) {
        LogError("Failed to create GetKeyboardState hook");
        return false;
    }

    // Hook ClipCursor
    if (MH_CreateHook(ClipCursor, ClipCursor_Detour, (LPVOID*)&ClipCursor_Original) != MH_OK) {
        LogError("Failed to create ClipCursor hook");
        return false;
    }

    // Hook GetCursorPos
    if (MH_CreateHook(GetCursorPos, GetCursorPos_Detour, (LPVOID*)&GetCursorPos_Original) != MH_OK) {
        LogError("Failed to create GetCursorPos hook");
        return false;
    }

    // Hook SetCursorPos
    if (MH_CreateHook(SetCursorPos, SetCursorPos_Detour, (LPVOID*)&SetCursorPos_Original) != MH_OK) {
        LogError("Failed to create SetCursorPos hook");
        return false;
    }

    // Hook GetKeyState
    if (MH_CreateHook(GetKeyState, GetKeyState_Detour, (LPVOID*)&GetKeyState_Original) != MH_OK) {
        LogError("Failed to create GetKeyState hook");
        return false;
    }

    // Hook GetAsyncKeyState
    if (MH_CreateHook(GetAsyncKeyState, GetAsyncKeyState_Detour, (LPVOID*)&GetAsyncKeyState_Original) != MH_OK) {
        LogError("Failed to create GetAsyncKeyState hook");
        return false;
    }

    // Hook SetWindowsHookExA
    if (MH_CreateHook(SetWindowsHookExA, SetWindowsHookExA_Detour, (LPVOID*)&SetWindowsHookExA_Original) != MH_OK) {
        LogError("Failed to create SetWindowsHookExA hook");
        return false;
    }

    // Hook SetWindowsHookExW
    if (MH_CreateHook(SetWindowsHookExW, SetWindowsHookExW_Detour, (LPVOID*)&SetWindowsHookExW_Original) != MH_OK) {
        LogError("Failed to create SetWindowsHookExW hook");
        return false;
    }

    // Hook UnhookWindowsHookEx
    if (MH_CreateHook(UnhookWindowsHookEx, UnhookWindowsHookEx_Detour, (LPVOID*)&UnhookWindowsHookEx_Original) != MH_OK) {
        LogError("Failed to create UnhookWindowsHookEx hook");
        return false;
    }

    // Hook GetRawInputBuffer
    if (MH_CreateHook(GetRawInputBuffer, GetRawInputBuffer_Detour, (LPVOID*)&GetRawInputBuffer_Original) != MH_OK) {
        LogError("Failed to create GetRawInputBuffer hook");
        return false;
    }

    // Hook TranslateMessage
    if (MH_CreateHook(TranslateMessage, TranslateMessage_Detour, (LPVOID*)&TranslateMessage_Original) != MH_OK) {
        LogError("Failed to create TranslateMessage hook");
        return false;
    }

    // Hook DispatchMessageA
    if (MH_CreateHook(DispatchMessageA, DispatchMessageA_Detour, (LPVOID*)&DispatchMessageA_Original) != MH_OK) {
        LogError("Failed to create DispatchMessageA hook");
        return false;
    }

    // Hook DispatchMessageW
    if (MH_CreateHook(DispatchMessageW, DispatchMessageW_Detour, (LPVOID*)&DispatchMessageW_Original) != MH_OK) {
        LogError("Failed to create DispatchMessageW hook");
        return false;
    }

    // Hook GetRawInputData
    if (MH_CreateHook(GetRawInputData, GetRawInputData_Detour, (LPVOID*)&GetRawInputData_Original) != MH_OK) {
        LogError("Failed to create GetRawInputData hook");
        return false;
    }

    // Hook RegisterRawInputDevices
    if (MH_CreateHook(RegisterRawInputDevices, RegisterRawInputDevices_Detour, (LPVOID*)&RegisterRawInputDevices_Original) != MH_OK) {
        LogError("Failed to create RegisterRawInputDevices hook");
        return false;
    }

    // Hook VkKeyScan
    if (MH_CreateHook(VkKeyScan, VkKeyScan_Detour, (LPVOID*)&VkKeyScan_Original) != MH_OK) {
        LogError("Failed to create VkKeyScan hook");
        return false;
    }

    // Hook VkKeyScanEx
    if (MH_CreateHook(VkKeyScanEx, VkKeyScanEx_Detour, (LPVOID*)&VkKeyScanEx_Original) != MH_OK) {
        LogError("Failed to create VkKeyScanEx hook");
        return false;
    }

    // Hook ToAscii
    if (MH_CreateHook(ToAscii, ToAscii_Detour, (LPVOID*)&ToAscii_Original) != MH_OK) {
        LogError("Failed to create ToAscii hook");
        return false;
    }

    // Hook ToAsciiEx
    if (MH_CreateHook(ToAsciiEx, ToAsciiEx_Detour, (LPVOID*)&ToAsciiEx_Original) != MH_OK) {
        LogError("Failed to create ToAsciiEx hook");
        return false;
    }

    // Hook ToUnicode
    if (MH_CreateHook(ToUnicode, ToUnicode_Detour, (LPVOID*)&ToUnicode_Original) != MH_OK) {
        LogError("Failed to create ToUnicode hook");
        return false;
    }

    // Hook ToUnicodeEx
    if (MH_CreateHook(ToUnicodeEx, ToUnicodeEx_Detour, (LPVOID*)&ToUnicodeEx_Original) != MH_OK) {
        LogError("Failed to create ToUnicodeEx hook");
        return false;
    }

    // Hook GetKeyNameTextA
    if (MH_CreateHook(GetKeyNameTextA, GetKeyNameTextA_Detour, (LPVOID*)&GetKeyNameTextA_Original) != MH_OK) {
        LogError("Failed to create GetKeyNameTextA hook");
        return false;
    }

    // Hook GetKeyNameTextW
    if (MH_CreateHook(GetKeyNameTextW, GetKeyNameTextW_Detour, (LPVOID*)&GetKeyNameTextW_Original) != MH_OK) {
        LogError("Failed to create GetKeyNameTextW hook");
        return false;
    }

    // Hook SendInput
    if (MH_CreateHook(SendInput, SendInput_Detour, (LPVOID*)&SendInput_Original) != MH_OK) {
        LogError("Failed to create SendInput hook");
        return false;
    }

    // Hook keybd_event
    if (MH_CreateHook(keybd_event, keybd_event_Detour, (LPVOID*)&keybd_event_Original) != MH_OK) {
        LogError("Failed to create keybd_event hook");
        return false;
    }

    // Hook mouse_event
    if (MH_CreateHook(mouse_event, mouse_event_Detour, (LPVOID*)&mouse_event_Original) != MH_OK) {
        LogError("Failed to create mouse_event hook");
        return false;
    }

    // Hook MapVirtualKey
    if (MH_CreateHook(MapVirtualKey, MapVirtualKey_Detour, (LPVOID*)&MapVirtualKey_Original) != MH_OK) {
        LogError("Failed to create MapVirtualKey hook");
        return false;
    }

    // Hook MapVirtualKeyEx
    if (MH_CreateHook(MapVirtualKeyEx, MapVirtualKeyEx_Detour, (LPVOID*)&MapVirtualKeyEx_Original) != MH_OK) {
        LogError("Failed to create MapVirtualKeyEx hook");
        return false;
    }

    // Enable all hooks
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        LogError("Failed to enable Windows message hooks");
        return false;
    }

    g_message_hooks_installed.store(true);
    LogInfo("Windows message hooks installed successfully");

    return true;
}

// Uninstall Windows message hooks
void UninstallWindowsMessageHooks() {
    if (!g_message_hooks_installed.load()) {
        LogInfo("Windows message hooks not installed");
        return;
    }

    // Disable all hooks
    MH_DisableHook(MH_ALL_HOOKS);

    // Remove hooks
    MH_RemoveHook(GetMessageA);
    MH_RemoveHook(GetMessageW);
    MH_RemoveHook(PeekMessageA);
    MH_RemoveHook(PeekMessageW);
    MH_RemoveHook(PostMessageA);
    MH_RemoveHook(PostMessageW);
    MH_RemoveHook(GetKeyboardState);
    MH_RemoveHook(ClipCursor);
    MH_RemoveHook(GetCursorPos);
    MH_RemoveHook(SetCursorPos);
    MH_RemoveHook(GetKeyState);
    MH_RemoveHook(GetAsyncKeyState);
    MH_RemoveHook(SetWindowsHookExA);
    MH_RemoveHook(SetWindowsHookExW);
    MH_RemoveHook(UnhookWindowsHookEx);
    MH_RemoveHook(GetRawInputBuffer);
    MH_RemoveHook(TranslateMessage);
    MH_RemoveHook(DispatchMessageA);
    MH_RemoveHook(DispatchMessageW);
    MH_RemoveHook(GetRawInputData);
    MH_RemoveHook(RegisterRawInputDevices);
    MH_RemoveHook(VkKeyScan);
    MH_RemoveHook(VkKeyScanEx);
    MH_RemoveHook(ToAscii);
    MH_RemoveHook(ToAsciiEx);
    MH_RemoveHook(ToUnicode);
    MH_RemoveHook(ToUnicodeEx);
    MH_RemoveHook(GetKeyNameTextA);
    MH_RemoveHook(GetKeyNameTextW);
    MH_RemoveHook(SendInput);
    MH_RemoveHook(keybd_event);
    MH_RemoveHook(mouse_event);
    MH_RemoveHook(MapVirtualKey);
    MH_RemoveHook(MapVirtualKeyEx);

    // Clean up
    GetMessageA_Original = nullptr;
    GetMessageW_Original = nullptr;
    PeekMessageA_Original = nullptr;
    PeekMessageW_Original = nullptr;
    PostMessageA_Original = nullptr;
    PostMessageW_Original = nullptr;
    GetKeyboardState_Original = nullptr;
    ClipCursor_Original = nullptr;
    GetCursorPos_Original = nullptr;
    SetCursorPos_Original = nullptr;
    GetKeyState_Original = nullptr;
    GetAsyncKeyState_Original = nullptr;
    SetWindowsHookExA_Original = nullptr;
    SetWindowsHookExW_Original = nullptr;
    UnhookWindowsHookEx_Original = nullptr;
    GetRawInputBuffer_Original = nullptr;
    TranslateMessage_Original = nullptr;
    DispatchMessageA_Original = nullptr;
    DispatchMessageW_Original = nullptr;
    GetRawInputData_Original = nullptr;
    RegisterRawInputDevices_Original = nullptr;
    VkKeyScan_Original = nullptr;
    VkKeyScanEx_Original = nullptr;
    ToAscii_Original = nullptr;
    ToAsciiEx_Original = nullptr;
    ToUnicode_Original = nullptr;
    ToUnicodeEx_Original = nullptr;
    GetKeyNameTextA_Original = nullptr;
    GetKeyNameTextW_Original = nullptr;
    SendInput_Original = nullptr;
    keybd_event_Original = nullptr;
    mouse_event_Original = nullptr;
    MapVirtualKey_Original = nullptr;
    MapVirtualKeyEx_Original = nullptr;

    g_message_hooks_installed.store(false);
    LogInfo("Windows message hooks uninstalled successfully");
}

// Check if Windows message hooks are installed
bool AreWindowsMessageHooksInstalled() {
    return g_message_hooks_installed.load();
}

// Hook statistics access functions
const HookCallStats& GetHookStats(int hook_index) {
    if (hook_index >= 0 && hook_index < HOOK_COUNT) {
        return g_hook_stats[hook_index];
    }
    static HookCallStats empty_stats;
    return empty_stats;
}

void ResetAllHookStats() {
    for (int i = 0; i < HOOK_COUNT; ++i) {
        g_hook_stats[i].reset();
    }
}

int GetHookCount() {
    return HOOK_COUNT;
}

const char* GetHookName(int hook_index) {
    if (hook_index >= 0 && hook_index < HOOK_COUNT) {
        return g_hook_names[hook_index];
    }
    return "Unknown";
}

} // namespace renodx::hooks
