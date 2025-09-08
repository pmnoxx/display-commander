/*
 * Copyright (C) 2024 Display Commander
 * Window style hooks implementation
 */

#include "window_style_hooks.hpp"
#include "../utils.hpp"
#include "../globals.hpp"
#include <atomic>

namespace renodx::hooks {

// Global variables for hook state
static std::atomic<bool> g_hooks_installed{false};
static HHOOK g_window_hook = nullptr;
HWND g_hooked_window = nullptr;
WNDPROC g_original_window_proc = nullptr;

// Hook procedure for window messages
LRESULT CALLBACK WindowStyleHookProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Call the original window procedure first
    if (g_original_window_proc) {
        return CallWindowProc(g_original_window_proc, hwnd, uMsg, wParam, lParam);
    }

    // Fallback to default window procedure
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// Hook procedure for window messages via SetWindowsHookEx
LRESULT CALLBACK WindowHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        CWPSTRUCT* pCwp = (CWPSTRUCT*)lParam;

        // Check if continue rendering is enabled
        bool continue_rendering_enabled = s_continue_rendering.load();

        // Handle specific window messages here
        switch (pCwp->message) {
            case WM_ACTIVATE: {
                // Handle window activation
                if (continue_rendering_enabled) {
                    // Suppress focus loss messages when continue rendering is enabled
                    if (LOWORD(pCwp->wParam) == WA_INACTIVE) {
                        // Send fake activation to keep the game thinking it's active
                        SendFakeActivationMessages(pCwp->hwnd);
                        return 0; // Suppress the message
                    }
                }
                break;
            }

            case WM_SETFOCUS:
                // Handle focus changes - always allow focus gained
                break;

            case WM_KILLFOCUS:
                // Handle focus loss - suppress if continue rendering is enabled
                if (continue_rendering_enabled) {
                    // Send fake activation to keep the game thinking it's active
                    SendFakeActivationMessages(pCwp->hwnd);
                    return 0; // Suppress the message
                }
                break;

            case WM_ACTIVATEAPP:
                // Handle application activation/deactivation
                if (continue_rendering_enabled) {
                    if (pCwp->wParam == FALSE) { // Application is being deactivated
                        // Send fake activation to keep the game thinking it's active
                        SendFakeActivationMessages(pCwp->hwnd);
                        return 0; // Suppress the message
                    }
                }
                break;

            case WM_NCACTIVATE:
                // Handle non-client area activation
                if (continue_rendering_enabled) {
                    if (pCwp->wParam == FALSE) { // Non-client area is being deactivated
                        // Send fake activation to keep the game thinking it's active
                        SendFakeActivationMessages(pCwp->hwnd);
                        return 0; // Suppress the message
                    }
                }
                break;

            case WM_WINDOWPOSCHANGING: {
                // Handle window position changes
                WINDOWPOS* pWp = (WINDOWPOS*)pCwp->lParam;

                // Check if this is a minimize/restore operation that might affect focus
                if (continue_rendering_enabled && (pWp->flags & SWP_SHOWWINDOW)) {
                    // Check if window is being minimized
                    if (IsIconic(pCwp->hwnd)) {
                        pWp->flags &= ~SWP_SHOWWINDOW; // Remove show window flag
                    }
                }
                break;
            }


            case WM_WINDOWPOSCHANGED: {
                // Handle window position changes
                break;
            }

            case WM_SHOWWINDOW: {
                // Handle window visibility changes
                if (continue_rendering_enabled && pCwp->wParam == FALSE) {
                    // Suppress window hide messages when continue rendering is enabled
                    // Send fake activation to keep the game thinking it's active
                    SendFakeActivationMessages(pCwp->hwnd);
                    return 0; // Suppress the message
                }
                break;
            }

            case WM_MOUSEACTIVATE: {
                // Handle mouse activation
                if (continue_rendering_enabled) {
                    // Always activate and eat the message to prevent focus loss
                    return MA_ACTIVATEANDEAT; // Activate and eat the message
                }
                break;
            }

            case WM_STYLECHANGING: {
                // Handle style changes
                break;
            }

            case WM_STYLECHANGED: {
                // Handle style changes
                break;
            }

            default:
                break;
        }
    }

    // Call the next hook in the chain
    return CallNextHookEx(g_window_hook, nCode, wParam, lParam);
}

