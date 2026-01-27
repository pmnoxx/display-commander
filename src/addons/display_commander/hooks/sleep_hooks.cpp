#include "sleep_hooks.hpp"
#include <MinHook.h>
#include <windows.h>
#include "../globals.hpp"
#include "../settings/experimental_tab_settings.hpp"
#include "../utils.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "hook_suppression_manager.hpp"
#include "windows_hooks/windows_message_hooks.hpp"

// Removed unused headers

namespace display_commanderhooks {

// Constant to disable sleep hooks
const bool DISABLE_SLEEP_HOOKS = false;

// Thread ID caching removed

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

    if (settings::g_experimentalTabSettings.sleep_hook_enabled.GetValue() && dwMilliseconds > 0) {
        {
            // Apply sleep multiplier
            float multiplier = settings::g_experimentalTabSettings.sleep_multiplier.GetValue();
            if (multiplier > 0.0f) {
                modified_duration = static_cast<DWORD>(dwMilliseconds * multiplier);
            }

            // Apply min/max constraints
            DWORD min_duration =
                static_cast<DWORD>(settings::g_experimentalTabSettings.min_sleep_duration_ms.GetValue());
            DWORD max_duration =
                static_cast<DWORD>(settings::g_experimentalTabSettings.max_sleep_duration_ms.GetValue());

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

            LogDebug("[TID:%d] Sleep hook: %d ms -> %d ms (multiplier: %f)", GetCurrentThreadId(), dwMilliseconds,
                     modified_duration, multiplier);
        }
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

    if (settings::g_experimentalTabSettings.sleep_hook_enabled.GetValue() && dwMilliseconds > 0) {
        {
            // Apply sleep multiplier
            float multiplier = settings::g_experimentalTabSettings.sleep_multiplier.GetValue();
            if (multiplier > 0.0f) {
                modified_duration = static_cast<DWORD>(dwMilliseconds * multiplier);
            }

            // Apply min/max constraints
            DWORD min_duration =
                static_cast<DWORD>(settings::g_experimentalTabSettings.min_sleep_duration_ms.GetValue());
            DWORD max_duration =
                static_cast<DWORD>(settings::g_experimentalTabSettings.max_sleep_duration_ms.GetValue());

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

            LogDebug("[TID:%d] SleepEx hook: %d ms -> %d ms (multiplier: %f)", GetCurrentThreadId(), dwMilliseconds,
                     modified_duration, multiplier);
        }
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

    if (settings::g_experimentalTabSettings.sleep_hook_enabled.GetValue() && dwMilliseconds > 0
        && dwMilliseconds != INFINITE) {
        {
            // Apply sleep multiplier
            float multiplier = settings::g_experimentalTabSettings.sleep_multiplier.GetValue();
            if (multiplier > 0.0f) {
                modified_duration = static_cast<DWORD>(dwMilliseconds * multiplier);
            }

            // Apply min/max constraints
            DWORD min_duration =
                static_cast<DWORD>(settings::g_experimentalTabSettings.min_sleep_duration_ms.GetValue());
            DWORD max_duration =
                static_cast<DWORD>(settings::g_experimentalTabSettings.max_sleep_duration_ms.GetValue());

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

            LogDebug("[TID:%d] WaitForSingleObject hook: %d ms -> %d ms (multiplier: %f)", GetCurrentThreadId(),
                     dwMilliseconds, modified_duration, multiplier);
        }
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

    if (settings::g_experimentalTabSettings.sleep_hook_enabled.GetValue() && dwMilliseconds > 0
        && dwMilliseconds != INFINITE) {
        {
            // Apply sleep multiplier
            float multiplier = settings::g_experimentalTabSettings.sleep_multiplier.GetValue();
            if (multiplier > 0.0f) {
                modified_duration = static_cast<DWORD>(dwMilliseconds * multiplier);
            }

            // Apply min/max constraints
            DWORD min_duration =
                static_cast<DWORD>(settings::g_experimentalTabSettings.min_sleep_duration_ms.GetValue());
            DWORD max_duration =
                static_cast<DWORD>(settings::g_experimentalTabSettings.max_sleep_duration_ms.GetValue());

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

            LogDebug("[TID:%d] WaitForMultipleObjects hook: %d ms -> %d ms (multiplier: %f)", GetCurrentThreadId(),
                     dwMilliseconds, modified_duration, multiplier);
        }
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
    if (!enabled_experimental_features) {
        return true;
    }
    if (DISABLE_SLEEP_HOOKS) {
        LogInfo("Sleep hooks are disabled via DISABLE_SLEEP_HOOKS constant");
        return true;  // Return success but don't install hooks
    }

    // Check if Sleep hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::SLEEP)) {
        LogInfo("Sleep hooks installation suppressed by user setting");
        return false;
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::SLEEP);
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
    if (!CreateAndEnableHook(Sleep, Sleep_Detour, (LPVOID*)&Sleep_Original, "Sleep")) {
        LogError("Failed to create and enable Sleep hook");
        return false;
    }

    // Hook SleepEx
    if (!CreateAndEnableHook(SleepEx, SleepEx_Detour, (LPVOID*)&SleepEx_Original, "SleepEx")) {
        LogError("Failed to create and enable SleepEx hook");
        return false;
    }

    // Hook WaitForSingleObject
    if (!CreateAndEnableHook(WaitForSingleObject, WaitForSingleObject_Detour, (LPVOID*)&WaitForSingleObject_Original,
                             "WaitForSingleObject")) {
        LogError("Failed to create and enable WaitForSingleObject hook");
        return false;
    }

    // Hook WaitForMultipleObjects
    if (!CreateAndEnableHook(WaitForMultipleObjects, WaitForMultipleObjects_Detour,
                             (LPVOID*)&WaitForMultipleObjects_Original, "WaitForMultipleObjects")) {
        LogError("Failed to create and enable WaitForMultipleObjects hook");
        return false;
    }

    LogInfo("Sleep hooks installed successfully");

    // Mark Sleep hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::SLEEP);

    return true;
}

// Uninstall sleep hooks
void UninstallSleepHooks() {
    if (DISABLE_SLEEP_HOOKS) {
        LogInfo("Sleep hooks are disabled via DISABLE_SLEEP_HOOKS constant - nothing to uninstall");
        return;
    }

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

}  // namespace display_commanderhooks
