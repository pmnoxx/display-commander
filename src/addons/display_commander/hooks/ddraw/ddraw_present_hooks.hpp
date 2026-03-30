#pragma once

// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
// Libraries <ReShade> / <imgui>
// Libraries <standard C++>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

// Libraries <Windows>

namespace display_commanderhooks::ddraw {

// DDraw hook support removed.
// Kept only as a stub signature so existing integration can compile without DirectDraw detours.
bool InstallDDrawHooks(HMODULE hmodule);

}  // namespace display_commanderhooks::ddraw
