#include "api_hooks.hpp"
#include "globals.hpp"
#include "../utils.hpp"
#include <MinHook.h>

namespace renodx::hooks {

// Original function pointers
GetFocus_pfn GetFocus_Original = nullptr;
GetForegroundWindow_pfn GetForegroundWindow_Original = nullptr;
GetGUIThreadInfo_pfn GetGUIThreadInfo_Original = nullptr;

// Hook state
static std::atomic<bool> g_api_hooks_installed{false};

// Get the game window handle (we'll need to track this)
static HWND g_game_window = nullptr;

HWND GetGameWindow() {
    return g_game_window;
}

bool IsGameWindow(HWND hwnd) {
    if (hwnd == nullptr) return false;
    return hwnd == g_game_window || IsChild(g_game_window, hwnd) || IsChild(hwnd, g_game_window);
}

// Hooked GetFocus function
HWND WINAPI GetFocus_Detour() {
    if (s_continue_rendering.load() && g_game_window != nullptr && IsWindow(g_game_window)) {
        // Return the game window even when it doesn't have focus
     //   LogInfo("GetFocus_Detour: Returning game window due to continue rendering - HWND: 0x%p", g_game_window);
        return g_game_window;
    }

    // Call original function
    return GetFocus_Original ? GetFocus_Original() : GetFocus();
}

// Hooked GetForegroundWindow function
HWND WINAPI GetForegroundWindow_Detour() {
    if (s_continue_rendering.load() && g_game_window != nullptr && IsWindow(g_game_window)) {
        // Return the game window even when it's not in foreground
    //    LogInfo("GetForegroundWindow_Detour: Returning game window due to continue rendering - HWND: 0x%p", g_game_window);
        return g_game_window;
    }

    // Call original function
    return GetForegroundWindow_Original ? GetForegroundWindow_Original() : GetForegroundWindow();
}

// Hooked GetGUIThreadInfo function
BOOL WINAPI GetGUIThreadInfo_Detour(DWORD idThread, PGUITHREADINFO pgui) {
    if (s_continue_rendering.load() && g_game_window != nullptr && IsWindow(g_game_window)) {
        // Call original function first
        BOOL result = GetGUIThreadInfo_Original ?
            GetGUIThreadInfo_Original(idThread, pgui) :
            GetGUIThreadInfo(idThread, pgui);

        if (result && pgui) {
            // Modify the thread info to show game window as active
            DWORD dwPid = 0;
            DWORD dwTid = GetWindowThreadProcessId(g_game_window, &dwPid);

            if (idThread == dwTid) {
                pgui->hwndActive = g_game_window;
                pgui->hwndFocus = g_game_window;
           //     LogInfo("GetGUIThreadInfo_Detour: Modified thread info to show game window as active - HWND: 0x%p", g_game_window);
            }
        }

        return result;
    }

    // Call original function
    return GetGUIThreadInfo_Original ?
        GetGUIThreadInfo_Original(idThread, pgui) :
        GetGUIThreadInfo(idThread, pgui);
}

bool InstallApiHooks() {
    if (g_api_hooks_installed.load()) {
        LogInfo("API hooks already installed");
        return true;
    }

    // Initialize MinHook
    if (MH_Initialize() != MH_OK) {
        LogError("Failed to initialize MinHook for API hooks");
        return false;
    }

    // Hook GetFocus
    if (MH_CreateHook(GetFocus, GetFocus_Detour, (LPVOID*)&GetFocus_Original) != MH_OK) {
        LogError("Failed to create GetFocus hook");
        return false;
    }

    // Hook GetForegroundWindow
    if (MH_CreateHook(GetForegroundWindow, GetForegroundWindow_Detour, (LPVOID*)&GetForegroundWindow_Original) != MH_OK) {
        LogError("Failed to create GetForegroundWindow hook");
        return false;
    }

    // Hook GetGUIThreadInfo
    if (MH_CreateHook(GetGUIThreadInfo, GetGUIThreadInfo_Detour, (LPVOID*)&GetGUIThreadInfo_Original) != MH_OK) {
        LogError("Failed to create GetGUIThreadInfo hook");
        return false;
    }

    // Enable all hooks
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        LogError("Failed to enable API hooks");
        return false;
    }

    g_api_hooks_installed.store(true);
    LogInfo("API hooks installed successfully");

    // Debug: Show current continue rendering state
    bool current_state = s_continue_rendering.load();
    LogInfo("API hooks installed - continue_rendering state: %s", current_state ? "enabled" : "disabled");

    return true;
}

void UninstallApiHooks() {
    if (!g_api_hooks_installed.load()) {
        LogInfo("API hooks not installed");
        return;
    }

    // Disable all hooks
    MH_DisableHook(MH_ALL_HOOKS);

    // Remove hooks
    MH_RemoveHook(GetFocus);
    MH_RemoveHook(GetForegroundWindow);
    MH_RemoveHook(GetGUIThreadInfo);

    // Clean up
    GetFocus_Original = nullptr;
    GetForegroundWindow_Original = nullptr;
    GetGUIThreadInfo_Original = nullptr;

    g_api_hooks_installed.store(false);
    LogInfo("API hooks uninstalled successfully");
}

bool AreApiHooksInstalled() {
    return g_api_hooks_installed.load();
}

// Function to set the game window (should be called when we detect the game window)
void SetGameWindow(HWND hwnd) {
    g_game_window = hwnd;
    LogInfo("Game window set for API hooks - HWND: 0x%p", hwnd);
}

} // namespace renodx::hooks
