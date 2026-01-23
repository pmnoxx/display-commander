#include "windows_message_hooks.hpp"
#include <MinHook.h>
#include <array>
#include "../../globals.hpp"                             // For s_continue_rendering
#include "../../process_exit_hooks.hpp"                  // For UnhandledExceptionHandler
#include "../../settings/experimental_tab_settings.hpp"  // For g_experimentalTabSettings
#include "../../settings/main_tab_settings.hpp"
#include "../../ui/new_ui/window_info_tab.hpp"  // For message history tracking
#include "../../utils.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"
#include "../api_hooks.hpp"  // For GetGameWindow and other functions
#include "../hook_suppression_manager.hpp"

namespace display_commanderhooks {

bool IsUIOpenedRecently() { return g_global_frame_id.load() - g_last_ui_drawn_frame_id.load() < 3; }

// Helper functions for specific input types
bool ShouldBlockKeyboardInput(bool assume_foreground) {
    // Check if Ctrl+I toggle is active
    if (s_input_blocking_toggle.load()) {
        return true;
    }

    const bool is_background = g_app_in_background.load(std::memory_order_acquire) && !assume_foreground;
    const InputBlockingMode mode = s_keyboard_input_blocking.load();

    switch (mode) {
        case InputBlockingMode::kDisabled:            return false;
        case InputBlockingMode::kEnabled:             return true;
        case InputBlockingMode::kEnabledInBackground: return is_background;
        default:                                      return false;
    }
}

bool ShouldBlockMouseInput(bool assume_foreground) {
    // Check if Ctrl+I toggle is active
    if (s_input_blocking_toggle.load()) {
        return true;
    }

    const bool is_background = g_app_in_background.load(std::memory_order_acquire) && !assume_foreground;
    const InputBlockingMode mode = s_mouse_input_blocking.load();

    switch (mode) {
        case InputBlockingMode::kDisabled:                  return false;
        case InputBlockingMode::kEnabled:                   return true;
        case InputBlockingMode::kEnabledInBackground:       return is_background;
        case InputBlockingMode::kEnabledWhenXInputDetected: {
            // Check if XInput was detected recently (within last 180 frames, ~3 seconds at 60 FPS)
            uint64_t current_frame_id = g_global_frame_id.load();
            uint64_t last_xinput_frame_id = g_last_xinput_detected_frame_id.load();

            // If XInput was never detected, return false
            if (last_xinput_frame_id == 0) {
                return false;
            }

            // Check if XInput was detected within the last 180 frames
            constexpr uint64_t kXInputDetectionWindowFrames = 180;
            uint64_t frame_difference =
                current_frame_id > last_xinput_frame_id ? current_frame_id - last_xinput_frame_id : 0;

            return frame_difference < kXInputDetectionWindowFrames && !is_background;
        }
        default: return false;
    }
}

bool ShouldBlockGamepadInput() {
    const bool is_background = g_app_in_background.load(std::memory_order_acquire);
    const InputBlockingMode mode = s_gamepad_input_blocking.load();

    switch (mode) {
        case InputBlockingMode::kDisabled:            return false;
        case InputBlockingMode::kEnabled:             return true;
        case InputBlockingMode::kEnabledInBackground: return is_background;
        default:                                      return false;
    }
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
DefRawInputProc_pfn DefRawInputProc_Original = nullptr;
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
SetCapture_pfn SetCapture_Original = nullptr;
ReleaseCapture_pfn ReleaseCapture_Original = nullptr;
MapVirtualKey_pfn MapVirtualKey_Original = nullptr;
MapVirtualKeyEx_pfn MapVirtualKeyEx_Original = nullptr;
DisplayConfigGetDeviceInfo_pfn DisplayConfigGetDeviceInfo_Original = nullptr;
SetUnhandledExceptionFilter_pfn SetUnhandledExceptionFilter_Original = nullptr;
IsDebuggerPresent_pfn IsDebuggerPresent_Original = nullptr;

// Hook state
static std::atomic<bool> g_message_hooks_installed{false};

// Cursor position tracking (similar to Reshade)
static POINT s_last_cursor_position = {};
static RECT s_last_clip_cursor = {};

// Hook statistics array
std::array<HookCallStats, HOOK_COUNT> g_hook_stats;

// Hook information array
static const std::array<HookInfo, HOOK_COUNT> g_hook_info = {{// user32.dll hooks
                                                              HookInfo{"GetMessageA", DllGroup::USER32},
                                                              {"GetMessageW", DllGroup::USER32},
                                                              {"PeekMessageA", DllGroup::USER32},
                                                              {"PeekMessageW", DllGroup::USER32},
                                                              {"PostMessageA", DllGroup::USER32},
                                                              {"PostMessageW", DllGroup::USER32},
                                                              {"GetKeyboardState", DllGroup::USER32},
                                                              {"ClipCursor", DllGroup::USER32},
                                                              {"GetCursorPos", DllGroup::USER32},
                                                              {"SetCursorPos", DllGroup::USER32},
                                                              {"GetKeyState", DllGroup::USER32},
                                                              {"GetAsyncKeyState", DllGroup::USER32},
                                                              {"SetWindowsHookExA", DllGroup::USER32},
                                                              {"SetWindowsHookExW", DllGroup::USER32},
                                                              {"UnhookWindowsHookEx", DllGroup::USER32},
                                                              {"GetRawInputBuffer", DllGroup::USER32},
                                                              {"TranslateMessage", DllGroup::USER32},
                                                              {"DispatchMessageA", DllGroup::USER32},
                                                              {"DispatchMessageW", DllGroup::USER32},
                                                              {"GetRawInputData", DllGroup::USER32},
                                                              {"DefRawInputProc", DllGroup::USER32},
                                                              {"VkKeyScan", DllGroup::USER32},
                                                              {"VkKeyScanEx", DllGroup::USER32},
                                                              {"ToAscii", DllGroup::USER32},
                                                              {"ToAsciiEx", DllGroup::USER32},
                                                              {"ToUnicode", DllGroup::USER32},
                                                              {"ToUnicodeEx", DllGroup::USER32},
                                                              {"GetKeyNameTextA", DllGroup::USER32},
                                                              {"GetKeyNameTextW", DllGroup::USER32},
                                                              {"SendInput", DllGroup::USER32},
                                                              {"keybd_event", DllGroup::USER32},
                                                              {"mouse_event", DllGroup::USER32},
                                                              {"SetCapture", DllGroup::USER32},
                                                              {"ReleaseCapture", DllGroup::USER32},
                                                              {"MapVirtualKey", DllGroup::USER32},
                                                              {"MapVirtualKeyEx", DllGroup::USER32},
                                                              {"DisplayConfigGetDeviceInfo", DllGroup::USER32},

                                                              // xinput1_4.dll hooks
                                                              {"XInputGetState", DllGroup::XINPUT1_4},
                                                              {"XInputGetStateEx", DllGroup::XINPUT1_4},
                                                              {"XInputSetState", DllGroup::XINPUT1_4},
                                                              {"XInputGetCapabilities", DllGroup::XINPUT1_4},

                                                              // kernel32.dll hooks
                                                              {"Sleep", DllGroup::KERNEL32},
                                                              {"SleepEx", DllGroup::KERNEL32},
                                                              {"WaitForSingleObject", DllGroup::KERNEL32},
                                                              {"WaitForMultipleObjects", DllGroup::KERNEL32},
                                                              {"SetUnhandledExceptionFilter", DllGroup::KERNEL32},
                                                              {"IsDebuggerPresent", DllGroup::KERNEL32},
                                                              {"SetThreadExecutionState", DllGroup::KERNEL32},

                                                              // dinput8.dll hooks
                                                              {"DirectInput8Create", DllGroup::DINPUT8},

                                                              // dinput.dll hooks
                                                              {"DirectInputCreate", DllGroup::DINPUT},

                                                              // OpenGL/WGL hooks
                                                              {"wglSwapBuffers", DllGroup::OPENGL},
                                                              {"wglMakeCurrent", DllGroup::OPENGL},
                                                              {"wglCreateContext", DllGroup::OPENGL},
                                                              {"wglDeleteContext", DllGroup::OPENGL},
                                                              {"wglChoosePixelFormat", DllGroup::OPENGL},
                                                              {"wglSetPixelFormat", DllGroup::OPENGL},
                                                              {"wglGetPixelFormat", DllGroup::OPENGL},
                                                              {"wglDescribePixelFormat", DllGroup::OPENGL},
                                                              {"wglCreateContextAttribsARB", DllGroup::OPENGL},
                                                              {"wglChoosePixelFormatARB", DllGroup::OPENGL},
                                                              {"wglGetPixelFormatAttribivARB", DllGroup::OPENGL},
                                                              {"wglGetPixelFormatAttribfvARB", DllGroup::OPENGL},
                                                              {"wglGetProcAddress", DllGroup::OPENGL},
                                                              {"wglSwapIntervalEXT", DllGroup::OPENGL},
                                                              {"wglGetSwapIntervalEXT", DllGroup::OPENGL},

                                                              // Display Settings hooks
                                                              {"ChangeDisplaySettingsA", DllGroup::DISPLAY_SETTINGS},
                                                              {"ChangeDisplaySettingsW", DllGroup::DISPLAY_SETTINGS},
                                                              {"ChangeDisplaySettingsExA", DllGroup::DISPLAY_SETTINGS},
                                                              {"ChangeDisplaySettingsExW", DllGroup::DISPLAY_SETTINGS},
                                                              {"SetWindowPos", DllGroup::DISPLAY_SETTINGS},
                                                              {"ShowWindow", DllGroup::DISPLAY_SETTINGS},
                                                              {"SetWindowLongA", DllGroup::DISPLAY_SETTINGS},
                                                              {"SetWindowLongW", DllGroup::DISPLAY_SETTINGS},
                                                              {"SetWindowLongPtrA", DllGroup::DISPLAY_SETTINGS},
                                                              {"SetWindowLongPtrW", DllGroup::DISPLAY_SETTINGS},

                                                              // HID API hooks
                                                              {"HID_CreateFileA", DllGroup::HID_API},
                                                              {"HID_CreateFileW", DllGroup::HID_API},
                                                              {"HID_ReadFile", DllGroup::HID_API},
                                                              {"HID_WriteFile", DllGroup::HID_API},
                                                              {"HID_DeviceIoControl", DllGroup::HID_API},
                                                              {"HIDD_GetInputReport", DllGroup::HID_API},
                                                              {"HIDD_GetAttributes", DllGroup::HID_API},
                                                              {"HIDD_GetPreparsedData", DllGroup::HID_API},
                                                              {"HIDD_FreePreparsedData", DllGroup::HID_API},
                                                              {"HIDD_GetCaps", DllGroup::HID_API},
                                                              {"HIDD_GetManufacturerString", DllGroup::HID_API},
                                                              {"HIDD_GetProductString", DllGroup::HID_API},
                                                              {"HIDD_GetSerialNumberString", DllGroup::HID_API},
                                                              {"HIDD_GetNumInputBuffers", DllGroup::HID_API},
                                                              {"HIDD_SetNumInputBuffers", DllGroup::HID_API},
                                                              {"HIDD_GetFeature", DllGroup::HID_API},
                                                              {"HIDD_SetFeature", DllGroup::HID_API}}};

// Check if we should suppress a message (for input blocking)
bool ShouldSuppressMessage(HWND hWnd, UINT uMsg) {
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
            case WM_SYSDEADCHAR: return ShouldBlockKeyboardInput(true);
            // Mouse DOWN messages only
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_XBUTTONDOWN:
            case WM_MOUSEMOVE:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
            // Cursor messages
            case WM_SETCURSOR: return ShouldBlockMouseInput(true);
            // Allow UP events to pass through to clear stuck keys/buttons
            case WM_KEYUP:
            case WM_SYSKEYUP:
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
            case WM_MBUTTONUP:
            case WM_XBUTTONUP:
            default:           return false;
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
#pragma warning(disable : 4191)  // 'type cast': unsafe conversion from 'function_pointer' to 'data_pointer'
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmicrosoft-cast"

// Hooked GetMessageA function
BOOL WINAPI GetMessageA_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
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
            // Track suppressed message in history
            ui::new_ui::AddMessageToHistoryIfKnown(lpMsg->message, lpMsg->wParam, lpMsg->lParam, true);
        }
        // Check if we should intercept it for other purposes
        else {
            g_hook_stats[HOOK_GetMessageA].increment_unsuppressed();
            // Track unsuppressed message in history
            ui::new_ui::AddMessageToHistoryIfKnown(lpMsg->message, lpMsg->wParam, lpMsg->lParam, false);
        }
    }

