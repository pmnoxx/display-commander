#include "overlay_window_detector.hpp"
#include <set>

namespace display_commander::utils {

std::wstring GetWindowTitle(HWND hwnd) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return L"";
    }

    wchar_t title[256] = {};
    int length = GetWindowTextW(hwnd, title, 256);
    if (length > 0) {
        return std::wstring(title);
    }
    return L"";
}

std::wstring GetProcessNameFromWindow(HWND hwnd) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return L"";
    }

    DWORD process_id = 0;
    GetWindowThreadProcessId(hwnd, &process_id);
    if (process_id == 0) {
        return L"";
    }

    HANDLE h_process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, process_id);
    if (h_process == nullptr) {
        return L"";
    }

    wchar_t process_name[MAX_PATH] = {};
    DWORD size = MAX_PATH;

    if (QueryFullProcessImageNameW(h_process, 0, process_name, &size)) {
        CloseHandle(h_process);

        // Extract just the filename from the full path
        std::wstring full_path(process_name);
        size_t last_slash = full_path.find_last_of(L"\\/");
        if (last_slash != std::wstring::npos) {
            return full_path.substr(last_slash + 1);
        }
        return full_path;
    }

    CloseHandle(h_process);
    return L"";
}

std::vector<HWND> GetWindowsAboveGameWindow(HWND game_window) {
    std::vector<HWND> windows_above;

    if (game_window == nullptr || !IsWindow(game_window)) {
        return windows_above;
    }

    // Get the monitor that the game window is on
    HMONITOR hMonitorGame = MonitorFromWindow(game_window, MONITOR_DEFAULTTONEAREST);
    if (hMonitorGame == nullptr) {
        return windows_above;
    }

    // Enumerate all top-level windows (Special-K approach)
    std::set<HWND> hWndTopLevel;
    std::set<HWND> hWndTopLevelOnGameMonitor;

    EnumWindows([](HWND hWnd, LPARAM lParam) -> BOOL {
        std::set<HWND>* pTopLevelSet = reinterpret_cast<std::set<HWND>*>(lParam);
        if (pTopLevelSet != nullptr) {
            pTopLevelSet->emplace(hWnd);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&hWndTopLevel));

    // Filter windows by monitor (check if window center is on the same monitor as game)
    for (HWND hWnd : hWndTopLevel) {
        RECT rcWindow = {};
        if (!GetWindowRect(hWnd, &rcWindow)) {
            continue;
        }

        POINT pt = {
            rcWindow.left + ((rcWindow.right - rcWindow.left) / 2),
            rcWindow.top + ((rcWindow.bottom - rcWindow.top) / 2)
        };

        if (MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST) == hMonitorGame) {
            hWndTopLevelOnGameMonitor.emplace(hWnd);
        }
    }

    // Traverse upward in Z-order from the game window (Special-K approach)
    HWND hWndAbove = GetWindow(game_window, GW_HWNDPREV);

    while (hWndAbove != nullptr && IsWindow(hWndAbove)) {
        // Only include visible windows that are on the same monitor
        if (IsWindowVisible(hWndAbove) != FALSE && hWndTopLevelOnGameMonitor.count(hWndAbove) > 0) {
            windows_above.push_back(hWndAbove);
        }

        hWndAbove = GetWindow(hWndAbove, GW_HWNDPREV);
    }

    return windows_above;
}

// Helper function to check if a window is above another window in Z-order
bool IsWindowAboveInZOrder(HWND hwnd_test, HWND hwnd_reference) {
    if (hwnd_test == nullptr || hwnd_reference == nullptr) {
        return false;
    }

    // Traverse upward from the reference window
    HWND hwnd_above = GetWindow(hwnd_reference, GW_HWNDPREV);
    while (hwnd_above != nullptr && IsWindow(hwnd_above)) {
        if (hwnd_above == hwnd_test) {
            return true;  // Found the test window above the reference
        }
        hwnd_above = GetWindow(hwnd_above, GW_HWNDPREV);
    }

    return false;  // Window is not above (either below or not in the same Z-order chain)
}

