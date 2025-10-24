#include "display_settings_hooks.hpp"
#include "../utils.hpp"
#include "../utils/logging.hpp"
#include "../globals.hpp"
#include "../settings/developer_tab_settings.hpp"
#include <MinHook.h>
#include <windows.h>
#include <wingdi.h>

// Original function pointers
ChangeDisplaySettingsA_pfn ChangeDisplaySettingsA_Original = nullptr;
ChangeDisplaySettingsW_pfn ChangeDisplaySettingsW_Original = nullptr;
ChangeDisplaySettingsExA_pfn ChangeDisplaySettingsExA_Original = nullptr;
ChangeDisplaySettingsExW_pfn ChangeDisplaySettingsExW_Original = nullptr;

// Window management original function pointers
SetWindowPos_pfn SetWindowPos_Original = nullptr;
ShowWindow_pfn ShowWindow_Original = nullptr;
SetWindowLongA_pfn SetWindowLongA_Original = nullptr;
SetWindowLongW_pfn SetWindowLongW_Original = nullptr;
SetWindowLongPtrA_pfn SetWindowLongPtrA_Original = nullptr;
SetWindowLongPtrW_pfn SetWindowLongPtrW_Original = nullptr;

// Hook installation state
static std::atomic<bool> g_display_settings_hooks_installed{false};

// Hook detour functions
LONG WINAPI ChangeDisplaySettingsA_Detour(DEVMODEA *lpDevMode, DWORD dwFlags) {
    g_display_settings_hook_counters[DISPLAY_SETTINGS_HOOK_CHANGEDISPLAYSETTINGSA].fetch_add(1);
    g_display_settings_hook_total_count.fetch_add(1);

    // Check if fullscreen prevention is enabled
    if (settings::g_developerTabSettings.prevent_fullscreen.GetValue()) {
        LogInfo("ChangeDisplaySettingsA blocked - fullscreen prevention enabled");
        return DISP_CHANGE_SUCCESSFUL; // Return success without changing display mode
    }

    return ChangeDisplaySettingsA_Original(lpDevMode, dwFlags);
}

LONG WINAPI ChangeDisplaySettingsW_Detour(DEVMODEW *lpDevMode, DWORD dwFlags) {
    g_display_settings_hook_counters[DISPLAY_SETTINGS_HOOK_CHANGEDISPLAYSETTINGSW].fetch_add(1);
    g_display_settings_hook_total_count.fetch_add(1);

    // Check if fullscreen prevention is enabled
    if (settings::g_developerTabSettings.prevent_fullscreen.GetValue()) {
        LogInfo("ChangeDisplaySettingsW blocked - fullscreen prevention enabled");
        return DISP_CHANGE_SUCCESSFUL; // Return success without changing display mode
    }

    return ChangeDisplaySettingsW_Original(lpDevMode, dwFlags);
}

LONG WINAPI ChangeDisplaySettingsExA_Detour(LPCSTR lpszDeviceName, DEVMODEA *lpDevMode, HWND hWnd, DWORD dwFlags, LPVOID lParam) {
    g_display_settings_hook_counters[DISPLAY_SETTINGS_HOOK_CHANGEDISPLAYSETTINGSEXA].fetch_add(1);
    g_display_settings_hook_total_count.fetch_add(1);

    // Check if fullscreen prevention is enabled
    if (settings::g_developerTabSettings.prevent_fullscreen.GetValue()) {
        LogInfo("ChangeDisplaySettingsExA blocked - fullscreen prevention enabled");
        return DISP_CHANGE_SUCCESSFUL; // Return success without changing display mode
    }

    return ChangeDisplaySettingsExA_Original(lpszDeviceName, lpDevMode, hWnd, dwFlags, lParam);
}

