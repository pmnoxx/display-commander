// Source Code <Display Commander>
#include "no_inject_windows.hpp"

#include "ui/standalone_ui_settings_bridge.hpp"

// Libraries <Windows.h>
#include <Windows.h>

namespace {

bool is_in_no_inject_list(HWND hwnd) {
    if (hwnd == nullptr) return false;
    // Independent/standalone settings window (ReShade "Show independent window") — skip
    // so we don't draw performance overlay or other addon UI into that window.
    if (hwnd == standalone_ui_settings::GetStandaloneUiHwnd()) return true;
    return false;
}

}  // namespace

bool should_skip_addon_injection_for_window(HWND hwnd) {
    return is_in_no_inject_list(hwnd);
}
