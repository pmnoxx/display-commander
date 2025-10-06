#include "driver_restart.h"

namespace DriverRestart {

std::vector<WindowManager::WindowInfo> WindowManager::saved_windows;

void WindowManager::SaveWindows() {
    saved_windows.clear();
    EnumWindows(EnumWindowsProc, 0);
}

void WindowManager::RestoreWindows() {
    for (const auto& window_info : saved_windows) {
        if (IsWindow(window_info.hwnd) != 0) {
            SetWindowPlacement(window_info.hwnd, const_cast<WINDOWPLACEMENT*>(&window_info.placement));
        }
    }
    saved_windows.clear();
}

BOOL CALLBACK WindowManager::EnumWindowsProc(HWND hwnd, LPARAM l_param) {
    if (IsWindowVisible(hwnd) == 0 || IsIconic(hwnd) != 0) {
        return TRUE;
    }

    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(WINDOWPLACEMENT);

    if (GetWindowPlacement(hwnd, &placement) != 0) {
        WindowInfo info = { hwnd, placement };
        saved_windows.push_back(info);
    }

    return TRUE;
}

} // namespace DriverRestart