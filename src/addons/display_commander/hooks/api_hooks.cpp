#include "api_hooks.hpp"
#include "../utils/general_utils.hpp"
#include "dxgi/dxgi_present_hooks.hpp"
#include "globals.hpp"
#include "loadlibrary_hooks.hpp"
#include "opengl_hooks.hpp"
#include "process_exit_hooks.hpp"
#include "sleep_hooks.hpp"
#include "timeslowdown_hooks.hpp"
#include "windows_gaming_input_hooks.hpp"
#include "windows_hooks/windows_message_hooks.hpp"
#include "dinput_hooks.hpp"
#include "display_settings_hooks.hpp"
#include <MinHook.h>

// External reference to screensaver mode setting
extern std::atomic<ScreensaverMode> s_screensaver_mode;

namespace display_commanderhooks {

// Original function pointers
GetFocus_pfn GetFocus_Original = nullptr;
GetForegroundWindow_pfn GetForegroundWindow_Original = nullptr;
GetActiveWindow_pfn GetActiveWindow_Original = nullptr;
GetGUIThreadInfo_pfn GetGUIThreadInfo_Original = nullptr;
SetThreadExecutionState_pfn SetThreadExecutionState_Original = nullptr;
CreateDXGIFactory_pfn CreateDXGIFactory_Original = nullptr;
CreateDXGIFactory1_pfn CreateDXGIFactory1_Original = nullptr;

// Hook state
static std::atomic<bool> g_api_hooks_installed{false};

// Get the game window handle (we'll need to track this)
static HWND g_game_window = nullptr;

HWND GetGameWindow() { return g_game_window; }

bool IsGameWindow(HWND hwnd) {
    if (hwnd == nullptr)
        return false;
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
        //    LogInfo("GetForegroundWindow_Detour: Returning game window due to continue rendering - HWND: 0x%p",
        //    g_game_window);
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
        BOOL result =
            GetGUIThreadInfo_Original ? GetGUIThreadInfo_Original(idThread, pgui) : GetGUIThreadInfo(idThread, pgui);

        if (result && pgui) {
            // Modify the thread info to show game window as active
            DWORD dwPid = 0;
            DWORD dwTid = GetWindowThreadProcessId(g_game_window, &dwPid);

            if (idThread == dwTid || idThread == 0) {
                // Set the game window as active and focused
                pgui->hwndActive = g_game_window;
                pgui->hwndFocus = g_game_window;
                pgui->hwndCapture = nullptr;     // Clear capture to prevent issues
                pgui->hwndCaret = g_game_window; // Set caret to game window

                // Set appropriate flags (using standard Windows constants)
                pgui->flags = 0x00000001 | 0x00000002; // GTI_CARETBLINKING | GTI_CARETSHOWN

                LogInfo("GetGUIThreadInfo_Detour: Modified thread info to show game window as active - HWND: 0x%p, "
                        "Thread: %lu",
                        g_game_window, idThread);
            }
        }

        return result;
    }

    // Call original function
    return GetGUIThreadInfo_Original ? GetGUIThreadInfo_Original(idThread, pgui) : GetGUIThreadInfo(idThread, pgui);
}

// Hooked SetThreadExecutionState function
EXECUTION_STATE WINAPI SetThreadExecutionState_Detour(EXECUTION_STATE esFlags) {
    // Check screensaver mode setting
    ScreensaverMode screensaver_mode = s_screensaver_mode.load();

    // If screensaver mode is DisableWhenFocused or Disable, ignore all calls
    if (screensaver_mode == ScreensaverMode::kDisableWhenFocused || screensaver_mode == ScreensaverMode::kDisable) {
        return 0x0; // Block game's attempt to control execution state
    }

    // Call original function for kDefault mode
    return SetThreadExecutionState_Original ? SetThreadExecutionState_Original(esFlags)
                                            : SetThreadExecutionState(esFlags);
}

// Hooked CreateDXGIFactory function
HRESULT WINAPI CreateDXGIFactory_Detour(REFIID riid, void **ppFactory) {
    // Increment counter
    g_dxgi_factory_event_counters[DXGI_FACTORY_EVENT_CREATEFACTORY].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Call original function
    HRESULT hr = CreateDXGIFactory_Original ? CreateDXGIFactory_Original(riid, ppFactory)
                                            : CreateDXGIFactory(riid, ppFactory);

    // If successful and we got a factory, hook it
    if (SUCCEEDED(hr) && ppFactory != nullptr && *ppFactory != nullptr) {
        IDXGIFactory *factory = static_cast<IDXGIFactory *>(*ppFactory);
        LogInfo("CreateDXGIFactory succeeded, hooking factory: 0x%p", factory);
        display_commanderhooks::dxgi::HookFactory(factory);// crashes Returnal
    }

    return hr;
}

