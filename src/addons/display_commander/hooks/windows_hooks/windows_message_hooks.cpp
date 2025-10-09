#include "windows_message_hooks.hpp"
#include "../../globals.hpp"                            // For s_continue_rendering
#include "../../settings/experimental_tab_settings.hpp" // For g_experimentalTabSettings
#include "../../settings/main_tab_settings.hpp"
#include "../../utils.hpp"
#include "../api_hooks.hpp" // For GetGameWindow and other functions
#include <MinHook.h>
#include <array>


namespace display_commanderhooks {

// Helper function to check if input should be blocked
bool ShouldBlockInput() {
    const bool block_without_reshade = s_block_input_without_reshade.load();
    const bool is_background = g_app_in_background.load(std::memory_order_acquire);
    const bool block_in_background = s_block_input_in_background.load();

    return block_without_reshade || (is_background && block_in_background);
}

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
DisplayConfigGetDeviceInfo_pfn DisplayConfigGetDeviceInfo_Original = nullptr;

// Hook state
static std::atomic<bool> g_message_hooks_installed{false};

// Cursor position tracking (similar to Reshade)
static POINT s_last_cursor_position = {};
static RECT s_last_clip_cursor = {};

// Hook statistics array
std::array<HookCallStats, HOOK_COUNT> g_hook_stats;

// Hook names for display
static const std::array<const char*, HOOK_COUNT> g_hook_names = {"GetMessageA",
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
                                               "MapVirtualKeyEx",
                                               "DisplayConfigGetDeviceInfo",
                                               "XInputGetState",
                                               "XInputGetStateEx",
                                               "Sleep",
                                               "SleepEx",
                                               "WaitForSingleObject",
                                               "WaitForMultipleObjects"};

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
    // LogInfo("Intercepted message: HWND=0x%p, Msg=0x%04X, WParam=0x%08X, LParam=0x%08X",
    //        lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);

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
    // Only suppress if input blocking is enabled (either manual or background)
    if (!ShouldBlockInput()) {
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
        // Only block DOWN events, allow UP events to clear stuck keys/buttons
        switch (uMsg) {
        // Keyboard DOWN messages only
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_CHAR:
        case WM_SYSCHAR:
        case WM_DEADCHAR:
        case WM_SYSDEADCHAR:
        // Mouse DOWN messages only
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        // Cursor messages
        case WM_SETCURSOR:
            return true;
        // Allow UP events to pass through to clear stuck keys/buttons
        case WM_KEYUP:
        case WM_SYSKEYUP:
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_XBUTTONUP:
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
        LogInfo("Suppressed input message: HWND=0x%p, Msg=0x%04X, WParam=0x%08X, LParam=0x%08X", lpMsg->hwnd,
                lpMsg->message, lpMsg->wParam, lpMsg->lParam);
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

// Suppress Microsoft extension warnings for MinHook function pointer conversions
#pragma warning(push)
#pragma warning(disable : 4191) // 'type cast': unsafe conversion from 'function_pointer' to 'data_pointer'
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmicrosoft-cast"

// Hooked GetMessageA function
BOOL WINAPI GetMessageA_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    // Track total calls
    g_hook_stats[HOOK_GetMessageA].increment_total();

    // Call original function first
    BOOL result = GetMessageA_Original ? GetMessageA_Original(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax)
                                       : GetMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);

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
    BOOL result = GetMessageW_Original ? GetMessageW_Original(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax)
                                       : GetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);

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
    BOOL result = PeekMessageA_Original ? PeekMessageA_Original(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg)
                                        : PeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);

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
    BOOL result = PeekMessageW_Original ? PeekMessageW_Original(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg)
                                        : PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);

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
            LogInfo("Suppressed PostMessageA: HWND=0x%p, Msg=0x%04X, WParam=0x%08X, LParam=0x%08X", hWnd, Msg, wParam,
                    lParam);
        }

        // Return TRUE to indicate the message was "processed" (blocked)
        return TRUE;
    }

    // Call original function
    return PostMessageA_Original ? PostMessageA_Original(hWnd, Msg, wParam, lParam)
                                 : PostMessageA(hWnd, Msg, wParam, lParam);
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
            LogInfo("Suppressed PostMessageW: HWND=0x%p, Msg=0x%04X, WParam=0x%08X, LParam=0x%08X", hWnd, Msg, wParam,
                    lParam);
        }

        // Return TRUE to indicate the message was "processed" (blocked)
        return TRUE;
    }

    // Call original function
    return PostMessageW_Original ? PostMessageW_Original(hWnd, Msg, wParam, lParam)
                                 : PostMessageW(hWnd, Msg, wParam, lParam);
}

