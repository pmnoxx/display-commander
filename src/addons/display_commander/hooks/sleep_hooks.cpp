#include "sleep_hooks.hpp"
#include "windows_hooks/windows_message_hooks.hpp"
#include "../utils.hpp"
#include "../globals.hpp"
#include <MinHook.h>
#include <windows.h>

namespace renodx::hooks {

// Global sleep hook statistics
SleepHookStats g_sleep_hook_stats;

// Original function pointers
Sleep_pfn Sleep_Original = nullptr;
SleepEx_pfn SleepEx_Original = nullptr;
WaitForSingleObject_pfn WaitForSingleObject_Original = nullptr;
WaitForMultipleObjects_pfn WaitForMultipleObjects_Original = nullptr;

// Hooked Sleep function
void WINAPI Sleep_Detour(DWORD dwMilliseconds) {
    // Track total calls
    g_hook_stats[HOOK_Sleep].increment_total();

    DWORD modified_duration = dwMilliseconds;

    if (g_sleep_hook_enabled.load() && dwMilliseconds > 0) {
        // Apply sleep multiplier
        float multiplier = g_sleep_multiplier.load();
        if (multiplier > 0.0f) {
            modified_duration = static_cast<DWORD>(dwMilliseconds * multiplier);
        }

        // Apply min/max constraints
        DWORD min_duration = g_min_sleep_duration_ms.load();
        DWORD max_duration = g_max_sleep_duration_ms.load();

        if (min_duration > 0) {
            modified_duration = (modified_duration > min_duration) ? modified_duration : min_duration;
        }
        if (max_duration > 0) {
            modified_duration = (modified_duration < max_duration) ? modified_duration : max_duration;
        }

        // Track modified calls
        g_hook_stats[HOOK_Sleep].increment_unsuppressed();
        g_sleep_hook_stats.increment_modified();
        g_sleep_hook_stats.add_original_duration(dwMilliseconds);
        g_sleep_hook_stats.add_modified_duration(modified_duration);

        LogDebug("Sleep hook: " + std::to_string(dwMilliseconds) + " ms -> " + std::to_string(modified_duration) + " ms (multiplier: " + std::to_string(multiplier) + ")");
    } else {
        // Track unmodified calls
        g_hook_stats[HOOK_Sleep].increment_unsuppressed();
    }

    // Call original function with modified duration
    if (Sleep_Original) {
        Sleep_Original(modified_duration);
    } else {
        Sleep(modified_duration);
    }
}

// Hooked SleepEx function
DWORD WINAPI SleepEx_Detour(DWORD dwMilliseconds, BOOL bAlertable) {
    // Track total calls
    g_hook_stats[HOOK_SleepEx].increment_total();

    DWORD modified_duration = dwMilliseconds;

    if (g_sleep_hook_enabled.load() && dwMilliseconds > 0) {
        // Apply sleep multiplier
        float multiplier = g_sleep_multiplier.load();
        if (multiplier > 0.0f) {
            modified_duration = static_cast<DWORD>(dwMilliseconds * multiplier);
        }

        // Apply min/max constraints
        DWORD min_duration = g_min_sleep_duration_ms.load();
        DWORD max_duration = g_max_sleep_duration_ms.load();

        if (min_duration > 0) {
            modified_duration = (modified_duration > min_duration) ? modified_duration : min_duration;
        }
        if (max_duration > 0) {
            modified_duration = (modified_duration < max_duration) ? modified_duration : max_duration;
        }

        // Track modified calls
        g_hook_stats[HOOK_SleepEx].increment_unsuppressed();
        g_sleep_hook_stats.increment_modified();
        g_sleep_hook_stats.add_original_duration(dwMilliseconds);
        g_sleep_hook_stats.add_modified_duration(modified_duration);

        LogDebug("SleepEx hook: " + std::to_string(dwMilliseconds) + " ms -> " + std::to_string(modified_duration) + " ms (multiplier: " + std::to_string(multiplier) + ")");
    } else {
        // Track unmodified calls
        g_hook_stats[HOOK_SleepEx].increment_unsuppressed();
    }

    // Call original function with modified duration
    if (SleepEx_Original) {
        return SleepEx_Original(modified_duration, bAlertable);
    } else {
        return SleepEx(modified_duration, bAlertable);
    }
}

// Hooked WaitForSingleObject function
DWORD WINAPI WaitForSingleObject_Detour(HANDLE hHandle, DWORD dwMilliseconds) {
    // Track total calls
    g_hook_stats[HOOK_WaitForSingleObject].increment_total();

    DWORD modified_duration = dwMilliseconds;

    if (g_sleep_hook_enabled.load() && dwMilliseconds > 0 && dwMilliseconds != INFINITE) {
        // Apply sleep multiplier
        float multiplier = g_sleep_multiplier.load();
        if (multiplier > 0.0f) {
            modified_duration = static_cast<DWORD>(dwMilliseconds * multiplier);
        }

        // Apply min/max constraints
        DWORD min_duration = g_min_sleep_duration_ms.load();
        DWORD max_duration = g_max_sleep_duration_ms.load();

        if (min_duration > 0) {
            modified_duration = (modified_duration > min_duration) ? modified_duration : min_duration;
        }
        if (max_duration > 0) {
            modified_duration = (modified_duration < max_duration) ? modified_duration : max_duration;
        }

        // Track modified calls
        g_hook_stats[HOOK_WaitForSingleObject].increment_unsuppressed();
        g_sleep_hook_stats.increment_modified();
        g_sleep_hook_stats.add_original_duration(dwMilliseconds);
        g_sleep_hook_stats.add_modified_duration(modified_duration);

        LogDebug("WaitForSingleObject hook: " + std::to_string(dwMilliseconds) + " ms -> " + std::to_string(modified_duration) + " ms (multiplier: " + std::to_string(multiplier) + ")");
    } else {
        // Track unmodified calls
        g_hook_stats[HOOK_WaitForSingleObject].increment_unsuppressed();
    }

    // Call original function with modified duration
    if (WaitForSingleObject_Original) {
        return WaitForSingleObject_Original(hHandle, modified_duration);
    } else {
        return WaitForSingleObject(hHandle, modified_duration);
    }
}

// Hooked WaitForMultipleObjects function
DWORD WINAPI WaitForMultipleObjects_Detour(DWORD nCount, const HANDLE* lpHandles, BOOL bWaitAll, DWORD dwMilliseconds) {
    // Track total calls
    g_hook_stats[HOOK_WaitForMultipleObjects].increment_total();

    DWORD modified_duration = dwMilliseconds;

    if (g_sleep_hook_enabled.load() && dwMilliseconds > 0 && dwMilliseconds != INFINITE) {
        // Apply sleep multiplier
        float multiplier = g_sleep_multiplier.load();
        if (multiplier > 0.0f) {
            modified_duration = static_cast<DWORD>(dwMilliseconds * multiplier);
        }

        // Apply min/max constraints
        DWORD min_duration = g_min_sleep_duration_ms.load();
        DWORD max_duration = g_max_sleep_duration_ms.load();

        if (min_duration > 0) {
            modified_duration = (modified_duration > min_duration) ? modified_duration : min_duration;
        }
        if (max_duration > 0) {
            modified_duration = (modified_duration < max_duration) ? modified_duration : max_duration;
        }

        // Track modified calls
        g_hook_stats[HOOK_WaitForMultipleObjects].increment_unsuppressed();
        g_sleep_hook_stats.increment_modified();
        g_sleep_hook_stats.add_original_duration(dwMilliseconds);
        g_sleep_hook_stats.add_modified_duration(modified_duration);

        LogDebug("WaitForMultipleObjects hook: " + std::to_string(dwMilliseconds) + " ms -> " + std::to_string(modified_duration) + " ms (multiplier: " + std::to_string(multiplier) + ")");
    } else {
        // Track unmodified calls
        g_hook_stats[HOOK_WaitForMultipleObjects].increment_unsuppressed();
    }

    // Call original function with modified duration
    if (WaitForMultipleObjects_Original) {
        return WaitForMultipleObjects_Original(nCount, lpHandles, bWaitAll, modified_duration);
    } else {
        return WaitForMultipleObjects(nCount, lpHandles, bWaitAll, modified_duration);
    }
}

// Install sleep hooks
bool InstallSleepHooks() {
    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for sleep hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with sleep hooks");
    } else {
        LogInfo("MinHook initialized successfully for sleep hooks");
    }