void InstallWindowStyleHooks(HMODULE hModule) {
    LogInfo("InstallWindowStyleHooks called");

    if (g_hooks_installed.load()) {
        LogInfo("Window style hooks already installed");
        return;
    }

    // Get module handle
    LogInfo("Module handle: 0x%p", hModule);

    // Debug: Try to find the game window
    HWND gameWindow = FindWindow(nullptr, nullptr);
    if (gameWindow) {
        DWORD gameThreadId = GetWindowThreadProcessId(gameWindow, nullptr);
        LogInfo("Found potential game window: 0x%p, Thread ID: %lu", gameWindow, gameThreadId);
    }

    // Install a window message hook for all threads (0 = all threads)
    LogInfo("Installing WH_CALLWNDPROC hook for all threads...");
    g_window_hook = SetWindowsHookEx(
        WH_CALLWNDPROC,
        WindowHookProc,
        hModule,
        0  // 0 = all threads in the current desktop
    );

    if (!g_window_hook) {
        DWORD error = GetLastError();
        LogError("Failed to install window style hooks. Error: %lu (0x%lx)", error, error);

        // Try alternative hook types as fallback
        LogInfo("Trying alternative hook types...");

        // Try WH_GETMESSAGE hook
        g_window_hook = SetWindowsHookEx(
            WH_GETMESSAGE,
            WindowHookProc,
            hModule,
            0
        );

        if (g_window_hook) {
            LogInfo("Successfully installed WH_GETMESSAGE hook as fallback");
        } else {
            error = GetLastError();
            LogError("Failed to install WH_GETMESSAGE hook. Error: %lu (0x%lx)", error, error);
            return;
        }
    }

    g_hooks_installed.store(true);
    LogInfo("Window style hooks installed successfully - Hook handle: 0x%p", g_window_hook);

    // Debug: Show current continue rendering state
    bool current_state = s_continue_rendering.load();
    LogInfo("Window style hooks installed - continue_rendering state: %s", current_state ? "enabled" : "disabled");
}

void UninstallWindowStyleHooks() {
    if (!g_hooks_installed.load()) {
        LogInfo("Window style hooks not installed");
        return;
    }

    // Uninstall the window message hook
    if (g_window_hook) {
        UnhookWindowsHookEx(g_window_hook);
        g_window_hook = nullptr;
    }

    // Restore original window procedure if we hooked it
    if (g_hooked_window && g_original_window_proc) {
        SetWindowLongPtr(g_hooked_window, GWLP_WNDPROC, (LONG_PTR)g_original_window_proc);
        g_hooked_window = nullptr;
        g_original_window_proc = nullptr;
    }

    g_hooks_installed.store(false);
    LogInfo("Window style hooks uninstalled successfully");
}

bool AreWindowStyleHooksInstalled() {
    return g_hooks_installed.load();
}

bool IsContinueRenderingEnabled() {
    return s_continue_rendering.load();
}

// Fake activation functions
void SendFakeActivationMessages(HWND hwnd) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return;
    }

    // Send fake activation messages to keep the game thinking it's active
    PostMessage(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
    PostMessage(hwnd, WM_SETFOCUS, 0, 0);
    PostMessage(hwnd, WM_ACTIVATEAPP, TRUE, 0);

    LogInfo("Sent fake activation messages to window - HWND: 0x%p", hwnd);
}

void FakeActivateWindow(HWND hwnd) {
    if (!s_continue_rendering.load()) {
        return;
    }

    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return;
    }

    // Send fake activation messages
    SendFakeActivationMessages(hwnd);

    // Also try to bring the window to front (but don't steal focus from other apps)
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    LogInfo("Fake activated window - HWND: 0x%p", hwnd);
}

} // namespace renodx::hooks
