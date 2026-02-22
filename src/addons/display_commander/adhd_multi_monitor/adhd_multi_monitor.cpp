#include "adhd_multi_monitor.hpp"
#include "../globals.hpp"
#include "../utils/logging.hpp"
#include <algorithm>
#include <dwmapi.h>

// Undefine Windows min/max macros to avoid conflicts with std::min/std::max
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace adhd_multi_monitor {

// Global instance
AdhdMultiMonitorManager g_adhdManager;

AdhdMultiMonitorManager::AdhdMultiMonitorManager()
    : enabled_(false), background_hwnd_(nullptr), last_game_in_foreground_(false), initialized_(false),
      background_window_created_(false) {
    game_monitor_rect_ = {0, 0, 0, 0};
}

AdhdMultiMonitorManager::~AdhdMultiMonitorManager() { Shutdown(); }

bool AdhdMultiMonitorManager::Initialize() {
    if (initialized_)
        return true;

    // Check if we have a valid game window handle
    HWND game_hwnd = g_last_swapchain_hwnd.load();
    if (game_hwnd == nullptr || !IsWindow(game_hwnd))
        return false;

    // Enumerate available monitors
    EnumerateMonitors();

    if (monitor_rects_.size() <= 1)
        return false; // No need for ADHD mode with single monitor

    // Register the background window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = BackgroundWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = BACKGROUND_WINDOW_CLASS;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    if (!RegisterClassExW(&wc)) {
        LogError("Failed to register ADHD background window class");
        return false;
    }

    initialized_ = true;
    return true;
}

void AdhdMultiMonitorManager::Shutdown() {
    if (!initialized_)
        return;

    DestroyBackgroundWindow();

    // Unregister the window class
    UnregisterClassW(BACKGROUND_WINDOW_CLASS, GetModuleHandle(nullptr));

    initialized_ = false;
}

void AdhdMultiMonitorManager::Update() {
    if (!enabled_ || !initialized_)
        return;

    // Update monitor info if game window changed or moved to another display
    HWND current_hwnd = g_last_swapchain_hwnd.load();
    if (!current_hwnd) {
        return;
    }
    static HWND last_hwnd = nullptr;
    if (current_hwnd != last_hwnd) {
        last_hwnd = current_hwnd;
        UpdateMonitorInfo();
    } else if (background_window_created_) {
        // Game window handle unchanged - check if it moved to another monitor
        HMONITOR current_monitor = MonitorFromWindow(current_hwnd, MONITOR_DEFAULTTONEAREST);
        if (current_monitor) {
            MONITORINFO mi = {};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(current_monitor, &mi)
                && (mi.rcMonitor.left != game_monitor_rect_.left || mi.rcMonitor.top != game_monitor_rect_.top
                    || mi.rcMonitor.right != game_monitor_rect_.right
                    || mi.rcMonitor.bottom != game_monitor_rect_.bottom)) {
                UpdateMonitorInfo();
                PositionBackgroundWindow();
            }
        }
    }

    // Only show background window when game is in foreground (same logic as g_app_in_background)
    bool game_in_foreground = !IsAppInBackground();
    if (game_in_foreground != last_game_in_foreground_) {
        last_game_in_foreground_ = game_in_foreground;

        if (background_window_created_) {
            ShowBackgroundWindow(game_in_foreground);

            if (game_in_foreground) {
                PositionBackgroundWindow();
            }
        }
    }
}

void AdhdMultiMonitorManager::SetEnabled(bool enabled) {
    HWND game_hwnd = g_last_swapchain_hwnd.load();
    if (!game_hwnd) {
        return;
    }
    if (enabled_ == enabled)
        return;

    enabled_ = enabled;

    if (enabled) {
        if (!background_window_created_) {
            CreateBackgroundWindow();
        }
        UpdateMonitorInfo();
        PositionBackgroundWindow();
        last_game_in_foreground_ = !IsAppInBackground();
        ShowBackgroundWindow(last_game_in_foreground_);
    } else {
        last_game_in_foreground_ = false;
        ShowBackgroundWindow(false);
    }
}

bool AdhdMultiMonitorManager::HasMultipleMonitors() const { return monitor_rects_.size() > 1; }

