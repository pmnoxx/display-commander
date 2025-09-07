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

        // Check if focus spoofing is enabled
        bool focus_spoofing_enabled = (s_spoof_window_focus.load() != 0);

        // Handle specific window messages here
        switch (pCwp->message) {
            case WM_ACTIVATE: {
                // Handle window activation
                if (focus_spoofing_enabled) {
                    // Suppress focus loss messages when spoofing is enabled
                    if (LOWORD(pCwp->wParam) == WA_INACTIVE) {
                        LogInfo("Suppressed window deactivation message due to focus spoofing - HWND: 0x%p", pCwp->hwnd);
                        return 0; // Suppress the message
                    }
                }
                LogInfo("Window activation message received - HWND: 0x%p, wParam: 0x%x",
                       pCwp->hwnd, pCwp->wParam);
                break;
            }

            case WM_SETFOCUS:
                // Handle focus changes - always allow focus gained
                LogInfo("Window focus message received - HWND: 0x%p", pCwp->hwnd);
                break;

            case WM_KILLFOCUS:
                // Handle focus loss - suppress if spoofing is enabled
                if (focus_spoofing_enabled) {
                    LogInfo("Suppressed WM_KILLFOCUS message due to focus spoofing - HWND: 0x%p", pCwp->hwnd);
                    return 0; // Suppress the message
                }
                LogInfo("Window focus lost message received - HWND: 0x%p", pCwp->hwnd);
                break;

            case WM_ACTIVATEAPP:
                // Handle application activation/deactivation
                if (focus_spoofing_enabled) {
                    if (pCwp->wParam == FALSE) { // Application is being deactivated
                        LogInfo("Suppressed WM_ACTIVATEAPP deactivation message due to focus spoofing - HWND: 0x%p", pCwp->hwnd);
                        return 0; // Suppress the message
                    }
                }
                LogInfo("Application activation message received - HWND: 0x%p, wParam: 0x%x", pCwp->hwnd, pCwp->wParam);
                break;

            case WM_NCACTIVATE:
                // Handle non-client area activation
                if (focus_spoofing_enabled) {
                    if (pCwp->wParam == FALSE) { // Non-client area is being deactivated
                        LogInfo("Suppressed WM_NCACTIVATE deactivation message due to focus spoofing - HWND: 0x%p", pCwp->hwnd);
                        return 0; // Suppress the message
                    }
                }
                LogInfo("Non-client activation message received - HWND: 0x%p, wParam: 0x%x", pCwp->hwnd, pCwp->wParam);
                break;

            case WM_WINDOWPOSCHANGING: {
                // Handle window position changes
                WINDOWPOS* pWp = (WINDOWPOS*)pCwp->lParam;

                // Check if this is a minimize/restore operation that might affect focus
                if (focus_spoofing_enabled && (pWp->flags & SWP_SHOWWINDOW)) {
                    // Check if window is being minimized
                    if (IsIconic(pCwp->hwnd)) {
                        LogInfo("Suppressed window minimize due to focus spoofing - HWND: 0x%p", pCwp->hwnd);
                        pWp->flags &= ~SWP_SHOWWINDOW; // Remove show window flag
                    }
                }

                LogInfo("Window position changing - HWND: 0x%p, flags: 0x%x",
                       pCwp->hwnd, pWp->flags);
                break;
            }


            case WM_WINDOWPOSCHANGED: {
                // Handle window position changes
                WINDOWPOS* pWpChanged = (WINDOWPOS*)pCwp->lParam;
                LogInfo("Window position changed - HWND: 0x%p, flags: 0x%x",
                       pCwp->hwnd, pWpChanged->flags);
                break;
            }

            case WM_STYLECHANGING: {
                // Handle style changes
                STYLESTRUCT* pStyle = (STYLESTRUCT*)pCwp->lParam;
                LogInfo("Window style changing - HWND: 0x%p, style: 0x%x",
                       pCwp->hwnd, pStyle->styleNew);
                break;
            }

            case WM_STYLECHANGED: {
                // Handle style changes
                STYLESTRUCT* pStyleChanged = (STYLESTRUCT*)pCwp->lParam;
                LogInfo("Window style changed - HWND: 0x%p, style: 0x%x",
                       pCwp->hwnd, pStyleChanged->styleNew);
                break;
            }

            default:
                break;
        }
    }

    // Call the next hook in the chain
    return CallNextHookEx(g_window_hook, nCode, wParam, lParam);
}

void InstallWindowStyleHooks() {
    if (g_hooks_installed.load()) {
        LogInfo("Window style hooks already installed");
        return;
    }

    // Install a window message hook
    g_window_hook = SetWindowsHookEx(
        WH_CALLWNDPROC,
        WindowHookProc,
        GetModuleHandle(nullptr),
        0
    );

    if (!g_window_hook) {
        DWORD error = GetLastError();
        LogError("Failed to install window style hooks. Error: %lu", error);
        return;
    }

    g_hooks_installed.store(true);
    LogInfo("Window style hooks installed successfully");
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

bool IsFocusSpoofingEnabled() {
    return (s_spoof_window_focus.load() != 0);
}

} // namespace renodx::hooks