    // Hook Sleep
    if (MH_CreateHook(Sleep, Sleep_Detour, (LPVOID*)&Sleep_Original) != MH_OK) {
        LogError("Failed to create Sleep hook");
        return false;
    }

    // Hook SleepEx
    if (MH_CreateHook(SleepEx, SleepEx_Detour, (LPVOID*)&SleepEx_Original) != MH_OK) {
        LogError("Failed to create SleepEx hook");
        return false;
    }

    // Hook WaitForSingleObject
    if (MH_CreateHook(WaitForSingleObject, WaitForSingleObject_Detour, (LPVOID*)&WaitForSingleObject_Original) != MH_OK) {
        LogError("Failed to create WaitForSingleObject hook");
        return false;
    }

    // Hook WaitForMultipleObjects
    if (MH_CreateHook(WaitForMultipleObjects, WaitForMultipleObjects_Detour, (LPVOID*)&WaitForMultipleObjects_Original) != MH_OK) {
        LogError("Failed to create WaitForMultipleObjects hook");
        return false;
    }

    // Enable all hooks
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        LogError("Failed to enable sleep hooks");
        return false;
    }

    LogInfo("Sleep hooks installed successfully");
    return true;
}

// Uninstall sleep hooks
void UninstallSleepHooks() {
    // Disable hooks
    MH_DisableHook(Sleep);
    MH_DisableHook(SleepEx);
    MH_DisableHook(WaitForSingleObject);
    MH_DisableHook(WaitForMultipleObjects);

    // Remove hooks
    MH_RemoveHook(Sleep);
    MH_RemoveHook(SleepEx);
    MH_RemoveHook(WaitForSingleObject);
    MH_RemoveHook(WaitForMultipleObjects);

    // Reset function pointers
    Sleep_Original = nullptr;
    SleepEx_Original = nullptr;
    WaitForSingleObject_Original = nullptr;
    WaitForMultipleObjects_Original = nullptr;

    LogInfo("Sleep hooks uninstalled");
}

} // namespace renodx::hooks
