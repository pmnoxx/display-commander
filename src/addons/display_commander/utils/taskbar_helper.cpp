// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "taskbar_helper.hpp"

// Libraries <standard C++>
// (none)

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

namespace display_commander {
namespace utils {

namespace {
// Track whether we hid the taskbar so we only call ShowWindow when state changes and so we can restore on exit.
bool s_taskbar_hidden_by_us = false;

struct EnumParam {
    int cmd;
};

BOOL CALLBACK EnumSecondaryTrayWnds(HWND hwnd, LPARAM lParam) {
    if (hwnd == nullptr) return TRUE;
    wchar_t class_name[64] = {};
    if (GetClassNameW(hwnd, class_name, 64) == 0) return TRUE;
    if (wcscmp(class_name, L"Shell_SecondaryTrayWnd") != 0) return TRUE;
    if (IsWindow(hwnd)) ShowWindow(hwnd, reinterpret_cast<EnumParam*>(lParam)->cmd);
    return TRUE;
}

void SetTaskbarWindowsVisible(bool show) {
    const int cmd = show ? SW_SHOW : SW_HIDE;

    HWND main_tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (main_tray != nullptr && IsWindow(main_tray)) {
        ShowWindow(main_tray, cmd);
    }

    EnumParam param = {cmd};
    EnumWindows(EnumSecondaryTrayWnds, reinterpret_cast<LPARAM>(&param));
}

}  // namespace

void UpdateTaskbarVisibility(bool in_foreground, int mode) {
    // 0 = no change, 1 = hide when in foreground, 2 = always hide
    bool want_hidden = (mode == 2) || (mode == 1 && in_foreground);
    if (want_hidden == s_taskbar_hidden_by_us) return;

    s_taskbar_hidden_by_us = want_hidden;
    SetTaskbarWindowsVisible(!want_hidden);
}

void RestoreTaskbarIfHidden() {
    if (!s_taskbar_hidden_by_us) return;
    s_taskbar_hidden_by_us = false;
    SetTaskbarWindowsVisible(true);
}

}  // namespace utils
}  // namespace display_commander
