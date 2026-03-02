#include "standalone_ui_settings_bridge.hpp"
#include "../globals.hpp"
#include "../hooks/api_hooks.hpp"

namespace standalone_ui_settings {

void SetStandaloneUiHwnd(uintptr_t hwnd) {
    ::g_standalone_ui_hwnd.store(reinterpret_cast<HWND>(hwnd), std::memory_order_release);
}

HWND CreateWindowW_Direct(LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth,
                          int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) {
    return display_commanderhooks::CreateWindowW_Direct(lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight,
                                                        hWndParent, hMenu, hInstance, lpParam);
}

}  // namespace standalone_ui_settings