    return result;
}

// Hooked GetMessageW function
BOOL WINAPI GetMessageW_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
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
            // Track suppressed message in history
            ui::new_ui::AddMessageToHistoryIfKnown(lpMsg->message, lpMsg->wParam, lpMsg->lParam, true);
        }
        // Check if we should intercept it for other purposes
        else {
            // Track unsuppressed calls
            g_hook_stats[HOOK_GetMessageW].increment_unsuppressed();
            // Track unsuppressed message in history
            ui::new_ui::AddMessageToHistoryIfKnown(lpMsg->message, lpMsg->wParam, lpMsg->lParam, false);
        }
    }

    return result;
}

// Hooked PeekMessageA function
BOOL WINAPI PeekMessageA_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_PeekMessageA].increment_total();

    // Call original function first
    BOOL result = PeekMessageA_Original ? PeekMessageA_Original(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg)
                                        : PeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);

    // Track unsuppressed calls (when we call the original function)
    g_hook_stats[HOOK_PeekMessageA].increment_unsuppressed();

    // If we got a message
    if (result && lpMsg != nullptr) {
        // Check if we should suppress this message (input blocking)
        if (ShouldSuppressMessage(hWnd, lpMsg->message)) {
            SuppressMessage(lpMsg);
            // Track suppressed message in history
            ui::new_ui::AddMessageToHistoryIfKnown(lpMsg->message, lpMsg->wParam, lpMsg->lParam, true);
        } else {
            // Track unsuppressed message in history
            ui::new_ui::AddMessageToHistoryIfKnown(lpMsg->message, lpMsg->wParam, lpMsg->lParam, false);
        }
    }

    return result;
}

// Hooked PeekMessageW function
BOOL WINAPI PeekMessageW_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_PeekMessageW].increment_total();

    // Call original function first
    BOOL result = PeekMessageW_Original ? PeekMessageW_Original(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg)
                                        : PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);

    // Track unsuppressed calls (when we call the original function)
    g_hook_stats[HOOK_PeekMessageW].increment_unsuppressed();

    // If we got a message
    if (result && lpMsg != nullptr) {
        // Check if we should suppress this message (input blocking)
        if (ShouldSuppressMessage(hWnd, lpMsg->message)) {
            SuppressMessage(lpMsg);
            // Track suppressed message in history
            ui::new_ui::AddMessageToHistoryIfKnown(lpMsg->message, lpMsg->wParam, lpMsg->lParam, true);
        } else {
            // Track unsuppressed message in history
            ui::new_ui::AddMessageToHistoryIfKnown(lpMsg->message, lpMsg->wParam, lpMsg->lParam, false);
        }
    }

    return result;
}

// Hooked PostMessageA function
BOOL WINAPI PostMessageA_Detour(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_PostMessageA].increment_total();

    if (ShouldBlockMouseInput() && Msg == WM_MOUSEMOVE) {
        return TRUE;
    }

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

        // Track suppressed message in history
        ui::new_ui::AddMessageToHistoryIfKnown(Msg, wParam, lParam, true);

        // Return TRUE to indicate the message was "processed" (blocked)
        return TRUE;
    }

    // Track unsuppressed calls (when we call the original function)
    g_hook_stats[HOOK_PostMessageA].increment_unsuppressed();

    // Track unsuppressed message in history
    ui::new_ui::AddMessageToHistoryIfKnown(Msg, wParam, lParam, false);

    // Call original function
    return PostMessageA_Original ? PostMessageA_Original(hWnd, Msg, wParam, lParam)
                                 : PostMessageA(hWnd, Msg, wParam, lParam);
}

