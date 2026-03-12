/*
 * Copyright (C) 2024 Display Commander
 * Window procedure hooks implementation - same approach as Special-K SK_InstallWindowHook:
 * - Hook into window message queue via SetWindowLongPtr(GWLP_WNDPROC) on the target HWND.
 * - Trampoline (WindowProc_Detour): call ProcessWindowMessage; if not suppressed, call
 *   original WNDPROC (from map). Original is stored before swapping to avoid early-message race.
 * - Game window is stored in atomic (SetGameWindow/GetGameWindow) like SK's game_window.hWnd.
 */

#include "window_proc_hooks.hpp"
#include <atomic>
#include <thread>
#include <unordered_map>
#include "../../exit_handler.hpp"
#include "../../globals.hpp"
#include "../../settings/advanced_tab_settings.hpp"
#include "../../ui/new_ui/window_info_tab.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/srwlock_registry.hpp"
#include "../../utils/srwlock_wrapper.hpp"
#include "../../utils/timing.hpp"
#include "api_hooks.hpp"  // For GetGameWindow
#include "../nvidia/pclstats_etw_hooks.hpp"

#include "../../../../../external/Streamline/source/plugins/sl.pcl/pclstats.h"

namespace display_commanderhooks {

// Global variables for hook state
static std::atomic<bool> g_sent_activate{false};

// HWND -> original WNDPROC. Store original *before* swapping to detour (avoids race with DefWindowProc).
static std::unordered_map<HWND, WNDPROC> g_original_wndproc;
static std::atomic<bool> g_wndproc_lock_initialized{false};

// Message rate detection: log when messages/sec exceed static thresholds (logarithmic steps).
static constexpr uint64_t kMessageRateWindowNs = 1'000'000'000;  // 1 second
static constexpr uint32_t kMessageRateThresholds[] = {128,   256,   512,    1024,   2048,   4096,    8192,    16384,
                                                      32768, 65536, 131072, 262144, 524288, 1048576, 2097152, 4194304};
static constexpr size_t kMessageRateThresholdCount = sizeof(kMessageRateThresholds) / sizeof(kMessageRateThresholds[0]);
static constexpr uint32_t kMessageRatePrintNextCountInitial =
    128;  // upon reaching limit, print this many; doubled each time reached
static constexpr uint32_t kMessageRatePrintNextCountMax = 65536u;
static std::atomic<uint64_t> g_message_rate_period_start_ns{0};
static std::atomic<uint32_t> g_message_rate_count{0};
static std::atomic<uint32_t> g_message_rate_last_logged_threshold{0};
static std::atomic<uint32_t> g_message_rate_print_remaining{0};
static std::atomic<uint32_t> g_message_rate_next_print_count{kMessageRatePrintNextCountInitial};

// Returns literal name for known WM_* (no allocation); nullptr for unknown.
static const char* GetWindowMessageNameForLog(UINT uMsg) {
    switch (uMsg) {
        case WM_NULL:              return "WM_NULL";
        case WM_CREATE:            return "WM_CREATE";
        case WM_DESTROY:           return "WM_DESTROY";
        case WM_MOVE:              return "WM_MOVE";
        case WM_SIZE:              return "WM_SIZE";
        case WM_ACTIVATE:          return "WM_ACTIVATE";
        case WM_SETFOCUS:          return "WM_SETFOCUS";
        case WM_KILLFOCUS:         return "WM_KILLFOCUS";
        case WM_PAINT:             return "WM_PAINT";
        case WM_CLOSE:             return "WM_CLOSE";
        case WM_QUIT:              return "WM_QUIT";
        case WM_ERASEBKGND:        return "WM_ERASEBKGND";
        case WM_SHOWWINDOW:        return "WM_SHOWWINDOW";
        case WM_ACTIVATEAPP:       return "WM_ACTIVATEAPP";
        case WM_NCACTIVATE:        return "WM_NCACTIVATE";
        case WM_GETTEXT:           return "WM_GETTEXT";
        case WM_GETTEXTLENGTH:     return "WM_GETTEXTLENGTH";
        case WM_TIMER:             return "WM_TIMER";
        case WM_HSCROLL:           return "WM_HSCROLL";
        case WM_VSCROLL:           return "WM_VSCROLL";
        case WM_INITMENU:          return "WM_INITMENU";
        case WM_COMMAND:           return "WM_COMMAND";
        case WM_SYSCOMMAND:        return "WM_SYSCOMMAND";
        case WM_WINDOWPOSCHANGING: return "WM_WINDOWPOSCHANGING";
        case WM_WINDOWPOSCHANGED:  return "WM_WINDOWPOSCHANGED";
        case WM_ENTERSIZEMOVE:     return "WM_ENTERSIZEMOVE";
        case WM_EXITSIZEMOVE:      return "WM_EXITSIZEMOVE";
        case WM_MOUSEACTIVATE:     return "WM_MOUSEACTIVATE";
        case WM_GETMINMAXINFO:     return "WM_GETMINMAXINFO";
        case WM_NCCREATE:          return "WM_NCCREATE";
        case WM_NCDESTROY:         return "WM_NCDESTROY";
        case WM_NCHITTEST:         return "WM_NCHITTEST";
        case WM_NCPAINT:           return "WM_NCPAINT";
        case WM_KEYDOWN:           return "WM_KEYDOWN";
        case WM_KEYUP:             return "WM_KEYUP";
        case WM_CHAR:              return "WM_CHAR";
        case WM_LBUTTONDOWN:       return "WM_LBUTTONDOWN";
        case WM_LBUTTONUP:         return "WM_LBUTTONUP";
        case WM_RBUTTONDOWN:       return "WM_RBUTTONDOWN";
        case WM_RBUTTONUP:         return "WM_RBUTTONUP";
        case WM_MOUSEMOVE:         return "WM_MOUSEMOVE";
        case WM_MOUSEWHEEL:        return "WM_MOUSEWHEEL";
        case WM_APP:               return "WM_APP";
        default:                   return nullptr;
    }
}

static void CheckMessageRateAndLogIfHigh(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // If we're in "print next N messages" mode, log this message and decrement.
    uint32_t remaining = g_message_rate_print_remaining.load(std::memory_order_acquire);
    if (remaining > 0) {
        const char* msg_name = GetWindowMessageNameForLog(uMsg);
        if (msg_name != nullptr) {
            LogErrorThrottled(40, "Window message [rate dump]: hwnd=0x%p uMsg=%s(0x%u) wParam=0x%llx lParam=0x%llx",
                              static_cast<void*>(hwnd), msg_name, static_cast<unsigned>(uMsg),
                              static_cast<unsigned long long>(wParam), static_cast<unsigned long long>(lParam));
        } else {
            LogErrorThrottled(40, "Window message [rate dump]: hwnd=0x%p uMsg=0x%u wParam=0x%llx lParam=0x%llx",
                              static_cast<void*>(hwnd), static_cast<unsigned>(uMsg),
                              static_cast<unsigned long long>(wParam), static_cast<unsigned long long>(lParam));
        }
        g_message_rate_print_remaining.store(remaining - 1, std::memory_order_release);
    }

    const uint64_t now = utils::get_real_time_ns();
    uint64_t period_start = g_message_rate_period_start_ns.load(std::memory_order_acquire);
    if (now - period_start >= kMessageRateWindowNs) {
        g_message_rate_period_start_ns.store(now, std::memory_order_release);
        g_message_rate_count.store(0, std::memory_order_release);
        g_message_rate_last_logged_threshold.store(0, std::memory_order_release);
        g_message_rate_print_remaining.store(0, std::memory_order_release);
        period_start = now;
    }
    const uint32_t c = g_message_rate_count.fetch_add(1, std::memory_order_relaxed) + 1;
    static size_t max_threshold_reached = -1;
    for (size_t i = 0; i < kMessageRateThresholdCount; ++i) {
        const uint32_t threshold = kMessageRateThresholds[i];
        if (c < threshold) {
            break;
        }
        uint32_t last = g_message_rate_last_logged_threshold.load(std::memory_order_acquire);
        if (last < threshold) {
            if (g_message_rate_last_logged_threshold.compare_exchange_strong(last, threshold, std::memory_order_release,
                                                                             std::memory_order_acquire)) {
                LogErrorThrottled(40, "Window message rate very high: %u messages in the last second (threshold %u)", c,
                                  threshold);
                if (threshold == kMessageRateThresholds[0]) {
                    uint32_t to_print = g_message_rate_next_print_count.load(std::memory_order_acquire);
                    g_message_rate_print_remaining.store(to_print, std::memory_order_release);
                    uint32_t next = (to_print >= kMessageRatePrintNextCountMax) ? to_print : to_print * 2;
                    g_message_rate_next_print_count.store(next, std::memory_order_release);
                }
            }
        }
    }
}

// Trampoline (SK-style): 1) ProcessWindowMessage; 2) if not skipped, call original WNDPROC.
static LRESULT CALLBACK WindowProc_Detour(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // CheckMessageRateAndLogIfHigh(hwnd, uMsg, wParam, lParam);
    g_last_window_message_processed_ns.store(utils::get_real_time_ns(), std::memory_order_release);
    if (ProcessWindowMessage(hwnd, uMsg, wParam, lParam)) {
        return 0;  // Message suppressed (skipped)
    }
    WNDPROC orig = nullptr;
    {
        utils::SRWLockShared guard(utils::g_wndproc_map_lock);
        auto it = g_original_wndproc.find(hwnd);
        if (it != g_original_wndproc.end()) {
            orig = it->second;
        }
    }
    if (orig == nullptr) {
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    LRESULT ret = IsWindowUnicode(hwnd) ? CallWindowProcW(orig, hwnd, uMsg, wParam, lParam)
                                        : CallWindowProcA(orig, hwnd, uMsg, wParam, lParam);
    if (uMsg == WM_DESTROY) {
        utils::SRWLockExclusive guard(utils::g_wndproc_map_lock);
        g_original_wndproc.erase(hwnd);
    }
    return ret;
}

// True if window has caption or thick frame (standard bordered window). Borderless windows return false.
bool WindowHasBorder(HWND hwnd) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return false;
    }
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    return (style & (WS_CAPTION | WS_THICKFRAME)) != 0;
}

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

