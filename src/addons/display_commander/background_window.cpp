#include "background_window.hpp"
#include "globals.hpp"
#include "utils.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <sstream>


// IMPORTANT ARCHITECTURAL PRINCIPLE:
// Windows must be created in the same thread that processes their messages.
// This prevents cross-thread ownership issues and ensures proper message routing.
// The background window is created inside m_background_thread, not in the main thread.

// Global instance is defined in globals.cpp
extern BackgroundWindowManager g_backgroundWindowManager;

BackgroundWindowManager::BackgroundWindowManager()
    : m_background_hwnd(nullptr), m_has_background_window(false), m_frame_counter(0)
// , m_ignore_focus_events(true) { // Start with focus events ignored
{}

BackgroundWindowManager::~BackgroundWindowManager() { DestroyBackgroundWindow(); }

bool BackgroundWindowManager::RegisterWindowClass() {
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = DefWindowProc; // Use default window procedure
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = BACKGROUND_WINDOW_CLASS;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.style = CS_HREDRAW | CS_VREDRAW;

    ATOM result = RegisterClassExA(&wc);
    if (result != 0) {
        LogInfo("Custom background window class registered successfully");
        return true;
    }

    // FALLBACK: Use existing Windows class if custom registration fails
    DWORD error = GetLastError();
    std::ostringstream oss;
    oss << "Custom window class registration failed with error: " << error << " - Using fallback approach";
    LogInfo(oss.str().c_str());

    // Don't return false - we'll use fallback in CreateBackgroundWindow
    return false;
}

void BackgroundWindowManager::UnregisterWindowClass() {
    UnregisterClassA(BACKGROUND_WINDOW_CLASS, GetModuleHandle(nullptr));
}

bool BackgroundWindowManager::CreateBackgroundWindow(HWND game_hwnd) {
    if (m_background_hwnd != nullptr) {
        return true; // Already exists
    }

    // Start the dedicated message processing thread (window will be created inside the thread)
    StartBackgroundThread(game_hwnd);

    // Enable focus events after thread is started
    //  m_ignore_focus_events.store(false);

    m_has_background_window.store(true);
    LogInfo("Background window thread started successfully");

    return true;
}