// Hooked PostMessageW function
BOOL WINAPI PostMessageW_Detour(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_PostMessageW].increment_total();
    if (ShouldBlockMouseInput() && Msg == WM_MOUSEMOVE) {
        return TRUE;
    }

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

        // Track suppressed message in history
        ui::new_ui::AddMessageToHistoryIfKnown(Msg, wParam, lParam, true);

        // Return TRUE to indicate the message was "processed" (blocked)
        return TRUE;
    }

    // Track unsuppressed calls (when we call the original function)
    g_hook_stats[HOOK_PostMessageW].increment_unsuppressed();

    // Track unsuppressed message in history
    ui::new_ui::AddMessageToHistoryIfKnown(Msg, wParam, lParam, false);

    // Call original function
    return PostMessageW_Original ? PostMessageW_Original(hWnd, Msg, wParam, lParam)
                                 : PostMessageW(hWnd, Msg, wParam, lParam);
}

// Hooked GetKeyboardState function
BOOL WINAPI GetKeyboardState_Detour(PBYTE lpKeyState) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_GetKeyboardState].increment_total();

    // Call original function first
    BOOL result = GetKeyboardState_Original ? GetKeyboardState_Original(lpKeyState) : GetKeyboardState(lpKeyState);

    // Track unsuppressed calls (when we call the original function)
    g_hook_stats[HOOK_GetKeyboardState].increment_unsuppressed();

    // If keyboard input blocking is enabled and we got valid key state data
    if (result && lpKeyState != nullptr && ShouldBlockKeyboardInput()) {
        // Clear all key states to simulate no keys being pressed
        // This effectively blocks keyboard input at the state level
        memset(lpKeyState, 0, 256);  // 256 bytes for all virtual keys

        // Log occasionally for debugging
        static std::atomic<int> clear_counter{0};
        int count = clear_counter.fetch_add(1);
        if (count % 1000 == 0) {
            LogInfo("Cleared keyboard state for input blocking");
        }
    }

    return result;
}

// Function to call ClipCursor directly without going through the hook
BOOL ClipCursor_Direct(const RECT* lpRect) {
    // Call the original Windows API directly, bypassing our hook
    return ClipCursor_Original ? ClipCursor_Original(lpRect) : ClipCursor(lpRect);
}

// Function to restore cursor clipping when input blocking is disabled
void RestoreClipCursor() {
    // Only restore if we have a valid clipping rectangle stored
    if ((s_last_clip_cursor.right - s_last_clip_cursor.left) != 0
        && (s_last_clip_cursor.bottom - s_last_clip_cursor.top) != 0) {
        // Restore the previous clipping rectangle using direct call
        ClipCursor_Direct(&s_last_clip_cursor);
    }
}

// Function to clip cursor to game window rectangle
void ClipCursorToGameWindow() {
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd != nullptr && IsWindow(hwnd)) {
        RECT window_rect;
        if (GetWindowRect(hwnd, &window_rect)) {
            ClipCursor_Direct(&window_rect);
            LogInfo("ClipCursorToGameWindow: Clipped cursor to game window rect (%ld,%ld) to (%ld,%ld)",
                    window_rect.left, window_rect.top, window_rect.right, window_rect.bottom);
        }
    }
}

// Hooked ClipCursor function
BOOL WINAPI ClipCursor_Detour(const RECT* lpRect) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_ClipCursor].increment_total();

    // Store the clip rectangle for reference
    s_last_clip_cursor = (lpRect != nullptr) ? *lpRect : RECT{};

    // If input blocking is enabled, disable cursor clipping
    // if (ShouldBlockMouseInput()) {
    //    // Disable cursor clipping when input is blocked
    //    lpRect = nullptr;
    //}

    // supress clip cursor if enabled
    if (settings::g_mainTabSettings.clip_cursor_enabled.GetValue()) {
        return TRUE;
    }
    if (ShouldBlockMouseInput()) {
        lpRect = nullptr;
    }
    g_hook_stats[HOOK_ClipCursor].increment_unsuppressed();
    // Call original function
    return ClipCursor_Original != nullptr ? ClipCursor_Original(lpRect) : ClipCursor(lpRect);
}

// Hooked GetCursorPos function
BOOL WINAPI GetCursorPos_Detour(LPPOINT lpPoint) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_GetCursorPos].increment_total();

    // If mouse position spoofing is enabled AND auto-click is enabled, return spoofed position
    if (settings::g_experimentalTabSettings.mouse_spoofing_enabled.GetValue() && lpPoint != nullptr) {
        // Check if auto-click is enabled
        if (g_auto_click_enabled.load()) {
            lpPoint->x = s_spoofed_mouse_x.load();
            lpPoint->y = s_spoofed_mouse_y.load();
            return TRUE;
        }
    }

    // If mouse input blocking is enabled, return last known cursor position
    if (ShouldBlockMouseInput() && lpPoint != nullptr) {
        *lpPoint = s_last_cursor_position;
        return TRUE;
    }

    // Track unsuppressed calls (when we call the original function)
    g_hook_stats[HOOK_GetCursorPos].increment_unsuppressed();

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
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_SetCursorPos].increment_total();

    // Update last known cursor position
    s_last_cursor_position.x = X;
    s_last_cursor_position.y = Y;

    // If mouse position spoofing is enabled AND auto-click is enabled, update spoofed position instead of moving cursor
    if (settings::g_experimentalTabSettings.mouse_spoofing_enabled.GetValue()) {
        // Check if auto-click is enabled
        if (g_auto_click_enabled.load()) {
            s_spoofed_mouse_x.store(X);
            s_spoofed_mouse_y.store(Y);
            return TRUE;  // Return success without actually moving the cursor
        }
    }

    // If mouse input blocking is enabled, block cursor position changes
    if (ShouldBlockMouseInput()) {
        return TRUE;  // Block the cursor position change
    } else {
        g_hook_stats[HOOK_SetCursorPos].increment_unsuppressed();
    }

    // Call original function
    return SetCursorPos_Original ? SetCursorPos_Original(X, Y) : SetCursorPos(X, Y);
}

// Hooked GetKeyState function
SHORT WINAPI GetKeyState_Detour(int vKey) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_GetKeyState].increment_total();

    // If input blocking is enabled, return 0 for all keys
    if (ShouldBlockKeyboardInput() && (vKey >= 0x08 && vKey <= 0xFF)) {
        return 0;  // Block input
    }
    if (ShouldBlockMouseInput() && (vKey >= VK_LBUTTON && vKey <= VK_XBUTTON2)) {
        return 0;  // Block input
    }

    // Track unsuppressed calls
    g_hook_stats[HOOK_GetKeyState].increment_unsuppressed();

    // Call original function
    return GetKeyState_Original ? GetKeyState_Original(vKey) : GetKeyState(vKey);
}

// Hooked GetAsyncKeyState function
SHORT WINAPI GetAsyncKeyState_Detour(int vKey) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_GetAsyncKeyState].increment_total();

    // If input blocking is enabled, return 0 for all keys
    if (ShouldBlockKeyboardInput() && (vKey >= 0x08 && vKey <= 0xFF)) {
        return 0;  // Block input
    }
    if (ShouldBlockMouseInput() && (vKey >= VK_LBUTTON && vKey <= VK_XBUTTON2)) {
        return 0;  // Block input
    }

    // Track unsuppressed calls
    g_hook_stats[HOOK_GetAsyncKeyState].increment_unsuppressed();

    // Call original function
    return GetAsyncKeyState_Original ? GetAsyncKeyState_Original(vKey) : GetAsyncKeyState(vKey);
}

// Hooked SetWindowsHookExA function
HHOOK WINAPI SetWindowsHookExA_Detour(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_SetWindowsHookExA].increment_total();

    // Call original function first
    HHOOK result = SetWindowsHookExA_Original ? SetWindowsHookExA_Original(idHook, lpfn, hmod, dwThreadId)
                                              : SetWindowsHookExA(idHook, lpfn, hmod, dwThreadId);

    // Track unsuppressed calls (when we call the original function)
    g_hook_stats[HOOK_SetWindowsHookExA].increment_unsuppressed();

    // Log hook installation for debugging
    if (result != nullptr) {
        LogInfo("SetWindowsHookExA installed: idHook=%d, hmod=0x%p, dwThreadId=%lu", idHook, hmod, dwThreadId);
    }

    return result;
}

