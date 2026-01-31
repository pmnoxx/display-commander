/*
 * Copyright (C) 2024 Display Commander
 * Window procedure hooks implementation - logic moved to message retrieval hooks
 */

#include "window_proc_hooks.hpp"
#include <atomic>
#include "../exit_handler.hpp"
#include "../globals.hpp"
#include "../ui/new_ui/window_info_tab.hpp"
#include "../utils/logging.hpp"
#include "api_hooks.hpp"  // For GetGameWindow

#include "../../../../external/Streamline/source/plugins/sl.pcl/pclstats.h"

namespace display_commanderhooks {

// Global variables for hook state
static std::atomic<bool> g_sent_activate{false};

// Helper function to check if HWND belongs to current process
static bool IsWindowFromCurrentProcess(HWND hwnd) {
    if (hwnd == nullptr) {
        return false;
    }
    DWORD window_process_id = 0;
    DWORD window_thread_id = GetWindowThreadProcessId(hwnd, &window_process_id);
    if (window_thread_id == 0) {
        return false;
    }
    return window_process_id == GetCurrentProcessId();
}

// Process window message - returns true if message should be suppressed
// This function contains the logic previously in WindowProc_Detour
bool ProcessWindowMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Check if continue rendering is enabled
    // Special-K style: set ping signal when ping message is received, inject marker on next SIMULATION_START
    if (PCLSTATS_IS_PING_MSG_ID(uMsg)) {
        g_pclstats_ping_signal.store(true, std::memory_order_release);

        // We assume frame id is equal to frame before simulation_end
        PCLSTATS_MARKER(PC_LATENCY_PING, g_pclstats_frame_id.load());
    }

    bool continue_rendering_enabled = s_continue_rendering.load();

    // Send fake activation messages once when continue rendering is enabled
    HWND game_window = GetGameWindow();
    if (continue_rendering_enabled && game_window == hwnd && !g_sent_activate.load()) {
        SendFakeActivationMessages(hwnd);
        g_sent_activate.store(true);
    }