// Hooked GetKeyboardState function
BOOL WINAPI GetKeyboardState_Detour(PBYTE lpKeyState) {
    // Call original function first
    BOOL result = GetKeyboardState_Original ? GetKeyboardState_Original(lpKeyState) : GetKeyboardState(lpKeyState);

    // If input blocking is enabled and we got valid key state data
    if (result && lpKeyState != nullptr && ShouldBlockInput()) {
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
BOOL WINAPI ClipCursor_Detour(const RECT *lpRect) {
    // Store the clip rectangle for reference
    s_last_clip_cursor = (lpRect != nullptr) ? *lpRect : RECT{};

    // If input blocking is enabled, disable cursor clipping
    if (ShouldBlockInput()) {
        // Disable cursor clipping when input is blocked
        lpRect = nullptr;
    }

    // Call original function
    return ClipCursor_Original ? ClipCursor_Original(lpRect) : ClipCursor(lpRect);
}

// Hooked GetCursorPos function
BOOL WINAPI GetCursorPos_Detour(LPPOINT lpPoint) {
    // If mouse position spoofing is enabled AND auto-click is enabled, return spoofed position
    if (settings::g_experimentalTabSettings.mouse_spoofing_enabled.GetValue() && lpPoint != nullptr) {
        // Check if auto-click is enabled by checking if the experimental tab settings are available
        // and auto-click is enabled
        if (settings::g_experimentalTabSettings.auto_click_enabled.GetValue()) {
            lpPoint->x = s_spoofed_mouse_x.load();
            lpPoint->y = s_spoofed_mouse_y.load();
            return TRUE;
        }
    }

    // If input blocking is enabled, return last known cursor position
    if (ShouldBlockInput() && lpPoint != nullptr) {
        *lpPoint = s_last_cursor_position;
        return TRUE;
    }

    // Call original function
    BOOL result = GetCursorPos_Original ? GetCursorPos_Original(lpPoint) : GetCursorPos(lpPoint);

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

    // If mouse position spoofing is enabled AND auto-click is enabled, update spoofed position instead of moving cursor
    if (settings::g_experimentalTabSettings.mouse_spoofing_enabled.GetValue()) {
        // Check if auto-click is enabled by checking if the experimental tab settings are available
        // and auto-click is enabled
        if (settings::g_experimentalTabSettings.auto_click_enabled.GetValue()) {
            s_spoofed_mouse_x.store(X);
            s_spoofed_mouse_y.store(Y);
            return TRUE; // Return success without actually moving the cursor
        }
    }

    // If input blocking is enabled, block cursor position changes
    if (ShouldBlockInput()) {
        return TRUE; // Block the cursor position change
    }

    // Call original function
    return SetCursorPos_Original ? SetCursorPos_Original(X, Y) : SetCursorPos(X, Y);
}

// Hooked GetKeyState function
SHORT WINAPI GetKeyState_Detour(int vKey) {
    // Track total calls
    g_hook_stats[HOOK_GetKeyState].increment_total();

    // If input blocking is enabled, return 0 for all keys
    if (ShouldBlockInput()) {
        // Block all keyboard keys (0x08-0xFF) and mouse buttons
        if ((vKey >= 0x08 && vKey <= 0xFF) || (vKey >= VK_LBUTTON && vKey <= VK_XBUTTON2)) {
            return 0; // Block input
        }
    }

    // Track unsuppressed calls
    g_hook_stats[HOOK_GetKeyState].increment_unsuppressed();

    // Call original function
    return GetKeyState_Original ? GetKeyState_Original(vKey) : GetKeyState(vKey);
}

// Hooked GetAsyncKeyState function
SHORT WINAPI GetAsyncKeyState_Detour(int vKey) {
    // Track total calls
    g_hook_stats[HOOK_GetAsyncKeyState].increment_total();

    // If input blocking is enabled, return 0 for all keys
    if (ShouldBlockInput()) {
        // Block all keyboard keys (0x08-0xFF) and mouse buttons
        if ((vKey >= 0x08 && vKey <= 0xFF) || (vKey >= VK_LBUTTON && vKey <= VK_XBUTTON2)) {
            return 0; // Block input
        }
    }

    // Track unsuppressed calls
    g_hook_stats[HOOK_GetAsyncKeyState].increment_unsuppressed();

    // Call original function
    return GetAsyncKeyState_Original ? GetAsyncKeyState_Original(vKey) : GetAsyncKeyState(vKey);
}

// Hooked SetWindowsHookExA function
HHOOK WINAPI SetWindowsHookExA_Detour(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId) {
    // Call original function first
    HHOOK result = SetWindowsHookExA_Original ? SetWindowsHookExA_Original(idHook, lpfn, hmod, dwThreadId)
                                              : SetWindowsHookExA(idHook, lpfn, hmod, dwThreadId);

    // Log hook installation for debugging
    if (result != nullptr) {
        LogInfo("SetWindowsHookExA installed: idHook=%d, hmod=0x%p, dwThreadId=%lu", idHook, hmod, dwThreadId);
    }

    return result;
}

// Hooked SetWindowsHookExW function
HHOOK WINAPI SetWindowsHookExW_Detour(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId) {
    // Call original function first
    HHOOK result = SetWindowsHookExW_Original ? SetWindowsHookExW_Original(idHook, lpfn, hmod, dwThreadId)
                                              : SetWindowsHookExW(idHook, lpfn, hmod, dwThreadId);

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
    return UnhookWindowsHookEx_Original ? UnhookWindowsHookEx_Original(hhk) : UnhookWindowsHookEx(hhk);
}

// Hooked GetRawInputBuffer function
UINT WINAPI GetRawInputBuffer_Detour(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader) {
    // Track total calls
    g_hook_stats[HOOK_GetRawInputBuffer].increment_total();

    // Call original function first
    UINT result = GetRawInputBuffer_Original ? GetRawInputBuffer_Original(pData, pcbSize, cbSizeHeader)
                                             : GetRawInputBuffer(pData, pcbSize, cbSizeHeader);

    // If input blocking is enabled and we got data, replace it
    if (result > 0 && pData != nullptr && pcbSize != nullptr && ShouldBlockInput()) {
        // Replace blocked input data with harmless data
        PRAWINPUT current = pData;
        UINT processed_count = 0;

        for (UINT i = 0; i < result; ++i) {
            // Check if this input should be replaced
            bool should_replace = false;

            if (current->header.dwType == RIM_TYPEKEYBOARD) {
                should_replace = true; // Replace all keyboard input
            } else if (current->header.dwType == RIM_TYPEMOUSE) {
                should_replace = true; // Replace all mouse input
            }

            // If should be replaced, replace with harmless data
            if (should_replace) {
                if (current->header.dwType == RIM_TYPEKEYBOARD) {
                    // Only block key DOWN events, allow UP events to clear stuck keys
                    if (current->data.keyboard.Flags & RI_KEY_BREAK) {
                        // This is a key UP event, don't block it
                        should_replace = false;
                    } else {
                        // This is a key DOWN event, block it by replacing with neutral data
                        current->header.dwType = RIM_TYPEKEYBOARD;
                        current->header.dwSize = sizeof(RAWINPUT);
                        current->data.keyboard.MakeCode = 0; // No scan code
                        current->data.keyboard.Flags = 0;    // Neutral flags - no key event
                        current->data.keyboard.Reserved = 0;
                        current->data.keyboard.VKey = 0;    // No virtual key
                        current->data.keyboard.Message = 0; // No message
                        current->data.keyboard.ExtraInformation = 0;
                    }
                } else if (current->header.dwType == RIM_TYPEMOUSE) {
                    // Only block mouse DOWN events, allow UP events to clear stuck buttons
                    if (current->data.mouse.usButtonFlags &
                        (RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_UP | RI_MOUSE_MIDDLE_BUTTON_UP |
                         RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_UP)) {
                        // This is a mouse button UP event, don't block it
                        should_replace = false;
                    } else {
                        // This is a mouse DOWN event or movement, block it
                        current->header.dwType = RIM_TYPEMOUSE;
                        current->header.dwSize = sizeof(RAWINPUT);
                        current->data.mouse.usFlags = 0;
                        current->data.mouse.usButtonFlags = 0;
                        current->data.mouse.usButtonData = 0;
                        current->data.mouse.ulRawButtons = 0;
                        current->data.mouse.lLastX = 0;
                        current->data.mouse.lLastY = 0;
                        current->data.mouse.ulExtraInformation = 0;
                    }
                }
            }

            // Move to next input
            current = (PRAWINPUT)((PBYTE)current + current->header.dwSize);
            processed_count++;
        }

        // Track unsuppressed calls (data was processed/replaced)
        g_hook_stats[HOOK_GetRawInputBuffer].increment_unsuppressed();

        // Log occasionally for debugging
        static std::atomic<int> replace_counter{0};
        int count = replace_counter.fetch_add(1);
        if (count % 1000 == 0) {
            LogInfo("Replaced raw input buffer: %u events processed", processed_count);
        }
    }

    return result;
}

// Hooked TranslateMessage function
BOOL WINAPI TranslateMessage_Detour(const MSG *lpMsg) {
    // If input blocking is enabled, don't translate messages that should be blocked
    if (ShouldBlockInput() && lpMsg != nullptr) {
        // Check if this message should be suppressed
        if (ShouldSuppressMessage(lpMsg->hwnd, lpMsg->message)) {
            // Don't translate blocked messages
            return FALSE;
        }
    }

    // Call original function
    return TranslateMessage_Original ? TranslateMessage_Original(lpMsg) : TranslateMessage(lpMsg);
}

// Hooked DispatchMessageA function
LRESULT WINAPI DispatchMessageA_Detour(const MSG *lpMsg) {
    // If input blocking is enabled, don't dispatch messages that should be blocked
    if (ShouldBlockInput() && lpMsg != nullptr) {
        // Check if this message should be suppressed
        if (ShouldSuppressMessage(lpMsg->hwnd, lpMsg->message)) {
            // Return 0 to indicate the message was "processed" (blocked)
            return 0;
        }
    }

    // Call original function
    return DispatchMessageA_Original ? DispatchMessageA_Original(lpMsg) : DispatchMessageA(lpMsg);
}

// Hooked DispatchMessageW function
LRESULT WINAPI DispatchMessageW_Detour(const MSG *lpMsg) {
    // If input blocking is enabled, don't dispatch messages that should be blocked
    if (ShouldBlockInput() && lpMsg != nullptr) {
        // Check if this message should be suppressed
        if (ShouldSuppressMessage(lpMsg->hwnd, lpMsg->message)) {
            // Return 0 to indicate the message was "processed" (blocked)
            return 0;
        }
    }

    // Call original function
    return DispatchMessageW_Original ? DispatchMessageW_Original(lpMsg) : DispatchMessageW(lpMsg);
}

// Hooked GetRawInputData function
UINT WINAPI GetRawInputData_Detour(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize,
                                   UINT cbSizeHeader) {
    // Track total calls
    g_hook_stats[HOOK_GetRawInputData].increment_total();

    // Call original function first
    UINT result = GetRawInputData_Original
                      ? GetRawInputData_Original(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader)
                      : GetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);

    // If input blocking is enabled and we got data, filter it
    if (result != UINT(-1) && pData != nullptr && pcbSize != nullptr && ShouldBlockInput()) {
        // For raw input data, we need to check if it's keyboard or mouse input
        if (uiCommand == RID_INPUT) {
            RAWINPUT *rawInput = static_cast<RAWINPUT *>(pData);

            // Only block DOWN events, allow UP events to clear stuck keys/buttons
            if (rawInput->header.dwType == RIM_TYPEKEYBOARD) {
                // Only block key DOWN events, allow UP events to clear stuck keys
                if (rawInput->data.keyboard.Flags & RI_KEY_BREAK) {
                    // This is a key UP event, don't block it - let it pass through
                } else {
                    // This is a key DOWN event, block it by replacing with neutral data
                    rawInput->header.dwType = RIM_TYPEKEYBOARD;
                    rawInput->header.dwSize = sizeof(RAWINPUT);
                    rawInput->data.keyboard.MakeCode = 0; // No scan code
                    rawInput->data.keyboard.Flags = 0;    // Neutral flags - no key event
                    rawInput->data.keyboard.Reserved = 0;
                    rawInput->data.keyboard.VKey = 0;    // No virtual key
                    rawInput->data.keyboard.Message = 0; // No message
                    rawInput->data.keyboard.ExtraInformation = 0;
                }
            } else if (rawInput->header.dwType == RIM_TYPEMOUSE) {
                // Only block mouse DOWN events, allow UP events to clear stuck buttons
                if (rawInput->data.mouse.usButtonFlags &
                    (RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_UP | RI_MOUSE_MIDDLE_BUTTON_UP |
                     RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_UP)) {
                    // This is a mouse button UP event, don't block it - let it pass through
                } else {
                    // This is a mouse DOWN event or movement, block it
                    rawInput->header.dwType = RIM_TYPEMOUSE;
                    rawInput->header.dwSize = sizeof(RAWINPUT);
                    rawInput->data.mouse.usFlags = 0;
                    rawInput->data.mouse.usButtonFlags = 0;
                    rawInput->data.mouse.usButtonData = 0;
                    rawInput->data.mouse.ulRawButtons = 0;
                    rawInput->data.mouse.lLastX = 0;
                    rawInput->data.mouse.lLastY = 0;
                    rawInput->data.mouse.ulExtraInformation = 0;
                }
            }

            // Track unsuppressed calls (data was processed/replaced)
            g_hook_stats[HOOK_GetRawInputData].increment_unsuppressed();
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
            LogInfo("  Device %u: UsagePage=0x%04X, Usage=0x%04X, Flags=0x%08X, hwndTarget=0x%p", i,
                    pRawInputDevices[i].usUsagePage, pRawInputDevices[i].usUsage, pRawInputDevices[i].dwFlags,
                    pRawInputDevices[i].hwndTarget);
        }
    }

    // Call original function
    return RegisterRawInputDevices_Original ? RegisterRawInputDevices_Original(pRawInputDevices, uiNumDevices, cbSize)
                                            : RegisterRawInputDevices(pRawInputDevices, uiNumDevices, cbSize);
}

// Hooked VkKeyScan function
SHORT WINAPI VkKeyScan_Detour(CHAR ch) {
    // If input blocking is enabled, return -1 to indicate no virtual key found
    if (ShouldBlockInput()) {
        return -1; // No virtual key found
    }

    // Call original function
    return VkKeyScan_Original ? VkKeyScan_Original(ch) : VkKeyScan(ch);
}

// Hooked VkKeyScanEx function
SHORT WINAPI VkKeyScanEx_Detour(CHAR ch, HKL dwhkl) {
    // If input blocking is enabled, return -1 to indicate no virtual key found
    if (ShouldBlockInput()) {
        return -1; // No virtual key found
    }

    // Call original function
    return VkKeyScanEx_Original ? VkKeyScanEx_Original(ch, dwhkl) : VkKeyScanEx(ch, dwhkl);
}

// Hooked ToAscii function
int WINAPI ToAscii_Detour(UINT uVirtKey, UINT uScanCode, const BYTE *lpKeyState, LPWORD lpChar, UINT uFlags) {
    // If input blocking is enabled, return 0 to indicate no character generated
    if (ShouldBlockInput()) {
        return 0; // No character generated
    }

    // Call original function
    return ToAscii_Original ? ToAscii_Original(uVirtKey, uScanCode, lpKeyState, lpChar, uFlags)
                            : ToAscii(uVirtKey, uScanCode, lpKeyState, lpChar, uFlags);
}

// Hooked ToAsciiEx function
int WINAPI ToAsciiEx_Detour(UINT uVirtKey, UINT uScanCode, const BYTE *lpKeyState, LPWORD lpChar, UINT uFlags,
                            HKL dwhkl) {
    // If input blocking is enabled, return 0 to indicate no character generated
    if (ShouldBlockInput()) {
        return 0; // No character generated
    }

    // Call original function
    return ToAsciiEx_Original ? ToAsciiEx_Original(uVirtKey, uScanCode, lpKeyState, lpChar, uFlags, dwhkl)
                              : ToAsciiEx(uVirtKey, uScanCode, lpKeyState, lpChar, uFlags, dwhkl);
}

// Hooked ToUnicode function
int WINAPI ToUnicode_Detour(UINT wVirtKey, UINT wScanCode, const BYTE *lpKeyState, LPWSTR pwszBuff, int cchBuff,
                            UINT wFlags) {
    // If input blocking is enabled, return 0 to indicate no character generated
    if (ShouldBlockInput()) {
        return 0; // No character generated
    }

    // Call original function
    return ToUnicode_Original ? ToUnicode_Original(wVirtKey, wScanCode, lpKeyState, pwszBuff, cchBuff, wFlags)
                              : ToUnicode(wVirtKey, wScanCode, lpKeyState, pwszBuff, cchBuff, wFlags);
}

// Hooked ToUnicodeEx function
int WINAPI ToUnicodeEx_Detour(UINT wVirtKey, UINT wScanCode, const BYTE *lpKeyState, LPWSTR pwszBuff, int cchBuff,
                              UINT wFlags, HKL dwhkl) {
    // If input blocking is enabled, return 0 to indicate no character generated
    if (ShouldBlockInput()) {
        return 0; // No character generated
    }

    // Call original function
    return ToUnicodeEx_Original
               ? ToUnicodeEx_Original(wVirtKey, wScanCode, lpKeyState, pwszBuff, cchBuff, wFlags, dwhkl)
               : ToUnicodeEx(wVirtKey, wScanCode, lpKeyState, pwszBuff, cchBuff, wFlags, dwhkl);
}

// Hooked GetKeyNameTextA function
int WINAPI GetKeyNameTextA_Detour(LONG lParam, LPSTR lpString, int cchSize) {
    // If input blocking is enabled, return 0 to indicate no key name
    if (ShouldBlockInput()) {
        HWND gameWindow = GetGameWindow();
        if (gameWindow != nullptr) {
            if (lpString != nullptr && cchSize > 0) {
                lpString[0] = '\0'; // Empty string
            }
            return 0; // No key name
        }
    }

    // Call original function
    return GetKeyNameTextA_Original ? GetKeyNameTextA_Original(lParam, lpString, cchSize)
                                    : GetKeyNameTextA(lParam, lpString, cchSize);
}

// Hooked GetKeyNameTextW function
int WINAPI GetKeyNameTextW_Detour(LONG lParam, LPWSTR lpString, int cchSize) {
    // If input blocking is enabled, return 0 to indicate no key name
    if (ShouldBlockInput()) {
        if (lpString != nullptr && cchSize > 0) {
            lpString[0] = L'\0'; // Empty string
        }
        return 0; // No key name
    }

    // Call original function
    return GetKeyNameTextW_Original ? GetKeyNameTextW_Original(lParam, lpString, cchSize)
                                    : GetKeyNameTextW(lParam, lpString, cchSize);
}

// Hooked SendInput function
UINT WINAPI SendInput_Detour(UINT nInputs, LPINPUT pInputs, int cbSize) {
    // If input blocking is enabled, selectively block DOWN events
    if (ShouldBlockInput() && pInputs != nullptr) {
        UINT allowed_inputs = 0;

        // Filter inputs - only allow UP events to pass through
        for (UINT i = 0; i < nInputs; ++i) {
            bool should_block = false;

            if (pInputs[i].type == INPUT_KEYBOARD) {
                // Block key DOWN events, allow UP events
                if (!(pInputs[i].ki.dwFlags & KEYEVENTF_KEYUP)) {
                    should_block = true; // This is a key DOWN event
                }
            } else if (pInputs[i].type == INPUT_MOUSE) {
                // Block mouse DOWN events, allow UP events
                if (pInputs[i].mi.dwFlags &
                    (MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_XDOWN)) {
                    should_block = true; // This is a mouse DOWN event
                }
            }

            if (!should_block) {
                // This is an UP event or non-input type, allow it through
                if (allowed_inputs != i) {
                    pInputs[allowed_inputs] = pInputs[i]; // Move to front of array
                }
                allowed_inputs++;
            }
        }

        if (allowed_inputs == 0) {
            // All inputs were blocked
            static std::atomic<int> block_counter{0};
            int count = block_counter.fetch_add(1);
            if (count % 100 == 0) {
                LogInfo("Blocked all SendInput: nInputs=%u", nInputs);
            }
            return 0;
        } else if (allowed_inputs < nInputs) {
            // Some inputs were blocked, some allowed
            static std::atomic<int> filter_counter{0};
            int count = filter_counter.fetch_add(1);
            if (count % 100 == 0) {
                LogInfo("Filtered SendInput: %u/%u inputs allowed", allowed_inputs, nInputs);
            }
            nInputs = allowed_inputs; // Update count to only process allowed inputs
        }
    }

    // Call original function with potentially filtered inputs
    return SendInput_Original ? SendInput_Original(nInputs, pInputs, cbSize) : SendInput(nInputs, pInputs, cbSize);
}

// Hooked keybd_event function
void WINAPI keybd_event_Detour(BYTE bVk, BYTE bScan, DWORD dwFlags, ULONG_PTR dwExtraInfo) {
    // If input blocking is enabled, selectively block DOWN events
    if (ShouldBlockInput()) {
        // Only block key DOWN events, allow UP events to clear stuck keys
        if (!(dwFlags & KEYEVENTF_KEYUP)) {
            // This is a key DOWN event, block it
            static std::atomic<int> block_counter{0};
            int count = block_counter.fetch_add(1);
            if (count % 100 == 0) {
                LogInfo("Blocked keybd_event DOWN: bVk=0x%02X, dwFlags=0x%08X", bVk, dwFlags);
            }
            return; // Block keyboard DOWN event generation
        }
        // This is a key UP event, allow it through to clear stuck keys
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
    // If input blocking is enabled, selectively block DOWN events
    if (ShouldBlockInput()) {
        // Only block mouse DOWN events, allow UP events to clear stuck buttons
        if (dwFlags & (MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_XDOWN)) {
            // This is a mouse DOWN event, block it
            static std::atomic<int> block_counter{0};
            int count = block_counter.fetch_add(1);
            if (count % 100 == 0) {
                LogInfo("Blocked mouse_event DOWN: dwFlags=0x%08X, dx=%u, dy=%u", dwFlags, dx, dy);
            }
            return; // Block mouse DOWN event generation
        }
        // This is a mouse UP event or movement, allow it through to clear stuck buttons
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
    if (ShouldBlockInput()) {
        return 0; // Block virtual key mapping
    }

    // Call original function
    return MapVirtualKey_Original ? MapVirtualKey_Original(uCode, uMapType) : MapVirtualKey(uCode, uMapType);
}

// Hooked MapVirtualKeyEx function
UINT WINAPI MapVirtualKeyEx_Detour(UINT uCode, UINT uMapType, HKL dwhkl) {
    // If input blocking is enabled, return 0 for virtual key mapping
    if (ShouldBlockInput()) {
        return 0; // Block virtual key mapping
    }

    // Call original function
    return MapVirtualKeyEx_Original ? MapVirtualKeyEx_Original(uCode, uMapType, dwhkl)
                                    : MapVirtualKeyEx(uCode, uMapType, dwhkl);
}

// Hooked DisplayConfigGetDeviceInfo function
LONG WINAPI DisplayConfigGetDeviceInfo_Detour(DISPLAYCONFIG_DEVICE_INFO_HEADER *requestPacket) {
    // Track total calls
    g_hook_stats[HOOK_DisplayConfigGetDeviceInfo].increment_total();

    // Call original function
    LONG result = DisplayConfigGetDeviceInfo_Original ? DisplayConfigGetDeviceInfo_Original(requestPacket)
                                                     : DisplayConfigGetDeviceInfo(requestPacket);

    // Hide HDR capabilities if enabled and this is an advanced color info request
    if (SUCCEEDED(result) && requestPacket != nullptr && s_hide_hdr_capabilities.load()) {
        if (requestPacket->type == DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO) {
            auto* colorInfo = reinterpret_cast<DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO*>(requestPacket);

            // Hide HDR support
            colorInfo->advancedColorSupported = 0;
            colorInfo->advancedColorEnabled = 0;
            colorInfo->wideColorEnforced = 0;
            colorInfo->advancedColorForceDisabled = 1;

            static int hdr_hidden_log_count = 0;
            if (hdr_hidden_log_count < 3) {
                LogInfo("HDR hiding: DisplayConfigGetDeviceInfo - hiding advanced color support");
                hdr_hidden_log_count++;
            }
        }
    }

    // Track unsuppressed calls
    g_hook_stats[HOOK_DisplayConfigGetDeviceInfo].increment_unsuppressed();

    return result;
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
    if (MH_CreateHook(GetMessageA, GetMessageA_Detour, (LPVOID *)&GetMessageA_Original) != MH_OK) {
        LogError("Failed to create GetMessageA hook");
        return false;
    }

    // Hook GetMessageW
    if (MH_CreateHook(GetMessageW, GetMessageW_Detour, (LPVOID *)&GetMessageW_Original) != MH_OK) {
        LogError("Failed to create GetMessageW hook");
        return false;
    }

    // Hook PeekMessageA
    if (MH_CreateHook(PeekMessageA, PeekMessageA_Detour, (LPVOID *)&PeekMessageA_Original) != MH_OK) {
        LogError("Failed to create PeekMessageA hook");
        return false;
    }

    // Hook PeekMessageW
    if (MH_CreateHook(PeekMessageW, PeekMessageW_Detour, (LPVOID *)&PeekMessageW_Original) != MH_OK) {
        LogError("Failed to create PeekMessageW hook");
        return false;
    }

    // Hook PostMessageA
    if (MH_CreateHook(PostMessageA, PostMessageA_Detour, (LPVOID *)&PostMessageA_Original) != MH_OK) {
        LogError("Failed to create PostMessageA hook");
        return false;
    }

    // Hook PostMessageW
    if (MH_CreateHook(PostMessageW, PostMessageW_Detour, (LPVOID *)&PostMessageW_Original) != MH_OK) {
        LogError("Failed to create PostMessageW hook");
        return false;
    }

    // Hook GetKeyboardState
    if (MH_CreateHook(GetKeyboardState, GetKeyboardState_Detour, (LPVOID *)&GetKeyboardState_Original) != MH_OK) {
        LogError("Failed to create GetKeyboardState hook");
        return false;
    }

    // Hook ClipCursor
    if (MH_CreateHook(ClipCursor, ClipCursor_Detour, (LPVOID *)&ClipCursor_Original) != MH_OK) {
        LogError("Failed to create ClipCursor hook");
        return false;
    }

    // Hook GetCursorPos
    if (MH_CreateHook(GetCursorPos, GetCursorPos_Detour, (LPVOID *)&GetCursorPos_Original) != MH_OK) {
        LogError("Failed to create GetCursorPos hook");
        return false;
    }

    // Hook SetCursorPos
    if (MH_CreateHook(SetCursorPos, SetCursorPos_Detour, (LPVOID *)&SetCursorPos_Original) != MH_OK) {
        LogError("Failed to create SetCursorPos hook");
        return false;
    }

    // Hook GetKeyState
    if (MH_CreateHook(GetKeyState, GetKeyState_Detour, (LPVOID *)&GetKeyState_Original) != MH_OK) {
        LogError("Failed to create GetKeyState hook");
        return false;
    }

    // Hook GetAsyncKeyState
    if (MH_CreateHook(GetAsyncKeyState, GetAsyncKeyState_Detour, (LPVOID *)&GetAsyncKeyState_Original) != MH_OK) {
        LogError("Failed to create GetAsyncKeyState hook");
        return false;
    }

    // Hook SetWindowsHookExA
    if (MH_CreateHook(SetWindowsHookExA, SetWindowsHookExA_Detour, (LPVOID *)&SetWindowsHookExA_Original) != MH_OK) {
        LogError("Failed to create SetWindowsHookExA hook");
        return false;
    }

    // Hook SetWindowsHookExW
    if (MH_CreateHook(SetWindowsHookExW, SetWindowsHookExW_Detour, (LPVOID *)&SetWindowsHookExW_Original) != MH_OK) {
        LogError("Failed to create SetWindowsHookExW hook");
        return false;
    }

    // Hook UnhookWindowsHookEx
    if (MH_CreateHook(UnhookWindowsHookEx, UnhookWindowsHookEx_Detour, (LPVOID *)&UnhookWindowsHookEx_Original) !=
        MH_OK) {
        LogError("Failed to create UnhookWindowsHookEx hook");
        return false;
    }

    // Hook GetRawInputBuffer
    if (MH_CreateHook(GetRawInputBuffer, GetRawInputBuffer_Detour, (LPVOID *)&GetRawInputBuffer_Original) != MH_OK) {
        LogError("Failed to create GetRawInputBuffer hook");
        return false;
    }

    // Hook TranslateMessage
    if (MH_CreateHook(TranslateMessage, TranslateMessage_Detour, (LPVOID *)&TranslateMessage_Original) != MH_OK) {
        LogError("Failed to create TranslateMessage hook");
        return false;
    }

    // Hook DispatchMessageA
    if (MH_CreateHook(DispatchMessageA, DispatchMessageA_Detour, (LPVOID *)&DispatchMessageA_Original) != MH_OK) {
        LogError("Failed to create DispatchMessageA hook");
        return false;
    }

    // Hook DispatchMessageW
    if (MH_CreateHook(DispatchMessageW, DispatchMessageW_Detour, (LPVOID *)&DispatchMessageW_Original) != MH_OK) {
        LogError("Failed to create DispatchMessageW hook");
        return false;
    }

    // Hook GetRawInputData
    if (MH_CreateHook(GetRawInputData, GetRawInputData_Detour, (LPVOID *)&GetRawInputData_Original) != MH_OK) {
        LogError("Failed to create GetRawInputData hook");
        return false;
    }

    // Hook RegisterRawInputDevices
    if (MH_CreateHook(RegisterRawInputDevices, RegisterRawInputDevices_Detour,
                      (LPVOID *)&RegisterRawInputDevices_Original) != MH_OK) {
        LogError("Failed to create RegisterRawInputDevices hook");
        return false;
    }

    // Hook VkKeyScan
    if (MH_CreateHook(VkKeyScan, VkKeyScan_Detour, (LPVOID *)&VkKeyScan_Original) != MH_OK) {
        LogError("Failed to create VkKeyScan hook");
        return false;
    }

    // Hook VkKeyScanEx
    if (MH_CreateHook(VkKeyScanEx, VkKeyScanEx_Detour, (LPVOID *)&VkKeyScanEx_Original) != MH_OK) {
        LogError("Failed to create VkKeyScanEx hook");
        return false;
    }

    // Hook ToAscii
    if (MH_CreateHook(ToAscii, ToAscii_Detour, (LPVOID *)&ToAscii_Original) != MH_OK) {
        LogError("Failed to create ToAscii hook");
        return false;
    }

    // Hook ToAsciiEx
    if (MH_CreateHook(ToAsciiEx, ToAsciiEx_Detour, (LPVOID *)&ToAsciiEx_Original) != MH_OK) {
        LogError("Failed to create ToAsciiEx hook");
        return false;
    }

    // Hook ToUnicode
    if (MH_CreateHook(ToUnicode, ToUnicode_Detour, (LPVOID *)&ToUnicode_Original) != MH_OK) {
        LogError("Failed to create ToUnicode hook");
        return false;
    }

    // Hook ToUnicodeEx
    if (MH_CreateHook(ToUnicodeEx, ToUnicodeEx_Detour, (LPVOID *)&ToUnicodeEx_Original) != MH_OK) {
        LogError("Failed to create ToUnicodeEx hook");
        return false;
    }

    // Hook GetKeyNameTextA
    if (MH_CreateHook(GetKeyNameTextA, GetKeyNameTextA_Detour, (LPVOID *)&GetKeyNameTextA_Original) != MH_OK) {
        LogError("Failed to create GetKeyNameTextA hook");
        return false;
    }

    // Hook GetKeyNameTextW
    if (MH_CreateHook(GetKeyNameTextW, GetKeyNameTextW_Detour, (LPVOID *)&GetKeyNameTextW_Original) != MH_OK) {
        LogError("Failed to create GetKeyNameTextW hook");
        return false;
    }

    // Hook SendInput
    if (MH_CreateHook(SendInput, SendInput_Detour, (LPVOID *)&SendInput_Original) != MH_OK) {
        LogError("Failed to create SendInput hook");
        return false;
    }

    // Hook keybd_event
    if (MH_CreateHook(keybd_event, keybd_event_Detour, (LPVOID *)&keybd_event_Original) != MH_OK) {
        LogError("Failed to create keybd_event hook");
        return false;
    }

    // Hook mouse_event
    if (MH_CreateHook(mouse_event, mouse_event_Detour, (LPVOID *)&mouse_event_Original) != MH_OK) {
        LogError("Failed to create mouse_event hook");
        return false;
    }

    // Hook MapVirtualKey
    if (MH_CreateHook(MapVirtualKey, MapVirtualKey_Detour, (LPVOID *)&MapVirtualKey_Original) != MH_OK) {
        LogError("Failed to create MapVirtualKey hook");
        return false;
    }

    // Hook MapVirtualKeyEx
    if (MH_CreateHook(MapVirtualKeyEx, MapVirtualKeyEx_Detour, (LPVOID *)&MapVirtualKeyEx_Original) != MH_OK) {
        LogError("Failed to create MapVirtualKeyEx hook");
        return false;
    }

    // Hook DisplayConfigGetDeviceInfo
    if (MH_CreateHook(DisplayConfigGetDeviceInfo, DisplayConfigGetDeviceInfo_Detour, (LPVOID *)&DisplayConfigGetDeviceInfo_Original) != MH_OK) {
        LogError("Failed to create DisplayConfigGetDeviceInfo hook");
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
    DisplayConfigGetDeviceInfo_Original = nullptr;

    g_message_hooks_installed.store(false);
    LogInfo("Windows message hooks uninstalled successfully");
}

// Check if Windows message hooks are installed
bool AreWindowsMessageHooksInstalled() { return g_message_hooks_installed.load(); }

// Hook statistics access functions
const HookCallStats &GetHookStats(int hook_index) {
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

int GetHookCount() { return HOOK_COUNT; }

const char *GetHookName(int hook_index) {
    if (hook_index >= 0 && hook_index < HOOK_COUNT) {
        return g_hook_names[hook_index];
    }
    return "Unknown";
}

// Keyboard state tracking implementation
namespace keyboard_tracker {

// Keyboard state arrays (256 keys)
static std::array<std::atomic<bool>, 256> s_key_down{};
static std::array<std::atomic<bool>, 256> s_key_pressed{};
static std::array<bool, 256> s_prev_key_state{};

void Initialize() {
    // Initialize all states to false
    for (int i = 0; i < 256; ++i) {
        s_key_down[i].store(false);
        s_key_pressed[i].store(false);
        s_prev_key_state[i] = false;
    }
}

void Update() {
    // Update keyboard state using GetAsyncKeyState
    for (int vKey = 0; vKey < 256; ++vKey) {
        // Use the original GetAsyncKeyState to get real keyboard state
        SHORT state = GetAsyncKeyState_Original ? GetAsyncKeyState_Original(vKey) : GetAsyncKeyState(vKey);

        // Check if key is currently down (high-order bit)
        bool is_down = (state & 0x8000) != 0;

        // Update current state
        s_key_down[vKey].store(is_down);

        // Check if this is a new press (was up, now down)
        bool was_down = s_prev_key_state[vKey];
        if (is_down && !was_down) {
            s_key_pressed[vKey].store(true);
        }

        // Store current state for next frame
        s_prev_key_state[vKey] = is_down;
    }
}

void ResetFrame() {
    // Reset all pressed states for next frame
    for (int i = 0; i < 256; ++i) {
        s_key_pressed[i].store(false);
    }
}

bool IsKeyDown(int vKey) {
    if (vKey < 0 || vKey >= 256) return false;
    return s_key_down[vKey].load();
}

bool IsKeyPressed(int vKey) {
    if (vKey < 0 || vKey >= 256) return false;
    return s_key_pressed[vKey].load();
}

} // namespace keyboard_tracker

} // namespace display_commanderhooks

// Restore warning settings
#pragma clang diagnostic pop
#pragma warning(pop)