bool AdhdMultiMonitorManager::CreateBackgroundWindow() {
    if (background_window_created_)
        return true;

    HWND game_hwnd = g_last_swapchain_hwnd.load();
    if (!game_hwnd)
        return false;

    // Create the background window
    background_hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, BACKGROUND_WINDOW_CLASS,
                                       BACKGROUND_WINDOW_TITLE, WS_POPUP, 0, 0, 1, 1, // Will be repositioned
                                       nullptr, nullptr, GetModuleHandle(nullptr), this);

    if (!background_hwnd_) {
        LogError("Failed to create ADHD background window");
        return false;
    }

    // Set the window to be transparent but visible
    SetLayeredWindowAttributes(background_hwnd_, 0, 255, LWA_ALPHA);

    // Make it click-through
    SetWindowLongPtrW(background_hwnd_, GWL_EXSTYLE,
                      GetWindowLongPtrW(background_hwnd_, GWL_EXSTYLE) | WS_EX_TRANSPARENT);

    background_window_created_ = true;
    return true;
}

void AdhdMultiMonitorManager::DestroyBackgroundWindow() {
    if (background_hwnd_) {
        DestroyWindow(background_hwnd_);
        background_hwnd_ = nullptr;
    }
    background_window_created_ = false;
}

void AdhdMultiMonitorManager::PositionBackgroundWindow() {
    if (!background_window_created_ || !enabled_)
        return;

    // Get the game monitor
    HWND game_hwnd = g_last_swapchain_hwnd.load();
    if (!game_hwnd)
        return;
    HMONITOR gameMonitor = MonitorFromWindow(game_hwnd, MONITOR_DEFAULTTONEAREST);
    if (!gameMonitor)
        return;

    // Find the game monitor rect
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(gameMonitor, &mi))
        return;

    game_monitor_rect_ = mi.rcMonitor;

    // Calculate the bounding rectangle of all monitors except the game monitor
    RECT boundingRect = {LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN};
    bool hasOtherMonitors = false;

    for (const auto &monitorRect : monitor_rects_) {
        // Skip the game monitor
        if (EqualRect(&monitorRect, &game_monitor_rect_))
            continue;

        hasOtherMonitors = true;
        boundingRect.left = std::min(boundingRect.left, monitorRect.left);
        boundingRect.top = std::min(boundingRect.top, monitorRect.top);
        boundingRect.right = std::max(boundingRect.right, monitorRect.right);
        boundingRect.bottom = std::max(boundingRect.bottom, monitorRect.bottom);
    }

    if (!hasOtherMonitors) {
        ShowBackgroundWindow(false);
        return;
    }

    // Position the background window to cover all monitors except the game monitor
    int width = boundingRect.right - boundingRect.left;
    int height = boundingRect.bottom - boundingRect.top;

    UINT swp_flags = SWP_NOACTIVATE;
    if (!IsAppInBackground()) {
        swp_flags |= SWP_SHOWWINDOW;
    }
    SetWindowPos(background_hwnd_, HWND_TOPMOST, boundingRect.left, boundingRect.top, width, height, swp_flags);
}

void AdhdMultiMonitorManager::ShowBackgroundWindow(bool show) {
    if (!background_window_created_)
        return;

    ShowWindow(background_hwnd_, show ? SW_SHOW : SW_HIDE);
}

void AdhdMultiMonitorManager::EnumerateMonitors() {
    monitor_rects_.clear();

    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR hMonitor, HDC hdc, LPRECT lprcMonitor, LPARAM lParam) -> BOOL {
            auto *manager = reinterpret_cast<AdhdMultiMonitorManager *>(lParam);
            manager->monitor_rects_.push_back(*lprcMonitor);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(this));
}

void AdhdMultiMonitorManager::UpdateMonitorInfo() {
    EnumerateMonitors();

    // Update game monitor info
    HWND game_hwnd = g_last_swapchain_hwnd.load();
    if (!game_hwnd) {
        return;
    }
    HMONITOR gameMonitor = MonitorFromWindow(game_hwnd, MONITOR_DEFAULTTONEAREST);
    if (gameMonitor) {
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(gameMonitor, &mi)) {
            game_monitor_rect_ = mi.rcMonitor;
        }
    }
}

LRESULT CALLBACK AdhdMultiMonitorManager::BackgroundWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        CREATESTRUCT *cs = reinterpret_cast<CREATESTRUCT *>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    } break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // Fill with black
        RECT rect;
        GetClientRect(hwnd, &rect);
        FillRect(hdc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));

        EndPaint(hwnd, &ps);
    }
        return 0;

    case WM_ERASEBKGND:
        return 1; // We handle background in WM_PAINT

    default:
        break;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

} // namespace adhd_multi_monitor