// Hooked CreateDXGIFactory1 function
HRESULT WINAPI CreateDXGIFactory1_Detour(REFIID riid, void **ppFactory) {
    // Increment counter
    g_dxgi_factory_event_counters[DXGI_FACTORY_EVENT_CREATEFACTORY1].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Call original function
    HRESULT hr = CreateDXGIFactory1_Original ? CreateDXGIFactory1_Original(riid, ppFactory)
                                             : CreateDXGIFactory1(riid, ppFactory);

    // If successful and we got a factory, hook it
    if (SUCCEEDED(hr) && ppFactory != nullptr && *ppFactory != nullptr) {
        IDXGIFactory *factory = static_cast<IDXGIFactory *>(*ppFactory);
        LogInfo("CreateDXGIFactory1 succeeded, hooking factory: 0x%p", factory);
        display_commanderhooks::dxgi::HookFactory(factory); // crashes Returnal
    }

    return hr;
}

bool InstallDxgiHooks() {
    static bool dxgi_hooks_installed = false;
    if (dxgi_hooks_installed) {
        LogInfo("DXGI hooks already installed");
        return true;
    }
    dxgi_hooks_installed = true;


    // Get dxgi.dll module handle
    HMODULE dxgi_module = GetModuleHandleW(L"dxgi.dll");
    if (dxgi_module == nullptr) {
        LogError("Failed to get dxgi.dll module handle");
        return false;
    }

    // Hook CreateDXGIFactory - try both system and ReShade versions
    auto CreateDXGIFactory_sys = reinterpret_cast<decltype(&CreateDXGIFactory)>(GetProcAddress(dxgi_module, "CreateDXGIFactory"));
    if (CreateDXGIFactory_sys != nullptr) {
        if (!CreateAndEnableHook(CreateDXGIFactory_sys, CreateDXGIFactory_Detour, reinterpret_cast<LPVOID *>(&CreateDXGIFactory_Original), "CreateDXGIFactory")) {
            LogError("Failed to create and enable CreateDXGIFactory system hook");
            return false;
        }
        LogInfo("CreateDXGIFactory system hook created successfully");
    } else {
        LogWarn("Failed to get CreateDXGIFactory system address, trying ReShade version");
        if (!CreateAndEnableHook(CreateDXGIFactory, CreateDXGIFactory_Detour, reinterpret_cast<LPVOID *>(&CreateDXGIFactory_Original), "CreateDXGIFactory")) {
            LogError("Failed to create and enable CreateDXGIFactory ReShade hook");
            return false;
        }
        LogInfo("CreateDXGIFactory ReShade hook created successfully");
    }

    // Hook CreateDXGIFactory1 - try both system and ReShade versions
    auto CreateDXGIFactory1_sys = reinterpret_cast<decltype(&CreateDXGIFactory1)>(GetProcAddress(dxgi_module, "CreateDXGIFactory1"));
    if (CreateDXGIFactory1_sys != nullptr) {
        if (!CreateAndEnableHook(CreateDXGIFactory1_sys, CreateDXGIFactory1_Detour, reinterpret_cast<LPVOID *>(&CreateDXGIFactory1_Original), "CreateDXGIFactory1")) {
            LogError("Failed to create and enable CreateDXGIFactory1 system hook");
            return false;
        }
        LogInfo("CreateDXGIFactory1 system hook created successfully");
    } else {
        LogWarn("Failed to get CreateDXGIFactory1 system address, trying ReShade version");
        if (!CreateAndEnableHook(CreateDXGIFactory1, CreateDXGIFactory1_Detour, reinterpret_cast<LPVOID *>(&CreateDXGIFactory1_Original), "CreateDXGIFactory1")) {
            LogError("Failed to create and enable CreateDXGIFactory1 ReShade hook");
            return false;
        }
        LogInfo("CreateDXGIFactory1 ReShade hook created successfully");
    }

    LogInfo("DXGI hooks installed successfully");
    return true;
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
    if (!CreateAndEnableHook(GetFocus, GetFocus_Detour, reinterpret_cast<LPVOID *>(&GetFocus_Original), "GetFocus")) {
        LogError("Failed to create and enable GetFocus hook");
    }

    // Hook GetForegroundWindow
    if (!CreateAndEnableHook(GetForegroundWindow, GetForegroundWindow_Detour, reinterpret_cast<LPVOID *>(&GetForegroundWindow_Original), "GetForegroundWindow")) {
        LogError("Failed to create and enable GetForegroundWindow hook");
    }

    // Hook GetActiveWindow
    if (!CreateAndEnableHook(GetActiveWindow, GetActiveWindow_Detour, reinterpret_cast<LPVOID *>(&GetActiveWindow_Original), "GetActiveWindow")) {
        LogError("Failed to create and enable GetActiveWindow hook");
    }

    // Hook GetGUIThreadInfo
    if (!CreateAndEnableHook(GetGUIThreadInfo, GetGUIThreadInfo_Detour, reinterpret_cast<LPVOID *>(&GetGUIThreadInfo_Original), "GetGUIThreadInfo")) {
        LogError("Failed to create and enable GetGUIThreadInfo hook");
    }

    // Hook SetThreadExecutionState
    if (!CreateAndEnableHook(SetThreadExecutionState, SetThreadExecutionState_Detour, reinterpret_cast<LPVOID *>(&SetThreadExecutionState_Original), "SetThreadExecutionState")) {
        LogError("Failed to create and enable SetThreadExecutionState hook");
    }

    // todo: move to loadlibrary hooks
    // Install Windows message hooks


    // ### SAME LIBRARY ###
    if (!InstallWindowsMessageHooks()) {
        LogError("Failed to install Windows message hooks");
    }

    // Install timeslowdown hooks
    if (!InstallTimeslowdownHooks()) {
        LogError("Failed to install timeslowdown hooks");
    }

    // Install process exit hooks
    if (!InstallProcessExitHooks()) {
        LogError("Failed to install process exit hooks");
    }

    if (!InstallSleepHooks()) {
        LogError("Failed to install sleep hooks");
    }

    // Install LoadLibrary hooks
    if (!InstallLoadLibraryHooks()) {
        LogError("Failed to install LoadLibrary hooks");
    }

    // Install DirectInput hooks
    if (!InstallDirectInputHooks()) {
        LogError("Failed to install DirectInput hooks");
    }

    // Install OpenGL hooks
    if (!InstallOpenGLHooks()) {
        LogError("Failed to install OpenGL hooks");
    }

    // Install display settings hooks
    if (!InstallDisplaySettingsHooks()) {
        LogError("Failed to install display settings hooks");
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

    // Uninstall Windows.Gaming.Input hooks
    UninstallWindowsGamingInputHooks();

    // Uninstall LoadLibrary hooks
    UninstallLoadLibraryHooks();

    // Uninstall DirectInput hooks
    UninstallDirectInputHooks();

    // Uninstall OpenGL hooks
    UninstallOpenGLHooks();

    // Uninstall Windows message hooks
    UninstallWindowsMessageHooks();


    // Uninstall sleep hooks
    UninstallSleepHooks();

    // Uninstall timeslowdown hooks
    UninstallTimeslowdownHooks();

    // Uninstall process exit hooks
    UninstallProcessExitHooks();

    // NVAPI hooks are uninstalled via LoadLibrary hooks cleanup

    // Disable all hooks
    MH_DisableHook(MH_ALL_HOOKS);

    // Remove hooks
    MH_RemoveHook(GetFocus);
    MH_RemoveHook(GetForegroundWindow);
    MH_RemoveHook(GetActiveWindow);
    MH_RemoveHook(GetGUIThreadInfo);
    MH_RemoveHook(SetThreadExecutionState);
    MH_RemoveHook(CreateDXGIFactory);
    MH_RemoveHook(CreateDXGIFactory1);

    // Clean up
    GetFocus_Original = nullptr;
    GetForegroundWindow_Original = nullptr;
    GetActiveWindow_Original = nullptr;
    GetGUIThreadInfo_Original = nullptr;
    SetThreadExecutionState_Original = nullptr;
    CreateDXGIFactory_Original = nullptr;
    CreateDXGIFactory1_Original = nullptr;

    g_api_hooks_installed.store(false);
    LogInfo("API hooks uninstalled successfully");
}
// Function to set the game window (should be called when we detect the game window)
void SetGameWindow(HWND hwnd) {
    g_game_window = hwnd;
    LogInfo("Game window set for API hooks - HWND: 0x%p", hwnd);
}

} // namespace display_commanderhooks