LONG WINAPI ChangeDisplaySettingsExW_Detour(LPCWSTR lpszDeviceName, DEVMODEW *lpDevMode, HWND hWnd, DWORD dwFlags, LPVOID lParam) {
    g_display_settings_hook_counters[DISPLAY_SETTINGS_HOOK_CHANGEDISPLAYSETTINGSEXW].fetch_add(1);
    g_display_settings_hook_total_count.fetch_add(1);

    // Check if fullscreen prevention is enabled
    if (settings::g_developerTabSettings.prevent_fullscreen.GetValue()) {
        LogInfo("ChangeDisplaySettingsExW blocked - fullscreen prevention enabled");
        return DISP_CHANGE_SUCCESSFUL; // Return success without changing display mode
    }

    return ChangeDisplaySettingsExW_Original(lpszDeviceName, lpDevMode, hWnd, dwFlags, lParam);
}

// Window management detour functions for OpenGL fullscreen prevention
BOOL WINAPI SetWindowPos_Detour(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    g_display_settings_hook_counters[DISPLAY_SETTINGS_HOOK_SETWINDOWPOS].fetch_add(1);
    g_display_settings_hook_total_count.fetch_add(1);

    // Check if fullscreen prevention is enabled
    if (settings::g_developerTabSettings.prevent_fullscreen.GetValue()) {
        // Prevent fullscreen positioning and maximize operations
        if (hWndInsertAfter == HWND_TOPMOST || hWndInsertAfter == HWND_NOTOPMOST) {
            // Allow topmost changes but prevent fullscreen positioning
            if (X == 0 && Y == 0 && cx > 0 && cy > 0) {
                // This might be an attempt to go fullscreen, force windowed positioning
                LogInfo("SetWindowPos blocked fullscreen attempt - forcing windowed positioning");
                return SetWindowPos_Original(hWnd, hWndInsertAfter, 100, 100, cx, cy, uFlags);
            }
        }
    }

    return SetWindowPos_Original(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

BOOL WINAPI ShowWindow_Detour(HWND hWnd, int nCmdShow) {
    g_display_settings_hook_counters[DISPLAY_SETTINGS_HOOK_SHOWWINDOW].fetch_add(1);
    g_display_settings_hook_total_count.fetch_add(1);

    // Check if fullscreen prevention is enabled
    if (settings::g_developerTabSettings.prevent_fullscreen.GetValue()) {
        // Prevent maximize operations that could lead to fullscreen
        if (nCmdShow == SW_MAXIMIZE || nCmdShow == SW_SHOWMAXIMIZED) {
            LogInfo("ShowWindow blocked maximize attempt - forcing normal window");
            return ShowWindow_Original(hWnd, SW_SHOWNORMAL);
        }
    }

    return ShowWindow_Original(hWnd, nCmdShow);
}

LONG WINAPI SetWindowLongA_Detour(HWND hWnd, int nIndex, LONG dwNewLong) {
    g_display_settings_hook_counters[DISPLAY_SETTINGS_HOOK_SETWINDOWLONGA].fetch_add(1);
    g_display_settings_hook_total_count.fetch_add(1);

    // Check if fullscreen prevention is enabled
    if (settings::g_developerTabSettings.prevent_fullscreen.GetValue()) {
        // Prevent window style changes that enable fullscreen
        if (nIndex == GWL_STYLE) {
            // Remove fullscreen-enabling styles
            if (dwNewLong & WS_POPUP) {
                LogInfo("SetWindowLongA blocked WS_POPUP style - forcing windowed style");
                dwNewLong &= ~WS_POPUP;
                dwNewLong |= WS_OVERLAPPEDWINDOW;
            }
        }
    }

    return SetWindowLongA_Original(hWnd, nIndex, dwNewLong);
}

LONG WINAPI SetWindowLongW_Detour(HWND hWnd, int nIndex, LONG dwNewLong) {
    g_display_settings_hook_counters[DISPLAY_SETTINGS_HOOK_SETWINDOWLONGW].fetch_add(1);
    g_display_settings_hook_total_count.fetch_add(1);

    // Check if fullscreen prevention is enabled
    if (settings::g_developerTabSettings.prevent_fullscreen.GetValue()) {
        // Prevent window style changes that enable fullscreen
        if (nIndex == GWL_STYLE) {
            // Remove fullscreen-enabling styles
            if (dwNewLong & WS_POPUP) {
                LogInfo("SetWindowLongW blocked WS_POPUP style - forcing windowed style");
                dwNewLong &= ~WS_POPUP;
                dwNewLong |= WS_OVERLAPPEDWINDOW;
            }
        }
    }

    return SetWindowLongW_Original(hWnd, nIndex, dwNewLong);
}

LONG_PTR WINAPI SetWindowLongPtrA_Detour(HWND hWnd, int nIndex, LONG_PTR dwNewLong) {
    g_display_settings_hook_counters[DISPLAY_SETTINGS_HOOK_SETWINDOWLONGPTRA].fetch_add(1);
    g_display_settings_hook_total_count.fetch_add(1);

    // Check if fullscreen prevention is enabled
    if (settings::g_developerTabSettings.prevent_fullscreen.GetValue()) {
        // Prevent window style changes that enable fullscreen
        if (nIndex == GWL_STYLE) {
            // Remove fullscreen-enabling styles
            if (dwNewLong & WS_POPUP) {
                LogInfo("SetWindowLongPtrA blocked WS_POPUP style - forcing windowed style");
                dwNewLong &= ~WS_POPUP;
                dwNewLong |= WS_OVERLAPPEDWINDOW;
            }
        }
    }

    return SetWindowLongPtrA_Original(hWnd, nIndex, dwNewLong);
}

LONG_PTR WINAPI SetWindowLongPtrW_Detour(HWND hWnd, int nIndex, LONG_PTR dwNewLong) {
    g_display_settings_hook_counters[DISPLAY_SETTINGS_HOOK_SETWINDOWLONGPTRW].fetch_add(1);
    g_display_settings_hook_total_count.fetch_add(1);

    // Check if fullscreen prevention is enabled
    if (settings::g_developerTabSettings.prevent_fullscreen.GetValue()) {
        // Prevent window style changes that enable fullscreen
        if (nIndex == GWL_STYLE) {
            // Remove fullscreen-enabling styles
            if (dwNewLong & WS_POPUP) {
                LogInfo("SetWindowLongPtrW blocked WS_POPUP style - forcing windowed style");
                dwNewLong &= ~WS_POPUP;
                dwNewLong |= WS_OVERLAPPEDWINDOW;
            }
        }
    }

    return SetWindowLongPtrW_Original(hWnd, nIndex, dwNewLong);
}

// Hook installation function
bool InstallDisplaySettingsHooks() {
    if (g_display_settings_hooks_installed.load()) {
        LogInfo("Display settings hooks already installed");
        return true;
    }

    if (g_shutdown.load()) {
        LogInfo("Display settings hooks installation skipped - shutdown in progress");
        return false;
    }

    LogInfo("Installing display settings hooks...");

    // Hook ChangeDisplaySettingsA
    if (!CreateAndEnableHook(ChangeDisplaySettingsA, ChangeDisplaySettingsA_Detour, (LPVOID*)&ChangeDisplaySettingsA_Original, "ChangeDisplaySettingsA")) {
        LogError("Failed to create and enable ChangeDisplaySettingsA hook");
        return false;
    }

    // Hook ChangeDisplaySettingsW
    if (!CreateAndEnableHook(ChangeDisplaySettingsW, ChangeDisplaySettingsW_Detour, (LPVOID*)&ChangeDisplaySettingsW_Original, "ChangeDisplaySettingsW")) {
        LogError("Failed to create and enable ChangeDisplaySettingsW hook");
        return false;
    }

    // Hook ChangeDisplaySettingsExA
    if (!CreateAndEnableHook(ChangeDisplaySettingsExA, ChangeDisplaySettingsExA_Detour, (LPVOID*)&ChangeDisplaySettingsExA_Original, "ChangeDisplaySettingsExA")) {
        LogError("Failed to create and enable ChangeDisplaySettingsExA hook");
        return false;
    }

    // Hook ChangeDisplaySettingsExW
    if (!CreateAndEnableHook(ChangeDisplaySettingsExW, ChangeDisplaySettingsExW_Detour, (LPVOID*)&ChangeDisplaySettingsExW_Original, "ChangeDisplaySettingsExW")) {
        LogError("Failed to create and enable ChangeDisplaySettingsExW hook");
        return false;
    }

    // Hook window management functions for OpenGL fullscreen prevention
    if (!CreateAndEnableHook(SetWindowPos, SetWindowPos_Detour, (LPVOID*)&SetWindowPos_Original, "SetWindowPos")) {
        LogError("Failed to create and enable SetWindowPos hook");
        return false;
    }

    if (!CreateAndEnableHook(ShowWindow, ShowWindow_Detour, (LPVOID*)&ShowWindow_Original, "ShowWindow")) {
        LogError("Failed to create and enable ShowWindow hook");
        return false;
    }

    if (!CreateAndEnableHook(SetWindowLongA, SetWindowLongA_Detour, (LPVOID*)&SetWindowLongA_Original, "SetWindowLongA")) {
        LogError("Failed to create and enable SetWindowLongA hook");
        return false;
    }

    if (!CreateAndEnableHook(SetWindowLongW, SetWindowLongW_Detour, (LPVOID*)&SetWindowLongW_Original, "SetWindowLongW")) {
        LogError("Failed to create and enable SetWindowLongW hook");
        return false;
    }

    if (!CreateAndEnableHook(SetWindowLongPtrA, SetWindowLongPtrA_Detour, (LPVOID*)&SetWindowLongPtrA_Original, "SetWindowLongPtrA")) {
        LogError("Failed to create and enable SetWindowLongPtrA hook");
        return false;
    }

    if (!CreateAndEnableHook(SetWindowLongPtrW, SetWindowLongPtrW_Detour, (LPVOID*)&SetWindowLongPtrW_Original, "SetWindowLongPtrW")) {
        LogError("Failed to create and enable SetWindowLongPtrW hook");
        return false;
    }

    g_display_settings_hooks_installed.store(true);
    LogInfo("Display settings hooks installed successfully");
    return true;
}

// Hook uninstallation function
void UninstallDisplaySettingsHooks() {
    if (!g_display_settings_hooks_installed.load()) {
        return;
    }

    LogInfo("Uninstalling display settings hooks...");

    // Disable hooks
    if (ChangeDisplaySettingsA_Original) {
        MH_DisableHook(ChangeDisplaySettingsA);
        ChangeDisplaySettingsA_Original = nullptr;
    }

    if (ChangeDisplaySettingsW_Original) {
        MH_DisableHook(ChangeDisplaySettingsW);
        ChangeDisplaySettingsW_Original = nullptr;
    }

    if (ChangeDisplaySettingsExA_Original) {
        MH_DisableHook(ChangeDisplaySettingsExA);
        ChangeDisplaySettingsExA_Original = nullptr;
    }

    if (ChangeDisplaySettingsExW_Original) {
        MH_DisableHook(ChangeDisplaySettingsExW);
        ChangeDisplaySettingsExW_Original = nullptr;
    }

    // Disable window management hooks
    if (SetWindowPos_Original) {
        MH_DisableHook(SetWindowPos);
        SetWindowPos_Original = nullptr;
    }

    if (ShowWindow_Original) {
        MH_DisableHook(ShowWindow);
        ShowWindow_Original = nullptr;
    }

    if (SetWindowLongA_Original) {
        MH_DisableHook(SetWindowLongA);
        SetWindowLongA_Original = nullptr;
    }

    if (SetWindowLongW_Original) {
        MH_DisableHook(SetWindowLongW);
        SetWindowLongW_Original = nullptr;
    }

    if (SetWindowLongPtrA_Original) {
        MH_DisableHook(SetWindowLongPtrA);
        SetWindowLongPtrA_Original = nullptr;
    }

    if (SetWindowLongPtrW_Original) {
        MH_DisableHook(SetWindowLongPtrW);
        SetWindowLongPtrW_Original = nullptr;
    }

    g_display_settings_hooks_installed.store(false);
    LogInfo("Display settings hooks uninstalled");
}

// Check if hooks are installed
bool AreDisplaySettingsHooksInstalled() {
    return g_display_settings_hooks_installed.load();
}