// Hooked SetWindowsHookExW function
HHOOK WINAPI SetWindowsHookExW_Detour(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_SetWindowsHookExW].increment_total();

    // Call original function first
    HHOOK result = SetWindowsHookExW_Original ? SetWindowsHookExW_Original(idHook, lpfn, hmod, dwThreadId)
                                              : SetWindowsHookExW(idHook, lpfn, hmod, dwThreadId);

    // Track unsuppressed calls (when we call the original function)
    g_hook_stats[HOOK_SetWindowsHookExW].increment_unsuppressed();

    // Log hook installation for debugging
    if (result != nullptr) {
        LogInfo("SetWindowsHookExW installed: idHook=%d, hmod=0x%p, dwThreadId=%lu", idHook, hmod, dwThreadId);
    }

    return result;
}

// Hooked UnhookWindowsHookEx function
BOOL WINAPI UnhookWindowsHookEx_Detour(HHOOK hhk) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_UnhookWindowsHookEx].increment_total();

    // Log hook removal for debugging
    LogInfo("UnhookWindowsHookEx called: hhk=0x%p", hhk);

    // Track unsuppressed calls (when we call the original function)
    g_hook_stats[HOOK_UnhookWindowsHookEx].increment_unsuppressed();

    // Call original function
    return UnhookWindowsHookEx_Original ? UnhookWindowsHookEx_Original(hhk) : UnhookWindowsHookEx(hhk);
}

// Hooked GetRawInputBuffer function
UINT WINAPI GetRawInputBuffer_Detour(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_GetRawInputBuffer].increment_total();

    // Call original function first
    UINT result = GetRawInputBuffer_Original ? GetRawInputBuffer_Original(pData, pcbSize, cbSizeHeader)
                                             : GetRawInputBuffer(pData, pcbSize, cbSizeHeader);

    // If input blocking is enabled and we got data, replace it
    if (result > 0 && pData != nullptr && pcbSize != nullptr) {
        // Replace blocked input data with harmless data
        PRAWINPUT current = pData;
        UINT processed_count = 0;

        for (UINT i = 0; i < result; ++i) {
            // CRITICAL: Save the original size BEFORE any modifications
            // This size is needed to correctly advance to the next RAWINPUT block
            const UINT original_size = current->header.dwSize;

            // Validate that we have a valid size (safety check)
            if (original_size < sizeof(RAWINPUTHEADER) || original_size > 1024) {
                // Invalid size, skip this block to avoid crash
                LogError("GetRawInputBuffer: Invalid dwSize %u, skipping block", original_size);
                break;
            }

            // Check if this input should be replaced
            bool should_replace = false;

            if (current->header.dwType == RIM_TYPEKEYBOARD && ShouldBlockKeyboardInput()) {
                should_replace = true;  // Replace all keyboard input
            } else if (current->header.dwType == RIM_TYPEMOUSE && ShouldBlockMouseInput()) {
                should_replace = true;  // Replace all mouse input
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
                        // Preserve original size - don't modify it
                        current->data.keyboard.MakeCode = 0;  // No scan code
                        current->data.keyboard.Flags = 0;     // Neutral flags - no key event
                        current->data.keyboard.Reserved = 0;
                        current->data.keyboard.VKey = 0;     // No virtual key
                        current->data.keyboard.Message = 0;  // No message
                        current->data.keyboard.ExtraInformation = 0;
                    }
                } else if (current->header.dwType == RIM_TYPEMOUSE) {
                    // Only block mouse DOWN events, allow UP events to clear stuck buttons
                    if (current->data.mouse.usButtonFlags
                        & (RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_UP | RI_MOUSE_MIDDLE_BUTTON_UP
                           | RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_UP)) {
                        // This is a mouse button UP event, don't block it
                        should_replace = false;
                    } else {
                        // This is a mouse DOWN event or movement, block it
                        current->header.dwType = RIM_TYPEMOUSE;
                        // Preserve original size - don't modify it
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

            // Move to next input using the ORIGINAL size (before any modifications)
            // Align to 8-byte boundary (required for RAWINPUT structures)
            UINT aligned_size = (original_size + 7) & ~7;
            current = (PRAWINPUT)((PBYTE)current + aligned_size);
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
BOOL WINAPI TranslateMessage_Detour(const MSG* lpMsg) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_TranslateMessage].increment_total();

    // If input blocking is enabled, don't translate messages that should be blocked
    if (lpMsg != nullptr) {
        // Check if this message should be suppressed
        if (ShouldSuppressMessage(lpMsg->hwnd, lpMsg->message)) {
            // Don't translate blocked messages
            return FALSE;
        }
    }

    // Track unsuppressed calls (when we call the original function)
    g_hook_stats[HOOK_TranslateMessage].increment_unsuppressed();

    // Call original function
    return TranslateMessage_Original ? TranslateMessage_Original(lpMsg) : TranslateMessage(lpMsg);
}

// Hooked DispatchMessageA function
LRESULT WINAPI DispatchMessageA_Detour(const MSG* lpMsg) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_DispatchMessageA].increment_total();

    // If input blocking is enabled, don't dispatch messages that should be blocked
    if (lpMsg != nullptr) {
        // Check if this message should be suppressed
        if (ShouldSuppressMessage(lpMsg->hwnd, lpMsg->message)) {
            // Track suppressed message in history
            ui::new_ui::AddMessageToHistoryIfKnown(lpMsg->message, lpMsg->wParam, lpMsg->lParam, true);
            // Return 0 to indicate the message was "processed" (blocked)
            return 0;
        } else {
            // Track unsuppressed message in history
            ui::new_ui::AddMessageToHistoryIfKnown(lpMsg->message, lpMsg->wParam, lpMsg->lParam, false);
        }
    }

    // Track unsuppressed calls (when we call the original function)
    g_hook_stats[HOOK_DispatchMessageA].increment_unsuppressed();

    // Call original function
    return DispatchMessageA_Original ? DispatchMessageA_Original(lpMsg) : DispatchMessageA(lpMsg);
}

// Hooked DispatchMessageW function
LRESULT WINAPI DispatchMessageW_Detour(const MSG* lpMsg) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_DispatchMessageW].increment_total();

    // If input blocking is enabled, don't dispatch messages that should be blocked
    if (lpMsg != nullptr) {
        // Check if this message should be suppressed
        if (ShouldSuppressMessage(lpMsg->hwnd, lpMsg->message)) {
            // Track suppressed message in history
            ui::new_ui::AddMessageToHistoryIfKnown(lpMsg->message, lpMsg->wParam, lpMsg->lParam, true);
            // Return 0 to indicate the message was "processed" (blocked)
            return 0;
        } else {
            // Track unsuppressed message in history
            ui::new_ui::AddMessageToHistoryIfKnown(lpMsg->message, lpMsg->wParam, lpMsg->lParam, false);
        }
    }

    // Track unsuppressed calls (when we call the original function)
    g_hook_stats[HOOK_DispatchMessageW].increment_unsuppressed();

    // Call original function
    return DispatchMessageW_Original ? DispatchMessageW_Original(lpMsg) : DispatchMessageW(lpMsg);
}

