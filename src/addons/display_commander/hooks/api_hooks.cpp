#include "api_hooks.hpp"
#include "xinput_hooks.hpp"
#include "windows_gaming_input_hooks.hpp"
#include "loadlibrary_hooks.hpp"
#include "directinput_hooks.hpp"
#include "windows_hooks/windows_message_hooks.hpp"
#include "sleep_hooks.hpp"
#include "timeslowdown_hooks.hpp"
#include "process_exit_hooks.hpp"
#include "globals.hpp"
#include "../utils.hpp"
#include <MinHook.h>

namespace renodx::hooks {

// Original function pointers
GetFocus_pfn GetFocus_Original = nullptr;
GetForegroundWindow_pfn GetForegroundWindow_Original = nullptr;
GetActiveWindow_pfn GetActiveWindow_Original = nullptr;
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
    if (true) {
        return g_game_window;
    }

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

// Hooked GetActiveWindow function
HWND WINAPI GetActiveWindow_Detour() {
    if (s_continue_rendering.load() && g_game_window != nullptr && IsWindow(g_game_window)) {
        // Return the game window even when it's not the active window
        // Check if we're in the same thread as the game window
        DWORD dwPid = 0;
        DWORD dwTid = GetWindowThreadProcessId(g_game_window, &dwPid);

        if (GetCurrentThreadId() == dwTid) {
            // We're in the same thread as the game window, return it
            return g_game_window;
        }

        // For other threads, check if the current process owns the game window
        if (GetCurrentProcessId() == dwPid) {
            return g_game_window;
        }
        return g_game_window;
    }

    // Call original function
    return GetActiveWindow_Original ? GetActiveWindow_Original() : GetActiveWindow();
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

            if (idThread == dwTid || idThread == 0) {
                // Set the game window as active and focused
                pgui->hwndActive = g_game_window;
                pgui->hwndFocus = g_game_window;
                pgui->hwndCapture = nullptr; // Clear capture to prevent issues
                pgui->hwndCaret = g_game_window; // Set caret to game window

                // Set appropriate flags (using standard Windows constants)
                pgui->flags = 0x00000001 | 0x00000002; // GTI_CARETBLINKING | GTI_CARETSHOWN

                LogInfo("GetGUIThreadInfo_Detour: Modified thread info to show game window as active - HWND: 0x%p, Thread: %lu", g_game_window, idThread);
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

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for API hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with API hooks");
    } else {
        LogInfo("MinHook initialized successfully for API hooks");
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

    // Hook GetActiveWindow
    if (MH_CreateHook(GetActiveWindow, GetActiveWindow_Detour, (LPVOID*)&GetActiveWindow_Original) != MH_OK) {
        LogError("Failed to create GetActiveWindow hook");
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

    // Install XInput hooks
    if (!InstallXInputHooks()) {
        LogError("Failed to install XInput hooks");
        return false;
    }

    // Install Windows.Gaming.Input hooks
    if (!InstallWindowsGamingInputHooks()) {
        LogError("Failed to install Windows.Gaming.Input hooks");
        return false;
    }

    // Install LoadLibrary hooks
    if (!InstallLoadLibraryHooks()) {
        LogError("Failed to install LoadLibrary hooks");
        return false;
    }

    // Install Windows message hooks
    if (!InstallWindowsMessageHooks()) {
        LogError("Failed to install Windows message hooks");
        return false;
    }

    // Install DirectInput hooks
    if (!InstallDirectInputHooks()) {
        LogError("Failed to install DirectInput hooks");
        return false;
    }

    // Install sleep hooks
    if (!InstallSleepHooks()) {
        LogError("Failed to install sleep hooks");
        return false;
    }

    // Install timeslowdown hooks
    if (!InstallTimeslowdownHooks()) {
        LogError("Failed to install timeslowdown hooks");
        return false;
    }

    // Install process exit hooks
    if (!InstallProcessExitHooks()) {
        LogError("Failed to install process exit hooks");
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

    // Uninstall XInput hooks first
    UninstallXInputHooks();

    // Uninstall Windows.Gaming.Input hooks
    UninstallWindowsGamingInputHooks();

    // Uninstall LoadLibrary hooks
    UninstallLoadLibraryHooks();

    // Uninstall Windows message hooks
    UninstallWindowsMessageHooks();

    // Uninstall DirectInput hooks
    UninstallDirectInputHooks();

    // Uninstall sleep hooks
    UninstallSleepHooks();

    // Uninstall timeslowdown hooks
    UninstallTimeslowdownHooks();

    // Uninstall process exit hooks
    UninstallProcessExitHooks();

    // Disable all hooks
    MH_DisableHook(MH_ALL_HOOKS);

    // Remove hooks
    MH_RemoveHook(GetFocus);
    MH_RemoveHook(GetForegroundWindow);
    MH_RemoveHook(GetActiveWindow);
    MH_RemoveHook(GetGUIThreadInfo);

    // Clean up
    GetFocus_Original = nullptr;
    GetForegroundWindow_Original = nullptr;
    GetActiveWindow_Original = nullptr;
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
