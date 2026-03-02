#pragma once

#include <cstdint>

#include <windows.h>

// Bridge for standalone settings UI (No ReShade): avoid including globals.hpp/reshade in the standalone UI TU.
// Implemented in standalone_ui_settings_bridge.cpp.

namespace standalone_ui_settings {

/** Register/unregister the standalone UI window so it is not treated as the game window. Call with hwnd after CreateWindow, nullptr before DestroyWindow. */
void SetStandaloneUiHwnd(uintptr_t hwnd);

/** CreateWindowW bypassing the hook (same signature as CreateWindowW). Use from standalone UI so window creation is not intercepted. */
HWND CreateWindowW_Direct(LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth,
                          int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);

}  // namespace standalone_ui_settings