    // Handle specific window messages here
    switch (uMsg) {
        case WM_ACTIVATE: {
            // Handle window activation
            if (continue_rendering_enabled) {
                // Suppress focus loss messages when continue rendering is enabled
                if (LOWORD(wParam) == WA_INACTIVE) {
                    LogInfo("Suppressed window deactivation message due to continue rendering - HWND: 0x%p", hwnd);
                    // Update the message history to show this was suppressed
                    ui::new_ui::AddMessageToHistoryIfKnown(uMsg, wParam, lParam, true);
                    return true;  // Suppress the message
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
                LogInfo("Suppressed WM_KILLFOCUS message due to continue rendering - HWND: 0x%p", hwnd);
                // Update the message history to show this was suppressed
                ui::new_ui::AddMessageToHistoryIfKnown(uMsg, wParam, lParam, true);
                SendFakeActivationMessages(hwnd);
                return true;  // Suppress the message
            }
            LogInfo("Window focus lost message received - HWND: 0x%p", hwnd);
            break;

        case WM_ACTIVATEAPP:
            // Handle application activation/deactivation
            if (continue_rendering_enabled) {
                if (wParam == FALSE) {  // Application is being deactivated
                    LogInfo("WM_ACTIVATEAPP: Suppressing application deactivation - HWND: 0x%p", hwnd);
                    // Update the message history to show this was suppressed
                    ui::new_ui::AddMessageToHistoryIfKnown(uMsg, wParam, lParam, true);
                    // Send fake activation to keep the game thinking it's active
                    SendFakeActivationMessages(hwnd);
                    return true;  // Suppress the message
                } else {
                    // Application is being activated - ensure proper state
                    LogInfo("WM_ACTIVATEAPP: Application activated - ensuring continued rendering - HWND: 0x%p", hwnd);
                    // Send fake focus message to maintain active state
                    DetourWindowMessage(hwnd, WM_SETFOCUS, 0, 0);
                }
            }
            break;

        case WM_NCACTIVATE:
            // Handle non-client area activation
            if (continue_rendering_enabled) {
                if (wParam != FALSE) {
                    // Non-client area is being activated - ensure window stays active
                    LogInfo("WM_NCACTIVATE: Window activated - ensuring continued rendering - HWND: 0x%p", hwnd);
                    // Send fake focus message to maintain active state
                    DetourWindowMessage(hwnd, WM_SETFOCUS, 0, 0);
                    return true;  // Suppress the message
                } else {
                    // Non-client area is being deactivated - suppress and fake activation
                    LogInfo("WM_NCACTIVATE: Suppressing deactivation - HWND: 0x%p", hwnd);
                    // Update the message history to show this was suppressed
                    ui::new_ui::AddMessageToHistoryIfKnown(uMsg, wParam, lParam, true);
                    return true;  // Suppress the message
                }
            }
            break;

        case WM_WINDOWPOSCHANGING: {
            // Handle window position changes
            // Note: In message hooks, we can't modify the WINDOWPOS structure directly
            // We can only suppress the message, which prevents the window position change
            if (continue_rendering_enabled) {
                WINDOWPOS* pWp = reinterpret_cast<WINDOWPOS*>(lParam);
                if (pWp != nullptr && (pWp->flags & SWP_SHOWWINDOW)) {
                    // Check if window is being minimized
                    if (IsIconic(hwnd)) {
                        // Suppress the message to prevent minimization
                        LogInfo("WM_WINDOWPOSCHANGING: Suppressing minimize - HWND: 0x%p", hwnd);
                        ui::new_ui::AddMessageToHistoryIfKnown(uMsg, wParam, lParam, true);
                        return true;  // Suppress the message
                    }
                }
            }
            break;
        }

        case WM_WINDOWPOSCHANGED:
            // Handle window position changes
            if (continue_rendering_enabled) {
                WINDOWPOS* pWp = (WINDOWPOS*)lParam;
                // Check if window is being minimized or hidden
                if (pWp->flags & SWP_HIDEWINDOW) {
                    LogInfo("WM_WINDOWPOSCHANGED: Suppressing window hide - HWND: 0x%p", hwnd);
                    // Update the message history to show this was suppressed
                    ui::new_ui::AddMessageToHistoryIfKnown(uMsg, wParam, lParam, true);
                    return true;  // Suppress the message
                }
            }
            break;

        case WM_SHOWWINDOW:
            // Handle window visibility changes
            if (continue_rendering_enabled && wParam == FALSE) {
                // Suppress window hide messages when continue rendering is enabled
                // Update the message history to show this was suppressed
                ui::new_ui::AddMessageToHistoryIfKnown(uMsg, wParam, lParam, true);
                return true;  // Suppress the message
            }
            break;

        case WM_MOUSEACTIVATE:
            // Handle mouse activation
            if (continue_rendering_enabled) {
                LogInfo("WM_MOUSEACTIVATE: Activating and eating message - HWND: 0x%p", hwnd);
                // Note: We can't return MA_ACTIVATEANDEAT from message hooks, so we suppress the message
                // The game will handle activation through other means
                return true;  // Suppress the message
            }
            break;

        case WM_SYSCOMMAND:
            // Handle system commands
            if (continue_rendering_enabled) {
                // Prevent minimization when continue rendering is enabled
                if (wParam == SC_MINIMIZE) {
                    LogInfo("WM_SYSCOMMAND: Suppressing minimize command - HWND: 0x%p", hwnd);
                    // Update the message history to show this was suppressed
                    ui::new_ui::AddMessageToHistoryIfKnown(uMsg, wParam, lParam, true);
                    return true;  // Suppress the message
                }
            }
            break;

        case WM_QUIT:
            // Handle window quit message
            LogInfo("WM_QUIT: Window quit message received - HWND: 0x%p", hwnd);
            exit_handler::OnHandleExit(exit_handler::ExitSource::WINDOW_QUIT, "WM_QUIT message received");
            break;

        case WM_CLOSE:
            // Handle window close message
            LogInfo("WM_CLOSE: Window close message received - HWND: 0x%p", hwnd);
            exit_handler::OnHandleExit(exit_handler::ExitSource::WINDOW_CLOSE, "WM_CLOSE message received");
            break;

        case WM_DESTROY:
            // Handle window destroy message
            LogInfo("WM_DESTROY: Window destroy message received - HWND: 0x%p", hwnd);
            exit_handler::OnHandleExit(exit_handler::ExitSource::WINDOW_DESTROY, "WM_DESTROY message received");
            break;

        default: break;
    }

    // Track message as not suppressed
    ui::new_ui::AddMessageToHistoryIfKnown(uMsg, wParam, lParam, false);

    return false;  // Don't suppress the message
}

bool InstallWindowProcHooks(HWND target_hwnd) {
    // Window proc hooks are now handled via message retrieval hooks (GetMessage/PeekMessage)
    // This function is kept for compatibility but just sets the game window
    // Logic has been moved to ProcessWindowMessage() which is called from message hooks
    if (target_hwnd != nullptr) {
        SetGameWindow(target_hwnd);
        g_sent_activate.store(false);  // Reset activation flag when game window changes
    }
    return true;
}

void UninstallWindowProcHooks() {
    // Window proc hooks are now handled via message retrieval hooks
    // Just reset the activation flag
    g_sent_activate.store(false);
}

bool IsContinueRenderingEnabled() { return s_continue_rendering.load(); }

// Fake activation functions
void SendFakeActivationMessages(HWND hwnd) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return;
    }

    // Send fake activation messages to keep the game thinking it's active
    // Based on Special-K's patterns for better compatibility
    PostMessage(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
    PostMessage(hwnd, WM_SETFOCUS, 0, 0);
    PostMessage(hwnd, WM_ACTIVATEAPP, TRUE, 0);
    PostMessage(hwnd, WM_NCACTIVATE, TRUE, 0);

    LogInfo("Sent fake activation messages to window - HWND: 0x%p", hwnd);
}

// Get the currently hooked window (backward compatibility - uses game window)
HWND GetHookedWindow() { return GetGameWindow(); }

// Message detouring function (similar to Special-K's SK_DetourWindowProc)
LRESULT DetourWindowMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return 0;
    }

    // Send the message directly to the window procedure
    return SendMessage(hwnd, uMsg, wParam, lParam);
}

}  // namespace display_commanderhooks