// Hooked GetRawInputData function
UINT WINAPI GetRawInputData_Detour(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize,
                                   UINT cbSizeHeader) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_GetRawInputData].increment_total();

    // Call original function first
    UINT result = GetRawInputData_Original
                      ? GetRawInputData_Original(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader)
                      : GetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);

    // If input blocking is enabled and we got data, filter it
    if (result != UINT(-1) && pData != nullptr && pcbSize != nullptr) {
        // For raw input data, we need to check if it's keyboard or mouse input
        if (uiCommand == RID_INPUT) {
            RAWINPUT* rawInput = static_cast<RAWINPUT*>(pData);

            // Only block DOWN events, allow UP events to clear stuck keys/buttons
            if (rawInput->header.dwType == RIM_TYPEKEYBOARD && ShouldBlockKeyboardInput()) {
                // Only block key DOWN events, allow UP events to clear stuck keys
                if (rawInput->data.keyboard.Flags & RI_KEY_BREAK) {
                    // This is a key UP event, don't block it - let it pass through
                } else {
                    // This is a key DOWN event, block it by replacing with neutral data
                    rawInput->header.dwType = RIM_TYPEKEYBOARD;
                    //  rawInput->header.dwSize = sizeof(RAWINPUT);
                    rawInput->data.keyboard.MakeCode = 0;  // No scan code
                    rawInput->data.keyboard.Flags = 0;     // Neutral flags - no key event
                    rawInput->data.keyboard.Reserved = 0;
                    rawInput->data.keyboard.VKey = 0;     // No virtual key
                    rawInput->data.keyboard.Message = 0;  // No message
                    rawInput->data.keyboard.ExtraInformation = 0;
                }
            } else if (rawInput->header.dwType == RIM_TYPEMOUSE && ShouldBlockMouseInput()) {
                // Only block mouse DOWN events, allow UP events to clear stuck buttons
                if (rawInput->data.mouse.usButtonFlags
                    & (RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_UP | RI_MOUSE_MIDDLE_BUTTON_UP
                       | RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_UP)) {
                    // This is a mouse button UP event, don't block it - let it pass through
                } else {
                    // This is a mouse DOWN event or movement, block it
                    rawInput->header.dwType = RIM_TYPEMOUSE;
                    // rawInput->header.dwSize = sizeof(RAWINPUT);
                    rawInput->data.mouse.usFlags = 0;
                    rawInput->data.mouse.usButtonFlags = 0;
                    rawInput->data.mouse.usButtonData = 0;
                    rawInput->data.mouse.ulRawButtons = 0;
                    rawInput->data.mouse.lLastX = 0;
                    rawInput->data.mouse.lLastY = 0;
                    rawInput->data.mouse.ulExtraInformation = 0;
                }
            } else {
                g_hook_stats[HOOK_GetRawInputData].increment_unsuppressed();
            }

            // Track unsuppressed calls (data was processed/replaced)
        }
    }
    return result;
}

// Hooked DefRawInputProc function
LRESULT WINAPI DefRawInputProc_Detour(PRAWINPUT paRawInput, INT nInput, UINT cbSizeHeader) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_hook_stats[HOOK_DefRawInputProc].increment_total();

    // Check if we should block raw input processing
    bool should_block = false;

    if (paRawInput != nullptr && nInput > 0) {
        for (INT i = 0; i < nInput; ++i) {
            PRAWINPUT current = &paRawInput[i];

            if (current->header.dwType == RIM_TYPEKEYBOARD && ShouldBlockKeyboardInput()) {
                should_block = true;
                break;
            } else if (current->header.dwType == RIM_TYPEMOUSE && ShouldBlockMouseInput()) {
                should_block = true;
                break;
            }
        }
    }

    if (should_block) {
        // Return 0 to indicate the message was processed (blocked)
        return 0;
    }

    // Call original function
    LRESULT result = DefRawInputProc_Original ? DefRawInputProc_Original(paRawInput, nInput, cbSizeHeader)
                                              : ::DefRawInputProc(&paRawInput, nInput, cbSizeHeader);

    g_hook_stats[HOOK_DefRawInputProc].increment_unsuppressed();
    return result;
}

// Hooked VkKeyScan function
SHORT WINAPI VkKeyScan_Detour(CHAR ch) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_VkKeyScan].increment_total();

    // If keyboard input blocking is enabled, return -1 to indicate no virtual key found
    if (ShouldBlockKeyboardInput()) {
        return -1;  // No virtual key found
    }

    // Track unsuppressed calls (when we call the original function)
    g_hook_stats[HOOK_VkKeyScan].increment_unsuppressed();

    // Call original function
    return VkKeyScan_Original ? VkKeyScan_Original(ch) : VkKeyScan(ch);
}

// Hooked VkKeyScanEx function
SHORT WINAPI VkKeyScanEx_Detour(CHAR ch, HKL dwhkl) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_VkKeyScanEx].increment_total();

    // If keyboard input blocking is enabled, return -1 to indicate no virtual key found
    if (ShouldBlockKeyboardInput()) {
        return -1;  // No virtual key found
    } else {
        g_hook_stats[HOOK_VkKeyScanEx].increment_unsuppressed();
    }

    // Call original function
    return VkKeyScanEx_Original ? VkKeyScanEx_Original(ch, dwhkl) : VkKeyScanEx(ch, dwhkl);
}

// Hooked ToAscii function
int WINAPI ToAscii_Detour(UINT uVirtKey, UINT uScanCode, const BYTE* lpKeyState, LPWORD lpChar, UINT uFlags) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_ToAscii].increment_total();

    // If keyboard input blocking is enabled, return 0 to indicate no character generated
    if (ShouldBlockKeyboardInput()) {
        return 0;  // No character generated
    }

    // Track unsuppressed calls (when we call the original function)
    g_hook_stats[HOOK_ToAscii].increment_unsuppressed();

    // Call original function
    return ToAscii_Original ? ToAscii_Original(uVirtKey, uScanCode, lpKeyState, lpChar, uFlags)
                            : ToAscii(uVirtKey, uScanCode, lpKeyState, lpChar, uFlags);
}

// Hooked ToAsciiEx function
int WINAPI ToAsciiEx_Detour(UINT uVirtKey, UINT uScanCode, const BYTE* lpKeyState, LPWORD lpChar, UINT uFlags,
                            HKL dwhkl) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_ToAsciiEx].increment_total();

    // If keyboard input blocking is enabled, return 0 to indicate no character generated
    if (ShouldBlockKeyboardInput()) {
        return 0;  // No character generated
    } else {
        g_hook_stats[HOOK_ToAsciiEx].increment_unsuppressed();
    }

    // Call original function
    return ToAsciiEx_Original ? ToAsciiEx_Original(uVirtKey, uScanCode, lpKeyState, lpChar, uFlags, dwhkl)
                              : ToAsciiEx(uVirtKey, uScanCode, lpKeyState, lpChar, uFlags, dwhkl);
}

// Hooked ToUnicode function
int WINAPI ToUnicode_Detour(UINT wVirtKey, UINT wScanCode, const BYTE* lpKeyState, LPWSTR pwszBuff, int cchBuff,
                            UINT wFlags) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_ToUnicode].increment_total();

    // If keyboard input blocking is enabled, return 0 to indicate no character generated
    if (ShouldBlockKeyboardInput()) {
        return 0;  // No character generated
    } else {
        g_hook_stats[HOOK_ToUnicode].increment_unsuppressed();
    }

    // Call original function
    return ToUnicode_Original ? ToUnicode_Original(wVirtKey, wScanCode, lpKeyState, pwszBuff, cchBuff, wFlags)
                              : ToUnicode(wVirtKey, wScanCode, lpKeyState, pwszBuff, cchBuff, wFlags);
}

// Hooked ToUnicodeEx function
int WINAPI ToUnicodeEx_Detour(UINT wVirtKey, UINT wScanCode, const BYTE* lpKeyState, LPWSTR pwszBuff, int cchBuff,
                              UINT wFlags, HKL dwhkl) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_ToUnicodeEx].increment_total();

    // If keyboard input blocking is enabled, return 0 to indicate no character generated
    if (ShouldBlockKeyboardInput()) {
        return 0;  // No character generated
    } else {
        g_hook_stats[HOOK_ToUnicodeEx].increment_unsuppressed();
    }

    // Call original function
    return ToUnicodeEx_Original
               ? ToUnicodeEx_Original(wVirtKey, wScanCode, lpKeyState, pwszBuff, cchBuff, wFlags, dwhkl)
               : ToUnicodeEx(wVirtKey, wScanCode, lpKeyState, pwszBuff, cchBuff, wFlags, dwhkl);
}

// Hooked GetKeyNameTextA function
int WINAPI GetKeyNameTextA_Detour(LONG lParam, LPSTR lpString, int cchSize) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_GetKeyNameTextA].increment_total();

    // If keyboard input blocking is enabled, return 0 to indicate no key name
    if (ShouldBlockKeyboardInput()) {
        HWND gameWindow = GetGameWindow();
        if (gameWindow != nullptr) {
            if (lpString != nullptr && cchSize > 0) {
                lpString[0] = '\0';  // Empty string
            }
            return 0;  // No key name
        }
    } else {
        g_hook_stats[HOOK_GetKeyNameTextA].increment_unsuppressed();
    }

    // Call original function
    return GetKeyNameTextA_Original ? GetKeyNameTextA_Original(lParam, lpString, cchSize)
                                    : GetKeyNameTextA(lParam, lpString, cchSize);
}

