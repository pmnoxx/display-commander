#include "adhd_multi_monitor.hpp"
#include <dwmapi.h>
#include <algorithm>
#include "../globals.hpp"
#include "../utils/logging.hpp"

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

AdhdMultiMonitorManager::AdhdMultiMonitorManager() : initialized_(false), background_window_created_(false) {
    game_monitor_rect_ = {0, 0, 0, 0};
}

AdhdMultiMonitorManager::~AdhdMultiMonitorManager() { Shutdown(); }

bool AdhdMultiMonitorManager::Initialize() {
    if (initialized_) return true;

    // Check if we have a valid game window handle
    HWND game_hwnd = g_last_swapchain_hwnd.load();
    if (game_hwnd == nullptr || !IsWindow(game_hwnd)) return false;

    // Enumerate available monitors
    EnumerateMonitors();

    // Register the single background window class
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

    if (!background_window_created_) {
        CreateBackgroundWindow();
    }

    initialized_ = true;
    return true;
}

void AdhdMultiMonitorManager::Shutdown() {
    if (!initialized_) return;

    DestroyBackgroundWindow();
    UnregisterClassW(BACKGROUND_WINDOW_CLASS, GetModuleHandle(nullptr));

    initialized_ = false;
}

void AdhdMultiMonitorManager::Update() {
    // Process all pending messages for the ADHD background window (so BackgroundWindowProc runs on this thread)
    if (initialized_ && background_hwnd_) {
        MSG msg = {};
        while (PeekMessageW(&msg, background_hwnd_, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if ((!enabled_for_other_displays_.load() && !enabled_for_game_display_.load()) || !initialized_) return;

    HWND current_hwnd = g_last_swapchain_hwnd.load();
    if (!current_hwnd) return;

    bool game_in_foreground = !IsAppInBackground();
    static std::optional<bool> last_game_in_foreground_ = std::nullopt;
    if (!last_game_in_foreground_.has_value() || game_in_foreground != last_game_in_foreground_.value_or(false)) {
        last_game_in_foreground_ = game_in_foreground;
        PositionBackgroundWindow();
    }
}

void AdhdMultiMonitorManager::SetEnabled(bool enabled_for_game_display, bool enabled_for_other_displays) {
    HWND game_hwnd = g_last_swapchain_hwnd.load();
    if (!game_hwnd) return;

    if (enabled_for_game_display_.load() == enabled_for_game_display
        && enabled_for_other_displays_.load() == enabled_for_other_displays) {
        return;
    }

    enabled_for_game_display_.store(enabled_for_game_display);
    enabled_for_other_displays_.store(enabled_for_other_displays);

    PositionBackgroundWindow();
}

bool AdhdMultiMonitorManager::HasMultipleMonitors() const { return monitor_rects_.size() > 1; }

bool AdhdMultiMonitorManager::CreateBackgroundWindow() {
    if (background_window_created_) return true;

    HWND game_hwnd = g_last_swapchain_hwnd.load();
    if (!game_hwnd) return false;

    HINSTANCE hInstance = GetModuleHandle(nullptr);
    background_hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, BACKGROUND_WINDOW_CLASS,
                                       BACKGROUND_WINDOW_TITLE, WS_POPUP, 0, 0, 1, 1,
                                       nullptr, nullptr, hInstance, this);

    if (!background_hwnd_) {
        LogError("Failed to create ADHD background window");
        return false;
    }

    SetLayeredWindowAttributes(background_hwnd_, 0, 255, LWA_ALPHA);
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
    if (!background_window_created_ || !background_hwnd_) return;

    HWND game_hwnd = g_last_swapchain_hwnd.load();
    if (!game_hwnd) return;

    bool other = enabled_for_other_displays_.load();
    bool game_display = enabled_for_game_display_.load();

    if (!other && !game_display) {
        ShowWindow(background_hwnd_, SW_HIDE);
        return;
    }

    RECT rect_to_cover = {};
    if (other) {
        // All displays: bounding rect of all monitors
        if (monitor_rects_.empty()) return;
        rect_to_cover = {LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN};
        for (const RECT& r : monitor_rects_) {
            rect_to_cover.left = (std::min)(rect_to_cover.left, r.left);
            rect_to_cover.top = (std::min)(rect_to_cover.top, r.top);
            rect_to_cover.right = (std::max)(rect_to_cover.right, r.right);
            rect_to_cover.bottom = (std::max)(rect_to_cover.bottom, r.bottom);
        }
    } else {
        // Game display only: rect of the monitor that contains the game window
        HMONITOR mon = MonitorFromWindow(game_hwnd, MONITOR_DEFAULTTONEAREST);
        if (!mon) return;
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(mon, &mi)) return;
        rect_to_cover = mi.rcMonitor;
    }

    int width = rect_to_cover.right - rect_to_cover.left;
    int height = rect_to_cover.bottom - rect_to_cover.top;
    SetWindowPos(background_hwnd_, game_hwnd, rect_to_cover.left, rect_to_cover.top, width, height, SWP_NOACTIVATE);

    bool show = !IsAppInBackground();
    ShowWindow(background_hwnd_, show ? SW_SHOW : SW_HIDE);
}

void AdhdMultiMonitorManager::EnumerateMonitors() {
    monitor_rects_.clear();

    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR hMonitor, HDC hdc, LPRECT lprcMonitor, LPARAM lParam) -> BOOL {
            auto* manager = reinterpret_cast<AdhdMultiMonitorManager*>(lParam);
            manager->monitor_rects_.push_back(*lprcMonitor);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(this));
}

LRESULT CALLBACK AdhdMultiMonitorManager::BackgroundWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
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

        case WM_ERASEBKGND: return 1;  // We handle background in WM_PAINT

        default: break;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

}  // namespace adhd_multi_monitor
