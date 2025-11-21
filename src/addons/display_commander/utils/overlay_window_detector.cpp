#include "overlay_window_detector.hpp"
#include <algorithm>

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

    // Start from the top window and traverse down until we reach the game window
    HWND hwnd = GetTopWindow(nullptr);

    while (hwnd != nullptr && hwnd != game_window) {
        if (IsWindowVisible(hwnd)) {
            windows_above.push_back(hwnd);
        }
        hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
    }

    return windows_above;
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

    // Get all windows above the game window
    auto windows_above = GetWindowsAboveGameWindow(game_window);

    // Check each window for overlap
    for (HWND hwnd : windows_above) {
        if (!IsWindow(hwnd)) {
            continue;
        }

        RECT window_rect = {};
        if (!GetWindowRect(hwnd, &window_rect)) {
            continue;
        }

        // Inflate the window rect slightly to account for borders
        InflateRect(&window_rect, -15, -15);

        // Check if windows overlap
        RECT intersect_rect = {};
        bool overlaps = IntersectRect(&intersect_rect, &window_rect, &game_rect) != FALSE;

        // Calculate overlapping area
        long overlapping_area = 0;
        float overlapping_percent = 0.0f;

        if (overlaps) {
            long intersect_width = intersect_rect.right - intersect_rect.left;
            long intersect_height = intersect_rect.bottom - intersect_rect.top;
            overlapping_area = intersect_width * intersect_height;

            // Calculate as percentage of game window area
            long game_width = game_rect.right - game_rect.left;
            long game_height = game_rect.bottom - game_rect.top;
            long game_area = game_width * game_height;

            if (game_area > 0) {
                overlapping_percent = (static_cast<float>(overlapping_area) / static_cast<float>(game_area)) * 100.0f;
            }
        }

        // Get window information
        std::wstring title = GetWindowTitle(hwnd);
        std::wstring process_name = GetProcessNameFromWindow(hwnd);

        DWORD process_id = 0;
        GetWindowThreadProcessId(hwnd, &process_id);

        bool is_visible = IsWindowVisible(hwnd) != FALSE;

        // Only include windows that overlap with the game window
        // This filters out windows that are above but don't actually overlap
        if (overlaps) {
            OverlayWindowInfo info;
            info.hwnd = hwnd;
            info.window_title = title;
            info.process_name = process_name;
            info.process_id = process_id;
            info.is_visible = is_visible;
            info.overlaps_game = overlaps;
            info.overlapping_area_pixels = overlapping_area;
            info.overlapping_area_percent = overlapping_percent;

            overlays.push_back(info);
        }
    }

    return overlays;
}

} // namespace display_commander::utils

