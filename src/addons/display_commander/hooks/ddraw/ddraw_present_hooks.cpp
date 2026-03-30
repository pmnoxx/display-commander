// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "ddraw_present_hooks.hpp"

// Libraries <ReShade> / <imgui>

// Libraries <standard C++>

// Libraries <Windows.h> — before other Windows headers

// Libraries <Windows>

namespace display_commanderhooks::ddraw {

bool InstallDDrawHooks(HMODULE hmodule) {
    (void)hmodule;
    // DDraw present/FPS-limiter support intentionally removed.
    return false;
}

}  // namespace display_commanderhooks::ddraw