// Hooked GetKeyNameTextW function
int WINAPI GetKeyNameTextW_Detour(LONG lParam, LPWSTR lpString, int cchSize) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_GetKeyNameTextW].increment_total();

    // If keyboard input blocking is enabled, return 0 to indicate no key name
    if (ShouldBlockKeyboardInput()) {
        if (lpString != nullptr && cchSize > 0) {
            lpString[0] = L'\0';  // Empty string
        }
        return 0;  // No key name
    } else {
        g_hook_stats[HOOK_GetKeyNameTextW].increment_unsuppressed();
    }

    // Call original function
    return GetKeyNameTextW_Original ? GetKeyNameTextW_Original(lParam, lpString, cchSize)
                                    : GetKeyNameTextW(lParam, lpString, cchSize);
}

// Hooked SendInput function
UINT WINAPI SendInput_Detour(UINT nInputs, LPINPUT pInputs, int cbSize) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_SendInput].increment_total();

    // If keyboard input blocking is enabled, selectively block DOWN events
    if (ShouldBlockKeyboardInput() && pInputs != nullptr) {
        UINT allowed_inputs = 0;

        // Filter inputs - only allow UP events to pass through
        for (UINT i = 0; i < nInputs; ++i) {
            bool should_block = false;

            if (pInputs[i].type == INPUT_KEYBOARD) {
                // Block key DOWN events, allow UP events
                if (!(pInputs[i].ki.dwFlags & KEYEVENTF_KEYUP)) {
                    should_block = true;  // This is a key DOWN event
                }
            } else if (pInputs[i].type == INPUT_MOUSE) {
                // Block mouse DOWN events, allow UP events
                if (pInputs[i].mi.dwFlags
                    & (MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_XDOWN
                       | MOUSEEVENTF_MOVE | MOUSEEVENTF_WHEEL | MOUSEEVENTF_HWHEEL | MOUSEEVENTF_ABSOLUTE)) {
                    should_block = true;  // This is a mouse DOWN event
                }
            }

            if (!should_block) {
                // This is an UP event or non-input type, allow it through
                if (allowed_inputs != i) {
                    pInputs[allowed_inputs] = pInputs[i];  // Move to front of array
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
            nInputs = allowed_inputs;  // Update count to only process allowed inputs
        }
    } else {
        g_hook_stats[HOOK_SendInput].increment_unsuppressed();
    }

    // Call original function with potentially filtered inputs
    return SendInput_Original ? SendInput_Original(nInputs, pInputs, cbSize) : SendInput(nInputs, pInputs, cbSize);
}

// Hooked keybd_event function
void WINAPI keybd_event_Detour(BYTE bVk, BYTE bScan, DWORD dwFlags, ULONG_PTR dwExtraInfo) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_keybd_event].increment_total();

    // If keyboard input blocking is enabled, selectively block DOWN events
    if (ShouldBlockKeyboardInput()) {
        // Only block key DOWN events, allow UP events to clear stuck keys
        if (!(dwFlags & KEYEVENTF_KEYUP)) {
            // This is a key DOWN event, block it
            static std::atomic<int> block_counter{0};
            int count = block_counter.fetch_add(1);
            if (count % 100 == 0) {
                LogInfo("Blocked keybd_event DOWN: bVk=0x%02X, dwFlags=0x%08X", bVk, dwFlags);
            }
            return;  // Block keyboard DOWN event generation
        }
        // This is a key UP event, allow it through to clear stuck keys
    } else {
        g_hook_stats[HOOK_keybd_event].increment_unsuppressed();
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
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_mouse_event].increment_total();

    // If mouse input blocking is enabled, selectively block DOWN events
    if (ShouldBlockMouseInput()) {
        // Only block mouse DOWN events, allow UP events to clear stuck buttons
        if (dwFlags
            & (MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_XDOWN
               | MOUSEEVENTF_MOVE)) {
            // This is a mouse DOWN event, block it
            static std::atomic<int> block_counter{0};
            int count = block_counter.fetch_add(1);
            if (count % 100 == 0) {
                LogInfo("Blocked mouse_event DOWN: dwFlags=0x%08X, dx=%u, dy=%u", dwFlags, dx, dy);
            }
            return;  // Block mouse DOWN event generation
        }
        // This is a mouse UP event or movement, allow it through to clear stuck buttons
    } else {
        g_hook_stats[HOOK_mouse_event].increment_unsuppressed();
    }

    // Call original function
    if (mouse_event_Original) {
        mouse_event_Original(dwFlags, dx, dy, dwData, dwExtraInfo);
    } else {
        mouse_event(dwFlags, dx, dy, dwData, dwExtraInfo);
    }
}

// Hooked SetCapture function
HWND WINAPI SetCapture_Detour(HWND hWnd) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_SetCapture].increment_total();

    // if (hWnd != nullptr && ShouldBlockMouseInput()) {
    //     ReleaseCapture();
    //     return hWnd;
    // }

    // Log the capture attempt
    LogDebug("SetCapture_Detour: hWnd=0x%p", hWnd);

    // Call original function
    HWND result = SetCapture_Original ? SetCapture_Original(hWnd) : SetCapture(hWnd);

    // Track unsuppressed calls
    g_hook_stats[HOOK_SetCapture].increment_unsuppressed();

    return result;
}

// Hooked ReleaseCapture function
BOOL WINAPI ReleaseCapture_Detour() {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_ReleaseCapture].increment_total();

    // Log the release attempt
    LogDebug("ReleaseCapture_Detour: called");

    // Call original function
    BOOL result = ReleaseCapture_Original ? ReleaseCapture_Original() : ReleaseCapture();

    // Track unsuppressed calls
    g_hook_stats[HOOK_ReleaseCapture].increment_unsuppressed();

    return result;
}

// Hooked MapVirtualKey function
UINT WINAPI MapVirtualKey_Detour(UINT uCode, UINT uMapType) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_MapVirtualKey].increment_total();

    // If keyboard input blocking is enabled, return 0 for virtual key mapping
    if (ShouldBlockKeyboardInput()) {
        return 0;  // Block virtual key mapping
    } else {
        g_hook_stats[HOOK_MapVirtualKey].increment_unsuppressed();
    }

    // Call original function
    return MapVirtualKey_Original ? MapVirtualKey_Original(uCode, uMapType) : MapVirtualKey(uCode, uMapType);
}

// Hooked MapVirtualKeyEx function
UINT WINAPI MapVirtualKeyEx_Detour(UINT uCode, UINT uMapType, HKL dwhkl) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_MapVirtualKeyEx].increment_total();

    // If keyboard input blocking is enabled, return 0 for virtual key mapping
    if (ShouldBlockKeyboardInput()) {
        return 0;  // Block virtual key mapping
    } else {
        g_hook_stats[HOOK_MapVirtualKeyEx].increment_unsuppressed();
    }

    // Call original function
    return MapVirtualKeyEx_Original ? MapVirtualKeyEx_Original(uCode, uMapType, dwhkl)
                                    : MapVirtualKeyEx(uCode, uMapType, dwhkl);
}

// Hooked DisplayConfigGetDeviceInfo function
LONG WINAPI DisplayConfigGetDeviceInfo_Detour(DISPLAYCONFIG_DEVICE_INFO_HEADER* requestPacket) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_DisplayConfigGetDeviceInfo].increment_total();

    // Call original function
    LONG result = DisplayConfigGetDeviceInfo_Original ? DisplayConfigGetDeviceInfo_Original(requestPacket)
                                                      : DisplayConfigGetDeviceInfo(requestPacket);

    bool supressed_hdr = false;
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
            supressed_hdr = true;
        }
    }
    if (!supressed_hdr) {
        g_hook_stats[HOOK_DisplayConfigGetDeviceInfo].increment_unsuppressed();
    }

    // Track unsuppressed calls

    return result;
}