std::vector<OverlayWindowInfo> DetectOverlayWindows(HWND game_window) {
    std::vector<OverlayWindowInfo> overlays;

    if (game_window == nullptr || !IsWindow(game_window)) {
        return overlays;
    }

    // Get game window rectangle
    RECT game_rect = {};
    if (!GetWindowRect(game_window, &game_rect)) {
        return overlays;
    }

    // Get the monitor that the game window is on
    HMONITOR hMonitorGame = MonitorFromWindow(game_window, MONITOR_DEFAULTTONEAREST);
    if (hMonitorGame == nullptr) {
        return overlays;
    }

    // Get all windows above the game window for Z-order checking
    auto windows_above = GetWindowsAboveGameWindow(game_window);
    std::set<HWND> windows_above_set(windows_above.begin(), windows_above.end());

    // Enumerate all top-level windows on the same monitor (Special-K approach)
    std::set<HWND> hWndTopLevel;
    std::set<HWND> hWndTopLevelOnGameMonitor;

    EnumWindows([](HWND hWnd, LPARAM lParam) -> BOOL {
        std::set<HWND>* pTopLevelSet = reinterpret_cast<std::set<HWND>*>(lParam);
        if (pTopLevelSet != nullptr) {
            pTopLevelSet->emplace(hWnd);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&hWndTopLevel));

    // Filter windows by monitor (check if window center is on the same monitor as game)
    for (HWND hWnd : hWndTopLevel) {
        // Only check visible windows
        if (IsWindowVisible(hWnd) == FALSE) {
            continue;
        }

        RECT rcWindow = {};
        if (!GetWindowRect(hWnd, &rcWindow)) {
            continue;
        }

        POINT pt = {
            rcWindow.left + ((rcWindow.right - rcWindow.left) / 2),
            rcWindow.top + ((rcWindow.bottom - rcWindow.top) / 2)
        };

        if (MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST) == hMonitorGame) {
            hWndTopLevelOnGameMonitor.emplace(hWnd);
        }
    }

    // Check each visible window on the same monitor for overlap
    for (HWND hwnd : hWndTopLevelOnGameMonitor) {
        if (hwnd == game_window) {
            continue;  // Skip the game window itself
        }

        if (!IsWindow(hwnd) || IsWindowVisible(hwnd) == FALSE) {
            continue;
        }

        RECT window_rect = {};
        if (!GetWindowRect(hwnd, &window_rect)) {
            continue;
        }

        // Inflate the window rect slightly to account for borders
        RECT window_rect_inflated = window_rect;
        InflateRect(&window_rect_inflated, -15, -15);

        // Check if windows overlap
        RECT intersect_rect = {};
        bool overlaps = IntersectRect(&intersect_rect, &window_rect_inflated, &game_rect) != FALSE;

        // Only process overlapping windows
        if (!overlaps) {
            continue;
        }

        // Calculate overlapping area
        long intersect_width = intersect_rect.right - intersect_rect.left;
        long intersect_height = intersect_rect.bottom - intersect_rect.top;
        long overlapping_area = intersect_width * intersect_height;

        // Calculate as percentage of game window area
        long game_width = game_rect.right - game_rect.left;
        long game_height = game_rect.bottom - game_rect.top;
        long game_area = game_width * game_height;
        float overlapping_percent = 0.0f;

        if (game_area > 0) {
            overlapping_percent = (static_cast<float>(overlapping_area) / static_cast<float>(game_area)) * 100.0f;
        }

        // Get window information
        std::wstring title = GetWindowTitle(hwnd);
        std::wstring process_name = GetProcessNameFromWindow(hwnd);

        DWORD process_id = 0;
        GetWindowThreadProcessId(hwnd, &process_id);

        bool is_visible = IsWindowVisible(hwnd) != FALSE;

        // Check if window is above the game window in Z-order
        bool is_above = windows_above_set.count(hwnd) > 0 || IsWindowAboveInZOrder(hwnd, game_window);

        OverlayWindowInfo info;
        info.hwnd = hwnd;
        info.window_title = title;
        info.process_name = process_name;
        info.process_id = process_id;
        info.is_visible = is_visible;
        info.overlaps_game = overlaps;
        info.is_above_game = is_above;
        info.overlapping_area_pixels = overlapping_area;
        info.overlapping_area_percent = overlapping_percent;

        overlays.push_back(info);
    }

    return overlays;
}

} // namespace display_commander::utils

