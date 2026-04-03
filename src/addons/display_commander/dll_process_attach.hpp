// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

#include <Windows.h>

namespace display_commander::dll_main {

void OnProcessAttach(HMODULE h_module);
void OnProcessDetach(HMODULE h_module);

}  // namespace display_commander::dll_main