// Count top-level windows belonging to current process, excluding exclude_hwnd and the standalone UI window.
// Used to avoid calling OnHandleExit when one of several game windows closes.
struct CountOtherWindowsData {
    DWORD pid;
    HWND exclude;
    HWND standalone;
    int count;
};
static BOOL CALLBACK EnumCountOtherProcessWindows(HWND hwnd, LPARAM lParam) {
    CountOtherWindowsData* d = reinterpret_cast<CountOtherWindowsData*>(lParam);
    if (hwnd == d->exclude || hwnd == d->standalone) {
        return TRUE;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == d->pid && IsWindow(hwnd)) {
        d->count++;
    }
    return TRUE;
}
static int CountOtherProcessWindows(HWND exclude_hwnd) {
    CountOtherWindowsData data = {GetCurrentProcessId(), exclude_hwnd,
                                  g_standalone_ui_hwnd.load(std::memory_order_acquire), 0};
    EnumWindows(EnumCountOtherProcessWindows, reinterpret_cast<LPARAM>(&data));
    return data.count;
}

// Process window message - returns true if message should be suppressed
// This function contains the logic previously in WindowProc_Detour
bool ProcessWindowMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (hwnd == g_standalone_ui_hwnd.load(std::memory_order_acquire)) {
        return false;
    }

    // Check if continue rendering is enabled
    // Special-K style: set ping signal when ping message is received, inject marker on next SIMULATION_START
    if (PCLSTATS_IS_PING_MSG_ID(uMsg)) {
        g_pclstats_ping_signal.store(true, std::memory_order_release);

        // We assume frame id is equal to frame before simulation_end
        RecordPCLStatsMarkerCall();
        PCLSTATS_MARKER(PC_LATENCY_PING, g_pclstats_frame_id.load());
    }

    bool continue_rendering_enabled = settings::g_advancedTabSettings.continue_rendering.GetValue();

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
                    DetourWindowMessageNonBlocking(hwnd, WM_SETFOCUS, 0, 0);
                }
            }
            break;

        case WM_NCACTIVATE:
            // Handle non-client area activation
            if (continue_rendering_enabled) {
                if (wParam != FALSE) {
                    // Non-client area is being activated - ensure window stays active
                    LogInfoThrottled(10, "WM_NCACTIVATE: Window activated - ensuring continued rendering - HWND: 0x%p",
                                     hwnd);
                    // Send fake focus message to maintain active state
                    DetourWindowMessageNonBlocking(hwnd, WM_SETFOCUS, 0, 0);
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
            bool prevent_minimize_enabled = settings::g_advancedTabSettings.prevent_minimize.GetValue();
            if (continue_rendering_enabled || prevent_minimize_enabled) {
                WINDOWPOS* pWp = reinterpret_cast<WINDOWPOS*>(lParam);
                if (pWp != nullptr && (pWp->flags & SWP_SHOWWINDOW)) {
                    // Check if window is being minimized
                    if (IsIconic_direct(hwnd)) {
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
                // Check if window is being minimized or hidden (guard pWp to avoid crash on null lParam)
                if (pWp != nullptr && (pWp->flags & SWP_HIDEWINDOW)) {
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

        case WM_SYSCOMMAND: {
            // Handle system commands
            bool prevent_minimize_enabled = settings::g_advancedTabSettings.prevent_minimize.GetValue();
            if (continue_rendering_enabled || prevent_minimize_enabled) {
                // Prevent minimization when continue rendering or prevent minimize is enabled
                if (wParam == SC_MINIMIZE) {
                    LogInfo("WM_SYSCOMMAND: Suppressing minimize command - HWND: 0x%p", hwnd);
                    // Update the message history to show this was suppressed
                    ui::new_ui::AddMessageToHistoryIfKnown(uMsg, wParam, lParam, true);
                    return true;  // Suppress the message
                }
            }
            break;
        }

        case WM_SIZE:
            // When continue rendering is on, prevent the game from seeing WM_SIZE SIZE_MINIMIZED so it doesn't know
            // it's minimized. Allow SIZE_RESTORED through so the game can react when the user restores the window.
            if (continue_rendering_enabled && game_window == hwnd && wParam == SIZE_MINIMIZED) {
                LogInfo("WM_SIZE: Suppressing SIZE_MINIMIZED message due to continue rendering - HWND: 0x%p", hwnd);
                ui::new_ui::AddMessageToHistoryIfKnown(uMsg, wParam, lParam, true);
                return true;  // Suppress the message
            }
            if (wParam == SIZE_MINIMIZED) {
                LogInfo("WM_SIZE SIZE_MINIMIZED received by game (not suppressed) - HWND: 0x%p", hwnd);
            } else if (wParam == SIZE_RESTORED) {
                LogInfo("WM_SIZE SIZE_RESTORED received by game (not suppressed) - HWND: 0x%p", hwnd);
            }
            break;

        case WM_QUIT:
            // Only trigger exit when no other game windows exist (e.g. multi-window games closing one window)
            if (CountOtherProcessWindows(hwnd) == 0) {
                if (g_no_exit_mode.load(std::memory_order_acquire)) {
                    LogInfo("WM_QUIT: .NO_EXIT active - blocking quit HWND: 0x%p; opening independent UI.", hwnd);
                    RequestShowIndependentWindow();
                    ui::new_ui::AddMessageToHistoryIfKnown(uMsg, wParam, lParam, true);
                    return true;  // Suppress message so window does not close
                }
                LogInfo("WM_QUIT: Window quit message received - HWND: 0x%p (last window)", hwnd);
                exit_handler::OnHandleExit(exit_handler::ExitSource::WINDOW_QUIT, "WM_QUIT message received");
            } else {
                LogInfo("WM_QUIT: Window quit message received - HWND: 0x%p (other windows still open, not exiting)",
                        hwnd);
            }
            break;

        case WM_CLOSE:
            if (CountOtherProcessWindows(hwnd) == 0) {
                if (g_no_exit_mode.load(std::memory_order_acquire)) {
                    LogInfo("WM_CLOSE: .NO_EXIT active - blocking close HWND: 0x%p; opening independent UI.", hwnd);
                    RequestShowIndependentWindow();
                    ui::new_ui::AddMessageToHistoryIfKnown(uMsg, wParam, lParam, true);
                    return true;  // Suppress message so window does not close
                }
                LogInfo("WM_CLOSE: Window close message received - HWND: 0x%p (last window)", hwnd);
                exit_handler::OnHandleExit(exit_handler::ExitSource::WINDOW_CLOSE, "WM_CLOSE message received");
            } else {
                LogInfo("WM_CLOSE: Window close - HWND: 0x%p (other windows still open, not exiting)", hwnd);
            }
            break;

        case WM_DESTROY:
            if (CountOtherProcessWindows(hwnd) == 0) {
                if (g_no_exit_mode.load(std::memory_order_acquire)) {
                    LogInfo("WM_DESTROY: .NO_EXIT active - blocking destroy HWND: 0x%p; opening independent UI.", hwnd);
                    RequestShowIndependentWindow();
                    ui::new_ui::AddMessageToHistoryIfKnown(uMsg, wParam, lParam, true);
                    return true;  // Suppress message so window does not close
                }
                LogInfo("WM_DESTROY: Window destroy message received - HWND: 0x%p (last window)", hwnd);
                exit_handler::OnHandleExit(exit_handler::ExitSource::WINDOW_DESTROY, "WM_DESTROY message received");
            } else {
                LogInfo("WM_DESTROY: Window destroy - HWND: 0x%p (other windows still open, not exiting)", hwnd);
            }
            break;

        default: break;
    }

    // Track message as not suppressed
    ui::new_ui::AddMessageToHistoryIfKnown(uMsg, wParam, lParam, false);

    return false;  // Don't suppress the message
}

// Install WNDPROC hook on target_hwnd (SK_InstallWindowHook style). Sets game window (atomic).
bool InstallWindowProcHooks(HWND target_hwnd) {
    static bool installed_hooks = false;
    if (installed_hooks) {
        return true;
    }
    if (target_hwnd == nullptr || !IsWindow(target_hwnd)) {
        return false;
    }
    if (!IsWindowFromCurrentProcess(target_hwnd)) {
        return false;
    }

    g_wndproc_lock_initialized.store(true);

    WNDPROC current = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(target_hwnd, GWLP_WNDPROC));
    if (current == WindowProc_Detour) {
        SetGameWindow(target_hwnd);  // game_window (atomic) = this HWND
        g_sent_activate.store(false);
        return true;  // Already hooked this window
    }

    // Store original *before* swapping to detour (per memory: avoid early-message race)
    {
        utils::SRWLockExclusive guard(utils::g_wndproc_map_lock);
        g_original_wndproc[target_hwnd] = current;
    }
    SetWindowLongPtrW(target_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowProc_Detour));

    SetGameWindow(target_hwnd);  // game_window (atomic), like SK game_window.hWnd
    g_sent_activate.store(false);
    LogInfo("Window procedure hook installed for HWND: 0x%p", target_hwnd);
    installed_hooks = true;
    return true;
    // Window proc hooks are now handled via message retrieval hooks
    // Just reset the activation flag
}

void UninstallWindowProcHooks() {
    g_sent_activate.store(false);
    if (!g_wndproc_lock_initialized.load()) {
        return;
    }
    utils::SRWLockExclusive guard(utils::g_wndproc_map_lock);
    for (const auto& kv : g_original_wndproc) {
        HWND hwnd = kv.first;
        WNDPROC orig = kv.second;
        if (IsWindow(hwnd)) {
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
        }
    }
    g_original_wndproc.clear();
}

bool IsContinueRenderingEnabled() { return settings::g_advancedTabSettings.continue_rendering.GetValue(); }

// Fake activation functions
void SendFakeActivationMessages(HWND hwnd) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return;
    }
    static uint64_t last_called_ns = 0;
    const uint64_t now = utils::get_real_time_ns();
    if (last_called_ns != 0 && (now - last_called_ns) < 100 * utils::NS_TO_MS) {
        return;
    }
    last_called_ns = now;

    // Send fake activation messages to keep the game thinking it's active
    // Based on Special-K's patterns for better compatibility
    std::thread([hwnd]() {
        if (IsWindow(hwnd)) {
            SendMessage(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
            SendMessage(hwnd, WM_SETFOCUS, 0, 0);
            SendMessage(hwnd, WM_ACTIVATEAPP, TRUE, 0);
            SendMessage(hwnd, WM_NCACTIVATE, TRUE, 0);
        }
    }).detach();

    LogInfo("Sent fake activation messages to window - HWND: 0x%p", hwnd);
}

// Last time we dispatched a non-blocking detour (ns). Used to skip calls within 100ms.
static std::atomic<uint64_t> g_last_detour_dispatch_ns{0};

static constexpr uint64_t kDetourThrottleMs = 100;
static constexpr uint64_t kDetourThrottleNs = kDetourThrottleMs * 1000 * 1000;

void DetourWindowMessageNonBlocking(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return;
    }

    static uint64_t last_called_ns = 0;
    const uint64_t now = utils::get_real_time_ns();
    if (last_called_ns != 0 && (now - last_called_ns) < 100 * utils::NS_TO_MS) {
        return;
    }
    last_called_ns = now;

    std::thread([hwnd, uMsg, wParam, lParam]() {
        if (IsWindow(hwnd)) {
            SendMessage(hwnd, uMsg, wParam, lParam);
        }
    }).detach();
}

}  // namespace display_commanderhooks
