#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace display_commander::utils {

struct OverlayWindowInfo {
    HWND hwnd;
    std::wstring window_title;
    std::wstring process_name;
    DWORD process_id;
    bool is_visible;
    bool overlaps_game;
    bool is_above_game;  // True if window is above the game window in Z-order
    long overlapping_area_pixels;  // Overlapping area in pixels (0 if not overlapping)
    float overlapping_area_percent; // Overlapping area as percentage of game window (0.0 if not overlapping)
};

// Get all windows that are above the game window in Z-order
std::vector<HWND> GetWindowsAboveGameWindow(HWND game_window);

// Detect overlay windows that overlap with the game window
std::vector<OverlayWindowInfo> DetectOverlayWindows(HWND game_window);

// Get window title text
std::wstring GetWindowTitle(HWND hwnd);

// Get process name from window handle
std::wstring GetProcessNameFromWindow(HWND hwnd);

// Check if a window with the specified title is visible
bool IsWindowWithTitleVisible(const std::wstring& window_title);

} // namespace display_commander::utils

