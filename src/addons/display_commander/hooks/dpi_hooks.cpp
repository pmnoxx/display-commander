#include "dpi_hooks.hpp"
#include "../settings/developer_tab_settings.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"

#include <MinHook.h>

namespace display_commanderhooks::dpi {

// Original function pointers
GetDpiForSystem_pfn GetDpiForSystem_Original = nullptr;
GetDpiForWindow_pfn GetDpiForWindow_Original = nullptr;
GetSystemDpiForProcess_pfn GetSystemDpiForProcess_Original = nullptr;
GetSystemMetricsForDpi_pfn GetSystemMetricsForDpi_Original = nullptr;
AdjustWindowRectExForDpi_pfn AdjustWindowRectExForDpi_Original = nullptr;
EnableNonClientDpiScaling_pfn EnableNonClientDpiScaling_Original = nullptr;
SystemParametersInfoForDpi_pfn SystemParametersInfoForDpi_Original = nullptr;
SetThreadDpiHostingBehavior_pfn SetThreadDpiHostingBehavior_Original = nullptr;
SetThreadDpiAwarenessContext_pfn SetThreadDpiAwarenessContext_Original = nullptr;
SetProcessDpiAwarenessContext_pfn SetProcessDpiAwarenessContext_Original = nullptr;

// Helper to set DPI awareness context before calling original API
static void EnsureDpiAwarenessContext() {
    if (!settings::g_developerTabSettings.disable_dpi_scaling.GetValue()) {
        return;
    }

    // Set per-monitor awareness context to ensure consistent behavior
    if (SetThreadDpiAwarenessContext_Original != nullptr) {
        SetThreadDpiAwarenessContext_Original(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
    if (SetProcessDpiAwarenessContext_Original != nullptr) {
        SetProcessDpiAwarenessContext_Original(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

UINT WINAPI GetDpiForSystem_Detour() {
    EnsureDpiAwarenessContext();
    return GetDpiForSystem_Original ? GetDpiForSystem_Original() : USER_DEFAULT_SCREEN_DPI;
}

UINT WINAPI GetDpiForWindow_Detour(HWND hwnd) {
    EnsureDpiAwarenessContext();
    return GetDpiForWindow_Original ? GetDpiForWindow_Original(hwnd) : USER_DEFAULT_SCREEN_DPI;
}

UINT WINAPI GetSystemDpiForProcess_Detour(HANDLE hProcess) {
    EnsureDpiAwarenessContext();
    return GetSystemDpiForProcess_Original ? GetSystemDpiForProcess_Original(hProcess) : USER_DEFAULT_SCREEN_DPI;
}

int WINAPI GetSystemMetricsForDpi_Detour(int nIndex, UINT dpi) {
    EnsureDpiAwarenessContext();
    return GetSystemMetricsForDpi_Original ? GetSystemMetricsForDpi_Original(nIndex, dpi) : 0;
}

BOOL WINAPI AdjustWindowRectExForDpi_Detour(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle, UINT dpi) {
    EnsureDpiAwarenessContext();
    return AdjustWindowRectExForDpi_Original ? AdjustWindowRectExForDpi_Original(lpRect, dwStyle, bMenu, dwExStyle, dpi)
                                             : FALSE;
}

BOOL WINAPI EnableNonClientDpiScaling_Detour(HWND hwnd) {
    EnsureDpiAwarenessContext();
    return EnableNonClientDpiScaling_Original ? EnableNonClientDpiScaling_Original(hwnd) : FALSE;
}

BOOL WINAPI SystemParametersInfoForDpi_Detour(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni, UINT dpi) {
    EnsureDpiAwarenessContext();
    return SystemParametersInfoForDpi_Original
               ? SystemParametersInfoForDpi_Original(uiAction, uiParam, pvParam, fWinIni, dpi)
               : FALSE;
}

DPI_HOSTING_BEHAVIOR WINAPI SetThreadDpiHostingBehavior_Detour(DPI_HOSTING_BEHAVIOR value) {
    EnsureDpiAwarenessContext();
    return SetThreadDpiHostingBehavior_Original ? SetThreadDpiHostingBehavior_Original(value)
                                                : DPI_HOSTING_BEHAVIOR_INVALID;
}

DPI_AWARENESS_CONTEXT WINAPI SetThreadDpiAwarenessContext_Detour(DPI_AWARENESS_CONTEXT dpiContext) {
    // Pass through - don't interfere with app's own DPI awareness setting
    return SetThreadDpiAwarenessContext_Original ? SetThreadDpiAwarenessContext_Original(dpiContext)
                                                 : DPI_AWARENESS_CONTEXT_UNAWARE;
}

BOOL WINAPI SetProcessDpiAwarenessContext_Detour(DPI_AWARENESS_CONTEXT value) {
    // Pass through - don't interfere with app's own DPI awareness setting
    return SetProcessDpiAwarenessContext_Original ? SetProcessDpiAwarenessContext_Original(value) : FALSE;
}

// CreateAndEnableHook is defined in general_utils.hpp

bool InstallDpiHooks() {
    HMODULE user32_module = GetModuleHandleW(L"user32.dll");
    if (user32_module == nullptr) {
        LogError("Failed to get user32.dll module handle for DPI hooks");
        return false;
    }

    // Get function addresses
    auto GetDpiForSystem_sys = reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "GetDpiForSystem"));
    auto GetDpiForWindow_sys = reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "GetDpiForWindow"));
    auto GetSystemDpiForProcess_sys = reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "GetSystemDpiForProcess"));
    auto GetSystemMetricsForDpi_sys = reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "GetSystemMetricsForDpi"));
    auto AdjustWindowRectExForDpi_sys =
        reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "AdjustWindowRectExForDpi"));
    auto EnableNonClientDpiScaling_sys =
        reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "EnableNonClientDpiScaling"));
    auto SystemParametersInfoForDpi_sys =
        reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "SystemParametersInfoForDpi"));
    auto SetThreadDpiHostingBehavior_sys =
        reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "SetThreadDpiHostingBehavior"));
    auto SetThreadDpiAwarenessContext_sys =
        reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "SetThreadDpiAwarenessContext"));
    auto SetProcessDpiAwarenessContext_sys =
        reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "SetProcessDpiAwarenessContext"));

    bool success = true;

    // Install hooks (only if functions exist - some are Windows 10+ only)
    if (GetDpiForSystem_sys != nullptr) {
        success &= CreateAndEnableHook(GetDpiForSystem_sys, GetDpiForSystem_Detour,
                                       reinterpret_cast<LPVOID*>(&GetDpiForSystem_Original), "GetDpiForSystem");
    }

    if (GetDpiForWindow_sys != nullptr) {
        success &= CreateAndEnableHook(GetDpiForWindow_sys, GetDpiForWindow_Detour,
                                       reinterpret_cast<LPVOID*>(&GetDpiForWindow_Original), "GetDpiForWindow");
    }

    if (GetSystemDpiForProcess_sys != nullptr) {
        success &=
            CreateAndEnableHook(GetSystemDpiForProcess_sys, GetSystemDpiForProcess_Detour,
                                reinterpret_cast<LPVOID*>(&GetSystemDpiForProcess_Original), "GetSystemDpiForProcess");
    }

    if (GetSystemMetricsForDpi_sys != nullptr) {
        success &=
            CreateAndEnableHook(GetSystemMetricsForDpi_sys, GetSystemMetricsForDpi_Detour,
                                reinterpret_cast<LPVOID*>(&GetSystemMetricsForDpi_Original), "GetSystemMetricsForDpi");
    }

    if (AdjustWindowRectExForDpi_sys != nullptr) {
        success &= CreateAndEnableHook(AdjustWindowRectExForDpi_sys, AdjustWindowRectExForDpi_Detour,
                                       reinterpret_cast<LPVOID*>(&AdjustWindowRectExForDpi_Original),
                                       "AdjustWindowRectExForDpi");
    }

    if (EnableNonClientDpiScaling_sys != nullptr) {
        success &= CreateAndEnableHook(EnableNonClientDpiScaling_sys, EnableNonClientDpiScaling_Detour,
                                       reinterpret_cast<LPVOID*>(&EnableNonClientDpiScaling_Original),
                                       "EnableNonClientDpiScaling");
    }

    if (SystemParametersInfoForDpi_sys != nullptr) {
        success &= CreateAndEnableHook(SystemParametersInfoForDpi_sys, SystemParametersInfoForDpi_Detour,
                                       reinterpret_cast<LPVOID*>(&SystemParametersInfoForDpi_Original),
                                       "SystemParametersInfoForDpi");
    }

    if (SetThreadDpiHostingBehavior_sys != nullptr) {
        success &= CreateAndEnableHook(SetThreadDpiHostingBehavior_sys, SetThreadDpiHostingBehavior_Detour,
                                       reinterpret_cast<LPVOID*>(&SetThreadDpiHostingBehavior_Original),
                                       "SetThreadDpiHostingBehavior");
    }

    if (SetThreadDpiAwarenessContext_sys != nullptr) {
        success &= CreateAndEnableHook(SetThreadDpiAwarenessContext_sys, SetThreadDpiAwarenessContext_Detour,
                                       reinterpret_cast<LPVOID*>(&SetThreadDpiAwarenessContext_Original),
                                       "SetThreadDpiAwarenessContext");
    }

    if (SetProcessDpiAwarenessContext_sys != nullptr) {
        success &= CreateAndEnableHook(SetProcessDpiAwarenessContext_sys, SetProcessDpiAwarenessContext_Detour,
                                       reinterpret_cast<LPVOID*>(&SetProcessDpiAwarenessContext_Original),
                                       "SetProcessDpiAwarenessContext");
    }

    if (success) {
        LogInfo("DPI hooks installed successfully");
    } else {
        LogError("Some DPI hooks failed to install");
    }

    return success;
}