LPTOP_LEVEL_EXCEPTION_FILTER WINAPI
SetUnhandledExceptionFilter_Detour(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_SetUnhandledExceptionFilter].increment_total();

    // Spoof: Always install our own exception handler instead of the one passed by the game
    // This is similar to Special-K's approach - we ignore the parameter and force our handler
    // Call the original function but with our own handler instead of the game's handler
    LPTOP_LEVEL_EXCEPTION_FILTER result =
        SetUnhandledExceptionFilter_Original
            ? SetUnhandledExceptionFilter_Original(process_exit_hooks::UnhandledExceptionHandler)
            :  // Force our handler
            SetUnhandledExceptionFilter(process_exit_hooks::UnhandledExceptionHandler);

    // All calls are suppressed by overriding the argument.

    return result;
}

BOOL WINAPI IsDebuggerPresent_Detour() {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_IsDebuggerPresent].increment_total();

    // Call original function to get actual debugger status
    BOOL result = IsDebuggerPresent_Original ? IsDebuggerPresent_Original() : IsDebuggerPresent();

    // Log debugger detection attempts for monitoring
    if (result) {
        LogInfo("IsDebuggerPresent: Debugger detected by game");
    }

    // All calls are passed through (not suppressed)
    g_hook_stats[HOOK_IsDebuggerPresent].increment_unsuppressed();

    return result;
}

// Install Windows message hooks
bool InstallWindowsMessageHooks() {
    if (g_message_hooks_installed.load()) {
        LogInfo("Windows message hooks already installed");
        return true;
    }

    // Check if Windows message hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::WINDOW_API)) {
        LogInfo("Windows message hooks installation suppressed by user setting");
        return false;
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::WINDOWS_MESSAGE);
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
    if (!CreateAndEnableHook(GetMessageA, GetMessageA_Detour, (LPVOID*)&GetMessageA_Original, "GetMessageA")) {
        LogError("Failed to create and enable GetMessageA hook");
    }

    // Hook GetMessageW
    if (!CreateAndEnableHook(GetMessageW, GetMessageW_Detour, (LPVOID*)&GetMessageW_Original, "GetMessageW")) {
        LogError("Failed to create and enable GetMessageW hook");
    }

    // Hook PeekMessageA
    if (!CreateAndEnableHook(PeekMessageA, PeekMessageA_Detour, (LPVOID*)&PeekMessageA_Original, "PeekMessageA")) {
        LogError("Failed to create and enable PeekMessageA hook");
    }

    // Hook PeekMessageW
    if (!CreateAndEnableHook(PeekMessageW, PeekMessageW_Detour, (LPVOID*)&PeekMessageW_Original, "PeekMessageW")) {
        LogError("Failed to create and enable PeekMessageW hook");
    }

    // Hook PostMessageA
    if (!CreateAndEnableHook(PostMessageA, PostMessageA_Detour, (LPVOID*)&PostMessageA_Original, "PostMessageA")) {
        LogError("Failed to create and enable PostMessageA hook");
    }

    // Hook PostMessageW
    if (!CreateAndEnableHook(PostMessageW, PostMessageW_Detour, (LPVOID*)&PostMessageW_Original, "PostMessageW")) {
        LogError("Failed to create and enable PostMessageW hook");
    }

    // Hook GetKeyboardState
    if (!CreateAndEnableHook(GetKeyboardState, GetKeyboardState_Detour, (LPVOID*)&GetKeyboardState_Original,
                             "GetKeyboardState")) {
        LogError("Failed to create and enable GetKeyboardState hook");
    }

    // Hook ClipCursor
    if (!CreateAndEnableHook(ClipCursor, ClipCursor_Detour, (LPVOID*)&ClipCursor_Original, "ClipCursor")) {
        LogError("Failed to create and enable ClipCursor hook");
    }

    // Hook GetCursorPos
    if (!CreateAndEnableHook(GetCursorPos, GetCursorPos_Detour, (LPVOID*)&GetCursorPos_Original, "GetCursorPos")) {
        LogError("Failed to create and enable GetCursorPos hook");
    }

    // Hook SetCursorPos
    if (!CreateAndEnableHook(SetCursorPos, SetCursorPos_Detour, (LPVOID*)&SetCursorPos_Original, "SetCursorPos")) {
        LogError("Failed to create and enable SetCursorPos hook");
    }

    // Hook GetKeyState
    if (!CreateAndEnableHook(GetKeyState, GetKeyState_Detour, (LPVOID*)&GetKeyState_Original, "GetKeyState")) {
        LogError("Failed to create and enable GetKeyState hook");
    }

    // Hook GetAsyncKeyState
    if (!CreateAndEnableHook(GetAsyncKeyState, GetAsyncKeyState_Detour, (LPVOID*)&GetAsyncKeyState_Original,
                             "GetAsyncKeyState")) {
        LogError("Failed to create and enable GetAsyncKeyState hook");
    }

    // Hook SetWindowsHookExA
    if (!CreateAndEnableHook(SetWindowsHookExA, SetWindowsHookExA_Detour, (LPVOID*)&SetWindowsHookExA_Original,
                             "SetWindowsHookExA")) {
        LogError("Failed to create and enable SetWindowsHookExA hook");
    }

    // Hook SetWindowsHookExW
    if (!CreateAndEnableHook(SetWindowsHookExW, SetWindowsHookExW_Detour, (LPVOID*)&SetWindowsHookExW_Original,
                             "SetWindowsHookExW")) {
        LogError("Failed to create and enable SetWindowsHookExW hook");
    }

    // Hook UnhookWindowsHookEx
    if (!CreateAndEnableHook(UnhookWindowsHookEx, UnhookWindowsHookEx_Detour, (LPVOID*)&UnhookWindowsHookEx_Original,
                             "UnhookWindowsHookEx")) {
        LogError("Failed to create and enable UnhookWindowsHookEx hook");
    }

    // Hook GetRawInputBuffer
    if (!CreateAndEnableHook(GetRawInputBuffer, GetRawInputBuffer_Detour, (LPVOID*)&GetRawInputBuffer_Original,
                             "GetRawInputBuffer")) {
        LogError("Failed to create and enable GetRawInputBuffer hook");
    }

    // Hook TranslateMessage
    if (!CreateAndEnableHook(TranslateMessage, TranslateMessage_Detour, (LPVOID*)&TranslateMessage_Original,
                             "TranslateMessage")) {
        LogError("Failed to create and enable TranslateMessage hook");
    }

    // Hook DispatchMessageA
    if (!CreateAndEnableHook(DispatchMessageA, DispatchMessageA_Detour, (LPVOID*)&DispatchMessageA_Original,
                             "DispatchMessageA")) {
        LogError("Failed to create and enable DispatchMessageA hook");
    }

    // Hook DispatchMessageW
    if (!CreateAndEnableHook(DispatchMessageW, DispatchMessageW_Detour, (LPVOID*)&DispatchMessageW_Original,
                             "DispatchMessageW")) {
        LogError("Failed to create and enable DispatchMessageW hook");
    }

    // Hook GetRawInputData
    if (!CreateAndEnableHook(GetRawInputData, GetRawInputData_Detour, (LPVOID*)&GetRawInputData_Original,
                             "GetRawInputData")) {
        LogError("Failed to create and enable GetRawInputData hook");
    }

    // Hook DefRawInputProc
    if (!CreateAndEnableHook(DefRawInputProc, DefRawInputProc_Detour, (LPVOID*)&DefRawInputProc_Original,
                             "DefRawInputProc")) {
        LogError("Failed to create and enable DefRawInputProc hook");
    }

    // Hook VkKeyScan
    if (!CreateAndEnableHook(VkKeyScan, VkKeyScan_Detour, (LPVOID*)&VkKeyScan_Original, "VkKeyScan")) {
        LogError("Failed to create and enable VkKeyScan hook");
    }

    // Hook VkKeyScanEx
    if (!CreateAndEnableHook(VkKeyScanEx, VkKeyScanEx_Detour, (LPVOID*)&VkKeyScanEx_Original, "VkKeyScanEx")) {
        LogError("Failed to create and enable VkKeyScanEx hook");
    }

    // Hook ToAscii
    if (!CreateAndEnableHook(ToAscii, ToAscii_Detour, (LPVOID*)&ToAscii_Original, "ToAscii")) {
        LogError("Failed to create and enable ToAscii hook");
    }

    // Hook ToAsciiEx
    if (!CreateAndEnableHook(ToAsciiEx, ToAsciiEx_Detour, (LPVOID*)&ToAsciiEx_Original, "ToAsciiEx")) {
        LogError("Failed to create and enable ToAsciiEx hook");
    }

    // Hook ToUnicode
    if (!CreateAndEnableHook(ToUnicode, ToUnicode_Detour, (LPVOID*)&ToUnicode_Original, "ToUnicode")) {
        LogError("Failed to create and enable ToUnicode hook");
    }

    // Hook ToUnicodeEx
    if (!CreateAndEnableHook(ToUnicodeEx, ToUnicodeEx_Detour, (LPVOID*)&ToUnicodeEx_Original, "ToUnicodeEx")) {
        LogError("Failed to create and enable ToUnicodeEx hook");
    }

    // Hook GetKeyNameTextA
    if (!CreateAndEnableHook(GetKeyNameTextA, GetKeyNameTextA_Detour, (LPVOID*)&GetKeyNameTextA_Original,
                             "GetKeyNameTextA")) {
        LogError("Failed to create and enable GetKeyNameTextA hook");
    }

    // Hook GetKeyNameTextW
    if (!CreateAndEnableHook(GetKeyNameTextW, GetKeyNameTextW_Detour, (LPVOID*)&GetKeyNameTextW_Original,
                             "GetKeyNameTextW")) {
        LogError("Failed to create and enable GetKeyNameTextW hook");
    }

    // Hook SendInput
    if (!CreateAndEnableHook(SendInput, SendInput_Detour, (LPVOID*)&SendInput_Original, "SendInput")) {
        LogError("Failed to create and enable SendInput hook");
    }

    // Hook keybd_event
    if (!CreateAndEnableHook(keybd_event, keybd_event_Detour, (LPVOID*)&keybd_event_Original, "keybd_event")) {
        LogError("Failed to create and enable keybd_event hook");
    }

    // Hook mouse_event
    if (!CreateAndEnableHook(mouse_event, mouse_event_Detour, (LPVOID*)&mouse_event_Original, "mouse_event")) {
        LogError("Failed to create and enable mouse_event hook");
    }

    // Hook SetCapture
    if (!CreateAndEnableHook(SetCapture, SetCapture_Detour, (LPVOID*)&SetCapture_Original, "SetCapture")) {
        LogError("Failed to create and enable SetCapture hook");
    }

    // Hook ReleaseCapture
    if (!CreateAndEnableHook(ReleaseCapture, ReleaseCapture_Detour, (LPVOID*)&ReleaseCapture_Original,
                             "ReleaseCapture")) {
        LogError("Failed to create and enable ReleaseCapture hook");
    }

    // Hook MapVirtualKey
    if (!CreateAndEnableHook(MapVirtualKey, MapVirtualKey_Detour, (LPVOID*)&MapVirtualKey_Original, "MapVirtualKey")) {
        LogError("Failed to create and enable MapVirtualKey hook");
    }

    // Hook MapVirtualKeyEx
    if (!CreateAndEnableHook(MapVirtualKeyEx, MapVirtualKeyEx_Detour, (LPVOID*)&MapVirtualKeyEx_Original,
                             "MapVirtualKeyEx")) {
        LogError("Failed to create and enable MapVirtualKeyEx hook");
    }

    // Hook DisplayConfigGetDeviceInfo
    if (!CreateAndEnableHook(DisplayConfigGetDeviceInfo, DisplayConfigGetDeviceInfo_Detour,
                             (LPVOID*)&DisplayConfigGetDeviceInfo_Original, "DisplayConfigGetDeviceInfo")) {
        LogError("Failed to create and enable DisplayConfigGetDeviceInfo hook");
    }

    // Hook SetUnhandledExceptionFilter
    if (!CreateAndEnableHook(SetUnhandledExceptionFilter, SetUnhandledExceptionFilter_Detour,
                             (LPVOID*)&SetUnhandledExceptionFilter_Original, "SetUnhandledExceptionFilter")) {
        LogError("Failed to create and enable SetUnhandledExceptionFilter hook");
    }

    // Hook IsDebuggerPresent
    if (!CreateAndEnableHook(IsDebuggerPresent, IsDebuggerPresent_Detour, (LPVOID*)&IsDebuggerPresent_Original,
                             "IsDebuggerPresent")) {
        LogError("Failed to create and enable IsDebuggerPresent hook");
    }

    g_message_hooks_installed.store(true);
    LogInfo("Windows message hooks installed successfully");

    // Mark Windows message hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::WINDOW_API);

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
    MH_RemoveHook(DefRawInputProc);
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
    MH_RemoveHook(IsDebuggerPresent);

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
    DefRawInputProc_Original = nullptr;
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
    SetCapture_Original = nullptr;
    ReleaseCapture_Original = nullptr;
    MapVirtualKey_Original = nullptr;
    MapVirtualKeyEx_Original = nullptr;
    DisplayConfigGetDeviceInfo_Original = nullptr;
    SetUnhandledExceptionFilter_Original = nullptr;
    IsDebuggerPresent_Original = nullptr;

    g_message_hooks_installed.store(false);
    LogInfo("Windows message hooks uninstalled successfully");
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

