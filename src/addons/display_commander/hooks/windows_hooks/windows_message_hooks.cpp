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

// Hook state
static std::atomic<bool> g_message_hooks_installed{false};

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
        }
    }

    return result;
}

// Hooked GetMessageW function
BOOL WINAPI GetMessageW_Detour(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
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
        // Get the game window from API hooks
        HWND gameWindow = GetGameWindow();
        if (gameWindow != nullptr) {
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
    }

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

    // Clean up
    GetMessageA_Original = nullptr;
    GetMessageW_Original = nullptr;
    PeekMessageA_Original = nullptr;
    PeekMessageW_Original = nullptr;
    PostMessageA_Original = nullptr;
    PostMessageW_Original = nullptr;
    GetKeyboardState_Original = nullptr;

    g_message_hooks_installed.store(false);
    LogInfo("Windows message hooks uninstalled successfully");
}

// Check if Windows message hooks are installed
bool AreWindowsMessageHooksInstalled() {
    return g_message_hooks_installed.load();
}

} // namespace renodx::hooks