void UninstallDpiHooks() {
    HMODULE user32_module = GetModuleHandleW(L"user32.dll");
    if (user32_module == nullptr) {
        return;
    }

    // Remove hooks
    auto GetDpiForSystem_sys = reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "GetDpiForSystem"));
    auto GetDpiForWindow_sys = reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "GetDpiForWindow"));
    auto GetSystemDpiForProcess_sys = reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "GetSystemDpiForProcess"));
    auto GetSystemMetricsForDpi_sys = reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "GetSystemMetricsForDpi"));
    auto AdjustWindowRectExForDpi_sys =
        reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "AdjustWindowRectExForDpi"));
    auto EnableNonClientDpiScaling_sys =
        reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "EnableNonClientDpiScaling"));
    auto SystemParametersInfoForDpi_sys =
        reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "SystemParametersInfoForDpi"));
    auto SetThreadDpiHostingBehavior_sys =
        reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "SetThreadDpiHostingBehavior"));
    auto SetThreadDpiAwarenessContext_sys =
        reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "SetThreadDpiAwarenessContext"));
    auto SetProcessDpiAwarenessContext_sys =
        reinterpret_cast<LPVOID>(GetProcAddress(user32_module, "SetProcessDpiAwarenessContext"));

    if (GetDpiForSystem_sys != nullptr) {
        MH_RemoveHook(GetDpiForSystem_sys);
    }
    if (GetDpiForWindow_sys != nullptr) {
        MH_RemoveHook(GetDpiForWindow_sys);
    }
    if (GetSystemDpiForProcess_sys != nullptr) {
        MH_RemoveHook(GetSystemDpiForProcess_sys);
    }
    if (GetSystemMetricsForDpi_sys != nullptr) {
        MH_RemoveHook(GetSystemMetricsForDpi_sys);
    }
    if (AdjustWindowRectExForDpi_sys != nullptr) {
        MH_RemoveHook(AdjustWindowRectExForDpi_sys);
    }
    if (EnableNonClientDpiScaling_sys != nullptr) {
        MH_RemoveHook(EnableNonClientDpiScaling_sys);
    }
    if (SystemParametersInfoForDpi_sys != nullptr) {
        MH_RemoveHook(SystemParametersInfoForDpi_sys);
    }
    if (SetThreadDpiHostingBehavior_sys != nullptr) {
        MH_RemoveHook(SetThreadDpiHostingBehavior_sys);
    }
    if (SetThreadDpiAwarenessContext_sys != nullptr) {
        MH_RemoveHook(SetThreadDpiAwarenessContext_sys);
    }
    if (SetProcessDpiAwarenessContext_sys != nullptr) {
        MH_RemoveHook(SetProcessDpiAwarenessContext_sys);
    }

    // Clear function pointers
    GetDpiForSystem_Original = nullptr;
    GetDpiForWindow_Original = nullptr;
    GetSystemDpiForProcess_Original = nullptr;
    GetSystemMetricsForDpi_Original = nullptr;
    AdjustWindowRectExForDpi_Original = nullptr;
    EnableNonClientDpiScaling_Original = nullptr;
    SystemParametersInfoForDpi_Original = nullptr;
    SetThreadDpiHostingBehavior_Original = nullptr;
    SetThreadDpiAwarenessContext_Original = nullptr;
    SetProcessDpiAwarenessContext_Original = nullptr;

    LogInfo("DPI hooks uninstalled");
}

}  // namespace display_commanderhooks::dpi
