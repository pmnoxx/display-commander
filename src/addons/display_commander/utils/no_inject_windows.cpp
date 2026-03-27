// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "no_inject_windows.hpp"

// Libraries <Windows>
#include <Windows.h>

bool should_skip_addon_injection_for_window(HWND hwnd) {
    (void)hwnd;
    return false;
}