bool BackgroundWindowManager::CreateBackgroundWindowInThread(HWND game_hwnd) {
    if (m_background_hwnd != nullptr) {
        return true; // Already exists
    }

    // Try custom class first, fallback to existing Windows class
    bool custom_class_registered = RegisterWindowClass();
    const char *window_class_to_use;

    if (custom_class_registered) {
        window_class_to_use = BACKGROUND_WINDOW_CLASS;
        LogInfo("Using custom background window class");
    } else {
        // FALLBACK: Use existing Windows class that we know works
        window_class_to_use = "Static"; // Windows built-in static control class
        LogInfo("Using fallback Windows class: Static");
    }

    // Get monitor info for the game window
    HMONITOR monitor = MonitorFromWindow(game_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info = {sizeof(MONITORINFO)};
    if (!GetMonitorInfo(monitor, &monitor_info)) {
        LogInfo("Failed to get monitor info for background window");
        return false;
    }

    // Create background window covering the entire monitor
    m_background_hwnd = CreateWindowExA(WS_EX_LAYERED | WS_EX_TOOLWINDOW,                           // Extended styles
                                        window_class_to_use,                                        // Window class
                                        "DISPLAY COMMANDER BACKGROUND WINDOW",                                 // Window title
                                        WS_POPUP | WS_VISIBLE,                                      // Window style
                                        monitor_info.rcMonitor.left,                                // X position
                                        monitor_info.rcMonitor.top,                                 // Y position
                                        monitor_info.rcMonitor.right - monitor_info.rcMonitor.left, // Width
                                        monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top, // Height
                                        nullptr,                                                    // Parent window
                                        nullptr,                                                    // Menu
                                        GetModuleHandle(nullptr),                                   // Instance
                                        nullptr                                                     // Additional data
    );

    if (m_background_hwnd == nullptr) {
        LogInfo("Failed to create background window");
        return false;
    }

    // Set window transparency
    SetLayeredWindowAttributes(m_background_hwnd, RGB(255, 0, 255), 255, LWA_COLORKEY | LWA_ALPHA);

    // Ensure the background window cannot receive focus or input
    SetWindowLongPtr(m_background_hwnd, GWL_EXSTYLE,
                     GetWindowLongPtr(m_background_hwnd, GWL_EXSTYLE) | WS_EX_NOACTIVATE);

    // Switch focus back to the game window after creating background window
    SetForegroundWindow(game_hwnd);
    SetActiveWindow(game_hwnd);
    SetFocus(game_hwnd);
    LogInfo("[BG-WINDOW-THREAD] Background window created, focus switched back to game window");

    return true;
}

void BackgroundWindowManager::StartBackgroundThread(HWND game_hwnd) {
    // Stop existing thread if any
    if (m_background_thread.joinable()) {
        m_background_thread.join();
    }

    // Start dedicated message processing thread
    m_background_thread = std::thread([this, game_hwnd]() {
        LogInfo("[BG-WINDOW-THREAD] Background window message thread started");

        // CREATE WINDOW IN THIS THREAD (fixes cross-thread ownership issue)
        if (!CreateBackgroundWindowInThread(game_hwnd)) {
            LogInfo("[BG-WINDOW-THREAD] Failed to create background window in thread");
            return;
        }

        // Set a timer for color cycling
        SetTimer(m_background_hwnd, 1, 100, nullptr);

        // Message processing loop
        MSG msg;
        while (m_has_background_window && IsWindow(m_background_hwnd)) {
            // Get and process messages
            if (GetMessage(&msg, nullptr, 0, 0)) {
                // Handle specific messages
                switch (msg.message) {
                case WM_PAINT: {
                    // Handle painting with different colors inside/outside game rectangle
                    PAINTSTRUCT ps;
                    HDC hdc = BeginPaint(m_background_hwnd, &ps);

                    if (hdc) {
                        // Get game window rectangle
                        RECT game_rect;
                        if (GetWindowRect(game_hwnd, &game_rect)) {
                            // Fill outside game rectangle with black
                            RECT outside_rects[4];

                            // Top rectangle (above game)
                            outside_rects[0] = {ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right, game_rect.top};

                            // Bottom rectangle (below game)
                            outside_rects[1] = {ps.rcPaint.left, game_rect.bottom, ps.rcPaint.right, ps.rcPaint.bottom};

                            // Left rectangle (left of game)
                            outside_rects[2] = {ps.rcPaint.left, ps.rcPaint.top, game_rect.left, ps.rcPaint.bottom};

                            // Right rectangle (right of game)
                            outside_rects[3] = {game_rect.right, ps.rcPaint.top, ps.rcPaint.right, ps.rcPaint.bottom};

                            // Paint outside areas with black
                            HBRUSH black_brush = CreateSolidBrush(COLORS[0]); // Black
                            if (black_brush) {
                                for (int i = 0; i < 4; i++) {
                                    if (outside_rects[i].right > outside_rects[i].left &&
                                        outside_rects[i].bottom > outside_rects[i].top) {
                                        FillRect(hdc, &outside_rects[i], black_brush);
                                    }
                                }
                                DeleteObject(black_brush);
                            }

                            // Fill inside game rectangle with transparent color (magenta)
                            RECT inside_rect;
                            inside_rect.left = (std::max)(ps.rcPaint.left, game_rect.left);
                            inside_rect.top = (std::max)(ps.rcPaint.top, game_rect.top);
                            inside_rect.right = (std::min)(ps.rcPaint.right, game_rect.right);
                            inside_rect.bottom = (std::min)(ps.rcPaint.bottom, game_rect.bottom);

                            if (inside_rect.right > inside_rect.left && inside_rect.bottom > inside_rect.top) {
                                HBRUSH magenta_brush = CreateSolidBrush(COLORS[1]); // Magenta (transparent)
                                if (magenta_brush) {
                                    FillRect(hdc, &inside_rect, magenta_brush);
                                    DeleteObject(magenta_brush);
                                }
                            }
                        } else {
                            // Fallback: fill entire area with black if we can't get game rect
                            HBRUSH black_brush = CreateSolidBrush(COLORS[0]);
                            if (black_brush) {
                                FillRect(hdc, &ps.rcPaint, black_brush);
                                DeleteObject(black_brush);
                            }
                        }
                    }

                    EndPaint(m_background_hwnd, &ps);
                    break;
                }

                case WM_TIMER: {
                    // Handle timer for repainting (no color cycling needed)
                    if (msg.wParam == 1) { // Our timer
                        // Just trigger a repaint to keep window responsive
                        InvalidateRect(m_background_hwnd, nullptr, FALSE);
                        UpdateWindow(m_background_hwnd);
                    }
                    break;
                }

                case WM_SETCURSOR: {
                    // Show "not allowed" cursor
                    SetCursor(LoadCursor(nullptr, IDC_NO));
                    break;
                }

                    // Removed click and focus handlers - window should keep running

                default:
                    // Let Windows handle other messages
                    DefWindowProc(m_background_hwnd, msg.message, msg.wParam, msg.lParam);
                    break;
                }
            } else {
                // WM_QUIT received, exit the loop
                break;
            }
        }

        // Cleanup
        KillTimer(m_background_hwnd, 1);

        LogInfo("[BG-WINDOW-THREAD] Background window message thread exiting");
    });
}

void BackgroundWindowManager::UpdateBackgroundWindowPosition(HWND game_hwnd) {
    if (m_background_hwnd == nullptr) {
        return; // Window not created yet
    }

    // Get monitor info for the game window
    HMONITOR monitor = MonitorFromWindow(game_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info = {sizeof(MONITORINFO)};
    if (!GetMonitorInfo(monitor, &monitor_info)) {
        return;
    }

    // Update background window to cover entire monitor, but ensure it stays behind the game window
    SetWindowPos(m_background_hwnd,
                 game_hwnd, // Place behind the game window specifically
                 monitor_info.rcMonitor.left, monitor_info.rcMonitor.top,
                 monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
                 monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top, SWP_NOACTIVATE);
}

void BackgroundWindowManager::UpdateBackgroundWindow(HWND game_hwnd) {
    if (game_hwnd == nullptr) {
        return;
    }

    if (!m_has_background_window) {
        // Create the background window
        CreateBackgroundWindow(game_hwnd);
        return;
    }

    // Update position of existing window
    UpdateBackgroundWindowPosition(game_hwnd);
}

void BackgroundWindowManager::DestroyBackgroundWindow() {
    /*  if (m_background_hwnd != nullptr) {
        // Stop the background thread
        m_has_background_window.store(false);

        // Wait for thread to finish
        if (m_background_thread.joinable()) {
            m_background_thread.join();
        }

        // Destroy the window
        DestroyWindow(m_background_hwnd);
        m_background_hwnd = nullptr;
        LogInfo("Background window destroyed");
    }*/
}

bool BackgroundWindowManager::HasBackgroundWindow() const { return m_has_background_window.load(); }

HWND BackgroundWindowManager::GetBackgroundWindow() const { return m_background_hwnd; }
