#include "adhd_multi_monitor.hpp"
#include "../globals.hpp"
#include "../utils.hpp"
#include <dwmapi.h>
#include <algorithm>
#include <string>

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
    : enabled_(false)
    , focus_disengage_(true)
    , game_has_focus_(true)
    , background_hwnd_(nullptr)
    , game_hwnd_(nullptr)
    , initialized_(false)
    , background_window_created_(false)
{
}

AdhdMultiMonitorManager::~AdhdMultiMonitorManager()
{
    Shutdown();
}

bool AdhdMultiMonitorManager::Initialize()
{
    if (initialized_)
        return true;

    // Get the game window handle from the global swapchain HWND
    game_hwnd_ = g_last_swapchain_hwnd.load();
    if (!game_hwnd_ || !IsWindow(game_hwnd_))
        return false;

    // Enumerate available monitors
    EnumerateMonitors();

    if (monitors_.size() <= 1)
        return false; // No need for ADHD mode with single monitor

    // Register the background window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = BackgroundWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = BACKGROUND_WINDOW_CLASS;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    if (!RegisterClassExW(&wc))
    {
        LogError("Failed to register ADHD background window class");
        return false;
    }

    initialized_ = true;
    return true;
}

void AdhdMultiMonitorManager::Shutdown()
{
    if (!initialized_)
        return;

    DestroyBackgroundWindow();

    // Unregister the window class
    UnregisterClassW(BACKGROUND_WINDOW_CLASS, GetModuleHandle(nullptr));

    initialized_ = false;
}

void AdhdMultiMonitorManager::SetEnabled(bool enabled)
{
    if (enabled_ == enabled)
        return;

    enabled_ = enabled;

    if (enabled)
    {
        if (!background_window_created_)
        {
            CreateBackgroundWindow();
        }
        UpdateBackgroundWindow();
    }
    else
    {
        ShowBackgroundWindow(false);
    }
}

void AdhdMultiMonitorManager::SetFocusDisengage(bool disengage)
{
    focus_disengage_ = disengage;
    UpdateBackgroundWindow();
}

void AdhdMultiMonitorManager::UpdateBackgroundWindow()
{
    if (!enabled_ || !background_window_created_)
        return;

    // Check if we should show the background window
    bool shouldShow = true;

    if (focus_disengage_ && !game_has_focus_)
    {
        shouldShow = false;
    }

    ShowBackgroundWindow(shouldShow);

    if (shouldShow)
    {
        PositionBackgroundWindow();
    }
}

void AdhdMultiMonitorManager::OnWindowFocusChanged(bool hasFocus)
{
    if (game_has_focus_ == hasFocus)
        return;

    game_has_focus_ = hasFocus;
    UpdateBackgroundWindow();
}

bool AdhdMultiMonitorManager::HasMultipleMonitors() const
{
    return monitors_.size() > 1;
}

HMONITOR AdhdMultiMonitorManager::GetGameMonitor() const
{
    if (!game_hwnd_)
        return nullptr;

    return MonitorFromWindow(game_hwnd_, MONITOR_DEFAULTTONEAREST);
}

void AdhdMultiMonitorManager::SetGameWindow(HWND hwnd)
{
    if (hwnd && IsWindow(hwnd))
    {
        game_hwnd_ = hwnd;
        // Update monitor info when window changes
        UpdateMonitorInfo();
    }
}

bool AdhdMultiMonitorManager::CreateBackgroundWindow()
{
    if (background_window_created_)
        return true;

    if (!game_hwnd_)
        return false;

    // Create the background window
    background_hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        BACKGROUND_WINDOW_CLASS,
        BACKGROUND_WINDOW_TITLE,
        WS_POPUP,
        0, 0, 1, 1, // Will be repositioned
        nullptr,
        nullptr,
        GetModuleHandle(nullptr),
        this
    );

    if (!background_hwnd_)
    {
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

void AdhdMultiMonitorManager::DestroyBackgroundWindow()
{
    if (background_hwnd_)
    {
        DestroyWindow(background_hwnd_);
        background_hwnd_ = nullptr;
    }
    background_window_created_ = false;
}

void AdhdMultiMonitorManager::PositionBackgroundWindow()
{
    if (!background_window_created_ || !enabled_)
        return;

    // Get the game monitor
    HMONITOR gameMonitor = GetGameMonitor();
    if (!gameMonitor)
        return;

    // Find the game monitor in our list
    MonitorInfo* gameMonitorInfo = nullptr;
    for (auto& monitor : monitors_)
    {
        if (monitor.hMonitor == gameMonitor)
        {
            gameMonitorInfo = &monitor;
            break;
        }
    }

    if (!gameMonitorInfo)
        return;

    // Calculate the bounding rectangle of all monitors except the game monitor
    RECT boundingRect = { LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN };
    bool hasOtherMonitors = false;

    for (const auto& monitor : monitors_)
    {
        if (monitor.hMonitor == gameMonitor)
            continue;

        hasOtherMonitors = true;
        boundingRect.left = std::min(boundingRect.left, monitor.rect.left);
        boundingRect.top = std::min(boundingRect.top, monitor.rect.top);
        boundingRect.right = std::max(boundingRect.right, monitor.rect.right);
        boundingRect.bottom = std::max(boundingRect.bottom, monitor.rect.bottom);
    }

    if (!hasOtherMonitors)
    {
        ShowBackgroundWindow(false);
        return;
    }

    // Position the background window to cover all monitors except the game monitor
    int width = boundingRect.right - boundingRect.left;
    int height = boundingRect.bottom - boundingRect.top;

    SetWindowPos(
        background_hwnd_,
        HWND_TOPMOST,
        boundingRect.left,
        boundingRect.top,
        width,
        height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );
}

void AdhdMultiMonitorManager::ShowBackgroundWindow(bool show)
{
    if (!background_window_created_)
        return;

    ShowWindow(background_hwnd_, show ? SW_SHOW : SW_HIDE);
}

void AdhdMultiMonitorManager::EnumerateMonitors()
{
    monitors_.clear();

    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hMonitor, HDC hdc, LPRECT lprcMonitor, LPARAM lParam) -> BOOL {
        auto* manager = reinterpret_cast<AdhdMultiMonitorManager*>(lParam);

        MonitorInfo info = {};
        info.hMonitor = hMonitor;
        info.rect = *lprcMonitor;

        // Check if this is the primary monitor
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hMonitor, &mi))
        {
            info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        }

        // Get device name
        DISPLAY_DEVICEW dd = {};
        dd.cb = sizeof(dd);
        if (EnumDisplayDevicesW(nullptr, 0, &dd, EDD_GET_DEVICE_INTERFACE_NAME))
        {
            info.deviceName = dd.DeviceString;
        }

        manager->monitors_.push_back(info);
        return TRUE;
    }, reinterpret_cast<LPARAM>(this));
}

void AdhdMultiMonitorManager::UpdateMonitorInfo()
{
    EnumerateMonitors();

    // Update game monitor info
    HMONITOR gameMonitor = GetGameMonitor();
    if (gameMonitor)
    {
        for (const auto& monitor : monitors_)
        {
            if (monitor.hMonitor == gameMonitor)
            {
                game_monitor_ = monitor;
                break;
            }
        }
    }
}

LRESULT CALLBACK AdhdMultiMonitorManager::BackgroundWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
        {
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }
        break;

    case WM_PAINT:
        {
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