int GetHookCount() { return HOOK_COUNT; }

const char* GetHookName(int hook_index) {
    if (hook_index >= 0 && hook_index < HOOK_COUNT) {
        return g_hook_info[hook_index].name;
    }
    return "Unknown";
}

// DLL group helper functions
const char* GetDllGroupName(DllGroup group) {
    switch (group) {
        case DllGroup::USER32:           return "user32.dll";
        case DllGroup::XINPUT1_4:        return "xinput1_4.dll";
        case DllGroup::KERNEL32:         return "kernel32.dll";
        case DllGroup::DINPUT8:          return "dinput8.dll";
        case DllGroup::DINPUT:           return "dinput.dll";
        case DllGroup::OPENGL:           return "opengl32.dll";
        case DllGroup::DISPLAY_SETTINGS: return "user32.dll (display_settings)";
        case DllGroup::HID_API:          return "kernel32.dll (hid_api)";
        default:                         return "Unknown";
    }
}

DllGroup GetHookDllGroup(int hook_index) {
    if (hook_index >= 0 && hook_index < HOOK_COUNT) {
        return g_hook_info[hook_index].dll_group;
    }
    return DllGroup::COUNT;
}

const HookInfo& GetHookInfo(int hook_index) {
    static const HookInfo empty_info = {"Unknown", DllGroup::COUNT};
    if (hook_index >= 0 && hook_index < HOOK_COUNT) {
        return g_hook_info[hook_index];
    }
    return empty_info;
}

// Keyboard state tracking implementation
namespace keyboard_tracker {

// Keyboard state arrays (256 keys)
std::array<std::atomic<bool>, 256> s_key_down{};
std::array<std::atomic<bool>, 256> s_key_pressed{};
std::array<bool, 256> s_prev_key_state{};
// only get key state if it was ever checked
std::array<bool, 256> was_ever_checked{};

void Initialize() {
    // Initialize all states to false
    for (int i = 0; i < 256; ++i) {
        s_key_down[i].store(false);
        s_key_pressed[i].store(false);
        s_prev_key_state[i] = false;
    }
}

void Update() {
    auto first_reshade_runtime = GetFirstReShadeRuntime();
    // Update keyboard state using GetAsyncKeyState
    for (int vKey = 0; vKey < 256; ++vKey) {
        if (!was_ever_checked[vKey]) {
            continue;
        }
        // Use the original GetAsyncKeyState to get real keyboard state
        SHORT state = GetAsyncKeyState_Original ? GetAsyncKeyState_Original(vKey) : GetAsyncKeyState(vKey);
        if (first_reshade_runtime != nullptr) {
            state |= first_reshade_runtime->is_key_down(vKey) ? 0x8000 : 0;
        }

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
    was_ever_checked[vKey] = true;
    return s_key_down[vKey].load();
}

bool IsKeyPressed(int vKey) {
    if (vKey < 0 || vKey >= 256) return false;
    was_ever_checked[vKey] = true;
    return s_key_pressed[vKey].load();
}

}  // namespace keyboard_tracker

}  // namespace display_commanderhooks

// Restore warning settings
#pragma clang diagnostic pop
#pragma warning(pop)
